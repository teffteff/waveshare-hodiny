#pragma once

#include <stddef.h>
#include <stdint.h>

#include "SkyFeed.h"

// Kresba noční oblohy: hvězdy, obrazce souhvězdí, planety, Měsíc s fází
// a Slunce ve stejném kruhu jako družice. Bez sítě a bez FreeRTOS, aby se dala
// testovat a prohlédnout na počítači; SatelliteService ji jen volá se svým
// bufferem.
//
// Jména těles se do obrázku nekreslí: pixelové písmo mapy nemá diakritiku
// ("Venuše", "Měsíc"). Kresba jen najde místo vedle tečky a ClockDashboard
// tam položí popisek LVGL.

struct SkyLabel {
  int16_t x = 0;
  int16_t y = 0;
  char name[SKY_NAME_LENGTH] = "";
  // Barva tělesa, 0xRRGGBB.
  uint32_t color = 0xFFFFFF;
};

struct SkyDetail {
  bool open = false;
  char name[SKY_NAME_LENGTH] = "";
  uint8_t kind = SKY_BODY_PLANET;
  float azimuthDeg = 0.0f;
  float altitudeDeg = 0.0f;
  bool hasMagnitude = false;
  float magnitude = 0.0f;
  float illumination = 0.0f;
  uint32_t distanceKm = 0;
  float distanceAu = 0.0f;
  char constellation[SKY_CONSTELLATION_LENGTH] = "";
  int64_t rise = 0;
  int64_t set = 0;
};

// Místo tělesa na displeji pro výběr klepnutím.
struct SkyTapPoint {
  int16_t x = -32768;
  int16_t y = -32768;
  char id[10] = "";
};

struct SkyRenderResult {
  SkyLabel labels[SKY_MAX_BODIES];
  uint8_t labelCount = 0;
  SkyTapPoint taps[SKY_MAX_BODIES];
  uint8_t tapCount = 0;
  // Vybrané těleso; open jen tehdy, když je v datech.
  SkyDetail detail;
  // Slunce aspoň šest stupňů pod obzorem, nebo nad ním.
  bool dark = false;
  bool sunUp = false;
  // Planet nad obzorem, které jde vidět okem (Merkur až Saturn).
  uint8_t planetsUp = 0;
};

// Řádky úkazů leží nad severní oblohou. Planety i Měsíc mají deklinaci nejvýš
// kolem 29°, takže ze středních severních šířek se do horní poloviny kruhu
// se severem nahoře skoro nedostanou - na jihu by řádky zakrývaly právě je.
// Se severem dole (azimut nahoře mezi 90° a 270°) nebo na jižní polokouli jdou
// řádky dolů. Hodnoty sdílí ClockDashboard; posun je od středu displeje.
constexpr int SKY_EVENTS_TOP_OFFSET_Y = -136;
constexpr int SKY_EVENTS_BOTTOM_OFFSET_Y = 122;
constexpr int SKY_EVENTS_ROW_STEP = 21;

inline bool skyEventsAtTop(uint16_t topBearingDeg, float latitude) {
  const bool northAtTop = topBearingDeg < 90 || topBearingDeg > 270;
  return northAtTop == (latitude >= 0.0f);
}

inline int skyEventRowOffsetY(bool atTop, int row) {
  return (atTop ? SKY_EVENTS_TOP_OFFSET_Y : SKY_EVENTS_BOTTOM_OFFSET_Y) +
         row * SKY_EVENTS_ROW_STEP;
}

// Nakreslí oblohu do bufferu 480 x 480 RGB565. Bez dat (feed == nullptr)
// jen prázdný kruh. selectedId je id vybraného tělesa nebo prázdný řetězec.
void skyRender(uint16_t *pixels, const SkyFeed *feed, float latitude,
               float longitude, double epoch, uint16_t topBearingDeg,
               bool night, bool english, const char *selectedId,
               SkyRenderResult &result);
