#pragma once

#include <Arduino.h>

// Sdílené kreslení do hotového rámce RGB565. Obrazovky, které si obrázek
// skládají samy a hotový buffer teprve předají LVGL canvasu - meteoradar a
// radar letadel - potřebují tytéž základní tahy: pixel s průhledností, úsečku,
// obdélník a drobné písmo. Dřív je měl každý svoje, takže se stejná Bresenhamova
// úsečka a stejná tabulka glyfů překládaly dvakrát.
//
// Rozměr je pevný: obě obrazovky kreslí přes celý kruhový displej.
constexpr int MAP_CANVAS_WIDTH = 480;
constexpr int MAP_CANVAS_HEIGHT = 480;

// Šířka jednoho znaku i s mezerou. Písmo je 5 px široké, mezera je šestý.
constexpr int MAP_CANVAS_GLYPH_ADVANCE = 6;
constexpr int MAP_CANVAS_GLYPH_HEIGHT = 7;

// Průhlednost je v procentech (0 = nic, 100 = plná barva), stejně jako ji
// zadává nastavení radaru.
uint16_t blendRgb565(uint16_t background, uint16_t foreground, uint8_t opacity);

void setMapPixel(uint16_t *buffer, int x, int y, uint16_t color,
                 uint8_t opacity);

// Úsečka ořezaná Cohen-Sutherlandem a vykreslená Bresenhamem. Body smí ležet
// daleko mimo displej - mapová data se promítají bez ohledu na okraje.
void drawMapLine(uint16_t *buffer, int x0, int y0, int x1, int y1,
                 uint16_t color, uint8_t opacity);

void fillMapRect(uint16_t *buffer, int x, int y, int width, int height,
                 uint16_t color, uint8_t opacity);

// Obrys a výplň kruhu. Radar letadel z nich staví dosahové kružnice, kroužek
// kolem vybraného letadla a značku letadla s neznámým směrem.
void drawMapCircle(uint16_t *buffer, int centerX, int centerY, int radius,
                   uint16_t color, uint8_t opacity);
void fillMapCircle(uint16_t *buffer, int centerX, int centerY, int radius,
                   uint16_t color, uint8_t opacity);

// Vyplněný trojúhelník. Ikona letadla je vějíř trojúhelníků ze středu, takže
// se pootočený tvar vykreslí bez obecného vyplňování mnohoúhelníku.
void fillMapTriangle(uint16_t *buffer, int x0, int y0, int x1, int y1, int x2,
                     int y2, uint16_t color, uint8_t opacity);

// Text VELKÝMI písmeny. Malá písmena se převedou, protože mapové popisky mají
// všechny stejnou výšku a diakritika v tabulce stejně není.
void drawMapText(uint16_t *buffer, int x, int y, const char *text,
                 uint16_t color, uint8_t opacity);

// Šířka textu v pixelech bez koncové mezery.
int mapTextWidth(const char *text);

struct MapLabelBox {
  int x;
  int y;
  int width;
  int height;
};

bool mapBoxesOverlap(const MapLabelBox &left, const MapLabelBox &right);

// Popisky se rozmisťují tak, aby na sebe nelezly: kdo si místo zabere první,
// ten si ho nechá. Města se proto procházejí po úrovních od největších.
// Evropská data mají přes tisíc měst, ale na kruh se jich vejde jen hrstka,
// takže seznam obsazených míst stačí pevný.
constexpr size_t MAP_LABEL_CAPACITY = 64;

struct MapLabelPlacer {
  MapLabelBox occupied[MAP_LABEL_CAPACITY] = {};
  size_t count = 0;

  void reset() { count = 0; }
  bool claim(const MapLabelBox &box);
};
