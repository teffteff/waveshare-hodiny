#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "PlaneFeedUrl.h"

namespace {

// Brno a dosah 100 km, tedy 53,9 námořní míle - přesně to, co posílá radar
// při největším nastaveném dosahu.
constexpr float LATITUDE = 49.1951f;
constexpr float LONGITUDE = 16.6068f;
constexpr float DISTANCE_NM = 53.9f;

void testEmptyAddressAsksAdsbDirectly() {
  char url[PLANE_FEED_URL_CAPACITY] = "";
  assert(planeFeedBuildUrl("", LATITUDE, LONGITUDE, DISTANCE_NM, url,
                           sizeof(url)));
  assert(std::string(url) ==
         "https://opendata.adsb.fi/api/v3/lat/49.19510/lon/16.60680/dist/53.9");
  // nullptr znamená totéž co prázdná adresa: nastavení ji nemá vyplněnou.
  char again[PLANE_FEED_URL_CAPACITY] = "";
  assert(planeFeedBuildUrl(nullptr, LATITUDE, LONGITUDE, DISTANCE_NM, again,
                           sizeof(again)));
  assert(std::string(again) == std::string(url));
}

void testOwnFeedGetsQueryParameters() {
  char url[PLANE_FEED_URL_CAPACITY] = "";
  assert(planeFeedBuildUrl("https://server.test/planes.json", LATITUDE,
                           LONGITUDE, DISTANCE_NM, url, sizeof(url)));
  assert(std::string(url) ==
         "https://server.test/planes.json?lat=49.19510&lon=16.60680&dist=53.9");
}

void testExistingQueryIsKept() {
  // Adresa už dotaz nese; parametry se musí připojit za něj, ne ho nahradit.
  char url[PLANE_FEED_URL_CAPACITY] = "";
  assert(planeFeedBuildUrl("https://server.test/api?zdroj=letadla", LATITUDE,
                           LONGITUDE, DISTANCE_NM, url, sizeof(url)));
  assert(std::string(url) ==
         "https://server.test/api?zdroj=letadla&lat=49.19510&lon=16.60680"
         "&dist=53.9");
}

void testCredentialsInAddressSurvive() {
  // Adresu s heslem umí HTTPClient sám; sestavení ji nesmí rozbít.
  char url[PLANE_FEED_URL_CAPACITY] = "";
  assert(planeFeedBuildUrl("https://hodiny:tajne@server.test/planes.json",
                           LATITUDE, LONGITUDE, DISTANCE_NM, url, sizeof(url)));
  assert(std::string(url) ==
         "https://hodiny:tajne@server.test/planes.json?lat=49.19510"
         "&lon=16.60680&dist=53.9");
}

void testTooLongAddressIsRefusedWhole() {
  // Useknutá adresa by vedla jinam, nebo by se ptala na jiný dosah, než jaký
  // je na displeji. Radši žádná.
  char url[48] = "";
  assert(!planeFeedBuildUrl("https://server.test/planes.json", LATITUDE,
                            LONGITUDE, DISTANCE_NM, url, sizeof(url)));
  assert(url[0] == '\0');
}

void testShortestRangeKeepsOneDecimal() {
  // Deset kilometrů je 5,4 námořní míle. Kdyby se dosah zaokrouhlil na celé
  // míle, kruh na displeji a dotaz na server by si přestaly odpovídat.
  char url[PLANE_FEED_URL_CAPACITY] = "";
  assert(planeFeedBuildUrl("", LATITUDE, LONGITUDE, 5.4f, url, sizeof(url)));
  assert(std::string(url).find("/dist/5.4") != std::string::npos);
}

void testRouteWithoutOwnFeedGoesToAdsbLol() {
  char url[PLANE_FEED_URL_CAPACITY] = "";
  assert(planeFeedBuildRouteUrl("", "CSA1234", LATITUDE, LONGITUDE, url,
                                sizeof(url)));
  assert(std::string(url) ==
         "https://api.adsb.lol/api/0/route/CSA1234/49.1951/16.6068");
}

void testRouteThroughOwnFeedUsesTheSameAddress() {
  // Jedna adresa v nastavení obstará polohy i trasy; liší se jen parametry.
  char url[PLANE_FEED_URL_CAPACITY] = "";
  assert(planeFeedBuildRouteUrl("https://server.test/planes.json", "CSA1234",
                                LATITUDE, LONGITUDE, url, sizeof(url)));
  assert(std::string(url) ==
         "https://server.test/planes.json?lat=49.1951&lon=16.6068"
         "&route=CSA1234");
}

void testRouteWithoutCallsignIsRefused() {
  // Bez volací značky se na trasu ptát nedá: to API čte hexadecimální adresu
  // jako číslo letu a odpovědělo by trasou cizího letadla.
  char url[PLANE_FEED_URL_CAPACITY] = "";
  assert(!planeFeedBuildRouteUrl("", "", LATITUDE, LONGITUDE, url,
                                 sizeof(url)));
  assert(url[0] == '\0');
  assert(!planeFeedBuildRouteUrl("https://server.test/planes.json", nullptr,
                                 LATITUDE, LONGITUDE, url, sizeof(url)));
  assert(url[0] == '\0');
}

}  // namespace

int main() {
  testRouteWithoutOwnFeedGoesToAdsbLol();
  testRouteThroughOwnFeedUsesTheSameAddress();
  testRouteWithoutCallsignIsRefused();
  testEmptyAddressAsksAdsbDirectly();
  testOwnFeedGetsQueryParameters();
  testExistingQueryIsKept();
  testCredentialsInAddressSurvive();
  testTooLongAddressIsRefusedWhole();
  testShortestRangeKeepsOneDecimal();
  printf("plane_feed_url OK\n");
  return 0;
}
