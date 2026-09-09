#include "ClockConfig.h"

#include <Preferences.h>
#include <nvs_flash.h>

#include <cmath>

namespace {
constexpr uint32_t CONFIG_MAGIC = 0x57484346;
constexpr char CONFIG_PARTITION[] = "clockcfg";
constexpr char CONFIG_NAMESPACE[] = "clock-config";
constexpr char CONFIG_KEY[] = "config";
constexpr char APPEARANCE_NAMESPACE[] = "clock-look";
constexpr char APPEARANCE_STYLE_KEY[] = "style";
constexpr char APPEARANCE_TONE_KEY[] = "tone";
constexpr char APPEARANCE_HAND_TONE_KEY[] = "hand-tone";
constexpr char APPEARANCE_ACCENT_COLOR_KEY[] = "accent-color";
constexpr char APPEARANCE_ACCENTS_KEY[] = "accents";
constexpr char APPEARANCE_OUTLINE_HANDS_KEY[] = "outline-hands";
constexpr char APPEARANCE_MONO_VALUES_KEY[] = "mono-values";
constexpr char APPEARANCE_VALUES_ABOVE_KEY[] = "values-above";
constexpr char APPEARANCE_DATE_FORMAT_KEY[] = "date-format";
constexpr char APPEARANCE_DATE_COLOR_KEY[] = "date-color";
constexpr char APPEARANCE_WEATHER_COLOR_KEY[] = "weather-color";

struct ConfigRecord {
  uint32_t magic;
  uint32_t schemaVersion;
  ClockConfig config;
  uint32_t checksum;
};

constexpr uint32_t PUBLIC_1_5_5_SCHEMA_VERSION = 20;
constexpr uint32_t LANGUAGE_SCHEMA_VERSION = 25;
constexpr uint32_t RADAR_SCHEMA_VERSION = 24;
constexpr uint32_t TMEP_PREDECESSOR_SCHEMA_VERSION = 26;
constexpr uint32_t SIDE_VALUES_PREDECESSOR_SCHEMA_VERSION = 27;
constexpr uint32_t VALUE_SLOTS_PREDECESSOR_SCHEMA_VERSION = 28;
constexpr uint32_t RSS_PREDECESSOR_SCHEMA_VERSION = 29;
constexpr uint32_t BOTTOM_SLOT_PREDECESSOR_SCHEMA_VERSION = 30;
constexpr uint32_t RADAR_SOURCE_PREDECESSOR_SCHEMA_VERSION = 31;
constexpr uint32_t RADAR_STATUS_LINE_PREDECESSOR_SCHEMA_VERSION = 32;
constexpr uint32_t FORECAST_PREDECESSOR_SCHEMA_VERSION = 33;
constexpr uint32_t PLANES_PREDECESSOR_SCHEMA_VERSION = 34;

// Firmware 1.5.5 stored the same prefix as ClockConfig up to dateFormat.
// Keeping the payload as bytes preserves its exact released NVS layout and
// checksum without retaining every unreleased development migration.
constexpr size_t PUBLIC_1_5_5_CONFIG_SIZE = offsetof(ClockConfig, radarRadiusKm);

struct ConfigRecordV155 {
  uint32_t magic;
  uint32_t schemaVersion;
  uint8_t config[PUBLIC_1_5_5_CONFIG_SIZE];
  uint32_t checksum;
};

constexpr size_t SCHEMA_26_CONFIG_SIZE = offsetof(ClockConfig, tmepExportKey);

struct ConfigRecordV26 {
  uint32_t magic;
  uint32_t schemaVersion;
  uint8_t config[SCHEMA_26_CONFIG_SIZE];
  uint32_t checksum;
};

constexpr size_t SCHEMA_27_CONFIG_SIZE = offsetof(ClockConfig, leftValue);

struct ConfigRecordV27 {
  uint32_t magic;
  uint32_t schemaVersion;
  uint8_t config[SCHEMA_27_CONFIG_SIZE];
  uint32_t checksum;
};

constexpr size_t SCHEMA_28_CONFIG_SIZE = offsetof(ClockConfig, slots);

struct ConfigRecordV28 {
  uint32_t magic;
  uint32_t schemaVersion;
  uint8_t config[SCHEMA_28_CONFIG_SIZE];
  uint32_t checksum;
};

constexpr size_t SCHEMA_29_CONFIG_SIZE = offsetof(ClockConfig, rss);

struct ConfigRecordV29 {
  uint32_t magic;
  uint32_t schemaVersion;
  uint8_t config[SCHEMA_29_CONFIG_SIZE];
  uint32_t checksum;
};

constexpr size_t SCHEMA_30_CONFIG_SIZE = offsetof(ClockConfig, bottomSlot);
constexpr size_t SCHEMA_31_CONFIG_SIZE = offsetof(ClockConfig, radarSource);
// Schéma 32 končilo boolem radarLegend, takže jeho ClockConfig měl na konci
// dva bajty zarovnávací výplně. Do záznamu i do kontrolního součtu se zapsaly,
// proto se musí načíst také - jinak by součet nikdy neseděl.
constexpr size_t SCHEMA_32_CONFIG_SIZE =
    (offsetof(ClockConfig, radarStatusLine) + alignof(ClockConfig) - 1) /
    alignof(ClockConfig) * alignof(ClockConfig);

struct ConfigRecordV30 {
  uint32_t magic;
  uint32_t schemaVersion;
  uint8_t config[SCHEMA_30_CONFIG_SIZE];
  uint32_t checksum;
};

struct ConfigRecordV31 {
  uint32_t magic;
  uint32_t schemaVersion;
  uint8_t config[SCHEMA_31_CONFIG_SIZE];
  uint32_t checksum;
};

struct ConfigRecordV32 {
  uint32_t magic;
  uint32_t schemaVersion;
  uint8_t config[SCHEMA_32_CONFIG_SIZE];
  uint32_t checksum;
};

// Schéma 33 končilo polem char[128], které leželo na zarovnané adrese, takže
// jeho ClockConfig žádnou koncovou výplň neměl - offset předpovědi se rovná
// jeho velikosti.
constexpr size_t SCHEMA_33_CONFIG_SIZE = offsetof(ClockConfig, forecast);

struct ConfigRecordV33 {
  uint32_t magic;
  uint32_t schemaVersion;
  uint8_t config[SCHEMA_33_CONFIG_SIZE];
  uint32_t checksum;
};

// Schéma 34 končilo strukturou předpovědi, jejíž poslední pole je uint16_t na
// zarovnané adrese, takže ani ono žádnou koncovou výplň nemělo - offset radaru
// letadel se rovná jeho velikosti.
constexpr size_t SCHEMA_34_CONFIG_SIZE = offsetof(ClockConfig, planes);

struct ConfigRecordV34 {
  uint32_t magic;
  uint32_t schemaVersion;
  uint8_t config[SCHEMA_34_CONFIG_SIZE];
  uint32_t checksum;
};

void applyLegacyValueSlotDefaults(ClockConfig &config);

// Zarovnávací výplň schématu 32 leží přesně tam, kde schéma 33 začíná, takže
// zkopírované bajty přepíšou první dvě nová pole nulami. Vrátíme jim proto
// výchozí hodnoty: stavový řádek radaru je součást obrazovky, ne obrazovka
// navíc, a tak se po povýšení firmwaru rovnou ukáže.
void applyLegacyRadarStatusLineDefaults(ClockConfig &config) {
  const ClockConfig defaults;
  config.radarStatusLine = defaults.radarStatusLine;
  config.radarStatusTemperatureEntityId[0] = '\0';
}

// Devátá hodnota se po povýšení firmwaru nesmí rozsvítit sama: mřížka by se
// bez vědomí majitele prodloužila o prázdný řádek.
void applyLegacyBottomSlotDefaults(ClockConfig &config) {
  config.bottomSlot = ClockValueSlotConfig{};
}

// Obrazovka zpráv se po povýšení firmwaru nesmí objevit sama od sebe: bez
// adresy kanálu není co ukazovat a rotace by jen blikala prázdnou stránkou.
void applyLegacyRssDefaults(ClockConfig &config) {
  config.rss = ClockRssConfig{};
  applyLegacyBottomSlotDefaults(config);
}

void applyLegacySideValueDefaults(ClockConfig &config) {
  config.leftValue = ClockSideValueConfig{};
  config.rightValue = ClockSideValueConfig{};
  // Původní levá a pravá teplota dovolovaly vlastní názvy. Zachováme je jako
  // vlastní hodnoty, aby samotné uložení nové stránky nepřejmenovalo například
  // VENKU nebo LOŽNICE na obecné TEPLOTA.
  config.leftValue.custom = true;
  config.rightValue.custom = true;
  clockConfigCopy(config.leftValue.preset,
                  sizeof(config.leftValue.preset), "custom");
  clockConfigCopy(config.rightValue.preset,
                  sizeof(config.rightValue.preset), "custom");
  config.leftValueColorScale = ClockMetricColorScale{};
  config.leftValueColorScale.points[0] = {0.0f, config.leftSide.color};
  config.rightValueColorScale = ClockMetricColorScale{};
  config.rightValueColorScale.points[0] = {0.0f, config.rightSide.color};
  applyLegacyValueSlotDefaults(config);
}

// Naplní první čtyři sloty schématu 29 z původních čtyř pozic dashboardu.
// Obrazovka CLOCK_STYLE_VALUES tak po migraci ukazuje přesně to, co doteď
// ukazovaly digitální hodiny; zbylé sloty zůstanou vypnuté.
void applyLegacyValueSlotDefaults(ClockConfig &config) {
  for (ClockValueSlotConfig &slot : config.slots) slot = ClockValueSlotConfig{};

  const ClockSideConfig *sides[] = {&config.leftSide, &config.rightSide};
  const ClockSideValueConfig *sideValues[] = {&config.leftValue,
                                              &config.rightValue};
  const ClockMetricColorScale *sideScales[] = {&config.leftValueColorScale,
                                               &config.rightValueColorScale};
  for (size_t index = 0; index < 2; ++index) {
    ClockValueSlotConfig &slot = config.slots[index];
    slot.enabled = true;
    slot.custom = sideValues[index]->custom;
    slot.decimals = sideValues[index]->decimals;
    clockConfigCopy(slot.preset, sizeof(slot.preset),
                    sideValues[index]->preset);
    clockConfigCopy(slot.name, sizeof(slot.name), sides[index]->name);
    clockConfigCopy(slot.entityId, sizeof(slot.entityId),
                    sides[index]->temperatureEntityId);
    clockConfigCopy(slot.suffix, sizeof(slot.suffix),
                    sideValues[index]->suffix);
    clockConfigCopy(slot.icon, sizeof(slot.icon), sides[index]->icon);
    slot.color = sides[index]->color;
    slot.colorScale = *sideScales[index];
  }

  const ClockMetricConfig *metrics[] = {&config.metricA, &config.metricB};
  const ClockMetricColorScale *metricScales[] = {&config.metricAColorScale,
                                                 &config.metricBColorScale};
  for (size_t index = 0; index < 2; ++index) {
    ClockValueSlotConfig &slot = config.slots[2 + index];
    slot.enabled = true;
    slot.custom = metrics[index]->custom;
    slot.decimals = metrics[index]->decimals;
    clockConfigCopy(slot.preset, sizeof(slot.preset), metrics[index]->preset);
    clockConfigCopy(slot.name, sizeof(slot.name), metrics[index]->name);
    clockConfigCopy(slot.entityId, sizeof(slot.entityId),
                    metrics[index]->entityId);
    clockConfigCopy(slot.suffix, sizeof(slot.suffix), metrics[index]->suffix);
    // Měřené hodnoty A/B dosud ikonu neměly.
    clockConfigCopy(slot.icon, sizeof(slot.icon), "none");
    slot.color = metricScales[index]->points[0].color;
    slot.colorScale = *metricScales[index];
  }

  applyLegacyRssDefaults(config);
}

void applyOpenMeteoDefaults(ClockConfig &config) {
  config.dataSource = CLOCK_DATA_SOURCE_OPEN_METEO;
  clockConfigCopy(config.openMeteoCity, sizeof(config.openMeteoCity), "Brno");
  config.openMeteoLatitude = 49.1951f;
  config.openMeteoLongitude = 16.6068f;
  config.openMeteoCountry = CLOCK_LOCATION_COUNTRY_CZECHIA;
  static const char *values[] = {"temperature_2m", "apparent_temperature",
                                 "relative_humidity_2m", "pressure_msl"};
  static const char *names[] = {"TEPLOTA", "POCITOVÁ", "VLHKOST", "TLAK"};
  static const uint32_t colors[] = {0x4CCBEC, 0xFFB843, 0x65C744, 0xFFB843};
  for (size_t index = 0; index < 4; ++index) {
    clockConfigCopy(config.openMeteoSlots[index].value,
                    sizeof(config.openMeteoSlots[index].value), values[index]);
    clockConfigCopy(config.openMeteoSlots[index].name,
                    sizeof(config.openMeteoSlots[index].name), names[index]);
    config.openMeteoSlots[index].color = colors[index];
  }
}

static_assert(PUBLIC_1_5_5_CONFIG_SIZE % alignof(ClockConfig) == 0,
              "Záznam veřejné verze 1.5.5 musí zahrnout koncový padding.");
static_assert(PUBLIC_1_5_5_CONFIG_SIZE == 2096 &&
                  sizeof(ConfigRecordV155) == 2108,
              "NVS formát veřejné verze 1.5.5 se nesmí změnit.");
static_assert(SCHEMA_26_CONFIG_SIZE == 2108 &&
                  sizeof(ConfigRecordV26) == 2120,
              "Migrační záznam schématu 26 musí zachovat přesnou velikost.");
static_assert(SCHEMA_27_CONFIG_SIZE == 2452 &&
                  sizeof(ConfigRecordV27) == 2464,
              "Migrační záznam schématu 27 musí zachovat přesnou velikost.");
static_assert(sizeof(ConfigRecordV155) <= sizeof(ConfigRecord),
              "Migrační záznam se musí vejít do společného pracovního bufferu.");
static_assert(sizeof(ConfigRecordV26) <= sizeof(ConfigRecord),
              "Schéma 26 se musí vejít do společného pracovního bufferu.");
static_assert(SCHEMA_28_CONFIG_SIZE == 2688 &&
                  sizeof(ConfigRecordV28) == 2700,
              "Migrační záznam schématu 28 musí zachovat přesnou velikost.");
static_assert(SCHEMA_29_CONFIG_SIZE == 5024 &&
                  sizeof(ConfigRecordV29) == 5036,
              "Migrační záznam schématu 29 musí zachovat přesnou velikost.");
static_assert(SCHEMA_30_CONFIG_SIZE == 5224 &&
                  sizeof(ConfigRecordV30) == 5236,
              "Migrační záznam schématu 30 musí zachovat přesnou velikost.");
static_assert(SCHEMA_32_CONFIG_SIZE == 5520,
              "Migrační záznam schématu 32 musí zachovat přesnou velikost.");
static_assert(SCHEMA_33_CONFIG_SIZE == 5648 &&
                  sizeof(ConfigRecordV33) == 5660,
              "Migrační záznam schématu 33 musí zachovat přesnou velikost.");
static_assert(SCHEMA_34_CONFIG_SIZE == 5656 &&
                  sizeof(ConfigRecordV34) == 5668,
              "Migrační záznam schématu 34 musí zachovat přesnou velikost.");
static_assert(sizeof(ConfigRecordV34) <= sizeof(ConfigRecord),
              "Schéma 34 se musí vejít do společného pracovního bufferu.");
static_assert(sizeof(ConfigRecordV33) <= sizeof(ConfigRecord),
              "Schéma 33 se musí vejít do společného pracovního bufferu.");
static_assert(sizeof(ConfigRecordV32) <= sizeof(ConfigRecord),
              "Schéma 32 se musí vejít do společného pracovního bufferu.");
static_assert(sizeof(ConfigRecordV30) <= sizeof(ConfigRecord),
              "Schéma 30 se musí vejít do společného pracovního bufferu.");
static_assert(sizeof(ConfigRecordV29) <= sizeof(ConfigRecord),
              "Schéma 29 se musí vejít do společného pracovního bufferu.");
static_assert(sizeof(ConfigRecordV27) <= sizeof(ConfigRecord),
              "Schéma 27 se musí vejít do společného pracovního bufferu.");
static_assert(sizeof(ConfigRecordV28) <= sizeof(ConfigRecord),
              "Schéma 28 se musí vejít do společného pracovního bufferu.");

uint32_t bytesChecksum(const uint8_t *bytes, size_t size) {
  uint32_t hash = 2166136261u;
  for (size_t index = 0; index < size; ++index) {
    hash ^= bytes[index];
    hash *= 16777619u;
  }
  return hash;
}

uint32_t configChecksum(const ClockConfig &config) {
  return bytesChecksum(reinterpret_cast<const uint8_t *>(&config),
                       sizeof(config));
}

void normalizeColorScale(ClockMetricColorScale &scale) {
  scale.count = constrain(scale.count, static_cast<uint8_t>(1),
                          static_cast<uint8_t>(CLOCK_METRIC_COLOR_POINT_COUNT));
  for (uint8_t index = 0; index < scale.count; ++index) {
    scale.points[index].color &= 0xFFFFFF;
  }
  for (uint8_t index = 1; index < scale.count; ++index) {
    const ClockMetricColorPoint point = scale.points[index];
    uint8_t position = index;
    while (position > 0 && scale.points[position - 1].value > point.value) {
      scale.points[position] = scale.points[position - 1];
      --position;
    }
    scale.points[position] = point;
  }
}

void normalizeConfig(ClockConfig &config) {
  config.schemaVersion = CLOCK_CONFIG_SCHEMA_VERSION;
  config.dayBrightness = constrain(config.dayBrightness, 1, 100);
  config.nightBrightness = constrain(config.nightBrightness, 1, 100);
  config.sunriseOffsetMinutes = constrain(config.sunriseOffsetMinutes, -60, 60);
  config.sunsetOffsetMinutes = constrain(config.sunsetOffsetMinutes, -60, 60);
  config.metricA.decimals = constrain(config.metricA.decimals, 0, 2);
  config.metricB.decimals = constrain(config.metricB.decimals, 0, 2);
  config.leftValue.decimals = constrain(config.leftValue.decimals, 0, 2);
  config.rightValue.decimals = constrain(config.rightValue.decimals, 0, 2);
  config.secondRingBackgroundDotSize =
      constrain(config.secondRingBackgroundDotSize, 1, 10);
  config.secondDotSize = constrain(config.secondDotSize, 1, 10);
  config.secondEffect = constrain(
      config.secondEffect, static_cast<uint8_t>(CLOCK_SECOND_EFFECT_DOTS),
      static_cast<uint8_t>(CLOCK_SECOND_EFFECT_COMET));
  config.timeColonEffect = constrain(
      config.timeColonEffect, static_cast<uint8_t>(CLOCK_TIME_COLON_STEADY),
      static_cast<uint8_t>(CLOCK_TIME_COLON_FADE));
  config.weatherIconStyle = constrain(
      config.weatherIconStyle,
      static_cast<uint8_t>(CLOCK_WEATHER_ICON_STYLE_MONOCHROME),
      static_cast<uint8_t>(CLOCK_WEATHER_ICON_STYLE_LINE));
  config.nightVisualMode = constrain(
      config.nightVisualMode, static_cast<uint8_t>(CLOCK_NIGHT_VISUAL_RED),
      static_cast<uint8_t>(CLOCK_NIGHT_VISUAL_BRIGHTNESS_ONLY));
  config.timeFont = constrain(
      config.timeFont, static_cast<uint8_t>(CLOCK_TIME_FONT_BARLOW),
      static_cast<uint8_t>(CLOCK_TIME_FONT_DOTO));
  config.dateFormat = constrain(
      config.dateFormat,
      static_cast<uint8_t>(CLOCK_DATE_FORMAT_WEEKDAY_DAY_MONTH),
      static_cast<uint8_t>(CLOCK_DATE_FORMAT_DAY_MONTH));
  config.dataSource = constrain(
      config.dataSource, static_cast<uint8_t>(CLOCK_DATA_SOURCE_OPEN_METEO),
      static_cast<uint8_t>(CLOCK_DATA_SOURCE_HOME_ASSISTANT));
  config.language = constrain(
      config.language, static_cast<uint8_t>(CLOCK_LANGUAGE_UNSET),
      static_cast<uint8_t>(CLOCK_LANGUAGE_ENGLISH));
  if (config.openMeteoCountry < CLOCK_LOCATION_COUNTRY_CZECHIA ||
      config.openMeteoCountry > CLOCK_LOCATION_COUNTRY_OTHER) {
    // Verze 1.5.5 i všechna dosavadní vývojová schémata byla určená české
    // komunitě. Konfigurace bez uložené země proto při migraci dostane CZ.
    // Každé nové vyhledání už ukládá výslovný country_code z Open-Meteo.
    config.openMeteoCountry = CLOCK_LOCATION_COUNTRY_CZECHIA;
  }
  if (config.openMeteoCountry != CLOCK_LOCATION_COUNTRY_CZECHIA)
    config.automaticRadarRotation = false;
  if (config.radarRadiusKm != 0 && config.radarRadiusKm != 25 &&
      config.radarRadiusKm != 50 &&
      config.radarRadiusKm != 100 && config.radarRadiusKm != 200) {
    config.radarRadiusKm = 50;
  }
  config.radarFrameCount = constrain(config.radarFrameCount, 1, 15);
  config.clockDisplaySeconds =
      constrain(config.clockDisplaySeconds, 10, 3600);
  config.radarDisplaySeconds =
      constrain(config.radarDisplaySeconds, 10, 3600);
  config.radarMapOpacity = constrain(config.radarMapOpacity, 0, 100);
  config.radarPauseSeconds = constrain(config.radarPauseSeconds, 0, 30);
  if (config.radarSource > CLOCK_RADAR_SOURCE_RAINVIEWER)
    config.radarSource = CLOCK_RADAR_SOURCE_CHMI;
  config.rss.itemCount = constrain(config.rss.itemCount, CLOCK_RSS_MIN_ITEMS,
                                   CLOCK_RSS_MAX_ITEMS);
  config.rss.refreshMinutes = constrain(config.rss.refreshMinutes, 5, 120);
  config.rss.displaySeconds = constrain(config.rss.displaySeconds, 10, 3600);
  config.forecast.dayCount =
      constrain(config.forecast.dayCount, static_cast<uint8_t>(0),
                CLOCK_FORECAST_MAX_DAYS);
  config.forecast.refreshMinutes =
      constrain(config.forecast.refreshMinutes, 10, 180);
  config.forecast.displaySeconds =
      constrain(config.forecast.displaySeconds, 10, 3600);
  if (config.planes.rangeIndex >= CLOCK_PLANE_RANGE_COUNT)
    config.planes.rangeIndex = 1;
  config.planes.refreshSeconds = constrain(config.planes.refreshSeconds, 5, 120);
  config.planes.displaySeconds =
      constrain(config.planes.displaySeconds, 10, 3600);
  if (config.planes.topBearingDeg >= 360) config.planes.topBearingDeg = 0;
  config.planes.altitudeMaxFt =
      constrain(config.planes.altitudeMaxFt, static_cast<uint16_t>(0),
                CLOCK_PLANE_ALTITUDE_CEILING_FT);
  config.planes.altitudeMinFt =
      constrain(config.planes.altitudeMinFt, static_cast<uint16_t>(0),
                CLOCK_PLANE_ALTITUDE_CEILING_FT);
  // Nulová horní mez by schovala všechno, co výšku hlásí. Dvě sousední políčka
  // formuláře dávají nule opačný význam - u dolní meze znamená "bez omezení" -
  // takže je to snadný překlep; bere se proto jako strop.
  if (config.planes.altitudeMaxFt == 0)
    config.planes.altitudeMaxFt = CLOCK_PLANE_ALTITUDE_CEILING_FT;
  // Prohozené meze by schovaly všechno; filtr se v tom případě vypne, protože
  // to je zjevně překlep, ne přání vidět prázdnou oblohu.
  if (config.planes.altitudeMinFt > config.planes.altitudeMaxFt) {
    config.planes.altitudeMinFt = 0;
    config.planes.altitudeMaxFt = CLOCK_PLANE_ALTITUDE_CEILING_FT;
  }
  if (!std::isfinite(config.openMeteoLatitude) ||
      config.openMeteoLatitude < -90.0f || config.openMeteoLatitude > 90.0f ||
      !std::isfinite(config.openMeteoLongitude) ||
      config.openMeteoLongitude < -180.0f || config.openMeteoLongitude > 180.0f) {
    config.openMeteoLatitude = 49.1951f;
    config.openMeteoLongitude = 16.6068f;
  }
  config.secondRingBackgroundColor &= 0xFFFFFF;
  config.secondDotColor &= 0xFFFFFF;
  config.leftSide.color &= 0xFFFFFF;
  config.rightSide.color &= 0xFFFFFF;
  for (ClockOpenMeteoSlotConfig &slot : config.openMeteoSlots) {
    slot.color &= 0xFFFFFF;
  }
  for (ClockTmepSlotConfig &slot : config.tmepSlots) {
    slot.decimals = constrain(slot.decimals, static_cast<uint8_t>(0),
                              static_cast<uint8_t>(2));
    if (slot.sensorId[0] == '\0' || slot.field[0] == '\0' ||
        slot.unit[0] == '\0') {
      slot = ClockTmepSlotConfig{};
    }
  }
  config.timeColor &= 0xFFFFFF;
  config.dateColor &= 0xFFFFFF;
  config.leftWeatherIconColor &= 0xFFFFFF;
  config.rightWeatherIconColor &= 0xFFFFFF;
  ClockMetricColorScale *scales[] = {
      &config.leftValueColorScale, &config.rightValueColorScale,
      &config.metricAColorScale, &config.metricBColorScale};
  for (ClockMetricColorScale *scale : scales) normalizeColorScale(*scale);
  // Devět slotů obrazovky CLOCK_STYLE_VALUES prochází stejnou kontrolou jako
  // starší pozice. Prázdné entityId slot nevypíná: sloty 0-3 se v režimu
  // Open-Meteo plní z původních pozic, ne z Home Assistantu.
  for (size_t index = 0; index < CLOCK_VALUE_SLOT_COUNT; ++index) {
    ClockValueSlotConfig &slot = clockConfigValueSlot(config, index);
    slot.decimals = constrain(slot.decimals, static_cast<uint8_t>(0),
                              static_cast<uint8_t>(2));
    slot.color &= 0xFFFFFF;
    normalizeColorScale(slot.colorScale);
  }
}
}  // namespace

bool clockConfigRadarAvailable(const ClockConfig &config) {
  // Kompozice ČHMÚ končí na hranicích, takže mimo ČR nemá co ukázat.
  // RainViewer pokrývá celou Evropu, a proto tuhle podmínku obchází - právě
  // kvůli tomu je druhý zdroj v nastavení.
  if (config.radarSource == CLOCK_RADAR_SOURCE_RAINVIEWER) return true;
  return config.openMeteoCountry == CLOCK_LOCATION_COUNTRY_CZECHIA;
}

bool clockConfigRssAvailable(const ClockConfig &config) {
  return config.rss.enabled && config.rss.url[0] != '\0';
}

bool clockConfigForecastAvailable(const ClockConfig &config) {
  return config.forecast.enabled;
}

bool clockConfigPlanesAvailable(const ClockConfig &config) {
  // adsb.fi pokrývá celý svět, takže na rozdíl od kompozice ČHMÚ nemá obrazovka
  // žádnou zeměpisnou podmínku - stačí, že ji majitel zapnul.
  return config.planes.enabled;
}

bool clockAppearanceLoad(ClockAppearanceConfig &appearance,
                         uint32_t defaultMonochromeWeatherIconColor,
                         uint8_t defaultAnalogDateFormat,
                         uint32_t defaultAnalogDateColor) {
  appearance = ClockAppearanceConfig{};
  appearance.monochromeWeatherIconColor =
      defaultMonochromeWeatherIconColor & 0xFFFFFF;
  Preferences preferences;
  if (!preferences.begin(APPEARANCE_NAMESPACE, true, CONFIG_PARTITION))
    return false;
  appearance.style = constrain(
      preferences.getUChar(APPEARANCE_STYLE_KEY, CLOCK_STYLE_DIGITAL),
      static_cast<uint8_t>(CLOCK_STYLE_DIGITAL),
      static_cast<uint8_t>(CLOCK_STYLE_VALUES));
  appearance.analogToneColor =
      preferences.getUInt(APPEARANCE_TONE_KEY, 0x00D6FF) & 0xFFFFFF;
  appearance.analogHandToneColor =
      preferences.getUInt(APPEARANCE_HAND_TONE_KEY,
                          appearance.analogToneColor) &
      0xFFFFFF;
  appearance.analogCardinalAccentColor =
      preferences.getUInt(APPEARANCE_ACCENT_COLOR_KEY, 0xFFAB00) &
      0xFFFFFF;
  appearance.analogCardinalAccentsEnabled =
      preferences.getBool(APPEARANCE_ACCENTS_KEY, true);
  appearance.analogOutlineHandsEnabled =
      preferences.getBool(APPEARANCE_OUTLINE_HANDS_KEY, false);
  appearance.analogMonochromeValuesEnabled =
      preferences.getBool(APPEARANCE_MONO_VALUES_KEY, false);
  appearance.analogValuesAboveHandsEnabled =
      preferences.getBool(APPEARANCE_VALUES_ABOVE_KEY, false);
  appearance.analogDateFormat = constrain(
      preferences.getUChar(APPEARANCE_DATE_FORMAT_KEY,
                           defaultAnalogDateFormat),
      static_cast<uint8_t>(CLOCK_DATE_FORMAT_WEEKDAY_DAY_MONTH),
      static_cast<uint8_t>(CLOCK_DATE_FORMAT_DAY_MONTH));
  appearance.analogDateColor =
      preferences.getUInt(APPEARANCE_DATE_COLOR_KEY,
                          defaultAnalogDateColor) &
      0xFFFFFF;
  appearance.monochromeWeatherIconColor =
      preferences.getUInt(APPEARANCE_WEATHER_COLOR_KEY,
                          defaultMonochromeWeatherIconColor) &
      0xFFFFFF;
  preferences.end();
  return true;
}

bool clockAppearanceSave(const ClockAppearanceConfig &appearance) {
  Preferences preferences;
  if (!preferences.begin(APPEARANCE_NAMESPACE, false, CONFIG_PARTITION))
    return false;
  const uint8_t style = constrain(
      appearance.style, static_cast<uint8_t>(CLOCK_STYLE_DIGITAL),
      static_cast<uint8_t>(CLOCK_STYLE_VALUES));
  const bool styleSaved =
      preferences.putUChar(APPEARANCE_STYLE_KEY, style) == sizeof(style);
  const bool toneSaved =
      preferences.putUInt(APPEARANCE_TONE_KEY,
                          appearance.analogToneColor & 0xFFFFFF) ==
      sizeof(uint32_t);
  const bool handToneSaved =
      preferences.putUInt(APPEARANCE_HAND_TONE_KEY,
                          appearance.analogHandToneColor & 0xFFFFFF) ==
      sizeof(uint32_t);
  const bool accentColorSaved =
      preferences.putUInt(APPEARANCE_ACCENT_COLOR_KEY,
                          appearance.analogCardinalAccentColor & 0xFFFFFF) ==
      sizeof(uint32_t);
  const bool accentsSaved =
      preferences.putBool(APPEARANCE_ACCENTS_KEY,
                          appearance.analogCardinalAccentsEnabled) ==
      sizeof(bool);
  const bool outlineHandsSaved =
      preferences.putBool(APPEARANCE_OUTLINE_HANDS_KEY,
                          appearance.analogOutlineHandsEnabled) ==
      sizeof(bool);
  const bool monoValuesSaved =
      preferences.putBool(APPEARANCE_MONO_VALUES_KEY,
                          appearance.analogMonochromeValuesEnabled) ==
      sizeof(bool);
  const bool valuesAboveSaved =
      preferences.putBool(APPEARANCE_VALUES_ABOVE_KEY,
                          appearance.analogValuesAboveHandsEnabled) ==
      sizeof(bool);
  const uint8_t dateFormat = constrain(
      appearance.analogDateFormat,
      static_cast<uint8_t>(CLOCK_DATE_FORMAT_WEEKDAY_DAY_MONTH),
      static_cast<uint8_t>(CLOCK_DATE_FORMAT_DAY_MONTH));
  const bool dateFormatSaved =
      preferences.putUChar(APPEARANCE_DATE_FORMAT_KEY, dateFormat) ==
      sizeof(dateFormat);
  const bool dateColorSaved =
      preferences.putUInt(APPEARANCE_DATE_COLOR_KEY,
                          appearance.analogDateColor & 0xFFFFFF) ==
      sizeof(uint32_t);
  const bool weatherColorSaved =
      preferences.putUInt(APPEARANCE_WEATHER_COLOR_KEY,
                          appearance.monochromeWeatherIconColor & 0xFFFFFF) ==
      sizeof(uint32_t);
  preferences.end();
  return styleSaved && toneSaved && handToneSaved && accentColorSaved &&
         accentsSaved && outlineHandsSaved && monoValuesSaved &&
         valuesAboveSaved && dateFormatSaved && dateColorSaved &&
         weatherColorSaved;
}

void clockConfigCopy(char *destination, size_t destinationSize,
                     const String &value) {
  clockConfigCopy(destination, destinationSize, value.c_str());
}

void clockConfigCopy(char *destination, size_t destinationSize,
                     const char *value) {
  if (destinationSize == 0) return;
  strlcpy(destination, value == nullptr ? "" : value, destinationSize);
}

void clockConfigApplyDefaults(ClockConfig &config) {
  config = ClockConfig{};
  applyOpenMeteoDefaults(config);
  clockConfigCopy(config.leftSide.name, sizeof(config.leftSide.name), "VENKU");
  clockConfigCopy(config.leftSide.icon, sizeof(config.leftSide.icon), "weather");
  config.leftSide.color = 0x4CCBEC;
  clockConfigCopy(config.rightSide.name, sizeof(config.rightSide.name),
                  "MÍSTNOST");
  clockConfigCopy(config.rightSide.icon, sizeof(config.rightSide.icon), "home");
  config.rightSide.color = 0xFFB843;
  config.metricA.custom = false;
  clockConfigCopy(config.metricA.preset, sizeof(config.metricA.preset), "co2");
  clockConfigCopy(config.metricA.name, sizeof(config.metricA.name), "CO₂");
  clockConfigCopy(config.metricA.suffix, sizeof(config.metricA.suffix), "ppm");
  config.metricA.decimals = 0;

  config.metricB.custom = false;
  clockConfigCopy(config.metricB.preset, sizeof(config.metricB.preset),
                  "humidity");
  clockConfigCopy(config.metricB.name, sizeof(config.metricB.name), "VLHKOST");
  clockConfigCopy(config.metricB.suffix, sizeof(config.metricB.suffix), "%");
  config.metricB.decimals = 0;
  config.metricAColorScale = ClockMetricColorScale{};
  config.metricAColorScale.points[0] = {0.0f, 0x65C744};
  config.metricBColorScale = ClockMetricColorScale{};
  config.metricBColorScale.points[0] = {0.0f, 0xFFB843};
  applyLegacySideValueDefaults(config);
}

bool clockConfigBegin() {
  return nvs_flash_init_partition(CONFIG_PARTITION) == ESP_OK;
}

bool clockConfigLoad(ClockConfig &config) {
  clockConfigApplyDefaults(config);
  Preferences preferences;
  if (!preferences.begin(CONFIG_NAMESPACE, false, CONFIG_PARTITION)) return false;

  // Aktuální i jediný podporovaný migrační záznam sdílejí jeden statický
  // buffer. Konfigurace je velká a nemá ležet na zásobníku loopTask.
  static ConfigRecord record;
  record = ConfigRecord{};
  const size_t storedSize = preferences.getBytesLength(CONFIG_KEY);
  const bool supportedSize = storedSize == sizeof(record) ||
                             storedSize == sizeof(ConfigRecordV34) ||
                             storedSize == sizeof(ConfigRecordV33) ||
                             storedSize == sizeof(ConfigRecordV32) ||
                             storedSize == sizeof(ConfigRecordV31) ||
                             storedSize == sizeof(ConfigRecordV30) ||
                             storedSize == sizeof(ConfigRecordV29) ||
                             storedSize == sizeof(ConfigRecordV28) ||
                             storedSize == sizeof(ConfigRecordV27) ||
                             storedSize == sizeof(ConfigRecordV26) ||
                             storedSize == sizeof(ConfigRecordV155);
  const bool readComplete =
      supportedSize && preferences.getBytes(CONFIG_KEY, &record, storedSize) ==
                           storedSize;
  preferences.end();

  const bool currentRecord =
      readComplete && storedSize == sizeof(record) &&
      record.magic == CONFIG_MAGIC &&
      record.schemaVersion == CLOCK_CONFIG_SCHEMA_VERSION &&
      record.config.schemaVersion == CLOCK_CONFIG_SCHEMA_VERSION &&
      record.checksum == configChecksum(record.config);
  if (currentRecord) {
    config = record.config;
    normalizeConfig(config);
    return true;
  }

  // Schéma 34 je přesnou předponou schématu 35; radar letadel si po zkopírování
  // bajtů podrží výchozí hodnoty z clockConfigApplyDefaults(), tedy vypnutou
  // obrazovku - povýšení firmwaru samo od sebe nezačne stahovat z adsb.fi.
  const ConfigRecordV34 &legacyV34 =
      *reinterpret_cast<const ConfigRecordV34 *>(&record);
  uint32_t embeddedSchemaV34 = 0;
  if (readComplete && storedSize == sizeof(legacyV34)) {
    memcpy(&embeddedSchemaV34, legacyV34.config, sizeof(embeddedSchemaV34));
  }
  const bool validSchema34Record =
      readComplete && storedSize == sizeof(legacyV34) &&
      legacyV34.magic == CONFIG_MAGIC &&
      legacyV34.schemaVersion == PLANES_PREDECESSOR_SCHEMA_VERSION &&
      embeddedSchemaV34 == PLANES_PREDECESSOR_SCHEMA_VERSION &&
      legacyV34.checksum ==
          bytesChecksum(legacyV34.config, sizeof(legacyV34.config));
  if (validSchema34Record) {
    memcpy(&config, legacyV34.config, sizeof(legacyV34.config));
    config.schemaVersion = CLOCK_CONFIG_SCHEMA_VERSION;
    normalizeConfig(config);
    return clockConfigSave(config);
  }

  // Schéma 33 je přesnou předponou schématu 34; obrazovka předpovědi si po
  // zkopírování bajtů podrží výchozí hodnoty z clockConfigApplyDefaults(),
  // tedy vypnutou obrazovku.
  const ConfigRecordV33 &legacyV33 =
      *reinterpret_cast<const ConfigRecordV33 *>(&record);
  uint32_t embeddedSchemaV33 = 0;
  if (readComplete && storedSize == sizeof(legacyV33)) {
    memcpy(&embeddedSchemaV33, legacyV33.config, sizeof(embeddedSchemaV33));
  }
  const bool validSchema33Record =
      readComplete && storedSize == sizeof(legacyV33) &&
      legacyV33.magic == CONFIG_MAGIC &&
      legacyV33.schemaVersion == FORECAST_PREDECESSOR_SCHEMA_VERSION &&
      embeddedSchemaV33 == FORECAST_PREDECESSOR_SCHEMA_VERSION &&
      legacyV33.checksum ==
          bytesChecksum(legacyV33.config, sizeof(legacyV33.config));
  if (validSchema33Record) {
    memcpy(&config, legacyV33.config, sizeof(legacyV33.config));
    config.schemaVersion = CLOCK_CONFIG_SCHEMA_VERSION;
    normalizeConfig(config);
    return clockConfigSave(config);
  }

  // Schéma 32 je přesnou předponou schématu 33; stavový řádek radaru si po
  // zkopírování bajtů podrží výchozí hodnoty z clockConfigApplyDefaults().
  const ConfigRecordV32 &legacyV32 =
      *reinterpret_cast<const ConfigRecordV32 *>(&record);
  uint32_t embeddedSchemaV32 = 0;
  if (readComplete && storedSize == sizeof(legacyV32)) {
    memcpy(&embeddedSchemaV32, legacyV32.config, sizeof(embeddedSchemaV32));
  }
  const bool validSchema32Record =
      readComplete && storedSize == sizeof(legacyV32) &&
      legacyV32.magic == CONFIG_MAGIC &&
      legacyV32.schemaVersion == RADAR_STATUS_LINE_PREDECESSOR_SCHEMA_VERSION &&
      embeddedSchemaV32 == RADAR_STATUS_LINE_PREDECESSOR_SCHEMA_VERSION &&
      legacyV32.checksum ==
          bytesChecksum(legacyV32.config, sizeof(legacyV32.config));
  if (validSchema32Record) {
    memcpy(&config, legacyV32.config, sizeof(legacyV32.config));
    config.schemaVersion = CLOCK_CONFIG_SCHEMA_VERSION;
    applyLegacyRadarStatusLineDefaults(config);
    normalizeConfig(config);
    return clockConfigSave(config);
  }

  // Schéma 31 je přesnou předponou schématu 32; zdroj radaru a legenda si po
  // zkopírování bajtů podrží výchozí hodnoty z clockConfigApplyDefaults().
  const ConfigRecordV31 &legacyV31 =
      *reinterpret_cast<const ConfigRecordV31 *>(&record);
  uint32_t embeddedSchemaV31 = 0;
  if (readComplete && storedSize == sizeof(legacyV31)) {
    memcpy(&embeddedSchemaV31, legacyV31.config, sizeof(embeddedSchemaV31));
  }
  const bool validSchema31Record =
      readComplete && storedSize == sizeof(legacyV31) &&
      legacyV31.magic == CONFIG_MAGIC &&
      legacyV31.schemaVersion == RADAR_SOURCE_PREDECESSOR_SCHEMA_VERSION &&
      embeddedSchemaV31 == RADAR_SOURCE_PREDECESSOR_SCHEMA_VERSION &&
      legacyV31.checksum ==
          bytesChecksum(legacyV31.config, sizeof(legacyV31.config));
  if (validSchema31Record) {
    memcpy(&config, legacyV31.config, sizeof(legacyV31.config));
    config.schemaVersion = CLOCK_CONFIG_SCHEMA_VERSION;
    normalizeConfig(config);
    return clockConfigSave(config);
  }

  const ConfigRecordV30 &legacyV30 =
      *reinterpret_cast<const ConfigRecordV30 *>(&record);
  uint32_t embeddedSchemaV30 = 0;
  if (readComplete && storedSize == sizeof(legacyV30)) {
    memcpy(&embeddedSchemaV30, legacyV30.config, sizeof(embeddedSchemaV30));
  }
  const bool validSchema30Record =
      readComplete && storedSize == sizeof(legacyV30) &&
      legacyV30.magic == CONFIG_MAGIC &&
      legacyV30.schemaVersion == BOTTOM_SLOT_PREDECESSOR_SCHEMA_VERSION &&
      embeddedSchemaV30 == BOTTOM_SLOT_PREDECESSOR_SCHEMA_VERSION &&
      legacyV30.checksum ==
          bytesChecksum(legacyV30.config, sizeof(legacyV30.config));
  if (validSchema30Record) {
    memcpy(&config, legacyV30.config, sizeof(legacyV30.config));
    config.schemaVersion = CLOCK_CONFIG_SCHEMA_VERSION;
    applyLegacyBottomSlotDefaults(config);
    normalizeConfig(config);
    return clockConfigSave(config);
  }

  const ConfigRecordV29 &legacyV29 =
      *reinterpret_cast<const ConfigRecordV29 *>(&record);
  uint32_t embeddedSchemaV29 = 0;
  if (readComplete && storedSize == sizeof(legacyV29)) {
    memcpy(&embeddedSchemaV29, legacyV29.config, sizeof(embeddedSchemaV29));
  }
  const bool validSchema29Record =
      readComplete && storedSize == sizeof(legacyV29) &&
      legacyV29.magic == CONFIG_MAGIC &&
      legacyV29.schemaVersion == RSS_PREDECESSOR_SCHEMA_VERSION &&
      embeddedSchemaV29 == RSS_PREDECESSOR_SCHEMA_VERSION &&
      legacyV29.checksum ==
          bytesChecksum(legacyV29.config, sizeof(legacyV29.config));
  if (validSchema29Record) {
    memcpy(&config, legacyV29.config, sizeof(legacyV29.config));
    config.schemaVersion = CLOCK_CONFIG_SCHEMA_VERSION;
    applyLegacyRssDefaults(config);
    normalizeConfig(config);
    return clockConfigSave(config);
  }

  const ConfigRecordV28 &legacyV28 =
      *reinterpret_cast<const ConfigRecordV28 *>(&record);
  uint32_t embeddedSchemaV28 = 0;
  if (readComplete && storedSize == sizeof(legacyV28)) {
    memcpy(&embeddedSchemaV28, legacyV28.config, sizeof(embeddedSchemaV28));
  }
  const bool validSchema28Record =
      readComplete && storedSize == sizeof(legacyV28) &&
      legacyV28.magic == CONFIG_MAGIC &&
      legacyV28.schemaVersion == VALUE_SLOTS_PREDECESSOR_SCHEMA_VERSION &&
      embeddedSchemaV28 == VALUE_SLOTS_PREDECESSOR_SCHEMA_VERSION &&
      legacyV28.checksum ==
          bytesChecksum(legacyV28.config, sizeof(legacyV28.config));
  if (validSchema28Record) {
    memcpy(&config, legacyV28.config, sizeof(legacyV28.config));
    config.schemaVersion = CLOCK_CONFIG_SCHEMA_VERSION;
    applyLegacyValueSlotDefaults(config);
    normalizeConfig(config);
    return clockConfigSave(config);
  }

  const ConfigRecordV27 &legacyV27 =
      *reinterpret_cast<const ConfigRecordV27 *>(&record);
  uint32_t embeddedSchemaV27 = 0;
  if (readComplete && storedSize == sizeof(legacyV27)) {
    memcpy(&embeddedSchemaV27, legacyV27.config,
           sizeof(embeddedSchemaV27));
  }
  const bool validSchema27Record =
      readComplete && storedSize == sizeof(legacyV27) &&
      legacyV27.magic == CONFIG_MAGIC &&
      legacyV27.schemaVersion == SIDE_VALUES_PREDECESSOR_SCHEMA_VERSION &&
      embeddedSchemaV27 == SIDE_VALUES_PREDECESSOR_SCHEMA_VERSION &&
      legacyV27.checksum ==
          bytesChecksum(legacyV27.config, sizeof(legacyV27.config));
  if (validSchema27Record) {
    memcpy(&config, legacyV27.config, sizeof(legacyV27.config));
    config.schemaVersion = CLOCK_CONFIG_SCHEMA_VERSION;
    applyLegacySideValueDefaults(config);
    normalizeConfig(config);
    return clockConfigSave(config);
  }

  const ConfigRecordV26 &legacyV26 =
      *reinterpret_cast<const ConfigRecordV26 *>(&record);
  uint32_t embeddedSchemaV26 = 0;
  if (readComplete && storedSize == sizeof(legacyV26)) {
    memcpy(&embeddedSchemaV26, legacyV26.config,
           sizeof(embeddedSchemaV26));
  }
  const bool validSchema26Prefix =
      readComplete && storedSize == sizeof(legacyV26) &&
      legacyV26.magic == CONFIG_MAGIC &&
      legacyV26.schemaVersion >= RADAR_SCHEMA_VERSION &&
      legacyV26.schemaVersion <= TMEP_PREDECESSOR_SCHEMA_VERSION &&
      embeddedSchemaV26 == legacyV26.schemaVersion &&
      legacyV26.checksum ==
          bytesChecksum(legacyV26.config, sizeof(legacyV26.config));

  if (validSchema26Prefix &&
      legacyV26.schemaVersion == TMEP_PREDECESSOR_SCHEMA_VERSION) {
    memcpy(&config, legacyV26.config, sizeof(legacyV26.config));
    config.schemaVersion = CLOCK_CONFIG_SCHEMA_VERSION;
    config.tmepExportKey[0] = '\0';
    config.tmepExportId[0] = '\0';
    for (ClockTmepSlotConfig &slot : config.tmepSlots)
      slot = ClockTmepSlotConfig{};
    normalizeConfig(config);
    return clockConfigSave(config);
  }

  // Schema 25 used 0 for Czech and 1 for English. Preserve that explicit
  // choice while migrating to the tri-state representation.
  const bool validLanguageRecord =
      validSchema26Prefix &&
      legacyV26.schemaVersion == LANGUAGE_SCHEMA_VERSION;
  if (validLanguageRecord) {
    memcpy(&config, legacyV26.config, sizeof(legacyV26.config));
    const uint8_t legacyLanguage = config.language;
    config.schemaVersion = CLOCK_CONFIG_SCHEMA_VERSION;
    config.language = legacyLanguage == 1 ? CLOCK_LANGUAGE_ENGLISH
                                          : CLOCK_LANGUAGE_CZECH;
    config.openMeteoCountry = CLOCK_LOCATION_COUNTRY_CZECHIA;
    config.tmepExportKey[0] = '\0';
    config.tmepExportId[0] = '\0';
    for (ClockTmepSlotConfig &slot : config.tmepSlots)
      slot = ClockTmepSlotConfig{};
    normalizeConfig(config);
    return clockConfigSave(config);
  }

  // Schema 24 has the same binary size. The language byte occupied trailing
  // padding, so the old checksum can be verified before migration.
  const bool validRadarRecord =
      validSchema26Prefix && legacyV26.schemaVersion == RADAR_SCHEMA_VERSION;
  if (validRadarRecord) {
    memcpy(&config, legacyV26.config, sizeof(legacyV26.config));
    config.schemaVersion = CLOCK_CONFIG_SCHEMA_VERSION;
    config.language = CLOCK_LANGUAGE_UNSET;
    config.openMeteoCountry = CLOCK_LOCATION_COUNTRY_CZECHIA;
    config.tmepExportKey[0] = '\0';
    config.tmepExportId[0] = '\0';
    for (ClockTmepSlotConfig &slot : config.tmepSlots)
      slot = ClockTmepSlotConfig{};
    normalizeConfig(config);
    return clockConfigSave(config);
  }

  const ConfigRecordV155 &legacy =
      *reinterpret_cast<const ConfigRecordV155 *>(&record);
  uint32_t embeddedSchemaVersion = 0;
  if (readComplete && storedSize == sizeof(legacy)) {
    memcpy(&embeddedSchemaVersion, legacy.config,
           sizeof(embeddedSchemaVersion));
  }
  const bool validPublic155Record =
      readComplete && storedSize == sizeof(legacy) &&
      legacy.magic == CONFIG_MAGIC &&
      legacy.schemaVersion == PUBLIC_1_5_5_SCHEMA_VERSION &&
      embeddedSchemaVersion == PUBLIC_1_5_5_SCHEMA_VERSION &&
      legacy.checksum == bytesChecksum(legacy.config, sizeof(legacy.config));
  if (!validPublic155Record) return clockConfigSave(config);

  memcpy(&config, legacy.config, sizeof(legacy.config));
  config.schemaVersion = CLOCK_CONFIG_SCHEMA_VERSION;
  config.radarRadiusKm = 0;
  config.radarFrameCount = 6;
  config.automaticRadarRotation = false;
  config.clockDisplaySeconds = 120;
  config.radarDisplaySeconds = 20;
  config.radarMapOpacity = 100;
  config.radarPauseSeconds = 5;
  config.radarSource = CLOCK_RADAR_SOURCE_CHMI;
  config.radarLegend = true;
  config.language = CLOCK_LANGUAGE_UNSET;
  config.openMeteoCountry = CLOCK_LOCATION_COUNTRY_CZECHIA;
  config.tmepExportKey[0] = '\0';
  config.tmepExportId[0] = '\0';
  for (ClockTmepSlotConfig &slot : config.tmepSlots)
    slot = ClockTmepSlotConfig{};
  config.rss = ClockRssConfig{};
  normalizeConfig(config);
  return clockConfigSave(config);
}

bool clockConfigSave(const ClockConfig &config) {
  static ConfigRecord record;
  record = ConfigRecord{};
  record.magic = CONFIG_MAGIC;
  record.schemaVersion = CLOCK_CONFIG_SCHEMA_VERSION;
  record.config = config;
  normalizeConfig(record.config);
  record.checksum = configChecksum(record.config);

  Preferences preferences;
  if (!preferences.begin(CONFIG_NAMESPACE, false, CONFIG_PARTITION)) return false;
  bool ok =
      preferences.putBytes(CONFIG_KEY, &record, sizeof(record)) == sizeof(record);
  if (!ok && preferences.remove(CONFIG_KEY)) {
    // Velký konfigurační blob při mnoha změnách schématu může zaplnit NVS
    // historickými verzemi. Odstranění pouze tohoto klíče umožní NVS staré
    // blobové stránky zkompaktovat; ostatní namespace v clockcfg zůstávají.
    preferences.end();
    if (!preferences.begin(CONFIG_NAMESPACE, false, CONFIG_PARTITION))
      return false;
    ok = preferences.putBytes(CONFIG_KEY, &record, sizeof(record)) ==
         sizeof(record);
  }
  preferences.end();
  return ok;
}
