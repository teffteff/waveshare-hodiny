#pragma once

#include <Arduino.h>

#include "ClockConfig.h"
#include "NetworkDiagnostics.h"
#include "RssParser.h"

constexpr size_t RSS_TIME_LENGTH = 6;
constexpr size_t RSS_MESSAGE_LENGTH = 96;

// Jedna zpráva připravená k vykreslení. Ukazatele míří do mezipaměti služby a
// platí jen po dobu běhu návštěvníka, tedy pod zámkem.
struct RssDisplayItem {
  const char *title;
  // "HH:MM" v místním čase, nebo prázdný řetězec, když kanál datum neposlal
  // nebo zařízení ještě nemá čas ze sítě.
  const char *time;
};

using RssItemVisitor = void (*)(size_t index, const RssDisplayItem &item,
                                void *context);

// Malý přehled bez samotných titulků, aby se vešel na zásobník volajícího.
struct RssStatus {
  uint32_t generation = 0;
  size_t count = 0;
  bool ready = false;
  bool loading = false;
  // Stáří posledního úspěšného stažení. Platí jen s lastSuccessAvailable;
  // podle něj se pozná, jestli má otevření obrazovky stahovat znovu.
  uint32_t lastSuccessAgeMs = 0;
  bool lastSuccessAvailable = false;
  char channelTitle[RSS_CHANNEL_TITLE_LENGTH] = "";
  char message[RSS_MESSAGE_LENGTH] = "";
};

void rssServiceBegin();
void rssServiceStatus(RssStatus &status);
// Projde uložené zprávy pod zámkem. Vrací false, když zámek nebyl volný;
// volající to zkusí při dalším průchodu smyčkou.
bool rssServiceVisitItems(RssItemVisitor visitor, void *context);
// Stáhne a rozebere kanál. Volá se z datové úlohy, nikdy ze smyčky displeje.
bool rssServiceFetch(const ClockRssConfig &config,
                     NetworkDiagnosticKind diagnosticKind, int &httpStatus,
                     String &error);
// Zahodí mezipaměť i stahovací buffer. Volá se, když se kanál vypne nebo
// změní adresa, aby na obrazovce nezůstaly zprávy z jiného zdroje.
void rssServiceClear();
