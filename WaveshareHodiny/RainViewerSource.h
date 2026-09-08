#pragma once

#include <Arduino.h>

#include "RainViewerIndex.h"

// Druhý zdroj srážkových dat pro obrazovku radaru.
//
// Kompozice ČHMÚ je nad Českem ostřejší a zůstává výchozí, ale jinde prostě
// žádná data nemá: po přesunu polohy do Vídně obrazovka zčerná. RainViewer
// pokrývá Evropu i svět, je zdarma a nepotřebuje klíč, jen je hrubší.
//
// Podoba dat se od ČHMÚ liší od základu. RainViewer servíruje běžné dlaždice
// Web Mercatoru (z/x/y, 256 px). Místo stažení jednoho velkého obrázku a
// výřezu z něj se proto vybere přiblížení, ve kterém jeden pixel světa
// odpovídá jednomu pixelu displeje; dlaždice se pak jen kopírují bez
// přepočtu. Přiblížení jde po mocninách dvou, takže vyjde nejbližší dostupný
// rozsah, a ne přesně to číslo na displeji - skutečný poloměr proto modul
// hlásí zpět, aby popisek nelhal.

// Nastaví pohled a spočítá přiblížení, měřítko i mřížku dlaždic. Vrací false,
// když se poloha ani rozsah nezměnily a přepočet není potřeba.
bool rainViewerSetView(float latitude, float longitude, uint16_t radiusKm);

// Poloměr, který vybrané přiblížení opravdu dává. Liší se od požadovaného,
// protože přiblížení jsou mocniny dvou.
uint16_t rainViewerEffectiveRadiusKm();

// Promítne zeměpisný bod do souřadnic displeje podle aktuálního pohledu.
void rainViewerProject(float latitude, float longitude, int &x, int &y);

// Viditelné okno ve stupních, aby se mapová data ořezala před kreslením.
void rainViewerWindow(float &southLatitude, float &northLatitude,
                      float &westLongitude, float &eastLongitude);

// Seznam nejnovějších snímků, seřazený od nejstaršího po nejnovější - stejně
// jako u ČHMÚ, aby se animace přehrávala v obou zdrojích stejným směrem.
bool rainViewerFetchIndex(size_t wantedFrameCount, RainViewerIndex &index,
                          uint32_t revision);

// Poskládá jeden snímek do cílového bufferu 480x480 RGB565. Vrací false jen
// tehdy, když nedorazila ani jedna dlaždice; chybějící jednotlivé dlaždice
// zůstanou černé, protože částečný snímek je pořád lepší než žádný.
bool rainViewerBuildFrame(const RainViewerFrame &frame, uint16_t *target,
                          uint32_t revision);

// Uvolní stahovací buffery. Volá se, když se radar přepne zpět na ČHMÚ nebo
// se chystá aktualizace firmwaru.
void rainViewerRelease();

// Zpětné volání, kterým si služba radaru pumpuje animaci a watchdog mezi
// jednotlivými dlaždicemi; bez něj by se během stahování animace zastavila.
using RainViewerPollCallback = void (*)();
void rainViewerSetPollCallback(RainViewerPollCallback callback);

// Přeruší probíhající stahování, jakmile přestane platit revize požadavku.
using RainViewerRevisionCheck = bool (*)(uint32_t revision);
void rainViewerSetRevisionCheck(RainViewerRevisionCheck check);
