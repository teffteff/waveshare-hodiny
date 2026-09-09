#pragma once

#include <Arduino.h>

#include "AgendaParser.h"
#include "ClockConfig.h"
#include "NetworkDiagnostics.h"

constexpr size_t AGENDA_MESSAGE_LENGTH = 96;

// Jedna událost připravená k vykreslení. Ukazatele míří do mezipaměti služby a
// platí jen po dobu běhu návštěvníka, tedy pod zámkem.
struct AgendaDisplayItem {
  // Popisek dne, neprázdný jen u první události toho dne. Podle něj obrazovka
  // pozná, kde nakreslit hlavičku.
  const char *day;
  // "HH:MM", nebo prázdný řetězec u celodenní události.
  const char *time;
  const char *title;
  uint8_t calendar;
};

using AgendaItemVisitor = void (*)(size_t index, const AgendaDisplayItem &item,
                                   void *context);

// Malý přehled bez samotných událostí, aby se vešel na zásobník volajícího.
struct AgendaStatus {
  uint32_t generation = 0;
  size_t count = 0;
  bool ready = false;
  bool loading = false;
  // Stáří posledního úspěšného stažení. Platí jen s lastSuccessAvailable;
  // podle něj se pozná, jestli má otevření obrazovky stahovat znovu.
  uint32_t lastSuccessAgeMs = 0;
  bool lastSuccessAvailable = false;
  char message[AGENDA_MESSAGE_LENGTH] = "";
};

// Přehled poslední zkoušky adresy z webu. Odděleně od AgendaStatus, protože
// zkouška se schválně nedotýká toho, co je právě na displeji.
struct AgendaProbeStatus {
  bool ready = false;
  size_t count = 0;
};

void agendaServiceBegin();
// Vrací false, když zámek nebyl volný; status pak zůstává prázdný a volající
// nesmí prázdnotu vydávat za "kalendář nic nemá". Zkusí to při dalším průchodu
// smyčkou.
bool agendaServiceStatus(AgendaStatus &status);
// Projde uložené události pod zámkem. Vrací false, když zámek nebyl volný.
bool agendaServiceVisitItems(AgendaItemVisitor visitor, void *context);
// Stáhne a rozebere agendu do mezipaměti obrazovky. Ověření proti svazku
// kořenů Mozilly stojí přes 16 kB zásobníku, takže se volá výhradně z úlohy
// agendy, nikdy ze smyčky displeje ani z web serveru.
bool agendaServiceFetch(const ClockAgendaConfig &config,
                        NetworkDiagnosticKind diagnosticKind, int &httpStatus,
                        String &error);
// Zkouška adresy pro web. Stahuje a rozebírá stejně jako agendaServiceFetch,
// ale výsledek ukládá stranou, aby zkoušená adresa nepřepsala to, co hodiny
// právě ukazují.
bool agendaServiceProbe(const ClockAgendaConfig &config, int &httpStatus,
                        String &error);
bool agendaServiceProbeStatus(AgendaProbeStatus &status);
bool agendaServiceVisitProbeItems(AgendaItemVisitor visitor, void *context);
// Zahodí mezipaměť i stahovací buffer. Volá se, když se agenda vypne nebo
// změní adresa, aby na obrazovce nezůstaly události z jiného serveru.
void agendaServiceClear();
