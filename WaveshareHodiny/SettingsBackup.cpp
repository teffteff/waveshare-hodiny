#include "SettingsBackup.h"

#include <cstring>

namespace {
constexpr uint8_t BACKUP_MAGIC[4] = {'W', 'H', 'S', 'B'};
// Verze formátu se nezvyšuje: jiná verze obnovu odmítne, takže by přestaly
// platit všechny dosavadní zálohy. Nový obsah přijde jako nový oddíl, který
// starší firmware přeskočí.
constexpr uint8_t BACKUP_VERSION = 1;
constexpr uint8_t BACKUP_FLAG_SECRETS = 0x01;
constexpr size_t BACKUP_HEADER_SIZE = 8;
constexpr size_t SECTION_HEADER_SIZE = 4;
constexpr size_t CHECKSUM_SIZE = 4;

enum SectionTag : uint16_t {
  SECTION_CONFIG_RECORD = 1,
  SECTION_APPEARANCE = 2,
  SECTION_WEB_MODE = 3,
  SECTION_WEB_PASSWORD = 4,
  SECTION_SHARE_URL = 5,
};

// Vzhled leží v NVS po jednotlivých klíčích, ne jako struktura, takže se ani
// tady nekopíruje paměť: pole mají pevné pořadí a velikost nezávislou na
// překladači.
//
// Tahle délka je navždy nejmenší, kterou obnova přijme - tolik zapisuje
// první firmware se zálohou. Nové pole vzhledu se připíše za ni, zapíše se
// delší oddíl a čtení nového pole se podmíní délkou; kratší oddíl ze starší
// zálohy pak nové pole nechá výchozí. Zvednout tuhle hodnotu by staré zálohy
// odmítlo jako neplatné.
constexpr size_t APPEARANCE_SIZE = 26;

uint32_t fnv1a(const uint8_t *bytes, size_t size) {
  uint32_t hash = 2166136261u;
  for (size_t index = 0; index < size; ++index) {
    hash ^= bytes[index];
    hash *= 16777619u;
  }
  return hash;
}

class Writer {
 public:
  Writer(uint8_t *output, size_t capacity)
      : output_(output), capacity_(capacity) {}

  void bytes(const uint8_t *data, size_t size) {
    if (failed_ || output_ == nullptr || size > capacity_ - length_) {
      failed_ = true;
      return;
    }
    memcpy(output_ + length_, data, size);
    length_ += size;
  }
  void u8(uint8_t value) { bytes(&value, 1); }
  void u16(uint16_t value) {
    const uint8_t data[] = {static_cast<uint8_t>(value),
                            static_cast<uint8_t>(value >> 8)};
    bytes(data, sizeof(data));
  }
  void u32(uint32_t value) {
    const uint8_t data[] = {
        static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
    bytes(data, sizeof(data));
  }
  void section(uint16_t tag, size_t size) {
    if (size > UINT16_MAX) {
      failed_ = true;
      return;
    }
    u16(tag);
    u16(static_cast<uint16_t>(size));
  }
  // Místo pro obsah, který zapíše někdo jiný (záznam konfigurace).
  uint8_t *reserve(size_t size) {
    if (failed_ || output_ == nullptr || size > capacity_ - length_) {
      failed_ = true;
      return nullptr;
    }
    uint8_t *start = output_ + length_;
    length_ += size;
    return start;
  }

  size_t length() const { return length_; }
  bool failed() const { return failed_; }
  const uint8_t *data() const { return output_; }

 private:
  uint8_t *output_;
  size_t capacity_;
  size_t length_ = 0;
  bool failed_ = false;
};

uint16_t readU16(const uint8_t *data) {
  return static_cast<uint16_t>(data[0] | (data[1] << 8));
}

uint32_t readU32(const uint8_t *data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

void writeAppearance(Writer &writer, const ClockAppearanceConfig &appearance) {
  writer.section(SECTION_APPEARANCE, APPEARANCE_SIZE);
  writer.u8(appearance.style);
  writer.u32(appearance.analogToneColor);
  writer.u32(appearance.analogHandToneColor);
  writer.u32(appearance.analogCardinalAccentColor);
  writer.u8(appearance.analogCardinalAccentsEnabled ? 1 : 0);
  writer.u8(appearance.analogOutlineHandsEnabled ? 1 : 0);
  writer.u8(appearance.analogMonochromeValuesEnabled ? 1 : 0);
  writer.u8(appearance.analogValuesAboveHandsEnabled ? 1 : 0);
  writer.u8(appearance.analogDateFormat);
  writer.u32(appearance.analogDateColor);
  writer.u32(appearance.monochromeWeatherIconColor);
}

void readAppearance(const uint8_t *data, ClockAppearanceConfig &appearance) {
  appearance.style = data[0];
  appearance.analogToneColor = readU32(data + 1) & 0xFFFFFF;
  appearance.analogHandToneColor = readU32(data + 5) & 0xFFFFFF;
  appearance.analogCardinalAccentColor = readU32(data + 9) & 0xFFFFFF;
  appearance.analogCardinalAccentsEnabled = data[13] != 0;
  appearance.analogOutlineHandsEnabled = data[14] != 0;
  appearance.analogMonochromeValuesEnabled = data[15] != 0;
  appearance.analogValuesAboveHandsEnabled = data[16] != 0;
  appearance.analogDateFormat = data[17];
  appearance.analogDateColor = readU32(data + 18) & 0xFFFFFF;
  appearance.monochromeWeatherIconColor = readU32(data + 22) & 0xFFFFFF;
}

// Každé pole ClockConfig v pořadí, v jakém leží, s částí, ke které patří.
// Pole sahá až k začátku dalšího, takže s ním jde i jeho zarovnávací výplň.
// Uprostřed záznamu nic přibýt nemůže (hlídají to předpony schémat
// v ClockConfig.h); nové pole na konci shodí static_assert níže, dokud se
// sem nezapíše s částí, kam patří.
struct ConfigField {
  size_t offset;
  uint8_t part;
};

#define CONFIG_FIELD(name, part) {offsetof(ClockConfig, name), part}
constexpr uint8_t PART_CONNECTION = SETTINGS_BACKUP_PART_CONNECTION;
constexpr uint8_t PART_VALUES = SETTINGS_BACKUP_PART_VALUES;
constexpr uint8_t PART_SCREENS = SETTINGS_BACKUP_PART_SCREENS;
constexpr uint8_t PART_DISPLAY = SETTINGS_BACKUP_PART_DISPLAY;
constexpr uint8_t PART_SYSTEM = SETTINGS_BACKUP_PART_SYSTEM;

constexpr ConfigField CONFIG_FIELDS[] = {
    CONFIG_FIELD(homeAssistantUrl, PART_CONNECTION),
    CONFIG_FIELD(homeAssistantToken, PART_CONNECTION),
    CONFIG_FIELD(weatherEntityId, PART_VALUES),
    CONFIG_FIELD(sunEntityId, PART_DISPLAY),
    CONFIG_FIELD(leftSide, PART_VALUES),
    CONFIG_FIELD(rightSide, PART_VALUES),
    CONFIG_FIELD(metricA, PART_VALUES),
    CONFIG_FIELD(metricB, PART_VALUES),
    CONFIG_FIELD(metricAColorScale, PART_VALUES),
    CONFIG_FIELD(metricBColorScale, PART_VALUES),
    CONFIG_FIELD(timeColor, PART_DISPLAY),
    CONFIG_FIELD(dateColor, PART_DISPLAY),
    CONFIG_FIELD(leftWeatherIconColor, PART_VALUES),
    CONFIG_FIELD(rightWeatherIconColor, PART_VALUES),
    CONFIG_FIELD(animatedWeatherIcons, PART_DISPLAY),
    CONFIG_FIELD(weatherIconStyle, PART_DISPLAY),
    CONFIG_FIELD(dayBrightness, PART_DISPLAY),
    CONFIG_FIELD(nightBrightness, PART_DISPLAY),
    CONFIG_FIELD(automaticDayNight, PART_DISPLAY),
    CONFIG_FIELD(sunsetOffsetMinutes, PART_DISPLAY),
    CONFIG_FIELD(automaticFirmwareUpdate, PART_SYSTEM),
    CONFIG_FIELD(secondRingEnabled, PART_DISPLAY),
    CONFIG_FIELD(secondEffect, PART_DISPLAY),
    CONFIG_FIELD(sunriseOffsetMinutes, PART_DISPLAY),
    CONFIG_FIELD(secondRingBackgroundColor, PART_DISPLAY),
    CONFIG_FIELD(secondRingBackgroundBrightness, PART_DISPLAY),
    CONFIG_FIELD(secondRingBackgroundDotSize, PART_DISPLAY),
    CONFIG_FIELD(secondDotSize, PART_DISPLAY),
    CONFIG_FIELD(secondDotColor, PART_DISPLAY),
    CONFIG_FIELD(secondDotBrightness, PART_DISPLAY),
    CONFIG_FIELD(dayNightLightEntityId, PART_DISPLAY),
    CONFIG_FIELD(nightVisualMode, PART_DISPLAY),
    CONFIG_FIELD(timeFont, PART_DISPLAY),
    CONFIG_FIELD(dataSource, PART_CONNECTION),
    CONFIG_FIELD(openMeteoCity, PART_CONNECTION),
    CONFIG_FIELD(openMeteoLatitude, PART_CONNECTION),
    CONFIG_FIELD(openMeteoLongitude, PART_CONNECTION),
    CONFIG_FIELD(openMeteoSlots, PART_VALUES),
    CONFIG_FIELD(timeColonEffect, PART_DISPLAY),
    CONFIG_FIELD(showLeadingHourZero, PART_DISPLAY),
    CONFIG_FIELD(dateFormat, PART_DISPLAY),
    CONFIG_FIELD(radarRadiusKm, PART_SCREENS),
    CONFIG_FIELD(radarFrameCount, PART_SCREENS),
    CONFIG_FIELD(automaticRadarRotation, PART_SCREENS),
    CONFIG_FIELD(clockDisplaySeconds, PART_SCREENS),
    CONFIG_FIELD(radarDisplaySeconds, PART_SCREENS),
    CONFIG_FIELD(radarMapOpacity, PART_SCREENS),
    CONFIG_FIELD(radarPauseSeconds, PART_SCREENS),
    CONFIG_FIELD(language, PART_SYSTEM),
    CONFIG_FIELD(openMeteoCountry, PART_CONNECTION),
    CONFIG_FIELD(tmepExportKey, PART_CONNECTION),
    CONFIG_FIELD(tmepExportId, PART_CONNECTION),
    CONFIG_FIELD(tmepSlots, PART_VALUES),
    CONFIG_FIELD(leftValue, PART_VALUES),
    CONFIG_FIELD(rightValue, PART_VALUES),
    CONFIG_FIELD(leftValueColorScale, PART_VALUES),
    CONFIG_FIELD(rightValueColorScale, PART_VALUES),
    CONFIG_FIELD(slots, PART_VALUES),
    CONFIG_FIELD(rss, PART_SCREENS),
    CONFIG_FIELD(bottomSlot, PART_VALUES),
    CONFIG_FIELD(radarSource, PART_SCREENS),
    CONFIG_FIELD(radarLegend, PART_SCREENS),
    CONFIG_FIELD(radarStatusLine, PART_SCREENS),
    CONFIG_FIELD(radarStatusTemperatureEntityId, PART_SCREENS),
    CONFIG_FIELD(forecast, PART_SCREENS),
    CONFIG_FIELD(planes, PART_SCREENS),
    CONFIG_FIELD(screenOrder, PART_SCREENS),
    CONFIG_FIELD(agenda, PART_SCREENS),
    CONFIG_FIELD(planesFeedUrl, PART_SCREENS),
    CONFIG_FIELD(secondPageSlots, PART_VALUES),
    CONFIG_FIELD(planesMapLabel, PART_SCREENS),
    CONFIG_FIELD(agendaCalendars, PART_SCREENS),
    // Adresa serveru blesků nese heslo stejně jako adresa zdroje letadel a
    // stejně jako ona patří k obrazovkám: záloha ji má přenést do dalších hodin.
    CONFIG_FIELD(lightning, PART_SCREENS),
    CONFIG_FIELD(sky, PART_SCREENS),
    CONFIG_FIELD(radarPrecipitation, PART_SCREENS),
    // Adresa serveru s rozvrhem nese heslo stejně jako adresa agendy.
    CONFIG_FIELD(school, PART_SCREENS),
    CONFIG_FIELD(satellites, PART_SCREENS),
    // Druhý blok pořadí obrazovek patří k prvnímu, tedy k obrazovkám.
    CONFIG_FIELD(screenOrderTail, PART_SCREENS),
    // Výchozí obrazovka a plán obrazovek patří k pořadí obrazovek.
    CONFIG_FIELD(screenSchedule, PART_SCREENS),
};
#undef CONFIG_FIELD

constexpr size_t CONFIG_FIELD_COUNT = sizeof(CONFIG_FIELDS) / sizeof(CONFIG_FIELDS[0]);

constexpr bool configFieldsInOrder() {
  for (size_t index = 1; index < CONFIG_FIELD_COUNT; ++index) {
    if (CONFIG_FIELDS[index].offset <= CONFIG_FIELDS[index - 1].offset)
      return false;
  }
  return true;
}

static_assert(CONFIG_FIELDS[0].offset == sizeof(uint32_t) && configFieldsInOrder(),
              "The part table must list ClockConfig fields in layout order.");
// Poslední pole ClockConfig dorovnává koncová výplň na čtyři bajty.
static_assert((offsetof(ClockConfig, screenSchedule) +
               sizeof(ClockScreenScheduleConfig) +
               alignof(ClockConfig) - 1) /
                      alignof(ClockConfig) * alignof(ClockConfig) ==
                  sizeof(ClockConfig),
              "A new ClockConfig field needs a backup part in CONFIG_FIELDS.");

struct PartName {
  const char *name;
  uint8_t part;
};

constexpr PartName PART_NAMES[] = {
    {"connection", SETTINGS_BACKUP_PART_CONNECTION},
    {"values", SETTINGS_BACKUP_PART_VALUES},
    {"screens", SETTINGS_BACKUP_PART_SCREENS},
    {"display", SETTINGS_BACKUP_PART_DISPLAY},
    {"system", SETTINGS_BACKUP_PART_SYSTEM},
};

constexpr char BASE64_URL[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

int base64Value(char character) {
  if (character >= 'A' && character <= 'Z') return character - 'A';
  if (character >= 'a' && character <= 'z') return character - 'a' + 26;
  if (character >= '0' && character <= '9') return character - '0' + 52;
  if (character == '-' || character == '+') return 62;
  if (character == '_' || character == '/') return 63;
  return -1;
}
}  // namespace

void settingsBackupStripSecrets(ClockConfig &config) {
  memset(config.homeAssistantToken, 0, sizeof(config.homeAssistantToken));
  memset(config.tmepExportKey, 0, sizeof(config.tmepExportKey));
  memset(config.tmepExportId, 0, sizeof(config.tmepExportId));
  memset(config.agendaCalendars.privateKey, 0,
         sizeof(config.agendaCalendars.privateKey));
}

void settingsBackupKeepCurrentParts(ClockConfig &incoming,
                                    const ClockConfig &current, uint8_t parts) {
  if ((parts & SETTINGS_BACKUP_PARTS_ALL) == SETTINGS_BACKUP_PARTS_ALL) return;
  auto *target = reinterpret_cast<uint8_t *>(&incoming);
  const auto *source = reinterpret_cast<const uint8_t *>(&current);
  for (size_t index = 0; index < CONFIG_FIELD_COUNT; ++index) {
    const ConfigField &field = CONFIG_FIELDS[index];
    if ((parts & field.part) != 0) continue;
    const size_t end = index + 1 < CONFIG_FIELD_COUNT
                           ? CONFIG_FIELDS[index + 1].offset
                           : sizeof(ClockConfig);
    memcpy(target + field.offset, source + field.offset, end - field.offset);
  }
}

bool settingsBackupParseParts(const char *text, uint8_t &parts) {
  parts = 0;
  if (text == nullptr || *text == '\0') {
    parts = SETTINGS_BACKUP_PARTS_ALL;
    return true;
  }
  const char *cursor = text;
  while (true) {
    const char *comma = strchr(cursor, ',');
    const size_t length =
        comma != nullptr ? static_cast<size_t>(comma - cursor) : strlen(cursor);
    bool known = false;
    for (const PartName &entry : PART_NAMES) {
      if (strlen(entry.name) == length &&
          strncmp(cursor, entry.name, length) == 0) {
        parts |= entry.part;
        known = true;
        break;
      }
    }
    if (!known) {
      parts = 0;
      return false;
    }
    if (comma == nullptr) break;
    cursor = comma + 1;
  }
  return parts != 0;
}

size_t settingsBackupEncode(ClockConfig &config,
                            const SettingsBackupContent &content,
                            uint8_t *output, size_t capacity) {
  if (!content.secrets) settingsBackupStripSecrets(config);
  Writer writer(output, capacity);
  writer.bytes(BACKUP_MAGIC, sizeof(BACKUP_MAGIC));
  writer.u8(BACKUP_VERSION);
  writer.u8(content.secrets ? BACKUP_FLAG_SECRETS : 0);
  writer.u16(0);

  const size_t recordSize = clockConfigRecordSize();
  writer.section(SECTION_CONFIG_RECORD, recordSize);
  uint8_t *record = writer.reserve(recordSize);
  if (record == nullptr ||
      clockConfigEncodeRecord(config, record, recordSize) != recordSize)
    return 0;

  writeAppearance(writer, content.appearance);
  writer.section(SECTION_WEB_MODE, 1);
  writer.u8(content.webMode);

  if (content.secrets) {
    if (content.webPasswordPresent) {
      writer.section(SECTION_WEB_PASSWORD, sizeof(content.webPassword));
      writer.bytes(content.webPassword, sizeof(content.webPassword));
    }
    const size_t urlLength =
        strnlen(content.shareUrl, sizeof(content.shareUrl) - 1);
    if (urlLength > 0) {
      writer.section(SECTION_SHARE_URL, urlLength);
      writer.bytes(reinterpret_cast<const uint8_t *>(content.shareUrl),
                   urlLength);
    }
  }

  if (writer.failed()) return 0;
  writer.u32(fnv1a(writer.data(), writer.length()));
  return writer.failed() ? 0 : writer.length();
}

SettingsBackupStatus settingsBackupDecode(const uint8_t *data, size_t size,
                                          ClockConfig &config,
                                          SettingsBackupContent &content) {
  content = SettingsBackupContent{};
  clockConfigApplyDefaults(config);
  if (data == nullptr ||
      size < BACKUP_HEADER_SIZE + CHECKSUM_SIZE ||
      size > SETTINGS_BACKUP_MAX_BYTES ||
      memcmp(data, BACKUP_MAGIC, sizeof(BACKUP_MAGIC)) != 0 ||
      data[4] != BACKUP_VERSION)
    return SettingsBackupStatus::Malformed;
  const size_t payloadEnd = size - CHECKSUM_SIZE;
  if (fnv1a(data, payloadEnd) != readU32(data + payloadEnd))
    return SettingsBackupStatus::Corrupted;
  content.secrets = (data[5] & BACKUP_FLAG_SECRETS) != 0;

  const uint8_t *record = nullptr;
  size_t recordSize = 0;
  bool appearanceSeen = false;
  bool webModeSeen = false;
  size_t offset = BACKUP_HEADER_SIZE;
  while (offset < payloadEnd) {
    if (payloadEnd - offset < SECTION_HEADER_SIZE)
      return SettingsBackupStatus::Malformed;
    const uint16_t tag = readU16(data + offset);
    const size_t length = readU16(data + offset + 2);
    offset += SECTION_HEADER_SIZE;
    if (length > payloadEnd - offset) return SettingsBackupStatus::Malformed;
    const uint8_t *section = data + offset;
    offset += length;
    switch (tag) {
      case SECTION_CONFIG_RECORD:
        if (record != nullptr) return SettingsBackupStatus::Malformed;
        record = section;
        recordSize = length;
        break;
      case SECTION_APPEARANCE:
        // Delší oddíl je novější vzhled; známá pole se přečtou, zbytek ne.
        if (appearanceSeen || length < APPEARANCE_SIZE)
          return SettingsBackupStatus::Malformed;
        readAppearance(section, content.appearance);
        appearanceSeen = true;
        break;
      case SECTION_WEB_MODE:
        if (webModeSeen || length != 1) return SettingsBackupStatus::Malformed;
        content.webMode = section[0];
        webModeSeen = true;
        break;
      case SECTION_WEB_PASSWORD:
        if (!content.secrets || content.webPasswordPresent ||
            length != sizeof(content.webPassword))
          return SettingsBackupStatus::Malformed;
        memcpy(content.webPassword, section, length);
        content.webPasswordPresent = true;
        break;
      case SECTION_SHARE_URL:
        if (!content.secrets || content.shareUrl[0] != '\0' ||
            length == 0 || length >= sizeof(content.shareUrl))
          return SettingsBackupStatus::Malformed;
        memcpy(content.shareUrl, section, length);
        content.shareUrl[length] = '\0';
        if (!settingsShareValidUrl(content.shareUrl))
          return SettingsBackupStatus::Malformed;
        break;
      default:
        break;
    }
  }
  if (record == nullptr || !appearanceSeen || !webModeSeen)
    return SettingsBackupStatus::Malformed;

  switch (clockConfigDecodeRecord(record, recordSize, config)) {
    case ClockConfigRecordStatus::Ok:
      break;
    case ClockConfigRecordStatus::NewerFirmware:
      return SettingsBackupStatus::NewerFirmware;
    case ClockConfigRecordStatus::Invalid:
      return SettingsBackupStatus::Malformed;
  }
  // Záloha bez tajemství je nesmí přinést ani omylem.
  if (!content.secrets) settingsBackupStripSecrets(config);
  return SettingsBackupStatus::Ok;
}

size_t settingsBackupBase64Encode(const uint8_t *data, size_t size,
                                  char *output, size_t capacity) {
  const size_t length = size / 3 * 4 + (size % 3 == 0 ? 0 : size % 3 + 1);
  if (output == nullptr || length + 1 > capacity) return 0;
  size_t written = 0;
  size_t index = 0;
  while (index + 3 <= size) {
    const uint32_t chunk = (static_cast<uint32_t>(data[index]) << 16) |
                           (static_cast<uint32_t>(data[index + 1]) << 8) |
                           data[index + 2];
    output[written++] = BASE64_URL[(chunk >> 18) & 0x3F];
    output[written++] = BASE64_URL[(chunk >> 12) & 0x3F];
    output[written++] = BASE64_URL[(chunk >> 6) & 0x3F];
    output[written++] = BASE64_URL[chunk & 0x3F];
    index += 3;
  }
  const size_t rest = size - index;
  if (rest > 0) {
    uint32_t chunk = static_cast<uint32_t>(data[index]) << 16;
    if (rest == 2) chunk |= static_cast<uint32_t>(data[index + 1]) << 8;
    output[written++] = BASE64_URL[(chunk >> 18) & 0x3F];
    output[written++] = BASE64_URL[(chunk >> 12) & 0x3F];
    if (rest == 2) output[written++] = BASE64_URL[(chunk >> 6) & 0x3F];
  }
  output[written] = '\0';
  return written;
}

bool settingsBackupBase64Decode(const char *text, size_t length,
                                uint8_t *output, size_t capacity,
                                size_t &written) {
  written = 0;
  if (text == nullptr || output == nullptr) return false;
  while (length > 0 && text[length - 1] == '=') --length;
  if (length % 4 == 1) return false;
  uint32_t chunk = 0;
  unsigned bits = 0;
  for (size_t index = 0; index < length; ++index) {
    const int value = base64Value(text[index]);
    if (value < 0) return false;
    chunk = (chunk << 6) | static_cast<uint32_t>(value);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (written >= capacity) return false;
      output[written++] = static_cast<uint8_t>(chunk >> bits);
    }
  }
  return true;
}

bool settingsBackupValidName(const char *name) {
  if (name == nullptr || name[0] == '\0' || name[0] == '-') return false;
  size_t length = 0;
  for (const char *cursor = name; *cursor != '\0'; ++cursor) {
    const char character = *cursor;
    const bool allowed = (character >= 'a' && character <= 'z') ||
                         (character >= '0' && character <= '9') ||
                         character == '-';
    if (!allowed || ++length >= SETTINGS_BACKUP_NAME_LENGTH) return false;
  }
  return true;
}

bool settingsShareValidUrl(const char *url) {
  static constexpr char PREFIX[] = "https://";
  if (url == nullptr || strncmp(url, PREFIX, sizeof(PREFIX) - 1) != 0)
    return false;
  const size_t length = strlen(url);
  if (length <= sizeof(PREFIX) - 1 || length >= SETTINGS_SHARE_URL_LENGTH)
    return false;
  for (size_t index = 0; index < length; ++index) {
    const uint8_t character = static_cast<uint8_t>(url[index]);
    // Dotaz ani kotva k adrese nepatří: firmware za ni skládá název zálohy.
    if (character <= 0x20 || character >= 0x7F || character == '?' ||
        character == '#' || character == '\\')
      return false;
  }
  const char *host = url + sizeof(PREFIX) - 1;
  const char *hostEnd = strchr(host, '/');
  const char *at = strchr(host, '@');
  if (at != nullptr && (hostEnd == nullptr || at < hostEnd)) host = at + 1;
  return *host != '\0' && *host != '/' && *host != ':';
}

String settingsShareDisplayUrl(const char *url) {
  if (!settingsShareValidUrl(url)) return String();
  static constexpr char PREFIX[] = "https://";
  const char *host = url + sizeof(PREFIX) - 1;
  const char *hostEnd = strchr(host, '/');
  const char *at = strchr(host, '@');
  if (at != nullptr && (hostEnd == nullptr || at < hostEnd)) host = at + 1;
  String result(PREFIX);
  result += host;
  return result;
}
