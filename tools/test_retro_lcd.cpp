// Retro LCD: geometrie segmentů, formátování a rozvržení obou desek.
// Testy geometrie a formátování odpovídají upstreamovým testům
// CooLajz/waveshare-hodiny (tests/retro_lcd_*_test.cpp, MIT).

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#include "RetroLcdFormat.h"
#include "RetroLcdGeometry.h"
#include "RetroLcdLayout.h"

namespace {

using Raster = std::vector<int>;

Raster raster(int w, int h, bool alpha, int segment, bool solid = false) {
  Raster out(w * h);
  retro_lcd_geometry::segment(
      w, h, alpha, segment,
      [&](int x, int y, int n, uint8_t a) {
        assert(x >= 0 && x + n <= w && y >= 0 && y < h);
        for (int i = 0; i < n; ++i) {
          assert(out[y * w + x + i] == 0);
          out[y * w + x + i] = a;
        }
      },
      solid);
  int sum = 0;
  for (int a : out) sum += a;
  assert(sum > 0);
  return out;
}

void mirror(const Raster &a, const Raster &b, int w, int h, bool mx, bool my) {
  const int right = 2 * ((w - 1) / 2), bottom = 2 * ((h - 1) / 2);
  for (int y = 0; y <= bottom; ++y)
    for (int x = 0; x <= right; ++x)
      assert(a[y * w + x] == b[(my ? bottom - y : y) * w + (mx ? right - x : x)]);
}

void testGeometry() {
  for (bool alpha : {false, true}) {
    const int w = alpha ? 18 : 16, h = alpha ? 30 : 29;
    std::vector<Raster> r;
    for (int i = 0; i < (alpha ? 14 : 7); ++i) {
      r.push_back(raster(w, h, alpha, i, true));
      for (int pixel : r.back()) assert(pixel == 0 || pixel == 255);
    }
    mirror(r[0], r[3], w, h, false, true);
    mirror(r[5], r[1], w, h, true, false);
    mirror(r[5], r[4], w, h, false, true);
    if (alpha) mirror(r[6], r[7], w, h, true, false);
  }
  // Všechny velikosti, které rozvržení obou desek používá, musí dát
  // souměrné segmenty uvnitř své buňky.
  const std::pair<int, int> sizes[] = {
      {16, 29}, {18, 30}, {60, 112}, {27, 50}, {24, 46},
      {26, 44}, {24, 44}, {104, 200}, {48, 90}, {32, 60}};
  for (const auto &size : sizes) {
    const int w = size.first, h = size.second;
    for (bool alpha : {false, true}) {
      std::vector<Raster> r;
      for (int i = 0; i < (alpha ? 14 : 7); ++i) r.push_back(raster(w, h, alpha, i));
      mirror(r[0], r[3], w, h, false, true);
      mirror(r[5], r[1], w, h, true, false);
      mirror(r[5], r[4], w, h, false, true);
      mirror(r[1], r[2], w, h, false, true);
      mirror(r[6], r[6], w, h, false, true);
      if (alpha) {
        mirror(r[6], r[7], w, h, true, false);
        mirror(r[8], r[10], w, h, true, false);
        mirror(r[8], r[13], w, h, false, true);
        mirror(r[9], r[12], w, h, false, true);
      }
    }
  }
}

void checkValue(float value, unsigned places, unsigned decimals,
                const char *expected, bool extra = false) {
  char out[32];
  bool external = !extra;
  retroLcdFormatValue(out, sizeof(out), value, places, decimals, &external);
  if (std::strcmp(out, expected) != 0) {
    std::printf("%g/%u/%u: '%s' != '%s'\n", value, places, decimals, out, expected);
    assert(false);
  }
  assert(external == extra);
}

void testValues() {
  checkValue(-.6f, 2, 1, "-0.6");
  checkValue(-1, 2, 0, "-1");
  checkValue(-9.9f, 2, 1, "-9.9");
  checkValue(-10, 2, 1, "10.0", true);
  checkValue(-9.96f, 2, 1, "10.0", true);
  checkValue(-99.96f, 2, 1, "--.-");
  checkValue(-.04f, 2, 1, " 0.0");
  checkValue(21.5f, 2, 1, "21.5");
  checkValue(5.0f, 2, 1, " 5.0");
  checkValue(812, 4, 0, " 812");
  checkValue(1013.2f, 4, 1, "1013.2");
  checkValue(NAN, 2, 1, "--.-");
  checkValue(INFINITY, 3, 0, "---");
  // Mimo rozsah 1-4 míst a 0-2 desetinných míst se meze srovnají.
  checkValue(7, 9, 0, "   7");
  checkValue(7, 0, 5, "7.00");
  char shortOut[3];
  bool external = true;
  retroLcdFormatValue(shortOut, sizeof(shortOut), -10, 2, 1, &external);
  assert(shortOut[0] == '\0' && !external);
}

void testDateAndWeekday() {
  const char *expected[] = {"06.07.2024", "06-07-2024", "06/07/2024", "07-06-2024",
                            "07/06/2024", "2024-07-06", "2024/07/06", " 6. 7.2024",
                            " 6- 7-2024", " 6/ 7/2024", " 7- 6-2024", " 7/ 6/2024",
                            "2024- 7- 6", "2024/ 7/ 6"};
  for (unsigned format = 0; format < 14; ++format) {
    char out[16];
    retroLcdFormatDate(out, sizeof(out), 6, 7, 2024, format);
    assert(std::strcmp(out, expected[format]) == 0);
    retroLcdFormatDate(out, sizeof(out), 1, 1, 2024, format, false);
    assert(std::strlen(out) == 10 && !std::strchr(out, ' '));
  }
  char out[16];
  retroLcdFormatDate(out, sizeof(out), 6, 7, 2024, 255);
  assert(std::strcmp(out, expected[0]) == 0);

  char day[12];
  retroLcdFormatWeekday(day, sizeof(day), 4, false, true);
  assert(std::strcmp(day, "CTVRTEK") == 0);
  retroLcdFormatWeekday(day, sizeof(day), 0, false, true);
  assert(std::strcmp(day, "NEDELE ") == 0);
  retroLcdFormatWeekday(day, sizeof(day), 0, false, false);
  assert(std::strcmp(day, "NEDELE") == 0);
  retroLcdFormatWeekday(day, sizeof(day), 3, true, true);
  assert(std::strcmp(day, "WEDNESDAY") == 0);
  retroLcdFormatWeekday(day, sizeof(day), -1, false, true);
  assert(std::strcmp(day, "-------") == 0);
}

void testProgress() {
  assert(retroLcdProgressPercent(50, 0, 100) == 50.0f);
  assert(retroLcdProgressPercent(-5, 0, 100) == 0.0f);
  assert(retroLcdProgressPercent(150, 0, 100) == 100.0f);
  assert(std::isnan(retroLcdProgressPercent(NAN, 0, 100)));
  assert(std::isnan(retroLcdProgressPercent(5, 10, 10)));
}

struct Box {
  int x1, y1, x2, y2;
};

bool overlaps(const Box &a, const Box &b) {
  return a.x1 < b.x2 && b.x1 < a.x2 && a.y1 < b.y2 && b.y1 < a.y2;
}

void insideScreen(const RetroLcdLayout &l, const Box &box, const char *what) {
  bool inside = box.x1 >= 0 && box.y1 >= 0 && box.x2 <= l.width && box.y2 <= l.height;
  if (inside && l.round) {
    // Všechny čtyři rohy uvnitř kruhu displeje.
    const float c = l.width / 2.0f;
    for (int x : {box.x1, box.x2})
      for (int y : {box.y1, box.y2})
        inside = inside && (x - c) * (x - c) + (y - c) * (y - c) <= c * c;
  }
  if (!inside) {
    std::printf("%s (%d,%d)-(%d,%d) mimo displej %s\n", what, box.x1, box.y1,
                box.x2, box.y2, l.round ? "2,1\"" : "7\"");
    assert(false);
  }
}

void testLayout(const RetroLcdLayout &l) {
  // Nejdelší texty: den anglicky (9 znaků), datum 10 znaků, čas 88:88.
  const int dayWidth = retroLcdTextWidth("WEDNESDAY", l.weekday.w, l.weekday.gap);
  const int dateWidth = retroLcdTextWidth("2024/07/06", l.date.w, l.date.gap, true, 4, 7);
  const int dateWidth2 = retroLcdTextWidth("06.07.2024", l.date.w, l.date.gap, true);
  const int timeWidth = retroLcdTextWidth("88:88", l.time.w, l.time.gap);
  const int secondsWidth = retroLcdTextWidth("88", l.seconds.w, l.seconds.gap);
  const Box weekday = {l.weekday.x - dayWidth / 2, l.weekday.y,
                       l.weekday.x + dayWidth / 2, l.weekday.y + l.weekday.h};
  const int wideDate = dateWidth > dateWidth2 ? dateWidth : dateWidth2;
  const Box date = {l.date.x - wideDate / 2, l.date.y, l.date.x + wideDate / 2,
                    l.date.y + l.date.h};
  const Box time = {l.time.x, l.time.y, l.time.x + timeWidth, l.time.y + l.time.h};
  const Box centered = {l.timeCenterX - timeWidth / 2, l.time.y,
                        l.timeCenterX + timeWidth / 2, l.time.y + l.time.h};
  const Box seconds = {l.seconds.x, l.seconds.y, l.seconds.x + secondsWidth,
                       l.seconds.y + l.seconds.h};
  const Box values[2] = {
      {l.valueCenterX[0] - l.valueWidth / 2, l.valueNameY,
       l.valueCenterX[0] + l.valueWidth / 2, l.valueBottom},
      {l.valueCenterX[1] - l.valueWidth / 2, l.valueNameY,
       l.valueCenterX[1] + l.valueWidth / 2, l.valueBottom}};
  // Ikona počasí místo hodnoty: 26 bodů po 4 px.
  const int grid = 26 * 4 - 1;
  const Box weatherValue = {l.valueCenterX[0] - grid / 2,
                            l.valueWeatherTop + l.valueWeatherHeight / 2 - grid / 2,
                            l.valueCenterX[0] + grid / 2,
                            l.valueWeatherTop + l.valueWeatherHeight / 2 + grid / 2};
  const Box bar = {l.bar.x, l.bar.y, l.bar.x + l.bar.w, l.bar.y + l.bar.h};
  const Box barLabel = {l.barLabel.x, l.barLabel.y, l.barLabel.x + l.barLabel.w,
                        l.barLabel.y + l.barLabel.h};
  const Box status = {l.statusCenterX - 70, l.statusY, l.statusCenterX + 70,
                      l.statusY + 18};

  insideScreen(l, weekday, "den");
  insideScreen(l, date, "datum");
  insideScreen(l, time, "čas");
  if (l.timeCenterX > 0) insideScreen(l, centered, "čas bez vteřin");
  insideScreen(l, seconds, "vteřiny");
  insideScreen(l, values[0], "levá hodnota");
  insideScreen(l, values[1], "pravá hodnota");
  insideScreen(l, weatherValue, "ikona místo hodnoty");
  insideScreen(l, bar, "ukazatel");
  insideScreen(l, barLabel, "popisek ukazatele");
  insideScreen(l, status, "stav");
  for (const RetroLcdBox &rule : l.rules)
    if (rule.w > 0) insideScreen(l, {rule.x, rule.y, rule.x + rule.w, rule.y + rule.h}, "čára");

  assert(!overlaps(weekday, date) || l.round);
  assert(!overlaps(time, seconds));
  assert(!overlaps(values[0], values[1]));
  assert(!overlaps(values[0], bar) && !overlaps(values[1], bar));
  assert(!overlaps(time, values[0]) && !overlaps(time, values[1]));
  assert(!overlaps(bar, status) && !overlaps(barLabel, status));
  assert(!overlaps(date, time) && !overlaps(weekday, time));
  if (l.weather.w > 0) {
    const Box weather = {l.weather.x, l.weather.y, l.weather.x + l.weather.w,
                         l.weather.y + l.weather.h};
    insideScreen(l, weather, "počasí");
    assert(!overlaps(weather, time) && !overlaps(weather, seconds));
    assert(grid <= l.weather.w && grid <= l.weather.h);
  }
  if (l.dotsRight > 0) {
    const Box dots = {l.dotsRight - 15 * 18 - 5, l.dotsY - 5, l.dotsRight + 5, l.dotsY + 5};
    insideScreen(l, dots, "tečky");
    assert(!overlaps(dots, date) && !overlaps(dots, weekday));
  }
  // Ikona místo hodnoty se vejde do pruhu pod čarou.
  assert(grid <= l.valueWeatherHeight);
  // Výchozí hodnota (teplota, dvě místa a desetina) se vejde bez zmenšení.
  const int number = retroLcdTextWidth("21.5", l.valueDigitW, l.valueGap);
  assert(number + l.valueDigitW + 4 + 30 + 6 <= l.valueWidth);
}

}  // namespace

int main() {
  testGeometry();
  testValues();
  testDateAndWeekday();
  testProgress();
  testLayout(RETRO_LCD_LAYOUT_ROUND);
  testLayout(RETRO_LCD_LAYOUT_LCD7);
  std::puts("retro_lcd OK");
  return 0;
}
