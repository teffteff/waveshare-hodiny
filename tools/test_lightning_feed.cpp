#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../WaveshareHodiny/LightningFeed.h"

namespace {
void collect(const LightningStroke &stroke, void *context) {
  static_cast<std::vector<LightningStroke> *>(context)->push_back(stroke);
}

LightningMessageKind parse(const char *text, LightningMessageInfo &info,
                           std::vector<LightningStroke> &strokes) {
  strokes.clear();
  return lightningParseMessage(text, text + strlen(text), info, collect,
                               &strokes);
}
}  // namespace

int main() {
  LightningMessageInfo info;
  std::vector<LightningStroke> strokes;

  // Skutečné zprávy zachycené ze serveru 13. 9. 2026.
  assert(parse("{\"cid\":893005,\"con\":151,\"port\":\"8086\","
               "\"time\":1789332454.286,\"k\":5338620.54180223}",
               info, strokes) == LightningMessageKind::Hello);
  assert(info.hasChallenge);
  assert(std::fabs(info.challengeKey - 5338620.54180223) < 1e-6);
  assert(strokes.empty());

  assert(parse("{\"time\":1789332479.68}", info, strokes) ==
         LightningMessageKind::Heartbeat);
  assert(!info.hasChallenge);

  assert(parse("{\"time\":1789332454,\"flags\":{\"2\":0},\"strokes\":"
               "[{\"time\":1789332254344,\"lat\":46.813302,\"lon\":14.464687,"
               "\"src\":2,\"srv\":1,\"id\":9731546,\"del\":1803,\"dev\":8100}]}",
               info, strokes) == LightningMessageKind::Strokes);
  assert(info.strokeCount == 1 && info.validStrokeCount == 1);
  assert(strokes.size() == 1);
  // Milisekundy se nesmí zaokrouhlit přes float.
  assert(strokes[0].epochSeconds == 1789332254U);
  assert(strokes[0].id == 9731546U);
  assert(std::fabs(strokes[0].latitude - 46.813302f) < 1e-4f);
  assert(std::fabs(strokes[0].longitude - 14.464687f) < 1e-4f);

  // Úder s nesmyslnou polohou nebo starým časem se přeskočí, zbytek platí.
  assert(parse("{\"time\":1,\"strokes\":["
               "{\"time\":1789332254344,\"lat\":95.0,\"lon\":14.0,\"id\":1},"
               "{\"time\":1500000000000,\"lat\":50.0,\"lon\":14.0,\"id\":2},"
               "{\"time\":1789332254999,\"lat\":\"50.1\",\"lon\":14.2,\"id\":3},"
               "{\"lat\":50.0,\"lon\":14.0,\"id\":4}]}",
               info, strokes) == LightningMessageKind::Strokes);
  assert(info.strokeCount == 4 && info.validStrokeCount == 1);
  assert(strokes.size() == 1 && strokes[0].id == 3);

  // Useknutá zpráva nečte za konec a nevydá nic.
  const char truncated[] =
      "{\"time\":1,\"strokes\":[{\"time\":1789332254344,\"lat\":50.0,\"lo";
  assert(lightningParseMessage(truncated, truncated + strlen(truncated), info,
                               collect, &strokes) ==
         LightningMessageKind::Invalid);
  assert(parse("", info, strokes) == LightningMessageKind::Invalid);
  assert(parse("[1,2]", info, strokes) == LightningMessageKind::Invalid);
  assert(parse("{\"cid\":1}", info, strokes) == LightningMessageKind::Invalid);

  // Odpověď vlastního serveru nese navíc "live".
  assert(parse("{\"time\":1789332500.1,\"live\":false,\"strokes\":[]}", info,
               strokes) == LightningMessageKind::Strokes);
  assert(!info.live && info.strokeCount == 0);
  assert(std::fabs(info.serverTime - 1789332500.1) < 1e-6);
  assert(parse("{\"time\":1789332500.1,\"live\":true,\"strokes\":[]}", info,
               strokes) == LightningMessageKind::Strokes);
  assert(info.live);

  char text[512];
  assert(lightningFeedBuildUrl("https://hodiny:heslo@example.net/lightning.json",
                               49.90461f, 14.7842f, 119.2f, 1789332254.25, text,
                               sizeof(text)));
  assert(strcmp(text, "https://hodiny:heslo@example.net/lightning.json"
                      "?lat=49.9046&lon=14.7842&r=120&since=1789332254.250") == 0);
  assert(lightningFeedBuildUrl("http://h/l.json?x=1", 49.9f, 14.8f, 10.0f, 0,
                               text, sizeof(text)));
  assert(strstr(text, "?x=1&lat=") != nullptr);
  assert(strstr(text, "&since=0.000") != nullptr);
  assert(!lightningFeedBuildUrl("", 49.9f, 14.8f, 10.0f, 0, text, sizeof(text)));
  assert(!lightningFeedBuildUrl("http://h/l.json", 49.9f, 14.8f, 10.0f, 0, text,
                                30));

  // Praha - Brno je zhruba 185 km.
  const float distance =
      lightningDistanceKm(50.0755f, 14.4378f, 49.1951f, 16.6068f);
  assert(distance > 180.0f && distance < 190.0f);
  return 0;
}
