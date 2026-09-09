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

// Trasu vybraného letu vozí adsb.lol, tedy jiný server než polohy. S vlastním
// zdrojem odpadá: ten obstará obojí, takže hodiny mluví s jediným jménem.
inline constexpr char PLANE_FEED_ROUTE_HOST[] =
    "https://api.adsb.lol/api/0/route/";

// Nejdelší adresa, kterou umí nastavení, plus místo na připojené parametry.
constexpr size_t PLANE_FEED_URL_CAPACITY = 256;

// Složí adresu do output. Prázdné feedUrl (nebo nullptr) znamená ptát se
// adsb.fi přímo. Vrací false, když se adresa do output nevejde - volající pak
// nemá o co opřít dotaz.
bool planeFeedBuildUrl(const char *feedUrl, float latitude, float longitude,
                       float distanceNm, char *output, size_t capacity);

// Totéž pro trasu jednoho letu. Poloha letadla jde do dotazu spolu se značkou,
// protože podle ní server posoudí, jestli trasa k letadlu vůbec sedí. Vlastní
// zdroj se pozná podle parametru "route" - je to táž adresa jako pro polohy,
// takže se v nastavení vyplňuje jen jednou.
bool planeFeedBuildRouteUrl(const char *feedUrl, const char *callsign,
                            float latitude, float longitude, char *output,
                            size_t capacity);
