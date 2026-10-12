#pragma once

#include <Arduino.h>

#include "HomeControlFeed.h"

// Obrazovka Ovládání: vypínače a světla se štítkem HA (HomeControlFeed.h).
//
// Vlastní úloha, protože tlačítko má reagovat hned: sdílení pracovníci
// (NetPoll) i úloha Home Assistanta drží dlouhé dotazy. Stav se čte jen
// tehdy, když je obrazovka vidět, a to každé 3 s; po stisku se služba HA
// zavolá okamžitě a stav se přečte znovu za necelou sekundu.
//
// Stisk je optimistický: dlaždice se přepne hned (pending) a čeká, až to
// potvrdí HA. Když to do PENDING_TIMEOUT nepotvrdí, vrátí se do skutečného
// stavu a služba ohlásí chybu.

struct HomeControlTile {
  HomeControlItem item;
  // Přepnuto na displeji, HA to ještě nepotvrdil; item.state už je čekaný stav.
  bool pending = false;
};

struct HomeControlSnapshot {
  uint32_t generation = 0;
  bool haveData = false;
  bool loading = false;
  unsigned long lastSuccessAt = 0;
  // Poslední chyba (česky); prázdná, když je vše v pořádku.
  char message[64] = "";
  uint8_t count = 0;
  HomeControlTile tiles[HOME_CONTROL_MAX_ITEMS];
};

void homeControlServiceBegin();
void homeControlServicePrepareForFirmwareUpdate();

// visible: obrazovka je na displeji. Adresa a token jsou z připojení k HA.
void homeControlServiceSetActive(bool visible, const char *haUrl,
                                 const char *token, const char *label);
// Zkopíruje stav, jen když se změnil od `knownGeneration`; vrací true po
// zkopírování.
bool homeControlServiceSnapshot(HomeControlSnapshot &snapshot,
                                uint32_t knownGeneration);
// Stisk dlaždice `index` (pořadí v posledním snímku). False, když dlaždice
// neexistuje, je nedostupná, nebo už jeden příkaz čeká.
bool homeControlServicePress(uint8_t index);
bool homeControlServiceHasData();
