#pragma once

#include <stddef.h>
#include <stdint.h>

#include "MapCanvas.h"
#include "TrafficFeed.h"

class MapLabelFont;

// Kresba mapy obrazovky Doprava do bufferu 480x480 RGB565: silnice obarvené
// podle provozu, sjezdy D1, místa s českými jmény, autobusy a drobná legenda.
// Bez sítě a bez FreeRTOS, aby se dala prohlédnout na počítači; TrafficService
// ji jen volá se sdíleným snímkem.
//
// Souřadnice posílá server už v pixelech displeje, takže tu žádná projekce
// není. Text kolem mapy (nadpis, čas dat, stavový řádek) kreslí ClockDashboard
// jako popisky LVGL; kresba si pro ně jen nechá volné pásy.

constexpr int TRAFFIC_MAP_SIZE = 480;
constexpr int TRAFFIC_MAP_CENTER = TRAFFIC_MAP_SIZE / 2;
// Popisky musí zůstat uvnitř kulatého displeje i s rezervou na jeho okraj.
constexpr int TRAFFIC_MAP_LABEL_RADIUS = 226;

// Pásy, které drží LVGL popisky obrazovky (y od horního okraje). Sdílí je
// ClockDashboard, aby se kresba a popisky nerozešly.
constexpr int TRAFFIC_TITLE_OFFSET_Y = -158;
// Stavová hláška (chyba, stará data) leží pod legendou, u spodního okraje.
constexpr int TRAFFIC_STATUS_OFFSET_Y = 180;
// Legenda dole nad stavovou hláškou; horní hrana obálky popisku.
constexpr int TRAFFIC_LEGEND_Y = 392;
// Spodní hrana pásu nahoře (tečky obrazovek, stavový řádek, nadpis).
constexpr int TRAFFIC_TOP_BAND_END_Y = 93;

// Mapa obrazovky Doprava na 7": vlevo pod lištou, vedle sloupce s cestami.
constexpr int TRAFFIC_LCD7_MAP_WIDTH = 440;
constexpr int TRAFFIC_LCD7_MAP_HEIGHT = 438;

// Barva úrovně provozu v RGB565 (bez nočního převodu).
uint16_t trafficLevelColor(uint8_t level);
// Totéž jako 0xRRGGBB pro popisky LVGL na textové stránce.
uint32_t trafficLevelRgb(uint8_t level);

struct TrafficRenderResult {
  // Obálky popisků míst, jak se nakonec položily (pro testy).
  uint8_t placeLabelCount = 0;
  MapLabelBox placeLabels[TRAFFIC_MAX_PLACES] = {};
  uint8_t busCount = 0;
  bool legendDrawn = false;
};

// Rozměr a tvar mapy. Kulatý 2,1" kreslí do kruhu 480 x 480 s pásy pro
// popisky obrazovky; 7" do obdélníku vlevo od sloupce s cestami, popisky
// smí až k jeho okraji a legenda leží vlevo dole.
struct TrafficRenderLayout {
  int width = TRAFFIC_MAP_SIZE;
  int height = TRAFFIC_MAP_SIZE;
  bool rectangular = false;
};

// Server promítá do kruhu 480 x 480; obdélník 7" má jiný tvar i velikost.
// Posune a zvětší všechny body (čáry, místa, sjezdy, autobusy) tak, aby
// jejich obálka vyplnila width x height s okrajem margin, nejvýš maxScale krát.
void trafficFitToRect(TrafficData &data, int width, int height, int margin,
                      float maxScale);

// Nakreslí mapu. Bez dat (data == nullptr) jen černé pozadí. night převede
// všechny barvy do odstínů červené jako na ostatních mapových obrazovkách.
// labelFont píše česká jména míst (clock_czech_14 bez verzálek); patří úloze,
// která kreslí. yield, je-li zadaný, se volá mezi delšími kusy práce, aby
// úloha pustila procesor hlídacímu psu.
void trafficRender(uint16_t *pixels, const TrafficData *data, bool night,
                   bool english, MapLabelFont &labelFont, void (*yield)(),
                   TrafficRenderResult &result,
                   const TrafficRenderLayout &layout = TrafficRenderLayout{});
