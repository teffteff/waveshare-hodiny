#pragma once

#include <stddef.h>
#include <stdint.h>

// Obrazovka Ovládání: seznam vypínačů a světel ze štítku Home Assistanta.
//
// Hodiny si entity nepamatují. Jedním POST /api/template se zeptají, které
// entity nesou štítek (label, výchozí "hodiny"), a šablona rovnou vypíše
// jejich stav. Řádek na entitu, pole oddělená tabulátorem:
//
//   switch.lampa_tv<TAB>on<TAB>Lampa TV<TAB><TAB>23.4
//   light.chytra_zarovka<TAB>on<TAB>Chytrá žárovka<TAB>178<TAB>
//
// Pole: ID entity, stav, jméno, jas 0-255 (jen světla, jinak prázdné),
// příkon ve W z čidla téhož zařízení s device_class power (jinak prázdné).
// Tabulátory a konce řádků ve jménech šablona nahradí mezerou.
//
// Modul nezná Arduino ani síť, aby šel testovat na počítači.

constexpr size_t HOME_CONTROL_MAX_ITEMS = 12;
constexpr size_t HOME_CONTROL_ENTITY_ID_LENGTH = 96;
constexpr size_t HOME_CONTROL_NAME_LENGTH = 48;
// Dotaz i odpověď; 12 entit s dlouhými jmény se vejde do 4 kB s rezervou.
constexpr size_t HOME_CONTROL_BUFFER_BYTES = 4096;

enum class HomeControlDomain : uint8_t {
  Switch,
  Light,
  InputBoolean,
  Fan,
  // Scéna a skript nemají stav: tlačítko je spouští (turn_on).
  Scene,
  Script,
};

enum class HomeControlState : uint8_t { Off, On, Unavailable, Stateless };

struct HomeControlItem {
  char entityId[HOME_CONTROL_ENTITY_ID_LENGTH] = "";
  char name[HOME_CONTROL_NAME_LENGTH] = "";
  HomeControlDomain domain = HomeControlDomain::Switch;
  HomeControlState state = HomeControlState::Off;
  // Jas v procentech, -1 = světlo bez jasu nebo vypnuté.
  int8_t brightnessPercent = -1;
  // Příkon ve W; záporný = zařízení příkon neměří.
  float powerWatts = -1.0f;
};

// Štítek, který jde bezpečně vložit do šablony: id štítku HA (a-z, 0-9, _).
bool homeControlLabelValid(const char *label);

// Doména podle ID entity; false u domény, kterou obrazovka neumí.
bool homeControlDomainOf(const char *entityId, HomeControlDomain &domain);

// Složí JSON tělo POST /api/template {"template":"..."} do `out` (s nulou).
// Vrací délku, 0 když se nevejde nebo štítek není platný.
size_t homeControlBuildTemplateRequest(const char *label, char *out,
                                       size_t capacity);

// Rozebere odpověď šablony (text, ne JSON). Neznámé domény a řádky bez ID
// přeskočí, víc než HOME_CONTROL_MAX_ITEMS položek zahodí. Vrací počet.
size_t homeControlParseResponse(const char *body, size_t length,
                                HomeControlItem *items, size_t capacity);

// Služba HA, kterou tlačítko volá: "switch/toggle", "scene/turn_on"...
// Vrací délku cesty (bez /api/services/), 0 když se nevejde.
size_t homeControlServicePath(HomeControlDomain domain, char *out,
                              size_t capacity);

// Tělo POST /api/services/...: {"entity_id":"<id>"}. ID musí projít
// homeControlEntityIdValid(). Vrací délku, 0 když se nevejde.
size_t homeControlBuildServiceBody(const char *entityId, char *out,
                                   size_t capacity);

// ID entity, které jde vložit do JSON bez escapování: doména.objekt jen
// z malých písmen, číslic a podtržítek (stejné pravidlo jako v HA).
bool homeControlEntityIdValid(const char *entityId);
