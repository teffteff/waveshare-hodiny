#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "../WaveshareHodiny/WeatherWarnings.h"

namespace {

bool parse(const char *text, WeatherWarningFeed &feed) {
  return weatherWarningsParse(text, text + strlen(text), feed);
}

// Odpověď serveru pro Ondřejov (ORP Říčany) z přehraného CAP souboru
// 28. 6. 2026 22:53 SELČ.
const char STORM_RESPONSE[] =
    "{\"v\":1,\"time\":1782680082,\"sent\":1782680022,\"covered\":true,"
    "\"orp\":\"2122\",\"area\":\"\\u0158\\u00ed\\u010dany\",\"stale\":false,"
    "\"warnings\":["
    "{\"id\":\"SIVS I.3\",\"lvl\":4,\"type\":5,\"ev\":\"Extrémně vysoké teploty\","
    "\"on\":1782679895,\"ex\":1782684000},"
    "{\"id\":\"SIVS I.2\",\"lvl\":3,\"type\":5,\"ev\":\"Velmi vysoké teploty\","
    "\"on\":1782684000,\"ex\":1782770400},"
    "{\"id\":\"SIVS X.2\",\"lvl\":3,\"type\":3,\"ev\":\"Velmi silné bouřky\","
    "\"on\":1782723600,\"ex\":1782770400},"
    "{\"id\":\"SIVS XIV.1\",\"lvl\":2,\"type\":8,\"ev\":\"Nebezpečí požárů\","
    "\"on\":1782679895,\"ex\":1782684000},"
    "{\"id\":\"SVRS SMOGSIT.O3\",\"lvl\":2,\"type\":11,"
    "\"ev\":\"Smogová situace - troposférický ozón O3\",\"on\":1782679895,\"ex\":0}]}";

constexpr int64_t STORM_NOW = 1782680082;  // 28. 6. 22:54 SELČ

}  // namespace

int main() {
  // Časy na řádku jsou místní; testy počítají se středoevropským časem.
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();

  WeatherWarningFeed feed;

  // --- rozbor odpovědi ---
  assert(parse(STORM_RESPONSE, feed));
  assert(feed.covered);
  assert(!feed.stale);
  assert(strcmp(feed.area, "Říčany") == 0);
  assert(feed.serverTime == 1782680082U);
  assert(feed.count == 5);
  assert(feed.items[0].level == 4 && feed.items[0].type == 5);
  assert(strcmp(feed.items[0].id, "SIVS I.3") == 0);
  assert(strcmp(feed.items[0].event, "Extrémně vysoké teploty") == 0);
  assert(feed.items[0].onset == 1782679895);
  assert(feed.items[4].expires == 0);

  // Prázdný seznam je klid; chybějící seznam je chyba.
  assert(parse("{\"covered\":true,\"warnings\":[]}", feed));
  assert(feed.count == 0 && feed.covered);
  assert(!parse("{\"covered\":true}", feed));
  assert(!parse("nesmysl", feed));
  assert(!parse("{\"warnings\":[", feed));

  // Stupeň mimo žlutou až červenou, prázdný text a chybějící začátek se
  // přeskočí; zbytek platí. Bez id poslouží text.
  assert(parse("{\"warnings\":[{\"lvl\":1,\"ev\":\"x\",\"on\":1},"
               "{\"lvl\":3,\"ev\":\"\",\"on\":1},{\"lvl\":3,\"ev\":\"y\"},"
               "{\"lvl\":3,\"ev\":\"Silný vítr\",\"on\":5}]}",
               feed));
  assert(feed.count == 1);
  assert(strcmp(feed.items[0].id, "Silný vítr") == 0);

  // --- adresa ---
  char url[160];
  assert(weatherWarningsBuildUrl("https://hodiny:heslo@server/warnings.json",
                                 49.90461f, 14.7842f, false, url, sizeof(url)));
  assert(strcmp(url,
                "https://hodiny:heslo@server/warnings.json?lat=49.90461&"
                "lon=14.78420&lang=cs") == 0);
  assert(weatherWarningsBuildUrl("https://server/w?x=1", 1.0f, 2.0f, true, url,
                                 sizeof(url)));
  assert(strstr(url, "?x=1&lat=") != nullptr && strstr(url, "lang=en") != nullptr);
  assert(!weatherWarningsBuildUrl("https://server/warnings.json", 1.0f, 2.0f,
                                  false, url, 20));
  assert(!weatherWarningsBuildUrl("", 1.0f, 2.0f, false, url, sizeof(url)));

  // --- řádek na radaru ---
  assert(parse(STORM_RESPONSE, feed));
  uint8_t others = 0;
  const WeatherWarning *top = weatherWarningsTop(feed, 2, STORM_NOW, others);
  assert(top != nullptr && top->level == 4);
  assert(others == 4);
  char text[96];
  // Červená platí do půlnoci: "do 24:00", ne "do zítra 00:00".
  weatherWarningBannerText(*top, others, STORM_NOW, false, text, sizeof(text));
  assert(strcmp(text, "Extrémně vysoké teploty do 24:00  +4") == 0);
  weatherWarningBannerText(*top, 0, STORM_NOW, true, text, sizeof(text));
  assert(strcmp(text, "Extrémně vysoké teploty until 24:00") == 0);

  // Jen oranžové a výš: žlutý požár ani smog se nepočítají.
  top = weatherWarningsTop(feed, 3, STORM_NOW, others);
  assert(top->level == 4 && others == 2);

  // Po půlnoci červená skončila; první je oranžové vedro, které právě platí.
  const int64_t afterMidnight = 1782684600;  // 29. 6. 00:10
  top = weatherWarningsTop(feed, 3, afterMidnight, others);
  assert(strcmp(top->id, "SIVS I.2") == 0 && others == 1);
  weatherWarningBannerText(*top, 0, afterMidnight, false, text, sizeof(text));
  assert(strcmp(text, "Velmi vysoké teploty do 24:00") == 0);

  // Bouřka začne v 11:00 téhož dne.
  weatherWarningBannerText(feed.items[2], 0, afterMidnight, false, text,
                           sizeof(text));
  assert(strcmp(text, "Velmi silné bouřky od 11:00") == 0);
  // Večer předtím je to "od zítra 11:00".
  weatherWarningBannerText(feed.items[2], 0, STORM_NOW, false, text,
                           sizeof(text));
  assert(strcmp(text, "Velmi silné bouřky od zítra 11:00") == 0);
  weatherWarningBannerText(feed.items[2], 0, STORM_NOW, true, text,
                           sizeof(text));
  assert(strcmp(text, "Velmi silné bouřky from tmrw 11:00") == 0);
  // Za víc než den se píše datum.
  weatherWarningBannerText(feed.items[2], 0, STORM_NOW - 2 * 86400, false,
                           text, sizeof(text));
  assert(strcmp(text, "Velmi silné bouřky od 29. 6. 11:00") == 0);
  // Do odvolání: bez času.
  weatherWarningBannerText(feed.items[4], 0, STORM_NOW, false, text,
                           sizeof(text));
  assert(strcmp(text, "Smogová situace - troposférický ozón O3") == 0);

  // Výstraha, která začne za víc než den, se ještě neukazuje.
  WeatherWarning distant = feed.items[2];
  distant.onset = STORM_NOW + 2 * 86400;
  assert(!weatherWarningRelevant(distant, STORM_NOW));
  distant.onset = STORM_NOW - 10;
  distant.expires = STORM_NOW - 1;
  assert(!weatherWarningRelevant(distant, STORM_NOW));

  // Nic od zvoleného stupně: nic.
  assert(parse("{\"covered\":true,\"warnings\":[{\"id\":\"a\",\"lvl\":2,"
               "\"ev\":\"Silný vítr\",\"on\":1,\"ex\":0}]}",
               feed));
  assert(weatherWarningsTop(feed, 3, STORM_NOW, others) == nullptr);

  // --- přepnutí na radar ---
  assert(parse(STORM_RESPONSE, feed));
  char alerted[4][WEATHER_WARNING_ID_LENGTH] = {};
  // Oranžová a výš: červené vedro platí teď.
  const WeatherWarning *announce =
      weatherWarningsToAnnounce(feed, 3, STORM_NOW, alerted, 0);
  assert(announce != nullptr && strcmp(announce->id, "SIVS I.3") == 0);
  // Jednou ohlášená se znovu nehlásí. Oranžové vedro začne o půlnoci, víc než
  // hodinu dopředu, a bouřka až zítra - takže teď nic dalšího.
  strcpy(alerted[0], "SIVS I.3");
  assert(weatherWarningsToAnnounce(feed, 3, STORM_NOW, alerted, 1) == nullptr);
  // Hodinu před půlnocí přijde na řadu oranžové vedro.
  announce = weatherWarningsToAnnounce(feed, 3, 1782681000, alerted, 1);
  assert(announce != nullptr && strcmp(announce->id, "SIVS I.2") == 0);
  // Jen červená: po ohlášení vedra už nic.
  assert(weatherWarningsToAnnounce(feed, 4, STORM_NOW, alerted, 1) == nullptr);
  // Nula znamená nepřepínat vůbec.
  assert(weatherWarningsToAnnounce(feed, 0, STORM_NOW, alerted, 0) == nullptr);
  // Bouřku od 11:00 ohlásí až od desíti.
  strcpy(alerted[1], "SIVS I.2");
  assert(weatherWarningsToAnnounce(feed, 3, 1782719000, alerted, 2) == nullptr);
  announce = weatherWarningsToAnnounce(feed, 3, 1782720300, alerted, 2);
  assert(announce != nullptr && strcmp(announce->id, "SIVS X.2") == 0);

  puts("weather warnings OK");
  return 0;
}
