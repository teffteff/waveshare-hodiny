// Kresba mapy obrazovky Doprava na počítači, se skutečným písmem hodin
// (ClockCzechFont14 přes LVGL a MapLabelFont).
//
//   test_traffic_render                          testy
//   test_traffic_render odpoved.json out.rgb565 [night] [en]
//                                                nakreslí odpověď do surového
//                                                RGB565 480x480 (little endian);
//                                                PNG z něj udělá
//                                                tools/rgb565_to_png.py
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../WaveshareHodiny/MapLabelFont.h"
#include "../WaveshareHodiny/TrafficRender.h"

extern "C" const lv_font_t clock_czech_14;

#ifndef TRAFFIC_FIXTURE
#error "TRAFFIC_FIXTURE musí ukazovat na tools/fixtures/doprava-hodiny.json"
#endif

namespace {

std::string readFile(const char *path) {
  FILE *input = fopen(path, "rb");
  if (input == nullptr) return "";
  std::string text;
  char chunk[4096];
  size_t read = 0;
  while ((read = fread(chunk, 1, sizeof(chunk), input)) > 0)
    text.append(chunk, read);
  fclose(input);
  return text;
}

int yields = 0;
void countYield() { ++yields; }

uint16_t at(const std::vector<uint16_t> &pixels, int x, int y) {
  return pixels[y * TRAFFIC_MAP_SIZE + x];
}

int dump(const char *jsonPath, const char *outPath, bool night, bool english) {
  static TrafficData data;
  const std::string text = readFile(jsonPath);
  if (trafficFeedParse(text.data(), text.size(), data) != TrafficParseStatus::Ok)
    return 3;
  std::vector<uint16_t> pixels(TRAFFIC_MAP_SIZE * TRAFFIC_MAP_SIZE, 0x1234);
  MapLabelFont font(&clock_czech_14, false);
  TrafficRenderResult result;
  trafficRender(pixels.data(), &data, night, english, font, nullptr, result);
  FILE *output = fopen(outPath, "wb");
  if (output == nullptr) return 4;
  fwrite(pixels.data(), sizeof(uint16_t), pixels.size(), output);
  fclose(output);
  for (uint8_t index = 0; index < result.placeLabelCount; ++index) {
    const MapLabelBox &box = result.placeLabels[index];
    printf("label %d %d %dx%d\n", box.x, box.y, box.width, box.height);
  }
  printf("buses %u legend %d\n", static_cast<unsigned>(result.busCount),
         result.legendDrawn);
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc >= 3) {
    bool night = false;
    bool english = false;
    for (int index = 3; index < argc; ++index) {
      night = night || strcmp(argv[index], "night") == 0;
      english = english || strcmp(argv[index], "en") == 0;
    }
    return dump(argv[1], argv[2], night, english);
  }

  MapLabelFont font(&clock_czech_14, false);
  std::vector<uint16_t> pixels(TRAFFIC_MAP_SIZE * TRAFFIC_MAP_SIZE, 0x1234);
  TrafficRenderResult result;

  // Bez dat jen černé pozadí.
  trafficRender(pixels.data(), nullptr, false, false, font, countYield, result);
  for (uint16_t pixel : pixels) assert(pixel == 0x0000);
  assert(yields > 0);

  static TrafficData data;
  const std::string text = readFile(TRAFFIC_FIXTURE);
  assert(trafficFeedParse(text.data(), text.size(), data) ==
         TrafficParseStatus::Ok);
  yields = 0;
  trafficRender(pixels.data(), &data, false, false, font, countYield, result);
  assert(yields >= 2);
  assert(result.busCount == data.busCount);
  assert(result.legendDrawn);
  // Všechna tři místa mají popisek, celý uvnitř kruhu displeje a mimo pásy
  // nadpisu a stavového řádku.
  assert(result.placeLabelCount == data.placeCount);
  for (uint8_t index = 0; index < result.placeLabelCount; ++index) {
    const MapLabelBox &box = result.placeLabels[index];
    for (int corner = 0; corner < 4; ++corner) {
      const int x = box.x + (corner & 1 ? box.width : 0) - TRAFFIC_MAP_CENTER;
      const int y = box.y + (corner & 2 ? box.height : 0) - TRAFFIC_MAP_CENTER;
      assert(x * x + y * y <= TRAFFIC_MAP_LABEL_RADIUS * TRAFFIC_MAP_LABEL_RADIUS);
    }
    assert(box.y >= TRAFFIC_TOP_BAND_END_Y);
    for (uint8_t other = 0; other < index; ++other)
      assert(!mapBoxesOverlap(box, result.placeLabels[other]));
  }
  // Úsek cesty do Prahy, kde se stojí (body 13-14), je červený.
  const TrafficLine *praha = nullptr;
  const TrafficLine *d1P = nullptr;
  const TrafficLine *d1B = nullptr;
  for (uint8_t index = 0; index < data.lineCount; ++index) {
    if (strcmp(data.lines[index].id, "ondrejov-praha") == 0) praha = &data.lines[index];
    if (strcmp(data.lines[index].id, "d1-P") == 0) d1P = &data.lines[index];
    if (strcmp(data.lines[index].id, "d1-B") == 0) d1B = &data.lines[index];
  }
  assert(praha != nullptr && d1P != nullptr && d1B != nullptr);
  const int redX = (praha->x[13] + praha->x[14]) / 2;
  const int redY = (praha->y[13] + praha->y[14]) / 2;
  assert(at(pixels, redX, redY) == trafficLevelColor(TRAFFIC_LEVEL_STANDING));
  // D1 oba směry vedle sebe: mezi body 12 a 13 d1-B (za sjezdem 41) jedou oba
  // volně, každý po své pravé straně osy a s černou mezerou mezi sebou.
  {
    const float x0 = d1B->x[12], y0 = d1B->y[12], x1 = d1B->x[13], y1 = d1B->y[13];
    const float dx = x1 - x0, dy = y1 - y0;
    const float length = sqrtf(dx * dx + dy * dy);
    const float nx = -dy / length, ny = dx / length;
    const float mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
    const int brnoX = static_cast<int>(lroundf(mx + nx * 2.6f));
    const int brnoY = static_cast<int>(lroundf(my + ny * 2.6f));
    const int prahaX = static_cast<int>(lroundf(mx - nx * 2.6f));
    const int prahaY = static_cast<int>(lroundf(my - ny * 2.6f));
    assert(at(pixels, brnoX, brnoY) == trafficLevelColor(TRAFFIC_LEVEL_FREE));
    assert(at(pixels, prahaX, prahaY) == trafficLevelColor(TRAFFIC_LEVEL_FREE));
    // Dál než oba pruhy už nic.
    assert(at(pixels, static_cast<int>(lroundf(mx + nx * 7.0f)),
              static_cast<int>(lroundf(my + ny * 7.0f))) == 0x0000);
  }
  // Úsek d1-B 1-5 je kolona (oranžová), 3-5 přes ni zpomalení (žlutá) - poslední
  // vyhrává. Bod mezi 2 a 3 je proto oranžový, posunutý na stranu Brna.
  {
    const float x0 = d1B->x[2], y0 = d1B->y[2], x1 = d1B->x[3], y1 = d1B->y[3];
    const float dx = x1 - x0, dy = y1 - y0;
    const float length = sqrtf(dx * dx + dy * dy);
    const int x = static_cast<int>((x0 + x1) / 2 - dy / length * 2.6f + 0.5f);
    const int y = static_cast<int>((y0 + y1) / 2 + dx / length * 2.6f + 0.5f);
    assert(at(pixels, x, y) == trafficLevelColor(TRAFFIC_LEVEL_JAM));
  }
  // Autobus je modrý kruh s číslem linky.
  assert(at(pixels, data.buses[0].x - 7, data.buses[0].y) == 0x1BDB);

  // Červený noční režim: v obrázku jen červený kanál.
  trafficRender(pixels.data(), &data, true, false, font, nullptr, result);
  for (uint16_t pixel : pixels) assert((pixel & 0x07FF) == 0);
  return 0;
}
