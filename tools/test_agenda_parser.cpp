#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "AgendaParser.h"

namespace {

// Zkrácená, jinak doslovná odpověď z https://$CLOCK_HOST/agenda.json ze
// 9. 9. 2026. Pořadí i tvar klíčů odpovídá tomu, co server opravdu posílá,
// včetně prázdného "day" u pokračování dne, prázdného "time" u celodenní
// události a diakritiky v titulcích.
const char *const REAL_RESPONSE =
    "{\"generated\":\"2026-09-09T15:01:02+02:00\",\"count\":12,\"items\":["
    "{\"day\":\"DNES\",\"date\":\"2026-09-09\",\"time\":\"14:00\","
    "\"title\":\"Plavání Vilem\",\"cal\":1},"
    "{\"day\":\"\",\"date\":\"\",\"time\":\"18:00\","
    "\"title\":\"Popelnice - která?\",\"cal\":0},"
    "{\"day\":\"ZÍTRA\",\"date\":\"2026-09-10\",\"time\":\"\","
    "\"title\":\"N s Bocanovou\",\"cal\":0},"
    "{\"day\":\"\",\"date\":\"\",\"time\":\"08:00\","
    "\"title\":\"Skoleni\",\"cal\":0},"
    "{\"day\":\"pá 11.9.\",\"date\":\"2026-09-11\",\"time\":\"16:00\","
    "\"title\":\"Sokol\",\"cal\":1}"
    "]}";

std::string copied(const char *source, size_t capacity) {
  char buffer[256];
  assert(capacity <= sizeof(buffer));
  memset(buffer, '#', sizeof(buffer));
  agendaCopyText(source, strlen(source), buffer, capacity);
  return std::string(buffer);
}

void testRealResponse() {
  AgendaFeed feed;
  const AgendaParseOutcome outcome =
      agendaParseFeed(REAL_RESPONSE, strlen(REAL_RESPONSE), AGENDA_MAX_ITEMS,
                      feed);
  assert(outcome.status == AgendaParseStatus::Ok);
  assert(outcome.count == 5);
  assert(feed.count == 5);

  // Čeština projde beze změny: písmo ji má, na rozdíl od zpráv, které
  // RssParser přepisuje do ASCII.
  assert(std::string(feed.items[0].title) == "Plavání Vilem");
  assert(std::string(feed.items[0].day) == "DNES");
  assert(std::string(feed.items[0].time) == "14:00");
  assert(feed.items[0].calendar == 1);
  assert(!feed.items[0].allDay());
  assert(feed.items[0].startsDay());

  // Pokračování dne nemá popisek, takže obrazovka nekreslí druhou hlavičku.
  assert(std::string(feed.items[1].day).empty());
  assert(!feed.items[1].startsDay());
  assert(std::string(feed.items[1].title) == "Popelnice - která?");
  assert(feed.items[1].calendar == 0);

  // Prázdný čas znamená celodenní událost.
  assert(std::string(feed.items[2].day) == "ZÍTRA");
  assert(std::string(feed.items[2].time).empty());
  assert(feed.items[2].allDay());

  assert(std::string(feed.items[4].day) == "pá 11.9.");
  assert(feed.items[4].calendar == 1);
}

void testItemLimit() {
  AgendaFeed feed;
  const AgendaParseOutcome outcome =
      agendaParseFeed(REAL_RESPONSE, strlen(REAL_RESPONSE), 2, feed);
  assert(outcome.status == AgendaParseStatus::Ok);
  assert(outcome.count == 2);
  assert(std::string(feed.items[1].title) == "Popelnice - která?");
}

void testEmptyAgenda() {
  // Den bez události je běžný stav, ne chyba: musí projít jako Ok s nulou,
  // aby obrazovka řekla "nic nemáš" místo "server je rozbitý".
  const char *const payload =
      "{\"generated\":\"2026-09-09T15:01:02+02:00\",\"count\":0,\"items\":[]}";
  AgendaFeed feed;
  const AgendaParseOutcome outcome =
      agendaParseFeed(payload, strlen(payload), AGENDA_MAX_ITEMS, feed);
  assert(outcome.status == AgendaParseStatus::Ok);
  assert(outcome.count == 0);
}

void testBrokenPayloads() {
  AgendaFeed feed;
  // Chybová stránka proxy místo JSONu.
  const char *const html = "<html><body>502 Bad Gateway</body></html>";
  assert(agendaParseFeed(html, strlen(html), AGENDA_MAX_ITEMS, feed).status ==
         AgendaParseStatus::NotJson);

  // Platný JSON, ale bez pole událostí.
  const char *const noItems = "{\"generated\":\"2026-09-09T15:01:02+02:00\"}";
  assert(agendaParseFeed(noItems, strlen(noItems), AGENDA_MAX_ITEMS, feed)
             .status == AgendaParseStatus::MissingArray);

  // Useknutá odpověď nesmí číst za koncem ani se zacyklit.
  const char *const truncated =
      "{\"items\":[{\"day\":\"DNES\",\"time\":\"14:00\",\"title\":\"Plavá";
  const AgendaParseOutcome outcome =
      agendaParseFeed(truncated, strlen(truncated), AGENDA_MAX_ITEMS, feed);
  assert(outcome.count == 0);

  assert(agendaParseFeed(nullptr, 0, AGENDA_MAX_ITEMS, feed).status ==
         AgendaParseStatus::NotJson);
}

void testTitleWithoutText() {
  // Titulek ze samých mezer nebo z jediného emoji by byl prázdný řádek.
  const char *const payload =
      "{\"items\":["
      "{\"day\":\"DNES\",\"time\":\"09:00\",\"title\":\"   \",\"cal\":0},"
      "{\"day\":\"\",\"time\":\"10:00\",\"title\":\"\\ud83d\\ude00\",\"cal\":0},"
      "{\"day\":\"\",\"time\":\"11:00\",\"title\":\"Sokol\",\"cal\":0}]}";
  AgendaFeed feed;
  const AgendaParseOutcome outcome =
      agendaParseFeed(payload, strlen(payload), AGENDA_MAX_ITEMS, feed);
  assert(outcome.status == AgendaParseStatus::Ok);
  assert(outcome.count == 1);
  assert(std::string(feed.items[0].title) == "Sokol");
}

void testTransliteration() {
  // Co písmo má, projde beze změny.
  assert(copied("Odčervit Zolíka", 64) == "Odčervit Zolíka");
  assert(copied("Štít 2026", 64) == "Štít 2026");
  assert(copied("Cvičení pejsků", 64) == "Cvičení pejsků");

  // Co písmo nemá, spadne na ASCII.
  assert(copied("Zürich", 64) == "Zurich");
  assert(copied("Straße", 64) == "Strasse");
  // Ó je i v češtině, takže ho písmo má a zůstane; mizí jen Ł a ź.
  assert(copied("Łódź", 64) == "Lódz");

  // Interpunkce, kterou sázejí kalendářové aplikace.
  assert(copied("Oběd \xE2\x80\x93 Petra", 64) == "Oběd - Petra");
  assert(copied("\xE2\x80\x9ETeta\xE2\x80\x9C", 64) == "\"Teta\"");
  assert(copied("Nakup\xE2\x80\xA6", 64) == "Nakup...");

  // Emoji z mobilu se zahodí, zbytek titulku zůstane.
  assert(copied("Fotbal \xF0\x9F\x8F\x88 Danik", 64) == "Fotbal Danik");

  // Bílé znaky se slučují a text se ořezává z obou stran.
  assert(copied("  Sokol\t\t v  Brne  ", 64) == "Sokol v Brne");

  // JSON escapy.
  assert(copied("Kino \\\"Scala\\\"", 64) == "Kino \"Scala\"");
  assert(copied("Plav\\u00e1n\\u00ed", 64) == "Plavání");
}

void testTruncationKeepsUtf8Intact() {
  // Každé á jsou dva bajty. Do kapacity šesti se vejdou dvě a ukončovací nula;
  // třetí se musí zahodit celé, ne rozseknout napůl.
  const std::string result = copied("áááá", 6);
  assert(result == "áá");
  assert(result.size() == 4);

  // Jednobajtový zbytek kapacity nesmí svést k zápisu půlky znaku.
  assert(copied("aáá", 4) == "aá");

  // I ASCII náhrada se musí vejít celá: "ss" za ß je delší než zdroj, takže
  // do sedmi bajtů se vejde "Strass" a koncové e už ne.
  assert(copied("Straße", 7) == "Strass");
  assert(copied("Straße", 8) == "Strasse");

  // Kapacita na jediný znak plus nulu.
  assert(copied("Sokol", 2) == "S");
}

}  // namespace

int main() {
  testRealResponse();
  testItemLimit();
  testEmptyAgenda();
  testBrokenPayloads();
  testTitleWithoutText();
  testTransliteration();
  testTruncationKeepsUtf8Intact();
  printf("test_agenda_parser: vsechny kontroly prosly\n");
  return 0;
}
