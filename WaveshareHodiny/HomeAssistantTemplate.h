#pragma once

#include <stddef.h>
#include <stdint.h>

#include "JsonScan.h"

// Stavy všech entit Home Assistantu jedním dotazem na /api/template místo
// samostatného GET /api/states/<entita> pro každou z nich (dřív až 44
// dotazů za minutu, každý se zámkem sítě, na 7" kolem 20 s).
//
// Šablona projde stavy v HA a pro každou žádanou entitu vypíše dvojici
// [klíč, stav]; u entity slunce navíc čas poslední změny a atributy
// next_rising a next_setting, které čte applySunState(). Klíč je pořadí
// entity v seznamu firmwaru, takže se v odpovědi neopakují ID entit:
//   [[0,"cloudy"],[1,"21.4"],[5,"above_horizon","2026-...","2026-...","2026-..."]]
// Entita, kterou HA nezná, v odpovědi chybí - přesně jako dřív 404.
// "unavailable" a "unknown" projdou jako stav, stejně jako z /api/states.
//
// /api/template vyžaduje token administrátora (APITemplateView.post má
// @require_admin, jinak 401). Firmware proto při neúspěchu přechází na
// dotazy po jednotlivých entitách.
//
// Modul nezná Arduino ani síť, aby šel testovat na počítači.

// Společný buffer dotazu i odpovědi (v PSRAM). Dotaz se 44 entitami s
// nejdelším povoleným ID (127 znaků) má ~6,5 kB; odpověď pro 44 entit se
// stavem o 255 znacích (strop HA) bez escapů ~11,7 kB. Patologická odpověď
// plná escapů se nevejde - pak se použijí dotazy po entitách.
constexpr size_t HA_TEMPLATE_BUFFER_BYTES = 16 * 1024;
// Atributy slunce se zkracují: čas v ISO 8601 má 32 znaků, delší hodnotu by
// stejně nic nepřečetlo, a jiná entita zvolená jako "slunce" tak nenafoukne
// odpověď libovolně dlouhým atributem.
constexpr size_t HA_TEMPLATE_ATTRIBUTE_MAX_CHARS = 40;
// Bez entity slunce v seznamu.
constexpr size_t HA_TEMPLATE_NO_ATTRIBUTES = static_cast<size_t>(-1);

// ID entity, které jde bezpečně vložit do šablony: doména.objekt jen z
// malých písmen, číslic a podtržítek (stejné pravidlo jako v HA). Nic jiného
// v ID HA být nemůže, takže se nic neescapuje a nic se nedá podstrčit.
bool haTemplateEntityIdValid(const char *entityId);

// Klíč položky `index`: pořadí prvního výskytu stejného ID v seznamu, nebo
// -1 u prázdného či neplatného ID (taková entita se v odpovědi neobjeví).
int haTemplateKey(const char *const *entityIds, size_t count, size_t index);

// Složí celé JSON tělo POST požadavku {"template":"..."} do `out` (s nulou
// na konci). `attributesIndex` je pozice entity slunce v seznamu, nebo
// HA_TEMPLATE_NO_ATTRIBUTES. Vrací délku těla, 0 když se nevejde nebo
// v seznamu není žádná platná entita.
size_t haTemplateBuildRequest(const char *const *entityIds, size_t count,
                              size_t attributesIndex, char *out,
                              size_t capacity);

struct HaTemplateEntity {
  bool found = false;
  JsonValue state;
  // Jen u entity slunce, jinak neplatné.
  JsonValue lastChanged;
  JsonValue nextRising;
  JsonValue nextSetting;
};

// Rozebere odpověď a rozdělí ji podle pozic v seznamu: entities[i] dostane
// údaje entity entityIds[i], i když se ID v seznamu opakuje. Vrací false u
// odpovědi, která není celé pole dvojic s platnými klíči (useknutá, chyba
// šablony, cizí obsah) - pak se nesmí použít nic z ní. Hodnoty ukazují do
// `body`.
bool haTemplateParseResponse(const char *body, size_t length,
                             const char *const *entityIds, size_t count,
                             HaTemplateEntity *entities);

// Text řetězce stejně jako dřívější extractJsonStringField() nad
// /api/states: escapovaný znak se opíše bez lomítka. Neplatná hodnota dá
// prázdný text. Vrací délku.
size_t haTemplateCopyText(const JsonValue &value, char *destination,
                          size_t capacity);
