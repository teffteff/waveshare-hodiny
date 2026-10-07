#include "TrafficFeed.h"

#include <math.h>
#include <string.h>

#include "JsonScan.h"

namespace {

// Souřadnice smí ležet i mimo displej (kreslení ořezává), ale ne libovolně
// daleko: int16 by přetekl a úsečka přes půl milionu pixelů by se ořezávala
// zbytečně dlouho.
constexpr float COORDINATE_LIMIT = 2000.0f;
// Rozumný strop pro minuty a zpoždění; nesmysl se zahodí jako chybějící údaj.
constexpr float MAX_MINUTES = 1440.0f;
constexpr float MAX_DELAY_SECONDS = 86400.0f;
// Unixový čas se čte jako celé číslo: float by ho zaokrouhlil o minuty.
constexpr int64_t MIN_EPOCH = 1600000000;
constexpr int64_t MAX_EPOCH = 4102444800;  // rok 2100

JsonValue itemValue(const JsonArrayCursor &cursor) {
  JsonValue value;
  value.begin = cursor.itemBegin;
  value.end = cursor.itemEnd;
  value.isString = cursor.itemBegin != nullptr && *cursor.itemBegin == '"';
  return value;
}

bool isNull(const JsonValue &value) {
  return value.valid() && !value.isString && value.end - value.begin == 4 &&
         memcmp(value.begin, "null", 4) == 0;
}

bool readInteger(const JsonValue &value, int64_t &out) {
  if (!value.valid() || value.isString) return false;
  const char *cursor = value.begin;
  const char *end = value.end;
  bool negative = false;
  if (cursor < end && *cursor == '-') {
    negative = true;
    ++cursor;
  }
  int64_t result = 0;
  bool digits = false;
  for (; cursor < end && *cursor >= '0' && *cursor <= '9'; ++cursor) {
    if (result > (INT64_MAX - 9) / 10) return false;
    result = result * 10 + (*cursor - '0');
    digits = true;
  }
  if (!digits || cursor != end) return false;
  out = negative ? -result : result;
  return true;
}

bool readCoordinate(const JsonValue &value, int16_t &out) {
  float number = 0.0f;
  if (value.isString || !jsonReadNumber(value, number)) return false;
  if (!(number > -COORDINATE_LIMIT && number < COORDINATE_LIMIT)) return false;
  out = static_cast<int16_t>(lroundf(number));
  return true;
}

// Dvojice [x, y] jednoho bodu.
bool readPoint(const char *objectBegin, const char *objectEnd, int16_t &x,
               int16_t &y) {
  JsonArrayCursor cursor =
      jsonOpenArray(jsonFindMember(objectBegin, objectEnd, "xy"));
  if (!cursor.valid() || !jsonNextItem(cursor) ||
      !readCoordinate(itemValue(cursor), x) || !jsonNextItem(cursor) ||
      !readCoordinate(itemValue(cursor), y))
    return false;
  return true;
}

// Celé minuty, nebo -1 pro chybějící či nesmyslný údaj.
int16_t readMinutes(const char *objectBegin, const char *objectEnd,
                    const char *key) {
  float number = 0.0f;
  if (!jsonReadNumberMember(objectBegin, objectEnd, key, number)) return -1;
  if (!(number >= 0.0f && number <= MAX_MINUTES)) return -1;
  return static_cast<int16_t>(lroundf(number));
}

// Text v UTF-8 zkrácený na kapacitu po celých znacích. jsonCopyText řeže po
// bajtech a rozseknutý znak by LVGL nakreslil jako smetí.
void copyText(const JsonValue &value, char *destination, size_t capacity) {
  char buffer[TRAFFIC_TEXT_LENGTH * 3];
  jsonCopyText(value, buffer, sizeof(buffer));
  trafficFoldText(buffer);
  size_t length = strlen(buffer);
  if (length >= capacity) {
    length = capacity - 1;
    // Pokračovací bajty 10xxxxxx patří k znaku, který začal dřív.
    while (length > 0 && (static_cast<uint8_t>(buffer[length]) & 0xC0) == 0x80)
      --length;
  }
  memcpy(destination, buffer, length);
  destination[length] = '\0';
}

TrafficLineKind lineKind(const char *id) {
  if (strcmp(id, "d1-P") == 0) return TrafficLineKind::D1Prague;
  if (strcmp(id, "d1-B") == 0) return TrafficLineKind::D1Brno;
  if (strncmp(id, "via-", 4) == 0) return TrafficLineKind::Via;
  if (strncmp(id, "ondrejov-", 9) == 0) return TrafficLineKind::Drive;
  return TrafficLineKind::Other;
}

bool parseLine(const char *objectBegin, const char *objectEnd,
               TrafficLine &line) {
  jsonCopyTextMember(objectBegin, objectEnd, "id", line.id, sizeof(line.id));
  line.kind = lineKind(line.id);
  line.pointCount = 0;
  line.segmentCount = 0;
  JsonArrayCursor cursor =
      jsonOpenArray(jsonFindMember(objectBegin, objectEnd, "xy"));
  if (!cursor.valid()) return false;
  int16_t pending = 0;
  bool havePending = false;
  while (jsonNextItem(cursor)) {
    int16_t value = 0;
    if (!readCoordinate(itemValue(cursor), value)) return false;
    if (!havePending) {
      pending = value;
      havePending = true;
      continue;
    }
    havePending = false;
    // Čára delší než strop se usekne; zbytek by se stejně nevešel.
    if (line.pointCount >= TRAFFIC_MAX_POINTS) continue;
    line.x[line.pointCount] = pending;
    line.y[line.pointCount] = value;
    ++line.pointCount;
  }
  if (line.pointCount < 2) return false;

  JsonArrayCursor segments =
      jsonOpenArray(jsonFindMember(objectBegin, objectEnd, "segs"));
  while (segments.valid() && line.segmentCount < TRAFFIC_MAX_SEGMENTS &&
         jsonNextItem(segments)) {
    JsonArrayCursor triple = jsonOpenArray(itemValue(segments));
    int64_t values[3] = {};
    bool ok = triple.valid();
    for (int index = 0; ok && index < 3; ++index)
      ok = jsonNextItem(triple) && readInteger(itemValue(triple), values[index]);
    if (!ok) continue;
    int64_t from = values[0];
    int64_t to = values[1];
    if (from > to) {
      const int64_t swap = from;
      from = to;
      to = swap;
    }
    // Úsek ukazující mimo uložené body (třeba za useknutý konec) se ořízne.
    if (from < 0) from = 0;
    if (to >= line.pointCount) to = line.pointCount - 1;
    if (from >= to) continue;
    int64_t level = values[2];
    if (level < TRAFFIC_LEVEL_FREE) level = TRAFFIC_LEVEL_FREE;
    if (level > TRAFFIC_LEVEL_CLOSED) level = TRAFFIC_LEVEL_CLOSED;
    TrafficSegment &segment = line.segments[line.segmentCount++];
    segment.from = static_cast<uint8_t>(from);
    segment.to = static_cast<uint8_t>(to);
    segment.level = static_cast<uint8_t>(level);
  }
  return true;
}

// Pole textů do pevných řádků; prázdné texty se přeskočí.
uint8_t parseTextList(const char *objectBegin, const char *objectEnd,
                      const char *key, char (*output)[TRAFFIC_TEXT_LENGTH],
                      uint8_t capacity) {
  uint8_t count = 0;
  JsonArrayCursor cursor =
      jsonOpenArray(jsonFindMember(objectBegin, objectEnd, key));
  while (cursor.valid() && count < capacity && jsonNextItem(cursor)) {
    const JsonValue value = itemValue(cursor);
    if (!value.isString) continue;
    copyText(value, output[count], TRAFFIC_TEXT_LENGTH);
    if (output[count][0] != '\0') ++count;
  }
  return count;
}

// Text cesty začíná jménem cíle ("Praha 30 min · stojí Chodov → ..."). Takový
// text se nevypisuje zvlášť - čísla nese cesta sama - a co je za první tečkou,
// je důvod zdržení. Vrací true, když text patřil některé cestě.
bool attachDriveText(TrafficData &data, const char *raw) {
  for (uint8_t index = 0; index < data.driveCount; ++index) {
    TrafficDrive &drive = data.drives[index];
    const size_t nameLength = strlen(drive.to);
    if (nameLength == 0 || strncmp(raw, drive.to, nameLength) != 0 ||
        raw[nameLength] != ' ')
      continue;
    // "·" je v UTF-8 C2 B7. Text bez něj nic navíc neříká.
    const char *dot = strstr(raw + nameLength, "\xC2\xB7");
    if (dot != nullptr && drive.detail[0] == '\0') {
      const char *detail = dot + 2;
      while (*detail == ' ') ++detail;
      char buffer[TRAFFIC_TEXT_LENGTH * 3];
      strncpy(buffer, detail, sizeof(buffer) - 1);
      buffer[sizeof(buffer) - 1] = '\0';
      trafficFoldText(buffer);
      size_t length = strlen(buffer);
      if (length >= sizeof(drive.detail)) {
        length = sizeof(drive.detail) - 1;
        while (length > 0 &&
               (static_cast<uint8_t>(buffer[length]) & 0xC0) == 0x80)
          --length;
      }
      memcpy(drive.detail, buffer, length);
      drive.detail[length] = '\0';
    }
    return true;
  }
  return false;
}

}  // namespace

void trafficFoldText(char *text) {
  if (text == nullptr) return;
  const uint8_t *read = reinterpret_cast<const uint8_t *>(text);
  char *write = text;
  while (*read != '\0') {
    const uint8_t first = read[0];
    if (first < 0x20) {
      // Zalomení a tabulátory z odpovědi by rozbily řádek na displeji.
      *write++ = ' ';
      ++read;
      continue;
    }
    if (first == 0xC2 && read[1] == 0xB7) {
      // "A · B" -> "A, B": tečka uprostřed odděluje části věty.
      if (write > text && write[-1] == ' ') --write;
      *write++ = ',';
      read += 2;
      continue;
    }
    if (first == 0xC2 && read[1] == 0xA0) {
      *write++ = ' ';
      read += 2;
      continue;
    }
    if (first == 0xE2 && read[1] != '\0' && read[2] != '\0') {
      const uint8_t second = read[1];
      const uint8_t third = read[2];
      const char *replacement = nullptr;
      if (second == 0x86 && third == 0x92) replacement = "-";  // →
      else if (second == 0x88 && third == 0x92) replacement = "-";  // −
      else if (second == 0x80) {
        switch (third) {
          case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95:
            replacement = "-";
            break;
          case 0x98: case 0x99: case 0x9A: case 0x9B: replacement = "'"; break;
          case 0x9C: case 0x9D: case 0x9E: case 0x9F: replacement = "\""; break;
          case 0xA6: replacement = "..."; break;
          case 0x87: case 0x89: case 0xAF: replacement = " "; break;
          default: break;
        }
      }
      if (replacement != nullptr) {
        const size_t length = strlen(replacement);
        memmove(write, replacement, length);
        write += length;
        read += 3;
        continue;
      }
    }
    *write++ = static_cast<char>(*read++);
  }
  *write = '\0';
}

TrafficParseStatus trafficFeedParse(const char *json, size_t length,
                                    TrafficData &data) {
  // Celý TrafficData má přes sedm kilobajtů; nuluje se jen to, co se čte,
  // aby se nevytvářela dočasná kopie na zásobníku úlohy.
  data.now = 0;
  data.sampleTime = 0;
  data.stale = false;
  data.lineCount = 0;
  data.placeCount = 0;
  data.exitCount = 0;
  data.busCount = 0;
  data.driveCount = 0;
  data.textCount = 0;
  data.warningCount = 0;
  data.departureCount = 0;
  if (json == nullptr || length == 0) return TrafficParseStatus::NotJson;
  const char *end = json + length;
  const char *objectBegin = jsonSkipWhitespace(json, end);
  if (objectBegin == nullptr || objectBegin >= end || *objectBegin != '{')
    return TrafficParseStatus::NotJson;
  const char *objectEnd = jsonValueEnd(objectBegin, end);
  if (objectEnd == nullptr) return TrafficParseStatus::NotJson;

  const JsonValue lines = jsonFindMember(objectBegin, objectEnd, "lines");
  if (!lines.isArray()) return TrafficParseStatus::Invalid;

  int64_t number = 0;
  if (readInteger(jsonFindMember(objectBegin, objectEnd, "now"), number) &&
      number >= MIN_EPOCH && number <= MAX_EPOCH)
    data.now = number;
  const JsonValue sample = jsonFindMember(objectBegin, objectEnd, "t");
  if (!isNull(sample) && readInteger(sample, number) && number >= MIN_EPOCH &&
      number <= MAX_EPOCH)
    data.sampleTime = number;
  data.stale = jsonReadBoolMember(objectBegin, objectEnd, "stale");

  JsonArrayCursor cursor = jsonOpenArray(lines);
  while (data.lineCount < TRAFFIC_MAX_LINES && jsonNextItem(cursor)) {
    if (*cursor.itemBegin != '{') continue;
    if (parseLine(cursor.itemBegin, cursor.itemEnd, data.lines[data.lineCount]))
      ++data.lineCount;
  }

  cursor = jsonOpenArray(jsonFindMember(objectBegin, objectEnd, "places"));
  while (cursor.valid() && data.placeCount < TRAFFIC_MAX_PLACES &&
         jsonNextItem(cursor)) {
    if (*cursor.itemBegin != '{') continue;
    TrafficPlace &place = data.places[data.placeCount];
    if (!readPoint(cursor.itemBegin, cursor.itemEnd, place.x, place.y))
      continue;
    copyText(jsonFindMember(cursor.itemBegin, cursor.itemEnd, "name"),
             place.name, sizeof(place.name));
    if (place.name[0] == '\0') continue;
    ++data.placeCount;
  }

  cursor = jsonOpenArray(jsonFindMember(objectBegin, objectEnd, "exits"));
  while (cursor.valid() && data.exitCount < TRAFFIC_MAX_EXITS &&
         jsonNextItem(cursor)) {
    if (*cursor.itemBegin != '{') continue;
    TrafficExit &exit = data.exits[data.exitCount];
    if (!readPoint(cursor.itemBegin, cursor.itemEnd, exit.x, exit.y)) continue;
    if (!readInteger(jsonFindMember(cursor.itemBegin, cursor.itemEnd, "n"),
                     number) ||
        number < 0 || number > 999)
      continue;
    exit.number = static_cast<uint16_t>(number);
    ++data.exitCount;
  }

  cursor = jsonOpenArray(jsonFindMember(objectBegin, objectEnd, "buses"));
  while (cursor.valid() && data.busCount < TRAFFIC_MAX_BUSES &&
         jsonNextItem(cursor)) {
    if (*cursor.itemBegin != '{') continue;
    TrafficBus &bus = data.buses[data.busCount];
    if (!readPoint(cursor.itemBegin, cursor.itemEnd, bus.x, bus.y)) continue;
    // Číslo linky se píše pixelovým písmem, které umí jen ASCII.
    jsonCopyTextMember(cursor.itemBegin, cursor.itemEnd, "line", bus.line,
                       sizeof(bus.line));
    for (char *c = bus.line; *c != '\0'; ++c)
      if (*c < 0x20 || *c > 0x7e) *c = '?';
    float delay = 0.0f;
    bus.hasDelay = jsonReadNumberMember(cursor.itemBegin, cursor.itemEnd,
                                        "delay", delay) &&
                   delay > -MAX_DELAY_SECONDS && delay < MAX_DELAY_SECONDS;
    bus.delaySeconds = bus.hasDelay ? static_cast<int32_t>(lroundf(delay)) : 0;
    ++data.busCount;
  }

  cursor = jsonOpenArray(jsonFindMember(objectBegin, objectEnd, "drives"));
  while (cursor.valid() && data.driveCount < TRAFFIC_MAX_DRIVES &&
         jsonNextItem(cursor)) {
    if (*cursor.itemBegin != '{') continue;
    TrafficDrive &drive = data.drives[data.driveCount];
    copyText(jsonFindMember(cursor.itemBegin, cursor.itemEnd, "to"), drive.to,
             sizeof(drive.to));
    if (drive.to[0] == '\0') continue;
    drive.minutes = readMinutes(cursor.itemBegin, cursor.itemEnd, "min");
    drive.usualMinutes = readMinutes(cursor.itemBegin, cursor.itemEnd, "usual");
    drive.backMinutes = readMinutes(cursor.itemBegin, cursor.itemEnd, "back");
    drive.detail[0] = '\0';
    ++data.driveCount;
  }

  // Texty se k cestám přiřazují ještě nepřevedené: jméno cíle i tečka se
  // hledají v tom, co poslal server.
  cursor = jsonOpenArray(jsonFindMember(objectBegin, objectEnd, "texts"));
  while (cursor.valid() && jsonNextItem(cursor)) {
    const JsonValue value = itemValue(cursor);
    if (!value.isString) continue;
    char raw[TRAFFIC_TEXT_LENGTH * 3];
    jsonCopyText(value, raw, sizeof(raw));
    if (raw[0] == '\0' || attachDriveText(data, raw)) continue;
    if (data.textCount >= TRAFFIC_MAX_TEXTS) continue;
    copyText(value, data.texts[data.textCount], TRAFFIC_TEXT_LENGTH);
    if (data.texts[data.textCount][0] != '\0') ++data.textCount;
  }

  data.warningCount = parseTextList(objectBegin, objectEnd, "warnings",
                                    data.warnings, TRAFFIC_MAX_WARNINGS);
  data.departureCount = parseTextList(objectBegin, objectEnd, "departures",
                                      data.departures, TRAFFIC_MAX_DEPARTURES);
  return TrafficParseStatus::Ok;
}
