// Retro LCD ciferník (viz RetroLcd.h). Kreslení segmentů, rastr ikony počasí
// a segmentový ukazatel jsou převzaté z CooLajz/waveshare-hodiny (RetroLcd.cpp,
// licence MIT); zdejší verze kreslí podle rozvržení desky (RetroLcdLayout.h),
// píše segmenty rovnou do kreslicího bufferu a bere hodnoty ze slotů hodnot.

#include "RetroLcd.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <new>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "ClockConfig.h"
#include "ClockFonts.h"
#include "RetroLcdFormat.h"
#include "RetroLcdGeometry.h"
#include "RetroLcdLayout.h"

namespace {

const RetroLcdLayout &L = RETRO_LCD_LAYOUT;

#if HODINY_BOARD_LCD7
const lv_font_t *const NAME_FONT = &clock_czech_20;
const lv_font_t *const UNIT_FONT = &clock_unit_28;
#else
const lv_font_t *const NAME_FONT = &clock_czech_16;
const lv_font_t *const UNIT_FONT = &clock_unit_24;
#endif

lv_obj_t *face = nullptr;

// Stav ciferníku leží v PSRAM (alokuje ho retroLcdCreate): interní RAM je
// vzácná a ciferník bez objektu nic nekreslí.
struct ValueState {
  char name[CLOCK_METRIC_NAME_LENGTH];
  char unit[CLOCK_METRIC_SUFFIX_LENGTH];
  char number[16];
  bool extraMinus;
  bool weather;
};

// Ikona počasí převedená na mřížku bodů jako na skutečném LCD.
constexpr int WEATHER_GRID = 26;

struct RetroState {
  uint32_t backgroundColor = CLOCK_RETRO_DEFAULT_BACKGROUND;
  uint32_t foregroundColor = CLOCK_RETRO_DEFAULT_FOREGROUND;
  uint8_t ghostPercent = CLOCK_RETRO_DEFAULT_GHOST_PERCENT;
  bool night = false;
  uint8_t dateFormat = 0;
  bool fixedWeekday = false;
  bool showSeconds = true;
  bool leadingZero = true;
  bool english = false;
  tm clockTime = {};
  bool timeAvailable = false;
  ValueState values[2] = {};
  uint8_t weatherMatrix[WEATHER_GRID * WEATHER_GRID / 8 + 1] = {};
  const lv_img_dsc_t *rasterizedIcon = nullptr;
  bool weatherKnown = false;
  bool barEnabled = false;
  float barPercent = NAN;
  uint8_t barSegments = CLOCK_RETRO_DEFAULT_SEGMENTS;
  char barText[CLOCK_METRIC_NAME_LENGTH + CLOCK_METRIC_SUFFIX_LENGTH + 8] = {};
  bool statusWifi = false, statusWeb = false, statusHa = false,
       statusShowHa = false, statusLightning = false;
  uint8_t dotCount = 0, dotActive = 0;
};
RetroState *state = nullptr;

lv_color_t ink() { return lv_color_hex(state->night ? 0xC34436 : state->foregroundColor); }
lv_color_t background() {
  return lv_color_hex(state->night ? 0x080A08 : state->backgroundColor);
}
// Stejně jemný kontrast duchů pro světlé i tmavé pozadí; drobné znaky dostanou
// ještě slabší, aby text nesplýval.
lv_color_t ghost(bool subtle = false) {
  const unsigned opacity = (state->ghostPercent * 255U + 50U) / 100U;
  return lv_color_mix(ink(), background(),
                      subtle ? (opacity * 5U + 7U) / 14U : opacity);
}

void invalidateBox(int x, int y, int w, int h) {
  if (face == nullptr || w <= 0 || h <= 0) return;
  lv_area_t area;
  lv_obj_get_coords(face, &area);
  const lv_coord_t left = area.x1;
  const lv_coord_t top = area.y1;
  area.x1 = left + x;
  area.y1 = top + y;
  area.x2 = area.x1 + w - 1;
  area.y2 = area.y1 + h - 1;
  lv_obj_invalidate_area(face, &area);
}

void invalidateAll() {
  if (face != nullptr) lv_obj_invalidate(face);
}

void rasterizeWeather(const lv_img_dsc_t *icon) {
  std::memset(state->weatherMatrix, 0, sizeof(state->weatherMatrix));
  if (icon == nullptr || icon->header.w == 0 || icon->header.h == 0) return;
  const int count = WEATHER_GRID;
  // Přesné pokrytí plochy: okrajové pixely zdroje přispějí poměrně. Hranice
  // drží celá čísla v jednotkách 1/denominator zdrojového pixelu, aby
  // zaokrouhlování po buňkách nikam nestáčelo.
  const int denominator = 32 * count;
  for (int y = 0; y < count; ++y) {
    const int top = (3 * count + y * 26) * icon->header.h;
    const int bottom = (3 * count + (y + 1) * 26) * icon->header.h;
    for (int x = 0; x < count; ++x) {
      const int left = (3 * count + x * 26) * icon->header.w;
      const int right = (3 * count + (x + 1) * 26) * icon->header.w;
      uint64_t alpha = 0;
      for (int sy = top / denominator;
           sy < (bottom + denominator - 1) / denominator; ++sy) {
        const int overlapY = std::min(bottom, (sy + 1) * denominator) -
                             std::max(top, sy * denominator);
        for (int sx = left / denominator;
             sx < (right + denominator - 1) / denominator; ++sx) {
          const int overlapX = std::min(right, (sx + 1) * denominator) -
                               std::max(left, sx * denominator);
          alpha += static_cast<uint64_t>(lv_img_buf_get_px_alpha(icon, sx, sy)) *
                   overlapX * overlapY;
        }
      }
      const uint64_t area = static_cast<uint64_t>(right - left) * (bottom - top);
      if (alpha >= area * 64) {
        const int bit = y * count + x;
        state->weatherMatrix[bit / 8] |= 1U << (bit % 8);
      }
    }
  }
}

bool dateSeparator(int position) {
  return state->dateFormat % 7 >= 5 ? (position == 4 || position == 7)
                             : (position == 2 || position == 5);
}

int dateSeparatorA() { return state->dateFormat % 7 >= 5 ? 4 : 2; }
int dateSeparatorB() { return state->dateFormat % 7 >= 5 ? 7 : 5; }

struct Painter {
  lv_draw_ctx_t *ctx;
  int ox, oy;

  // Vodorovný úsek rovnou do kreslicího bufferu. Segmenty jsou neprůhledné
  // (hrany se míchají s barvou pozadí LCD), takže stačí přepsat pixely.
  void span(int x, int y, int length, lv_color_t color) {
    const lv_area_t *clip = ctx->clip_area;
    const int sy = oy + y;
    if (sy < clip->y1 || sy > clip->y2) return;
    int x1 = std::max<int>(ox + x, clip->x1);
    const int x2 = std::min<int>(ox + x + length - 1, clip->x2);
    if (x1 > x2) return;
    const lv_coord_t stride = lv_area_get_width(ctx->buf_area);
    lv_color_t *row = static_cast<lv_color_t *>(ctx->buf) +
                      (sy - ctx->buf_area->y1) * stride - ctx->buf_area->x1;
    for (; x1 <= x2; ++x1) row[x1] = color;
  }

  void rect(int x, int y, int w, int h, lv_color_t color) {
    for (int row = 0; row < h; ++row) span(x, y + row, w, color);
  }

  bool visible(int x, int y, int w, int h) const {
    const lv_area_t *clip = ctx->clip_area;
    return ox + x <= clip->x2 && ox + x + w - 1 >= clip->x1 &&
           oy + y <= clip->y2 && oy + y + h - 1 >= clip->y1;
  }

  void label(const char *text, int x, int y, int w, const lv_font_t *font,
             lv_color_t color, lv_text_align_t align = LV_TEXT_ALIGN_CENTER) {
    if (text == nullptr || text[0] == '\0') return;
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = font;
    d.color = color;
    d.align = align;
    d.flag = LV_TEXT_FLAG_EXPAND;
    lv_area_t area = {static_cast<lv_coord_t>(ox + x), static_cast<lv_coord_t>(oy + y),
                      static_cast<lv_coord_t>(ox + x + w - 1),
                      static_cast<lv_coord_t>(oy + y + font->line_height - 1)};
    // Jeden řádek; dlouhé vlastní názvy se oříznou na šířku buňky.
    lv_area_t clip;
    if (!_lv_area_intersect(&clip, ctx->clip_area, &area)) return;
    const lv_area_t *old = ctx->clip_area;
    ctx->clip_area = &clip;
    lv_draw_label(ctx, &d, &area, text, nullptr);
    ctx->clip_area = old;
  }

  void segments(int x, int y, int w, int h, bool alphabet, uint16_t mask,
                bool solid) {
    if (!visible(x, y, w, h)) return;
    // Na plných malých znacích nesmí neaktivní diagonály přepsat rozsvícené
    // středové tahy, proto se aktivní kreslí až ve druhém průchodu.
    for (int pass = 0; pass < (solid ? 2 : 1); ++pass) {
      for (int i = 0; i < (alphabet ? 14 : 7); ++i) {
        const bool active = mask & (1U << i);
        if (solid && active != (pass == 1)) continue;
        const lv_color_t color = active ? ink() : ghost(alphabet || h <= 32);
        const lv_color_t back = background();
        retro_lcd_geometry::segment(
            w, h, alphabet, i,
            [&](int sx, int sy, int length, uint8_t coverage) {
              span(x + sx, y + sy, length, lv_color_mix(color, back, coverage));
            },
            solid);
      }
    }
  }

  void digit(char c, int x, int y, int w, int h, bool alphabet = false,
             bool solid = false) {
    static const uint8_t DIGITS[] = {0x3F, 0x06, 0x5B, 0x4F, 0x66,
                                     0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    uint16_t mask = c >= '0' && c <= '9' ? DIGITS[c - '0'] : c == '-' ? 0x40 : 0;
    // A B C D E F G1 G2 H I J K L M; diagonály a středové svislice.
    if (alphabet) {
      switch (c) {
        case '-': mask = 0xC0; break;
        case 'A': mask = 0xF7; break;
        case 'B': mask = 0x128F; break;
        case 'C': mask = 0x39; break;
        case 'D': mask = 0x120F; break;
        case 'E': mask = 0xF9; break;
        case 'F': mask = 0xF1; break;
        case 'G': mask = 0xBD; break;
        case 'H': mask = 0xF6; break;
        case 'I': mask = 0x1209; break;
        case 'K': mask = 0xC30; break;
        case 'L': mask = 0x38; break;
        case 'M': mask = 0x536; break;
        case 'N': mask = 0x936; break;
        case 'O': mask = 0x3F; break;
        case 'P': mask = 0xF3; break;
        case 'R': mask = 0x8F3; break;
        case 'S': mask = 0xED; break;
        case 'T': mask = 0x1201; break;
        case 'U': mask = 0x3E; break;
        case 'V': mask = 0x2430; break;
        case 'W': mask = 0x2836; break;
        case 'Y': mask = 0x1500; break;
        default: break;
      }
    }
    segments(x, y, w, h, alphabet, mask, solid);
  }

  int width(const char *text, int w, int gap, bool dateSymbols = false) const {
    return retroLcdTextWidth(text, w, gap, dateSymbols, dateSeparatorA(),
                             dateSeparatorB());
  }

  void text(const char *value, int x, int y, int w, int h, int gap,
            bool alphabet = false, bool solid = false,
            bool dateSymbols = false) {
    if (!visible(x, y, width(value, w, gap, dateSymbols) + 1, h + 1)) return;
    for (const char *c = value; *c; ++c) {
      int cw = w;
      if (dateSymbols && dateSeparator(c - value) && (*c == '-' || *c == '/')) {
        cw = std::max(5, w / 2);
        if (*c == '-') {
          rect(x, y + h / 2, cw, 2, ink());
        } else {
          const int length = h / 2;
          for (int row = 0; row < length; ++row)
            rect(x + cw - 2 - row * (cw - 2) / (length - 1), y + h / 4 + row, 2,
                 1, ink());
        }
      } else if (*c == '.' || *c == ':') {
        cw = std::max(4, w / 3);
        const int s = std::max(3, h / 12);
        if (*c == ':') {
          rect(x + (cw - s) / 2, y + h / 3 - s / 2, s, s, ink());
          rect(x + (cw - s) / 2, y + 2 * h / 3 - s / 2, s, s, ink());
        } else {
          rect(x + (cw - s) / 2, y + h - s, s, s, ink());
        }
      } else {
        digit(*c, x, y, w, h, alphabet, solid);
      }
      x += cw + gap;
    }
  }

  void weatherGrid(int centerX, int centerY, int pitch) {
    const int pixel = std::max(1, pitch - 1);
    const int extent = WEATHER_GRID * pitch - 1;
    const int left = centerX - extent / 2;
    const int top = centerY - extent / 2;
    if (!visible(left, top, extent, extent)) return;
    for (int row = 0; row < WEATHER_GRID; ++row) {
      for (int column = 0; column < WEATHER_GRID; ++column) {
        const int bit = row * WEATHER_GRID + column;
        const bool active = state->weatherKnown && (state->weatherMatrix[bit / 8] & (1U << (bit % 8)));
        rect(left + column * pitch, top + row * pitch, pixel, pixel,
             active ? ink() : ghost());
      }
    }
  }
};

// Šířka času "HH:MM" (vždy pět znaků, pozice pro hodinu je i bez nuly).
int timeWidth() {
  return retroLcdTextWidth("88:88", L.time.w, L.time.gap);
}

int timeLeft() {
  if (state->showSeconds || L.timeCenterX == 0) return L.time.x;
  return L.timeCenterX - timeWidth() / 2;
}

void drawValue(Painter &p, int index) {
  const ValueState &v = state->values[index];
  const int center = L.valueCenterX[index];
  if (v.weather) {
    const int pitch = 4;
    p.weatherGrid(center, L.valueWeatherTop + L.valueWeatherHeight / 2, pitch);
    return;
  }
  p.label(v.name, center - L.valueWidth / 2, L.valueNameY, L.valueWidth,
          NAME_FONT, ink());
  int w = L.valueDigitW;
  int h = L.valueDigitH;
  int gap = L.valueGap;
  lv_point_t unitSize = {0, 0};
  if (v.unit[0] != '\0')
    lv_txt_get_size(&unitSize, v.unit, UNIT_FONT, 0, 0, 200, LV_TEXT_FLAG_EXPAND);
  const int unitWidth = std::min<int>(L.valueWidth / 2, unitSize.x);
  const int limit = L.valueWidth;
  // Číslo se zmenšuje, dokud se i s místem pro mínus a jednotkou nevejde.
  while (w > 10 && p.width(v.number, w, gap) + w + 4 + unitWidth + 6 > limit) {
    --w;
    h -= 2;
  }
  while (gap > 1 && p.width(v.number, w, gap) + w + 4 + unitWidth + 6 > limit)
    --gap;
  const int numberWidth = p.width(v.number, w, gap);
  const int signWidth = w;
  const int left =
      center - (signWidth + 4 + numberWidth + unitWidth + (unitWidth ? 6 : 0)) / 2;
  const int numberLeft = left + signWidth + 4;
  const int top = L.valueBottom - h;
  if (v.extraMinus) {
    const lv_color_t color = ink();
    const lv_color_t back = background();
    retro_lcd_geometry::segment(
        signWidth, h, false, 6, [&](int sx, int sy, int length, uint8_t coverage) {
          p.span(left + sx, top + sy, length, lv_color_mix(color, back, coverage));
        });
  }
  p.text(v.number, numberLeft, top, w, h, gap);
  if (unitWidth)
    p.label(v.unit, numberLeft + numberWidth + 6, L.valueUnitY, unitWidth,
            UNIT_FONT, ink(), LV_TEXT_ALIGN_LEFT);
}

void drawBar(Painter &p) {
  if (!state->barEnabled) return;
  const int count = state->barSegments;
  const int filled = std::isfinite(state->barPercent)
                         ? static_cast<int>(std::round(state->barPercent * count / 100.0f))
                         : 0;
  const int gap = std::max(1, std::min(5, 50 / count));
  // Celé pixely: všechny segmenty i mezery stejně široké, zbytek na střed.
  const int segmentWidth = (L.bar.w - gap * (count - 1)) / count;
  const int totalWidth = segmentWidth * count + gap * (count - 1);
  const int left = L.bar.x + (L.bar.w - totalWidth) / 2;
  for (int i = 0; i < count; ++i)
    p.rect(left + i * (segmentWidth + gap), L.bar.y, segmentWidth, L.bar.h,
           i < filled ? ink() : ghost());
  p.label(state->barText, L.barLabel.x, L.barLabel.y, L.barLabel.w, NAME_FONT, ink());
}

void drawStatus(Painter &p) {
  const char *symbols[4] = {};
  bool lit[4] = {};
  int count = 0;
  if (state->statusLightning) {
    symbols[count] = LV_SYMBOL_CHARGE;
    lit[count++] = true;
  }
  symbols[count] = LV_SYMBOL_WIFI;
  lit[count++] = state->statusWifi;
  if (state->statusShowHa) {
    symbols[count] = LV_SYMBOL_HOME;
    lit[count++] = state->statusHa;
  }
  if (state->statusWeb) {
    symbols[count] = LV_SYMBOL_SETTINGS;
    lit[count++] = true;
  }
  constexpr int SPACING = 36;
  int x = L.statusCenterX - ((count - 1) * SPACING + 24) / 2;
  for (int i = 0; i < count; ++i, x += SPACING)
    p.label(symbols[i], x, L.statusY, 24, &lv_font_montserrat_16,
            lit[i] ? ink() : ghost());
}

void drawDots(Painter &p) {
  if (L.dotsRight == 0 || state->dotCount < 2) return;
  constexpr int GAP = 18;
  const int first = L.dotsRight - (state->dotCount - 1) * GAP;
  for (int i = 0; i < state->dotCount; ++i) {
    const int size = i == state->dotActive ? 10 : 7;
    p.rect(first + i * GAP - size / 2, L.dotsY - size / 2, size, size,
           i == state->dotActive ? ink() : ghost());
  }
}

void draw(lv_event_t *event) {
  lv_area_t area;
  lv_obj_get_coords(face, &area);
  Painter p{lv_event_get_draw_ctx(event), area.x1, area.y1};

  char day[12];
  retroLcdFormatWeekday(day, sizeof(day), state->timeAvailable ? state->clockTime.tm_wday : -1,
                        state->english, state->fixedWeekday);
  const RetroLcdCell &wd = L.weekday;
  p.text(day, wd.x - p.width(day, wd.w, wd.gap) / 2, wd.y, wd.w, wd.h, wd.gap,
         true, true);

  char date[16];
  retroLcdFormatDate(date, sizeof(date), state->clockTime.tm_mday, state->clockTime.tm_mon + 1,
                     state->clockTime.tm_year + 1900, state->dateFormat, state->timeAvailable);
  const RetroLcdCell &dt = L.date;
  p.text(date, dt.x - p.width(date, dt.w, dt.gap, true) / 2, dt.y, dt.w, dt.h,
         dt.gap, false, true, true);

  char time[8] = "--:--";
  char seconds[4] = "--";
  if (state->timeAvailable) {
    snprintf(time, sizeof(time), state->leadingZero ? "%02d:%02d" : "%2d:%02d",
             state->clockTime.tm_hour, state->clockTime.tm_min);
    snprintf(seconds, sizeof(seconds), "%02d", state->clockTime.tm_sec);
  }
  p.text(time, timeLeft(), L.time.y, L.time.w, L.time.h, L.time.gap);
  if (state->showSeconds)
    p.text(seconds, L.seconds.x, L.seconds.y, L.seconds.w, L.seconds.h,
           L.seconds.gap);
  if (L.weather.w > 0)
    p.weatherGrid(L.weather.x + L.weather.w / 2, L.weather.y + L.weather.h / 2, 4);

  for (const RetroLcdBox &rule : L.rules)
    if (rule.w > 0) p.rect(rule.x, rule.y, rule.w, rule.h, ink());
  for (const RetroLcdBox &divider : L.dividers)
    if (divider.w > 0) p.rect(divider.x, divider.y, divider.w, divider.h, ink());

  drawValue(p, 0);
  drawValue(p, 1);
  drawBar(p);
  drawStatus(p);
  drawDots(p);
}

// Oblasti k překreslení podle rozvržení.
void invalidateTopRows() {
  const int top = std::min(L.weekday.y, L.date.y) - 4;
  const int bottom = std::max(L.weekday.y + L.weekday.h, L.date.y + L.date.h) + 4;
  if (L.round) {
    invalidateBox(40, L.weekday.y - 4, L.width - 80, L.date.y + L.date.h + 8 - L.weekday.y);
  } else {
    invalidateBox(0, top, L.width, bottom - top);
  }
}

void invalidateTime() {
  const int left = std::min<int>(L.time.x, L.timeCenterX ? L.timeCenterX - timeWidth() / 2 : L.time.x);
  const int right = std::max<int>(L.time.x, L.timeCenterX ? L.timeCenterX - timeWidth() / 2 : L.time.x) + timeWidth();
  invalidateBox(left - 4, L.time.y - 4, right - left + 8, L.time.h + 8);
}

void invalidateSeconds() {
  invalidateBox(L.seconds.x - 4, L.seconds.y - 4,
                retroLcdTextWidth("88", L.seconds.w, L.seconds.gap) + 8,
                L.seconds.h + 8);
}

void invalidateValue(int index) {
  const int top = std::min<int>(L.valueNameY, L.valueWeatherTop) - 2;
  const int bottom = std::max<int>(L.valueBottom, L.valueWeatherTop + L.valueWeatherHeight) + 4;
  invalidateBox(L.valueCenterX[index] - L.valueWidth / 2 - 4, top,
                L.valueWidth + 8, bottom - top);
}

void invalidateBar() {
  const int top = std::min(L.bar.y, L.barLabel.y) - 2;
  const int bottom = std::max(L.bar.y + L.bar.h, L.barLabel.y + L.barLabel.h + 6) + 2;
  const int left = std::min(L.bar.x, L.barLabel.x) - 2;
  const int right = std::max(L.bar.x + L.bar.w, L.barLabel.x + L.barLabel.w) + 2;
  invalidateBox(left, top, right - left, bottom - top);
}

void invalidateStatus() {
  invalidateBox(L.statusCenterX - 90, L.statusY - 2, 180, 24);
}

void invalidateWeather() {
  if (L.weather.w > 0) invalidateBox(L.weather.x - 8, L.weather.y - 8,
                                     L.weather.w + 16, L.weather.h + 16);
  for (int i = 0; i < 2; ++i)
    if (state->values[i].weather) invalidateValue(i);
}

}  // namespace

lv_obj_t *retroLcdCreate(lv_obj_t *parent, lv_coord_t x, lv_coord_t y) {
  if (face != nullptr) return face;
  void *memory = heap_caps_malloc(sizeof(RetroState), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (memory == nullptr) memory = malloc(sizeof(RetroState));
  if (memory == nullptr) return nullptr;
  state = new (memory) RetroState();
  face = lv_obj_create(parent);
  lv_obj_remove_style_all(face);
  lv_obj_set_size(face, L.width, L.height);
  lv_obj_set_pos(face, x, y);
  // Kulaté pozadí stačí: segmenty se píšou přímo do bufferu (bez masek)
  // a rozvržení kulatého leží celé uvnitř kruhu.
  if (L.round) lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(face, background(), 0);
  lv_obj_set_style_bg_opa(face, LV_OPA_COVER, 0);
  lv_obj_clear_flag(face, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(face, draw, LV_EVENT_DRAW_MAIN, nullptr);
  lv_obj_add_flag(face, LV_OBJ_FLAG_HIDDEN);
  return face;
}

lv_obj_t *retroLcdObject() { return face; }

void retroLcdSetColors(uint32_t bg, uint32_t fg, uint8_t ghostValue,
                       bool redNight) {
  if (state == nullptr) return;
  bg &= 0xFFFFFF;
  fg &= 0xFFFFFF;
  ghostValue = std::min<uint8_t>(ghostValue, CLOCK_RETRO_MAX_GHOST_PERCENT);
  if (bg == state->backgroundColor && fg == state->foregroundColor &&
      ghostValue == state->ghostPercent && redNight == state->night)
    return;
  state->backgroundColor = bg;
  state->foregroundColor = fg;
  state->ghostPercent = ghostValue;
  state->night = redNight;
  if (face != nullptr) lv_obj_set_style_bg_color(face, background(), 0);
  invalidateAll();
}

void retroLcdSetOptions(uint8_t format, bool fixed, bool seconds,
                        bool leadingHourZero, bool useEnglish) {
  if (state == nullptr) return;
  if (format >= CLOCK_RETRO_DATE_FORMAT_COUNT) format = 0;
  if (format == state->dateFormat && fixed == state->fixedWeekday && seconds == state->showSeconds &&
      leadingHourZero == state->leadingZero && useEnglish == state->english)
    return;
  state->dateFormat = format;
  state->fixedWeekday = fixed;
  state->showSeconds = seconds;
  state->leadingZero = leadingHourZero;
  state->english = useEnglish;
  invalidateAll();
}

void retroLcdSetTime(const tm &value, bool valid) {
  if (state == nullptr) return;
  if (!valid) {
    if (state->timeAvailable) {
      state->timeAvailable = false;
      invalidateAll();
    }
    return;
  }
  const bool wasAvailable = state->timeAvailable;
  if (!wasAvailable || value.tm_yday != state->clockTime.tm_yday ||
      value.tm_year != state->clockTime.tm_year)
    invalidateTopRows();
  if (!wasAvailable || value.tm_hour != state->clockTime.tm_hour ||
      value.tm_min != state->clockTime.tm_min)
    invalidateTime();
  if (state->showSeconds && (!wasAvailable || value.tm_sec != state->clockTime.tm_sec))
    invalidateSeconds();
  state->clockTime = value;
  state->timeAvailable = true;
}

void retroLcdSetValue(uint8_t index, const RetroLcdValue &value) {
  if (state == nullptr) return;
  if (index > 1) return;
  ValueState next = {};
  strlcpy(next.name, value.name ? value.name : "", sizeof(next.name));
  strlcpy(next.unit, value.unit ? value.unit : "", sizeof(next.unit));
  next.weather = value.weather;
  if (!value.weather)
    retroLcdFormatValue(next.number, sizeof(next.number), value.value,
                        value.digits, value.decimals, &next.extraMinus);
  ValueState &current = state->values[index];
  if (current.weather == next.weather && current.extraMinus == next.extraMinus &&
      strcmp(current.name, next.name) == 0 &&
      strcmp(current.unit, next.unit) == 0 &&
      strcmp(current.number, next.number) == 0)
    return;
  current = next;
  invalidateValue(index);
}

void retroLcdSetWeatherIcon(const lv_img_dsc_t *icon) {
  if (state == nullptr) return;
  const bool known = icon != nullptr;
  if (icon == state->rasterizedIcon && known == state->weatherKnown) return;
  rasterizeWeather(icon);
  state->rasterizedIcon = icon;
  state->weatherKnown = known;
  invalidateWeather();
}

void retroLcdSetBar(const char *label, float value, float minimum,
                    float maximum, uint8_t segments) {
  if (state == nullptr) return;
  segments = std::max<uint8_t>(CLOCK_RETRO_MIN_SEGMENTS,
                               std::min<uint8_t>(CLOCK_RETRO_MAX_SEGMENTS, segments));
  const bool enabled = label != nullptr;
  const float next = enabled ? retroLcdProgressPercent(value, minimum, maximum) : NAN;
  char text[sizeof(state->barText)] = {};
  if (enabled)
    snprintf(text, sizeof(text), "%s%s", label, std::isfinite(value) ? "" : "  --");
  const bool same =
      enabled == state->barEnabled && segments == state->barSegments && strcmp(text, state->barText) == 0 &&
      std::isfinite(next) == std::isfinite(state->barPercent) &&
      (!std::isfinite(next) || next == state->barPercent);
  if (same) return;
  state->barEnabled = enabled;
  state->barSegments = segments;
  state->barPercent = next;
  strlcpy(state->barText, text, sizeof(state->barText));
  invalidateBar();
}

void retroLcdSetStatus(bool wifi, bool web, bool homeAssistant, bool showHa,
                       bool lightning) {
  if (state == nullptr) return;
  if (wifi == state->statusWifi && web == state->statusWeb && homeAssistant == state->statusHa &&
      showHa == state->statusShowHa && lightning == state->statusLightning)
    return;
  state->statusWifi = wifi;
  state->statusWeb = web;
  state->statusHa = homeAssistant;
  state->statusShowHa = showHa;
  state->statusLightning = lightning;
  invalidateStatus();
}

void retroLcdSetDots(uint8_t count, uint8_t active) {
  if (state == nullptr) return;
  if (L.dotsRight == 0 || (count == state->dotCount && active == state->dotActive)) return;
  state->dotCount = std::min<uint8_t>(count, 16);
  state->dotActive = active;
  invalidateBox(L.dotsRight - 16 * 18 - 8, L.dotsY - 8, 16 * 18 + 16, 16);
}
