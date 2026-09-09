#pragma once

#include <stddef.h>
#include <stdint.h>

// Rozbor odpovědi adsb.fi, oddělený od stahování, aby šel testovat na počítači -
// stejně jako RssParser vedle RssService nebo WeatherForecast vedle
// WeatherForecastService. Převzato z projektu MeteoPlaneRadar a přepsané z
// ArduinoJsonu na ruční průchod, protože firmware žádnou JSON knihovnu nemá.
//
// Odpověď /api/v3/lat/... má tvar
//   {"ac":[{"hex":"49d0d1","flight":"CSA1234 ","lat":50.1,...}],"msg":"No error"}
// Pole se podle serveru jmenuje "ac" (adsb.fi) nebo "aircraft" (servery
// odvozené od ADSBexchange), takže se berou obě jména.

// Nejvíc letadel, o kterých má smysl vědět. Na kruh se jich čitelně vejde
// zlomek, ale strop nesmí být těsný: hledání nouzového kódu a hlídaného letu
// se dělá až nad staženým seznamem, takže co sem nepadne, to se neukáže vůbec.
// Sto padesát pokryje i sto kilometrů nad hustou Evropou; obě pole leží v PSRAM,
// takže je to zhruba pětatřicet kilobajtů, o které jinde nikdo nepřijde.
constexpr size_t ADSB_MAX_AIRCRAFT = 150;

// Nouzové squawky. Drží se jako text, protože 7700 je osmičkový kód a "0021"
// se nesmí změnit na 21.
constexpr char ADSB_SQUAWK_HIJACK[] = "7500";
constexpr char ADSB_SQUAWK_RADIO[] = "7600";
constexpr char ADSB_SQUAWK_EMERGENCY[] = "7700";

struct AdsbAircraft {
  float latitude = 0.0f;
  float longitude = 0.0f;
  // Traťový úhel ve stupních. hasTrack říká, jestli ho letadlo vůbec hlásí -
  // bez něj se kreslí kolečko místo šipky.
  float trackDeg = 0.0f;
  float altitudeFt = 0.0f;
  // Hlásí letadlo výšku vůbec? Nula ani záporné číslo samy o sobě nestačí:
  // barometrická výška smí být lehce pod nulou (letiště pod hladinou moře nebo
  // tlak nad standardem) a taková hodnota je platná, jen nízká.
  bool hasAltitude = false;
  float groundSpeedKt = 0.0f;
  float verticalRateFtMin = 0.0f;
  // Callsign je číslo LETU, ne letadla: mezi rotacemi se recykluje. Jako
  // identita se proto nikdy nepoužívá.
  char callsign[10] = "";
  // ICAO 24bitová adresa. Tohle je identita letadla - drží se airframu a mezi
  // stahováními se nemění, na rozdíl od pozice v poli, které adsb.fi
  // přerovnává, jak se mu zlíbí. Osm bajtů proto, že necíkaové cíle (TIS-B,
  // ADS-R) mají před adresou vlnovku, tedy sedm znaků a ukončovací nula.
  char hex[8] = "";
  // Typ letounu ("A320") a registrace ("OK-TVU"). Obojí veze táž odpověď, tedy
  // zadarmo - druhé API kvůli tomu není potřeba.
  char type[10] = "";
  char registration[12] = "";
  // Typ vypsaný slovy ("AIRBUS A-321neo"), jak ho server zná z databáze
  // letadel. Zkratka "A21N" sama o sobě řekne něco jen tomu, kdo je zná
  // nazpaměť. Veze ho táž odpověď jako polohu, takže zase zadarmo; asi dvacetina
  // letadel ho nemá, protože je server ve své databázi nenajde. Čtyřicet bajtů
  // pokryje i "BOMBARDIER BD-700 Global 7000/7500", delší jména se useknou.
  char description[40] = "";
  char squawk[6] = "";
  bool hasTrack = false;
};

enum class AdsbParseStatus : uint8_t {
  // Rozebráno, ať už letadla přišla nebo je obloha prázdná.
  Ok = 0,
  // Odpověď vůbec nezačíná objektem - chybová stránka proxy, gzip, zbytek
  // chunků. Opakovat nemá smysl, dokud se nezmění to, co server posílá.
  NotJson = 1,
  // Platný JSON, ale bez pole letadel. Server obvykle řekne proč v "msg".
  MissingArray = 2,
};

struct AdsbParseOutcome {
  AdsbParseStatus status = AdsbParseStatus::NotJson;
  size_t count = 0;
  // Text z pole "msg", když ho server poslal. Slouží diagnostice, ne obrazovce.
  char message[48] = "";
};

// Rozebere odpověď do pole, které dodá volající - kde ta paměť leží, je věc
// služby, ne rozboru. Letadla na zemi se zahazují, aby v poli nebrala místo
// těm ve vzduchu; při plném poli se zbytek odpovědi ignoruje.
AdsbParseOutcome adsbParseAircraft(const char *payload, AdsbAircraft *aircraft,
                                   size_t capacity);

// Vrací kód nouze, kterým letadlo právě vysílá, jinak nullptr.
const char *adsbEmergencyCode(const AdsbAircraft &aircraft);

// Jak vážný ten kód je. 0 znamená žádnou nouzi, vyšší číslo horší stav:
// porucha rádia je nepříjemnost, obecná nouze ohrožení letu a únos to nejhorší.
// Na obrazovce je na hlášku jediný řádek, takže se musí dát srovnat, které
// z několika letadel na něj patří.
uint8_t adsbEmergencySeverity(const char *code);

// Najde letadlo podle ICAO adresy. Vrací index v aktuálním poli, nebo -1, když
// v posledních datech není. Používá se místo držení indexu přes stahování -
// pole se pokaždé staví znovu a jeho pořadí není zaručené.
int adsbFindByHex(const AdsbAircraft *aircraft, size_t count, const char *hex);
