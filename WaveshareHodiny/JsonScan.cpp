#include "JsonScan.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

namespace {

// Konec řetězce včetně uzavírací uvozovky. Escapovaná uvozovka se musí
// přeskočit, jinak by jméno s lomítkem rozseklo objekt na půl.
const char *stringEnd(const char *cursor, const char *end) {
  // cursor ukazuje na úvodní uvozovku.
  ++cursor;
  while (cursor < end) {
    if (*cursor == '\\') {
      cursor += 2;
      continue;
    }
    if (*cursor == '"') return cursor + 1;
    ++cursor;
  }
  return nullptr;
}

}  // namespace

const char *jsonSkipWhitespace(const char *cursor, const char *end) {
  while (cursor < end && (*cursor == ' ' || *cursor == '\t' ||
                          *cursor == '\r' || *cursor == '\n')) {
    ++cursor;
  }
  return cursor;
}

const char *jsonValueEnd(const char *cursor, const char *end) {
  if (cursor >= end) return nullptr;
  if (*cursor == '"') return stringEnd(cursor, end);
  if (*cursor == '{' || *cursor == '[') {
    const char open = *cursor;
    const char close = (open == '{') ? '}' : ']';
    int depth = 0;
    while (cursor < end) {
      if (*cursor == '"') {
        const char *next = stringEnd(cursor, end);
        if (next == nullptr) return nullptr;
        cursor = next;
        continue;
      }
      if (*cursor == open) ++depth;
      else if (*cursor == close && --depth == 0) return cursor + 1;
      ++cursor;
    }
    return nullptr;
  }
  // Číslo nebo literál končí u čárky nebo u zavírací závorky. Bílé znaky před
  // nimi do hodnoty nepatří: server, který odpověď formátuje na řádky, by jinak
  // poslal "35000\n  " a převod na číslo by to odmítl jako text.
  const char *stop = cursor;
  while (stop < end && *stop != ',' && *stop != '}' && *stop != ']') {
    ++stop;
  }
  while (stop > cursor && (stop[-1] == ' ' || stop[-1] == '\t' ||
                           stop[-1] == '\r' || stop[-1] == '\n')) {
    --stop;
  }
  return stop;
}

JsonValue jsonFindMember(const char *objectBegin, const char *objectEnd,
                         const char *key) {
  JsonValue result;
  if (objectBegin == nullptr || objectBegin >= objectEnd) return result;
  const size_t keyLength = strlen(key);
  const char *cursor = jsonSkipWhitespace(objectBegin, objectEnd);
  if (cursor >= objectEnd || *cursor != '{') return result;
  ++cursor;
  for (;;) {
    cursor = jsonSkipWhitespace(cursor, objectEnd);
    if (cursor >= objectEnd || *cursor == '}') return result;
    if (*cursor != '"') return result;
    const char *nameEnd = stringEnd(cursor, objectEnd);
    if (nameEnd == nullptr) return result;
    const char *nameBegin = cursor + 1;
    const size_t nameLength = static_cast<size_t>(nameEnd - 1 - nameBegin);
    cursor = jsonSkipWhitespace(nameEnd, objectEnd);
    if (cursor >= objectEnd || *cursor != ':') return result;
    cursor = jsonSkipWhitespace(cursor + 1, objectEnd);
    const char *afterValue = jsonValueEnd(cursor, objectEnd);
    if (afterValue == nullptr) return result;
    if (nameLength == keyLength && memcmp(nameBegin, key, keyLength) == 0) {
      result.begin = cursor;
      result.end = afterValue;
      result.isString = (*cursor == '"');
      return result;
    }
    cursor = jsonSkipWhitespace(afterValue, objectEnd);
    if (cursor < objectEnd && *cursor == ',') ++cursor;
  }
}

bool jsonReadNumber(const JsonValue &value, float &out) {
  if (!value.valid()) return false;
  const char *begin = value.contentBegin();
  const char *end = value.contentEnd();
  if (begin >= end) return false;
  char text[32];
  const size_t length = static_cast<size_t>(end - begin);
  if (length >= sizeof(text)) return false;
  memcpy(text, begin, length);
  text[length] = '\0';
  char *stop = nullptr;
  const float parsed = strtof(text, &stop);
  if (stop == text || !isfinite(parsed)) return false;
  // Za číslem smí zůstat jen mezery - "35000 ft" není výška.
  while (*stop == ' ') ++stop;
  if (*stop != '\0') return false;
  out = parsed;
  return true;
}

bool jsonReadNumberMember(const char *objectBegin, const char *objectEnd,
                          const char *key, float &out) {
  return jsonReadNumber(jsonFindMember(objectBegin, objectEnd, key), out);
}

bool jsonReadBoolMember(const char *objectBegin, const char *objectEnd,
                        const char *key) {
  const JsonValue value = jsonFindMember(objectBegin, objectEnd, key);
  if (!value.valid() || value.isString) return false;
  const size_t length = static_cast<size_t>(value.end - value.begin);
  return length == 4 && memcmp(value.begin, "true", 4) == 0;
}

namespace {

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Zapíše jeden znak Unicode jako UTF-8. Volající si text dál skládá na ASCII
// (viz routeTextToAscii), takže stačí kódování, ne převod.
size_t appendUtf8(char *destination, size_t capacity, size_t written,
                  uint32_t codePoint) {
  const auto put = [&](char value) {
    if (written + 1 < capacity) destination[written++] = value;
  };
  if (codePoint < 0x80) {
    put(static_cast<char>(codePoint));
  } else if (codePoint < 0x800) {
    put(static_cast<char>(0xC0 | (codePoint >> 6)));
    put(static_cast<char>(0x80 | (codePoint & 0x3F)));
  } else {
    put(static_cast<char>(0xE0 | (codePoint >> 12)));
    put(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
    put(static_cast<char>(0x80 | (codePoint & 0x3F)));
  }
  return written;
}

}  // namespace

void jsonCopyTextMember(const char *objectBegin, const char *objectEnd,
                        const char *key, char *destination, size_t capacity) {
  if (destination == nullptr || capacity == 0) return;
  destination[0] = '\0';
  const JsonValue value = jsonFindMember(objectBegin, objectEnd, key);
  if (!value.valid() || !value.isString) return;
  const char *begin = value.contentBegin();
  const char *end = value.contentEnd();
  while (begin < end && (*begin == ' ' || *begin == '\t')) ++begin;
  while (end > begin && (end[-1] == ' ' || end[-1] == '\t')) --end;

  // Escapy se rozkódují, jinak by se na displeji objevilo doslova "Malmo\u00f6"
  // nebo uvozovka s lomítkem. Neznámý escape se opíše tak, jak přišel.
  size_t written = 0;
  const char *cursor = begin;
  while (cursor < end && written + 1 < capacity) {
    if (*cursor != '\\' || cursor + 1 >= end) {
      destination[written++] = *cursor++;
      continue;
    }
    const char escape = cursor[1];
    cursor += 2;
    switch (escape) {
      case '"': destination[written++] = '"'; break;
      case '\\': destination[written++] = '\\'; break;
      case '/': destination[written++] = '/'; break;
      case 'b': destination[written++] = '\b'; break;
      case 'f': destination[written++] = '\f'; break;
      case 'n': destination[written++] = '\n'; break;
      case 'r': destination[written++] = '\r'; break;
      case 't': destination[written++] = '\t'; break;
      case 'u': {
        uint32_t codePoint = 0;
        bool ok = cursor + 4 <= end;
        for (int index = 0; ok && index < 4; ++index) {
          const int digit = hexDigit(cursor[index]);
          if (digit < 0) ok = false;
          else codePoint = (codePoint << 4) | static_cast<uint32_t>(digit);
        }
        if (!ok) {
          destination[written++] = 'u';
          break;
        }
        cursor += 4;
        // Náhradní páry neskládáme: takové znaky písmo displeje stejně nemá.
        if (codePoint >= 0xD800 && codePoint <= 0xDFFF) break;
        written = appendUtf8(destination, capacity, written, codePoint);
        break;
      }
      default:
        destination[written++] = escape;
        break;
    }
  }
  destination[written] = '\0';
}

bool jsonTextMemberEquals(const char *objectBegin, const char *objectEnd,
                          const char *key, const char *expected) {
  const JsonValue value = jsonFindMember(objectBegin, objectEnd, key);
  if (!value.valid() || !value.isString) return false;
  const size_t length =
      static_cast<size_t>(value.contentEnd() - value.contentBegin());
  return length == strlen(expected) &&
         memcmp(value.contentBegin(), expected, length) == 0;
}

JsonArrayCursor jsonOpenArray(const JsonValue &value) {
  JsonArrayCursor result;
  if (!value.isArray()) return result;
  result.cursor = value.begin + 1;
  result.end = value.end;
  return result;
}

bool jsonNextItem(JsonArrayCursor &cursor) {
  cursor.itemBegin = nullptr;
  cursor.itemEnd = nullptr;
  if (!cursor.valid()) return false;
  cursor.cursor = jsonSkipWhitespace(cursor.cursor, cursor.end);
  if (cursor.cursor >= cursor.end || *cursor.cursor == ']') return false;
  const char *itemEnd = jsonValueEnd(cursor.cursor, cursor.end);
  if (itemEnd == nullptr) return false;
  // Prázdná hodnota znamená, že se pole rozbilo - typicky přebytečná závorka
  // uvnitř. Kdyby se to prohlásilo za prvek, ukazatel by se neposunul a smyčka
  // volajícího by se točila donekonečna; úloha radaru bez jediného uvolnění
  // procesoru znamená restart od hlídacího psa.
  if (itemEnd == cursor.cursor) return false;
  cursor.itemBegin = cursor.cursor;
  cursor.itemEnd = itemEnd;
  cursor.cursor = jsonSkipWhitespace(itemEnd, cursor.end);
  if (cursor.cursor < cursor.end && *cursor.cursor == ',') ++cursor.cursor;
  return true;
}
