#include "AgendaParser.h"

#include <string.h>

#include "JsonScan.h"

namespace {

// Kódové body, které ClockCzechFont*.c opravdu obsahuje. Odpovídá rozsahu
// v hlavičce těch souborů (-r 0x20-0x7E,0xB0,...,0x2082) a všechna čtyři
// písma ho mají stejný. Čeština projde celá, takže "Plavání" zůstane
// "Plavání" - na rozdíl od zpráv, které RssParser přepisuje do ASCII, protože
// tam může přijít text v jakémkoli jazyce.
bool fontHasCodePoint(uint32_t codePoint) {
  if (codePoint >= 0x20 && codePoint <= 0x7E) return true;
  switch (codePoint) {
    case 0x00B0:  // °
    case 0x00B3:  // ³
    case 0x00B5:  // µ
    case 0x00C1: case 0x00E1:  // Á á
    case 0x00C9: case 0x00E9:  // É é
    case 0x00CD: case 0x00ED:  // Í í
    case 0x00D3: case 0x00F3:  // Ó ó
    case 0x00DA: case 0x00FA:  // Ú ú
    case 0x00DD: case 0x00FD:  // Ý ý
    case 0x010C: case 0x010D:  // Č č
    case 0x010E: case 0x010F:  // Ď ď
    case 0x011A: case 0x011B:  // Ě ě
    case 0x0147: case 0x0148:  // Ň ň
    case 0x0158: case 0x0159:  // Ř ř
    case 0x0160: case 0x0161:  // Š š
    case 0x0164: case 0x0165:  // Ť ť
    case 0x016E: case 0x016F:  // Ů ů
    case 0x017D: case 0x017E:  // Ž ž
    case 0x2082:               // ₂
      return true;
    default:
      return false;
  }
}

// Co v písmu není, ale v rodinném kalendáři se objevit může. Slovenština,
// polština a němčina proto, že se v názvech akcí a jménech míchají s češtinou;
// interpunkce proto, že ji do titulků sází kdejaká aplikace.
const char *fallbackAscii(uint32_t codePoint) {
  switch (codePoint) {
    // Německé přehlásky a ostré s.
    case 0x00C4: return "A";  case 0x00E4: return "a";
    case 0x00D6: return "O";  case 0x00F6: return "o";
    case 0x00DC: return "U";  case 0x00FC: return "u";
    case 0x00DF: return "ss";
    // Slovenština, která nesdílí českou sadu.
    case 0x00C0: case 0x00C2: case 0x00C3: case 0x00C5: return "A";
    case 0x00E0: case 0x00E2: case 0x00E3: case 0x00E5: return "a";
    case 0x00C8: case 0x00CA: case 0x00CB: return "E";
    case 0x00E8: case 0x00EA: case 0x00EB: return "e";
    case 0x00CC: case 0x00CE: case 0x00CF: return "I";
    case 0x00EC: case 0x00EE: case 0x00EF: return "i";
    case 0x00D2: case 0x00D4: case 0x00D5: return "O";
    case 0x00F2: case 0x00F4: case 0x00F5: return "o";
    case 0x00D9: case 0x00DB: return "U";
    case 0x00F9: case 0x00FB: return "u";
    case 0x00C7: return "C";  case 0x00E7: return "c";
    case 0x00D1: return "N";  case 0x00F1: return "n";
    case 0x0139: case 0x013D: return "L";
    case 0x013A: case 0x013E: return "l";
    case 0x0154: return "R";  case 0x0155: return "r";
    // Polština.
    case 0x0104: return "A";  case 0x0105: return "a";
    case 0x0106: return "C";  case 0x0107: return "c";
    case 0x0118: return "E";  case 0x0119: return "e";
    case 0x0141: return "L";  case 0x0142: return "l";
    case 0x0143: return "N";  case 0x0144: return "n";
    case 0x015A: return "S";  case 0x015B: return "s";
    case 0x0179: case 0x017B: return "Z";
    case 0x017A: case 0x017C: return "z";
    // Interpunkce, kterou kalendářové aplikace sázejí samy.
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014:
    case 0x2015: case 0x2212: return "-";
    case 0x2018: case 0x2019: case 0x201A: case 0x201B: return "'";
    case 0x201C: case 0x201D: case 0x201E: case 0x201F: return "\"";
    case 0x00AB: case 0x00BB: return "\"";
    case 0x2026: return "...";
    case 0x20AC: return "EUR";
    case 0x00A9: return "(c)";
    default: return nullptr;
  }
}

bool isSpaceCodePoint(uint32_t codePoint) {
  return codePoint == ' ' || codePoint == '\t' || codePoint == '\n' ||
         codePoint == '\r' || codePoint == 0x00A0 || codePoint == 0x2007 ||
         codePoint == 0x2009 || codePoint == 0x202F;
}

// Zapisovač, který nikdy nenechá v cíli půlku vícebajtového znaku: sekvence se
// buď vejde celá, nebo se zahodí. Rozseknuté UTF-8 by LVGL vykreslil jako
// smetí až do konce řádku.
struct Utf8Writer {
  char *destination;
  size_t capacity;
  size_t written = 0;
  // Mezera se zapisuje až spolu s dalším znakem, takže na konci žádná
  // nezůstane a nemusí se ořezávat zpětně.
  bool spacePending = false;
  bool anyWritten = false;

  bool fits(size_t length) const { return written + length + 1 <= capacity; }

  void append(const char *text, size_t length) {
    if (length == 0) return;
    const size_t extra = (spacePending && anyWritten) ? 1 : 0;
    if (!fits(length + extra)) return;
    if (extra != 0) destination[written++] = ' ';
    memcpy(destination + written, text, length);
    written += length;
    spacePending = false;
    anyWritten = true;
  }

  void append(const char *text) { append(text, strlen(text)); }

  void space() {
    if (anyWritten) spacePending = true;
  }

  void appendCodePoint(uint32_t codePoint) {
    char buffer[4];
    size_t length = 0;
    if (codePoint < 0x80) {
      buffer[length++] = static_cast<char>(codePoint);
    } else if (codePoint < 0x800) {
      buffer[length++] = static_cast<char>(0xC0 | (codePoint >> 6));
      buffer[length++] = static_cast<char>(0x80 | (codePoint & 0x3F));
    } else if (codePoint < 0x10000) {
      buffer[length++] = static_cast<char>(0xE0 | (codePoint >> 12));
      buffer[length++] = static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
      buffer[length++] = static_cast<char>(0x80 | (codePoint & 0x3F));
    } else {
      return;
    }
    append(buffer, length);
  }

  void finish() {
    if (capacity != 0) destination[written] = '\0';
  }
};

int hexDigit(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

// Jeden kódový bod z UTF-8. Neplatná sekvence posune kurzor o jediný bajt a
// vrátí false, takže se poškozený vstup nezacyklí ani nepřečte za koncem.
bool decodeUtf8(const char *&cursor, const char *end, uint32_t &codePoint) {
  const unsigned char first = static_cast<unsigned char>(*cursor);
  size_t extra = 0;
  if (first < 0x80) {
    codePoint = first;
  } else if ((first & 0xE0) == 0xC0) {
    codePoint = first & 0x1Fu;
    extra = 1;
  } else if ((first & 0xF0) == 0xE0) {
    codePoint = first & 0x0Fu;
    extra = 2;
  } else if ((first & 0xF8) == 0xF0) {
    codePoint = first & 0x07u;
    extra = 3;
  } else {
    ++cursor;
    return false;
  }
  if (cursor + extra >= end) {
    ++cursor;
    return false;
  }
  for (size_t index = 1; index <= extra; ++index) {
    const unsigned char next = static_cast<unsigned char>(cursor[index]);
    if ((next & 0xC0) != 0x80) {
      ++cursor;
      return false;
    }
    codePoint = (codePoint << 6) | (next & 0x3Fu);
  }
  cursor += extra + 1;
  return true;
}

void writeCodePoint(Utf8Writer &writer, uint32_t codePoint) {
  if (isSpaceCodePoint(codePoint)) {
    writer.space();
    return;
  }
  if (fontHasCodePoint(codePoint)) {
    writer.appendCodePoint(codePoint);
    return;
  }
  const char *fallback = fallbackAscii(codePoint);
  if (fallback != nullptr) writer.append(fallback);
  // Cokoliv dalšího se zahodí, včetně emoji, která do titulků chodí z mobilu.
}

}  // namespace

size_t agendaCopyText(const char *source, size_t length, char *destination,
                      size_t destinationSize) {
  if (destination == nullptr || destinationSize == 0) return 0;
  Utf8Writer writer{destination, destinationSize};
  if (source == nullptr) {
    writer.finish();
    return 0;
  }

  const char *cursor = source;
  const char *end = source + length;
  while (cursor < end) {
    if (*cursor != '\\') {
      uint32_t codePoint = 0;
      if (decodeUtf8(cursor, end, codePoint)) writeCodePoint(writer, codePoint);
      continue;
    }
    if (cursor + 1 >= end) break;
    const char escape = cursor[1];
    cursor += 2;
    switch (escape) {
      case '"': writer.append("\"", 1); break;
      case '\\': writer.append("\\", 1); break;
      case '/': writer.append("/", 1); break;
      case 'b': case 'f': case 'n': case 'r': case 't': writer.space(); break;
      case 'u': {
        uint32_t codePoint = 0;
        bool ok = cursor + 4 <= end;
        for (int index = 0; ok && index < 4; ++index) {
          const int digit = hexDigit(cursor[index]);
          if (digit < 0) ok = false;
          else codePoint = (codePoint << 4) | static_cast<uint32_t>(digit);
        }
        if (!ok) break;
        cursor += 4;
        // Náhradní páry neskládáme: takové znaky písmo displeje stejně nemá.
        if (codePoint >= 0xD800 && codePoint <= 0xDFFF) break;
        writeCodePoint(writer, codePoint);
        break;
      }
      default:
        break;
    }
  }
  writer.finish();
  return writer.written;
}

AgendaParseOutcome agendaParseFeed(const char *payload, size_t length,
                                   size_t maximumItems, AgendaFeed &feed) {
  AgendaParseOutcome outcome;
  feed.count = 0;
  if (payload == nullptr || length == 0) return outcome;

  const char *begin = jsonSkipWhitespace(payload, payload + length);
  const char *end = payload + length;
  if (begin >= end || *begin != '{') return outcome;

  const JsonValue items = jsonFindMember(begin, end, "items");
  if (!items.isArray()) {
    outcome.status = AgendaParseStatus::MissingArray;
    return outcome;
  }

  // Jména kalendářů. Chybějící pole není chyba: starší server je neposílal a
  // obrazovka pak jen vynechá legendu.
  const JsonValue calendars = jsonFindMember(begin, end, "calendars");
  if (calendars.isArray()) {
    JsonArrayCursor names = jsonOpenArray(calendars);
    while (feed.calendarCount < AGENDA_MAX_CALENDARS && jsonNextItem(names)) {
      const char *nameBegin = names.itemBegin;
      const char *nameEnd = names.itemEnd;
      // Prvky pole jsou řetězce, takže se uvozovky musí odloupnout ručně -
      // jsonFindMember tady nepomůže, jméno nemá klíč.
      if (nameEnd - nameBegin < 2 || *nameBegin != '"') continue;
      agendaCopyText(nameBegin + 1, static_cast<size_t>(nameEnd - nameBegin - 2),
                     feed.calendars[feed.calendarCount],
                     AGENDA_CALENDAR_NAME_LENGTH);
      ++feed.calendarCount;
    }
  }

  const size_t limit =
      maximumItems < AGENDA_MAX_ITEMS ? maximumItems : AGENDA_MAX_ITEMS;
  JsonArrayCursor cursor = jsonOpenArray(items);
  while (feed.count < limit && jsonNextItem(cursor)) {
    const JsonValue title =
        jsonFindMember(cursor.itemBegin, cursor.itemEnd, "title");
    // Událost bez titulku by na displeji byla prázdný řádek. Server takové
    // zahazuje, ale spolehnout se na to nemá smysl - stojí to jednu podmínku.
    if (!title.isString) continue;
    AgendaItem &item = feed.items[feed.count];
    item = AgendaItem{};
    if (agendaCopyText(title.contentBegin(),
                       static_cast<size_t>(title.contentEnd() -
                                           title.contentBegin()),
                       item.title, sizeof(item.title)) == 0) {
      continue;
    }

    const JsonValue day = jsonFindMember(cursor.itemBegin, cursor.itemEnd, "day");
    if (day.isString) {
      agendaCopyText(day.contentBegin(),
                     static_cast<size_t>(day.contentEnd() - day.contentBegin()),
                     item.day, sizeof(item.day));
    }
    const JsonValue time =
        jsonFindMember(cursor.itemBegin, cursor.itemEnd, "time");
    if (time.isString) {
      agendaCopyText(time.contentBegin(),
                     static_cast<size_t>(time.contentEnd() -
                                         time.contentBegin()),
                     item.time, sizeof(item.time));
    }
    float calendar = 0.0f;
    if (jsonReadNumberMember(cursor.itemBegin, cursor.itemEnd, "cal",
                             calendar) &&
        calendar >= 0.0f && calendar < 256.0f) {
      item.calendar = static_cast<uint8_t>(calendar);
    }
    ++feed.count;
  }

  outcome.status = AgendaParseStatus::Ok;
  outcome.count = feed.count;
  return outcome;
}
