// Domovská obrazovka 7" displeje (800 x 480): digitální hodiny, analogový
// ciferník s pravým sloupcem a dlaždice hodnot podle návrhu "Hodiny 7" design".
//
// Soubor vkládá jen ClockDashboard.cpp na svém konci (nepřekládá se sám), aby
// obrazovka četla stav ciferníku - nastavené názvy, desetinná místa, barevné
// škály, noční režim, aktivní obrazovku - bez dalšího rozhraní. Kulatý
// ciferník dál řídí střídání obrazovek, gesta i nastavení; tady se jen kreslí.
// Ostatní obrazovky zatím běží na jevišti 480 x 480 vpravo.

#pragma once

#include "Board.h"
#if HODINY_BOARD_LCD7

#include "Lcd7Fonts.h"
#include "OpenWeatherIcons.h"

namespace lcd7 {

bool created = false;

enum class HomeMode : uint8_t { None, Digital, Analog, Values, Forecast, Radar };

constexpr int SIDE_X = 480;
constexpr int SIDE_WIDTH = SCREEN_WIDTH - SIDE_X;
constexpr int HOME_DOT_GAP = 18;
// Dlaždice vyplní plochu pod lištou podle počtu zapnutých hodnot: do čtyř
// 2 x 2, do šesti 3 x 2, jinak 3 x 3 (celá stránka hodnot).
constexpr int TILE_GAP = 11;
constexpr int TILE_LEFT = 14;
constexpr int TILE_TOP = 52;
constexpr int TILE_BOTTOM = SCREEN_HEIGHT - 14;
constexpr int TILE_PAD = 14;
static_assert(3 * 3 == CLOCK_VALUE_PAGE_SLOT_COUNT,
              "dlaždice musí pokrýt jednu stránku hodnot");
constexpr uint32_t SYNC_INTERVAL_MS = 100;

const lv_color_t TILE_BACKGROUND = LV_COLOR_MAKE(11, 11, 11);
const lv_color_t TILE_BORDER = LV_COLOR_MAKE(34, 34, 34);
const lv_color_t LINE = LV_COLOR_MAKE(31, 31, 31);
const lv_color_t DOT_OFF = LV_COLOR_MAKE(74, 74, 74);
// Červený noční vzhled: hlavní text v barvě COLOR_ERROR jako na kulatém
// ciferníku, vedlejší a čáry tlumeněji, aby noc nesvítila víc než musí.
const lv_color_t NIGHT_MUTED = LV_COLOR_MAKE(150, 38, 34);
const lv_color_t NIGHT_LINE = LV_COLOR_MAKE(58, 12, 10);
const lv_color_t NIGHT_DOT_OFF = LV_COLOR_MAKE(70, 14, 12);

struct Palette {
  bool night;
  lv_color_t text;
  lv_color_t muted;
  lv_color_t line;
  lv_color_t dotOff;
};

Palette palette() {
  if (redNightVisualEnabled())
    return {true, COLOR_ERROR, NIGHT_MUTED, NIGHT_LINE, NIGHT_DOT_OFF};
  return {false, COLOR_TEXT, COLOR_MUTED, LINE, DOT_OFF};
}

// Hodnota barvy podle škály, v noci jednotně červeně.
lv_color_t scaled(const Palette &p, float value,
                  const ClockMetricColorScale &scale) {
  return p.night ? COLOR_ERROR : metricColorForValue(value, scale);
}

// --- Pomocníci: měnit jen to, co se opravdu změnilo -----------------------
// LVGL překresluje oblast při každém nastavení textu nebo stylu, i když je
// stejné; synchronizace běží desetkrát za sekundu, takže by kreslila pořád.

void setText(lv_obj_t *label, const char *text) {
  if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

void setColor(lv_obj_t *object, lv_color_t color) {
  if (lv_color_to32(lv_obj_get_style_text_color(object, LV_PART_MAIN)) !=
      lv_color_to32(color))
    lv_obj_set_style_text_color(object, color, LV_PART_MAIN);
}

void setBackground(lv_obj_t *object, lv_color_t color) {
  if (lv_color_to32(lv_obj_get_style_bg_color(object, LV_PART_MAIN)) !=
      lv_color_to32(color))
    lv_obj_set_style_bg_color(object, color, LV_PART_MAIN);
}

void setBorder(lv_obj_t *object, lv_color_t color) {
  if (lv_color_to32(lv_obj_get_style_border_color(object, LV_PART_MAIN)) !=
      lv_color_to32(color))
    lv_obj_set_style_border_color(object, color, LV_PART_MAIN);
}

void setVisible(lv_obj_t *object, bool visible) {
  if (object == nullptr) return;
  if (visible == lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) {
    if (visible)
      lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
  }
}

void setPosition(lv_obj_t *object, lv_coord_t x, lv_coord_t y) {
  if (lv_obj_get_x(object) != x || lv_obj_get_y(object) != y)
    lv_obj_set_pos(object, x, y);
}

lv_obj_t *panel(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w,
                lv_coord_t h) {
  lv_obj_t *object = lv_obj_create(parent);
  lv_obj_set_pos(object, x, y);
  lv_obj_set_size(object, w, h);
  lv_obj_set_style_bg_color(object, COLOR_BACKGROUND, 0);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(object, 0, 0);
  lv_obj_set_style_radius(object, 0, 0);
  lv_obj_set_style_pad_all(object, 0, 0);
  lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE);
  return object;
}

lv_obj_t *line(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w,
               lv_coord_t h) {
  lv_obj_t *object = panel(parent, x, y, w, h);
  lv_obj_set_style_bg_color(object, LINE, 0);
  return object;
}

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_coord_t x,
                lv_coord_t y, lv_coord_t width = 0) {
  lv_obj_t *object = lv_label_create(parent);
  lv_obj_set_style_text_font(object, font, 0);
  lv_obj_set_style_text_color(object, COLOR_TEXT, 0);
  lv_label_set_text(object, "");
  lv_obj_set_pos(object, x, y);
  if (width > 0) {
    lv_obj_set_width(object, width);
    lv_label_set_long_mode(object, LV_LABEL_LONG_DOT);
  }
  return object;
}

// Desetinná tečka z formatMetricValue po česku jako čárka.
void formatValue(char *buffer, size_t capacity, float value, uint8_t decimals) {
  formatMetricValue(buffer, capacity, value, decimals);
  for (char *c = buffer; *c != '\0'; ++c)
    if (*c == '.') *c = ',';
}

// Jména ze svátkové tabulky jsou velkými písmeny ("ZUZANA"); návrh 7" je
// píše jako jméno ("Zuzana"). Malá písmena pro ASCII a české znaky s diakritikou
// (Latin-1 a Latin Extended-A, kde velké a malé písmeno leží vedle sebe).
void capitalizeName(const char *source, char *output, size_t capacity) {
  size_t out = 0;
  bool wordStart = true;
  for (size_t i = 0; source[i] != '\0' && out + 3 < capacity;) {
    const uint8_t c = static_cast<uint8_t>(source[i]);
    if (c < 0x80) {
      char ch = static_cast<char>(c);
      if (!wordStart && ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + 32);
      wordStart = !((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'));
      output[out++] = ch;
      ++i;
      continue;
    }
    if ((c & 0xE0) == 0xC0 && source[i + 1] != '\0') {
      uint16_t cp = ((c & 0x1F) << 6) | (static_cast<uint8_t>(source[i + 1]) & 0x3F);
      if (!wordStart) {
        if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) {
          cp += 0x20;
        } else if (((cp >= 0x100 && cp <= 0x137) ||
                    (cp >= 0x14A && cp <= 0x177)) &&
                   cp % 2 == 0) {
          cp += 1;
        } else if (((cp >= 0x139 && cp <= 0x148) ||
                    (cp >= 0x179 && cp <= 0x17E)) &&
                   cp % 2 == 1) {
          cp += 1;
        }
      }
      output[out++] = static_cast<char>(0xC0 | (cp >> 6));
      output[out++] = static_cast<char>(0x80 | (cp & 0x3F));
      wordStart = false;
      i += 2;
      continue;
    }
    output[out++] = source[i++];
    wordStart = false;
  }
  output[out] = '\0';
}

// --- Společné části -------------------------------------------------------

struct ValueView {
  lv_obj_t *title;
  lv_obj_t *value;
  lv_obj_t *unit;
};

ValueView makeValue(lv_obj_t *parent, const lv_font_t *titleFont,
                    const lv_font_t *valueFont, const lv_font_t *unitFont,
                    lv_coord_t x, lv_coord_t y, lv_coord_t titleGap) {
  ValueView view;
  view.title = label(parent, titleFont, x, y);
  lv_obj_set_style_text_letter_space(view.title, 2, 0);
  view.value = label(parent, valueFont, x, y + titleGap);
  view.unit = label(parent, unitFont, x, y + titleGap);
  return view;
}

// Jednotka sedí vpravo nahoře u čísla (°C, %), ostatní vpravo dole (ppm).
void placeUnit(const ValueView &view, bool top) {
  lv_obj_update_layout(view.value);
  const lv_coord_t x = lv_obj_get_x(view.value) + lv_obj_get_width(view.value) + 5;
  const lv_coord_t y =
      top ? lv_obj_get_y(view.value) - 2
          : lv_obj_get_y(view.value) + lv_obj_get_height(view.value) -
                lv_font_get_line_height(lv_obj_get_style_text_font(view.unit, 0));
  setPosition(view.unit, x, y);
}

void showValue(const ValueView &view, const char *title, float value,
               uint8_t decimals, const char *unit, lv_color_t color,
               bool unitTop) {
  char number[16];
  formatValue(number, sizeof(number), value, decimals);
  setText(view.title, title);
  setText(view.value, number);
  setText(view.unit, std::isnan(value) ? "" : unit);
  setColor(view.title, color);
  setColor(view.value, color);
  setColor(view.unit, color);
  placeUnit(view, unitTop);
}

struct StatusIcons {
  lv_obj_t *lightning;
  lv_obj_t *wifi;
  lv_obj_t *homeAssistant;
  lv_obj_t *web;
};

StatusIcons makeStatusIcons(lv_obj_t *parent) {
  StatusIcons icons;
  icons.lightning = label(parent, &lv_font_montserrat_16, 0, 0);
  icons.wifi = label(parent, &lv_font_montserrat_16, 0, 0);
  lv_label_set_text(icons.wifi, LV_SYMBOL_WIFI);
  icons.homeAssistant = label(parent, &lv_font_montserrat_16, 0, 0);
  lv_label_set_text(icons.homeAssistant, LV_SYMBOL_HOME);
  icons.web = label(parent, &lv_font_montserrat_16, 0, 0);
  lv_label_set_text(icons.web, LV_SYMBOL_SETTINGS);
  return icons;
}

// Ikony stavu v řadě od x (zarovnané vlevo), nebo končící v x (vpravo).
void showStatusIcons(const StatusIcons &icons, const Palette &p, lv_coord_t x,
                     lv_coord_t y, bool alignRight) {
  const bool showWifi = !p.night || wifiConnected;
  const bool showHomeAssistant =
      homeAssistantStatusRelevant &&
      (!p.night || currentValues.homeAssistantOnline);
  lv_obj_t *objects[] = {icons.lightning, icons.wifi, icons.homeAssistant,
                         icons.web};
  const bool visible[] = {lightningAlertActive, showWifi, showHomeAssistant,
                          webActive};
  setText(icons.lightning, lv_label_get_text(lightningStatusLabel));
  setColor(icons.lightning, COLOR_ERROR);
  setColor(icons.wifi, p.night ? COLOR_ERROR
                               : (wifiConnected ? COLOR_AIR : COLOR_ERROR));
  setColor(icons.homeAssistant,
           p.night ? COLOR_ERROR
                   : (currentValues.homeAssistantOnline ? COLOR_AIR
                                                        : COLOR_ERROR));
  setColor(icons.web, p.night ? COLOR_ERROR : COLOR_OUTSIDE);
  constexpr lv_coord_t GAP = 14;
  lv_coord_t widths[4] = {};
  lv_coord_t total = 0;
  for (int i = 0; i < 4; ++i) {
    setVisible(objects[i], visible[i]);
    if (!visible[i]) continue;
    lv_obj_update_layout(objects[i]);
    widths[i] = lv_obj_get_width(objects[i]);
    total += widths[i] + (total > 0 ? GAP : 0);
  }
  lv_coord_t left = alignRight ? x - total : x;
  for (int i = 0; i < 4; ++i) {
    if (!visible[i]) continue;
    setPosition(objects[i], left, y);
    left += widths[i] + GAP;
  }
}

struct PageDots {
  lv_obj_t *dots[SCREEN_DOT_COUNT];
};

PageDots makePageDots(lv_obj_t *parent) {
  PageDots view;
  for (lv_obj_t *&dot : view.dots) {
    dot = panel(parent, 0, 0, 7, 7);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
  }
  return view;
}

// Tečky v pořadí z konfigurace, stejně jako updateScreenDots() na kulatém.
// centerX < 0 znamená zarovnat vlevo na -centerX.
void showPageDots(const PageDots &view, const Palette &p, lv_coord_t centerX,
                  lv_coord_t centerY) {
  uint8_t count = 0;
  uint8_t activeIndex = 0;
  for (uint8_t position = 0; position < SCREEN_DOT_COUNT; ++position) {
    const uint8_t screen = clockConfigScreenAt(dashboardRuntimeConfig, position);
    if (!screenAvailable(screen)) continue;
    if (screen == activeScreen) activeIndex = count;
    ++count;
  }
  const bool visible = count > 1;
  const lv_coord_t span = (count - 1) * HOME_DOT_GAP;
  const lv_coord_t first = centerX < 0 ? -centerX : centerX - span / 2;
  for (uint8_t index = 0; index < SCREEN_DOT_COUNT; ++index) {
    lv_obj_t *dot = view.dots[index];
    if (!visible || index >= count) {
      setVisible(dot, false);
      continue;
    }
    const bool active = index == activeIndex;
    const lv_coord_t size = active ? 10 : 7;
    if (lv_obj_get_width(dot) != size) lv_obj_set_size(dot, size, size);
    setBackground(dot, active ? p.text : p.dotOff);
    setPosition(dot, first + index * HOME_DOT_GAP - size / 2,
                centerY - size / 2);
    setVisible(dot, true);
  }
}

// Výstraha ČHMÚ (název ve barvě stupně, za ním čas) a blížící se déšť. Stav
// drží kulatý ciferník kvůli řádku na radaru.
bool warningActive() { return radarWarningEvent[0] != '\0'; }

lv_color_t warningColor(const Palette &p) {
  return p.night ? COLOR_ERROR : warningLevelColor(radarWarningLevel);
}

// Nejbližší události z agendy: přímo z popisků stránky agendy, která je
// jediným místem, kde ciferník seznam drží. Bez zapnuté agendy nic.
uint8_t agendaEventCount() {
  if (!agendaFeatureAvailable || agendaPage == nullptr) return 0;
  return agendaVisibleItemCount;
}

void formatAgendaEvent(uint8_t index, const Palette &p, char *output,
                       size_t capacity) {
  lv_color32_t color;
  color.full = lv_color_to32(
      p.night ? COLOR_ERROR : agendaCalendarColor(agendaRowCalendar[index]));
  snprintf(output, capacity, "#%02x%02x%02x %s#  %s", color.ch.red,
           color.ch.green, color.ch.blue,
           lv_label_get_text(agendaTimeLabels[index]),
           lv_label_get_text(agendaTitleLabels[index]));
}

const lv_img_dsc_t *weatherIcon(const Palette &p) {
  if (p.night || !weatherConfigured) return nullptr;
  return openWeatherIconForCode(currentValues.weatherCode,
                                currentValues.weatherIsDay);
}

void showWeatherIcon(lv_obj_t *image, const Palette &p) {
  const lv_img_dsc_t *icon = weatherIcon(p);
  setVisible(image, icon != nullptr);
  if (icon != nullptr && lv_img_get_src(image) != icon) lv_img_set_src(image, icon);
}

// --- Digitální domovská obrazovka -----------------------------------------

struct DigitalView {
  lv_obj_t *root;
  lv_obj_t *time;
  lv_obj_t *date;
  lv_obj_t *nameday;
  ValueView outside;
  lv_obj_t *weather;
  lv_obj_t *outsideLine;
  ValueView room;
  lv_obj_t *roomIcon;
  lv_obj_t *bandLine;
  lv_obj_t *bandDividers[2];
  ValueView metricA;
  ValueView metricB;
  lv_obj_t *warningIcon;
  lv_obj_t *warningEvent;
  lv_obj_t *warningTail;
  lv_obj_t *rain;
  lv_obj_t *agendaHead;
  lv_obj_t *agendaFirst;
  lv_obj_t *agendaSecond;
  StatusIcons status;
  PageDots dots;
} digital;

constexpr int RIGHT_X = 520;
constexpr int BAND_Y = 276;
constexpr int BAND_COLUMN_2 = 292;
constexpr int BAND_COLUMN_3 = 550;

void createDigital(lv_obj_t *screen) {
  DigitalView &v = digital;
  v.root = panel(screen, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
  v.time = label(v.root, &lcd7_time176, 24, 26);
  lv_obj_set_style_text_letter_space(v.time, -4, 0);
  v.date = label(v.root, &lcd7_text28, 32, 172, 470);
  lv_obj_set_style_text_letter_space(v.date, 3, 0);
  v.nameday = label(v.root, &lcd7_text22, 32, 212, 470);
  lv_label_set_recolor(v.nameday, true);

  v.outside = makeValue(v.root, &lcd7_text18, &lcd7_number72, &lcd7_text28,
                        RIGHT_X, 22, 26);
  v.weather = lv_img_create(v.root);
  lv_obj_set_pos(v.weather, SCREEN_WIDTH - 32 - 84, 24);
  v.outsideLine = line(v.root, RIGHT_X, 128, SCREEN_WIDTH - 32 - RIGHT_X, 1);
  v.room = makeValue(v.root, &lcd7_text18, &lcd7_number72, &lcd7_text28,
                     RIGHT_X, 140, 26);
  v.roomIcon = label(v.root, &clock_icons_42, SCREEN_WIDTH - 32 - 52, 160);
  lv_label_set_text(v.roomIcon, "\xEF\x80\x95");  // dům (U+F015)

  v.bandLine = line(v.root, 32, BAND_Y - 14, SCREEN_WIDTH - 64, 1);
  v.bandDividers[0] = line(v.root, BAND_COLUMN_2 - 18, BAND_Y, 1, 124);
  v.bandDividers[1] = line(v.root, BAND_COLUMN_3 - 18, BAND_Y, 1, 124);

  v.metricA = makeValue(v.root, &lcd7_text18, &lcd7_number38, &lcd7_text18, 32,
                        BAND_Y + 4, 24);
  v.metricB = makeValue(v.root, &lcd7_text18, &lcd7_number38, &lcd7_text18, 32,
                        BAND_Y + 66, 24);

  v.warningIcon = label(v.root, &lv_font_montserrat_20, BAND_COLUMN_2, BAND_Y + 8);
  lv_label_set_text(v.warningIcon, LV_SYMBOL_WARNING);
  v.warningEvent = label(v.root, &lcd7_text22, BAND_COLUMN_2 + 30, BAND_Y + 2, 196);
  v.warningTail = label(v.root, &lcd7_text15, BAND_COLUMN_2 + 30, BAND_Y + 32, 196);
  v.rain = label(v.root, &lcd7_text22, BAND_COLUMN_2, BAND_Y + 78, 226);

  v.agendaHead = label(v.root, &lcd7_text15, BAND_COLUMN_3, BAND_Y + 4, 218);
  lv_obj_set_style_text_letter_space(v.agendaHead, 2, 0);
  v.agendaFirst = label(v.root, &lcd7_text22, BAND_COLUMN_3, BAND_Y + 30, 218);
  lv_label_set_recolor(v.agendaFirst, true);
  v.agendaSecond = label(v.root, &lcd7_text18, BAND_COLUMN_3, BAND_Y + 70, 218);
  lv_label_set_recolor(v.agendaSecond, true);

  v.status = makeStatusIcons(v.root);
  v.dots = makePageDots(v.root);
}

void showWarningAndRain(lv_obj_t *icon, lv_obj_t *event, lv_obj_t *tail,
                        lv_obj_t *rain, const Palette &p) {
  // Bez výstrahy ikona chybí a text začíná tam, kde by stála.
  lv_obj_update_layout(icon);
  const lv_coord_t textX =
      lv_obj_get_x(icon) + (warningActive() ? lv_obj_get_width(icon) + 10 : 0);
  setPosition(event, textX, lv_obj_get_y(event));
  if (tail != nullptr) setPosition(tail, textX, lv_obj_get_y(tail));
  if (warningActive()) {
    setVisible(icon, true);
    setText(event, radarWarningEvent);
    setColor(icon, warningColor(p));
    setColor(event, warningColor(p));
    if (tail != nullptr) {
      setVisible(tail, true);
      setText(tail, radarWarningTail);
      setColor(tail, p.muted);
    }
  } else {
    setVisible(icon, false);
    setText(event, "Bez výstrah");
    setColor(event, p.muted);
    setVisible(tail, false);
  }
  setVisible(rain, rainAlertNote[0] != '\0');
  setText(rain, rainAlertNote);
  setColor(rain, p.night ? COLOR_ERROR : COLOR_OUTSIDE);
}

void showAgenda(lv_obj_t *head, lv_obj_t *first, lv_obj_t *second,
                const Palette &p) {
  const uint8_t count = agendaEventCount();
  char text[160];
  setVisible(head, head != nullptr && agendaFeatureAvailable);
  if (head != nullptr) {
    setText(head, count > 0 && agendaRowStartsDay[0]
                      ? lv_label_get_text(agendaDayLabels[0])
                      : "AGENDA");
    setColor(head, p.muted);
  }
  if (count > 0) {
    formatAgendaEvent(0, p, text, sizeof(text));
    setText(first, text);
    setColor(first, p.text);
  } else {
    setText(first, agendaFeatureAvailable && agendaPage != nullptr
                       ? lv_label_get_text(agendaStatusLabel)
                       : "");
    setColor(first, p.muted);
  }
  setVisible(second, count > 1);
  if (count > 1) {
    formatAgendaEvent(1, p, text, sizeof(text));
    setText(second, text);
    setColor(second, p.muted);
  }
}

void showDate(lv_obj_t *date, lv_obj_t *nameday, const Palette &p) {
  setText(date, lv_label_get_text(dateLabel));
  setColor(date, p.night ? COLOR_ERROR : COLOR_TEXT);
  const char *name = lv_label_get_text(namedayLabel);
  setVisible(nameday, name[0] != '\0');
  if (name[0] == '\0') return;
  char capitalized[64];
  capitalizeName(name, capitalized, sizeof(capitalized));
  lv_color32_t color;
  color.full = lv_color_to32(p.text);
  char text[96];
  snprintf(text, sizeof(text), "svátek má #%02x%02x%02x %s#", color.ch.red,
           color.ch.green, color.ch.blue, capitalized);
  setText(nameday, text);
  setColor(nameday, p.muted);
}

void syncDigital(const Palette &p) {
  DigitalView &v = digital;
  setBackground(v.root, COLOR_BACKGROUND);
  setText(v.time, displayedTimeText);
  setColor(v.time, p.text);
  showDate(v.date, v.nameday, p);

  showValue(v.outside, lv_label_get_text(outsideTitleLabel),
            currentValues.leftTemperatureC, outsideDecimals, outsideUnit,
            scaled(p, currentValues.leftTemperatureC, leftValueColorScale), true);
  showValue(v.room, lv_label_get_text(roomTitleLabel),
            currentValues.rightTemperatureC, roomDecimals, roomUnit,
            scaled(p, currentValues.rightTemperatureC, rightValueColorScale),
            true);
  setVisible(v.outside.title, outsideConfigured);
  setVisible(v.outside.value, outsideConfigured);
  setVisible(v.outside.unit, outsideConfigured);
  setVisible(v.room.title, roomConfigured);
  setVisible(v.room.value, roomConfigured);
  setVisible(v.room.unit, roomConfigured);
  showWeatherIcon(v.weather, p);
  setVisible(v.roomIcon, roomConfigured);
  setColor(v.roomIcon, scaled(p, currentValues.rightTemperatureC,
                              rightValueColorScale));

  showValue(v.metricA, metricAConfig.name, currentValues.metricAValue,
            metricAConfig.decimals, metricAConfig.suffix,
            scaled(p, currentValues.metricAValue, metricAColorScale), false);
  showValue(v.metricB, metricBConfig.name, currentValues.metricBValue,
            metricBConfig.decimals, metricBConfig.suffix,
            scaled(p, currentValues.metricBValue, metricBColorScale), false);

  showWarningAndRain(v.warningIcon, v.warningEvent, v.warningTail, v.rain, p);
  showAgenda(v.agendaHead, v.agendaFirst, v.agendaSecond, p);
  // Bez agendy patří třetí sloupec výstraze, jinak by zůstal prázdný.
  const bool agenda = agendaFeatureAvailable;
  setVisible(v.bandDividers[1], agenda);
  setVisible(v.agendaFirst, agenda);
  const lv_coord_t warningWidth =
      (agenda ? BAND_COLUMN_3 - 18 : SCREEN_WIDTH - 32) - BAND_COLUMN_2 - 18;
  if (lv_obj_get_width(v.rain) != warningWidth) {
    lv_obj_set_width(v.warningEvent, warningWidth - 30);
    lv_obj_set_width(v.warningTail, warningWidth - 30);
    lv_obj_set_width(v.rain, warningWidth);
  }

  for (lv_obj_t *divider : {v.outsideLine, v.bandLine, v.bandDividers[0],
                            v.bandDividers[1]})
    setBackground(divider, p.line);
  showStatusIcons(v.status, p, 32, SCREEN_HEIGHT - 32, false);
  showPageDots(v.dots, p, SCREEN_WIDTH / 2, SCREEN_HEIGHT - 24);
}

// --- Analogový ciferník: kulatý ciferník vlevo, sloupec vpravo ------------

struct AnalogView {
  lv_obj_t *root;
  lv_obj_t *date;
  lv_obj_t *nameday;
  lv_obj_t *lines[3];
  ValueView outside;
  lv_obj_t *weather;
  ValueView room;
  ValueView metricA;
  ValueView metricB;
  lv_obj_t *warningIcon;
  lv_obj_t *warningEvent;
  lv_obj_t *rain;
  lv_obj_t *agendaFirst;
  lv_obj_t *agendaSecond;
  StatusIcons status;
  PageDots dots;
} analog;

constexpr int SIDE_PAD = 18;
constexpr int SIDE_CONTENT = SIDE_WIDTH - 2 * SIDE_PAD;

void createAnalog(lv_obj_t *screen) {
  AnalogView &v = analog;
  v.root = panel(screen, SIDE_X, 0, SIDE_WIDTH, SCREEN_HEIGHT);
  v.date = label(v.root, &lcd7_text22, SIDE_PAD, 22, SIDE_CONTENT);
  lv_obj_set_style_text_letter_space(v.date, 2, 0);
  v.nameday = label(v.root, &lcd7_text18, SIDE_PAD, 54, SIDE_CONTENT);
  lv_label_set_recolor(v.nameday, true);
  v.lines[0] = line(v.root, SIDE_PAD, 84, SIDE_CONTENT, 1);
  v.outside = makeValue(v.root, &lcd7_text15, &lcd7_number72, &lcd7_text22,
                        SIDE_PAD, 94, 22);
  v.weather = lv_img_create(v.root);
  lv_obj_set_pos(v.weather, SIDE_WIDTH - SIDE_PAD - 84, 92);
  v.room = makeValue(v.root, &lcd7_text15, &lcd7_number72, &lcd7_text22,
                     SIDE_PAD, 180, 22);
  v.lines[1] = line(v.root, SIDE_PAD, 270, SIDE_CONTENT, 1);
  v.metricA = makeValue(v.root, &lcd7_text15, &lcd7_number38, &lcd7_text15,
                        SIDE_PAD, 280, 20);
  v.metricB = makeValue(v.root, &lcd7_text15, &lcd7_number38, &lcd7_text15,
                        SIDE_PAD + SIDE_CONTENT / 2, 280, 20);
  v.lines[2] = line(v.root, SIDE_PAD, 340, SIDE_CONTENT, 1);
  v.warningIcon = label(v.root, &lv_font_montserrat_16, SIDE_PAD, 352);
  lv_label_set_text(v.warningIcon, LV_SYMBOL_WARNING);
  v.warningEvent = label(v.root, &lcd7_text18, SIDE_PAD + 24, 350,
                         SIDE_CONTENT - 24);
  v.rain = label(v.root, &lcd7_text18, SIDE_PAD, 376, SIDE_CONTENT);
  v.agendaFirst = label(v.root, &lcd7_text18, SIDE_PAD, 402, SIDE_CONTENT);
  lv_label_set_recolor(v.agendaFirst, true);
  v.agendaSecond = label(v.root, &lcd7_text15, SIDE_PAD, 426, SIDE_CONTENT);
  lv_label_set_recolor(v.agendaSecond, true);
  v.status = makeStatusIcons(v.root);
  v.dots = makePageDots(v.root);
}

void syncAnalog(const Palette &p) {
  AnalogView &v = analog;
  showDate(v.date, v.nameday, p);
  showValue(v.outside, lv_label_get_text(outsideTitleLabel),
            currentValues.leftTemperatureC, outsideDecimals, outsideUnit,
            scaled(p, currentValues.leftTemperatureC, leftValueColorScale), true);
  showValue(v.room, lv_label_get_text(roomTitleLabel),
            currentValues.rightTemperatureC, roomDecimals, roomUnit,
            scaled(p, currentValues.rightTemperatureC, rightValueColorScale),
            true);
  showWeatherIcon(v.weather, p);
  showValue(v.metricA, metricAConfig.name, currentValues.metricAValue,
            metricAConfig.decimals, metricAConfig.suffix,
            scaled(p, currentValues.metricAValue, metricAColorScale), false);
  showValue(v.metricB, metricBConfig.name, currentValues.metricBValue,
            metricBConfig.decimals, metricBConfig.suffix,
            scaled(p, currentValues.metricBValue, metricBColorScale), false);
  // Výstraha v jednom řádku i s časem, sloupec je úzký.
  if (warningActive()) {
    char text[96];
    snprintf(text, sizeof(text), "%s · %s", radarWarningEvent, radarWarningTail);
    setVisible(v.warningIcon, true);
    setPosition(v.warningEvent, SIDE_PAD + 24, lv_obj_get_y(v.warningEvent));
    setText(v.warningEvent, text);
    setColor(v.warningIcon, warningColor(p));
    setColor(v.warningEvent, warningColor(p));
  } else {
    showWarningAndRain(v.warningIcon, v.warningEvent, nullptr, v.rain, p);
  }
  setVisible(v.rain, rainAlertNote[0] != '\0');
  setText(v.rain, rainAlertNote);
  setColor(v.rain, p.night ? COLOR_ERROR : COLOR_OUTSIDE);
  showAgenda(nullptr, v.agendaFirst, v.agendaSecond, p);
  for (lv_obj_t *divider : v.lines) setBackground(divider, p.line);
  showPageDots(v.dots, p, -(SIDE_PAD + 5), SCREEN_HEIGHT - 24);
  showStatusIcons(v.status, p, SIDE_WIDTH - SIDE_PAD, SCREEN_HEIGHT - 32, true);
}

// Na kulatém ciferníku jsou hodnoty, datum a ikony uvnitř kruhu. Na 7" je
// má pravý sloupec, takže v ciferníku zůstanou jen ručičky a značky.
void hideRoundDialContent() {
  lv_obj_t *objects[] = {
      analogOutsideTitleLabel, analogOutsideValueLabel, analogOutsideDecimalLabel,
      analogOutsideUnitLabel,  analogRoomTitleLabel,    analogRoomValueLabel,
      analogRoomDecimalLabel,  analogRoomUnitLabel,     analogMetricATitleLabel,
      analogMetricAValueLabel, analogMetricAUnitLabel,  analogMetricBTitleLabel,
      analogMetricBValueLabel, analogMetricBUnitLabel,  analogMetricDivider,
      dateLabel,               namedayLabel,            weatherImage,
      weatherAnimation,        wifiStatusLabel,         statusLabel,
      webStatusLabel,          lightningStatusLabel,    screenDotsBacking,
  };
  for (lv_obj_t *object : objects) setVisible(object, false);
  for (lv_obj_t *dot : screenDots) setVisible(dot, false);
}

// --- Dlaždice hodnot ------------------------------------------------------

// Průběh hodnot za posledních 24 h pro čáru v dlaždici. Vzorek každých
// 15 minut, jen v RAM: po restartu se čára plní znovu, zato se nic nezapisuje
// do flash (zápis do flash rozhodí RGB panel). Paměť se bere hned při startu,
// než se PSRAM rozdrobí.
constexpr size_t HISTORY_POINTS = 96;
constexpr uint32_t HISTORY_INTERVAL_MS = 15UL * 60UL * 1000UL;

struct History {
  float values[CLOCK_VALUE_SLOT_COUNT][HISTORY_POINTS];
  uint8_t head;   // kam padne příští vzorek
  uint8_t count;  // kolik vzorků je platných
  uint32_t version;
};
History *history = nullptr;
uint32_t lastHistorySampleAt = 0;

void allocateHistory() {
  history = static_cast<History *>(
      heap_caps_calloc(1, sizeof(History), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

float slotReadingForHistory(size_t slotIndex) {
  const float battery = currentValues.slotBatteryPercent[slotIndex];
  if (!std::isnan(battery) && battery <= CLOCK_DEAD_BATTERY_PERCENT) return NAN;
  return valueSlotReading(slotIndex);
}

void recordHistorySample() {
  for (size_t slot = 0; slot < CLOCK_VALUE_SLOT_COUNT; ++slot)
    history->values[slot][history->head] = slotReadingForHistory(slot);
  history->head = (history->head + 1) % HISTORY_POINTS;
  if (history->count < HISTORY_POINTS) ++history->count;
  ++history->version;
  lastHistorySampleAt = millis();
}

// První vzorek padne, jakmile je známá aspoň jedna hodnota, další po
// HISTORY_INTERVAL_MS.
void maintainHistory() {
  if (history == nullptr) return;
  if (history->count > 0) {
    if (millis() - lastHistorySampleAt >= HISTORY_INTERVAL_MS) recordHistorySample();
    return;
  }
  for (size_t slot = 0; slot < CLOCK_VALUE_SLOT_COUNT; ++slot) {
    if (!std::isnan(slotReadingForHistory(slot))) {
      recordHistorySample();
      return;
    }
  }
}

// Vzorek "age" kroků zpět (0 = nejnovější).
float historyAt(size_t slot, size_t age) {
  const size_t index = (history->head + HISTORY_POINTS - 1 - age) % HISTORY_POINTS;
  return history->values[slot][index];
}

#if !FIRMWARE_RELEASE
// Ukázkový průběh pro screenshot, než se nasbírá skutečný (USB HISTORYDEMO).
void fillHistoryDemo() {
  if (history == nullptr) return;
  for (size_t slot = 0; slot < CLOCK_VALUE_SLOT_COUNT; ++slot) {
    const float now = slotReadingForHistory(slot);
    const float base = std::isnan(now) ? 10.0f : now;
    const float swing = std::fabs(base) * 0.15f + 1.0f;
    for (size_t i = 0; i < HISTORY_POINTS; ++i) {
      const float phase = static_cast<float>(i) / HISTORY_POINTS * 6.283f;
      history->values[slot][i] =
          base + swing * (std::sin(phase + slot) - std::sin(6.283f + slot));
    }
  }
  history->head = 0;
  history->count = HISTORY_POINTS;
  ++history->version;
}
#endif

struct Tile {
  lv_obj_t *box;
  lv_obj_t *title;
  lv_obj_t *value;
  lv_obj_t *unit;
  lv_obj_t *battery;
  lv_obj_t *spark;
  lv_obj_t *since;
  lv_obj_t *range;
  lv_point_t points[HISTORY_POINTS];
  uint32_t drawnVersion;
  size_t drawnSlot;
  lv_coord_t drawnWidth;
  lv_coord_t drawnHeight;
};

// Horní lišta 800 x 40 obrazovek s množstvím dat: čas, datum, obě teploty,
// výstraha a tečky obrazovek.
constexpr int BAR_HEIGHT = 40;

struct TopBar {
  lv_obj_t *time;
  lv_obj_t *date;
  lv_obj_t *outside;
  lv_obj_t *room;
  lv_obj_t *warning;
  lv_obj_t *line;
  PageDots dots;
};

TopBar makeTopBar(lv_obj_t *parent) {
  TopBar bar;
  bar.time = label(parent, &lcd7_text28, 20, 2);
  bar.date = label(parent, &lcd7_text15, 0, 12);
  lv_obj_set_style_text_letter_space(bar.date, 2, 0);
  bar.outside = label(parent, &lcd7_text22, 0, 6);
  bar.room = label(parent, &lcd7_text22, 0, 6);
  bar.warning = label(parent, &lcd7_text15, 0, 12, 180);
  bar.line = line(parent, 0, BAR_HEIGHT, SCREEN_WIDTH, 1);
  bar.dots = makePageDots(parent);
  return bar;
}

struct ValuesView {
  lv_obj_t *root;
  TopBar bar;
  Tile tiles[CLOCK_VALUE_PAGE_SLOT_COUNT];
} values;

void createValues(lv_obj_t *screen) {
  ValuesView &v = values;
  v.root = panel(screen, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
  v.bar = makeTopBar(v.root);
  for (size_t index = 0; index < CLOCK_VALUE_PAGE_SLOT_COUNT; ++index) {
    Tile &tile = v.tiles[index];
    tile.box = panel(v.root, 0, 0, 1, 1);
    lv_obj_set_style_bg_color(tile.box, TILE_BACKGROUND, 0);
    lv_obj_set_style_border_width(tile.box, 1, 0);
    lv_obj_set_style_border_color(tile.box, TILE_BORDER, 0);
    lv_obj_set_style_radius(tile.box, 12, 0);
    tile.title = label(tile.box, &lcd7_text15, TILE_PAD, 10, 1);
    lv_obj_set_style_text_letter_space(tile.title, 2, 0);
    tile.value = label(tile.box, &lcd7_number72, TILE_PAD, 52);
    tile.unit = label(tile.box, &lcd7_text18, TILE_PAD, 52);
    tile.battery = label(tile.box, &lv_font_montserrat_16, 0, 10);
    lv_label_set_text(tile.battery, LV_SYMBOL_BATTERY_EMPTY);
    tile.spark = lv_line_create(tile.box);
    lv_obj_set_style_line_width(tile.spark, 2, 0);
    lv_obj_set_style_line_rounded(tile.spark, true, 0);
    lv_obj_add_flag(tile.spark, LV_OBJ_FLAG_HIDDEN);
    tile.since = label(tile.box, &lcd7_text15, TILE_PAD, 0);
    lv_label_set_text(tile.since, "před 24 h");
    tile.range = label(tile.box, &lcd7_text15, 0, 0);
    tile.drawnVersion = UINT32_MAX;
  }
}

void syncTopBar(TopBar &v, const Palette &p) {
  setText(v.time, displayedTimeText);
  setColor(v.time, p.text);
  lv_obj_update_layout(v.time);
  lv_coord_t x = lv_obj_get_x(v.time) + lv_obj_get_width(v.time) + 16;
  setText(v.date, lv_label_get_text(dateLabel));
  setColor(v.date, p.muted);
  setPosition(v.date, x, 12);
  lv_obj_update_layout(v.date);
  x += lv_obj_get_width(v.date) + 18;
  char text[32];
  char number[16];
  formatValue(number, sizeof(number), currentValues.leftTemperatureC,
              outsideDecimals);
  snprintf(text, sizeof(text), "%s %s", number, outsideUnit);
  setText(v.outside, text);
  setColor(v.outside, scaled(p, currentValues.leftTemperatureC, leftValueColorScale));
  setPosition(v.outside, x, 6);
  setVisible(v.outside, outsideConfigured);
  lv_obj_update_layout(v.outside);
  x += lv_obj_get_width(v.outside) + 16;
  formatValue(number, sizeof(number), currentValues.rightTemperatureC,
              roomDecimals);
  snprintf(text, sizeof(text), "%s %s", number, roomUnit);
  setText(v.room, text);
  setColor(v.room, scaled(p, currentValues.rightTemperatureC, rightValueColorScale));
  setPosition(v.room, x, 6);
  setVisible(v.room, roomConfigured);

  setVisible(v.warning, warningActive());
  setText(v.warning, radarWarningEvent);
  setColor(v.warning, warningColor(p));
  setBackground(v.line, p.line);
  // Tečky vpravo; jejich šířka určuje, kde končí výstraha.
  uint8_t count = 0;
  for (uint8_t position = 0; position < SCREEN_DOT_COUNT; ++position)
    if (screenAvailable(clockConfigScreenAt(dashboardRuntimeConfig, position)))
      ++count;
  const lv_coord_t dotsLeft =
      SCREEN_WIDTH - 20 - (count > 1 ? (count - 1) * HOME_DOT_GAP : 0);
  showPageDots(v.dots, p, -dotsLeft, BAR_HEIGHT / 2);
  lv_obj_update_layout(v.warning);
  setPosition(v.warning, dotsLeft - 16 - lv_obj_get_width(v.warning), 12);
}

// Rozmístí dlaždici na pozici "position" v mřížce pro "count" dlaždic.
void placeTile(Tile &tile, size_t position, size_t count) {
  const int columns = count <= 4 ? 2 : 3;
  const int rows = count <= 2 ? 1 : (count <= 6 ? 2 : 3);
  const int width = (SCREEN_WIDTH - 2 * TILE_LEFT - (columns - 1) * TILE_GAP) / columns;
  const int height = (TILE_BOTTOM - TILE_TOP - (rows - 1) * TILE_GAP) / rows;
  const int x = TILE_LEFT + (position % columns) * (width + TILE_GAP);
  const int y = TILE_TOP + (position / columns) * (height + TILE_GAP);
  if (lv_obj_get_width(tile.box) != width || lv_obj_get_height(tile.box) != height) {
    lv_obj_set_size(tile.box, width, height);
    lv_obj_set_width(tile.title, width - 2 * TILE_PAD - 24);
    setPosition(tile.battery, width - TILE_PAD - 22, 10);
  }
  setPosition(tile.box, x, y);
}

void showSparkline(Tile &tile, size_t slotIndex, const ValueSlotDisplay &display,
                   lv_color_t color, const Palette &p, lv_coord_t tileWidth,
                   lv_coord_t tileHeight, lv_coord_t sparkY,
                   lv_coord_t sparkHeight, bool tall) {
  const size_t count = history == nullptr ? 0 : history->count;
  float low = NAN;
  float high = NAN;
  size_t valid = 0;
  for (size_t age = 0; age < count; ++age) {
    const float value = historyAt(slotIndex, age);
    if (std::isnan(value)) continue;
    low = std::isnan(low) ? value : std::min(low, value);
    high = std::isnan(high) ? value : std::max(high, value);
    ++valid;
  }
  const bool visible = valid >= 2;
  setVisible(tile.spark, visible);
  setVisible(tile.since, visible && tall);
  setVisible(tile.range, visible && tall);
  if (!visible) return;
  if (lv_color_to32(lv_obj_get_style_line_color(tile.spark, 0)) != lv_color_to32(color))
    lv_obj_set_style_line_color(tile.spark, color, 0);
  setColor(tile.since, p.muted);
  setColor(tile.range, p.muted);

  if (tile.drawnVersion == history->version && tile.drawnSlot == slotIndex &&
      tile.drawnWidth == tileWidth && tile.drawnHeight == tileHeight)
    return;
  tile.drawnVersion = history->version;
  tile.drawnSlot = slotIndex;
  tile.drawnWidth = tileWidth;
  tile.drawnHeight = tileHeight;

  // Osa x je vždy celých 24 h, nejnovější vzorek vpravo; mezery bez
  // hodnoty čára přeskočí.
  const lv_coord_t width = tileWidth - 2 * TILE_PAD;
  const float span = high - low > 1e-6f ? high - low : 1.0f;
  uint16_t points = 0;
  for (size_t age = count; age-- > 0;) {
    const float value = historyAt(slotIndex, age);
    if (std::isnan(value)) continue;
    const lv_coord_t x = static_cast<lv_coord_t>(
        width - 1 - static_cast<int32_t>(age) * (width - 1) / (HISTORY_POINTS - 1));
    const lv_coord_t y = static_cast<lv_coord_t>(
        high - low > 1e-6f ? (high - value) / span * (sparkHeight - 2) + 1
                           : sparkHeight / 2);
    tile.points[points++] = {x, y};
  }
  lv_line_set_points(tile.spark, tile.points, points);
  setPosition(tile.spark, TILE_PAD, sparkY);
  if (tall) {
    const lv_coord_t captionY = tileHeight - 10 - lv_font_get_line_height(&lcd7_text15);
    setPosition(tile.since, TILE_PAD, captionY);
    char lowText[16];
    char highText[16];
    char text[40];
    formatValue(lowText, sizeof(lowText), low, display.decimals);
    formatValue(highText, sizeof(highText), high, display.decimals);
    snprintf(text, sizeof(text), "%s – %s", lowText, highText);
    setText(tile.range, text);
    lv_obj_update_layout(tile.range);
    setPosition(tile.range, tileWidth - TILE_PAD - lv_obj_get_width(tile.range),
                captionY);
  }
}

void syncValues(const Palette &p) {
  ValuesView &v = values;
  syncTopBar(v.bar, p);
  const bool openMeteo =
      dashboardRuntimeConfig.dataSource == CLOCK_DATA_SOURCE_OPEN_METEO;
  // Stejné pravidlo jako updateValuesPage(): Open-Meteo plní jen první
  // čtyři sloty.
  bool enabled[CLOCK_VALUE_PAGE_SLOT_COUNT] = {};
  size_t enabledCount = 0;
  for (size_t index = 0; index < CLOCK_VALUE_PAGE_SLOT_COUNT; ++index) {
    const ClockValueSlotConfig &slot = clockConfigValueSlot(
        dashboardRuntimeConfig,
        index + activeValuesPage * CLOCK_VALUE_PAGE_SLOT_COUNT);
    enabled[index] = slot.enabled && !(openMeteo && index > 3);
    if (enabled[index]) ++enabledCount;
  }
  size_t position = 0;
  for (size_t index = 0; index < CLOCK_VALUE_PAGE_SLOT_COUNT; ++index) {
    Tile &tile = v.tiles[index];
    const size_t slotIndex = index + activeValuesPage * CLOCK_VALUE_PAGE_SLOT_COUNT;
    const ClockValueSlotConfig &slot =
        clockConfigValueSlot(dashboardRuntimeConfig, slotIndex);
    setVisible(tile.box, enabled[index]);
    if (!enabled[index]) continue;
    placeTile(tile, position++, enabledCount);
    const lv_coord_t tileWidth = lv_obj_get_width(tile.box);
    const lv_coord_t tileHeight = lv_obj_get_height(tile.box);
    const ValueSlotDisplay display = valueSlotDisplay(slotIndex, slot);
    const float batteryPercent = currentValues.slotBatteryPercent[slotIndex];
    const bool batteryLow = !openMeteo && !std::isnan(batteryPercent) &&
                            batteryPercent < CLOCK_LOW_BATTERY_PERCENT;
    const bool batteryDead =
        batteryLow && batteryPercent <= CLOCK_DEAD_BATTERY_PERCENT;
    const float reading = batteryDead ? NAN : valueSlotReading(slotIndex);

    char suffix[CLOCK_METRIC_SUFFIX_LENGTH];
    strlcpy(suffix, display.suffix, sizeof(suffix));
    normalizeMicroSign(suffix);
    char number[16];
    formatValue(number, sizeof(number), reading, display.decimals);
    setText(tile.title, display.name);
    setText(tile.value, number);
    setText(tile.unit, std::isnan(reading) ? "" : suffix);
    // Dlouhé číslo s jednotkou se do dlaždice velkým písmem nevejde.
    lv_point_t numberSize;
    lv_point_t unitSize;
    lv_txt_get_size(&numberSize, number, &lcd7_number72, 0, 0, LV_COORD_MAX,
                    LV_TEXT_FLAG_NONE);
    lv_txt_get_size(&unitSize, suffix, &lcd7_text18, 0, 0, LV_COORD_MAX,
                    LV_TEXT_FLAG_NONE);
    const bool large =
        numberSize.x + 5 + unitSize.x <= tileWidth - 2 * TILE_PAD;
    const lv_font_t *font = large ? &lcd7_number72 : &lcd7_number44;
    if (lv_obj_get_style_text_font(tile.value, 0) != font)
      lv_obj_set_style_text_font(tile.value, font, 0);
    // Pod číslem čára průběhu; ve vysoké dlaždici i popisek s rozsahem.
    const bool tall = tileHeight >= 150;
    const lv_coord_t captionHeight = tall ? lv_font_get_line_height(&lcd7_text15) + 4 : 0;
    const lv_coord_t sparkHeight = tall ? 56 : 24;
    const lv_coord_t sparkY = tileHeight - 12 - captionHeight - sparkHeight;
    // Číslo uprostřed plochy mezi názvem a čárou.
    const lv_coord_t titleBottom = 10 + lv_font_get_line_height(&lcd7_text15);
    setPosition(tile.value, TILE_PAD,
                titleBottom + (sparkY - titleBottom -
                               lv_font_get_line_height(font)) / 2);
    const lv_color_t color = scaled(p, reading, *display.colorScale);
    showSparkline(tile, slotIndex, display, color, p, tileWidth, tileHeight,
                  sparkY, sparkHeight, tall);
    setColor(tile.value, color);
    setColor(tile.unit, color);
    setColor(tile.title, p.muted);
    setVisible(tile.battery, batteryLow);
    setColor(tile.battery, COLOR_ERROR);
    lv_obj_update_layout(tile.value);
    setPosition(tile.unit,
                lv_obj_get_x(tile.value) + lv_obj_get_width(tile.value) + 5,
                lv_obj_get_y(tile.value) + lv_obj_get_height(tile.value) -
                    lv_font_get_line_height(&lcd7_text18));
    setBackground(tile.box, p.night ? COLOR_BACKGROUND : TILE_BACKGROUND);
    setBorder(tile.box, p.night ? NIGHT_LINE : TILE_BORDER);
  }
}

// --- Předpověď: graf přes celou šířku, pod ním týden ---------------------
// Graf kreslí drawForecastChart() kulatého ciferníku podle šířky objektu,
// jen s většími písmy. Tažení střídá předpověď a naměřených 24 hodin
// (clockDashboardSwipeForecast).

constexpr int FORECAST_CHART_X = 8;
constexpr int FORECAST_CHART_Y = BAR_HEIGHT + 8;
constexpr int FORECAST_CHART_W = SCREEN_WIDTH - 2 * FORECAST_CHART_X;
constexpr int FORECAST_CHART_H = 262;
constexpr int FORECAST_DAYS_Y = FORECAST_CHART_Y + FORECAST_CHART_H + 8;
constexpr int FORECAST_DAY_W = FORECAST_CHART_W / WEATHER_FORECAST_MAX_DAYS;
constexpr int FORECAST_DAY_ICON = 40;
constexpr size_t FORECAST_HOUR_ICONS = WEATHER_FORECAST_MAX_HOURS;

struct ForecastDayView {
  lv_obj_t *name;
  lv_obj_t *icon;
  lv_obj_t *temperatures;
  lv_obj_t *rain;
  ForecastIconState iconState;
};

struct ForecastView {
  lv_obj_t *root;
  TopBar bar;
  lv_obj_t *chart;
  lv_obj_t *message;
  lv_obj_t *icons[FORECAST_HOUR_ICONS];
  ForecastIconState iconStates[FORECAST_HOUR_ICONS];
  lv_obj_t *daysLine;
  ForecastDayView days[WEATHER_FORECAST_MAX_DAYS];
  lv_obj_t *air;
  lv_obj_t *header;
  uint32_t signature;
} forecast;

void drawWideForecastChart(lv_event_t *event) {
  forecastChartSmallFont = &lcd7_text15;
  forecastChartLabelFont = &lcd7_text18;
  drawForecastChart(event);
  forecastChartSmallFont = &clock_czech_14;
  forecastChartLabelFont = &clock_czech_16;
}

lv_obj_t *forecastIcon(lv_obj_t *parent, lv_coord_t size,
                       ForecastIconState *state) {
  lv_obj_t *icon = lv_obj_create(parent);
  lv_obj_remove_style_all(icon);
  lv_obj_set_size(icon, size, size);
  lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(icon, drawForecastIcon, LV_EVENT_DRAW_MAIN, state);
  lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
  return icon;
}

void createForecast(lv_obj_t *screen) {
  ForecastView &v = forecast;
  v.root = panel(screen, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
  v.bar = makeTopBar(v.root);
  v.chart = lv_obj_create(v.root);
  lv_obj_remove_style_all(v.chart);
  lv_obj_set_pos(v.chart, FORECAST_CHART_X, FORECAST_CHART_Y);
  lv_obj_set_size(v.chart, FORECAST_CHART_W, FORECAST_CHART_H);
  lv_obj_clear_flag(v.chart, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(v.chart, drawWideForecastChart, LV_EVENT_DRAW_MAIN, nullptr);
  v.message = label(v.root, &lcd7_text22, FORECAST_CHART_X,
                    FORECAST_CHART_Y + FORECAST_CHART_H / 2 - 14, FORECAST_CHART_W);
  lv_obj_set_style_text_align(v.message, LV_TEXT_ALIGN_CENTER, 0);
  for (size_t index = 0; index < FORECAST_HOUR_ICONS; ++index)
    v.icons[index] = forecastIcon(v.root, FORECAST_ICON_SIZE, &v.iconStates[index]);
  v.daysLine = line(v.root, FORECAST_CHART_X, FORECAST_DAYS_Y - 4, FORECAST_CHART_W, 1);
  for (size_t index = 0; index < WEATHER_FORECAST_MAX_DAYS; ++index) {
    ForecastDayView &day = v.days[index];
    const lv_coord_t x = FORECAST_CHART_X + index * FORECAST_DAY_W;
    day.name = label(v.root, &lcd7_text15, x, FORECAST_DAYS_Y + 2, FORECAST_DAY_W);
    lv_obj_set_style_text_align(day.name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(day.name, 2, 0);
    day.icon = forecastIcon(v.root, FORECAST_DAY_ICON, &day.iconState);
    lv_obj_set_pos(day.icon, x + (FORECAST_DAY_W - FORECAST_DAY_ICON) / 2,
                   FORECAST_DAYS_Y + 24);
    day.temperatures = label(v.root, &lcd7_text22, x, FORECAST_DAYS_Y + 68,
                             FORECAST_DAY_W);
    lv_obj_set_style_text_align(day.temperatures, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_recolor(day.temperatures, true);
    day.rain = label(v.root, &lcd7_text15, x, FORECAST_DAYS_Y + 98, FORECAST_DAY_W);
    lv_obj_set_style_text_align(day.rain, LV_TEXT_ALIGN_CENTER, 0);
  }
  v.air = label(v.root, &lcd7_text15, 20, SCREEN_HEIGHT - 26, 520);
  lv_label_set_recolor(v.air, true);
  v.header = label(v.root, &lcd7_text15, 0, SCREEN_HEIGHT - 26);
}

// Co se změnilo od posledního kreslení grafu: data, stránka, noc a minuta
// (značka "teď" se posouvá).
uint32_t forecastSignature() {
  uint32_t hash = 2166136261u;
  const auto mix = [&hash](uint32_t value) { hash = (hash ^ value) * 16777619u; };
  mix(forecastDisplayedAvailable);
  mix(forecastHistoryAvailable);
  mix(forecastPageIndex);
  mix(redNightVisualEnabled());
  mix(static_cast<uint32_t>(time(nullptr) / 60));
  if (forecastDisplayedAvailable && forecastDisplayed != nullptr) {
    mix(forecastDisplayed->hourCount);
    mix(static_cast<uint32_t>(forecastDisplayed->hours[0].time));
    for (size_t i = 0; i < forecastDisplayed->hourCount; ++i) {
      float t = forecastDisplayed->hours[i].temperatureC;
      uint32_t bits;
      memcpy(&bits, &t, sizeof(bits));
      mix(bits);
    }
  }
  if (forecastHistoryAvailable && forecastHistory != nullptr) {
    mix(forecastHistory->hourCount);
    mix(forecastHistory->lineCount);
  }
  return hash;
}

void syncForecastIcons(const Palette &p) {
  ForecastView &v = forecast;
  size_t used = 0;
  const WeatherForecastHour *hours = nullptr;
  size_t count = 0;
  bool historyMode = false;
  if (forecastChartView(hours, count, historyMode)) {
    // Stejné sloupce a krok popisků jako drawForecastChart().
    const float columnWidth =
        static_cast<float>(FORECAST_CHART_W - FORECAST_CHART_AXIS_WIDTH) / count;
    const int step =
        weatherForecastChartLabelStep(columnWidth, FORECAST_CHART_LABEL_SPACING);
    for (size_t index = 0; index < count && used < FORECAST_HOUR_ICONS; ++index) {
      const WeatherForecastHour &hour = hours[index];
      if (!forecastChartLabelled(hour, step) || hour.weatherCode < 0) continue;
      const int x = FORECAST_CHART_X + FORECAST_CHART_AXIS_WIDTH +
                    static_cast<int>(lroundf((index + 0.5f) * columnWidth)) -
                    FORECAST_ICON_SIZE / 2;
      v.iconStates[used] = {hour.weatherCode, hour.isDay, p.night};
      setPosition(v.icons[used], x, FORECAST_CHART_Y + 1);
      setVisible(v.icons[used], true);
      lv_obj_invalidate(v.icons[used]);
      ++used;
    }
  }
  for (size_t index = used; index < FORECAST_HOUR_ICONS; ++index)
    setVisible(v.icons[index], false);
}

void syncForecastDays(const Palette &p) {
  ForecastView &v = forecast;
  const bool available = forecastDisplayedAvailable && forecastDisplayed != nullptr;
  const size_t count = available ? forecastDisplayed->dayCount : 0;
  for (size_t index = 0; index < WEATHER_FORECAST_MAX_DAYS; ++index) {
    ForecastDayView &view = v.days[index];
    const bool shown = index < count;
    setVisible(view.name, shown);
    setVisible(view.icon, shown && forecastDisplayed->days[index].weatherCode >= 0);
    setVisible(view.temperatures, shown);
    setVisible(view.rain, shown);
    if (!shown) continue;
    const WeatherForecastDay &day = forecastDisplayed->days[index];
    const time_t stamp = static_cast<time_t>(day.time);
    struct tm local;
    localtime_r(&stamp, &local);
    setText(view.name, forecastWeekdayName(local.tm_wday));
    setColor(view.name, p.muted);
    const ForecastIconState state = {day.weatherCode, true, p.night};
    if (view.iconState.wmoCode != state.wmoCode ||
        view.iconState.redNight != state.redNight) {
      view.iconState = state;
      lv_obj_invalidate(view.icon);
    }
    char high[16];
    char low[16];
    formatValue(high, sizeof(high), day.maximumC, 0);
    formatValue(low, sizeof(low), day.minimumC, 0);
    char highTag[10] = "";
    char lowTag[10] = "";
    forecastAppendColorTag(highTag, sizeof(highTag),
                           p.night ? COLOR_ERROR : forecastTemperatureColor(day.maximumC));
    forecastAppendColorTag(lowTag, sizeof(lowTag),
                           p.night ? NIGHT_MUTED : forecastTemperatureColor(day.minimumC));
    char text[64];
    snprintf(text, sizeof(text), "%s%s°#  %s%s°#", highTag, high, lowTag, low);
    setText(view.temperatures, text);
    if (!std::isnan(day.precipitationMm) && day.precipitationMm >= 0.1f) {
      char rain[24];
      formatValue(rain, sizeof(rain), day.precipitationMm,
                  day.precipitationMm < 10.0f ? 1 : 0);
      snprintf(text, sizeof(text), "%s mm", rain);
      setText(view.rain, text);
    } else {
      setText(view.rain, "");
    }
    setColor(view.rain, p.night ? COLOR_ERROR : COLOR_OUTSIDE);
  }
  setBackground(v.daysLine, p.line);
}

void syncForecastAir(const Palette &p) {
  ForecastView &v = forecast;
  const bool shown = forecastDisplayedAvailable && forecastDisplayed != nullptr &&
                     forecastDisplayed->airAvailable;
  setVisible(v.air, shown);
  if (shown) {
    const WeatherForecastAirQuality &air = forecastDisplayed->air;
    char aqi[12];
    char pm[12];
    char pollen[12];
    forecastRoundedValue(aqi, sizeof(aqi), air.europeanAqi);
    forecastRoundedValue(pm, sizeof(pm), air.pm25);
    forecastRoundedValue(pollen, sizeof(pollen), air.grassPollen);
    char aqiTag[10] = "";
    char pmTag[10] = "";
    char pollenTag[10] = "";
    forecastAppendColorTag(aqiTag, sizeof(aqiTag),
                           p.night ? COLOR_ERROR : forecastAqiColor(air.europeanAqi));
    forecastAppendColorTag(pmTag, sizeof(pmTag),
                           p.night ? COLOR_ERROR : forecastPm25Color(air.pm25));
    forecastAppendColorTag(pollenTag, sizeof(pollenTag),
                           p.night ? COLOR_ERROR : forecastPollenColor(air.grassPollen));
    char text[160];
    snprintf(text, sizeof(text), "AQI %s%s#    PM2,5 %s%s# µg/m³    %s %s%s#",
             aqiTag, aqi, pmTag, pm, englishLanguage() ? "Grass pollen" : "Pyl trav",
             pollenTag, pollen);
    setText(v.air, text);
    setColor(v.air, p.muted);
  }
  // Stav stažení (aktualizováno ...) vpravo dole, jak ho skládá kulatá
  // stránka.
  setText(v.header, forecastHeaderLabel != nullptr
                        ? lv_label_get_text(forecastHeaderLabel)
                        : "");
  setColor(v.header, p.muted);
  lv_obj_update_layout(v.header);
  setPosition(v.header, SCREEN_WIDTH - 20 - lv_obj_get_width(v.header),
              SCREEN_HEIGHT - 26);
}

void syncForecast(const Palette &p) {
  ForecastView &v = forecast;
  syncTopBar(v.bar, p);
  const WeatherForecastHour *hours = nullptr;
  size_t count = 0;
  bool historyMode = false;
  const bool chart = forecastChartView(hours, count, historyMode);
  setVisible(v.chart, chart);
  setVisible(v.message, !chart);
  if (!chart) {
    setText(v.message, forecastMessageLabel != nullptr
                           ? lv_label_get_text(forecastMessageLabel)
                           : "");
    setColor(v.message, p.muted);
  }
  const uint32_t signature = forecastSignature();
  if (signature != v.signature) {
    v.signature = signature;
    lv_obj_invalidate(v.chart);
    syncForecastIcons(p);
  }
  syncForecastDays(p);
  syncForecastAir(p);
}

// --- Meteoradar: mapa 640 x 480 vlevo, úzký sloupec vpravo ---------------
// Snímek kreslí ChmiRadarService do obdélníku CHMI_RADAR_WIDTH x 480; tady
// se jen ukáže na vlastním plátně. Popisky kulaté stránky (čas, rozsah,
// snímek) přebírá sloupec vpravo.

constexpr int RADAR_COLUMN_X = CHMI_RADAR_WIDTH;
constexpr int RADAR_COLUMN_W = SCREEN_WIDTH - RADAR_COLUMN_X;
constexpr int RADAR_PAD = 14;
constexpr int RADAR_CONTENT = RADAR_COLUMN_W - 2 * RADAR_PAD;
constexpr int RADAR_FRAME_DOTS = 16;
constexpr int RADAR_DOTS_PER_ROW = 8;
constexpr int RADAR_DOT_GAP = 16;

struct RadarView {
  lv_obj_t *root;
  lv_obj_t *canvas;
  lv_obj_t *status;
  lv_obj_t *column;
  lv_obj_t *time;
  lv_obj_t *date;
  lv_obj_t *outside;
  lv_obj_t *lines[2];
  lv_obj_t *range;
  lv_obj_t *frame;
  lv_obj_t *frameDots[RADAR_FRAME_DOTS];
  lv_obj_t *warningIcon;
  lv_obj_t *warningEvent;
  lv_obj_t *warningTail;
  lv_obj_t *rain;
  StatusIcons icons;
  PageDots dots;
  const uint16_t *pixels;
} radar;

void createRadar(lv_obj_t *screen) {
  RadarView &v = radar;
  v.root = panel(screen, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
  v.canvas = lv_canvas_create(v.root);
  lv_obj_set_pos(v.canvas, 0, 0);
  lv_obj_add_flag(v.canvas, LV_OBJ_FLAG_HIDDEN);
  v.status = label(v.root, &lcd7_text22, 0, SCREEN_HEIGHT / 2 - 14, CHMI_RADAR_WIDTH);
  lv_obj_set_style_text_align(v.status, LV_TEXT_ALIGN_CENTER, 0);
  v.column = panel(v.root, RADAR_COLUMN_X, 0, RADAR_COLUMN_W, SCREEN_HEIGHT);
  lv_obj_t *c = v.column;
  v.time = label(c, &lcd7_text28, RADAR_PAD, 10);
  v.date = label(c, &lcd7_text15, RADAR_PAD, 48, RADAR_CONTENT);
  v.outside = label(c, &lcd7_text22, RADAR_PAD, 70);
  v.lines[0] = line(c, RADAR_PAD, 106, RADAR_CONTENT, 1);
  v.range = label(c, &lcd7_text28, RADAR_PAD, 116, RADAR_CONTENT);
  v.frame = label(c, &lcd7_text18, RADAR_PAD, 156, RADAR_CONTENT);
  lv_label_set_recolor(v.frame, true);
  for (lv_obj_t *&dot : v.frameDots) {
    dot = panel(c, 0, 0, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
  }
  v.lines[1] = line(c, RADAR_PAD, 222, RADAR_CONTENT, 1);
  v.warningIcon = label(c, &lv_font_montserrat_16, RADAR_PAD, 236);
  lv_label_set_text(v.warningIcon, LV_SYMBOL_WARNING);
  v.warningEvent = label(c, &lcd7_text18, RADAR_PAD + 24, 234, RADAR_CONTENT - 24);
  v.warningTail = label(c, &lcd7_text15, RADAR_PAD + 24, 258, RADAR_CONTENT - 24);
  v.rain = label(c, &lcd7_text22, RADAR_PAD, 290, RADAR_CONTENT);
  lv_label_set_long_mode(v.rain, LV_LABEL_LONG_WRAP);
  v.icons = makeStatusIcons(c);
  v.dots = makePageDots(c);
}

void radarFrameChanged(const uint16_t *pixels) {
  if (!created) return;
  RadarView &v = radar;
  if (pixels != nullptr && pixels != v.pixels) {
    lv_canvas_set_buffer(v.canvas, const_cast<uint16_t *>(pixels),
                         CHMI_RADAR_WIDTH, CHMI_RADAR_HEIGHT, LV_IMG_CF_TRUE_COLOR);
    v.pixels = pixels;
  }
  if (pixels != nullptr && !lv_obj_has_flag(v.root, LV_OBJ_FLAG_HIDDEN))
    lv_obj_invalidate(v.canvas);
}

void syncRadar(const Palette &p) {
  RadarView &v = radar;
  const bool haveFrame = v.pixels != nullptr &&
                         !lv_obj_has_flag(radarCanvas, LV_OBJ_FLAG_HIDDEN);
  setVisible(v.canvas, haveFrame);
  setVisible(v.status, !haveFrame);
  setText(v.status, lv_label_get_text(radarStatusLabel));
  setColor(v.status, p.muted);

  setText(v.time, displayedTimeText);
  setColor(v.time, p.text);
  setText(v.date, lv_label_get_text(dateLabel));
  setColor(v.date, p.muted);
  char number[16];
  char text[48];
  formatValue(number, sizeof(number), currentValues.outsideTemperatureC, 1);
  snprintf(text, sizeof(text), "%s °C", number);
  setText(v.outside, text);
  setColor(v.outside, p.night ? COLOR_ERROR : COLOR_OUTSIDE);
  setVisible(v.outside, !std::isnan(currentValues.outsideTemperatureC));

  setText(v.range, haveFrame ? lv_label_get_text(radarRangeLabel) : "");
  setColor(v.range, p.night ? COLOR_ERROR : COLOR_OUTSIDE);
  setText(v.frame, haveFrame ? lv_label_get_text(radarTitleLabel) : "");
  setColor(v.frame, p.text);

  // Snímky jako tečky po osmi v řadě, právě zobrazený větší a zvýrazněný.
  const uint8_t count = std::min<uint8_t>(radarShownFrameCount, RADAR_FRAME_DOTS);
  for (uint8_t index = 0; index < RADAR_FRAME_DOTS; ++index) {
    lv_obj_t *dot = v.frameDots[index];
    if (count < 2 || index >= count) {
      setVisible(dot, false);
      continue;
    }
    const bool current = index + 1 == radarShownFrameNumber;
    const lv_coord_t size = current ? 11 : 7;
    if (lv_obj_get_width(dot) != size) lv_obj_set_size(dot, size, size);
    setBackground(dot, current ? (p.night ? COLOR_ERROR : COLOR_AIR) : p.dotOff);
    const lv_coord_t centerX =
        RADAR_PAD + 4 + (index % RADAR_DOTS_PER_ROW) * RADAR_DOT_GAP;
    const lv_coord_t centerY = 192 + (index / RADAR_DOTS_PER_ROW) * RADAR_DOT_GAP;
    setPosition(dot, centerX - size / 2, centerY - size / 2);
    setVisible(dot, true);
  }

  showWarningAndRain(v.warningIcon, v.warningEvent, v.warningTail, v.rain, p);
  for (lv_obj_t *divider : v.lines) setBackground(divider, p.line);
  showStatusIcons(v.icons, p, RADAR_PAD, SCREEN_HEIGHT - 58, false);
  showPageDots(v.dots, p, -(RADAR_PAD + 5), SCREEN_HEIGHT - 22);
}

// --- Přepínání ------------------------------------------------------------

// Objekty a texty obrazovek 7" patří do PSRAM, ne do interní RAM: je jich
// přes dvě stě a interní RAM potřebuje Wi-Fi a TLS (handshake si bere dva
// šestnáctikilobajtové buffery). Bez toho zbylo 10 kB a hodiny vypadly ze sítě.
class PsramAllocations {
 public:
  PsramAllocations() { clockLvglPreferPsram(true); }
  ~PsramAllocations() { clockLvglPreferPsram(false); }
  PsramAllocations(const PsramAllocations &) = delete;
  PsramAllocations &operator=(const PsramAllocations &) = delete;
};

HomeMode shownMode = HomeMode::None;
uint32_t lastSyncAt = 0;

HomeMode desiredMode() {
  if (settingsVisible || firmwareUpdateActive) return HomeMode::None;
  if (activeScreen == DASHBOARD_SCREEN_FORECAST && forecastPage != nullptr)
    return HomeMode::Forecast;
  if (activeScreen == DASHBOARD_SCREEN_RADAR && radarPage != nullptr)
    return HomeMode::Radar;
  if (activeScreen != DASHBOARD_SCREEN_CLOCK) return HomeMode::None;
  if (valuesLayoutEnabled()) return HomeMode::Values;
  if (analogLayoutEnabled()) return HomeMode::Analog;
  return HomeMode::Digital;
}

void create() {
  const PsramAllocations psram;
  allocateHistory();
  lv_obj_t *screen = lv_scr_act();
  createDigital(screen);
  createAnalog(screen);
  createValues(screen);
  createForecast(screen);
  createRadar(screen);
  for (lv_obj_t *root :
       {digital.root, analog.root, values.root, forecast.root, radar.root})
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
  created = true;
}

// Celoplošné obrazovky překryjí jeviště; analogový ciferník je jeviště samo,
// jen posunuté k levému okraji.
void applyMode(HomeMode mode) {
  lv_obj_t *stage = displayDriverStage();
  setVisible(digital.root, mode == HomeMode::Digital);
  setVisible(analog.root, mode == HomeMode::Analog);
  setVisible(values.root, mode == HomeMode::Values);
  setVisible(forecast.root, mode == HomeMode::Forecast);
  if (mode == HomeMode::Forecast) forecast.signature = 0;
  if (mode == HomeMode::Radar && radar.pixels != nullptr) lv_obj_invalidate(radar.canvas);
  setVisible(radar.root, mode == HomeMode::Radar);
  const bool stageLeft = mode == HomeMode::Analog;
  setVisible(stage, mode == HomeMode::None || stageLeft);
  setPosition(stage, stageLeft ? 0 : STAGE_X, STAGE_Y);
  shownMode = mode;
}

void sync(bool force) {
  if (!created) return;
  const PsramAllocations psram;
  maintainHistory();
  const HomeMode mode = desiredMode();
  if (mode != shownMode) {
    applyMode(mode);
    force = true;
  }
  // Kulatý kód při každé změně hodnot odkrývá své popisky v ciferníku; skrýt
  // je musí ještě před vykreslením, ne až s další synchronizací.
  if (mode == HomeMode::Analog) hideRoundDialContent();
  const uint32_t now = millis();
  if (!force && now - lastSyncAt < SYNC_INTERVAL_MS) return;
  lastSyncAt = now;
  const Palette p = palette();
  switch (mode) {
    case HomeMode::Digital: syncDigital(p); break;
    case HomeMode::Analog: syncAnalog(p); break;
    case HomeMode::Values: syncValues(p); break;
    case HomeMode::Forecast: syncForecast(p); break;
    case HomeMode::Radar: syncRadar(p); break;
    case HomeMode::None: break;
  }
}

}  // namespace lcd7

#endif  // HODINY_BOARD_LCD7
