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

// Rozebere odpověď. Poloha letadla slouží k výběru úseku u vícenohé trasy:
// u "LKPR-LTFM-OMDB" se dřív ukazoval první odlet a poslední přílet, i když
// letadlo letělo prostřední úsek.
RouteParseStatus routeParse(const char *payload, float aircraftLatitude,
                            float aircraftLongitude, RouteInfo &info);

// Složí text dolů na ASCII: diakritika padá, písmeno pod ní zůstává. adsb.lol
// vrací názvy měst v Unicode - Izmir přijde jako U+0130 plus "zmir", tedy dva
// bajty tam, kde písmo čeká jeden.
void routeTextToAscii(char *destination, size_t capacity, const char *source);
