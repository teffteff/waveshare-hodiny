#include <cassert>
#include <cstring>
#include <string>

#include "SchoolLayout.h"
#include "SchoolParser.h"

namespace {

// Odpověď ve tvaru, jaký skládá infra/school/feed.py (viz test_feed.py).
const char *const RESPONSE =
    "{\"generated\":\"2026-09-15T12:00:00+02:00\",\"student\":\"Adam\","
    "\"class\":\"III.B\",\"days\":["
    "{\"day\":\"DNES\",\"weekday\":\"ÚTERÝ\",\"date\":\"2026-09-15\","
    "\"today\":true,\"end\":\"12:30\",\"lessons\":["
    "{\"hour\":\"1.\",\"start\":\"08:00\",\"end\":\"08:45\","
    "\"subject\":\"Anglický jazyk\",\"abbrev\":\"Aj\","
    "\"note\":\"Suplování\",\"state\":1},"
    "{\"hour\":\"2.\",\"start\":\"08:55\",\"end\":\"09:40\","
    "\"subject\":\"Hudební výchova\",\"abbrev\":\"Hv\","
    "\"note\":\"Odpadlá hodina\",\"state\":2},"
    "{\"hour\":\"3.\",\"start\":\"10:00\",\"end\":\"11:35\","
    "\"subject\":\"\",\"abbrev\":\"\",\"note\":\"\",\"state\":0},"
    "{\"hour\":\"4.\",\"start\":\"11:45\",\"end\":\"12:30\","
    "\"subject\":\"Tělesná výchova \\u2013 hřiště\",\"abbrev\":\"Tv\","
    "\"note\":\"\",\"state\":7}]},"
    "{\"day\":\"ZÍTRA\",\"weekday\":\"STŘEDA\",\"date\":\"2026-09-16\","
    "\"today\":false,\"end\":\"08:45\",\"lessons\":["
    "{\"hour\":\"1.\",\"start\":\"08:00\",\"end\":\"08:45\","
    "\"subject\":\"Matematika\",\"abbrev\":\"M\",\"state\":0}]},"
    "{\"day\":\"PÁ 18.9.\",\"lessons\":[]}"
    "],\"homework\":["
    "{\"due\":\"DNES\",\"date\":\"2026-09-15\",\"subject\":\"Český jazyk\","
    "\"abbrev\":\"Čj\",\"title\":\"Přečíst kapitolu o Praze…\"},"
    "{\"due\":\"ZÍTRA\",\"subject\":\"Matematika\",\"abbrev\":\"M\",\"title\":\"\"}"
    "],\"messageCount\":9,\"messages\":["
    "{\"when\":\"VČERA\",\"sender\":\"Nováková\",\"title\":\"Třídní schůzky\"},"
    "{\"when\":\"DNES\",\"sender\":\"Malá\",\"title\":\"\"}"
    "],\"markCount\":1,\"marks\":["
    "{\"when\":\"DNES\",\"subject\":\"Matematika\",\"abbrev\":\"M\","
    "\"mark\":\"1\",\"theme\":\"Násobilka\"}"
    "],\"problem\":\"\"}";

void testResponse() {
  SchoolFeed feed;
  assert(schoolParseFeed(RESPONSE, strlen(RESPONSE), feed) ==
         SchoolParseStatus::Ok);
  assert(std::string(feed.student) == "Adam");
  // Třetí den se na kruh nevejde a hodiny ho zahodí.
  assert(feed.dayCount == 2);

  const SchoolDay &today = feed.days[0];
  assert(std::string(today.day) == "DNES");
  assert(std::string(today.weekday) == "ÚTERÝ");
  assert(std::string(today.end) == "12:30");
  assert(today.today);
  // Hodina bez předmětu se přeskočí, zbytek zůstane v pořadí.
  assert(today.lessonCount == 3);
  const SchoolLesson &english = today.lessons[0];
  assert(std::string(english.hour) == "1.");
  assert(std::string(english.start) == "08:00");
  assert(std::string(english.end) == "08:45");
  assert(std::string(english.subject) == "Anglický jazyk");
  assert(std::string(english.abbrev) == "Aj");
  assert(english.state == SchoolLessonState::Changed);
  assert(today.lessons[1].state == SchoolLessonState::Canceled);
  // Pomlčku písmo nemá, přepíše se; neznámý stav se čte jako změna.
  assert(std::string(today.lessons[2].subject) == "Tělesná výchova - hřiště");
  assert(today.lessons[2].state == SchoolLessonState::Changed);

  const SchoolDay &tomorrow = feed.days[1];
  assert(std::string(tomorrow.day) == "ZÍTRA" && !tomorrow.today);
  assert(tomorrow.lessonCount == 1);
  assert(std::string(tomorrow.lessons[0].subject) == "Matematika");

  // Úkol bez titulku se přeskočí; vypustka se přepíše na tři tečky.
  assert(feed.homeworkCount == 1 && feed.homeworkTotal == 1);
  assert(std::string(feed.homework[0].due) == "DNES");
  assert(std::string(feed.homework[0].abbrev) == "Čj");
  assert(std::string(feed.homework[0].title) == "Přečíst kapitolu o Praze...");

  // Zpráva bez titulku se přeskočí, celkový počet ale platí ze serveru.
  assert(feed.hasMessages && feed.messageCount == 1 && feed.messageTotal == 9);
  assert(std::string(feed.messages[0].when) == "VČERA");
  assert(std::string(feed.messages[0].sender) == "Nováková");
  assert(std::string(feed.messages[0].title) == "Třídní schůzky");
  assert(feed.hasMarks && feed.markCount == 1 && feed.markTotal == 1);
  assert(std::string(feed.marks[0].abbrev) == "M");
  assert(std::string(feed.marks[0].mark) == "1");
  assert(std::string(feed.marks[0].theme) == "Násobilka");
}

void testBrokenResponses() {
  SchoolFeed feed;
  feed.dayCount = 5;
  assert(schoolParseFeed("<html>", 6, feed) == SchoolParseStatus::NotJson);
  assert(feed.dayCount == 0);
  // Starý tvar s jedním dnem na nejvyšší úrovni už se nečte.
  const char *oldShape = "{\"day\":\"DNES\",\"lessons\":[]}";
  assert(schoolParseFeed(oldShape, strlen(oldShape), feed) ==
         SchoolParseStatus::MissingArray);
  // Prázdniny: platná odpověď bez dnů, úkoly chybět smí.
  const char *holidays = "{\"days\":[]}";
  assert(schoolParseFeed(holidays, strlen(holidays), feed) ==
         SchoolParseStatus::Ok);
  assert(feed.dayCount == 0 && feed.homeworkCount == 0);
  // Starší server bez zpráv a známek: druhá stránka se nenabízí.
  assert(!feed.hasMessages && !feed.hasMarks);
  const char *empty = "{\"days\":[],\"messages\":[],\"marks\":[]}";
  assert(schoolParseFeed(empty, strlen(empty), feed) == SchoolParseStatus::Ok);
  assert(feed.hasMessages && feed.messageTotal == 0 && feed.hasMarks);
  // Den bez popisku nebo bez hodin se přeskočí.
  const char *odd = "{\"days\":[{\"day\":\"\",\"lessons\":[]},{\"day\":\"DNES\"}]}";
  assert(schoolParseFeed(odd, strlen(odd), feed) == SchoolParseStatus::Ok);
  assert(feed.dayCount == 0);
  // Useknutá odpověď nesmí číst za konec.
  std::string cut(RESPONSE, strlen(RESPONSE) / 2);
  schoolParseFeed(cut.c_str(), cut.size(), feed);
  assert(feed.dayCount <= SCHOOL_MAX_DAYS);
}

void testHomeworkOverCapIsCounted() {
  std::string payload = "{\"days\":[],\"homework\":[";
  for (int index = 0; index < 11; ++index) {
    if (index > 0) payload += ',';
    payload += "{\"due\":\"ZÍTRA\",\"title\":\"Úkol\"}";
  }
  // Úkol bez titulku se nepočítá ani nad stropem.
  payload += ",{\"due\":\"ZÍTRA\",\"title\":\"\"}]}";
  SchoolFeed feed;
  assert(schoolParseFeed(payload.c_str(), payload.size(), feed) ==
         SchoolParseStatus::Ok);
  assert(feed.homeworkCount == SCHOOL_MAX_HOMEWORK);
  assert(feed.homeworkTotal == 11);
}

void testCurrentLesson() {
  SchoolLesson lessons[3];
  strcpy(lessons[0].start, "08:00");
  strcpy(lessons[0].end, "08:45");
  strcpy(lessons[1].start, "08:55");
  strcpy(lessons[1].end, "09:40");
  lessons[1].state = SchoolLessonState::Canceled;
  strcpy(lessons[2].start, "10:00");
  strcpy(lessons[2].end, "");
  SchoolLessonTime times[3];
  for (size_t index = 0; index < 3; ++index)
    times[index] = schoolLessonTime(lessons[index]);
  assert(times[0].start == 480 && times[0].end == 525 && !times[0].canceled);
  assert(times[1].canceled && times[2].end == -1);
  assert(schoolClockMinutes("7:05") == 425);
  assert(schoolClockMinutes("24:00") == -1 && schoolClockMinutes("8:5") == -1);
  assert(schoolCurrentLesson(times, 3, 7 * 60) == 0);
  assert(schoolCurrentLesson(times, 3, 8 * 60 + 44) == 0);
  // Přestávka před odpadlou hodinou ukazuje na tu, která se opravdu koná.
  assert(schoolCurrentLesson(times, 3, 8 * 60 + 45) == 2);
  // Bez konce se počítá se čtyřiceti pěti minutami.
  assert(schoolCurrentLesson(times, 3, 10 * 60 + 44) == 2);
  assert(schoolCurrentLesson(times, 3, 10 * 60 + 45) == -1);
  assert(schoolCurrentLesson(times, 3, -1) == -1);
}

// Řádek 20 + 2 px, hlavička 17 + 2 px, mezera nad úkoly 7 px.
constexpr int LINE = 20, HEADING = 17, GAP = 2, SECTION = 7;

SchoolLayoutResult layout(uint8_t lessons, uint8_t homework, int height,
                          uint8_t total = 0, bool emptyNotice = false) {
  return schoolLayout(lessons, homework, total > homework ? total : homework,
                      emptyNotice,
                      SchoolLayoutMetrics{LINE, HEADING, GAP, SECTION, height});
}

void testLayout() {
  // Hlavička 19 + 6 hodin * 22 = 151; úkoly 7 + 19 + 3 * 22 = 92 -> 243.
  SchoolLayoutResult all = layout(6, 3, 243);
  assert(all.lessons == 6 && all.homework == 3 && !all.homeworkEllipsis);

  // O pixel méně: poslední úkol nahradí tečky.
  SchoolLayoutResult cut = layout(6, 3, 242);
  assert(cut.lessons == 6 && cut.homework == 2 && cut.homeworkEllipsis);

  // Rozvrh má přednost; na úkoly nezbude ani řádek, takže se nekreslí vůbec.
  SchoolLayoutResult busy = layout(8, 4, 19 + 8 * 22 + 7 + 19 + 21);
  assert(busy.lessons == 8 && busy.homework == 0 && !busy.homeworkEllipsis);

  // Jediný řádek s tečkami se ukáže, když úkolů je víc.
  SchoolLayoutResult one = layout(8, 4, 19 + 8 * 22 + 7 + 19 + 22);
  assert(one.homework == 1 && one.homeworkEllipsis);

  // Moc hodin na malý pás: vejde se, kolik se vejde.
  assert(layout(10, 0, 19 + 5 * 22 + 21).lessons == 5);
  // Prázdniny s úkoly.
  SchoolLayoutResult holidays = layout(0, 2, 200);
  assert(holidays.lessons == 0 && holidays.homework == 2);

  // Uložených osm se vejde, ale server jich poslal dvanáct: poslední řádek
  // nahradí tečky.
  SchoolLayoutResult capped = layout(0, 8, 400, 12);
  assert(capped.homework == 8 && capped.homeworkEllipsis);
  assert(!layout(0, 8, 400, 8).homeworkEllipsis);

  // Žádné úkoly: hlavička se ukáže, když se pod rozvrh vejde (6 hodin = 151,
  // mezera 7 a hlavička 17 -> 175), a jen když o ni obrazovka stojí.
  SchoolLayoutResult none = layout(6, 0, 175, 0, true);
  assert(none.lessons == 6 && none.homework == 0 && none.homeworkEmpty);
  assert(!layout(6, 0, 174, 0, true).homeworkEmpty);
  assert(!layout(6, 0, 400).homeworkEmpty);
  assert(!layout(6, 2, 400, 2, true).homeworkEmpty);
}

SchoolListsResult lists(bool firstOn, uint8_t first, uint8_t firstTotal,
                        bool secondOn, uint8_t second, uint8_t secondTotal,
                        int height) {
  return schoolListsLayout(firstOn, first, firstTotal, secondOn, second,
                           secondTotal,
                           SchoolLayoutMetrics{LINE, HEADING, GAP, SECTION, height});
}

void testListsLayout() {
  // Všechno se vejde: 19 + 2 * 22 + 7 + 19 + 1 * 22 = 111.
  SchoolListsResult all = lists(true, 2, 2, true, 1, 1, 111);
  assert(all.firstHeading && all.first == 2 && !all.firstEllipsis);
  assert(all.secondHeading && all.second == 1 && !all.secondEllipsis);

  // Hodně zpráv nevytlačí známky: ty si drží hlavičku a dva řádky.
  // 345 - (7 + 19 + 44) = 275 na zprávy -> (275 - 19) / 22 = 11 řádků.
  SchoolListsResult busy = lists(true, 6, 20, true, 8, 8, 345);
  assert(busy.first == 6 && busy.firstEllipsis);
  assert(busy.secondHeading && busy.second >= 2 && busy.secondEllipsis);
  SchoolListsResult tight = lists(true, 6, 6, true, 8, 8, 19 + 3 * 22 + 7 + 19 + 2 * 22);
  assert(tight.first == 3 && tight.firstEllipsis && tight.second == 2);

  // Prázdné sekce: jen hlavičky, bez mezery pod nimi.
  SchoolListsResult none = lists(true, 0, 0, true, 0, 0, 17 + 7 + 17);
  assert(none.firstHeading && none.secondHeading && none.first == 0);
  assert(!lists(true, 0, 0, true, 0, 0, 17 + 7 + 16).secondHeading);

  // Vypnutá sekce se vynechá a nic si nerezervuje.
  SchoolListsResult onlyMarks = lists(false, 0, 0, true, 3, 3, 200);
  assert(!onlyMarks.firstHeading && onlyMarks.secondHeading && onlyMarks.second == 3);
  SchoolListsResult onlyMessages = lists(true, 6, 6, false, 0, 0, 19 + 6 * 22);
  assert(onlyMessages.first == 6 && !onlyMessages.secondHeading);
}

}  // namespace

int main() {
  testResponse();
  testBrokenResponses();
  testHomeworkOverCapIsCounted();
  testCurrentLesson();
  testLayout();
  testListsLayout();
  return 0;
}
