#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "PlaneNearby.h"

namespace {

// Ondřejov, výchozí poloha hodin.
constexpr float HOME_LATITUDE = 49.90461f;
constexpr float HOME_LONGITUDE = 14.7842f;
constexpr float KM_PER_DEGREE_LATITUDE = 110.574f;
constexpr uint16_t RANGES_KM[] = {10, 25, 50, 100};

bool near(float a, float b, float tolerance) {
  return std::fabs(a - b) <= tolerance;
}

// Letadlo v dané vzdálenosti a azimutu od domova.
AdsbAircraft aircraftAt(const char *hex, float distanceKm, float bearingDeg,
                        float altitudeFt = 3000.0f) {
  AdsbAircraft aircraft;
  const float radians = bearingDeg * 0.0174532925f;
  const float northKm = distanceKm * std::cos(radians);
  const float eastKm = distanceKm * std::sin(radians);
  aircraft.latitude = HOME_LATITUDE + northKm / KM_PER_DEGREE_LATITUDE;
  const float meanLatitude = (HOME_LATITUDE + aircraft.latitude) * 0.5f;
  aircraft.longitude =
      HOME_LONGITUDE +
      eastKm / (111.32f * std::cos(meanLatitude * 0.0174532925f));
  aircraft.altitudeFt = altitudeFt;
  aircraft.hasAltitude = true;
  strncpy(aircraft.hex, hex, sizeof(aircraft.hex) - 1);
  strncpy(aircraft.callsign, "CSA1", sizeof(aircraft.callsign) - 1);
  return aircraft;
}

PlaneNearbySettings enabledSettings() {
  PlaneNearbySettings settings;
  settings.enabled = true;
  return settings;
}

void testDistanceAndBearing() {
  float distance = 0.0f;
  float bearing = 0.0f;
  // Bod 10 km na sever, na východ, na jih a na západ.
  const float points[4] = {0.0f, 90.0f, 180.0f, 270.0f};
  for (float expected : points) {
    const AdsbAircraft aircraft = aircraftAt("aaaaaa", 10.0f, expected);
    planeNearbyDistanceBearing(HOME_LATITUDE, HOME_LONGITUDE,
                               aircraft.latitude, aircraft.longitude, distance,
                               bearing);
    assert(near(distance, 10.0f, 0.05f));
    assert(planeNearbyAngleDiff(bearing, expected) < 0.5f);
  }
  // Doma: nula a azimut v mezích.
  planeNearbyDistanceBearing(HOME_LATITUDE, HOME_LONGITUDE, HOME_LATITUDE,
                             HOME_LONGITUDE, distance, bearing);
  assert(distance == 0.0f);
  assert(bearing >= 0.0f && bearing < 360.0f);
}

void testAngleDiffWrapsAround() {
  assert(near(planeNearbyAngleDiff(350.0f, 10.0f), 20.0f, 0.01f));
  assert(near(planeNearbyAngleDiff(10.0f, 350.0f), 20.0f, 0.01f));
  assert(near(planeNearbyAngleDiff(0.0f, 180.0f), 180.0f, 0.01f));
  assert(near(planeNearbyAngleDiff(90.0f, 90.0f), 0.0f, 0.01f));
  assert(near(planeNearbyAngleDiff(-30.0f, 30.0f), 60.0f, 0.01f));
  assert(near(planeNearbyAngleDiff(720.0f + 5.0f, 355.0f), 10.0f, 0.01f));
}

void testWindowSector() {
  PlaneNearbySettings settings = enabledSettings();
  settings.windowEnabled = true;
  settings.windowDirection = 0;  // sever
  settings.windowWidthDeg = 90;
  settings.windowRangeKm = 15;
  assert(planeNearbyWindowAzimuth(0) == 0.0f);
  assert(planeNearbyWindowAzimuth(2) == 90.0f);
  assert(planeNearbyWindowAzimuth(7) == 315.0f);
  // Přes přechod 360 -> 0: okno na sever s šířkou 90 sahá od 315 do 45.
  assert(planeNearbyInWindow(settings, 10.0f, 350.0f));
  assert(planeNearbyInWindow(settings, 10.0f, 44.0f));
  assert(planeNearbyInWindow(settings, 10.0f, 316.0f));
  assert(!planeNearbyInWindow(settings, 10.0f, 46.0f));
  assert(!planeNearbyInWindow(settings, 10.0f, 300.0f));
  assert(!planeNearbyInWindow(settings, 10.0f, 180.0f));
  // Za dohledem okna nic.
  assert(!planeNearbyInWindow(settings, 16.0f, 0.0f));
  // Okno na severozápad, úzké.
  settings.windowDirection = 7;
  settings.windowWidthDeg = 20;
  assert(planeNearbyInWindow(settings, 5.0f, 320.0f));
  assert(!planeNearbyInWindow(settings, 5.0f, 330.0f));
  // Vypnuté okno neplatí nikde.
  settings.windowEnabled = false;
  assert(!planeNearbyInWindow(settings, 5.0f, 315.0f));
}

void testNearestWithinRadius() {
  const PlaneNearbySettings settings = enabledSettings();
  PlaneNearbyMemory memory;
  AdsbAircraft list[3] = {aircraftAt("aaaaa1", 4.0f, 90.0f),
                          aircraftAt("aaaaa2", 2.0f, 200.0f),
                          aircraftAt("aaaaa3", 6.0f, 10.0f)};
  PlaneNearbyPick pick;
  assert(planeNearbyPick(list, 3, HOME_LATITUDE, HOME_LONGITUDE, settings,
                         PlaneFilter{}, memory, 1000, pick));
  assert(pick.index == 1);
  assert(!pick.inWindow);
  assert(near(pick.distanceKm, 2.0f, 0.05f));
  // Nic v okruhu: nic.
  AdsbAircraft far[1] = {aircraftAt("aaaaa4", 6.0f, 0.0f)};
  assert(!planeNearbyPick(far, 1, HOME_LATITUDE, HOME_LONGITUDE, settings,
                          PlaneFilter{}, memory, 1000, pick));
  assert(pick.index == -1);
  // Vypnutá funkce nevybere nic ani nad hlavou.
  PlaneNearbySettings off;
  AdsbAircraft overhead[1] = {aircraftAt("aaaaa5", 0.5f, 0.0f)};
  assert(!planeNearbyPick(overhead, 1, HOME_LATITUDE, HOME_LONGITUDE, off,
                          PlaneFilter{}, memory, 1000, pick));
}

void testWindowWinsOverCloser() {
  PlaneNearbySettings settings = enabledSettings();
  settings.windowEnabled = true;
  settings.windowDirection = 2;  // východ
  settings.windowWidthDeg = 60;
  settings.windowRangeKm = 15;
  PlaneNearbyMemory memory;
  // Za domem 3 km, před oknem 12 km (dál než okruh 5 km) a 9 km.
  AdsbAircraft list[3] = {aircraftAt("bbbbb1", 3.0f, 270.0f),
                          aircraftAt("bbbbb2", 12.0f, 95.0f),
                          aircraftAt("bbbbb3", 9.0f, 80.0f)};
  PlaneNearbyPick pick;
  assert(planeNearbyPick(list, 3, HOME_LATITUDE, HOME_LONGITUDE, settings,
                         PlaneFilter{}, memory, 0, pick));
  assert(pick.index == 2);
  assert(pick.inWindow);
  // Bez okna vyhraje to za domem.
  settings.windowEnabled = false;
  assert(planeNearbyPick(list, 3, HOME_LATITUDE, HOME_LONGITUDE, settings,
                         PlaneFilter{}, memory, 0, pick));
  assert(pick.index == 0);
  assert(!pick.inWindow);
}

void testSkipsSondesAndUnknownAltitudeAndFilter() {
  const PlaneNearbySettings settings = enabledSettings();
  PlaneNearbyMemory memory;
  AdsbAircraft list[4] = {aircraftAt("ccccc1", 1.0f, 0.0f),
                          aircraftAt("ccccc2", 1.5f, 0.0f),
                          aircraftAt("ccccc3", 2.0f, 0.0f, 40000.0f),
                          aircraftAt("ccccc4", 3.0f, 0.0f)};
  list[0].sonde = true;
  list[1].hasAltitude = false;
  list[3].callsign[0] = '\0';
  PlaneFilter filter;
  filter.altitudeLimited = true;
  filter.altitudeMinFt = 0;
  filter.altitudeMaxFt = 10000;
  PlaneNearbyPick pick;
  // Sonda, bez výšky, nad filtrem: zbývá čtvrté.
  assert(planeNearbyPick(list, 4, HOME_LATITUDE, HOME_LONGITUDE, settings,
                         filter, memory, 0, pick));
  assert(pick.index == 3);
  // Skrytá letadla bez volací značky: nezbývá nic.
  filter.onlyWithCallsign = true;
  assert(!planeNearbyPick(list, 4, HOME_LATITUDE, HOME_LONGITUDE, settings,
                          filter, memory, 0, pick));
  // Bez výškového filtru projde i to ve 40 000 stopách.
  filter.altitudeLimited = false;
  assert(planeNearbyPick(list, 4, HOME_LATITUDE, HOME_LONGITUDE, settings,
                         filter, memory, 0, pick));
  assert(pick.index == 2);
}

void testMemoryForgetsAfterFifteenMinutes() {
  const PlaneNearbySettings settings = enabledSettings();
  PlaneNearbyMemory memory;
  AdsbAircraft list[2] = {aircraftAt("ddddd1", 1.0f, 0.0f),
                          aircraftAt("ddddd2", 4.0f, 0.0f)};
  PlaneNearbyPick pick;
  const uint32_t start = 0xfffff000u;  // přes přetečení millis()
  assert(planeNearbyPick(list, 2, HOME_LATITUDE, HOME_LONGITUDE, settings,
                         PlaneFilter{}, memory, start, pick));
  assert(pick.index == 0);
  planeNearbyRemember(memory, list[0].hex, start);
  // Ukázané letadlo neblokuje další.
  assert(planeNearbyPick(list, 2, HOME_LATITUDE, HOME_LONGITUDE, settings,
                         PlaneFilter{}, memory, start + 1000, pick));
  assert(pick.index == 1);
  planeNearbyRemember(memory, list[1].hex, start + 1000);
  assert(!planeNearbyPick(list, 2, HOME_LATITUDE, HOME_LONGITUDE, settings,
                          PlaneFilter{}, memory, start + 60000, pick));
  // Po patnácti minutách se první zapomene.
  assert(planeNearbySeenRecently(memory, "ddddd1",
                                 start + PLANE_NEARBY_FORGET_MS - 1));
  assert(!planeNearbySeenRecently(memory, "ddddd1",
                                  start + PLANE_NEARBY_FORGET_MS));
  assert(planeNearbyPick(list, 2, HOME_LATITUDE, HOME_LONGITUDE, settings,
                         PlaneFilter{}, memory, start + PLANE_NEARBY_FORGET_MS,
                         pick));
  assert(pick.index == 0);
}

void testMemoryIsRingOfSixteen() {
  PlaneNearbyMemory memory;
  char hex[8];
  for (int index = 0; index < 17; ++index) {
    snprintf(hex, sizeof(hex), "e%05d", index);
    planeNearbyRemember(memory, hex, 100);
  }
  // Sedmnácté vytlačilo první, ostatní zůstala.
  assert(!planeNearbySeenRecently(memory, "e00000", 200));
  assert(planeNearbySeenRecently(memory, "e00001", 200));
  assert(planeNearbySeenRecently(memory, "e00016", 200));
  // Opakované zapamatování nezabere další místo.
  planeNearbyRemember(memory, "e00016", 300);
  assert(planeNearbySeenRecently(memory, "e00001", 300));
  assert(!planeNearbySeenRecently(memory, "", 300));
}

void testWatchRadiusAndRange() {
  PlaneNearbySettings settings = enabledSettings();
  assert(planeNearbyWatchRadiusKm(settings) == 5.0f);
  settings.windowEnabled = true;
  assert(planeNearbyWatchRadiusKm(settings) == 15.0f);
  settings.windowRangeKm = 3;
  assert(planeNearbyWatchRadiusKm(settings) == 5.0f);

  assert(planeNearbyRangeIndex(1.0f, RANGES_KM, 4) == 0);
  assert(planeNearbyRangeIndex(5.0f, RANGES_KM, 4) == 0);
  assert(planeNearbyRangeIndex(6.0f, RANGES_KM, 4) == 1);
  assert(planeNearbyRangeIndex(15.0f, RANGES_KM, 4) == 1);
  assert(planeNearbyRangeIndex(30.0f, RANGES_KM, 4) == 2);
  assert(planeNearbyRangeIndex(50.0f, RANGES_KM, 4) == 3);
  assert(planeNearbyRangeIndex(500.0f, RANGES_KM, 4) == 3);
}

}  // namespace

int main() {
  testDistanceAndBearing();
  testAngleDiffWrapsAround();
  testWindowSector();
  testNearestWithinRadius();
  testWindowWinsOverCloser();
  testSkipsSondesAndUnknownAltitudeAndFilter();
  testMemoryForgetsAfterFifteenMinutes();
  testMemoryIsRingOfSixteen();
  testWatchRadiusAndRange();
  std::puts("plane_nearby OK");
  return 0;
}
