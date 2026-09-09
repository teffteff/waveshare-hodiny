#pragma once

#include <stddef.h>
#include <stdint.h>

// Rozbor agendy z vlastního serveru, oddělený od stahování, aby šel testovat
// na počítači - stejně jako RssParser vedle RssService nebo AdsbParser vedle
// PlaneRadarService.
//
// Odpověď /agenda.json má tvar
//   {"generated":"2026-09-09T14:56:39+02:00","count":12,"items":[
//     {"day":"DNES","date":"2026-09-09","time":"18:00","title":"Popelnice","cal":0},
//     {"day":"","date":"","time":"20:00","title":"Svoz","cal":1}]}
//
// Server posílá hotové řetězce: časovou zónu, rozbalení opakovaných událostí
// i skládání popisků dne udělal on, takže tady ani na displeji nezůstává žádná
// datumová aritmetika. Klíč "date" se schválně nečte - na displeji ho nic
// nepoužívá a ve formátu je proto, aby si firmware mohl popisky dnů někdy
// přeložit sám, aniž by se musel měnit server.

// Strop pro jednu rozebranou agendu. Server jich ve výchozím stavu posílá
// dvanáct; rezerva navíc drží parser nezávislý na ClockConfig, aby šel
// testovat na počítači.
constexpr size_t AGENDA_MAX_ITEMS = 12;
// "DNES", "ZÍTRA" nebo "pá 11.9.". Í je v UTF-8 dvoubajtové, proto víc bajtů
// než znaků.
constexpr size_t AGENDA_DAY_LENGTH = 24;
// "HH:MM" a ukončovací nula.
constexpr size_t AGENDA_TIME_LENGTH = 6;
// Server titulky ořezává na 48 znaků. České znaky jsou dvoubajtové, takže
// nejhorší případ je 96 bajtů; zbytek je rezerva na server nastavený jinak.
constexpr size_t AGENDA_TITLE_LENGTH = 104;

struct AgendaItem {
  // Popisek dne nese jen PRVNÍ událost toho dne, u ostatních je prázdný. Podle
  // toho obrazovka pozná, kde začít novou hlavičku, aniž by porovnávala data.
  char day[AGENDA_DAY_LENGTH] = "";
  // Prázdný čas znamená celodenní událost. Ta nemá podle čeho se řadit, takže
  // ji server staví na začátek svého dne.
  char time[AGENDA_TIME_LENGTH] = "";
  char title[AGENDA_TITLE_LENGTH] = "";
  // Pořadí kalendáře v konfiguraci serveru. Obrazovka si podle něj vybírá
  // barvu, takže rozsah hlídá až ona - parser čísla jen opíše.
  uint8_t calendar = 0;

  bool allDay() const { return time[0] == '\0'; }
  bool startsDay() const { return day[0] != '\0'; }
};

struct AgendaFeed {
  size_t count = 0;
  AgendaItem items[AGENDA_MAX_ITEMS];
};

enum class AgendaParseStatus : uint8_t {
  // Rozebráno, ať už události přišly nebo je kalendář prázdný. Prázdno je tady
  // legitimní odpověď, na rozdíl od kanálu se zprávami: den bez události je
  // běžný stav, ne rozbitý server.
  Ok = 0,
  // Odpověď vůbec nezačíná objektem - chybová stránka proxy, gzip, zbytek
  // chunků. Opakovat nemá smysl, dokud se nezmění to, co server posílá.
  NotJson = 1,
  // Platný JSON, ale bez pole "items".
  MissingArray = 2,
};

struct AgendaParseOutcome {
  AgendaParseStatus status = AgendaParseStatus::NotJson;
  size_t count = 0;
};

// Přepis textu do znaků, které projektová písma opravdu obsahují. Zdroj je
// vnitřek řetězce v JSONu, takže se cestou dekódují escapy (\", č) i
// UTF-8. České znaky ClockCzechFont*.c má, ty projdou beze změny; co v písmu
// není, se přepíše na ASCII, a co ani to ne, se zahodí - prázdné místo je
// čitelnější než obdélníček, který LVGL vykreslí za chybějící glyf.
//
// Souvislé bílé znaky se sloučí do jedné mezery a výsledek se ořízne. Zkrácení
// nikdy nerozsekne vícebajtový znak: buď se vejde celý, nebo tam není.
//
// Vrací délku zapsaného řetězce bez koncové nuly. Do destination se vždy
// zapíše platný řetězec, i když se text nevejde celý.
size_t agendaCopyText(const char *source, size_t length, char *destination,
                      size_t destinationSize);

// Rozebere odpověď do feedu. maximumItems omezuje, kolik událostí si feed
// nechá; hodnota nad AGENDA_MAX_ITEMS se ořízne na strop.
AgendaParseOutcome agendaParseFeed(const char *payload, size_t length,
                                   size_t maximumItems, AgendaFeed &feed);
