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

// Tatáž noc s jmény hvězd, obrazci, dráhou Měsíce, tmou a radiantem.
const char EXTRA[] =
    "{\"v\":1,\"time\":1790020800,"
    "\"bodies\":["
    "{\"id\":\"sun\",\"n\":\"Slunce\",\"k\":0,\"ra\":11.9299,\"dec\":0.453},"
    "{\"id\":\"moon\",\"n\":\"M\\u011bs\\u00edc\",\"k\":1,\"ra\":20.2727,"
    "\"dec\":-23.127,\"ill\":0.763}],"
    "\"stars\":[18616,3878,0,20690,4528,12],"
    "\"starNames\":[\"Vega\",\"Deneb\"],"
    "\"starCons\":[\"Lyra\",\"Labu\\u0165\"],"
    "\"lines\":[0,1],"
    "\"cons\":[{\"n\":\"Labu\\u0165\",\"ra\":20.267,\"dec\":39.34},"
    "{\"n\":\"Orion\",\"ra\":5.6,\"dec\":-1.09}],"
    "\"moonTrack\":{\"t\":1790019000,\"s\":1800,\"p\":["
    "20266,-2318,20282,-2314,20298,-2310,20314,-2305,20330,-2300,20346,-2295,"
    "20362,-2290,20378,-2285,20394,-2280,20410,-2275,20426,-2270,20442,-2265,"
    "20458,-2260,20474,-2255,20490,-2250,20506,-2245,20522,-2240,20538,-2235,"
    "20554,-2230,20570,-2225,20586,-2220,20602,-2215,20618,-2210,20634,-2205,"
    "20650,-2200,20666,-2195,20682,-2190,20698,-2185,20714,-2180]},"
    "\"dark\":[1790014600,1790050000],"
    "\"radiant\":{\"n\":\"Perseidy\",\"ra\":3.2,\"dec\":58}}";

// Velký vůz nízko nad severem (21. 9. ve 22:00), tedy v pásu řádků úkazů.
const char NORTH[] =
    "{\"v\":1,\"time\":1790020800,"
    "\"bodies\":[{\"id\":\"sun\",\"n\":\"Slunce\",\"k\":0,\"ra\":11.9299,"
    "\"dec\":0.453}],"
    "\"cons\":[{\"n\":\"Velk\\u00fd v\\u016fz\",\"ra\":12.272,\"dec\":56.91}]}";

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
            true, "", result);
  FILE *output = fopen(outPath, "wb");
  if (output == nullptr) return 4;
  fwrite(pixels.data(), sizeof(uint16_t), pixels.size(), output);
  fclose(output);
  printf("dark %d sunUp %d taps %u\n", result.dark, result.sunUp,
         static_cast<unsigned>(result.tapCount));
  for (uint8_t index = 0; index < result.labelCount; ++index)
    printf("label %d %d %06X %s\n", result.labels[index].x,
           result.labels[index].y, static_cast<unsigned>(result.labels[index].color),
           result.labels[index].name);
  // Jména obrazců a roje kreslí na hodinách služba; tady se jen vypíšou.
  for (uint8_t index = 0; index < result.paintedCount; ++index) {
    const uint16_t color = result.painted[index].color;
    printf("label %d %d %06X %s\n", result.painted[index].x,
           result.painted[index].y,
           static_cast<unsigned>(((color >> 11) & 31) * 255 / 31 << 16 |
                                 ((color >> 5) & 63) * 255 / 63 << 8 |
                                 (color & 31) * 255 / 31),
           result.painted[index].name);
  }
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
  skyRender(pixels.data(), nullptr, LATITUDE, LONGITUDE, EPOCH, 0, false, false, true,
            "", result);
  assert(pixels[0] == 0x0000);
  assert(result.labelCount == 0 && result.tapCount == 0);
  // Obzor leží na poloměru 222 přímo nad středem.
  assert(pixels[(SKY_CANVAS_CENTER_Y - SKY_CANVAS_RADIUS) * SKY_CANVAS_SIZE +
                SKY_CANVAS_CENTER_X] != 0x0000);

  static SkyFeed feed;
  assert(parse(RESPONSE, feed));
  pixels = canvas();
  skyRender(pixels.data(), &feed, LATITUDE, LONGITUDE, EPOCH, 0, false, false, true,
            "saturn", result);
  // Slunce 27° pod obzorem: tma, Saturn je jediná planeta nahoře.
  assert(result.dark && !result.sunUp);
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
  skyRender(pixels.data(), &feed, LATITUDE, LONGITUDE, EPOCH, 0, false, false, true,
            "pluto", result);
  assert(!result.detail.open);

  // Červený noční režim: v obrázku jen červený kanál, popisky červené.
  pixels = canvas();
  skyRender(pixels.data(), &feed, LATITUDE, LONGITUDE, EPOCH, 0, true, false, true,
            "", result);
  for (uint16_t pixel : pixels) assert((pixel & 0x07FF) == 0);
  for (uint8_t index = 0; index < result.labelCount; ++index)
    assert(result.labels[index].color == 0xFF4848);

  // Řádky jdou tam, kde je sever: se severem dole (nebo na jihu) dolů.
  assert(!skyEventsAtTop(180, LATITUDE));
  assert(skyEventsAtTop(315, LATITUDE) && skyEventsAtTop(45, LATITUDE));
  assert(!skyEventsAtTop(0, -33.9f) && skyEventsAtTop(180, -33.9f));

  // Otočení: s jihem nahoře je Saturn vlevo nahoře.
  skyRender(pixels.data(), &feed, LATITUDE, LONGITUDE, EPOCH, 180, false, false, true,
            "", result);
  for (uint8_t index = 0; index < result.tapCount; ++index) {
    if (strcmp(result.taps[index].id, "saturn") != 0) continue;
    assert(result.taps[index].x < SKY_CANVAS_CENTER_X - 100);
    assert(result.taps[index].y < SKY_CANVAS_CENTER_Y);
  }

  // Ve dne: Slunce nad obzorem, nic není "za tmy".
  skyRender(pixels.data(), &feed, LATITUDE, LONGITUDE, EPOCH + 12 * 3600.0, 0,
            false, false, true, "", result);
  assert(result.sunUp && !result.dark);

  // Hvězdy se jménem jdou vybrat, těleso má ale přednost.
  {
    SkyTapPoint taps[3];
    taps[0] = {100, 100, "*4"};
    taps[1] = {120, 100, "moon"};
    taps[2] = {300, 300, "*7"};
    // Hvězda blíž, Měsíc do 30 px: Měsíc.
    assert(skyPickTap(taps, 3, 102, 100) == 1);
    // Jen hvězda do 18 px.
    assert(skyPickTap(taps, 3, 310, 305) == 2);
    // Hvězda dál než 18 px nic.
    assert(skyPickTap(taps, 3, 320, 300) == -1);
  }
  static SkyFeed extra;
  assert(parse(EXTRA, extra));
  assert(extra.stars[0].name[0] == 'V');
  assert(extra.figureCount == 2 && extra.moonTrackCount == 29);
  assert(extra.hasRadiant && extra.darkFrom == 1790014600);
  pixels = canvas();
  // Radiant Perseid leží nízko na severovýchodě, v pásu řádků úkazů; tady
  // se proto kreslí bez nich.
  skyRender(pixels.data(), &extra, LATITUDE, LONGITUDE, EPOCH, 0, false, false,
            false, "*0", result);
  // Vega je vysoko a pojmenovaná, takže jde vybrat a má detail.
  assert(result.detail.open && result.detail.kind == SKY_BODY_STAR);
  assert(strcmp(result.detail.name, "Vega") == 0);
  assert(strcmp(result.detail.constellation, "Lyra") == 0);
  assert(result.detail.altitudeDeg > 50.0f);
  bool sawVega = false;
  for (uint8_t index = 0; index < result.tapCount; ++index)
    sawVega = sawVega || strcmp(result.taps[index].id, "*0") == 0;
  assert(sawVega);
  // Labuť je za tmy vysoko: jméno dostane, Orion pod obzorem ne. Radiant
  // Perseid na severovýchodě taky.
  // Tahle jména kreslí do bufferu služba, mezi popisky LVGL nejsou.
  bool sawSwan = false;
  bool sawOrion = false;
  bool sawRadiant = false;
  for (uint8_t index = 0; index < result.paintedCount; ++index) {
    sawSwan = sawSwan || strcmp(result.painted[index].name, "Labuť") == 0;
    sawOrion = sawOrion || strcmp(result.painted[index].name, "Orion") == 0;
    sawRadiant = sawRadiant || strcmp(result.painted[index].name, "Perseidy") == 0;
  }
  assert(sawSwan && !sawOrion && sawRadiant);
  assert(result.labelCount == 1);  // jen Měsíc
  // V červeném nočním režimu jsou červená i ona.
  skyRender(pixels.data(), &extra, LATITUDE, LONGITUDE, EPOCH, 0, true, false, true,
            "", result);
  for (uint8_t index = 0; index < result.paintedCount; ++index)
    assert((result.painted[index].color & 0x07FF) == 0);
  // Ve dne jména obrazců nejsou, radiant ano.
  skyRender(pixels.data(), &extra, LATITUDE, LONGITUDE, EPOCH + 12 * 3600.0, 0,
            false, false, true, "", result);
  for (uint8_t index = 0; index < result.paintedCount; ++index)
    assert(strcmp(result.painted[index].name, "Labuť") != 0);

  // Bez řádků úkazů smí popisky i do jejich pásu nad severní oblohou.
  {
    const int bandTop = SKY_CANVAS_CENTER_Y + skyEventRowOffsetY(true, 0) - 12;
    const int bandBottom = SKY_CANVAS_CENTER_Y + skyEventRowOffsetY(true, 2) + 14;
    auto inBand = [&](const SkyRenderResult &frame) {
      int count = 0;
      for (uint8_t index = 0; index < frame.paintedCount; ++index)
        if (frame.painted[index].y + 17 > bandTop &&
            frame.painted[index].y < bandBottom)
          ++count;
      return count;
    };
    static SkyFeed north;
    assert(parse(NORTH, north));
    skyRender(pixels.data(), &north, LATITUDE, LONGITUDE, EPOCH, 0, false,
              false, true, "", result);
    assert(inBand(result) == 0);
    skyRender(pixels.data(), &north, LATITUDE, LONGITUDE, EPOCH, 0, false,
              false, false, "", result);
    assert(inBand(result) == 1);
  }

  puts("sky render OK");
  return 0;
}
