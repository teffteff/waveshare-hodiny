#pragma once

#include <stddef.h>
#include <stdint.h>

// Rozbor odpovědi na trasu letu z api.adsb.lol, oddělený od stahování, aby šel
// testovat na počítači. Převzato z projektu MeteoPlaneRadar a přepsané z
// ArduinoJsonu na společný průchod v JsonScan.h.
//
// Endpoint (bez klíče, bez registrace):
//   GET https://api.adsb.lol/api/0/route/{callsign}/{lat}/{lon}
//
// Spolu s callsignem se posílá i poloha letadla a server podle ní vrací
// příznak "plausible": počítá kolmou vzdálenost polohy od ortodromy mezi
// letišti trasy s tolerancí max(50 NM, 20 % délky trasy). Trasa, která k poloze
// nesedí, se tím odfiltruje ještě na serveru - právě kvůli tomu se letadlu nad
// Prahou dřív ukazovala trasa Atény - Istanbul.

struct RouteInfo {
  // Město ("Prague"), u malých letišť kód IATA. Bez diakritiky - písmo na
  // displeji nic jiného než ASCII neumí.
  char from[20] = "";
  char to[20] = "";
};

enum class RouteParseStatus : uint8_t {
  // Trasa nalezena a server ji označil za věrohodnou k poloze.
  Ok = 0,
  // Dotaz proběhl, ale použitelná trasa není. Spousta letů žádnou nemá
  // (všeobecné letectví, vojenské stroje, vrtulníky) - je to normální stav,
  // ne chyba.
  NoRoute = 1,
  // Odpověď se nedala přečíst vůbec.
  Invalid = 2,
};

// Jak je na tom trasa vybraného letu. Tři stavy proto, že "ještě nevím" a
// "vím, že žádná není" vypadaly na displeji stejně - prázdným místem - a
// majitel neměl jak poznat, jestli se má dál dívat, nebo je hotovo.
enum class PlaneRouteState : uint8_t {
  // Dotaz běží, nebo se po chybě sítě bude opakovat.
  Pending = 0,
  // Trasa je známá a sedí k poloze letadla.
  Known = 1,
  // Trasa není a nebude: server ji nezná, letadlo nevysílá volací značku, bez
  // které se na ni nedá zeptat, nebo se dotaz opakovaně nepovedl.
  Unknown = 2,
};

// Kolikrát se smí dotaz na tutéž volací značku zopakovat, než se to vzdá.
// Nepovedené stažení není totéž co "tenhle let trasu nemá", takže se po první
// chybě čeká - jenže api.adsb.lol na některé volací značky odpovídá chybou 500
// pokaždé, a to už totéž prakticky je. Bez stropu by panel psal "zjišťuji
// trasu" donekonečna a hodiny se ptaly každých pár vteřin nadarmo.
constexpr uint8_t ROUTE_MAX_ATTEMPTS = 3;

// Za jak dlouho se nepovedený dotaz zopakuje. Dřív se další pokus zařadil až
// za příští stažení letadel (5 až 15 s, po chybě dvojnásobek), takže panel
// psal "zjišťuji trasu" klidně půl minuty kvůli jednomu ztracenému spojení.
// Server (infra/planes) teď odpovídá z předem stažené paměti za desetinu
// vteřiny, takže dvě vteřiny stačí na to, aby chyba sítě odezněla, a API se
// tím nemlátí: strop ROUTE_MAX_ATTEMPTS platí dál.
constexpr uint32_t ROUTE_RETRY_DELAY_MS = 2000;

struct RouteAttemptOutcome {
  // Co má panel ukázat.
  PlaneRouteState state = PlaneRouteState::Pending;
  // Má se dotaz zopakovat?
  bool tryAgain = false;
  // Za kolik milisekund; 0 bez opakování.
  uint32_t retryDelayMs = 0;
  // Kolik pokusů na tutéž značku už selhalo.
  uint8_t failures = 0;
};

// Jak dopadl pokus o trasu. previousFailures je počet dosavadních neúspěchů
// pro tutéž volací značku; vrácené failures se ukládá zpátky.
RouteAttemptOutcome routeAttemptAfter(RouteParseStatus status,
                                      uint8_t previousFailures);

// Rozebere odpověď. Poloha letadla slouží k výběru úseku u vícenohé trasy:
// u "LKPR-LTFM-OMDB" se dřív ukazoval první odlet a poslední přílet, i když
// letadlo letělo prostřední úsek.
RouteParseStatus routeParse(const char *payload, float aircraftLatitude,
                            float aircraftLongitude, RouteInfo &info);

// Složí text dolů na ASCII: diakritika padá, písmeno pod ní zůstává. adsb.lol
// vrací názvy měst v Unicode - Izmir přijde jako U+0130 plus "zmir", tedy dva
// bajty tam, kde písmo čeká jeden.
void routeTextToAscii(char *destination, size_t capacity, const char *source);

// Paměť posledních tras podle volací značky. Přepínání mezi letadly (nebo
// letadlo poblíž, na které se hodiny vrátí za pár minut) se tak na trasu
// neptá znovu. Malé pevné pole bez haldy: osm značek po ~60 bajtech.
//   nalezená trasa platí 20 minut - let se za tu dobu nezmění,
//   "trasa není" jen minutu - server ji mohl mezitím doplnit, nebo nesouhlasila
//     s polohou, která se posunula,
//   nepovedený dotaz se nepamatuje vůbec: to není odpověď.
constexpr uint8_t ROUTE_CACHE_SIZE = 8;
constexpr uint32_t ROUTE_CACHE_FOUND_MS = 20UL * 60UL * 1000UL;
constexpr uint32_t ROUTE_CACHE_NO_ROUTE_MS = 60UL * 1000UL;

struct RouteCacheEntry {
  char callsign[10] = "";
  bool found = false;
  uint32_t storedAt = 0;
  RouteInfo info;
};

struct RouteCache {
  RouteCacheEntry entries[ROUTE_CACHE_SIZE];
};

// Uloží odpověď. Invalid (chyba sítě, rozbitá odpověď) se neukládá. Stejná
// značka se přepíše, jinak se bere volné místo, nakonec nejstarší záznam.
void routeCacheStore(RouteCache &cache, const char *callsign,
                     RouteParseStatus status, const RouteInfo &info,
                     uint32_t nowMs);

// Platná odpověď pro značku: state je Known (s info), nebo Unknown. false,
// když značku paměť nezná nebo záznam vypršel.
bool routeCacheLookup(const RouteCache &cache, const char *callsign,
                      uint32_t nowMs, PlaneRouteState &state, RouteInfo &info);

void routeCacheClear(RouteCache &cache);
