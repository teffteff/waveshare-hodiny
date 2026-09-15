#pragma once

#include <Arduino.h>

#include "ClockConfig.h"
#include "NetworkDiagnostics.h"
#include "SchoolParser.h"

constexpr size_t SCHOOL_MESSAGE_LENGTH = 96;

// Malý přehled bez hodin a úkolů, aby se vešel na zásobník volajícího.
struct SchoolStatus {
  uint32_t generation = 0;
  // Hodiny všech zobrazených dnů dohromady.
  size_t lessonCount = 0;
  size_t homeworkCount = 0;
  bool ready = false;
  bool loading = false;
  // Stáří posledního úspěšného stažení; platí jen s lastSuccessAvailable.
  uint32_t lastSuccessAgeMs = 0;
  bool lastSuccessAvailable = false;
  char message[SCHOOL_MESSAGE_LENGTH] = "";
};

// Návštěvník dostane feed pod zámkem služby. Ukazatele do něj platí jen po
// dobu volání, takže se rozvrh nekopíruje na zásobník.
using SchoolFeedVisitor = void (*)(const SchoolFeed &feed, void *context);

void schoolServiceBegin();
// Vrací false, když zámek nebyl volný; volající nesmí prázdný status vydávat
// za prázdný rozvrh.
bool schoolServiceStatus(SchoolStatus &status);
// Projde uložený rozvrh. Vrací false, když zámek nebyl volný nebo rozvrh ještě
// není.
bool schoolServiceVisit(SchoolFeedVisitor visitor, void *context);
// Stáhne a rozebere rozvrh do mezipaměti obrazovky. Adresu zadává majitel,
// takže se ověřuje proti svazku kořenů Mozilly - volá se jen z úlohy školy.
bool schoolServiceFetch(const ClockSchoolConfig &config,
                        NetworkDiagnosticKind diagnosticKind, int &httpStatus,
                        String &error);
// Zkouška adresy z webu. Výsledek leží stranou, aby zkoušená adresa nepřepsala
// to, co hodiny právě ukazují.
bool schoolServiceProbe(const ClockSchoolConfig &config, int &httpStatus,
                        String &error);
bool schoolServiceVisitProbe(SchoolFeedVisitor visitor, void *context);
// Po SCHOOL_STALE_MS od posledního úspěchu schová uložený rozvrh. Stažení
// při chybě to hlídá samo; tohle je pro chvíle, kdy se nestahuje vůbec
// (Wi-Fi dole).
void schoolServiceExpireStale();
// Zahodí mezipaměť i buffer. Volá se po vypnutí obrazovky nebo změně adresy.
void schoolServiceClear();
