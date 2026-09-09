#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "RouteParser.h"

namespace {

// Zkrácená, jinak doslovná odpověď z api.adsb.lol.
const char *const REAL_RESPONSE =
    "{\"callsign\":\"CSA1234\",\"airport_codes\":\"LKPR-LTFM\","
    "\"plausible\":true,\"_airports\":["
    "{\"alt_feet\":1247,\"alt_meters\":380.1,\"countryiso2\":\"CZ\","
    "\"iata\":\"PRG\",\"icao\":\"LKPR\",\"lat\":50.1008,"
    "\"location\":\"Prague\",\"lon\":14.26,\"name\":\"Vaclav Havel\"},"
    "{\"alt_feet\":325,\"countryiso2\":\"TR\",\"iata\":\"IST\","
    "\"icao\":\"LTFM\",\"lat\":41.2753,\"location\":\"Istanbul\","
    "\"lon\":28.7519,\"name\":\"Istanbul Airport\"}]}";

void testRealResponse() {
  RouteInfo info;
  assert(routeParse(REAL_RESPONSE, 49.9f, 15.2f, info) == RouteParseStatus::Ok);
  assert(std::string(info.from) == "Prague");
  assert(std::string(info.to) == "Istanbul");
}

void testImplausibleRouteIsRejected() {
  // Server trasu našel, ale k poloze nesedí. Přesně tohle dřív dělalo z letadla
  // nad Prahou let Atény - Istanbul.
  const char *const payload =
      "{\"airport_codes\":\"LGAV-LTFM\",\"plausible\":false,\"_airports\":["
      "{\"iata\":\"ATH\",\"location\":\"Athens\",\"lat\":37.9,\"lon\":23.9},"
      "{\"iata\":\"IST\",\"location\":\"Istanbul\",\"lat\":41.2,\"lon\":28.7}]}";
  RouteInfo info;
  assert(routeParse(payload, 50.1f, 14.4f, info) == RouteParseStatus::NoRoute);
  assert(info.from[0] == '\0');
}

void testMissingPlausibleDefaultsToRejected() {
  // Při nenalezené trase pole "plausible" v odpovědi vůbec není. Chybějící
  // příznak nesmí projít jako věrohodný.
  const char *const payload =
      "{\"airport_codes\":\"LKPR-LTFM\",\"_airports\":["
      "{\"iata\":\"PRG\",\"location\":\"Prague\",\"lat\":50.1,\"lon\":14.2},"
      "{\"iata\":\"IST\",\"location\":\"Istanbul\",\"lat\":41.2,\"lon\":28.7}]}";
  RouteInfo info;
  assert(routeParse(payload, 50.1f, 14.4f, info) == RouteParseStatus::NoRoute);
}

void testUnknownRoute() {
  const char *const payload =
      "{\"callsign\":\"OKXYZ\",\"airport_codes\":\"unknown\"}";
  RouteInfo info;
  assert(routeParse(payload, 50.1f, 14.4f, info) == RouteParseStatus::NoRoute);
}

void testMultiLegPicksNearestSegment() {
  // Praha - Istanbul - Dubaj. Letadlo nad Tureckem letí druhý úsek, takže se
  // musí ukázat Istanbul -> Dubai, ne Prague -> Dubai.
  const char *const payload =
      "{\"airport_codes\":\"LKPR-LTFM-OMDB\",\"plausible\":true,\"_airports\":["
      "{\"iata\":\"PRG\",\"location\":\"Prague\",\"lat\":50.1,\"lon\":14.26},"
      "{\"iata\":\"IST\",\"location\":\"Istanbul\",\"lat\":41.27,\"lon\":28.75},"
      "{\"iata\":\"DXB\",\"location\":\"Dubai\",\"lat\":25.25,\"lon\":55.36}]}";
  RouteInfo info;
  assert(routeParse(payload, 35.0f, 40.0f, info) == RouteParseStatus::Ok);
  assert(std::string(info.from) == "Istanbul");
  assert(std::string(info.to) == "Dubai");

  // Totéž letadlo ještě nad Českem letí první úsek.
  assert(routeParse(payload, 49.5f, 15.0f, info) == RouteParseStatus::Ok);
  assert(std::string(info.from) == "Prague");
  assert(std::string(info.to) == "Istanbul");
}

void testFallsBackToIataWhenCityMissing() {
  // Malá letiště nemají vyplněné město; kód je pořád lepší než prázdno.
  const char *const payload =
      "{\"airport_codes\":\"LKKB-LKPR\",\"plausible\":true,\"_airports\":["
      "{\"iata\":\"KBL\",\"location\":\"\",\"lat\":50.1,\"lon\":14.5},"
      "{\"iata\":\"PRG\",\"location\":\"Prague\",\"lat\":50.1,\"lon\":14.26}]}";
  RouteInfo info;
  assert(routeParse(payload, 50.1f, 14.4f, info) == RouteParseStatus::Ok);
  assert(std::string(info.from) == "KBL");
  assert(std::string(info.to) == "Prague");
}

void testSingleAirportIsNotARoute() {
  const char *const payload =
      "{\"airport_codes\":\"LKPR\",\"plausible\":true,\"_airports\":["
      "{\"iata\":\"PRG\",\"location\":\"Prague\",\"lat\":50.1,\"lon\":14.26}]}";
  RouteInfo info;
  assert(routeParse(payload, 50.1f, 14.4f, info) == RouteParseStatus::NoRoute);
}

void testPrettyPrintedRouteIsAccepted() {
  // Formátovaná odpověď nechá za literálem konec řádku; "plausible" se pak
  // četlo jako false a trasa se nikdy neukázala.
  const char *const payload =
      "{\n"
      "  \"airport_codes\": \"LKPR-LTFM\",\n"
      "  \"_airports\": [\n"
      "    {\"iata\": \"PRG\", \"location\": \"Prague\", \"lat\": 50.1, \"lon\": 14.26},\n"
      "    {\"iata\": \"IST\", \"location\": \"Istanbul\", \"lat\": 41.2, \"lon\": 28.7}\n"
      "  ],\n"
      "  \"plausible\": true\n"
      "}";
  RouteInfo info;
  assert(routeParse(payload, 49.9f, 15.2f, info) == RouteParseStatus::Ok);
  assert(std::string(info.from) == "Prague");
  assert(std::string(info.to) == "Istanbul");
}

void testJsonEscapesAreDecoded() {
  // Escapy z odpovědi se musí rozkódovat, jinak by na displeji stálo doslova
  // "Malmo\\u00f6". Po rozkódování je na řadě složení na ASCII.
  const char *const payload =
      "{\"airport_codes\":\"AAA-BBB\",\"plausible\":true,\"_airports\":["
      "{\"iata\":\"AAA\",\"location\":\"Malm\\u00f6\",\"lat\":55.6,\"lon\":13.0},"
      "{\"iata\":\"BBB\",\"location\":\"Krak\\u00f3w\",\"lat\":50.1,\"lon\":19.8}]}";
  RouteInfo info;
  assert(routeParse(payload, 52.0f, 16.0f, info) == RouteParseStatus::Ok);
  assert(std::string(info.from) == "Malmo");
  assert(std::string(info.to) == "Krakow");
}

void testRejectsGarbage() {
  RouteInfo info;
  assert(routeParse("<html>", 50.0f, 14.0f, info) ==
         RouteParseStatus::Invalid);
  assert(routeParse(nullptr, 50.0f, 14.0f, info) == RouteParseStatus::Invalid);
}

void testUnicodeCityNamesFoldToAscii() {
  // Písmo na displeji umí jen ASCII. Diakritika padá, písmeno pod ní zůstává -
  // jinak by se z Izmiru stalo "-?zmir".
  char output[24];
  routeTextToAscii(output, sizeof(output), "\xC4\xB0zmir");  // U+0130
  assert(std::string(output) == "Izmir");
  routeTextToAscii(output, sizeof(output), "Krak\xC3\xB3w");  // U+00F3
  assert(std::string(output) == "Krakow");
  routeTextToAscii(output, sizeof(output), "M\xC3\xA1laga");  // U+00E1
  assert(std::string(output) == "Malaga");
  routeTextToAscii(output, sizeof(output), "\xC5\x81odz");  // U+0141
  assert(std::string(output) == "Lodz");
  // Znak mimo obě tabulky se zahodí, zbytek textu zůstane čitelný.
  routeTextToAscii(output, sizeof(output), "To\xE4\xB8\xAD kyo");
  assert(std::string(output) == "To kyo");
  // Rozbitý bajt nesmí přetéct ani zacyklit.
  routeTextToAscii(output, sizeof(output), "ab\xFF");
  assert(std::string(output) == "ab");
}

void testAsciiFoldingRespectsCapacity() {
  char output[6];
  routeTextToAscii(output, sizeof(output), "Frankfurt am Main");
  assert(strlen(output) == 5);
  assert(std::string(output) == "Frank");
  // Nulová kapacita se nesmí pokusit zapsat ani ukončovací nulu.
  char guard[2] = {'x', 'y'};
  routeTextToAscii(guard, 0, "abc");
  assert(guard[0] == 'x');
}

void testLongCityNameIsTruncatedInRoute() {
  const char *const payload =
      "{\"airport_codes\":\"AAA-BBB\",\"plausible\":true,\"_airports\":["
      "{\"iata\":\"AAA\",\"location\":\"Frankfurt am Main Flughafen\","
      "\"lat\":50.0,\"lon\":8.5},"
      "{\"iata\":\"BBB\",\"location\":\"Prague\",\"lat\":50.1,\"lon\":14.26}]}";
  RouteInfo info;
  assert(routeParse(payload, 50.0f, 10.0f, info) == RouteParseStatus::Ok);
  assert(strlen(info.from) == sizeof(info.from) - 1);
}

}  // namespace

void testGoodAnswerSettlesImmediately() {
  const RouteAttemptOutcome outcome =
      routeAttemptAfter(RouteParseStatus::Ok, 0);
  assert(outcome.state == PlaneRouteState::Known);
  assert(!outcome.tryAgain);
  assert(outcome.failures == 0);
}

void testNoRouteIsAnAnswerNotAFailure() {
  // Server odpověděl a trasu nemá. Opakovat nemá co přinést a panel to má
  // rovnou napsat.
  const RouteAttemptOutcome outcome =
      routeAttemptAfter(RouteParseStatus::NoRoute, 0);
  assert(outcome.state == PlaneRouteState::Unknown);
  assert(!outcome.tryAgain);
  assert(outcome.failures == 0);
}

void testFirstFailuresKeepLooking() {
  // Chyba sítě není totéž co "trasa neexistuje", takže se ještě zkusí a panel
  // dál píše, že hledá.
  RouteAttemptOutcome outcome = routeAttemptAfter(RouteParseStatus::Invalid, 0);
  assert(outcome.state == PlaneRouteState::Pending);
  assert(outcome.tryAgain);
  assert(outcome.failures == 1);

  outcome = routeAttemptAfter(RouteParseStatus::Invalid, outcome.failures);
  assert(outcome.state == PlaneRouteState::Pending);
  assert(outcome.tryAgain);
  assert(outcome.failures == 2);
}

void testRepeatedFailureGivesUp() {
  // api.adsb.lol vrací na některé volací značky 500 pokaždé. Bez stropu by
  // panel psal "zjišťuji trasu" donekonečna a hodiny se ptaly nadarmo.
  const RouteAttemptOutcome outcome =
      routeAttemptAfter(RouteParseStatus::Invalid, ROUTE_MAX_ATTEMPTS - 1);
  assert(outcome.state == PlaneRouteState::Unknown);
  assert(!outcome.tryAgain);
  assert(outcome.failures == ROUTE_MAX_ATTEMPTS);
}

void testFailureCounterDoesNotWrap() {
  // Počítadlo je jeden bajt. Přetečení by stroj vrátilo na začátek a dotazy by
  // se rozjely nanovo.
  const RouteAttemptOutcome outcome =
      routeAttemptAfter(RouteParseStatus::Invalid, 0xFF);
  assert(outcome.state == PlaneRouteState::Unknown);
  assert(!outcome.tryAgain);
  assert(outcome.failures == 0xFF);
}

void testTrimmedRouteFromOwnFeedParses() {
  // Doslovná odpověď vlastního zdroje z infra/planes: tytéž klíče, jen bez
  // nadmořských výšek, ICAO kódů a jmen zemí, které se na displej nedostanou.
  // Tvar musí zůstat zaměnitelný s adsb.lol, jinak by byl potřeba druhý parser.
  const char *const payload =
      "{\"airport_codes\":\"LEMG-LKMT\",\"plausible\":true,\"_airports\":["
      "{\"iata\":\"AGP\",\"location\":\"M\\u00e1laga\",\"lat\":36.6749,"
      "\"lon\":-4.49911},"
      "{\"iata\":\"OSR\",\"location\":\"Ostrava\",\"lat\":49.696301,"
      "\"lon\":18.111099}]}";
  RouteInfo info;
  const RouteParseStatus status =
      routeParse(payload, 49.1951f, 16.6068f, info);
  assert(status == RouteParseStatus::Ok);
  // Diakritika padá na ASCII, protože písmo displeje nic jiného neumí.
  assert(std::string(info.from) == "Malaga");
  assert(std::string(info.to) == "Ostrava");
}

int main() {
  testTrimmedRouteFromOwnFeedParses();
  testGoodAnswerSettlesImmediately();
  testNoRouteIsAnAnswerNotAFailure();
  testFirstFailuresKeepLooking();
  testRepeatedFailureGivesUp();
  testFailureCounterDoesNotWrap();
  testRealResponse();
  testImplausibleRouteIsRejected();
  testMissingPlausibleDefaultsToRejected();
  testUnknownRoute();
  testMultiLegPicksNearestSegment();
  testFallsBackToIataWhenCityMissing();
  testSingleAirportIsNotARoute();
  testPrettyPrintedRouteIsAccepted();
  testJsonEscapesAreDecoded();
  testRejectsGarbage();
  testUnicodeCityNamesFoldToAscii();
  testAsciiFoldingRespectsCapacity();
  testLongCityNameIsTruncatedInRoute();
  printf("route_parser OK\n");
  return 0;
}
