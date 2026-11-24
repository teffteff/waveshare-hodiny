// Snímky meteoradaru se dekódují po řádcích rovnou do RGB332 a mapa se do
// nich peče bez celoplošného mezibufferu RGB565. Test hlídá, že výsledek je
// stejný jako dřív (RGB565 -> mapa -> převod celého snímku do RGB332):
// převod pixelu, paleta ČHMÚ (na ni je navázaná noční paleta i stupnice)
// a kreslení mapy plnou barvou bit po bitu; průhledná mapa nad srážkami nebo
// v místech, kde se tahy překrývají, nejvýš o krok RGB332 ve složce (při
// krytí kolem 10 % o dva: drobné přírůstky se v RGB332 nesčítají).
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <vector>

#include "MapCanvas.h"

namespace {

// Převod, kterým se dřív balil hotový snímek RGB565 (ChmiRadarService.cpp
// před přechodem na přímé dekódování).
uint8_t oldRgb565ToRgb332(uint16_t color) {
  const uint8_t red = static_cast<uint8_t>((color >> 11) & 0x1f);
  const uint8_t green = static_cast<uint8_t>((color >> 5) & 0x3f);
  const uint8_t blue = static_cast<uint8_t>(color & 0x1f);
  return static_cast<uint8_t>(((red >> 2) << 5) | ((green >> 3) << 2) |
                              (blue >> 3));
}

constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
  return static_cast<uint16_t>(((red >> 3) << 11) | ((green >> 2) << 5) |
                               (blue >> 3));
}

// Kopie palety ČHMÚ z ChmiRadarService.cpp (stupně 1..15) a šedé bez dat.
const uint16_t CHMI_COLORS[] = {
    rgb565(56, 0, 112),   rgb565(48, 0, 168),    rgb565(0, 0, 252),
    rgb565(0, 108, 192),  rgb565(0, 160, 0),     rgb565(0, 188, 0),
    rgb565(52, 216, 0),   rgb565(156, 220, 0),   rgb565(224, 220, 0),
    rgb565(252, 176, 0),  rgb565(252, 132, 0),   rgb565(252, 88, 0),
    rgb565(252, 0, 0),    rgb565(160, 0, 0),     rgb565(252, 252, 252),
    rgb565(196, 196, 196)};
constexpr size_t CHMI_COLOR_COUNT = sizeof(CHMI_COLORS) / sizeof(CHMI_COLORS[0]);

constexpr int W = MAP_CANVAS_WIDTH;
constexpr int H = MAP_CANVAS_HEIGHT;

void testConversionMatchesOldPacking() {
  for (uint32_t color = 0; color <= 0xffff; ++color)
    assert(mapRgb565ToRgb332(static_cast<uint16_t>(color)) ==
           oldRgb565ToRgb332(static_cast<uint16_t>(color)));
}

void testExpansionRoundTrips() {
  // Nepomalovaný pixel projde mícháním beze změny jen tehdy, když se
  // rozšířený bajt zúží zpátky na sebe.
  for (int color = 0; color < 256; ++color)
    assert(mapRgb565ToRgb332(mapRgb332ToRgb565(static_cast<uint8_t>(color))) ==
           color);
}

void testChmiPaletteCodes() {
  // Kódy RGB332 stupňů ČHMÚ, na které je navázaná noční paleta
  // (nightRadarColor). Přímé dekódování je musí dát stejné.
  const uint8_t expected[CHMI_COLOR_COUNT] = {
      0x21, 0x22, 0x03, 0x0f, 0x14, 0x14, 0x38, 0x98,
      0xf8, 0xf4, 0xf0, 0xe8, 0xe0, 0xa0, 0xff, 0xdb};
  for (size_t index = 0; index < CHMI_COLOR_COUNT; ++index) {
    if (mapRgb565ToRgb332(CHMI_COLORS[index]) != expected[index]) {
      fprintf(stderr, "stupeň %zu: 0x%02x místo 0x%02x\n", index + 1,
              mapRgb565ToRgb332(CHMI_COLORS[index]), expected[index]);
      assert(false);
    }
  }
  // Vzorky stupnice (dBZ) musí po převodu padnout na barvy srážek, aby je
  // noční paleta obarvila stejně jako mapu.
  const uint16_t legend[6] = {0xa000, 0xf800, 0xfc20, 0xe6e0, 0x05e0, 0x001f};
  std::set<uint8_t> palette;
  for (uint16_t color : CHMI_COLORS) palette.insert(mapRgb565ToRgb332(color));
  for (uint16_t color : legend)
    assert(palette.count(mapRgb565ToRgb332(color)) == 1);
}

// Syntetický snímek: pruhy všech barev palety, černé okno bez srážek.
std::vector<uint16_t> rainFrame565() {
  std::vector<uint16_t> frame(static_cast<size_t>(W) * H, 0);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      if (x > 300 && y > 300) continue;
      frame[static_cast<size_t>(y) * W + x] =
          CHMI_COLORS[((x / 7) + (y / 11)) % CHMI_COLOR_COUNT];
    }
  return frame;
}

template <typename Pixel>
void drawOverlay(Pixel *buffer, uint8_t opacity) {
  // Stejné tahy, jaké skládají mapu kulatého: hranice, popisky s podkladem,
  // značka polohy a stupnice.
  for (int step = 0; step < 40; ++step)
    drawMapLine(buffer, -20 + step * 13, 5 + step * 11, 470 - step * 7,
                480 - step * 9, 0xbdf7, opacity);
  fillMapRect(buffer, 200, 190, 60, 12, 0x0000, opacity);
  drawMapText(buffer, 202, 192, "PRAHA BRNO 12 / 0.2", 0x07ff, opacity);
  fillMapRect(buffer, 320, 320, 40, 12, 0x0000, opacity);
  drawMapText(buffer, 322, 322, "OSTRAVA", 0x07ff, opacity);
  for (int offset = -9; offset <= 9; ++offset) {
    setMapPixel(buffer, 240 + offset, 240, 0xffff, opacity);
    setMapPixel(buffer, 240, 240 + offset, 0xffff, opacity);
  }
  fillMapRect(buffer, 28, 140, 96, 92, 0x0000, 100);
  fillMapRect(buffer, 30, 160, 9, 9, 0xfc20, 100);
  drawMapText(buffer, 44, 161, "44 / 20", 0xffff, 100);
}

struct Comparison {
  size_t differing = 0;
  int maxStep = 0;
};

Comparison compare(uint8_t opacity, bool emptyBackground) {
  std::vector<uint16_t> old565 = rainFrame565();
  if (emptyBackground) std::fill(old565.begin(), old565.end(), 0);
  std::vector<uint8_t> direct(old565.size());
  // Nové dekódování: převod při zápisu řádku.
  for (size_t pixel = 0; pixel < old565.size(); ++pixel)
    direct[pixel] = mapRgb565ToRgb332(old565[pixel]);
  drawOverlay(old565.data(), opacity);
  drawOverlay(direct.data(), opacity);
  Comparison result;
  for (size_t pixel = 0; pixel < old565.size(); ++pixel) {
    const uint8_t before = oldRgb565ToRgb332(old565[pixel]);
    const uint8_t after = direct[pixel];
    if (before == after) continue;
    ++result.differing;
    const int red = abs((before >> 5) - (after >> 5));
    const int green = abs(((before >> 2) & 7) - ((after >> 2) & 7));
    const int blue = abs((before & 3) - (after & 3));
    result.maxStep = std::max(result.maxStep, std::max(red, std::max(green, blue)));
  }
  return result;
}

void testOverlayEquivalence() {
  // Plná mapa: bit po bitu stejný snímek.
  Comparison opaque = compare(100, false);
  assert(opaque.differing == 0);
  // Průhledná mapa: zaokrouhlení podkladu na RGB332 před mícháním (nad
  // srážkami, nebo kde se tahy mapy překrývají) smí posunout pixel nejvýš
  // o dva kroky složky; nad 10 % krytí je to v praxi jeden.
  for (bool empty : {true, false})
    for (uint8_t opacity : {10, 35, 50, 60, 80, 99}) {
      Comparison result = compare(opacity, empty);
      printf("  %s, krytí %3u %%: %zu pixelů jinak, nejvýš o %d krok\n",
             empty ? "bez srážek" : "srážky", opacity, result.differing,
             result.maxStep);
      assert(result.maxStep <= (opacity < 35 ? 2 : 1));
    }
}

void testSingleBlendOverBlackIsExact() {
  // Jedna vrstva průhledné barvy nad černou (tah mapy mimo srážky, kde se
  // nic nepřekrývá) vyjde přesně jako dřív.
  for (uint32_t color = 0; color <= 0xffff; color += 7)
    for (uint8_t opacity = 0; opacity <= 100; ++opacity)
      assert(blendRgb332(0, static_cast<uint16_t>(color), opacity) ==
             oldRgb565ToRgb332(
                 blendRgb565(0, static_cast<uint16_t>(color), opacity)));
}

void testBlendRgb332Endpoints() {
  for (int color = 0; color < 256; ++color) {
    assert(blendRgb332(static_cast<uint8_t>(color), 0xf800, 0) == color);
    assert(blendRgb332(static_cast<uint8_t>(color), 0x07ff, 100) ==
           mapRgb565ToRgb332(0x07ff));
  }
}

}  // namespace

int main() {
  testConversionMatchesOldPacking();
  testExpansionRoundTrips();
  testChmiPaletteCodes();
  testBlendRgb332Endpoints();
  testSingleBlendOverBlackIsExact();
  testOverlayEquivalence();
  puts("radar_rgb332 OK");
  return 0;
}
