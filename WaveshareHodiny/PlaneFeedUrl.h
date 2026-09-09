#pragma once

#include <stddef.h>

// Adresa dotazu na letadla, oddělená od stahování, aby šla testovat na
// počítači - stejně jako AdsbParser vedle PlaneRadarService.
//
// Hodiny se ptají buď adsb.fi přímo, nebo vlastního serveru z infra/planes,
// který tutéž odpověď ořeže na to, co firmware opravdu čte. Obě adresy nesou
// touž trojici údajů, jen jinak zapsanou: adsb.fi je má v cestě, vlastní zdroj
// v dotazu, aby mohl být obyčejný soubor za proxy.

// Veřejné API adsb.fi. Dosah se udává v námořních mílích.
inline constexpr char PLANE_FEED_ADSB_HOST[] =
    "https://opendata.adsb.fi/api/v3/lat/";

// Nejdelší adresa, kterou umí nastavení, plus místo na připojené parametry.
constexpr size_t PLANE_FEED_URL_CAPACITY = 256;

// Složí adresu do output. Prázdné feedUrl (nebo nullptr) znamená ptát se
// adsb.fi přímo. Vrací false, když se adresa do output nevejde - volající pak
// nemá o co opřít dotaz.
bool planeFeedBuildUrl(const char *feedUrl, float latitude, float longitude,
                       float distanceNm, char *output, size_t capacity);
