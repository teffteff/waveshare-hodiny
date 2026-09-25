#pragma once

#include <stddef.h>
#include <stdint.h>

// Rozbor rozvrhu a úkolů z vlastního serveru (infra/school), oddělený od
// stahování, aby šel testovat na počítači - stejně jako AgendaParser vedle
// AgendaService.
//
// Odpověď /school.json má tvar
//   {"generated":"2026-09-15T14:05:00+02:00","student":"Adam",
//    "days":[{"day":"DNES","weekday":"ÚTERÝ","today":true,"end":"12:20",
//             "lessons":[{"hour":"1.","start":"08:00","end":"08:45",
//                         "subject":"Matematika","abbrev":"M",
//                         "note":"","state":0}]},
//            {"day":"ZÍTRA", ...}],
//    "homework":[{"due":"ZÍTRA","subject":"Matematika","abbrev":"M",
//                 "title":"Pracovní sešit str. 12"}],
//    "messageCount":2,
//    "messages":[{"when":"VČERA","sender":"Nováková","title":"Třídní schůzky"}],
//    "markCount":1,
//    "marks":[{"when":"DNES","subject":"Matematika","abbrev":"M","mark":"1",
//              "theme":"Násobilka"}],
//    "noticeCount":1,
//    "notices":[{"when":"VČERA","title":"Střevní problémy",
//                "text":"děti v budově mají střevní problémy…"}],
//    "meals":[{"when":"DNES","today":true,"who":"ZŠ",
//              "text":"Kuře na paprice, těstoviny"}],
//    "newsUpdated":"9:05",
//    "timetableUpdated":"VČERA 22:10",
//    "problem":""}
//
// Zprávy, známky a nástěnka (nasems.cz) jsou nepovinné: bez klíčů "messages",
// "marks" i "notices" hodiny druhou stránku obrazovky nenabízejí (starší
// server, nebo je má vypnuté). Stejně nepovinné jsou obědy pod rozvrhem
// ("meals") a úkoly ("homework"): server s vypnutými úkoly klíč neposílá
// a hodiny pak nepíšou ani "ŽÁDNÉ ÚKOLY".
//
// Škola OnLine sama do hodin nikdy nedorazí: přihlášení, výběr dne, suplování
// i popisky termínů řeší server. Firmware jen opisuje hotové řetězce, takže
// změna neoficiálního API Školy OnLine znamená opravu serveru, ne firmwaru.

// Základní škola má nejvýš osm hodin denně, s nultou a odpolední devět.
constexpr size_t SCHOOL_MAX_LESSONS = 10;
// Kolik dnů stojí na kruhu vedle sebe. Server jich posílá tolik, kolik má
// nastaveno; hodiny si nechají první dva.
constexpr size_t SCHOOL_MAX_DAYS = 2;
// Kolik úkolů si hodiny nechají. Na kruh se jich pod rozvrh vejde méně;
// kolik jich server poslal celkem, nese SchoolFeed::homeworkTotal.
constexpr size_t SCHOOL_MAX_HOMEWORK = 8;
// "DNES", "ZÍTRA", "ČT 17.9." - české znaky jsou dvoubajtové.
constexpr size_t SCHOOL_DAY_LENGTH = 24;
constexpr size_t SCHOOL_HOUR_LENGTH = 8;
// "HH:MM" a ukončovací nula.
constexpr size_t SCHOOL_TIME_LENGTH = 6;
// Server předměty ořezává na 60 znaků; displej je stejně nahradí zkratkou,
// když se nevejdou.
constexpr size_t SCHOOL_SUBJECT_LENGTH = 72;
constexpr size_t SCHOOL_SHORT_LENGTH = 24;
constexpr size_t SCHOOL_NOTE_LENGTH = 40;
constexpr size_t SCHOOL_TITLE_LENGTH = 104;
constexpr size_t SCHOOL_STUDENT_LENGTH = 40;
// Druhá stránka: nepřečtené zprávy a nové známky. Server posílá nejvýš šest
// zpráv a osm známek, celkový počet zvlášť.
constexpr size_t SCHOOL_MAX_MESSAGES = 6;
constexpr size_t SCHOOL_MAX_MARKS = 8;
constexpr size_t SCHOOL_SENDER_LENGTH = 32;
constexpr size_t SCHOOL_MARK_LENGTH = 12;
// Oznámení z nástěnky školky; server jich posílá nejvýš šest.
constexpr size_t SCHOOL_MAX_NOTICES = 6;
// Obědy na dva dny pro školu a školku, tedy čtyři řádky; dva navíc pro
// případ, že server přidá další jídelnu.
constexpr size_t SCHOOL_MAX_MEALS = 6;
// Popisek jídelny před jídlem ("ZŠ", "MŠ").
constexpr size_t SCHOOL_MEAL_WHO_LENGTH = 16;
// "9:05", nebo "ČT 24.9. 20:53", když není dnešní.
constexpr size_t SCHOOL_UPDATED_LENGTH = 32;

// Stav hodiny, jak ho posílá server.
enum class SchoolLessonState : uint8_t {
  Normal = 0,
  // Suplování, přesun, školní akce: hodina se koná, ale jinak než obvykle.
  Changed = 1,
  Canceled = 2,
};

struct SchoolLesson {
  char hour[SCHOOL_HOUR_LENGTH] = "";
  char start[SCHOOL_TIME_LENGTH] = "";
  char end[SCHOOL_TIME_LENGTH] = "";
  char subject[SCHOOL_SUBJECT_LENGTH] = "";
  char abbrev[SCHOOL_SHORT_LENGTH] = "";
  char note[SCHOOL_NOTE_LENGTH] = "";
  SchoolLessonState state = SchoolLessonState::Normal;
};

struct SchoolHomework {
  char due[SCHOOL_DAY_LENGTH] = "";
  char subject[SCHOOL_SUBJECT_LENGTH] = "";
  char abbrev[SCHOOL_SHORT_LENGTH] = "";
  char title[SCHOOL_TITLE_LENGTH] = "";
};

// Jen odesílatel a titulek; tělo zprávy server hodinám neposílá.
struct SchoolMessage {
  char when[SCHOOL_DAY_LENGTH] = "";
  char sender[SCHOOL_SENDER_LENGTH] = "";
  char title[SCHOOL_TITLE_LENGTH] = "";
};

struct SchoolMark {
  char when[SCHOOL_DAY_LENGTH] = "";
  char subject[SCHOOL_SUBJECT_LENGTH] = "";
  char abbrev[SCHOOL_SHORT_LENGTH] = "";
  char mark[SCHOOL_MARK_LENGTH] = "";
  char theme[SCHOOL_TITLE_LENGTH] = "";
};

// Titulek a začátek textu oznámení; řádek si displej zkrátí sám.
struct SchoolNotice {
  char when[SCHOOL_DAY_LENGTH] = "";
  char title[SCHOOL_TITLE_LENGTH] = "";
  char text[SCHOOL_TITLE_LENGTH] = "";
};

// Jedno jídlo: den, pro koho a hlavní chod bez alergenů a nápojů.
struct SchoolMeal {
  char when[SCHOOL_DAY_LENGTH] = "";
  // Dnešní oběd. Odpoledne, po hodině nastavené v konfiguraci, ustoupí
  // zítřejšímu; starší server klíč neposílá a pak se nic neschovává.
  bool today = false;
  char who[SCHOOL_MEAL_WHO_LENGTH] = "";
  char text[SCHOOL_TITLE_LENGTH] = "";
};

struct SchoolDay {
  char day[SCHOOL_DAY_LENGTH] = "";
  // Jen u DNES a ZÍTRA, kde samotný popisek den v týdnu neřekne.
  char weekday[SCHOOL_DAY_LENGTH] = "";
  // Rozvrh je dnešní, takže má smysl zvýraznit právě probíhající hodinu.
  bool today = false;
  // Konec poslední hodiny, která se koná; prázdný, když všechno odpadá.
  char end[SCHOOL_TIME_LENGTH] = "";
  size_t lessonCount = 0;
  SchoolLesson lessons[SCHOOL_MAX_LESSONS];
};

struct SchoolFeed {
  char student[SCHOOL_STUDENT_LENGTH] = "";
  // Žádný den znamená, že se v dohledné době neučí (prázdniny).
  size_t dayCount = 0;
  SchoolDay days[SCHOOL_MAX_DAYS];
  // Server úkoly posílá. Bez klíče "homework" je má vypnuté a displej
  // nepíše ani "ŽÁDNÉ ÚKOLY".
  bool hasHomework = false;
  size_t homeworkCount = 0;
  // Všechny úkoly s titulkem v odpovědi, i ty nad SCHOOL_MAX_HOMEWORK. Podle
  // toho rozvržení pozná, že má místo posledního řádku ukázat tečky.
  size_t homeworkTotal = 0;
  SchoolHomework homework[SCHOOL_MAX_HOMEWORK];
  // Server zprávy stahuje; prázdný seznam pak znamená "nic nového".
  bool hasMessages = false;
  size_t messageCount = 0;
  // Nepřečtené zprávy celkem, i ty nad SCHOOL_MAX_MESSAGES.
  size_t messageTotal = 0;
  SchoolMessage messages[SCHOOL_MAX_MESSAGES];
  bool hasMarks = false;
  size_t markCount = 0;
  size_t markTotal = 0;
  SchoolMark marks[SCHOOL_MAX_MARKS];
  bool hasNotices = false;
  size_t noticeCount = 0;
  size_t noticeTotal = 0;
  SchoolNotice notices[SCHOOL_MAX_NOTICES];
  // Kdy server naposled stáhl zprávy, známky a nástěnku (nejstarší z nich).
  // Prázdné u staršího serveru.
  char newsUpdated[SCHOOL_UPDATED_LENGTH] = "";
  // Totéž pro první stránku: rozvrh a obědy, nejstarší z nich.
  char timetableUpdated[SCHOOL_UPDATED_LENGTH] = "";
  // Server obědy stahuje; prázdné pole je víkend nebo den bez jídelníčku.
  bool hasMeals = false;
  size_t mealCount = 0;
  SchoolMeal meals[SCHOOL_MAX_MEALS];
};

enum class SchoolParseStatus : uint8_t {
  // Rozebráno, i když je rozvrh prázdný - o prázdninách je to správná odpověď.
  Ok = 0,
  // Odpověď vůbec nezačíná objektem - chybová stránka proxy, zbytek chunků.
  NotJson = 1,
  // Platný JSON, ale bez pole "days".
  MissingArray = 2,
};

// Rozebere odpověď do feedu. Hodina bez předmětu a úkol bez titulku se
// přeskočí; stav hodiny mimo známé hodnoty se čte jako změna, aby nový stav
// na serveru nevypadal jako obyčejná hodina.
SchoolParseStatus schoolParseFeed(const char *payload, size_t length,
                                  SchoolFeed &feed);

// "HH:MM" nebo "H:MM" na minuty dne; -1 u čehokoli jiného.
int schoolClockMinutes(const char *text);

// Čas hodiny v minutách dne. Obrazovka si drží jen tohle, ne celé hodiny,
// aby zvýraznění nestálo kilobajty statické paměti.
struct SchoolLessonTime {
  int16_t start = -1;
  int16_t end = -1;
  bool canceled = false;
};

SchoolLessonTime schoolLessonTime(const SchoolLesson &lesson);

// Index hodiny, kterou má obrazovka zvýraznit v minutě dne `minuteOfDay`:
// právě probíhající, nebo nejbližší příští. Odpadlé hodiny se přeskakují,
// hodina bez konce trvá 45 minut. Vrací -1, když už je po vyučování nebo čas
// nejde přečíst.
int schoolCurrentLesson(const SchoolLessonTime *lessons, size_t count,
                        int minuteOfDay);
