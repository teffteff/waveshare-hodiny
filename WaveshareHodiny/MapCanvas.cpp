#include "MapCanvas.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {

uint8_t lineOutCode(int x, int y) {
  return (x < 0 ? 1 : 0) | (x >= MAP_CANVAS_WIDTH ? 2 : 0) |
         (y < 0 ? 4 : 0) | (y >= MAP_CANVAS_HEIGHT ? 8 : 0);
}

// Písmo 5x7, sloupec po sloupci, bit 0 je horní řádek. Číslice a interpunkce
// jsou převzaté z klasického fontu Adafruit GFX, verzálky mají o něco širší
// kresbu, aby popisky měst na mapě držely tvar i přes jeden pixel.
const uint8_t *upperGlyph(char character) {
  static const uint8_t glyphs[26][5] = {
      {0x7e, 0x11, 0x11, 0x11, 0x7e}, {0x7f, 0x49, 0x49, 0x49, 0x36},
      {0x3e, 0x41, 0x41, 0x41, 0x22}, {0x7f, 0x41, 0x41, 0x22, 0x1c},
      {0x7f, 0x49, 0x49, 0x49, 0x41}, {0x7f, 0x09, 0x09, 0x09, 0x01},
      {0x3e, 0x41, 0x49, 0x49, 0x7a}, {0x7f, 0x08, 0x08, 0x08, 0x7f},
      {0x00, 0x41, 0x7f, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3f, 0x01},
      {0x7f, 0x08, 0x14, 0x22, 0x41}, {0x7f, 0x40, 0x40, 0x40, 0x40},
      {0x7f, 0x02, 0x0c, 0x02, 0x7f}, {0x7f, 0x04, 0x08, 0x10, 0x7f},
      {0x3e, 0x41, 0x41, 0x41, 0x3e}, {0x7f, 0x09, 0x09, 0x09, 0x06},
      {0x3e, 0x41, 0x51, 0x21, 0x5e}, {0x7f, 0x09, 0x19, 0x29, 0x46},
      {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7f, 0x01, 0x01},
      {0x3f, 0x40, 0x40, 0x40, 0x3f}, {0x1f, 0x20, 0x40, 0x20, 0x1f},
      {0x3f, 0x40, 0x38, 0x40, 0x3f}, {0x63, 0x14, 0x08, 0x14, 0x63},
      {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43},
  };
  if (character >= 'A' && character <= 'Z') return glyphs[character - 'A'];
  return nullptr;
}

const uint8_t *symbolGlyph(char character) {
  static const uint8_t digits[10][5] = {
      {0x3e, 0x51, 0x49, 0x45, 0x3e}, {0x00, 0x42, 0x7f, 0x40, 0x00},
      {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4b, 0x31},
      {0x18, 0x14, 0x12, 0x7f, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
      {0x3c, 0x4a, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
      {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1e},
  };
  static const uint8_t slash[5] = {0x20, 0x10, 0x08, 0x04, 0x02};
  static const uint8_t greater[5] = {0x00, 0x41, 0x22, 0x14, 0x08};
  static const uint8_t less[5] = {0x00, 0x08, 0x14, 0x22, 0x41};
  // Hvězdička u počtu letadel značí, že mezi nimi je ten hlídaný.
  static const uint8_t star[5] = {0x14, 0x08, 0x3e, 0x08, 0x14};
  if (character >= '0' && character <= '9') return digits[character - '0'];
  switch (character) {
    case '/': return slash;
    case '>': return greater;
    case '<': return less;
    case '*': return star;
    default: return nullptr;
  }
}

// Jeden znak. Vrací false pro to, co se kreslí zvlášť (pomlčka, tečka) nebo
// co v písmu není.
bool drawGlyph(uint16_t *buffer, int x, int y, char character, uint16_t color,
               uint8_t opacity) {
  const uint8_t *glyph = upperGlyph(character);
  if (glyph == nullptr) glyph = symbolGlyph(character);
  if (glyph != nullptr) {
    for (int column = 0; column < 5; ++column)
      for (int row = 0; row < MAP_CANVAS_GLYPH_HEIGHT; ++row)
        if (glyph[column] & (1U << row))
          setMapPixel(buffer, x + column, y + row, color, opacity);
    return true;
  }
  if (character == '-') {
    for (int column = 1; column < 5; ++column)
      setMapPixel(buffer, x + column, y + 3, color, opacity);
    return true;
  }
  if (character == '.') {
    setMapPixel(buffer, x + 2, y + 6, color, opacity);
    return true;
  }
  return false;
}

}  // namespace

uint16_t blendRgb565(uint16_t background, uint16_t foreground,
                     uint8_t opacity) {
  if (opacity == 0) return background;
  if (opacity >= 100) return foreground;
  const uint16_t inverse = 100 - opacity;
  const uint16_t red =
      (((background >> 11) & 0x1f) * inverse +
       ((foreground >> 11) & 0x1f) * opacity + 50) /
      100;
  const uint16_t green =
      (((background >> 5) & 0x3f) * inverse +
       ((foreground >> 5) & 0x3f) * opacity + 50) /
      100;
  const uint16_t blue =
      ((background & 0x1f) * inverse + (foreground & 0x1f) * opacity + 50) /
      100;
  return static_cast<uint16_t>((red << 11) | (green << 5) | blue);
}

void setMapPixel(uint16_t *buffer, int x, int y, uint16_t color,
                 uint8_t opacity) {
  if (x >= 0 && x < MAP_CANVAS_WIDTH && y >= 0 && y < MAP_CANVAS_HEIGHT) {
    uint16_t &pixel = buffer[y * MAP_CANVAS_WIDTH + x];
    pixel = blendRgb565(pixel, color, opacity);
  }
}

void drawMapLine(uint16_t *buffer, int x0, int y0, int x1, int y1,
                 uint16_t color, uint8_t opacity) {
  uint8_t code0 = lineOutCode(x0, y0);
  uint8_t code1 = lineOutCode(x1, y1);
  while (code0 || code1) {
    if (code0 & code1) return;
    const uint8_t code = code0 ? code0 : code1;
    int x = 0;
    int y = 0;
    if (code & 8) {
      y = MAP_CANVAS_HEIGHT - 1;
      x = x0 + static_cast<int64_t>(x1 - x0) * (y - y0) / (y1 - y0);
    } else if (code & 4) {
      y = 0;
      x = x0 + static_cast<int64_t>(x1 - x0) * (y - y0) / (y1 - y0);
    } else if (code & 2) {
      x = MAP_CANVAS_WIDTH - 1;
      y = y0 + static_cast<int64_t>(y1 - y0) * (x - x0) / (x1 - x0);
    } else {
      x = 0;
      y = y0 + static_cast<int64_t>(y1 - y0) * (x - x0) / (x1 - x0);
    }
    if (code == code0) {
      x0 = x;
      y0 = y;
      code0 = lineOutCode(x0, y0);
    } else {
      x1 = x;
      y1 = y;
      code1 = lineOutCode(x1, y1);
    }
  }
  const int deltaX = abs(x1 - x0);
  const int stepX = x0 < x1 ? 1 : -1;
  const int deltaY = -abs(y1 - y0);
  const int stepY = y0 < y1 ? 1 : -1;
  int error = deltaX + deltaY;
  for (;;) {
    setMapPixel(buffer, x0, y0, color, opacity);
    if (x0 == x1 && y0 == y1) break;
    const int doubled = 2 * error;
    if (doubled >= deltaY) {
      error += deltaY;
      x0 += stepX;
    }
    if (doubled <= deltaX) {
      error += deltaX;
      y0 += stepY;
    }
  }
}

void fillMapRect(uint16_t *buffer, int x, int y, int width, int height,
                 uint16_t color, uint8_t opacity) {
  for (int row = y; row < y + height; ++row)
    for (int column = x; column < x + width; ++column)
      setMapPixel(buffer, column, row, color, opacity);
}

void drawMapCircle(uint16_t *buffer, int centerX, int centerY, int radius,
                   uint16_t color, uint8_t opacity) {
  if (radius < 0) return;
  if (radius == 0) {
    setMapPixel(buffer, centerX, centerY, color, opacity);
    return;
  }
  // Bod uprostřed hrany: jen sčítání, žádná odmocnina na pixel.
  int x = 0;
  int y = radius;
  int error = 1 - radius;
  while (x <= y) {
    setMapPixel(buffer, centerX + x, centerY + y, color, opacity);
    setMapPixel(buffer, centerX - x, centerY + y, color, opacity);
    setMapPixel(buffer, centerX + x, centerY - y, color, opacity);
    setMapPixel(buffer, centerX - x, centerY - y, color, opacity);
    setMapPixel(buffer, centerX + y, centerY + x, color, opacity);
    setMapPixel(buffer, centerX - y, centerY + x, color, opacity);
    setMapPixel(buffer, centerX + y, centerY - x, color, opacity);
    setMapPixel(buffer, centerX - y, centerY - x, color, opacity);
    ++x;
    if (error < 0) {
      error += 2 * x + 1;
    } else {
      --y;
      error += 2 * (x - y) + 1;
    }
  }
}

void fillMapCircle(uint16_t *buffer, int centerX, int centerY, int radius,
                   uint16_t color, uint8_t opacity) {
  if (radius < 0) return;
  for (int offsetY = -radius; offsetY <= radius; ++offsetY) {
    const int span =
        static_cast<int>(sqrtf(static_cast<float>(radius) * radius -
                               static_cast<float>(offsetY) * offsetY));
    for (int offsetX = -span; offsetX <= span; ++offsetX)
      setMapPixel(buffer, centerX + offsetX, centerY + offsetY, color, opacity);
  }
}

void fillMapTriangle(uint16_t *buffer, int x0, int y0, int x1, int y1, int x2,
                     int y2, uint16_t color, uint8_t opacity) {
  // Seřadit vrcholy shora dolů, pak dvě smyčky vodorovných úseček.
  if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
  if (y1 > y2) { int t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
  if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }

  if (y0 == y2) {
    // Zdegenerovaný trojúhelník je úsečka.
    int left = x0 < x1 ? x0 : x1;
    if (x2 < left) left = x2;
    int right = x0 > x1 ? x0 : x1;
    if (x2 > right) right = x2;
    for (int x = left; x <= right; ++x)
      setMapPixel(buffer, x, y0, color, opacity);
    return;
  }

  for (int y = y0; y <= y2; ++y) {
    // Dlouhá strana vede přes celou výšku, krátká se v půlce mění.
    const int longX =
        x0 + static_cast<int>(static_cast<int64_t>(x2 - x0) * (y - y0) /
                              (y2 - y0));
    int shortX;
    if (y < y1) {
      if (y1 == y0) continue;
      shortX = x0 + static_cast<int>(static_cast<int64_t>(x1 - x0) * (y - y0) /
                                     (y1 - y0));
    } else {
      if (y2 == y1) {
        shortX = x1;
      } else {
        shortX = x1 + static_cast<int>(static_cast<int64_t>(x2 - x1) *
                                       (y - y1) / (y2 - y1));
      }
    }
    const int left = longX < shortX ? longX : shortX;
    const int right = longX > shortX ? longX : shortX;
    for (int x = left; x <= right; ++x)
      setMapPixel(buffer, x, y, color, opacity);
  }
}

void drawMapText(uint16_t *buffer, int x, int y, const char *text,
                 uint16_t color, uint8_t opacity) {
  for (size_t index = 0; text[index] != '\0'; ++index) {
    const char character = static_cast<char>(
        toupper(static_cast<unsigned char>(text[index])));
    drawGlyph(buffer, x + static_cast<int>(index) * MAP_CANVAS_GLYPH_ADVANCE, y,
              character, color, opacity);
  }
}

int mapTextWidth(const char *text) {
  if (text == nullptr || text[0] == '\0') return 0;
  return static_cast<int>(strlen(text)) * MAP_CANVAS_GLYPH_ADVANCE - 1;
}

bool mapBoxesOverlap(const MapLabelBox &left, const MapLabelBox &right) {
  return left.x < right.x + right.width && left.x + left.width > right.x &&
         left.y < right.y + right.height && left.y + left.height > right.y;
}

bool MapLabelPlacer::claim(const MapLabelBox &box) {
  if (count >= MAP_LABEL_CAPACITY) return false;
  for (size_t index = 0; index < count; ++index)
    if (mapBoxesOverlap(box, occupied[index])) return false;
  occupied[count++] = box;
  return true;
}
