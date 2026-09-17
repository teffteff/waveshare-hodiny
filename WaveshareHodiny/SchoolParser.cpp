#include "SchoolParser.h"

#include "AgendaParser.h"
#include "JsonScan.h"

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
  feed.homeworkCount = 0;
  feed.homeworkTotal = 0;
  feed.hasMessages = false;
  feed.messageCount = 0;
  feed.messageTotal = 0;
  feed.hasMarks = false;
  feed.markCount = 0;
  feed.markTotal = 0;
  if (payload == nullptr || length == 0) return SchoolParseStatus::NotJson;
  const char *end = payload + length;
  const char *begin = jsonSkipWhitespace(payload, end);
  if (begin >= end || *begin != '{') return SchoolParseStatus::NotJson;

  const JsonValue days = jsonFindMember(begin, end, "days");
  if (!days.isArray()) return SchoolParseStatus::MissingArray;

  copyMember(begin, end, "student", feed.student, sizeof(feed.student));

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
      ++feed.noticeCount;
    }
    feed.noticeTotal = readTotal("noticeCount", feed.noticeCount);
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
