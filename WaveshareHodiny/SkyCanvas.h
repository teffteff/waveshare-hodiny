#pragma once

#include <stdint.h>

// Kruh oblohy společný družicím i noční obloze: zenit uprostřed, obzor na
// okraji, zvolený azimut nahoře. Kreslí do bufferu RGB565 480 x 480 přes
// MapCanvas a v červeném nočním režimu převádí barvy na odstíny červené.
//
// Odděleno od SatelliteService, aby se obloha dala kreslit a testovat i na
// počítači (SkyRender).

constexpr int SKY_CANVAS_SIZE = 480;
constexpr int SKY_CANVAS_CENTER_X = SKY_CANVAS_SIZE / 2;
constexpr int SKY_CANVAS_CENTER_Y = SKY_CANVAS_SIZE / 2;
// Obzor. Kruh displeje má poloměr 240; světové strany stojí uvnitř obzoru.
constexpr int SKY_CANVAS_RADIUS = 222;

class SkyCanvas {
 public:
  SkyCanvas(uint16_t *pixels, uint16_t topBearingDeg, bool night);

  uint16_t *pixels() const { return pixels_; }
  bool night() const { return night_; }

  // Barva po průchodu nočním režimem: jas se zachová, odstín je červený.
  uint16_t color(uint16_t rgb565) const;

  // Bod kruhu (x, y v jednotkovém kruhu, sever nahoře) na displej se
  // zvoleným azimutem nahoře. Nesmyslné hodnoty se oříznou dřív, než se
  // převedou na int.
  void project(float x, float y, int &screenX, int &screenY) const;

  // Černé pozadí, obzor, kružnice 30° a 60°, osy, světové strany a
  // tečkovaná kružnice nejmenší výšky (0 = žádná).
  void clear() const;
  void drawGrid(uint8_t minElevationDeg, bool english) const;

 private:
  uint16_t *pixels_ = nullptr;
  uint16_t topBearingDeg_ = 0;
  bool night_ = false;
  float sine_ = 0.0f;
  float cosine_ = 1.0f;
};
