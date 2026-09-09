#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "AdsbParser.h"

namespace {

// Zkrácená, jinak doslovná odpověď z opendata.adsb.fi. Pořadí i tvar klíčů
// odpovídá tomu, co server opravdu posílá, včetně callsignu doplněného
// mezerami na osm znaků a squawku jako textu.
const char *const REAL_RESPONSE =
    "{\"ac\":["
    "{\"hex\":\"49d0d1\",\"type\":\"adsb_icao\",\"flight\":\"CSA1234 \","
    "\"r\":\"OK-TVU\",\"t\":\"A320\",\"desc\":\"AIRBUS A-320\","
    "\"alt_baro\":35000,\"gs\":420.5,"
    "\"track\":95.2,\"baro_rate\":-64,\"squawk\":\"1000\","
    "\"lat\":50.104,\"lon\":14.26,\"messages\":1234,\"seen\":0.1},"
    "{\"hex\":\"3c6dd2\",\"type\":\"adsb_icao\",\"flight\":\"DLH88X  \","
    "\"r\":\"D-AIBL\",\"t\":\"A319\",\"alt_baro\":12000,\"gs\":310,"
    "\"true_heading\":270.5,\"baro_rate\":1216,\"squawk\":\"7700\","
    "\"lat\":49.9,\"lon\":14.9}"
    "],\"msg\":\"No error\",\"now\":1788809424.1,\"total\":2}";

void testRealResponse() {
  AdsbAircraft aircraft[ADSB_MAX_AIRCRAFT];
  const AdsbParseOutcome outcome =
      adsbParseAircraft(REAL_RESPONSE, aircraft, ADSB_MAX_AIRCRAFT);
  assert(outcome.status == AdsbParseStatus::Ok);
  assert(outcome.count == 2);
  assert(std::string(outcome.message) == "No error");

  assert(std::string(aircraft[0].hex) == "49d0d1");
  // Callsign přijde doplněný mezerami; ty se musí oříznout, protože se posílá
  // do API na trasu.
  assert(std::string(aircraft[0].callsign) == "CSA1234");
  assert(std::string(aircraft[0].registration) == "OK-TVU");
  // Typ draku je "t", ne "type" - "adsb_icao" je zdroj zprávy.
  assert(std::string(aircraft[0].type) == "A320");
  // Týž typ slovy. Detail letadla ho píše pod zkratku, které nikdo nerozumí.
  assert(std::string(aircraft[0].description) == "AIRBUS A-320");
  assert(aircraft[0].altitudeFt == 35000.0f);
  assert(aircraft[0].hasTrack);
  assert(aircraft[0].trackDeg > 95.1f && aircraft[0].trackDeg < 95.3f);
  assert(aircraft[0].verticalRateFtMin == -64.0f);
  assert(std::string(aircraft[0].squawk) == "1000");
  assert(adsbEmergencyCode(aircraft[0]) == nullptr);

  // Druhé letadlo hlásí směr pod jiným klíčem a vysílá nouzový kód. Jméno typu
  // nemá - server ho ve své databázi nenašel - a to je normální stav.
  assert(std::string(aircraft[1].callsign) == "DLH88X");
  assert(aircraft[1].description[0] == '\0');
  assert(aircraft[1].hasTrack);
  assert(aircraft[1].trackDeg > 270.4f && aircraft[1].trackDeg < 270.6f);
  assert(std::string(adsbEmergencyCode(aircraft[1])) == "7700");
}

void testGroundTrafficIsDropped() {
  // Letadla na zemi se poznají podle textu místo výšky. Musí zmizet dřív, než
  // u letiště seberou místo těm ve vzduchu.
  const char *const payload =
      "{\"ac\":["
      "{\"hex\":\"aaaaaa\",\"alt_baro\":\"ground\",\"lat\":50.1,\"lon\":14.2},"
      "{\"hex\":\"bbbbbb\",\"alt_baro\":3000,\"lat\":50.2,\"lon\":14.3}"
      "],\"msg\":\"No error\"}";
  AdsbAircraft aircraft[ADSB_MAX_AIRCRAFT];
  const AdsbParseOutcome outcome =
      adsbParseAircraft(payload, aircraft, ADSB_MAX_AIRCRAFT);
  assert(outcome.status == AdsbParseStatus::Ok);
  assert(outcome.count == 1);
  assert(std::string(aircraft[0].hex) == "bbbbbb");
  assert(aircraft[0].altitudeFt == 3000.0f);
}

void testAircraftWithoutPositionIsDropped() {
  // Bez polohy není co kreslit; takový záznam nesmí projít ani s nulou.
  const char *const payload =
      "{\"ac\":[{\"hex\":\"aaaaaa\",\"alt_baro\":3000},"
      "{\"hex\":\"bbbbbb\",\"lat\":50.2,\"lon\":14.3}]}";
  AdsbAircraft aircraft[ADSB_MAX_AIRCRAFT];
  const AdsbParseOutcome outcome =
      adsbParseAircraft(payload, aircraft, ADSB_MAX_AIRCRAFT);
  assert(outcome.status == AdsbParseStatus::Ok);
  assert(outcome.count == 1);
  assert(std::string(aircraft[0].hex) == "bbbbbb");
}

void testMissingTrackIsMarked() {
  // Neznámý směr se kreslí kolečkem, ne šipkou na sever - proto se rozlišuje.
  const char *const payload =
      "{\"ac\":[{\"hex\":\"aaaaaa\",\"lat\":50.2,\"lon\":14.3,"
      "\"alt_baro\":3000}]}";
  AdsbAircraft aircraft[ADSB_MAX_AIRCRAFT];
  const AdsbParseOutcome outcome =
      adsbParseAircraft(payload, aircraft, ADSB_MAX_AIRCRAFT);
  assert(outcome.count == 1);
  assert(!aircraft[0].hasTrack);
  assert(aircraft[0].trackDeg == 0.0f);
}

void testNumbersSentAsStrings() {
  // Některé servery posílají čísla v uvozovkách; jiné pošlou text, který
  // číslo není, a ten se nesmí tvářit jako nula.
  const char *const payload =
      "{\"ac\":[{\"hex\":\"aaaaaa\",\"lat\":\"50.2\",\"lon\":\"14.3\","
      "\"alt_baro\":\"3000\",\"gs\":\"neco\"}]}";
  AdsbAircraft aircraft[ADSB_MAX_AIRCRAFT];
  const AdsbParseOutcome outcome =
      adsbParseAircraft(payload, aircraft, ADSB_MAX_AIRCRAFT);
  assert(outcome.count == 1);
  assert(aircraft[0].latitude > 50.19f && aircraft[0].latitude < 50.21f);
  assert(aircraft[0].altitudeFt == 3000.0f);
  assert(aircraft[0].groundSpeedKt == 0.0f);
}

void testNumericSquawkKeepsLeadingZero() {
  // Číselný squawk už vedoucí nulu ztratil; musí se doplnit, jinak by se
  // srovnání s nouzovými kódy rozešlo.
  const char *const payload =
      "{\"ac\":[{\"hex\":\"aaaaaa\",\"lat\":50.2,\"lon\":14.3,"
      "\"squawk\":21}]}";
  AdsbAircraft aircraft[ADSB_MAX_AIRCRAFT];
  const AdsbParseOutcome outcome =
      adsbParseAircraft(payload, aircraft, ADSB_MAX_AIRCRAFT);
  assert(outcome.count == 1);
  assert(std::string(aircraft[0].squawk) == "0021");
}

void testAlternateArrayName() {
  // Servery odvozené od ADSBexchange posílají totéž pole pod jiným jménem.
  const char *const payload =
      "{\"aircraft\":[{\"hex\":\"aaaaaa\",\"lat\":50.2,\"lon\":14.3}]}";
  AdsbAircraft aircraft[ADSB_MAX_AIRCRAFT];
  const AdsbParseOutcome outcome =
      adsbParseAircraft(payload, aircraft, ADSB_MAX_AIRCRAFT);
  assert(outcome.status == AdsbParseStatus::Ok);
  assert(outcome.count == 1);
}

void testRejectsNonJson() {
  AdsbAircraft aircraft[ADSB_MAX_AIRCRAFT];
  assert(adsbParseAircraft("<html>chyba proxy</html>", aircraft,
                           ADSB_MAX_AIRCRAFT)
             .status == AdsbParseStatus::NotJson);
  assert(adsbParseAircraft("", aircraft, ADSB_MAX_AIRCRAFT).status ==
         AdsbParseStatus::NotJson);
  assert(adsbParseAircraft(nullptr, aircraft, ADSB_MAX_AIRCRAFT).status ==
         AdsbParseStatus::NotJson);
}

void testReportsMissingArrayWithMessage() {
  // Platný JSON špatného tvaru: server řekne proč a opakování by nepomohlo.
  const char *const payload = "{\"msg\":\"No data\",\"now\":1788809424}";
  AdsbAircraft aircraft[ADSB_MAX_AIRCRAFT];
  const AdsbParseOutcome outcome =
      adsbParseAircraft(payload, aircraft, ADSB_MAX_AIRCRAFT);
  assert(outcome.status == AdsbParseStatus::MissingArray);
  assert(outcome.count == 0);
  assert(std::string(outcome.message) == "No data");
}

void testEmptySkyIsSuccess() {
  // Prázdná obloha je platná odpověď, ne chyba - jinak by radar držel stará
  // letadla, dokud nějaké nepřiletí.
  const char *const payload = "{\"ac\":[],\"msg\":\"No error\"}";
  AdsbAircraft aircraft[ADSB_MAX_AIRCRAFT];
  const AdsbParseOutcome outcome =
      adsbParseAircraft(payload, aircraft, ADSB_MAX_AIRCRAFT);
  assert(outcome.status == AdsbParseStatus::Ok);
  assert(outcome.count == 0);
}

void testHonoursCapacity() {
  std::string payload = "{\"ac\":[";
  for (int index = 0; index < 10; ++index) {
    if (index > 0) payload += ",";
    payload += "{\"hex\":\"aaaaa" + std::to_string(index) +
               "\",\"lat\":50.2,\"lon\":14.3}";
  }
  payload += "]}";
  AdsbAircraft aircraft[4];
  const AdsbParseOutcome outcome =
      adsbParseAircraft(payload.c_str(), aircraft, 4);
  assert(outcome.status == AdsbParseStatus::Ok);
  assert(outcome.count == 4);
  assert(std::string(aircraft[3].hex) == "aaaaa3");
}

void testKeyInsideStringValueIsNotMistaken() {
  // Klíč schovaný v textu jiné hodnoty se nesmí sebrat místo toho pravého -
  // proto se prochází po dvojicích, ne hledáním podřetězce.
  const char *const payload =
      "{\"ac\":[{\"hex\":\"aaaaaa\",\"t\":\"A\\\"lat\\\":9,\","
      "\"lat\":50.2,\"lon\":14.3}]}";
  AdsbAircraft aircraft[ADSB_MAX_AIRCRAFT];
  const AdsbParseOutcome outcome =
      adsbParseAircraft(payload, aircraft, ADSB_MAX_AIRCRAFT);
  assert(outcome.count == 1);
  assert(aircraft[0].latitude > 50.19f && aircraft[0].latitude < 50.21f);
}

void testNestedObjectsAreSkipped() {
  // Některé servery přidávají vnořené objekty a pole; hodnota za nimi se musí
  // pořád najít.
  const char *const payload =
      "{\"ac\":[{\"hex\":\"aaaaaa\",\"mlat\":[],\"tisb\":[],"
      "\"extra\":{\"a\":{\"b\":1}},\"lat\":50.2,\"lon\":14.3,"
      "\"alt_baro\":4000}]}";
  AdsbAircraft aircraft[ADSB_MAX_AIRCRAFT];
  const AdsbParseOutcome outcome =
      adsbParseAircraft(payload, aircraft, ADSB_MAX_AIRCRAFT);
  assert(outcome.count == 1);
  assert(aircraft[0].altitudeFt == 4000.0f);
}

void testPrettyPrintedResponse() {
  // Server, který odpověď formátuje na řádky, nechá za číslem konec řádku
  // a mezery. Do hodnoty nepatří - jinak by se výška četla jako text a letadlo
  // by se tvářilo, že ji nehlásí.
  const char *const payload =
      "{\n"
      "  \"ac\": [\n"
      "    {\n"
      "      \"hex\": \"49d0d1\",\n"
      "      \"lat\": 50.104,\n"
      "      \"lon\": 14.26,\n"
      "      \"gs\": 420,\n"
      "      \"alt_baro\": 35000\n"
      "    }\n"
      "  ],\n"
      "  \"msg\": \"No error\"\n"
      "}";
  AdsbAircraft aircraft[ADSB_MAX_AIRCRAFT];
  const AdsbParseOutcome outcome =
      adsbParseAircraft(payload, aircraft, ADSB_MAX_AIRCRAFT);
  assert(outcome.status == AdsbParseStatus::Ok);
  assert(outcome.count == 1);
  assert(aircraft[0].altitudeFt == 35000.0f);
  assert(aircraft[0].groundSpeedKt == 420.0f);
  assert(std::string(outcome.message) == "No error");
}

void testStrayBraceDoesNotHang() {
  // Přebytečná závorka uvnitř pole nesmí smyčku rozboru zacyklit: úloha radaru
  // by se točila bez jediného uvolnění procesoru a hlídací pes by restartoval
  // celé hodiny.
  const char *const payload =
      "{\"ac\":[{\"hex\":\"49d0d1\",\"lat\":50.1,\"lon\":14.4}}],"
      "\"msg\":\"No error\"}";
  AdsbAircraft aircraft[ADSB_MAX_AIRCRAFT];
  const AdsbParseOutcome outcome =
      adsbParseAircraft(payload, aircraft, ADSB_MAX_AIRCRAFT);
  // Vrátit se musí, ať už s letadlem nebo bez něj - podstatné je, že vrátí.
  assert(outcome.count <= 1);
}

void testUnclosedArrayDoesNotHang() {
  const char *const payload = "{\"ac\":[{\"hex\":\"a\"},,,]}";
  AdsbAircraft aircraft[ADSB_MAX_AIRCRAFT];
  const AdsbParseOutcome outcome =
      adsbParseAircraft(payload, aircraft, ADSB_MAX_AIRCRAFT);
  assert(outcome.count == 0);
}

void testNegativeAltitudeCountsAsReported() {
  // Barometrická výška smí být lehce pod nulou - letiště pod hladinou moře nebo
  // tlak nad standardem. Taková hodnota je platná, jen nízká, a nesmí se
  // splést s "výšku nehlásí": to by letadlo obarvilo šedě a pustilo přes
  // výškový filtr.
  const char *const payload =
      "{\"ac\":[{\"hex\":\"aaaaaa\",\"lat\":52.3,\"lon\":4.76,"
      "\"alt_baro\":-75}]}";
  AdsbAircraft aircraft[ADSB_MAX_AIRCRAFT];
  const AdsbParseOutcome outcome =
      adsbParseAircraft(payload, aircraft, ADSB_MAX_AIRCRAFT);
  assert(outcome.count == 1);
  assert(aircraft[0].hasAltitude);
  assert(aircraft[0].altitudeFt == -75.0f);

  // Chybějící pole je naopak opravdu neznámá výška.
  const char *const missing =
      "{\"ac\":[{\"hex\":\"bbbbbb\",\"lat\":50.1,\"lon\":14.4}]}";
  const AdsbParseOutcome second =
      adsbParseAircraft(missing, aircraft, ADSB_MAX_AIRCRAFT);
  assert(second.count == 1);
  assert(!aircraft[0].hasAltitude);
  assert(aircraft[0].altitudeFt == 0.0f);
}

void testFindByHex() {
  AdsbAircraft aircraft[3];
  strcpy(aircraft[0].hex, "aaaaaa");
  strcpy(aircraft[1].hex, "bbbbbb");
  strcpy(aircraft[2].hex, "cccccc");
  assert(adsbFindByHex(aircraft, 3, "bbbbbb") == 1);
  assert(adsbFindByHex(aircraft, 3, "dddddd") == -1);
  assert(adsbFindByHex(aircraft, 3, "") == -1);
  assert(adsbFindByHex(aircraft, 3, nullptr) == -1);
  assert(adsbFindByHex(nullptr, 0, "aaaaaa") == -1);
}

void testEmergencyCodes() {
  AdsbAircraft aircraft;
  strcpy(aircraft.squawk, "7500");
  assert(std::string(adsbEmergencyCode(aircraft)) == "7500");
  strcpy(aircraft.squawk, "7600");
  assert(std::string(adsbEmergencyCode(aircraft)) == "7600");
  strcpy(aircraft.squawk, "7700");
  assert(std::string(adsbEmergencyCode(aircraft)) == "7700");
  strcpy(aircraft.squawk, "1000");
  assert(adsbEmergencyCode(aircraft) == nullptr);
  aircraft.squawk[0] = '\0';
  assert(adsbEmergencyCode(aircraft) == nullptr);
}

void testEmergencySeverityOrdering() {
  // Únos je horší než obecná nouze a ta horší než porucha rádia. Obrazovka má
  // na hlášku jediný řádek, takže se musí dát seřadit.
  assert(adsbEmergencySeverity(nullptr) == 0);
  assert(adsbEmergencySeverity("") == 0);
  assert(adsbEmergencySeverity("1000") == 0);
  assert(adsbEmergencySeverity("7600") < adsbEmergencySeverity("7700"));
  assert(adsbEmergencySeverity("7700") < adsbEmergencySeverity("7500"));
}

void testLongFieldsAreTruncatedNotOverflowed() {
  const char *const payload =
      "{\"ac\":[{\"hex\":\"aaaaaaaaaaaaaaaaaaaa\","
      "\"flight\":\"ABCDEFGHIJKLMNOP\","
      "\"r\":\"REGISTRACEDLOUHA\",\"t\":\"TYPTYPTYPTYP\","
      "\"desc\":\"VYROBCE MODEL S NEKONECNE DLOUHYM JMENEM TYPU\","
      "\"lat\":50.2,\"lon\":14.3}]}";
  AdsbAircraft aircraft[ADSB_MAX_AIRCRAFT];
  const AdsbParseOutcome outcome =
      adsbParseAircraft(payload, aircraft, ADSB_MAX_AIRCRAFT);
  assert(outcome.count == 1);
  assert(strlen(aircraft[0].hex) == sizeof(aircraft[0].hex) - 1);
  assert(strlen(aircraft[0].callsign) == sizeof(aircraft[0].callsign) - 1);
  assert(strlen(aircraft[0].registration) ==
         sizeof(aircraft[0].registration) - 1);
  assert(strlen(aircraft[0].type) == sizeof(aircraft[0].type) - 1);
  assert(strlen(aircraft[0].description) ==
         sizeof(aircraft[0].description) - 1);
}

void testTruncatedPayloadIsRejectedWhole() {
  // Useknutá odpověď se zahodí celá, ne po letadlech. Půlka oblohy vypadá jako
  // platná data, ale není - volající si má nechat předchozí snímek a stáhnout
  // znovu. Nesmí se přitom číst za konec bufferu.
  const char *const payload =
      "{\"ac\":[{\"hex\":\"aaaaaa\",\"lat\":50.2,\"lon\":14.3},"
      "{\"hex\":\"bbbbbb\",\"lat\":49.9";
  AdsbAircraft aircraft[ADSB_MAX_AIRCRAFT];
  const AdsbParseOutcome outcome =
      adsbParseAircraft(payload, aircraft, ADSB_MAX_AIRCRAFT);
  assert(outcome.status == AdsbParseStatus::MissingArray);
  assert(outcome.count == 0);
}

}  // namespace

int main() {
  testRealResponse();
  testGroundTrafficIsDropped();
  testAircraftWithoutPositionIsDropped();
  testMissingTrackIsMarked();
  testNumbersSentAsStrings();
  testNumericSquawkKeepsLeadingZero();
  testAlternateArrayName();
  testRejectsNonJson();
  testReportsMissingArrayWithMessage();
  testEmptySkyIsSuccess();
  testHonoursCapacity();
  testKeyInsideStringValueIsNotMistaken();
  testNestedObjectsAreSkipped();
  testPrettyPrintedResponse();
  testStrayBraceDoesNotHang();
  testUnclosedArrayDoesNotHang();
  testNegativeAltitudeCountsAsReported();
  testFindByHex();
  testEmergencyCodes();
  testEmergencySeverityOrdering();
  testLongFieldsAreTruncatedNotOverflowed();
  testTruncatedPayloadIsRejectedWhole();
  printf("adsb_parser OK\n");
  return 0;
}
