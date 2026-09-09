#include "RouteParser.h"

#include <math.h>
#include <string.h>

#include "JsonScan.h"

namespace {

// U+00C0..U+00FF a U+0100..U+017F složené na ASCII. Reálné případy z odpovědí
// adsb.lol: Izmir (U+0130), Krakow (U+0142), Malaga (U+00E1).
const char LATIN1_MAP[65] =
    "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
const char LATIN_EXTENDED_MAP[129] =
    "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiIiJjKkkLlLlLlLlLlNnNnNn"
    "nNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";

// Vzdušná vzdálenost v km. Slouží jen k porovnávání úseků mezi sebou, takže na
// přesném poloměru Země nezáleží.
float haversineKm(float latitude1, float longitude1, float latitude2,
                  float longitude2) {
  constexpr float EARTH_RADIUS_KM = 6371.0f;
  constexpr float DEGREES = 0.017453293f;
  const float deltaLatitude = (latitude2 - latitude1) * DEGREES;
  const float deltaLongitude = (longitude2 - longitude1) * DEGREES;
  float value = sinf(deltaLatitude * 0.5f) * sinf(deltaLatitude * 0.5f) +
                cosf(latitude1 * DEGREES) * cosf(latitude2 * DEGREES) *
                    sinf(deltaLongitude * 0.5f) * sinf(deltaLongitude * 0.5f);
  if (value < 0.0f) value = 0.0f;
  if (value > 1.0f) value = 1.0f;
  return 2.0f * EARTH_RADIUS_KM * asinf(sqrtf(value));
}

// Popisek letiště: město se čte líp než kód, ale u malých letišť bývá prázdné -
// pak IATA.
void airportLabel(char *destination, size_t capacity, const char *objectBegin,
                  const char *objectEnd) {
  char raw[48];
  jsonCopyTextMember(objectBegin, objectEnd, "location", raw, sizeof(raw));
  if (raw[0] == '\0') {
    jsonCopyTextMember(objectBegin, objectEnd, "iata", raw, sizeof(raw));
  }
  routeTextToAscii(destination, capacity, raw);
}

struct Airport {
  const char *begin = nullptr;
  const char *end = nullptr;
  float latitude = 0.0f;
  float longitude = 0.0f;
};

constexpr size_t MAX_AIRPORTS = 6;

}  // namespace

void routeTextToAscii(char *destination, size_t capacity, const char *source) {
  if (destination == nullptr || capacity == 0) return;
  destination[0] = '\0';
  if (source == nullptr) return;
  size_t written = 0;
  const unsigned char *cursor = reinterpret_cast<const unsigned char *>(source);
  while (*cursor != '\0' && written + 1 < capacity) {
    const unsigned char lead = *cursor;
    uint32_t codePoint = 0;
    if (lead < 0x80) {
      codePoint = lead;
      cursor += 1;
    } else if ((lead & 0xE0) == 0xC0 && cursor[1] != '\0') {
      codePoint = ((lead & 0x1F) << 6) | (cursor[1] & 0x3F);
      cursor += 2;
    } else if ((lead & 0xF0) == 0xE0 && cursor[1] != '\0' && cursor[2] != '\0') {
      codePoint = ((lead & 0x0F) << 12) | ((cursor[1] & 0x3F) << 6) |
                  (cursor[2] & 0x3F);
      cursor += 3;
    } else {
      // Rozbitý bajt - přeskoč.
      cursor += 1;
      continue;
    }

    char output;
    if (codePoint < 0x80) {
      output = static_cast<char>(codePoint);
    } else if (codePoint >= 0xC0 && codePoint <= 0xFF) {
      output = LATIN1_MAP[codePoint - 0xC0];
    } else if (codePoint >= 0x100 && codePoint <= 0x17F) {
      output = LATIN_EXTENDED_MAP[codePoint - 0x100];
    } else {
      // Hádat nemá cenu.
      continue;
    }
    if (output < 0x20) continue;
    destination[written++] = output;
  }
  destination[written] = '\0';
}

RouteParseStatus routeParse(const char *payload, float aircraftLatitude,
                            float aircraftLongitude, RouteInfo &info) {
  info = RouteInfo{};
  if (payload == nullptr) return RouteParseStatus::Invalid;
  const char *end = payload + strlen(payload);
  const char *begin = jsonSkipWhitespace(payload, end);
  if (begin >= end || *begin != '{') return RouteParseStatus::Invalid;

  char codes[32];
  jsonCopyTextMember(begin, end, "airport_codes", codes, sizeof(codes));
  // Při nenalezené trase pole "plausible" v odpovědi vůbec není, takže výchozí
  // hodnota musí být false - jinak by neznámá trasa prošla jako věrohodná.
  const bool plausible = jsonReadBoolMember(begin, end, "plausible");
  if (codes[0] == '\0' || strcmp(codes, "unknown") == 0 || !plausible) {
    return RouteParseStatus::NoRoute;
  }

  const JsonValue airportsValue = jsonFindMember(begin, end, "_airports");
  if (!airportsValue.isArray()) return RouteParseStatus::NoRoute;

  Airport airports[MAX_AIRPORTS];
  size_t count = 0;
  JsonArrayCursor items = jsonOpenArray(airportsValue);
  while (count < MAX_AIRPORTS && jsonNextItem(items)) {
    if (items.itemBegin >= items.itemEnd || *items.itemBegin != '{') continue;
    airports[count].begin = items.itemBegin;
    airports[count].end = items.itemEnd;
    jsonReadNumberMember(items.itemBegin, items.itemEnd, "lat",
                         airports[count].latitude);
    jsonReadNumberMember(items.itemBegin, items.itemEnd, "lon",
                         airports[count].longitude);
    ++count;
  }
  // Kódy trasy jsou, ale letiště se v databázi nenašla - není co popsat.
  if (count < 2) return RouteParseStatus::NoRoute;

  // Mezipřistání: u vícenohé trasy se vezme ta sousední dvojice letišť, ke
  // které je letadlo souhrnně nejblíž.
  size_t best = 0;
  if (count > 2) {
    float bestDistance = HUGE_VALF;
    for (size_t index = 0; index + 1 < count; ++index) {
      const float distance =
          haversineKm(aircraftLatitude, aircraftLongitude,
                      airports[index].latitude, airports[index].longitude) +
          haversineKm(aircraftLatitude, aircraftLongitude,
                      airports[index + 1].latitude,
                      airports[index + 1].longitude);
      if (distance < bestDistance) {
        bestDistance = distance;
        best = index;
      }
    }
  }

  airportLabel(info.from, sizeof(info.from), airports[best].begin,
               airports[best].end);
  airportLabel(info.to, sizeof(info.to), airports[best + 1].begin,
               airports[best + 1].end);
  if (info.from[0] == '\0' && info.to[0] == '\0') {
    return RouteParseStatus::NoRoute;
  }
  return RouteParseStatus::Ok;
}
