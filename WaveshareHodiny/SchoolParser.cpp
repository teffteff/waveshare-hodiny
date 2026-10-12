#include "SchoolParser.h"

#include "AgendaParser.h"
#include "JsonScan.h"

#include <stdio.h>
#include <string.h>

namespace {

// Text z JSONu přes stejný převod jako agenda: escapy, UTF-8 a znaky, které
// písmo displeje nemá. Vrací délku zapsaného textu.
size_t copyMember(const char *objectBegin, const char *objectEnd,
                  const char *key, char *destination, size_t capacity) {
  const JsonValue value = jsonFindMember(objectBegin, objectEnd, key);
  if (!value.isString) {
    if (capacity != 0) destination[0] = '\0';
    return 0;
  }
  return agendaCopyText(
      value.contentBegin(),
      static_cast<size_t>(value.contentEnd() - value.contentBegin()),
      destination, capacity);
}

// "cal" u zprávy nebo oznámení: chybí, když s ním kalendář nic nedělal;
// neznámá hodnota (novější server) se bere jako žádná.
SchoolCalendarMark readCalendarMark(const char *objectBegin,
                                    const char *objectEnd) {
  char calendar[8] = "";
  copyMember(objectBegin, objectEnd, "cal", calendar, sizeof(calendar));
  if (strcmp(calendar, "added") == 0) return SchoolCalendarMark::Added;
  if (strcmp(calendar, "review") == 0) return SchoolCalendarMark::Review;
  return SchoolCalendarMark::None;
}

}  // namespace

int schoolClockMinutes(const char *text) {
  if (text == nullptr) return -1;
  int hours = 0;
  int digits = 0;
  const char *cursor = text;
  while (*cursor >= '0' && *cursor <= '9' && digits < 2) {
    hours = hours * 10 + (*cursor - '0');
    ++cursor;
    ++digits;
  }
  if (digits == 0 || *cursor != ':') return -1;
  ++cursor;
  if (cursor[0] < '0' || cursor[0] > '9' || cursor[1] < '0' || cursor[1] > '9')
    return -1;
  const int minutes = (cursor[0] - '0') * 10 + (cursor[1] - '0');
  if (hours > 23 || minutes > 59) return -1;
  return hours * 60 + minutes;
}

SchoolParseStatus schoolParseFeed(const char *payload, size_t length,
                                  SchoolFeed &feed) {
  feed.student[0] = '\0';
  feed.dayCount = 0;
  feed.hasHomework = false;
  feed.homeworkCount = 0;
  feed.homeworkTotal = 0;
  feed.hasMessages = false;
  feed.messageCount = 0;
  feed.messageTotal = 0;
  feed.hasMarks = false;
  feed.markCount = 0;
  feed.markTotal = 0;
  feed.hasTests = false;
  feed.testCount = 0;
  feed.testTotal = 0;
  feed.hasNotices = false;
  feed.noticeCount = 0;
  feed.noticeTotal = 0;
  feed.hasFamily = false;
  feed.familyCount = 0;
  feed.familyTotal = 0;
  feed.newsUpdated[0] = '\0';
  feed.timetableUpdated[0] = '\0';
  feed.hasMeals = false;
  feed.mealCount = 0;
  if (payload == nullptr || length == 0) return SchoolParseStatus::NotJson;
  const char *end = payload + length;
  const char *begin = jsonSkipWhitespace(payload, end);
  if (begin >= end || *begin != '{') return SchoolParseStatus::NotJson;

  const JsonValue days = jsonFindMember(begin, end, "days");
  if (!days.isArray()) return SchoolParseStatus::MissingArray;

  copyMember(begin, end, "student", feed.student, sizeof(feed.student));
  copyMember(begin, end, "newsUpdated", feed.newsUpdated,
             sizeof(feed.newsUpdated));
  copyMember(begin, end, "timetableUpdated", feed.timetableUpdated,
             sizeof(feed.timetableUpdated));

  JsonArrayCursor dayCursor = jsonOpenArray(days);
  while (feed.dayCount < SCHOOL_MAX_DAYS && jsonNextItem(dayCursor)) {
    const char *dayBegin = dayCursor.itemBegin;
    const char *dayEnd = dayCursor.itemEnd;
    const JsonValue lessons = jsonFindMember(dayBegin, dayEnd, "lessons");
    if (!lessons.isArray()) continue;
    SchoolDay &day = feed.days[feed.dayCount];
    copyMember(dayBegin, dayEnd, "day", day.day, sizeof(day.day));
    // Den bez popisku by na displeji neměl hlavičku; server takový neposílá.
    if (day.day[0] == '\0') continue;
    copyMember(dayBegin, dayEnd, "weekday", day.weekday, sizeof(day.weekday));
    copyMember(dayBegin, dayEnd, "end", day.end, sizeof(day.end));
    day.today = jsonReadBoolMember(dayBegin, dayEnd, "today");
    day.lessonCount = 0;

    JsonArrayCursor cursor = jsonOpenArray(lessons);
    while (day.lessonCount < SCHOOL_MAX_LESSONS && jsonNextItem(cursor)) {
      SchoolLesson &lesson = day.lessons[day.lessonCount];
      lesson = SchoolLesson{};
      const char *itemBegin = cursor.itemBegin;
      const char *itemEnd = cursor.itemEnd;
      if (copyMember(itemBegin, itemEnd, "subject", lesson.subject,
                     sizeof(lesson.subject)) == 0 &&
          copyMember(itemBegin, itemEnd, "abbrev", lesson.subject,
                     sizeof(lesson.subject)) == 0) {
        continue;
      }
      copyMember(itemBegin, itemEnd, "hour", lesson.hour, sizeof(lesson.hour));
      copyMember(itemBegin, itemEnd, "start", lesson.start,
                 sizeof(lesson.start));
      copyMember(itemBegin, itemEnd, "end", lesson.end, sizeof(lesson.end));
      copyMember(itemBegin, itemEnd, "abbrev", lesson.abbrev,
                 sizeof(lesson.abbrev));
      copyMember(itemBegin, itemEnd, "note", lesson.note, sizeof(lesson.note));
      float state = 0.0f;
      if (jsonReadNumberMember(itemBegin, itemEnd, "state", state)) {
        if (state == 0.0f) lesson.state = SchoolLessonState::Normal;
        else if (state == 2.0f) lesson.state = SchoolLessonState::Canceled;
        else lesson.state = SchoolLessonState::Changed;
      }
      ++day.lessonCount;
    }
    ++feed.dayCount;
  }

  // Chybějící úkoly nejsou chyba: server je s vypnutými úkoly posílat nemusí.
  const JsonValue homework = jsonFindMember(begin, end, "homework");
  if (homework.isArray()) {
    feed.hasHomework = true;
    JsonArrayCursor items = jsonOpenArray(homework);
    while (jsonNextItem(items)) {
      if (feed.homeworkCount >= SCHOOL_MAX_HOMEWORK) {
        // Úkoly nad strop se jen spočítají.
        const JsonValue title =
            jsonFindMember(items.itemBegin, items.itemEnd, "title");
        if (title.isString && title.contentEnd() > title.contentBegin())
          ++feed.homeworkTotal;
        continue;
      }
      SchoolHomework &item = feed.homework[feed.homeworkCount];
      item = SchoolHomework{};
      if (copyMember(items.itemBegin, items.itemEnd, "title", item.title,
                     sizeof(item.title)) == 0) {
        continue;
      }
      copyMember(items.itemBegin, items.itemEnd, "due", item.due,
                 sizeof(item.due));
      copyMember(items.itemBegin, items.itemEnd, "subject", item.subject,
                 sizeof(item.subject));
      copyMember(items.itemBegin, items.itemEnd, "abbrev", item.abbrev,
                 sizeof(item.abbrev));
      ++feed.homeworkCount;
      ++feed.homeworkTotal;
    }
  }
  // Celkový počet posílá server zvlášť; bez něj stačí, co přišlo v poli.
  const auto readTotal = [&](const char *key, size_t parsed) {
    float total = 0.0f;
    if (!jsonReadNumberMember(begin, end, key, total) || !(total >= 0.0f))
      return parsed;
    const size_t declared =
        total > 9999.0f ? 9999 : static_cast<size_t>(total);
    return declared > parsed ? declared : parsed;
  };

  const JsonValue messages = jsonFindMember(begin, end, "messages");
  if (messages.isArray()) {
    feed.hasMessages = true;
    JsonArrayCursor items = jsonOpenArray(messages);
    while (feed.messageCount < SCHOOL_MAX_MESSAGES && jsonNextItem(items)) {
      SchoolMessage &message = feed.messages[feed.messageCount];
      message = SchoolMessage{};
      if (copyMember(items.itemBegin, items.itemEnd, "title", message.title,
                     sizeof(message.title)) == 0) {
        continue;
      }
      copyMember(items.itemBegin, items.itemEnd, "when", message.when,
                 sizeof(message.when));
      copyMember(items.itemBegin, items.itemEnd, "sender", message.sender,
                 sizeof(message.sender));
      message.calendar = readCalendarMark(items.itemBegin, items.itemEnd);
      ++feed.messageCount;
    }
    feed.messageTotal = readTotal("messageCount", feed.messageCount);
  }

  const JsonValue marks = jsonFindMember(begin, end, "marks");
  if (marks.isArray()) {
    feed.hasMarks = true;
    JsonArrayCursor items = jsonOpenArray(marks);
    while (feed.markCount < SCHOOL_MAX_MARKS && jsonNextItem(items)) {
      SchoolMark &mark = feed.marks[feed.markCount];
      mark = SchoolMark{};
      if (copyMember(items.itemBegin, items.itemEnd, "mark", mark.mark,
                     sizeof(mark.mark)) == 0) {
        continue;
      }
      copyMember(items.itemBegin, items.itemEnd, "when", mark.when,
                 sizeof(mark.when));
      copyMember(items.itemBegin, items.itemEnd, "subject", mark.subject,
                 sizeof(mark.subject));
      copyMember(items.itemBegin, items.itemEnd, "abbrev", mark.abbrev,
                 sizeof(mark.abbrev));
      copyMember(items.itemBegin, items.itemEnd, "theme", mark.theme,
                 sizeof(mark.theme));
      ++feed.markCount;
    }
    feed.markTotal = readTotal("markCount", feed.markCount);
  }

  const JsonValue tests = jsonFindMember(begin, end, "tests");
  if (tests.isArray()) {
    feed.hasTests = true;
    JsonArrayCursor items = jsonOpenArray(tests);
    while (feed.testCount < SCHOOL_MAX_TESTS && jsonNextItem(items)) {
      SchoolTest &test = feed.tests[feed.testCount];
      test = SchoolTest{};
      if (copyMember(items.itemBegin, items.itemEnd, "when", test.when,
                     sizeof(test.when)) == 0) {
        continue;
      }
      copyMember(items.itemBegin, items.itemEnd, "subject", test.subject,
                 sizeof(test.subject));
      copyMember(items.itemBegin, items.itemEnd, "abbrev", test.abbrev,
                 sizeof(test.abbrev));
      copyMember(items.itemBegin, items.itemEnd, "theme", test.theme,
                 sizeof(test.theme));
      test.done = jsonReadBoolMember(items.itemBegin, items.itemEnd, "done");
      test.calendar = readCalendarMark(items.itemBegin, items.itemEnd);
      ++feed.testCount;
    }
    // testCount od serveru počítá jen nadcházející; když chybí, platí počet
    // nenapsaných v poli.
    size_t upcoming = 0;
    for (size_t index = 0; index < feed.testCount; ++index)
      if (!feed.tests[index].done) ++upcoming;
    float total = 0.0f;
    feed.testTotal =
        jsonReadNumberMember(begin, end, "testCount", total) && total >= 0.0f
            ? (total > 9999.0f ? 9999 : static_cast<size_t>(total))
            : upcoming;
  }

  const JsonValue notices = jsonFindMember(begin, end, "notices");
  if (notices.isArray()) {
    feed.hasNotices = true;
    JsonArrayCursor items = jsonOpenArray(notices);
    while (feed.noticeCount < SCHOOL_MAX_NOTICES && jsonNextItem(items)) {
      SchoolNotice &notice = feed.notices[feed.noticeCount];
      notice = SchoolNotice{};
      if (copyMember(items.itemBegin, items.itemEnd, "title", notice.title,
                     sizeof(notice.title)) == 0) {
        continue;
      }
      copyMember(items.itemBegin, items.itemEnd, "when", notice.when,
                 sizeof(notice.when));
      copyMember(items.itemBegin, items.itemEnd, "text", notice.text,
                 sizeof(notice.text));
      notice.calendar = readCalendarMark(items.itemBegin, items.itemEnd);
      ++feed.noticeCount;
    }
    feed.noticeTotal = readTotal("noticeCount", feed.noticeCount);
  }

  const JsonValue family = jsonFindMember(begin, end, "family");
  if (family.isArray()) {
    feed.hasFamily = true;
    JsonArrayCursor items = jsonOpenArray(family);
    while (feed.familyCount < SCHOOL_MAX_FAMILY && jsonNextItem(items)) {
      SchoolNotice &line = feed.family[feed.familyCount];
      line = SchoolNotice{};
      if (copyMember(items.itemBegin, items.itemEnd, "title", line.title,
                     sizeof(line.title)) == 0) {
        continue;
      }
      copyMember(items.itemBegin, items.itemEnd, "when", line.when,
                 sizeof(line.when));
      copyMember(items.itemBegin, items.itemEnd, "text", line.text,
                 sizeof(line.text));
      ++feed.familyCount;
    }
    feed.familyTotal = readTotal("familyCount", feed.familyCount);
  }

  const JsonValue meals = jsonFindMember(begin, end, "meals");
  if (meals.isArray()) {
    feed.hasMeals = true;
    JsonArrayCursor items = jsonOpenArray(meals);
    while (feed.mealCount < SCHOOL_MAX_MEALS && jsonNextItem(items)) {
      SchoolMeal &meal = feed.meals[feed.mealCount];
      meal = SchoolMeal{};
      if (copyMember(items.itemBegin, items.itemEnd, "text", meal.text,
                     sizeof(meal.text)) == 0) {
        continue;
      }
      copyMember(items.itemBegin, items.itemEnd, "when", meal.when,
                 sizeof(meal.when));
      copyMember(items.itemBegin, items.itemEnd, "who", meal.who,
                 sizeof(meal.who));
      meal.today = jsonReadBoolMember(items.itemBegin, items.itemEnd, "today");
      ++feed.mealCount;
    }
  }
  return SchoolParseStatus::Ok;
}

SchoolLessonTime schoolLessonTime(const SchoolLesson &lesson) {
  SchoolLessonTime time;
  time.start = static_cast<int16_t>(schoolClockMinutes(lesson.start));
  time.end = static_cast<int16_t>(schoolClockMinutes(lesson.end));
  time.canceled = lesson.state == SchoolLessonState::Canceled;
  return time;
}

int schoolCurrentLesson(const SchoolLessonTime *lessons, size_t count,
                        int minuteOfDay) {
  if (lessons == nullptr || minuteOfDay < 0) return -1;
  for (size_t index = 0; index < count; ++index) {
    const SchoolLessonTime &lesson = lessons[index];
    if (lesson.canceled) continue;
    const int start = lesson.start;
    int end = lesson.end;
    if (start < 0) continue;
    if (end <= start) end = start + 45;
    // První hodina, která ještě neskončila: buď právě běží, nebo je příští.
    if (minuteOfDay < end) return static_cast<int>(index);
  }
  return -1;
}

void schoolRequestUrl(const char *url, size_t days, char *output,
                      size_t capacity) {
  if (output == nullptr || capacity == 0) return;
  if (url == nullptr) url = "";
  strlcpy(output, url, capacity);
  const size_t length = strlen(url);
  if (days == 0 || length == 0) return;
  const char *query = strchr(url, '?');
  // Majitel si počet dnů mohl dát do adresy sám; ten má přednost.
  if (query != nullptr) {
    for (const char *cursor = query; cursor != nullptr;
         cursor = strchr(cursor + 1, '&')) {
      if (strncmp(cursor + 1, "days=", 5) == 0) return;
    }
  }
  const char *separator = query == nullptr                 ? "?"
                          : url[length - 1] == '?' ||
                                  url[length - 1] == '&' ? ""
                                                         : "&";
  char parameter[16];
  snprintf(parameter, sizeof(parameter), "%sdays=%u", separator,
           static_cast<unsigned>(days));
  if (length + strlen(parameter) + 1 > capacity) return;
  strlcat(output, parameter, capacity);
}

namespace {

const char *const WEEKDAY_NAMES[7] = {"PONDĚLÍ", "ÚTERÝ",  "STŘEDA", "ČTVRTEK",
                                      "PÁTEK",   "SOBOTA", "NEDĚLE"};
const char *const WEEKDAY_SHORT[7] = {"PO", "ÚT", "ST", "ČT", "PÁ", "SO", "NE"};

}  // namespace

int schoolDayWeekday(const SchoolDay &day, int todayWeekday) {
  for (int index = 0; index < 7; ++index)
    if (strcmp(day.weekday, WEEKDAY_NAMES[index]) == 0) return index;
  for (int index = 0; index < 7; ++index) {
    const size_t length = strlen(WEEKDAY_SHORT[index]);
    if (strncmp(day.day, WEEKDAY_SHORT[index], length) == 0 &&
        (day.day[length] == ' ' || day.day[length] == '\0'))
      return index;
  }
  if (todayWeekday < 0 || todayWeekday > 6) return -1;
  if (strcmp(day.day, "DNES") == 0) return todayWeekday;
  if (strcmp(day.day, "ZÍTRA") == 0) return (todayWeekday + 1) % 7;
  return -1;
}

int schoolLessonNumber(const char *hour) {
  if (hour == nullptr) return -1;
  int number = 0;
  int digits = 0;
  while (*hour >= '0' && *hour <= '9' && digits < 2) {
    number = number * 10 + (*hour - '0');
    ++hour;
    ++digits;
  }
  if (digits == 0 || (*hour != '.' && *hour != '\0')) return -1;
  return number;
}

void schoolBuildWeekGrid(const SchoolFeed &feed, int todayWeekday,
                         SchoolWeekGrid &grid) {
  grid = SchoolWeekGrid{};
  for (size_t day = 0; day < SCHOOL_MAX_DAYS; ++day)
    for (size_t index = 0; index < SCHOOL_MAX_LESSONS; ++index)
      grid.row[day][index] = -1;
  const size_t dayCount =
      feed.dayCount < SCHOOL_MAX_DAYS ? feed.dayCount : SCHOOL_MAX_DAYS;
  int firstColumn = -1;
  for (size_t day = 0; day < dayCount; ++day) {
    const int weekday = schoolDayWeekday(feed.days[day], todayWeekday);
    if (weekday < 0 || weekday >= static_cast<int>(SCHOOL_WEEK_COLUMNS) ||
        grid.day[weekday] >= 0)
      continue;
    grid.day[weekday] = static_cast<int8_t>(day);
    if (firstColumn < 0) firstColumn = weekday;
  }
  for (int column = 0; column < firstColumn; ++column)
    grid.nextWeek[column] = grid.day[column] >= 0;

  // Řádky od nejnižšího čísla hodiny v týdnu: den s nultou hodinou přidá
  // řádek nahoru všem, ostatní hodiny zůstanou vedle sebe.
  int lowest = -1;
  int highest = -1;
  size_t longest = 0;
  for (size_t column = 0; column < SCHOOL_WEEK_COLUMNS; ++column) {
    if (grid.day[column] < 0) continue;
    const SchoolDay &day = feed.days[grid.day[column]];
    if (day.lessonCount > longest) longest = day.lessonCount;
    for (size_t index = 0; index < day.lessonCount; ++index) {
      const int number = schoolLessonNumber(day.lessons[index].hour);
      if (number < 0) continue;
      if (lowest < 0 || number < lowest) lowest = number;
      if (number > highest) highest = number;
    }
  }
  size_t rows = lowest >= 0 ? static_cast<size_t>(highest - lowest + 1) : 0;
  if (rows < longest) rows = longest;
  if (rows > SCHOOL_MAX_LESSONS) rows = SCHOOL_MAX_LESSONS;
  grid.rows = static_cast<uint8_t>(rows);
  grid.firstHour = static_cast<int8_t>(lowest);

  for (size_t column = 0; column < SCHOOL_WEEK_COLUMNS; ++column) {
    if (grid.day[column] < 0) continue;
    const size_t dayIndex = static_cast<size_t>(grid.day[column]);
    const SchoolDay &day = feed.days[dayIndex];
    bool taken[SCHOOL_MAX_LESSONS] = {};
    // Nejdřív hodiny s číslem na svůj řádek...
    for (size_t index = 0; index < day.lessonCount; ++index) {
      const int number = schoolLessonNumber(day.lessons[index].hour);
      if (number < 0 || lowest < 0) continue;
      const int row = number - lowest;
      if (row >= static_cast<int>(rows) || taken[row]) continue;
      taken[row] = true;
      grid.row[dayIndex][index] = static_cast<int8_t>(row);
    }
    // ...pak ty bez čísla nebo na obsazeném řádku na nejbližší volný za
    // předchozí hodinou dne, ať drží pořadí. Kde volný není, hodina se
    // nekreslí.
    int previous = -1;
    for (size_t index = 0; index < day.lessonCount; ++index) {
      if (grid.row[dayIndex][index] >= 0) {
        previous = grid.row[dayIndex][index];
        continue;
      }
      for (int row = previous + 1; row < static_cast<int>(rows); ++row) {
        if (taken[row]) continue;
        taken[row] = true;
        grid.row[dayIndex][index] = static_cast<int8_t>(row);
        previous = row;
        break;
      }
    }
  }
}
