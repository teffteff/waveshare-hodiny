#pragma once

// Prstenec předpovědi kolem ciferníku ("vějíř" z CooLajz/waveshare-hodiny,
// ForecastDial.cpp a ForecastFanRegion.h, licence MIT, viz
// THIRD_PARTY_NOTICES.md). Každá hodina má na ciferníku svůj výsek obarvený
// podle předpovězené teploty a sousední výseky do sebe plynule přecházejí.
// Ve výseku právě probíhající hodiny leží šev: za ním začíná nejbližší hodina,
// před ním je ta nejvzdálenější (o 12 nebo 24 hodin dál).
//
// Na rozdíl od upstreamu, který drží celý obrázek 480 x 480, se tu ukládá jen
// mezikruží (asi 37 tisíc pixelů místo 230 tisíc) a překreslují se jen výseky,
// jejichž barvy nebo šev se změnily. Na 7" vedle digitálních hodin je místo
// prstence pás na 24 hodin (forecastStripPixel).
//
// Jen čistá logika bez LVGL, aby šla testovat na počítači
// (tools/test_forecast_fan.cpp).

#include <cmath>
#include <cstddef>
#include <cstdint>

constexpr uint8_t FORECAST_FAN_MAX_SECTORS = 24;
// Šedá pro hodiny, ke kterým předpověď nemá teplotu.
constexpr uint32_t FORECAST_FAN_MISSING_COLOR = 0x30343A;
// Prstenec svítí na tři čtvrtiny, aby nepřebil hodnoty ciferníku.
constexpr float FORECAST_FAN_BRIGHTNESS = 0.75f;
// Pás na 7" pokrývá 24 hodin od začátku právě probíhající, tedy 25 bodů.
constexpr uint8_t FORECAST_STRIP_POINTS = 25;

// Pevná stupnice: stejná teplota má každý den stejnou barvu.
inline uint32_t forecastFanTemperatureColor(float temperature) {
  if (!std::isfinite(temperature)) return FORECAST_FAN_MISSING_COLOR;
  static const float STOPS[] = {-10, 0, 10, 20, 30, 40};
  static const uint32_t COLORS[] = {0x6868E8, 0x3294F0, 0x33C7CE,
                                    0x81CB68, 0xFFB843, 0xEF5543};
  if (temperature <= STOPS[0]) return COLORS[0];
  for (int i = 1; i < 6; ++i) {
    if (temperature <= STOPS[i]) {
      const float fraction = (temperature - STOPS[i - 1]) / 10.0f;
      uint32_t color = 0;
      for (int shift = 0; shift <= 16; shift += 8) {
        const float a = (COLORS[i - 1] >> shift) & 255;
        const float b = (COLORS[i] >> shift) & 255;
        const int value = static_cast<int>(std::lround(a + (b - a) * fraction));
        color |= static_cast<uint32_t>(value) << shift;
      }
      return color;
    }
  }
  return COLORS[5];
}

struct ForecastFanKey {
  // Barva na začátku každého výseku ciferníku (pozice hodiny 0..sectors-1).
  uint32_t colors[FORECAST_FAN_MAX_SECTORS] = {};
  uint8_t sectors = 12;
  // Výsek a minuta "teď" (místní čas).
  uint8_t hour = 0;
  uint8_t minute = 0;
  bool valid = false;
};

inline bool operator==(const ForecastFanKey &a, const ForecastFanKey &b) {
  if (a.sectors != b.sectors || a.hour != b.hour || a.minute != b.minute ||
      a.valid != b.valid)
    return false;
  for (int i = 0; i < a.sectors; ++i)
    if (a.colors[i] != b.colors[i]) return false;
  return true;
}
inline bool operator!=(const ForecastFanKey &a, const ForecastFanKey &b) {
  return !(a == b);
}

// temperatures[i] je předpověď na začátek hodiny i+1 od teď (i = 0..sectors-1),
// NAN tam, kde chybí. localHour/minute je místní čas teď.
inline void forecastFanBuildKey(const float *temperatures, uint8_t sectors,
                                int localHour, int minute, bool valid,
                                ForecastFanKey &key) {
  key = ForecastFanKey{};
  key.sectors = sectors == 24 ? 24 : 12;
  key.valid = valid;
  key.hour = static_cast<uint8_t>(((localHour % key.sectors) + key.sectors) % key.sectors);
  key.minute = static_cast<uint8_t>(minute < 0 ? 0 : minute > 59 ? 59 : minute);
  for (int i = 0; i < key.sectors; ++i) {
    const int position = (key.hour + 1 + i) % key.sectors;
    key.colors[position] = valid ? forecastFanTemperatureColor(temperatures[i])
                                 : FORECAST_FAN_MISSING_COLOR;
  }
}

// Rozptýlení 4 x 4 (Bayer): plynulý přechod i v RGB565 bez schodů.
inline uint32_t forecastFanDither(float red, float green, float blue, int x,
                                  int y) {
  static const uint8_t BAYER[4][4] = {
      {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  const float threshold = (BAYER[y & 3][x & 3] + 0.5f) / 16.0f;
  auto channel = [threshold](float value, int levels) -> uint32_t {
    int quantized = static_cast<int>(value * levels / 255.0f + threshold);
    if (quantized < 0) quantized = 0;
    if (quantized > levels) quantized = levels;
    return static_cast<uint32_t>((quantized * 255 + levels - 1) / levels);
  };
  return (channel(red, 31) << 16) | (channel(green, 63) << 8) |
         channel(blue, 31);
}

inline uint32_t forecastFanMix(uint32_t left, uint32_t right, float fraction,
                               float brightness, int x, int y) {
  float rgb[3];
  for (int i = 0; i < 3; ++i) {
    const int shift = 16 - 8 * i;
    const float a = (left >> shift) & 255;
    const float b = (right >> shift) & 255;
    rgb[i] = (a + (b - a) * fraction) * brightness;
  }
  return forecastFanDither(rgb[0], rgb[1], rgb[2], x, y);
}

// Úhel bodu ve stupních po směru hodinových ručiček od dvanácté, 0..360.
inline float forecastFanAngle(int dx, int dy) {
  float angle = std::atan2(static_cast<float>(dx), static_cast<float>(-dy)) *
                (180.0f / 3.14159265358979f);
  if (angle < 0) angle += 360.0f;
  if (angle >= 360.0f) angle -= 360.0f;
  return angle;
}

inline int forecastFanSectorAt(const ForecastFanKey &key, int dx, int dy) {
  const int sector =
      static_cast<int>(forecastFanAngle(dx, dy) * key.sectors / 360.0f);
  return sector >= key.sectors ? key.sectors - 1 : sector;
}

// Barva pixelu prstence pod úhlem angle (forecastFanAngle), RGB888 už
// zaokrouhlená na RGB565; x, y jsou souřadnice pro rozptýlení.
inline uint32_t forecastFanPixelAt(const ForecastFanKey &key, float angle,
                                   int x, int y) {
  const float position = angle * key.sectors / 360.0f;
  int sector = static_cast<int>(position);
  if (sector >= key.sectors) sector = key.sectors - 1;
  float fraction = position - sector;
  // Výsek s "teď": poslední hodina předpovědi končí tady a hned za švem
  // začíná ta nejbližší. Mezi nimi se nepřechází.
  if (key.valid && sector == key.hour)
    fraction = fraction >= key.minute / 60.0f ? 1.0f : 0.0f;
  const uint32_t left = key.colors[sector];
  const uint32_t right = key.colors[(sector + 1) % key.sectors];
  return forecastFanMix(left, right, fraction, FORECAST_FAN_BRIGHTNESS, x, y);
}

// Totéž pro bod dx, dy od středu.
inline uint32_t forecastFanPixel(const ForecastFanKey &key, int dx, int dy,
                                 int x, int y) {
  return forecastFanPixelAt(key, forecastFanAngle(dx, dy), x, y);
}

// Úhel "teď" ve stupních (šev a značka na prstenci).
inline float forecastFanNowAngle(const ForecastFanKey &key) {
  return (key.hour + key.minute / 60.0f) * 360.0f / key.sectors;
}

// Které výseky se musí překreslit: výsek s závisí na barvách na obou svých
// koncích a výsek "teď" i na švu. Bit s = výsek s.
inline uint32_t forecastFanDirtySectors(const ForecastFanKey &previous,
                                        const ForecastFanKey &next,
                                        bool initialized) {
  const uint32_t all = next.sectors >= 32 ? 0xFFFFFFFFu
                                          : (1u << next.sectors) - 1u;
  if (!initialized || previous.sectors != next.sectors ||
      previous.valid != next.valid)
    return all;
  uint32_t mask = 0;
  for (int s = 0; s < next.sectors; ++s) {
    const int e = (s + 1) % next.sectors;
    if (previous.colors[s] != next.colors[s] ||
        previous.colors[e] != next.colors[e])
      mask |= 1u << s;
  }
  if (next.valid &&
      (previous.hour != next.hour || previous.minute != next.minute)) {
    mask |= 1u << previous.hour;
    mask |= 1u << next.hour;
  }
  return mask;
}

struct ForecastFanBox {
  int x1, y1, x2, y2;
};

// Obdélník kolem výseku mezikruží (souřadnice od středu), se dvěma pixely
// rezervy na zaokrouhlení. Výsek do 30° nemá uvnitř jiný extrém než osu,
// takže stačí krajní body na obou poloměrech a osy, které leží uvnitř.
inline ForecastFanBox forecastFanSectorBox(int sectors, int sector, int inner,
                                           int outer) {
  const float step = 360.0f / sectors;
  const float from = sector * step;
  const float to = (sector + 1) * step;
  ForecastFanBox box = {outer, outer, -outer, -outer};
  auto include = [&box](float degrees, int radius) {
    const float radians = degrees * (3.14159265358979f / 180.0f);
    const int x = static_cast<int>(std::lround(std::sin(radians) * radius));
    const int y = static_cast<int>(std::lround(-std::cos(radians) * radius));
    if (x - 2 < box.x1) box.x1 = x - 2;
    if (x + 2 > box.x2) box.x2 = x + 2;
    if (y - 2 < box.y1) box.y1 = y - 2;
    if (y + 2 > box.y2) box.y2 = y + 2;
  };
  include(from, inner);
  include(from, outer);
  include(to, inner);
  include(to, outer);
  for (int axis = 0; axis <= 360; axis += 90)
    if (axis > from && axis < to) include(static_cast<float>(axis), outer);
  if (box.x1 < -outer) box.x1 = -outer;
  if (box.y1 < -outer) box.y1 = -outer;
  if (box.x2 > outer) box.x2 = outer;
  if (box.y2 > outer) box.y2 = outer;
  return box;
}

// Řádek mezikruží ve vzdálenosti dy od středu: pixely s inner² <= dx²+dy² <=
// outer². outerHalf je největší |dx|, innerHalf nejmenší |dx| (0 = řádek je
// jediný souvislý úsek). Vrací počet pixelů řádku, 0 mimo mezikruží.
inline int forecastFanRow(int inner, int outer, int dy, int &innerHalf,
                          int &outerHalf) {
  innerHalf = 0;
  outerHalf = -1;
  const int ady = dy < 0 ? -dy : dy;
  if (ady > outer) return 0;
  const int outerSquare = outer * outer - ady * ady;
  int xo = static_cast<int>(std::sqrt(static_cast<float>(outerSquare)));
  while ((xo + 1) * (xo + 1) <= outerSquare) ++xo;
  while (xo * xo > outerSquare) --xo;
  outerHalf = xo;
  if (ady >= inner) return 2 * xo + 1;
  const int innerSquare = inner * inner - ady * ady;
  int xi = static_cast<int>(std::sqrt(static_cast<float>(innerSquare)));
  while (xi * xi < innerSquare) ++xi;
  while (xi > 0 && (xi - 1) * (xi - 1) >= innerSquare) --xi;
  innerHalf = xi;
  if (xi > xo) return 0;
  return 2 * (xo - xi + 1);
}

// Počet pixelů celého mezikruží (velikost bufferu).
inline size_t forecastFanPixelCount(int inner, int outer) {
  size_t total = 0;
  for (int dy = -outer; dy <= outer; ++dy) {
    int innerHalf = 0, outerHalf = 0;
    total += static_cast<size_t>(forecastFanRow(inner, outer, dy, innerHalf, outerHalf));
  }
  return total;
}

// Pás na 24 hodin (7"): colors[i] je barva začátku hodiny i od začátku té
// právě probíhající, i = 0..24. Pixel x z šířky width.
inline uint32_t forecastStripPixel(const uint32_t *colors, int x, int width,
                                   int y) {
  const float position =
      static_cast<float>(x) * (FORECAST_STRIP_POINTS - 1) / (width > 1 ? width - 1 : 1);
  int index = static_cast<int>(position);
  if (index >= FORECAST_STRIP_POINTS - 1) index = FORECAST_STRIP_POINTS - 2;
  return forecastFanMix(colors[index], colors[index + 1], position - index,
                        FORECAST_FAN_BRIGHTNESS, x, y);
}
