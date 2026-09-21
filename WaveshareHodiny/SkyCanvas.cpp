#include "SkyCanvas.h"

#include <cmath>

#include "MapCanvas.h"
#include "SatelliteFeed.h"

namespace {

constexpr float DEGREES_TO_RADIANS = 0.0174532925f;
constexpr uint16_t COLOR_BLACK = 0x0000;
constexpr uint16_t COLOR_GRAY = 0x8410;
constexpr uint16_t COLOR_DARK_GRAY = 0x4208;
constexpr uint16_t COLOR_RING = 0x3186;

}  // namespace

SkyCanvas::SkyCanvas(uint16_t *pixels, uint16_t topBearingDeg, bool night)
    : pixels_(pixels), topBearingDeg_(topBearingDeg), night_(night) {
  const float topRadians = topBearingDeg * DEGREES_TO_RADIANS;
  sine_ = std::sin(topRadians);
  cosine_ = std::cos(topRadians);
}

uint16_t SkyCanvas::color(uint16_t rgb565) const {
  if (!night_) return rgb565;
  const uint16_t red = ((rgb565 >> 11) & 0x1f) << 3;
  const uint16_t green = ((rgb565 >> 5) & 0x3f) << 2;
  const uint16_t blue = (rgb565 & 0x1f) << 3;
  const uint16_t luma = (77 * red + 150 * green + 29 * blue) >> 8;
  return static_cast<uint16_t>((luma >> 3) << 11);
}

void SkyCanvas::project(float x, float y, int &screenX, int &screenY) const {
  const float rotatedX = x * cosine_ + y * sine_;
  const float rotatedY = y * cosine_ - x * sine_;
  const auto toPixel = [](float value) {
    if (!std::isfinite(value)) return 0;
    if (value > 4.0f) value = 4.0f;
    if (value < -4.0f) value = -4.0f;
    return static_cast<int>(std::lround(value * SKY_CANVAS_RADIUS));
  };
  screenX = SKY_CANVAS_CENTER_X + toPixel(rotatedX);
  screenY = SKY_CANVAS_CENTER_Y + toPixel(rotatedY);
}

void SkyCanvas::clear() const {
  const uint16_t background = color(COLOR_BLACK);
  const int count = SKY_CANVAS_SIZE * SKY_CANVAS_SIZE;
  for (int index = 0; index < count; ++index) pixels_[index] = background;
}

void SkyCanvas::drawGrid(uint8_t minElevationDeg, bool english) const {
  const uint16_t ring = color(COLOR_RING);
  drawMapCircle(pixels_, SKY_CANVAS_CENTER_X, SKY_CANVAS_CENTER_Y,
                SKY_CANVAS_RADIUS, color(COLOR_DARK_GRAY), 100);
  drawMapCircle(pixels_, SKY_CANVAS_CENTER_X, SKY_CANVAS_CENTER_Y,
                SKY_CANVAS_RADIUS * 2 / 3, ring, 100);
  drawMapCircle(pixels_, SKY_CANVAS_CENTER_X, SKY_CANVAS_CENTER_Y,
                SKY_CANVAS_RADIUS / 3, ring, 100);
  // Osy sever-jih a východ-západ, otočené s oblohou.
  for (int bearing = 0; bearing < 180; bearing += 90) {
    float x = 0.0f;
    float y = 0.0f;
    satelliteSkyProject(static_cast<float>(bearing), 0.0f, x, y);
    int ax = 0;
    int ay = 0;
    int bx = 0;
    int by = 0;
    project(x, y, ax, ay);
    project(-x, -y, bx, by);
    drawMapLine(pixels_, ax, ay, bx, by, ring, 100);
  }
  if (minElevationDeg > 0) {
    // Tečka každé tři stupně; plná kružnice by se pletla s obzorem.
    const int radius = SKY_CANVAS_RADIUS * (90 - minElevationDeg) / 90;
    for (int degree = 0; degree < 360; degree += 3) {
      const float angle = degree * DEGREES_TO_RADIANS;
      setMapPixel(pixels_,
                  SKY_CANVAS_CENTER_X +
                      static_cast<int>(std::lround(radius * std::sin(angle))),
                  SKY_CANVAS_CENTER_Y -
                      static_cast<int>(std::lround(radius * std::cos(angle))),
                  color(COLOR_GRAY), 100);
    }
  }
  for (int offset = -5; offset <= 5; ++offset) {
    setMapPixel(pixels_, SKY_CANVAS_CENTER_X + offset, SKY_CANVAS_CENTER_Y,
                color(COLOR_GRAY), 100);
    setMapPixel(pixels_, SKY_CANVAS_CENTER_X, SKY_CANVAS_CENTER_Y + offset,
                color(COLOR_GRAY), 100);
  }
  // Světové strany uvnitř obzoru.
  static const char *const CZECH[4] = {"S", "V", "J", "Z"};
  static const char *const ENGLISH[4] = {"N", "E", "S", "W"};
  const char *const *labels = english ? ENGLISH : CZECH;
  constexpr int MARK_RADIUS = SKY_CANVAS_RADIUS - 12;
  for (int index = 0; index < 4; ++index) {
    const float angle =
        (index * 90 - static_cast<int>(topBearingDeg_)) * DEGREES_TO_RADIANS;
    const int x = SKY_CANVAS_CENTER_X +
                  static_cast<int>(std::lround(MARK_RADIUS * std::sin(angle))) - 2;
    const int y = SKY_CANVAS_CENTER_Y -
                  static_cast<int>(std::lround(MARK_RADIUS * std::cos(angle))) - 3;
    drawMapText(pixels_, x, y, labels[index], color(COLOR_GRAY), 100);
  }
  // Popisky výšek kruhů pod zenitem, u jižní poloviny osy.
  drawMapText(pixels_, SKY_CANVAS_CENTER_X + 4,
              SKY_CANVAS_CENTER_Y + SKY_CANVAS_RADIUS / 3 - 9, "60",
              color(COLOR_DARK_GRAY), 100);
  drawMapText(pixels_, SKY_CANVAS_CENTER_X + 4,
              SKY_CANVAS_CENTER_Y + SKY_CANVAS_RADIUS * 2 / 3 - 9, "30",
              color(COLOR_DARK_GRAY), 100);
}
