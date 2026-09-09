#include "AdsbParser.h"

#include <stdio.h>
#include <string.h>

#include "JsonScan.h"

namespace {

// Squawk. Většina serverů ho posílá jako text, některé jako číslo - tam už je
// vedoucí nula pryč, takže se doplní zpět, aby srovnání se "7700" sedělo.
void copySquawk(const char *objectBegin, const char *objectEnd,
                char *destination, size_t capacity) {
  destination[0] = '\0';
  const JsonValue value = jsonFindMember(objectBegin, objectEnd, "squawk");
  if (!value.valid()) return;
  if (value.isString) {
    jsonCopyTextMember(objectBegin, objectEnd, "squawk", destination, capacity);
    return;
  }
  float number = 0.0f;
  if (!jsonReadNumber(value, number)) return;
  if (number < 0.0f || number > 7777.0f) return;
  snprintf(destination, capacity, "%04d", static_cast<int>(number));
}

// Najde pole letadel. adsb.fi ho posílá jako "ac", servery odvozené od
// ADSBexchange jako "aircraft"; brát obě jména stojí nic a přejmenování nahoře
// pak radar nevymaže.
JsonValue findAircraftArray(const char *begin, const char *end) {
  JsonValue value = jsonFindMember(begin, end, "ac");
  if (!value.isArray()) value = jsonFindMember(begin, end, "aircraft");
  if (!value.isArray()) return JsonValue{};
  return value;
}

}  // namespace

AdsbParseOutcome adsbParseAircraft(const char *payload, AdsbAircraft *aircraft,
                                   size_t capacity) {
  AdsbParseOutcome outcome;
  if (payload == nullptr || aircraft == nullptr || capacity == 0) {
    return outcome;
  }
  const char *end = payload + strlen(payload);
  const char *begin = jsonSkipWhitespace(payload, end);
  if (begin >= end || *begin != '{') {
    outcome.status = AdsbParseStatus::NotJson;
    return outcome;
  }

  jsonCopyTextMember(begin, end, "msg", outcome.message,
                     sizeof(outcome.message));

  const JsonValue array = findAircraftArray(begin, end);
  if (!array.valid()) {
    outcome.status = AdsbParseStatus::MissingArray;
    return outcome;
  }

  outcome.status = AdsbParseStatus::Ok;
  JsonArrayCursor items = jsonOpenArray(array);
  while (outcome.count < capacity && jsonNextItem(items)) {
    const char *objectBegin = items.itemBegin;
    const char *objectEnd = items.itemEnd;
    if (objectBegin >= objectEnd || *objectBegin != '{') continue;

    AdsbAircraft &target = aircraft[outcome.count];
    target = AdsbAircraft{};
    // Bez polohy není co kreslit, takže takový záznam propadne rovnou.
    if (!jsonReadNumberMember(objectBegin, objectEnd, "lat", target.latitude) ||
        !jsonReadNumberMember(objectBegin, objectEnd, "lon",
                              target.longitude)) {
      continue;
    }
    // Letadla na zemi se zahazují tady, aby u letiště nesebrala místo těm ve
    // vzduchu. Poznají se podle toho, že místo výšky pošlou text "ground".
    if (jsonTextMemberEquals(objectBegin, objectEnd, "alt_baro", "ground")) {
      continue;
    }

    float value = 0.0f;
    if (jsonReadNumberMember(objectBegin, objectEnd, "track", value) ||
        jsonReadNumberMember(objectBegin, objectEnd, "true_heading", value)) {
      target.trackDeg = value;
      target.hasTrack = true;
    }
    if (jsonReadNumberMember(objectBegin, objectEnd, "alt_baro", value)) {
      target.altitudeFt = value;
      target.hasAltitude = true;
    }
    if (jsonReadNumberMember(objectBegin, objectEnd, "gs", value))
      target.groundSpeedKt = value;
    if (jsonReadNumberMember(objectBegin, objectEnd, "baro_rate", value))
      target.verticalRateFtMin = value;
    jsonCopyTextMember(objectBegin, objectEnd, "hex", target.hex,
                       sizeof(target.hex));
    // Jen callsign, žádná náhrada adresou. Callsign se posílá do API na trasu
    // a hexadecimální adresa se tam normalizuje na platné číslo letu -
    // "a31234" se stane "A31234", tedy Aegean 1234, a letadlo nad Prahou
    // dostalo trasu Atény - Istanbul. Kdo potřebuje něco vypsat, sáhne po
    // adrese sám.
    jsonCopyTextMember(objectBegin, objectEnd, "flight", target.callsign,
                       sizeof(target.callsign));
    // "t" je typ draku ("A320"). "type" je zdroj zprávy ("adsb_icao"), a právě
    // proto se jako náhrada nepoužívá.
    jsonCopyTextMember(objectBegin, objectEnd, "t", target.type,
                       sizeof(target.type));
    jsonCopyTextMember(objectBegin, objectEnd, "r", target.registration,
                       sizeof(target.registration));
    // "desc" je týž typ vypsaný slovy ("AIRBUS A-321neo"). Server ho pošle jen
    // u letadel, která má ve své databázi, takže chybějící pole je normální
    // stav - detail pak ukáže samotnou zkratku.
    jsonCopyTextMember(objectBegin, objectEnd, "desc", target.description,
                       sizeof(target.description));
    copySquawk(objectBegin, objectEnd, target.squawk, sizeof(target.squawk));
    ++outcome.count;
  }
  return outcome;
}

const char *adsbEmergencyCode(const AdsbAircraft &aircraft) {
  if (aircraft.squawk[0] == '\0') return nullptr;
  if (strcmp(aircraft.squawk, ADSB_SQUAWK_HIJACK) == 0)
    return ADSB_SQUAWK_HIJACK;
  if (strcmp(aircraft.squawk, ADSB_SQUAWK_RADIO) == 0)
    return ADSB_SQUAWK_RADIO;
  if (strcmp(aircraft.squawk, ADSB_SQUAWK_EMERGENCY) == 0)
    return ADSB_SQUAWK_EMERGENCY;
  return nullptr;
}

uint8_t adsbEmergencySeverity(const char *code) {
  if (code == nullptr || code[0] == '\0') return 0;
  if (strcmp(code, ADSB_SQUAWK_RADIO) == 0) return 1;
  if (strcmp(code, ADSB_SQUAWK_EMERGENCY) == 0) return 2;
  if (strcmp(code, ADSB_SQUAWK_HIJACK) == 0) return 3;
  return 0;
}

int adsbFindByHex(const AdsbAircraft *aircraft, size_t count, const char *hex) {
  if (aircraft == nullptr || hex == nullptr || hex[0] == '\0') return -1;
  for (size_t index = 0; index < count; ++index) {
    if (strcmp(aircraft[index].hex, hex) == 0) return static_cast<int>(index);
  }
  return -1;
}
