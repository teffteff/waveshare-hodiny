// Rozbor odpovědi obrazovky Doprava (toulky /api/doprava?for=hodiny).
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "../WaveshareHodiny/TrafficFeed.h"

#ifndef TRAFFIC_FIXTURE
#error "TRAFFIC_FIXTURE musí ukazovat na tools/fixtures/doprava-hodiny.json"
#endif

namespace {

std::string readFile(const char *path) {
  FILE *input = fopen(path, "rb");
  assert(input != nullptr);
  std::string text;
  char chunk[4096];
  size_t read = 0;
  while ((read = fread(chunk, 1, sizeof(chunk), input)) > 0)
    text.append(chunk, read);
  fclose(input);
  return text;
}

TrafficParseStatus parse(const std::string &text, TrafficData &data) {
  return trafficFeedParse(text.data(), text.size(), data);
}

const TrafficLine *findLine(const TrafficData &data, const char *id) {
  for (uint8_t index = 0; index < data.lineCount; ++index)
    if (strcmp(data.lines[index].id, id) == 0) return &data.lines[index];
  return nullptr;
}

void testFixture() {
  static TrafficData data;
  const std::string text = readFile(TRAFFIC_FIXTURE);
  assert(parse(text, data) == TrafficParseStatus::Ok);
  assert(data.now == 1791371126);
  assert(data.sampleTime == 1791371100);
  assert(!data.stale);

  assert(data.lineCount == 5);
  const TrafficLine *praha = findLine(data, "ondrejov-praha");
  assert(praha != nullptr && praha->kind == TrafficLineKind::Drive);
  assert(praha->pointCount == 17);
  assert(praha->x[0] == 243 && praha->y[0] == 214);
  assert(praha->x[16] == 76 && praha->y[16] == 101);
  assert(praha->segmentCount == 1);
  assert(praha->segments[0].from == 13 && praha->segments[0].to == 14 &&
         praha->segments[0].level == TRAFFIC_LEVEL_STANDING);
  const TrafficLine *benesov = findLine(data, "ondrejov-benesov");
  assert(benesov != nullptr && benesov->segmentCount == 0);
  const TrafficLine *d1P = findLine(data, "d1-P");
  const TrafficLine *d1B = findLine(data, "d1-B");
  assert(d1P != nullptr && d1P->kind == TrafficLineKind::D1Prague);
  assert(d1B != nullptr && d1B->kind == TrafficLineKind::D1Brno);
  assert(d1B->segmentCount == 2);
  assert(d1B->segments[0].from == 1 && d1B->segments[0].to == 5 &&
         d1B->segments[0].level == TRAFFIC_LEVEL_JAM);
  const TrafficLine *via = findLine(data, "via-B");
  assert(via != nullptr && via->kind == TrafficLineKind::Via);

  assert(data.placeCount == 3);
  assert(strcmp(data.places[0].name, "Tomíčkova 1") == 0);
  assert(strcmp(data.places[2].name, "Ondřejov") == 0);
  assert(data.places[1].x == 192 && data.places[1].y == 317);
  assert(data.exitCount == 7);
  assert(data.exits[0].number == 15 && data.exits[6].number == 56);
  assert(data.busCount == 5);
  assert(strcmp(data.buses[0].line, "383") == 0 && !data.buses[0].hasDelay);

  assert(data.driveCount == 2);
  assert(strcmp(data.drives[0].to, "Praha") == 0);
  assert(data.drives[0].minutes == 30 && data.drives[0].usualMinutes == 29 &&
         data.drives[0].backMinutes == 30);
  // Důvod zdržení z textu cesty, s interpunkcí, kterou písmo hodin umí.
  assert(strcmp(data.drives[0].detail, "stojí Chodov - Tomíčkova, 12 km/h") == 0);
  // Text bez tečky nic navíc neříká.
  assert(strcmp(data.drives[1].to, "Benešov") == 0);
  assert(data.drives[1].detail[0] == '\0');
  // Zbylé texty jsou D1 oběma směry.
  assert(data.textCount == 2);
  assert(strcmp(data.texts[0], "D1 do Prahy: jede") == 0);
  assert(strncmp(data.texts[1], "D1 na Brno: stojí Všechromy - Mirošovice", 41) == 0);
  assert(data.warningCount == 1);
  assert(strcmp(data.warnings[0],
                "Dopravní kolony, D1 Modletice - Hvězdonice, +10 min") == 0);
  assert(data.departureCount == 4);
  assert(strcmp(data.departures[3], "13:58 490 Strančice žel.st.") == 0);
}

void testNullsDelaysAndStale() {
  static TrafficData data;
  const std::string text =
      "{\"now\":1791371126,\"t\":null,\"stale\":true,"
      "\"lines\":[{\"id\":\"d1-B\",\"xy\":[1,2,3,4,5,6],"
      "\"segs\":[[2,0,9],[0,7,3],[1,1,2],\"x\",[0,1]]}],"
      "\"buses\":[{\"line\":\"383\",\"xy\":[10,20],\"delay\":240},"
      "{\"line\":\"490\",\"xy\":[10,20],\"delay\":null},"
      "{\"line\":\"1\",\"xy\":[10]}],"
      "\"drives\":[{\"to\":\"Praha\",\"min\":null,\"usual\":-5,\"back\":31}]}";
  assert(parse(text, data) == TrafficParseStatus::Ok);
  assert(data.sampleTime == 0 && data.stale);
  const TrafficLine &line = data.lines[0];
  assert(line.pointCount == 3);
  // Prohozené pořadí se srovná, úroveň nad uzavírku se ořízne, úsek za koncem
  // čáry se zkrátí, nulový a rozbitý se zahodí.
  assert(line.segmentCount == 2);
  assert(line.segments[0].from == 0 && line.segments[0].to == 2 &&
         line.segments[0].level == TRAFFIC_LEVEL_CLOSED);
  assert(line.segments[1].from == 0 && line.segments[1].to == 2 &&
         line.segments[1].level == TRAFFIC_LEVEL_STANDING);
  assert(data.busCount == 2);
  assert(data.buses[0].hasDelay && data.buses[0].delaySeconds == 240);
  assert(!data.buses[1].hasDelay);
  assert(data.driveCount == 1);
  assert(data.drives[0].minutes == -1 && data.drives[0].usualMinutes == -1 &&
         data.drives[0].backMinutes == 31);
  assert(data.placeCount == 0 && data.exitCount == 0 && data.textCount == 0);
}

void testMalformed() {
  static TrafficData data;
  assert(trafficFeedParse(nullptr, 0, data) == TrafficParseStatus::NotJson);
  assert(parse("", data) == TrafficParseStatus::NotJson);
  assert(parse("<html>502</html>", data) == TrafficParseStatus::NotJson);
  assert(parse("[1,2]", data) == TrafficParseStatus::NotJson);
  // Useknutá odpověď.
  const std::string full = readFile(TRAFFIC_FIXTURE);
  for (size_t cut : {size_t(1), size_t(50), full.size() / 2, full.size() - 1}) {
    const TrafficParseStatus status =
        trafficFeedParse(full.data(), cut, data);
    assert(status == TrafficParseStatus::NotJson);
  }
  // Objekt bez čar není odpověď dopravy.
  assert(parse("{\"now\":1791371126}", data) == TrafficParseStatus::Invalid);
  assert(parse("{\"lines\":{}}", data) == TrafficParseStatus::Invalid);
  // Čára s lichým počtem čísel, textem místo čísla nebo jediným bodem se
  // přeskočí, ostatní zůstanou.
  assert(parse("{\"lines\":[{\"id\":\"a\",\"xy\":[1,2,3]},"
               "{\"id\":\"b\",\"xy\":[1,\"x\",3,4]},"
               "{\"id\":\"c\",\"xy\":[1e9,2,3,4]},"
               "{\"id\":\"d\",\"xy\":[1,2,3,4]}]}",
               data) == TrafficParseStatus::Ok);
  // Lichý konec se zahodí, z "a" zbude jediný bod a ta se přeskočí.
  assert(data.lineCount == 1 && strcmp(data.lines[0].id, "d") == 0);
}

void testOversized() {
  static TrafficData data;
  // Víc čar, bodů, autobusů a textů, než se vejde: přebytek se zahodí.
  std::string text = "{\"lines\":[";
  for (int line = 0; line < 12; ++line) {
    if (line) text += ',';
    text += "{\"id\":\"ondrejov-x\",\"xy\":[";
    for (int point = 0; point < 200; ++point) {
      if (point) text += ',';
      text += std::to_string(point) + "," + std::to_string(point);
    }
    text += "],\"segs\":[";
    for (int seg = 0; seg < 30; ++seg) {
      if (seg) text += ',';
      text += "[" + std::to_string(seg) + "," + std::to_string(seg + 1) + ",2]";
    }
    text += "]}";
  }
  text += "],\"buses\":[";
  for (int bus = 0; bus < 40; ++bus) {
    if (bus) text += ',';
    text += "{\"line\":\"383\",\"xy\":[1,2],\"delay\":0}";
  }
  text += "],\"warnings\":[";
  for (int warning = 0; warning < 9; ++warning) {
    if (warning) text += ',';
    text += "\"" + std::string(300, 'a') + "\"";
  }
  text += "],\"departures\":[\"" + std::string(200, 'b') + "\"]}";
  assert(parse(text, data) == TrafficParseStatus::Ok);
  assert(data.lineCount == TRAFFIC_MAX_LINES);
  for (uint8_t line = 0; line < data.lineCount; ++line) {
    assert(data.lines[line].pointCount == TRAFFIC_MAX_POINTS);
    assert(data.lines[line].segmentCount == TRAFFIC_MAX_SEGMENTS);
  }
  assert(data.busCount == TRAFFIC_MAX_BUSES);
  assert(data.warningCount == TRAFFIC_MAX_WARNINGS);
  assert(strlen(data.warnings[0]) == TRAFFIC_TEXT_LENGTH - 1);
  assert(data.departureCount == 1);
}

void testFoldText() {
  char text[96];
  strcpy(text, "A \xC2\xB7 B \xE2\x86\x92 C \xE2\x80\x93 D\xE2\x80\xA6 \xE2\x80\x9E" "E\xE2\x80\x9C");
  trafficFoldText(text);
  assert(strcmp(text, "A, B - C - D... \"E\"") == 0);
  // Čeština zůstane, jak je.
  strcpy(text, "Benešov, Mirošovice, žel.st.");
  trafficFoldText(text);
  assert(strcmp(text, "Benešov, Mirošovice, žel.st.") == 0);
  // Dlouhý text se zkrátí po celých znacích, ne uprostřed "š".
  static TrafficData data;
  std::string longWarning = "{\"lines\":[],\"warnings\":[\"";
  for (int index = 0; index < 60; ++index) longWarning += "š";
  longWarning += "\"]}";
  assert(parse(longWarning, data) == TrafficParseStatus::Ok);
  const size_t length = strlen(data.warnings[0]);
  assert(length == TRAFFIC_TEXT_LENGTH - 1 || length == TRAFFIC_TEXT_LENGTH - 2);
  assert(length % 2 == 0);
}

}  // namespace

int main() {
  testFixture();
  testNullsDelaysAndStale();
  testMalformed();
  testOversized();
  testFoldText();
  return 0;
}
