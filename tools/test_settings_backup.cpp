// Ověří zálohu nastavení na počítači: že nese celou konfiguraci, že bez
// hesla nevynese tajemství a že se poškozený nebo cizí soubor odmítne dřív,
// než by přepsal nastavení.
//
// Překlad viz tools/run_host_tests.sh.

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

#include "ClockConfig.h"
#include "Preferences.h"
#include "SettingsBackup.h"

namespace {

// Hlavička zálohy (8 B) a hlavička prvního oddílu (4 B); první oddíl je vždy
// záznam konfigurace.
constexpr size_t RECORD_OFFSET = 12;

uint32_t fnv1a(const uint8_t *bytes, size_t size) {
  uint32_t hash = 2166136261u;
  for (size_t index = 0; index < size; ++index) {
    hash ^= bytes[index];
    hash *= 16777619u;
  }
  return hash;
}

void resealBackup(std::vector<uint8_t> &backup) {
  const uint32_t checksum = fnv1a(backup.data(), backup.size() - 4);
  memcpy(backup.data() + backup.size() - 4, &checksum, sizeof(checksum));
}

// Každé pole, na které se dá sáhnout, dostane hodnotu jinou než výchozí, aby
// se ztráta kteréhokoli z nich při cestě zálohou projevila.
void fillEverything(ClockConfig &config) {
  clockConfigApplyDefaults(config);
  clockConfigCopy(config.homeAssistantUrl, sizeof(config.homeAssistantUrl),
                  "https://ha.example.test");
  clockConfigCopy(config.homeAssistantToken,
                  sizeof(config.homeAssistantToken), "secret-ha-token");
  clockConfigCopy(config.tmepExportKey, sizeof(config.tmepExportKey),
                  "tmep-key");
  clockConfigCopy(config.tmepExportId, sizeof(config.tmepExportId), "11746");
  config.dataSource = CLOCK_DATA_SOURCE_HOME_ASSISTANT;
  config.dayBrightness = 77;
  config.language = CLOCK_LANGUAGE_ENGLISH;
  config.timeFont = CLOCK_TIME_FONT_DOTO;
  config.rss.enabled = true;
  clockConfigCopy(config.rss.url, sizeof(config.rss.url),
                  "https://example.test/top.xml");
  config.agenda.enabled = true;
  clockConfigCopy(config.agenda.url, sizeof(config.agenda.url),
                  "https://hodiny:pw@example.test/agenda.json");
  config.agendaCalendars.hiddenMask = 0b10;
  clockConfigCopy(config.agendaCalendars.privateKey,
                  sizeof(config.agendaCalendars.privateKey), "agenda-private-key");
  clockConfigCopy(config.planesFeedUrl, sizeof(config.planesFeedUrl),
                  "https://example.test/planes.json");
  config.planes.enabled = true;
  config.planes.topBearingDeg = 123;
  config.planesMapLabel = CLOCK_PLANE_MAP_LABEL_CALLSIGN;
  config.screenOrder[0] = CLOCK_SCREEN_AGENDA;
  config.screenOrder[5] = CLOCK_SCREEN_CLOCK;
  // Poslední slot druhé stránky i s ikonou a barvou, které web neukazuje -
  // přesně to stará záloha z prohlížeče ztrácela.
  ClockValueSlotConfig &slot =
      clockConfigValueSlot(config, CLOCK_VALUE_SLOT_COUNT - 1);
  slot.enabled = true;
  clockConfigCopy(slot.entityId, sizeof(slot.entityId), "sensor.last");
  clockConfigCopy(slot.icon, sizeof(slot.icon), "kitchen");
  slot.color = 0x123456;
  slot.colorScale.count = 2;
  slot.colorScale.points[1] = {10.0f, 0xABCDEF};
}

SettingsBackupContent fullContent() {
  SettingsBackupContent content;
  content.secrets = true;
  content.appearance.style = CLOCK_STYLE_VALUES;
  content.appearance.analogToneColor = 0x112233;
  content.appearance.analogHandToneColor = 0x223344;
  content.appearance.analogCardinalAccentColor = 0x334455;
  content.appearance.analogCardinalAccentsEnabled = false;
  content.appearance.analogOutlineHandsEnabled = true;
  content.appearance.analogMonochromeValuesEnabled = true;
  content.appearance.analogValuesAboveHandsEnabled = true;
  content.appearance.analogDateFormat = CLOCK_DATE_FORMAT_NUMERIC;
  content.appearance.analogDateColor = 0x445566;
  content.appearance.monochromeWeatherIconColor = 0x556677;
  content.webMode = 2;
  content.webPasswordPresent = true;
  for (size_t index = 0; index < sizeof(content.webPassword); ++index)
    content.webPassword[index] = static_cast<uint8_t>(index * 7 + 1);
  clockConfigCopy(content.shareUrl, sizeof(content.shareUrl),
                  "https://hodiny:pw@example.test/settings");
  return content;
}

std::vector<uint8_t> encode(ClockConfig &config,
                            const SettingsBackupContent &content) {
  std::vector<uint8_t> backup(SETTINGS_BACKUP_MAX_BYTES);
  const size_t size =
      settingsBackupEncode(config, content, backup.data(), backup.size());
  assert(size > 0);
  backup.resize(size);
  return backup;
}

void testFullBackupCarriesEverything() {
  ClockConfig source;
  fillEverything(source);
  ClockConfig expected = source;
  const SettingsBackupContent content = fullContent();
  const std::vector<uint8_t> backup = encode(source, content);

  ClockConfig restored;
  SettingsBackupContent restoredContent;
  assert(settingsBackupDecode(backup.data(), backup.size(), restored,
                              restoredContent) == SettingsBackupStatus::Ok);

  // Celá konfigurace bajt po bajtu: záznam prošel stejnou normalizací jako při
  // uložení, takže se musí rovnat tomu, co by hodiny uložily do NVS.
  std::vector<uint8_t> expectedRecord(clockConfigRecordSize());
  std::vector<uint8_t> restoredRecord(clockConfigRecordSize());
  clockConfigEncodeRecord(expected, expectedRecord.data(),
                          expectedRecord.size());
  clockConfigEncodeRecord(restored, restoredRecord.data(),
                          restoredRecord.size());
  assert(expectedRecord == restoredRecord);
  assert(strcmp(restored.homeAssistantToken, "secret-ha-token") == 0);
  assert(strcmp(restored.tmepExportKey, "tmep-key") == 0);
  assert(strcmp(restored.agendaCalendars.privateKey, "agenda-private-key") == 0);
  assert(restored.agendaCalendars.hiddenMask == 0b10);
  assert(strcmp(clockConfigValueSlot(restored, CLOCK_VALUE_SLOT_COUNT - 1).icon,
                "kitchen") == 0);

  assert(restoredContent.secrets);
  assert(restoredContent.webMode == 2);
  assert(restoredContent.webPasswordPresent);
  assert(memcmp(restoredContent.webPassword, content.webPassword,
                sizeof(content.webPassword)) == 0);
  assert(strcmp(restoredContent.shareUrl, content.shareUrl) == 0);
  const ClockAppearanceConfig &look = restoredContent.appearance;
  assert(look.style == CLOCK_STYLE_VALUES);
  assert(look.analogToneColor == 0x112233);
  assert(look.analogHandToneColor == 0x223344);
  assert(look.analogCardinalAccentColor == 0x334455);
  assert(!look.analogCardinalAccentsEnabled);
  assert(look.analogOutlineHandsEnabled);
  assert(look.analogMonochromeValuesEnabled);
  assert(look.analogValuesAboveHandsEnabled);
  assert(look.analogDateFormat == CLOCK_DATE_FORMAT_NUMERIC);
  assert(look.analogDateColor == 0x445566);
  assert(look.monochromeWeatherIconColor == 0x556677);
}

// Bez hesla se tajemství do zálohy nedostanou - ani v konfiguraci, ani
// v oddílech - a obnova bez nich je nesmí vyrobit.
void testBackupWithoutSecretsLeavesThemOut() {
  ClockConfig source;
  fillEverything(source);
  SettingsBackupContent content = fullContent();
  content.secrets = false;
  const std::vector<uint8_t> backup = encode(source, content);

  const std::string bytes(backup.begin(), backup.end());
  assert(bytes.find("secret-ha-token") == std::string::npos);
  assert(bytes.find("tmep-key") == std::string::npos);
  assert(bytes.find("agenda-private-key") == std::string::npos);
  assert(bytes.find("/settings") == std::string::npos);

  ClockConfig restored;
  SettingsBackupContent restoredContent;
  assert(settingsBackupDecode(backup.data(), backup.size(), restored,
                              restoredContent) == SettingsBackupStatus::Ok);
  assert(!restoredContent.secrets);
  assert(!restoredContent.webPasswordPresent);
  assert(restoredContent.shareUrl[0] == '\0');
  assert(restored.homeAssistantToken[0] == '\0');
  assert(restored.tmepExportId[0] == '\0');
  // Všechno ostatní zůstává.
  assert(strcmp(restored.homeAssistantUrl, "https://ha.example.test") == 0);
  assert(strcmp(restored.agenda.url,
                "https://hodiny:pw@example.test/agenda.json") == 0);
  assert(restored.planes.topBearingDeg == 123);
  assert(restored.planesMapLabel == CLOCK_PLANE_MAP_LABEL_CALLSIGN);
  assert(restoredContent.appearance.style == CLOCK_STYLE_VALUES);
}

void testCorruptedBackupIsRejected() {
  ClockConfig source;
  fillEverything(source);
  std::vector<uint8_t> backup = encode(source, fullContent());
  ClockConfig restored;
  SettingsBackupContent content;

  std::vector<uint8_t> flipped = backup;
  flipped[RECORD_OFFSET + 100] ^= 0x01;
  assert(settingsBackupDecode(flipped.data(), flipped.size(), restored,
                              content) == SettingsBackupStatus::Corrupted);
  // Po odmítnutí nesmí zůstat nic z cizí zálohy.
  assert(restored.homeAssistantToken[0] == '\0');

  std::vector<uint8_t> truncated(backup.begin(), backup.end() - 40);
  resealBackup(truncated);
  assert(settingsBackupDecode(truncated.data(), truncated.size(), restored,
                              content) == SettingsBackupStatus::Malformed);

  const char notBackup[] = "{\"format\":\"something else\"}";
  assert(settingsBackupDecode(reinterpret_cast<const uint8_t *>(notBackup),
                              sizeof(notBackup), restored, content) ==
         SettingsBackupStatus::Malformed);
}

// Pozměněný záznam s přepočítaným součtem zálohy, ale ne záznamu, musí padnout
// na kontrole záznamu - obnova nesmí věřit jen obálce.
void testRecordChecksumStillApplies() {
  ClockConfig source;
  fillEverything(source);
  std::vector<uint8_t> backup = encode(source, fullContent());
  backup[RECORD_OFFSET + 200] ^= 0x01;
  resealBackup(backup);
  ClockConfig restored;
  SettingsBackupContent content;
  assert(settingsBackupDecode(backup.data(), backup.size(), restored,
                              content) == SettingsBackupStatus::Malformed);
}

void testNewerFirmwareBackupIsRefused() {
  ClockConfig source;
  fillEverything(source);
  std::vector<uint8_t> backup = encode(source, fullContent());
  const uint32_t newer = CLOCK_CONFIG_SCHEMA_VERSION + 1;
  memcpy(backup.data() + RECORD_OFFSET + 4, &newer, sizeof(newer));
  resealBackup(backup);
  ClockConfig restored;
  SettingsBackupContent content;
  assert(settingsBackupDecode(backup.data(), backup.size(), restored,
                              content) == SettingsBackupStatus::NewerFirmware);
}

// Záloha z hodin se starším firmwarem projde stejnou migrací jako záznam
// v NVS. Schéma 38 je předchozí verze: druhá stránka hodnot přibude vypnutá.
void testOlderSchemaRecordIsMigrated() {
  ClockConfig source;
  fillEverything(source);
  const size_t v38Size = offsetof(ClockConfig, secondPageSlots);
  std::vector<uint8_t> record(sizeof(uint32_t) * 3 + v38Size);
  const uint32_t magic = 0x57484346;
  const uint32_t schema = 38;
  memcpy(record.data(), &magic, 4);
  memcpy(record.data() + 4, &schema, 4);
  memcpy(record.data() + 8, &source, v38Size);
  memcpy(record.data() + 8, &schema, 4);
  const uint32_t checksum = fnv1a(record.data() + 8, v38Size);
  memcpy(record.data() + 8 + v38Size, &checksum, 4);

  // Záloha s tímto záznamem místo aktuálního.
  ClockConfig scratch;
  fillEverything(scratch);
  const std::vector<uint8_t> current = encode(scratch, fullContent());
  const size_t currentRecordSize = clockConfigRecordSize();
  std::vector<uint8_t> backup(current.begin(), current.begin() + 10);
  backup.push_back(static_cast<uint8_t>(record.size()));
  backup.push_back(static_cast<uint8_t>(record.size() >> 8));
  backup.insert(backup.end(), record.begin(), record.end());
  backup.insert(backup.end(), current.begin() + RECORD_OFFSET + currentRecordSize,
                current.end());
  resealBackup(backup);

  ClockConfig restored;
  SettingsBackupContent content;
  assert(settingsBackupDecode(backup.data(), backup.size(), restored,
                              content) == SettingsBackupStatus::Ok);
  assert(restored.schemaVersion == CLOCK_CONFIG_SCHEMA_VERSION);
  assert(strcmp(restored.agenda.url,
                "https://hodiny:pw@example.test/agenda.json") == 0);
  assert(restored.planes.topBearingDeg == 123);
  assert(!clockConfigValueSlot(restored, CLOCK_VALUE_SLOT_COUNT - 1).enabled);
  // Popisek letadel přišel až se schématem 40, takže je výchozí.
  assert(restored.planesMapLabel == CLOCK_PLANE_MAP_LABEL_TYPE_NAME);
}

// Záloha je vstup zvenčí. Neukončený řetězec by se četl za konec pole, takže
// ho normalizace ukončí - i když součty sedí.
void testUnterminatedTextIsTerminated() {
  ClockConfig source;
  fillEverything(source);
  std::vector<uint8_t> backup = encode(source, fullContent());
  uint8_t *record = backup.data() + RECORD_OFFSET;
  const size_t urlOffset = 8 + offsetof(ClockConfig, homeAssistantUrl);
  memset(record + urlOffset, 'A', CLOCK_HA_URL_LENGTH);
  const size_t configSize = sizeof(ClockConfig);
  const uint32_t checksum = fnv1a(record + 8, configSize);
  memcpy(record + 8 + configSize, &checksum, sizeof(checksum));
  resealBackup(backup);

  ClockConfig restored;
  SettingsBackupContent content;
  assert(settingsBackupDecode(backup.data(), backup.size(), restored,
                              content) == SettingsBackupStatus::Ok);
  assert(strlen(restored.homeAssistantUrl) == CLOCK_HA_URL_LENGTH - 1);
}

void testBase64RoundTrip() {
  for (size_t size = 0; size < 64; ++size) {
    std::vector<uint8_t> data(size);
    for (size_t index = 0; index < size; ++index)
      data[index] = static_cast<uint8_t>(index * 37 + size);
    char text[128];
    const size_t length =
        settingsBackupBase64Encode(data.data(), size, text, sizeof(text));
    assert(length == strlen(text));
    assert(strpbrk(text, "+/=") == nullptr);
    uint8_t decoded[64];
    size_t written = 0;
    assert(settingsBackupBase64Decode(text, length, decoded, sizeof(decoded),
                                      written));
    assert(written == size);
    assert(size == 0 || memcmp(decoded, data.data(), size) == 0);
  }
  uint8_t out[8];
  size_t written = 0;
  // Klasické base64 s doplněním projde taky.
  assert(settingsBackupBase64Decode("+/8=", 4, out, sizeof(out), written));
  assert(written == 2 && out[0] == 0xFB && out[1] == 0xFF);
  assert(!settingsBackupBase64Decode("ab$d", 4, out, sizeof(out), written));
  assert(!settingsBackupBase64Decode("abcde", 5, out, sizeof(out), written));
  assert(!settingsBackupBase64Decode("AAAAAAAAAAAAAAAA", 16, out, sizeof(out),
                                     written));
}

void testNamesAndUrls() {
  assert(settingsBackupValidName("kuchyn"));
  assert(settingsBackupValidName("obyvak-2"));
  assert(settingsBackupValidName("abcdefghijklmnopqrstuvwxyz012345"));
  assert(!settingsBackupValidName("abcdefghijklmnopqrstuvwxyz0123456"));
  assert(!settingsBackupValidName(""));
  assert(!settingsBackupValidName("-kuchyn"));
  assert(!settingsBackupValidName("Kuchyn"));
  assert(!settingsBackupValidName("../etc"));
  assert(!settingsBackupValidName("a/b"));

  assert(settingsShareValidUrl("https://hodiny:pw@example.test/settings"));
  assert(settingsShareValidUrl("https://example.test"));
  assert(!settingsShareValidUrl("http://hodiny:pw@example.test/settings"));
  assert(!settingsShareValidUrl("https://"));
  assert(!settingsShareValidUrl("https://hodiny:pw@/settings"));
  assert(!settingsShareValidUrl("https://example.test/settings?x=1"));
  assert(!settingsShareValidUrl("https://example.test/set tings"));

  assert(settingsShareDisplayUrl("https://hodiny:pw@example.test/settings") ==
         String("https://example.test/settings"));
  assert(settingsShareDisplayUrl("https://example.test/a@b") ==
         String("https://example.test/a@b"));
}

}  // namespace

int main() {
  testFullBackupCarriesEverything();
  testBackupWithoutSecretsLeavesThemOut();
  testCorruptedBackupIsRejected();
  testRecordChecksumStillApplies();
  testNewerFirmwareBackupIsRefused();
  testOlderSchemaRecordIsMigrated();
  testUnterminatedTextIsTerminated();
  testBase64RoundTrip();
  testNamesAndUrls();
  return 0;
}
