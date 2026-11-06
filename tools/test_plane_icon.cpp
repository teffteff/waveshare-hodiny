// Ikony letadel podle typu (PlaneIcon.cpp): určení druhu z typu draku
// a ADS-B kategorie a to, že se druhy na mapě opravdu liší.
//
// Překlad viz tools/run_host_tests.sh.

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

#include "MapCanvas.h"
#include "PlaneIcon.h"

namespace {

void testClassify() {
  // Typ draku ze seznamů, i malými písmeny.
  assert(PlaneIcon_Classify("A320", "") == PI_NARROW);
  assert(PlaneIcon_Classify("a21n", "") == PI_NARROW);
  assert(PlaneIcon_Classify("B77W", "") == PI_WIDE);
  assert(PlaneIcon_Classify("A388", "") == PI_QUAD);
  assert(PlaneIcon_Classify("EC35", "") == PI_HELI);
  assert(PlaneIcon_Classify("C150", "") == PI_LIGHT);
  assert(PlaneIcon_Classify("L410", "") == PI_TWINPROP);
  assert(PlaneIcon_Classify("E190", "") == PI_REGIONAL);
  assert(PlaneIcon_Classify("C130", "") == PI_MILTRANS);
  assert(PlaneIcon_Classify("ASK21", "") == PI_GLIDER);
  assert(PlaneIcon_Classify("F16", "") == PI_FIGHTER);
  // Předpona pro typ, který seznamy nejmenují.
  assert(PlaneIcon_Classify("B73X", "") == PI_NARROW);
  // Neznámý typ: rozhodne kategorie, i když typ je vyplněný.
  assert(PlaneIcon_Classify("QQQQ", "A7") == PI_HELI);
  assert(PlaneIcon_Classify("", "A1") == PI_LIGHT);
  assert(PlaneIcon_Classify("", "A5") == PI_WIDE);
  assert(PlaneIcon_Classify("", "B1") == PI_GLIDER);
  assert(PlaneIcon_Classify("", "B2") == PI_BALLOON);
  assert(PlaneIcon_Classify("", "B6") == PI_DRONE);
  // Typ má přednost před kategorií.
  assert(PlaneIcon_Classify("A320", "A7") == PI_NARROW);
  // Nic známého: původní šipka. Ani nesmysl nebo prázdno nespadne.
  assert(PlaneIcon_Classify("", "") == PI_GENERIC);
  assert(PlaneIcon_Classify(nullptr, nullptr) == PI_GENERIC);
  assert(PlaneIcon_Classify("XXXXXXXXXXXXXXXXXXXX", "C2") == PI_GENERIC);
  assert(strcmp(PlaneIcon_Name(PI_HELI), "helicopter") == 0);
  assert(strcmp(PlaneIcon_Name(200), "aircraft") == 0);
}

size_t litPixels(uint8_t kind, bool hasTrack, float track = 45.0f) {
  std::vector<uint16_t> buffer(MAP_CANVAS_WIDTH * MAP_CANVAS_HEIGHT, 0);
  PlaneIcon_Draw(buffer.data(), 240, 240, track, hasTrack, kind, 0xFFFF);
  size_t lit = 0;
  for (uint16_t pixel : buffer) lit += pixel != 0;
  return lit;
}

void testDrawing() {
  // Každý druh něco nakreslí a velikost odpovídá váze: A380 víc než A320,
  // A320 víc než Cessna.
  for (uint8_t kind = 0; kind < PI_KIND_N; ++kind) assert(litPixels(kind, true) > 20);
  assert(litPixels(PI_QUAD, true) > litPixels(PI_NARROW, true));
  assert(litPixels(PI_NARROW, true) > litPixels(PI_LIGHT, true));
  // Bez směru je to u letadel kroužek s tečkou, stejný pro všechny druhy.
  assert(litPixels(PI_NARROW, false) == litPixels(PI_HELI, false));
  assert(litPixels(PI_GENERIC, false) == litPixels(PI_WIDE, false));
  // Ikona u okraje mapy se ořízne, nespadne.
  std::vector<uint16_t> buffer(MAP_CANVAS_WIDTH * MAP_CANVAS_HEIGHT, 0);
  for (uint8_t kind = 0; kind < PI_KIND_N; ++kind) {
    PlaneIcon_Draw(buffer.data(), 1, 1, 300.0f, true, kind, 0xFFFF);
    PlaneIcon_Draw(buffer.data(), MAP_CANVAS_WIDTH - 1, MAP_CANVAS_HEIGHT - 1,
                   120.0f, true, kind, 0xFFFF);
  }
}

}  // namespace

int main() {
  testClassify();
  testDrawing();
  puts("plane icon OK");
  return 0;
}
