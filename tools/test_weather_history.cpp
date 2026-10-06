#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "WeatherHistory.h"

namespace {

const char *SAMPLE =
    "{\"start\":1791248400,\"now\":1791335100,"
    "\"temp\":{\"start\":1791248400,\"step\":600,"
    "\"values\":[17.9,18.1,null,18.5,18.9,19.1,19.5]},"
    "\"wind\":[2.3,2.8,null],\"rain\":[0.0,0.5,0.0],"
    "\"code\":[0,61,null],\"day\":[1,1,0]}";

void testParse() {
  static WeatherHistoryData data;
  assert(weatherHistoryParse(SAMPLE, strlen(SAMPLE), data));
  // Čas od epochy přesně, ne zaokrouhlený přes float.
  assert(data.start == 1791248400);
  assert(data.now == 1791335100);
  assert(data.lineStep == 600);
  assert(data.lineCount == 7);
  assert(std::isnan(data.line[2]));
  assert(data.hourCount == 3);
  assert(data.hours[1].time == 1791248400 + 3600);
  assert(data.hours[0].temperatureC == 17.9f);
  // Hodina 1 začíná za šestou desetiminutovkou (19.5).
  assert(data.hours[1].temperatureC == 19.5f);
  assert(std::isnan(data.hours[2].temperatureC));
  assert(data.hours[0].windKmh == 2.3f);
  assert(std::isnan(data.hours[2].windKmh));
  assert(data.hours[1].precipitationMm == 0.5f);
  assert(data.hours[1].weatherCode == 61);
  assert(data.hours[2].weatherCode == -1);
  assert(data.hours[0].isDay && !data.hours[2].isDay);
}

void testLineAt() {
  static WeatherHistoryData data;
  assert(weatherHistoryParse(SAMPLE, strlen(SAMPLE), data));
  assert(std::fabs(weatherHistoryLineAt(data, 1791248400 + 300) - 18.0f) < 0.001f);
  // Přes chybějící bod se neinterpoluje.
  assert(std::isnan(weatherHistoryLineAt(data, 1791248400 + 900)));
  assert(std::isnan(weatherHistoryLineAt(data, 1791248400 - 1)));
  assert(std::isnan(weatherHistoryLineAt(data, 1791248400 + 7 * 600)));
}

void testRejects() {
  static WeatherHistoryData data;
  assert(!weatherHistoryParse(nullptr, 0, data));
  assert(!weatherHistoryParse("[]", 2, data));
  const char *noStart = "{\"wind\":[1]}";
  assert(!weatherHistoryParse(noStart, strlen(noStart), data));
  const char *cut = "{\"start\":1791248400,\"wind\":[1,2";
  assert(!weatherHistoryParse(cut, strlen(cut), data));
  const char *empty = "{\"start\":1791248400,\"temp\":{\"values\":[]}}";
  assert(!weatherHistoryParse(empty, strlen(empty), data));
}

void testBuildUrl() {
  char url[256];
  const char *base = "https://h:p@s/history.json?wind=obyvak&rain=obyvak";
  assert(weatherHistoryBuildUrl(base, "sensor.pracovna_venkovni_teplota", url,
                                sizeof(url)));
  assert(strcmp(url, "https://h:p@s/history.json?wind=obyvak&rain=obyvak"
                     "&temp_entity=sensor.pracovna_venkovni_teplota") == 0);
  // Bez dotazu otazník; adresa, která teplotu vybírá sama, zůstane.
  assert(weatherHistoryBuildUrl("https://s/history.json", "sensor.a", url,
                                sizeof(url)));
  assert(strcmp(url, "https://s/history.json?temp_entity=sensor.a") == 0);
  assert(weatherHistoryBuildUrl("https://s/h.json?wind=x&temp=pracovna",
                                "sensor.a", url, sizeof(url)));
  assert(strcmp(url, "https://s/h.json?wind=x&temp=pracovna") == 0);
  // "temperature" není "temp"; podivná entita se nepřidá; prázdná taky ne.
  assert(weatherHistoryBuildUrl("https://s/h.json?temperature=1", "sensor.a",
                                url, sizeof(url)));
  assert(strcmp(url, "https://s/h.json?temperature=1&temp_entity=sensor.a") == 0);
  assert(weatherHistoryBuildUrl("https://s/h.json", "sensor.a&x=1", url,
                                sizeof(url)));
  assert(strcmp(url, "https://s/h.json") == 0);
  assert(weatherHistoryBuildUrl("https://s/h.json", "", url, sizeof(url)));
  assert(strcmp(url, "https://s/h.json") == 0);
  char tiny[10];
  assert(!weatherHistoryBuildUrl("https://s/h.json", "sensor.a", tiny,
                                 sizeof(tiny)));
}

}  // namespace

int main() {
  testParse();
  testLineAt();
  testRejects();
  testBuildUrl();
  printf("weather history: OK\n");
  return 0;
}
