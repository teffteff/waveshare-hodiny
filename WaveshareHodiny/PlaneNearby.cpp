#include "PlaneNearby.h"

#include <math.h>
#include <string.h>

namespace {

constexpr float DEGREES_TO_RADIANS = 0.0174532925f;
// Rezerva dosahu: letadlo v hladině uletí za patnáct vteřin podržení skoro
// čtyři kilometry, takže se k vzdálenosti přidá čtvrtina a tři kilometry.
constexpr float RANGE_MARGIN_FACTOR = 1.25f;
constexpr float RANGE_MARGIN_KM = 3.0f;

}  // namespace

bool planeFilterPasses(const AdsbAircraft &aircraft, const PlaneFilter &filter) {
  if (filter.onlyWithCallsign && aircraft.callsign[0] == '\0') return false;
  if (!filter.altitudeLimited) return true;
  // Neznámou výšku filtr nezahazuje - není podle čeho ji zařadit.
  if (!aircraft.hasAltitude) return true;
  return aircraft.altitudeFt >= static_cast<float>(filter.altitudeMinFt) &&
         aircraft.altitudeFt <= static_cast<float>(filter.altitudeMaxFt);
}

bool planeNearbySeenRecently(const PlaneNearbyMemory &memory, const char *hex,
                             uint32_t now) {
  if (hex == nullptr || hex[0] == '\0') return false;
  for (const PlaneNearbyMemory::Entry &entry : memory.entries) {
    if (entry.hex[0] == '\0') continue;
    if (strncmp(entry.hex, hex, sizeof(entry.hex)) != 0) continue;
    // Rozdíl přes uint32_t přežije i přetečení millis().
    if (now - entry.at < PLANE_NEARBY_FORGET_MS) return true;
  }
  return false;
}

void planeNearbyRemember(PlaneNearbyMemory &memory, const char *hex,
                         uint32_t now) {
  if (hex == nullptr || hex[0] == '\0') return;
  // Letadlo, které už v paměti je, si jen obnoví čas a nezabere druhé místo.
  for (PlaneNearbyMemory::Entry &entry : memory.entries) {
    if (strncmp(entry.hex, hex, sizeof(entry.hex)) == 0) {
      entry.at = now;
      return;
    }
  }
  PlaneNearbyMemory::Entry &entry =
      memory.entries[memory.next % PLANE_NEARBY_MEMORY_SIZE];
  strncpy(entry.hex, hex, sizeof(entry.hex) - 1);
  entry.hex[sizeof(entry.hex) - 1] = '\0';
  entry.at = now;
  memory.next =
      static_cast<uint8_t>((memory.next + 1) % PLANE_NEARBY_MEMORY_SIZE);
}

void planeNearbyDistanceBearing(float homeLatitude, float homeLongitude,
                                float latitude, float longitude,
                                float &distanceKm, float &bearingDeg) {
  const float meanLatitude = (homeLatitude + latitude) * 0.5f;
  const float eastKm = (longitude - homeLongitude) * 111.32f *
                       cosf(meanLatitude * DEGREES_TO_RADIANS);
  const float northKm = (latitude - homeLatitude) * 110.574f;
  distanceKm = hypotf(eastKm, northKm);
  float bearing = atan2f(eastKm, northKm) / DEGREES_TO_RADIANS;
  if (bearing < 0.0f) bearing += 360.0f;
  if (!(bearing >= 0.0f && bearing < 360.0f)) bearing = 0.0f;
  bearingDeg = bearing;
}

float planeNearbyAngleDiff(float a, float b) {
  float difference = fmodf(fabsf(a - b), 360.0f);
  if (!(difference >= 0.0f)) return 180.0f;
  if (difference > 180.0f) difference = 360.0f - difference;
  return difference;
}

float planeNearbyWindowAzimuth(uint8_t direction) {
  return static_cast<float>(direction % PLANE_WINDOW_DIRECTION_COUNT) * 45.0f;
}

bool planeNearbyInWindow(const PlaneNearbySettings &settings, float distanceKm,
                         float bearingDeg) {
  if (!settings.windowEnabled) return false;
  if (distanceKm > static_cast<float>(settings.windowRangeKm)) return false;
  return planeNearbyAngleDiff(bearingDeg,
                              planeNearbyWindowAzimuth(
                                  settings.windowDirection)) <=
         static_cast<float>(settings.windowWidthDeg) * 0.5f;
}

float planeNearbyWatchRadiusKm(const PlaneNearbySettings &settings) {
  float radius = static_cast<float>(settings.radiusKm);
  if (settings.windowEnabled &&
      static_cast<float>(settings.windowRangeKm) > radius)
    radius = static_cast<float>(settings.windowRangeKm);
  return radius;
}

uint8_t planeNearbyRangeIndex(float distanceKm, const uint16_t *rangesKm,
                              size_t rangeCount) {
  if (rangesKm == nullptr || rangeCount == 0) return 0;
  const float needed = distanceKm * RANGE_MARGIN_FACTOR + RANGE_MARGIN_KM;
  for (size_t index = 0; index < rangeCount; ++index) {
    if (static_cast<float>(rangesKm[index]) >= needed)
      return static_cast<uint8_t>(index);
  }
  return static_cast<uint8_t>(rangeCount - 1);
}

bool planeNearbyPick(const AdsbAircraft *list, size_t count,
                     float homeLatitude, float homeLongitude,
                     const PlaneNearbySettings &settings,
                     const PlaneFilter &filter,
                     const PlaneNearbyMemory &memory, uint32_t now,
                     PlaneNearbyPick &pick) {
  pick = PlaneNearbyPick{};
  if (!settings.enabled || list == nullptr) return false;
  const float nearKm = static_cast<float>(settings.radiusKm);
  for (size_t index = 0; index < count; ++index) {
    const AdsbAircraft &aircraft = list[index];
    // Sonda padá na padáku, z okna se na ni nikdo nedívá; letadlo bez výšky
    // může stát na zemi nebo mít vadný odpovídač.
    if (aircraft.sonde || !aircraft.hasAltitude || aircraft.hex[0] == '\0')
      continue;
    if (!planeFilterPasses(aircraft, filter)) continue;
    if (planeNearbySeenRecently(memory, aircraft.hex, now)) continue;
    float distanceKm = 0.0f;
    float bearingDeg = 0.0f;
    planeNearbyDistanceBearing(homeLatitude, homeLongitude, aircraft.latitude,
                               aircraft.longitude, distanceKm, bearingDeg);
    if (!(distanceKm >= 0.0f)) continue;  // NaN z rozbité polohy
    const bool inWindow =
        planeNearbyInWindow(settings, distanceKm, bearingDeg);
    if (!inWindow && distanceKm > nearKm) continue;
    // Letadlo v okně porazí každé mimo okno; uvnitř téže skupiny vyhrává
    // bližší.
    const bool better =
        pick.index < 0 || (inWindow && !pick.inWindow) ||
        (inWindow == pick.inWindow && distanceKm < pick.distanceKm);
    if (!better) continue;
    pick.index = static_cast<int>(index);
    pick.distanceKm = distanceKm;
    pick.bearingDeg = bearingDeg;
    pick.inWindow = inWindow;
  }
  return pick.index >= 0;
}
