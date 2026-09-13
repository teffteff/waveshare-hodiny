#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "RssFeedUrl.h"

namespace {

constexpr float LATITUDE = 49.1951f;
constexpr float LONGITUDE = 16.6068f;

void testAddressWithoutTokensIsUnchanged() {
  // Cizí kanál se o polohu neprosí, takže ji ani nedostane.
  char url[RSS_FEED_URL_CAPACITY] = "";
  assert(rssFeedBuildUrl("https://www.irozhlas.cz/rss/irozhlas?x=1", "Brno",
                         LATITUDE, LONGITUDE, url, sizeof(url)));
  assert(std::string(url) == "https://www.irozhlas.cz/rss/irozhlas?x=1");
}

void testTokensAreReplaced() {
  char url[RSS_FEED_URL_CAPACITY] = "";
  assert(rssFeedBuildUrl(
      "https://server.test/top.xml?city={city}&lat={lat}&lon={lon}", "Brno",
      LATITUDE, LONGITUDE, url, sizeof(url)));
  assert(std::string(url) ==
         "https://server.test/top.xml?city=Brno&lat=49.19510&lon=16.60680");
}

void testCityIsPercentEncoded() {
  // Diakritika jako bajty UTF-8, mezera i "&" zakódované, aby jméno místa
  // nerozbilo dotaz ani nepřidalo vlastní parametr.
  char url[RSS_FEED_URL_CAPACITY] = "";
  assert(rssFeedBuildUrl("https://server.test/top.xml?city={city}",
                         "Ústí nad Labem&x=1", LATITUDE, LONGITUDE, url,
                         sizeof(url)));
  assert(std::string(url) ==
         "https://server.test/top.xml?city=%C3%9Ast%C3%AD%20nad%20Labem%26x%3D1");
}

void testNegativeCoordinatesAndRepeatedTokens() {
  char url[RSS_FEED_URL_CAPACITY] = "";
  assert(rssFeedBuildUrl("https://server.test/{lat}/{lon}?again={lat}",
                         nullptr, -33.8688f, -151.2093f, url, sizeof(url)));
  assert(std::string(url) ==
         "https://server.test/-33.86880/-151.20930?again=-33.86880");
}

void testUnknownBracesStay() {
  char url[RSS_FEED_URL_CAPACITY] = "";
  assert(rssFeedBuildUrl("https://server.test/{country}?c={city", "Brno",
                         LATITUDE, LONGITUDE, url, sizeof(url)));
  assert(std::string(url) == "https://server.test/{country}?c={city");
}

void testTooLongResultIsRefusedWhole() {
  // Šablona se vejde (39 znaků), ale po doplnění Českých Budějovic už ne.
  char url[40] = "";
  assert(!rssFeedBuildUrl("https://server.test/top.xml?city={city}",
                          "České Budějovice", LATITUDE, LONGITUDE, url,
                          sizeof(url)));
  assert(url[0] == '\0');
  // Přesně na hranu: 39 znaků a nula se vejdou, o znak víc už ne.
  char edge[40] = "";
  const std::string fits(39, 'a');
  assert(rssFeedBuildUrl(fits.c_str(), "", 0, 0, edge, sizeof(edge)));
  assert(std::string(edge) == fits);
  const std::string tooLong(40, 'a');
  assert(!rssFeedBuildUrl(tooLong.c_str(), "", 0, 0, edge, sizeof(edge)));
}

void testWorstCaseFitsCapacity() {
  // Nejdelší adresa z nastavení (191 znaků) se jménem místa o 63 bajtech,
  // z nichž se každý kóduje na tři znaky, a se souřadnicemi nejdelšího tvaru.
  const std::string prefix = "https://server.test/top.xml?lat={lat}&lon={lon}&q=";
  const std::string address =
      prefix + std::string(191 - prefix.size() - 7, 'x') + "&{city}";
  assert(address.size() == 191);
  const std::string city(63, '&');
  char url[RSS_FEED_URL_CAPACITY] = "";
  assert(rssFeedBuildUrl(address.c_str(), city.c_str(), -89.99999f,
                         -179.99999f, url, sizeof(url)));
}

}  // namespace

int main() {
  testAddressWithoutTokensIsUnchanged();
  testTokensAreReplaced();
  testCityIsPercentEncoded();
  testNegativeCoordinatesAndRepeatedTokens();
  testUnknownBracesStay();
  testTooLongResultIsRefusedWhole();
  testWorstCaseFitsCapacity();
  printf("rss_feed_url OK\n");
  return 0;
}
