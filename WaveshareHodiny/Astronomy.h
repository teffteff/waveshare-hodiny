#pragma once

#include <stdint.h>

// Poloha Slunce a Měsíce, jejich východy a západy a fáze Měsíce, spočítané
// přímo na zařízení z polohy a času. Nepotřebuje síť, takže denní a noční
// režim přepne na minutu přesně i tehdy, když Open-Meteo zrovna neodpovídá.
//
// Výpočet sleduje zjednodušené dráhy Paula Schlytera ("How to compute
// planetary positions"): Slunce na zhruba úhlovou minutu, Měsíc s hlavními
// poruchami na několik minut. Na východ a západ to dává chybu do dvou minut,
// což je pro hodiny víc než dost. Časy jsou unixové sekundy v UTC; do místního
// času je převede až volající.

enum class AstronomyBody : uint8_t {
  Sun = 0,
  Moon,
};

// Zdánlivá výška středu tělesa nad obzorem ve stupních, bez refrakce.
double astronomyAltitudeDeg(AstronomyBody body, int64_t epoch,
                            double latitudeDeg, double longitudeDeg);

// Výška tělesa nad obzorem vůči okamžiku východu či západu: kladná, když je
// nad obzorem. U Slunce se počítá s refrakcí a poloměrem kotouče, u Měsíce
// navíc s paralaxou podle jeho okamžité vzdálenosti.
double astronomyHorizonMarginDeg(AstronomyBody body, int64_t epoch,
                                 double latitudeDeg, double longitudeDeg);

// Nejbližší východ (rising == true) nebo západ po okamžiku from, hledaný
// nejvýš searchSeconds dopředu. Při záporném searchSeconds se hledá zpět a
// vrátí se nejbližší událost před from. Vrací false, když v okně žádná není -
// za polárním kruhem nebo u Měsíce, který některý den nevyjde.
bool astronomyFindEvent(AstronomyBody body, bool rising, int64_t from,
                        int64_t searchSeconds, double latitudeDeg,
                        double longitudeDeg, int64_t &event);

// Střed Slunce 6° pod obzorem: občanské svítání a soumrak. Do té doby je
// venku dost světla na běžnou činnost bez umělého osvětlení.
constexpr double ASTRONOMY_CIVIL_TWILIGHT_DEG = -6.0;

// Okamžik, kdy střed Slunce vystoupá (rising) nebo klesne na zadanou výšku,
// bez refrakce. Směr hledání a návratová hodnota jako u astronomyFindEvent;
// za polárním kruhem Slunce danou výšku některé dny nemine vůbec.
bool astronomyFindSunAltitude(double altitudeDeg, bool rising, int64_t from,
                              int64_t searchSeconds, double latitudeDeg,
                              double longitudeDeg, int64_t &event);

struct AstronomyMoonPhase {
  // 0 nov, 0,25 první čtvrť, 0,5 úplněk, 0,75 poslední čtvrť.
  double phase = 0.0;
  // Osvětlená část kotouče v procentech.
  double illuminationPercent = 0.0;
};

AstronomyMoonPhase astronomyMoonPhase(int64_t epoch);

// Nejbližší okamžik po from, kdy Měsíc dosáhne fáze targetPhase (0 nov,
// 0,5 úplněk). Lunace trvá 29,5 dne, takže při hledání aspoň třicet dní dopředu
// se najde vždycky.
bool astronomyFindMoonPhase(double targetPhase, int64_t from,
                            int64_t searchSeconds, int64_t &event);

// Osm pojmenovaných fází po 45 stupních, nov je 0. Hranice leží v půli mezi
// nimi, takže úplněk platí pro fázi 0,4375 až 0,5625.
uint8_t astronomyMoonPhaseIndex(double phase);
