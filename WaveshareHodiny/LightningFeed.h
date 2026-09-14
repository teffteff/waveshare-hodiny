#pragma once

#include <stddef.h>
#include <stdint.h>

// Zprávy realtime vrstvy blesků z LightningMaps.org (data sítě Blitzortung),
// oddělené od spojení, aby šly testovat na počítači.
//
// Server wss://live2.lightningmaps.org/ po připojení čeká na JSON s výřezem
// mapy a pak posílá tři druhy zpráv:
//   {"cid":893005,"con":151,"port":"8086","time":1789332454.286,"k":5338620.54}
//     - pozdrav s výzvou "k", na kterou prohlížeč odpovídá; bez odpovědi server
//       spojení časem zavře,
//   {"time":1789332479.68}
//     - tep každých deset sekund, i když nic neblýská,
//   {"time":...,"flags":{...},"strokes":[{"time":1789332254344,"lat":46.81,
//     "lon":14.46,"src":2,"srv":1,"id":9731546,"del":1803,"dev":8100}, ...]}
//     - údery; čas je v milisekundách. První dávka po přihlášení nese až pět set
//       úderů z posledních minut, pak chodí po jednotkách.
//
// Výřez, o který si klient řekne, bere LightningMaps jen jako vodítko; údery
// filtruje až vlastní server.

struct LightningStroke {
  float latitude = 0.0f;
  float longitude = 0.0f;
  // Unixový čas úderu v sekundách.
  uint32_t epochSeconds = 0;
  // Identifikátor úderu na serveru. Po znovupřipojení server pošle část
  // posledních úderů znovu a podle id se poznají.
  uint32_t id = 0;
};

enum class LightningMessageKind : uint8_t {
  Invalid,
  // Pozdrav s výzvou; challengeKey je platný.
  Hello,
  // Tep bez úderů.
  Heartbeat,
  Strokes,
};

struct LightningMessageInfo {
  LightningMessageKind kind = LightningMessageKind::Invalid;
  double challengeKey = 0.0;
  bool hasChallenge = false;
  // Úderů ve zprávě a kolik z nich prošlo kontrolou rozsahu.
  uint16_t strokeCount = 0;
  uint16_t validStrokeCount = 0;
  // Vlastní server přidává "live": false, dokud jeho spojení na LightningMaps
  // výřez hodin nepokrývá. Prázdný seznam pak neznamená, že se neblýská.
  bool live = true;
  // Hodnota "time" ze zprávy v sekundách. U vlastního serveru je to kurzor pro
  // další dotaz: server podle něj vrátí jen údery, které přijal později.
  double serverTime = 0.0;
};

// Volá se pro každý platný úder zprávy.
using LightningStrokeSink = void (*)(const LightningStroke &stroke,
                                     void *context);

LightningMessageKind lightningParseMessage(const char *begin, const char *end,
                                           LightningMessageInfo &info,
                                           LightningStrokeSink sink,
                                           void *context);

// Adresa dotazu na vlastní server z infra/lightning: základní adresa z nastavení
// (klidně i se jménem a heslem pro basic_auth) doplněná o kruh a kurzor
// serverTime z předchozí odpovědi (0 = všechno, co server drží). Vrací false,
// když se do výstupu nevejde.
bool lightningFeedBuildUrl(const char *baseUrl, float latitude, float longitude,
                           float radiusKm, double serverCursor,
                           char *output, size_t capacity);

// Vzdálenost dvou bodů po povrchu Země v kilometrech.
float lightningDistanceKm(float latitudeA, float longitudeA, float latitudeB,
                          float longitudeB);
