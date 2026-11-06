#include "DisplayNotification.h"

#include <string.h>

#include "JsonScan.h"

namespace {

bool validUtf8(const char *text) {
  const unsigned char *cursor = reinterpret_cast<const unsigned char *>(text);
  while (*cursor != 0) {
    const unsigned char lead = *cursor;
    size_t extra = 0;
    uint32_t codePoint = 0;
    if (lead < 0x80) {
      ++cursor;
      continue;
    } else if ((lead & 0xE0) == 0xC0) {
      extra = 1;
      codePoint = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
      extra = 2;
      codePoint = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
      extra = 3;
      codePoint = lead & 0x07;
    } else {
      return false;
    }
    ++cursor;
    for (size_t index = 0; index < extra; ++index, ++cursor) {
      if ((*cursor & 0xC0) != 0x80) return false;
      codePoint = (codePoint << 6) | (*cursor & 0x3F);
    }
    // Přetažené zápisy, náhradní páry a kódy za Unicode.
    if ((extra == 1 && codePoint < 0x80) ||
        (extra == 2 && codePoint < 0x800) ||
        (extra == 3 && codePoint < 0x10000) || codePoint > 0x10FFFF ||
        (codePoint >= 0xD800 && codePoint <= 0xDFFF))
      return false;
  }
  return true;
}

// Celé nezáporné číslo do limitu; prázdný text, znaménko ani desetiny ne.
bool parseBoundedInteger(const char *text, uint32_t limit, uint32_t &out) {
  if (text == nullptr || *text == '\0') return false;
  uint32_t value = 0;
  for (const char *cursor = text; *cursor != '\0'; ++cursor) {
    if (*cursor < '0' || *cursor > '9') return false;
    value = value * 10 + static_cast<uint32_t>(*cursor - '0');
    if (value > limit) return false;
  }
  out = value;
  return true;
}

// Okrajové mezery a konce řádků pryč; \r se zahodí všude, v nadpisu se z \n
// a tabulátoru stane mezera.
void normalizeText(const char *source, char *destination, size_t capacity,
                   bool singleLine) {
  while (*source == ' ' || *source == '\t' || *source == '\r' ||
         *source == '\n')
    ++source;
  size_t written = 0;
  for (; *source != '\0' && written + 1 < capacity; ++source) {
    char character = *source;
    if (character == '\r') continue;
    if (character == '\t' || (singleLine && character == '\n')) character = ' ';
    destination[written++] = character;
  }
  while (written > 0 &&
         (destination[written - 1] == ' ' || destination[written - 1] == '\n'))
    --written;
  destination[written] = '\0';
}

}  // namespace

bool notificationParseColor(const char *text, uint32_t &out) {
  if (text == nullptr) return false;
  if (*text == '#') ++text;
  if (strlen(text) != 6) return false;
  uint32_t value = 0;
  for (const char *cursor = text; *cursor != '\0'; ++cursor) {
    const char c = *cursor;
    uint32_t digit = 0;
    if (c >= '0' && c <= '9') digit = c - '0';
    else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
    else return false;
    value = (value << 4) | digit;
  }
  out = value;
  return true;
}

const char *notificationFromFields(const NotificationFields &fields,
                                   DisplayNotification &out) {
  DisplayNotification parsed;
  if (fields.title == nullptr) return "Chybí nadpis notifikace (title).";
  if (fields.message == nullptr) return "Chybí text notifikace (message).";
  // Nejdřív délka, pak UTF-8: příliš dlouhý text z JSONu přijde oříznutý,
  // klidně uprostřed znaku, a hláška má říct, že je dlouhý.
  char title[NOTIFICATION_TITLE_MAX_BYTES + 2];
  normalizeText(fields.title, title, sizeof(title), true);
  if (strlen(title) > NOTIFICATION_TITLE_MAX_BYTES)
    return "Nadpis notifikace může mít nejvýš 96 bajtů.";
  if (title[0] == '\0') return "Chybí nadpis notifikace (title).";
  char message[NOTIFICATION_MESSAGE_MAX_BYTES + 2];
  normalizeText(fields.message, message, sizeof(message), false);
  if (strlen(message) > NOTIFICATION_MESSAGE_MAX_BYTES)
    return "Text notifikace může mít nejvýš 768 bajtů.";
  if (message[0] == '\0') return "Chybí text notifikace (message).";
  if (!validUtf8(title) || !validUtf8(message))
    return "Nadpis nebo text notifikace není platné UTF-8.";
  strcpy(parsed.title, title);
  strcpy(parsed.message, message);

  if (fields.durationSeconds != nullptr &&
      !parseBoundedInteger(fields.durationSeconds,
                           NOTIFICATION_MAX_DURATION_SECONDS,
                           parsed.durationSeconds))
    return "Doba zobrazení (durationSeconds) musí být celé číslo 0 až 86400.";
  uint32_t beep = 0;
  if (fields.beep != nullptr &&
      !parseBoundedInteger(fields.beep, NOTIFICATION_MAX_BEEP_MS, beep))
    return "Délka pípnutí (beep) musí být celé číslo 0 až 5000 ms.";
  parsed.beepMs = static_cast<uint16_t>(beep);
  if (fields.textColor != nullptr &&
      !notificationParseColor(fields.textColor, parsed.textColor))
    return "Barva textu (textColor) musí být ve tvaru #RRGGBB.";
  if (fields.backgroundColor != nullptr &&
      !notificationParseColor(fields.backgroundColor, parsed.backgroundColor))
    return "Barva pozadí (backgroundColor) musí být ve tvaru #RRGGBB.";
  out = parsed;
  return nullptr;
}

namespace {

// Hodnota členu jako text: řetězec rozkódovaný, číslo nebo literál opsaný.
// Buffer má rezervu, aby příliš dlouhý text neprošel oříznutý. Vrací false,
// když člen chybí; null se počítá jako chybějící.
bool jsonMemberText(const char *begin, const char *end, const char *key,
                    char *destination, size_t capacity) {
  const JsonValue value = jsonFindMember(begin, end, key);
  if (!value.valid()) return false;
  if (value.isString) {
    jsonCopyText(value, destination, capacity);
    return true;
  }
  const size_t length = static_cast<size_t>(value.end - value.begin);
  if (length == 4 && strncmp(value.begin, "null", 4) == 0) return false;
  // Objekt nebo pole tu nemá co dělat; projde jako neplatná hodnota.
  const size_t copied = length < capacity ? length : capacity - 1;
  memcpy(destination, value.begin, copied);
  destination[copied] = '\0';
  return true;
}

}  // namespace

const char *notificationFromJson(const char *body, size_t length,
                                 DisplayNotification &out,
                                 NotificationScratch &scratch) {
  const char *end = body + length;
  const char *begin = jsonSkipWhitespace(body, end);
  if (begin == nullptr || begin >= end || *begin != '{' ||
      jsonValueEnd(begin, end) == nullptr)
    return "Tělo požadavku není platný JSON objekt.";
  const char *objectEnd = jsonValueEnd(begin, end);
  char *title = scratch.title;
  char *message = scratch.message;
  char duration[16], beep[16], textColor[16], backgroundColor[16];
  NotificationFields fields;
  if (jsonMemberText(begin, objectEnd, "title", title, sizeof(scratch.title)))
    fields.title = title;
  if (jsonMemberText(begin, objectEnd, "message", message, sizeof(scratch.message)))
    fields.message = message;
  if (jsonMemberText(begin, objectEnd, "durationSeconds", duration,
                     sizeof(duration)))
    fields.durationSeconds = duration;
  if (jsonMemberText(begin, objectEnd, "beep", beep, sizeof(beep)))
    fields.beep = beep;
  if (jsonMemberText(begin, objectEnd, "textColor", textColor,
                     sizeof(textColor)))
    fields.textColor = textColor;
  if (jsonMemberText(begin, objectEnd, "backgroundColor", backgroundColor,
                     sizeof(backgroundColor)))
    fields.backgroundColor = backgroundColor;
  return notificationFromFields(fields, out);
}
