#pragma once

#include <stddef.h>

// Adresa kanálu zpráv s doplněnou polohou hodin. Oddělená od stahování, aby
// šla testovat na počítači - stejně jako PlaneFeedUrl vedle radaru letadel.
//
// Poloha se do adresy dostane jen tam, kde si o ni uživatel řekne zástupnými
// značkami {city}, {lat} a {lon}. Připojovat ji ke každé adrese by prozradilo
// polohu domácnosti každé redakci, jejíž kanál někdo zadá, a podepsané nebo
// kešované adresy cizích serverů by se tím mohly rozbít. Vlastní generátor
// zpráv (infra/news) podle nich vybírá zprávy z okolí.

inline constexpr char RSS_FEED_URL_CITY_TOKEN[] = "{city}";
inline constexpr char RSS_FEED_URL_LATITUDE_TOKEN[] = "{lat}";
inline constexpr char RSS_FEED_URL_LONGITUDE_TOKEN[] = "{lon}";

// Nejdelší adresa z nastavení (191 znaků) plus místo na polohu. Jméno místa
// má v nastavení až 63 bajtů a po procentovém kódování každý bajt až tři znaky.
constexpr size_t RSS_FEED_URL_CAPACITY = 512;

// Složí adresu do output. Značky se nahradí všude, kde se v šabloně vyskytnou;
// jméno místa se procentově kóduje, souřadnice mají pět desetinných míst.
// Adresa bez značek projde beze změny. Vrací false, když se výsledek do output
// nevejde - useknutá adresa by se ptala na jiné místo, než jaké je nastavené.
bool rssFeedBuildUrl(const char *templateUrl, const char *city, float latitude,
                     float longitude, char *output, size_t capacity);
