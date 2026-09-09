#pragma once

#include <Arduino.h>

constexpr size_t CLOCK_ROOM_NAME_LENGTH = 32;
constexpr size_t CLOCK_HA_URL_LENGTH = 192;
constexpr size_t CLOCK_HA_TOKEN_LENGTH = 256;
constexpr size_t CLOCK_ENTITY_ID_LENGTH = 128;
constexpr size_t CLOCK_METRIC_NAME_LENGTH = 24;
constexpr size_t CLOCK_METRIC_SUFFIX_LENGTH = 16;
constexpr size_t CLOCK_ROOM_ICON_LENGTH = 16;
constexpr size_t CLOCK_OPEN_METEO_CITY_LENGTH = 64;
constexpr size_t CLOCK_OPEN_METEO_VALUE_LENGTH = 32;
constexpr size_t CLOCK_TMEP_EXPORT_KEY_LENGTH = 128;
constexpr size_t CLOCK_TMEP_EXPORT_ID_LENGTH = 16;
constexpr size_t CLOCK_TMEP_SENSOR_ID_LENGTH = 16;
constexpr size_t CLOCK_TMEP_FIELD_LENGTH = 16;
constexpr size_t CLOCK_TMEP_UNIT_LENGTH = 16;
constexpr size_t CLOCK_METRIC_COLOR_POINT_COUNT = 10;
// Počet hodnot na obrazovce CLOCK_STYLE_VALUES: mřížka 2 sloupce × 4 řádky
// a devátá hodnota na středu pod mřížkou, kde kruhový displej nechává volné
// místo zrcadlící okraj nad časem.
constexpr size_t CLOCK_VALUE_GRID_SLOT_COUNT = 8;
constexpr size_t CLOCK_VALUE_SLOT_COUNT = CLOCK_VALUE_GRID_SLOT_COUNT + 1;
constexpr size_t CLOCK_RSS_URL_LENGTH = 192;
// Kolik zpráv smí obrazovka kanálu ukázat. Kruhový displej pobere pět zpráv
// po dvou řádcích titulku; při šesti zbývá na titulek řádek jediný.
constexpr uint8_t CLOCK_RSS_MIN_ITEMS = 3;
constexpr uint8_t CLOCK_RSS_MAX_ITEMS = 6;
// Schema 20 is the public 1.5.5 baseline. Schema 24 added CHMI radar settings
// plus automatic clock/radar rotation. Schema 25 added the persistent UI
// language; schema 26 distinguishes an as-yet unselected language and uses
// the remaining byte for CHMI radar country availability.
// Intermediate development schemas were never released.
// Schema 27 adds optional TMEP credentials parsed from an export URL and a
// TMEP source descriptor for each of the four Open-Meteo dashboard positions.
// Schema 28 appends generic formatting and color scales for the two top Home
// Assistant values. The complete schema 27 prefix stays byte-for-byte
// unchanged so existing temperature-only configuration can be migrated safely.
// Schema 29 appends eight independent value slots for the CLOCK_STYLE_VALUES
// screen. The schema 28 prefix again stays byte-for-byte unchanged; the first
// four slots are seeded from leftSide/rightSide/metricA/metricB during
// migration, so an existing dashboard keeps showing exactly what it did.
// Schema 30 appends the RSS news screen. The schema 29 prefix stays
// byte-for-byte unchanged and the screen starts disabled, so an upgrade never
// pushes an unconfigured screen into the rotation.
// Schema 31 appends the ninth value slot drawn below the 2 x 4 grid. It sits
// after rss rather than inside slots[] so the schema 30 prefix again stays
// byte-for-byte unchanged; the slot starts disabled, so an upgrade never adds
// a value the owner did not ask for.
// Schema 33 appends the radar status line - the clock plus the outside
// temperature drawn under the screen dots - together with the Home Assistant
// entity that feeds that temperature. Open-Meteo reads temperature_2m from the
// response it already fetches, so the entity matters only for the Home
// Assistant source. The schema 32 prefix stays byte-for-byte unchanged.
// Schema 34 appends the weather forecast screen. The schema 33 prefix stays
// byte-for-byte unchanged and the screen starts disabled, so an upgrade never
// pushes another screen into the rotation.
// Schema 35 appends the aircraft radar screen ported from MeteoPlaneRadar. The
// schema 34 prefix stays byte-for-byte unchanged and the screen starts
// disabled, so an upgrade never starts polling adsb.fi on its own.
// Schema 36 appends the order the screens rotate in. The schema 35 prefix stays
// byte-for-byte unchanged and the order starts at the built-in one, so an
// upgrade keeps showing the screens exactly where they were.
constexpr uint32_t CLOCK_CONFIG_SCHEMA_VERSION = 36;

// Obrazovky, které se dají poskládat do vlastního pořadí. Nastavení mezi ně
// nepatří: v cyklu zůstává poslední, aby se z něj vždycky odcházelo stejně.
constexpr size_t CLOCK_SCREEN_ORDER_COUNT = 5;

enum ClockOrderedScreen : uint8_t {
  CLOCK_SCREEN_CLOCK = 0,
  CLOCK_SCREEN_RADAR = 1,
  CLOCK_SCREEN_RSS = 2,
  CLOCK_SCREEN_FORECAST = 3,
  CLOCK_SCREEN_PLANES = 4,
};

enum ClockLanguage : uint8_t {
  CLOCK_LANGUAGE_UNSET = 0,
  CLOCK_LANGUAGE_CZECH = 1,
  CLOCK_LANGUAGE_ENGLISH = 2,
};

enum ClockDataSource : uint8_t {
  CLOCK_DATA_SOURCE_OPEN_METEO = 0,
  CLOCK_DATA_SOURCE_HOME_ASSISTANT = 1,
};

enum ClockLocationCountry : uint8_t {
  CLOCK_LOCATION_COUNTRY_UNKNOWN = 0,
  CLOCK_LOCATION_COUNTRY_CZECHIA = 1,
  CLOCK_LOCATION_COUNTRY_OTHER = 2,
};

enum ClockSecondEffect : uint8_t {
  CLOCK_SECOND_EFFECT_DOTS = 0,
  CLOCK_SECOND_EFFECT_LINE = 1,
  CLOCK_SECOND_EFFECT_COMET = 2,
};

enum ClockTimeColonEffect : uint8_t {
  CLOCK_TIME_COLON_STEADY = 0,
  CLOCK_TIME_COLON_BLINK = 1,
  CLOCK_TIME_COLON_FADE = 2,
};

enum ClockWeatherIconStyle : uint8_t {
  CLOCK_WEATHER_ICON_STYLE_MONOCHROME = 0,
  CLOCK_WEATHER_ICON_STYLE_FLAT = 1,
  CLOCK_WEATHER_ICON_STYLE_LINE = 2,
};

enum ClockRadarSource : uint8_t {
  // ČHMÚ je jediná celostátní kompozice pro ČR; RainViewer je dlaždicová
  // služba, která pokrývá i zbytek Evropy.
  CLOCK_RADAR_SOURCE_CHMI = 0,
  CLOCK_RADAR_SOURCE_RAINVIEWER = 1,
};

enum ClockNightVisualMode : uint8_t {
  CLOCK_NIGHT_VISUAL_RED = 0,
  CLOCK_NIGHT_VISUAL_BRIGHTNESS_ONLY = 1,
};

enum ClockTimeFont : uint8_t {
  CLOCK_TIME_FONT_BARLOW = 0,
  CLOCK_TIME_FONT_LIBERATION_SANS = 1,
  CLOCK_TIME_FONT_LCD = 2,
  CLOCK_TIME_FONT_DOTO = 3,
};

enum ClockDateFormat : uint8_t {
  CLOCK_DATE_FORMAT_WEEKDAY_DAY_MONTH = 0,
  CLOCK_DATE_FORMAT_NUMERIC = 1,
  CLOCK_DATE_FORMAT_DAY_MONTH_YEAR = 2,
  CLOCK_DATE_FORMAT_WEEKDAY_DAY_MONTH_YEAR = 3,
  CLOCK_DATE_FORMAT_HIDDEN = 4,
  CLOCK_DATE_FORMAT_DAY_MONTH = 5,
};

enum ClockStyle : uint8_t {
  CLOCK_STYLE_DIGITAL = 0,
  CLOCK_STYLE_ANALOG = 1,
  // Malý čas nahoře a osm hodnot v mřížce 2 × 4 pod ním.
  CLOCK_STYLE_VALUES = 2,
};

struct ClockAppearanceConfig {
  uint8_t style = CLOCK_STYLE_DIGITAL;
  uint32_t analogToneColor = 0x00D6FF;
  uint32_t analogHandToneColor = 0x00D6FF;
  uint32_t analogCardinalAccentColor = 0xFFAB00;
  bool analogCardinalAccentsEnabled = true;
  bool analogOutlineHandsEnabled = false;
  bool analogMonochromeValuesEnabled = false;
  bool analogValuesAboveHandsEnabled = false;
  uint8_t analogDateFormat = CLOCK_DATE_FORMAT_WEEKDAY_DAY_MONTH;
  uint32_t analogDateColor = 0xB5B5B5;
  uint32_t monochromeWeatherIconColor = 0xFFFFFF;
};

struct ClockMetricConfig {
  bool custom = false;
  char preset[16] = "co2";
  char name[CLOCK_METRIC_NAME_LENGTH] = "CO₂";
  char entityId[CLOCK_ENTITY_ID_LENGTH] = "";
  char suffix[CLOCK_METRIC_SUFFIX_LENGTH] = "ppm";
  uint8_t decimals = 0;
};

struct ClockSideConfig {
  char name[CLOCK_ROOM_NAME_LENGTH] = "MÍSTNOST";
  char temperatureEntityId[CLOCK_ENTITY_ID_LENGTH] = "";
  char icon[CLOCK_ROOM_ICON_LENGTH] = "home";
  uint32_t color = 0xFFFFFF;
};

struct ClockSideValueConfig {
  bool custom = false;
  char preset[16] = "temperature";
  char suffix[CLOCK_METRIC_SUFFIX_LENGTH] = "°C";
  uint8_t decimals = 1;
};

struct ClockMetricColorPoint {
  float value = 0.0f;
  uint32_t color = 0xFFFFFF;
};

struct ClockMetricColorScale {
  uint8_t count = 1;
  ClockMetricColorPoint points[CLOCK_METRIC_COLOR_POINT_COUNT];
};

struct ClockOpenMeteoSlotConfig {
  char value[CLOCK_OPEN_METEO_VALUE_LENGTH] = "temperature_2m";
  char name[CLOCK_METRIC_NAME_LENGTH] = "TEPLOTA";
  uint32_t color = 0xFFFFFF;
};

struct ClockTmepSlotConfig {
  bool enabled = false;
  char sensorId[CLOCK_TMEP_SENSOR_ID_LENGTH] = "";
  char field[CLOCK_TMEP_FIELD_LENGTH] = "";
  char unit[CLOCK_TMEP_UNIT_LENGTH] = "";
  uint8_t decimals = 1;
};

// Jedna hodnota na obrazovce CLOCK_STYLE_VALUES. Slučuje to, co starší schéma
// drželo zvlášť v ClockSideConfig, ClockSideValueConfig a ClockMetricConfig,
// aby všech osm pozic mělo stejné možnosti: název, ikonu, jednotku i škálu.
struct ClockValueSlotConfig {
  bool enabled = false;
  bool custom = false;
  uint8_t decimals = 1;
  char preset[16] = "temperature";
  char name[CLOCK_METRIC_NAME_LENGTH] = "";
  char entityId[CLOCK_ENTITY_ID_LENGTH] = "";
  char suffix[CLOCK_METRIC_SUFFIX_LENGTH] = "°C";
  char icon[CLOCK_ROOM_ICON_LENGTH] = "none";
  uint32_t color = 0xFFFFFF;
  ClockMetricColorScale colorScale;
};

// Obrazovka se zprávami. Adresa je volitelná, takže se do rotace zapojí až
// tehdy, když ji uživatel vyplní; enabled sám o sobě nestačí.
struct ClockRssConfig {
  bool enabled = false;
  // Zapojení do automatické rotace. Nezávislé na radaru, aby šlo mít jen
  // jedno z toho. Stejně jako u radaru je střídání ve výchozím stavu vypnuté;
  // ručně otevřená obrazovka tak nezmizí dřív, než se dočte.
  bool automaticRotation = false;
  uint8_t itemCount = 5;
  uint8_t refreshMinutes = 10;
  uint16_t displaySeconds = 20;
  char url[CLOCK_RSS_URL_LENGTH] = "";
};

// Obrazovka předpovědi. Hodiny se počítají z místa, které na kruhovém displeji
// zbude, takže se nenastavují: kdo chce víc hodin, vypne kvalitu ovzduší nebo
// ubere dny. Meze tady drží web i normalizace, aby se počítalo se stejnými
// čísly jako v rozvržení obrazovky.
constexpr uint8_t CLOCK_FORECAST_MAX_DAYS = 4;

struct ClockForecastConfig {
  bool enabled = false;
  // Zapojení do automatické rotace, stejně jako u radaru a zpráv. Ve výchozím
  // stavu vypnuté, aby ručně otevřená obrazovka nezmizela dřív, než se dočte.
  bool automaticRotation = false;
  // Sekce s kvalitou ovzduší pod předpovědí. Vypnutá uvolní tři řádky, které
  // rozvržení rozdá hodinám - z šesti se tak stane devět.
  bool airQuality = true;
  uint8_t dayCount = 3;
  uint8_t refreshMinutes = 30;
  uint16_t displaySeconds = 20;
};

// Obrazovka radaru letadel. Data vozí adsb.fi, trasu vybraného letu adsb.lol.
// Převzato z projektu MeteoPlaneRadar (viz THIRD_PARTY_NOTICES.md).
//
// Dosahy jsou pevné, protože je pevná i mřížka teček pod mapou a poloměr kruhu
// se na ně přepočítává; ukládá se proto index, ne kilometry.
constexpr uint8_t CLOCK_PLANE_RANGE_COUNT = 4;
inline constexpr uint16_t CLOCK_PLANE_RANGES_KM[CLOCK_PLANE_RANGE_COUNT] = {
    10, 25, 50, 100};
// Callsign má nejvýš osm znaků, ICAO adresa sedm; šestnáct bajtů pokryje obojí
// i s rezervou, protože se hlídaný let zadává ručně.
constexpr size_t CLOCK_PLANE_CALLSIGN_LENGTH = 16;
// Nad tuhle výšku už nic nelétá, takže horní mez filtru znamená "vypnuto".
constexpr uint16_t CLOCK_PLANE_ALTITUDE_CEILING_FT = 60000;

struct ClockPlanesConfig {
  bool enabled = false;
  // Zapojení do automatické rotace, stejně jako u radaru, zpráv a předpovědi.
  bool automaticRotation = false;
  // Emergency squawky 7500/7600/7700 zvýrazní letadlo kroužkem a přeberou řádek
  // s počtem letadel. Ve výchozím stavu zapnuté - je to jediné, co obrazovka
  // hlásí sama od sebe.
  bool squawkAlert = true;
  // Letadla bez callsignu (TIS-B, MLAT, vojenské a soukromé stroje) se dají
  // schovat, protože o nich stejně není co ukázat.
  bool onlyWithCallsign = false;
  // Metry a km/h proti stopám a uzlům v detailu letadla.
  bool metricUnits = true;
  uint8_t rangeIndex = 1;
  // Nejkratší perioda dotazu. Větší dosahy si k ní přidají svoje minimum, aby
  // se z hodin nestal nezdvořilý uživatel API zdarma.
  uint8_t refreshSeconds = 5;
  // Azimut, který je nahoře na displeji - tedy směr, kterým se člověk dívá z
  // okna. Otáčí se projekce, ne displej.
  uint16_t topBearingDeg = 0;
  uint16_t displaySeconds = 20;
  // Výškový filtr ve stopách. Netýká se počtu letadel ani hlídaného letu, aby
  // nastavená výška nemohla schovat nouzový stav.
  uint16_t altitudeMinFt = 0;
  uint16_t altitudeMaxFt = CLOCK_PLANE_ALTITUDE_CEILING_FT;
  // Hlídaný let. Porovnává se s callsignem i s ICAO adresou, protože lidé
  // citují to, co zrovna mají.
  char watchCallsign[CLOCK_PLANE_CALLSIGN_LENGTH] = "";
};

struct ClockConfig {
  uint32_t schemaVersion = CLOCK_CONFIG_SCHEMA_VERSION;
  char homeAssistantUrl[CLOCK_HA_URL_LENGTH] = "";
  char homeAssistantToken[CLOCK_HA_TOKEN_LENGTH] = "";
  char weatherEntityId[CLOCK_ENTITY_ID_LENGTH] = "";
  char sunEntityId[CLOCK_ENTITY_ID_LENGTH] = "sun.sun";
  ClockSideConfig leftSide;
  ClockSideConfig rightSide;
  ClockMetricConfig metricA;
  ClockMetricConfig metricB;
  ClockMetricColorScale metricAColorScale;
  ClockMetricColorScale metricBColorScale;
  uint32_t timeColor = 0xF6F6F6;
  uint32_t dateColor = 0xB5B5B5;
  uint32_t leftWeatherIconColor = 0xFFFFFF;
  uint32_t rightWeatherIconColor = 0xFFFFFF;
  bool animatedWeatherIcons = true;
  uint8_t weatherIconStyle = CLOCK_WEATHER_ICON_STYLE_MONOCHROME;
  uint8_t dayBrightness = 35;
  uint8_t nightBrightness = 10;
  bool automaticDayNight = false;
  int8_t sunsetOffsetMinutes = 0;
  bool automaticFirmwareUpdate = false;
  bool secondRingEnabled = true;
  uint8_t secondEffect = CLOCK_SECOND_EFFECT_DOTS;
  int8_t sunriseOffsetMinutes = 0;
  uint32_t secondRingBackgroundColor = 0xFFFFFF;
  uint8_t secondRingBackgroundBrightness = 0;
  uint8_t secondRingBackgroundDotSize = 3;
  uint8_t secondDotSize = 3;
  uint32_t secondDotColor = 0xFFFFFF;
  uint8_t secondDotBrightness = 175;
  char dayNightLightEntityId[CLOCK_ENTITY_ID_LENGTH] = "";
  uint8_t nightVisualMode = CLOCK_NIGHT_VISUAL_RED;
  uint8_t timeFont = CLOCK_TIME_FONT_BARLOW;
  uint8_t dataSource = CLOCK_DATA_SOURCE_OPEN_METEO;
  char openMeteoCity[CLOCK_OPEN_METEO_CITY_LENGTH] = "Brno";
  float openMeteoLatitude = 49.1951f;
  float openMeteoLongitude = 16.6068f;
  ClockOpenMeteoSlotConfig openMeteoSlots[4];
  uint8_t timeColonEffect = CLOCK_TIME_COLON_STEADY;
  bool showLeadingHourZero = true;
  uint8_t dateFormat = CLOCK_DATE_FORMAT_WEEKDAY_DAY_MONTH;
  uint16_t radarRadiusKm = 0;
  uint8_t radarFrameCount = 6;
  bool automaticRadarRotation = false;
  uint16_t clockDisplaySeconds = 120;
  uint16_t radarDisplaySeconds = 20;
  uint8_t radarMapOpacity = 100;
  uint8_t radarPauseSeconds = 5;
  uint8_t language = CLOCK_LANGUAGE_UNSET;
  uint8_t openMeteoCountry = CLOCK_LOCATION_COUNTRY_CZECHIA;
  char tmepExportKey[CLOCK_TMEP_EXPORT_KEY_LENGTH] = "";
  char tmepExportId[CLOCK_TMEP_EXPORT_ID_LENGTH] = "";
  ClockTmepSlotConfig tmepSlots[4];
  ClockSideValueConfig leftValue;
  ClockSideValueConfig rightValue;
  ClockMetricColorScale leftValueColorScale;
  ClockMetricColorScale rightValueColorScale;
  ClockValueSlotConfig slots[CLOCK_VALUE_GRID_SLOT_COUNT];
  ClockRssConfig rss;
  // Devátý slot leží až za rss, aby schéma 30 zůstalo přesnou předponou
  // schématu 31 a migrace zůstala prostým zkopírováním bajtů. Zbytek firmwaru
  // ho vidí jako index 8 přes clockConfigValueSlot().
  ClockValueSlotConfig bottomSlot;
  // Pole schématu 32 leží až za bottomSlot, aby schéma 31 zůstalo přesnou
  // předponou a migrace byla opět jen zkopírováním bajtů.
  uint8_t radarSource = CLOCK_RADAR_SOURCE_CHMI;
  bool radarLegend = true;
  // Pole schématu 33 leží až za radarLegend, aby schéma 32 zůstalo přesnou
  // předponou a migrace byla opět jen zkopírováním bajtů.
  bool radarStatusLine = true;
  // Venkovní teplota do stavového řádku radaru. Open-Meteo si ji vezme
  // z odpovědi, kterou stahuje tak jako tak; s Home Assistantem se musí říct,
  // která entita ji nese.
  char radarStatusTemperatureEntityId[CLOCK_ENTITY_ID_LENGTH] = "";
  // Pole schématu 34 leží až za radarStatusTemperatureEntityId, aby schéma 33
  // zůstalo přesnou předponou a migrace byla opět jen zkopírováním bajtů.
  ClockForecastConfig forecast;
  // Pole schématu 35 leží až za předpovědí, aby schéma 34 zůstalo přesnou
  // předponou a migrace byla opět jen zkopírováním bajtů.
  ClockPlanesConfig planes;
  // Pole schématu 36 leží až za radarem letadel, aby schéma 35 zůstalo přesnou
  // předponou a migrace byla opět jen zkopírováním bajtů. Drží pořadí, ve
  // kterém se obrazovky střídají - hodnoty jsou ClockOrderedScreen, každá
  // právě jednou.
  uint8_t screenOrder[CLOCK_SCREEN_ORDER_COUNT] = {
      CLOCK_SCREEN_CLOCK, CLOCK_SCREEN_RADAR, CLOCK_SCREEN_RSS,
      CLOCK_SCREEN_FORECAST, CLOCK_SCREEN_PLANES};
};

static_assert(offsetof(ClockConfig, language) == 2106 &&
                  offsetof(ClockConfig, openMeteoCountry) == 2107 &&
                  offsetof(ClockConfig, tmepExportKey) == 2108 &&
                  offsetof(ClockConfig, leftValue) == 2452 &&
                  sizeof(ClockTmepSlotConfig) == 50 &&
                  sizeof(ClockSideValueConfig) == 34,
              "Schema 28 must preserve the complete schema 27 prefix.");

static_assert(offsetof(ClockConfig, slots) == 2688 &&
                  sizeof(ClockValueSlotConfig) == 292,
              "Schema 29 must preserve the complete schema 28 prefix.");

static_assert(offsetof(ClockConfig, rss) == 5024 &&
                  sizeof(ClockRssConfig) == 198,
              "Schema 30 must preserve the complete schema 29 prefix.");

static_assert(offsetof(ClockConfig, bottomSlot) == 5224,
              "Schema 31 must preserve the complete schema 30 prefix.");

static_assert(offsetof(ClockConfig, radarSource) == 5516,
              "Schema 32 must preserve the complete schema 31 prefix.");

static_assert(offsetof(ClockConfig, radarStatusLine) == 5518,
              "Schema 33 must preserve the complete schema 32 prefix.");

static_assert(offsetof(ClockConfig, forecast) == 5648 &&
                  sizeof(ClockForecastConfig) == 8,
              "Schema 34 must preserve the complete schema 33 prefix.");

static_assert(offsetof(ClockConfig, planes) == 5656 &&
                  sizeof(ClockPlanesConfig) == 32,
              "Schema 35 must preserve the complete schema 34 prefix.");

static_assert(offsetof(ClockConfig, screenOrder) == 5688,
              "Schema 36 must preserve the complete schema 35 prefix.");

// Devět slotů obrazovky HODNOTY v jedné řadě: indexy 0-7 leží v mřížce,
// index 8 je hodnota pod ní. Díky tomu smyčky nemusí řešit, že poslední slot
// je kvůli migraci uložený zvlášť.
inline ClockValueSlotConfig &clockConfigValueSlot(ClockConfig &config,
                                                 size_t index) {
  return index < CLOCK_VALUE_GRID_SLOT_COUNT ? config.slots[index]
                                             : config.bottomSlot;
}

inline const ClockValueSlotConfig &clockConfigValueSlot(
    const ClockConfig &config, size_t index) {
  return index < CLOCK_VALUE_GRID_SLOT_COUNT ? config.slots[index]
                                             : config.bottomSlot;
}

// Jedna ClockConfig má 5,7 kB. Statické kopie po modulech dohromady ukrajovaly
// přes 50 kB interní RAM, takže na TLS handshake (dva 16kB záznamové buffery)
// už nezbylo a HTTPS padalo napříč službami. Kopie proto leží v PSRAM a moduly
// si drží jen referenci; PSRAM je připravená dřív než globální konstruktory.
ClockConfig &clockConfigAllocate();

bool clockConfigBegin();
bool clockConfigLoad(ClockConfig &config);
bool clockConfigSave(const ClockConfig &config);
void clockConfigApplyDefaults(ClockConfig &config);
bool clockConfigRadarAvailable(const ClockConfig &config);
// Obrazovka zpráv se kreslí jen se zapnutým kanálem a vyplněnou adresou.
bool clockConfigRssAvailable(const ClockConfig &config);
// Předpověď stojí na Open-Meteo, takže se kreslí jen se zapnutou obrazovkou.
// Zdroj hodnot na ciferníku na tom nezáleží: souřadnice má konfigurace i tehdy,
// když hodnoty čte z Home Assistantu.
bool clockConfigForecastAvailable(const ClockConfig &config);
// Radar letadel stojí na veřejném API adsb.fi, takže stačí zapnutá obrazovka -
// souřadnice bere ze stejného místa jako meteoradar a předpověď.
bool clockConfigPlanesAvailable(const ClockConfig &config);
// Obrazovka na dané pozici v pořadí střídání. Mimo rozsah vrací ciferník,
// který je jediná obrazovka, kterou vypnout nejde.
uint8_t clockConfigScreenAt(const ClockConfig &config, uint8_t position);
// Pozice obrazovky v pořadí střídání; neznámá obrazovka končí na nule.
uint8_t clockConfigScreenPosition(const ClockConfig &config, uint8_t screen);
// Srovná pořadí zpátky na permutaci všech obrazovek. Neznámé i zdvojené
// hodnoty se zahodí a chybějící obrazovky se doplní ve výchozím pořadí, takže
// z poškozeného pole nikdy nezmizí obrazovka, na kterou se dá přepnout.
void clockConfigNormalizeScreenOrder(uint8_t *order);
bool clockAppearanceLoad(ClockAppearanceConfig &appearance,
                         uint32_t defaultMonochromeWeatherIconColor = 0xFFFFFF,
                         uint8_t defaultAnalogDateFormat =
                             CLOCK_DATE_FORMAT_WEEKDAY_DAY_MONTH,
                         uint32_t defaultAnalogDateColor = 0xB5B5B5);
bool clockAppearanceSave(const ClockAppearanceConfig &appearance);

void clockConfigCopy(char *destination, size_t destinationSize,
                     const String &value);
void clockConfigCopy(char *destination, size_t destinationSize,
                     const char *value);
