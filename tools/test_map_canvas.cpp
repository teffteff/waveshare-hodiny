#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

#include "MapCanvas.h"

namespace {

using Buffer = std::vector<uint16_t>;

Buffer makeBuffer() {
  return Buffer(static_cast<size_t>(MAP_CANVAS_WIDTH) * MAP_CANVAS_HEIGHT, 0);
}

uint16_t at(const Buffer &buffer, int x, int y) {
  return buffer[static_cast<size_t>(y) * MAP_CANVAS_WIDTH + x];
}

size_t litPixels(const Buffer &buffer) {
  size_t count = 0;
  for (uint16_t pixel : buffer)
    if (pixel != 0) ++count;
  return count;
}

void testBlendEndpoints() {
  // Nula a sto jsou zkratky, ne výpočet - musí vrátit přesně původní barvy,
  // jinak by průhledná mapová vrstva zesvětlila i to, co má nechat být.
  assert(blendRgb565(0x1234, 0xf800, 0) == 0x1234);
  assert(blendRgb565(0x1234, 0xf800, 100) == 0xf800);
  assert(blendRgb565(0x1234, 0xf800, 255) == 0xf800);
  // Půlka mezi černou a bílou leží uprostřed každé složky.
  const uint16_t half = blendRgb565(0x0000, 0xffff, 50);
  assert(((half >> 11) & 0x1f) == 16);
  assert(((half >> 5) & 0x3f) == 32);
  assert((half & 0x1f) == 16);
}

void testPixelClipping() {
  // Mapová data se promítají bez ohledu na okraje displeje, takže se souřadnice
  // mimo buffer musí zahodit, ne zapsat vedle.
  Buffer buffer = makeBuffer();
  setMapPixel(buffer.data(), -1, 10, 0xffff, 100);
  setMapPixel(buffer.data(), 10, -1, 0xffff, 100);
  setMapPixel(buffer.data(), MAP_CANVAS_WIDTH, 10, 0xffff, 100);
  setMapPixel(buffer.data(), 10, MAP_CANVAS_HEIGHT, 0xffff, 100);
  assert(litPixels(buffer) == 0);

  setMapPixel(buffer.data(), 0, 0, 0xffff, 100);
  setMapPixel(buffer.data(), MAP_CANVAS_WIDTH - 1, MAP_CANVAS_HEIGHT - 1,
              0xf800, 100);
  assert(at(buffer, 0, 0) == 0xffff);
  assert(at(buffer, MAP_CANVAS_WIDTH - 1, MAP_CANVAS_HEIGHT - 1) == 0xf800);
  assert(litPixels(buffer) == 2);
}

void testLines() {
  Buffer buffer = makeBuffer();
  drawMapLine(buffer.data(), 10, 20, 14, 20, 0xffff, 100);
  for (int x = 10; x <= 14; ++x) assert(at(buffer, x, 20) == 0xffff);
  assert(litPixels(buffer) == 5);

  buffer = makeBuffer();
  drawMapLine(buffer.data(), 7, 3, 7, 9, 0xffff, 100);
  for (int y = 3; y <= 9; ++y) assert(at(buffer, 7, y) == 0xffff);
  assert(litPixels(buffer) == 7);
}

void testLineClipping() {
  // Úsečka celá mimo displej se musí zahodit dřív, než se z ní stane
  // Bresenham přes miliony kroků.
  Buffer buffer = makeBuffer();
  drawMapLine(buffer.data(), -5000, -5000, -10, -10, 0xffff, 100);
  assert(litPixels(buffer) == 0);
  drawMapLine(buffer.data(), 5000, 10, 6000, 20, 0xffff, 100);
  assert(litPixels(buffer) == 0);

  // Úsečka, která displejem prochází, se ořízne a nakreslí jen viditelnou část.
  drawMapLine(buffer.data(), -1000, 100, 1000, 100, 0xffff, 100);
  assert(litPixels(buffer) == static_cast<size_t>(MAP_CANVAS_WIDTH));
  assert(at(buffer, 0, 100) == 0xffff);
  assert(at(buffer, MAP_CANVAS_WIDTH - 1, 100) == 0xffff);
}

void testFillRectClips() {
  Buffer buffer = makeBuffer();
  fillMapRect(buffer.data(), -3, -3, 6, 6, 0xffff, 100);
  // Z šesti na šest pixelů zbudou po oříznutí tři na tři v rohu.
  assert(litPixels(buffer) == 9);
  assert(at(buffer, 0, 0) == 0xffff);
  assert(at(buffer, 2, 2) == 0xffff);
  assert(at(buffer, 3, 3) == 0x0000);
}

void testCircles() {
  Buffer buffer = makeBuffer();
  drawMapCircle(buffer.data(), 100, 100, 10, 0xffff, 100);
  // Obrys prochází čtyřmi krajními body a střed zůstává prázdný.
  assert(at(buffer, 110, 100) == 0xffff);
  assert(at(buffer, 90, 100) == 0xffff);
  assert(at(buffer, 100, 110) == 0xffff);
  assert(at(buffer, 100, 90) == 0xffff);
  assert(at(buffer, 100, 100) == 0x0000);

  buffer = makeBuffer();
  fillMapCircle(buffer.data(), 100, 100, 5, 0xffff, 100);
  assert(at(buffer, 100, 100) == 0xffff);
  assert(at(buffer, 105, 100) == 0xffff);
  assert(at(buffer, 100, 105) == 0xffff);
  // Roh opsaného čtverce do kruhu nepatří.
  assert(at(buffer, 105, 105) == 0x0000);

  // Nulový poloměr je jediný pixel, záporný se nekreslí.
  buffer = makeBuffer();
  drawMapCircle(buffer.data(), 50, 50, 0, 0xffff, 100);
  assert(litPixels(buffer) == 1);
  fillMapCircle(buffer.data(), 200, 200, -3, 0xffff, 100);
  assert(litPixels(buffer) == 1);
}

void testCirclesClipAtEdges() {
  // Kroužek kolem letadla u okraje displeje se musí oříznout, ne zapsat vedle.
  Buffer buffer = makeBuffer();
  drawMapCircle(buffer.data(), 2, 2, 20, 0xffff, 100);
  fillMapCircle(buffer.data(), MAP_CANVAS_WIDTH - 2, MAP_CANVAS_HEIGHT - 2, 20,
                0xffff, 100);
  // Bez pádu a bez zápisu mimo buffer; něco viditelného zůstat musí.
  assert(litPixels(buffer) > 0);
}

void testTriangleFill() {
  Buffer buffer = makeBuffer();
  fillMapTriangle(buffer.data(), 100, 100, 120, 100, 100, 120, 0xffff, 100);
  // Vrcholy a vnitřek jsou vyplněné, protilehlý roh ne.
  assert(at(buffer, 100, 100) == 0xffff);
  assert(at(buffer, 120, 100) == 0xffff);
  assert(at(buffer, 100, 120) == 0xffff);
  assert(at(buffer, 105, 105) == 0xffff);
  assert(at(buffer, 119, 119) == 0x0000);
}

void testDegenerateTriangleIsALine() {
  Buffer buffer = makeBuffer();
  fillMapTriangle(buffer.data(), 10, 50, 20, 50, 30, 50, 0xffff, 100);
  for (int x = 10; x <= 30; ++x) assert(at(buffer, x, 50) == 0xffff);
  assert(litPixels(buffer) == 21);
}

void testTriangleClips() {
  // Ikona letadla u okraje se ořízne; nesmí přetéct do sousedního řádku.
  Buffer buffer = makeBuffer();
  fillMapTriangle(buffer.data(), -50, -50, 5, 5, -50, 60, 0xffff, 100);
  assert(litPixels(buffer) > 0);
  fillMapTriangle(buffer.data(), 1000, 1000, 2000, 2000, 1500, 3000, 0xffff,
                  100);
}

void testTextWidth() {
  assert(mapTextWidth("") == 0);
  assert(mapTextWidth(nullptr) == 0);
  assert(mapTextWidth("A") == MAP_CANVAS_GLYPH_ADVANCE - 1);
  assert(mapTextWidth("PRAHA") == 5 * MAP_CANVAS_GLYPH_ADVANCE - 1);
}

void testTextFoldsCase() {
  // drawMapText převádí na verzálky, takže malá a velká varianta dají stejný
  // obrázek - na tom stojí vzhled popisků měst na meteoradaru.
  Buffer lower = makeBuffer();
  Buffer upper = makeBuffer();
  drawMapText(lower.data(), 5, 5, "praha", 0xffff, 100);
  drawMapText(upper.data(), 5, 5, "PRAHA", 0xffff, 100);
  assert(lower == upper);
  assert(litPixels(lower) > 0);
}

void testEveryLetterAndDigitDraws() {
  // Prázdný glyf by se projevil až na displeji jako díra v popisku.
  const char *const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  for (const char *c = alphabet; *c != '\0'; ++c) {
    Buffer buffer = makeBuffer();
    const char text[2] = {*c, '\0'};
    drawMapText(buffer.data(), 10, 10, text, 0xffff, 100);
    assert(litPixels(buffer) > 0);
  }
  // Znaky, které stupnice a popisky opravdu používají.
  const char *const symbols = "/<>*-.";
  for (const char *c = symbols; *c != '\0'; ++c) {
    Buffer buffer = makeBuffer();
    const char text[2] = {*c, '\0'};
    drawMapText(buffer.data(), 10, 10, text, 0xffff, 100);
    assert(litPixels(buffer) > 0);
  }
}

void testLabelPlacer() {
  MapLabelPlacer placer;
  assert(placer.claim({10, 10, 20, 10}));
  // Překryv i o jediný pixel se musí odmítnout - popisky by se slily.
  assert(!placer.claim({29, 19, 20, 10}));
  // Dotyk hranou překryv není.
  assert(placer.claim({30, 10, 20, 10}));
  assert(placer.claim({10, 20, 20, 10}));
  assert(placer.count == 3);

  placer.reset();
  assert(placer.count == 0);
  assert(placer.claim({10, 10, 20, 10}));
}

void testLabelPlacerCapacity() {
  // Po zaplnění seznamu se další popisky musí tiše vzdát, ne přepsat pole.
  MapLabelPlacer placer;
  for (size_t index = 0; index < MAP_LABEL_CAPACITY; ++index) {
    assert(placer.claim({0, static_cast<int>(index) * 12, 10, 10}));
  }
  assert(placer.count == MAP_LABEL_CAPACITY);
  assert(!placer.claim({300, 300, 10, 10}));
  assert(placer.count == MAP_LABEL_CAPACITY);
}

}  // namespace

int main() {
  testBlendEndpoints();
  testPixelClipping();
  testLines();
  testLineClipping();
  testFillRectClips();
  testCircles();
  testCirclesClipAtEdges();
  testTriangleFill();
  testDegenerateTriangleIsALine();
  testTriangleClips();
  testTextWidth();
  testTextFoldsCase();
  testEveryLetterAndDigitDraws();
  testLabelPlacer();
  testLabelPlacerCapacity();
  printf("map_canvas OK\n");
  return 0;
}
