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

enum class HomeMode : uint8_t { None, Digital, Analog, Values, Forecast, Radar,
                               Agenda, Rss, School, Planes,
                               RoundLeft, Traffic };
HomeMode shownMode = HomeMode::None;

constexpr int SIDE_X = 480;
constexpr int SIDE_WIDTH = SCREEN_WIDTH - SIDE_X;
constexpr int HOME_DOT_GAP = 18;
// Úzký sloupec 160 px radarů: deset teček se musí vejít do 132 px.
constexpr int NARROW_DOT_GAP = 13;
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
// Aktuální krok dosahu letadel, žlutá jako tečka dosahu na kulatém.
const lv_color_t COLOR_HIGH_DOT = LV_COLOR_MAKE(255, 255, 0);

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
                  lv_coord_t centerY, lv_coord_t gap = HOME_DOT_GAP) {
  uint8_t count = 0;
  uint8_t activeIndex = 0;
  for (uint8_t position = 0; position < SCREEN_DOT_COUNT; ++position) {
    const uint8_t screen = clockConfigScreenAt(dashboardRuntimeConfig, position);
    if (!screenAvailable(screen)) continue;
    if (screen == activeScreen) activeIndex = count;
    ++count;
  }
  const bool visible = count > 1;
  const lv_coord_t span = (count - 1) * gap;
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
    setPosition(dot, first + index * gap - size / 2,
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
  // Kulatá stránka má vlastní řádek s datem, časem a teplotou; na 7" je
  // totéž v liště nahoře, takže se dole neopakuje.
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
// Obraz 640 x 480 skládá ChmiRadarService ve sdíleném snímku (zvětšené
// srážky, ostrá mapa a blesky); tady se jen ukáže na vlastním plátně. Popisky
// kulaté stránky (čas, rozsah, snímek) přebírá sloupec vpravo.

// Mapy radarů leží pod lištou.
constexpr int MAP_TOP = BAR_HEIGHT;
constexpr int RADAR_COLUMN_X = CHMI_RADAR_VIEW_WIDTH;
constexpr int RADAR_COLUMN_W = SCREEN_WIDTH - RADAR_COLUMN_X;
constexpr int RADAR_PAD = 14;
constexpr int RADAR_CONTENT = RADAR_COLUMN_W - 2 * RADAR_PAD;
constexpr int RADAR_FRAME_DOTS = 16;
constexpr int RADAR_DOTS_PER_ROW = 8;
constexpr int RADAR_DOT_GAP = 16;

struct RadarView {
  lv_obj_t *root;
  TopBar bar;
  lv_obj_t *canvas;
  lv_obj_t *status;
  lv_obj_t *column;
  lv_obj_t *lines[1];
  lv_obj_t *range;
  lv_obj_t *frame;
  lv_obj_t *frameDots[RADAR_FRAME_DOTS];
  lv_obj_t *warningIcon;
  lv_obj_t *warningEvent;
  lv_obj_t *warningTail;
  lv_obj_t *rain;
  StatusIcons icons;
  const uint16_t *pixels;
} radar;

void createRadar(lv_obj_t *screen) {
  RadarView &v = radar;
  v.root = panel(screen, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
  v.bar = makeTopBar(v.root);
  v.canvas = lv_canvas_create(v.root);
  lv_obj_set_pos(v.canvas, 0, MAP_TOP);
  lv_obj_add_flag(v.canvas, LV_OBJ_FLAG_HIDDEN);
  v.status = label(v.root, &lcd7_text22, 0, SCREEN_HEIGHT / 2 - 14, RADAR_COLUMN_X);
  lv_obj_set_style_text_align(v.status, LV_TEXT_ALIGN_CENTER, 0);
  // Sloupec pod lištou: čas, datum a tečky obrazovek jsou v liště.
  v.column = panel(v.root, RADAR_COLUMN_X, MAP_TOP + 1, RADAR_COLUMN_W,
                   SCREEN_HEIGHT - MAP_TOP - 1);
  lv_obj_t *c = v.column;
  v.range = label(c, &lcd7_text28, RADAR_PAD, 10, RADAR_CONTENT);
  v.frame = label(c, &lcd7_text18, RADAR_PAD, 50, RADAR_CONTENT);
  lv_label_set_recolor(v.frame, true);
  for (lv_obj_t *&dot : v.frameDots) {
    dot = panel(c, 0, 0, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
  }
  v.lines[0] = line(c, RADAR_PAD, 116, RADAR_CONTENT, 1);
  v.warningIcon = label(c, &lv_font_montserrat_16, RADAR_PAD, 130);
  lv_label_set_text(v.warningIcon, LV_SYMBOL_WARNING);
  v.warningEvent = label(c, &lcd7_text18, RADAR_PAD + 24, 128, RADAR_CONTENT - 24);
  v.warningTail = label(c, &lcd7_text15, RADAR_PAD + 24, 152, RADAR_CONTENT - 24);
  v.rain = label(c, &lcd7_text22, RADAR_PAD, 184, RADAR_CONTENT);
  lv_label_set_long_mode(v.rain, LV_LABEL_LONG_WRAP);
  v.icons = makeStatusIcons(c);
}

void radarFrameChanged(const uint16_t *pixels) {
  if (!created) return;
  RadarView &v = radar;
  if (pixels != nullptr && pixels != v.pixels) {
    lv_canvas_set_buffer(v.canvas, const_cast<uint16_t *>(pixels),
                         CHMI_RADAR_VIEW_WIDTH, CHMI_RADAR_VIEW_HEIGHT,
                         LV_IMG_CF_TRUE_COLOR);
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
  syncTopBar(v.bar, p);

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
    const lv_coord_t centerY = 86 + (index / RADAR_DOTS_PER_ROW) * RADAR_DOT_GAP;
    setPosition(dot, centerX - size / 2, centerY - size / 2);
    setVisible(dot, true);
  }

  showWarningAndRain(v.warningIcon, v.warningEvent, v.warningTail, v.rain, p);
  for (lv_obj_t *divider : v.lines) setBackground(divider, p.line);
  showStatusIcons(v.icons, p, RADAR_PAD, SCREEN_HEIGHT - MAP_TOP - 34, false);
}

// --- Agenda ---------------------------------------------------------------
// Pod lištou dva sloupce: vlevo první dva dny větším písmem (dnes a zítra,
// na které se člověk dívá nejčastěji), vpravo další dny menším. Když se pravý
// sloupec nevejde a levý má místo, přejde doleva i další den. Den delší než
// levý sloupec pokračuje vpravo pod zopakovanou hlavičkou. Co se nevejde
// nikam, nahradí tři tečky na posledním řádku. Dole legenda kalendářů
// s barevnými tečkami a kdy server kalendáře naposled stáhl.
//
// Texty drží popisky kulaté stránky agendy (kulatý je na 7" nerozmisťuje
// a nezkracuje), tady se jen čtou.

constexpr int AGENDA_X = 28;
constexpr int AGENDA_TOP = BAR_HEIGHT + 18;
constexpr int AGENDA_LEGEND_H = 40;
constexpr int AGENDA_BOTTOM = SCREEN_HEIGHT - AGENDA_LEGEND_H;
constexpr int AGENDA_COLUMN_GAP_7 = 40;
constexpr int AGENDA_LEFT_W = 352;
constexpr int AGENDA_DIVIDER_X = AGENDA_X + AGENDA_LEFT_W + AGENDA_COLUMN_GAP_7 / 2;
constexpr int AGENDA_RIGHT_X = AGENDA_DIVIDER_X + 32;
constexpr int AGENDA_RIGHT_W = SCREEN_WIDTH - AGENDA_X - AGENDA_RIGHT_X;
constexpr int AGENDA_GROUP_GAP = 14;
constexpr size_t AGENDA_ROWS = CLOCK_AGENDA_MAX_ITEMS;

struct AgendaColumnStyle {
  lv_coord_t x;
  lv_coord_t width;
  lv_coord_t timeWidth;
  lv_coord_t timeGap;
  const lv_font_t *font;
  lv_coord_t rowGap;
  lv_coord_t headGap;
};

const AgendaColumnStyle AGENDA_COLUMNS[2] = {
    {AGENDA_X, AGENDA_LEFT_W, 64, 16, &lcd7_text22, 4, 6},
    {AGENDA_RIGHT_X, AGENDA_RIGHT_W, 54, 14, &lcd7_text18, 4, 5},
};

struct AgendaView {
  lv_obj_t *root;
  TopBar bar;
  lv_obj_t *divider;
  lv_obj_t *status;
  lv_obj_t *heads[AGENDA_ROWS];
  lv_obj_t *times[AGENDA_ROWS];
  lv_obj_t *titles[AGENDA_ROWS];
  lv_obj_t *legendDots[AGENDA_MAX_CALENDARS];
  lv_obj_t *legendNames[AGENDA_MAX_CALENDARS];
  lv_obj_t *updated;
  uint32_t drawnRevision;
  bool drawnNight;
  bool drawnEnglish;
} agenda;

// Kam padne každý řádek: sloupec, svislá poloha, jestli nad ním stojí
// hlavička (začátek dne nebo pokračování v pravém sloupci) a jestli místo
// události nese tři tečky.
struct AgendaPlacement {
  int8_t column[AGENDA_ROWS];
  lv_coord_t y[AGENDA_ROWS];
  bool head[AGENDA_ROWS];
  bool more[AGENDA_ROWS];
  bool overflow;
};

lv_coord_t agendaRowHeight(uint8_t column) {
  const AgendaColumnStyle &style = AGENDA_COLUMNS[column];
  return lv_font_get_line_height(style.font) + style.rowGap;
}

lv_coord_t agendaHeadHeight(uint8_t column) {
  return lv_font_get_line_height(&lcd7_text15) + AGENDA_COLUMNS[column].headGap;
}

// Rozmístí řádky, když levý sloupec bere prvních "leftGroups" dnů.
AgendaPlacement placeAgenda(uint8_t count, uint8_t leftGroups) {
  AgendaPlacement result = {};
  lv_coord_t cursor[2] = {AGENDA_TOP, AGENDA_TOP};
  bool started[2] = {false, false};
  int8_t group = -1;
  uint8_t column = 0;
  int lastShown = -1;
  for (uint8_t index = 0; index < count; ++index) {
    const bool startsDay = agendaRowStartsDay[index] || index == 0;
    if (startsDay) ++group;
    if (column == 0 && group >= leftGroups) column = 1;
    // Řádek se musí vejít celý; v levém sloupci i s hlavičkou dne.
    for (;;) {
      // Pravý sloupec začíná vždycky hlavičkou, i když den pokračuje zleva.
      const bool head = startsDay || !started[column];
      const lv_coord_t gap = head && started[column] ? AGENDA_GROUP_GAP : 0;
      const lv_coord_t headHeight = head ? agendaHeadHeight(column) : 0;
      const lv_coord_t bottom = cursor[column] + gap + headHeight + agendaRowHeight(column);
      if (bottom <= AGENDA_BOTTOM) {
        result.head[index] = head;
        result.column[index] = column;
        result.y[index] = cursor[column] + gap + headHeight;
        cursor[column] = bottom;
        started[column] = true;
        lastShown = index;
        break;
      }
      if (column == 0) {
        column = 1;
        continue;
      }
      result.overflow = true;
      break;
    }
    if (result.overflow) break;
  }
  for (uint8_t index = lastShown + 1; index < AGENDA_ROWS; ++index)
    result.column[index] = -1;
  // Tři tečky místo posledního řádku; hlavička nad ním zůstane, pokud
  // nestojí sama bez jediné události.
  if (result.overflow && lastShown >= 0) {
    result.more[lastShown] = true;
    if (result.head[lastShown] && lastShown > 0) result.head[lastShown] = false;
  }
  return result;
}

AgendaPlacement layoutAgenda(uint8_t count) {
  uint8_t groups = 0;
  for (uint8_t index = 0; index < count; ++index)
    if (agendaRowStartsDay[index] || index == 0) ++groups;
  uint8_t leftGroups = std::min<uint8_t>(2, groups);
  AgendaPlacement placement = placeAgenda(count, leftGroups);
  // Další den doleva, dokud se tím zbavíme přetečení a vlevo se vejde celý.
  while (placement.overflow && leftGroups < groups) {
    AgendaPlacement wider = placeAgenda(count, leftGroups + 1);
    bool leftFits = true;
    uint8_t group = 0;
    for (uint8_t index = 0; index < count; ++index) {
      if (index > 0 && agendaRowStartsDay[index]) ++group;
      if (group <= leftGroups && wider.column[index] != 0) leftFits = false;
    }
    if (!leftFits) break;
    ++leftGroups;
    placement = wider;
  }
  return placement;
}

void setFont(lv_obj_t *object, const lv_font_t *font) {
  if (lv_obj_get_style_text_font(object, LV_PART_MAIN) != font)
    lv_obj_set_style_text_font(object, font, LV_PART_MAIN);
}

void setWidth(lv_obj_t *object, lv_coord_t width) {
  if (lv_obj_get_width(object) != width) lv_obj_set_width(object, width);
}

void createAgenda(lv_obj_t *screen) {
  AgendaView &v = agenda;
  v.root = panel(screen, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
  v.bar = makeTopBar(v.root);
  v.divider = line(v.root, AGENDA_DIVIDER_X, AGENDA_TOP, 1, AGENDA_BOTTOM - AGENDA_TOP);
  v.status = label(v.root, &lcd7_text22, 0, (AGENDA_TOP + AGENDA_BOTTOM) / 2 - 14,
                   SCREEN_WIDTH);
  lv_obj_set_style_text_align(v.status, LV_TEXT_ALIGN_CENTER, 0);
  for (size_t index = 0; index < AGENDA_ROWS; ++index) {
    v.heads[index] = label(v.root, &lcd7_text15, 0, 0, 1);
    lv_obj_set_style_text_letter_space(v.heads[index], 2, 0);
    v.times[index] = label(v.root, &lcd7_text22, 0, 0);
    v.titles[index] = label(v.root, &lcd7_text22, 0, 0, 1);
  }
  for (size_t index = 0; index < AGENDA_MAX_CALENDARS; ++index) {
    v.legendDots[index] = panel(v.root, 0, 0, 9, 9);
    lv_obj_set_style_radius(v.legendDots[index], LV_RADIUS_CIRCLE, 0);
    v.legendNames[index] = label(v.root, &lcd7_text18, 0, 0);
  }
  v.updated = label(v.root, &lcd7_text15, 0, 0);
  v.drawnRevision = UINT32_MAX;
}

// "DNES" a "TODAY" mají světlejší hlavičku, ostatní dny tlumenou.
bool agendaDayIsToday(const char *day) {
  return strcmp(day, "DNES") == 0 || strcmp(day, "TODAY") == 0;
}

// Den, ke kterému řádek patří (popisek nese jen první řádek dne).
const char *agendaDayOf(uint8_t index) {
  while (index > 0 && !agendaRowStartsDay[index]) --index;
  return lv_label_get_text(agendaDayLabels[index]);
}

void syncAgendaLegend(const Palette &p) {
  AgendaView &v = agenda;
  constexpr lv_coord_t GAP = 24;
  constexpr lv_coord_t DOT_GAP = 7;
  const lv_coord_t centerY = AGENDA_BOTTOM + AGENDA_LEGEND_H / 2;
  lv_coord_t x = AGENDA_X;
  for (size_t index = 0; index < AGENDA_MAX_CALENDARS; ++index) {
    const bool shown = index < agendaCalendarCount && agendaCalendarNames[index][0] != '\0';
    setVisible(v.legendDots[index], shown);
    setVisible(v.legendNames[index], shown);
    if (!shown) continue;
    const lv_color_t color = agendaCalendarColor(static_cast<uint8_t>(index));
    setBackground(v.legendDots[index], color);
    setPosition(v.legendDots[index], x, centerY - 4);
    setText(v.legendNames[index], agendaCalendarNames[index]);
    setColor(v.legendNames[index], color);
    setPosition(v.legendNames[index], x + 9 + DOT_GAP,
                centerY - lv_font_get_line_height(&lcd7_text18) / 2);
    lv_obj_update_layout(v.legendNames[index]);
    x += 9 + DOT_GAP + lv_obj_get_width(v.legendNames[index]) + GAP;
  }
  char text[AGENDA_UPDATED_LENGTH + 24];
  if (agendaUpdated[0] != '\0')
    formatUpdatedText(agendaUpdated, text, sizeof(text));
  else
    text[0] = '\0';
  setText(v.updated, text);
  setColor(v.updated, p.muted);
  lv_obj_update_layout(v.updated);
  setPosition(v.updated, SCREEN_WIDTH - AGENDA_X - lv_obj_get_width(v.updated),
              centerY - lv_font_get_line_height(&lcd7_text15) / 2);
}

void syncAgenda(const Palette &p) {
  AgendaView &v = agenda;
  syncTopBar(v.bar, p);
  const bool english = englishLanguage();
  if (v.drawnRevision == agendaRevision && v.drawnNight == p.night &&
      v.drawnEnglish == english)
    return;
  v.drawnRevision = agendaRevision;
  v.drawnNight = p.night;
  v.drawnEnglish = english;

  const uint8_t count = agendaVisibleItemCount;
  setVisible(v.status, count == 0);
  setText(v.status, lv_label_get_text(agendaStatusLabel));
  setColor(v.status, p.muted);
  const AgendaPlacement placement = layoutAgenda(count);
  bool rightUsed = false;
  for (uint8_t index = 0; index < AGENDA_ROWS; ++index) {
    const int8_t column = index < count ? placement.column[index] : -1;
    lv_obj_t *head = v.heads[index];
    lv_obj_t *time = v.times[index];
    lv_obj_t *title = v.titles[index];
    if (column < 0) {
      setVisible(head, false);
      setVisible(time, false);
      setVisible(title, false);
      continue;
    }
    if (column == 1) rightUsed = true;
    const AgendaColumnStyle &style = AGENDA_COLUMNS[column];
    const lv_coord_t y = placement.y[index];
    const char *day = agendaDayOf(index);
    setVisible(head, placement.head[index]);
    if (placement.head[index]) {
      setText(head, day);
      setColor(head, agendaDayIsToday(day) ? p.text : p.muted);
      setWidth(head, style.width);
      setPosition(head, style.x, y - agendaHeadHeight(column));
    }
    const bool more = placement.more[index];
    setVisible(time, !more);
    setFont(time, style.font);
    const char *timeText = lv_label_get_text(agendaTimeLabels[index]);
    setText(time, strcmp(timeText, AGENDA_ALL_DAY_MARK) == 0 ? "–" : timeText);
    setColor(time, p.night ? COLOR_ERROR : agendaCalendarColor(agendaRowCalendar[index]));
    setPosition(time, style.x, y);
    setVisible(title, true);
    setFont(title, style.font);
    setText(title, more ? "…" : lv_label_get_text(agendaTitleLabels[index]));
    setColor(title, more ? p.muted : p.text);
    setWidth(title, style.width - style.timeWidth - style.timeGap);
    setPosition(title, more ? style.x : style.x + style.timeWidth + style.timeGap, y);
  }
  setVisible(v.divider, rightUsed);
  setBackground(v.divider, p.line);
  syncAgendaLegend(p);
}

// --- Zprávy ---------------------------------------------------------------
// Vlevo seznam titulků (čas a dva řádky titulku), vpravo vybraná zpráva celá:
// čas, titulek a shrnutí. Výběr se sám posouvá po RSS_ADVANCE_MS, aby hodiny
// bez obsluhy ukázaly všechny zprávy; klepnutí na titulek ho vybere a podrží.
// Kulatý detail přes celou stránku tu není potřeba - texty bere z jeho
// zásoby rssDetailTexts.

constexpr int RSS_LIST_W = 340;
constexpr int RSS_LIST_TOP = BAR_HEIGHT + 1;
constexpr int RSS_ROW_PAD_X = 20;
constexpr int RSS_DETAIL_X = RSS_LIST_W + 28;
constexpr int RSS_DETAIL_W = SCREEN_WIDTH - RSS_DETAIL_X - 28;
constexpr int RSS_DETAIL_TOP = BAR_HEIGHT + 22;
constexpr int RSS_FOOTER_LINE_Y = SCREEN_HEIGHT - 42;
constexpr uint32_t RSS_ADVANCE_MS = 12000;
constexpr uint32_t RSS_HOLD_MS = 60000;
const lv_color_t RSS_SELECTED = LV_COLOR_MAKE(14, 26, 31);
const lv_color_t RSS_SELECTED_NIGHT = LV_COLOR_MAKE(28, 6, 5);
const lv_color_t RSS_ROW_LINE = LV_COLOR_MAKE(26, 26, 26);

struct RssRow {
  lv_obj_t *box;
  lv_obj_t *time;
  lv_obj_t *title;
};

struct RssView {
  lv_obj_t *root;
  TopBar bar;
  lv_obj_t *divider;
  lv_obj_t *status;
  RssRow rows[CLOCK_RSS_MAX_ITEMS];
  lv_obj_t *time;
  lv_obj_t *title;
  lv_obj_t *summary;
  lv_obj_t *footerLine;
  lv_obj_t *updated;
  lv_obj_t *position;
  uint8_t selected;
  uint32_t selectedAt;
  uint32_t holdUntil;
  uint32_t drawnRevision;
  uint8_t drawnSelected;
  bool drawnNight;
  bool drawnEnglish;
  int64_t drawnMinute;
} rss;

uint8_t rssCount() {
  return rssDetailTexts != nullptr ? rssVisibleItemCount : 0;
}

lv_coord_t rssRowHeight(uint8_t count) {
  return (SCREEN_HEIGHT - RSS_LIST_TOP) / std::max<uint8_t>(count, 5);
}

void createRss(lv_obj_t *screen) {
  RssView &v = rss;
  v.root = panel(screen, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
  v.bar = makeTopBar(v.root);
  for (RssRow &row : v.rows) {
    row.box = panel(v.root, 0, 0, RSS_LIST_W, 1);
    lv_obj_set_style_border_side(row.box, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row.box, 1, 0);
    lv_obj_set_style_border_color(row.box, RSS_ROW_LINE, 0);
    row.time = label(row.box, &lcd7_text15, RSS_ROW_PAD_X, 7);
    lv_obj_set_style_text_letter_space(row.time, 1, 0);
    row.title = label(row.box, &lcd7_text18, RSS_ROW_PAD_X, 30);
    lv_obj_set_width(row.title, RSS_LIST_W - RSS_ROW_PAD_X - 16);
    lv_label_set_long_mode(row.title, LV_LABEL_LONG_DOT);
  }
  v.divider = line(v.root, RSS_LIST_W, RSS_LIST_TOP, 1, SCREEN_HEIGHT - RSS_LIST_TOP);
  v.status = label(v.root, &lcd7_text22, 0, SCREEN_HEIGHT / 2 - 14, SCREEN_WIDTH);
  lv_obj_set_style_text_align(v.status, LV_TEXT_ALIGN_CENTER, 0);
  v.time = label(v.root, &lcd7_text15, RSS_DETAIL_X, RSS_DETAIL_TOP);
  lv_obj_set_style_text_letter_space(v.time, 2, 0);
  v.title = label(v.root, &lcd7_text28, RSS_DETAIL_X, RSS_DETAIL_TOP + 30);
  lv_obj_set_width(v.title, RSS_DETAIL_W);
  lv_label_set_long_mode(v.title, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(v.title, 4, 0);
  v.summary = label(v.root, &lcd7_text22, RSS_DETAIL_X, 0);
  lv_obj_set_width(v.summary, RSS_DETAIL_W);
  lv_obj_set_style_text_line_space(v.summary, 6, 0);
  v.footerLine = line(v.root, RSS_DETAIL_X, RSS_FOOTER_LINE_Y, RSS_DETAIL_W, 1);
  v.updated = label(v.root, &lcd7_text15, RSS_DETAIL_X, RSS_FOOTER_LINE_Y + 12);
  v.position = label(v.root, &lcd7_text15, 0, RSS_FOOTER_LINE_Y + 12);
  v.drawnRevision = UINT32_MAX;
}

// Shrnutí pod titulkem až k čáře patičky: větším písmem, když se vejde celé,
// jinak menším; co se nevejde ani tak, utnou tři tečky na posledním řádku.
void placeRssSummary(const char *text, lv_coord_t top) {
  lv_obj_t *summary = rss.summary;
  const lv_coord_t room = RSS_FOOTER_LINE_Y - 14 - top;
  const lv_font_t *font = &lcd7_text22;
  lv_coord_t lineSpace = 6;
  lv_point_t size;
  lv_txt_get_size(&size, text, font, 0, lineSpace, RSS_DETAIL_W, LV_TEXT_FLAG_NONE);
  if (size.y > room) {
    font = &lcd7_text18;
    lineSpace = 5;
    lv_txt_get_size(&size, text, font, 0, lineSpace, RSS_DETAIL_W, LV_TEXT_FLAG_NONE);
  }
  setFont(summary, font);
  lv_obj_set_style_text_line_space(summary, lineSpace, 0);
  const lv_coord_t lineHeight = lv_font_get_line_height(font) + lineSpace;
  const lv_coord_t lines = std::max<lv_coord_t>(1, (room + lineSpace) / lineHeight);
  lv_label_set_long_mode(summary, LV_LABEL_LONG_DOT);
  lv_obj_set_height(summary, std::min<lv_coord_t>(size.y, lines * lineHeight - lineSpace));
  setText(summary, text);
  setPosition(summary, RSS_DETAIL_X, top);
}

void syncRssDetail(const Palette &p, uint8_t count) {
  RssView &v = rss;
  const bool shown = count > 0;
  for (lv_obj_t *object : {v.time, v.title, v.summary, v.footerLine, v.position})
    setVisible(object, shown);
  if (!shown) return;
  const RssDetailText &item = rssDetailTexts[v.selected];
  setText(v.time, item.time);
  setColor(v.time, p.night ? COLOR_ERROR : COLOR_OUTSIDE);
  const lv_coord_t titleTop = item.time[0] != '\0' ? RSS_DETAIL_TOP + 30 : RSS_DETAIL_TOP;
  setText(v.title, item.title);
  setColor(v.title, p.text);
  setPosition(v.title, RSS_DETAIL_X, titleTop);
  lv_obj_update_layout(v.title);
  const bool haveSummary = item.summary[0] != '\0';
  placeRssSummary(haveSummary ? item.summary
                              : (englishLanguage() ? "The feed sends no text for this story."
                                                   : "Kanál k téhle zprávě text neposílá."),
                  titleTop + lv_obj_get_height(v.title) + 16);
  setColor(v.summary, p.night ? COLOR_ERROR : (haveSummary ? COLOR_TEXT : COLOR_MUTED));
  char text[16];
  snprintf(text, sizeof(text), "%u / %u", static_cast<unsigned>(v.selected + 1),
           static_cast<unsigned>(count));
  setText(v.position, text);
  setColor(v.position, p.muted);
  lv_obj_update_layout(v.position);
  setPosition(v.position, RSS_DETAIL_X + RSS_DETAIL_W - lv_obj_get_width(v.position),
              RSS_FOOTER_LINE_Y + 12);
  setBackground(v.footerLine, p.line);
}

void syncRss(const Palette &p) {
  RssView &v = rss;
  syncTopBar(v.bar, p);
  const uint8_t count = rssCount();
  const uint32_t now = millis();
  if (v.selected >= count) v.selected = 0;
  if (count > 1 && now >= v.holdUntil && now - v.selectedAt >= RSS_ADVANCE_MS) {
    v.selected = static_cast<uint8_t>((v.selected + 1) % count);
    v.selectedAt = now;
  }
  // "aktualizováno" se skládá z času a o půlnoci se přepíše na VČERA.
  char updated[64];
  const bool haveUpdated =
      rssFooterShown && formatUpdatedMoment(rssUpdatedAt, updated, sizeof(updated));
  setVisible(v.updated, haveUpdated && count > 0);
  if (haveUpdated) setText(v.updated, updated);
  setColor(v.updated, p.muted);

  const bool english = englishLanguage();
  if (v.drawnRevision == rssRevision && v.drawnSelected == v.selected &&
      v.drawnNight == p.night && v.drawnEnglish == english)
    return;
  v.drawnRevision = rssRevision;
  v.drawnSelected = v.selected;
  v.drawnNight = p.night;
  v.drawnEnglish = english;

  setVisible(v.status, count == 0);
  setText(v.status, lv_label_get_text(rssStatusLabel));
  setColor(v.status, p.night ? COLOR_ERROR : COLOR_OUTSIDE);
  setVisible(v.divider, count > 0);
  setBackground(v.divider, p.line);
  const lv_coord_t rowHeight = rssRowHeight(count);
  const lv_coord_t lineHeight = lv_font_get_line_height(&lcd7_text18);
  for (uint8_t index = 0; index < CLOCK_RSS_MAX_ITEMS; ++index) {
    RssRow &row = v.rows[index];
    setVisible(row.box, index < count);
    if (index >= count) continue;
    const RssDetailText &item = rssDetailTexts[index];
    const bool selected = index == v.selected;
    if (lv_obj_get_height(row.box) != rowHeight) lv_obj_set_height(row.box, rowHeight);
    setPosition(row.box, 0, RSS_LIST_TOP + index * rowHeight);
    setBackground(row.box, selected ? (p.night ? RSS_SELECTED_NIGHT : RSS_SELECTED)
                                    : COLOR_BACKGROUND);
    setBorder(row.box, p.night ? NIGHT_LINE : RSS_ROW_LINE);
    setText(row.time, item.time);
    setColor(row.time, p.night ? COLOR_ERROR : COLOR_OUTSIDE);
    setVisible(row.time, item.time[0] != '\0');
    // Bez času začíná titulek nahoře; řádků titulku je tolik, kolik se do
    // řádku seznamu vejde (u šesti zpráv dva, u pěti i bez času tři).
    const lv_coord_t titleTop = item.time[0] != '\0' ? 25 : 8;
    setPosition(row.title, RSS_ROW_PAD_X, titleTop);
    const lv_coord_t lines = std::max<lv_coord_t>(
        1, std::min<lv_coord_t>(3, (rowHeight - titleTop - 3) / lineHeight));
    if (lv_obj_get_height(row.title) != lines * lineHeight)
      lv_obj_set_height(row.title, lines * lineHeight);
    setText(row.title, item.title);
    setColor(row.title, p.night ? (selected ? COLOR_ERROR : NIGHT_MUTED)
                                : (selected ? COLOR_TEXT : COLOR_MUTED));
  }
  syncRssDetail(p, count);
}

void rssTap(int16_t x, int16_t y) {
  if (!created || shownMode != HomeMode::Rss || x >= RSS_LIST_W) return;
  const uint8_t count = rssCount();
  if (count == 0 || y < RSS_LIST_TOP) return;
  const int index = (y - RSS_LIST_TOP) / rssRowHeight(count);
  if (index >= count) return;
  rss.selected = static_cast<uint8_t>(index);
  rss.selectedAt = millis();
  rss.holdUntil = rss.selectedAt + RSS_HOLD_MS;
  sync(true);
}

// --- Škola ----------------------------------------------------------------
// Vlevo rozvrh dnů, které server posílá (dnešek nebo příští školní den a den
// po něm), každý den jako sloupec dlaždic: číslo hodiny, předmět celým
// názvem (zkratka, když se nevejde) a pod ním čas nebo poznámka. Probíhající
// či nejbližší hodina má modrý rámeček a "teď · do 9:40", změněná oranžový,
// odpadlá je přeškrtnutá a tlumená. Vpravo pod sebou obědy, testy, úkoly,
// známky a zprávy, dokud je místo - 7" nepotřebuje druhou a třetí stránku
// kulatého, takže tažení vede rovnou na další obrazovku.

constexpr int SCHOOL7_LEFT = 18;
constexpr int SCHOOL7_LEFT_END = 532;
constexpr int SCHOOL7_COLUMN_GAP = 12;
constexpr int SCHOOL7_TOP = BAR_HEIGHT + 12;
constexpr int SCHOOL7_BOTTOM = SCREEN_HEIGHT - 12;
constexpr int SCHOOL7_HEAD_H = 32;
constexpr int SCHOOL7_ROW_GAP = 6;
constexpr int SCHOOL7_ROW_MAX = 58;
constexpr int SCHOOL7_TWO_LINE_ROW = 48;
constexpr int SCHOOL7_HOUR_W = 34;
constexpr int SCHOOL7_DIVIDER_X = 548;
constexpr int SCHOOL7_SIDE_X = 566;
constexpr int SCHOOL7_SIDE_W = SCREEN_WIDTH - SCHOOL7_SIDE_X - 18;
constexpr int SCHOOL7_SIDE_ROWS = 14;
constexpr int SCHOOL7_SIDE_SECTIONS = 5;
constexpr int SCHOOL7_MARK_BOX = 26;
const lv_color_t SCHOOL7_CELL = LV_COLOR_MAKE(11, 11, 11);
const lv_color_t SCHOOL7_CELL_BORDER = LV_COLOR_MAKE(28, 28, 28);
const lv_color_t SCHOOL7_NOW_FILL = LV_COLOR_MAKE(14, 26, 31);
const lv_color_t SCHOOL7_TEST = LV_COLOR_MAKE(255, 176, 32);
const lv_color_t SCHOOL7_MARK_2 = LV_COLOR_MAKE(255, 194, 26);

struct SchoolCell {
  lv_obj_t *box;
  lv_obj_t *hour;
  lv_obj_t *subject;
  lv_obj_t *sub;
};

struct SchoolSideRow {
  lv_obj_t *text;
  lv_obj_t *markBox;
  lv_obj_t *mark;
};

struct SchoolView {
  lv_obj_t *root;
  TopBar bar;
  lv_obj_t *status;
  lv_obj_t *heads[SCHOOL_MAX_DAYS];
  lv_obj_t *ends[SCHOOL_MAX_DAYS];
  SchoolCell cells[SCHOOL_MAX_DAYS][SCHOOL_MAX_LESSONS];
  lv_obj_t *divider;
  lv_obj_t *sectionHeads[SCHOOL7_SIDE_SECTIONS];
  SchoolSideRow side[SCHOOL7_SIDE_ROWS];
  lv_obj_t *updated;
  uint32_t drawnRevision;
  int drawnMinute;
  bool drawnNight;
  bool drawnEnglish;
} school;

void createSchool(lv_obj_t *screen) {
  SchoolView &v = school;
  v.root = panel(screen, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
  v.bar = makeTopBar(v.root);
  v.status = label(v.root, &lcd7_text22, SCHOOL7_LEFT, SCREEN_HEIGHT / 2 - 14,
                   SCHOOL7_LEFT_END - SCHOOL7_LEFT);
  lv_obj_set_style_text_align(v.status, LV_TEXT_ALIGN_CENTER, 0);
  for (size_t day = 0; day < SCHOOL_MAX_DAYS; ++day) {
    v.heads[day] = label(v.root, &lcd7_text18, 0, SCHOOL7_TOP + 4, 1);
    lv_obj_set_style_text_letter_space(v.heads[day], 1, 0);
    v.ends[day] = label(v.root, &lcd7_text15, 0, SCHOOL7_TOP + 7);
    for (SchoolCell &cell : v.cells[day]) {
      cell.box = panel(v.root, 0, 0, 1, 1);
      lv_obj_set_style_radius(cell.box, 8, 0);
      lv_obj_set_style_border_width(cell.box, 2, 0);
      cell.hour = label(cell.box, &lcd7_text15, 0, 0, SCHOOL7_HOUR_W);
      lv_obj_set_style_text_align(cell.hour, LV_TEXT_ALIGN_RIGHT, 0);
      cell.subject = label(cell.box, &lcd7_text22, 0, 0, 1);
      cell.sub = label(cell.box, &lcd7_text15, 0, 0, 1);
    }
  }
  v.divider = line(v.root, SCHOOL7_DIVIDER_X, SCHOOL7_TOP, 1, SCHOOL7_BOTTOM - SCHOOL7_TOP);
  for (lv_obj_t *&head : v.sectionHeads) {
    head = label(v.root, &lcd7_text15, SCHOOL7_SIDE_X, 0);
    lv_obj_set_style_text_letter_space(head, 2, 0);
  }
  for (SchoolSideRow &row : v.side) {
    row.text = label(v.root, &lcd7_text18, SCHOOL7_SIDE_X, 0, SCHOOL7_SIDE_W);
    lv_label_set_recolor(row.text, true);
    row.markBox = panel(v.root, SCHOOL7_SIDE_X, 0, SCHOOL7_MARK_BOX, SCHOOL7_MARK_BOX);
    lv_obj_set_style_radius(row.markBox, 6, 0);
    row.mark = label(row.markBox, &lcd7_text18, 0, 1, SCHOOL7_MARK_BOX);
    lv_obj_set_style_text_align(row.mark, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(row.mark, COLOR_BACKGROUND, 0);
  }
  v.updated = label(v.root, &lcd7_text15, SCHOOL7_SIDE_X, SCHOOL7_BOTTOM - 18);
  v.drawnRevision = UINT32_MAX;
}

void setDecor(lv_obj_t *object, lv_text_decor_t decor) {
  if (lv_obj_get_style_text_decor(object, LV_PART_MAIN) != decor)
    lv_obj_set_style_text_decor(object, decor, LV_PART_MAIN);
}

void setSize(lv_obj_t *object, lv_coord_t w, lv_coord_t h) {
  if (lv_obj_get_width(object) != w || lv_obj_get_height(object) != h)
    lv_obj_set_size(object, w, h);
}

// "9:40" z minut dne.
void formatMinutes(char *output, size_t capacity, int minutes) {
  snprintf(output, capacity, "%d:%02d", minutes / 60, minutes % 60);
}

void syncSchoolDay(const Palette &p, size_t dayIndex, const SchoolFeed *feed,
                   lv_coord_t x, lv_coord_t width, int minute) {
  SchoolView &v = school;
  const bool present = feed != nullptr && dayIndex < feed->dayCount;
  setVisible(v.heads[dayIndex], present);
  setVisible(v.ends[dayIndex], present);
  const size_t count = present ? feed->days[dayIndex].lessonCount : 0;
  for (size_t index = 0; index < SCHOOL_MAX_LESSONS; ++index)
    setVisible(v.cells[dayIndex][index].box, index < count);
  if (!present) return;
  const SchoolDay &day = feed->days[dayIndex];
  const bool english = englishLanguage();

  char text[SCHOOL_TITLE_LENGTH + 32];
  if (day.weekday[0] != '\0')
    snprintf(text, sizeof(text), "%s · %s", day.day, day.weekday);
  else
    strlcpy(text, day.day, sizeof(text));
  setText(v.heads[dayIndex], text);
  setColor(v.heads[dayIndex], p.night ? COLOR_ERROR : (day.today ? COLOR_OUTSIDE : COLOR_TEXT));
  setPosition(v.heads[dayIndex], x, SCHOOL7_TOP + 4);
  if (day.end[0] != '\0')
    snprintf(text, sizeof(text), english ? "until %s" : "do %s", day.end);
  else
    strlcpy(text, english ? "all off" : "vše odpadá", sizeof(text));
  setText(v.ends[dayIndex], text);
  setColor(v.ends[dayIndex], p.muted);
  lv_obj_update_layout(v.ends[dayIndex]);
  const lv_coord_t endWidth = lv_obj_get_width(v.ends[dayIndex]);
  setPosition(v.ends[dayIndex], x + width - endWidth, SCHOOL7_TOP + 7);
  setWidth(v.heads[dayIndex], std::max<lv_coord_t>(1, width - endWidth - 10));

  // Zvýrazněná hodina jen v dnešním rozvrhu, stejně jako na kulatém.
  SchoolLessonTime times[SCHOOL_MAX_LESSONS];
  for (size_t index = 0; index < count; ++index) times[index] = schoolLessonTime(day.lessons[index]);
  const int current = day.today ? schoolCurrentLesson(times, count, minute) : -1;

  const lv_coord_t top = SCHOOL7_TOP + SCHOOL7_HEAD_H;
  const lv_coord_t room = SCHOOL7_BOTTOM - top;
  const lv_coord_t rowHeight = count == 0 ? 0
      : std::min<lv_coord_t>(SCHOOL7_ROW_MAX,
                             (room + SCHOOL7_ROW_GAP) / count - SCHOOL7_ROW_GAP);
  const bool twoLines = rowHeight >= SCHOOL7_TWO_LINE_ROW;
  const lv_coord_t subjectX = SCHOOL7_HOUR_W + 10;
  const lv_coord_t subjectWidth = width - subjectX - 10;
  for (size_t index = 0; index < count; ++index) {
    const SchoolLesson &lesson = day.lessons[index];
    SchoolCell &cell = v.cells[dayIndex][index];
    const bool canceled = lesson.state == SchoolLessonState::Canceled;
    const bool changed = lesson.state == SchoolLessonState::Changed;
    const bool now = static_cast<int>(index) == current;
    setSize(cell.box, width, rowHeight);
    setPosition(cell.box, x, top + index * (rowHeight + SCHOOL7_ROW_GAP));
    lv_color_t border = SCHOOL7_CELL_BORDER;
    lv_color_t subjectColor = COLOR_TEXT;
    lv_color_t subColor = COLOR_MUTED;
    if (now) border = subColor = COLOR_OUTSIDE;
    else if (changed) border = subjectColor = subColor = COLOR_ROOM;
    if (canceled) subjectColor = COLOR_MUTED;
    if (p.night) {
      border = now || changed ? COLOR_ERROR : NIGHT_LINE;
      subjectColor = canceled ? NIGHT_MUTED : COLOR_ERROR;
      subColor = NIGHT_MUTED;
    }
    setBackground(cell.box, now && !p.night ? SCHOOL7_NOW_FILL : SCHOOL7_CELL);
    setBorder(cell.box, border);

    setText(cell.hour, lesson.hour);
    setColor(cell.hour, p.muted);
    setPosition(cell.hour, 0, (rowHeight - 4) / 2 - 10);

    // Plný název, dokud se vejde; zkratka řekne víc než useknutý název.
    const char *subject = lesson.subject;
    if (lesson.abbrev[0] != '\0' &&
        lv_txt_get_width(subject, strlen(subject), &lcd7_text22, 0, LV_TEXT_FLAG_NONE) >
            subjectWidth)
      subject = lesson.abbrev;
    setText(cell.subject, subject);
    setColor(cell.subject, subjectColor);
    setDecor(cell.subject, canceled ? LV_TEXT_DECOR_STRIKETHROUGH : LV_TEXT_DECOR_NONE);
    setWidth(cell.subject, subjectWidth);

    // Druhý řádek: u probíhající hodiny do kdy, u příští odkdy, jinak
    // poznámka serveru (suplování), u odpadlé "odpadá", jinak čas hodiny.
    char sub[SCHOOL_NOTE_LENGTH + 32];
    char from[8];
    char to[8];
    sub[0] = '\0';
    if (now && times[index].start >= 0) {
      const bool running = minute >= times[index].start;
      if (running && times[index].end >= 0) {
        formatMinutes(to, sizeof(to), times[index].end);
        snprintf(sub, sizeof(sub), english ? "now · until %s" : "teď · do %s", to);
      } else {
        formatMinutes(from, sizeof(from), times[index].start);
        snprintf(sub, sizeof(sub), english ? "next · %s" : "příští · %s", from);
      }
    } else if (lesson.note[0] != '\0') {
      strlcpy(sub, lesson.note, sizeof(sub));
    } else if (canceled) {
      strlcpy(sub, english ? "off" : "odpadá", sizeof(sub));
    } else if (lesson.start[0] != '\0') {
      snprintf(sub, sizeof(sub), "%s – %s", lesson.start, lesson.end);
    }
    const lv_coord_t subjectHeight = lv_font_get_line_height(&lcd7_text22);
    const lv_coord_t subHeight = lv_font_get_line_height(&lcd7_text15);
    if (twoLines && sub[0] != '\0') {
      const lv_coord_t blockTop = (rowHeight - 4 - subjectHeight - subHeight) / 2;
      setPosition(cell.subject, subjectX, blockTop);
      setPosition(cell.sub, subjectX, blockTop + subjectHeight);
      setWidth(cell.sub, subjectWidth);
      setText(cell.sub, sub);
      setColor(cell.sub, subColor);
      setVisible(cell.sub, true);
    } else {
      setPosition(cell.subject, subjectX, (rowHeight - 4 - subjectHeight) / 2);
      setVisible(cell.sub, false);
    }
  }
}

// Text do recolor popisku: mřížka v textu ze serveru se zdvojí.
void appendEscaped(char *output, size_t capacity, const char *text) {
  size_t length = strlen(output);
  for (const char *c = text; *c != '\0' && length + 2 < capacity; ++c) {
    output[length++] = *c;
    if (*c == '#') output[length++] = '#';
  }
  output[length] = '\0';
}

// "#RRGGBB KDY# text" - kdy v barvě sekce, zbytek v barvě textu.
void composeSideRow(char *output, size_t capacity, const char *when,
                    lv_color_t whenColor, const char *first, const char *second) {
  if (redNightVisualEnabled()) whenColor = NIGHT_MUTED;
  output[0] = '\0';
  if (when != nullptr && when[0] != '\0') {
    char tag[AGENDA_COLOR_TAG_LENGTH + 1];
    agendaBuildColorTag(tag, whenColor);
    strlcat(output, tag, capacity);
    appendEscaped(output, capacity, when);
    strlcat(output, "#  ", capacity);
  }
  if (first != nullptr && first[0] != '\0') {
    appendEscaped(output, capacity, first);
    if (second != nullptr && second[0] != '\0') strlcat(output, " · ", capacity);
  }
  if (second != nullptr) appendEscaped(output, capacity, second);
}

lv_color_t markColor(const char *mark) {
  switch (mark[0]) {
    case '1': return COLOR_AIR;
    case '2': return SCHOOL7_MARK_2;
    case '3': return COLOR_ROOM;
    case '4':
    case '5': return COLOR_ERROR;
    default: return COLOR_MUTED;
  }
}

// Pravý sloupec: sekce pod sebou, dokud se vejdou. Řádek se láme nejvýš na
// dva řádky; sekce bez jediného řádku, který by se vešel, se vynechá celá.
class SchoolSide {
 public:
  SchoolSide(const Palette &p) : p_(p), y_(SCHOOL7_TOP + 2) {}

  bool beginSection(const char *title, lv_color_t color) {
    if (section_ >= SCHOOL7_SIDE_SECTIONS) return false;
    const lv_coord_t head = lv_font_get_line_height(&lcd7_text15) + 4;
    const lv_coord_t gap = section_ > 0 ? 14 : 0;
    if (y_ + gap + head + lineHeight() > bottom()) return false;
    lv_obj_t *label = school.sectionHeads[section_++];
    y_ += gap;
    setText(label, title);
    setColor(label, p_.night ? COLOR_ERROR : color);
    setPosition(label, SCHOOL7_SIDE_X, y_);
    setVisible(label, true);
    y_ += head;
    return true;
  }

  // Vrací false, když se řádek už nevešel.
  bool addRow(const char *text, const char *mark = nullptr) {
    if (row_ >= SCHOOL7_SIDE_ROWS) return false;
    SchoolSideRow &row = school.side[row_];
    const lv_coord_t indent = mark != nullptr ? SCHOOL7_MARK_BOX + 10 : 0;
    const lv_coord_t width = SCHOOL7_SIDE_W - indent;
    lv_point_t size;
    lv_txt_get_size(&size, text, &lcd7_text18, 0, 0, width, LV_TEXT_FLAG_RECOLOR);
    lv_coord_t lines = size.y > lineHeight() ? 2 : 1;
    if (y_ + lines * lineHeight() > bottom()) lines = 1;
    if (y_ + lineHeight() > bottom()) return false;
    setWidth(row.text, width);
    if (lv_obj_get_height(row.text) != lines * lineHeight())
      lv_obj_set_height(row.text, lines * lineHeight());
    setText(row.text, text);
    setColor(row.text, p_.night ? COLOR_ERROR : COLOR_TEXT);
    setPosition(row.text, SCHOOL7_SIDE_X + indent, y_);
    setVisible(row.text, true);
    setVisible(row.markBox, mark != nullptr);
    if (mark != nullptr) {
      setText(row.mark, mark);
      setBackground(row.markBox, p_.night ? COLOR_ERROR : markColor(mark));
      setPosition(row.markBox, SCHOOL7_SIDE_X, y_ - 2);
    }
    y_ += lines * lineHeight() + 4;
    ++row_;
    return true;
  }

  void finish() {
    for (size_t index = section_; index < SCHOOL7_SIDE_SECTIONS; ++index)
      setVisible(school.sectionHeads[index], false);
    for (size_t index = row_; index < SCHOOL7_SIDE_ROWS; ++index) {
      setVisible(school.side[index].text, false);
      setVisible(school.side[index].markBox, false);
    }
  }

  bool empty() const { return section_ == 0; }

 private:
  static lv_coord_t lineHeight() { return lv_font_get_line_height(&lcd7_text18); }
  static lv_coord_t bottom() { return SCHOOL7_BOTTOM - 26; }

  const Palette &p_;
  lv_coord_t y_;
  size_t section_ = 0;
  size_t row_ = 0;
};

void syncSchoolSide(const Palette &p, const SchoolFeed *feed, int minute) {
  SchoolSide side(p);
  const bool english = englishLanguage();
  char text[SCHOOL_TITLE_LENGTH * 2 + 48];
  if (feed != nullptr && feed->hasMeals && feed->mealCount > 0) {
    bool today[SCHOOL_MAX_MEALS];
    for (size_t index = 0; index < feed->mealCount; ++index)
      today[index] = feed->meals[index].today;
    const size_t hidden = schoolMealsHiddenByHour(
        today, feed->mealCount, minute, dashboardRuntimeConfig.school.mealNextDayHour);
    if (hidden < feed->mealCount && side.beginSection(english ? "LUNCH" : "OBĚDY", COLOR_AIR)) {
      for (size_t index = hidden; index < feed->mealCount; ++index) {
        const SchoolMeal &meal = feed->meals[index];
        const bool newDay =
            index == hidden || strcmp(meal.when, feed->meals[index - 1].when) != 0;
        char what[SCHOOL_MEAL_WHO_LENGTH + SCHOOL_TITLE_LENGTH + 4];
        if (meal.who[0] != '\0')
          snprintf(what, sizeof(what), "%s: %s", meal.who, meal.text);
        else
          strlcpy(what, meal.text, sizeof(what));
        composeSideRow(text, sizeof(text), newDay ? meal.when : "",
                       COLOR_TEXT, nullptr, what);
        if (!side.addRow(text)) break;
      }
    }
  }
  if (feed != nullptr && feed->hasTests && feed->testCount > 0 &&
      side.beginSection(english ? "TESTS" : "TESTY", SCHOOL7_TEST)) {
    for (size_t index = 0; index < feed->testCount; ++index) {
      const SchoolTest &test = feed->tests[index];
      composeSideRow(text, sizeof(text), test.when,
                     test.done ? COLOR_MUTED : SCHOOL7_TEST,
                     test.abbrev[0] != '\0' ? test.abbrev : test.subject, test.theme);
      if (!side.addRow(text)) break;
    }
  }
  if (feed != nullptr && feed->hasHomework && feed->homeworkCount > 0 &&
      side.beginSection(english ? "HOMEWORK" : "ÚKOLY", COLOR_OUTSIDE)) {
    for (size_t index = 0; index < feed->homeworkCount; ++index) {
      const SchoolHomework &task = feed->homework[index];
      composeSideRow(text, sizeof(text), task.due, COLOR_OUTSIDE,
                     task.abbrev[0] != '\0' ? task.abbrev : task.subject, task.title);
      if (!side.addRow(text)) break;
    }
  }
  if (feed != nullptr && feed->hasMarks && feed->markCount > 0 &&
      side.beginSection(english ? "MARKS" : "ZNÁMKY", COLOR_MUTED)) {
    for (size_t index = 0; index < feed->markCount; ++index) {
      const SchoolMark &mark = feed->marks[index];
      composeSideRow(text, sizeof(text), "", COLOR_TEXT,
                     mark.abbrev[0] != '\0' ? mark.abbrev : mark.subject, mark.theme);
      if (!side.addRow(text, mark.mark)) break;
    }
  }
  const bool messages = feed != nullptr && feed->hasMessages && feed->messageCount > 0;
  const bool notices = feed != nullptr && feed->hasNotices && feed->noticeCount > 0;
  if ((messages || notices) &&
      side.beginSection(english ? "MESSAGES" : "ZPRÁVY", COLOR_MUTED)) {
    bool room = true;
    for (size_t index = 0; messages && room && index < feed->messageCount; ++index) {
      const SchoolMessage &message = feed->messages[index];
      composeSideRow(text, sizeof(text), message.when, COLOR_MUTED, message.sender,
                     message.title);
      room = side.addRow(text);
    }
    for (size_t index = 0; notices && room && index < feed->noticeCount; ++index) {
      const SchoolNotice &notice = feed->notices[index];
      composeSideRow(text, sizeof(text), notice.when, COLOR_MUTED, nullptr, notice.title);
      room = side.addRow(text);
    }
  }
  side.finish();
  setVisible(school.divider, !side.empty());
  setBackground(school.divider, p.line);

  const bool haveUpdated = feed != nullptr && feed->timetableUpdated[0] != '\0';
  setVisible(school.updated, haveUpdated);
  if (haveUpdated) {
    formatUpdatedText(feed->timetableUpdated, text, sizeof(text));
    setText(school.updated, text);
  }
  setColor(school.updated, p.muted);
}

void syncSchool(const Palette &p) {
  SchoolView &v = school;
  syncTopBar(v.bar, p);
  const int minute = schoolMinuteOfDay();
  const bool english = englishLanguage();
  if (v.drawnRevision == schoolRevision && v.drawnMinute == minute &&
      v.drawnNight == p.night && v.drawnEnglish == english)
    return;
  v.drawnRevision = schoolRevision;
  v.drawnMinute = minute;
  v.drawnNight = p.night;
  v.drawnEnglish = english;

  const SchoolFeed *feed = schoolReady ? schoolLcd7Feed : nullptr;
  const size_t days = feed != nullptr ? std::min(feed->dayCount, SCHOOL_MAX_DAYS) : 0;
  const lv_coord_t area = SCHOOL7_LEFT_END - SCHOOL7_LEFT;
  const lv_coord_t width =
      days > 1 ? (area - SCHOOL7_COLUMN_GAP * (days - 1)) / days : area;
  for (size_t day = 0; day < SCHOOL_MAX_DAYS; ++day)
    syncSchoolDay(p, day, feed, SCHOOL7_LEFT + day * (width + SCHOOL7_COLUMN_GAP), width,
                  minute);

  setVisible(v.status, days == 0);
  if (days == 0) {
    const char *text;
    if (!schoolReady && schoolMessage[0] != '\0')
      text = schoolMessage;
    else if (schoolReady)
      text = english ? "No school coming up" : "Žádné vyučování";
    else
      text = english ? "Loading timetable..." : "Načítám rozvrh...";
    setText(v.status, text);
    setColor(v.status, p.muted);
  }
  syncSchoolSide(p, feed, minute);
}

// --- Radar letadel ----------------------------------------------------------
// Mapa 640 x 480 kreslí služba radaru letadel (obdélník místo kruhu, stupnice
// výšek vlevo dole). Sloupec vpravo nese to, co kulatý píše přes mapu: čas,
// dosah s tečkami kroků, počet letadel nebo nouzi a detail vybraného letadla.
// Detail bere z popisků kulatého panelu, který se na 7" sám nikdy neukáže.

constexpr int PLANES_COLUMN_X = PLANE_RADAR_WIDTH;
constexpr int PLANES_COLUMN_W = SCREEN_WIDTH - PLANES_COLUMN_X;
constexpr int PLANES_PAD = 14;
constexpr int PLANES_CONTENT = PLANES_COLUMN_W - 2 * PLANES_PAD;
constexpr int PLANES_DETAIL_Y = 118;
constexpr int PLANES_DETAIL_LINES = PLANES_DETAIL_ROW_COUNT + 4;

struct PlanesView {
  lv_obj_t *root;
  TopBar bar;
  lv_obj_t *canvas;
  lv_obj_t *column;
  lv_obj_t *lines[1];
  lv_obj_t *range;
  lv_obj_t *rangeDots[CLOCK_PLANE_RANGE_COUNT];
  lv_obj_t *status;
  lv_obj_t *title;
  lv_obj_t *detail[PLANES_DETAIL_LINES];
  lv_obj_t *values[PLANES_DETAIL_LINES];
  lv_obj_t *hint;
  StatusIcons icons;
  const uint16_t *pixels;
} planes;

void createPlanes(lv_obj_t *screen) {
  PlanesView &v = planes;
  v.root = panel(screen, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
  v.bar = makeTopBar(v.root);
  v.canvas = lv_canvas_create(v.root);
  lv_obj_set_pos(v.canvas, 0, MAP_TOP);
  lv_obj_add_flag(v.canvas, LV_OBJ_FLAG_HIDDEN);
  // Sloupec pod lištou: čas, datum a tečky obrazovek jsou v liště.
  v.column = panel(v.root, PLANES_COLUMN_X, MAP_TOP + 1, PLANES_COLUMN_W,
                   SCREEN_HEIGHT - MAP_TOP - 1);
  lv_obj_t *c = v.column;
  v.range = label(c, &lcd7_text28, PLANES_PAD, 10, PLANES_CONTENT);
  for (lv_obj_t *&dot : v.rangeDots) {
    dot = panel(c, 0, 0, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  }
  v.status = label(c, &lcd7_text18, PLANES_PAD, 70, PLANES_CONTENT);
  lv_label_set_recolor(v.status, true);
  lv_label_set_long_mode(v.status, LV_LABEL_LONG_WRAP);
  v.lines[0] = line(c, PLANES_PAD, PLANES_DETAIL_Y - 10, PLANES_CONTENT, 1);
  v.title = label(c, &lcd7_text22, PLANES_PAD, PLANES_DETAIL_Y, PLANES_CONTENT);
  for (size_t index = 0; index < PLANES_DETAIL_LINES; ++index) {
    v.detail[index] = label(c, &lcd7_text15, PLANES_PAD, 0, PLANES_CONTENT);
    v.values[index] = label(c, &lcd7_text15, PLANES_PAD, 0, PLANES_CONTENT);
  }
  v.hint = label(c, &lcd7_text15, PLANES_PAD, PLANES_DETAIL_Y, PLANES_CONTENT);
  lv_label_set_long_mode(v.hint, LV_LABEL_LONG_WRAP);
  v.icons = makeStatusIcons(c);
}

void planesFrameChanged(const uint16_t *pixels) {
  if (!created) return;
  PlanesView &v = planes;
  if (pixels != nullptr && pixels != v.pixels) {
    lv_canvas_set_buffer(v.canvas, const_cast<uint16_t *>(pixels), PLANE_RADAR_WIDTH,
                         PLANE_RADAR_HEIGHT, LV_IMG_CF_TRUE_COLOR);
    v.pixels = pixels;
  }
  setVisible(v.canvas, pixels != nullptr);
  if (pixels != nullptr && !lv_obj_has_flag(v.root, LV_OBJ_FLAG_HIDDEN))
    lv_obj_invalidate(v.canvas);
}

// Řádek s počtem letadel, hláškou nebo nouzí; barvy podle palety.
void composePlanesStatus(const Palette &p, char *text, size_t capacity) {
  const bool english = englishLanguage();
  char tag[AGENDA_COLOR_TAG_LENGTH + 1];
  if (planesLcd7.emergency[0] != '\0') {
    agendaBuildColorTag(tag, COLOR_ERROR);
    snprintf(text, capacity, "%s%s  %s#", tag, planesLcd7.emergency,
             planesEmergencyText(planesLcd7.emergency));
  } else if (planesLcd7.message[0] != '\0') {
    agendaBuildColorTag(tag, p.night ? COLOR_ERROR : COLOR_ROOM);
    snprintf(text, capacity, "%s%s#", tag, planesLcd7.message);
  } else if (!planesLcd7.havePixels || !planesLcd7.haveAircraftData) {
    agendaBuildColorTag(tag, p.muted);
    snprintf(text, capacity, "%s%s#", tag,
             planesLcd7.loading ? (english ? "Loading aircraft..." : "Načítám letadla...")
                                : (english ? "Waiting for data" : "Čekám na data"));
  } else {
    agendaBuildColorTag(tag, p.night ? COLOR_ERROR
                                     : (planesLcd7.watchedVisible ? COLOR_AIR : COLOR_TEXT));
    snprintf(text, capacity, "%s%s: %u%s#", tag, english ? "AIRCRAFT" : "LETADLA",
             static_cast<unsigned>(planesLcd7.shownCount),
             planesLcd7.watchedVisible ? " *" : "");
  }
}

void syncPlanes(const Palette &p) {
  PlanesView &v = planes;
  syncTopBar(v.bar, p);
  char text[96];

  const uint16_t rangeKm = planesLcd7.rangeKm;
  if (rangeKm > 0)
    snprintf(text, sizeof(text), "%u km", static_cast<unsigned>(rangeKm));
  else
    text[0] = '\0';
  setText(v.range, text);
  setColor(v.range, p.night ? COLOR_ERROR : COLOR_OUTSIDE);
  // Tečky kroků dosahu, aktuální větší; tažením se mění o krok.
  for (uint8_t index = 0; index < CLOCK_PLANE_RANGE_COUNT; ++index) {
    lv_obj_t *dot = v.rangeDots[index];
    const bool current = CLOCK_PLANE_RANGES_KM[index] == rangeKm;
    const lv_coord_t size = current ? 11 : 7;
    if (lv_obj_get_width(dot) != size) lv_obj_set_size(dot, size, size);
    setBackground(dot, current ? (p.night ? COLOR_ERROR : COLOR_HIGH_DOT) : p.dotOff);
    setPosition(dot, PLANES_PAD + 4 + index * 20 - size / 2, 54 - size / 2);
    setVisible(dot, rangeKm > 0);
  }
  composePlanesStatus(p, text, sizeof(text));
  setText(v.status, text);

  // Detail vybraného letadla, nebo nápověda.
  const bool open = planesDetailPanel != nullptr &&
                    !lv_obj_has_flag(planesDetailPanel, LV_OBJ_FLAG_HIDDEN);
  setVisible(v.title, open);
  setVisible(v.hint, !open && planesLcd7.haveAircraftData);
  setText(v.hint, englishLanguage() ? "Tap a plane for details"
                                    : "Klepnutím na letadlo zobrazíš detail");
  setColor(v.hint, p.muted);
  lv_obj_t *sources[PLANES_DETAIL_LINES] = {};
  if (open) {
    setText(v.title, lv_label_get_text(planesDetailTitle));
    setColor(v.title, p.night ? COLOR_ERROR : COLOR_ROOM);
    size_t count = 0;
    for (lv_obj_t *row : planesDetailRows) sources[count++] = row;
    sources[count++] = planesDetailRegistration;
    sources[count++] = planesDetailRouteFrom;
    sources[count++] = planesDetailRouteTo;
    if (!lv_obj_has_flag(planesDetailSignalLost, LV_OBJ_FLAG_HIDDEN))
      sources[count++] = planesDetailSignalLost;
  }
  // Řádky kulatého jsou "VÝŠKA: 4663 m"; tady jméno tlumeně vlevo a hodnota
  // vpravo, a když se vedle nevejde, na vlastním řádku pod jménem.
  lv_coord_t y = PLANES_DETAIL_Y + 32;
  const lv_coord_t step = lv_font_get_line_height(&lcd7_text15) + 3;
  for (size_t index = 0; index < PLANES_DETAIL_LINES; ++index) {
    lv_obj_t *row = v.detail[index];
    lv_obj_t *value = v.values[index];
    const char *source = sources[index] != nullptr ? lv_label_get_text(sources[index]) : "";
    // Kulatý píše nouzi do řádku s recolor značkou; tady stačí text.
    if (source[0] == '#') {
      const char *space = strchr(source, ' ');
      source = space != nullptr ? space + 1 : source;
    }
    const bool shown = open && y + step <= SCREEN_HEIGHT - MAP_TOP - 46 && source[0] != '\0';
    setVisible(row, shown);
    if (!shown) {
      setVisible(value, false);
      continue;
    }
    char line[64];
    strlcpy(line, source, sizeof(line));
    size_t length = strlen(line);
    if (length > 0 && line[length - 1] == '#') line[--length] = '\0';
    const lv_color_t main = p.night ? COLOR_ERROR : COLOR_TEXT;
    char *colon = strstr(line, ": ");
    if (colon == nullptr) {
      setVisible(value, false);
      setText(row, line);
      setColor(row, p.night ? COLOR_ERROR
                            : (index >= PLANES_DETAIL_ROW_COUNT ? COLOR_MUTED : main));
      setPosition(row, PLANES_PAD, y);
      y += step;
      continue;
    }
    *colon = '\0';
    const char *valueText = colon + 2;
    setText(row, line);
    setColor(row, p.muted);
    setPosition(row, PLANES_PAD, y);
    setText(value, valueText);
    setColor(value, main);
    setVisible(value, true);
    const lv_coord_t nameWidth =
        lv_txt_get_width(line, strlen(line), &lcd7_text15, 0, LV_TEXT_FLAG_NONE);
    const lv_coord_t valueWidth = std::min<lv_coord_t>(
        PLANES_CONTENT,
        lv_txt_get_width(valueText, strlen(valueText), &lcd7_text15, 0, LV_TEXT_FLAG_NONE) + 1);
    setWidth(value, valueWidth);
    if (nameWidth + 8 + valueWidth <= PLANES_CONTENT) {
      setPosition(value, PLANES_PAD + PLANES_CONTENT - valueWidth, y);
      y += step;
    } else {
      y += step;
      if (y + step > SCREEN_HEIGHT - MAP_TOP - 46) {
        setVisible(value, false);
        continue;
      }
      setPosition(value, PLANES_PAD, y);
      y += step;
    }
  }
  for (lv_obj_t *divider : v.lines) setBackground(divider, p.line);
  showStatusIcons(v.icons, p, PLANES_PAD, SCREEN_HEIGHT - MAP_TOP - 34, false);
}

// --- Doprava ----------------------------------------------------------------
// Pod lištou vlevo mapa 440 x 438 (služba dopravy ji kreslí do obdélníku
// a trasy na něj roztáhne), vpravo čtyři cesty z domova: doba jízdy velkým
// písmem, obvyklá doba, zdržení v barvě (do 2 min zelená, do 6 oranžová,
// pak červená) a kudy jet. Dole stav D1, upozornění a stáří dat.

constexpr int TRAFFIC7_MAP_Y = SCREEN_HEIGHT - TRAFFIC_LCD7_MAP_HEIGHT;
constexpr int TRAFFIC7_COLUMN_X = TRAFFIC_LCD7_MAP_WIDTH + 1;
constexpr int TRAFFIC7_PAD = 20;
constexpr int TRAFFIC7_CONTENT = SCREEN_WIDTH - TRAFFIC7_COLUMN_X - 2 * TRAFFIC7_PAD;
constexpr int TRAFFIC7_CARDS_BOTTOM = SCREEN_HEIGHT - 56;
constexpr int TRAFFIC7_CARD_H = (TRAFFIC7_CARDS_BOTTOM - TRAFFIC7_MAP_Y) / TRAFFIC_MAX_DRIVES;

struct TrafficCard {
  lv_obj_t *name;
  lv_obj_t *usual;
  lv_obj_t *minutes;
  lv_obj_t *unit;
  lv_obj_t *delta;
  lv_obj_t *detail;
  lv_obj_t *line;
};

struct TrafficView {
  lv_obj_t *root;
  TopBar bar;
  lv_obj_t *canvas;
  lv_obj_t *status;
  lv_obj_t *divider;
  TrafficCard cards[TRAFFIC_MAX_DRIVES];
  lv_obj_t *notes;
  lv_obj_t *updated;
  const uint16_t *pixels;
  uint32_t drawnRevision;
  bool drawnNight;
  bool drawnEnglish;
} traffic;

void createTraffic(lv_obj_t *screen) {
  TrafficView &v = traffic;
  v.root = panel(screen, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
  v.bar = makeTopBar(v.root);
  v.canvas = lv_canvas_create(v.root);
  lv_obj_set_pos(v.canvas, 0, TRAFFIC7_MAP_Y);
  lv_obj_add_flag(v.canvas, LV_OBJ_FLAG_HIDDEN);
  v.status = label(v.root, &lcd7_text22, 0, SCREEN_HEIGHT / 2 - 14,
                   TRAFFIC_LCD7_MAP_WIDTH);
  lv_obj_set_style_text_align(v.status, LV_TEXT_ALIGN_CENTER, 0);
  v.divider = line(v.root, TRAFFIC_LCD7_MAP_WIDTH, TRAFFIC7_MAP_Y, 1,
                   SCREEN_HEIGHT - TRAFFIC7_MAP_Y);
  const lv_coord_t x = TRAFFIC7_COLUMN_X + TRAFFIC7_PAD;
  for (size_t index = 0; index < TRAFFIC_MAX_DRIVES; ++index) {
    TrafficCard &card = v.cards[index];
    const lv_coord_t top = TRAFFIC7_MAP_Y + index * TRAFFIC7_CARD_H;
    card.name = label(v.root, &lcd7_text18, x, top + 6, TRAFFIC7_CONTENT / 2 + 30);
    card.usual = label(v.root, &lcd7_text15, x, top + 9);
    card.minutes = label(v.root, &lcd7_number44, x, top + 29);
    card.unit = label(v.root, &lcd7_text18, x, top + 49);
    card.delta = label(v.root, &lcd7_text22, x, top + 41, 150);
    card.detail = label(v.root, &lcd7_text15, x, top + TRAFFIC7_CARD_H - 24,
                        TRAFFIC7_CONTENT);
    card.line = line(v.root, x, top + TRAFFIC7_CARD_H - 1, TRAFFIC7_CONTENT, 1);
  }
  v.notes = label(v.root, &lcd7_text15, x, TRAFFIC7_CARDS_BOTTOM + 8, TRAFFIC7_CONTENT);
  lv_label_set_recolor(v.notes, true);
  v.updated = label(v.root, &lcd7_text15, x, TRAFFIC7_CARDS_BOTTOM + 30, TRAFFIC7_CONTENT);
  lv_label_set_recolor(v.updated, true);
  v.drawnRevision = UINT32_MAX;
}

void trafficFrameChanged(const uint16_t *pixels) {
  if (!created) return;
  TrafficView &v = traffic;
  if (pixels != nullptr && pixels != v.pixels) {
    lv_canvas_set_buffer(v.canvas, const_cast<uint16_t *>(pixels),
                         TRAFFIC_LCD7_MAP_WIDTH, TRAFFIC_LCD7_MAP_HEIGHT,
                         LV_IMG_CF_TRUE_COLOR);
    v.pixels = pixels;
  }
  setVisible(v.canvas, pixels != nullptr);
  if (pixels != nullptr && !lv_obj_has_flag(v.root, LV_OBJ_FLAG_HIDDEN))
    lv_obj_invalidate(v.canvas);
}

lv_color_t trafficDelayColor(const Palette &p, int delay) {
  if (p.night) return COLOR_ERROR;
  if (delay <= 2) return COLOR_AIR;
  if (delay <= 6) return COLOR_ROOM;
  return COLOR_ERROR;
}

void syncTrafficCard(const Palette &p, TrafficCard &card, const TrafficDrive *drive,
                     lv_coord_t top) {
  lv_obj_t *objects[] = {card.name, card.usual, card.minutes, card.unit,
                         card.delta, card.detail};
  for (lv_obj_t *object : objects) setVisible(object, drive != nullptr);
  setBackground(card.line, p.line);
  if (drive == nullptr) return;
  const bool english = englishLanguage();
  const lv_coord_t x = TRAFFIC7_COLUMN_X + TRAFFIC7_PAD;
  char text[TRAFFIC_TEXT_LENGTH + 8];
  setText(card.name, drive->to);
  setColor(card.name, p.muted);

  if (drive->usualMinutes >= 0)
    snprintf(text, sizeof(text), english ? "usually %d min" : "obvykle %d min",
             drive->usualMinutes);
  else
    text[0] = '\0';
  setText(card.usual, text);
  setColor(card.usual, p.muted);
  lv_obj_update_layout(card.usual);
  setPosition(card.usual, x + TRAFFIC7_CONTENT - lv_obj_get_width(card.usual), top + 9);

  const bool known = drive->minutes >= 0;
  const int delay = known && drive->usualMinutes >= 0 ? drive->minutes - drive->usualMinutes : 0;
  const lv_color_t color = trafficDelayColor(p, delay);
  if (known)
    snprintf(text, sizeof(text), "%d", drive->minutes);
  else
    strlcpy(text, "–", sizeof(text));
  setText(card.minutes, text);
  setColor(card.minutes, color);
  lv_obj_update_layout(card.minutes);
  lv_coord_t after = x + lv_obj_get_width(card.minutes) + 6;
  setText(card.unit, "min");
  setColor(card.unit, color);
  setPosition(card.unit, after, top + 49);
  lv_obj_update_layout(card.unit);
  after += lv_obj_get_width(card.unit) + 14;
  if (!known)
    text[0] = '\0';
  else if (delay > 0)
    snprintf(text, sizeof(text), "+%d", delay);
  else
    strlcpy(text, english ? "no delay" : "bez zdržení", sizeof(text));
  setText(card.delta, text);
  setColor(card.delta, color);
  setPosition(card.delta, after, delay > 0 ? top + 44 : top + 47);
  setFont(card.delta, delay > 0 ? &lcd7_text22 : &lcd7_text18);

  setText(card.detail, drive->detail);
  setColor(card.detail, p.night ? COLOR_ERROR : (drive->better ? COLOR_AIR : COLOR_TEXT));
}

void syncTraffic(const Palette &p) {
  TrafficView &v = traffic;
  syncTopBar(v.bar, p);
  const bool english = englishLanguage();
  if (v.drawnRevision == trafficRevision && v.drawnNight == p.night &&
      v.drawnEnglish == english)
    return;
  v.drawnRevision = trafficRevision;
  v.drawnNight = p.night;
  v.drawnEnglish = english;

  const TrafficSnapshot *snapshot = trafficLastSnapshot;
  const bool haveData = snapshot != nullptr && snapshot->haveData;
  setBackground(v.divider, p.line);

  // Bez dat hláška místo mapy: chyba, načítání nebo čekání.
  const char *status = "";
  if (snapshot == nullptr || !haveData) {
    if (snapshot != nullptr && snapshot->message[0] != '\0')
      status = english ? "Traffic is not available right now" : "Doprava teď není k dispozici";
    else if (snapshot != nullptr && snapshot->loading)
      status = english ? "Loading traffic..." : "Načítám dopravu...";
    else
      status = english ? "Waiting for data" : "Čekám na data";
  }
  setText(v.status, status);
  setColor(v.status, p.muted);
  setVisible(v.status, !haveData);

  const TrafficTexts *texts = haveData ? &snapshot->texts : nullptr;
  for (size_t index = 0; index < TRAFFIC_MAX_DRIVES; ++index) {
    const TrafficDrive *drive =
        texts != nullptr && index < texts->driveCount ? &texts->drives[index] : nullptr;
    syncTrafficCard(p, v.cards[index], drive, TRAFFIC7_MAP_Y + index * TRAFFIC7_CARD_H);
  }

  // Upozornění (oranžově) a stav D1 v jednom řádku.
  char notes[TRAFFIC_TEXT_LENGTH * 3];
  notes[0] = '\0';
  if (texts != nullptr) {
    char tag[AGENDA_COLOR_TAG_LENGTH + 1];
    for (uint8_t index = 0; index < texts->warningCount; ++index) {
      agendaBuildColorTag(tag, p.night ? COLOR_ERROR : COLOR_ROOM);
      if (notes[0] != '\0') strlcat(notes, "  ·  ", sizeof(notes));
      strlcat(notes, tag, sizeof(notes));
      appendEscaped(notes, sizeof(notes), texts->warnings[index]);
      strlcat(notes, "#", sizeof(notes));
    }
    for (uint8_t index = 0; index < texts->textCount; ++index) {
      if (notes[0] != '\0') strlcat(notes, "  ·  ", sizeof(notes));
      appendEscaped(notes, sizeof(notes), texts->texts[index]);
    }
  }
  setText(v.notes, notes);
  setColor(v.notes, p.muted);

  char updated[64];
  updated[0] = '\0';
  if (texts != nullptr && texts->sampleTime > 0) {
    const time_t when = static_cast<time_t>(texts->sampleTime);
    struct tm local;
    char clock[8] = "";
    if (localtime_r(&when, &local) != nullptr) strftime(clock, sizeof(clock), "%H:%M", &local);
    char tag[AGENDA_COLOR_TAG_LENGTH + 1];
    agendaBuildColorTag(tag, p.night ? COLOR_ERROR : COLOR_ROOM);
    snprintf(updated, sizeof(updated), "%s %s%s%s%s", english ? "traffic at" : "provoz v",
             clock, texts->stale ? "  " : "", texts->stale ? tag : "",
             texts->stale ? (english ? "old data#" : "stará data#") : "");
  }
  setText(v.updated, updated);
  setColor(v.updated, p.muted);
}

// --- Družice a noční obloha: Slunce a Měsíc vpravo --------------------------
// Kulatá mapa oblohy zůstává u levého okraje, přes její horní okraj vede
// stejná lišta jako jinde (řádek s časem v kruhu se proto schová). Vpravo
// Slunce a Měsíc - samostatná obrazovka Slunce a Měsíc na 7" není. Texty
// a kotouč Měsíce skládá skrytá kulatá stránka (updateSkyPage).

constexpr int SKY_SIDE_X = STAGE_SIZE;
constexpr int SKY_SIDE_W = SCREEN_WIDTH - SKY_SIDE_X;
constexpr int SKY_SIDE_PAD = 18;
constexpr int SKY_SIDE_CONTENT = SKY_SIDE_W - 2 * SKY_SIDE_PAD;
const lv_color_t SKY_SUNRISE = LV_COLOR_MAKE(255, 194, 26);

struct SkySideView {
  lv_obj_t *barRoot;
  TopBar bar;
  lv_obj_t *column;
  lv_obj_t *sunTitle;
  lv_obj_t *sunCaptions[2];
  lv_obj_t *sunTimes[2];
  lv_obj_t *twilight;
  lv_obj_t *dayLength;
  lv_obj_t *line;
  lv_obj_t *moonTitle;
  lv_obj_t *moon;
  lv_obj_t *phase;
  lv_obj_t *illumination;
  lv_obj_t *moonTimes;
  lv_obj_t *nextPhases;
  StatusIcons icons;
  uint32_t drawnRevision;
} skySide;

void createSkySide(lv_obj_t *screen) {
  SkySideView &v = skySide;
  v.barRoot = panel(screen, 0, 0, SCREEN_WIDTH, BAR_HEIGHT + 1);
  v.bar = makeTopBar(v.barRoot);
  v.column = panel(screen, SKY_SIDE_X, BAR_HEIGHT + 1, SKY_SIDE_W, SCREEN_HEIGHT - BAR_HEIGHT - 1);
  lv_obj_t *c = v.column;
  const lv_coord_t half = SKY_SIDE_CONTENT / 2;
  v.sunTitle = label(c, &lcd7_text15, SKY_SIDE_PAD, 14);
  lv_obj_set_style_text_letter_space(v.sunTitle, 2, 0);
  for (int index = 0; index < 2; ++index) {
    v.sunCaptions[index] = label(c, &lcd7_text15, SKY_SIDE_PAD + index * half, 38);
    v.sunTimes[index] = label(c, &lcd7_text28, SKY_SIDE_PAD + index * half, 56);
  }
  v.twilight = label(c, &lcd7_text15, SKY_SIDE_PAD, 96, SKY_SIDE_CONTENT);
  v.dayLength = label(c, &lcd7_text15, SKY_SIDE_PAD, 118, SKY_SIDE_CONTENT);
  v.line = line(c, SKY_SIDE_PAD, 150, SKY_SIDE_CONTENT, 1);
  v.moonTitle = label(c, &lcd7_text15, SKY_SIDE_PAD, 164);
  lv_obj_set_style_text_letter_space(v.moonTitle, 2, 0);
  v.moon = lv_canvas_create(c);
  lv_obj_set_pos(v.moon, SKY_SIDE_PAD, 190);
  lv_obj_add_flag(v.moon, LV_OBJ_FLAG_HIDDEN);
  const lv_coord_t textX = SKY_SIDE_PAD + SKY_MOON_SIZE + 16;
  const lv_coord_t textW = SKY_SIDE_CONTENT - SKY_MOON_SIZE - 16;
  v.phase = label(c, &lcd7_text22, textX, 192, textW);
  v.illumination = label(c, &lcd7_text15, textX, 222, textW);
  v.moonTimes = label(c, &lcd7_text15, textX, 244, textW);
  lv_label_set_long_mode(v.moonTimes, LV_LABEL_LONG_WRAP);
  v.nextPhases = label(c, &lcd7_text15, SKY_SIDE_PAD, 296, SKY_SIDE_CONTENT);
  lv_label_set_long_mode(v.nextPhases, LV_LABEL_LONG_WRAP);
  v.icons = makeStatusIcons(c);
  v.drawnRevision = UINT32_MAX;
}

// Text kulatého popisku, nebo prázdný, dokud stránka Slunce a Měsíc nevznikla.
const char *skyText(lv_obj_t *source) {
  return source != nullptr ? lv_label_get_text(source) : "";
}

void syncSkySide(const Palette &p) {
  SkySideView &v = skySide;
  syncTopBar(v.bar, p);
  showStatusIcons(v.icons, p, SKY_SIDE_W - SKY_SIDE_PAD,
                  SCREEN_HEIGHT - BAR_HEIGHT - 34, true);
  setBackground(v.line, p.line);
  const bool english = englishLanguage();
  setText(v.sunTitle, english ? "SUN" : "SLUNCE");
  setColor(v.sunTitle, p.night ? COLOR_ERROR : COLOR_ROOM);
  setText(v.moonTitle, english ? "MOON" : "MĚSÍC");
  setColor(v.moonTitle, p.night ? COLOR_ERROR : COLOR_MOON);
  for (int index = 0; index < 2; ++index) {
    setText(v.sunCaptions[index], skyText(skyCaptionLabels[index]));
    setColor(v.sunCaptions[index], p.muted);
    setText(v.sunTimes[index], skyText(skyTimeLabels[index]));
    setColor(v.sunTimes[index],
             p.night ? COLOR_ERROR : (index == 0 ? SKY_SUNRISE : COLOR_ROOM));
  }
  char text[96];
  snprintf(text, sizeof(text), "%s %s  ·  %s %s", english ? "dawn" : "svítání",
           skyText(skyTwilightLabels[0]), english ? "dusk" : "soumrak",
           skyText(skyTwilightLabels[1]));
  setText(v.twilight, text);
  setColor(v.twilight, p.muted);
  setText(v.dayLength, skyText(skyDayLengthLabel));
  setColor(v.dayLength, p.night ? COLOR_ERROR : COLOR_TEXT);

  setText(v.phase, skyText(skyPhaseNameLabel));
  setColor(v.phase, p.night ? COLOR_ERROR : COLOR_TEXT);
  setText(v.illumination, skyText(skyIlluminationLabel));
  setColor(v.illumination, p.muted);
  snprintf(text, sizeof(text), "%s %s\n%s %s", skyText(skyCaptionLabels[2]),
           skyText(skyTimeLabels[2]), skyText(skyCaptionLabels[3]),
           skyText(skyTimeLabels[3]));
  setText(v.moonTimes, text);
  setColor(v.moonTimes, p.night ? COLOR_ERROR : COLOR_TEXT);
  setText(v.nextPhases, skyText(skyNextPhasesLabel));
  setColor(v.nextPhases, p.muted);

  // Kotouč Měsíce kreslí kulatá stránka do vlastního bufferu; stačí ho
  // ukázat a po přepočtu překreslit.
  setVisible(v.moon, skyMoonPixels != nullptr);
  if (skyMoonPixels != nullptr && v.drawnRevision != skyRevision) {
    lv_canvas_set_buffer(v.moon, skyMoonPixels, SKY_MOON_SIZE, SKY_MOON_SIZE,
                         LV_IMG_CF_TRUE_COLOR);
    lv_obj_invalidate(v.moon);
    v.drawnRevision = skyRevision;
  }
}

// --- Nastavení --------------------------------------------------------------
// Stejné ovladače jako na kulatém (createSettingsPage), jen rozložené přes
// celou obrazovku: nahoře hlavička s adresou a tlačítky Zrušit/Uložit, vlevo
// sekce místo šipek, vpravo obsah vybrané sekce. Logika (náhled jasu,
// ukládání, firmware) zůstává kulatá.

constexpr int SETTINGS_HEADER_H = 68;
constexpr int SETTINGS_NAV_W = 224;
constexpr int SETTINGS_NAV_BUTTON_H = 62;
const lv_color_t SETTINGS_NAV_SELECTED = LV_COLOR_MAKE(29, 43, 48);
const lv_color_t SETTINGS_BUTTON_BORDER = LV_COLOR_MAKE(58, 58, 58);

struct SettingsView {
  lv_obj_t *title;
  lv_obj_t *subtitle;
  lv_obj_t *nav[SETTINGS_PAGE_COUNT];
  lv_obj_t *navLabels[SETTINGS_PAGE_COUNT];
  lv_obj_t *cancelLabel;
  lv_obj_t *saveLabel;
  uint8_t drawnPage;
  bool drawnEnglish;
} settings;

void settingsNavEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_SHORT_CLICKED) return;
  showSettingsSubpage(static_cast<uint8_t>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event))));
}

// Tlačítko jako pilulka s textem místo kulatého s ikonou.
lv_obj_t *restyleSettingsButton(lv_obj_t *button, lv_coord_t width, lv_coord_t x,
                                lv_color_t background, lv_color_t text, bool border) {
  lv_obj_set_size(button, width, 50);
  lv_obj_set_align(button, LV_ALIGN_TOP_LEFT);
  lv_obj_set_pos(button, x, (SETTINGS_HEADER_H - 50) / 2);
  lv_obj_set_style_radius(button, 25, 0);
  lv_obj_set_style_bg_color(button, background, 0);
  lv_obj_set_style_border_width(button, border ? 2 : 0, 0);
  lv_obj_set_style_border_color(button, SETTINGS_BUTTON_BORDER, 0);
  lv_obj_t *caption = lv_obj_get_child(button, 0);
  lv_obj_set_style_text_font(caption, &lcd7_text22, 0);
  lv_obj_set_style_text_color(caption, text, 0);
  return caption;
}

void adaptSettings() {
  SettingsView &v = settings;
  lv_obj_t *page = settingsPage;
  lv_obj_set_parent(page, lv_scr_act());
  lv_obj_set_align(page, LV_ALIGN_TOP_LEFT);
  lv_obj_set_pos(page, 0, 0);
  lv_obj_set_size(page, SCREEN_WIDTH, SCREEN_HEIGHT);
  // První potomek je ozdobný prstenec po okraji kulatého displeje.
  lv_obj_add_flag(lv_obj_get_child(page, 0), LV_OBJ_FLAG_HIDDEN);
  for (lv_obj_t *object : {settingsPreviousButton, settingsNextButton, settingsPageNumberLabel})
    if (object != nullptr) lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);

  v.title = label(page, &lcd7_text28, 24, 16);
  v.subtitle = label(page, &lcd7_text15, 0, 28);
  line(page, 0, SETTINGS_HEADER_H, SCREEN_WIDTH, 1);
  line(page, SETTINGS_NAV_W, SETTINGS_HEADER_H, 1, SCREEN_HEIGHT - SETTINGS_HEADER_H);

  v.cancelLabel = restyleSettingsButton(settingsCancelButton, 128, SCREEN_WIDTH - 20 - 128 - 12 - 128,
                                        COLOR_BACKGROUND, COLOR_TEXT, true);
  v.saveLabel = restyleSettingsButton(settingsSaveButton, 128, SCREEN_WIDTH - 20 - 128,
                                      COLOR_AIR, COLOR_BACKGROUND, false);

  for (uint8_t index = 0; index < SETTINGS_PAGE_COUNT; ++index) {
    lv_obj_t *button = lv_btn_create(page);
    lv_obj_set_pos(button, 14, SETTINGS_HEADER_H + 12 + index * (SETTINGS_NAV_BUTTON_H + 6));
    lv_obj_set_size(button, SETTINGS_NAV_W - 26, SETTINGS_NAV_BUTTON_H);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, COLOR_BACKGROUND, 0);
    lv_obj_add_event_cb(button, settingsNavEvent, LV_EVENT_SHORT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(index)));
    v.nav[index] = button;
    v.navLabels[index] = label(button, &lcd7_text22, 2, 0);
    lv_obj_align(v.navLabels[index], LV_ALIGN_LEFT_MID, 4, 0);
    // Obsah sekce do pravé části; ovladače jsou zarovnané na střed panelu.
    lv_obj_t *content = settingsContent[index];
    lv_obj_set_align(content, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(content, SETTINGS_NAV_W + 1, SETTINGS_HEADER_H + 1);
    lv_obj_set_size(content, SCREEN_WIDTH - SETTINGS_NAV_W - 1,
                    SCREEN_HEIGHT - SETTINGS_HEADER_H - 1);
  }
  v.drawnPage = UINT8_MAX;
}

void syncSettings() {
  SettingsView &v = settings;
  if (v.title == nullptr) return;
  const bool english = englishLanguage();
  setText(v.title, english ? "Settings" : "Nastavení");
  lv_obj_update_layout(v.title);
  // Adresa z kulaté stránky Systém ("IP: 192.168.1.166").
  setText(v.subtitle, wifiAddressLabel != nullptr ? lv_label_get_text(wifiAddressLabel) : "");
  setColor(v.subtitle, COLOR_MUTED);
  setPosition(v.subtitle, lv_obj_get_x(v.title) + lv_obj_get_width(v.title) + 16, 28);
  if (v.drawnPage == settingsPageIndex && v.drawnEnglish == english) return;
  v.drawnPage = settingsPageIndex;
  v.drawnEnglish = english;
  static const char *const CZECH[SETTINGS_PAGE_COUNT] = {
      "Vzhled", "Jas den/noc", "Ikony a vteřiny", "Systém", "Heslo webu"};
  static const char *const ENGLISH[SETTINGS_PAGE_COUNT] = {
      "Appearance", "Brightness", "Icons & seconds", "System", "Web password"};
  for (uint8_t index = 0; index < SETTINGS_PAGE_COUNT; ++index) {
    const bool selected = index == settingsPageIndex;
    setText(v.navLabels[index], english ? ENGLISH[index] : CZECH[index]);
    setColor(v.navLabels[index], selected ? COLOR_TEXT : COLOR_MUTED);
    setBackground(v.nav[index], selected ? SETTINGS_NAV_SELECTED : COLOR_BACKGROUND);
  }
  setText(v.cancelLabel, english ? "Cancel" : "Zrušit");
  setText(v.saveLabel, english ? "Save" : "Uložit");
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

uint32_t lastSyncAt = 0;

HomeMode desiredMode() {
  if (settingsVisible || firmwareUpdateActive) return HomeMode::None;
  if (activeScreen == DASHBOARD_SCREEN_FORECAST && forecastPage != nullptr)
    return HomeMode::Forecast;
  if (activeScreen == DASHBOARD_SCREEN_RADAR && radarPage != nullptr)
    return HomeMode::Radar;
  if (activeScreen == DASHBOARD_SCREEN_AGENDA && agendaPage != nullptr)
    return HomeMode::Agenda;
  if (activeScreen == DASHBOARD_SCREEN_RSS && rssPage != nullptr)
    return HomeMode::Rss;
  if (activeScreen == DASHBOARD_SCREEN_SCHOOL && schoolPage != nullptr)
    return HomeMode::School;
  if (activeScreen == DASHBOARD_SCREEN_PLANES && planesPage != nullptr)
    return HomeMode::Planes;
  if (activeScreen == DASHBOARD_SCREEN_TRAFFIC && trafficPage != nullptr)
    return HomeMode::Traffic;
  // Družice (s noční oblohou) zůstávají kulaté: jeviště vlevo pod lištou,
  // vpravo Slunce a Měsíc. Samostatná obrazovka Slunce a Měsíc na 7" není.
  if (activeScreen == DASHBOARD_SCREEN_SATELLITES && satellitesPage != nullptr)
    return HomeMode::RoundLeft;
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
  createAgenda(screen);
  createRss(screen);
  createSchool(screen);
  createPlanes(screen);
  createTraffic(screen);
  createSkySide(screen);
  for (lv_obj_t *root : {skySide.barRoot, skySide.column})
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
  for (lv_obj_t *root : {digital.root, analog.root, values.root, forecast.root,
                         radar.root, agenda.root, rss.root, school.root, planes.root,
                         traffic.root})
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
  created = true;
}

// Celoplošné obrazovky překryjí jeviště; analogový ciferník je jeviště samo,
// jen posunuté k levému okraji.
void applyMode(HomeMode mode) {
  lv_obj_t *stage = displayDriverStage();
  setVisible(digital.root, mode == HomeMode::Digital);
  setVisible(analog.root, mode == HomeMode::Analog);
  setVisible(skySide.barRoot, mode == HomeMode::RoundLeft);
  setVisible(skySide.column, mode == HomeMode::RoundLeft);
  if (mode == HomeMode::RoundLeft) skySide.drawnRevision = UINT32_MAX;
  setVisible(values.root, mode == HomeMode::Values);
  setVisible(forecast.root, mode == HomeMode::Forecast);
  if (mode == HomeMode::Forecast) forecast.signature = 0;
  if (mode == HomeMode::Radar && radar.pixels != nullptr) lv_obj_invalidate(radar.canvas);
  setVisible(radar.root, mode == HomeMode::Radar);
  if (mode == HomeMode::Agenda) agenda.drawnRevision = UINT32_MAX;
  setVisible(agenda.root, mode == HomeMode::Agenda);
  if (mode == HomeMode::Rss && shownMode != HomeMode::Rss) {
    // Obrazovka se vždycky otevírá na nejnovější zprávě.
    rss.selected = 0;
    rss.selectedAt = millis();
    rss.holdUntil = 0;
    rss.drawnRevision = UINT32_MAX;
  }
  setVisible(rss.root, mode == HomeMode::Rss);
  if (mode == HomeMode::School) school.drawnRevision = UINT32_MAX;
  setVisible(school.root, mode == HomeMode::School);
  if (mode == HomeMode::Planes && planes.pixels != nullptr) lv_obj_invalidate(planes.canvas);
  setVisible(planes.root, mode == HomeMode::Planes);
  if (mode == HomeMode::Traffic) {
    traffic.drawnRevision = UINT32_MAX;
    if (traffic.pixels != nullptr) lv_obj_invalidate(traffic.canvas);
  }
  setVisible(traffic.root, mode == HomeMode::Traffic);
  const bool stageLeft = mode == HomeMode::Analog || mode == HomeMode::RoundLeft;
  setVisible(stage, mode == HomeMode::None || stageLeft);
  setPosition(stage, stageLeft ? 0 : STAGE_X, STAGE_Y);
  shownMode = mode;
}

// Leží jeviště u levého okraje? Klepnutí pak nesmí odečítat STAGE_X.
bool stageAtLeft() {
  return shownMode == HomeMode::Analog || shownMode == HomeMode::RoundLeft;
}

void sync(bool force) {
  if (!created) return;
  const PsramAllocations psram;
  maintainHistory();
  if (settingsVisible) syncSettings();
  const HomeMode mode = desiredMode();
  if (mode != shownMode) {
    applyMode(mode);
    force = true;
  }
  // Kulatý kód při každé změně hodnot odkrývá své popisky v ciferníku; skrýt
  // je musí ještě před vykreslením, ne až s další synchronizací.
  if (mode == HomeMode::Analog) hideRoundDialContent();
  // Řádek s datem, časem a teplotou v kruhu družic leží pod lištou.
  if (mode == HomeMode::RoundLeft) setVisible(satellitesClockLabel, false);
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
    case HomeMode::Agenda: syncAgenda(p); break;
    case HomeMode::Rss: syncRss(p); break;
    case HomeMode::School: syncSchool(p); break;
    case HomeMode::Planes: syncPlanes(p); break;
    case HomeMode::Traffic: syncTraffic(p); break;
    case HomeMode::RoundLeft: syncSkySide(p); break;
    case HomeMode::None: break;
  }
}

}  // namespace lcd7

#endif  // HODINY_BOARD_LCD7
