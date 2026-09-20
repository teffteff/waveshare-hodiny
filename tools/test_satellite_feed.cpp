#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "../WaveshareHodiny/SatelliteFeed.h"

namespace {
SatelliteParseStatus parse(const std::string &text, SatelliteTrack *tracks,
                           size_t capacity, SatelliteFeedInfo &info) {
  return satelliteParseFeed(text.c_str(), text.c_str() + text.size(), tracks,
                            capacity, info);
}

// Dráha se dvěma body po patnácti sekundách, jak ji pošle server.
std::string feed(const std::string &satellites, const std::string &extra = "") {
  return "{\"v\":1,\"time\":1789507185,\"step\":15,\"samples\":2,\"sun\":-334,"
         "\"total\":3,\"age\":2,\"sats\":[" +
         satellites + "]" + extra + "}";
}

bool near(float a, float b, float tolerance) {
  return std::fabs(a - b) <= tolerance;
}

void testParsesTracksAndMetadata() {
  SatelliteTrack tracks[4];
  SatelliteFeedInfo info;
  const std::string text = feed(
      "{\"id\":25544,\"n\":\"ISS (ZARYA)\",\"g\":0,\"h\":418,\"r\":1210,"
      "\"l\":1,\"p\":[2750,153,2800,250]},"
      // Vadné záznamy: azimut mimo rozsah, chybějící bod, neznámá skupina,
      // záporné id, text místo čísla.
      "{\"id\":1,\"n\":\"BAD AZ\",\"g\":1,\"p\":[3600,10,0,10]},"
      "{\"id\":2,\"n\":\"SHORT\",\"g\":1,\"p\":[10,10,20]},"
      "{\"id\":3,\"n\":\"GROUP\",\"g\":9,\"p\":[10,10,20,20]},"
      "{\"id\":-4,\"n\":\"NEG\",\"g\":1,\"p\":[10,10,20,20]},"
      "{\"id\":5,\"n\":\"TEXT\",\"g\":1,\"p\":[\"10\",10,20,20]},"
      "{\"id\":44713,\"n\":\"STARLINK-1007\",\"g\":5,\"h\":550,\"r\":900000,"
      "\"l\":0,\"p\":[100,-20,120,-5]}",
      ",\"pass\":{\"id\":25544,\"n\":\"ISS\",\"rise\":1789508390,"
      "\"set\":1789508779,\"maxTime\":1789508587,\"max\":48,\"vis\":1},"
      "\"problem\":\"weather elements are 8 days old\","
      "\"pending\":[\"starlink\"]");
  assert(parse(text, tracks, 4, info) == SatelliteParseStatus::Ok);
  assert(info.count == 2);
  assert(info.startEpoch == 1789507185LL);
  assert(info.stepSeconds == 15 && info.sampleCount == 2);
  assert(info.hasSunElevation && near(info.sunElevationDeg, -33.4f, 0.01f));
  assert(info.total == 3 && info.ageHours == 2);
  assert(info.pending);
  assert(strcmp(info.problem, "weather elements are 8 days old") == 0);
  // Starší server posílá jen "pass"; popisek u něj patří ISS.
  assert(info.passCount == 1 && info.passes[0].valid && info.passes[0].visible);
  assert(strcmp(info.passes[0].name, "ISS") == 0 && !info.passes[0].preferred);
  assert(info.passes[0].rise == 1789508390LL && info.passes[0].set == 1789508779LL);
  assert(info.passes[0].maxTime == 1789508587LL &&
         info.passes[0].maxElevationDeg == 48);

  assert(tracks[0].noradId == 25544 && strcmp(tracks[0].name, "ISS (ZARYA)") == 0);
  assert(tracks[0].group == SATELLITE_GROUP_STATIONS);
  assert(tracks[0].sunlit && tracks[0].altitudeKm == 418 && tracks[0].rangeKm == 1210);
  assert(tracks[0].azimuthTenths[1] == 2800 && tracks[0].elevationTenths[1] == 250);
  assert(tracks[1].noradId == 44713 && tracks[1].group == SATELLITE_GROUP_STARLINK);
  // Vzdálenost přes strop se ořízne, nepřeteče.
  assert(tracks[1].rangeKm == 65535 && !tracks[1].sunlit);
  assert(tracks[1].elevationTenths[0] == -20);
}

void testCapacityAndInvalidDocuments() {
  SatelliteTrack tracks[1];
  SatelliteFeedInfo info;
  const std::string two = feed(
      "{\"id\":1,\"n\":\"A\",\"g\":1,\"p\":[0,100,10,110]},"
      "{\"id\":2,\"n\":\"B\",\"g\":1,\"p\":[0,100,10,110]}");
  assert(parse(two, tracks, 1, info) == SatelliteParseStatus::Ok);
  assert(info.count == 1 && tracks[0].noradId == 1);

  assert(parse("<html>502</html>", tracks, 1, info) == SatelliteParseStatus::NotJson);
  assert(parse("{\"error\":\"loading\"}", tracks, 1, info) ==
         SatelliteParseStatus::Invalid);
  // Jiná verze formátu, starý čas, příliš mnoho bodů, useknutý dokument.
  assert(parse("{\"v\":2,\"time\":1789507185,\"step\":15,\"samples\":2,\"sats\":[]}",
               tracks, 1, info) == SatelliteParseStatus::Invalid);
  assert(parse("{\"v\":1,\"time\":1500000000,\"step\":15,\"samples\":2,\"sats\":[]}",
               tracks, 1, info) == SatelliteParseStatus::Invalid);
  assert(parse("{\"v\":1,\"time\":1789507185,\"step\":15,\"samples\":14,\"sats\":[]}",
               tracks, 1, info) == SatelliteParseStatus::Invalid);
  const std::string cut = two.substr(0, two.size() / 2);
  assert(parse(cut, tracks, 1, info) == SatelliteParseStatus::NotJson);
  // Neplatný přelet se zahodí, zbytek odpovědi platí.
  assert(parse(feed("", ",\"pass\":{\"rise\":1789508390,\"set\":1789508000,\"max\":48}"),
               tracks, 1, info) == SatelliteParseStatus::Ok);
  assert(info.passCount == 0 && info.count == 0);
  // Jméno s řídicím znakem se nerozbije.
  assert(parse(feed("{\"id\":7,\"n\":\"A\\u0007B\",\"g\":2,\"p\":[0,0,0,0]}"),
               tracks, 1, info) == SatelliteParseStatus::Ok);
  for (const char *c = tracks[0].name; *c; ++c) assert(*c >= 0x20 && *c <= 0x7e);
}

void testUrl() {
  char url[160];
  assert(satelliteFeedBuildUrl("https://hodiny:x@host/satellites.json", 49.90461f,
                               14.7842f, 0b000111, 10, url, sizeof(url)));
  assert(strcmp(url, "https://hodiny:x@host/satellites.json?lat=49.90&lon=14.78"
                     "&groups=stations,visual,weather&minel=10") == 0);
  assert(satelliteFeedBuildUrl("http://host/s?x=1", -33.5f, -70.25f, 0b100000, 0,
                               url, sizeof(url)));
  assert(strcmp(url, "http://host/s?x=1&lat=-33.50&lon=-70.25&groups=starlink&minel=0") == 0);
  assert(!satelliteFeedBuildUrl("http://host/s", 1, 1, 0, 10, url, sizeof(url)));
  assert(!satelliteFeedBuildUrl("http://host/s", 1, 1, 0b10000000, 10, url, sizeof(url)));
  assert(satelliteFeedBuildUrl("http://host/s", 1, 1, 0b01000000, 10, url, sizeof(url)));
  assert(strcmp(url, "http://host/s?lat=1.00&lon=1.00&groups=satgus&minel=10") == 0);
  // Všechny skupiny najednou se do vnitřního bufferu vejdou.
  assert(satelliteFeedBuildUrl("http://host/s", 1, 1, 0b01111111, 10, url,
                               sizeof(url)));
  assert(!satelliteFeedBuildUrl("", 1, 1, 1, 10, url, sizeof(url)));
  assert(!satelliteFeedBuildUrl("http://host/s", NAN, 1, 1, 10, url, sizeof(url)));
  char tiny[20];
  assert(!satelliteFeedBuildUrl("http://host/s", 1, 1, 1, 10, tiny, sizeof(tiny)));
  assert(strcmp(satelliteGroupName(SATELLITE_GROUP_GNSS), "gnss") == 0);
  assert(satelliteGroupName(SATELLITE_GROUP_COUNT) == nullptr);
}

void testProjectionAndInterpolation() {
  float x = 0;
  float y = 0;
  satelliteSkyProject(0.0f, 90.0f, x, y);
  assert(near(x, 0, 1e-5f) && near(y, 0, 1e-5f));
  satelliteSkyProject(0.0f, 0.0f, x, y);   // sever na obzoru nahoře
  assert(near(x, 0, 1e-5f) && near(y, -1, 1e-5f));
  satelliteSkyProject(90.0f, 45.0f, x, y); // východ vpravo, v půli poloměru
  assert(near(x, 0.5f, 1e-5f) && near(y, 0, 1e-5f));

  SatelliteFeedInfo info;
  info.startEpoch = 1789507185LL;
  info.stepSeconds = 15;
  info.sampleCount = 3;
  SatelliteTrack track;
  // Přelet přes sever: 350° -> 10° na stejné výšce. Interpolace azimutu by
  // vedla přes jih, v rovině kruhu jde krátce přes sever.
  track.azimuthTenths[0] = 3500;
  track.elevationTenths[0] = 300;
  track.azimuthTenths[1] = 100;
  track.elevationTenths[1] = 300;
  track.azimuthTenths[2] = 300;
  track.elevationTenths[2] = 400;
  SatelliteSkyPoint point;
  assert(satelliteTrackAt(track, info, 1789507185.0 + 7.5, point));
  assert(point.azimuthDeg < 1.0f || point.azimuthDeg > 359.0f);
  assert(near(point.elevationDeg, 30.0f, 0.01f));
  assert(point.y < 0);
  // Konec okna platí, za ním ne; ani před začátkem.
  assert(satelliteTrackAt(track, info, 1789507185.0 + 30.0, point));
  assert(near(point.elevationDeg, 40.0f, 0.01f) && near(point.azimuthDeg, 30.0f, 0.05f));
  assert(!satelliteTrackAt(track, info, 1789507185.0 + 30.5, point));
  assert(!satelliteTrackAt(track, info, 1789507184.0, point));
  assert(!satelliteTrackAt(track, info, NAN, point));
  assert(satelliteFeedEndEpoch(info) == 1789507215.0);
}

// Řádek pod oblohou: nejbližší přelet, ale domácí družice má náskok.
void testPickPass() {
  SatelliteTrack tracks[1];
  SatelliteFeedInfo info;
  const std::string both = feed(
      "",
      ",\"passes\":[{\"id\":62713,\"n\":\"SATGUS\",\"rise\":1789510000,"
      "\"set\":1789510400,\"maxTime\":1789510200,\"max\":31,\"vis\":1,"
      "\"pref\":1},"
      "{\"id\":25544,\"n\":\"ISS\",\"rise\":1789509000,\"set\":1789509400,"
      "\"maxTime\":1789509200,\"max\":48,\"vis\":0}],"
      "\"pass\":{\"id\":25544,\"n\":\"ISS\",\"rise\":1789509000,"
      "\"set\":1789509400,\"maxTime\":1789509200,\"max\":48,\"vis\":0}");
  assert(parse(both, tracks, 1, info) == SatelliteParseStatus::Ok);
  assert(info.passCount == 2);
  assert(strcmp(info.passes[0].name, "SATGUS") == 0 && info.passes[0].preferred);
  assert(info.passes[0].visible && info.passes[0].maxElevationDeg == 31);
  assert(!info.passes[1].preferred);
  // ISS začíná o necelou půlhodinu dřív, přesto vyhraje SATGUS.
  const SatellitePass *pick = satellitePickPass(info, 1789508000LL);
  assert(pick != nullptr && strcmp(pick->name, "SATGUS") == 0);
  // Hodinu před ISS už je náskok malý.
  info.passes[0].rise = info.passes[1].rise + 3600;
  info.passes[0].set = info.passes[0].rise + 400;
  pick = satellitePickPass(info, 1789508000LL);
  assert(pick != nullptr && strcmp(pick->name, "ISS") == 0);
  // Probíhající přelet vyhraje i nad domácí družicí, která přijde za chvíli.
  pick = satellitePickPass(info, info.passes[1].rise + 10);
  assert(pick != nullptr && strcmp(pick->name, "ISS") == 0);
  // Po konci ISS zbude SATGUS, po obou nezbude nic.
  pick = satellitePickPass(info, info.passes[1].set + 1);
  assert(pick != nullptr && strcmp(pick->name, "SATGUS") == 0);
  assert(satellitePickPass(info, info.passes[0].set + 1) == nullptr);

  // Přelet bez jména se v seznamu zahodí; starší tvar "pass" se nečte.
  SatelliteFeedInfo nameless;
  assert(parse(feed("", ",\"passes\":[{\"id\":62713,\"rise\":1789510000,"
                        "\"set\":1789510400,\"max\":31}],"
                        "\"pass\":{\"id\":25544,\"n\":\"ISS\","
                        "\"rise\":1789509000,\"set\":1789509400,\"max\":48}"),
               tracks, 1, nameless) == SatelliteParseStatus::Ok);
  assert(nameless.passCount == 0 && satellitePickPass(nameless, 1789508000LL) == nullptr);
}

void testShortName() {
  char text[25];
  satelliteShortName("ISS (ZARYA)", text, sizeof(text));
  assert(strcmp(text, "ISS") == 0);
  satelliteShortName("GPS BIIR-2  (PRN 13)", text, sizeof(text));
  assert(strcmp(text, "GPS BIIR-2") == 0);
  satelliteShortName("NOAA 19", text, sizeof(text));
  assert(strcmp(text, "NOAA 19") == 0);
  satelliteShortName("(UNKNOWN)", text, sizeof(text));
  assert(strcmp(text, "(UNKNOWN)") == 0);
  char small[4];
  satelliteShortName("STARLINK-1007", small, sizeof(small));
  assert(strcmp(small, "STA") == 0);
}
}  // namespace

int main() {
  testParsesTracksAndMetadata();
  testCapacityAndInvalidDocuments();
  testUrl();
  testProjectionAndInterpolation();
  testPickPass();
  testShortName();
  puts("satellite feed OK");
  return 0;
}
