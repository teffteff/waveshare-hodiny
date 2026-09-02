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
  assert(strcmp(config.openMeteoCity, "Brno") == 0);
  assert(config.dataSource == CLOCK_DATA_SOURCE_OPEN_METEO);
  assert(strcmp(config.leftSide.name, "VENKU") == 0);
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
    assert(!migrated.slots[index].enabled);
    assert(migrated.slots[index].entityId[0] == '\0');
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

int main() {
  testEmptyStorageUsesDefaults();
  testRoundTripPreservesValues();
  testSchema27MigrationKeepsStoredValues();
  testCorruptRecordFallsBackToDefaults();
  testNormalizationClampsStoredValues();
  testSchema28MigrationSeedsValueSlots();
  testValueSlotsRoundTrip();
  testValuesStyleSurvivesAppearanceSave();
  return 0;
}
