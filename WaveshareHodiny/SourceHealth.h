#pragma once

#include <stddef.h>
#include <stdint.h>

// Jak se daří zdrojům dat: kdy naposledy odpověděly, kolikrát za sebou
// neodpověděly a kdy je další pokus. Podle SrcHealth.h z MeteoPlaneRadar
// 0.9.7 (Petr / chiptron.cz, MIT), viz THIRD_PARTY_NOTICES.md.
//
// Služby už dřív při chybě nechávaly na obrazovce poslední data nebo psaly
// každá vlastní hlášku. S tímhle stavem řeknou obrazovky jedním stejným
// řádkem "Zdroj nedostupný · další pokus za 4 min", a když dál ukazují
// poslední data, označí je "Stará data". Řádek kreslí ciferník
// (clockDashboardSetSourceStatus) a stejný stav ukazuje diagnostika.
//
// Radar letadel má vlastní hlídání stáří (šedá letadla, "Stará data: N min",
// PlaneRadarService.cpp); sem jen hlásí výsledky stahování, aby řádek
// i diagnostika věděly o dalším pokusu.

enum class ClockSource : uint8_t {
  HomeAssistant = 0,
  OpenMeteo,
  Forecast,
  History,
  Radar,
  Lightning,
  Warnings,
  Planes,
  Satellites,
  School,
  Rss,
  Agenda,
  Traffic,
  Count,
};

constexpr size_t CLOCK_SOURCE_COUNT = static_cast<size_t>(ClockSource::Count);

// Kolik neúspěchů za sebou už znamená "nedostupný" u zdroje, který dřív
// odpovídal. Jediný výpadek (Wi-Fi zaváhala, server se restartoval) se
// nehlásí; zdroj, který ještě neodpověděl ani jednou, je nedostupný hned.
constexpr uint16_t SOURCE_HEALTH_DOWN_FAILURES = 2;

struct SourceHealth {
  uint32_t okAt = 0;     // millis() posledního úspěchu
  uint32_t nextAt = 0;   // millis() dalšího pokusu
  uint16_t failures = 0; // neúspěchy za sebou od posledního úspěchu
  bool everOk = false;
  bool tried = false;

  // Po pokusu: jak dopadl a za jak dlouho je další.
  void record(uint32_t now, bool ok, uint32_t nextInMs);
  // Zdroj se vypnul nebo změnil adresu: začíná se znovu, ne jako nedostupný.
  void reset() { *this = SourceHealth(); }
  // Hlásit jako nedostupný?
  bool down() const;
  // Milisekundy od posledního úspěchu; UINT32_MAX, když žádný nebyl.
  uint32_t ageMs(uint32_t now) const;
  // Milisekundy do dalšího pokusu; 0, když je splatný nebo právě běží.
  uint32_t nextInMs(uint32_t now) const;
};

// "Zdroj nedostupný · další pokus za 4 min" (do minuty "za 40 s", po termínu
// "zkouším znovu"); se staleData s předponou "Stará data · ". Prázdný text,
// když zdroj není nedostupný. Vrací délku textu.
size_t sourceHealthFormat(const SourceHealth &health, uint32_t now,
                          bool staleData, bool english, char *output,
                          size_t capacity);

// Krátký název zdroje pro diagnostiku ("ha", "openMeteo", ...).
const char *sourceHealthName(ClockSource source);

#if defined(ESP_PLATFORM)
// Sdílený stav všech zdrojů. Bezpečné z kterékoli úlohy (ne z přerušení);
// čas bere z millis().
void sourceHealthRecord(ClockSource source, bool ok, uint32_t nextInMs);
void sourceHealthReset(ClockSource source);
SourceHealth sourceHealthGet(ClockSource source);
// Text řádku pro obrazovku, viz sourceHealthFormat.
size_t sourceHealthText(ClockSource source, bool staleData, bool english,
                        char *output, size_t capacity);
#endif
