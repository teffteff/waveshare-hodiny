#include "HomeAssistantTemplate.h"

#include <stdio.h>
#include <string.h>

namespace {

bool entityIdCharacter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

// Postupné skládání do pevného bufferu; po přetečení už jen hlásí chybu.
struct Writer {
  char *out;
  size_t capacity;
  size_t length = 0;
  bool overflow = false;

  void put(const char *text) {
    const size_t size = strlen(text);
    if (overflow || length + size + 1 > capacity) {
      overflow = true;
      return;
    }
    memcpy(out + length, text, size);
    length += size;
    out[length] = '\0';
  }

  void putNumber(size_t value) {
    char text[24];
    snprintf(text, sizeof(text), "%u", static_cast<unsigned>(value));
    put(text);
  }
};

// Přečte nezáporné celé číslo (klíč) přesně v rozsahu hodnoty.
bool readKey(const JsonValue &value, size_t &key) {
  if (!value.valid() || value.isString || value.begin == value.end)
    return false;
  size_t result = 0;
  for (const char *cursor = value.begin; cursor < value.end; ++cursor) {
    if (*cursor < '0' || *cursor > '9') return false;
    result = result * 10 + static_cast<size_t>(*cursor - '0');
    if (result > 100000) return false;
  }
  key = result;
  return true;
}

// Další prvek pole [klíč, "stav", ...]; cursor ukazuje za '[' nebo za
// předchozí prvek. Vrací false na konci pole nebo u rozbitého zápisu.
bool nextElement(const char *&cursor, const char *end, bool first,
                 JsonValue &value) {
  cursor = jsonSkipWhitespace(cursor, end);
  if (!first) {
    if (cursor >= end || *cursor != ',') return false;
    cursor = jsonSkipWhitespace(cursor + 1, end);
  }
  const char *valueEnd = jsonValueEnd(cursor, end);
  if (valueEnd == nullptr || valueEnd == cursor) return false;
  value.begin = cursor;
  value.end = valueEnd;
  value.isString = *cursor == '"';
  cursor = valueEnd;
  return true;
}

}  // namespace

bool haTemplateEntityIdValid(const char *entityId) {
  if (entityId == nullptr || entityId[0] == '\0') return false;
  const char *dot = strchr(entityId, '.');
  if (dot == nullptr || dot == entityId || dot[1] == '\0') return false;
  for (const char *cursor = entityId; *cursor != '\0'; ++cursor) {
    if (cursor == dot) continue;
    if (!entityIdCharacter(*cursor)) return false;
  }
  return true;
}

int haTemplateKey(const char *const *entityIds, size_t count, size_t index) {
  if (index >= count || !haTemplateEntityIdValid(entityIds[index])) return -1;
  for (size_t earlier = 0; earlier < index; ++earlier) {
    if (strcmp(entityIds[earlier], entityIds[index]) == 0)
      return static_cast<int>(earlier);
  }
  return static_cast<int>(index);
}

size_t haTemplateBuildRequest(const char *const *entityIds, size_t count,
                              size_t attributesIndex, char *out,
                              size_t capacity) {
  if (out == nullptr || capacity == 0) return 0;
  out[0] = '\0';
  Writer writer{out, capacity};
  // V šabloně nesmí být uvozovka ani zpětné lomítko: vkládá se do JSON těla
  // bez escapování, proto Jinja používá apostrofy. ID entit prošla
  // haTemplateEntityIdValid(), takže apostrof ani nic jiného neobsahují.
  writer.put("{\"template\":\"{% set ids = {");
  size_t listed = 0;
  for (size_t index = 0; index < count; ++index) {
    if (haTemplateKey(entityIds, count, index) != static_cast<int>(index))
      continue;
    if (listed++ > 0) writer.put(",");
    writer.put("'");
    writer.put(entityIds[index]);
    writer.put("':");
    writer.putNumber(index);
  }
  if (listed == 0) {
    out[0] = '\0';
    return 0;
  }
  writer.put("} %}");
  // Atributy jen u entity slunce: jeho klíč (první výskyt stejného ID).
  const int attributesKey =
      attributesIndex == HA_TEMPLATE_NO_ATTRIBUTES
          ? -1
          : haTemplateKey(entityIds, count, attributesIndex);
  writer.put("{% set a = ");
  if (attributesKey < 0) {
    writer.put("-1");
  } else {
    writer.putNumber(static_cast<size_t>(attributesKey));
  }
  writer.put(" %}");
  char limit[8];
  snprintf(limit, sizeof(limit), "%u",
           static_cast<unsigned>(HA_TEMPLATE_ATTRIBUTE_MAX_CHARS));
  writer.put(
      "[{% for s in states if s.entity_id in ids %}"
      "{% if not loop.first %},{% endif %}"
      "{% set k = ids[s.entity_id] %}"
      "{% if k == a %}{{ [k, s.state, s.last_changed.isoformat(), "
      "((s.attributes.next_rising or '') | string)[:");
  writer.put(limit);
  writer.put("], ((s.attributes.next_setting or '') | string)[:");
  writer.put(limit);
  writer.put(
      "]] | to_json }}"
      "{% else %}{{ [k, s.state] | to_json }}{% endif %}"
      "{% endfor %}]\"}");
  if (writer.overflow) {
    out[0] = '\0';
    return 0;
  }
  return writer.length;
}

bool haTemplateParseResponse(const char *body, size_t length,
                             const char *const *entityIds, size_t count,
                             HaTemplateEntity *entities) {
  if (body == nullptr || entities == nullptr) return false;
  for (size_t index = 0; index < count; ++index)
    entities[index] = HaTemplateEntity{};
  const char *end = body + length;
  const char *cursor = jsonSkipWhitespace(body, end);
  if (cursor >= end || *cursor != '[') return false;
  const char *arrayEnd = jsonValueEnd(cursor, end);
  if (arrayEnd == nullptr) return false;
  // Za polem smí být jen bílé znaky; cokoli jiného znamená cizí obsah.
  if (jsonSkipWhitespace(arrayEnd, end) != end) return false;
  ++cursor;
  const char *itemsEnd = arrayEnd - 1;  // zavírací ']'
  bool first = true;
  while (true) {
    cursor = jsonSkipWhitespace(cursor, itemsEnd);
    if (cursor >= itemsEnd) break;
    JsonValue item;
    if (!nextElement(cursor, itemsEnd, first, item)) return false;
    first = false;
    if (!item.isArray()) return false;

    const char *inner = item.begin + 1;
    const char *innerEnd = item.end - 1;
    JsonValue fields[5];
    size_t fieldCount = 0;
    while (true) {
      inner = jsonSkipWhitespace(inner, innerEnd);
      if (inner >= innerEnd) break;
      if (fieldCount >= 5) return false;
      if (!nextElement(inner, innerEnd, fieldCount == 0, fields[fieldCount]))
        return false;
      ++fieldCount;
    }
    size_t key = 0;
    if ((fieldCount != 2 && fieldCount != 5) || !readKey(fields[0], key))
      return false;
    if (key >= count || haTemplateKey(entityIds, count, key) !=
                            static_cast<int>(key))
      return false;
    for (size_t field = 1; field < fieldCount; ++field) {
      if (!fields[field].isString) return false;
    }
    HaTemplateEntity entity;
    entity.found = true;
    entity.state = fields[1];
    if (fieldCount == 5) {
      entity.lastChanged = fields[2];
      entity.nextRising = fields[3];
      entity.nextSetting = fields[4];
    }
    // Stejné ID na víc pozicích seznamu dostane stejné údaje.
    for (size_t index = key; index < count; ++index) {
      if (haTemplateKey(entityIds, count, index) == static_cast<int>(key))
        entities[index] = entity;
    }
  }
  return true;
}

size_t haTemplateCopyText(const JsonValue &value, char *destination,
                          size_t capacity) {
  if (destination == nullptr || capacity == 0) return 0;
  destination[0] = '\0';
  if (!value.valid() || !value.isString) return 0;
  size_t written = 0;
  bool escaped = false;
  for (const char *cursor = value.contentBegin();
       cursor < value.contentEnd() && written + 1 < capacity; ++cursor) {
    if (!escaped && *cursor == '\\') {
      escaped = true;
      continue;
    }
    escaped = false;
    destination[written++] = *cursor;
  }
  destination[written] = '\0';
  return written;
}
