// Prstenec předpovědi: barvy, šev "teď", mezikruží a výseky k překreslení.

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#include "ForecastFan.h"

namespace {

void testTemperatureColors() {
  assert(forecastFanTemperatureColor(NAN) == FORECAST_FAN_MISSING_COLOR);
  assert(forecastFanTemperatureColor(-30) == 0x6868E8);
  assert(forecastFanTemperatureColor(0) == 0x3294F0);
  assert(forecastFanTemperatureColor(20) == 0x81CB68);
  assert(forecastFanTemperatureColor(55) == 0xEF5543);
  // Mezi body se barva plynule mění.
  const uint32_t middle = forecastFanTemperatureColor(25);
  assert(middle != 0x81CB68 && middle != 0xFFB843);
}

void testKey() {
  float temperatures[24];
  for (int i = 0; i < 24; ++i) temperatures[i] = static_cast<float>(i);
  ForecastFanKey key;
  // 14:20: první hodina předpovědi (15:00) patří na pozici 3, poslední
  // (o 12 hodin dál, 2:00) na pozici 2 = výsek "teď".
  forecastFanBuildKey(temperatures, 12, 14, 20, true, key);
  assert(key.sectors == 12 && key.hour == 2 && key.minute == 20);
  assert(key.colors[3] == forecastFanTemperatureColor(0));
  assert(key.colors[2] == forecastFanTemperatureColor(11));
  forecastFanBuildKey(temperatures, 24, 14, 20, true, key);
  assert(key.sectors == 24 && key.hour == 14);
  assert(key.colors[15] == forecastFanTemperatureColor(0));
  assert(key.colors[14] == forecastFanTemperatureColor(23));
  // Neplatný klíč je celý šedý; neznámý počet výseků padá na 12.
  forecastFanBuildKey(temperatures, 7, 3, 99, false, key);
  assert(key.sectors == 12 && key.minute == 59);
  for (int i = 0; i < 12; ++i) assert(key.colors[i] == FORECAST_FAN_MISSING_COLOR);
}

void testAngles() {
  assert(std::fabs(forecastFanAngle(0, -10) - 0.0f) < 0.01f);
  assert(std::fabs(forecastFanAngle(10, 0) - 90.0f) < 0.01f);
  assert(std::fabs(forecastFanAngle(0, 10) - 180.0f) < 0.01f);
  assert(std::fabs(forecastFanAngle(-10, 0) - 270.0f) < 0.01f);
}

// Šev: ve výseku "teď" je před minutou nejvzdálenější hodina a za ní ta
// nejbližší, bez přechodu.
void testSeam() {
  ForecastFanKey key;
  key.sectors = 12;
  key.valid = true;
  key.hour = 3;
  key.minute = 30;
  for (int i = 0; i < 12; ++i) key.colors[i] = 0x000000;
  key.colors[3] = 0xFF0000;  // nejvzdálenější
  key.colors[4] = 0x0000FF;  // nejbližší
  auto at = [&](float degrees) {
    const float radians = degrees * 3.14159265f / 180.0f;
    const int dx = static_cast<int>(std::lround(std::sin(radians) * 235));
    const int dy = static_cast<int>(std::lround(-std::cos(radians) * 235));
    return forecastFanPixel(key, dx, dy, 0, 0);
  };
  const uint32_t before = at(3 * 30 + 5);   // 3:10
  const uint32_t after = at(3 * 30 + 25);   // 3:50
  assert((before >> 16) > 100 && (before & 255) < 20);
  assert((after & 255) > 100 && (after >> 16) < 20);
  // Mimo šev se přechází plynule: uprostřed výseku 5 je půl napůl.
  key.colors[5] = 0xFFFFFF;
  key.colors[6] = 0x000000;
  const uint32_t half = at(5 * 30 + 15);
  const int green = (half >> 8) & 255;
  assert(green > 60 && green < 140);
}

void testDither() {
  // Rozptýlení vrací barvy přesně zobrazitelné v RGB565 a v průměru drží jas.
  long sum = 0;
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 4; ++x) {
      const uint32_t c = forecastFanDither(100, 100, 100, x, y);
      const int red = (c >> 16) & 255;
      const int green = (c >> 8) & 255;
      assert(red == ((red * 31 + 127) / 255 * 255 + 30) / 31);
      assert(green == ((green * 63 + 127) / 255 * 255 + 62) / 63);
      sum += red;
    }
  assert(std::abs(sum / 16 - 100) <= 4);
}

void testRing() {
  const int inner = 231, outer = 240;
  size_t total = 0;
  for (int dy = -outer; dy <= outer; ++dy) {
    int innerHalf = 0, outerHalf = 0;
    const int count = forecastFanRow(inner, outer, dy, innerHalf, outerHalf);
    total += count;
    // Každý pixel řádku leží v mezikruží a sousedé za hranou už ne.
    for (int dx = -outerHalf; dx <= outerHalf; ++dx) {
      const int d = dx * dx + dy * dy;
      const bool inRing = d >= inner * inner && d <= outer * outer;
      const bool listed = innerHalf == 0 || dx <= -innerHalf || dx >= innerHalf;
      assert(inRing == listed);
    }
    assert((outerHalf + 1) * (outerHalf + 1) + dy * dy > outer * outer);
  }
  assert(total == forecastFanPixelCount(inner, outer));
  // Plocha mezikruží: zhruba pí * (R² - r²), řádově 13 tisíc pixelů.
  const double area = 3.14159265 * (outer * outer - inner * inner);
  assert(std::fabs(static_cast<double>(total) - area) < area * 0.05);
}

void testDirtySectors() {
  ForecastFanKey a;
  float temperatures[12];
  for (int i = 0; i < 12; ++i) temperatures[i] = 10;
  forecastFanBuildKey(temperatures, 12, 10, 0, true, a);
  assert(forecastFanDirtySectors(a, a, false) == 0xFFF);
  assert(forecastFanDirtySectors(a, a, true) == 0);
  // Posun minuty mění jen výsek "teď".
  ForecastFanKey b = a;
  b.minute = 1;
  assert(forecastFanDirtySectors(a, b, true) == (1u << 10));
  // Jiná teplota na pozici 7 mění výseky 6 a 7.
  ForecastFanKey c = a;
  c.colors[7] = forecastFanTemperatureColor(30);
  assert(forecastFanDirtySectors(a, c, true) == ((1u << 6) | (1u << 7)));
  // Další hodina: šev se přestěhuje a pozice, která právě prošla, dostane
  // hodinu o 12 dál.
  float later[12];
  for (int i = 0; i < 12; ++i) later[i] = i == 11 ? 20 : 10;
  ForecastFanKey d;
  forecastFanBuildKey(later, 12, 11, 0, true, d);
  const uint32_t mask = forecastFanDirtySectors(a, d, true);
  assert(mask & (1u << 10));
  assert(mask & (1u << 11));
  assert(!(mask & (1u << 3)));
  // Přepnutí 12/24 hodin překreslí všechno.
  ForecastFanKey e;
  float day[24];
  for (int i = 0; i < 24; ++i) day[i] = 10;
  forecastFanBuildKey(day, 24, 10, 0, true, e);
  assert(forecastFanDirtySectors(a, e, true) == 0xFFFFFF);
}

void testSectorBoxes() {
  const int inner = 231, outer = 240;
  for (int sectors : {12, 24}) {
    for (int s = 0; s < sectors; ++s) {
      const ForecastFanBox box = forecastFanSectorBox(sectors, s, inner, outer);
      // Každý pixel mezikruží, který do výseku patří, leží v obdélníku.
      for (int dy = -outer; dy <= outer; ++dy)
        for (int dx = -outer; dx <= outer; ++dx) {
          const int d = dx * dx + dy * dy;
          if (d < inner * inner || d > outer * outer) continue;
          ForecastFanKey key;
          key.sectors = static_cast<uint8_t>(sectors);
          if (forecastFanSectorAt(key, dx, dy) != s) continue;
          assert(dx >= box.x1 && dx <= box.x2 && dy >= box.y1 && dy <= box.y2);
        }
      // A obdélník je malý: výsek 30° nezabere víc než čtvrtinu displeje.
      assert((box.x2 - box.x1) * (box.y2 - box.y1) < outer * outer);
    }
  }
}

void testStrip() {
  uint32_t colors[FORECAST_STRIP_POINTS];
  for (int i = 0; i < FORECAST_STRIP_POINTS; ++i) colors[i] = i < 12 ? 0x000000 : 0xFFFFFF;
  const int width = 736;
  // Začátek je barva první hodiny, konec poslední.
  assert(forecastStripPixel(colors, 0, width, 0) < 0x101010);
  assert(((forecastStripPixel(colors, width - 1, width, 0) >> 8) & 255) > 150);
}

}  // namespace

int main() {
  testTemperatureColors();
  testKey();
  testAngles();
  testSeam();
  testDither();
  testRing();
  testDirtySectors();
  testSectorBoxes();
  testStrip();
  std::puts("forecast_fan OK");
  return 0;
}
