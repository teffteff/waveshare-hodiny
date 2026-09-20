#include <Arduino.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <esp_sntp.h>
#include <esp_task_wdt.h>
#include <freertos/idf_additions.h>
#include <time.h>

#include "ClockDashboard.h"
#include "ClockConfig.h"
#include "ClockNamedays.h"
#include "ChmiRadarService.h"
#include "PlaneRadarService.h"
#include "RainAlertService.h"
#include "SatelliteService.h"
#include "AgendaService.h"
#include "SchoolService.h"
#include "Astronomy.h"
#include "RssService.h"
#include "SettingsShareService.h"
#include "ConfigurationWeb.h"
#include "DayNightLogic.h"
#include "DeviceName.h"
#include "DisplayDriver.h"
#include "Display_ST7701.h"
#include "FirmwareBuild.h"
#include "FirmwareHubCa.h"
#include "FirmwareUpdateService.h"
#include "I2C_Driver.h"
#include "ImprovSerialService.h"
#include "LightningService.h"
#include "NetworkDiagnostics.h"
#include "NetworkCoordinator.h"
#include "TCA9554PWR.h"
#include "TlsMemory.h"
#include "TmepService.h"
#include "WifiOnboarding.h"
#include "WifiProvisioning.h"
#include "ScreenSchedule.h"
#include "WeatherAnimationService.h"
#include "WeatherForecastService.h"
#include "WeatherIconMapping.h"

// Zásobník úlohy loop musí unést LVGL render, webový server i TLS. Kopie
// ClockConfig se do něj od schématu 29 (přes 5 kB) nevejde, proto ji úlohy
// drží ve sdílených bufferech v .bss; viz loopConfigSnapshot().
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

#if !FIRMWARE_RELEASE && __has_include("local/secrets.h")
#include "local/secrets.h"
#define HAS_WIFI_SECRETS 1
#else
#define HAS_WIFI_SECRETS 0
#endif

#if HAS_WIFI_SECRETS && defined(HOME_ASSISTANT_URL) && \
    defined(HOME_ASSISTANT_TOKEN) && defined(HA_ENTITY_WEATHER_CODE) && \
    defined(HA_ENTITY_OUTSIDE_TEMPERATURE) && \
    defined(HA_ENTITY_ROOM_TEMPERATURE) && defined(HA_ENTITY_ROOM_CO2) && \
    defined(HA_ENTITY_ROOM_HUMIDITY) && defined(HA_ENTITY_SUN)
#define HAS_HOME_ASSISTANT_SECRETS 1
#else
#define HAS_HOME_ASSISTANT_SECRETS 0
#endif

// Adresa zpráv je nezávislá na Home Assistantu; kanál běží na vlastním serveru,
// takže se předvyplní i v sestavení bez HA sekce v .env.
#if HAS_WIFI_SECRETS && defined(NEWS_URL)
#define HAS_NEWS_SECRETS 1
#else
#define HAS_NEWS_SECRETS 0
#endif

namespace {
ClockValues sampleValues;
// Šest kopií po 5,7 kB, viz clockConfigAllocate().
ClockConfig &runtimeConfig = clockConfigAllocate();
ClockConfig &persistedConfig = clockConfigAllocate();
ClockConfig &configSaveBuffer = clockConfigAllocate();
ClockConfig &dashboardConfigBuffer = clockConfigAllocate();
ClockConfig &loopConfigBuffer = clockConfigAllocate();
ClockConfig &homeAssistantConfigBuffer = clockConfigAllocate();
ClockAppearanceConfig persistedAppearance;
ClockAppearanceConfig activeAppearance;
ClockAppearanceConfig pendingAppearance;
SemaphoreHandle_t runtimeConfigMutex = nullptr;
TaskHandle_t homeAssistantTaskHandle = nullptr;
TaskHandle_t rssTaskHandle = nullptr;
TaskHandle_t agendaTaskHandle = nullptr;
TaskHandle_t schoolTaskHandle = nullptr;
TaskHandle_t forecastTaskHandle = nullptr;
String usbCommand;
bool screenshotTransferActive = false;
unsigned long displayResyncAt = 0;
int lastDisplayedSecond = -1;
#if !FIRMWARE_RELEASE
int32_t displayTimeOffsetSeconds = 0;
#endif
bool wifiWasConnected = false;
bool timeWasSynchronized = false;
ClockValues pendingHomeAssistantValues;
portMUX_TYPE homeAssistantValuesMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool homeAssistantUpdatePending = false;
volatile bool dayNightLightRefreshRequested = false;
portMUX_TYPE dayNightLightRefreshMux = portMUX_INITIALIZER_UNLOCKED;
bool mdnsStarted = false;
// Název hodin v síti (DeviceName.h) a kdy mDNS přejde na nový. Přejmenování
// počká pár vteřin, aby stránka ještě stihla pod starou adresou načíst
// uložené nastavení.
char networkDeviceName[DEVICE_NAME_LENGTH] = "";
unsigned long mdnsRestartAt = 0;
constexpr unsigned long MDNS_RENAME_DELAY_MS = 3000;
String displayedWifiIp;
bool runtimeConfigurationApplyPending = false;
bool clockAppearanceApplyPending = false;
unsigned long runtimeConfigurationApplyAt = 0;
int lastAutomaticFirmwareCheckDate = -1;
unsigned long lastFirmwareDisplayRefreshAt = 0;
char displayedFirmwareVersion[24] = "";
bool displayedFirmwareUpdateAvailable = false;
bool firmwareDisplayInitialized = false;
#if !FIRMWARE_RELEASE
bool forceNightTestActive = false;
#endif
uint8_t currentDisplayBrightness = 35;
bool displayForcedOff = false;
volatile bool firmwareUpdateDisplayRequested = false;
volatile bool firmwareUpdateDisplayPresented = false;
volatile bool firmwareUpdateBlackRequested = false;
volatile bool firmwareUpdateBlackPresented = false;
volatile bool firmwareUpdateCountdownStarted = false;
volatile unsigned long firmwareUpdateCountdownStartedAt = 0;
uint8_t firmwareUpdateCountdownDisplayed = 0;
bool firmwareUpdateDisplayActive = false;
uint32_t displayedRadarGeneration = UINT32_MAX;
char displayedRadarTime[6] = "";
bool displayedRadarBusy = false;
uint16_t displayedRadarRadiusKm = 50;
bool radarRadiusApplyPending = false;
unsigned long radarRadiusApplyAt = 0;
bool automaticRotationPaused = true;
uint32_t displayedRssGeneration = UINT32_MAX;
uint32_t displayedAgendaGeneration = UINT32_MAX;
uint32_t displayedSchoolGeneration = UINT32_MAX;
uint32_t displayedForecastGeneration = UINT32_MAX;
uint32_t displayedPlanesGeneration = UINT32_MAX;
// Obloha s družicemi se překresluje každou vteřinu; obrazovka převezme každý
// nový snímek a mezi nimi i změnu hlášky nebo načítání.
uint32_t displayedSatellitesGeneration = UINT32_MAX;
bool displayedSatellitesLoading = false;
bool displayedSatellitesDetailOpen = false;
char displayedSatellitesMessage[64] = "";
// Detail se překresluje mimo generaci snímku: klepnutí na letadlo mění panel,
// ne mapu pod ním, takže by se jinak ukázal až s dalším stažením.
bool displayedPlanesDetailOpen = false;
PlaneRouteState displayedPlanesRouteState = PlaneRouteState::Pending;
char displayedPlanesDetailHex[8] = "";
// Hláška se porovnává zvlášť: chyba, po které se vůbec nekreslilo, generaci
// snímku neposune, a obrazovka by o ní jinak nikdy nedala vědět.
char displayedPlanesMessage[64] = "";
// Dosah se porovnává zvlášť, aby se popisek po přetažení prstem přepsal hned.
// Nový snímek přijde až po stažení, které na pomalé síti trvá i vteřiny, a do
// té doby by pod prstem svítil starý údaj.
uint16_t displayedPlanesRangeKm = 0;
// Stejně tak stahování: první snímek s kruhy je na displeji dřív, než začne
// první stažení, a řádek nad ním se musí přepnout na "Načítám letadla..."
// i bez nového snímku.
bool displayedPlanesLoading = false;
unsigned long displayModeStartedAt = 0;
bool radarRotationWaitingForCycle = false;
// Plán obrazovek. Obrazovka, kterou právě drží okno plánu, nebo
// CLOCK_SCREEN_ORDER_UNUSED mimo okno. Dokud ji drží, automatické střídání
// stojí; ruční přepnutí platí, ale po SCHEDULE_RETURN_MS bez dalšího se
// hodiny k obrazovce plánu vrátí.
uint8_t scheduleHeldScreen = CLOCK_SCREEN_ORDER_UNUSED;
// Upozornění na déšť. Do kdy drží radar, nebo 0 mimo upozornění. Drží se
// stejně jako okno plánu, jen má přednost: je to výjimka na pár minut, kdežto
// plán platí celé okno.
unsigned long rainAlertHeldUntil = 0;
unsigned long rainAlertRaisedAt = 0;
unsigned long rainAlertCheckedAt = 0;
bool rainAlertEverRaised = false;
bool scheduleCheckPending = true;
unsigned long scheduleCheckedAt = 0;
unsigned long lastManualScreenChangeAt = 0;
bool startupScreenShown = false;
uint32_t radarRotationCycleAtTimeout = 0;
bool radarRedNightModeApplied = false;

constexpr uint32_t LOOP_WATCHDOG_TIMEOUT_MS = 20UL * 1000UL;
constexpr uint32_t NTP_SYNC_INTERVAL_MS = 60UL * 60UL * 1000UL;
constexpr uint32_t HOME_ASSISTANT_REFRESH_MS = 60UL * 1000UL;
constexpr uint32_t HOME_ASSISTANT_RETRY_MS = 5UL * 1000UL;
constexpr uint32_t HOME_ASSISTANT_CONNECT_TIMEOUT_MS = 5000;
constexpr uint32_t HOME_ASSISTANT_RESPONSE_TIMEOUT_MS = 8000;
constexpr uint8_t HOME_ASSISTANT_REQUEST_ATTEMPTS = 2;
constexpr uint32_t HOME_ASSISTANT_REQUEST_RETRY_DELAY_MS = 250;
// Pauza mezi entitami, ve které smyčka nedrží zámek sítě.
constexpr uint32_t HOME_ASSISTANT_ENTITY_YIELD_MS = 20;
constexpr uint32_t OPEN_METEO_REFRESH_MS = 10UL * 60UL * 1000UL;
constexpr uint32_t TMEP_REFRESH_MS = 60UL * 1000UL;
constexpr uint32_t EXTERNAL_DATA_RETRY_MS = 60UL * 1000UL;
// Kanál zpráv se po chybě zkouší dřív než v nastaveném intervalu, ale ne tak
// často, aby při trvale nedostupném serveru zatěžoval síť.
constexpr uint32_t RSS_RETRY_MS = 2UL * 60UL * 1000UL;
constexpr uint32_t AGENDA_RETRY_MS = 2UL * 60UL * 1000UL;
// Otevření obrazovky zpráv stáhne kanál znovu, jsou-li titulky starší. Stejná
// mez jako u radaru: bez ní by rotace i gesto listovaly zprávami staré tolik,
// kolik je nastavený interval, s ní zase kratší mez znamená stahování při
// každém průletu rotace.
constexpr uint32_t RSS_VISIBILITY_REFRESH_MS = 5UL * 60UL * 1000UL;
// Server agendu přepočítává po čtvrthodině, takže mladší data otevření
// obrazovky obnovovat nemusí - stejně by přišla stejná odpověď.
constexpr uint32_t AGENDA_VISIBILITY_REFRESH_MS = 10UL * 60UL * 1000UL;
// Škola: server se Školy OnLine ptá po dvaceti minutách, takže mladší data
// otevření obrazovky neobnovuje.
constexpr uint32_t SCHOOL_RETRY_MS = 2UL * 60UL * 1000UL;
constexpr uint32_t SCHOOL_VISIBILITY_REFRESH_MS = 10UL * 60UL * 1000UL;
// Předpověď se mění po hodinách, takže otevření obrazovky nemá cenu
// stahovat znovu dřív než po čtvrthodině.
constexpr uint32_t FORECAST_VISIBILITY_REFRESH_MS = 15UL * 60UL * 1000UL;
constexpr uint32_t FORECAST_RETRY_MS = 2UL * 60UL * 1000UL;
constexpr time_t VALID_TIME_THRESHOLD = 1700000000;

const char *CZECH_WEEKDAYS[] = {
    "NEDĚLE", "PONDĚLÍ", "ÚTERÝ", "STŘEDA",
    "ČTVRTEK", "PÁTEK", "SOBOTA",
};
const char *CZECH_MONTHS[] = {
    "LEDNA", "ÚNORA", "BŘEZNA", "DUBNA", "KVĚTNA", "ČERVNA",
    "ČERVENCE", "SRPNA", "ZÁŘÍ", "ŘÍJNA", "LISTOPADU", "PROSINCE",
};
const char *ENGLISH_WEEKDAYS[] = {
    "SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY",
    "THURSDAY", "FRIDAY", "SATURDAY",
};
const char *ENGLISH_MONTHS[] = {
    "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE",
    "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER",
};

void applyDevelopmentDefaults(ClockConfig &config);

void copyRuntimeConfig(ClockConfig &destination) {
  if (runtimeConfigMutex == nullptr) {
    destination = runtimeConfig;
    return;
  }
  xSemaphoreTake(runtimeConfigMutex, portMAX_DELAY);
  destination = runtimeConfig;
  xSemaphoreGive(runtimeConfigMutex);
}

// Zkouška kanálu z webu. Ověření proti svazku kořenů Mozilly potřebuje víc
// zásobníku, než má celá smyčka (16 kB, a leží v ní i LVGL a web server),
// takže se stahování předává úloze kanálu, která na to má vyměřených 20 kB.
// Web server je jednovláknový, takže stačí jediná žádost.
struct AgendaProbeRequest {
  ClockAgendaConfig config;
  ClockAgendaCalendarsConfig calendars;
  int httpStatus = 0;
  bool ok = false;
  char error[AGENDA_MESSAGE_LENGTH] = "";
};
AgendaProbeRequest agendaProbeRequest;
volatile bool agendaProbePending = false;
volatile bool agendaProbeDone = false;
constexpr uint32_t AGENDA_PROBE_TIMEOUT_MS = 30UL * 1000UL;

// Zkouška adresy rozvrhu z webu. Stejně jako u agendy ji provede úloha školy,
// která má zásobník na ověření proti svazku kořenů Mozilly.
struct SchoolProbeRequest {
  ClockSchoolConfig config;
  int httpStatus = 0;
  bool ok = false;
  char error[SCHOOL_MESSAGE_LENGTH] = "";
};
SchoolProbeRequest schoolProbeRequest;
volatile bool schoolProbePending = false;
volatile bool schoolProbeDone = false;
constexpr uint32_t SCHOOL_PROBE_TIMEOUT_MS = 30UL * 1000UL;

// Přenos zálohy nastavení na server a zpět. Potřebuje stejné ověření proti
// svazku kořenů Mozilly jako agenda, takže ho obstará úloha agendy; web server
// jen předá žádost a počká. Web server je jednovláknový, takže stačí jediná.
SettingsShareRequest settingsShareRequest;
SettingsShareResult settingsShareResult;
volatile bool settingsSharePending = false;
volatile bool settingsShareDone = false;
// Nad součtem stropů přenosu: 10 s čekání na síť, 5 s spojení, 8 s odpověď.
constexpr uint32_t SETTINGS_SHARE_TIMEOUT_MS = 30UL * 1000UL;

struct RssProbeRequest {
  ClockRssConfig config;
  RssLocation location;
  int httpStatus = 0;
  bool ok = false;
  char error[RSS_MESSAGE_LENGTH] = "";
};
RssProbeRequest rssProbeRequest;
volatile bool rssProbePending = false;
volatile bool rssProbeDone = false;
// Nad součtem vlastních stropů stahování: 10 s čekání na síť, 5 s spojení,
// 8 s odpověď. Kratší mez by hlásila chybu kanálu, který se ještě stahuje.
constexpr uint32_t RSS_PROBE_TIMEOUT_MS = 30UL * 1000UL;

// Úloha agendy si stejně jako kanál zpráv nebere celou ClockConfig, aby
// nepotřebovala další pětikilobajtový buffer ani ho neměla na zásobníku vedle
// TLS.
void copyRuntimeAgendaConfig(ClockAgendaConfig &destination,
                             ClockAgendaCalendarsConfig &calendars) {
  if (runtimeConfigMutex == nullptr) {
    destination = runtimeConfig.agenda;
    calendars = runtimeConfig.agendaCalendars;
    return;
  }
  xSemaphoreTake(runtimeConfigMutex, portMAX_DELAY);
  destination = runtimeConfig.agenda;
  calendars = runtimeConfig.agendaCalendars;
  xSemaphoreGive(runtimeConfigMutex);
}

void copyRuntimeSchoolConfig(ClockSchoolConfig &destination) {
  if (runtimeConfigMutex != nullptr)
    xSemaphoreTake(runtimeConfigMutex, portMAX_DELAY);
  destination = runtimeConfig.school;
  if (runtimeConfigMutex != nullptr) xSemaphoreGive(runtimeConfigMutex);
}

// Úloha kanálu zpráv si nebere celou ClockConfig, aby nepotřebovala další
// pětikilobajtový buffer ani ho neměla na zásobníku vedle TLS.
// Poloha jde s ní, protože adresa kanálu si ji může vyžádat značkami
// {city}, {lat} a {lon}.
void copyRuntimeRssConfig(ClockRssConfig *destination, RssLocation &location) {
  if (runtimeConfigMutex != nullptr)
    xSemaphoreTake(runtimeConfigMutex, portMAX_DELAY);
  if (destination != nullptr) *destination = runtimeConfig.rss;
  strlcpy(location.city, runtimeConfig.openMeteoCity, sizeof(location.city));
  location.latitude = runtimeConfig.openMeteoLatitude;
  location.longitude = runtimeConfig.openMeteoLongitude;
  if (runtimeConfigMutex != nullptr) xSemaphoreGive(runtimeConfigMutex);
}

// Úloha předpovědi si bere jen to, na čem stojí její stahování: souřadnice,
// interval a přepínač kvality ovzduší. Celá ClockConfig by na jejím zásobníku
// ležela vedle TLS relace.
struct ForecastTaskConfig {
  ClockForecastConfig forecast;
  float latitude = 0.0f;
  float longitude = 0.0f;
};

void copyRuntimeForecastConfig(ForecastTaskConfig &destination) {
  if (runtimeConfigMutex != nullptr)
    xSemaphoreTake(runtimeConfigMutex, portMAX_DELAY);
  destination.forecast = runtimeConfig.forecast;
  destination.latitude = runtimeConfig.openMeteoLatitude;
  destination.longitude = runtimeConfig.openMeteoLongitude;
  if (runtimeConfigMutex != nullptr) xSemaphoreGive(runtimeConfigMutex);
}

// Od schématu 29 má ClockConfig přes 5 kB. Vracet ho hodnotou znamenalo kopii
// na zásobníku v každém volajícím; jen samotné loop() si tak alokovalo 10 kB
// ze 16 kB zásobníku úlohy a na LVGL ani na uložení nastavení už nezbývalo.
// Úloha loop je jednovláknová, proto všechna její volání sdílí jediný buffer.
// Vrácená reference platí do dalšího volání ze stejné úlohy a smí se měnit -
// je to pracovní kopie, ne sdílený stav.
ClockConfig &loopConfigSnapshot() {
  copyRuntimeConfig(loopConfigBuffer);
  return loopConfigBuffer;
}

// Úloha home-assistant běží souběžně s loop, proto má vlastní buffer.
const ClockConfig &homeAssistantConfigSnapshot() {
  copyRuntimeConfig(homeAssistantConfigBuffer);
  return homeAssistantConfigBuffer;
}

void loadRuntimeConfigForWeb(ClockConfig &config) {
  copyRuntimeConfig(config);
}

bool saveRuntimeConfig(const ClockConfig &config, bool tokenWasSubmitted) {
  configSaveBuffer = config;
  if (!tokenWasSubmitted) {
    clockConfigCopy(configSaveBuffer.homeAssistantToken,
                    sizeof(configSaveBuffer.homeAssistantToken),
                    persistedConfig.homeAssistantToken);
  }
  if (!clockConfigSave(configSaveBuffer)) return false;
  radarRadiusApplyPending = false;
  xSemaphoreTake(runtimeConfigMutex, portMAX_DELAY);
  persistedConfig = configSaveBuffer;
  runtimeConfig = configSaveBuffer;
  applyDevelopmentDefaults(runtimeConfig);
  xSemaphoreGive(runtimeConfigMutex);
  runtimeConfigurationApplyPending = true;
  runtimeConfigurationApplyAt = millis() + 250;
  if (homeAssistantTaskHandle != nullptr) xTaskNotifyGive(homeAssistantTaskHandle);
  // Bez tohoto by se změna adresy nebo zapnutí kanálu projevily až po
  // doběhnutí nastaveného intervalu, tedy klidně za dvě hodiny.
  if (rssTaskHandle != nullptr) xTaskNotifyGive(rssTaskHandle);
  if (agendaTaskHandle != nullptr) xTaskNotifyGive(agendaTaskHandle);
  if (schoolTaskHandle != nullptr) xTaskNotifyGive(schoolTaskHandle);
  if (forecastTaskHandle != nullptr) xTaskNotifyGive(forecastTaskHandle);
  return true;
}

void loadClockAppearanceForWeb(ClockAppearanceConfig &saved,
                               ClockAppearanceConfig &active) {
  saved = persistedAppearance;
  active = activeAppearance;
}

bool previewClockAppearanceFromWeb(const ClockAppearanceConfig &appearance) {
  activeAppearance = appearance;
  activeAppearance.style = constrain(
      activeAppearance.style, static_cast<uint8_t>(CLOCK_STYLE_DIGITAL),
      static_cast<uint8_t>(CLOCK_STYLE_VALUES));
  activeAppearance.analogToneColor &= 0xFFFFFF;
  activeAppearance.analogHandToneColor &= 0xFFFFFF;
  activeAppearance.analogCardinalAccentColor &= 0xFFFFFF;
  activeAppearance.analogDateFormat = constrain(
      activeAppearance.analogDateFormat,
      static_cast<uint8_t>(CLOCK_DATE_FORMAT_WEEKDAY_DAY_MONTH),
      static_cast<uint8_t>(CLOCK_DATE_FORMAT_DAY_MONTH));
  activeAppearance.analogDateColor &= 0xFFFFFF;
  activeAppearance.monochromeWeatherIconColor &= 0xFFFFFF;
  pendingAppearance = activeAppearance;
  clockAppearanceApplyPending = true;
  return true;
}

bool saveClockAppearanceFromWeb(const ClockAppearanceConfig &appearance) {
  ClockAppearanceConfig normalized = appearance;
  normalized.style = constrain(
      normalized.style, static_cast<uint8_t>(CLOCK_STYLE_DIGITAL),
      static_cast<uint8_t>(CLOCK_STYLE_VALUES));
  normalized.analogToneColor &= 0xFFFFFF;
  normalized.analogHandToneColor &= 0xFFFFFF;
  normalized.analogCardinalAccentColor &= 0xFFFFFF;
  normalized.analogDateFormat = constrain(
      normalized.analogDateFormat,
      static_cast<uint8_t>(CLOCK_DATE_FORMAT_WEEKDAY_DAY_MONTH),
      static_cast<uint8_t>(CLOCK_DATE_FORMAT_DAY_MONTH));
  normalized.analogDateColor &= 0xFFFFFF;
  normalized.monochromeWeatherIconColor &= 0xFFFFFF;
  if (!clockAppearanceSave(normalized)) return false;
  persistedAppearance = normalized;
  activeAppearance = normalized;
  pendingAppearance = normalized;
  clockAppearanceApplyPending = true;
  return true;
}

void applyPendingClockAppearance() {
  if (!clockAppearanceApplyPending) return;
  clockAppearanceApplyPending = false;
  clockDashboardApplyAppearance(pendingAppearance);
}

// Radar letadel se musí dozvědět o každé změně nastavení - dosah, azimut,
// filtr výšky i hlídaný let mění to, na co se serveru ptáme a co se kreslí.
// Zapnuté střídání navíc drží stahování i se schovanou obrazovkou, jinak by
// rotace obrazovku přeskakovala pořád dokola: čeká na první snímek, který by
// se bez stahování nikdy nevykreslil.
void applyPlaneRadarState(const ClockConfig &config, bool visible) {
  const bool planesAvailable = clockConfigPlanesAvailable(config);
  planeRadarServiceSetActive(
      planesAvailable && visible,
      planesAvailable && config.planes.automaticRotation,
      config.openMeteoLatitude, config.openMeteoLongitude, config.planes,
      config.planesFeedUrl, config.planesMapLabel);
}

void applyPlaneRadarState(const ClockConfig &config) {
  applyPlaneRadarState(config, clockDashboardPlanesVisible());
}

// Družice stahují jen viditelné, nebo řídce ve střídání, aby měla rotace
// s čím obrazovku otevřít.
void applySatellitesState(const ClockConfig &config, bool visible) {
  const bool available = clockConfigSatellitesAvailable(config);
  satelliteServiceSetActive(available && visible,
                            available && config.satellites.automaticRotation,
                            config.openMeteoLatitude, config.openMeteoLongitude,
                            config.satellites,
                            config.language == CLOCK_LANGUAGE_ENGLISH);
}

void applySatellitesState(const ClockConfig &config) {
  applySatellitesState(config, clockDashboardSatellitesVisible());
}

// Upozornění na déšť se ptá serveru bez ohledu na to, která obrazovka je právě
// vidět: jeho smysl je přepnout na radar ve chvíli, kdy se na hodiny nikdo
// nedívá. Radar vypnutý v pořadí obrazovek ale nemá kam přepnout, takže se
// v tom případě nestahuje nic.
void applyRainAlertState(const ClockConfig &config) {
  const bool available =
      config.rainAlert.enabled && clockConfigRadarAvailable(config);
  rainAlertServiceSetActive(available, config.rainAlert.url,
                            config.openMeteoLatitude,
                            config.openMeteoLongitude,
                            config.rainAlert.radiusKm,
                            config.rainAlert.refreshMinutes);
}

void applyPendingRuntimeConfiguration() {
  if (!runtimeConfigurationApplyPending ||
      static_cast<long>(millis() - runtimeConfigurationApplyAt) < 0) {
    return;
  }
  runtimeConfigurationApplyPending = false;
  runtimeConfigurationApplyAt = 0;
  automaticRotationPaused = true;
  radarRotationWaitingForCycle = false;
  xSemaphoreTake(runtimeConfigMutex, portMAX_DELAY);
  dashboardConfigBuffer = runtimeConfig;
  xSemaphoreGive(runtimeConfigMutex);
  clockDashboardApplyConfiguration(dashboardConfigBuffer);
  const bool radarAvailable =
      clockConfigRadarAvailable(dashboardConfigBuffer);
  chmiRadarServiceSetActive(
      radarAvailable && clockDashboardRadarVisible(),
      radarAvailable && dashboardConfigBuffer.automaticRadarRotation,
      dashboardConfigBuffer.openMeteoLatitude,
      dashboardConfigBuffer.openMeteoLongitude,
      dashboardConfigBuffer.radarRadiusKm,
      dashboardConfigBuffer.radarFrameCount,
      dashboardConfigBuffer.radarMapOpacity,
      dashboardConfigBuffer.radarPauseSeconds,
      dashboardConfigBuffer.radarLegend,
      dashboardConfigBuffer.radarPrecipitation,
      dashboardConfigBuffer.radarSource);
  applyPlaneRadarState(dashboardConfigBuffer);
  applySatellitesState(dashboardConfigBuffer);
  applyRainAlertState(dashboardConfigBuffer);
  // Nový plán nebo vypnutá obrazovka platí hned, ne až při další kontrole.
  scheduleCheckPending = true;
  // Zápis do flash může na ESP32-S3 rozhodit vertikální synchronizaci RGB
  // panelu. Provádíme ji až po dokončení obsluhy HTTP požadavku.
  LCD_Resync();
  displayResyncAt = millis() + 750;
}

void applyDevelopmentDefaults(ClockConfig &config) {
#if HAS_HOME_ASSISTANT_SECRETS && defined(WAVESHARE_DEVELOPMENT_BUILD)
  if (config.homeAssistantUrl[0] == '\0') {
    clockConfigCopy(config.homeAssistantUrl, sizeof(config.homeAssistantUrl),
                    HOME_ASSISTANT_URL);
  }
  if (config.homeAssistantToken[0] == '\0') {
    clockConfigCopy(config.homeAssistantToken,
                    sizeof(config.homeAssistantToken), HOME_ASSISTANT_TOKEN);
  }
  if (config.weatherEntityId[0] == '\0') {
    clockConfigCopy(config.weatherEntityId, sizeof(config.weatherEntityId),
                    HA_ENTITY_WEATHER_CODE);
  }
  if (config.leftSide.temperatureEntityId[0] == '\0') {
    clockConfigCopy(config.leftSide.temperatureEntityId,
                    sizeof(config.leftSide.temperatureEntityId),
                    HA_ENTITY_OUTSIDE_TEMPERATURE);
  }
  if (config.rightSide.temperatureEntityId[0] == '\0') {
    clockConfigCopy(config.rightSide.temperatureEntityId,
                    sizeof(config.rightSide.temperatureEntityId),
                    HA_ENTITY_ROOM_TEMPERATURE);
  }
  if (config.metricA.entityId[0] == '\0') {
    clockConfigCopy(config.metricA.entityId, sizeof(config.metricA.entityId),
                    HA_ENTITY_ROOM_CO2);
  }
  if (config.metricB.entityId[0] == '\0') {
    clockConfigCopy(config.metricB.entityId, sizeof(config.metricB.entityId),
                    HA_ENTITY_ROOM_HUMIDITY);
  }
  if (config.sunEntityId[0] == '\0') {
    clockConfigCopy(config.sunEntityId, sizeof(config.sunEntityId), HA_ENTITY_SUN);
  }
#endif
#if HAS_NEWS_SECRETS && defined(WAVESHARE_DEVELOPMENT_BUILD)
  // Jen prázdné pole, stejně jako u ostatních výchozích hodnot: ručně zadanou
  // adresu tím nepřepíšeme. Obrazovku samotnou nezapínáme, to je volba uživatele.
  if (config.rss.url[0] == '\0') {
    clockConfigCopy(config.rss.url, sizeof(config.rss.url), NEWS_URL);
  }
#endif
}

void handleBrightnessPreview(uint8_t brightness) {
  currentDisplayBrightness = constrain(brightness, 1, 100);
  Set_Backlight(displayForcedOff ? 0 : currentDisplayBrightness);
}

void handleDisplayPower(bool forcedOff) {
  if (displayForcedOff == forcedOff) return;
  displayForcedOff = forcedOff;
  if (displayForcedOff) {
    Set_Backlight(0);
    LCD_Sleep();
    return;
  }
  LCD_Wake();
  displayDriverRefresh();
  Set_Backlight(currentDisplayBrightness);
}

bool displayPowerForcedOff() {
  return displayForcedOff;
}

void handleSettingsOpen() {
  configurationWebEnsureActive();
  clockDashboardSetWebMode(configurationWebMode());
  clockDashboardSetWebPasswordConfigured(configurationWebPasswordConfigured());
}

// Kanál zpráv se stahuje na pozadí i se zavřenou obrazovkou, takže otevření
// nic nezapíná; jen zkrátí čekání, když jsou zprávy v mezipaměti staré.
void handleAgendaVisibility(bool visible) {
  if (!visible || agendaTaskHandle == nullptr) return;
  const ClockConfig &config = loopConfigSnapshot();
  if (!clockConfigAgendaAvailable(config)) return;
  AgendaStatus status;
  // Zamčená mezipaměť: raději nestahovat než stahovat naslepo. Otevření
  // obrazovky se zopakuje, jakmile se na ni přepne příště.
  if (!agendaServiceStatus(status)) return;
  if (status.ready && status.lastSuccessAvailable &&
      status.lastSuccessAgeMs < AGENDA_VISIBILITY_REFRESH_MS) {
    return;
  }
  // Notifikace v agendaTask nuluje deadline, takže se stahuje hned.
  xTaskNotifyGive(agendaTaskHandle);
}

void handleSchoolVisibility(bool visible) {
  if (!visible || schoolTaskHandle == nullptr) return;
  const ClockConfig &config = loopConfigSnapshot();
  if (!clockConfigSchoolAvailable(config)) return;
  SchoolStatus status;
  if (!schoolServiceStatus(status) || status.loading) return;
  if (status.ready && status.lastSuccessAvailable &&
      status.lastSuccessAgeMs < SCHOOL_VISIBILITY_REFRESH_MS) {
    return;
  }
  xTaskNotifyGive(schoolTaskHandle);
}

void handleRssVisibility(bool visible) {
  if (!visible || rssTaskHandle == nullptr) return;
  const ClockConfig &config = loopConfigSnapshot();
  if (!clockConfigRssAvailable(config)) return;
  RssStatus status;
  // Zamčená mezipaměť znamená "nevím", ne "nic tu není". Bez téhle podmínky
  // by se při každém takovém otevření stahovalo znovu, i kdyby byly zprávy
  // čerstvé.
  if (!rssServiceStatus(status)) return;
  if (status.loading) return;
  if (status.lastSuccessAvailable &&
      status.lastSuccessAgeMs < RSS_VISIBILITY_REFRESH_MS) {
    return;
  }
  // Notifikace v rssTask nuluje deadline, takže se stahuje hned.
  xTaskNotifyGive(rssTaskHandle);
}

// Přijme zkoušku kanálu z web serveru a počká na úlohu kanálu, která ji
// provede. Čeká se po malých krocích a mezi nimi se krmí watchdog smyčky:
// stahování smí trvat přes dvacet sekund, což je jeho mez.
bool runAgendaProbeFromWeb(const ClockAgendaConfig &config,
                           const ClockAgendaCalendarsConfig &calendars,
                           int &httpStatus, String &error) {
  error = "";
  if (agendaTaskHandle == nullptr) {
    error = F("Úloha agendy neběží.");
    return false;
  }
  if (agendaProbePending) {
    error = F("Zkouška už probíhá.");
    return false;
  }
  agendaProbeRequest.config = config;
  agendaProbeRequest.calendars = calendars;
  agendaProbeRequest.httpStatus = 0;
  agendaProbeRequest.ok = false;
  agendaProbeRequest.error[0] = '\0';
  agendaProbeDone = false;
  agendaProbePending = true;
  xTaskNotifyGive(agendaTaskHandle);

  const unsigned long deadline = millis() + AGENDA_PROBE_TIMEOUT_MS;
  while (!agendaProbeDone) {
    if (static_cast<long>(millis() - deadline) >= 0) {
      // Žádost zůstává rozpracovaná; agendaProbePending pustí další zkoušku,
      // až ta současná doběhne.
      error = F("Zkouška se nedokončila včas.");
      return false;
    }
    delay(20);
  }
  httpStatus = agendaProbeRequest.httpStatus;
  if (!agendaProbeRequest.ok) error = agendaProbeRequest.error;
  return agendaProbeRequest.ok;
}

// Přijme přenos zálohy z web serveru a počká na úlohu agendy. Stejně jako u
// zkoušky kanálu se mezitím kreslí displej a krmí watchdog smyčky.
bool runSettingsShareFromWeb(const SettingsShareRequest &request,
                             SettingsShareResult &result) {
  result = SettingsShareResult{};
  if (agendaTaskHandle == nullptr) {
    strlcpy(result.error, "Úloha pro přenos zálohy neběží.",
            sizeof(result.error));
    return false;
  }
  if (settingsSharePending || agendaProbePending) {
    strlcpy(result.error, "Jiný přenos ještě probíhá.", sizeof(result.error));
    return false;
  }
  settingsShareRequest = request;
  settingsShareDone = false;
  settingsSharePending = true;
  xTaskNotifyGive(agendaTaskHandle);

  const unsigned long deadline = millis() + SETTINGS_SHARE_TIMEOUT_MS;
  while (!settingsShareDone) {
    if (static_cast<long>(millis() - deadline) >= 0) {
      // Žádost zůstává rozpracovaná; settingsSharePending pustí další přenos,
      // až úloha agendy tenhle dokončí.
      strlcpy(result.error, "Server neodpověděl včas.", sizeof(result.error));
      return false;
    }
    if (!screenshotTransferActive) {
      clockDashboardLoop();
      displayDriverLoop();
    }
    delay(5);
    feedLoopWDT();
  }
  result = settingsShareResult;
  return result.ok;
}

// Odvození klíče šifrované zálohy trvá sekundy. Přímo ve web serveru by po
// tu dobu stál displej, proto ho počítá vlastní úloha a smyčka mezitím kreslí
// a krmí watchdog, stejně jako při přenosu zálohy. Úloha vznikne při první
// záloze a pak jen čeká; zásobník leží v PSRAM.
TaskHandle_t backgroundWorkTaskHandle = nullptr;
void (*backgroundWork)(void *) = nullptr;
void *backgroundWorkContext = nullptr;
volatile bool backgroundWorkDone = false;

void backgroundWorkTask(void *) {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (backgroundWork != nullptr) backgroundWork(backgroundWorkContext);
    backgroundWorkDone = true;
  }
}

// Volá se jen ze smyčky (web server), takže dvě práce naráz nepřijdou. Čeká
// bez limitu: práce je omezená (SETTINGS_BACKUP_KDF_MAX_ITERATIONS) a její
// kontext leží na zásobníku volajícího, takže odejít dřív by nešlo.
bool runBackgroundWorkFromWeb(void (*work)(void *), void *context) {
  if (backgroundWorkTaskHandle == nullptr &&
      xTaskCreatePinnedToCoreWithCaps(
          backgroundWorkTask, "web-work", 6144, nullptr, 1,
          &backgroundWorkTaskHandle, 0,
          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    backgroundWorkTaskHandle = nullptr;
    return false;
  }
  backgroundWork = work;
  backgroundWorkContext = context;
  backgroundWorkDone = false;
  xTaskNotifyGive(backgroundWorkTaskHandle);
  while (!backgroundWorkDone) {
    if (!screenshotTransferActive) {
      clockDashboardLoop();
      displayDriverLoop();
    }
    delay(5);
    feedLoopWDT();
  }
  return true;
}

bool runSchoolProbeFromWeb(const ClockSchoolConfig &config, int &httpStatus,
                           String &error) {
  httpStatus = 0;
  if (schoolTaskHandle == nullptr) {
    error = F("Úloha rozvrhu neběží.");
    return false;
  }
  if (schoolProbePending) {
    error = F("Zkouška rozvrhu už probíhá.");
    return false;
  }
  schoolProbeRequest.config = config;
  schoolProbeRequest.httpStatus = 0;
  schoolProbeRequest.ok = false;
  schoolProbeRequest.error[0] = '\0';
  schoolProbeDone = false;
  schoolProbePending = true;
  xTaskNotifyGive(schoolTaskHandle);

  const unsigned long deadline = millis() + SCHOOL_PROBE_TIMEOUT_MS;
  while (!schoolProbeDone) {
    if (static_cast<long>(millis() - deadline) >= 0) {
      error = F("Zkouška rozvrhu se nedočkala odpovědi.");
      return false;
    }
    // Stejně jako u zkoušky kanálu: displej nesmí zamrznout a watchdog
    // smyčky musí dostat najíst.
    if (!screenshotTransferActive) {
      clockDashboardLoop();
      displayDriverLoop();
    }
    delay(5);
    feedLoopWDT();
  }
  httpStatus = schoolProbeRequest.httpStatus;
  if (!schoolProbeRequest.ok) error = schoolProbeRequest.error;
  return schoolProbeRequest.ok;
}

// Zkouška adresy serveru družic z webu. Provede ji úloha družic; web server
// mezitím nesmí zamrznout displej ani nechat hladovět watchdog smyčky.
constexpr uint32_t SATELLITES_PROBE_TIMEOUT_MS = 30UL * 1000UL;

bool runSatellitesProbeFromWeb(const ClockSatellitesConfig &config,
                               SatelliteProbeResult &result) {
  result = SatelliteProbeResult{};
  const ClockConfig &current = loopConfigSnapshot();
  if (!satelliteServiceStartProbe(config, current.openMeteoLatitude,
                                  current.openMeteoLongitude)) {
    strlcpy(result.error, "Zkouška družic už probíhá nebo úloha neběží.",
            sizeof(result.error));
    return false;
  }
  const unsigned long deadline = millis() + SATELLITES_PROBE_TIMEOUT_MS;
  while (!satelliteServiceProbeResult(result)) {
    if (static_cast<long>(millis() - deadline) >= 0) {
      satelliteServiceAbandonProbe();
      result = SatelliteProbeResult{};
      strlcpy(result.error, "Zkouška družic se nedočkala odpovědi.",
              sizeof(result.error));
      return false;
    }
    if (!screenshotTransferActive) {
      clockDashboardLoop();
      displayDriverLoop();
    }
    delay(5);
    feedLoopWDT();
  }
  return result.ok;
}

bool runRssProbeFromWeb(const ClockRssConfig &config, int &httpStatus,
                        String &error) {
  httpStatus = 0;
  if (rssTaskHandle == nullptr) {
    error = F("Úloha kanálu zpráv neběží.");
    return false;
  }
  if (rssProbePending) {
    error = F("Zkouška kanálu už probíhá.");
    return false;
  }
  rssProbeRequest.config = config;
  // Zkouška bere uloženou polohu, ne tu z formuláře: kanál pak na hodinách
  // ukáže totéž, co zkouška.
  copyRuntimeRssConfig(nullptr, rssProbeRequest.location);
  rssProbeRequest.httpStatus = 0;
  rssProbeRequest.ok = false;
  rssProbeRequest.error[0] = '\0';
  rssProbeDone = false;
  rssProbePending = true;
  xTaskNotifyGive(rssTaskHandle);

  const unsigned long deadline = millis() + RSS_PROBE_TIMEOUT_MS;
  while (!rssProbeDone) {
    if (static_cast<long>(millis() - deadline) >= 0) {
      // Žádost zůstává rozpracovaná; rssProbePending pustí další zkoušku, až
      // ji úloha kanálu dokončí, takže se požadavky nepřekryjí.
      error = F("Zkouška kanálu se nedočkala odpovědi.");
      return false;
    }
    // Zkouška smí trvat desítky sekund, ale hodiny na stole mezitím nesmí
    // zamrznout. LVGL běží v téhle úloze, takže se stačí prokousat jeho
    // časovači stejně, jako to dělá smyčka; gesta se schválně neobsluhují,
    // aby se během čekání nepřepínaly obrazovky.
    if (!screenshotTransferActive) {
      clockDashboardLoop();
      displayDriverLoop();
    }
    delay(5);
    feedLoopWDT();
  }
  httpStatus = rssProbeRequest.httpStatus;
  if (!rssProbeRequest.ok) error = rssProbeRequest.error;
  return rssProbeRequest.ok;
}

// Předpověď se stahuje na pozadí i se zavřenou obrazovkou, takže otevření nic
// nezapíná; jen zkrátí čekání, když jsou data v mezipaměti stará.
void handleForecastVisibility(bool visible) {
  displayModeStartedAt = millis();
  automaticRotationPaused = false;
  if (!visible || forecastTaskHandle == nullptr) return;
  const ClockConfig &config = loopConfigSnapshot();
  if (!clockConfigForecastAvailable(config)) return;
  WeatherForecastStatus status;
  weatherForecastServiceStatus(status);
  if (status.loading) return;
  if (status.lastSuccessAvailable &&
      status.lastSuccessAgeMs < FORECAST_VISIBILITY_REFRESH_MS) {
    return;
  }
  // Notifikace ve forecastTask nuluje deadline, takže se stahuje hned.
  xTaskNotifyGive(forecastTaskHandle);
}

void handleRadarVisibility(bool visible) {
  displayModeStartedAt = millis();
  automaticRotationPaused = false;
  radarRotationWaitingForCycle = false;
  const ClockConfig &config = loopConfigSnapshot();
  const bool radarAvailable = clockConfigRadarAvailable(config);
  chmiRadarServiceSetActive(radarAvailable && visible,
                            radarAvailable && config.automaticRadarRotation,
                            config.openMeteoLatitude,
                            config.openMeteoLongitude, config.radarRadiusKm,
                            config.radarFrameCount, config.radarMapOpacity,
                            config.radarPauseSeconds, config.radarLegend,
                            config.radarPrecipitation, config.radarSource);
}

// Radar letadel stahuje jen když je vidět, nebo když si ho majitel pustil do
// rotace - adsb.fi je API zdarma a hodiny z něj nemají tahat data pořád.
void handlePlanesVisibility(bool visible) {
  displayModeStartedAt = millis();
  automaticRotationPaused = false;
  if (visible) {
    // Odchod z obrazovky canvas schová a odkrýt ho umí jen předání snímku.
    // Beze změny dat by se ale žádné nekonalo - třeba když jsou hodiny offline -
    // a mapa by po návratu zůstala černá, i když hotový snímek pořád leží
    // v paměti. Vynulovaná generace vynutí jedno předání hned.
    displayedPlanesGeneration = UINT32_MAX;
  }
  // Bere se předaná hodnota, ne to, co zrovna ukazuje activeScreen: při
  // aktualizaci firmwaru přijde false ještě dřív, než se obrazovka přepne, a
  // odvozením by radar dál stahoval přes celé odpočítávání OTA.
  applyPlaneRadarState(loopConfigSnapshot(), visible);
}

void handleSatellitesVisibility(bool visible) {
  displayModeStartedAt = millis();
  automaticRotationPaused = false;
  // Canvas se při odchodu schová; vynulovaná generace ho po návratu odkryje
  // i tehdy, když se mezitím nic nepřekreslilo.
  if (visible) displayedSatellitesGeneration = UINT32_MAX;
  applySatellitesState(loopConfigSnapshot(), visible);
}

void handleRadarRangeChange(int8_t direction) {
  static constexpr uint16_t RADAR_RADII[] = {25, 50, 100, 200, 0};
  const ClockConfig &config = loopConfigSnapshot();
  if (!clockConfigRadarAvailable(config)) return;
  // Poslední rozsah je "celá ČR" s pevným středem Česka; mimo něj řada končí
  // u 200 km, aby swipe nepřehodil obraz na cizí zemi.
  const size_t rangeCount =
      clockConfigWholeCountryRangeAvailable(config) ? 5 : 4;
  size_t index = 1;
  for (size_t candidate = 0; candidate < rangeCount; ++candidate) {
    if (RADAR_RADII[candidate] == config.radarRadiusKm) {
      index = candidate;
      break;
    }
  }
  if (direction > 0 && index + 1 < rangeCount)
    ++index;
  else if (direction < 0 && index > 0)
    --index;
  else
    return;
  xSemaphoreTake(runtimeConfigMutex, portMAX_DELAY);
  runtimeConfig.radarRadiusKm = RADAR_RADII[index];
  xSemaphoreGive(runtimeConfigMutex);
  radarRadiusApplyPending = true;
  radarRadiusApplyAt = millis() + 350;
  displayModeStartedAt = millis();
  radarRotationWaitingForCycle = false;
}

void maintainRadarRangeChange() {
  if (radarRadiusApplyPending &&
      static_cast<long>(millis() - radarRadiusApplyAt) >= 0) {
    radarRadiusApplyPending = false;
    radarRadiusApplyAt = 0;
    const ClockConfig &config = loopConfigSnapshot();
    if (clockConfigRadarAvailable(config) &&
        (clockDashboardRadarVisible() || config.automaticRadarRotation)) {
      chmiRadarServiceSetActive(clockDashboardRadarVisible(),
                                config.automaticRadarRotation,
                                config.openMeteoLatitude,
                                config.openMeteoLongitude,
                                config.radarRadiusKm,
                                config.radarFrameCount,
                                config.radarMapOpacity,
                                config.radarPauseSeconds,
                                config.radarLegend,
                                config.radarPrecipitation, config.radarSource);
    }
  }
}

void loadRadarRangeStateForWeb(uint16_t &savedRadiusKm,
                               uint16_t &activeRadiusKm) {
  savedRadiusKm = persistedConfig.radarRadiusKm;
  activeRadiusKm = loopConfigSnapshot().radarRadiusKm;
}

bool previewRadarRangeFromWeb(uint16_t radiusKm) {
  xSemaphoreTake(runtimeConfigMutex, portMAX_DELAY);
  if (!clockConfigRadarAvailable(runtimeConfig)) {
    xSemaphoreGive(runtimeConfigMutex);
    return false;
  }
  runtimeConfig.radarRadiusKm = radiusKm;
  xSemaphoreGive(runtimeConfigMutex);
  // Kopie hodnotou by si na zásobníku loop vzala přes 8 kB z šestnácti;
  // viz loopConfigSnapshot().
  const ClockConfig &config = loopConfigSnapshot();
  radarRadiusApplyPending = false;
  radarRadiusApplyAt = 0;
  displayModeStartedAt = millis();
  radarRotationWaitingForCycle = false;
  if (clockDashboardRadarVisible() || config.automaticRadarRotation) {
    chmiRadarServiceSetActive(clockDashboardRadarVisible(),
                              config.automaticRadarRotation,
                              config.openMeteoLatitude,
                              config.openMeteoLongitude,
                              config.radarRadiusKm,
                              config.radarFrameCount,
                              config.radarMapOpacity,
                              config.radarPauseSeconds,
                              config.radarLegend, config.radarPrecipitation,
                              config.radarSource);
  }
  return true;
}

// Ciferník, radar, zprávy, předpověď a nastavení se střídají na jednom místě.
// Podržení prstu prochází celý tento cyklus, automatická rotace jen jeho
// datovou část; nedostupná obrazovka se přeskočí. Čísla obrazovek odpovídají
// ClockOrderedScreen, takže se jimi dá indexovat pořadí z konfigurace.
constexpr uint8_t ROTATION_SCREEN_CLOCK = CLOCK_SCREEN_CLOCK;
constexpr uint8_t ROTATION_SCREEN_RADAR = CLOCK_SCREEN_RADAR;
constexpr uint8_t ROTATION_SCREEN_RSS = CLOCK_SCREEN_RSS;
constexpr uint8_t ROTATION_SCREEN_FORECAST = CLOCK_SCREEN_FORECAST;
constexpr uint8_t ROTATION_SCREEN_PLANES = CLOCK_SCREEN_PLANES;
constexpr uint8_t ROTATION_SCREEN_AGENDA = CLOCK_SCREEN_AGENDA;
constexpr uint8_t ROTATION_SCREEN_SKY = CLOCK_SCREEN_SKY;
constexpr uint8_t ROTATION_SCREEN_SCHOOL = CLOCK_SCREEN_SCHOOL;
constexpr uint8_t ROTATION_SCREEN_SATELLITES = CLOCK_SCREEN_SATELLITES;
constexpr uint8_t ROTATION_SCREEN_SETTINGS = CLOCK_SCREEN_ORDER_COUNT;
constexpr uint8_t ROTATION_SCREEN_COUNT = CLOCK_SCREEN_ORDER_COUNT + 1;

// Pořadí obrazovek v cyklu si skládá majitel na webu; nastavení v něm zůstává
// poslední, aby se z něj odcházelo vždycky stejným směrem. Cyklus se proto
// prochází po pozicích, ne po číslech obrazovek.
uint8_t rotationScreenAtPosition(const ClockConfig &config, uint8_t position) {
  if (position >= CLOCK_SCREEN_ORDER_COUNT) return ROTATION_SCREEN_SETTINGS;
  return clockConfigScreenAt(config, position);
}

uint8_t rotationScreenPosition(const ClockConfig &config, uint8_t screen) {
  if (screen == ROTATION_SCREEN_SETTINGS) return CLOCK_SCREEN_ORDER_COUNT;
  return clockConfigScreenPosition(config, screen);
}

uint8_t activeRotationScreen() {
  // Nastavení je překryv nad ostatními stránkami, takže rozhoduje první.
  if (clockDashboardSettingsVisible()) return ROTATION_SCREEN_SETTINGS;
  if (clockDashboardRadarVisible()) return ROTATION_SCREEN_RADAR;
  if (clockDashboardRssVisible()) return ROTATION_SCREEN_RSS;
  if (clockDashboardForecastVisible()) return ROTATION_SCREEN_FORECAST;
  if (clockDashboardPlanesVisible()) return ROTATION_SCREEN_PLANES;
  if (clockDashboardAgendaVisible()) return ROTATION_SCREEN_AGENDA;
  if (clockDashboardSkyVisible()) return ROTATION_SCREEN_SKY;
  if (clockDashboardSchoolVisible()) return ROTATION_SCREEN_SCHOOL;
  if (clockDashboardSatellitesVisible()) return ROTATION_SCREEN_SATELLITES;
  return ROTATION_SCREEN_CLOCK;
}

void showRotationScreen(uint8_t screen) {
  if (screen == ROTATION_SCREEN_SETTINGS) {
    clockDashboardShowSettings();
    return;
  }
  // Pod otevřeným nastavením se stránka přepnout nedá, nejdřív ho zavřeme.
  clockDashboardCloseSettings();
  switch (screen) {
    case ROTATION_SCREEN_RADAR:
      clockDashboardSetRadarVisible(true);
      break;
    case ROTATION_SCREEN_RSS:
      clockDashboardSetRssVisible(true);
      break;
    case ROTATION_SCREEN_FORECAST:
      clockDashboardSetForecastVisible(true);
      break;
    case ROTATION_SCREEN_PLANES:
      clockDashboardSetPlanesVisible(true);
      break;
    case ROTATION_SCREEN_AGENDA:
      clockDashboardSetAgendaVisible(true);
      break;
    case ROTATION_SCREEN_SKY:
      clockDashboardSetSkyVisible(true);
      break;
    case ROTATION_SCREEN_SCHOOL:
      clockDashboardSetSchoolVisible(true);
      break;
    case ROTATION_SCREEN_SATELLITES:
      clockDashboardSetSatellitesVisible(true);
      break;
    default:
      // Všechny překryvné stránky se skrývají stejnou cestou zpět na ciferník.
      clockDashboardSetRadarVisible(false);
      clockDashboardSetRssVisible(false);
      clockDashboardSetForecastVisible(false);
      clockDashboardSetPlanesVisible(false);
      clockDashboardSetAgendaVisible(false);
      clockDashboardSetSkyVisible(false);
      clockDashboardSetSchoolVisible(false);
      clockDashboardSetSatellitesVisible(false);
      break;
  }
}

// Obrazovka je v cyklu ručního gesta, tedy nastavená a použitelná. Nastavení
// vypnout nejde; jinak by hodiny bez radaru i zpráv neměly cestu k webu.
// Drží upozornění na déšť právě teď radar?
bool rainAlertHolding() {
  return rainAlertHeldUntil != 0 &&
         static_cast<long>(millis() - rainAlertHeldUntil) < 0;
}

// Obrazovka, kterou něco drží, nebo CLOCK_SCREEN_ORDER_UNUSED. Upozornění na
// déšť přebíjí okno plánu: trvá jen pár minut a pak obrazovku zase pustí.
uint8_t heldScreen() {
  return rainAlertHolding() ? static_cast<uint8_t>(CLOCK_SCREEN_RADAR)
                            : scheduleHeldScreen;
}

bool rotationScreenAvailable(const ClockConfig &config, uint8_t screen) {
  switch (screen) {
    case ROTATION_SCREEN_RADAR: return clockConfigRadarAvailable(config);
    case ROTATION_SCREEN_RSS: return clockConfigRssAvailable(config);
    case ROTATION_SCREEN_FORECAST:
      return clockConfigForecastAvailable(config);
    case ROTATION_SCREEN_PLANES:
      return clockConfigPlanesAvailable(config);
    case ROTATION_SCREEN_AGENDA:
      return clockConfigAgendaAvailable(config);
    case ROTATION_SCREEN_SKY:
      return clockConfigSkyAvailable(config);
    case ROTATION_SCREEN_SCHOOL:
      return clockConfigSchoolAvailable(config);
    case ROTATION_SCREEN_SATELLITES:
      return clockConfigSatellitesAvailable(config);
    default: return true;
  }
}

// Obrazovka se navíc účastní automatické rotace. Nastavení do ní nepatří:
// rozečtenou stránku nesmí čas odklikat pryč.
bool rotationScreenEnabled(const ClockConfig &config, uint8_t screen) {
  switch (screen) {
    case ROTATION_SCREEN_RADAR:
      return clockConfigRadarAvailable(config) && config.automaticRadarRotation;
    case ROTATION_SCREEN_RSS:
      return clockConfigRssAvailable(config) && config.rss.automaticRotation;
    case ROTATION_SCREEN_FORECAST:
      return clockConfigForecastAvailable(config) &&
             config.forecast.automaticRotation;
    case ROTATION_SCREEN_PLANES:
      return clockConfigPlanesAvailable(config) &&
             config.planes.automaticRotation;
    case ROTATION_SCREEN_AGENDA:
      return clockConfigAgendaAvailable(config) &&
             config.agenda.automaticRotation;
    case ROTATION_SCREEN_SKY:
      return clockConfigSkyAvailable(config) && config.sky.automaticRotation;
    case ROTATION_SCREEN_SCHOOL:
      return clockConfigSchoolAvailable(config) &&
             config.school.automaticRotation;
    case ROTATION_SCREEN_SATELLITES:
      return clockConfigSatellitesAvailable(config) &&
             config.satellites.automaticRotation;
    case ROTATION_SCREEN_SETTINGS:
      return false;
    default: return true;
  }
}

// Automatická rotace nesmí obrazovku otevřít, dokud nemá co ukázat. Ruční
// gesto zůstává neblokované a přepne kdykoliv.
bool rotationScreenReady(const ClockConfig &config, uint8_t screen) {
  if (screen == ROTATION_SCREEN_RADAR) {
    ChmiRadarSnapshot snapshot;
    chmiRadarServiceSnapshot(snapshot);
    if (!snapshot.ready || snapshot.loading ||
        snapshot.fullPreparationInProgress)
      return false;
    if (snapshot.mapOnly) return true;
    // RainViewer nabídne jen tolik snímků, kolik jich zrovna má - bývá jich
    // kolem třinácti. Trvat na přesném počtu by radar do rotace nikdy nepustil.
    if (snapshot.rainViewerSource) return snapshot.animationFrameCount > 0;
    return snapshot.animationFrameCount == config.radarFrameCount;
  }
  if (screen == ROTATION_SCREEN_RSS) {
    RssStatus status;
    // Zamčená mezipaměť neznamená prázdný kanál; rotace to zkusí za chvíli
    // znovu, místo aby obrazovku přeskočila jako nepřipravenou.
    if (!rssServiceStatus(status)) return false;
    return status.ready && status.count > 0;
  }
  if (screen == ROTATION_SCREEN_FORECAST) {
    WeatherForecastStatus status;
    weatherForecastServiceStatus(status);
    return status.ready && status.hourCount > 0;
  }
  if (screen == ROTATION_SCREEN_AGENDA) {
    AgendaStatus status;
    // Zamčená mezipaměť neznamená prázdnou agendu; rotace to zkusí za chvíli
    // znovu, místo aby obrazovku přeskočila jako nepřipravenou.
    if (!agendaServiceStatus(status)) return false;
    // Prázdný den je platný výsledek, ale rotovat na obrazovku, která řekne
    // jen "nic nemáš", nemá cenu - ruční gesto na ni pustí pořád.
    return status.ready && status.count > 0;
  }
  if (screen == ROTATION_SCREEN_SCHOOL) {
    SchoolStatus status;
    // Zamčená mezipaměť neznamená prázdný rozvrh; zkusí se za chvíli znovu.
    if (!schoolServiceStatus(status)) return false;
    // O prázdninách bez úkolů a obědů by obrazovka řekla jen "žádné
    // vyučování"; ruční gesto na ni pustí pořád.
    return status.ready && (status.lessonCount > 0 || status.homeworkCount > 0 ||
                            status.mealCount > 0);
  }
  if (screen == ROTATION_SCREEN_SATELLITES) {
    // Dráhy z řídkého stahování na pozadí musí pokrývat tuhle chvíli; prázdná
    // obloha nad nastavenou výškou je přitom platný stav.
    return satelliteServiceHasCurrentData();
  }
  if (screen == ROTATION_SCREEN_PLANES) {
    // Prázdná obloha je platný stav, takže se čeká jen na první vykreslený
    // snímek - ne na to, až nějaké letadlo přiletí.
    PlaneRadarSnapshot snapshot;
    planeRadarServiceSnapshot(snapshot);
    return snapshot.ready && snapshot.pixels != nullptr;
  }
  return true;
}

unsigned long rotationDurationMs(const ClockConfig &config, uint8_t screen) {
  switch (screen) {
    case ROTATION_SCREEN_RADAR:
      return static_cast<unsigned long>(config.radarDisplaySeconds) * 1000UL;
    case ROTATION_SCREEN_RSS:
      return static_cast<unsigned long>(config.rss.displaySeconds) * 1000UL;
    case ROTATION_SCREEN_FORECAST:
      return static_cast<unsigned long>(config.forecast.displaySeconds) *
             1000UL;
    case ROTATION_SCREEN_PLANES:
      return static_cast<unsigned long>(config.planes.displaySeconds) * 1000UL;
    case ROTATION_SCREEN_AGENDA:
      return static_cast<unsigned long>(config.agenda.displaySeconds) * 1000UL;
    case ROTATION_SCREEN_SKY:
      return static_cast<unsigned long>(config.sky.displaySeconds) * 1000UL;
    case ROTATION_SCREEN_SCHOOL:
      return static_cast<unsigned long>(config.school.displaySeconds) * 1000UL;
    case ROTATION_SCREEN_SATELLITES:
      return static_cast<unsigned long>(config.satellites.displaySeconds) *
             1000UL;
    default:
      return static_cast<unsigned long>(config.clockDisplaySeconds) * 1000UL;
  }
}

void maintainAutomaticScreenRotation() {
  const ClockConfig &config = loopConfigSnapshot();
  const bool anyRotation =
      rotationScreenEnabled(config, ROTATION_SCREEN_RADAR) ||
      rotationScreenEnabled(config, ROTATION_SCREEN_RSS) ||
      rotationScreenEnabled(config, ROTATION_SCREEN_FORECAST) ||
      rotationScreenEnabled(config, ROTATION_SCREEN_PLANES) ||
      rotationScreenEnabled(config, ROTATION_SCREEN_AGENDA) ||
      rotationScreenEnabled(config, ROTATION_SCREEN_SKY) ||
      rotationScreenEnabled(config, ROTATION_SCREEN_SCHOOL) ||
      rotationScreenEnabled(config, ROTATION_SCREEN_SATELLITES);
  // Okno plánu i upozornění na déšť drží svou obrazovku; střídání se rozběhne
  // až po nich.
  const bool allowed = anyRotation && WiFi.status() == WL_CONNECTED &&
                       timeWasSynchronized && !displayForcedOff &&
                       clockDashboardAutomaticRotationAllowed() &&
                       heldScreen() == CLOCK_SCREEN_ORDER_UNUSED;
  if (!allowed) {
    automaticRotationPaused = true;
    radarRotationWaitingForCycle = false;
    return;
  }
  const unsigned long now = millis();
  if (automaticRotationPaused) {
    automaticRotationPaused = false;
    displayModeStartedAt = now;
    radarRotationWaitingForCycle = false;
    return;
  }
  const uint8_t current = activeRotationScreen();
  // Obrazovka mimo střídání sama nezmizí. Bez toho by ručně otevřený radar
  // zavřela zapnutá rotace zpráv a naopak, přestože je jeho vlastní rotace
  // vypnutá.
  if (!rotationScreenEnabled(config, current)) return;
  if (now - displayModeStartedAt < rotationDurationMs(config, current)) return;

  if (current == ROTATION_SCREEN_RADAR) {
    ChmiRadarSnapshot snapshot;
    chmiRadarServiceSnapshot(snapshot);
    const bool staticRadarReady =
        snapshot.ready && snapshot.animationFrameCount <= 1;
    if (!staticRadarReady) {
      if (!radarRotationWaitingForCycle) {
        // Nastavený čas je pouze minimum. Od této chvíle čekáme na dokončení
        // právě rozběhnutého cyklu včetně koncové pauzy.
        radarRotationWaitingForCycle = true;
        radarRotationCycleAtTimeout = snapshot.completedAnimationCycles;
        return;
      }
      if (snapshot.completedAnimationCycles == radarRotationCycleAtTimeout)
        return;
    }
  }

  const uint8_t currentPosition = rotationScreenPosition(config, current);
  for (uint8_t step = 1; step < ROTATION_SCREEN_COUNT; ++step) {
    const uint8_t candidate = rotationScreenAtPosition(
        config,
        static_cast<uint8_t>((currentPosition + step) % ROTATION_SCREEN_COUNT));
    if (!rotationScreenEnabled(config, candidate)) continue;
    if (!rotationScreenReady(config, candidate)) continue;
    radarRotationWaitingForCycle = false;
    showRotationScreen(candidate);
    displayModeStartedAt = millis();
    return;
  }
  // Žádná další obrazovka není připravená; zkusíme to v dalším průchodu.
}

void maintainDisplayGestures() {
  const ClockConfig &config = loopConfigSnapshot();
  const bool radarAvailable = clockConfigRadarAvailable(config);
  // Podržení prstu prochází cyklus obrazovek. Vlevo od svislé osy zpět,
  // vpravo vpřed; nastavení je v cyklu poslední a odchází se z něj stejně.
  const int8_t holdDirection = displayDriverTakeScreenHold();
  if (holdDirection != 0 && clockDashboardManualScreenChangeAllowed()) {
    const uint8_t current =
        rotationScreenPosition(config, activeRotationScreen());
    for (uint8_t step = 1; step < ROTATION_SCREEN_COUNT; ++step) {
      const uint8_t candidate = rotationScreenAtPosition(
          config, static_cast<uint8_t>(
                      (current + ROTATION_SCREEN_COUNT + holdDirection * step) %
                      ROTATION_SCREEN_COUNT));
      if (!rotationScreenAvailable(config, candidate)) continue;
      showRotationScreen(candidate);
      displayModeStartedAt = millis();
      radarRotationWaitingForCycle = false;
      lastManualScreenChangeAt = millis();
      break;
    }
  }
  const int8_t rangeSwipeDirection = displayDriverTakeRangeSwipe();
  if (rangeSwipeDirection != 0 && clockDashboardAutomaticRotationAllowed()) {
    if (clockDashboardSwipeValues() || clockDashboardSwipeSchool()) {
      displayModeStartedAt = millis();
    } else if (radarAvailable && clockDashboardRadarVisible()) {
      handleRadarRangeChange(rangeSwipeDirection);
    } else if (clockDashboardPlanesVisible()) {
      planeRadarServiceChangeRange(rangeSwipeDirection);
      displayModeStartedAt = millis();
    }
  }
  int16_t tapX = 0;
  int16_t tapY = 0;
  // Jedno klepnutí vybírá letadlo na radaru nebo zavírá jeho detail; jinde
  // nedělá nic. Denní režim přepíná až dvojklepnutí, protože jedno klepnutí se
  // pletlo s podržením prstu a místo obrazovky přepínalo den a noc.
  if (displayDriverTakeShortTap(tapX, tapY))
    clockDashboardHandleSingleTap(tapX, tapY);
  if (displayDriverTakeDoubleTap()) clockDashboardHandleDoubleTap();
}

// Přepne na obrazovku, pokud je zapnutá. Nastavení na displeji zavře; když
// běží aktualizace firmwaru, nepřepne nic.
bool switchToScreen(const ClockConfig &config, uint8_t screen) {
  if (screen >= CLOCK_SCREEN_ORDER_COUNT ||
      !rotationScreenAvailable(config, screen) ||
      !clockDashboardManualScreenChangeAllowed())
    return false;
  showRotationScreen(screen);
  displayModeStartedAt = millis();
  radarRotationWaitingForCycle = false;
  return true;
}

// Přepnutí z webu nebo z ovládacího API se počítá jako ruční: plán se k své
// obrazovce vrátí za pět minut, stejně jako po přetažení prstem.
bool showScreenFromWeb(uint8_t screen) {
  if (!switchToScreen(loopConfigSnapshot(), screen)) return false;
  lastManualScreenChangeAt = millis();
  return true;
}

uint8_t currentScreenForWeb() {
  const uint8_t screen = activeRotationScreen();
  return screen == ROTATION_SCREEN_SETTINGS ? CLOCK_SCREEN_ORDER_COUNT : screen;
}

constexpr unsigned long SCHEDULE_CHECK_MS = 30UL * 1000UL;
constexpr unsigned long SCHEDULE_RETURN_MS = 5UL * 60UL * 1000UL;

// Výchozí obrazovka po startu a plán obrazovek. Plán se počítá z času
// a polohy, takže se po restartu uprostřed okna chytí sám; kontroluje se
// jednou za půl minuty a hned po změně nastavení.
void maintainScreenSchedule() {
  const ClockConfig &config = loopConfigSnapshot();
  const unsigned long now = millis();
  if (!startupScreenShown) {
    // Hned po startu, ještě bez času ze sítě: plán se chytí, jakmile čas
    // dorazí, a výchozí obrazovku případně vystřídá.
    startupScreenShown = true;
    const uint8_t startup = config.screenSchedule.startupScreen;
    if (startup != CLOCK_SCREEN_CLOCK) switchToScreen(config, startup);
  }
  if (!timeWasSynchronized) return;

  if (scheduleCheckPending || now - scheduleCheckedAt >= SCHEDULE_CHECK_MS) {
    scheduleCheckPending = false;
    scheduleCheckedAt = now;
    const int rule = screenScheduleActiveRule(
        config.screenSchedule, static_cast<int64_t>(time(nullptr)),
        config.openMeteoLatitude, config.openMeteoLongitude);
    uint8_t screen = rule >= 0 ? config.screenSchedule.rules[rule].screen
                               : CLOCK_SCREEN_ORDER_UNUSED;
    // Pravidlo pro vypnutou obrazovku nic nedrží, jinak by stálo i střídání.
    if (screen != CLOCK_SCREEN_ORDER_UNUSED &&
        !rotationScreenAvailable(config, screen))
      screen = CLOCK_SCREEN_ORDER_UNUSED;
    if (screen != scheduleHeldScreen) {
      const bool ended = scheduleHeldScreen != CLOCK_SCREEN_ORDER_UNUSED &&
                         screen == CLOCK_SCREEN_ORDER_UNUSED;
      scheduleHeldScreen = screen;
      if (!clockDashboardSettingsVisible()) {
        if (screen != CLOCK_SCREEN_ORDER_UNUSED) {
          switchToScreen(config, screen);
        } else if (ended) {
          // Konec okna vrací výchozí obrazovku a střídání jede od ní dál.
          if (!switchToScreen(config, config.screenSchedule.startupScreen))
            switchToScreen(config, CLOCK_SCREEN_CLOCK);
        }
      }
    }
  }

  // Ručně odbočené hodiny se k obrazovce plánu vrátí po pěti minutách.
  // Otevřené nastavení na displeji se nezavírá: kdo v něm je, něco dělá.
  // Dokud drží upozornění na déšť, plán svou obrazovku nevynucuje - jinak by
  // se ty dvě přetahovaly o displej každou půlminutu.
  if (scheduleHeldScreen != CLOCK_SCREEN_ORDER_UNUSED && !rainAlertHolding() &&
      !clockDashboardSettingsVisible() &&
      activeRotationScreen() != scheduleHeldScreen &&
      now - lastManualScreenChangeAt >= SCHEDULE_RETURN_MS) {
    switchToScreen(config, scheduleHeldScreen);
  }
}

constexpr unsigned long RAIN_ALERT_CHECK_MS = 10UL * 1000UL;

// Upozornění na déšť: když se do nastaveného horizontu blíží srážky, přepne se
// na radar a chvíli se na něm podrží. Předpověď vozí vlastní server, rozhodnutí
// padá tady - stejně jako u blesků, kde server vozí údery a poplach vyhlašuje
// ciferník.
void maintainRainAlert() {
  const ClockConfig &config = loopConfigSnapshot();
  const unsigned long now = millis();
  const bool enabled =
      config.rainAlert.enabled && clockConfigRadarAvailable(config);

  if (!enabled) {
    if (rainAlertHeldUntil != 0) {
      rainAlertHeldUntil = 0;
      clockDashboardSetRainAlertNote("");
      scheduleCheckPending = true;
    }
    return;
  }

  // Konec držení vrací obrazovku střídání nebo plánu. Střídání začne počítat
  // od nuly, aby radar nezmizel v tomtéž průchodu.
  if (rainAlertHeldUntil != 0 &&
      static_cast<long>(now - rainAlertHeldUntil) >= 0) {
    rainAlertHeldUntil = 0;
    clockDashboardSetRainAlertNote("");
    displayModeStartedAt = now;
    scheduleCheckPending = true;
  }

  if (now - rainAlertCheckedAt < RAIN_ALERT_CHECK_MS) return;
  rainAlertCheckedAt = now;

  RainAlertStatus status;
  rainAlertServiceStatus(status);
  // Bez čerstvé předpovědi se nevyhlašuje nic. Stará by přepnula na déšť,
  // který už dávno přešel.
  if (!status.ready) return;

  RainAlertDecision decision;
  if (!rainAlertEvaluate(status.forecast, config.rainAlert.minimumDbz,
                         config.rainAlert.horizonMinutes, decision))
    return;

  // Prodleva mezi dvěma spuštěními, aby jedna fronta nepřepínala pořád dokola.
  if (rainAlertEverRaised &&
      now - rainAlertRaisedAt <
          static_cast<unsigned long>(config.rainAlert.cooldownMinutes) * 60000UL)
    return;
  // V noci se nepřepíná, když si to majitel přeje. Noční režim je tu měřítkem:
  // je to tentýž přepínač, podle kterého displej ztmavne.
  if (config.rainAlert.quietAtNight && clockDashboardNightModeEnabled()) return;
  // Otevřené nastavení na displeji se nezavírá: kdo v něm je, něco dělá.
  if (clockDashboardSettingsVisible()) return;
  if (!switchToScreen(config, CLOCK_SCREEN_RADAR)) return;

  rainAlertHeldUntil =
      now + static_cast<unsigned long>(config.rainAlert.holdMinutes) * 60000UL;
  rainAlertRaisedAt = now;
  rainAlertEverRaised = true;
  char note[24];
  snprintf(note, sizeof(note),
           config.language == CLOCK_LANGUAGE_ENGLISH ? "Rain in %u min"
                                                     : "Déšť za %u min",
           static_cast<unsigned>(decision.minutesAway));
  clockDashboardSetRainAlertNote(note);
}

void pushRssItemToDashboard(size_t index, const RssDisplayItem &item, void *) {
  clockDashboardSetRssItem(index, item.title, item.time);
}

void pushAgendaItemToDashboard(size_t index, const AgendaDisplayItem &item,
                               void *) {
  clockDashboardSetAgendaItem(index, item.day, item.time, item.title,
                              item.calendar);
}

// Předá obrazovce novou předpověď, jakmile ji služba stáhne. Generace se mění
// i po neúspěšném pokusu, takže se hláška o nedostupnosti dostane na displej
// stejnou cestou jako data.
void maintainForecastDisplay() {
  WeatherForecastStatus status;
  weatherForecastServiceStatus(status);
  if (status.generation == displayedForecastGeneration) return;
  if (status.ready) {
    static WeatherForecastData forecast;
    if (!weatherForecastServiceSnapshot(forecast)) return;
    clockDashboardSetForecast(forecast);
  } else {
    clockDashboardSetForecastFailed(status.failed);
  }
  displayedForecastGeneration = status.generation;
}

void maintainRssDisplay() {
  RssStatus status;
  // Bez téhle podmínky by zamčená mezipaměť vypadala jako prázdný kanál a
  // obrazovka by na jeden průchod zhasla a ukázala "Načítám zprávy…".
  if (!rssServiceStatus(status)) return;
  if (status.generation == displayedRssGeneration) return;
  clockDashboardSetRssStatus(status.message,
                             static_cast<uint8_t>(status.count));
  // Když je mezipaměť právě zamčená stahováním, generaci si nezapíšeme a
  // řádky doplníme při dalším průchodu.
  if (status.count > 0 &&
      !rssServiceVisitItems(pushRssItemToDashboard, nullptr)) {
    return;
  }
  displayedRssGeneration = status.generation;
}

void maintainAgendaDisplay() {
  AgendaStatus status;
  // Bez téhle podmínky by zamčená mezipaměť vypadala jako prázdná agenda a
  // obrazovka by na jeden průchod zhasla a ukázala "Načítám agendu…".
  if (!agendaServiceStatus(status)) return;
  if (status.generation == displayedAgendaGeneration) return;
  // Legenda musí stát dřív než řádky: bez jmen by se po prvním stažení
  // nakreslila prázdná a doplnila se až při dalším.
  const char *calendarNames[AGENDA_MAX_CALENDARS];
  for (size_t index = 0; index < status.calendarCount; ++index) {
    calendarNames[index] = status.calendars[index];
  }
  clockDashboardSetAgendaCalendars(calendarNames, status.calendarCount);
  clockDashboardSetAgendaStatus(
      status.message, static_cast<uint8_t>(status.count), status.ready);
  // Když je mezipaměť právě zamčená stahováním, generaci si nezapíšeme a
  // řádky doplníme při dalším průchodu.
  if (status.count > 0 &&
      !agendaServiceVisitItems(pushAgendaItemToDashboard, nullptr)) {
    return;
  }
  displayedAgendaGeneration = status.generation;
}

void pushSchoolToDashboard(const SchoolFeed &feed, void *context) {
  *static_cast<bool *>(context) = clockDashboardSetSchool(&feed, "");
}

void maintainSchoolDisplay() {
  SchoolStatus status;
  // Zamčená mezipaměť by jinak vypadala jako nenačtený rozvrh.
  if (!schoolServiceStatus(status)) return;
  if (status.generation == displayedSchoolGeneration) return;
  // Stránka se zakládá až se zapnutím obrazovky. Kdyby první stažení doběhlo
  // dřív, generace se nezapíše a data se předají, jakmile stránka vznikne.
  bool shown = false;
  if (status.ready) {
    // Když je mezipaměť právě zamčená, generace se nezapíše a zkusí se znovu.
    if (!schoolServiceVisit(pushSchoolToDashboard, &shown)) return;
  } else {
    shown = clockDashboardSetSchool(nullptr, status.message);
  }
  if (shown) displayedSchoolGeneration = status.generation;
}

// Snímek radaru letadel na obrazovku. Generace se mění i po neúspěšném
// stažení, takže se hláška dostane na displej stejnou cestou jako data.
void maintainPlanesDisplay() {
  if (!clockDashboardPlanesVisible()) return;
  PlaneRadarSnapshot snapshot;
  planeRadarServiceSnapshot(snapshot);
  // Detail i hláška se překreslují i beze změny generace: klepnutí na letadlo
  // mění jen panel, ne mapu pod ním, a chyba, po které se vůbec nekreslilo -
  // třeba čekání na síť nebo nedostatek PSRAM - generaci neposune vůbec. Bez
  // porovnání hlášky by obrazovka mlčky visela na starém snímku.
  if (snapshot.generation == displayedPlanesGeneration &&
      snapshot.detail.open == displayedPlanesDetailOpen &&
      snapshot.detail.routeState == displayedPlanesRouteState &&
      strcmp(snapshot.detail.hex, displayedPlanesDetailHex) == 0 &&
      snapshot.rangeKm == displayedPlanesRangeKm &&
      snapshot.loading == displayedPlanesLoading &&
      strcmp(snapshot.message, displayedPlanesMessage) == 0) {
    return;
  }
  clockDashboardSetPlanesSnapshot(snapshot.pixels, snapshot.shownCount,
                                  snapshot.watchedVisible, snapshot.emergency,
                                  snapshot.rangeKm, snapshot.message,
                                  snapshot.loading, snapshot.haveAircraftData,
                                  snapshot.detail);
  displayedPlanesGeneration = snapshot.generation;
  displayedPlanesDetailOpen = snapshot.detail.open;
  displayedPlanesRouteState = snapshot.detail.routeState;
  strlcpy(displayedPlanesDetailHex, snapshot.detail.hex,
          sizeof(displayedPlanesDetailHex));
  strlcpy(displayedPlanesMessage, snapshot.message,
          sizeof(displayedPlanesMessage));
  displayedPlanesRangeKm = snapshot.rangeKm;
  displayedPlanesLoading = snapshot.loading;
}

void maintainSatellitesDisplay() {
  if (!clockDashboardSatellitesVisible()) return;
  static SatelliteSnapshot snapshot;
  satelliteServiceSnapshot(snapshot);
  if (snapshot.generation == displayedSatellitesGeneration &&
      snapshot.loading == displayedSatellitesLoading &&
      snapshot.detail.open == displayedSatellitesDetailOpen &&
      strcmp(snapshot.message, displayedSatellitesMessage) == 0) {
    return;
  }
  clockDashboardSetSatellitesSnapshot(snapshot);
  displayedSatellitesGeneration = snapshot.generation;
  displayedSatellitesLoading = snapshot.loading;
  displayedSatellitesDetailOpen = snapshot.detail.open;
  strlcpy(displayedSatellitesMessage, snapshot.message,
          sizeof(displayedSatellitesMessage));
}

void maintainRadarDisplay() {
  ChmiRadarSnapshot snapshot;
  chmiRadarServiceSnapshot(snapshot);
  // Konec stahování generaci neposouvá vždycky - nepovedené stažení jen
  // přepíše hlášku. Bez porovnání by pod mapou zůstalo viset "Načítám radar".
  const bool busy = snapshot.loading || snapshot.fullPreparationInProgress;
  if (snapshot.generation == displayedRadarGeneration &&
      strcmp(snapshot.frameTime, displayedRadarTime) == 0 &&
      busy == displayedRadarBusy)
    return;
  displayedRadarGeneration = snapshot.generation;
  displayedRadarBusy = busy;
  displayedRadarRadiusKm = snapshot.radiusKm;
  strlcpy(displayedRadarTime, snapshot.frameTime,
          sizeof(displayedRadarTime));
  clockDashboardSetRadarSnapshot(snapshot.pixels, snapshot.frameTime,
                                 displayedRadarRadiusKm,
                                 snapshot.message, snapshot.loading,
                                 snapshot.fullPreparationInProgress,
                                 snapshot.latestFrame,
                                 snapshot.currentFrameNumber,
                                 snapshot.animationFrameCount,
                                 snapshot.effectiveRadiusKm, snapshot.mapOnly);
}

// Blesky z vlastního serveru: předá službě kruh výstrahy i pohled radaru,
// radaru nastavení překryvu a ciferníku výstrahu.
void maintainLightning() {
  const ClockConfig &config = loopConfigSnapshot();
  const bool enabled = clockConfigLightningAvailable(config);
  const float latitude = config.openMeteoLatitude;
  const float longitude = config.openMeteoLongitude;
  const bool overlay = enabled && config.lightning.radarOverlay &&
                       clockConfigRadarAvailable(config);
  float viewLatitude = latitude;
  float viewLongitude = longitude;
  float viewRadiusKm = 0.0f;
  if (overlay)
    chmiRadarViewCircle(latitude, longitude, config.radarRadiusKm, viewLatitude,
                        viewLongitude, viewRadiusKm);
  lightningServiceSetActive(enabled, config.lightning.url, latitude, longitude,
                            config.lightning.alarmRadiusKm, viewLatitude,
                            viewLongitude, viewRadiusKm,
                            overlay && clockDashboardRadarVisible());
  chmiRadarServiceSetLightning(overlay, latitude, longitude,
                               config.lightning.alarmRadiusKm,
                               config.lightning.alarmMinutes);

  // Výstraha se přepočítává jednou za pět sekund; údery chodí nejčastěji po
  // dvaceti a stárnou po minutách.
  static unsigned long checkedAt = 0;
  const unsigned long now = millis();
  if (checkedAt != 0 && now - checkedAt < 5000) return;
  checkedAt = now;
  LightningProximity proximity;
  const bool active =
      enabled && config.lightning.clockAlert &&
      lightningServiceProximity(latitude, longitude,
                                config.lightning.alarmRadiusKm,
                                config.lightning.alarmMinutes * 60U,
                                proximity) &&
      proximity.count > 0;
  clockDashboardSetLightningAlert(active, proximity.nearestKm);
}

// Dnešní místní půlnoc a délka dne v sekundách; při změně času má den 23 nebo
// 25 hodin.
void localDayBounds(time_t epoch, int64_t &dayStart, int64_t &dayLength) {
  struct tm local;
  localtime_r(&epoch, &local);
  local.tm_hour = 0;
  local.tm_min = 0;
  local.tm_sec = 0;
  local.tm_isdst = -1;
  dayStart = static_cast<int64_t>(mktime(&local));
  local.tm_mday += 1;
  local.tm_isdst = -1;
  dayLength = static_cast<int64_t>(mktime(&local)) - dayStart;
}

// S Open-Meteo se den a noc počítají ze Slunce na zařízení, ne z odpovědi
// počasí: ta chodí po deseti minutách, takže přepnutí přicházelo pozdě,
// a při výpadku sítě zamrzlo. Home Assistant má vlastní entitu slunce.
// Poslední výsledek se drží, aby výpočet neběžel v každé smyčce.
struct LocalSunCache {
  bool valid = false;
  unsigned long computedAt = 0;
  float latitude = NAN;
  float longitude = NAN;
  int8_t sunriseOffset = 0;
  int8_t sunsetOffset = 0;
  ClockLocalSunState state;
};
LocalSunCache localSunCache;

bool applyLocalSunState(const ClockConfig &config, ClockValues &values) {
  if (config.dataSource != CLOCK_DATA_SOURCE_OPEN_METEO) return false;
  const time_t epoch = time(nullptr);
  if (epoch < VALID_TIME_THRESHOLD) return false;
  const unsigned long now = millis();
  const bool settingsChanged =
      config.openMeteoLatitude != localSunCache.latitude ||
      config.openMeteoLongitude != localSunCache.longitude ||
      config.sunriseOffsetMinutes != localSunCache.sunriseOffset ||
      config.sunsetOffsetMinutes != localSunCache.sunsetOffset;
  // Po dvaceti sekundách: přechod pak přijde nejvýš tak pozdě, a hledání
  // událostí stojí jen pár milisekund.
  if (!localSunCache.valid || settingsChanged ||
      now - localSunCache.computedAt >= 20000) {
    int64_t dayStart = 0;
    int64_t dayLength = 0;
    localDayBounds(epoch, dayStart, dayLength);
    localSunCache.state = clockLocalSunState(
        epoch, dayStart, dayLength, config.openMeteoLatitude,
        config.openMeteoLongitude, config.sunriseOffsetMinutes,
        config.sunsetOffsetMinutes);
    localSunCache.valid = true;
    localSunCache.computedAt = now;
    localSunCache.latitude = config.openMeteoLatitude;
    localSunCache.longitude = config.openMeteoLongitude;
    localSunCache.sunriseOffset = config.sunriseOffsetMinutes;
    localSunCache.sunsetOffset = config.sunsetOffsetMinutes;
  }
  const bool changed =
      !values.sunStateAvailable ||
      values.weatherIsDay != localSunCache.state.isDay ||
      values.nextSunriseTimestamp !=
          static_cast<uint64_t>(localSunCache.state.sunrise) ||
      values.nextSunsetTimestamp !=
          static_cast<uint64_t>(localSunCache.state.sunset);
  values.weatherIsDay = localSunCache.state.isDay;
  values.sunStateAvailable = true;
  values.nextSunriseTimestamp =
      static_cast<uint64_t>(localSunCache.state.sunrise);
  values.nextSunsetTimestamp = static_cast<uint64_t>(localSunCache.state.sunset);
  return changed;
}

void maintainLocalSunState() {
#if !FIRMWARE_RELEASE
  if (forceNightTestActive) return;
#endif
  if (applyLocalSunState(loopConfigSnapshot(), sampleValues))
    clockDashboardUpdate(sampleValues);
}

// Slunce a Měsíc pro obrazovku. Počítá se jen se zapnutou obrazovkou, jednou
// za minutu a hned po změně polohy nebo jazyka - hledání východů projde den
// po deseti minutách, což je pár milisekund, ale v každé smyčce zbytečných.
void maintainSkyData() {
  const ClockConfig &config = loopConfigSnapshot();
  if (!clockConfigSkyAvailable(config)) return;
  static unsigned long computedAt = 0;
  static float computedLatitude = NAN;
  static float computedLongitude = NAN;
  static bool computedValid = false;
  const unsigned long now = millis();
  const time_t epoch = time(nullptr);
  const bool valid = epoch >= VALID_TIME_THRESHOLD;
  const bool moved = config.openMeteoLatitude != computedLatitude ||
                     config.openMeteoLongitude != computedLongitude;
  if (!moved && valid == computedValid && computedAt != 0 &&
      now - computedAt < 60000)
    return;
  computedAt = now;
  computedLatitude = config.openMeteoLatitude;
  computedLongitude = config.openMeteoLongitude;
  computedValid = valid;

  ClockSkyData data;
  if (valid) {
    const double latitude = config.openMeteoLatitude;
    const double longitude = config.openMeteoLongitude;
    // Den se počítá od místní půlnoci, ne od teď: odpolední obrazovka má
    // ukázat dnešní ranní východ, ne ten zítřejší.
    int64_t dayStart = 0;
    int64_t dayLength = 0;
    localDayBounds(epoch, dayStart, dayLength);
    int64_t event = 0;
    if (astronomyFindEvent(AstronomyBody::Sun, true, dayStart, dayLength,
                           latitude, longitude, event))
      data.sunrise = event;
    if (astronomyFindEvent(AstronomyBody::Sun, false, dayStart, dayLength,
                           latitude, longitude, event))
      data.sunset = event;
    if (astronomyFindSunAltitude(ASTRONOMY_CIVIL_TWILIGHT_DEG, true, dayStart,
                                 dayLength, latitude, longitude, event))
      data.civilDawn = event;
    if (astronomyFindSunAltitude(ASTRONOMY_CIVIL_TWILIGHT_DEG, false, dayStart,
                                 dayLength, latitude, longitude, event))
      data.civilDusk = event;
    if (astronomyFindEvent(AstronomyBody::Moon, true, dayStart, dayLength,
                           latitude, longitude, event))
      data.moonrise = event;
    if (astronomyFindEvent(AstronomyBody::Moon, false, dayStart, dayLength,
                           latitude, longitude, event))
      data.moonset = event;
    const int64_t noon = dayStart + dayLength / 2;
    data.sunUpAllDay =
        astronomyHorizonMarginDeg(AstronomyBody::Sun, noon, latitude,
                                  longitude) > 0.0;
    if (data.sunrise > 0 && data.sunset > data.sunrise) {
      data.dayLengthMinutes =
          static_cast<int>((data.sunset - data.sunrise + 30) / 60);
    } else if (data.sunrise > 0 || data.sunset > 0) {
      // Západ před východem: den začal včera, nebo skončí zítra. Délka světla
      // je doplněk tmy mezi nimi.
      const int64_t dark = data.sunrise > data.sunset && data.sunset > 0
                               ? data.sunrise - data.sunset
                               : 0;
      data.dayLengthMinutes =
          static_cast<int>((dayLength - dark + 30) / 60);
    }
    const AstronomyMoonPhase phase = astronomyMoonPhase(epoch);
    data.moonPhase = static_cast<float>(phase.phase);
    data.moonIllumination = static_cast<float>(phase.illuminationPercent);
    constexpr int64_t LUNATION_SEARCH_SECONDS = 31LL * 24 * 60 * 60;
    if (astronomyFindMoonPhase(0.5, epoch, LUNATION_SEARCH_SECONDS, event))
      data.nextFullMoon = event;
    if (astronomyFindMoonPhase(0.0, epoch, LUNATION_SEARCH_SECONDS, event))
      data.nextNewMoon = event;
    data.southernHemisphere = latitude < 0.0;
    data.valid = true;
  }
  clockDashboardSetSky(data);
}

void maintainRadarNightVisual() {
  const ClockConfig &config = loopConfigSnapshot();
  const bool enabled =
      clockDashboardNightModeEnabled() &&
      config.nightVisualMode == CLOCK_NIGHT_VISUAL_RED;
  if (radarRedNightModeApplied == enabled) return;
  radarRedNightModeApplied = enabled;
  chmiRadarServiceSetRedNightMode(enabled);
  planeRadarServiceSetRedNightMode(enabled);
  satelliteServiceSetRedNightMode(enabled);
}

void handleConfigurationWebStatus(bool active) {
  clockDashboardSetWebActive(active);
}

void loadSunTransitionTimesForWeb(uint64_t &nextSunriseTimestamp,
                                  uint64_t &nextSunsetTimestamp) {
  nextSunriseTimestamp = sampleValues.nextSunriseTimestamp;
  nextSunsetTimestamp = sampleValues.nextSunsetTimestamp;
}

bool requestHomeAssistantRefreshFromWeb() {
  if (homeAssistantTaskHandle == nullptr) return false;
  portENTER_CRITICAL(&dayNightLightRefreshMux);
  dayNightLightRefreshRequested = true;
  portEXIT_CRITICAL(&dayNightLightRefreshMux);
  xTaskNotifyGive(homeAssistantTaskHandle);
  return true;
}

bool consumeDayNightLightRefreshRequest() {
  portENTER_CRITICAL(&dayNightLightRefreshMux);
  const bool requested = dayNightLightRefreshRequested;
  dayNightLightRefreshRequested = false;
  portEXIT_CRITICAL(&dayNightLightRefreshMux);
  return requested;
}

void loadDayNightStatusForWeb(bool &sunAvailable, bool &sunIsDay,
                              bool &lightAvailable, bool &lightOn,
                              bool &nightMode) {
  sunAvailable = sampleValues.sunStateAvailable;
  sunIsDay = sampleValues.weatherIsDay;
  lightAvailable = sampleValues.dayNightLightStateAvailable;
  lightOn = sampleValues.dayNightLightOn;
  nightMode = clockDashboardNightModeEnabled();
}

void handleSettingsSave(uint8_t clockStyle, uint8_t dayBrightness,
                        uint8_t nightBrightness,
                        bool automaticDayNight, bool secondRingEnabled,
                        uint8_t selectedSecondEffect,
                        bool animatedWeatherIcons, uint8_t weatherIconStyle,
                        bool automaticFirmwareUpdate, uint8_t webMode) {
  ClockConfig &config = loopConfigSnapshot();
  config.dayBrightness = constrain(dayBrightness, 1, 100);
  config.nightBrightness = constrain(nightBrightness, 1, 100);
  config.automaticDayNight = automaticDayNight;
  config.secondRingEnabled = secondRingEnabled;
  config.secondEffect = selectedSecondEffect;
  config.animatedWeatherIcons = animatedWeatherIcons;
  config.weatherIconStyle = weatherIconStyle;
  config.automaticFirmwareUpdate = automaticFirmwareUpdate;
  if (saveRuntimeConfig(config, false)) {
    ClockAppearanceConfig appearance = persistedAppearance;
    appearance.style = constrain(
        clockStyle, static_cast<uint8_t>(CLOCK_STYLE_DIGITAL),
        static_cast<uint8_t>(CLOCK_STYLE_VALUES));
    if (appearance.style != persistedAppearance.style)
      saveClockAppearanceFromWeb(appearance);
    configurationWebSetMode(static_cast<ConfigurationWebMode>(webMode));
    clockDashboardSetWebMode(configurationWebMode());
  }
}

void handleSettingsFirmwareCheck() {
  firmwareUpdateServiceRequestCheck(false);
}

void handleSettingsFirmwareInstall() {
  firmwareUpdateServiceRequestCheck(true);
}

void handleUsbCommands() {
#if !FIRMWARE_RELEASE
  while (Serial.available() > 0) {
    const char character = static_cast<char>(Serial.read());
    if (character == '\n' || character == '\r') {
      usbCommand.trim();
      if (usbCommand == "SCREENSHOT" && !screenshotTransferActive) {
        Serial.println("WSFB1_BEGIN");
        if (!displayDriverBeginFramebufferCapture(Serial)) {
          Serial.println("WSFB1_ERROR");
        } else {
          screenshotTransferActive = true;
        }
      } else if (usbCommand == "SETTINGS" && !screenshotTransferActive) {
        clockDashboardShowSettings();
        Serial.println("SETTINGS_OPEN");
      } else if (usbCommand == "SETTINGS2" && !screenshotTransferActive) {
        clockDashboardShowSettingsPage(1);
        Serial.println("SETTINGS_OPEN");
      } else if (usbCommand == "SETTINGS3" && !screenshotTransferActive) {
        clockDashboardShowSettingsPage(2);
        Serial.println("SETTINGS_OPEN");
      } else if (usbCommand == "SETTINGS4" && !screenshotTransferActive) {
        clockDashboardShowSettingsPage(3);
        Serial.println("SETTINGS_OPEN");
      } else if (usbCommand == "SETTINGS5" && !screenshotTransferActive) {
        clockDashboardShowSettingsPage(4);
        Serial.println("SETTINGS_OPEN");
      } else if (usbCommand == "NIGHT" && !screenshotTransferActive) {
        clockDashboardSetNightMode(true);
        Serial.println("NIGHT_OPEN");
      } else if (usbCommand == "NIGHTTEST" && !screenshotTransferActive) {
        forceNightTestActive = true;
        sampleValues.weatherIsDay = false;
        sampleValues.sunStateAvailable = true;
        clockDashboardUpdate(sampleValues);
        Serial.println("NIGHT_TEST_ON");
      } else if (usbCommand == "NIGHTTESTOFF" && !screenshotTransferActive) {
        forceNightTestActive = false;
        if (homeAssistantTaskHandle != nullptr)
          xTaskNotifyGive(homeAssistantTaskHandle);
        Serial.println("NIGHT_TEST_OFF");
      } else if (usbCommand == "RSSOFF" && !screenshotTransferActive) {
        // Záchranná brzda: kanál se dá vypnout i s nefunkčním webem.
        ClockConfig &config = loopConfigSnapshot();
        config.rss.enabled = false;
        Serial.println(saveRuntimeConfig(config, true) ? "RSS_OFF"
                                                       : "RSS_OFF_FAILED");
      } else if (usbCommand == "RSSON" && !screenshotTransferActive) {
        ClockConfig &config = loopConfigSnapshot();
        config.rss.enabled = true;
        Serial.println(saveRuntimeConfig(config, true) ? "RSS_ON"
                                                       : "RSS_ON_FAILED");
      } else if (usbCommand == "RSSFETCH" && !screenshotTransferActive) {
        // Notifikace v rssTask nuluje deadline, takže tohle opravdu vynutí
        // stažení i uprostřed nastaveného intervalu.
        if (rssTaskHandle != nullptr) xTaskNotifyGive(rssTaskHandle);
        Serial.println("RSS_FETCH_REQUESTED");
      } else if (usbCommand == "RSSSHOW" && !screenshotTransferActive) {
        // Otevření sériem: připojení k portu desku resetuje, takže ručně
        // nalistovanou obrazovku by screenshot nikdy nezastihl.
        clockDashboardSetRssVisible(true);
        Serial.println("RSS_SHOWN");
      } else if (usbCommand == "FORECASTOFF" && !screenshotTransferActive) {
        // Záchranná brzda: obrazovka se dá vypnout i s nefunkčním webem.
        ClockConfig &config = loopConfigSnapshot();
        config.forecast.enabled = false;
        Serial.println(saveRuntimeConfig(config, true) ? "FORECAST_OFF"
                                                       : "FORECAST_OFF_FAILED");
      } else if (usbCommand == "FORECASTON" && !screenshotTransferActive) {
        ClockConfig &config = loopConfigSnapshot();
        config.forecast.enabled = true;
        Serial.println(saveRuntimeConfig(config, true) ? "FORECAST_ON"
                                                       : "FORECAST_ON_FAILED");
      } else if (usbCommand.startsWith("FORECASTAIR") &&
                 !screenshotTransferActive) {
        // Přepnutí kvality ovzduší bez webu, aby šly obě varianty rozvržení
        // porovnat na jednom snímku vedle druhého.
        ClockConfig &config = loopConfigSnapshot();
        config.forecast.airQuality = usbCommand.endsWith("ON");
        Serial.println(saveRuntimeConfig(config, true)
                           ? (config.forecast.airQuality ? "FORECAST_AIR_ON"
                                                         : "FORECAST_AIR_OFF")
                           : "FORECAST_AIR_FAILED");
      } else if (usbCommand == "FORECASTFETCH" && !screenshotTransferActive) {
        // Notifikace ve forecastTask nuluje deadline, takže tohle opravdu
        // vynutí stažení i uprostřed nastaveného intervalu.
        if (forecastTaskHandle != nullptr) xTaskNotifyGive(forecastTaskHandle);
        Serial.println("FORECAST_FETCH_REQUESTED");
      } else if (usbCommand == "FORECASTSHOW" && !screenshotTransferActive) {
        // Ze stejného důvodu jako RSSSHOW: připojení k portu desku resetuje,
        // takže ručně nalistovaná předpověď by screenshot nikdy nezastihl.
        clockDashboardSetForecastVisible(true);
        Serial.println("FORECAST_SHOWN");
      } else if (usbCommand == "AGENDASHOW" && !screenshotTransferActive) {
        // Ze stejného důvodu jako RSSSHOW: připojení k portu desku resetuje,
        // takže ručně nalistovaná agenda by screenshot nikdy nezastihl.
        clockDashboardSetAgendaVisible(true);
        Serial.println("AGENDA_SHOWN");
      } else if (usbCommand == "SCHOOLSHOW" && !screenshotTransferActive) {
        // Ze stejného důvodu jako RSSSHOW: připojení k portu desku resetuje.
        clockDashboardSetSchoolVisible(true);
        Serial.println("SCHOOL_SHOWN");
      } else if (usbCommand == "SCHOOLNEWS" && !screenshotTransferActive) {
        // Druhá stránka Školy pro screenshot: tažení prstu přes USB poslat nejde.
        clockDashboardSetSchoolVisible(true);
        Serial.println(clockDashboardSwipeSchool() ? "SCHOOL_NEWS_SHOWN"
                                                   : "SCHOOL_NEWS_UNAVAILABLE");
      } else if (usbCommand == "SKYSHOW" && !screenshotTransferActive) {
        // Ze stejného důvodu jako RSSSHOW: připojení k portu desku resetuje,
        // takže ručně nalistovanou obrazovku by screenshot nikdy nezastihl.
        clockDashboardSetSkyVisible(true);
        Serial.println("SKY_SHOWN");
      } else if (usbCommand == "RADARSHOW" && !screenshotTransferActive) {
        // Ze stejného důvodu jako RSSSHOW: připojení k portu desku resetuje,
        // takže ručně nalistovaný radar by screenshot nikdy nezastihl.
        clockDashboardSetRadarVisible(true);
        Serial.println("RADAR_SHOWN");
      } else if (usbCommand == "PLANESSHOW" && !screenshotTransferActive) {
        // Ze stejného důvodu jako RSSSHOW: připojení k portu desku resetuje,
        // takže ručně nalistovaná letadla by screenshot nikdy nezastihl.
        clockDashboardSetPlanesVisible(true);
        Serial.println("PLANES_SHOWN");
      } else if (usbCommand == "SATELLITESSHOW" && !screenshotTransferActive) {
        // Ze stejného důvodu jako RSSSHOW: připojení k portu desku resetuje.
        clockDashboardSetSatellitesVisible(true);
        Serial.println("SATELLITES_SHOWN");
      } else if (usbCommand.startsWith("RADARSOURCE") &&
                 !screenshotTransferActive) {
        // Přepnutí zdroje srážek bez webu, aby šly obě varianty porovnat.
        const long source = strtol(usbCommand.substring(11).c_str(), nullptr, 10);
        if (source != CLOCK_RADAR_SOURCE_CHMI &&
            source != CLOCK_RADAR_SOURCE_RAINVIEWER) {
          Serial.println("RADAR_SOURCE_ERROR");
        } else {
          ClockConfig &config = loopConfigSnapshot();
          config.radarSource = static_cast<uint8_t>(source);
          Serial.println(saveRuntimeConfig(config, true) ? "RADAR_SOURCE_SET"
                                                         : "RADAR_SOURCE_FAILED");
        }
      } else if (usbCommand.startsWith("RSSITEMS") &&
                 !screenshotTransferActive) {
        const long items = strtol(usbCommand.substring(8).c_str(), nullptr, 10);
        if (items < CLOCK_RSS_MIN_ITEMS || items > CLOCK_RSS_MAX_ITEMS) {
          Serial.println("RSS_ITEMS_ERROR");
        } else {
          ClockConfig &config = loopConfigSnapshot();
          config.rss.itemCount = static_cast<uint8_t>(items);
          Serial.println(saveRuntimeConfig(config, true) ? "RSS_ITEMS_SET"
                                                         : "RSS_ITEMS_FAILED");
        }
      } else if (usbCommand == "WEBLOCK" && !screenshotTransferActive) {
        configurationWebLockForTest();
        Serial.println("WEB_CONFIG_LOCKED");
      } else if (usbCommand == "WEBUNLOCK" && !screenshotTransferActive) {
        configurationWebUnlockForTest();
        Serial.println("WEB_CONFIG_UNLOCKED");
      } else if (usbCommand.startsWith("TIMEOFFSET") &&
                 !screenshotTransferActive) {
        const String minutesText = usbCommand.substring(10);
        char *end = nullptr;
        const long minutes = strtol(minutesText.c_str(), &end, 10);
        if (end != minutesText.c_str() && *end == '\0' &&
            minutes >= -720 && minutes <= 720) {
          displayTimeOffsetSeconds = static_cast<int32_t>(minutes * 60L);
          lastDisplayedSecond = -1;
          Serial.printf("TIME_OFFSET_MINUTES=%ld\n", minutes);
        } else {
          Serial.println("TIME_OFFSET_ERROR");
        }
      }
      usbCommand = "";
    } else if (usbCommand.length() < 32) {
      usbCommand += character;
    } else {
      usbCommand = "";
    }
  }
#endif
}

void streamScreenshot() {
#if !FIRMWARE_RELEASE
  if (!screenshotTransferActive) return;
  if (!displayDriverStreamFramebufferChunk(Serial)) return;

  Serial.println();
  Serial.println("WSFB1_END");
  screenshotTransferActive = false;
  LCD_Resync();
#endif
}

void maintainDisplaySync() {
  if (displayResyncAt == 0 ||
      static_cast<long>(millis() - displayResyncAt) < 0) {
    return;
  }
  LCD_Resync();
  displayResyncAt = 0;
}

void initializeNetworkTime() {
  configTzTime("CET-1CEST,M3.5.0/2,M10.5.0/3", "pool.ntp.org",
               "time.cloudflare.com");
  sntp_set_sync_interval(NTP_SYNC_INTERVAL_MS);
#if !FIRMWARE_RELEASE
#if HAS_WIFI_SECRETS
  Serial.println("Wi-Fi a NTP inicializovany");
#else
  Serial.println("Wi-Fi konfigurace chybi; cas zustava demonstracni");
#endif
#endif
}

void startMdns() {
  mdnsStarted = MDNS.begin(networkDeviceName);
  if (!mdnsStarted) return;
  MDNS.addService("http", "tcp", 80);
#if !FIRMWARE_RELEASE
  Serial.printf("Nastaveni: http://%s.local/\n", networkDeviceName);
#endif
}

void handleDeviceNameChanged(const char *name) {
  clockConfigCopy(networkDeviceName, sizeof(networkDeviceName), name);
  // DHCP jméno převezme router při dalším připojení k Wi-Fi.
  WiFi.setHostname(networkDeviceName);
  mdnsRestartAt = (millis() + MDNS_RENAME_DELAY_MS) | 1;
}

void maintainNetworkTime() {
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  if (!wifiConnected) {
    if (!displayedWifiIp.isEmpty()) {
      displayedWifiIp = "";
      clockDashboardSetWifiAddress("");
    }
    if (wifiWasConnected) clockDashboardSetWifiConnected(false);
    wifiWasConnected = false;
    return;
  }

  const String wifiIp = WiFi.localIP().toString();
  if (wifiIp != displayedWifiIp) {
    displayedWifiIp = wifiIp;
    clockDashboardSetWifiAddress(displayedWifiIp.c_str());
  }

  if (!wifiWasConnected) {
    wifiWasConnected = true;
    clockDashboardSetWifiConnected(true);
    displayResyncAt = millis() + 2000;
#if !FIRMWARE_RELEASE
    Serial.println("Wi-Fi pripojena, cekam na NTP");
#endif
    if (!mdnsStarted) startMdns();
  }
  if (mdnsRestartAt != 0 &&
      static_cast<long>(millis() - mdnsRestartAt) >= 0) {
    mdnsRestartAt = 0;
    if (mdnsStarted) MDNS.end();
    mdnsStarted = false;
    startMdns();
  }

  time_t now;
  time(&now);
  if (now < VALID_TIME_THRESHOLD) return;
  if (!timeWasSynchronized) {
    timeWasSynchronized = true;
    if (homeAssistantTaskHandle != nullptr) {
      xTaskNotifyGive(homeAssistantTaskHandle);
    }
    // Čas se právě synchronizoval, takže teď má smysl stáhnout zprávy: bez něj
    // by se u nich nedal spočítat místní čas vydání.
    if (rssTaskHandle != nullptr) xTaskNotifyGive(rssTaskHandle);
    const ClockConfig &config = loopConfigSnapshot();
    const bool radarAvailable = clockConfigRadarAvailable(config);
    chmiRadarServiceSetActive(
        radarAvailable && clockDashboardRadarVisible(),
        radarAvailable && config.automaticRadarRotation,
        config.openMeteoLatitude, config.openMeteoLongitude,
        config.radarRadiusKm, config.radarFrameCount,
        config.radarMapOpacity, config.radarPauseSeconds,
        config.radarLegend, config.radarPrecipitation, config.radarSource);
    applyPlaneRadarState(config);
    applySatellitesState(config);
#if !FIRMWARE_RELEASE
    Serial.println("NTP synchronizovano");
#endif
  }

  time_t displayedNow = now;
#if !FIRMWARE_RELEASE
  displayedNow += displayTimeOffsetSeconds;
#endif
  struct tm localTime;
  localtime_r(&displayedNow, &localTime);
  if (localTime.tm_sec == lastDisplayedSecond) return;
  lastDisplayedSecond = localTime.tm_sec;

  const ClockConfig &config = loopConfigSnapshot();
  char timeText[6];
  snprintf(timeText, sizeof(timeText), config.showLeadingHourZero ? "%02d:%02d"
                                                                  : "%d:%02d",
           localTime.tm_hour, localTime.tm_min);
  clockDashboardSetTime(timeText);
  clockDashboardSetSecond(static_cast<uint8_t>(localTime.tm_sec));

  char dateText[64];
  const bool english = config.language == CLOCK_LANGUAGE_ENGLISH;
  const char *const *weekdays = english ? ENGLISH_WEEKDAYS : CZECH_WEEKDAYS;
  const char *const *months = english ? ENGLISH_MONTHS : CZECH_MONTHS;
  const uint8_t displayedDateFormat =
      activeAppearance.style == CLOCK_STYLE_ANALOG
          ? activeAppearance.analogDateFormat
          : config.dateFormat;
  switch (displayedDateFormat) {
    case CLOCK_DATE_FORMAT_HIDDEN:
      dateText[0] = '\0';
      break;
    case CLOCK_DATE_FORMAT_NUMERIC:
      if (english) {
        snprintf(dateText, sizeof(dateText), "%04d-%02d-%02d",
                 localTime.tm_year + 1900, localTime.tm_mon + 1,
                 localTime.tm_mday);
      } else {
        snprintf(dateText, sizeof(dateText), "%02d.%02d.%04d",
                 localTime.tm_mday, localTime.tm_mon + 1,
                 localTime.tm_year + 1900);
      }
      break;
    case CLOCK_DATE_FORMAT_DAY_MONTH:
      snprintf(dateText, sizeof(dateText), "%02d.%02d.",
               localTime.tm_mday, localTime.tm_mon + 1);
      break;
    case CLOCK_DATE_FORMAT_DAY_MONTH_YEAR:
      if (english)
        snprintf(dateText, sizeof(dateText), "%s %d, %d",
                 months[localTime.tm_mon], localTime.tm_mday,
                 localTime.tm_year + 1900);
      else
        snprintf(dateText, sizeof(dateText), "%d. %s %d", localTime.tm_mday,
                 months[localTime.tm_mon], localTime.tm_year + 1900);
      break;
    case CLOCK_DATE_FORMAT_WEEKDAY_DAY_MONTH_YEAR:
      if (english)
        snprintf(dateText, sizeof(dateText), "%s, %s %d, %d",
                 weekdays[localTime.tm_wday], months[localTime.tm_mon],
                 localTime.tm_mday, localTime.tm_year + 1900);
      else
        snprintf(dateText, sizeof(dateText), "%s, %d. %s %d",
                 weekdays[localTime.tm_wday], localTime.tm_mday,
                 months[localTime.tm_mon], localTime.tm_year + 1900);
      break;
    case CLOCK_DATE_FORMAT_WEEKDAY_DAY_MONTH:
    default:
      if (english)
        snprintf(dateText, sizeof(dateText), "%s, %s %d",
                 weekdays[localTime.tm_wday], months[localTime.tm_mon],
                 localTime.tm_mday);
      else
        snprintf(dateText, sizeof(dateText), "%s, %d. %s",
                 weekdays[localTime.tm_wday], localTime.tm_mday,
                 months[localTime.tm_mon]);
      break;
  }
  clockDashboardSetDate(dateText);

  // Jmeniny jsou český pojem, takže se ukazují jen u českého data. Tabulka je
  // pevná funkce kalendáře, počítá se přímo na zařízení bez sítě. Se skrytým
  // datem mizí i ony: samotné jméno bez data je na ciferníku bezprizorní.
  const bool namedayVisible =
      !english && displayedDateFormat != CLOCK_DATE_FORMAT_HIDDEN;
  clockDashboardSetNameday(
      namedayVisible ? czechNamedayFor(localTime.tm_mon + 1, localTime.tm_mday)
                     : nullptr);
}

void handleFirmwareUpdateLifecycle(bool updating) {
  weatherAnimationServiceSetFirmwareUpdateActive(updating);
  if (homeAssistantTaskHandle != nullptr) {
    if (updating) {
      vTaskSuspend(homeAssistantTaskHandle);
    } else {
      vTaskResume(homeAssistantTaskHandle);
    }
  }
  firmwareUpdateDisplayRequested = updating;
  if (updating) {
    displayResyncAt = 0;
    firmwareUpdateDisplayPresented = false;
    const unsigned long deadline = millis() + 1000;
    while (!firmwareUpdateDisplayPresented &&
           static_cast<long>(deadline - millis()) > 0) {
      delay(5);
    }
    firmwareUpdateBlackPresented = false;
    firmwareUpdateCountdownStartedAt = millis();
    firmwareUpdateCountdownStarted = true;
    const unsigned long blackDeadline = millis() + 7000;
    while (!firmwareUpdateBlackPresented &&
           static_cast<long>(blackDeadline - millis()) > 0) {
      delay(5);
    }
    // Necháme několik obnovovacích cyklů naplnit framebuffer i RGB bounce
    // buffery čistou černou ještě před prvním zápisem OTA do flash.
    delay(500);
    chmiRadarServicePrepareForFirmwareUpdate();
    planeRadarServicePrepareForFirmwareUpdate();
    satelliteServicePrepareForFirmwareUpdate();
    lightningServicePrepareForFirmwareUpdate();
    rainAlertServicePrepareForFirmwareUpdate();
  } else {
    chmiRadarServiceBegin();
    planeRadarServiceBegin();
    // Příprava na aktualizaci službu vypnula. Se schovanou obrazovkou by ji
    // nikdo nezapnul zpátky - callback viditelnosti se nezavolá - a radar by po
    // přerušené aktualizaci mlčel až do restartu, včetně vypadnutí ze střídání.
    applyPlaneRadarState(loopConfigSnapshot());
    satelliteServiceBegin();
    applySatellitesState(loopConfigSnapshot());
    // Ze stejného důvodu jako u letadel a družic: příprava na aktualizaci
    // úlohu zastavila a po přerušené aktualizaci by mlčela až do restartu.
    rainAlertServiceBegin();
    applyRainAlertState(loopConfigSnapshot());
    firmwareUpdateCountdownStarted = false;
    firmwareUpdateBlackRequested = false;
    displayResyncAt = millis() + 500;
  }
}

void applyFirmwareUpdateDisplayRequest() {
  const bool requested = firmwareUpdateDisplayRequested;
  if (firmwareUpdateDisplayActive == requested) return;
  firmwareUpdateDisplayActive = requested;
  clockDashboardSetFirmwareUpdateActive(requested);
  if (!requested) clockDashboardSetFirmwareUpdateBlack(false);
}

void maintainAutomaticFirmwareUpdate() {
  if (!IS_RELEASE_FIRMWARE || !timeWasSynchronized ||
      WiFi.status() != WL_CONNECTED) {
    return;
  }
  const ClockConfig &config = loopConfigSnapshot();
  if (!config.automaticFirmwareUpdate) return;
  time_t now;
  time(&now);
  struct tm localTime;
  localtime_r(&now, &localTime);
  if (localTime.tm_hour < 4 ||
      (localTime.tm_hour == 4 && localTime.tm_min < 10)) {
    return;
  }
  const int dateKey = (localTime.tm_year + 1900) * 1000 + localTime.tm_yday;
  if (dateKey == lastAutomaticFirmwareCheckDate) return;
  if (firmwareUpdateServiceRequestCheck(true)) {
    lastAutomaticFirmwareCheckDate = dateKey;
  }
}

void maintainFirmwareDisplayStatus() {
  const unsigned long now = millis();
  if (firmwareDisplayInitialized &&
      now - lastFirmwareDisplayRefreshAt < 250) {
    return;
  }
  lastFirmwareDisplayRefreshAt = now;
  const FirmwareUpdateSnapshot snapshot = firmwareUpdateServiceSnapshot();
  if (firmwareDisplayInitialized &&
      strcmp(displayedFirmwareVersion, snapshot.currentVersion) == 0 &&
      displayedFirmwareUpdateAvailable == snapshot.updateAvailable) {
    return;
  }
  strlcpy(displayedFirmwareVersion, snapshot.currentVersion,
          sizeof(displayedFirmwareVersion));
  displayedFirmwareUpdateAvailable = snapshot.updateAvailable;
  firmwareDisplayInitialized = true;
  clockDashboardSetFirmwareVersion(displayedFirmwareVersion,
                                   displayedFirmwareUpdateAvailable);
}

bool extractJsonStringField(const String &payload, const char *key,
                            String &value) {
  const String quotedKey = String('"') + key + '"';
  const int keyPosition = payload.indexOf(quotedKey);
  if (keyPosition < 0) return false;
  const int colonPosition = payload.indexOf(':', keyPosition + quotedKey.length());
  const int openingQuote = payload.indexOf('"', colonPosition + 1);
  if (colonPosition < 0 || openingQuote < 0) return false;
  value = "";
  bool escaped = false;
  for (int index = openingQuote + 1; index < payload.length(); ++index) {
    const char character = payload[index];
    if (escaped) {
      value += character;
      escaped = false;
    } else if (character == '\\') {
      escaped = true;
    } else if (character == '"') {
      return true;
    } else {
      value += character;
    }
  }
  return false;
}

bool extractJsonNumberField(const String &payload, const char *key,
                            double &value) {
  const String quotedKey = String('"') + key + '"';
  int searchFrom = 0;
  while (true) {
    const int keyPosition = payload.indexOf(quotedKey, searchFrom);
    if (keyPosition < 0) return false;
    const int colonPosition =
        payload.indexOf(':', keyPosition + quotedKey.length());
    if (colonPosition < 0) return false;
    const char *start = payload.c_str() + colonPosition + 1;
    while (*start == ' ' || *start == '\t' || *start == '\r' ||
           *start == '\n' || *start == '[')
      ++start;
    char *end = nullptr;
    value = strtod(start, &end);
    if (end != start && std::isfinite(value)) return true;
    searchFrom = keyPosition + quotedKey.length();
  }
}

bool stateAsFloat(const String &state, float &value);
int weatherCodeForState(const String &state);

bool fetchOpenMeteo(const ClockConfig &config, ClockValues &values) {
  networkDiagnosticsBegin(NetworkDiagnosticKind::OpenMeteoRuntime);
  NetworkOperationGuard networkGuard(HOME_ASSISTANT_RESPONSE_TIMEOUT_MS);
  if (!networkGuard) {
    networkDiagnosticsEnd(NetworkDiagnosticKind::OpenMeteoRuntime, false,
                          HTTPC_ERROR_CONNECTION_REFUSED);
    return false;
  }
  String url = F("https://api.open-meteo.com/v1/forecast?latitude=");
  url += String(config.openMeteoLatitude, 5);
  url += F("&longitude=");
  url += String(config.openMeteoLongitude, 5);
  url += F("&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,precipitation,rain,showers,snowfall,weather_code,cloud_cover,pressure_msl,surface_pressure,wind_speed_10m,wind_direction_10m,wind_gusts_10m,uv_index&daily=sunrise,sunset&timeformat=unixtime&timezone=auto&forecast_days=1");
  WiFiClientSecure client;
  client.setCACert(FIRMWARE_RELEASE_ROOT_CA);
  client.setHandshakeTimeout(NETWORK_TLS_HANDSHAKE_TIMEOUT_S);
  HTTPClient http;
  http.setConnectTimeout(HOME_ASSISTANT_CONNECT_TIMEOUT_MS);
  http.setTimeout(HOME_ASSISTANT_RESPONSE_TIMEOUT_MS);
  int status = HTTPC_ERROR_CONNECTION_REFUSED;
  String payload;
  if (http.begin(client, url)) {
    status = http.GET();
    if (status == HTTP_CODE_OK) payload = http.getString();
    http.end();
  }
  if (status != HTTP_CODE_OK) {
    networkDiagnosticsEnd(NetworkDiagnosticKind::OpenMeteoRuntime, false,
                          status);
    return false;
  }
  double number = 0;
  bool ok = extractJsonNumberField(payload, "weather_code", number);
  if (ok) values.weatherCode = weatherCodeFromWmo(lround(number));
  if (extractJsonNumberField(payload, "is_day", number)) {
    values.weatherIsDay = number >= 0.5;
    values.sunStateAvailable = true;
  }
  if (extractJsonNumberField(payload, "sunrise", number))
    values.nextSunriseTimestamp = static_cast<uint64_t>(number);
  if (extractJsonNumberField(payload, "sunset", number))
    values.nextSunsetTimestamp = static_cast<uint64_t>(number);
  if (values.nextSunriseTimestamp > 0 && values.nextSunsetTimestamp > 0) {
    const time_t now = time(nullptr);
    if (now >= VALID_TIME_THRESHOLD) {
      const time_t dayStarts =
          static_cast<time_t>(values.nextSunriseTimestamp) +
          config.sunriseOffsetMinutes * 60;
      const time_t dayEnds = static_cast<time_t>(values.nextSunsetTimestamp) +
                             config.sunsetOffsetMinutes * 60;
      values.weatherIsDay = now >= dayStarts && now < dayEnds;
      values.sunStateAvailable = true;
    }
  }
  // Stavový řádek radaru chce teplotu venku bez ohledu na to, co si majitel
  // nastavil do čtyř pozic ciferníku; odpověď ji nese vždycky.
  values.outsideTemperatureC =
      extractJsonNumberField(payload, "temperature_2m", number)
          ? static_cast<float>(number)
          : NAN;
  float *destinations[] = {&values.leftTemperatureC, &values.rightTemperatureC,
                           &values.metricAValue, &values.metricBValue};
  for (size_t index = 0; index < 4; ++index) {
    if (config.tmepSlots[index].enabled) continue;
    if (extractJsonNumberField(payload, config.openMeteoSlots[index].value,
                               number)) {
      *destinations[index] = static_cast<float>(number);
    } else {
      *destinations[index] = NAN;
      ok = false;
    }
  }
  values.homeAssistantOnline = false;
  networkDiagnosticsSetDetail(NetworkDiagnosticKind::OpenMeteoRuntime,
                              ok ? F("Aktuální data načtena")
                                 : F("Odpověď neobsahuje všechny hodnoty"));
  networkDiagnosticsEnd(NetworkDiagnosticKind::OpenMeteoRuntime, ok, status);
  return ok;
}

bool tmepSlotsEnabled(const ClockConfig &config) {
  for (const ClockTmepSlotConfig &slot : config.tmepSlots) {
    if (slot.enabled) return true;
  }
  return false;
}

struct TmepValuesContext {
  const ClockConfig &config;
  ClockValues &values;
  bool complete = true;
};

void applyTmepValues(const TmepCatalog &catalog, void *rawContext) {
  TmepValuesContext &context =
      *static_cast<TmepValuesContext *>(rawContext);
  float *destinations[] = {&context.values.leftTemperatureC,
                           &context.values.rightTemperatureC,
                           &context.values.metricAValue,
                           &context.values.metricBValue};
  for (size_t index = 0; index < 4; ++index) {
    const ClockTmepSlotConfig &slot = context.config.tmepSlots[index];
    if (!slot.enabled) continue;
    const TmepSensor *sensor = tmepFindSensor(catalog, slot.sensorId);
    const TmepValue *value =
        sensor == nullptr ? nullptr : tmepFindValue(*sensor, slot.field);
    if (value == nullptr || !value->available) {
      *destinations[index] = NAN;
      context.complete = false;
      continue;
    }
    *destinations[index] = value->value;
  }
}

bool fetchTmepValues(const ClockConfig &config, ClockValues &values) {
  TmepValuesContext context{config, values};
  int status = HTTPC_ERROR_CONNECTION_REFUSED;
  String error;
  if (!tmepFetchCatalog(config.tmepExportId, config.tmepExportKey,
                        applyTmepValues, &context,
                        NetworkDiagnosticKind::TmepRuntime, status, error)) {
    return false;
  }

  if (!context.complete) {
    networkDiagnosticsSetDetail(
        NetworkDiagnosticKind::TmepRuntime,
        F("Export neobsahuje všechny vybrané hodnoty."));
  }
  return context.complete;
}

bool applyHomeAssistantState(const ClockConfig &config, const String &entityId,
                             const String &state, ClockValues &values) {
  float number;
  // Sloty se plní nezávisle na řetězci níže: jedna entita může krmit zároveň
  // starou pozici (metricA) i slot obrazovky CLOCK_STYLE_VALUES.
  bool filledSlot = false;
  if (stateAsFloat(state, number)) {
    for (size_t index = 0; index < CLOCK_VALUE_SLOT_COUNT; ++index) {
      const ClockValueSlotConfig &slot = clockConfigValueSlot(config, index);
      if (!slot.enabled || slot.entityId[0] == '\0') continue;
      if (entityId != slot.entityId) continue;
      values.slotValues[index] = number;
      filledSlot = true;
    }
  }
  // Teplota pro radar se plní nezávisle na řetězci níže, stejně jako sloty:
  // jedna entita může krmit zároveň pozici na ciferníku i stavový řádek.
  bool filledRadarTemperature = false;
  if (config.radarStatusTemperatureEntityId[0] != '\0' &&
      entityId == config.radarStatusTemperatureEntityId &&
      stateAsFloat(state, number)) {
    values.outsideTemperatureC = number;
    filledRadarTemperature = true;
  }
  if (entityId == config.weatherEntityId) {
    values.weatherCode = weatherCodeForState(state);
  } else if (entityId == config.leftSide.temperatureEntityId &&
             stateAsFloat(state, number)) {
    values.leftTemperatureC = number;
  } else if (entityId == config.rightSide.temperatureEntityId &&
             stateAsFloat(state, number)) {
    values.rightTemperatureC = number;
  } else if (entityId == config.metricA.entityId && stateAsFloat(state, number)) {
    values.metricAValue = number;
  } else if (entityId == config.metricB.entityId && stateAsFloat(state, number)) {
    values.metricBValue = number;
  } else if (entityId == config.sunEntityId &&
             (state == "above_horizon" || state == "below_horizon")) {
    values.weatherIsDay = state == "above_horizon";
    values.sunStateAvailable = true;
  } else if (entityId == config.dayNightLightEntityId &&
             (state == "on" || state == "off")) {
    values.dayNightLightOn = state == "on";
    values.dayNightLightStateAvailable = true;
  } else {
    return filledSlot || filledRadarTemperature;
  }
  return true;
}

bool shouldRetryHomeAssistantRequest(int status) {
  return status <= 0 || status == 408 || status == 429 ||
         status >= 500;
}

bool requestHomeAssistantState(NetworkClient &client,
                               const ClockConfig &config,
                               const char *entityId, String &payload,
                               int &lastStatus) {
  if (entityId[0] == '\0') return false;
  NetworkOperationGuard networkGuard(HOME_ASSISTANT_RESPONSE_TIMEOUT_MS);
  if (!networkGuard) {
    lastStatus = HTTPC_ERROR_CONNECTION_REFUSED;
    return false;
  }
  const String url = String(config.homeAssistantUrl) + "/api/states/" + entityId;
  for (uint8_t attempt = 0; attempt < HOME_ASSISTANT_REQUEST_ATTEMPTS;
       ++attempt) {
    HTTPClient http;
    http.setConnectTimeout(HOME_ASSISTANT_CONNECT_TIMEOUT_MS);
    http.setTimeout(HOME_ASSISTANT_RESPONSE_TIMEOUT_MS);
    http.setReuse(true);
    if (!http.begin(client, url)) {
      lastStatus = HTTPC_ERROR_CONNECTION_REFUSED;
    } else {
      http.addHeader("Authorization",
                     String("Bearer ") + config.homeAssistantToken);
      http.addHeader("Accept", "application/json");
      lastStatus = http.GET();
      if (lastStatus == HTTP_CODE_OK) payload = http.getString();
      http.end();
      if (lastStatus == HTTP_CODE_OK) return true;
    }
    if (!shouldRetryHomeAssistantRequest(lastStatus) ||
        attempt + 1 >= HOME_ASSISTANT_REQUEST_ATTEMPTS) {
      break;
    }
    delay(HOME_ASSISTANT_REQUEST_RETRY_DELAY_MS);
  }
  return false;
}

bool parseIso8601Timestamp(const String &value, time_t &timestamp) {
  if (value.length() < 19) return false;
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  if (sscanf(value.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day,
             &hour, &minute, &second) != 6) {
    return false;
  }
  const char *end = value.c_str() + 19;
  while ((*end >= '0' && *end <= '9') || *end == '.') ++end;
  long offsetSeconds = 0;
  if (*end == '+' || *end == '-') {
    const int direction = *end == '+' ? 1 : -1;
    int hours = 0;
    int minutes = 0;
    if (sscanf(end + 1, "%2d:%2d", &hours, &minutes) != 2) return false;
    offsetSeconds = direction * (hours * 3600L + minutes * 60L);
  } else if (*end != 'Z' && *end != '\0') {
    return false;
  }
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned dayOfYear =
      (153U * static_cast<unsigned>(month + (month > 2 ? -3 : 9)) + 2U) /
          5U +
      static_cast<unsigned>(day - 1);
  const unsigned dayOfEra =
      yearOfEra * 365U + yearOfEra / 4U - yearOfEra / 100U + dayOfYear;
  const int64_t daysSinceEpoch =
      static_cast<int64_t>(era) * 146097 + dayOfEra - 719468;
  timestamp = static_cast<time_t>(daysSinceEpoch * 86400 + hour * 3600 +
                                  minute * 60 + second - offsetSeconds);
  return timestamp > 0;
}

bool previousLocalDayTimestamp(time_t nextTimestamp,
                               time_t &previousTimestamp) {
  if (nextTimestamp <= 0) return false;
  struct tm localTransition;
  if (localtime_r(&nextTimestamp, &localTransition) == nullptr) return false;
  --localTransition.tm_mday;
  localTransition.tm_isdst = -1;
  previousTimestamp = mktime(&localTransition);
  return previousTimestamp > 0 && previousTimestamp < nextTimestamp;
}

bool applySunState(const ClockConfig &config, const String &payload,
                   const String &state, ClockValues &values) {
  values.sunStateAvailable = false;
  if (state != "above_horizon" && state != "below_horizon") return false;
  String sunriseText;
  String sunsetText;
  time_t sunrise = 0;
  time_t sunset = 0;
  if (extractJsonStringField(payload, "next_rising", sunriseText) &&
      parseIso8601Timestamp(sunriseText, sunrise)) {
    values.nextSunriseTimestamp = static_cast<uint64_t>(sunrise);
  }
  if (extractJsonStringField(payload, "next_setting", sunsetText) &&
      parseIso8601Timestamp(sunsetText, sunset)) {
    values.nextSunsetTimestamp = static_cast<uint64_t>(sunset);
  }
  const bool horizonIsDay = state == "above_horizon";
  String lastChangedText;
  time_t lastChanged = 0;
  time_t expectedCompletedTransition = 0;
  const time_t nextSameTransition = horizonIsDay ? sunrise : sunset;
  const time_t nextUpcomingTransition = horizonIsDay ? sunset : sunrise;
  const time_t now = time(nullptr);
  const bool lastChangedAvailable =
      extractJsonStringField(payload, "last_changed", lastChangedText) &&
      parseIso8601Timestamp(lastChangedText, lastChanged);
  const bool expectedCompletedTransitionAvailable = previousLocalDayTimestamp(
      nextSameTransition, expectedCompletedTransition);
  int64_t completedTransition = 0;
  const bool completedTransitionAvailable =
      clockSelectCompletedTransitionTimestamp(
          lastChangedAvailable, static_cast<int64_t>(lastChanged),
          expectedCompletedTransitionAvailable,
          static_cast<int64_t>(expectedCompletedTransition),
          completedTransition);
  const bool nextTransitionAvailable = nextUpcomingTransition > 0;
  const ClockSunDecision decision = clockEvaluateSunDecision(
      horizonIsDay, config.sunriseOffsetMinutes, config.sunsetOffsetMinutes,
      static_cast<int64_t>(now), completedTransitionAvailable,
      completedTransition, nextTransitionAvailable,
      static_cast<int64_t>(nextUpcomingTransition));
  if (decision == ClockSunDecision::Unavailable) return false;
  values.weatherIsDay = decision == ClockSunDecision::Day;
  values.sunStateAvailable = true;
  return true;
}

bool fetchHomeAssistantStates(NetworkClient &client, const ClockConfig &config,
                              ClockValues &values) {
  networkDiagnosticsBegin(NetworkDiagnosticKind::HomeAssistantRuntime);
  // Osm pevných entit plus entity zapnutých slotů. Duplicity se vynechají,
  // aby se stejný senzor nestahoval dvakrát.
  const char *entityIds[8 + CLOCK_VALUE_SLOT_COUNT] = {
      config.weatherEntityId,
      config.leftSide.temperatureEntityId,
      config.rightSide.temperatureEntityId,
      config.metricA.entityId,
      config.metricB.entityId,
      config.sunEntityId,
      config.dayNightLightEntityId,
      // Teplota do stavového řádku radaru. Leží až za entitou SUN, aby index
      // 5 níže dál ukazoval na ni.
      config.radarStatusTemperatureEntityId,
  };
  size_t entityCount = 8;
  for (size_t index = 0; index < CLOCK_VALUE_SLOT_COUNT; ++index) {
    const ClockValueSlotConfig &slot = clockConfigValueSlot(config, index);
    if (!slot.enabled || slot.entityId[0] == '\0') continue;
    bool alreadyListed = false;
    for (size_t listed = 0; listed < entityCount; ++listed) {
      if (strcmp(entityIds[listed], slot.entityId) == 0) {
        alreadyListed = true;
        break;
      }
    }
    if (!alreadyListed) entityIds[entityCount++] = slot.entityId;
  }
  values.sunStateAvailable = false;
  values.dayNightLightStateAvailable = false;
  // Bez vybrané entity nemá stavový řádek radaru odkud teplotu vzít; stará
  // hodnota by tam jinak visela dál a tvářila se jako aktuální.
  if (config.radarStatusTemperatureEntityId[0] == '\0')
    values.outsideTemperatureC = NAN;
  uint8_t configuredCount = 0;
  uint8_t successfulCount = 0;
  int lastStatus = 0;
  for (size_t index = 0; index < entityCount; ++index) {
    if (entityIds[index][0] == '\0') continue;
    ++configuredCount;
    // Zámek sítě se bere zvlášť pro každou entitu. Bez pauzy po jeho uvolnění
    // si ho tahle smyčka vezme zpátky dřív, než se probuzený čekatel vůbec
    // dostane ke slovu, a kontrola firmware nebo zprávy vyprší i po deseti
    // pokusech. Krátký spánek dá frontu čekatelů dopředu.
    if (configuredCount > 1) delay(HOME_ASSISTANT_ENTITY_YIELD_MS);
    String payload;
    if (!requestHomeAssistantState(client, config, entityIds[index], payload,
                                   lastStatus)) {
      continue;
    }
    String state;
    if (!extractJsonStringField(payload, "state", state)) continue;
    bool applied = false;
    if (index == 5) {
      applied = applySunState(config, payload, state, values);
    } else {
      applied = applyHomeAssistantState(config, entityIds[index], state, values);
    }
    if (applied) ++successfulCount;
  }
  const bool apiResponded = successfulCount > 0;
  String detail = String(successfulCount) + '/' + configuredCount +
                  F(" entit načteno");
  if (!apiResponded && lastStatus != 0) {
    detail += F(", poslední výsledek ");
    detail += lastStatus;
  }
  networkDiagnosticsSetDetail(NetworkDiagnosticKind::HomeAssistantRuntime,
                              detail);
  networkDiagnosticsEnd(NetworkDiagnosticKind::HomeAssistantRuntime,
                        apiResponded,
                        apiResponded ? HTTP_CODE_OK : lastStatus);
  return apiResponded;
}

bool fetchHomeAssistantStates(const ClockConfig &config, ClockValues &values) {
  if (config.homeAssistantUrl[0] == '\0' ||
      config.homeAssistantToken[0] == '\0') {
    return false;
  }
  if (String(config.homeAssistantUrl).startsWith("https://")) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(NETWORK_TLS_HANDSHAKE_TIMEOUT_S);
    return fetchHomeAssistantStates(client, config, values);
  }
  WiFiClient client;
  return fetchHomeAssistantStates(client, config, values);
}

bool fetchDayNightStates(NetworkClient &client, const ClockConfig &config,
                         ClockValues &values) {
  values.sunStateAvailable = false;
  values.dayNightLightStateAvailable = false;
  bool sunUpdated = false;
  bool lightUpdated = false;
  String payload;
  int status = 0;
  if (requestHomeAssistantState(client, config, config.sunEntityId, payload,
                                status)) {
    String state;
    if (extractJsonStringField(payload, "state", state)) {
      sunUpdated = applySunState(config, payload, state, values);
    }
  }
  payload = "";
  if (requestHomeAssistantState(client, config,
                                config.dayNightLightEntityId, payload,
                                status)) {
    String state;
    if (extractJsonStringField(payload, "state", state)) {
      lightUpdated = applyHomeAssistantState(
          config, config.dayNightLightEntityId, state, values);
    }
  }
  return sunUpdated || lightUpdated;
}

bool fetchDayNightStates(const ClockConfig &config, ClockValues &values) {
  if (config.homeAssistantUrl[0] == '\0' ||
      config.homeAssistantToken[0] == '\0') {
    return false;
  }
  if (String(config.homeAssistantUrl).startsWith("https://")) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(NETWORK_TLS_HANDSHAKE_TIMEOUT_S);
    return fetchDayNightStates(client, config, values);
  }
  WiFiClient client;
  return fetchDayNightStates(client, config, values);
}


bool stateAsFloat(const String &state, float &value) {
  char *end = nullptr;
  value = strtof(state.c_str(), &end);
  return end != state.c_str() && *end == '\0' && std::isfinite(value);
}

void publishHomeAssistantValues(const ClockValues &values) {
  portENTER_CRITICAL(&homeAssistantValuesMux);
  pendingHomeAssistantValues = values;
  homeAssistantUpdatePending = true;
  portEXIT_CRITICAL(&homeAssistantValuesMux);
}

int weatherCodeForState(const String &state) {
  char *end = nullptr;
  const long numericCode = strtol(state.c_str(), &end, 10);
  if (end != state.c_str() && *end == '\0') return numericCode;
  if (state == "sunny" || state == "clear-night") return 800;
  if (state == "partlycloudy") return 801;
  if (state == "cloudy") return 804;
  if (state == "fog") return 741;
  if (state == "rainy") return 500;
  if (state == "pouring") return 502;
  if (state == "lightning") return 200;
  if (state == "lightning-rainy") return 202;
  if (state == "exceptional") return 200;
  if (state == "snowy") return 600;
  if (state == "snowy-rainy") return 616;
  if (state == "hail") return 511;
  if (state == "windy" || state == "windy-variant") return 771;
  return -1;
}

// Agenda má vlastní úlohu ze stejného důvodu jako kanál zpráv: adresu zadává
// majitel, takže se ověřuje proti svazku kořenů Mozilly, a ten potřebuje
// výrazně víc zásobníku než jeden připnutý kořen.
// Vrací, za jak dlouho je další pokus, nebo 0, když se agenda nepoužívá.
unsigned long maintainAgendaFetch(const ClockAgendaConfig &config,
                                  const ClockAgendaCalendarsConfig &calendars,
                                  unsigned long &nextAgendaRefreshAt,
                                  char *lastFetchedUrl,
                                  size_t lastFetchedUrlSize,
                                  ClockAgendaCalendarsConfig &lastCalendars) {
  if (!(config.enabled && config.url[0] != '\0')) {
    if (lastFetchedUrl[0] != '\0') {
      // Agenda se vypnula nebo se jí vymazala adresa; staré události nesmí
      // zůstat na obrazovce.
      agendaServiceClear();
      lastFetchedUrl[0] = '\0';
    }
    nextAgendaRefreshAt = 0;
    return 0;
  }
  const bool calendarsChanged =
      lastCalendars.hiddenMask != calendars.hiddenMask ||
      strcmp(lastCalendars.privateKey, calendars.privateKey) != 0;
  if (strcmp(lastFetchedUrl, config.url) != 0 || calendarsChanged) {
    // Jiný server nebo jiný výběr kalendářů: zahodíme staré události a
    // stáhneme hned. U výběru je to důležité kvůli soukromému kalendáři -
    // po odebrání hesla nesmí jeho události zůstat na displeji do dalšího
    // úspěšného stažení, které se třeba vůbec nepovede.
    if (lastFetchedUrl[0] != '\0') agendaServiceClear();
    strlcpy(lastFetchedUrl, config.url, lastFetchedUrlSize);
    lastCalendars = calendars;
    nextAgendaRefreshAt = 0;
  }
  const unsigned long now = millis();
  if (nextAgendaRefreshAt != 0 &&
      static_cast<long>(now - nextAgendaRefreshAt) < 0) {
    return nextAgendaRefreshAt - now;
  }
  int httpStatus = 0;
  String error;
  const bool ok = agendaServiceFetch(config, calendars,
                                    NetworkDiagnosticKind::AgendaRuntime,
                                    httpStatus, error);
  const unsigned long interval =
      ok ? static_cast<unsigned long>(config.refreshMinutes) * 60UL * 1000UL
         : AGENDA_RETRY_MS;
  nextAgendaRefreshAt = millis() + interval;
  return interval;
}

// Provede zkoušku adresy, o kterou si řekl web server. Výsledek nesahá na
// mezipaměť obrazovky, takže zkoušená adresa nepřepíše zobrazené události.
void runPendingAgendaProbe() {
  int httpStatus = 0;
  String error;
  const bool ok =
      agendaServiceProbe(agendaProbeRequest.config,
                         agendaProbeRequest.calendars, httpStatus, error);
  agendaProbeRequest.httpStatus = httpStatus;
  agendaProbeRequest.ok = ok;
  strlcpy(agendaProbeRequest.error, error.c_str(),
          sizeof(agendaProbeRequest.error));
  // Pořadí je závazné: agendaProbePending pouští další žádost, takže se nuluje
  // až po zapsání celého výsledku.
  agendaProbeDone = true;
  agendaProbePending = false;
}

void agendaTask(void *) {
  unsigned long nextAgendaRefreshAt = 0;
  char lastAgendaUrl[CLOCK_AGENDA_URL_LENGTH] = "";
  ClockAgendaConfig config;
  ClockAgendaCalendarsConfig calendars;
  ClockAgendaCalendarsConfig lastCalendars;
  for (;;) {
    // Zkouška z webu má přednost, stejně jako u kanálu zpráv.
    if (agendaProbePending) {
      runPendingAgendaProbe();
      continue;
    }
    if (settingsSharePending) {
      settingsShareExecute(settingsShareRequest, settingsShareResult);
      // Stejné pořadí jako u zkoušky: pending pouští další žádost, takže se
      // nuluje až po zapsání výsledku.
      settingsShareDone = true;
      settingsSharePending = false;
      continue;
    }
    copyRuntimeAgendaConfig(config, calendars);
    if (WiFi.status() != WL_CONNECTED) {
      nextAgendaRefreshAt = 0;
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HOME_ASSISTANT_RETRY_MS));
      continue;
    }
    const unsigned long waitMs =
        maintainAgendaFetch(config, calendars, nextAgendaRefreshAt,
                            lastAgendaUrl, sizeof(lastAgendaUrl), lastCalendars);
    // Vypnutá agenda nemá kdy pokračovat sama; probudí ji až uložení nastavení.
    if (ulTaskNotifyTake(pdTRUE, waitMs == 0 ? portMAX_DELAY
                                             : pdMS_TO_TICKS(waitMs)) > 0) {
      // Probuzení kvůli zkoušce nesmí zahodit naplánované stažení.
      if (!agendaProbePending && !settingsSharePending) nextAgendaRefreshAt = 0;
    }
  }
}

// Škola má vlastní úlohu ze stejného důvodu jako agenda: adresu zadává
// majitel, ověřuje se proti svazku kořenů Mozilly a zásobník leží v PSRAM.
// Vrací, za jak dlouho je další pokus, nebo 0, když se škola nepoužívá.
unsigned long maintainSchoolFetch(const ClockSchoolConfig &config,
                                  unsigned long &nextRefreshAt,
                                  char *lastFetchedUrl,
                                  size_t lastFetchedUrlSize) {
  if (!(config.enabled && config.url[0] != '\0')) {
    if (lastFetchedUrl[0] != '\0') {
      // Obrazovka se vypnula nebo přišla o adresu; rozvrh nesmí zůstat.
      schoolServiceClear();
      lastFetchedUrl[0] = '\0';
    }
    nextRefreshAt = 0;
    return 0;
  }
  if (strcmp(lastFetchedUrl, config.url) != 0) {
    // Jiný server: rozvrh z toho původního pryč a stáhnout hned.
    if (lastFetchedUrl[0] != '\0') schoolServiceClear();
    strlcpy(lastFetchedUrl, config.url, lastFetchedUrlSize);
    nextRefreshAt = 0;
  }
  const unsigned long now = millis();
  if (nextRefreshAt != 0 && static_cast<long>(now - nextRefreshAt) < 0) {
    return nextRefreshAt - now;
  }
  int httpStatus = 0;
  String error;
  const bool ok = schoolServiceFetch(
      config, NetworkDiagnosticKind::SchoolRuntime, httpStatus, error);
  const unsigned long interval =
      ok ? static_cast<unsigned long>(config.refreshMinutes) * 60UL * 1000UL
         : SCHOOL_RETRY_MS;
  nextRefreshAt = millis() + interval;
  return interval;
}

void schoolTask(void *) {
  unsigned long nextRefreshAt = 0;
  char lastUrl[CLOCK_SCHOOL_URL_LENGTH] = "";
  ClockSchoolConfig config;
  for (;;) {
    if (schoolProbePending) {
      int httpStatus = 0;
      String error;
      const bool ok =
          schoolServiceProbe(schoolProbeRequest.config, httpStatus, error);
      schoolProbeRequest.httpStatus = httpStatus;
      schoolProbeRequest.ok = ok;
      strlcpy(schoolProbeRequest.error, error.c_str(),
              sizeof(schoolProbeRequest.error));
      // Pending pouští další žádost, takže se nuluje až po zapsání výsledku.
      schoolProbeDone = true;
      schoolProbePending = false;
      continue;
    }
    copyRuntimeSchoolConfig(config);
    if (WiFi.status() != WL_CONNECTED) {
      nextRefreshAt = 0;
      // Bez sítě se nestahuje, takže by stáří rozvrhu nikdo nehlídal.
      schoolServiceExpireStale();
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HOME_ASSISTANT_RETRY_MS));
      continue;
    }
    const unsigned long waitMs =
        maintainSchoolFetch(config, nextRefreshAt, lastUrl, sizeof(lastUrl));
    if (ulTaskNotifyTake(pdTRUE, waitMs == 0 ? portMAX_DELAY
                                             : pdMS_TO_TICKS(waitMs)) > 0) {
      // Probuzení kvůli zkoušce nesmí zahodit naplánované stažení.
      if (!schoolProbePending) nextRefreshAt = 0;
    }
  }
}

// Kanál zpráv má vlastní úlohu. Do úlohy home-assistant se nevešel: její
// zásobník je vyměřený na TLS s jedním připnutým kořenem a na parsování JSON,
// kdežto ověření proti svazku kořenů Mozilly potřebuje výrazně víc.
// Vrací, za jak dlouho je další pokus, nebo 0, když se kanál nepoužívá.
bool sameRssLocation(const RssLocation &left, const RssLocation &right) {
  return strcmp(left.city, right.city) == 0 &&
         left.latitude == right.latitude && left.longitude == right.longitude;
}

unsigned long maintainRssFetch(const ClockRssConfig &config,
                               const RssLocation &location,
                               unsigned long &nextRssRefreshAt,
                               char *lastFetchedUrl, size_t lastFetchedUrlSize,
                               RssLocation &lastLocation) {
  if (!(config.enabled && config.url[0] != '\0')) {
    if (lastFetchedUrl[0] != '\0') {
      // Kanál se vypnul nebo se mu vymazala adresa; staré zprávy nesmí zůstat.
      rssServiceClear();
      lastFetchedUrl[0] = '\0';
    }
    nextRssRefreshAt = 0;
    return 0;
  }
  // Nová poloha se počítá jako jiný zdroj jen u adresy, která ji nese; jinak
  // by přesun hodin zbytečně smazal a znovu stáhl tentýž kanál.
  const bool usesLocation = strchr(config.url, '{') != nullptr;
  if (strcmp(lastFetchedUrl, config.url) != 0 ||
      (usesLocation && !sameRssLocation(lastLocation, location))) {
    // Jiný zdroj: zahodíme zprávy z toho původního a stáhneme hned.
    if (lastFetchedUrl[0] != '\0') rssServiceClear();
    strlcpy(lastFetchedUrl, config.url, lastFetchedUrlSize);
    lastLocation = location;
    nextRssRefreshAt = 0;
  }
  const unsigned long now = millis();
  if (nextRssRefreshAt != 0 &&
      static_cast<long>(now - nextRssRefreshAt) < 0) {
    return nextRssRefreshAt - now;
  }
  int httpStatus = 0;
  String error;
  const bool ok = rssServiceFetch(config, location,
                                  NetworkDiagnosticKind::RssRuntime, httpStatus,
                                  error);
  const unsigned long interval =
      ok ? static_cast<unsigned long>(config.refreshMinutes) * 60UL * 1000UL
         : RSS_RETRY_MS;
  nextRssRefreshAt = millis() + interval;
  return interval;
}

// Provede zkoušku kanálu, o kterou si řekl web server. Výsledek nesahá na
// mezipaměť obrazovky, takže zkoušená adresa nepřepíše zobrazené zprávy.
void runPendingRssProbe() {
  int httpStatus = 0;
  String error;
  const bool ok = rssServiceProbe(rssProbeRequest.config,
                                  rssProbeRequest.location, httpStatus, error);
  rssProbeRequest.httpStatus = httpStatus;
  rssProbeRequest.ok = ok;
  strlcpy(rssProbeRequest.error, error.c_str(),
          sizeof(rssProbeRequest.error));
  // Pořadí je závazné: rssProbePending pouští další žádost, takže se nuluje
  // až po zapsání celého výsledku.
  rssProbeDone = true;
  rssProbePending = false;
}

void rssTask(void *) {
  unsigned long nextRssRefreshAt = 0;
  char lastRssUrl[CLOCK_RSS_URL_LENGTH] = "";
  RssLocation lastRssLocation;
  ClockRssConfig config;
  RssLocation location;
  for (;;) {
    // Zkouška z webu má přednost. Běží tady právě proto, že ověření proti
    // svazku kořenů Mozilly se do zásobníku smyčky displeje nevejde.
    if (rssProbePending) {
      runPendingRssProbe();
      continue;
    }
    copyRuntimeRssConfig(&config, location);
    if (WiFi.status() != WL_CONNECTED) {
      nextRssRefreshAt = 0;
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HOME_ASSISTANT_RETRY_MS));
      continue;
    }
    const unsigned long waitMs =
        maintainRssFetch(config, location, nextRssRefreshAt, lastRssUrl,
                         sizeof(lastRssUrl), lastRssLocation);
    // Vypnutý kanál nemá kdy pokračovat sám; probudí ho až uložení nastavení.
    if (ulTaskNotifyTake(pdTRUE, waitMs == 0 ? portMAX_DELAY
                                             : pdMS_TO_TICKS(waitMs)) > 0) {
      // Probuzení kvůli zkoušce nesmí zahodit naplánované stažení: zkouška
      // kanálu není jeho obnovením, takže deadline platí dál.
      if (!rssProbePending) nextRssRefreshAt = 0;
    }
  }
}

// Vrací, za jak dlouho je další pokus, nebo 0, když se předpověď nepoužívá.
unsigned long maintainForecastFetch(const ForecastTaskConfig &config,
                                    unsigned long &nextRefreshAt,
                                    bool &cacheHolds) {
  if (!config.forecast.enabled) {
    if (cacheHolds) {
      // Obrazovka se vypnula; stará předpověď nesmí zůstat v paměti.
      weatherForecastServiceClear();
      cacheHolds = false;
    }
    nextRefreshAt = 0;
    return 0;
  }
  const unsigned long now = millis();
  if (nextRefreshAt != 0 && static_cast<long>(now - nextRefreshAt) < 0) {
    return nextRefreshAt - now;
  }
  const bool ok = weatherForecastServiceFetch(
      config.latitude, config.longitude, config.forecast.airQuality,
      NetworkDiagnosticKind::ForecastRuntime);
  if (ok) cacheHolds = true;
  const unsigned long interval =
      ok ? static_cast<unsigned long>(config.forecast.refreshMinutes) * 60UL *
               1000UL
         : FORECAST_RETRY_MS;
  nextRefreshAt = millis() + interval;
  return interval;
}

void forecastTask(void *) {
  unsigned long nextRefreshAt = 0;
  bool cacheHolds = false;
  ForecastTaskConfig config;
  float lastLatitude = NAN;
  float lastLongitude = NAN;
  for (;;) {
    copyRuntimeForecastConfig(config);
    // Bez času ze sítě by TLS odmítlo každý certifikát jako "ještě neplatný" a
    // předpověď by se navíc neměla podle čeho oříznout na hodiny od současné
    // dál. Čekání na NTP je tedy levnější než pokus, který nemůže vyjít.
    if (WiFi.status() != WL_CONNECTED || time(nullptr) < VALID_TIME_THRESHOLD) {
      nextRefreshAt = 0;
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HOME_ASSISTANT_RETRY_MS));
      continue;
    }
    // Jiné město znamená jinou předpověď; ta stará se nesmí dokreslit vedle
    // nové hlavičky.
    if (config.latitude != lastLatitude || config.longitude != lastLongitude) {
      if (cacheHolds) {
        weatherForecastServiceClear();
        cacheHolds = false;
      }
      lastLatitude = config.latitude;
      lastLongitude = config.longitude;
      nextRefreshAt = 0;
    }
    const unsigned long waitMs =
        maintainForecastFetch(config, nextRefreshAt, cacheHolds);
    // Vypnutá obrazovka nemá kdy pokračovat sama; probudí ji až uložení
    // nastavení.
    if (ulTaskNotifyTake(pdTRUE, waitMs == 0 ? portMAX_DELAY
                                             : pdMS_TO_TICKS(waitMs)) > 0) {
      nextRefreshAt = 0;
    }
  }
}

void homeAssistantTask(void *) {
  ClockValues lastAvailableValues;
  unsigned long nextOpenMeteoRefreshAt = 0;
  unsigned long nextTmepRefreshAt = 0;
  bool tmepCatalogPrimed = false;
  for (;;) {
    const ClockConfig &config = homeAssistantConfigSnapshot();
    ClockValues values = lastAvailableValues;
    if (WiFi.status() != WL_CONNECTED) {
      nextOpenMeteoRefreshAt = 0;
      nextTmepRefreshAt = 0;
      tmepCatalogPrimed = false;
      publishHomeAssistantValues(ClockValues{});
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HOME_ASSISTANT_RETRY_MS));
      continue;
    }

    if (config.dataSource == CLOCK_DATA_SOURCE_OPEN_METEO) {
      const auto deadlineReached = [](unsigned long now,
                                      unsigned long deadline) {
        return deadline == 0 || static_cast<long>(now - deadline) >= 0;
      };
      unsigned long now = millis();
      bool valuesUpdated = false;
      if (deadlineReached(now, nextOpenMeteoRefreshAt)) {
        const bool responded = fetchOpenMeteo(config, values);
        nextOpenMeteoRefreshAt =
            millis() +
            (responded ? OPEN_METEO_REFRESH_MS : EXTERNAL_DATA_RETRY_MS);
        valuesUpdated = true;
      }

      const bool tmepEnabled = tmepSlotsEnabled(config);
      const bool tmepConfigured = config.tmepExportId[0] != '\0' &&
                                  config.tmepExportKey[0] != '\0';
      now = millis();
      const bool tmepRefreshDue =
          tmepConfigured && deadlineReached(now, nextTmepRefreshAt) &&
          (tmepEnabled || !tmepCatalogPrimed);
      if (tmepRefreshDue) {
        const bool responded = fetchTmepValues(config, values);
        tmepCatalogPrimed = responded;
        nextTmepRefreshAt =
            millis() + (responded ? TMEP_REFRESH_MS : EXTERNAL_DATA_RETRY_MS);
        valuesUpdated = tmepEnabled;
      } else if (!tmepConfigured) {
        nextTmepRefreshAt = 0;
        tmepCatalogPrimed = false;
      }

      if (valuesUpdated) {
        lastAvailableValues = values;
        publishHomeAssistantValues(values);
      }

      now = millis();
      unsigned long waitMs =
          deadlineReached(now, nextOpenMeteoRefreshAt)
              ? 0
              : nextOpenMeteoRefreshAt - now;
      if (tmepConfigured && (tmepEnabled || !tmepCatalogPrimed)) {
        const unsigned long tmepWaitMs =
            deadlineReached(now, nextTmepRefreshAt)
                ? 0
                : nextTmepRefreshAt - now;
        if (tmepWaitMs < waitMs) waitMs = tmepWaitMs;
      }
      if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(waitMs)) > 0) {
        nextOpenMeteoRefreshAt = 0;
        nextTmepRefreshAt = 0;
        tmepCatalogPrimed = false;
      }
      continue;
    }

    nextOpenMeteoRefreshAt = 0;
    nextTmepRefreshAt = 0;
    tmepCatalogPrimed = false;

    if (config.homeAssistantUrl[0] == '\0' ||
        config.homeAssistantToken[0] == '\0') {
      publishHomeAssistantValues(ClockValues{});
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HOME_ASSISTANT_RETRY_MS));
      continue;
    }

    const bool apiResponded = fetchHomeAssistantStates(config, values);
    values.homeAssistantOnline = apiResponded;
    if (apiResponded) {
      lastAvailableValues = values;
    }
    publishHomeAssistantValues(values);
    if (!apiResponded) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HOME_ASSISTANT_RETRY_MS));
      continue;
    }

    const unsigned long fullRefreshAt = millis() + HOME_ASSISTANT_REFRESH_MS;
    while (static_cast<long>(millis() - fullRefreshAt) < 0) {
      const unsigned long remaining = fullRefreshAt - millis();
      if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(remaining)) == 0) break;
      if (!consumeDayNightLightRefreshRequest()) break;

      static ClockConfig &lightConfig = clockConfigAllocate();
      static ClockValues lightValues;
      copyRuntimeConfig(lightConfig);
      lightValues = lastAvailableValues;
      fetchDayNightStates(lightConfig, lightValues);
      lastAvailableValues.weatherIsDay = lightValues.weatherIsDay;
      lastAvailableValues.sunStateAvailable = lightValues.sunStateAvailable;
      lastAvailableValues.dayNightLightStateAvailable =
          lightValues.dayNightLightStateAvailable;
      lastAvailableValues.dayNightLightOn = lightValues.dayNightLightOn;
      publishHomeAssistantValues(lightValues);
    }
  }
}

void applyPendingHomeAssistantValues() {
  if (!homeAssistantUpdatePending) return;
  ClockValues values;
  portENTER_CRITICAL(&homeAssistantValuesMux);
  values = pendingHomeAssistantValues;
  homeAssistantUpdatePending = false;
  portEXIT_CRITICAL(&homeAssistantValuesMux);
#if !FIRMWARE_RELEASE
  if (forceNightTestActive) {
    values.weatherIsDay = false;
    values.sunStateAvailable = true;
  }
#endif
  sampleValues = values;
#if !FIRMWARE_RELEASE
  if (!forceNightTestActive)
#endif
    applyLocalSunState(loopConfigSnapshot(), sampleValues);
  clockDashboardUpdate(sampleValues);
}
}  // namespace

void setup() {
  Serial.setTxBufferSize(4096);
  Serial.begin(FIRMWARE_RELEASE ? 115200 : 921600);
  delay(300);
#if !FIRMWARE_RELEASE
  Serial.println("Waveshare Hodiny startuji");
#endif
  // Dřív než cokoli otevře první TLS spojení: handshaky pak berou paměť
  // z PSRAM a vnitřní SRAM nerozdrobí.
  tlsMemoryBegin();

  I2C_Init();
  Set_EXIOS(0x0C);
  TCA9554PWR_Init(0x70);
  runtimeConfigMutex = xSemaphoreCreateMutex();
  // Případná migrace konfigurace zapisuje do flash. Proveď ji dříve, než
  // spustíme RGB panel nad framebufferem v PSRAM, jinak může první start po
  // OTA rozhodit řádkovou synchronizaci displeje.
  if (!clockConfigBegin() || !clockConfigLoad(persistedConfig)) {
    clockConfigApplyDefaults(persistedConfig);
#if !FIRMWARE_RELEASE
    Serial.println("Konfiguracni pamet se nepodarilo nacist");
#endif
  }
  uint32_t legacyWeatherIconColor = persistedConfig.leftWeatherIconColor;
  if (persistedConfig.dataSource == CLOCK_DATA_SOURCE_OPEN_METEO) {
    legacyWeatherIconColor = persistedConfig.openMeteoSlots[0].color;
  } else if (strcmp(persistedConfig.leftSide.icon, "weather") != 0 &&
             strcmp(persistedConfig.rightSide.icon, "weather") == 0) {
    legacyWeatherIconColor = persistedConfig.rightWeatherIconColor;
  }
  clockAppearanceLoad(persistedAppearance, legacyWeatherIconColor,
                      persistedConfig.dateFormat, persistedConfig.dateColor);
  activeAppearance = persistedAppearance;
  runtimeConfig = persistedConfig;
  applyDevelopmentDefaults(runtimeConfig);
  networkCoordinatorBegin();
  tmepServiceBegin();
  rssServiceBegin();
  agendaServiceBegin();
  schoolServiceBegin();
  weatherForecastServiceBegin();
  LCD_Init();
  currentDisplayBrightness = runtimeConfig.dayBrightness;
  Set_Backlight(currentDisplayBrightness);
  displayDriverInit();
  deviceNameLoad(networkDeviceName, sizeof(networkDeviceName));
  // Jméno pro DHCP se předává při startu rozhraní, proto před Wi-Fi.
  WiFi.setHostname(networkDeviceName);
#if FIRMWARE_RELEASE
  improvSerialServiceInit(wifiProvisioningStart);
#endif
  wifiProvisioningBegin();
  // Release bez dosažitelné sítě zůstane v nastavovacím portálu (QR kód,
  // přístupový bod) a po zadání sítě se restartuje. Vývojové buildy pokračují.
  wifiOnboardingRequireConnection();
  clockDashboardApplyAppearance(activeAppearance);
  clockDashboardInit(sampleValues, runtimeConfig.dayBrightness,
                       runtimeConfig.nightBrightness,
                       runtimeConfig.automaticDayNight,
                       handleBrightnessPreview, handleSettingsOpen,
                       handleSettingsSave, handleSettingsFirmwareCheck,
                       handleSettingsFirmwareInstall, handleRadarVisibility,
                       handleRadarRangeChange, handleRssVisibility,
                       handleForecastVisibility);
  clockDashboardSetPlanesVisibilityCallback(handlePlanesVisibility);
  clockDashboardSetAgendaVisibilityCallback(handleAgendaVisibility);
  clockDashboardSetSchoolVisibilityCallback(handleSchoolVisibility);
  clockDashboardSetSatellitesVisibilityCallback(handleSatellitesVisibility);
  clockDashboardSetWebPasswordResetCallback(configurationWebClearPassword);
  clockDashboardApplyConfiguration(runtimeConfig);
  chmiRadarServiceBegin();
  planeRadarServiceBegin();
  satelliteServiceBegin();
  lightningServiceBegin();
  rainAlertServiceBegin();
  applyRainAlertState(runtimeConfig);
  chmiRadarServiceSetActive(
      false, false,
      runtimeConfig.openMeteoLatitude, runtimeConfig.openMeteoLongitude,
      runtimeConfig.radarRadiusKm, runtimeConfig.radarFrameCount,
      runtimeConfig.radarMapOpacity, runtimeConfig.radarPauseSeconds,
      runtimeConfig.radarLegend, runtimeConfig.radarPrecipitation,
      runtimeConfig.radarSource);
  // Poloha a nastavení se službě předají hned; stahovat začne až po
  // synchronizaci času, kdy applyPlaneRadarState() doplní skutečný stav.
  planeRadarServiceSetActive(false, false, runtimeConfig.openMeteoLatitude,
                             runtimeConfig.openMeteoLongitude,
                             runtimeConfig.planes,
                             runtimeConfig.planesFeedUrl,
                             runtimeConfig.planesMapLabel);
  // Stejně jako letadla: nastavení hned, stahovat se začne po synchronizaci času.
  satelliteServiceSetActive(false, false, runtimeConfig.openMeteoLatitude,
                            runtimeConfig.openMeteoLongitude,
                            runtimeConfig.satellites,
                            runtimeConfig.language == CLOCK_LANGUAGE_ENGLISH);
  clockDashboardSetSecond(60);
  displayResyncAt = millis() + 2000;
  initializeNetworkTime();
  // Zásobník musí pokrýt kopii ClockConfig (schéma 29 má přes 5 kB) i TLS
  // a parsování JSON ve fetch* funkcích. Při 12288 B kanárek přetekl.
  xTaskCreatePinnedToCoreWithCaps(
      homeAssistantTask, "home-assistant", 20480, nullptr, 1,
      &homeAssistantTaskHandle, 0,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  configurationWebSetHomeAssistantTask(homeAssistantTaskHandle);
  // Ověření proti svazku kořenů Mozilly projde při handshaku stovky
  // certifikátů a je na zásobník výrazně náročnější než jeden připnutý kořen,
  // který používají ostatní služby. Rezervu hlídá diagnostika: "Zásobník
  // zpráv" ukazuje, kolik úloze nejméně zbývalo.
  xTaskCreatePinnedToCoreWithCaps(
      rssTask, "rss", 20480, nullptr, 1, &rssTaskHandle, 0,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  configurationWebSetRssTask(rssTaskHandle);
  configurationWebSetRssProbe(runRssProbeFromWeb);
  // Agenda se ověřuje proti svazku kořenů Mozilly, takže její handshake stojí
  // stejně zásobníku jako u kanálu zpráv.
  xTaskCreatePinnedToCoreWithCaps(
      agendaTask, "agenda", 20480, nullptr, 1, &agendaTaskHandle, 0,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  configurationWebSetAgendaTask(agendaTaskHandle);
  configurationWebSetAgendaProbe(runAgendaProbeFromWeb);
  configurationWebSetSettingsShare(runSettingsShareFromWeb);
  // Rozvrh se ověřuje proti svazku kořenů Mozilly stejně jako agenda.
  xTaskCreatePinnedToCoreWithCaps(
      schoolTask, "school", 20480, nullptr, 1, &schoolTaskHandle, 0,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  configurationWebSetSchoolTask(schoolTaskHandle);
  configurationWebSetSchoolProbe(runSchoolProbeFromWeb);
  configurationWebSetSatellitesProbe(runSatellitesProbeFromWeb);
  configurationWebSetBackgroundWork(runBackgroundWorkFromWeb);
  configurationWebSetDeviceNameChanged(handleDeviceNameChanged);
  configurationWebSetScreenControl(showScreenFromWeb, currentScreenForWeb);
  // Předpověď se ověřuje proti svazku kořenů Mozilly, takže její handshake
  // stojí stejně zásobníku jako u kanálu zpráv.
  xTaskCreatePinnedToCoreWithCaps(
      forecastTask, "forecast", 20480, nullptr, 1, &forecastTaskHandle, 0,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  firmwareUpdateServiceBegin(handleFirmwareUpdateLifecycle);
  maintainFirmwareDisplayStatus();
  configurationWebBegin(loadRuntimeConfigForWeb, saveRuntimeConfig,
                        handleConfigurationWebStatus,
                        loadSunTransitionTimesForWeb,
                        requestHomeAssistantRefreshFromWeb,
                        loadDayNightStatusForWeb, handleDisplayPower,
                        displayPowerForcedOff, loadRadarRangeStateForWeb,
                        previewRadarRangeFromWeb, loadClockAppearanceForWeb,
                        previewClockAppearanceFromWeb,
                        saveClockAppearanceFromWeb);
  clockDashboardSetWebMode(configurationWebMode());

  const esp_task_wdt_config_t watchdogConfig = {
      .timeout_ms = LOOP_WATCHDOG_TIMEOUT_MS,
      .idle_core_mask = 0,
      .trigger_panic = true,
  };
  if (esp_task_wdt_reconfigure(&watchdogConfig) == ESP_OK) enableLoopWDT();
}

void loop() {
#if FIRMWARE_RELEASE
  improvSerialServiceLoop();
#else
  handleUsbCommands();
#endif
  wifiProvisioningLoop();
  applyFirmwareUpdateDisplayRequest();
  maintainNetworkTime();
  maintainAutomaticFirmwareUpdate();
  maintainFirmwareDisplayStatus();
  applyPendingHomeAssistantValues();
  const ClockConfig &animationConfig = loopConfigSnapshot();
  const uint8_t weatherIconStyle =
      clockDashboardWeatherIconStyle(animationConfig.weatherIconStyle);
  weatherAnimationServiceLoop(sampleValues.weatherCode,
                              sampleValues.weatherIsDay,
                              weatherIconStyle,
                              !clockDashboardRadarVisible() &&
                                  animationConfig.animatedWeatherIcons &&
                                  (activeAppearance.style ==
                                       CLOCK_STYLE_ANALOG ||
                                   animationConfig.dataSource ==
                                       CLOCK_DATA_SOURCE_OPEN_METEO ||
                                   strcmp(animationConfig.leftSide.icon,
                                          "weather") == 0 ||
                                   strcmp(animationConfig.rightSide.icon,
                                          "weather") == 0));
  configurationWebLoop();
  applyPendingRuntimeConfiguration();
  applyPendingClockAppearance();
  maintainDisplayGestures();
  maintainRadarNightVisual();
  maintainRadarRangeChange();
  maintainLightning();
  maintainLocalSunState();
  maintainSkyData();
  maintainRadarDisplay();
  maintainPlanesDisplay();
  maintainSatellitesDisplay();
  maintainRssDisplay();
  maintainAgendaDisplay();
  maintainSchoolDisplay();
  maintainForecastDisplay();
  maintainScreenSchedule();
  maintainRainAlert();
  maintainAutomaticScreenRotation();
  // Během DEV screenshotu už přenášíme neměnnou kopii framebufferu. Dočasné
  // pozastavení LVGL timerů zabrání tomu, aby GIF dekodér soupeřil s USB CDC;
  // po dokončení přenosu se animace plynule rozběhne od dalšího snímku.
  if (!screenshotTransferActive) {
    clockDashboardLoop();
    displayDriverLoop();
  }
  if (firmwareUpdateDisplayRequested && firmwareUpdateDisplayActive) {
    firmwareUpdateDisplayPresented = true;
  }
  if (firmwareUpdateCountdownStarted && !firmwareUpdateBlackPresented) {
    const unsigned long elapsed = millis() - firmwareUpdateCountdownStartedAt;
    if (elapsed < 5000) {
      const uint8_t seconds = 5 - static_cast<uint8_t>(elapsed / 1000);
      if (seconds != firmwareUpdateCountdownDisplayed) {
        firmwareUpdateCountdownDisplayed = seconds;
        clockDashboardSetFirmwareUpdateCountdown(seconds);
      }
    } else {
      firmwareUpdateBlackRequested = true;
      firmwareUpdateCountdownStarted = false;
    }
  }
  if (firmwareUpdateBlackRequested && !firmwareUpdateBlackPresented) {
    clockDashboardSetFirmwareUpdateBlack(true);
    displayDriverLoop();
    firmwareUpdateBlackPresented = true;
  }
#if !FIRMWARE_RELEASE
  streamScreenshot();
#endif
  maintainDisplaySync();
  delay(5);
}
