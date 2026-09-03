#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#include "RssParser.h"

namespace {

std::string translit(const std::string &source) {
  char buffer[RSS_TITLE_LENGTH];
  rssTransliterate(source.c_str(), source.size(), buffer, sizeof(buffer));
  return std::string(buffer);
}

int64_t date(const std::string &text) {
  int64_t value = 0;
  assert(rssParseDate(text.c_str(), text.size(), value));
  return value;
}

void testTransliteration() {
  // Celá česká abeceda s diakritikou, velká i malá.
  assert(translit("ÁČĎÉĚÍŇÓŘŠŤÚŮÝŽ") == "ACDEEINORSTUUYZ");
  assert(translit("áčďéěíňóřšťúůýž") == "acdeeinorstuuyz");
  // Skutečný titulek z kanálu iROZHLAS.cz.
  assert(translit("Židovská obec se soudí o bývalý spolkový dům v Brně") ==
         "Zidovska obec se soudi o byvaly spolkovy dum v Brne");
  // Sousední jazyky, které se ve zpravodajství objevují.
  assert(translit("Wałęsa Gdańsk Košice Ľuboš Müller Ærø") ==
         "Walesa Gdansk Kosice Lubos Muller AEro");
  assert(translit("Straße") == "Strasse");

  // Uvozovky, které iROZHLAS.cz opravdu sází (U+201A a U+2018), a pomlčky.
  assert(translit("‚Stranicka politika‘") == "'Stranicka politika'");
  assert(translit("„Citace“ – zdroj…") ==
         "\"Citace\" - zdroj...");

  // XML entity, včetně číselných tvarů kódujících diakritiku.
  assert(translit("AC&amp;C &lt;tag&gt; &quot;x&quot;") == "AC&C <tag> \"x\"");
  assert(translit("&#345;eka a &#x158;eka") == "reka a Reka");
  assert(translit("&nedefinovana; pryc") == "pryc");

  // Bílé znaky: odsazení z XML se sloučí a ořízne.
  assert(translit("\n   Dva    slova \t\n ") == "Dva slova");
  // Nedělitelná mezera se chová jako mezera, ne jako zahozený znak.
  assert(translit("30 stupnu") == "30 stupnu");
  // Znaky, které písmo má, projdou beze změny.
  assert(translit("30 °C") == "30 °C");

  // Přetečení nesmí přepsat cizí paměť ani zapomenout koncovou nulu.
  char small[8];
  const char *longText = "Velmi dlouhy titulek";
  const size_t written =
      rssTransliterate(longText, strlen(longText), small, sizeof(small));
  assert(written == sizeof(small) - 1);
  assert(small[sizeof(small) - 1] == '\0');
  assert(strcmp(small, "Velmi d") == 0);

  // Poškozené UTF-8 nesmí zacyklit ani nic zapsat navíc.
  const char broken[] = {'a', static_cast<char>(0xC3), '\0'};
  assert(translit(std::string(broken, 2)) == "a");
}

void testDates() {
  // RFC 822 se středoevropským posunem: 16:42 +0200 je 14:42 UTC.
  assert(date("Wed, 02 Sep 2026 16:42:00 +0200") == 1788360120);
  // Stejný okamžik zapsaný bez názvu dne, bez sekund a v UTC.
  assert(date("02 Sep 2026 14:42 GMT") == 1788360120);
  assert(date("Wed, 02 Sep 2026 14:42:00 UT") == 1788360120);
  // Atom, ISO 8601 v obou zápisech zóny.
  assert(date("2026-09-02T16:42:00+02:00") == 1788360120);
  assert(date("2026-09-02T14:42:00Z") == 1788360120);
  assert(date("2026-09-02T14:42:00.123Z") == 1788360120);
  // Dvouciferný rok podle RFC 822.
  assert(date("Wed, 02 Sep 26 14:42:00 +0000") == 1788360120);
  // Přestupný rok a konec roku, kde se počítání dnů nejspíš zlomí.
  assert(date("Mon, 29 Feb 2016 00:00:00 +0000") == 1456704000);
  assert(date("Thu, 31 Dec 1970 23:59:59 +0000") == 31535999);

  int64_t ignored = 0;
  assert(!rssParseDate("", 0, ignored));
  assert(!rssParseDate("vcera", 5, ignored));
  assert(!rssParseDate("Wed, 02 Xxx 2026 16:42:00 +0200", 31, ignored));
  // Neplatný měsíc se musí odmítnout, ne tiše spočítat.
  assert(!rssParseDate("2026-13-02T00:00:00Z", 20, ignored));
}

void testFeed() {
  static const char PAYLOAD[] =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<rss version=\"2.0\"><channel>\n"
      " <title>iROZHLAS.cz</title>\n"
      " <item>\n"
      "  <title>Starsi zprava</title>\n"
      "  <pubDate>Wed, 02 Sep 2026 10:00:00 +0200</pubDate>\n"
      " </item>\n"
      " <item>\n"
      "  <title><![CDATA[Nejnovejsi zpr\xc3\xa1va & spol]]></title>\n"
      "  <pubDate>Wed, 02 Sep 2026 16:42:00 +0200</pubDate>\n"
      " </item>\n"
      "</channel></rss>";
  RssFeed feed;
  char error[160];
  assert(rssParseFeed(PAYLOAD, sizeof(PAYLOAD) - 1, 5, feed, error,
                      sizeof(error)));
  assert(strcmp(feed.channelTitle, "iROZHLAS.cz") == 0);
  assert(feed.count == 2);
  // Nejnovější zpráva musí být první i tehdy, když v kanálu byla druhá.
  assert(strcmp(feed.items[0].title, "Nejnovejsi zprava & spol") == 0);
  assert(strcmp(feed.items[1].title, "Starsi zprava") == 0);
  assert(feed.items[0].publishedAt > feed.items[1].publishedAt);
  assert(feed.items[0].timeAvailable && feed.items[1].timeAvailable);

  // maximumItems musí opravdu omezit počet a nechat ty nejnovější.
  assert(rssParseFeed(PAYLOAD, sizeof(PAYLOAD) - 1, 1, feed, error,
                      sizeof(error)));
  assert(feed.count == 1);
  assert(strcmp(feed.items[0].title, "Nejnovejsi zprava & spol") == 0);

  // Atom s atributem na značce, prefixem jmenného prostoru a bez data.
  static const char ATOM[] =
      "<feed xmlns=\"http://www.w3.org/2005/Atom\">"
      "<title type=\"text\">Atom kanal</title>"
      "<entry><title>Prvni</title>"
      "<updated>2026-09-02T14:42:00Z</updated></entry>"
      "<entry><title>Druha</title></entry>"
      "</feed>";
  assert(rssParseFeed(ATOM, sizeof(ATOM) - 1, 5, feed, error, sizeof(error)));
  assert(strcmp(feed.channelTitle, "Atom kanal") == 0);
  assert(feed.count == 2);
  // Datovaná zpráva jde před nedatovanou.
  assert(strcmp(feed.items[0].title, "Prvni") == 0);
  assert(feed.items[0].timeAvailable);
  assert(!feed.items[1].timeAvailable);

  // Kanál bez data si musí udržet pořadí dokumentu.
  static const char UNDATED[] =
      "<rss><channel><title>Bez data</title>"
      "<item><title>Prvni</title></item>"
      "<item><title>Druha</title></item>"
      "<item><title>Treti</title></item>"
      "</channel></rss>";
  assert(rssParseFeed(UNDATED, sizeof(UNDATED) - 1, 5, feed, error,
                      sizeof(error)));
  assert(feed.count == 3);
  assert(strcmp(feed.items[0].title, "Prvni") == 0);
  assert(strcmp(feed.items[2].title, "Treti") == 0);

  // Odpovědi, které nejsou kanálem, musí selhat se srozumitelnou hláškou.
  static const char HTML[] = "<!doctype html><html><body>Ahoj</body></html>";
  assert(!rssParseFeed(HTML, sizeof(HTML) - 1, 5, feed, error, sizeof(error)));
  assert(strlen(error) > 0);
  static const char PLAIN[] = "404 Not Found";
  assert(!rssParseFeed(PLAIN, sizeof(PLAIN) - 1, 5, feed, error,
                       sizeof(error)));
  assert(!rssParseFeed(nullptr, 0, 5, feed, error, sizeof(error)));

  // Nedokončená značka nesmí přečíst za konec vstupu.
  static const char TRUNCATED[] = "<rss><channel><item><title>Nedok";
  assert(!rssParseFeed(TRUNCATED, sizeof(TRUNCATED) - 1, 5, feed, error,
                       sizeof(error)));
}

// Volitelný běh proti staženému kanálu: test_rss_parser <soubor.xml>.
int testLiveFeed(const char *path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  const std::string payload((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
  RssFeed feed;
  char error[160];
  if (!rssParseFeed(payload.c_str(), payload.size(), 5, feed, error,
                    sizeof(error))) {
    printf("selhalo: %s\n", error);
    return 1;
  }
  printf("kanal: %s\n", feed.channelTitle);
  for (size_t index = 0; index < feed.count; ++index) {
    printf("  [%lld] %s\n",
           static_cast<long long>(feed.items[index].publishedAt),
           feed.items[index].title);
    // Titulek musí projít beze zbytku diakritiky.
    for (const char *scan = feed.items[index].title; *scan != '\0'; ++scan) {
      assert(static_cast<unsigned char>(*scan) < 0x80);
    }
    assert(feed.items[index].timeAvailable);
  }
  assert(feed.count == 5);
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc == 2) return testLiveFeed(argv[1]);
  testTransliteration();
  testDates();
  testFeed();
  return 0;
}
