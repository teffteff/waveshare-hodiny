// Kresba noční oblohy na počítači.
//
//   test_sky_render                         testy
//   test_sky_render odpoved.json EPOCHA out.rgb565 [night]
//                                           nakreslí odpověď serveru do
//                                           surového RGB565 480x480 (little
//                                           endian) a vypíše, kam přijdou
//                                           popisky - pro náhled bez hodin
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../WaveshareHodiny/SkyCanvas.h"
#include "../WaveshareHodiny/SkyRender.h"

namespace {

constexpr float LATITUDE = 49.90461f;
constexpr float LONGITUDE = 14.7842f;
constexpr double EPOCH = 1790020800.0;  // 21. 9. 2026 22:00 SELČ

// Tatáž zkrácená odpověď jako v test_sky_feed: Měsíc a Saturn nad obzorem,
// Jupiter a Slunce pod ním.
const char RESPONSE[] =
    "{\"v\":1,\"time\":1790020800,"
    "\"bodies\":["
    "{\"id\":\"sun\",\"n\":\"Slunce\",\"k\":0,\"ra\":11.9299,\"dec\":0.453},"
    "{\"id\":\"moon\",\"n\":\"M\\u011bs\\u00edc\",\"k\":1,\"ra\":20.2727,"
    "\"dec\":-23.127,\"dist\":389012,\"ill\":0.763,\"con\":\"Kozoroh\"},"
    "{\"id\":\"saturn\",\"n\":\"Saturn\",\"k\":2,\"ra\":0.8235,\"dec\":2.366,"
    "\"au\":8.451,\"mag\":0.4,\"con\":\"Velryba\"},"
    "{\"id\":\"jupiter\",\"n\":\"Jupiter\",\"k\":2,\"ra\":9.3679,\"dec\":16.014,"
    "\"au\":5.8,\"mag\":-1.8,\"con\":\"Rak\"}],"
    "\"stars\":[18616,3878,0,20690,4528,12],"
    "\"lines\":[0,1]}";

std::vector<uint16_t> canvas() {
  return std::vector<uint16_t>(SKY_CANVAS_SIZE * SKY_CANVAS_SIZE, 0x1234);
}

bool parse(const char *text, SkyFeed &feed) {
  return skyFeedParse(text, text + strlen(text), feed);
}

int dump(const char *jsonPath, double epoch, const char *outPath, bool night) {
  FILE *input = fopen(jsonPath, "rb");
  if (input == nullptr) return 2;
  std::string text;
  char chunk[4096];
  size_t read = 0;
  while ((read = fread(chunk, 1, sizeof(chunk), input)) > 0) text.append(chunk, read);
  fclose(input);
  static SkyFeed feed;
  if (!skyFeedParse(text.data(), text.data() + text.size(), feed)) return 3;
  std::vector<uint16_t> pixels = canvas();
  SkyRenderResult result;
  skyRender(pixels.data(), &feed, LATITUDE, LONGITUDE, epoch, 0, night, false,
            "", result);
  FILE *output = fopen(outPath, "wb");
  if (output == nullptr) return 4;
  fwrite(pixels.data(), sizeof(uint16_t), pixels.size(), output);
  fclose(output);
  printf("dark %d sunUp %d planetsUp %u\n", result.dark, result.sunUp,
         result.planetsUp);
  for (uint8_t index = 0; index < result.labelCount; ++index)
    printf("label %d %d %06X %s\n", result.labels[index].x,
           result.labels[index].y, static_cast<unsigned>(result.labels[index].color),
           result.labels[index].name);
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc >= 4)
    return dump(argv[1], atof(argv[2]), argv[3],
                argc >= 5 && strcmp(argv[4], "night") == 0);

  // Bez dat: jen kruh s mřížkou, pozadí je černé.
  std::vector<uint16_t> pixels = canvas();
  SkyRenderResult result;
  skyRender(pixels.data(), nullptr, LATITUDE, LONGITUDE, EPOCH, 0, false, false,
            "", result);
  assert(pixels[0] == 0x0000);
  assert(result.labelCount == 0 && result.tapCount == 0);
  // Obzor leží na poloměru 222 přímo nad středem.
  assert(pixels[(SKY_CANVAS_CENTER_Y - SKY_CANVAS_RADIUS) * SKY_CANVAS_SIZE +
                SKY_CANVAS_CENTER_X] != 0x0000);

  static SkyFeed feed;
  assert(parse(RESPONSE, feed));
  pixels = canvas();
  skyRender(pixels.data(), &feed, LATITUDE, LONGITUDE, EPOCH, 0, false, false,
            "saturn", result);
  // Slunce 27° pod obzorem: tma, Saturn je jediná planeta nahoře.
  assert(result.dark && !result.sunUp);
  assert(result.planetsUp == 1);
  // Měsíc a Saturn jdou vybrat, Jupiter pod obzorem ne.
  assert(result.tapCount == 2);
  bool sawMoon = false;
  bool sawSaturn = false;
  for (uint8_t index = 0; index < result.tapCount; ++index) {
    sawMoon = sawMoon || strcmp(result.taps[index].id, "moon") == 0;
    sawSaturn = sawSaturn || strcmp(result.taps[index].id, "saturn") == 0;
    assert(strcmp(result.taps[index].id, "jupiter") != 0);
  }
  assert(sawMoon && sawSaturn);
  // Saturn stojí na jihovýchodě (azimut 115°, výška 23°): vpravo dole.
  for (uint8_t index = 0; index < result.tapCount; ++index) {
    if (strcmp(result.taps[index].id, "saturn") != 0) continue;
    assert(result.taps[index].x > SKY_CANVAS_CENTER_X + 100);
    assert(result.taps[index].y > SKY_CANVAS_CENTER_Y);
    // Kolem tečky je bílý kroužek výběru.
    const int ringY = result.taps[index].y;
    const int ringX = result.taps[index].x + 3 + 7;
    assert(pixels[ringY * SKY_CANVAS_SIZE + ringX] == 0xFFFF);
  }
  // Detail vybraného tělesa, i s výškou z polohy hodin.
  assert(result.detail.open);
  assert(strcmp(result.detail.name, "Saturn") == 0);
  assert(result.detail.altitudeDeg > 22.0f && result.detail.altitudeDeg < 23.5f);
  assert(strcmp(result.detail.constellation, "Velryba") == 0);
  // Popisky: oba s diakritikou vcelku a mimo pásy textu obrazovky.
  assert(result.labelCount == 2);
  for (uint8_t index = 0; index < result.labelCount; ++index) {
    const SkyLabel &label = result.labels[index];
    // Se severem nahoře jsou řádky úkazů nad severní oblohou.
    assert(skyEventsAtTop(0, LATITUDE));
    assert(label.y > SKY_CANVAS_CENTER_Y + skyEventRowOffsetY(true, 2) + 14);
    if (strcmp(label.name, "Měsíc") == 0) assert(label.color == 0xE6E6DC);
  }

  // Nevybrané těleso detail nemá; neznámé id taky ne.
  skyRender(pixels.data(), &feed, LATITUDE, LONGITUDE, EPOCH, 0, false, false,
            "pluto", result);
  assert(!result.detail.open);

  // Červený noční režim: v obrázku jen červený kanál, popisky červené.
  pixels = canvas();
  skyRender(pixels.data(), &feed, LATITUDE, LONGITUDE, EPOCH, 0, true, false,
            "", result);
  for (uint16_t pixel : pixels) assert((pixel & 0x07FF) == 0);
  for (uint8_t index = 0; index < result.labelCount; ++index)
    assert(result.labels[index].color == 0xFF4848);

  // Řádky jdou tam, kde je sever: se severem dole (nebo na jihu) dolů.
  assert(!skyEventsAtTop(180, LATITUDE));
  assert(skyEventsAtTop(315, LATITUDE) && skyEventsAtTop(45, LATITUDE));
  assert(!skyEventsAtTop(0, -33.9f) && skyEventsAtTop(180, -33.9f));

  // Otočení: s jihem nahoře je Saturn vlevo nahoře.
  skyRender(pixels.data(), &feed, LATITUDE, LONGITUDE, EPOCH, 180, false, false,
            "", result);
  for (uint8_t index = 0; index < result.tapCount; ++index) {
    if (strcmp(result.taps[index].id, "saturn") != 0) continue;
    assert(result.taps[index].x < SKY_CANVAS_CENTER_X - 100);
    assert(result.taps[index].y < SKY_CANVAS_CENTER_Y);
  }

  // Ve dne: Slunce nad obzorem, nic není "za tmy".
  skyRender(pixels.data(), &feed, LATITUDE, LONGITUDE, EPOCH + 12 * 3600.0, 0,
            false, false, "", result);
  assert(result.sunUp && !result.dark);

  puts("sky render OK");
  return 0;
}
