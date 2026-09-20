// Ověří perzistenci a migrace ClockConfig na počítači. Firmwarové NVS
// nahrazuje paměťová vrstva v tools/hostshim, takže test běží bez zařízení.
//
// Překlad viz tools/run_host_tests.sh.

#include <cassert>
#include <cstring>
#include <string>

#include "ClockConfig.h"
#include "Preferences.h"

namespace {

// Musí odpovídat privátním konstantám v ClockConfig.cpp. Rozejití se projeví
// selháním migračních testů níže, což je záměr.
constexpr uint32_t CONFIG_MAGIC = 0x57484346;
constexpr char CONFIG_PARTITION[] = "clockcfg";
constexpr char CONFIG_NAMESPACE[] = "clock-config";
constexpr char CONFIG_KEY[] = "config";
constexpr uint32_t SCHEMA_27 = 27;
constexpr size_t SCHEMA_27_CONFIG_SIZE = offsetof(ClockConfig, leftValue);
constexpr size_t SCHEMA_27_RECORD_SIZE =
    sizeof(uint32_t) * 3 + SCHEMA_27_CONFIG_SIZE;
constexpr uint32_t SCHEMA_28 = 28;
constexpr size_t SCHEMA_28_CONFIG_SIZE = offsetof(ClockConfig, slots);
constexpr size_t SCHEMA_28_RECORD_SIZE =
    sizeof(uint32_t) * 3 + SCHEMA_28_CONFIG_SIZE;
constexpr uint32_t SCHEMA_29 = 29;
constexpr size_t SCHEMA_29_CONFIG_SIZE = offsetof(ClockConfig, rss);
constexpr size_t SCHEMA_29_RECORD_SIZE =
    sizeof(uint32_t) * 3 + SCHEMA_29_CONFIG_SIZE;
constexpr uint32_t SCHEMA_30 = 30;
constexpr size_t SCHEMA_30_CONFIG_SIZE = offsetof(ClockConfig, bottomSlot);
constexpr size_t SCHEMA_30_RECORD_SIZE =
    sizeof(uint32_t) * 3 + SCHEMA_30_CONFIG_SIZE;
constexpr uint32_t SCHEMA_32 = 32;
// Schéma 32 končilo boolem, takže jeho záznam nesl i dva bajty zarovnávací
// výplně; bez nich by kontrolní součet neseděl.
constexpr size_t SCHEMA_32_CONFIG_SIZE =
    (offsetof(ClockConfig, radarStatusLine) + alignof(ClockConfig) - 1) /
    alignof(ClockConfig) * alignof(ClockConfig);
constexpr size_t SCHEMA_32_RECORD_SIZE =
    sizeof(uint32_t) * 3 + SCHEMA_32_CONFIG_SIZE;
constexpr uint32_t SCHEMA_34 = 34;
constexpr size_t SCHEMA_34_CONFIG_SIZE = offsetof(ClockConfig, planes);
constexpr size_t SCHEMA_34_RECORD_SIZE =
    sizeof(uint32_t) * 3 + SCHEMA_34_CONFIG_SIZE;

constexpr uint32_t SCHEMA_36 = 36;
constexpr uint32_t SCHEMA_37 = 37;
// Velikost bere ClockConfig.h: schéma 37 končilo agendou s polem char[192],
// za kterou uložený záznam nese ještě dva bajty koncové výplně.
constexpr size_t SCHEMA_37_CONFIG_SIZE = CLOCK_CONFIG_SCHEMA_37_SIZE;
constexpr size_t SCHEMA_37_RECORD_SIZE =
    sizeof(uint32_t) * 3 + SCHEMA_37_CONFIG_SIZE;
// Schéma 36 končilo pětibajtovým pořadím obrazovek, které zarovnání dorovnalo
// na hranici čtyř bajtů - tedy přesně tam, kde v schématu 37 začíná agenda.
constexpr size_t SCHEMA_36_CONFIG_SIZE = offsetof(ClockConfig, agenda);
constexpr size_t SCHEMA_36_RECORD_SIZE =
    sizeof(uint32_t) * 3 + SCHEMA_36_CONFIG_SIZE;

constexpr uint32_t SCHEMA_35 = 35;
constexpr size_t SCHEMA_35_CONFIG_SIZE = offsetof(ClockConfig, screenOrder);
constexpr size_t SCHEMA_35_RECORD_SIZE =
    sizeof(uint32_t) * 3 + SCHEMA_35_CONFIG_SIZE;

uint32_t fnv1a(const uint8_t *bytes, size_t size) {
  uint32_t hash = 2166136261u;
  for (size_t index = 0; index < size; ++index) {
    hash ^= bytes[index];
    hash *= 16777619u;
  }
  return hash;
}

// Poskládá surový NVS záznam staršího schématu z prefixu aktuální
// konfigurace. Každé starší schéma je bajtově shodný prefix toho novějšího,
// takže stačí useknutí na jeho velikost.
std::string legacyRecord(const ClockConfig &source, uint32_t schema,
                         size_t configSize, uint32_t magic = CONFIG_MAGIC,
                         bool corruptChecksum = false) {
  std::string record(sizeof(uint32_t) * 3 + configSize, '\0');
  uint8_t *bytes = reinterpret_cast<uint8_t *>(&record[0]);
  memcpy(bytes, &magic, sizeof(magic));
  memcpy(bytes + 4, &schema, sizeof(schema));
  memcpy(bytes + 8, &source, configSize);
  // Uložené schéma je i uvnitř payloadu; načtení ho kontroluje zvlášť.
  memcpy(bytes + 8, &schema, sizeof(schema));
  uint32_t checksum = fnv1a(bytes + 8, configSize);
  if (corruptChecksum) checksum ^= 0xFFFFFFFFu;
  memcpy(bytes + 8 + configSize, &checksum, sizeof(checksum));
  return record;
}

std::string schema27Record(const ClockConfig &source,
                           uint32_t magic = CONFIG_MAGIC,
                           bool corruptChecksum = false) {
  return legacyRecord(source, SCHEMA_27, SCHEMA_27_CONFIG_SIZE, magic,
                      corruptChecksum);
}

std::string schema28Record(const ClockConfig &source) {
  return legacyRecord(source, SCHEMA_28, SCHEMA_28_CONFIG_SIZE);
}

std::string schema29Record(const ClockConfig &source) {
  return legacyRecord(source, SCHEMA_29, SCHEMA_29_CONFIG_SIZE);
}

std::string schema30Record(const ClockConfig &source) {
  return legacyRecord(source, SCHEMA_30, SCHEMA_30_CONFIG_SIZE);
}

std::string schema32Record(const ClockConfig &source) {
  return legacyRecord(source, SCHEMA_32, SCHEMA_32_CONFIG_SIZE);
}

std::string schema34Record(const ClockConfig &source) {
  return legacyRecord(source, SCHEMA_34, SCHEMA_34_CONFIG_SIZE);
}

std::string schema35Record(const ClockConfig &source) {
  return legacyRecord(source, SCHEMA_35, SCHEMA_35_CONFIG_SIZE);
}

std::string schema37Record(const ClockConfig &source) {
  return legacyRecord(source, SCHEMA_37, SCHEMA_37_CONFIG_SIZE);
}

std::string schema36Record(const ClockConfig &source) {
  return legacyRecord(source, SCHEMA_36, SCHEMA_36_CONFIG_SIZE);
}

void seed(const std::string &record) {
  hostPreferencesSeedBlob(CONFIG_PARTITION, CONFIG_NAMESPACE, CONFIG_KEY,
                          record.data(), record.size());
}

size_t storedSize() {
  return hostPreferencesBlobSize(CONFIG_PARTITION, CONFIG_NAMESPACE,
                                 CONFIG_KEY);
}

// Prázdné NVS musí skončit u výchozí konfigurace, ne u nahodilých bajtů.
void testEmptyStorageUsesDefaults() {
  hostPreferencesReset();
  ClockConfig config;
  clockConfigLoad(config);
  assert(config.schemaVersion == CLOCK_CONFIG_SCHEMA_VERSION);
  assert(strcmp(config.openMeteoCity,
                "Ondřejov · Praha-východ · Středočeský kraj · Česko") == 0);
  assert(config.dataSource == CLOCK_DATA_SOURCE_OPEN_METEO);
  assert(strcmp(config.leftSide.name, "VENKU") == 0);
  // Střídání obrazovek je stejně jako u radaru vypnuté, dokud ho uživatel
  // sám nezapne.
  assert(!config.automaticRadarRotation);
  assert(!config.rss.automaticRotation);
}

// Uložení a načtení nesmí hodnoty měnit.
void testRoundTripPreservesValues() {
  hostPreferencesReset();
  ClockConfig saved;
  clockConfigApplyDefaults(saved);
  saved.dataSource = CLOCK_DATA_SOURCE_HOME_ASSISTANT;
  clockConfigCopy(saved.homeAssistantUrl, sizeof(saved.homeAssistantUrl),
                  "http://homeassistant.local:8123");
  clockConfigCopy(saved.leftSide.name, sizeof(saved.leftSide.name), "LOŽNICE");
  clockConfigCopy(saved.leftSide.temperatureEntityId,
                  sizeof(saved.leftSide.temperatureEntityId),
                  "sensor.loznice_teplota");
  saved.metricA.decimals = 2;
  assert(clockConfigSave(saved));

  ClockConfig loaded;
  assert(clockConfigLoad(loaded));
  assert(loaded.dataSource == CLOCK_DATA_SOURCE_HOME_ASSISTANT);
  assert(strcmp(loaded.homeAssistantUrl,
                "http://homeassistant.local:8123") == 0);
  assert(strcmp(loaded.leftSide.name, "LOŽNICE") == 0);
  assert(strcmp(loaded.leftSide.temperatureEntityId,
                "sensor.loznice_teplota") == 0);
  assert(loaded.metricA.decimals == 2);
}

// Migrace 27 → 28 musí zachovat vše, co starší firmware uložil.
void testSchema27MigrationKeepsStoredValues() {
  hostPreferencesReset();
  ClockConfig legacy;
  clockConfigApplyDefaults(legacy);
  legacy.dataSource = CLOCK_DATA_SOURCE_HOME_ASSISTANT;
  clockConfigCopy(legacy.leftSide.name, sizeof(legacy.leftSide.name), "VENKU");
  clockConfigCopy(legacy.leftSide.temperatureEntityId,
                  sizeof(legacy.leftSide.temperatureEntityId),
                  "sensor.venkovni_teplota");
  legacy.leftSide.color = 0x4CCBEC;
  clockConfigCopy(legacy.rightSide.name, sizeof(legacy.rightSide.name),
                  "OBÝVÁK");
  legacy.rightSide.color = 0xFFB843;
  clockConfigCopy(legacy.metricA.entityId, sizeof(legacy.metricA.entityId),
                  "sensor.obyvak_co2");
  legacy.dayBrightness = 42;
  seed(schema27Record(legacy));

  ClockConfig migrated;
  assert(clockConfigLoad(migrated));
  assert(migrated.schemaVersion == CLOCK_CONFIG_SCHEMA_VERSION);
  assert(migrated.dataSource == CLOCK_DATA_SOURCE_HOME_ASSISTANT);
  assert(strcmp(migrated.leftSide.temperatureEntityId,
                "sensor.venkovni_teplota") == 0);
  assert(strcmp(migrated.rightSide.name, "OBÝVÁK") == 0);
  assert(strcmp(migrated.metricA.entityId, "sensor.obyvak_co2") == 0);
  assert(migrated.dayBrightness == 42);

  // Nová pole schématu 28 dostanou zpětně kompatibilní výchozí hodnoty:
  // původní názvy stran zůstávají vlastní a barva se přenese do škály.
  assert(migrated.leftValue.custom);
  assert(strcmp(migrated.leftValue.preset, "custom") == 0);
  assert(migrated.rightValue.custom);
  assert(migrated.leftValueColorScale.count == 1);
  assert(migrated.leftValueColorScale.points[0].color == 0x4CCBEC);
  assert(migrated.rightValueColorScale.points[0].color == 0xFFB843);

  // Migrace se musí zapsat zpět, jinak proběhne při každém startu znovu.
  assert(storedSize() == sizeof(uint32_t) * 3 + sizeof(ClockConfig));
  ClockConfig reloaded;
  assert(clockConfigLoad(reloaded));
  assert(strcmp(reloaded.leftSide.temperatureEntityId,
                "sensor.venkovni_teplota") == 0);
}

// Poškozený záznam nesmí projít; firmware musí spadnout zpět na výchozí stav.
void testCorruptRecordFallsBackToDefaults() {
  hostPreferencesReset();
  ClockConfig legacy;
  clockConfigApplyDefaults(legacy);
  clockConfigCopy(legacy.leftSide.name, sizeof(legacy.leftSide.name), "SKLEP");
  seed(schema27Record(legacy, CONFIG_MAGIC, /*corruptChecksum=*/true));

  ClockConfig config;
  clockConfigLoad(config);
  assert(strcmp(config.leftSide.name, "VENKU") == 0);

  hostPreferencesReset();
  seed(schema27Record(legacy, /*magic=*/0xDEADBEEF));
  clockConfigLoad(config);
  assert(strcmp(config.leftSide.name, "VENKU") == 0);
}

// Normalizace musí uříznout hodnoty mimo povolený rozsah i při načtení.
void testNormalizationClampsStoredValues() {
  hostPreferencesReset();
  ClockConfig saved;
  clockConfigApplyDefaults(saved);
  saved.dayBrightness = 250;
  saved.nightBrightness = 0;
  saved.metricA.decimals = 9;
  saved.sunriseOffsetMinutes = 120;
  assert(clockConfigSave(saved));

  ClockConfig loaded;
  assert(clockConfigLoad(loaded));
  assert(loaded.dayBrightness == 100);
  assert(loaded.nightBrightness == 1);
  assert(loaded.metricA.decimals == 2);
  assert(loaded.sunriseOffsetMinutes == 60);
}

// Migrace 28 -> 29 musí přenést všechny čtyři původní pozice do slots[0..3]
// se vším, co uživatel nastavil, a zbytek nechat vypnutý.
void testSchema28MigrationSeedsValueSlots() {
  hostPreferencesReset();
  ClockConfig legacy;
  clockConfigApplyDefaults(legacy);
  legacy.dataSource = CLOCK_DATA_SOURCE_HOME_ASSISTANT;

  clockConfigCopy(legacy.leftSide.name, sizeof(legacy.leftSide.name), "VENKU");
  clockConfigCopy(legacy.leftSide.temperatureEntityId,
                  sizeof(legacy.leftSide.temperatureEntityId),
                  "sensor.venkovni_teplota");
  clockConfigCopy(legacy.leftSide.icon, sizeof(legacy.leftSide.icon),
                  "weather");
  legacy.leftSide.color = 0x4CCBEC;
  legacy.leftValue.decimals = 1;
  clockConfigCopy(legacy.leftValue.suffix, sizeof(legacy.leftValue.suffix),
                  "°C");
  legacy.leftValueColorScale.count = 2;
  legacy.leftValueColorScale.points[0] = {0.0f, 0x4CCBEC};
  legacy.leftValueColorScale.points[1] = {25.0f, 0xFF0000};

  clockConfigCopy(legacy.rightSide.name, sizeof(legacy.rightSide.name),
                  "OBÝVÁK");
  clockConfigCopy(legacy.rightSide.icon, sizeof(legacy.rightSide.icon), "sofa");
  legacy.rightSide.color = 0xFFB843;

  legacy.metricA.custom = true;
  clockConfigCopy(legacy.metricA.name, sizeof(legacy.metricA.name), "VOC");
  clockConfigCopy(legacy.metricA.entityId, sizeof(legacy.metricA.entityId),
                  "sensor.obyvak_voc");
  clockConfigCopy(legacy.metricA.suffix, sizeof(legacy.metricA.suffix), "ppb");
  legacy.metricA.decimals = 0;
  legacy.metricAColorScale.points[0] = {0.0f, 0x65C744};

  clockConfigCopy(legacy.metricB.entityId, sizeof(legacy.metricB.entityId),
                  "sensor.obyvak_co2");

  seed(schema28Record(legacy));

  ClockConfig migrated;
  assert(clockConfigLoad(migrated));
  assert(migrated.schemaVersion == CLOCK_CONFIG_SCHEMA_VERSION);

  // Původní pole zůstávají nedotčená, obě obrazovky tak čtou totéž.
  assert(migrated.dataSource == CLOCK_DATA_SOURCE_HOME_ASSISTANT);
  assert(strcmp(migrated.leftSide.temperatureEntityId,
                "sensor.venkovni_teplota") == 0);
  assert(strcmp(migrated.metricA.entityId, "sensor.obyvak_voc") == 0);

  // slots[0] = levá teplota i s ikonou, jednotkou a celou barevnou škálou.
  assert(migrated.slots[0].enabled);
  assert(strcmp(migrated.slots[0].name, "VENKU") == 0);
  assert(strcmp(migrated.slots[0].entityId, "sensor.venkovni_teplota") == 0);
  assert(strcmp(migrated.slots[0].icon, "weather") == 0);
  assert(strcmp(migrated.slots[0].suffix, "°C") == 0);
  assert(migrated.slots[0].decimals == 1);
  assert(migrated.slots[0].color == 0x4CCBEC);
  assert(migrated.slots[0].colorScale.count == 2);
  assert(migrated.slots[0].colorScale.points[1].value == 25.0f);
  assert(migrated.slots[0].colorScale.points[1].color == 0xFF0000);

  // slots[1] = pravá teplota.
  assert(migrated.slots[1].enabled);
  assert(strcmp(migrated.slots[1].name, "OBÝVÁK") == 0);
  assert(strcmp(migrated.slots[1].icon, "sofa") == 0);
  assert(migrated.slots[1].color == 0xFFB843);

  // slots[2] a slots[3] = měřené hodnoty A a B, které ikonu nikdy neměly.
  assert(migrated.slots[2].enabled);
  assert(migrated.slots[2].custom);
  assert(strcmp(migrated.slots[2].name, "VOC") == 0);
  assert(strcmp(migrated.slots[2].entityId, "sensor.obyvak_voc") == 0);
  assert(strcmp(migrated.slots[2].suffix, "ppb") == 0);
  assert(migrated.slots[2].decimals == 0);
  assert(strcmp(migrated.slots[2].icon, "none") == 0);
  assert(migrated.slots[2].colorScale.points[0].color == 0x65C744);
  assert(migrated.slots[3].enabled);
  assert(strcmp(migrated.slots[3].entityId, "sensor.obyvak_co2") == 0);

  // Zbylé čtyři pozice čekají vypnuté na uživatele.
  for (size_t index = 4; index < CLOCK_VALUE_SLOT_COUNT; ++index) {
    assert(!clockConfigValueSlot(migrated, index).enabled);
    assert(clockConfigValueSlot(migrated, index).entityId[0] == '\0');
  }

  // Migrace se musí uložit zpět jako plný záznam schématu 29.
  assert(storedSize() == sizeof(uint32_t) * 3 + sizeof(ClockConfig));
  ClockConfig reloaded;
  assert(clockConfigLoad(reloaded));
  assert(strcmp(reloaded.slots[2].name, "VOC") == 0);
  assert(reloaded.slots[0].colorScale.count == 2);
}

// Uživatelem vyplněné sloty 4-7 musí přežít uložení i načtení.
void testValueSlotsRoundTrip() {
  hostPreferencesReset();
  ClockConfig saved;
  clockConfigApplyDefaults(saved);
  saved.slots[6].enabled = true;
  clockConfigCopy(saved.slots[6].name, sizeof(saved.slots[6].name), "LOŽNICE");
  clockConfigCopy(saved.slots[6].entityId, sizeof(saved.slots[6].entityId),
                  "sensor.loznice_teplota");
  clockConfigCopy(saved.slots[6].icon, sizeof(saved.slots[6].icon), "bed");
  saved.slots[6].decimals = 1;
  saved.slots[6].color = 0x65C744;
  assert(clockConfigSave(saved));

  ClockConfig loaded;
  assert(clockConfigLoad(loaded));
  assert(loaded.slots[6].enabled);
  assert(strcmp(loaded.slots[6].name, "LOŽNICE") == 0);
  assert(strcmp(loaded.slots[6].entityId, "sensor.loznice_teplota") == 0);
  assert(strcmp(loaded.slots[6].icon, "bed") == 0);
  assert(loaded.slots[6].color == 0x65C744);
  assert(!loaded.slots[7].enabled);
}

// Sloty musí procházet stejnou normalizací jako starší pozice: desetinná
// místa 0-2, barva bez alfa kanálu a barevná škála seřazená a bez prázdného
// počtu bodů.
void testNormalizationClampsValueSlots() {
  hostPreferencesReset();
  ClockConfig saved;
  clockConfigApplyDefaults(saved);
  saved.slots[5].enabled = true;
  saved.slots[5].decimals = 9;
  saved.slots[5].color = 0xAB65C744;
  saved.slots[5].colorScale.count = 0;
  saved.slots[4].enabled = true;
  saved.slots[4].colorScale.count = 3;
  saved.slots[4].colorScale.points[0] = {30.0f, 0xFF0000};
  saved.slots[4].colorScale.points[1] = {10.0f, 0x00FF00};
  saved.slots[4].colorScale.points[2] = {20.0f, 0xCC0000FF};
  assert(clockConfigSave(saved));

  ClockConfig loaded;
  assert(clockConfigLoad(loaded));
  assert(loaded.slots[5].decimals == 2);
  assert(loaded.slots[5].color == 0x65C744);
  assert(loaded.slots[5].colorScale.count == 1);
  assert(loaded.slots[4].colorScale.count == 3);
  assert(loaded.slots[4].colorScale.points[0].value == 10.0f);
  assert(loaded.slots[4].colorScale.points[1].value == 20.0f);
  assert(loaded.slots[4].colorScale.points[2].value == 30.0f);
  assert(loaded.slots[4].colorScale.points[1].color == 0x0000FF);
}

// Nová obrazovka musí projít i skrz ukládání vzhledu, které styl ořezává.
void testValuesStyleSurvivesAppearanceSave() {
  hostPreferencesReset();
  ClockAppearanceConfig appearance;
  appearance.style = CLOCK_STYLE_VALUES;
  assert(clockAppearanceSave(appearance));

  ClockAppearanceConfig loaded;
  assert(clockAppearanceLoad(loaded));
  assert(loaded.style == CLOCK_STYLE_VALUES);
}

}  // namespace

// Migrace 29 -> 30 nesmí sáhnout na nic ze schématu 29 a obrazovka zpráv se
// nesmí zapnout sama: bez adresy kanálu by rotace ukazovala prázdnou stránku.
void testSchema29MigrationAddsDisabledRss() {
  hostPreferencesReset();
  ClockConfig source;
  clockConfigApplyDefaults(source);
  source.dataSource = CLOCK_DATA_SOURCE_HOME_ASSISTANT;
  clockConfigCopy(source.leftSide.name, sizeof(source.leftSide.name), "VENKU");
  source.slots[5].enabled = true;
  clockConfigCopy(source.slots[5].name, sizeof(source.slots[5].name), "SKLEP");
  clockConfigCopy(source.slots[5].entityId, sizeof(source.slots[5].entityId),
                  "sensor.sklep_teplota");
  source.slots[5].colorScale.count = 2;
  source.slots[5].colorScale.points[1] = {18.0f, 0xFF0000};
  source.radarDisplaySeconds = 33;

  const std::string record = schema29Record(source);
  assert(record.size() == SCHEMA_29_RECORD_SIZE);
  seed(record);

  ClockConfig migrated;
  assert(clockConfigLoad(migrated));
  assert(migrated.schemaVersion == CLOCK_CONFIG_SCHEMA_VERSION);
  // Celý prefix schématu 29 zůstal nedotčený.
  assert(migrated.dataSource == CLOCK_DATA_SOURCE_HOME_ASSISTANT);
  assert(strcmp(migrated.leftSide.name, "VENKU") == 0);
  assert(migrated.slots[5].enabled);
  assert(strcmp(migrated.slots[5].name, "SKLEP") == 0);
  assert(strcmp(migrated.slots[5].entityId, "sensor.sklep_teplota") == 0);
  assert(migrated.slots[5].colorScale.count == 2);
  assert(migrated.slots[5].colorScale.points[1].value == 18.0f);
  assert(migrated.radarDisplaySeconds == 33);
  // Nová část je vypnutá a prázdná.
  assert(!migrated.rss.enabled);
  assert(migrated.rss.url[0] == '\0');
  assert(!clockConfigRssAvailable(migrated));
  assert(migrated.rss.itemCount == 5);
  assert(migrated.rss.refreshMinutes == 10);
  assert(!migrated.rss.automaticRotation);

  // Migrace se musí uložit v novém formátu, aby proběhla jen jednou.
  assert(storedSize() == sizeof(uint32_t) * 3 + sizeof(ClockConfig));
}

// Nastavení kanálu musí přežít uložení i načtení a nesmyslné hodnoty se
// musí srovnat do povoleného rozsahu.
void testRssRoundTripAndClamping() {
  hostPreferencesReset();
  ClockConfig saved;
  clockConfigApplyDefaults(saved);
  saved.rss.enabled = true;
  saved.rss.automaticRotation = false;
  saved.rss.itemCount = 4;
  saved.rss.refreshMinutes = 15;
  saved.rss.displaySeconds = 25;
  clockConfigCopy(saved.rss.url, sizeof(saved.rss.url),
                  "https://www.irozhlas.cz/rss/irozhlas");
  assert(clockConfigSave(saved));

  ClockConfig loaded;
  assert(clockConfigLoad(loaded));
  assert(loaded.rss.enabled);
  assert(!loaded.rss.automaticRotation);
  assert(loaded.rss.itemCount == 4);
  assert(loaded.rss.refreshMinutes == 15);
  assert(loaded.rss.displaySeconds == 25);
  assert(strcmp(loaded.rss.url, "https://www.irozhlas.cz/rss/irozhlas") == 0);
  assert(clockConfigRssAvailable(loaded));

  // Zapnutý kanál bez adresy se nesmí považovat za dostupný.
  loaded.rss.url[0] = '\0';
  assert(!clockConfigRssAvailable(loaded));

  hostPreferencesReset();
  ClockConfig extreme;
  clockConfigApplyDefaults(extreme);
  extreme.rss.itemCount = 99;
  extreme.rss.refreshMinutes = 1;
  extreme.rss.displaySeconds = 5;
  assert(clockConfigSave(extreme));
  ClockConfig clamped;
  assert(clockConfigLoad(clamped));
  assert(clamped.rss.itemCount == CLOCK_RSS_MAX_ITEMS);
  assert(clamped.rss.refreshMinutes == 5);
  assert(clamped.rss.displaySeconds == 10);

  hostPreferencesReset();
  ClockConfig tooFew;
  clockConfigApplyDefaults(tooFew);
  tooFew.rss.itemCount = 0;
  assert(clockConfigSave(tooFew));
  ClockConfig raised;
  assert(clockConfigLoad(raised));
  assert(raised.rss.itemCount == CLOCK_RSS_MIN_ITEMS);
}

// Migrace 30 -> 31 nesmí sáhnout na nic ze schématu 30 a devátá hodnota se
// nesmí zapnout sama: mřížka by se bez vědomí majitele prodloužila o řádek.
void testSchema30MigrationAddsDisabledBottomSlot() {
  hostPreferencesReset();
  ClockConfig source;
  clockConfigApplyDefaults(source);
  source.dataSource = CLOCK_DATA_SOURCE_HOME_ASSISTANT;
  source.slots[7].enabled = true;
  clockConfigCopy(source.slots[7].name, sizeof(source.slots[7].name), "GARÁŽ");
  clockConfigCopy(source.slots[7].entityId, sizeof(source.slots[7].entityId),
                  "sensor.garaz_teplota");
  source.rss.enabled = true;
  clockConfigCopy(source.rss.url, sizeof(source.rss.url),
                  "https://www.irozhlas.cz/rss/irozhlas");
  source.rss.itemCount = 4;

  const std::string record = schema30Record(source);
  assert(record.size() == SCHEMA_30_RECORD_SIZE);
  seed(record);

  ClockConfig migrated;
  assert(clockConfigLoad(migrated));
  assert(migrated.schemaVersion == CLOCK_CONFIG_SCHEMA_VERSION);
  // Celý prefix schématu 30 zůstal nedotčený, včetně obrazovky zpráv na konci.
  assert(migrated.dataSource == CLOCK_DATA_SOURCE_HOME_ASSISTANT);
  assert(migrated.slots[7].enabled);
  assert(strcmp(migrated.slots[7].name, "GARÁŽ") == 0);
  assert(strcmp(migrated.slots[7].entityId, "sensor.garaz_teplota") == 0);
  assert(migrated.rss.enabled);
  assert(strcmp(migrated.rss.url, "https://www.irozhlas.cz/rss/irozhlas") == 0);
  assert(migrated.rss.itemCount == 4);
  // Devátý slot je vypnutý a prázdný.
  assert(!clockConfigValueSlot(migrated, 8).enabled);
  assert(clockConfigValueSlot(migrated, 8).entityId[0] == '\0');

  // Migrace se musí uložit v novém formátu, aby proběhla jen jednou.
  assert(storedSize() == sizeof(uint32_t) * 3 + sizeof(ClockConfig));
}

// Devátá hodnota leží kvůli migraci mimo pole slots, takže musí zvlášť projít
// uložením, načtením i normalizací.
void testBottomValueSlotRoundTripAndNormalization() {
  hostPreferencesReset();
  ClockConfig saved;
  clockConfigApplyDefaults(saved);
  ClockValueSlotConfig &bottom = clockConfigValueSlot(saved, 8);
  bottom.enabled = true;
  clockConfigCopy(bottom.name, sizeof(bottom.name), "TLAK");
  clockConfigCopy(bottom.entityId, sizeof(bottom.entityId), "sensor.tlak");
  clockConfigCopy(bottom.suffix, sizeof(bottom.suffix), "hPa");
  bottom.decimals = 9;
  bottom.color = 0xAB65C744;
  bottom.colorScale.count = 0;
  assert(clockConfigSave(saved));
  assert(&clockConfigValueSlot(saved, 8) == &saved.bottomSlot);
  assert(&clockConfigValueSlot(saved, 7) == &saved.slots[7]);

  ClockConfig loaded;
  assert(clockConfigLoad(loaded));
  const ClockValueSlotConfig &reloaded = clockConfigValueSlot(loaded, 8);
  assert(reloaded.enabled);
  assert(strcmp(reloaded.name, "TLAK") == 0);
  assert(strcmp(reloaded.entityId, "sensor.tlak") == 0);
  assert(strcmp(reloaded.suffix, "hPa") == 0);
  assert(reloaded.decimals == 2);
  assert(reloaded.color == 0x65C744);
  assert(reloaded.colorScale.count == 1);
  // Mřížka o devátou hodnotu nepřišla ani se nepřepsala.
  assert(!loaded.slots[7].enabled);
}

// Migrace 32 -> 33 nesmí sáhnout na nic ze schématu 32. Stavový řádek radaru
// se zapne, protože jde o kus obrazovky, ne o novou obrazovku navíc; entita
// teploty zůstane prázdná, dokud si ji majitel nevyplní.
void testSchema32MigrationAddsRadarStatusLine() {
  hostPreferencesReset();
  ClockConfig source;
  clockConfigApplyDefaults(source);
  source.dataSource = CLOCK_DATA_SOURCE_HOME_ASSISTANT;
  source.radarSource = CLOCK_RADAR_SOURCE_RAINVIEWER;
  source.radarLegend = false;
  source.radarRadiusKm = 100;
  clockConfigCopy(source.rss.url, sizeof(source.rss.url),
                  "https://www.irozhlas.cz/rss/irozhlas");
  source.rss.enabled = true;
  // Bajty, které schéma 32 mělo jako výplň, musí migrace přepsat výchozími
  // hodnotami, ne tím, co v nich zůstalo.
  source.radarStatusLine = false;
  clockConfigCopy(source.radarStatusTemperatureEntityId,
                  sizeof(source.radarStatusTemperatureEntityId),
                  "sensor.zbytek");

  const std::string record = schema32Record(source);
  assert(record.size() == SCHEMA_32_RECORD_SIZE);
  seed(record);

  ClockConfig migrated;
  assert(clockConfigLoad(migrated));
  assert(migrated.schemaVersion == CLOCK_CONFIG_SCHEMA_VERSION);
  // Celý prefix schématu 32 zůstal nedotčený, včetně jeho posledních polí.
  assert(migrated.dataSource == CLOCK_DATA_SOURCE_HOME_ASSISTANT);
  assert(migrated.radarSource == CLOCK_RADAR_SOURCE_RAINVIEWER);
  assert(!migrated.radarLegend);
  assert(migrated.radarRadiusKm == 100);
  assert(migrated.rss.enabled);
  assert(strcmp(migrated.rss.url, "https://www.irozhlas.cz/rss/irozhlas") == 0);
  // Nová pole dostanou výchozí hodnoty.
  assert(migrated.radarStatusLine);
  assert(migrated.radarStatusTemperatureEntityId[0] == '\0');

  // Migrace se musí uložit v novém formátu, aby proběhla jen jednou.
  assert(storedSize() == sizeof(uint32_t) * 3 + sizeof(ClockConfig));
}

// Povýšení na schéma 35 nesmí radar letadel zapnout samo od sebe: obrazovka by
// začala stahovat z adsb.fi, aniž o to kdo požádal.
void testSchema34MigrationAddsDisabledPlanes() {
  hostPreferencesReset();
  ClockConfig source;
  clockConfigApplyDefaults(source);
  source.dataSource = CLOCK_DATA_SOURCE_HOME_ASSISTANT;
  source.radarSource = CLOCK_RADAR_SOURCE_RAINVIEWER;
  source.radarStatusLine = false;
  source.forecast.enabled = true;
  source.forecast.dayCount = 4;
  source.forecast.airQuality = false;
  clockConfigCopy(source.rss.url, sizeof(source.rss.url),
                  "https://www.irozhlas.cz/rss/irozhlas");
  source.rss.enabled = true;

  const std::string record = schema34Record(source);
  assert(record.size() == SCHEMA_34_RECORD_SIZE);
  seed(record);

  ClockConfig migrated;
  assert(clockConfigLoad(migrated));
  assert(migrated.schemaVersion == CLOCK_CONFIG_SCHEMA_VERSION);
  // Celý prefix schématu 34 zůstal nedotčený, včetně předpovědi na jeho konci.
  assert(migrated.dataSource == CLOCK_DATA_SOURCE_HOME_ASSISTANT);
  assert(migrated.radarSource == CLOCK_RADAR_SOURCE_RAINVIEWER);
  assert(!migrated.radarStatusLine);
  assert(migrated.rss.enabled);
  assert(strcmp(migrated.rss.url, "https://www.irozhlas.cz/rss/irozhlas") == 0);
  assert(migrated.forecast.enabled);
  assert(migrated.forecast.dayCount == 4);
  assert(!migrated.forecast.airQuality);
  // Nová obrazovka zůstává vypnutá a mimo rotaci.
  assert(!migrated.planes.enabled);
  assert(!migrated.planes.automaticRotation);
  assert(!clockConfigPlanesAvailable(migrated));
  assert(migrated.planes.rangeIndex == 1);
  assert(migrated.planes.watchCallsign[0] == '\0');

  // Migrace se musí uložit v novém formátu, aby proběhla jen jednou.
  assert(storedSize() == sizeof(uint32_t) * 3 + sizeof(ClockConfig));
}

void testPlanesRoundTripAndClamping() {
  hostPreferencesReset();
  ClockConfig config;
  clockConfigApplyDefaults(config);
  config.planes.enabled = true;
  config.planes.automaticRotation = true;
  config.planes.rangeIndex = 3;
  config.planes.refreshSeconds = 12;
  config.planes.topBearingDeg = 90;
  config.planes.displaySeconds = 45;
  config.planes.altitudeMinFt = 1000;
  config.planes.altitudeMaxFt = 12000;
  config.planes.onlyWithCallsign = true;
  config.planes.squawkAlert = false;
  config.planes.metricUnits = false;
  clockConfigCopy(config.planes.watchCallsign,
                  sizeof(config.planes.watchCallsign), "CSA1234");
  assert(clockConfigSave(config));

  ClockConfig loaded;
  assert(clockConfigLoad(loaded));
  assert(loaded.planes.enabled);
  assert(loaded.planes.automaticRotation);
  assert(loaded.planes.rangeIndex == 3);
  assert(loaded.planes.refreshSeconds == 12);
  assert(loaded.planes.topBearingDeg == 90);
  assert(loaded.planes.displaySeconds == 45);
  assert(loaded.planes.altitudeMinFt == 1000);
  assert(loaded.planes.altitudeMaxFt == 12000);
  assert(loaded.planes.onlyWithCallsign);
  assert(!loaded.planes.squawkAlert);
  assert(!loaded.planes.metricUnits);
  assert(strcmp(loaded.planes.watchCallsign, "CSA1234") == 0);
  assert(clockConfigPlanesAvailable(loaded));

  // Nesmyslný dosah, perioda i azimut se musí srovnat do mezí.
  hostPreferencesReset();
  ClockConfig wild;
  clockConfigApplyDefaults(wild);
  wild.planes.rangeIndex = 9;
  wild.planes.refreshSeconds = 1;
  wild.planes.topBearingDeg = 400;
  wild.planes.displaySeconds = 5;
  assert(clockConfigSave(wild));
  ClockConfig clamped;
  assert(clockConfigLoad(clamped));
  assert(clamped.planes.rangeIndex == 1);
  assert(clamped.planes.refreshSeconds == 5);
  assert(clamped.planes.topBearingDeg == 0);
  assert(clamped.planes.displaySeconds == 10);

  // Nulová horní mez by schovala všechno, co výšku hlásí; u dolní meze přitom
  // nula znamená "bez omezení", takže je to snadný překlep.
  hostPreferencesReset();
  ClockConfig zeroCeiling;
  clockConfigApplyDefaults(zeroCeiling);
  zeroCeiling.planes.altitudeMinFt = 0;
  zeroCeiling.planes.altitudeMaxFt = 0;
  assert(clockConfigSave(zeroCeiling));
  ClockConfig ceilingFixed;
  assert(clockConfigLoad(ceilingFixed));
  assert(ceilingFixed.planes.altitudeMinFt == 0);
  assert(ceilingFixed.planes.altitudeMaxFt == CLOCK_PLANE_ALTITUDE_CEILING_FT);

  // Prohozené meze výšky filtr vypnou, místo aby schovaly celou oblohu.
  hostPreferencesReset();
  ClockConfig swapped;
  clockConfigApplyDefaults(swapped);
  swapped.planes.altitudeMinFt = 20000;
  swapped.planes.altitudeMaxFt = 3000;
  assert(clockConfigSave(swapped));
  ClockConfig fixed;
  assert(clockConfigLoad(fixed));
  assert(fixed.planes.altitudeMinFt == 0);
  assert(fixed.planes.altitudeMaxFt == CLOCK_PLANE_ALTITUDE_CEILING_FT);
}

// Migrace 35 → 36 nesmí obrazovky přeskládat: pořadí, které starší firmware
// neznal, začíná na zabudovaném.
void testSchema35MigrationAddsDefaultScreenOrder() {
  hostPreferencesReset();
  ClockConfig source;
  clockConfigApplyDefaults(source);
  source.planes.enabled = true;
  source.planes.rangeIndex = 3;
  clockConfigCopy(source.planes.watchCallsign,
                  sizeof(source.planes.watchCallsign), "CSA1234");
  source.forecast.enabled = true;

  const std::string record = schema35Record(source);
  assert(record.size() == SCHEMA_35_RECORD_SIZE);
  seed(record);

  ClockConfig migrated;
  assert(clockConfigLoad(migrated));
  assert(migrated.schemaVersion == CLOCK_CONFIG_SCHEMA_VERSION);
  // Celý prefix schématu 35 zůstal nedotčený, včetně radaru letadel na konci.
  assert(migrated.planes.enabled);
  assert(migrated.planes.rangeIndex == 3);
  assert(strcmp(migrated.planes.watchCallsign, "CSA1234") == 0);
  assert(migrated.forecast.enabled);
  // Nové pole drží zabudované pořadí obrazovek.
  assert(migrated.screenOrder[0] == CLOCK_SCREEN_CLOCK);
  assert(migrated.screenOrder[1] == CLOCK_SCREEN_RADAR);
  assert(migrated.screenOrder[2] == CLOCK_SCREEN_RSS);
  assert(migrated.screenOrder[3] == CLOCK_SCREEN_FORECAST);
  assert(migrated.screenOrder[4] == CLOCK_SCREEN_PLANES);

  // Migrace se musí uložit v novém formátu, aby proběhla jen jednou.
  assert(storedSize() == sizeof(uint32_t) * 3 + sizeof(ClockConfig));
}

// Migrace 36 → 37 nesmí sáhnout na přeskládané pořadí ani zapnout agendu:
// bez adresy serveru není co ukazovat.
void testSchema36MigrationAddsDisabledAgenda() {
  hostPreferencesReset();
  ClockConfig source;
  clockConfigApplyDefaults(source);
  source.rss.enabled = true;
  clockConfigCopy(source.rss.url, sizeof(source.rss.url),
                  "https://example.test/top.xml");
  source.planes.enabled = true;
  // Pořadí, které si majitel přeskládal ještě před agendou.
  source.screenOrder[0] = CLOCK_SCREEN_RSS;
  source.screenOrder[1] = CLOCK_SCREEN_CLOCK;
  source.screenOrder[2] = CLOCK_SCREEN_RADAR;
  source.screenOrder[3] = CLOCK_SCREEN_PLANES;
  source.screenOrder[4] = CLOCK_SCREEN_FORECAST;
  // Schéma 36 mělo pole dlouhé pět bajtů. Zbytek, který se do záznamu dostane,
  // je koncové zarovnání - u uložené konfigurace nuly, ne obrazovky.
  source.screenOrder[5] = 0;
  source.screenOrder[6] = 0;
  source.screenOrder[7] = 0;

  const std::string record = schema36Record(source);
  assert(record.size() == SCHEMA_36_RECORD_SIZE);
  seed(record);

  ClockConfig migrated;
  assert(clockConfigLoad(migrated));
  assert(migrated.schemaVersion == CLOCK_CONFIG_SCHEMA_VERSION);
  // Celý prefix schématu 36 zůstal nedotčený.
  assert(migrated.rss.enabled);
  assert(strcmp(migrated.rss.url, "https://example.test/top.xml") == 0);
  assert(migrated.planes.enabled);
  // Přeskládané pořadí přežilo a agenda se přidala na konec cyklu, ne
  // doprostřed. Nuly ze zarovnání se zahodily jako zdvojený ciferník.
  assert(migrated.screenOrder[0] == CLOCK_SCREEN_RSS);
  assert(migrated.screenOrder[1] == CLOCK_SCREEN_CLOCK);
  assert(migrated.screenOrder[2] == CLOCK_SCREEN_RADAR);
  assert(migrated.screenOrder[3] == CLOCK_SCREEN_PLANES);
  assert(migrated.screenOrder[4] == CLOCK_SCREEN_FORECAST);
  assert(migrated.screenOrder[5] == CLOCK_SCREEN_AGENDA);
  // Agenda zůstává vypnutá a bez adresy, takže se do rotace nedostane.
  assert(!migrated.agenda.enabled);
  assert(migrated.agenda.url[0] == '\0');
  assert(!clockConfigAgendaAvailable(migrated));

  // Migrace se musí uložit v novém formátu, aby proběhla jen jednou.
  assert(storedSize() == sizeof(uint32_t) * 3 + sizeof(ClockConfig));
}

void testSchema37MigrationKeepsAskingAdsbDirectly() {
  hostPreferencesReset();
  ClockConfig source;
  clockConfigApplyDefaults(source);
  source.planes.enabled = true;
  source.planes.rangeIndex = 3;
  clockConfigCopy(source.planes.watchCallsign,
                  sizeof(source.planes.watchCallsign), "CSA1234");
  source.agenda.enabled = true;
  clockConfigCopy(source.agenda.url, sizeof(source.agenda.url),
                  "https://example.test/agenda.json");

  std::string record = schema37Record(source);
  assert(record.size() == SCHEMA_37_RECORD_SIZE);
  // Do koncové výplně schématu 37 se nasype smetí. Na skutečné desce v ní bývá
  // nula, ale zaručené to není a padne přesně na začátek nové adresy, takže se
  // z ní jinak stane dvouznaková adresa a radar se ptá někam, kam nemá.
  uint8_t *bytes = reinterpret_cast<uint8_t *>(&record[0]);
  const size_t paddingOffset =
      sizeof(uint32_t) * 2 + offsetof(ClockConfig, planesFeedUrl);
  bytes[paddingOffset] = 'X';
  bytes[paddingOffset + 1] = 'Y';
  // Kontrolní součet se počítá z payloadu, takže se po zásahu musí přepsat -
  // jinak by záznam propadl jako poškozený a test by nic nedokazoval.
  const uint32_t checksum = fnv1a(bytes + 8, SCHEMA_37_CONFIG_SIZE);
  memcpy(bytes + 8 + SCHEMA_37_CONFIG_SIZE, &checksum, sizeof(checksum));
  seed(record);

  ClockConfig migrated;
  assert(clockConfigLoad(migrated));
  assert(migrated.schemaVersion == CLOCK_CONFIG_SCHEMA_VERSION);
  // Celý prefix schématu 37 zůstal nedotčený, agendu na jeho konci nevyjímaje.
  assert(migrated.planes.enabled);
  assert(migrated.planes.rangeIndex == 3);
  assert(strcmp(migrated.planes.watchCallsign, "CSA1234") == 0);
  assert(migrated.agenda.enabled);
  assert(strcmp(migrated.agenda.url, "https://example.test/agenda.json") == 0);
  // Adresa vlastního zdroje zůstane prázdná: povýšení firmwaru nesmí radar
  // přesměrovat na server, který majitel nezadal.
  assert(migrated.planesFeedUrl[0] == '\0');

  // Migrace se musí uložit v novém formátu, aby proběhla jen jednou.
  assert(storedSize() == sizeof(uint32_t) * 3 + sizeof(ClockConfig));
}

void testPlanesFeedUrlRoundTrip() {
  hostPreferencesReset();
  ClockConfig config;
  clockConfigApplyDefaults(config);
  clockConfigCopy(config.planesFeedUrl, sizeof(config.planesFeedUrl),
                  "https://example.test/planes.json");
  assert(clockConfigSave(config));

  ClockConfig loaded;
  assert(clockConfigLoad(loaded));
  assert(strcmp(loaded.planesFeedUrl, "https://example.test/planes.json") == 0);
}

void testScreenOrderRoundTripAndNormalization() {
  hostPreferencesReset();
  ClockConfig config;
  clockConfigApplyDefaults(config);
  config.screenOrder[0] = CLOCK_SCREEN_PLANES;
  config.screenOrder[1] = CLOCK_SCREEN_CLOCK;
  config.screenOrder[2] = CLOCK_SCREEN_FORECAST;
  config.screenOrder[3] = CLOCK_SCREEN_RSS;
  config.screenOrder[4] = CLOCK_SCREEN_RADAR;
  assert(clockConfigSave(config));

  ClockConfig loaded;
  assert(clockConfigLoad(loaded));
  assert(loaded.screenOrder[0] == CLOCK_SCREEN_PLANES);
  assert(loaded.screenOrder[4] == CLOCK_SCREEN_RADAR);
  assert(clockConfigScreenAt(loaded, 1) == CLOCK_SCREEN_CLOCK);
  assert(clockConfigScreenPosition(loaded, CLOCK_SCREEN_RSS) == 3);

  // Zdvojená a neznámá hodnota by ubrala obrazovku z cyklu; chybějící se
  // doplní na konec ve výchozím pořadí.
  hostPreferencesReset();
  ClockConfig wild;
  clockConfigApplyDefaults(wild);
  wild.screenOrder[0] = CLOCK_SCREEN_RSS;
  wild.screenOrder[1] = CLOCK_SCREEN_RSS;
  wild.screenOrder[2] = 200;
  wild.screenOrder[3] = CLOCK_SCREEN_PLANES;
  wild.screenOrder[4] = CLOCK_SCREEN_PLANES;
  assert(clockConfigSave(wild));

  ClockConfig repaired;
  assert(clockConfigLoad(repaired));
  assert(repaired.screenOrder[0] == CLOCK_SCREEN_RSS);
  assert(repaired.screenOrder[1] == CLOCK_SCREEN_PLANES);
  // Agenda, Slunce s Měsícem, škola a družice ve druhém bloku přežily
  // z výchozího pořadí, takže se doplňuje až za ně; teprve pak přijdou
  // obrazovky, které v poli vůbec nebyly - i přes hranici obou bloků.
  assert(repaired.screenOrder[2] == CLOCK_SCREEN_AGENDA);
  assert(repaired.screenOrder[3] == CLOCK_SCREEN_SKY);
  assert(repaired.screenOrder[4] == CLOCK_SCREEN_SCHOOL);
  assert(repaired.screenOrder[5] == CLOCK_SCREEN_SATELLITES);
  assert(repaired.screenOrder[6] == CLOCK_SCREEN_CLOCK);
  assert(repaired.screenOrder[7] == CLOCK_SCREEN_RADAR);
  assert(repaired.screenOrderTail[0] == CLOCK_SCREEN_FORECAST);
  assert(clockConfigScreenAt(repaired, 8) == CLOCK_SCREEN_FORECAST);
  assert(clockConfigScreenPosition(repaired, CLOCK_SCREEN_FORECAST) == 8);
  for (size_t index = CLOCK_SCREEN_ORDER_COUNT;
       index < CLOCK_SCREEN_ORDER_CAPACITY; ++index) {
    assert(clockConfigScreenOrderSlot(repaired, index) ==
           CLOCK_SCREEN_ORDER_UNUSED);
  }

  // Výchozí konfigurace má družice hned za školou, na začátku druhého bloku.
  ClockConfig defaults;
  clockConfigApplyDefaults(defaults);
  assert(defaults.screenOrder[7] == CLOCK_SCREEN_SCHOOL);
  assert(defaults.screenOrderTail[0] == CLOCK_SCREEN_SATELLITES);
  assert(defaults.screenOrderTail[1] == CLOCK_SCREEN_ORDER_UNUSED);
}

void testSecondValuePagePersistenceAndMigration() {
  hostPreferencesReset();
  ClockConfig source;
  clockConfigApplyDefaults(source);
  source.bottomSlot.enabled = true;
  clockConfigCopy(source.bottomSlot.name, sizeof(source.bottomSlot.name), "FIRST");
  clockConfigCopy(source.planesFeedUrl, sizeof(source.planesFeedUrl), "https://example.test/planes");
  seed(legacyRecord(source, 38, offsetof(ClockConfig, secondPageSlots)));
  ClockConfig migrated;
  assert(clockConfigLoad(migrated));
  assert(strcmp(migrated.planesFeedUrl, source.planesFeedUrl) == 0);
  assert(migrated.bottomSlot.enabled);
  for (size_t i = 9; i < 18; ++i) {
    auto &slot = clockConfigValueSlot(migrated, i);
    assert(!slot.enabled);
    assert(slot.entityId[0] == '\0');
    slot.enabled = true;
    clockConfigCopy(slot.entityId, sizeof(slot.entityId), ("sensor.second_" + std::to_string(i)).c_str());
    slot.decimals = 2;
  }
  assert(clockConfigSave(migrated));
  ClockConfig loaded;
  assert(clockConfigLoad(loaded));
  assert(strcmp(loaded.bottomSlot.name, "FIRST") == 0);
  for (size_t i = 9; i < 18; ++i) {
    const auto &slot = clockConfigValueSlot(loaded, i);
    assert(slot.enabled && slot.decimals == 2);
    assert(std::string(slot.entityId) == "sensor.second_" + std::to_string(i));
  }
  auto corrupted = legacyRecord(source, 38, offsetof(ClockConfig, secondPageSlots));
  corrupted.back() ^= 1;
  seed(corrupted);
  assert(clockConfigLoad(loaded));
  assert(!loaded.bottomSlot.enabled);
  assert(loaded.planesFeedUrl[0] == '\0');
}

void testPlanesMapLabelPersistenceAndMigration() {
  hostPreferencesReset();
  ClockConfig defaults;
  clockConfigApplyDefaults(defaults);
  assert(defaults.planesMapLabel == CLOCK_PLANE_MAP_LABEL_TYPE_NAME);

  // Schéma 39 popisek nemá; po povýšení firmwaru se píše typ letadla a všechno
  // před ním zůstane, jak bylo.
  ClockConfig source;
  clockConfigApplyDefaults(source);
  source.planes.topBearingDeg = 225;
  clockConfigValueSlot(source, CLOCK_VALUE_SLOT_COUNT - 1).enabled = true;
  seed(legacyRecord(source, 39, offsetof(ClockConfig, planesMapLabel)));
  ClockConfig migrated;
  assert(clockConfigLoad(migrated));
  assert(migrated.schemaVersion == CLOCK_CONFIG_SCHEMA_VERSION);
  assert(migrated.planes.topBearingDeg == 225);
  assert(clockConfigValueSlot(migrated, CLOCK_VALUE_SLOT_COUNT - 1).enabled);
  assert(migrated.planesMapLabel == CLOCK_PLANE_MAP_LABEL_TYPE_NAME);

  migrated.planesMapLabel = CLOCK_PLANE_MAP_LABEL_CALLSIGN;
  assert(clockConfigSave(migrated));
  ClockConfig loaded;
  assert(clockConfigLoad(loaded));
  assert(loaded.planesMapLabel == CLOCK_PLANE_MAP_LABEL_CALLSIGN);

  // Neznámá hodnota se vrátí k výchozímu typu letadla.
  hostPreferencesReset();
  ClockConfig wild;
  clockConfigApplyDefaults(wild);
  wild.planesMapLabel = 7;
  assert(clockConfigSave(wild));
  assert(clockConfigLoad(loaded));
  assert(loaded.planesMapLabel == CLOCK_PLANE_MAP_LABEL_TYPE_NAME);
}

void testAgendaCalendarsPersistenceAndMigration() {
  hostPreferencesReset();
  ClockConfig defaults;
  clockConfigApplyDefaults(defaults);
  assert(defaults.agendaCalendars.hiddenMask == 0);
  assert(defaults.agendaCalendars.privateKey[0] == '\0');

  // Schéma 40 výběr kalendářů nemá; po povýšení se ukáže všechno jako dosud
  // a žádné heslo se neobjeví ani z bajtů koncové výplně.
  ClockConfig source;
  clockConfigApplyDefaults(source);
  source.planesMapLabel = CLOCK_PLANE_MAP_LABEL_CALLSIGN;
  source.agenda.enabled = true;
  seed(legacyRecord(source, 40, CLOCK_CONFIG_SCHEMA_40_SIZE));
  ClockConfig migrated;
  assert(clockConfigLoad(migrated));
  assert(migrated.schemaVersion == CLOCK_CONFIG_SCHEMA_VERSION);
  assert(migrated.planesMapLabel == CLOCK_PLANE_MAP_LABEL_CALLSIGN);
  assert(migrated.agenda.enabled);
  assert(migrated.agendaCalendars.hiddenMask == 0);
  assert(migrated.agendaCalendars.privateKey[0] == '\0');

  migrated.agendaCalendars.hiddenMask = 0b101;
  clockConfigCopy(migrated.agendaCalendars.privateKey,
                  sizeof(migrated.agendaCalendars.privateKey), "tajne heslo!");
  assert(clockConfigSave(migrated));
  ClockConfig loaded;
  assert(clockConfigLoad(loaded));
  assert(loaded.agendaCalendars.hiddenMask == 0b101);
  assert(strcmp(loaded.agendaCalendars.privateKey, "tajne heslo!") == 0);

  // Heslo míří do HTTP hlavičky, takže řídicí znak z poškozeného záznamu ho
  // zahodí celé.
  hostPreferencesReset();
  ClockConfig wild;
  clockConfigApplyDefaults(wild);
  clockConfigCopy(wild.agendaCalendars.privateKey,
                  sizeof(wild.agendaCalendars.privateKey), "a\r\nX-Evil: 1");
  assert(clockConfigSave(wild));
  assert(clockConfigLoad(loaded));
  assert(loaded.agendaCalendars.privateKey[0] == '\0');
  assert(clockConfigAgendaPrivateKeyValid(""));
  assert(!clockConfigAgendaPrivateKeyValid("heslo\xc3\xa1"));
}

void testLightningAndSkyPersistenceAndMigration() {
  hostPreferencesReset();
  ClockConfig defaults;
  clockConfigApplyDefaults(defaults);
  assert(!defaults.lightning.enabled && defaults.lightning.url[0] == '\0');
  assert(!clockConfigLightningAvailable(defaults));
  assert(!clockConfigSkyAvailable(defaults));

  // Schéma 41 blesky ani obrazovku Slunce a Měsíce nezná. Po povýšení zůstane
  // obojí vypnuté a obrazovka se zařadí na konec cyklu.
  ClockConfig source;
  clockConfigApplyDefaults(source);
  source.agendaCalendars.hiddenMask = 0b11;
  source.screenOrder[0] = CLOCK_SCREEN_RADAR;
  source.screenOrder[1] = CLOCK_SCREEN_CLOCK;
  // Záznam schématu 41 měl na posledních dvou pozicích volno; škola na [7]
  // přišla až s výchozím pořadím schématu 44.
  source.screenOrder[6] = CLOCK_SCREEN_ORDER_UNUSED;
  source.screenOrder[7] = CLOCK_SCREEN_ORDER_UNUSED;
  seed(legacyRecord(source, 41, CLOCK_CONFIG_SCHEMA_41_SIZE));
  ClockConfig migrated;
  assert(clockConfigLoad(migrated));
  assert(migrated.schemaVersion == CLOCK_CONFIG_SCHEMA_VERSION);
  assert(migrated.agendaCalendars.hiddenMask == 0b11);
  assert(!migrated.lightning.enabled && migrated.lightning.url[0] == '\0');
  assert(migrated.lightning.alarmRadiusKm == 10);
  assert(!migrated.sky.enabled);
  assert(migrated.screenOrder[0] == CLOCK_SCREEN_RADAR);
  assert(migrated.screenOrder[1] == CLOCK_SCREEN_CLOCK);
  assert(migrated.screenOrder[6] == CLOCK_SCREEN_SKY);
  // Škola přišla se schématem 44 a normalizace ji připojí na konec cyklu,
  // družice ze schématu 45 až za ni.
  assert(migrated.screenOrder[7] == CLOCK_SCREEN_SCHOOL);
  assert(migrated.screenOrderTail[0] == CLOCK_SCREEN_SATELLITES);

  migrated.lightning.enabled = true;
  clockConfigCopy(migrated.lightning.url, sizeof(migrated.lightning.url),
                  "https://hodiny:heslo@example.test/lightning.json");
  migrated.lightning.alarmRadiusKm = 200;
  migrated.lightning.alarmMinutes = 0;
  migrated.sky.enabled = true;
  migrated.sky.displaySeconds = 1;
  assert(clockConfigSave(migrated));
  ClockConfig loaded;
  assert(clockConfigLoad(loaded));
  assert(clockConfigLightningAvailable(loaded));
  assert(clockConfigSkyAvailable(loaded));
  assert(loaded.lightning.alarmRadiusKm == CLOCK_LIGHTNING_MAX_ALARM_KM);
  assert(loaded.lightning.alarmMinutes == CLOCK_LIGHTNING_MIN_ALARM_MINUTES);
  assert(loaded.sky.displaySeconds == 10);
}

void testRadarPrecipitationPersistenceAndMigration() {
  hostPreferencesReset();
  ClockConfig defaults;
  clockConfigApplyDefaults(defaults);
  assert(defaults.radarPrecipitation);

  // Schéma 42 má stejnou velikost; vypínač srážek ležel v jeho koncové výplni,
  // takže nula tam po povýšení nesmí srážky vypnout.
  ClockConfig source;
  clockConfigApplyDefaults(source);
  source.lightning.enabled = true;
  source.radarLegend = false;
  source.radarPrecipitation = false;
  seed(legacyRecord(source, 42, CLOCK_CONFIG_SCHEMA_42_SIZE));
  ClockConfig migrated;
  assert(clockConfigLoad(migrated));
  assert(migrated.schemaVersion == CLOCK_CONFIG_SCHEMA_VERSION);
  assert(migrated.lightning.enabled);
  assert(!migrated.radarLegend);
  assert(migrated.radarPrecipitation);

  migrated.radarPrecipitation = false;
  assert(clockConfigSave(migrated));
  ClockConfig loaded;
  assert(clockConfigLoad(loaded));
  assert(!loaded.radarPrecipitation);
}

void testSchoolPersistenceAndMigration() {
  hostPreferencesReset();
  ClockConfig defaults;
  clockConfigApplyDefaults(defaults);
  assert(!defaults.school.enabled && defaults.school.url[0] == '\0');
  assert(defaults.school.showHomework);
  assert(defaults.school.mealNextDayHour == 16);
  assert(!clockConfigSchoolAvailable(defaults));

  // Schéma 43 je předponou 44: škola zůstane vypnutá a bez adresy, pořadí
  // obrazovek si podrží vlastní sled a školu přidá až na konec.
  ClockConfig source;
  clockConfigApplyDefaults(source);
  source.radarPrecipitation = false;
  source.sky.enabled = true;
  source.screenOrder[0] = CLOCK_SCREEN_AGENDA;
  source.screenOrder[5] = CLOCK_SCREEN_CLOCK;
  source.screenOrder[7] = CLOCK_SCREEN_ORDER_UNUSED;
  seed(legacyRecord(source, 43, CLOCK_CONFIG_SCHEMA_43_SIZE));
  ClockConfig migrated;
  assert(clockConfigLoad(migrated));
  assert(migrated.schemaVersion == CLOCK_CONFIG_SCHEMA_VERSION);
  assert(!migrated.radarPrecipitation);
  assert(migrated.sky.enabled);
  assert(!migrated.school.enabled && migrated.school.url[0] == '\0');
  assert(migrated.school.refreshMinutes == 20);
  assert(migrated.school.displaySeconds == 20);
  assert(migrated.screenOrder[0] == CLOCK_SCREEN_AGENDA);
  assert(migrated.screenOrder[5] == CLOCK_SCREEN_CLOCK);
  assert(migrated.screenOrder[7] == CLOCK_SCREEN_SCHOOL);

  migrated.school.enabled = true;
  migrated.school.showHomework = false;
  migrated.school.refreshMinutes = 1;
  migrated.school.displaySeconds = 60000;
  clockConfigCopy(migrated.school.url, sizeof(migrated.school.url),
                  "https://hodiny:heslo@example.test/school.json");
  assert(clockConfigSave(migrated));
  ClockConfig loaded;
  assert(clockConfigLoad(loaded));
  assert(clockConfigSchoolAvailable(loaded));
  assert(!loaded.school.showHomework);
  assert(loaded.school.refreshMinutes == 5);
  assert(loaded.school.displaySeconds == 3600);
  assert(strcmp(loaded.school.url,
                "https://hodiny:heslo@example.test/school.json") == 0);

  // Schéma 47 má stejnou velikost jako 48: hodina přepnutí obědů leží v jeho
  // koncové výplni, takže se po migraci nastaví natvrdo, ať v ní leželo cokoli.
  hostPreferencesReset();
  ClockConfig legacy;
  clockConfigApplyDefaults(legacy);
  legacy.school.enabled = true;
  clockConfigCopy(legacy.school.url, sizeof(legacy.school.url),
                  "https://example.test/school.json");
  legacy.school.mealNextDayHour = 9;
  seed(legacyRecord(legacy, 47, sizeof(ClockConfig)));
  ClockConfig fromSchema47;
  assert(clockConfigLoad(fromSchema47));
  assert(fromSchema47.schemaVersion == CLOCK_CONFIG_SCHEMA_VERSION);
  assert(fromSchema47.school.enabled);
  assert(fromSchema47.school.mealNextDayHour == 16);

  // Hodina mimo den se čte jako "nepřepínat".
  fromSchema47.school.mealNextDayHour = 30;
  assert(clockConfigSave(fromSchema47));
  ClockConfig reloaded;
  assert(clockConfigLoad(reloaded));
  assert(reloaded.school.mealNextDayHour == 0);

  assert(clockConfigUrlHasCredentials("http://hodiny:heslo@server/school.json"));
  assert(clockConfigUrlHasCredentials("https://hodiny@server"));
  assert(!clockConfigUrlHasCredentials("http://server/school.json"));
  assert(!clockConfigUrlHasCredentials("http://server/a@b"));
  assert(!clockConfigUrlHasCredentials("http://server?mail=a@b"));
  assert(!clockConfigUrlHasCredentials("server@example"));
  assert(!clockConfigUrlHasCredentials(nullptr));
}

void testSatellitesPersistenceAndMigration() {
  hostPreferencesReset();
  ClockConfig defaults;
  clockConfigApplyDefaults(defaults);
  assert(!defaults.satellites.enabled && defaults.satellites.url[0] == '\0');
  assert(defaults.satellites.groups ==
         (CLOCK_SATELLITE_GROUP_STATIONS | CLOCK_SATELLITE_GROUP_VISUAL |
          CLOCK_SATELLITE_GROUP_WEATHER | CLOCK_SATELLITE_GROUP_SATGUS));
  assert(defaults.satellites.showTracks);
  assert(!clockConfigSatellitesAvailable(defaults));

  // Schéma 44 je předponou 45: škola a přeskládané pořadí zůstanou, družice
  // jsou vypnuté bez adresy a v cyklu až za poslední obrazovkou.
  ClockConfig source;
  clockConfigApplyDefaults(source);
  source.school.enabled = true;
  clockConfigCopy(source.school.url, sizeof(source.school.url),
                  "https://hodiny:heslo@example.test/school.json");
  source.screenOrder[0] = CLOCK_SCREEN_SCHOOL;
  source.screenOrder[7] = CLOCK_SCREEN_CLOCK;
  seed(legacyRecord(source, 44, CLOCK_CONFIG_SCHEMA_44_SIZE));
  ClockConfig migrated;
  assert(clockConfigLoad(migrated));
  assert(migrated.schemaVersion == CLOCK_CONFIG_SCHEMA_VERSION);
  assert(clockConfigSchoolAvailable(migrated));
  assert(!migrated.satellites.enabled && migrated.satellites.url[0] == '\0');
  assert(migrated.satellites.minElevationDeg == 10);
  assert(migrated.satellites.refreshSeconds == 60);
  assert(migrated.screenOrder[0] == CLOCK_SCREEN_SCHOOL);
  assert(migrated.screenOrder[7] == CLOCK_SCREEN_CLOCK);
  assert(clockConfigScreenAt(migrated, 8) == CLOCK_SCREEN_SATELLITES);
  assert(storedSize() == sizeof(uint32_t) * 3 + sizeof(ClockConfig));

  // Hodnoty mimo rozsah se srovnají, neznámé bity skupin zahodí.
  migrated.satellites.enabled = true;
  migrated.satellites.groups = 0xFF;
  migrated.satellites.minElevationDeg = 90;
  migrated.satellites.refreshSeconds = 5;
  migrated.satellites.topBearingDeg = 400;
  migrated.satellites.displaySeconds = 5;
  migrated.satellites.showTracks = false;
  clockConfigCopy(migrated.satellites.url, sizeof(migrated.satellites.url),
                  "https://hodiny:heslo@example.test/satellites.json");
  // Družice přesunuté dopředu: pořadí přes oba bloky se musí uložit celé.
  uint8_t order[CLOCK_SCREEN_ORDER_CAPACITY];
  clockConfigReadScreenOrder(migrated, order);
  order[8] = CLOCK_SCREEN_CLOCK;
  order[7] = CLOCK_SCREEN_SATELLITES;
  clockConfigWriteScreenOrder(migrated, order);
  assert(clockConfigSave(migrated));
  ClockConfig loaded;
  assert(clockConfigLoad(loaded));
  assert(clockConfigSatellitesAvailable(loaded));
  assert(loaded.satellites.groups == CLOCK_SATELLITE_GROUP_ALL);
  assert(loaded.satellites.minElevationDeg == CLOCK_SATELLITES_MAX_MIN_ELEVATION);
  assert(loaded.satellites.refreshSeconds == CLOCK_SATELLITES_MIN_REFRESH_SECONDS);
  assert(loaded.satellites.topBearingDeg == 0);
  assert(loaded.satellites.displaySeconds == 10);
  assert(!loaded.satellites.showTracks);
  assert(clockConfigScreenPosition(loaded, CLOCK_SCREEN_SATELLITES) == 7);
  assert(clockConfigScreenAt(loaded, 8) == CLOCK_SCREEN_CLOCK);

  // Bez jediné skupiny není co stahovat ani ukazovat.
  loaded.satellites.groups = 0;
  assert(!clockConfigSatellitesAvailable(loaded));

  // Schéma 48 má stejnou velikost jako 49 a bit SATGUS v něm byl vždycky nula:
  // migrace domácí družici zapne, ostatní skupiny nechá být.
  hostPreferencesReset();
  ClockConfig schema48;
  clockConfigApplyDefaults(schema48);
  schema48.satellites.enabled = true;
  clockConfigCopy(schema48.satellites.url, sizeof(schema48.satellites.url),
                  "https://example.test/satellites.json");
  schema48.satellites.groups =
      CLOCK_SATELLITE_GROUP_STATIONS | CLOCK_SATELLITE_GROUP_STARLINK;
  seed(legacyRecord(schema48, 48, sizeof(ClockConfig)));
  ClockConfig fromSchema48;
  assert(clockConfigLoad(fromSchema48));
  assert(fromSchema48.schemaVersion == CLOCK_CONFIG_SCHEMA_VERSION);
  assert(fromSchema48.satellites.enabled);
  assert(fromSchema48.satellites.groups ==
         (CLOCK_SATELLITE_GROUP_STATIONS | CLOCK_SATELLITE_GROUP_STARLINK |
          CLOCK_SATELLITE_GROUP_SATGUS));
  // Vypnutá družice se dá uložit a po načtení zůstane vypnutá.
  fromSchema48.satellites.groups &= ~CLOCK_SATELLITE_GROUP_SATGUS;
  assert(clockConfigSave(fromSchema48));
  ClockConfig withoutSatgus;
  assert(clockConfigLoad(withoutSatgus));
  assert((withoutSatgus.satellites.groups & CLOCK_SATELLITE_GROUP_SATGUS) == 0);
}

void testScreenSchedulePersistenceAndMigration() {
  hostPreferencesReset();
  ClockConfig defaults;
  clockConfigApplyDefaults(defaults);
  assert(defaults.screenSchedule.startupScreen == CLOCK_SCREEN_CLOCK);
  for (const ClockScreenScheduleRule &rule : defaults.screenSchedule.rules)
    assert(rule.screen == CLOCK_SCREEN_ORDER_UNUSED);

  // Schéma 45 je předponou 46: družice i pořadí zůstanou, výchozí obrazovka
  // je ciferník a plán je vypnutý.
  ClockConfig source;
  clockConfigApplyDefaults(source);
  source.satellites.enabled = true;
  source.screenOrderTail[0] = CLOCK_SCREEN_SATELLITES;
  seed(legacyRecord(source, 45, CLOCK_CONFIG_SCHEMA_45_SIZE));
  ClockConfig migrated;
  assert(clockConfigLoad(migrated));
  assert(migrated.schemaVersion == CLOCK_CONFIG_SCHEMA_VERSION);
  assert(migrated.satellites.enabled);
  assert(clockConfigScreenAt(migrated, 8) == CLOCK_SCREEN_SATELLITES);
  assert(migrated.screenSchedule.startupScreen == CLOCK_SCREEN_CLOCK);
  for (const ClockScreenScheduleRule &rule : migrated.screenSchedule.rules)
    assert(rule.screen == CLOCK_SCREEN_ORDER_UNUSED);

  // Družice od soumraku do svítání, letadla přes den; uloží se a načtou.
  migrated.screenSchedule.startupScreen = CLOCK_SCREEN_PLANES;
  ClockScreenScheduleRule &night = migrated.screenSchedule.rules[0];
  night.screen = CLOCK_SCREEN_SATELLITES;
  night.startEvent = CLOCK_SCHEDULE_CIVIL_DUSK;
  night.endEvent = CLOCK_SCHEDULE_CIVIL_DAWN;
  night.startValue = 15;
  // Hodnoty mimo rozsah se srovnají, neznámá obrazovka pravidlo vypne.
  ClockScreenScheduleRule &broken = migrated.screenSchedule.rules[1];
  broken.screen = 42;
  ClockScreenScheduleRule &clamped = migrated.screenSchedule.rules[2];
  clamped.screen = CLOCK_SCREEN_RADAR;
  clamped.startEvent = CLOCK_SCHEDULE_TIME;
  clamped.startValue = 2000;
  clamped.endEvent = 9;
  clamped.endValue = 77;
  ClockScreenScheduleRule &offset = migrated.screenSchedule.rules[3];
  offset.screen = CLOCK_SCREEN_CLOCK;
  offset.startEvent = CLOCK_SCHEDULE_SUNSET;
  offset.startValue = -500;
  assert(clockConfigSave(migrated));
  ClockConfig loaded;
  assert(clockConfigLoad(loaded));
  assert(loaded.screenSchedule.startupScreen == CLOCK_SCREEN_PLANES);
  assert(loaded.screenSchedule.rules[0].screen == CLOCK_SCREEN_SATELLITES);
  assert(loaded.screenSchedule.rules[0].startEvent == CLOCK_SCHEDULE_CIVIL_DUSK);
  assert(loaded.screenSchedule.rules[0].startValue == 15);
  assert(loaded.screenSchedule.rules[1].screen == CLOCK_SCREEN_ORDER_UNUSED);
  assert(loaded.screenSchedule.rules[2].startValue == 1439);
  assert(loaded.screenSchedule.rules[2].endEvent == CLOCK_SCHEDULE_TIME);
  assert(loaded.screenSchedule.rules[2].endValue == 0);
  assert(loaded.screenSchedule.rules[3].startValue ==
         -CLOCK_SCHEDULE_MAX_OFFSET_MINUTES);
  loaded.screenSchedule.startupScreen = 200;
  clockConfigNormalizeScreenSchedule(loaded.screenSchedule);
  assert(loaded.screenSchedule.startupScreen == CLOCK_SCREEN_CLOCK);
}

void testRainAlertPersistenceAndMigration() {
  hostPreferencesReset();
  ClockConfig defaults;
  clockConfigApplyDefaults(defaults);
  assert(!defaults.rainAlert.enabled);
  assert(defaults.rainAlert.url[0] == '\0');
  assert(defaults.rainAlert.horizonMinutes == 30);
  assert(defaults.rainAlert.minimumDbz == 28);

  // Schéma 46 je předponou 47: plán obrazovek zůstane, upozornění na déšť je
  // vypnuté a bez adresy.
  ClockConfig source;
  clockConfigApplyDefaults(source);
  source.screenSchedule.startupScreen = CLOCK_SCREEN_RADAR;
  source.screenSchedule.rules[0].screen = CLOCK_SCREEN_SATELLITES;
  source.screenSchedule.rules[0].startEvent = CLOCK_SCHEDULE_CIVIL_DUSK;
  source.satellites.enabled = true;
  seed(legacyRecord(source, 46, CLOCK_CONFIG_SCHEMA_46_SIZE));
  ClockConfig migrated;
  assert(clockConfigLoad(migrated));
  assert(migrated.schemaVersion == CLOCK_CONFIG_SCHEMA_VERSION);
  // Starší záznam si podržel všechno, co v něm bylo.
  assert(migrated.satellites.enabled);
  assert(migrated.screenSchedule.startupScreen == CLOCK_SCREEN_RADAR);
  assert(migrated.screenSchedule.rules[0].screen == CLOCK_SCREEN_SATELLITES);
  assert(migrated.screenSchedule.rules[0].startEvent ==
         CLOCK_SCHEDULE_CIVIL_DUSK);
  // A nové pole má výchozí hodnoty, ne smetí z výplně.
  assert(!migrated.rainAlert.enabled);
  assert(migrated.rainAlert.url[0] == '\0');
  assert(migrated.rainAlert.horizonMinutes == 30);
  assert(migrated.rainAlert.radiusKm == 5);

  // Uloží se a načte beze změny.
  migrated.rainAlert.enabled = true;
  strcpy(migrated.rainAlert.url, "https://hodiny:heslo@example.net/rain.json");
  migrated.rainAlert.horizonMinutes = 60;
  migrated.rainAlert.minimumDbz = 36;
  migrated.rainAlert.radiusKm = 12;
  migrated.rainAlert.holdMinutes = 20;
  migrated.rainAlert.cooldownMinutes = 45;
  migrated.rainAlert.refreshMinutes = 10;
  migrated.rainAlert.quietAtNight = true;
  assert(clockConfigSave(migrated));
  ClockConfig loaded;
  assert(clockConfigLoad(loaded));
  assert(loaded.rainAlert.enabled);
  assert(strcmp(loaded.rainAlert.url,
                "https://hodiny:heslo@example.net/rain.json") == 0);
  assert(loaded.rainAlert.horizonMinutes == 60);
  assert(loaded.rainAlert.minimumDbz == 36);
  assert(loaded.rainAlert.radiusKm == 12);
  assert(loaded.rainAlert.holdMinutes == 20);
  assert(loaded.rainAlert.cooldownMinutes == 45);
  assert(loaded.rainAlert.refreshMinutes == 10);
  assert(loaded.rainAlert.quietAtNight);

  // Bez adresy se upozornění nedá zapnout a hodnoty mimo rozsah se srovnají.
  loaded.rainAlert.url[0] = '\0';
  loaded.rainAlert.horizonMinutes = 200;
  loaded.rainAlert.minimumDbz = 250;
  loaded.rainAlert.radiusKm = 200;
  loaded.rainAlert.refreshMinutes = 1;
  assert(clockConfigSave(loaded));
  ClockConfig clamped;
  assert(clockConfigLoad(clamped));
  assert(!clamped.rainAlert.enabled);
  assert(clamped.rainAlert.horizonMinutes == CLOCK_RAIN_HORIZON_MAX_MINUTES);
  assert(clamped.rainAlert.minimumDbz == CLOCK_RAIN_DBZ_MAX);
  assert(clamped.rainAlert.radiusKm == CLOCK_RAIN_RADIUS_MAX_KM);
  assert(clamped.rainAlert.refreshMinutes == CLOCK_RAIN_REFRESH_MIN_MINUTES);
}

int main() {
  testRainAlertPersistenceAndMigration();
  testScreenSchedulePersistenceAndMigration();
  testSatellitesPersistenceAndMigration();
  testSchoolPersistenceAndMigration();
  testRadarPrecipitationPersistenceAndMigration();
  testAgendaCalendarsPersistenceAndMigration();
  testPlanesMapLabelPersistenceAndMigration();
  testSecondValuePagePersistenceAndMigration();
  testLightningAndSkyPersistenceAndMigration();
  testEmptyStorageUsesDefaults();
  testRoundTripPreservesValues();
  testSchema27MigrationKeepsStoredValues();
  testCorruptRecordFallsBackToDefaults();
  testNormalizationClampsStoredValues();
  testSchema28MigrationSeedsValueSlots();
  testValueSlotsRoundTrip();
  testNormalizationClampsValueSlots();
  testValuesStyleSurvivesAppearanceSave();
  testSchema29MigrationAddsDisabledRss();
  testRssRoundTripAndClamping();
  testSchema30MigrationAddsDisabledBottomSlot();
  testBottomValueSlotRoundTripAndNormalization();
  testSchema32MigrationAddsRadarStatusLine();
  testSchema34MigrationAddsDisabledPlanes();
  testPlanesRoundTripAndClamping();
  testSchema35MigrationAddsDefaultScreenOrder();
  testSchema36MigrationAddsDisabledAgenda();
  testSchema37MigrationKeepsAskingAdsbDirectly();
  testPlanesFeedUrlRoundTrip();
  testScreenOrderRoundTripAndNormalization();
  return 0;
}
