#pragma once

#include <stddef.h>
#include <stdint.h>

#include "AgendaParser.h"

// Data obrazovek bez LVGL. Setry ciferníku (ClockDashboard.cpp) tu skládají
// texty na stejném místě, kde je dřív psaly rovnou do popisků; kulatá stránka
// je z modelu přepíše do svých popisků a 7" (ClockDashboardLcd7.h) je čte
// odsud, místo aby lovil text v popiscích kulatých stránek. Díky tomu se na 7"
// kulaté stránky, které se tam nikdy neukážou, vůbec nemusí zakládat.
//
// Každý model má počítadlo revision, které roste s každou změnou; 7" podle něj
// pozná, kdy znovu rozmístit, a nemusí porovnávat texty. Modely leží v PSRAM
// (DashboardViewModels se zakládá jednou při startu ciferníku).

// Datum a jmeniny pod časem.
struct DateViewModel {
  char date[48] = "";
  // Jmeniny VELKÝMI písmeny, jak je posílá tabulka; prázdné = nezobrazovat.
  char nameday[64] = "";
  uint32_t revision = 0;
};

// Blesk se vzdáleností nejbližšího úderu ve stavových ikonách.
struct LightningViewModel {
  bool active = false;
  char text[24] = "";
};

// Slunce a Měsíc: hotové texty stránky (časy, popisky, fáze).
struct SkyViewModel {
  // 0 východ Slunce, 1 západ Slunce, 2 východ Měsíce, 3 západ Měsíce.
  char captions[4][16] = {};
  char times[4][8] = {};
  // Občanské svítání a soumrak; prázdné za polárním kruhem.
  char twilight[2][8] = {};
  char dayLength[64] = "";
  char phaseName[32] = "";
  char illumination[32] = "";
  char nextPhases[64] = "";
  uint32_t revision = 0;
};

// Stav meteoradaru kolem snímku.
struct RadarViewModel {
  // Je v plátně snímek k ukázání? Skrývá se při návratu na obrazovku, dokud
  // nedorazí první snímek nové animace.
  bool frameShown = false;
  // Snímek v posledním stavu je, jinak se ukazuje hláška status.
  bool haveFrame = false;
  // "Načítám radar..." nebo "Radar zatím nemá snímek".
  char status[48] = "";
  // Řádek o snímku ("NYNÍ 14:35" s recolor značkou, "-25 min  14:10").
  char frame[48] = "";
  // "100 km" nebo "CELÁ ČR"; rangeKm je zvolený krok (0 = celá republika).
  char range[40] = "";
  uint16_t rangeKm = 0;
  uint32_t revision = 0;
};

// Agenda: řádky tak, jak přišly ze serveru, bez rozmístění.
struct AgendaRowViewModel {
  // Popisek dne nese jen první událost dne (startsDay); jinak prázdný.
  char day[AGENDA_DAY_LENGTH] = "";
  // Prázdný čas = celodenní událost.
  char time[AGENDA_TIME_LENGTH] = "";
  char title[AGENDA_TITLE_LENGTH] = "";
  uint8_t calendar = 0;
  bool startsDay = false;
};

struct AgendaViewModel {
  uint8_t count = 0;
  // Hláška místo seznamu (count == 0): chyba, "Načítám agendu...", "Nic
  // naplánovaného".
  char status[96] = "";
  AgendaRowViewModel rows[AGENDA_MAX_ITEMS];
  uint32_t revision = 0;
};

// Detail vybraného letadla. Popisky a hodnoty zvlášť, aby si je každá
// obrazovka poskládala po svém (kulatá "VÝŠKA: 1200 m", 7" pod sebou).
struct PlaneDetailViewModel {
  static constexpr uint8_t VALUE_ROWS = 4;  // výška, rychlost, směr, stoupání
  bool open = false;
  bool signalLost = false;
  // Volací znak, ICAO adresa nebo "?".
  char title[12] = "";
  char captions[VALUE_ROWS][16] = {};
  char values[VALUE_ROWS][24] = {};
  // Typ slovy nebo zkratkou, "?" když ho letadlo s registrací nehlásí,
  // prázdný když není ani registrace (řádek TYP se pak nepíše).
  char type[40] = "";
  char registration[12] = "";
  // "vlastní přijímač" nebo "internet".
  char source[24] = "";
  // "7700  NOUZE"; prázdné bez nouze.
  char emergency[32] = "";
  // Trasa na dvou řádcích: předložka ("Z", "Do") a místo, nebo bez předložky
  // stav hledání ("Zjišťuji trasu...", "Trasa neznámá"). U meteosondy
  // "Přistání" s časem a pod tím vzdálenost a směr od domova.
  char routeCaption[2][12] = {};
  char route[2][32] = {};
  uint32_t revision = 0;
};

struct DashboardViewModels {
  DateViewModel date;
  LightningViewModel lightning;
  SkyViewModel sky;
  RadarViewModel radar;
  AgendaViewModel agenda;
  PlaneDetailViewModel planeDetail;
};
