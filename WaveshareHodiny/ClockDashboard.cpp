#include "AgendaParser.h"
#include "ClockDashboard.h"
#include "OpenWeatherIcons.h"
#include "WeatherIconMapping.h"

#include <lvgl.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <esp_heap_caps.h>

#include "ClockFonts.h"
#include "ClockLvglMemory.h"
#include "WeatherForecastLayout.h"
#include "DisplayDriver.h"
#include "FirmwareUpdateService.h"

namespace {
const lv_color_t COLOR_BACKGROUND = LV_COLOR_MAKE(0, 0, 0);
const lv_color_t COLOR_TEXT = LV_COLOR_MAKE(246, 246, 246);
const lv_color_t COLOR_MUTED = LV_COLOR_MAKE(181, 181, 181);
const lv_color_t COLOR_DIVIDER = LV_COLOR_MAKE(47, 47, 47);
const lv_color_t COLOR_OUTSIDE = LV_COLOR_MAKE(76, 203, 236);
const lv_color_t COLOR_ROOM = LV_COLOR_MAKE(255, 184, 67);
const lv_color_t COLOR_AIR = LV_COLOR_MAKE(101, 199, 68);
const lv_color_t COLOR_HUMIDITY = LV_COLOR_MAKE(63, 151, 219);
const lv_color_t COLOR_ERROR = LV_COLOR_MAKE(255, 72, 72);
constexpr int SECOND_DOT_COUNT = 60;
constexpr float SECOND_RING_RADIUS = 226.0f;
constexpr float PI_VALUE = 3.14159265358979323846f;
constexpr unsigned long SECOND_DOT_FADE_TOTAL_MS = 2000;
constexpr unsigned long SECOND_LINE_FADE_TOTAL_MS = 4000;
constexpr unsigned long SECOND_FADE_DOT_MS = 200;
constexpr unsigned long SECOND_FADE_START_SPAN_MS =
    SECOND_DOT_FADE_TOTAL_MS - SECOND_FADE_DOT_MS;
// Plynulé efekty držíme pod fyzickou obnovovací frekvencí panelu, aby se do
// jednoho snímku zbytečně neposílalo více různých stavů.
constexpr unsigned long SMOOTH_EFFECT_FRAME_MS = 40;
constexpr float SECOND_COMET_TRAIL_SECONDS = 12.0f;
constexpr unsigned long WEATHER_ANIMATION_REVEAL_DELAY_MS = 100;
constexpr float ANALOG_PI = 3.14159265358979323846f;
constexpr int ANALOG_CENTER_X = 240;
constexpr int ANALOG_CENTER_Y = 240;
constexpr int ANALOG_RING_RADIUS = 239;
// Jediný osový obdélník kolem šikmé ručičky může pokrýt desítky tisíc
// pixelů. Pevné pásy omezí překreslení na okolí skutečného tahu a současně
// udrží i starou a novou polohu všech ručiček pod 32 dirty oblastmi LVGL.
constexpr lv_coord_t ANALOG_HAND_INVALIDATION_STRIP = 96;
uint8_t activeClockStyle = CLOCK_STYLE_DIGITAL;
uint32_t analogToneColor = 0x00D6FF;
uint32_t analogHandToneColor = 0x00D6FF;
uint32_t analogCardinalAccentColor = 0xFFAB00;
bool analogCardinalAccentsEnabled = true;
bool analogOutlineHandsEnabled = false;
bool analogMonochromeValuesEnabled = false;
bool analogValuesAboveHandsEnabled = false;
uint32_t analogDateColor = 0xB5B5B5;
uint32_t monochromeWeatherIconColor = 0xFFFFFF;
ClockConfig &dashboardRuntimeConfig = clockConfigAllocate();
bool dashboardRuntimeConfigAvailable = false;

bool analogLayoutEnabled() { return activeClockStyle == CLOCK_STYLE_ANALOG; }

bool valuesLayoutEnabled() { return activeClockStyle == CLOCK_STYLE_VALUES; }

lv_obj_t *timeLabel = nullptr;
lv_obj_t *dateLabel = nullptr;
lv_obj_t *namedayLabel = nullptr;
lv_obj_t *outsideTitleLabel = nullptr;
lv_obj_t *outsideIntegerLabel = nullptr;
lv_obj_t *outsideDecimalLabel = nullptr;
lv_obj_t *outsideUnitLabel = nullptr;
lv_obj_t *roomTitleLabel = nullptr;
lv_obj_t *roomIntegerLabel = nullptr;
lv_obj_t *roomDecimalLabel = nullptr;
lv_obj_t *roomUnitLabel = nullptr;
lv_obj_t *outsideIconLabel = nullptr;
lv_obj_t *roomIconLabel = nullptr;
lv_obj_t *co2TitleLabel = nullptr;
lv_obj_t *co2ValueLabel = nullptr;
lv_obj_t *co2UnitLabel = nullptr;
lv_obj_t *humidityTitleLabel = nullptr;
lv_obj_t *humidityValueLabel = nullptr;
lv_obj_t *humidityUnitLabel = nullptr;
lv_obj_t *weatherImage = nullptr;
lv_obj_t *roomWeatherImage = nullptr;
lv_obj_t *weatherAnimation = nullptr;
lv_obj_t *roomWeatherAnimation = nullptr;
lv_obj_t *wifiStatusLabel = nullptr;
lv_obj_t *statusLabel = nullptr;
lv_obj_t *webStatusLabel = nullptr;
lv_obj_t *dashboardContent = nullptr;
lv_obj_t *valuesPage = nullptr;
lv_obj_t *valuesTimeLabel = nullptr;
lv_obj_t *valuesDateLabel = nullptr;
lv_obj_t *valuesNamedayLabel = nullptr;
lv_obj_t *valueSlotTitleLabels[CLOCK_VALUE_SLOT_COUNT] = {};
lv_obj_t *valueSlotValueLabels[CLOCK_VALUE_SLOT_COUNT] = {};
// Kořen obrazovky se drží kvůli stránce předpovědi, která se zakládá až při
// prvním zapnutí - do té doby nestojí ani jeden objekt LVGL.
lv_obj_t *dashboardScreen = nullptr;
lv_obj_t *forecastPage = nullptr;
lv_obj_t *planesPage = nullptr;
lv_obj_t *planesCanvas = nullptr;
lv_obj_t *planesClockLabel = nullptr;
lv_obj_t *planesStatusLabel = nullptr;
lv_obj_t *planesRangeLabel = nullptr;
// Detail vybraného letadla. Panel je LVGL, ne kresba do bufferu: potřebuje
// pořádné písmo, malá písmena i diakritiku, což drobné mapové písmo neumí.
lv_obj_t *planesDetailPanel = nullptr;
lv_obj_t *planesDetailTitle = nullptr;
lv_obj_t *planesDetailClose = nullptr;
constexpr uint8_t PLANES_DETAIL_ROW_COUNT = 6;
// Řádek s typem draku. Hned pod ním leží název typu vypsaný slovy, takže se
// řádky za ním posunou o jeho výšku níž.
constexpr uint8_t PLANES_DETAIL_TYPE_ROW = 4;
lv_obj_t *planesDetailRows[PLANES_DETAIL_ROW_COUNT] = {};
lv_obj_t *planesDetailTypeName = nullptr;
lv_obj_t *planesDetailRouteFrom = nullptr;
lv_obj_t *planesDetailRouteTo = nullptr;
lv_obj_t *planesDetailSignalLost = nullptr;
bool planesFeatureAvailable = false;
lv_obj_t *rssPage = nullptr;
lv_obj_t *agendaPage = nullptr;
lv_obj_t *agendaHeaderLabel = nullptr;
lv_obj_t *agendaStatusLabel = nullptr;
lv_obj_t *agendaLegendLabel = nullptr;
lv_obj_t *agendaDayLabels[CLOCK_AGENDA_MAX_ITEMS] = {};
// Čas a název stojí ve dvou sloupcích, ne v jednom štítku s recolor značkou:
// jeden štítek se nedá zarovnat do mřížky, protože písmo není neproporcionální.
lv_obj_t *agendaTimeLabels[CLOCK_AGENDA_MAX_ITEMS] = {};
lv_obj_t *agendaTitleLabels[CLOCK_AGENDA_MAX_ITEMS] = {};
// Který řádek otevírá nový den. Podle toho se rozmisťuje: řádek s hlavičkou je
// o její výšku vyšší než ostatní.
bool agendaRowStartsDay[CLOCK_AGENDA_MAX_ITEMS] = {};
// Kalendář každého řádku. Drží se proto, že noční paleta časy přebarvuje a bez
// indexu by se po návratu do dne slily do jedné barvy.
uint8_t agendaRowCalendar[CLOCK_AGENDA_MAX_ITEMS] = {};
uint8_t agendaVisibleItemCount = 0;
size_t agendaCalendarCount = 0;
char agendaCalendarNames[AGENDA_MAX_CALENDARS][AGENDA_CALENDAR_NAME_LENGTH] = {};
lv_obj_t *rssHeaderLabel = nullptr;
lv_obj_t *rssStatusLabel = nullptr;
lv_obj_t *rssTitleLabels[CLOCK_RSS_MAX_ITEMS] = {};
// Jen položky s časem nesou recolor značku, kterou přebarvuje applyRssColors().
bool rssItemHasTime[CLOCK_RSS_MAX_ITEMS] = {};
uint8_t rssVisibleItemCount = 0;
lv_obj_t *radarPage = nullptr;
lv_obj_t *radarCanvas = nullptr;
// Pás pod ukazatelem obrazovek: čas a venkovní teplota. Stejná informace na
// stejném místě jako na ostatních obrazovkách, aby se radar nemusel opouštět
// jen kvůli pohledu na hodiny.
lv_obj_t *radarClockLabel = nullptr;
// Řádek o snímku animace - "NYNÍ 14:35" nebo "-25 min 14:10".
lv_obj_t *radarTitleLabel = nullptr;
// Rozsah patří dolů pod mapu, hned nad svoje tečky.
lv_obj_t *radarRangeLabel = nullptr;
// Tečky místo pruhu: jedna na snímek animace nahoře, jedna na rozsah dole.
// Horní řada tak rovnou ukazuje, kolik snímků je připravených, a spodní, kde
// v řadě rozsahů se člověk právě nachází.
constexpr uint8_t RADAR_FRAME_DOT_CAPACITY = 15;
constexpr uint8_t RADAR_RANGE_DOT_COUNT = 5;
lv_obj_t *radarFrameDots[RADAR_FRAME_DOT_CAPACITY] = {};
lv_obj_t *radarRangeDots[RADAR_RANGE_DOT_COUNT] = {};
// Tečky leží přímo na mapě. Nad hustými srážkami by šedá i červená zanikly,
// proto má každá řada pod sebou stejný tmavý podklad jako popisek nahoře.
lv_obj_t *radarFrameDotsBacking = nullptr;
lv_obj_t *radarRangeDotsBacking = nullptr;
// Musí souhlasit s pořadím rozsahů v handleRadarRangeChange().
constexpr uint16_t RADAR_RANGE_DOT_RADII[RADAR_RANGE_DOT_COUNT] = {25, 50, 100,
                                                                   200, 0};
bool radarFullPreparationInProgress = false;
lv_obj_t *radarStatusLabel = nullptr;

// Ukazatel obrazovek. Leží nad všemi stránkami, ne v jedné z nich - jinak by
// z něj byl ukazatel jediné obrazovky. Pořadí teček odpovídá pořadí, ve kterém
// se obrazovky střídají, tedy tomu z konfigurace.
constexpr uint8_t SCREEN_DOT_COUNT = CLOCK_SCREEN_ORDER_COUNT;
lv_obj_t *screenDots[SCREEN_DOT_COUNT] = {};
lv_obj_t *screenDotsBacking = nullptr;
// Vteřinový prstenec má poloměr 226 a jeho největší tečka (kometa při velikosti
// 10) sahá k y=20. Pás teček tedy začíná až na y=22 a má užší podklad než řady
// na radaru, aby se pod něj vešla i hlavička obrazovky zpráv.
constexpr int SCREEN_DOT_OFFSET_Y = -212;
constexpr int SCREEN_DOT_BACKING_PADDING = 4;

// Pásy radarové obrazovky odshora dolů, v pořadí ukazatel obrazovek - čas
// a teplota - tečky snímků - čas snímku. Rozestupy vycházejí z MeteoPlaneRadar,
// jen přepočtené na střed prvku místo horního okraje textu a roztažené o to,
// oč je zdejší písmo vyšší než vestavěné GFX. Čísla musí sedět s výškami
// clock_czech_20 (24 px) a clock_czech_16 (20 px) plus 2 px odsazení nahoře
// i dole - jinak si dva pásy vlezou do sebe.
// Mezera mezi časem a venkovní teplotou ve stavovém řádku. Sdílí ji radar
// i hlavička předpovědi, aby na obou obrazovkách seděl stejný odstup; mezery
// jsou v proporcionálním písmu úzké, takže jich pár musí být.
constexpr char STATUS_LINE_GAP[] = "      ";
constexpr int RADAR_CLOCK_OFFSET_Y = -186;
constexpr int RADAR_FRAME_DOTS_OFFSET_Y = -162;
constexpr int RADAR_FRAME_LABEL_OFFSET_Y = -140;
constexpr int RADAR_RANGE_LABEL_OFFSET_Y = 164;
constexpr int RADAR_RANGE_DOTS_OFFSET_Y = 190;
// Stavový řádek se dá vypnout; ostatní pásy jsou pevná výbava obrazovky.
bool radarStatusLineEnabled = true;
lv_obj_t *settingsPage = nullptr;
lv_obj_t *dayBrightnessSlider = nullptr;
lv_obj_t *nightBrightnessSlider = nullptr;
lv_obj_t *dayBrightnessValueLabel = nullptr;
lv_obj_t *nightBrightnessValueLabel = nullptr;
lv_obj_t *automaticDayNightSwitch = nullptr;
lv_obj_t *secondModeDropdown = nullptr;
lv_obj_t *weatherIconModeDropdown = nullptr;
lv_obj_t *automaticUpdateSwitch = nullptr;
lv_obj_t *webModeDropdown = nullptr;
constexpr uint8_t SETTINGS_PAGE_COUNT = 4;
lv_obj_t *settingsContent[SETTINGS_PAGE_COUNT] = {};
lv_obj_t *settingsPreviousButton = nullptr;
lv_obj_t *settingsNextButton = nullptr;
lv_obj_t *settingsPageNumberLabel = nullptr;
lv_obj_t *clockStyleTitleLabel = nullptr;
lv_obj_t *digitalClockStyleCard = nullptr;
lv_obj_t *analogClockStyleCard = nullptr;
lv_obj_t *valuesClockStyleCard = nullptr;
lv_obj_t *digitalClockStyleLabel = nullptr;
lv_obj_t *analogClockStyleLabel = nullptr;
lv_obj_t *valuesClockStyleLabel = nullptr;
lv_obj_t *deviceInfoLabel = nullptr;
lv_obj_t *firmwareStatusLabel = nullptr;
lv_obj_t *firmwareCheckButton = nullptr;
lv_obj_t *firmwareInstallButton = nullptr;
lv_obj_t *dayBrightnessTitleLabel = nullptr;
lv_obj_t *nightBrightnessTitleLabel = nullptr;
lv_obj_t *automaticDayNightTitleLabel = nullptr;
lv_obj_t *weatherIconModeTitleLabel = nullptr;
lv_obj_t *secondModeTitleLabel = nullptr;
lv_obj_t *webModeTitleLabel = nullptr;
lv_obj_t *automaticUpdateTitleLabel = nullptr;
lv_obj_t *firmwareCheckLabel = nullptr;
lv_obj_t *firmwareInstallLabel = nullptr;
lv_obj_t *wifiAddressLabel = nullptr;
lv_obj_t *firmwareVersionLabel = nullptr;
lv_obj_t *firmwareUpdateOverlay = nullptr;
lv_obj_t *firmwareUpdateTitleLabel = nullptr;
lv_obj_t *firmwareUpdateCountdownLabel = nullptr;
lv_obj_t *analogDialLayer = nullptr;
lv_obj_t *analogHandsLayer = nullptr;
lv_obj_t *analogOutsideTitleLabel = nullptr;
lv_obj_t *analogOutsideValueLabel = nullptr;
lv_obj_t *analogOutsideDecimalLabel = nullptr;
lv_obj_t *analogOutsideUnitLabel = nullptr;
lv_obj_t *analogRoomTitleLabel = nullptr;
lv_obj_t *analogRoomValueLabel = nullptr;
lv_obj_t *analogRoomDecimalLabel = nullptr;
lv_obj_t *analogRoomUnitLabel = nullptr;
lv_obj_t *analogMetricATitleLabel = nullptr;
lv_obj_t *analogMetricAValueLabel = nullptr;
lv_obj_t *analogMetricAUnitLabel = nullptr;
lv_obj_t *analogMetricBTitleLabel = nullptr;
lv_obj_t *analogMetricBValueLabel = nullptr;
lv_obj_t *analogMetricBUnitLabel = nullptr;
lv_obj_t *analogMetricDivider = nullptr;

struct AnalogDialRun {
  uint16_t length;
  lv_color_t color;
};

struct AnalogDialCache {
  uint32_t *rowOffsets = nullptr;
  AnalogDialRun *runs = nullptr;
  uint32_t runCount = 0;
};

AnalogDialCache analogDialCache;
lv_obj_t *digitalAirArc = nullptr;
lv_obj_t *digitalAirStem = nullptr;
lv_obj_t *digitalAirLeftLeg = nullptr;
lv_obj_t *digitalAirRightLeg = nullptr;
lv_obj_t *digitalMetricDivider = nullptr;
lv_obj_t *digitalBottomDivider = nullptr;
lv_obj_t *secondDots[SECOND_DOT_COUNT] = {};
lv_obj_t *secondLineBackgroundArc = nullptr;
lv_obj_t *secondLineFadeArc = nullptr;
lv_obj_t *secondLineActiveArc = nullptr;
lv_obj_t *secondLineActiveBridge = nullptr;
lv_obj_t *secondCometHead = nullptr;
int16_t secondDotCenterX[SECOND_DOT_COUNT] = {};
int16_t secondDotCenterY[SECOND_DOT_COUNT] = {};
lv_point_t secondLineActiveBridgePoints[2] = {};
uint8_t displayedSecond = 255;
unsigned long secondTickStartedAt = 0;
bool secondFadeActive = false;
unsigned long secondFadeStartedAt = 0;
unsigned long lastSecondFadeFrameAt = 0;
bool settingsVisible = false;
bool suppressNextDashboardClick = false;
unsigned long suppressDashboardClickUntil = 0;
// Ciferník, radar a zprávy se střídají na jednom místě. Držet to jako jeden
// stav je bezpečnější než dvě nezávislé viditelnosti, které by se mohly
// odkrýt naráz.
// Čísla odpovídají ClockOrderedScreen, takže se jimi dá indexovat pořadí
// obrazovek uložené v konfiguraci.
enum DashboardScreen : uint8_t {
  DASHBOARD_SCREEN_CLOCK = CLOCK_SCREEN_CLOCK,
  DASHBOARD_SCREEN_RADAR = CLOCK_SCREEN_RADAR,
  DASHBOARD_SCREEN_RSS = CLOCK_SCREEN_RSS,
  DASHBOARD_SCREEN_FORECAST = CLOCK_SCREEN_FORECAST,
  DASHBOARD_SCREEN_PLANES = CLOCK_SCREEN_PLANES,
  DASHBOARD_SCREEN_AGENDA = CLOCK_SCREEN_AGENDA,
};
uint8_t activeScreen = DASHBOARD_SCREEN_CLOCK;
bool radarFeatureAvailable = true;
bool rssFeatureAvailable = false;
bool agendaFeatureAvailable = false;
bool forecastFeatureAvailable = false;
bool nightModeEnabled = false;
uint8_t nightVisualMode = CLOCK_NIGHT_VISUAL_RED;
bool automaticDayNightEnabled = true;
uint8_t savedDayBrightness = 35;
uint8_t savedNightBrightness = 10;
bool secondRingEnabled = true;
uint8_t secondEffect = CLOCK_SECOND_EFFECT_DOTS;
uint32_t secondRingBackgroundColor = 0xFFFFFF;
uint8_t secondRingBackgroundBrightness = 0;
uint8_t secondRingBackgroundDotSize = 3;
uint8_t secondDotSize = 3;
uint32_t secondDotColor = 0xFFFFFF;
uint32_t leftWeatherIconColor = 0xFFFFFF;
uint32_t rightWeatherIconColor = 0xFFFFFF;
uint8_t secondDotBrightness = 175;
uint32_t timeColor = 0xF6F6F6;
uint32_t dateColor = 0xB5B5B5;
uint8_t timeFont = CLOCK_TIME_FONT_BARLOW;
uint8_t language = CLOCK_LANGUAGE_CZECH;
bool outsideUsesWeatherIcon = true;
bool roomUsesWeatherIcon = false;
bool homeAssistantStatusRelevant = true;
char outsideUnit[16] = "°C";
char roomUnit[16] = "°C";
uint8_t outsideDecimals = 1;
uint8_t roomDecimals = 1;
bool weatherConfigured = false;
bool outsideConfigured = false;
bool roomConfigured = false;
bool metricAConfigured = false;
bool metricBConfigured = false;
bool weatherAnimationAvailable = false;
bool animatedWeatherIconsEnabled = true;
uint8_t configuredWeatherIconStyle = CLOCK_WEATHER_ICON_STYLE_MONOCHROME;

uint8_t selectedSecondMode() {
  return secondRingEnabled ? secondEffect + 1 : 0;
}

void applySelectedSecondMode(uint8_t selected) {
  secondRingEnabled = selected != 0;
  if (secondRingEnabled) {
    secondEffect = constrain(
        static_cast<uint8_t>(selected - 1),
        static_cast<uint8_t>(CLOCK_SECOND_EFFECT_DOTS),
        static_cast<uint8_t>(CLOCK_SECOND_EFFECT_COMET));
  }
}

uint8_t selectedWeatherIconMode() {
  if (!animatedWeatherIconsEnabled) return 0;
  if (configuredWeatherIconStyle == CLOCK_WEATHER_ICON_STYLE_FLAT) return 1;
  if (configuredWeatherIconStyle == CLOCK_WEATHER_ICON_STYLE_LINE) return 2;
  return 3;
}

void applySelectedWeatherIconMode(uint8_t selected) {
  animatedWeatherIconsEnabled = selected != 0;
  if (selected == 1) {
    configuredWeatherIconStyle = CLOCK_WEATHER_ICON_STYLE_FLAT;
  } else if (selected == 2) {
    configuredWeatherIconStyle = CLOCK_WEATHER_ICON_STYLE_LINE;
  } else {
    configuredWeatherIconStyle = CLOCK_WEATHER_ICON_STYLE_MONOCHROME;
  }
}
bool weatherAnimationRevealPending = false;
unsigned long weatherAnimationRevealAt = 0;
bool firmwareUpdateActive = false;
uint8_t settingsPageIndex = 0;
uint8_t settingsSelectedClockStyle = CLOCK_STYLE_DIGITAL;
uint8_t selectedWebMode = 0;
bool automaticFirmwareUpdateEnabled = false;
bool displayedCanInstall = false;
unsigned long lastSettingsInfoRefreshAt = 0;
char displayedDeviceInfo[64] = "";
char displayedFirmwareStatus[160] = "";
char weatherAnimationKey[48] = "";
char leftWeatherDecoderKey[48] = "";
char rightWeatherDecoderKey[48] = "";
lv_img_dsc_t weatherAnimationSource = {};
bool webActive = false;
bool wifiConnected = false;
uint8_t timeColonEffect = CLOCK_TIME_COLON_STEADY;
char displayedTimeText[6] = "--:--";
uint32_t lastRenderedTimeColonColor = UINT32_MAX;
ClockValues currentValues;
ClockSideValueConfig leftValueConfig;
ClockSideValueConfig rightValueConfig;
ClockMetricConfig metricAConfig;
ClockMetricConfig metricBConfig;
ClockMetricColorScale leftValueColorScale;
ClockMetricColorScale rightValueColorScale;
ClockMetricColorScale metricAColorScale;
ClockMetricColorScale metricBColorScale;
BrightnessPreviewCallback brightnessPreviewCallback = nullptr;
SettingsOpenCallback settingsOpenCallback = nullptr;
SettingsSaveCallback settingsSaveCallback = nullptr;
SettingsActionCallback firmwareCheckCallback = nullptr;
SettingsActionCallback firmwareInstallCallback = nullptr;
RadarVisibilityCallback radarVisibilityCallback = nullptr;
RssVisibilityCallback rssVisibilityCallback = nullptr;
// Agenda se registruje zvlášť, stejně jako radar letadel: obrazovka vzniká až
// při prvním zapnutí, takže ji clockDashboardInit() ještě nemusí znát.
RssVisibilityCallback agendaVisibilityCallback = nullptr;
ForecastVisibilityCallback forecastVisibilityCallback = nullptr;
RadarRangeCallback radarRangeCallback = nullptr;
RssVisibilityCallback planesVisibilityCallback = nullptr;

bool redNightVisualEnabled() {
  return nightModeEnabled && nightVisualMode == CLOCK_NIGHT_VISUAL_RED;
}

const lv_font_t *configuredTimeFont() {
  if (timeFont == CLOCK_TIME_FONT_LIBERATION_SANS)
    return &clock_time_liberation_110;
  if (timeFont == CLOCK_TIME_FONT_LCD) return &clock_time_lcd_80;
  if (timeFont == CLOCK_TIME_FONT_DOTO) return &clock_time_doto_98;
  return &clock_time_110;
}

void showSettingsSubpage(uint8_t page);
void updateRadarFrameDots(uint8_t frameCount, uint8_t currentFrameNumber);
void updateScreenDots();
void updateRadarClockLabel();
void updatePlanesClockLabel();
void updateOverlayStatusLabels();
const char *radarEmptyStateText(bool busy);
void updateClockStyleCardSelection();
void applyValuesPageColors();
void alignCenter(lv_obj_t *object, int x, int y);
void setTextColor(lv_obj_t *object, lv_color_t color);
lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font,
                    lv_color_t color);
void applyRssColors();
void applyAgendaColors();
void layoutAgendaItems();
void updateAgendaHeaderLabel();
void updateAgendaLegendLabel();
lv_color_t forecastTemperatureColor(float degrees);
void forecastAppendColorTag(char *destination, size_t capacity,
                            lv_color_t color);
void updateForecastPage();
void updateForecastHeaderLabel();

bool englishLanguage() { return language == CLOCK_LANGUAGE_ENGLISH; }

void applyDashboardLanguage() {
  const bool english = englishLanguage();
  if (clockStyleTitleLabel != nullptr)
    lv_label_set_text(clockStyleTitleLabel,
                      english ? "CLOCK TYPE" : "TYP HODIN");
  if (digitalClockStyleLabel != nullptr)
    lv_label_set_text(digitalClockStyleLabel,
                      english ? "DIGITAL" : "DIGITÁLNÍ");
  if (analogClockStyleLabel != nullptr)
    lv_label_set_text(analogClockStyleLabel,
                      english ? "ANALOG" : "ANALOGOVÉ");
  if (valuesClockStyleLabel != nullptr)
    lv_label_set_text(valuesClockStyleLabel,
                      english ? "VALUES" : "HODNOTY");
  if (dayBrightnessTitleLabel != nullptr)
    lv_label_set_text(dayBrightnessTitleLabel,
                      english ? "DAY BRIGHTNESS" : "DENNÍ JAS");
  if (nightBrightnessTitleLabel != nullptr)
    lv_label_set_text(nightBrightnessTitleLabel,
                      english ? "NIGHT BRIGHTNESS" : "NOČNÍ JAS");
  if (automaticDayNightTitleLabel != nullptr)
    lv_label_set_text(automaticDayNightTitleLabel,
                      english ? "AUTOMATIC DAY/NIGHT"
                              : "AUTOMATICKY DEN/NOC");
  if (weatherIconModeTitleLabel != nullptr)
    lv_label_set_text(weatherIconModeTitleLabel,
                      english ? "WEATHER ICONS" : "IKONY POČASÍ");
  if (weatherIconModeDropdown != nullptr) {
    const uint16_t selected = lv_dropdown_get_selected(weatherIconModeDropdown);
    lv_dropdown_set_options(
        weatherIconModeDropdown,
        english ? "STATIC MONOCHROME\nANIMATED FLAT\nANIMATED LINE\nANIMATED MONOCHROME"
                : "STATICKÉ MONOCHROMATICKÉ\nANIMOVANÉ FLAT\nANIMOVANÉ LINE\nANIMOVANÉ MONOCHROMATICKÉ");
    lv_dropdown_set_selected(weatherIconModeDropdown, selected);
  }
  if (secondModeTitleLabel != nullptr)
    lv_label_set_text(secondModeTitleLabel,
                      english ? "SECONDS" : "VTEŘINY");
  if (secondModeDropdown != nullptr) {
    const uint16_t selected = lv_dropdown_get_selected(secondModeDropdown);
    lv_dropdown_set_options(secondModeDropdown,
                            english ? "OFF\nDOTS\nLINE\nCOMET"
                                    : "VYPNUTO\nTEČKY\nLINKA\nKOMETA");
    lv_dropdown_set_selected(secondModeDropdown, selected);
  }
  if (webModeDropdown != nullptr) {
    const uint16_t selected = lv_dropdown_get_selected(webModeDropdown);
    lv_dropdown_set_options(webModeDropdown,
                            english ? "10 MINUTES\nALWAYS\nOFF"
                                    : "10 MINUT\nVŽDY\nVYPNUTÝ");
    lv_dropdown_set_selected(webModeDropdown, selected);
  }
  if (automaticUpdateTitleLabel != nullptr)
    lv_label_set_text(automaticUpdateTitleLabel,
                      english ? "AUTOMATIC OTA" : "AUTOMATICKÉ OTA");
  if (firmwareCheckLabel != nullptr)
    lv_label_set_text(firmwareCheckLabel,
                      english ? "CHECK" : "ZKONTROLOVAT");
  if (firmwareInstallLabel != nullptr)
    lv_label_set_text(firmwareInstallLabel,
                      english ? "UPDATE" : "AKTUALIZOVAT");
  if (firmwareUpdateTitleLabel != nullptr)
    lv_label_set_text(firmwareUpdateTitleLabel,
                      english ? "FIRMWARE UPDATE"
                              : "AKTUALIZACE FIRMWARE");
  // Hláška prázdné obrazovky se překládá jen tehdy, když je opravdu vidět;
  // pod snímkem je skrytá a další snapshot si ji stejně přepíše.
  if (radarStatusLabel != nullptr &&
      !lv_obj_has_flag(radarStatusLabel, LV_OBJ_FLAG_HIDDEN)) {
    lv_label_set_text(radarStatusLabel, radarEmptyStateText(false));
    alignCenter(radarStatusLabel, 0, 0);
  }
  // Předpověď má v každém řádku vlastní text, takže se překládá tím, že se
  // řádky složí znovu.
  updateForecastPage();
  displayedDeviceInfo[0] = '\0';
  displayedFirmwareStatus[0] = '\0';
}

// Digitální i analogový ciferník sdílí dashboardContent; hodnotová obrazovka
// má vlastní stránku. Viditelnost se všude řídí přes tento ukazatel, aby
// přepnutí stylu nenechalo obě stránky odkryté naráz.
lv_obj_t *primaryClockPage() {
  return valuesLayoutEnabled() && valuesPage != nullptr ? valuesPage
                                                        : dashboardContent;
}

// Stránka překrývající ciferník. Pro samotný ciferník vrací nullptr, protože
// ten se odkrývá přes primaryClockPage().
lv_obj_t *overlayPage(uint8_t screen) {
  switch (screen) {
    case DASHBOARD_SCREEN_RADAR: return radarPage;
    case DASHBOARD_SCREEN_RSS: return rssPage;
    case DASHBOARD_SCREEN_AGENDA: return agendaPage;
    case DASHBOARD_SCREEN_FORECAST: return forecastPage;
    case DASHBOARD_SCREEN_PLANES: return planesPage;
    default: return nullptr;
  }
}

bool screenAvailable(uint8_t screen) {
  switch (screen) {
    case DASHBOARD_SCREEN_RADAR: return radarFeatureAvailable;
    case DASHBOARD_SCREEN_RSS: return rssFeatureAvailable;
    // Stránka se zakládá až s prvním zapnutím obrazovky, takže dokud ji
    // majitel nechce, nestojí ani jeden objekt LVGL.
    case DASHBOARD_SCREEN_FORECAST:
      return forecastFeatureAvailable && forecastPage != nullptr;
    case DASHBOARD_SCREEN_PLANES:
      return planesFeatureAvailable && planesPage != nullptr;
    case DASHBOARD_SCREEN_AGENDA:
      return agendaFeatureAvailable && agendaPage != nullptr;
    default: return true;
  }
}

void setActiveScreen(uint8_t screen) {
  if (!screenAvailable(screen)) screen = DASHBOARD_SCREEN_CLOCK;
  if (activeScreen == screen || settingsVisible) return;
  const uint8_t previous = activeScreen;
  activeScreen = screen;
  if (screen == DASHBOARD_SCREEN_RADAR) {
    // Při návratu na radar neodkrývej snímek, který zůstal v canvasu z
    // předchozího cyklu. Canvas znovu zobrazí až první snapshot nové animace.
    lv_obj_add_flag(radarCanvas, LV_OBJ_FLAG_HIDDEN);
    updateRadarFrameDots(0, 0);
    // Popisky patří ke snímku, který zrovna zmizel; nechat je viset nad
    // prázdnou stránkou by tvrdilo, že se něco ukazuje.
    if (radarTitleLabel != nullptr)
      lv_obj_add_flag(radarTitleLabel, LV_OBJ_FLAG_HIDDEN);
    if (radarRangeLabel != nullptr)
      lv_obj_add_flag(radarRangeLabel, LV_OBJ_FLAG_HIDDEN);
    updateOverlayStatusLabels();
  }
  lv_obj_t *previousPage = overlayPage(previous);
  if (previousPage == nullptr) previousPage = primaryClockPage();
  lv_obj_t *nextPage = overlayPage(screen);
  if (nextPage == nullptr) nextPage = primaryClockPage();
  lv_obj_add_flag(previousPage, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(nextPage, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(nextPage);
  // Radar si stahování zapíná a vypíná podle toho, jestli je vidět.
  const bool wasRadar = previous == DASHBOARD_SCREEN_RADAR;
  const bool isRadar = screen == DASHBOARD_SCREEN_RADAR;
  if (wasRadar != isRadar && radarVisibilityCallback != nullptr)
    radarVisibilityCallback(isRadar);
  // Kanál zpráv běží na vlastním intervalu i skrytý; otevření obrazovky mu
  // jen dá vědět, aby stará data stáhl znovu.
  const bool wasRss = previous == DASHBOARD_SCREEN_RSS;
  const bool isRss = screen == DASHBOARD_SCREEN_RSS;
  if (wasRss != isRss && rssVisibilityCallback != nullptr)
    rssVisibilityCallback(isRss);
  // Agenda běží na vlastním intervalu i skrytá; otevření obrazovky jí jen dá
  // vědět, aby stará data stáhla znovu.
  const bool wasAgenda = previous == DASHBOARD_SCREEN_AGENDA;
  const bool isAgenda = screen == DASHBOARD_SCREEN_AGENDA;
  if (wasAgenda != isAgenda && agendaVisibilityCallback != nullptr)
    agendaVisibilityCallback(isAgenda);
  // Předpověď běží na vlastním intervalu i skrytá; otevření obrazovky jí jen
  // dá vědět, aby stará data stáhla znovu.
  const bool wasForecast = previous == DASHBOARD_SCREEN_FORECAST;
  const bool isForecast = screen == DASHBOARD_SCREEN_FORECAST;
  if (wasForecast != isForecast && forecastVisibilityCallback != nullptr)
    forecastVisibilityCallback(isForecast);
  // Radar letadel stahuje jen když je vidět, nebo když si ho majitel pustil do
  // rotace; jinak by se z hodin stal stálý zákazník cizího API.
  const bool wasPlanes = previous == DASHBOARD_SCREEN_PLANES;
  const bool isPlanes = screen == DASHBOARD_SCREEN_PLANES;
  if (wasPlanes != isPlanes) {
    if (!isPlanes) {
      // Odchod z obrazovky zavírá detail: po návratu by visel nad letadlem,
      // které už dávno odletělo.
      planeRadarServiceCloseDetail();
      if (planesCanvas != nullptr)
        lv_obj_add_flag(planesCanvas, LV_OBJ_FLAG_HIDDEN);
      if (planesDetailPanel != nullptr)
        lv_obj_add_flag(planesDetailPanel, LV_OBJ_FLAG_HIDDEN);
    }
    if (planesVisibilityCallback != nullptr) planesVisibilityCallback(isPlanes);
  }
  // Stránka se právě vytáhla dopředu, takže ukazatel musí zpátky nad ni.
  updateScreenDots();
}

void setRadarVisible(bool visible) {
  setActiveScreen(visible ? DASHBOARD_SCREEN_RADAR : DASHBOARD_SCREEN_CLOCK);
}

void setObjectVisible(lv_obj_t *object, bool visible) {
  if (visible) {
    lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
  }
}

const char *roomIconGlyph(const char *icon) {
  if (strcmp(icon, "home") == 0) return "\xEF\x80\x95";         // U+F015
  if (strcmp(icon, "living-room") == 0) return "\xEF\x92\xB8";  // U+F4B8
  if (strcmp(icon, "kitchen") == 0) return "\xEF\x8B\xA7";      // U+F2E7
  if (strcmp(icon, "none") == 0 || strcmp(icon, "weather") == 0) return "";
  return "\xEF\x88\xB6";  // U+F236
}

void normalizeMicroSign(char *text) {
  if (text == nullptr) return;
  for (size_t index = 0; text[index] != '\0'; ++index) {
    if (static_cast<uint8_t>(text[index]) == 0xCE &&
        static_cast<uint8_t>(text[index + 1]) == 0xBC) {
      // Řecké malé mí U+03BC nahraď znakem mikro U+00B5, který obsahují
      // dashboardové fonty. Oba znaky mají v UTF-8 stejnou délku.
      text[index] = static_cast<char>(0xC2);
      text[index + 1] = static_cast<char>(0xB5);
      ++index;
    }
  }
}

void ensureWeatherAnimationDecoders() {
  if (!weatherAnimationAvailable || !animatedWeatherIconsEnabled ||
      weatherAnimationKey[0] == '\0') {
    return;
  }
  auto updateDecoder = [](lv_obj_t *&decoder, char *decoderKey,
                          size_t decoderKeySize, bool used, lv_coord_t x) {
    if (used) {
      if (strcmp(decoderKey, weatherAnimationKey) != 0) {
        lv_gif_set_src(decoder, &weatherAnimationSource);
        strlcpy(decoderKey, weatherAnimationKey, decoderKeySize);
      }
      return;
    }
    if (decoderKey[0] == '\0') return;

    // Skrytí LVGL GIF objektu nezastaví jeho timer. Objekt proto před
    // uvolněním starého assetu zrušíme a vytvoříme znovu bez zdroje.
    lv_obj_t *parent = lv_obj_get_parent(decoder);
    lv_obj_del(decoder);
    decoder = lv_gif_create(parent);
    alignCenter(decoder, analogLayoutEnabled() ? 0 : x,
                analogLayoutEnabled() ? -70 : 107);
    lv_obj_add_flag(decoder, LV_OBJ_FLAG_HIDDEN);
    decoderKey[0] = '\0';
  };

  updateDecoder(weatherAnimation, leftWeatherDecoderKey,
                sizeof(leftWeatherDecoderKey), outsideUsesWeatherIcon, -142);
  updateDecoder(roomWeatherAnimation, rightWeatherDecoderKey,
                sizeof(rightWeatherDecoderKey), roomUsesWeatherIcon, 142);
}

lv_color_t configuredColor(uint32_t color) {
  // LVGL 8.3 rozbaluje LV_COLOR_MAKE do makra, jehož argumenty nejsou
  // uzávorkované. Bitové výrazy by se proto kvůli prioritě operátorů
  // vyhodnotily chybně. Kanály nejdřív materializujeme do samostatných hodnot.
  const uint8_t red = static_cast<uint8_t>((color >> 16) & 0xFF);
  const uint8_t green = static_cast<uint8_t>((color >> 8) & 0xFF);
  const uint8_t blue = static_cast<uint8_t>(color & 0xFF);
  return LV_COLOR_MAKE(red, green, blue);
}

lv_color_t analogTone(float intensity = 1.0f) {
  intensity = constrain(intensity, 0.0f, 1.0f);
  const uint8_t red = static_cast<uint8_t>(
      ((analogToneColor >> 16) & 0xFF) * intensity);
  const uint8_t green = static_cast<uint8_t>(
      ((analogToneColor >> 8) & 0xFF) * intensity);
  const uint8_t blue =
      static_cast<uint8_t>((analogToneColor & 0xFF) * intensity);
  return lv_color_make(red, green, blue);
}

lv_color_t analogHandTone(float intensity = 1.0f) {
  intensity = constrain(intensity, 0.0f, 1.0f);
  const uint8_t red = static_cast<uint8_t>(
      ((analogHandToneColor >> 16) & 0xFF) * intensity);
  const uint8_t green = static_cast<uint8_t>(
      ((analogHandToneColor >> 8) & 0xFF) * intensity);
  const uint8_t blue = static_cast<uint8_t>(
      (analogHandToneColor & 0xFF) * intensity);
  return lv_color_make(red, green, blue);
}

lv_point_t analogPoint(const lv_point_t &center, float angleDegrees,
                       float radius) {
  const float radians = angleDegrees * ANALOG_PI / 180.0f;
  return {
      static_cast<lv_coord_t>(center.x +
                              std::round(std::sin(radians) * radius)),
      static_cast<lv_coord_t>(center.y -
                              std::round(std::cos(radians) * radius)),
  };
}

struct AnalogDrawTarget {
  lv_draw_ctx_t *context;
  lv_obj_t *canvas;
};

void drawAnalogLine(const AnalogDrawTarget &target, const lv_point_t &from,
                    const lv_point_t &to, lv_color_t color, lv_coord_t width,
                    lv_opa_t opacity = LV_OPA_COVER) {
  lv_draw_line_dsc_t descriptor;
  lv_draw_line_dsc_init(&descriptor);
  descriptor.color = color;
  descriptor.width = width;
  descriptor.opa = opacity;
  descriptor.round_start = true;
  descriptor.round_end = true;
  if (target.canvas != nullptr) {
    const lv_point_t points[] = {from, to};
    lv_canvas_draw_line(target.canvas, points, 2, &descriptor);
  } else {
    lv_draw_line(target.context, &descriptor, &from, &to);
  }
}

void drawAnalogCircle(const AnalogDrawTarget &target,
                      const lv_point_t &center,
                      lv_coord_t radius, lv_color_t color,
                      lv_opa_t opacity = LV_OPA_COVER) {
  lv_draw_rect_dsc_t descriptor;
  lv_draw_rect_dsc_init(&descriptor);
  descriptor.radius = LV_RADIUS_CIRCLE;
  descriptor.bg_color = color;
  descriptor.bg_opa = opacity;
  descriptor.border_width = 0;
  lv_area_t area = {
      static_cast<lv_coord_t>(center.x - radius),
      static_cast<lv_coord_t>(center.y - radius),
      static_cast<lv_coord_t>(center.x + radius),
      static_cast<lv_coord_t>(center.y + radius),
  };
  if (target.canvas != nullptr) {
    lv_canvas_draw_rect(target.canvas, area.x1, area.y1,
                        lv_area_get_width(&area), lv_area_get_height(&area),
                        &descriptor);
  } else {
    lv_draw_rect(target.context, &descriptor, &area);
  }
}

void drawAnalogArc(const AnalogDrawTarget &target, const lv_point_t &center,
                   uint16_t radius, lv_color_t color, lv_coord_t width,
                   lv_opa_t opacity = LV_OPA_COVER) {
  lv_draw_arc_dsc_t descriptor;
  lv_draw_arc_dsc_init(&descriptor);
  descriptor.color = color;
  descriptor.width = width;
  descriptor.opa = opacity;
  descriptor.rounded = true;
  if (target.canvas != nullptr) {
    lv_canvas_draw_arc(target.canvas, center.x, center.y, radius, 0, 360,
                       &descriptor);
  } else {
    lv_draw_arc(target.context, &descriptor, &center, radius, 0, 360);
  }
}

void drawAnalogArcSegment(const AnalogDrawTarget &target,
                          const lv_point_t &center, uint16_t radius,
                          float startAngleDegrees, float endAngleDegrees,
                          lv_color_t color, lv_coord_t width) {
  const auto lvglAngle = [](float analogAngle) -> uint16_t {
    int angle = static_cast<int>(std::round(analogAngle - 90.0f)) % 360;
    if (angle < 0) angle += 360;
    return static_cast<uint16_t>(angle);
  };
  lv_draw_arc_dsc_t descriptor;
  lv_draw_arc_dsc_init(&descriptor);
  descriptor.color = color;
  descriptor.width = width;
  descriptor.opa = LV_OPA_COVER;
  descriptor.rounded = true;
  const uint16_t startAngle = lvglAngle(startAngleDegrees);
  const uint16_t endAngle = lvglAngle(endAngleDegrees);
  if (target.canvas != nullptr) {
    lv_canvas_draw_arc(target.canvas, center.x, center.y, radius, startAngle,
                       endAngle, &descriptor);
  } else {
    lv_draw_arc(target.context, &descriptor, &center, radius, startAngle,
                endAngle);
  }
}

void drawAnalogRadialLine(const AnalogDrawTarget &target,
                          const lv_point_t &center, float angleDegrees,
                          float innerRadius, float outerRadius,
                          lv_color_t color, lv_coord_t width,
                          lv_opa_t opacity = LV_OPA_COVER) {
  const lv_point_t from = analogPoint(center, angleDegrees, innerRadius);
  const lv_point_t to = analogPoint(center, angleDegrees, outerRadius);
  drawAnalogLine(target, from, to, color, width, opacity);
}

void drawAnalogHand(const AnalogDrawTarget &target, const lv_point_t &center,
                    float angleDegrees, float rearLength, float frontLength,
                    lv_coord_t outlineWidth, lv_coord_t edgeWidth,
                    lv_coord_t coreWidth) {
  const lv_coord_t outlineSideOffset =
      max(static_cast<lv_coord_t>(3),
          static_cast<lv_coord_t>(edgeWidth / 2 - 1));
  const float effectiveRearLength =
      rearLength + (analogOutlineHandsEnabled ? outlineSideOffset : 0);
  const lv_point_t from =
      analogPoint(center, angleDegrees + 180.0f, effectiveRearLength);
  const lv_point_t to = analogPoint(center, angleDegrees, frontLength);
  const lv_color_t outline = redNightVisualEnabled()
                                 ? lv_color_make(48, 0, 0)
                                 : lv_color_make(4, 14, 24);
  const lv_color_t edge = redNightVisualEnabled()
                              ? COLOR_ERROR
                              : analogHandTone(0.88f);
  const lv_color_t core = redNightVisualEnabled()
                              ? lv_color_make(255, 112, 112)
                              : lv_color_make(246, 250, 252);
  if (analogOutlineHandsEnabled) {
    const lv_color_t outlineColor =
        redNightVisualEnabled() ? COLOR_ERROR : analogHandTone();
    const lv_coord_t sideOffset = outlineSideOffset;
    const lv_point_t leftFrom =
        analogPoint(from, angleDegrees - 90.0f, sideOffset);
    const lv_point_t leftTo =
        analogPoint(to, angleDegrees - 90.0f, sideOffset);
    const lv_point_t rightFrom =
        analogPoint(from, angleDegrees + 90.0f, sideOffset);
    const lv_point_t rightTo =
        analogPoint(to, angleDegrees + 90.0f, sideOffset);

    drawAnalogLine(target, leftFrom, leftTo, outlineColor, 4);
    drawAnalogLine(target, rightFrom, rightTo, outlineColor, 4);
    drawAnalogArcSegment(target, to, sideOffset, angleDegrees - 90.0f,
                         angleDegrees + 90.0f, outlineColor, 4);
    drawAnalogLine(target, leftFrom, rightFrom, outlineColor, 4);
    return;
  }
  drawAnalogLine(target, from, to, outline, outlineWidth);
  drawAnalogLine(target, from, to, edge, edgeWidth);
  drawAnalogLine(target, from, to, core, coreWidth);
}

void renderAnalogDial(const AnalogDrawTarget &target,
                      const lv_point_t &center) {
  const bool redNight = redNightVisualEnabled();
  const lv_color_t cyan = redNight ? COLOR_ERROR : analogTone();
  const lv_color_t amber =
      redNight ? COLOR_ERROR : configuredColor(analogCardinalAccentColor);
  const lv_color_t markerCore =
      redNight ? lv_color_make(255, 112, 112) : COLOR_TEXT;
  const lv_color_t markerEdge = redNight ? COLOR_ERROR : analogTone(0.75f);

  drawAnalogCircle(target, center, 239,
                   redNight ? lv_color_make(10, 0, 0)
                            : lv_color_make(0, 7, 16));
  drawAnalogCircle(target, center, 216,
                   redNight ? lv_color_make(15, 0, 0)
                            : lv_color_make(0, 10, 20));
  drawAnalogArc(target, center, 239,
                redNight ? lv_color_make(58, 14, 14)
                         : lv_color_make(17, 35, 52),
                6);
  drawAnalogArc(target, center, ANALOG_RING_RADIUS, cyan, 3,
                redNight ? LV_OPA_70 : LV_OPA_COVER);

  for (int minute = 0; minute < 60; ++minute) {
    if (minute % 5 == 0) continue;
    drawAnalogRadialLine(target, center, minute * 6.0f, 225.0f, 232.0f,
                         markerCore, 2, redNight ? LV_OPA_60 : LV_OPA_80);
  }

  for (int hour = 0; hour < 12; ++hour) {
    const float angle = hour * 30.0f;
    drawAnalogRadialLine(target, center, angle, 206.0f, 232.0f,
                         lv_color_make(2, 15, 27), 15);
    drawAnalogRadialLine(target, center, angle, 206.0f, 232.0f,
                         markerEdge, 11);
    drawAnalogRadialLine(target, center, angle, 207.0f, 231.0f,
                         markerCore, 7);
  }

  if (analogCardinalAccentsEnabled) {
    // Čtyři shodné akcenty vznikají ze stejného úhlového rozsahu. Nejde o
    // samostatně odhadnuté souřadnice, takže jsou na 12/3/6/9 stejně dlouhé.
    for (int cardinal = 0; cardinal < 4; ++cardinal) {
      const float centerAngle = cardinal * 90.0f;
      const lv_point_t from =
          analogPoint(center, centerAngle - 5.0f, ANALOG_RING_RADIUS);
      const lv_point_t to =
          analogPoint(center, centerAngle + 5.0f, ANALOG_RING_RADIUS);
      drawAnalogLine(target, from, to, amber, 7);
    }
  }
}

void drawCachedAnalogDial(lv_draw_ctx_t *drawContext,
                          const lv_area_t &coordinates) {
  if (analogDialCache.rowOffsets == nullptr ||
      analogDialCache.runs == nullptr) {
    const lv_point_t center = {
        static_cast<lv_coord_t>(coordinates.x1 + ANALOG_CENTER_X),
        static_cast<lv_coord_t>(coordinates.y1 + ANALOG_CENTER_Y),
    };
    renderAnalogDial({drawContext, nullptr}, center);
    return;
  }

  lv_area_t clipped;
  if (!_lv_area_intersect(&clipped, drawContext->clip_area, &coordinates))
    return;
  const lv_coord_t stride = lv_area_get_width(drawContext->buf_area);
  auto *destination = static_cast<lv_color_t *>(drawContext->buf);

  for (lv_coord_t y = clipped.y1; y <= clipped.y2; ++y) {
    const uint16_t sourceY = static_cast<uint16_t>(y - coordinates.y1);
    const uint16_t wantedStart =
        static_cast<uint16_t>(clipped.x1 - coordinates.x1);
    const uint16_t wantedEnd =
        static_cast<uint16_t>(clipped.x2 - coordinates.x1);
    uint16_t runStart = 0;
    for (uint32_t runIndex = analogDialCache.rowOffsets[sourceY];
         runIndex < analogDialCache.rowOffsets[sourceY + 1]; ++runIndex) {
      const AnalogDialRun &run = analogDialCache.runs[runIndex];
      const uint16_t runEnd = runStart + run.length - 1;
      if (runEnd >= wantedStart && runStart <= wantedEnd) {
        const uint16_t copyStart = max(runStart, wantedStart);
        const uint16_t copyEnd = min(runEnd, wantedEnd);
        lv_color_t *row = destination +
            (y - drawContext->buf_area->y1) * stride -
            drawContext->buf_area->x1;
        for (uint16_t x = copyStart; x <= copyEnd; ++x)
          row[coordinates.x1 + x] = run.color;
      }
      runStart = runEnd + 1;
      if (runStart > wantedEnd) break;
    }
  }
}

void drawAnalogDialEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_DRAW_MAIN) return;
  lv_area_t coordinates;
  lv_obj_get_coords(lv_event_get_target(event), &coordinates);
  drawCachedAnalogDial(lv_event_get_draw_ctx(event), coordinates);
}

void drawAnalogHandsEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_DRAW_MAIN) return;
  int hour = 0;
  int minute = 0;
  if (sscanf(displayedTimeText, "%d:%d", &hour, &minute) != 2) return;

  lv_draw_ctx_t *drawContext = lv_event_get_draw_ctx(event);
  lv_area_t coordinates;
  lv_obj_get_coords(lv_event_get_target(event), &coordinates);
  const lv_point_t center = {
      static_cast<lv_coord_t>(coordinates.x1 + ANALOG_CENTER_X),
      static_cast<lv_coord_t>(coordinates.y1 + ANALOG_CENTER_Y),
  };
  const uint8_t second = displayedSecond > 59 ? 0 : displayedSecond;
  const float hourAngle =
      (hour % 12) * 30.0f + minute * 0.5f + second / 120.0f;
  const float minuteAngle = minute * 6.0f + second * 0.1f;
  const float secondAngle = second * 6.0f;
  const AnalogDrawTarget target = {drawContext, nullptr};

  // Ručičky se kreslí až nad texty. Delší provedení záměrně překrývá údaje,
  // stejně jako skutečné ručičky nad potištěným ciferníkem.
  drawAnalogHand(target, center, hourAngle, 16.0f, 145.0f, 21, 15, 9);
  drawAnalogHand(target, center, minuteAngle, 20.0f, 178.0f, 17, 12, 7);

  const lv_point_t secondFrom = analogPoint(center, secondAngle + 180.0f, 34.0f);
  const lv_point_t secondTo = analogPoint(center, secondAngle, 192.0f);
  drawAnalogLine(target, secondFrom, secondTo,
                 redNightVisualEnabled() ? COLOR_ERROR : analogHandTone(),
                 3);

  drawAnalogCircle(target, center, 18,
                   redNightVisualEnabled() ? lv_color_make(48, 0, 0)
                                           : lv_color_make(3, 16, 27));
  drawAnalogCircle(target, center, 13,
                   redNightVisualEnabled() ? COLOR_ERROR
                                           : analogHandTone(0.9f));
  drawAnalogCircle(target, center, 8,
                   redNightVisualEnabled() ? lv_color_make(255, 112, 112)
                                           : COLOR_TEXT);
  drawAnalogCircle(target, center, 4,
                   redNightVisualEnabled() ? lv_color_make(62, 0, 0)
                                           : lv_color_make(0, 35, 50));
}

lv_obj_t *makeAnalogLayer(lv_obj_t *parent, lv_event_cb_t drawCallback) {
  lv_obj_t *layer = lv_obj_create(parent);
  lv_obj_set_size(layer, 480, 480);
  lv_obj_set_pos(layer, 0, 0);
  lv_obj_set_style_bg_opa(layer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(layer, 0, 0);
  lv_obj_set_style_pad_all(layer, 0, 0);
  lv_obj_set_style_radius(layer, 0, 0);
  lv_obj_clear_flag(layer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(layer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(layer, drawCallback, LV_EVENT_DRAW_MAIN, nullptr);
  return layer;
}

void clearAnalogDialCache() {
  if (analogDialCache.rowOffsets != nullptr)
    heap_caps_free(analogDialCache.rowOffsets);
  if (analogDialCache.runs != nullptr)
    heap_caps_free(analogDialCache.runs);
  analogDialCache = {};
}

bool rebuildAnalogDialCache() {
  constexpr size_t PIXEL_COUNT = 480U * 480U;
  auto *pixels = static_cast<lv_color_t *>(heap_caps_malloc(
      PIXEL_COUNT * sizeof(lv_color_t),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (pixels == nullptr) {
    clearAnalogDialCache();
    return false;
  }

  lv_obj_t *canvas = lv_canvas_create(lv_layer_sys());
  lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
  lv_canvas_set_buffer(canvas, pixels, 480, 480, LV_IMG_CF_TRUE_COLOR);
  lv_canvas_fill_bg(canvas, COLOR_BACKGROUND, LV_OPA_COVER);
  renderAnalogDial({nullptr, canvas}, {ANALOG_CENTER_X, ANALOG_CENTER_Y});

  uint32_t runCount = 0;
  for (uint16_t y = 0; y < 480; ++y) {
    const lv_color_t *row = pixels + static_cast<size_t>(y) * 480U;
    ++runCount;
    for (uint16_t x = 1; x < 480; ++x) {
      if (row[x].full != row[x - 1].full) ++runCount;
    }
  }

  auto *newOffsets = static_cast<uint32_t *>(heap_caps_malloc(
      481U * sizeof(uint32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  auto *newRuns = static_cast<AnalogDialRun *>(heap_caps_malloc(
      static_cast<size_t>(runCount) * sizeof(AnalogDialRun),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (newOffsets == nullptr || newRuns == nullptr) {
    if (newOffsets != nullptr) heap_caps_free(newOffsets);
    if (newRuns != nullptr) heap_caps_free(newRuns);
    lv_obj_del(canvas);
    heap_caps_free(pixels);
    clearAnalogDialCache();
    return false;
  }

  uint32_t runIndex = 0;
  for (uint16_t y = 0; y < 480; ++y) {
    newOffsets[y] = runIndex;
    const lv_color_t *row = pixels + static_cast<size_t>(y) * 480U;
    uint16_t runStart = 0;
    for (uint16_t x = 1; x <= 480; ++x) {
      if (x == 480 || row[x].full != row[runStart].full) {
        newRuns[runIndex++] = {
            static_cast<uint16_t>(x - runStart), row[runStart]};
        runStart = x;
      }
    }
  }
  newOffsets[480] = runIndex;

  lv_obj_del(canvas);
  heap_caps_free(pixels);
  clearAnalogDialCache();
  analogDialCache.rowOffsets = newOffsets;
  analogDialCache.runs = newRuns;
  analogDialCache.runCount = runIndex;
  Serial.printf("[analog] Cache ciferniku: %lu behu, %lu B PSRAM\n",
                static_cast<unsigned long>(runIndex),
                static_cast<unsigned long>(
                    481U * sizeof(uint32_t) +
                    static_cast<size_t>(runIndex) * sizeof(AnalogDialRun)));
  return true;
}

bool analogHandAngles(float &hourAngle, float &minuteAngle,
                      float &secondAngle) {
  int hour = 0;
  int minute = 0;
  if (sscanf(displayedTimeText, "%d:%d", &hour, &minute) != 2) return false;
  const uint8_t second = displayedSecond > 59 ? 0 : displayedSecond;
  hourAngle = (hour % 12) * 30.0f + minute * 0.5f + second / 120.0f;
  minuteAngle = minute * 6.0f + second * 0.1f;
  secondAngle = second * 6.0f;
  return true;
}

void invalidateAnalogHandArea(float angle, float rearLength,
                              float frontLength, lv_coord_t width) {
  if (!analogLayoutEnabled() || analogHandsLayer == nullptr) return;
  lv_area_t coordinates;
  lv_obj_get_coords(analogHandsLayer, &coordinates);
  const lv_point_t center = {
      static_cast<lv_coord_t>(coordinates.x1 + ANALOG_CENTER_X),
      static_cast<lv_coord_t>(coordinates.y1 + ANALOG_CENTER_Y),
  };
  const lv_point_t from = analogPoint(center, angle + 180.0f, rearLength);
  const lv_point_t to = analogPoint(center, angle, frontLength);
  const lv_coord_t margin = width / 2 + 3;
  const float deltaX = static_cast<float>(to.x - from.x);
  const float deltaY = static_cast<float>(to.y - from.y);
  const bool splitAlongX = std::fabs(deltaX) >= std::fabs(deltaY);
  const float axisFrom = splitAlongX ? from.x : from.y;
  const float axisTo = splitAlongX ? to.x : to.y;
  const float otherFrom = splitAlongX ? from.y : from.x;
  const float otherTo = splitAlongX ? to.y : to.x;
  const float axisDelta = axisTo - axisFrom;
  const float otherDelta = otherTo - otherFrom;
  const lv_coord_t lineAxisMin = min(axisFrom, axisTo);
  const lv_coord_t lineAxisMax = max(axisFrom, axisTo);
  const lv_coord_t dirtyAxisMin = lineAxisMin - margin;
  const lv_coord_t dirtyAxisMax = lineAxisMax + margin;
  const lv_coord_t firstStrip =
      (dirtyAxisMin / ANALOG_HAND_INVALIDATION_STRIP) *
      ANALOG_HAND_INVALIDATION_STRIP;

  auto otherCoordinate = [&](float axis) {
    if (std::fabs(axisDelta) < 0.001f) return otherFrom;
    return otherFrom + (axis - axisFrom) * otherDelta / axisDelta;
  };

  for (lv_coord_t stripStart = firstStrip; stripStart <= dirtyAxisMax;
       stripStart += ANALOG_HAND_INVALIDATION_STRIP) {
    const lv_coord_t stripEnd =
        stripStart + ANALOG_HAND_INVALIDATION_STRIP - 1;
    const lv_coord_t areaAxisMin = max(dirtyAxisMin, stripStart);
    const lv_coord_t areaAxisMax = min(dirtyAxisMax, stripEnd);
    const float sampleAxisMin =
        constrain(static_cast<float>(areaAxisMin),
                  static_cast<float>(lineAxisMin),
                  static_cast<float>(lineAxisMax));
    const float sampleAxisMax =
        constrain(static_cast<float>(areaAxisMax),
                  static_cast<float>(lineAxisMin),
                  static_cast<float>(lineAxisMax));
    const float otherAtMin = otherCoordinate(sampleAxisMin);
    const float otherAtMax = otherCoordinate(sampleAxisMax);
    const lv_coord_t areaOtherMin = static_cast<lv_coord_t>(
        std::floor(min(otherAtMin, otherAtMax) - margin));
    const lv_coord_t areaOtherMax = static_cast<lv_coord_t>(
        std::ceil(max(otherAtMin, otherAtMax) + margin));

    lv_area_t area;
    if (splitAlongX) {
      area = {areaAxisMin, areaOtherMin, areaAxisMax, areaOtherMax};
    } else {
      area = {areaOtherMin, areaAxisMin, areaOtherMax, areaAxisMax};
    }
    lv_obj_invalidate_area(analogHandsLayer, &area);
  }
}

void invalidateAnalogHands(bool hourAndMinute = true,
                           bool second = true) {
  float hourAngle = 0.0f;
  float minuteAngle = 0.0f;
  float secondAngle = 0.0f;
  if (!analogHandAngles(hourAngle, minuteAngle, secondAngle)) return;
  if (hourAndMinute) {
    invalidateAnalogHandArea(hourAngle, 22.0f, 145.0f, 21);
    invalidateAnalogHandArea(minuteAngle, 25.0f, 178.0f, 17);
  }
  if (second) invalidateAnalogHandArea(secondAngle, 34.0f, 192.0f, 3);
}

void drawAnalogOutlinedLabelEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_DRAW_MAIN_BEGIN ||
      !analogLayoutEnabled() || !analogValuesAboveHandsEnabled) {
    return;
  }

  lv_obj_t *label = lv_event_get_target(event);
  lv_draw_ctx_t *drawContext = lv_event_get_draw_ctx(event);
  lv_draw_label_dsc_t descriptor;
  lv_draw_label_dsc_init(&descriptor);
  lv_obj_init_draw_label_dsc(label, LV_PART_MAIN, &descriptor);
  descriptor.color = COLOR_BACKGROUND;
  descriptor.opa = LV_OPA_COVER;
  descriptor.flag = LV_TEXT_FLAG_FIT;

  lv_area_t coordinates;
  lv_obj_get_content_coords(label, &coordinates);
  static constexpr int8_t OFFSETS[][2] = {
      {-1, -1}, {0, -1}, {1, -1}, {-1, 0},
      {1, 0},   {-1, 1}, {0, 1},  {1, 1},
  };
  const char *text = lv_label_get_text(label);
  for (const auto &offset : OFFSETS) {
    lv_area_t outlineCoordinates = coordinates;
    lv_area_move(&outlineCoordinates, offset[0], offset[1]);
    lv_draw_label(drawContext, &descriptor, &outlineCoordinates, text, nullptr);
  }
}

void updateAnalogValueLayerOrder() {
  if (analogHandsLayer == nullptr) return;
  lv_obj_move_foreground(analogHandsLayer);
  if (!analogValuesAboveHandsEnabled) return;

  lv_obj_t *labels[] = {
      dateLabel,
      namedayLabel,
      analogOutsideTitleLabel,
      analogOutsideValueLabel,
      analogOutsideDecimalLabel,
      analogOutsideUnitLabel,
      analogRoomTitleLabel,
      analogRoomValueLabel,
      analogRoomDecimalLabel,
      analogRoomUnitLabel,
      analogMetricATitleLabel,
      analogMetricAValueLabel,
      analogMetricAUnitLabel,
      analogMetricBTitleLabel,
      analogMetricBValueLabel,
      analogMetricBUnitLabel,
  };
  for (lv_obj_t *label : labels) lv_obj_move_foreground(label);
}

void createAnalogLayout(lv_obj_t *content) {
  analogDialLayer = makeAnalogLayer(content, drawAnalogDialEvent);
  lv_obj_move_background(analogDialLayer);
  rebuildAnalogDialCache();

  lv_obj_t *digitalOnly[] = {
      timeLabel,          outsideTitleLabel,   outsideIntegerLabel,
      outsideDecimalLabel, outsideUnitLabel,   roomTitleLabel,
      roomIntegerLabel,   roomDecimalLabel,    roomUnitLabel,
      outsideIconLabel,   co2TitleLabel,       co2ValueLabel,
      co2UnitLabel,       humidityTitleLabel,  humidityValueLabel,
      humidityUnitLabel,  roomWeatherImage,    weatherAnimation,
      roomWeatherAnimation, roomIconLabel,
  };
  if (analogLayoutEnabled()) {
    for (lv_obj_t *object : digitalOnly)
      lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
  }

  lv_obj_set_style_text_font(dateLabel, &clock_czech_20, 0);
  lv_obj_set_style_text_letter_space(dateLabel, 3, 0);
  alignCenter(dateLabel, 0, -142);

  analogOutsideTitleLabel = makeLabel(content, &clock_czech_20, COLOR_OUTSIDE);
  lv_obj_set_style_text_letter_space(analogOutsideTitleLabel, 1, 0);
  alignCenter(analogOutsideTitleLabel, -112, -8);
  analogOutsideValueLabel =
      makeLabel(content, &lv_font_montserrat_36, COLOR_OUTSIDE);
  analogOutsideDecimalLabel =
      makeLabel(content, &lv_font_montserrat_28, COLOR_OUTSIDE);
  analogOutsideUnitLabel =
      makeLabel(content, &clock_unit_28, COLOR_OUTSIDE);

  analogRoomTitleLabel = makeLabel(content, &clock_czech_20, COLOR_ROOM);
  lv_obj_set_style_text_letter_space(analogRoomTitleLabel, 1, 0);
  alignCenter(analogRoomTitleLabel, 112, -8);
  analogRoomValueLabel =
      makeLabel(content, &lv_font_montserrat_36, COLOR_ROOM);
  analogRoomDecimalLabel =
      makeLabel(content, &lv_font_montserrat_28, COLOR_ROOM);
  analogRoomUnitLabel = makeLabel(content, &clock_unit_28, COLOR_ROOM);

  analogMetricATitleLabel = makeLabel(content, &clock_czech_16, COLOR_AIR);
  analogMetricAValueLabel =
      makeLabel(content, &lv_font_montserrat_32, COLOR_AIR);
  analogMetricAUnitLabel = makeLabel(content, &clock_czech_16, COLOR_AIR);

  analogMetricBTitleLabel =
      makeLabel(content, &clock_czech_16, COLOR_HUMIDITY);
  analogMetricBValueLabel =
      makeLabel(content, &lv_font_montserrat_32, COLOR_HUMIDITY);
  analogMetricBUnitLabel =
      makeLabel(content, &clock_czech_16, COLOR_HUMIDITY);

  analogMetricDivider = lv_obj_create(content);
  lv_obj_set_size(analogMetricDivider, 138, 2);
  lv_obj_set_style_radius(analogMetricDivider, 1, 0);
  lv_obj_set_style_border_width(analogMetricDivider, 0, 0);
  lv_obj_set_style_bg_color(analogMetricDivider, analogTone(), 0);
  lv_obj_set_style_bg_opa(analogMetricDivider, LV_OPA_80, 0);
  lv_obj_clear_flag(analogMetricDivider, LV_OBJ_FLAG_SCROLLABLE);
  alignCenter(analogMetricDivider, 0, 120);

  lv_img_set_zoom(weatherImage, 256);
  alignCenter(weatherImage, 0, -70);
  alignCenter(weatherAnimation, 0, -70);

  analogHandsLayer = makeAnalogLayer(content, drawAnalogHandsEvent);
  lv_obj_move_foreground(analogHandsLayer);
  lv_obj_t *outlinedLabels[] = {
      dateLabel,
      namedayLabel,
      analogOutsideTitleLabel,
      analogOutsideValueLabel,
      analogOutsideDecimalLabel,
      analogOutsideUnitLabel,
      analogRoomTitleLabel,
      analogRoomValueLabel,
      analogRoomDecimalLabel,
      analogRoomUnitLabel,
      analogMetricATitleLabel,
      analogMetricAValueLabel,
      analogMetricAUnitLabel,
      analogMetricBTitleLabel,
      analogMetricBValueLabel,
      analogMetricBUnitLabel,
  };
  for (lv_obj_t *label : outlinedLabels) {
    lv_obj_add_event_cb(label, drawAnalogOutlinedLabelEvent,
                        LV_EVENT_DRAW_MAIN_BEGIN, nullptr);
  }
  updateAnalogValueLayerOrder();
  if (!analogLayoutEnabled()) {
    lv_obj_t *analogOnly[] = {
        analogDialLayer,          analogHandsLayer,
        analogOutsideTitleLabel, analogOutsideValueLabel,
        analogOutsideDecimalLabel, analogOutsideUnitLabel,
        analogRoomTitleLabel,    analogRoomValueLabel,
        analogRoomDecimalLabel,  analogRoomUnitLabel,
        analogMetricATitleLabel, analogMetricAValueLabel,
        analogMetricAUnitLabel,  analogMetricBTitleLabel,
        analogMetricBValueLabel, analogMetricBUnitLabel,
        analogMetricDivider,
    };
    for (lv_obj_t *object : analogOnly)
      lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
  }
}

lv_color_t interpolateColor(uint32_t from, uint32_t to, float progress) {
  progress = constrain(progress, 0.0f, 1.0f);
  const uint8_t fromRed = static_cast<uint8_t>((from >> 16) & 0xFF);
  const uint8_t fromGreen = static_cast<uint8_t>((from >> 8) & 0xFF);
  const uint8_t fromBlue = static_cast<uint8_t>(from & 0xFF);
  const uint8_t toRed = static_cast<uint8_t>((to >> 16) & 0xFF);
  const uint8_t toGreen = static_cast<uint8_t>((to >> 8) & 0xFF);
  const uint8_t toBlue = static_cast<uint8_t>(to & 0xFF);
  const uint8_t red = static_cast<uint8_t>(
      fromRed + (static_cast<int>(toRed) - fromRed) * progress);
  const uint8_t green = static_cast<uint8_t>(
      fromGreen + (static_cast<int>(toGreen) - fromGreen) * progress);
  const uint8_t blue = static_cast<uint8_t>(
      fromBlue + (static_cast<int>(toBlue) - fromBlue) * progress);
  return LV_COLOR_MAKE(red, green, blue);
}

lv_color_t metricColorForValue(float value,
                               const ClockMetricColorScale &scale) {
  if (std::isnan(value)) return COLOR_MUTED;
  const uint8_t count = constrain(
      scale.count, static_cast<uint8_t>(1),
      static_cast<uint8_t>(CLOCK_METRIC_COLOR_POINT_COUNT));
  if (value <= scale.points[0].value) {
    return configuredColor(scale.points[0].color);
  }
  for (uint8_t index = 1; index < count; ++index) {
    if (value < scale.points[index].value) {
      const ClockMetricColorPoint &from = scale.points[index - 1];
      const ClockMetricColorPoint &to = scale.points[index];
      return interpolateColor(from.color, to.color,
                              (value - from.value) / (to.value - from.value));
    }
  }
  return configuredColor(scale.points[count - 1].color);
}

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, lv_color_t color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  return label;
}

void alignCenter(lv_obj_t *object, int x, int y) {
  lv_obj_align(object, LV_ALIGN_CENTER, x, y);
}

void alignAnalogValue(lv_obj_t *valueLabel, lv_obj_t *unitLabel, int centerX,
                      int centerY) {
  lv_obj_update_layout(valueLabel);
  lv_obj_update_layout(unitLabel);
  constexpr int UNIT_GAP = 6;
  const int totalWidth = lv_obj_get_width(valueLabel) + UNIT_GAP +
                         lv_obj_get_width(unitLabel);
  const int left = centerX - totalWidth / 2;
  lv_obj_align(valueLabel, LV_ALIGN_CENTER,
               left + lv_obj_get_width(valueLabel) / 2, centerY);
  lv_obj_align(unitLabel, LV_ALIGN_CENTER,
               left + lv_obj_get_width(valueLabel) + UNIT_GAP +
                   lv_obj_get_width(unitLabel) / 2,
               centerY + 5);
}

bool setLabelTextIfChanged(lv_obj_t *label, const char *text) {
  if (strcmp(lv_label_get_text(label), text) == 0) return false;
  lv_label_set_text(label, text);
  return true;
}

bool setAnalogValue(lv_obj_t *valueLabel, lv_obj_t *unitLabel, float value,
                    uint8_t decimals, const char *unit, int centerX,
                    int centerY) {
  char valueText[20];
  if (std::isnan(value)) {
    strlcpy(valueText, "--", sizeof(valueText));
  } else {
    snprintf(valueText, sizeof(valueText), "%.*f", constrain(decimals, 0, 2),
             value);
    char *decimalPoint = strchr(valueText, '.');
    if (decimalPoint != nullptr) *decimalPoint = ',';
  }
  const bool changed = setLabelTextIfChanged(valueLabel, valueText) |
                       setLabelTextIfChanged(unitLabel, unit);
  if (changed) alignAnalogValue(valueLabel, unitLabel, centerX, centerY);
  return changed;
}

void setAnalogTemperature(lv_obj_t *integerLabel, lv_obj_t *decimalLabel,
                          lv_obj_t *unitLabel, float value, uint8_t decimals,
                          const char *unit, int centerX, int centerY) {
  char integerText[20];
  char decimalText[8] = "";
  if (std::isnan(value)) {
    strlcpy(integerText, "--", sizeof(integerText));
  } else {
    char valueText[20];
    snprintf(valueText, sizeof(valueText), "%.*f", constrain(decimals, 0, 2),
             value);
    char *decimalPoint = strchr(valueText, '.');
    if (decimalPoint != nullptr) {
      snprintf(decimalText, sizeof(decimalText), ",%s", decimalPoint + 1);
      *decimalPoint = '\0';
    }
    strlcpy(integerText, valueText, sizeof(integerText));
  }

  const bool changed = setLabelTextIfChanged(integerLabel, integerText) |
                       setLabelTextIfChanged(decimalLabel, decimalText) |
                       setLabelTextIfChanged(unitLabel, unit);
  if (!changed) return;
  lv_obj_update_layout(integerLabel);
  lv_obj_update_layout(decimalLabel);
  lv_obj_update_layout(unitLabel);

  constexpr int DECIMAL_GAP = 1;
  constexpr int UNIT_GAP = 5;
  const int decimalWidth =
      decimalText[0] == '\0' ? 0 : lv_obj_get_width(decimalLabel);
  const int totalWidth = lv_obj_get_width(integerLabel) + decimalWidth +
                         (decimalWidth > 0 ? DECIMAL_GAP : 0) + UNIT_GAP +
                         lv_obj_get_width(unitLabel);
  int left = centerX - totalWidth / 2;
  alignCenter(integerLabel, left + lv_obj_get_width(integerLabel) / 2,
              centerY);
  left += lv_obj_get_width(integerLabel);
  if (decimalWidth > 0) {
    left += DECIMAL_GAP;
    alignCenter(decimalLabel, left + decimalWidth / 2, centerY + 4);
    left += decimalWidth;
  }
  left += UNIT_GAP;
  alignCenter(unitLabel, left + lv_obj_get_width(unitLabel) / 2, centerY + 4);
}

int analogMetricLineWidth(lv_obj_t *titleLabel, lv_obj_t *valueLabel,
                          lv_obj_t *unitLabel) {
  lv_obj_update_layout(titleLabel);
  lv_obj_update_layout(valueLabel);
  lv_obj_update_layout(unitLabel);
  constexpr int TITLE_GAP = 12;
  constexpr int UNIT_GAP = 7;
  return lv_obj_get_width(titleLabel) + TITLE_GAP +
         lv_obj_get_width(valueLabel) + UNIT_GAP + lv_obj_get_width(unitLabel);
}

void alignAnalogMetricLine(lv_obj_t *titleLabel, lv_obj_t *valueLabel,
                           lv_obj_t *unitLabel, int centerY,
                           int unitOffsetY = 6) {
  constexpr int TITLE_GAP = 12;
  constexpr int UNIT_GAP = 7;
  const int totalWidth =
      analogMetricLineWidth(titleLabel, valueLabel, unitLabel);
  int left = -totalWidth / 2;
  alignCenter(titleLabel, left + lv_obj_get_width(titleLabel) / 2, centerY + 5);
  left += lv_obj_get_width(titleLabel) + TITLE_GAP;
  alignCenter(valueLabel, left + lv_obj_get_width(valueLabel) / 2, centerY);
  left += lv_obj_get_width(valueLabel) + UNIT_GAP;
  alignCenter(unitLabel, left + lv_obj_get_width(unitLabel) / 2,
              centerY + unitOffsetY);
}

void updateAnalogMetricDivider() {
  if (analogMetricDivider == nullptr) return;
  constexpr int MINIMUM_WIDTH = 138;
  constexpr int HORIZONTAL_PADDING = 12;
  constexpr int MAXIMUM_WIDTH = 250;
  const int widestLine =
      max(analogMetricLineWidth(analogMetricATitleLabel,
                                analogMetricAValueLabel,
                                analogMetricAUnitLabel),
          analogMetricLineWidth(analogMetricBTitleLabel,
                                analogMetricBValueLabel,
                                analogMetricBUnitLabel));
  const int dividerWidth =
      constrain(widestLine + HORIZONTAL_PADDING, MINIMUM_WIDTH, MAXIMUM_WIDTH);
  if (lv_obj_get_width(analogMetricDivider) != dividerWidth)
    lv_obj_set_width(analogMetricDivider, dividerWidth);
  alignCenter(analogMetricDivider, 0, 120);
}

void applyAnalogColors() {
  if (!analogLayoutEnabled() || analogDialLayer == nullptr) return;
  const bool redNight = redNightVisualEnabled();
  const lv_color_t unifiedColor = analogTone();
  const lv_color_t outside =
      redNight ? COLOR_ERROR
               : analogMonochromeValuesEnabled
                     ? unifiedColor
                     : metricColorForValue(currentValues.leftTemperatureC,
                                           leftValueColorScale);
  const lv_color_t room =
      redNight ? COLOR_ERROR
               : analogMonochromeValuesEnabled
                     ? unifiedColor
                     : metricColorForValue(currentValues.rightTemperatureC,
                                           rightValueColorScale);
  const lv_color_t metricA =
      redNight ? COLOR_ERROR
               : analogMonochromeValuesEnabled
                     ? unifiedColor
                     : metricColorForValue(currentValues.metricAValue,
                                           metricAColorScale);
  const lv_color_t metricB =
      redNight ? COLOR_ERROR
               : analogMonochromeValuesEnabled
                     ? unifiedColor
                     : metricColorForValue(currentValues.metricBValue,
                                           metricBColorScale);
  lv_obj_t *outsideLabels[] = {
      analogOutsideTitleLabel, analogOutsideValueLabel,
      analogOutsideDecimalLabel, analogOutsideUnitLabel};
  lv_obj_t *roomLabels[] = {analogRoomTitleLabel, analogRoomValueLabel,
                            analogRoomDecimalLabel, analogRoomUnitLabel};
  lv_obj_t *metricALabels[] = {analogMetricATitleLabel,
                               analogMetricAValueLabel,
                               analogMetricAUnitLabel};
  lv_obj_t *metricBLabels[] = {analogMetricBTitleLabel,
                               analogMetricBValueLabel,
                               analogMetricBUnitLabel};
  for (lv_obj_t *label : outsideLabels) setTextColor(label, outside);
  for (lv_obj_t *label : roomLabels) setTextColor(label, room);
  for (lv_obj_t *label : metricALabels) setTextColor(label, metricA);
  for (lv_obj_t *label : metricBLabels) setTextColor(label, metricB);
  lv_obj_set_style_bg_color(
      analogMetricDivider, redNight ? COLOR_ERROR : unifiedColor, 0);
  setTextColor(dateLabel, redNight ? COLOR_ERROR
                                  : analogMonochromeValuesEnabled
                                        ? unifiedColor
                                        : configuredColor(analogDateColor));
  if (namedayLabel != nullptr)
    setTextColor(namedayLabel, redNight ? COLOR_ERROR
                                        : analogMonochromeValuesEnabled
                                              ? unifiedColor
                                              : configuredColor(analogDateColor));
  const lv_color_t weather =
      redNight ? COLOR_ERROR
               : analogMonochromeValuesEnabled
                     ? unifiedColor
                     : configuredColor(monochromeWeatherIconColor);
  const bool animationIsMonochrome =
      strncmp(weatherAnimationKey, "monochrome-", 11) == 0;
  lv_obj_set_style_img_recolor(weatherImage, weather, 0);
  lv_obj_set_style_img_recolor_opa(weatherImage, LV_OPA_COVER, 0);
  lv_obj_set_style_img_recolor(weatherAnimation, weather, 0);
  lv_obj_set_style_img_recolor_opa(
      weatherAnimation,
      redNight || analogMonochromeValuesEnabled || animationIsMonochrome
          ? LV_OPA_COVER
          : LV_OPA_TRANSP,
      0);
  setTextColor(roomIconLabel, room);
}

void alignConnectionStatusIcons() {
  const int STATUS_Y = analogLayoutEnabled() ? -177 : 202;
  constexpr int STATUS_SPACING = 28;
  const bool redNightVisual = redNightVisualEnabled();
  const bool showWifi = !redNightVisual || wifiConnected;
  const bool showHomeAssistant =
      homeAssistantStatusRelevant &&
      (!redNightVisual || currentValues.homeAssistantOnline);
  const bool showWeb = webActive;

  lv_obj_t *icons[] = {wifiStatusLabel, statusLabel, webStatusLabel};
  const bool visible[] = {showWifi, showHomeAssistant, showWeb};
  int visibleCount = 0;
  for (bool iconVisible : visible) {
    if (iconVisible) ++visibleCount;
  }

  int visibleIndex = 0;
  for (int index = 0; index < 3; ++index) {
    if (!visible[index]) {
      lv_obj_add_flag(icons[index], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(icons[index], LV_OBJ_FLAG_HIDDEN);
    const int x =
        visibleIndex * STATUS_SPACING - (visibleCount - 1) * STATUS_SPACING / 2;
    alignCenter(icons[index], x, STATUS_Y);
    ++visibleIndex;
  }
}

void applyConnectionStatusColors() {
  if (redNightVisualEnabled()) {
    setTextColor(wifiStatusLabel, COLOR_ERROR);
    setTextColor(statusLabel, COLOR_ERROR);
    setTextColor(webStatusLabel, COLOR_ERROR);
  } else {
    const lv_color_t connectedColor =
        analogLayoutEnabled() ? analogTone() : COLOR_AIR;
    const lv_color_t settingsColor =
        analogLayoutEnabled() ? analogTone() : COLOR_OUTSIDE;
    setTextColor(wifiStatusLabel,
                 wifiConnected ? connectedColor : COLOR_ERROR);
    setTextColor(statusLabel,
                 currentValues.homeAssistantOnline ? connectedColor
                                                   : COLOR_ERROR);
    setTextColor(webStatusLabel, settingsColor);
  }
  alignConnectionStatusIcons();
}

void makeChildrenTapThrough(lv_obj_t *parent) {
  const uint32_t childCount = lv_obj_get_child_cnt(parent);
  for (uint32_t index = 0; index < childCount; ++index) {
    lv_obj_t *child = lv_obj_get_child(parent, index);
    lv_obj_clear_flag(child, LV_OBJ_FLAG_CLICKABLE);
    makeChildrenTapThrough(child);
  }
}

lv_obj_t *makeDivider(lv_obj_t *parent, int width, int height, int x, int y) {
  lv_obj_t *divider = lv_obj_create(parent);
  lv_obj_set_size(divider, width, height);
  lv_obj_set_style_radius(divider, 3, 0);
  lv_obj_set_style_border_width(divider, 0, 0);
  lv_obj_set_style_bg_color(divider, COLOR_DIVIDER, 0);
  alignCenter(divider, x, y);
  lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
  return divider;
}

void setTopValue(float value, uint8_t decimals, int centerX,
                 lv_obj_t *integerLabel, lv_obj_t *decimalLabel,
                 lv_obj_t *unitLabel) {
  char valueText[20];
  if (std::isnan(value)) {
    snprintf(valueText, sizeof(valueText), "--");
  } else {
    snprintf(valueText, sizeof(valueText), "%.*f", constrain(decimals, 0, 2),
             value);
  }

  char *decimalPoint = strchr(valueText, '.');
  char decimalText[8] = "";
  if (decimalPoint != nullptr) {
    snprintf(decimalText, sizeof(decimalText), ",%s", decimalPoint + 1);
    *decimalPoint = '\0';
  }

  lv_label_set_text(integerLabel, valueText);
  lv_label_set_text(decimalLabel, decimalText);
  lv_obj_update_layout(integerLabel);
  lv_obj_update_layout(decimalLabel);
  lv_obj_update_layout(unitLabel);

  constexpr int DECIMAL_GAP = 1;
  constexpr int UNIT_GAP = 6;
  const int decimalWidth = decimalText[0] == '\0' ? 0 : lv_obj_get_width(decimalLabel);
  const int totalWidth = lv_obj_get_width(integerLabel) + decimalWidth +
                         (decimalWidth > 0 ? DECIMAL_GAP : 0) + UNIT_GAP +
                         lv_obj_get_width(unitLabel);
  int x = centerX - totalWidth / 2;
  lv_obj_set_pos(integerLabel, x, 262);
  x += lv_obj_get_width(integerLabel) + (decimalWidth > 0 ? DECIMAL_GAP : 0);
  lv_obj_set_pos(decimalLabel, x, 275);
  x += decimalWidth + UNIT_GAP;
  lv_obj_set_pos(unitLabel, x, 264);
}

void alignCo2Value() {
  lv_obj_update_layout(co2ValueLabel);
  lv_obj_update_layout(co2UnitLabel);
  constexpr int GAP = 7;
  const int totalWidth = lv_obj_get_width(co2ValueLabel) + GAP +
                         lv_obj_get_width(co2UnitLabel);
  const int x = 240 - totalWidth / 2;
  lv_obj_set_pos(co2ValueLabel, x, 320);
  lv_obj_set_pos(co2UnitLabel, x + lv_obj_get_width(co2ValueLabel) + GAP, 334);
}

void alignMetricBValue() {
  lv_obj_update_layout(humidityTitleLabel);
  lv_obj_update_layout(humidityValueLabel);
  lv_obj_update_layout(humidityUnitLabel);
  constexpr int TITLE_VALUE_GAP = 12;
  constexpr int VALUE_UNIT_GAP = 4;
  const int titleWidth = lv_obj_get_width(humidityTitleLabel);
  const int valueWidth = lv_obj_get_width(humidityValueLabel);
  const int unitWidth = lv_obj_get_width(humidityUnitLabel);
  const int totalWidth = titleWidth + TITLE_VALUE_GAP + valueWidth +
                         (unitWidth > 0 ? VALUE_UNIT_GAP + unitWidth : 0);
  int x = -totalWidth / 2;
  alignCenter(humidityTitleLabel, x + titleWidth / 2, 151);
  x += titleWidth + TITLE_VALUE_GAP;
  alignCenter(humidityValueLabel, x + valueWidth / 2, 151);
  x += valueWidth + VALUE_UNIT_GAP;
  alignCenter(humidityUnitLabel, x + unitWidth / 2, 154);
}

void formatMetricValue(char *buffer, size_t bufferSize, float value,
                       uint8_t decimals) {
  if (std::isnan(value)) {
    strlcpy(buffer, "--", bufferSize);
    return;
  }
  snprintf(buffer, bufferSize, "%.*f", constrain(decimals, 0, 2), value);
}

void makeSecondRing(lv_obj_t *parent) {
  for (int index = 0; index < SECOND_DOT_COUNT; ++index) {
    const float angle = (static_cast<float>(index) / SECOND_DOT_COUNT) *
                            2.0f * PI_VALUE -
                        PI_VALUE / 2.0f;
    const int x = 240 + static_cast<int>(std::round(std::cos(angle) *
                                                    SECOND_RING_RADIUS));
    const int y = 240 + static_cast<int>(std::round(std::sin(angle) *
                                                    SECOND_RING_RADIUS));
    secondDotCenterX[index] = x;
    secondDotCenterY[index] = y;
    secondDots[index] = lv_obj_create(parent);
    lv_obj_set_size(secondDots[index], secondRingBackgroundDotSize,
                    secondRingBackgroundDotSize);
    lv_obj_set_style_radius(secondDots[index], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(secondDots[index], 0, 0);
    lv_obj_set_style_bg_color(secondDots[index],
                              lv_color_make(secondRingBackgroundBrightness,
                                            secondRingBackgroundBrightness,
                                            secondRingBackgroundBrightness),
                              0);
    lv_obj_set_style_pad_all(secondDots[index], 0, 0);
    lv_obj_set_pos(secondDots[index], x - secondRingBackgroundDotSize / 2,
                   y - secondRingBackgroundDotSize / 2);
    lv_obj_clear_flag(secondDots[index], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(secondDots[index], LV_OBJ_FLAG_CLICKABLE);
  }

  auto makeArc = [parent]() {
    lv_obj_t *arc = lv_arc_create(parent);
    lv_arc_set_rotation(arc, 270);
    lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
    lv_obj_remove_style(arc, nullptr, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    return arc;
  };
  auto makeBridge = [parent]() {
    lv_obj_t *line = lv_line_create(parent);
    lv_obj_set_size(line, 480, 480);
    lv_obj_set_pos(line, 0, 0);
    lv_obj_set_style_line_rounded(line, false, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
    return line;
  };
  secondLineBackgroundArc = makeArc();
  secondLineFadeArc = makeArc();
  secondLineActiveArc = makeArc();
  secondLineActiveBridge = makeBridge();

  secondCometHead = lv_obj_create(parent);
  lv_obj_set_style_radius(secondCometHead, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(secondCometHead, 0, 0);
  lv_obj_set_style_pad_all(secondCometHead, 0, 0);
  lv_obj_clear_flag(secondCometHead, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(secondCometHead, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(secondCometHead, LV_OBJ_FLAG_HIDDEN);
}

lv_color_t brightnessScaledColor(uint32_t color, uint8_t brightness) {
  const uint8_t red = static_cast<uint8_t>(
      (((color >> 16) & 0xFF) * brightness + 127) / 255);
  const uint8_t green = static_cast<uint8_t>(
      (((color >> 8) & 0xFF) * brightness + 127) / 255);
  const uint8_t blue = static_cast<uint8_t>(
      ((color & 0xFF) * brightness + 127) / 255);
  return lv_color_make(red, green, blue);
}

uint8_t brightnessScaledChannel(uint32_t color, uint8_t brightness,
                                uint8_t shift) {
  return static_cast<uint8_t>(
      (((color >> shift) & 0xFF) * brightness + 127) / 255);
}

lv_point_t secondLinePoint(float fraction) {
  const float angle = fraction * 2.0f * PI_VALUE - PI_VALUE / 2.0f;
  return {
      static_cast<lv_coord_t>(240 + std::round(std::cos(angle) * SECOND_RING_RADIUS)),
      static_cast<lv_coord_t>(240 + std::round(std::sin(angle) * SECOND_RING_RADIUS)),
  };
}

void configureSecondArc(lv_obj_t *arc, uint8_t width, lv_color_t color) {
  const int diameter = static_cast<int>(SECOND_RING_RADIUS * 2.0f) + width;
  lv_obj_set_size(arc, diameter, diameter);
  alignCenter(arc, 0, 0);
  lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, color, LV_PART_MAIN);
}

void renderSecondDots(unsigned long now) {
  const unsigned long elapsed = now - secondFadeStartedAt;
  for (int index = 0; index < SECOND_DOT_COUNT; ++index) {
    const bool dotsEffect = secondEffect == CLOCK_SECOND_EFFECT_DOTS;
    if (!secondRingEnabled || !dotsEffect) {
      lv_obj_add_flag(secondDots[index], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(secondDots[index], LV_OBJ_FLAG_HIDDEN);
    float intensity = 0.0f;
    if (secondFadeActive) {
      const unsigned long fadeStart =
          static_cast<unsigned long>(index) * SECOND_FADE_START_SPAN_MS /
          (SECOND_DOT_COUNT - 1);
      if (elapsed <= fadeStart) {
        intensity = 1.0f;
      } else if (elapsed < fadeStart + SECOND_FADE_DOT_MS) {
        const unsigned long dotElapsed = elapsed - fadeStart;
        const float fadeProgress = static_cast<float>(dotElapsed) /
                                   SECOND_FADE_DOT_MS;
        intensity = 1.0f - fadeProgress;
      }
    }

    // Nová minuta má před doznívajícím starým prstencem prioritu.
    if (index < displayedSecond) intensity = 1.0f;
    // Noční režim mění pouze aktivní část prstence. Neaktivní pozadí zůstává
    // v uživatelem nastavené barvě a jasu.
    const uint32_t activeColor =
        redNightVisualEnabled() ? 0xFF0000 : secondDotColor;
    const uint8_t backgroundRed = brightnessScaledChannel(
        secondRingBackgroundColor, secondRingBackgroundBrightness, 16);
    const uint8_t backgroundGreen = brightnessScaledChannel(
        secondRingBackgroundColor, secondRingBackgroundBrightness, 8);
    const uint8_t backgroundBlue = brightnessScaledChannel(
        secondRingBackgroundColor, secondRingBackgroundBrightness, 0);
    const uint8_t activeRed =
        brightnessScaledChannel(activeColor, secondDotBrightness, 16);
    const uint8_t activeGreen =
        brightnessScaledChannel(activeColor, secondDotBrightness, 8);
    const uint8_t activeBlue =
        brightnessScaledChannel(activeColor, secondDotBrightness, 0);
    const uint8_t red = static_cast<uint8_t>(
        backgroundRed + (static_cast<int>(activeRed) - backgroundRed) *
                            intensity);
    const uint8_t green = static_cast<uint8_t>(
        backgroundGreen + (static_cast<int>(activeGreen) - backgroundGreen) *
                              intensity);
    const uint8_t blue = static_cast<uint8_t>(
        backgroundBlue + (static_cast<int>(activeBlue) - backgroundBlue) *
                             intensity);
    const uint8_t renderedSize = constrain(
        static_cast<int>(std::round(
            secondRingBackgroundDotSize +
            (static_cast<int>(secondDotSize) - secondRingBackgroundDotSize) *
                intensity)),
        1, 10);
    lv_obj_set_size(secondDots[index], renderedSize, renderedSize);
    lv_obj_set_pos(secondDots[index],
                   secondDotCenterX[index] - renderedSize / 2,
                   secondDotCenterY[index] - renderedSize / 2);
    lv_obj_set_style_bg_color(
        secondDots[index], lv_color_make(red, green, blue), 0);
  }
}

void renderSecondComet(unsigned long now) {
  const bool visible = secondRingEnabled &&
                       secondEffect == CLOCK_SECOND_EFFECT_COMET;
  if (visible)
    lv_obj_clear_flag(secondCometHead, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(secondCometHead, LV_OBJ_FLAG_HIDDEN);
  if (!visible) return;

  const uint32_t configuredActiveColor =
      redNightVisualEnabled() ? 0xFF0000 : secondDotColor;
  const float elapsedWithinSecond = constrain(
      static_cast<float>(now - secondTickStartedAt) / 1000.0f, 0.0f, 1.0f);
  const float headSecond =
      static_cast<float>(displayedSecond) + elapsedWithinSecond;
  const float pulse = std::sin(elapsedWithinSecond * PI_VALUE);
  const uint8_t pulsingBrightness = constrain(
      static_cast<int>(std::round(secondDotBrightness *
                                  (1.0f + 0.35f * pulse))),
      0, 255);

  // Tečky zůstávají na pevných bodech kružnice. Plynulý pohyb vzniká pouze
  // přeléváním jasu mezi sousedními body, takže ocas nemůže kličkovat do stran.
  for (int index = 0; index < SECOND_DOT_COUNT; ++index) {
    float distanceBehind = headSecond - static_cast<float>(index);
    while (distanceBehind < -30.0f) distanceBehind += 60.0f;
    while (distanceBehind >= 30.0f) distanceBehind -= 60.0f;

    float trailIntensity = 0.0f;
    if (distanceBehind >= 0.45f &&
        distanceBehind <= SECOND_COMET_TRAIL_SECONDS) {
      const float trailPosition =
          distanceBehind / SECOND_COMET_TRAIL_SECONDS;
      trailIntensity = 0.65f *
                       (1.0f - trailPosition) * (1.0f - trailPosition);
    }

    if (trailIntensity < 0.015f) {
      lv_obj_add_flag(secondDots[index], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(secondDots[index], LV_OBJ_FLAG_HIDDEN);
    const uint8_t activeRed =
        brightnessScaledChannel(configuredActiveColor, secondDotBrightness, 16);
    const uint8_t activeGreen =
        brightnessScaledChannel(configuredActiveColor, secondDotBrightness, 8);
    const uint8_t activeBlue =
        brightnessScaledChannel(configuredActiveColor, secondDotBrightness, 0);
    const uint8_t renderedSize = secondDotSize;
    lv_obj_set_size(secondDots[index], renderedSize, renderedSize);
    lv_obj_set_pos(secondDots[index],
                   secondDotCenterX[index] - renderedSize / 2,
                   secondDotCenterY[index] - renderedSize / 2);
    lv_obj_set_style_bg_color(
        secondDots[index],
        lv_color_make(static_cast<uint8_t>(activeRed * trailIntensity),
                      static_cast<uint8_t>(activeGreen * trailIntensity),
                      static_cast<uint8_t>(activeBlue * trailIntensity)),
        0);
  }

  const uint8_t headRed =
      brightnessScaledChannel(configuredActiveColor, pulsingBrightness, 16);
  const uint8_t headGreen =
      brightnessScaledChannel(configuredActiveColor, pulsingBrightness, 8);
  const uint8_t headBlue =
      brightnessScaledChannel(configuredActiveColor, pulsingBrightness, 0);
  const uint8_t headSize =
      constrain(static_cast<int>(secondDotSize) + 3, 4, 13);
  const lv_point_t headCenter = secondLinePoint(headSecond / 60.0f);
  lv_obj_set_size(secondCometHead, headSize, headSize);
  lv_obj_set_pos(secondCometHead, headCenter.x - headSize / 2,
                 headCenter.y - headSize / 2);
  lv_obj_set_style_bg_color(secondCometHead,
                            lv_color_make(headRed, headGreen, headBlue), 0);
}

void renderSecondLine(unsigned long now) {
  const bool visible = secondRingEnabled &&
                       secondEffect == CLOCK_SECOND_EFFECT_LINE;
  lv_obj_t *objects[] = {secondLineBackgroundArc, secondLineFadeArc,
                         secondLineActiveArc, secondLineActiveBridge};
  for (lv_obj_t *object : objects) {
    if (visible) {
      lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (!visible) return;

  const uint32_t configuredActiveColor =
      redNightVisualEnabled() ? 0xFF0000 : secondDotColor;
  const lv_color_t backgroundColor = brightnessScaledColor(
      secondRingBackgroundColor, secondRingBackgroundBrightness);
  const lv_color_t activeColor =
      brightnessScaledColor(configuredActiveColor, secondDotBrightness);
  configureSecondArc(secondLineBackgroundArc, secondRingBackgroundDotSize,
                     backgroundColor);
  configureSecondArc(secondLineActiveArc, secondDotSize, activeColor);
  lv_arc_set_bg_angles(secondLineBackgroundArc, 0, 360);
  lv_obj_set_style_line_color(secondLineActiveBridge, activeColor, 0);
  lv_obj_set_style_line_width(secondLineActiveBridge, secondDotSize, 0);

  const float elapsedWithinSecond = constrain(
      static_cast<float>(now - secondTickStartedAt) / 1000.0f, 0.0f, 1.0f);
  const float activeDegrees = constrain(
      (static_cast<float>(displayedSecond) + elapsedWithinSecond) * 6.0f,
      0.0f, 360.0f);
  const int activeWholeDegrees = static_cast<int>(std::floor(activeDegrees));
  if (activeWholeDegrees <= 0) {
    lv_obj_add_flag(secondLineActiveArc, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_arc_set_bg_angles(secondLineActiveArc, 0,
                         min(activeWholeDegrees, 360));
  }
  if (activeDegrees > activeWholeDegrees && activeDegrees < 360.0f) {
    secondLineActiveBridgePoints[0] =
        secondLinePoint(activeWholeDegrees / 360.0f);
    secondLineActiveBridgePoints[1] = secondLinePoint(activeDegrees / 360.0f);
    lv_line_set_points(secondLineActiveBridge, secondLineActiveBridgePoints, 2);
  } else {
    lv_obj_add_flag(secondLineActiveBridge, LV_OBJ_FLAG_HIDDEN);
  }

  if (secondFadeActive) {
    const float progress = constrain(
        static_cast<float>(now - secondFadeStartedAt) /
            SECOND_LINE_FADE_TOTAL_MS,
        0.0f, 1.0f);
    // Smoothstep má nulovou rychlost na začátku i na konci. Starý kruh tak
    // neproblikne ani náhle nezmizí, ale plynule se rozpustí do pozadí.
    const float intensity = 1.0f - progress * progress * (3.0f - 2.0f * progress);
    const uint8_t backgroundRed = brightnessScaledChannel(
        secondRingBackgroundColor, secondRingBackgroundBrightness, 16);
    const uint8_t backgroundGreen = brightnessScaledChannel(
        secondRingBackgroundColor, secondRingBackgroundBrightness, 8);
    const uint8_t backgroundBlue = brightnessScaledChannel(
        secondRingBackgroundColor, secondRingBackgroundBrightness, 0);
    const uint8_t activeRed = brightnessScaledChannel(
        configuredActiveColor, secondDotBrightness, 16);
    const uint8_t activeGreen = brightnessScaledChannel(
        configuredActiveColor, secondDotBrightness, 8);
    const uint8_t activeBlue = brightnessScaledChannel(
        configuredActiveColor, secondDotBrightness, 0);
    const lv_color_t fadingColor = lv_color_make(
        static_cast<uint8_t>(backgroundRed +
                             (activeRed - backgroundRed) * intensity),
        static_cast<uint8_t>(backgroundGreen +
                             (activeGreen - backgroundGreen) * intensity),
        static_cast<uint8_t>(backgroundBlue +
                             (activeBlue - backgroundBlue) * intensity));
    const uint8_t fadingWidth = constrain(
        static_cast<int>(std::round(
            secondRingBackgroundDotSize +
            (static_cast<int>(secondDotSize) - secondRingBackgroundDotSize) *
                intensity)),
        1, 10);
    configureSecondArc(secondLineFadeArc, fadingWidth, fadingColor);
    lv_arc_set_bg_angles(secondLineFadeArc, 0, 360);
  } else {
    lv_obj_add_flag(secondLineFadeArc, LV_OBJ_FLAG_HIDDEN);
  }
}

void renderSecondRing(unsigned long now) {
  if (analogLayoutEnabled()) {
    for (lv_obj_t *dot : secondDots) {
      if (dot != nullptr) lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_t *lineObjects[] = {secondLineBackgroundArc, secondLineFadeArc,
                               secondLineActiveArc, secondLineActiveBridge,
                               secondCometHead};
    for (lv_obj_t *object : lineObjects) {
      if (object != nullptr) lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
    secondFadeActive = false;
    return;
  }
  const unsigned long fadeDuration =
      secondEffect == CLOCK_SECOND_EFFECT_LINE ? SECOND_LINE_FADE_TOTAL_MS
                                               : SECOND_DOT_FADE_TOTAL_MS;
  if (secondFadeActive && now - secondFadeStartedAt >= fadeDuration) {
    secondFadeActive = false;
  }
  renderSecondDots(now);
  renderSecondLine(now);
  renderSecondComet(now);
}

void setTextColor(lv_obj_t *object, lv_color_t color) {
  if (object == nullptr) return;
  if (lv_obj_get_style_text_color(object, 0).full == color.full) return;
  lv_obj_set_style_text_color(object, color, 0);
}

void renderTimeColon(unsigned long now, bool force = false) {
  if (timeLabel == nullptr) return;
  if (timeColonEffect == CLOCK_TIME_COLON_STEADY) {
    if (force || lastRenderedTimeColonColor != UINT32_MAX) {
      lv_label_set_text(timeLabel, displayedTimeText);
      alignCenter(timeLabel, 0, -105);
      lastRenderedTimeColonColor = UINT32_MAX;
    }
    return;
  }

  const char *colon = strchr(displayedTimeText, ':');
  if (colon == nullptr) return;
  float intensity = (displayedSecond & 1U) == 0 ? 1.0f : 0.0f;
  if (timeColonEffect == CLOCK_TIME_COLON_FADE) {
    const float elapsedWithinSecond = constrain(
        static_cast<float>(now - secondTickStartedAt) / 1000.0f, 0.0f, 1.0f);
    const float phase = static_cast<float>(displayedSecond & 1U) +
                        elapsedWithinSecond;
    intensity = 0.5f - 0.5f * std::cos(phase * PI_VALUE);
  }
  // Inline barva dvojtečky musí na maximu přesně odpovídat barvě celého
  // časového labelu v červeném nočním režimu (COLOR_ERROR).
  const uint32_t baseColor = redNightVisualEnabled() ? 0xFF4848 : timeColor;
  const uint8_t red = static_cast<uint8_t>(
      std::round(((baseColor >> 16) & 0xFF) * intensity));
  const uint8_t green = static_cast<uint8_t>(
      std::round(((baseColor >> 8) & 0xFF) * intensity));
  const uint8_t blue = static_cast<uint8_t>(
      std::round((baseColor & 0xFF) * intensity));
  const uint32_t renderedColor =
      (static_cast<uint32_t>(red) << 16) |
      (static_cast<uint32_t>(green) << 8) | blue;
  if (!force && renderedColor == lastRenderedTimeColonColor) return;

  char renderedText[24];
  snprintf(renderedText, sizeof(renderedText), "%.*s#%02x%02x%02x :#%s",
           static_cast<int>(colon - displayedTimeText), displayedTimeText, red,
           green, blue, colon + 1);
  lv_label_set_text(timeLabel, renderedText);
  alignCenter(timeLabel, 0, -105);
  lastRenderedTimeColonColor = renderedColor;
}

void applyDashboardColors() {
  const bool animationIsMonochrome =
      strncmp(weatherAnimationKey, "monochrome-", 11) == 0;
  if (redNightVisualEnabled()) {
    lv_obj_t *coloredLabels[] = {
        timeLabel,          dateLabel,          namedayLabel,
        outsideTitleLabel,
        outsideIntegerLabel, outsideDecimalLabel, outsideUnitLabel,
        roomTitleLabel, roomIntegerLabel, roomDecimalLabel,
        roomUnitLabel,  outsideIconLabel,     roomIconLabel,
        co2TitleLabel,
        co2ValueLabel,     co2UnitLabel,        humidityTitleLabel,
        humidityValueLabel, humidityUnitLabel,
    };
    for (lv_obj_t *label : coloredLabels) setTextColor(label, COLOR_ERROR);
    setTextColor(radarClockLabel, COLOR_ERROR);
    setTextColor(radarTitleLabel, COLOR_ERROR);
    setTextColor(radarRangeLabel, COLOR_ERROR);
    setTextColor(radarStatusLabel, COLOR_ERROR);
    lv_obj_set_style_img_recolor(weatherImage, COLOR_ERROR, 0);
    lv_obj_set_style_img_recolor_opa(weatherImage, LV_OPA_COVER, 0);
    lv_obj_set_style_img_recolor(roomWeatherImage, COLOR_ERROR, 0);
    lv_obj_set_style_img_recolor_opa(roomWeatherImage, LV_OPA_COVER, 0);
    lv_obj_set_style_img_recolor(weatherAnimation, COLOR_ERROR, 0);
    lv_obj_set_style_img_recolor_opa(weatherAnimation, LV_OPA_COVER, 0);
    lv_obj_set_style_img_recolor(roomWeatherAnimation, COLOR_ERROR, 0);
    lv_obj_set_style_img_recolor_opa(roomWeatherAnimation, LV_OPA_COVER, 0);
  } else {
    const lv_color_t configuredOutsideColor =
        metricColorForValue(currentValues.leftTemperatureC,
                            leftValueColorScale);
    const lv_color_t configuredRoomColor =
        metricColorForValue(currentValues.rightTemperatureC,
                            rightValueColorScale);
    setTextColor(timeLabel, configuredColor(timeColor));
    setTextColor(radarClockLabel, COLOR_TEXT);
    setTextColor(radarTitleLabel, COLOR_OUTSIDE);
    setTextColor(radarRangeLabel, COLOR_OUTSIDE);
    setTextColor(radarStatusLabel, COLOR_OUTSIDE);
    setTextColor(dateLabel,
                 configuredColor(analogLayoutEnabled() ? analogDateColor
                                                       : dateColor));
    if (namedayLabel != nullptr)
      setTextColor(namedayLabel,
                   configuredColor(analogLayoutEnabled() ? analogDateColor
                                                         : dateColor));
    setTextColor(outsideTitleLabel, configuredOutsideColor);
    setTextColor(outsideIntegerLabel, configuredOutsideColor);
    setTextColor(outsideDecimalLabel, configuredOutsideColor);
    setTextColor(outsideUnitLabel, configuredOutsideColor);
    setTextColor(outsideIconLabel, configuredColor(leftWeatherIconColor));
    setTextColor(roomTitleLabel, configuredRoomColor);
    setTextColor(roomIntegerLabel, configuredRoomColor);
    setTextColor(roomDecimalLabel, configuredRoomColor);
    setTextColor(roomUnitLabel, configuredRoomColor);
    setTextColor(roomIconLabel, configuredColor(rightWeatherIconColor));
    const lv_color_t co2Color =
        metricColorForValue(currentValues.metricAValue, metricAColorScale);
    setTextColor(co2TitleLabel, co2Color);
    setTextColor(co2ValueLabel, co2Color);
    setTextColor(co2UnitLabel, co2Color);
    const lv_color_t configuredMetricBColor =
        metricColorForValue(currentValues.metricBValue, metricBColorScale);
    setTextColor(humidityTitleLabel, configuredMetricBColor);
    setTextColor(humidityValueLabel, configuredMetricBColor);
    setTextColor(humidityUnitLabel, configuredMetricBColor);
    const lv_color_t weatherColor =
        configuredColor(monochromeWeatherIconColor);
    lv_obj_set_style_img_recolor(weatherImage, weatherColor, 0);
    lv_obj_set_style_img_recolor_opa(weatherImage, LV_OPA_COVER, 0);
    lv_obj_set_style_img_recolor(roomWeatherImage, weatherColor, 0);
    lv_obj_set_style_img_recolor_opa(roomWeatherImage, LV_OPA_COVER, 0);
    lv_obj_set_style_img_recolor(weatherAnimation, weatherColor, 0);
    lv_obj_set_style_img_recolor_opa(
        weatherAnimation,
        animationIsMonochrome ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_img_recolor(roomWeatherAnimation, weatherColor, 0);
    lv_obj_set_style_img_recolor_opa(
        roomWeatherAnimation,
        animationIsMonochrome ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  }
  applyConnectionStatusColors();
  applyValuesPageColors();
  applyRssColors();
  applyAgendaColors();
  // Předpověď má barvu v každé hodnotě zvlášť, takže se přebarvuje tím, že se
  // řádky složí znovu.
  updateForecastPage();
  // Ukazatel obrazovek přebarvuje noční režim stejně jako všechno ostatní.
  updateScreenDots();
  renderSecondRing(millis());
  renderTimeColon(millis(), true);
  applyAnalogColors();
}

void updateBrightnessLabel(lv_obj_t *label, int brightness, int x, int y) {
  char text[8];
  snprintf(text, sizeof(text), "%d %%", brightness);
  lv_label_set_text(label, text);
  alignCenter(label, x, y);
}

void showSettings() {
  if (settingsVisible) return;
  if (settingsOpenCallback != nullptr) settingsOpenCallback();
  lv_slider_set_value(dayBrightnessSlider, savedDayBrightness, LV_ANIM_OFF);
  lv_slider_set_value(nightBrightnessSlider, savedNightBrightness, LV_ANIM_OFF);
  updateBrightnessLabel(dayBrightnessValueLabel, savedDayBrightness, 105, -72);
  updateBrightnessLabel(nightBrightnessValueLabel, savedNightBrightness, 105, 8);
  if (automaticDayNightEnabled) {
    lv_obj_add_state(automaticDayNightSwitch, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(automaticDayNightSwitch, LV_STATE_CHECKED);
  }
  lv_dropdown_set_selected(secondModeDropdown, selectedSecondMode());
  lv_dropdown_set_selected(weatherIconModeDropdown,
                           selectedWeatherIconMode());
  if (automaticFirmwareUpdateEnabled)
    lv_obj_add_state(automaticUpdateSwitch, LV_STATE_CHECKED);
  else
    lv_obj_clear_state(automaticUpdateSwitch, LV_STATE_CHECKED);
  lv_dropdown_set_selected(webModeDropdown, selectedWebMode);
  settingsSelectedClockStyle = activeClockStyle;
  updateClockStyleCardSelection();
  showSettingsSubpage(0);
  settingsVisible = true;
  lv_obj_clear_flag(settingsPage, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(settingsPage);
  updateScreenDots();
}

// Obrazovka zpráv: hlavička s názvem kanálu a pod ní seznam titulků, každý
// se svým časem vydání.
//
// Kruhový displej ubírá šířku u okrajů, takže se šířka řádku počítá z tětivy
// kružnice v jeho nejužším místě. Blok je svisle na střed; hlavička se skryje,
// jakmile by do ní seznam zasahoval.
//
// Čas se kreslí do stejného štítku jako titulek, jen obarvený recolor značkou.
// Pevný sloupec by ubíral svých 52 px i na krajních řádcích, které jsou u
// okraje kružnice nejužší, a druhý ani třetí řádek by ho stejně nevyužily.
constexpr int RSS_RADIUS = 240;
constexpr int RSS_INSET = 14;
constexpr int RSS_ROW_GAP = 14;
constexpr int RSS_BLOCK_CENTER_Y = 0;
// Posunuto o 6 px dolů, aby se hlavička nedotýkala podkladu pod ukazatelem
// obrazovek, který sahá k y=34.
constexpr int RSS_HEADER_Y = -190;
constexpr int RSS_MIN_ROW_WIDTH = 140;
// Mezera mezi časem a titulkem na prvním řádku.
constexpr char RSS_TIME_SEPARATOR[] = "  ";
// "#RRGGBB " před časem. Při přepnutí palety se přepisuje jen hex, proto se
// délka značky musí shodovat s tím, co skládá rssAppendTimeTag().
constexpr size_t RSS_COLOR_TAG_LENGTH = 8;

int rssLineHeight() {
  return lv_font_get_line_height(&clock_czech_16);
}

// Kolik řádků dostane titulek. Tři řádky se vejdou nejvýš k pěti zprávám; při
// šesti by krajní řádky spadly do úzkého konce kružnice a pojaly by méně textu
// než dnešní dva řádky.
int rssTitleLines(uint8_t count) { return count >= 6 ? 2 : 3; }

// Šířka použitelná v pásu mezi yTop a yBottom. Rozhoduje ten okraj, který je
// dál od středu, protože LVGL láme text na jednu pevnou šířku.
int rssRowWidth(int yTop, int yBottom) {
  const int top = yTop < 0 ? -yTop : yTop;
  const int bottom = yBottom < 0 ? -yBottom : yBottom;
  int extent = top > bottom ? top : bottom;
  if (extent >= RSS_RADIUS) extent = RSS_RADIUS - 1;
  const float half = sqrtf(static_cast<float>(RSS_RADIUS) * RSS_RADIUS -
                           static_cast<float>(extent) * extent);
  const int width = static_cast<int>(2.0f * half) - 2 * RSS_INSET;
  return width < RSS_MIN_ROW_WIDTH ? RSS_MIN_ROW_WIDTH : width;
}

lv_color_t rssTimeColor() {
  return redNightVisualEnabled() ? COLOR_ERROR : COLOR_OUTSIDE;
}

// Sestaví recolor značku "#RRGGBB ". LVGL si hex převede zpět do palety panelu,
// takže se barva shoduje s tím, co by nastavil setTextColor().
void rssBuildColorTag(char tag[RSS_COLOR_TAG_LENGTH + 1], lv_color_t color) {
  const uint32_t rgb = lv_color_to32(color);
  snprintf(tag, RSS_COLOR_TAG_LENGTH + 1, "#%02X%02X%02X ",
           static_cast<unsigned>((rgb >> 16) & 0xFF),
           static_cast<unsigned>((rgb >> 8) & 0xFF),
           static_cast<unsigned>(rgb & 0xFF));
}

// V recolor režimu je '#' řídicí znak; zdvojení je jeho doslovný zápis. Bez
// toho by mřížka v titulku spolkla kus věty.
void rssAppendEscaped(String &target, const char *text) {
  if (text == nullptr) return;
  for (const char *cursor = text; *cursor != '\0'; ++cursor) {
    target += *cursor;
    if (*cursor == '#') target += '#';
  }
}

void applyRssColors() {
  if (rssPage == nullptr) return;
  const bool redNight = redNightVisualEnabled();
  setTextColor(rssHeaderLabel, redNight ? COLOR_ERROR : COLOR_MUTED);
  setTextColor(rssStatusLabel, redNight ? COLOR_ERROR : COLOR_OUTSIDE);
  char tag[RSS_COLOR_TAG_LENGTH + 1];
  rssBuildColorTag(tag, rssTimeColor());
  for (size_t index = 0; index < CLOCK_RSS_MAX_ITEMS; ++index) {
    lv_obj_t *title = rssTitleLabels[index];
    if (title == nullptr) continue;
    setTextColor(title, redNight ? COLOR_ERROR : COLOR_TEXT);
    // Značka stojí na začátku a má pevnou délku, takže stačí přepsat hex.
    // Titulek bez času žádnou nemá a sahat se do něj nesmí.
    if (!rssItemHasTime[index]) continue;
    char *text = lv_label_get_text(title);
    if (text == nullptr || text[0] != '#') continue;
    memcpy(text + 1, tag + 1, RSS_COLOR_TAG_LENGTH - 2);
    lv_obj_invalidate(title);
  }
}

// Rozmístí řádky pro daný počet zpráv. Volá se jen při změně počtu, ne při
// každém obnovení kanálu.
void layoutRssItems(uint8_t count) {
  if (rssPage == nullptr) return;
  if (count > CLOCK_RSS_MAX_ITEMS) count = CLOCK_RSS_MAX_ITEMS;
  rssVisibleItemCount = count;
  const int lineHeight = rssLineHeight();
  const int titleLines = rssTitleLines(count);
  const int rowHeight = titleLines * lineHeight + RSS_ROW_GAP;
  const int total = rowHeight * count;
  const int top = RSS_BLOCK_CENTER_Y - total / 2;
  // Hlavička se vejde jen tehdy, když blok nezasahuje až k hornímu okraji.
  // Mez zůstává na -174 i po posunu hlavičky, aby se počet zpráv, při kterém
  // hlavička mizí, nezměnil.
  setObjectVisible(rssHeaderLabel, count > 0 && top > RSS_HEADER_Y + 16);

  for (size_t index = 0; index < CLOCK_RSS_MAX_ITEMS; ++index) {
    lv_obj_t *title = rssTitleLabels[index];
    if (title == nullptr) continue;
    if (index >= count) {
      setObjectVisible(title, false);
      continue;
    }
    const int rowTop = top + static_cast<int>(index) * rowHeight;
    const int width = rssRowWidth(rowTop, rowTop + titleLines * lineHeight);
    const int left = -width / 2;
    lv_obj_set_width(title, width);
    lv_obj_set_height(title, titleLines * lineHeight);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, RSS_RADIUS + left,
                 RSS_RADIUS + rowTop);
  }
}

void createRssPage(lv_obj_t *screen) {
  rssPage = lv_obj_create(screen);
  lv_obj_set_size(rssPage, 480, 480);
  lv_obj_center(rssPage);
  lv_obj_set_style_bg_color(rssPage, COLOR_BACKGROUND, 0);
  lv_obj_set_style_bg_opa(rssPage, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(rssPage, 0, 0);
  lv_obj_set_style_pad_all(rssPage, 0, 0);
  lv_obj_set_style_radius(rssPage, 0, 0);
  lv_obj_clear_flag(rssPage, LV_OBJ_FLAG_SCROLLABLE);

  rssHeaderLabel = makeLabel(rssPage, &clock_czech_16, COLOR_MUTED);
  lv_obj_set_width(rssHeaderLabel, 300);
  lv_label_set_long_mode(rssHeaderLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(rssHeaderLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(rssHeaderLabel, "");
  alignCenter(rssHeaderLabel, 0, RSS_HEADER_Y);

  rssStatusLabel = makeLabel(rssPage, &clock_czech_16, COLOR_OUTSIDE);
  lv_label_set_long_mode(rssStatusLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(rssStatusLabel, 340);
  lv_obj_set_style_text_align(rssStatusLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(rssStatusLabel, "");
  alignCenter(rssStatusLabel, 0, 0);
  lv_obj_add_flag(rssStatusLabel, LV_OBJ_FLAG_HIDDEN);

  for (size_t index = 0; index < CLOCK_RSS_MAX_ITEMS; ++index) {
    lv_obj_t *title = makeLabel(rssPage, &clock_czech_16, COLOR_TEXT);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_label_set_recolor(title, true);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(title, "");
    rssTitleLabels[index] = title;
    rssItemHasTime[index] = false;

    lv_obj_add_flag(title, LV_OBJ_FLAG_HIDDEN);
  }
  layoutRssItems(0);

  makeChildrenTapThrough(rssPage);
  lv_obj_add_flag(rssPage, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(rssPage, LV_OBJ_FLAG_HIDDEN);
}

// Obrazovka s agendou z kalendáře. Sloupce jako u předpovědi: čas vlevo pod
// sebou, název události v druhém sloupci. Zarovnávat každý řádek do kruhu zvlášť
// vypadalo jako schodiště - text má stát v mřížce, kruh jen omezuje, jak široká
// ta mřížka smí být.
//
// Nahoře stojí hodiny a venkovní teplota jako na předpovědi a radaru, dole
// legenda: jméno každého kalendáře ve své barvě. Barva času tak nepotřebuje
// vysvětlení jinde než na téže obrazovce.
constexpr int AGENDA_RADIUS = 240;
// Mřížka není na kruhu vystředěná, ale posunutá doprava: čas je krátký a levý
// sloupec by jinak nechával širokou mezeru, zatímco názvy vpravo naráží na
// okraj. Meze drží nejnižší a nejvyšší řádek - v y = ±166 má kruh poloviční
// tětivu 167 px, takže se −150 i 166 vejdou i do nejkrajnějšího řádku. Počítá
// se s tím, že úplně nahoře nemusí stát hlavička dne, ale rovnou celý název.
constexpr int AGENDA_LEFT = -150;
constexpr int AGENDA_RIGHT = 166;
constexpr int AGENDA_TIME_WIDTH = 66;
constexpr int AGENDA_COLUMN_GAP = 8;
// Mezery jsou úsporné schválně: každé ušetřené dva pixely na řádek jsou při
// deseti událostech dvacet pixelů, a to je celý další řádek.
constexpr int AGENDA_ROW_GAP = 2;
// Mezera nad hlavičkou dne. Odděluje dny výrazněji než mezera mezi řádky, aby
// se seznam četl po dnech, ne jako jeden sloupec.
constexpr int AGENDA_DAY_GAP = 7;
// Hlavička a legenda jsou u samého okraje: obě jsou krátké a vejdou se i tam,
// kde by se celý řádek agendy už nevešel. Uvolněné místo uprostřed je přesně
// to, co pojme další události.
constexpr int AGENDA_HEADER_Y = -196;
constexpr int AGENDA_LEGEND_Y = 186;
// Svislý pás mezi hlavičkou a legendou. Co se do něj nevejde, se neukáže -
// řádek přes legendu je horší než o událost méně. Při deseti událostech a
// čtyřech hlavičkách dnů zbývá ještě rezerva, takže se vejde i nejhorší případ.
constexpr int AGENDA_BLOCK_TOP = -172;
constexpr int AGENDA_BLOCK_HEIGHT = 345;
// Celodenní událost nemá čas; pomlčka drží sloupec, aby názvy začínaly pod
// sebou i pod ní.
constexpr char AGENDA_ALL_DAY_MARK[] = "-";
constexpr size_t AGENDA_COLOR_TAG_LENGTH = 8;

int agendaLineHeight() { return lv_font_get_line_height(&clock_czech_16); }
int agendaDayHeight() { return lv_font_get_line_height(&clock_czech_14); }

// Barva kalendáře. Index posílá server podle pořadí v AGENDA_CALENDARS, takže
// se jednotlivé kalendáře od sebe poznají bez popisku u každé události. V noční
// červené paletě se barvy slévají do jedné, protože jiná než červená by rozbila
// noční vidění - legenda tam proto nese jen jména.
lv_color_t agendaCalendarColor(uint8_t calendar) {
  if (redNightVisualEnabled()) return COLOR_ERROR;
  switch (calendar) {
    case 0: return COLOR_OUTSIDE;
    case 1: return COLOR_ROOM;
    case 2: return COLOR_AIR;
    default: return COLOR_HUMIDITY;
  }
}

void agendaBuildColorTag(char tag[AGENDA_COLOR_TAG_LENGTH + 1],
                         lv_color_t color) {
  const uint32_t rgb = lv_color_to32(color);
  snprintf(tag, AGENDA_COLOR_TAG_LENGTH + 1, "#%02X%02X%02X ",
           static_cast<unsigned>((rgb >> 16) & 0xFF),
           static_cast<unsigned>((rgb >> 8) & 0xFF),
           static_cast<unsigned>(rgb & 0xFF));
}

void agendaAppendEscaped(String &target, const char *text) {
  if (text == nullptr) return;
  for (const char *cursor = text; *cursor != '\0'; ++cursor) {
    target += *cursor;
    if (*cursor == '#') target += '#';
  }
}

// Kolik řádků se do pásu vejde i s hlavičkami dnů. Vrací počet událostí, ne
// výšku: rozvržení podle něj rovnou ví, kde přestat.
uint8_t agendaFittingItemCount(uint8_t count) {
  const int lineHeight = agendaLineHeight();
  const int dayHeight = agendaDayHeight();
  int total = 0;
  for (uint8_t index = 0; index < count; ++index) {
    int next = total + lineHeight + AGENDA_ROW_GAP;
    if (agendaRowStartsDay[index]) next += dayHeight + AGENDA_DAY_GAP;
    if (next > AGENDA_BLOCK_HEIGHT) return index;
    total = next;
  }
  return count;
}

// Rozmístí řádky do mřížky. Volá se až po naplnění všech řádků, protože dřív
// není známo, kde jsou hranice dnů, a řádek s hlavičkou je o její výšku vyšší.
void layoutAgendaItems() {
  if (agendaPage == nullptr) return;
  const int lineHeight = agendaLineHeight();
  const int dayHeight = agendaDayHeight();
  const uint8_t visible = agendaFittingItemCount(agendaVisibleItemCount);
  // Seznam začíná pod hlavičkou, ne uprostřed kruhu. Vystředěný blok nechával
  // nahoře i dole mezeru a při krátké agendě vypadal, že se na obrazovku víc
  // nevejde - přitom místo bylo. Takhle roste dolů do prázdna, jako hodiny na
  // předpovědi.
  int cursorY = AGENDA_BLOCK_TOP;
  const int titleLeft = AGENDA_LEFT + AGENDA_TIME_WIDTH + AGENDA_COLUMN_GAP;

  for (size_t index = 0; index < CLOCK_AGENDA_MAX_ITEMS; ++index) {
    lv_obj_t *day = agendaDayLabels[index];
    lv_obj_t *time = agendaTimeLabels[index];
    lv_obj_t *title = agendaTitleLabels[index];
    if (day == nullptr || time == nullptr || title == nullptr) continue;
    if (index >= visible) {
      setObjectVisible(day, false);
      setObjectVisible(time, false);
      setObjectVisible(title, false);
      continue;
    }
    if (agendaRowStartsDay[index]) {
      cursorY += AGENDA_DAY_GAP;
      lv_obj_align(day, LV_ALIGN_TOP_LEFT, AGENDA_RADIUS + AGENDA_LEFT,
                   AGENDA_RADIUS + cursorY);
      setObjectVisible(day, true);
      cursorY += dayHeight;
    } else {
      setObjectVisible(day, false);
    }
    lv_obj_align(time, LV_ALIGN_TOP_LEFT, AGENDA_RADIUS + AGENDA_LEFT,
                 AGENDA_RADIUS + cursorY);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, AGENDA_RADIUS + titleLeft,
                 AGENDA_RADIUS + cursorY);
    setObjectVisible(time, true);
    setObjectVisible(title, true);
    cursorY += lineHeight + AGENDA_ROW_GAP;
  }
}

// Legenda: jméno každého kalendáře ve své barvě, na jednom řádku. Bez ní barva
// času nic neříká - a odznak s tečkou nejde, písmo žádnou nemá.
void updateAgendaLegendLabel() {
  if (agendaLegendLabel == nullptr) return;
  if (agendaCalendarCount == 0) {
    setObjectVisible(agendaLegendLabel, false);
    return;
  }
  String text;
  text.reserve(AGENDA_MAX_CALENDARS *
               (AGENDA_CALENDAR_NAME_LENGTH + AGENDA_COLOR_TAG_LENGTH + 4));
  for (size_t index = 0; index < agendaCalendarCount; ++index) {
    if (index > 0) text += "   ";
    char tag[AGENDA_COLOR_TAG_LENGTH + 1];
    agendaBuildColorTag(tag, agendaCalendarColor(static_cast<uint8_t>(index)));
    text += tag;
    agendaAppendEscaped(text, agendaCalendarNames[index]);
    text += '#';
  }
  lv_label_set_text(agendaLegendLabel, text.c_str());
  setObjectVisible(agendaLegendLabel, true);
}

// Hodiny a venkovní teplota, stejná řádka jako na předpovědi a radaru. Skládá
// se tady, aby ji přepnutí jazyka i noční palety chytly spolu se zbytkem
// obrazovky.
void updateAgendaHeaderLabel() {
  if (agendaHeaderLabel == nullptr) return;
  const bool haveTime = displayedTimeText[0] != '\0' &&
                        strcmp(displayedTimeText, "--:--") != 0;
  char temperature[12] = "";
  const float degrees = currentValues.outsideTemperatureC;
  if (!std::isnan(degrees) && degrees > -60.0f && degrees < 60.0f) {
    snprintf(temperature, sizeof(temperature), "%d°C",
             static_cast<int>(std::lround(degrees)));
  }
  char tag[10];
  forecastAppendColorTag(tag, sizeof(tag), forecastTemperatureColor(degrees));
  char text[48];
  if (haveTime && temperature[0] != '\0') {
    snprintf(text, sizeof(text), "%s%s%s%s#", displayedTimeText,
             STATUS_LINE_GAP, tag, temperature);
  } else if (haveTime) {
    snprintf(text, sizeof(text), "%s", displayedTimeText);
  } else if (temperature[0] != '\0') {
    snprintf(text, sizeof(text), "%s%s#", tag, temperature);
  } else {
    text[0] = '\0';
  }
  setObjectVisible(agendaHeaderLabel, text[0] != '\0');
  if (text[0] == '\0') return;
  setTextColor(agendaHeaderLabel,
               redNightVisualEnabled() ? COLOR_ERROR : COLOR_TEXT);
  lv_label_set_text(agendaHeaderLabel, text);
}

// Stránka agendy leží v PSRAM. Bez toho by třicet štítků ukouslo přes deset
// kilobajtů interní RAM, kterou potřebuje TLS - a rozbilo by to i kanál se
// zprávami, který o agendě nic neví.
class AgendaPsramAllocations {
 public:
  AgendaPsramAllocations() { clockLvglPreferPsram(true); }
  ~AgendaPsramAllocations() { clockLvglPreferPsram(false); }

  AgendaPsramAllocations(const AgendaPsramAllocations &) = delete;
  AgendaPsramAllocations &operator=(const AgendaPsramAllocations &) = delete;
};

void createAgendaPage(lv_obj_t *screen) {
  AgendaPsramAllocations psram;
  agendaPage = lv_obj_create(screen);
  lv_obj_set_size(agendaPage, 480, 480);
  lv_obj_center(agendaPage);
  lv_obj_set_style_bg_color(agendaPage, COLOR_BACKGROUND, 0);
  lv_obj_set_style_bg_opa(agendaPage, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(agendaPage, 0, 0);
  lv_obj_set_style_pad_all(agendaPage, 0, 0);
  lv_obj_set_style_radius(agendaPage, 0, 0);
  lv_obj_clear_flag(agendaPage, LV_OBJ_FLAG_SCROLLABLE);

  agendaHeaderLabel = makeLabel(agendaPage, &clock_czech_20, COLOR_TEXT);
  lv_label_set_recolor(agendaHeaderLabel, true);
  lv_label_set_text(agendaHeaderLabel, "");
  alignCenter(agendaHeaderLabel, 0, AGENDA_HEADER_Y);

  agendaStatusLabel = makeLabel(agendaPage, &clock_czech_16, COLOR_MUTED);
  lv_label_set_long_mode(agendaStatusLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(agendaStatusLabel, 340);
  lv_obj_set_style_text_align(agendaStatusLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(agendaStatusLabel, "");
  alignCenter(agendaStatusLabel, 0, 0);
  lv_obj_add_flag(agendaStatusLabel, LV_OBJ_FLAG_HIDDEN);

  const int titleWidth = AGENDA_RIGHT - AGENDA_LEFT - AGENDA_TIME_WIDTH -
                         AGENDA_COLUMN_GAP;
  for (size_t index = 0; index < CLOCK_AGENDA_MAX_ITEMS; ++index) {
    lv_obj_t *day = makeLabel(agendaPage, &clock_czech_14, COLOR_MUTED);
    lv_label_set_long_mode(day, LV_LABEL_LONG_DOT);
    lv_obj_set_width(day, AGENDA_RIGHT - AGENDA_LEFT);
    lv_obj_set_style_text_align(day, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(day, "");
    lv_obj_add_flag(day, LV_OBJ_FLAG_HIDDEN);
    agendaDayLabels[index] = day;

    lv_obj_t *time = makeLabel(agendaPage, &clock_czech_16, COLOR_OUTSIDE);
    lv_obj_set_width(time, AGENDA_TIME_WIDTH);
    lv_obj_set_style_text_align(time, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(time, "");
    lv_obj_add_flag(time, LV_OBJ_FLAG_HIDDEN);
    agendaTimeLabels[index] = time;

    lv_obj_t *title = makeLabel(agendaPage, &clock_czech_16, COLOR_TEXT);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, titleWidth);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(title, "");
    lv_obj_add_flag(title, LV_OBJ_FLAG_HIDDEN);
    agendaTitleLabels[index] = title;

    agendaRowStartsDay[index] = false;
  }
  agendaVisibleItemCount = 0;

  agendaLegendLabel = makeLabel(agendaPage, &clock_czech_14, COLOR_MUTED);
  lv_label_set_recolor(agendaLegendLabel, true);
  lv_label_set_long_mode(agendaLegendLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_width(agendaLegendLabel, 300);
  lv_obj_set_style_text_align(agendaLegendLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(agendaLegendLabel, "");
  alignCenter(agendaLegendLabel, 0, AGENDA_LEGEND_Y);
  lv_obj_add_flag(agendaLegendLabel, LV_OBJ_FLAG_HIDDEN);

  makeChildrenTapThrough(agendaPage);
  lv_obj_add_flag(agendaPage, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(agendaPage, LV_OBJ_FLAG_HIDDEN);
}

void applyAgendaColors() {
  if (agendaPage == nullptr) return;
  const bool redNight = redNightVisualEnabled();
  setTextColor(agendaStatusLabel, redNight ? COLOR_ERROR : COLOR_MUTED);
  for (size_t index = 0; index < CLOCK_AGENDA_MAX_ITEMS; ++index) {
    if (agendaDayLabels[index] != nullptr) {
      setTextColor(agendaDayLabels[index], redNight ? COLOR_ERROR : COLOR_MUTED);
    }
    if (agendaTitleLabels[index] != nullptr) {
      setTextColor(agendaTitleLabels[index],
                   redNight ? COLOR_ERROR : COLOR_TEXT);
    }
    // Čas nese barvu kalendáře, takže se přebarvuje podle uloženého indexu, ne
    // paušálně - jinak by se po návratu z noci všechny slily do jedné.
    if (agendaTimeLabels[index] != nullptr) {
      setTextColor(agendaTimeLabels[index],
                   agendaCalendarColor(agendaRowCalendar[index]));
    }
  }
  updateAgendaHeaderLabel();
  updateAgendaLegendLabel();
}

// Obrazovka předpovědi. Svislý rozpočet - kolik hodin po denní části a
// kvalitě ovzduší zbude - je ve WeatherForecastLayout.h, aby ho šlo testovat
// bez LVGL; tady zůstávají jen vodorovné sloupce a kreslení.
constexpr int FORECAST_ROW_HEIGHT = WEATHER_FORECAST_ROW_HEIGHT;
constexpr int FORECAST_HEADER_Y = -190;
constexpr int FORECAST_AIR_LINE_COUNT = WEATHER_FORECAST_AIR_LINE_COUNT;
constexpr int FORECAST_AIR_LINE_HEIGHT = WEATHER_FORECAST_AIR_LINE_HEIGHT;
constexpr int FORECAST_AIR_TOP_Y = WEATHER_FORECAST_AIR_TOP_Y;
constexpr int FORECAST_RADIUS = 240;
constexpr int FORECAST_DIVIDER_INSET = 24;
// Ikony jsou stejné meteocons jako na ciferníku, jen zmenšené z 84 na 24 px.
constexpr int FORECAST_ICON_SIZE = 24;
constexpr uint16_t FORECAST_ICON_ZOOM =
    (256 * FORECAST_ICON_SIZE + 42) / 84;
constexpr int FORECAST_LABEL_X = -134;
constexpr int FORECAST_LABEL_WIDTH = 32;
constexpr int FORECAST_ICON_X = -96;
constexpr int FORECAST_TEMPERATURE_X = -49;
constexpr int FORECAST_TEMPERATURE_WIDTH = 70;
constexpr int FORECAST_PRECIPITATION_X = 28;
constexpr int FORECAST_PRECIPITATION_WIDTH = 76;
constexpr int FORECAST_WIND_X = 110;
constexpr int FORECAST_WIND_WIDTH = 84;
// Nejvíc řádků, které si obrazovka může vyžádat naráz. Objekty se zakládají
// jen pro ty, které aktuální nastavení opravdu kreslí.
constexpr size_t FORECAST_MAX_ROWS =
    WEATHER_FORECAST_MAX_HOURS + WEATHER_FORECAST_MAX_DAYS;

// Objekty obrazovky předpovědi se zakládají do PSRAM. Je jich přes padesát a
// v interní RAM by ubraly kolem dvanácti kilobajtů, o které pak přijde TLS:
// handshake si bere dva šestnáctikilobajtové buffery a bez souvislého místa
// selže s MBEDTLS_ERR_SSL_ALLOC_FAILED - a to i kanálu zpráv, který o
// předpovědi nic neví. Obrazovka se staví jednou a překresluje jen při změně
// dat, takže pomalejší přístup do PSRAM na ní není poznat.
class ForecastPsramAllocations {
 public:
  ForecastPsramAllocations() { clockLvglPreferPsram(true); }
  ~ForecastPsramAllocations() { clockLvglPreferPsram(false); }

  ForecastPsramAllocations(const ForecastPsramAllocations &) = delete;
  ForecastPsramAllocations &operator=(const ForecastPsramAllocations &) =
      delete;
};

// Jeden řádek předpovědi. Hodnota a jednotka sdílejí jeden popisek s recolor
// značkou: dva objekty na sloupec by na šestnácti řádcích znamenaly desítky
// kilobajtů paměti navíc.
struct ForecastRow {
  lv_obj_t *label = nullptr;
  lv_obj_t *icon = nullptr;
  lv_obj_t *temperature = nullptr;
  lv_obj_t *precipitation = nullptr;
  lv_obj_t *wind = nullptr;
};

lv_obj_t *forecastHeaderLabel = nullptr;
lv_obj_t *forecastMessageLabel = nullptr;
lv_obj_t *forecastDivider = nullptr;
lv_obj_t *forecastAirLabels[FORECAST_AIR_LINE_COUNT] = {};
ForecastRow forecastRows[FORECAST_MAX_ROWS];
uint8_t forecastCreatedRowCount = 0;
uint8_t forecastHourRowCount = 0;
uint8_t forecastDayRowCount = 0;
bool forecastAirQualityEnabled = true;
WeatherForecastData forecastDisplayed;
bool forecastDisplayedAvailable = false;
bool forecastFetchFailed = false;

// Šířka tětivy kruhu v dané výšce, zmenšená o okraj. Používá to jen dělicí
// čára, která má být tak dlouhá, jak jí displej dovolí.
int forecastChordWidth(int y, int inset) {
  const int extent = y < 0 ? -y : y;
  if (extent >= FORECAST_RADIUS) return 0;
  const float half =
      sqrtf(static_cast<float>(FORECAST_RADIUS) * FORECAST_RADIUS -
            static_cast<float>(extent) * extent);
  const int width = static_cast<int>(2.0f * half) - 2 * inset;
  return width < 0 ? 0 : width;
}

lv_color_t forecastMutedColor() {
  return redNightVisualEnabled() ? lv_color_make(140, 0, 0) : COLOR_MUTED;
}

// Teplotní škála pro hodinové i denní řádky. Denní řádek se barví podle
// maxima, protože to je z dvojice ta hodnota, kvůli které se člověk obléká.
lv_color_t forecastTemperatureColor(float degrees) {
  if (redNightVisualEnabled()) return COLOR_ERROR;
  if (std::isnan(degrees)) return COLOR_MUTED;
  if (degrees < 0.0f) return lv_color_make(143, 211, 255);
  if (degrees < 20.0f) return COLOR_OUTSIDE;
  if (degrees < 27.0f) return COLOR_TEXT;
  if (degrees < 32.0f) return COLOR_ROOM;
  return COLOR_ERROR;
}

lv_color_t forecastWindColor(float kmh) {
  if (redNightVisualEnabled()) return COLOR_ERROR;
  if (std::isnan(kmh)) return COLOR_MUTED;
  // Hranice odpovídají Beaufortově stupnici: od 6 stupně se hůř chodí, od 8
  // se lámou větve.
  if (kmh < 39.0f) return COLOR_MUTED;
  if (kmh < 62.0f) return COLOR_ROOM;
  return COLOR_ERROR;
}

// Barva ikony podle podmínek. Statické meteocons jsou bílé masky s alfou,
// takže barvu určuje až img_recolor - stejně jako u ikony na ciferníku.
lv_color_t forecastIconColor(int weatherCode, bool isDay) {
  if (redNightVisualEnabled()) return COLOR_ERROR;
  switch (weatherIconConditionForCode(weatherCode, isDay)) {
    case WeatherIconCondition::ClearDay:
    case WeatherIconCondition::MostlyClearDay:
    case WeatherIconCondition::PartlyCloudyDay:
      return lv_color_make(255, 209, 102);
    case WeatherIconCondition::ClearNight:
    case WeatherIconCondition::MostlyClearNight:
    case WeatherIconCondition::PartlyCloudyNight:
    case WeatherIconCondition::OvercastNight:
      return lv_color_make(181, 199, 232);
    case WeatherIconCondition::Drizzle:
    case WeatherIconCondition::Rain:
      return COLOR_OUTSIDE;
    case WeatherIconCondition::Sleet:
      return lv_color_make(143, 211, 255);
    case WeatherIconCondition::Snow:
      return lv_color_make(232, 244, 255);
    case WeatherIconCondition::Thunderstorms:
      return COLOR_ROOM;
    default:
      return COLOR_MUTED;
  }
}

// Pásma evropského indexu kvality ovzduší (0-20 dobrý až nad 100 mimořádně
// špatný), jak je definuje Evropská agentura pro životní prostředí.
lv_color_t forecastAqiColor(float aqi) {
  if (redNightVisualEnabled()) return COLOR_ERROR;
  if (std::isnan(aqi)) return COLOR_MUTED;
  if (aqi <= 20.0f) return COLOR_AIR;
  if (aqi <= 40.0f) return lv_color_make(168, 216, 84);
  if (aqi <= 60.0f) return lv_color_make(255, 213, 79);
  if (aqi <= 80.0f) return COLOR_ROOM;
  return COLOR_ERROR;
}

// Denní pásma PM2.5 podle stejné stupnice; pod 10 µg/m3 je vzduch dobrý.
lv_color_t forecastPm25Color(float value) {
  if (redNightVisualEnabled()) return COLOR_ERROR;
  if (std::isnan(value)) return COLOR_MUTED;
  if (value <= 10.0f) return COLOR_AIR;
  if (value <= 20.0f) return lv_color_make(168, 216, 84);
  if (value <= 25.0f) return lv_color_make(255, 213, 79);
  if (value <= 50.0f) return COLOR_ROOM;
  return COLOR_ERROR;
}

// Pyl trav v zrnech na krychlový metr. Alergikům začíná být nepříjemně kolem
// dvaceti zrn, nad padesáti je sezóna v plném proudu.
lv_color_t forecastPollenColor(float value) {
  if (redNightVisualEnabled()) return COLOR_ERROR;
  if (std::isnan(value)) return COLOR_MUTED;
  if (value < 1.0f) return COLOR_MUTED;
  if (value < 20.0f) return COLOR_AIR;
  if (value < 50.0f) return lv_color_make(255, 213, 79);
  if (value < 200.0f) return COLOR_ROOM;
  return COLOR_ERROR;
}

// Recolor značka LVGL: "#RRGGBB text#". Skládá se ručně, protože barvy jsou
// lv_color_t a v 16bitovém režimu se z nich musí složky vytáhnout zpátky.
void forecastAppendColorTag(char *destination, size_t capacity,
                            lv_color_t color) {
  const uint32_t rgb = lv_color_to32(color);
  snprintf(destination, capacity, "#%02X%02X%02X ",
           static_cast<unsigned>((rgb >> 16) & 0xFF),
           static_cast<unsigned>((rgb >> 8) & 0xFF),
           static_cast<unsigned>(rgb & 0xFF));
}

// Hodnota s jednotkou v jednom popisku. Jednotka je vždycky tlumená, aby
// z řádku vystoupilo číslo; prázdná hodnota se nekreslí vůbec.
void forecastSetValue(lv_obj_t *label, const char *value, const char *unit,
                      lv_color_t color) {
  if (label == nullptr) return;
  if (value == nullptr || value[0] == '\0') {
    lv_label_set_text(label, "");
    return;
  }
  setTextColor(label, color);
  if (unit == nullptr || unit[0] == '\0') {
    lv_label_set_text(label, value);
    return;
  }
  char tag[10];
  forecastAppendColorTag(tag, sizeof(tag), forecastMutedColor());
  char text[48];
  snprintf(text, sizeof(text), "%s %s%s#", value, tag, unit);
  lv_label_set_text(label, text);
}

// Zaokrouhlená hodnota, nebo pomlčky, když ji odpověď nenesla.
void forecastRoundedValue(char *destination, size_t capacity, float value) {
  if (std::isnan(value)) {
    snprintf(destination, capacity, "--");
    return;
  }
  snprintf(destination, capacity, "%d", static_cast<int>(std::lround(value)));
}

// Zkratka dne v týdnu. Dvě písmena drží sloupec úzký a v obou jazycích se
// v nich den pozná. Česky velkými písmeny, protože z háčků a čárek mají
// dashboardové fonty jen ty nad velkými - "Pá" by skončilo čtverečkem.
const char *forecastWeekdayName(int weekday) {
  static const char *const CZECH[] = {"NE", "PO", "ÚT", "ST",
                                      "ČT", "PÁ", "SO"};
  static const char *const ENGLISH[] = {"Su", "Mo", "Tu", "We",
                                        "Th", "Fr", "Sa"};
  if (weekday < 0 || weekday > 6) return "";
  return englishLanguage() ? ENGLISH[weekday] : CZECH[weekday];
}

// Řádky kreslí clock_czech_16, ne clock_czech_18: osmnáctka nemá stupeň ani
// mikro, takže by z "°C" i z "µg/m³" byly prázdné čtverečky.
lv_obj_t *createForecastValueLabel(int width) {
  lv_obj_t *label = makeLabel(forecastPage, &clock_czech_16, COLOR_TEXT);
  lv_label_set_recolor(label, true);
  lv_obj_set_width(label, width);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_text(label, "");
  return label;
}

void createForecastRow(ForecastRow &row) {
  row.label = makeLabel(forecastPage, &clock_czech_16, COLOR_MUTED);
  lv_obj_set_width(row.label, FORECAST_LABEL_WIDTH);
  lv_obj_set_style_text_align(row.label, LV_TEXT_ALIGN_LEFT, 0);
  lv_label_set_text(row.label, "");

  // Ikona si drží svých 84 x 84 px a kreslí se zmenšená kolem středu, takže
  // se zarovnává středem. Menší objekt by zmenšenou kresbu odřízl.
  row.icon = lv_img_create(forecastPage);
  lv_img_set_zoom(row.icon, FORECAST_ICON_ZOOM);
  lv_img_set_antialias(row.icon, true);
  lv_obj_set_style_img_recolor_opa(row.icon, LV_OPA_COVER, 0);
  lv_obj_add_flag(row.icon, LV_OBJ_FLAG_HIDDEN);

  row.temperature = createForecastValueLabel(FORECAST_TEMPERATURE_WIDTH);
  row.precipitation = createForecastValueLabel(FORECAST_PRECIPITATION_WIDTH);
  row.wind = createForecastValueLabel(FORECAST_WIND_WIDTH);
}

void layoutForecastRow(const ForecastRow &row, int y) {
  alignCenter(row.label, FORECAST_LABEL_X, y);
  alignCenter(row.icon, FORECAST_ICON_X, y);
  alignCenter(row.temperature, FORECAST_TEMPERATURE_X, y);
  alignCenter(row.precipitation, FORECAST_PRECIPITATION_X, y);
  alignCenter(row.wind, FORECAST_WIND_X, y);
}

void setForecastRowVisible(const ForecastRow &row, bool visible) {
  setObjectVisible(row.label, visible);
  setObjectVisible(row.temperature, visible);
  setObjectVisible(row.precipitation, visible);
  setObjectVisible(row.wind, visible);
  if (!visible) lv_obj_add_flag(row.icon, LV_OBJ_FLAG_HIDDEN);
}

int forecastRowY(size_t index) {
  return weatherForecastRowY(static_cast<uint8_t>(index),
                             forecastHourRowCount);
}

int forecastDividerY() {
  if (forecastHourRowCount == 0 || forecastDayRowCount == 0) return 0;
  const int lastHour = forecastRowY(forecastHourRowCount - 1);
  const int firstDay = forecastRowY(forecastHourRowCount);
  return (lastHour + firstDay) / 2;
}

void layoutForecastPage() {
  if (forecastPage == nullptr) return;
  const size_t needed = forecastHourRowCount + forecastDayRowCount;
  for (size_t index = 0; index < forecastCreatedRowCount; ++index) {
    if (index < needed) {
      layoutForecastRow(forecastRows[index], forecastRowY(index));
    } else {
      setForecastRowVisible(forecastRows[index], false);
    }
  }
  const bool showDivider = forecastHourRowCount > 0 && forecastDayRowCount > 0;
  setObjectVisible(forecastDivider, showDivider);
  if (showDivider) {
    const int y = forecastDividerY();
    lv_obj_set_width(forecastDivider,
                     forecastChordWidth(y, FORECAST_DIVIDER_INSET));
    alignCenter(forecastDivider, 0, y);
  }
  for (int line = 0; line < FORECAST_AIR_LINE_COUNT; ++line) {
    alignCenter(forecastAirLabels[line], 0,
                FORECAST_AIR_TOP_Y + line * FORECAST_AIR_LINE_HEIGHT);
  }
}

// Založí tolik řádků, kolik jich současné nastavení kreslí. Volá se při
// každé změně nastavení; ubrané řádky se ruší, aby nedržely paměť pro
// obrazovku, na které už nejsou.
void rebuildForecastRows(uint8_t hourCount, uint8_t dayCount) {
  if (forecastPage == nullptr) return;
  const ForecastPsramAllocations psramAllocations;
  forecastHourRowCount = hourCount;
  forecastDayRowCount = dayCount;
  const size_t needed = hourCount + dayCount;
  while (forecastCreatedRowCount < needed) {
    createForecastRow(forecastRows[forecastCreatedRowCount]);
    ++forecastCreatedRowCount;
  }
  while (forecastCreatedRowCount > needed) {
    --forecastCreatedRowCount;
    ForecastRow &row = forecastRows[forecastCreatedRowCount];
    lv_obj_del(row.label);
    lv_obj_del(row.icon);
    lv_obj_del(row.temperature);
    lv_obj_del(row.precipitation);
    lv_obj_del(row.wind);
    row = ForecastRow{};
  }
  makeChildrenTapThrough(forecastPage);
  layoutForecastPage();
  updateForecastPage();
}

void createForecastPage(lv_obj_t *screen) {
  const ForecastPsramAllocations psramAllocations;
  forecastPage = lv_obj_create(screen);
  lv_obj_set_size(forecastPage, 480, 480);
  lv_obj_center(forecastPage);
  lv_obj_set_style_bg_color(forecastPage, COLOR_BACKGROUND, 0);
  lv_obj_set_style_bg_opa(forecastPage, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(forecastPage, 0, 0);
  lv_obj_set_style_pad_all(forecastPage, 0, 0);
  lv_obj_set_style_radius(forecastPage, 0, 0);
  lv_obj_clear_flag(forecastPage, LV_OBJ_FLAG_SCROLLABLE);

  forecastHeaderLabel = makeLabel(forecastPage, &clock_czech_20, COLOR_TEXT);
  lv_label_set_recolor(forecastHeaderLabel, true);
  lv_label_set_text(forecastHeaderLabel, "");
  alignCenter(forecastHeaderLabel, 0, FORECAST_HEADER_Y);

  forecastMessageLabel =
      makeLabel(forecastPage, &clock_czech_20, COLOR_OUTSIDE);
  lv_label_set_long_mode(forecastMessageLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(forecastMessageLabel, 340);
  lv_obj_set_style_text_align(forecastMessageLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(forecastMessageLabel, "");
  alignCenter(forecastMessageLabel, 0, 0);
  lv_obj_add_flag(forecastMessageLabel, LV_OBJ_FLAG_HIDDEN);

  forecastDivider = makeDivider(forecastPage, 300, 1, 0, 0);
  lv_obj_add_flag(forecastDivider, LV_OBJ_FLAG_HIDDEN);

  for (int line = 0; line < FORECAST_AIR_LINE_COUNT; ++line) {
    lv_obj_t *label = makeLabel(forecastPage, &clock_czech_20, COLOR_TEXT);
    lv_label_set_recolor(label, true);
    lv_label_set_text(label, "");
    forecastAirLabels[line] = label;
  }

  makeChildrenTapThrough(forecastPage);
  lv_obj_add_flag(forecastPage, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(forecastPage, LV_OBJ_FLAG_HIDDEN);
}

// --- Radar letadel ----------------------------------------------------------
// Mapa i letadla přijdou hotové z PlaneRadarService jako buffer RGB565; tady
// se jen podloží pod canvas. Text kolem je LVGL, protože drobné mapové písmo
// neumí malá písmena ani diakritiku, a jména měst z trasy je potřebují.
constexpr int PLANES_CLOCK_OFFSET_Y = -186;
// Řádek s počtem letadel sedí těsně pod hodinami: podklad hodin sahá k y=68,
// řádek při písmu 14 měří 22 px, takže -158 nechá mezi oběma proužky 2 px.
// Výš už to nejde, aniž by se podklady slily do jednoho bloku.
constexpr int PLANES_STATUS_OFFSET_Y = -158;
constexpr int PLANES_RANGE_OFFSET_Y = 164;
constexpr int PLANES_DETAIL_WIDTH = 330;
// Šest řádků, název typu slovy, dvě řádky trasy a poznámka o ztraceném
// signálu. Rohy panelu musí zůstat uvnitř kruhu displeje: 330x310 dává
// polovinu úhlopříčky 227 px proti poloměru 240 px.
constexpr int PLANES_DETAIL_HEIGHT = 310;
// První řádek a rozestup mezi řádky. Název typu je menším písmem, takže si
// bere méně místa než celý řádek.
constexpr int PLANES_DETAIL_ROWS_TOP = 48;
constexpr int PLANES_DETAIL_ROW_STEP = 24;
constexpr int PLANES_DETAIL_TYPE_NAME_HEIGHT = 20;

// Kam padne který řádek. Řádky pod typem draku uhýbají názvu typu.
int planesDetailRowY(uint8_t row) {
  const int shift =
      row > PLANES_DETAIL_TYPE_ROW ? PLANES_DETAIL_TYPE_NAME_HEIGHT : 0;
  return PLANES_DETAIL_ROWS_TOP + row * PLANES_DETAIL_ROW_STEP + shift;
}

// Objekty obrazovky se zakládají do PSRAM, ze stejného důvodu jako u
// předpovědi: v interní RAM by ubraly kilobajty, o které pak přijde TLS
// handshake - a to i službám, které o radaru letadel nic nevědí.
class PlanesPsramAllocations {
 public:
  PlanesPsramAllocations() { clockLvglPreferPsram(true); }
  ~PlanesPsramAllocations() { clockLvglPreferPsram(false); }

  PlanesPsramAllocations(const PlanesPsramAllocations &) = delete;
  PlanesPsramAllocations &operator=(const PlanesPsramAllocations &) = delete;
};

lv_obj_t *makePlanesOverlayLabel(lv_obj_t *parent, const lv_font_t *font,
                                 lv_color_t color, int offsetY) {
  // Tmavý podklad tu není ozdoba: řádek leží přímo na mapě a nad světlou
  // ikonou letadla by text zmizel.
  lv_obj_t *label = makeLabel(parent, font, color);
  lv_label_set_text(label, "");
  lv_obj_set_style_bg_color(label, COLOR_BACKGROUND, 0);
  lv_obj_set_style_bg_opa(label, LV_OPA_80, 0);
  lv_obj_set_style_pad_hor(label, 8, 0);
  lv_obj_set_style_pad_ver(label, 2, 0);
  alignCenter(label, 0, offsetY);
  return label;
}

void createPlanesPage(lv_obj_t *screen) {
  PlanesPsramAllocations psramAllocations;

  planesPage = lv_obj_create(screen);
  lv_obj_set_size(planesPage, 480, 480);
  lv_obj_center(planesPage);
  lv_obj_set_style_bg_color(planesPage, COLOR_BACKGROUND, 0);
  lv_obj_set_style_bg_opa(planesPage, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(planesPage, 0, 0);
  lv_obj_set_style_pad_all(planesPage, 0, 0);
  lv_obj_set_style_radius(planesPage, 0, 0);
  lv_obj_clear_flag(planesPage, LV_OBJ_FLAG_SCROLLABLE);

  planesCanvas = lv_canvas_create(planesPage);
  lv_obj_center(planesCanvas);
  lv_obj_add_flag(planesCanvas, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(planesCanvas, LV_OBJ_FLAG_CLICKABLE);

  planesClockLabel = makePlanesOverlayLabel(planesPage, &clock_czech_20,
                                            COLOR_TEXT, PLANES_CLOCK_OFFSET_Y);
  lv_obj_add_flag(planesClockLabel, LV_OBJ_FLAG_HIDDEN);
  planesStatusLabel = makePlanesOverlayLabel(
      planesPage, &clock_czech_14, COLOR_OUTSIDE, PLANES_STATUS_OFFSET_Y);
  lv_label_set_recolor(planesStatusLabel, true);
  planesRangeLabel = makePlanesOverlayLabel(
      planesPage, &clock_czech_20, COLOR_OUTSIDE, PLANES_RANGE_OFFSET_Y);

  // Detail letadla. Leží nad mapou a odchází se z něj klepnutím kamkoli, takže
  // sám žádné dotyky nechytá.
  planesDetailPanel = lv_obj_create(planesPage);
  lv_obj_set_size(planesDetailPanel, PLANES_DETAIL_WIDTH, PLANES_DETAIL_HEIGHT);
  lv_obj_center(planesDetailPanel);
  lv_obj_set_style_bg_color(planesDetailPanel, COLOR_BACKGROUND, 0);
  lv_obj_set_style_bg_opa(planesDetailPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(planesDetailPanel, COLOR_OUTSIDE, 0);
  lv_obj_set_style_border_width(planesDetailPanel, 2, 0);
  lv_obj_set_style_radius(planesDetailPanel, 14, 0);
  lv_obj_set_style_pad_all(planesDetailPanel, 0, 0);
  lv_obj_clear_flag(planesDetailPanel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(planesDetailPanel, LV_OBJ_FLAG_HIDDEN);

  planesDetailTitle = makeLabel(planesDetailPanel, &clock_czech_20, COLOR_ROOM);
  lv_label_set_text(planesDetailTitle, "");
  lv_obj_align(planesDetailTitle, LV_ALIGN_TOP_LEFT, 18, 14);

  // Křížek v rohu. Je to značka, ne tlačítko - zavírá kterékoli klepnutí.
  planesDetailClose = makeLabel(planesDetailPanel, &clock_czech_18, COLOR_ERROR);
  lv_label_set_text(planesDetailClose, "X");
  lv_obj_align(planesDetailClose, LV_ALIGN_TOP_RIGHT, -18, 14);

  for (uint8_t row = 0; row < PLANES_DETAIL_ROW_COUNT; ++row) {
    lv_obj_t *label = makeLabel(planesDetailPanel, &clock_czech_16, COLOR_TEXT);
    lv_label_set_recolor(label, true);
    lv_label_set_text(label, "");
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 18, planesDetailRowY(row));
    planesDetailRows[row] = label;
  }

  // Název typu slovy pod zkratkou draku. Menším písmem a potichu, protože je
  // to vysvětlivka k řádku nad ním, ne další údaj. Nejdelší jména z databáze
  // letadel jsou přes čtyřicet znaků, takže má pevnou šířku a přeteklý zbytek
  // uzavřou tři tečky - jinak by text vylezl z panelu ven.
  planesDetailTypeName =
      makeLabel(planesDetailPanel, &clock_czech_14, COLOR_MUTED);
  lv_label_set_text(planesDetailTypeName, "");
  lv_obj_set_width(planesDetailTypeName, PLANES_DETAIL_WIDTH - 36);
  lv_label_set_long_mode(planesDetailTypeName, LV_LABEL_LONG_DOT);
  lv_obj_align(planesDetailTypeName, LV_ALIGN_TOP_LEFT, 18,
               planesDetailRowY(PLANES_DETAIL_TYPE_ROW) +
                   PLANES_DETAIL_ROW_STEP);

  // Trasa na DVĚ řádky. Jedna řádka se šipkou mezi městy se musela zmenšit,
  // aby se vedle sebe vešla dvě jména, a byla pak nečitelná; na půl má každá
  // celou šířku panelu.
  planesDetailRouteFrom =
      makeLabel(planesDetailPanel, &clock_czech_16, COLOR_MUTED);
  lv_label_set_text(planesDetailRouteFrom, "");
  lv_obj_align(planesDetailRouteFrom, LV_ALIGN_TOP_LEFT, 18,
               planesDetailRowY(PLANES_DETAIL_ROW_COUNT) + 6);
  planesDetailRouteTo =
      makeLabel(planesDetailPanel, &clock_czech_16, COLOR_MUTED);
  lv_label_set_text(planesDetailRouteTo, "");
  lv_obj_align(planesDetailRouteTo, LV_ALIGN_TOP_LEFT, 18,
               planesDetailRowY(PLANES_DETAIL_ROW_COUNT) + 28);

  planesDetailSignalLost =
      makeLabel(planesDetailPanel, &clock_czech_16, COLOR_ROOM);
  lv_label_set_text(planesDetailSignalLost, "");
  lv_obj_align(planesDetailSignalLost, LV_ALIGN_BOTTOM_RIGHT, -14, -8);
  lv_obj_add_flag(planesDetailSignalLost, LV_OBJ_FLAG_HIDDEN);

  makeChildrenTapThrough(planesPage);
  lv_obj_add_flag(planesPage, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(planesPage, LV_OBJ_FLAG_HIDDEN);
}

// Hlavička obrazovky: čas a venkovní teplota, stejně jako ve stavovém řádku
// radaru. Předpověď totiž zabírá celý displej a ciferník pod ní není vidět.
void updateForecastHeaderLabel() {
  if (forecastHeaderLabel == nullptr) return;
  const bool haveTime = displayedTimeText[0] != '\0' &&
                        strcmp(displayedTimeText, "--:--") != 0;
  char temperature[12] = "";
  const float degrees = currentValues.outsideTemperatureC;
  if (!std::isnan(degrees) && degrees > -60.0f && degrees < 60.0f) {
    snprintf(temperature, sizeof(temperature), "%d°C",
             static_cast<int>(std::lround(degrees)));
  }
  char tag[10];
  forecastAppendColorTag(tag, sizeof(tag),
                         forecastTemperatureColor(degrees));
  char text[48];
  if (haveTime && temperature[0] != '\0') {
    snprintf(text, sizeof(text), "%s%s%s%s#", displayedTimeText,
             STATUS_LINE_GAP, tag, temperature);
  } else if (haveTime) {
    snprintf(text, sizeof(text), "%s", displayedTimeText);
  } else if (temperature[0] != '\0') {
    snprintf(text, sizeof(text), "%s%s#", tag, temperature);
  } else {
    text[0] = '\0';
  }
  setObjectVisible(forecastHeaderLabel, text[0] != '\0');
  if (text[0] == '\0') return;
  setTextColor(forecastHeaderLabel,
               redNightVisualEnabled() ? COLOR_ERROR : COLOR_TEXT);
  lv_label_set_text(forecastHeaderLabel, text);
  alignCenter(forecastHeaderLabel, 0, FORECAST_HEADER_Y);
}

// Srážky pod desetinu milimetru nejsou déšť, jen vlhko ve vzduchu. Prázdný
// sloupec je čitelnější než les nul.
void forecastSetPrecipitation(lv_obj_t *label, float millimeters) {
  if (std::isnan(millimeters) || millimeters < 0.05f) {
    lv_label_set_text(label, "");
    return;
  }
  char value[12];
  snprintf(value, sizeof(value), "%.1f", millimeters);
  forecastSetValue(label, value, "mm",
                   redNightVisualEnabled() ? COLOR_ERROR : COLOR_OUTSIDE);
}

void forecastSetWind(lv_obj_t *label, float kmh) {
  if (std::isnan(kmh)) {
    lv_label_set_text(label, "");
    return;
  }
  char value[12];
  snprintf(value, sizeof(value), "%d", static_cast<int>(std::lround(kmh)));
  forecastSetValue(label, value, "km/h", forecastWindColor(kmh));
}

void forecastSetIcon(lv_obj_t *icon, int wmoCode, bool isDay) {
  const int code = weatherCodeFromWmo(wmoCode);
  const lv_img_dsc_t *source = openWeatherIconForCode(code, isDay);
  if (source == nullptr) {
    lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_img_set_src(icon, source);
  // lv_img_set_src si objekt roztáhne na velikost zdroje, takže zarovnání
  // musí přijít až po něm.
  lv_obj_set_style_img_recolor(icon, forecastIconColor(code, isDay), 0);
  lv_obj_clear_flag(icon, LV_OBJ_FLAG_HIDDEN);
}

void updateForecastAirQuality() {
  const bool show = forecastAirQualityEnabled && forecastDisplayedAvailable &&
                    forecastDisplayed.airAvailable;
  for (int line = 0; line < FORECAST_AIR_LINE_COUNT; ++line)
    setObjectVisible(forecastAirLabels[line], show);
  if (!show) return;
  const WeatherForecastAirQuality &air = forecastDisplayed.air;
  const bool english = englishLanguage();
  char muted[10];
  forecastAppendColorTag(muted, sizeof(muted), forecastMutedColor());
  char text[64];
  char number[12];

  forecastRoundedValue(number, sizeof(number), air.europeanAqi);
  snprintf(text, sizeof(text), "%sAQI# %s", muted, number);
  setTextColor(forecastAirLabels[0], forecastAqiColor(air.europeanAqi));
  lv_label_set_text(forecastAirLabels[0], text);

  forecastRoundedValue(number, sizeof(number), air.pm25);
  if (std::isnan(air.pm25)) {
    snprintf(text, sizeof(text), "%sPM2.5# %s", muted, number);
  } else {
    snprintf(text, sizeof(text), "%sPM2.5# %s %sµg/m³#", muted, number, muted);
  }
  setTextColor(forecastAirLabels[1], forecastPm25Color(air.pm25));
  lv_label_set_text(forecastAirLabels[1], text);

  // Mimo evropskou doménu modelu CAMS pyl nikdo nepočítá; řádek pak jen
  // přizná, že hodnotu nemá.
  forecastRoundedValue(number, sizeof(number), air.grassPollen);
  snprintf(text, sizeof(text), "%s%s# %s", muted,
           english ? "Grass pollen" : "Pyl trav", number);
  setTextColor(forecastAirLabels[2], forecastPollenColor(air.grassPollen));
  lv_label_set_text(forecastAirLabels[2], text);
  alignCenter(forecastAirLabels[0], 0, FORECAST_AIR_TOP_Y);
  alignCenter(forecastAirLabels[1], 0,
              FORECAST_AIR_TOP_Y + FORECAST_AIR_LINE_HEIGHT);
  alignCenter(forecastAirLabels[2], 0,
              FORECAST_AIR_TOP_Y + 2 * FORECAST_AIR_LINE_HEIGHT);
}

void updateForecastPage() {
  if (forecastPage == nullptr) return;
  updateForecastHeaderLabel();
  updateAgendaHeaderLabel();

  // Dokud předpověď nedorazila, drží obrazovku jediná hláška - poloprázdné
  // řádky by tvrdily, že data jsou a jen chybí čísla.
  const bool showRows = forecastDisplayedAvailable &&
                        forecastDisplayed.hourCount > 0;
  setObjectVisible(forecastMessageLabel, !showRows);
  if (!showRows) {
    for (size_t index = 0; index < forecastCreatedRowCount; ++index)
      setForecastRowVisible(forecastRows[index], false);
    setObjectVisible(forecastDivider, false);
    for (int line = 0; line < FORECAST_AIR_LINE_COUNT; ++line)
      setObjectVisible(forecastAirLabels[line], false);
    setTextColor(forecastMessageLabel,
                 redNightVisualEnabled() ? COLOR_ERROR : COLOR_OUTSIDE);
    // Verzálkami: z háčků a čárek mají dashboardové fonty jen ty nad velkými
    // písmeny, takže by z malého "ř" nebo "ě" byl prázdný čtvereček.
    const bool english = englishLanguage();
    lv_label_set_text(
        forecastMessageLabel,
        forecastFetchFailed
            ? (english ? "FORECAST IS NOT AVAILABLE"
                       : "PŘEDPOVĚĎ NENÍ DOSTUPNÁ")
            : (english ? "LOADING FORECAST..." : "NAČÍTÁM PŘEDPOVĚĎ..."));
    alignCenter(forecastMessageLabel, 0, 0);
    return;
  }

  char value[16];
  for (size_t index = 0; index < forecastHourRowCount; ++index) {
    const ForecastRow &row = forecastRows[index];
    const bool present = index < forecastDisplayed.hourCount;
    setForecastRowVisible(row, present);
    if (!present) continue;
    const WeatherForecastHour &hour = forecastDisplayed.hours[index];
    const time_t stamp = static_cast<time_t>(hour.time);
    struct tm local;
    if (localtime_r(&stamp, &local) != nullptr) {
      snprintf(value, sizeof(value), "%02d", local.tm_hour);
    } else {
      snprintf(value, sizeof(value), "--");
    }
    setTextColor(row.label, forecastMutedColor());
    lv_label_set_text(row.label, value);
    forecastSetIcon(row.icon, hour.weatherCode, hour.isDay);
    if (std::isnan(hour.temperatureC)) {
      lv_label_set_text(row.temperature, "");
    } else {
      snprintf(value, sizeof(value), "%d",
               static_cast<int>(std::lround(hour.temperatureC)));
      forecastSetValue(row.temperature, value, "°C",
                       forecastTemperatureColor(hour.temperatureC));
    }
    forecastSetPrecipitation(row.precipitation, hour.precipitationMm);
    forecastSetWind(row.wind, hour.windKmh);
  }

  for (size_t offset = 0; offset < forecastDayRowCount; ++offset) {
    const size_t index = forecastHourRowCount + offset;
    if (index >= forecastCreatedRowCount) break;
    const ForecastRow &row = forecastRows[index];
    const bool present = offset < forecastDisplayed.dayCount;
    setForecastRowVisible(row, present);
    if (!present) continue;
    const WeatherForecastDay &day = forecastDisplayed.days[offset];
    const time_t stamp = static_cast<time_t>(day.time);
    struct tm local;
    setTextColor(row.label, forecastMutedColor());
    lv_label_set_text(row.label, localtime_r(&stamp, &local) != nullptr
                                     ? forecastWeekdayName(local.tm_wday)
                                     : "");
    forecastSetIcon(row.icon, day.weatherCode, true);
    if (std::isnan(day.maximumC) && std::isnan(day.minimumC)) {
      lv_label_set_text(row.temperature, "");
    } else if (std::isnan(day.minimumC)) {
      snprintf(value, sizeof(value), "%d",
               static_cast<int>(std::lround(day.maximumC)));
      forecastSetValue(row.temperature, value, "",
                       forecastTemperatureColor(day.maximumC));
    } else {
      // Maximum a minimum v jednom sloupci: "27/17" se vejde tam, kde by dvě
      // čísla s jednotkami řádek přetáhla přes okraj kruhu.
      snprintf(value, sizeof(value), "%d/%d",
               static_cast<int>(std::lround(day.maximumC)),
               static_cast<int>(std::lround(day.minimumC)));
      forecastSetValue(row.temperature, value, "",
                       forecastTemperatureColor(day.maximumC));
    }
    forecastSetPrecipitation(row.precipitation, day.precipitationMm);
    forecastSetWind(row.wind, day.windKmh);
  }

  setObjectVisible(forecastDivider,
                   forecastHourRowCount > 0 && forecastDayRowCount > 0 &&
                       forecastDisplayed.dayCount > 0);
  updateForecastAirQuality();
}

// Jedna tečka ukazatele. lv_obj_create přináší vlastní rámeček i odsazení,
// obojí je tady na obtíž, proto se ruší.
lv_obj_t *createRadarDot(lv_obj_t *parent, int diameter) {
  lv_obj_t *dot = lv_obj_create(parent);
  lv_obj_set_size(dot, diameter, diameter);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(dot, 0, 0);
  lv_obj_set_style_pad_all(dot, 0, 0);
  lv_obj_set_style_bg_color(dot, COLOR_DIVIDER, 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
  return dot;
}

// Podklad jedné řady teček. Kreslí se jako první, takže zůstane pod nimi.
lv_obj_t *createRadarDotBacking(lv_obj_t *parent) {
  lv_obj_t *backing = lv_obj_create(parent);
  lv_obj_set_style_radius(backing, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(backing, 0, 0);
  lv_obj_set_style_pad_all(backing, 0, 0);
  lv_obj_set_style_bg_color(backing, COLOR_BACKGROUND, 0);
  lv_obj_set_style_bg_opa(backing, LV_OPA_80, 0);
  lv_obj_clear_flag(backing, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(backing, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(backing, LV_OBJ_FLAG_HIDDEN);
  return backing;
}

// Podklad podle počtu teček v řadě; skryje se s prázdnou řadou.
void layoutRadarDotBacking(lv_obj_t *backing, uint8_t count, int gap,
                           int diameter, int offsetY, int padding = 8) {
  if (backing == nullptr) return;
  if (count == 0) {
    lv_obj_add_flag(backing, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  const int width = (count - 1) * gap + diameter + 12;
  lv_obj_set_size(backing, width, diameter + padding);
  lv_obj_align(backing, LV_ALIGN_CENTER, 0, offsetY);
  lv_obj_clear_flag(backing, LV_OBJ_FLAG_HIDDEN);
}

// Barva zvýrazněné tečky. Červená v nočním režimu a také tehdy, když se na
// pozadí teprve chystá kompletní animace - tuhle informaci dřív nesl pruh.
lv_color_t radarDotAccentColor() {
  if (redNightVisualEnabled()) return COLOR_ERROR;
  return radarFullPreparationInProgress ? COLOR_ERROR : COLOR_OUTSIDE;
}

void updateRadarFrameDots(uint8_t frameCount, uint8_t currentFrameNumber) {
  // Jediný snímek není animace, řada teček by o něm nic neřekla.
  const bool visibleRow = frameCount > 1 && currentFrameNumber > 0;
  const uint8_t shown =
      visibleRow ? min<uint8_t>(frameCount, RADAR_FRAME_DOT_CAPACITY) : 0;
  // Při patnácti snímcích by se řada s roztečí 16 px rozlezla přes celý kruh.
  const int gap = shown > 8 ? 12 : 16;
  const int firstX = -((shown - 1) * gap) / 2;
  const lv_color_t accent = radarDotAccentColor();
  layoutRadarDotBacking(radarFrameDotsBacking, shown, gap, 8,
                        RADAR_FRAME_DOTS_OFFSET_Y);
  for (uint8_t index = 0; index < RADAR_FRAME_DOT_CAPACITY; ++index) {
    lv_obj_t *dot = radarFrameDots[index];
    if (dot == nullptr) continue;
    if (index >= shown) {
      lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_set_style_bg_color(
        dot, index + 1 == currentFrameNumber ? accent : COLOR_DIVIDER, 0);
    lv_obj_align(dot, LV_ALIGN_CENTER, firstX + index * gap,
                 RADAR_FRAME_DOTS_OFFSET_Y);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
  }
}

void updateRadarRangeDots(uint16_t radiusKm) {
  const int gap = 16;
  const int firstX = -((RADAR_RANGE_DOT_COUNT - 1) * gap) / 2;
  const lv_color_t accent =
      redNightVisualEnabled() ? COLOR_ERROR : COLOR_OUTSIDE;
  layoutRadarDotBacking(radarRangeDotsBacking, RADAR_RANGE_DOT_COUNT, gap, 6,
                        RADAR_RANGE_DOTS_OFFSET_Y);
  for (uint8_t index = 0; index < RADAR_RANGE_DOT_COUNT; ++index) {
    lv_obj_t *dot = radarRangeDots[index];
    if (dot == nullptr) continue;
    lv_obj_set_style_bg_color(
        dot, RADAR_RANGE_DOT_RADII[index] == radiusKm ? accent : COLOR_DIVIDER,
        0);
    lv_obj_align(dot, LV_ALIGN_CENTER, firstX + index * gap,
                 RADAR_RANGE_DOTS_OFFSET_Y);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
  }
}

// Text prázdné obrazovky. Hlášky ze služby jsou diagnostické a jen česky
// ("Snimek CHMU se nepodarilo pripravit"), takže patří na stránku diagnostiky;
// na displeji stačí přeložený stav.
const char *radarEmptyStateText(bool busy) {
  if (busy) return englishLanguage() ? "Loading radar..." : "Načítám radar...";
  return englishLanguage() ? "No radar frame yet" : "Radar zatím nemá snímek";
}

// Čas a venkovní teplota do jednoho řádku. Ukáže jen tu polovinu, kterou už
// zařízení zná - půl řádku je pořád lepší než prázdné místo.
// Čas a venkovní teplota do jednoho řádku. Používá ho stavový řádek
// meteoradaru i radaru letadel, aby obě mapy ukazovaly totéž stejně.
bool composeStatusLineText(char *text, size_t capacity) {
  // "--:--" drží místo, dokud se čas nesynchronizuje; jako údaj nemá cenu.
  const bool haveTime = displayedTimeText[0] != '\0' &&
                        strcmp(displayedTimeText, "--:--") != 0;
  char temperature[12] = "";
  const float degrees = currentValues.outsideTemperatureC;
  // Zaokrouhluje se na celé stupně: desetina je u venkovní teploty šum a dva
  // znaky navíc rozhodují o tom, jestli se řádek do kruhu vejde.
  if (!std::isnan(degrees) && degrees > -60.0f && degrees < 60.0f) {
    snprintf(temperature, sizeof(temperature), "%d°C",
             static_cast<int>(std::lround(degrees)));
  }
  if (haveTime && temperature[0] != '\0') {
    snprintf(text, capacity, "%s%s%s", displayedTimeText, STATUS_LINE_GAP,
             temperature);
  } else if (haveTime) {
    snprintf(text, capacity, "%s", displayedTimeText);
  } else {
    snprintf(text, capacity, "%s", temperature);
  }
  return text[0] != '\0';
}

void updateRadarClockLabel() {
  if (radarClockLabel == nullptr) return;
  char text[32];
  if (!radarStatusLineEnabled || !composeStatusLineText(text, sizeof(text))) {
    lv_obj_add_flag(radarClockLabel, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_label_set_text(radarClockLabel, text);
  alignCenter(radarClockLabel, 0, RADAR_CLOCK_OFFSET_Y);
  lv_obj_clear_flag(radarClockLabel, LV_OBJ_FLAG_HIDDEN);
}

void updatePlanesClockLabel() {
  if (planesClockLabel == nullptr) return;
  char text[32];
  // Stejný přepínač jako u meteoradaru: je to tentýž řádek na téže mapě.
  if (!radarStatusLineEnabled || !composeStatusLineText(text, sizeof(text))) {
    lv_obj_add_flag(planesClockLabel, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_label_set_text(planesClockLabel, text);
  alignCenter(planesClockLabel, 0, PLANES_CLOCK_OFFSET_Y);
  lv_obj_clear_flag(planesClockLabel, LV_OBJ_FLAG_HIDDEN);
}

// Čas a venkovní teplota se ukazují ve stavovém řádku radaru i v hlavičce
// předpovědi, takže je obě obnovuje jedno volání.
void updateOverlayStatusLabels() {
  updateRadarClockLabel();
  updatePlanesClockLabel();
  updateForecastHeaderLabel();
  updateAgendaHeaderLabel();
}

// Stáří snímku v minutách podle jeho času "HH:MM" v místním čase. Vrací -1,
// když čas ještě není synchronizovaný nebo je popisek nečitelný; půlnoc se
// řeší tím, že se záporný rozdíl posune o celý den.
int radarFrameMinutesAgo(const char *frameTime) {
  if (frameTime == nullptr || frameTime[0] == '\0') return -1;
  int frameHour = 0;
  int frameMinute = 0;
  if (sscanf(frameTime, "%d:%d", &frameHour, &frameMinute) != 2) return -1;
  const time_t now = time(nullptr);
  if (now < 1600000000) return -1;
  struct tm localNow;
  if (localtime_r(&now, &localNow) == nullptr) return -1;
  int minutes = (localNow.tm_hour * 60 + localNow.tm_min) -
                (frameHour * 60 + frameMinute);
  if (minutes < 0) minutes += 24 * 60;
  // Radar drží nanejvýš pár hodin dozadu; cokoliv víc znamená, že čas snímku
  // a hodiny nesedí, a tvrdit "-1400 min" by bylo horší než mlčet.
  if (minutes > 6 * 60) return -1;
  return minutes;
}

// Ukazatel obrazovek se zakládá až po všech stránkách, aby ležel nad nimi.
// Stránky se při přepnutí vytahují dopředu, takže si ho updateScreenDots()
// vytáhne zpátky nad ně.
void createScreenDots(lv_obj_t *screen) {
  screenDotsBacking = createRadarDotBacking(screen);
  for (uint8_t index = 0; index < SCREEN_DOT_COUNT; ++index)
    screenDots[index] = createRadarDot(screen, 8);
  updateScreenDots();
}

void updateScreenDots() {
  if (screenDotsBacking == nullptr) return;
  uint8_t activeIndex = 0;
  uint8_t count = 0;
  // Tečky se řadí podle pořadí z konfigurace, ne podle čísel obrazovek - jinak
  // by ukazatel sliboval jiný sled, než jakým se obrazovky střídají.
  for (uint8_t position = 0; position < SCREEN_DOT_COUNT; ++position) {
    const uint8_t screen =
        clockConfigScreenAt(dashboardRuntimeConfig, position);
    if (!screenAvailable(screen)) continue;
    if (screen == activeScreen) activeIndex = count;
    ++count;
  }
  // Jediná obrazovka nemá mezi čím přepínat a jedna tečka by o ničem
  // nevypovídala. Nastavení ani aktualizace firmwaru do rotace nepatří.
  const bool visible = count > 1 && !settingsVisible && !firmwareUpdateActive;
  const int gap = 20;
  const int firstX = -((count - 1) * gap) / 2;
  const lv_color_t accent =
      redNightVisualEnabled() ? COLOR_ERROR : COLOR_TEXT;
  layoutRadarDotBacking(screenDotsBacking, visible ? count : 0, gap, 8,
                        SCREEN_DOT_OFFSET_Y, SCREEN_DOT_BACKING_PADDING);
  lv_obj_move_foreground(screenDotsBacking);
  for (uint8_t index = 0; index < SCREEN_DOT_COUNT; ++index) {
    lv_obj_t *dot = screenDots[index];
    if (dot == nullptr) continue;
    if (!visible || index >= count) {
      lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_set_style_bg_color(dot, index == activeIndex ? accent
                                                        : COLOR_DIVIDER,
                              0);
    lv_obj_align(dot, LV_ALIGN_CENTER, firstX + index * gap,
                 SCREEN_DOT_OFFSET_Y);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(dot);
  }
}

void createRadarPage(lv_obj_t *screen) {
  radarPage = lv_obj_create(screen);
  lv_obj_set_size(radarPage, 480, 480);
  lv_obj_center(radarPage);
  lv_obj_set_style_bg_color(radarPage, COLOR_BACKGROUND, 0);
  lv_obj_set_style_bg_opa(radarPage, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(radarPage, 0, 0);
  lv_obj_set_style_pad_all(radarPage, 0, 0);
  lv_obj_set_style_radius(radarPage, 0, 0);
  lv_obj_clear_flag(radarPage, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(radarPage, LV_OBJ_FLAG_CLICKABLE);

  radarCanvas = lv_canvas_create(radarPage);
  lv_obj_center(radarCanvas);
  lv_obj_add_flag(radarCanvas, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(radarCanvas, LV_OBJ_FLAG_CLICKABLE);

  // Čas a venkovní teplota. Tmavý podklad tu není ozdoba: řádek leží přímo na
  // mapě a nad žlutými srážkami by bílý text zmizel.
  radarClockLabel = makeLabel(radarPage, &clock_czech_20, COLOR_TEXT);
  lv_label_set_text(radarClockLabel, "");
  lv_obj_set_style_bg_color(radarClockLabel, COLOR_BACKGROUND, 0);
  lv_obj_set_style_bg_opa(radarClockLabel, LV_OPA_80, 0);
  lv_obj_set_style_pad_hor(radarClockLabel, 8, 0);
  lv_obj_set_style_pad_ver(radarClockLabel, 2, 0);
  alignCenter(radarClockLabel, 0, RADAR_CLOCK_OFFSET_Y);
  lv_obj_add_flag(radarClockLabel, LV_OBJ_FLAG_HIDDEN);

  radarTitleLabel = makeLabel(radarPage, &clock_czech_16, COLOR_OUTSIDE);
  lv_label_set_recolor(radarTitleLabel, true);
  lv_label_set_text(radarTitleLabel, "");
  lv_obj_set_style_bg_color(radarTitleLabel, COLOR_BACKGROUND, 0);
  lv_obj_set_style_bg_opa(radarTitleLabel, LV_OPA_80, 0);
  lv_obj_set_style_pad_hor(radarTitleLabel, 8, 0);
  lv_obj_set_style_pad_ver(radarTitleLabel, 2, 0);
  alignCenter(radarTitleLabel, 0, RADAR_FRAME_LABEL_OFFSET_Y);

  radarRangeLabel = makeLabel(radarPage, &clock_czech_20, COLOR_OUTSIDE);
  lv_label_set_text(radarRangeLabel, "");
  lv_obj_set_style_bg_color(radarRangeLabel, COLOR_BACKGROUND, 0);
  lv_obj_set_style_bg_opa(radarRangeLabel, LV_OPA_80, 0);
  lv_obj_set_style_pad_hor(radarRangeLabel, 8, 0);
  lv_obj_set_style_pad_ver(radarRangeLabel, 2, 0);
  alignCenter(radarRangeLabel, 0, RADAR_RANGE_LABEL_OFFSET_Y);

  radarFrameDotsBacking = createRadarDotBacking(radarPage);
  for (uint8_t index = 0; index < RADAR_FRAME_DOT_CAPACITY; ++index)
    radarFrameDots[index] = createRadarDot(radarPage, 8);
  radarRangeDotsBacking = createRadarDotBacking(radarPage);
  for (uint8_t index = 0; index < RADAR_RANGE_DOT_COUNT; ++index)
    radarRangeDots[index] = createRadarDot(radarPage, 6);

  // Dokud není ani jeden snímek, je obrazovka prázdná a hláška patří doprostřed
  // - u spodního okraje kruhu by se dlouhý text neměl kam zalomit.
  radarStatusLabel = makeLabel(radarPage, &clock_czech_20, COLOR_OUTSIDE);
  lv_label_set_long_mode(radarStatusLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(radarStatusLabel, 340);
  lv_obj_set_style_text_align(radarStatusLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(radarStatusLabel, radarEmptyStateText(false));
  lv_obj_set_style_bg_color(radarStatusLabel, COLOR_BACKGROUND, 0);
  lv_obj_set_style_bg_opa(radarStatusLabel, LV_OPA_80, 0);
  lv_obj_set_style_pad_all(radarStatusLabel, 6, 0);
  alignCenter(radarStatusLabel, 0, 0);

  makeChildrenTapThrough(radarPage);
  lv_obj_add_flag(radarPage, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(radarPage, LV_OBJ_FLAG_HIDDEN);
}

void closeSettings(bool saveChanges) {
  if (!settingsVisible) return;
  if (saveChanges) {
    savedDayBrightness =
        static_cast<uint8_t>(lv_slider_get_value(dayBrightnessSlider));
    savedNightBrightness =
        static_cast<uint8_t>(lv_slider_get_value(nightBrightnessSlider));
    automaticDayNightEnabled =
        lv_obj_has_state(automaticDayNightSwitch, LV_STATE_CHECKED);
    applySelectedSecondMode(lv_dropdown_get_selected(secondModeDropdown));
    applySelectedWeatherIconMode(
        lv_dropdown_get_selected(weatherIconModeDropdown));
    automaticFirmwareUpdateEnabled =
        lv_obj_has_state(automaticUpdateSwitch, LV_STATE_CHECKED);
    selectedWebMode = lv_dropdown_get_selected(webModeDropdown);
    if (settingsSaveCallback != nullptr) {
      settingsSaveCallback(settingsSelectedClockStyle, savedDayBrightness,
                           savedNightBrightness,
                           automaticDayNightEnabled, secondRingEnabled,
                           secondEffect, animatedWeatherIconsEnabled,
                           configuredWeatherIconStyle,
                           automaticFirmwareUpdateEnabled, selectedWebMode);
    }
    renderSecondRing(millis());
  }
  if (automaticDayNightEnabled && currentValues.sunStateAvailable) {
    const bool lightForcesDay = currentValues.dayNightLightStateAvailable &&
                                currentValues.dayNightLightOn;
    clockDashboardSetNightMode(!currentValues.weatherIsDay && !lightForcesDay);
  } else if (brightnessPreviewCallback != nullptr) {
    brightnessPreviewCallback(nightModeEnabled ? savedNightBrightness
                                               : savedDayBrightness);
  }
  // Dotykový řadič hlásí stejné uvolnění také jako globální SINGLE_CLICK.
  // Zahodíme právě tento následující klik. Časový limit slouží jen jako
  // pojistka, aby se nepotlačil pozdější skutečný dotyk, kdyby řadič klik
  // po zavření překryvu výjimečně vůbec neposlal.
  suppressNextDashboardClick = true;
  suppressDashboardClickUntil = millis() + 3000;
  settingsVisible = false;
  lv_obj_add_flag(settingsPage, LV_OBJ_FLAG_HIDDEN);
  updateScreenDots();
}

void cancelSettingsEvent(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED) closeSettings(false);
}

void saveSettingsEvent(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED) closeSettings(true);
}

void brightnessSliderEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
  lv_obj_t *slider = lv_event_get_target(event);
  const uint8_t brightness = static_cast<uint8_t>(lv_slider_get_value(slider));
  if (slider == dayBrightnessSlider) {
    updateBrightnessLabel(dayBrightnessValueLabel, brightness, 105, -72);
  } else {
    updateBrightnessLabel(nightBrightnessValueLabel, brightness, 105, 8);
  }
  if (brightnessPreviewCallback != nullptr) brightnessPreviewCallback(brightness);
}

void showSettingsSubpage(uint8_t page) {
  settingsPageIndex = constrain(page, static_cast<uint8_t>(0),
                                static_cast<uint8_t>(SETTINGS_PAGE_COUNT - 1));
  for (uint8_t index = 0; index < SETTINGS_PAGE_COUNT; ++index) {
    setObjectVisible(settingsContent[index], index == settingsPageIndex);
  }
  if (settingsPageNumberLabel != nullptr) {
    char pageNumber[2] = {static_cast<char>('1' + settingsPageIndex), '\0'};
    lv_label_set_text(settingsPageNumberLabel, pageNumber);
    alignCenter(settingsPageNumberLabel, 0, -184);
  }
  if (settingsPreviousButton != nullptr) {
    if (settingsPageIndex == 0)
      lv_obj_add_state(settingsPreviousButton, LV_STATE_DISABLED);
    else
      lv_obj_clear_state(settingsPreviousButton, LV_STATE_DISABLED);
  }
  if (settingsNextButton != nullptr) {
    if (settingsPageIndex == SETTINGS_PAGE_COUNT - 1)
      lv_obj_add_state(settingsNextButton, LV_STATE_DISABLED);
    else
      lv_obj_clear_state(settingsNextButton, LV_STATE_DISABLED);
  }
}

void settingsPreviousEvent(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_RELEASED &&
      settingsPageIndex > 0)
    showSettingsSubpage(settingsPageIndex - 1);
}

void settingsNextEvent(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_RELEASED &&
      settingsPageIndex < SETTINGS_PAGE_COUNT - 1)
    showSettingsSubpage(settingsPageIndex + 1);
}

void updateClockStyleCardSelection() {
  lv_obj_t *const cards[] = {digitalClockStyleCard, analogClockStyleCard,
                             valuesClockStyleCard};
  const uint8_t styles[] = {CLOCK_STYLE_DIGITAL, CLOCK_STYLE_ANALOG,
                            CLOCK_STYLE_VALUES};
  for (size_t index = 0; index < 3; ++index) {
    if (cards[index] == nullptr) continue;
    const bool selected = settingsSelectedClockStyle == styles[index];
    lv_obj_set_style_border_width(cards[index], selected ? 4 : 1, 0);
    lv_obj_set_style_border_color(
        cards[index], selected ? COLOR_OUTSIDE : COLOR_DIVIDER, 0);
  }
}

void clockStyleCardEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_SHORT_CLICKED) return;
  settingsSelectedClockStyle = static_cast<uint8_t>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  updateClockStyleCardSelection();
}

void drawClockStylePreviewEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_DRAW_MAIN) return;
  lv_obj_t *preview = lv_event_get_target(event);
  lv_draw_ctx_t *drawContext = lv_event_get_draw_ctx(event);
  lv_area_t coordinates;
  lv_obj_get_coords(preview, &coordinates);
  const lv_point_t center = {
      static_cast<lv_coord_t>((coordinates.x1 + coordinates.x2) / 2),
      static_cast<lv_coord_t>((coordinates.y1 + coordinates.y2) / 2),
  };
  const uint8_t style = static_cast<uint8_t>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  const AnalogDrawTarget target = {drawContext, nullptr};
  // Náhledy byly kresleny na 136 px široké kartě; tři karty se na kulatý
  // displej vejdou jen užší, takže se všechny poloměry škálují podle šířky.
  const float scale =
      static_cast<float>(coordinates.x2 - coordinates.x1 + 1) / 136.0f;
  const auto scaled = [scale](float value) {
    return static_cast<int>(std::lround(value * scale));
  };
  // Vodorovná čárka jako zástupný symbol textu v náhledu.
  const auto drawBar = [&](int offsetX, int offsetY, int halfWidth,
                           lv_color_t color, int thickness) {
    const lv_point_t from = {
        static_cast<lv_coord_t>(center.x + scaled(offsetX - halfWidth)),
        static_cast<lv_coord_t>(center.y + scaled(offsetY)),
    };
    const lv_point_t to = {
        static_cast<lv_coord_t>(center.x + scaled(offsetX + halfWidth)),
        from.y,
    };
    drawAnalogLine(target, from, to, color, thickness);
  };

  drawAnalogCircle(target, center, scaled(62), LV_COLOR_MAKE(8, 13, 17));
  drawAnalogArc(target, center, scaled(60),
                style == CLOCK_STYLE_ANALOG ? analogTone(0.65f)
                                            : COLOR_DIVIDER,
                2);

  if (style == CLOCK_STYLE_ANALOG) {
    for (uint8_t hour = 0; hour < 12; ++hour) {
      const bool cardinal = hour % 3 == 0;
      drawAnalogRadialLine(target, center, hour * 30.0f,
                           cardinal ? 49.0f * scale : 53.0f * scale,
                           57.0f * scale,
                           cardinal ? COLOR_ROOM : analogTone(0.7f),
                           cardinal ? 3 : 1);
    }
    drawAnalogLine(target, center,
                   analogPoint(center, 305.0f, 31.0f * scale), COLOR_TEXT, 6);
    drawAnalogLine(target, center, analogPoint(center, 50.0f, 45.0f * scale),
                   COLOR_TEXT, 4);
    drawAnalogLine(target, analogPoint(center, 180.0f, 13.0f * scale),
                   analogPoint(center, 180.0f, 51.0f * scale), analogTone(),
                   2);
    drawAnalogCircle(target, center, scaled(5), analogTone());
    drawAnalogCircle(target, center, scaled(2), COLOR_TEXT);
  } else if (style == CLOCK_STYLE_VALUES) {
    // Malý čas nahoře a mřížka 2 x 4 pod ním, ve stejném pořadí barev jako
    // na skutečné obrazovce.
    drawBar(0, -38, 20, COLOR_TEXT, 7);
    const lv_color_t columnColors[2] = {COLOR_OUTSIDE, COLOR_ROOM};
    for (int row = 0; row < 4; ++row) {
      for (int column = 0; column < 2; ++column) {
        const int offsetX = column == 0 ? -25 : 25;
        const int offsetY = -14 + row * 15;
        drawBar(offsetX, offsetY, 16, columnColors[column], 4);
      }
    }
  } else {
    const lv_point_t dividerFrom = {
        static_cast<lv_coord_t>(center.x + scaled(-42)),
        static_cast<lv_coord_t>(center.y + scaled(29)),
    };
    const lv_point_t dividerTo = {
        static_cast<lv_coord_t>(center.x + scaled(42)), dividerFrom.y,
    };
    drawAnalogLine(target, dividerFrom, dividerTo, COLOR_DIVIDER, 1);
    drawAnalogArc(target, center, scaled(54), COLOR_OUTSIDE, 2, LV_OPA_60);
  }
}

// Tři karty vedle sebe se musí vejít do kruhu o průměru 480 px: nejvzdálenější
// roh karty (144 + 66, 6 + 95) leží 233 px od středu, tedy uvnitř.
constexpr int CLOCK_STYLE_CARD_WIDTH = 132;
constexpr int CLOCK_STYLE_CARD_HEIGHT = 190;
constexpr int CLOCK_STYLE_CARD_PREVIEW = 104;

lv_obj_t *makeClockStyleCard(lv_obj_t *parent, uint8_t style, int x,
                             lv_obj_t **textLabel) {
  lv_obj_t *card = lv_btn_create(parent);
  lv_obj_set_size(card, CLOCK_STYLE_CARD_WIDTH, CLOCK_STYLE_CARD_HEIGHT);
  alignCenter(card, x, 6);
  lv_obj_set_style_radius(card, 20, 0);
  lv_obj_set_style_bg_color(card, LV_COLOR_MAKE(13, 18, 22), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(card, 0, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_add_event_cb(card, clockStyleCardEvent, LV_EVENT_SHORT_CLICKED,
                      reinterpret_cast<void *>(static_cast<uintptr_t>(style)));

  lv_obj_t *preview = lv_obj_create(card);
  lv_obj_set_size(preview, CLOCK_STYLE_CARD_PREVIEW, CLOCK_STYLE_CARD_PREVIEW);
  alignCenter(preview, 0, -26);
  lv_obj_set_style_bg_opa(preview, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(preview, 0, 0);
  lv_obj_set_style_pad_all(preview, 0, 0);
  lv_obj_clear_flag(preview, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(preview, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(
      preview, drawClockStylePreviewEvent, LV_EVENT_DRAW_MAIN,
      reinterpret_cast<void *>(static_cast<uintptr_t>(style)));

  if (style == CLOCK_STYLE_DIGITAL) {
    lv_obj_t *time = makeLabel(preview, &lv_font_montserrat_20, COLOR_TEXT);
    lv_label_set_text(time, "10:09");
    alignCenter(time, 0, -15);
    lv_obj_t *date = makeLabel(preview, &lv_font_montserrat_12, COLOR_MUTED);
    lv_label_set_text(date, "SO 30. 8.");
    alignCenter(date, 0, 6);
    lv_obj_t *values =
        makeLabel(preview, &lv_font_montserrat_12, COLOR_OUTSIDE);
    lv_label_set_text(values, "22.4°  45%");
    alignCenter(values, 0, 24);
  }

  lv_obj_t *label = makeLabel(card, &clock_czech_16, COLOR_TEXT);
  if (textLabel != nullptr) *textLabel = label;
  lv_label_set_text(label,
                    style == CLOCK_STYLE_ANALOG   ? "ANALOGOVÉ"
                    : style == CLOCK_STYLE_VALUES ? "HODNOTY"
                                                  : "DIGITÁLNÍ");
  alignCenter(label, 0, 74);
  lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
  return card;
}

lv_obj_t *makeSettingsSwitch(lv_obj_t *parent, const char *title, int y,
                             bool checked, lv_obj_t **titleLabel = nullptr) {
  lv_obj_t *label = makeLabel(parent, &clock_czech_16, COLOR_TEXT);
  if (titleLabel != nullptr) *titleLabel = label;
  lv_label_set_text(label, title);
  alignCenter(label, -55, y);
  lv_obj_t *control = lv_switch_create(parent);
  lv_obj_set_size(control, 54, 28);
  alignCenter(control, 132, y);
  lv_obj_set_style_bg_color(control, COLOR_DIVIDER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(control, COLOR_AIR,
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(control, COLOR_TEXT, LV_PART_KNOB);
  if (checked) lv_obj_add_state(control, LV_STATE_CHECKED);
  return control;
}

lv_obj_t *makeSettingsDropdown(lv_obj_t *parent, const char *title,
                               const char *options, int y, uint8_t selected,
                               int labelX = -92, int controlX = 95,
                               int controlWidth = 180,
                               int labelYOffset = 0,
                               bool centerText = false,
                               lv_obj_t **titleLabel = nullptr) {
  lv_obj_t *label = makeLabel(parent, &clock_czech_16, COLOR_TEXT);
  if (titleLabel != nullptr) *titleLabel = label;
  lv_label_set_text(label, title);
  alignCenter(label, labelX, y + labelYOffset);
  lv_obj_t *control = lv_dropdown_create(parent);
  lv_obj_set_size(control, controlWidth, 42);
  alignCenter(control, controlX, y);
  lv_dropdown_set_options(control, options);
  lv_dropdown_set_symbol(control, centerText ? nullptr : "v");
  lv_dropdown_set_selected(control, selected);
  lv_obj_set_style_bg_color(control, COLOR_DIVIDER, 0);
  lv_obj_set_style_text_color(control, COLOR_TEXT, 0);
  lv_obj_set_style_text_font(control, &clock_czech_16, 0);
  lv_obj_set_style_text_font(lv_dropdown_get_list(control), &clock_czech_16, 0);
  if (centerText) {
    lv_obj_set_style_text_align(control, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(lv_dropdown_get_list(control),
                                LV_TEXT_ALIGN_CENTER, 0);
  }
  return control;
}

void firmwareCheckEvent(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED &&
      firmwareCheckCallback != nullptr) firmwareCheckCallback();
}

void firmwareInstallEvent(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED &&
      firmwareInstallCallback != nullptr) firmwareInstallCallback();
}

void createSettingsPage(lv_obj_t *screen) {
  settingsPage = lv_obj_create(screen);
  lv_obj_set_size(settingsPage, 480, 480);
  lv_obj_center(settingsPage);
  lv_obj_set_style_bg_color(settingsPage, COLOR_BACKGROUND, 0);
  lv_obj_set_style_bg_opa(settingsPage, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(settingsPage, 0, 0);
  lv_obj_set_style_pad_all(settingsPage, 0, 0);
  lv_obj_set_style_radius(settingsPage, 0, 0);
  lv_obj_clear_flag(settingsPage, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *settingsRing = lv_obj_create(settingsPage);
  lv_obj_set_size(settingsRing, 474, 474);
  lv_obj_center(settingsRing);
  lv_obj_set_style_radius(settingsRing, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(settingsRing, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(settingsRing, 3, 0);
  lv_obj_set_style_border_color(settingsRing, COLOR_DIVIDER, 0);
  lv_obj_clear_flag(settingsRing, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(settingsRing, LV_OBJ_FLAG_CLICKABLE);

  settingsPreviousButton = lv_btn_create(settingsPage);
  lv_obj_set_size(settingsPreviousButton, 58, 50);
  alignCenter(settingsPreviousButton, -58, -184);
  lv_obj_set_style_bg_color(settingsPreviousButton, COLOR_ROOM, 0);
  lv_obj_set_style_bg_opa(settingsPreviousButton, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(settingsPreviousButton, 25, 0);
  lv_obj_set_style_shadow_width(settingsPreviousButton, 0, 0);
  lv_obj_set_style_border_width(settingsPreviousButton, 0, 0);
  lv_obj_add_event_cb(settingsPreviousButton, settingsPreviousEvent,
                      LV_EVENT_RELEASED, nullptr);
  lv_obj_t *previousLabel =
      makeLabel(settingsPreviousButton, &lv_font_montserrat_28, COLOR_BACKGROUND);
  lv_label_set_text(previousLabel, LV_SYMBOL_LEFT);
  lv_obj_center(previousLabel);
  lv_obj_clear_flag(previousLabel, LV_OBJ_FLAG_CLICKABLE);

  settingsNextButton = lv_btn_create(settingsPage);
  lv_obj_set_size(settingsNextButton, 58, 50);
  alignCenter(settingsNextButton, 58, -184);
  lv_obj_set_style_bg_color(settingsNextButton, COLOR_ROOM, 0);
  lv_obj_set_style_bg_opa(settingsNextButton, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(settingsNextButton, 25, 0);
  lv_obj_set_style_shadow_width(settingsNextButton, 0, 0);
  lv_obj_set_style_border_width(settingsNextButton, 0, 0);
  lv_obj_add_event_cb(settingsNextButton, settingsNextEvent,
                      LV_EVENT_RELEASED, nullptr);
  lv_obj_t *nextLabel =
      makeLabel(settingsNextButton, &lv_font_montserrat_28, COLOR_BACKGROUND);
  lv_label_set_text(nextLabel, LV_SYMBOL_RIGHT);
  lv_obj_center(nextLabel);
  lv_obj_clear_flag(nextLabel, LV_OBJ_FLAG_CLICKABLE);

  settingsPageNumberLabel =
      makeLabel(settingsPage, &lv_font_montserrat_28, COLOR_TEXT);
  lv_label_set_text(settingsPageNumberLabel, "1");
  alignCenter(settingsPageNumberLabel, 0, -184);

  for (lv_obj_t *&content : settingsContent) {
    content = lv_obj_create(settingsPage);
    lv_obj_set_size(content, 430, 315);
    alignCenter(content, 0, -5);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  }

  clockStyleTitleLabel =
      makeLabel(settingsContent[0], &clock_czech_16, COLOR_MUTED);
  lv_label_set_text(clockStyleTitleLabel, "TYP HODIN");
  alignCenter(clockStyleTitleLabel, 0, -132);
  digitalClockStyleCard = makeClockStyleCard(
      settingsContent[0], CLOCK_STYLE_DIGITAL, -144, &digitalClockStyleLabel);
  analogClockStyleCard = makeClockStyleCard(
      settingsContent[0], CLOCK_STYLE_ANALOG, 0, &analogClockStyleLabel);
  valuesClockStyleCard = makeClockStyleCard(
      settingsContent[0], CLOCK_STYLE_VALUES, 144, &valuesClockStyleLabel);
  updateClockStyleCardSelection();

  dayBrightnessTitleLabel =
      makeLabel(settingsContent[1], &clock_czech_16, COLOR_ROOM);
  lv_label_set_text(dayBrightnessTitleLabel, "DENNÍ JAS");
  alignCenter(dayBrightnessTitleLabel, -55, -72);

  dayBrightnessValueLabel =
      makeLabel(settingsContent[1], &lv_font_montserrat_28, COLOR_TEXT);
  updateBrightnessLabel(dayBrightnessValueLabel, savedDayBrightness, 105, -72);

  dayBrightnessSlider = lv_slider_create(settingsContent[1]);
  lv_obj_set_size(dayBrightnessSlider, 330, 20);
  alignCenter(dayBrightnessSlider, 0, -40);
  lv_slider_set_range(dayBrightnessSlider, 1, 100);
  lv_slider_set_value(dayBrightnessSlider, savedDayBrightness, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(dayBrightnessSlider, COLOR_DIVIDER, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(dayBrightnessSlider, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(dayBrightnessSlider, COLOR_ROOM,
                            LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(dayBrightnessSlider, LV_OPA_COVER,
                          LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(dayBrightnessSlider, COLOR_TEXT, LV_PART_KNOB);
  lv_obj_set_style_pad_all(dayBrightnessSlider, 5, LV_PART_KNOB);
  lv_obj_add_event_cb(dayBrightnessSlider, brightnessSliderEvent, LV_EVENT_ALL,
                      nullptr);

  nightBrightnessTitleLabel =
      makeLabel(settingsContent[1], &clock_czech_16, COLOR_OUTSIDE);
  lv_label_set_text(nightBrightnessTitleLabel, "NOČNÍ JAS");
  alignCenter(nightBrightnessTitleLabel, -55, 8);

  nightBrightnessValueLabel =
      makeLabel(settingsContent[1], &lv_font_montserrat_28, COLOR_TEXT);
  updateBrightnessLabel(nightBrightnessValueLabel, savedNightBrightness, 105, 8);

  nightBrightnessSlider = lv_slider_create(settingsContent[1]);
  lv_obj_set_size(nightBrightnessSlider, 330, 20);
  alignCenter(nightBrightnessSlider, 0, 40);
  lv_slider_set_range(nightBrightnessSlider, 1, 100);
  lv_slider_set_value(nightBrightnessSlider, savedNightBrightness, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(nightBrightnessSlider, COLOR_DIVIDER, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(nightBrightnessSlider, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(nightBrightnessSlider, COLOR_OUTSIDE,
                            LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(nightBrightnessSlider, LV_OPA_COVER,
                          LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(nightBrightnessSlider, COLOR_TEXT, LV_PART_KNOB);
  lv_obj_set_style_pad_all(nightBrightnessSlider, 5, LV_PART_KNOB);
  lv_obj_add_event_cb(nightBrightnessSlider, brightnessSliderEvent, LV_EVENT_ALL,
                      nullptr);

  automaticDayNightSwitch = makeSettingsSwitch(
      settingsContent[1], "AUTOMATICKY DEN/NOC", 92, automaticDayNightEnabled,
      &automaticDayNightTitleLabel);

  weatherIconModeDropdown = makeSettingsDropdown(
      settingsContent[2], "IKONY POČASÍ",
      "STATICKÉ MONOCHROMATICKÉ\nANIMOVANÉ FLAT\nANIMOVANÉ LINE\nANIMOVANÉ MONOCHROMATICKÉ",
      -54, selectedWeatherIconMode(), 0, 0, 360, -40, true,
      &weatherIconModeTitleLabel);
  secondModeDropdown = makeSettingsDropdown(
      settingsContent[2], "VTEŘINY", "VYPNUTO\nTEČKY\nLINKA\nKOMETA", 50,
      selectedSecondMode(), 0, 0, 360, -40, true, &secondModeTitleLabel);

  wifiAddressLabel = makeLabel(settingsContent[3], &lv_font_montserrat_16, COLOR_MUTED);
  lv_label_set_text(wifiAddressLabel, "IP: —");
  alignCenter(wifiAddressLabel, 0, -112);
  firmwareVersionLabel = makeLabel(settingsContent[3], &lv_font_montserrat_16, COLOR_MUTED);
  lv_label_set_text(firmwareVersionLabel, "FIRMWARE: —");
  alignCenter(firmwareVersionLabel, 0, -88);
  deviceInfoLabel = makeLabel(settingsContent[3], &clock_czech_16, COLOR_MUTED);
  lv_label_set_text(deviceInfoLabel, "");
  alignCenter(deviceInfoLabel, 0, -64);
  webModeDropdown = makeSettingsDropdown(
      settingsContent[3], "WEB", "10 MINUT\nVŽDY\nVYPNUTÝ", -24,
      selectedWebMode, -92, 95, 180, 0, false, &webModeTitleLabel);
  automaticUpdateSwitch = makeSettingsSwitch(
      settingsContent[3], "AUTOMATICKÉ OTA", 30,
      automaticFirmwareUpdateEnabled, &automaticUpdateTitleLabel);

  firmwareCheckButton = lv_btn_create(settingsContent[3]);
  lv_obj_set_size(firmwareCheckButton, 190, 42);
  alignCenter(firmwareCheckButton, 0, 88);
  lv_obj_set_style_radius(firmwareCheckButton, 21, 0);
  lv_obj_set_style_bg_color(firmwareCheckButton, COLOR_HUMIDITY, 0);
  lv_obj_add_event_cb(firmwareCheckButton, firmwareCheckEvent, LV_EVENT_SHORT_CLICKED, nullptr);
  firmwareCheckLabel =
      makeLabel(firmwareCheckButton, &clock_czech_16, COLOR_TEXT);
  lv_label_set_text(firmwareCheckLabel, "ZKONTROLOVAT");
  lv_obj_center(firmwareCheckLabel);

  firmwareInstallButton = lv_btn_create(settingsContent[3]);
  lv_obj_set_size(firmwareInstallButton, 190, 42);
  alignCenter(firmwareInstallButton, 0, 88);
  lv_obj_set_style_radius(firmwareInstallButton, 21, 0);
  lv_obj_set_style_bg_color(firmwareInstallButton, COLOR_AIR, 0);
  lv_obj_add_event_cb(firmwareInstallButton, firmwareInstallEvent,
                      LV_EVENT_SHORT_CLICKED, nullptr);
  firmwareInstallLabel =
      makeLabel(firmwareInstallButton, &clock_czech_16, COLOR_BACKGROUND);
  lv_label_set_text(firmwareInstallLabel, "AKTUALIZOVAT");
  lv_obj_center(firmwareInstallLabel);
  lv_obj_add_flag(firmwareInstallButton, LV_OBJ_FLAG_HIDDEN);
  firmwareStatusLabel = makeLabel(settingsContent[3], &clock_czech_16, COLOR_MUTED);
  lv_obj_set_width(firmwareStatusLabel, 360);
  lv_obj_set_style_text_align(firmwareStatusLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(firmwareStatusLabel, "");
  alignCenter(firmwareStatusLabel, 0, 123);

  lv_obj_t *cancelButton = lv_btn_create(settingsPage);
  lv_obj_set_size(cancelButton, 64, 64);
  alignCenter(cancelButton, 50, 180);
  lv_obj_set_style_radius(cancelButton, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(cancelButton, COLOR_ERROR, 0);
  lv_obj_add_event_cb(cancelButton, cancelSettingsEvent, LV_EVENT_SHORT_CLICKED, nullptr);
  lv_obj_t *cancelLabel = makeLabel(cancelButton, &lv_font_montserrat_28, COLOR_TEXT);
  lv_label_set_text(cancelLabel, LV_SYMBOL_CLOSE);
  lv_obj_center(cancelLabel);

  lv_obj_t *saveButton = lv_btn_create(settingsPage);
  lv_obj_set_size(saveButton, 64, 64);
  alignCenter(saveButton, -50, 180);
  lv_obj_set_style_radius(saveButton, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(saveButton, COLOR_AIR, 0);
  lv_obj_add_event_cb(saveButton, saveSettingsEvent, LV_EVENT_SHORT_CLICKED, nullptr);
  lv_obj_t *saveLabel = makeLabel(saveButton, &lv_font_montserrat_28, COLOR_BACKGROUND);
  lv_label_set_text(saveLabel, LV_SYMBOL_OK);
  lv_obj_center(saveLabel);

  showSettingsSubpage(0);

  lv_obj_add_flag(settingsPage, LV_OBJ_FLAG_HIDDEN);
}
// Čas a datum hodnotové obrazovky se řídí stejnými barvami jako digitální
// ciferník, včetně červené noční palety.
void applyValuesPageColors() {
  if (valuesTimeLabel == nullptr || valuesDateLabel == nullptr) return;
  const bool redNight = redNightVisualEnabled();
  setTextColor(valuesTimeLabel,
               redNight ? COLOR_ERROR : configuredColor(timeColor));
  setTextColor(valuesDateLabel,
               redNight ? COLOR_ERROR : configuredColor(dateColor));
  if (valuesNamedayLabel != nullptr)
    setTextColor(valuesNamedayLabel,
                 redNight ? COLOR_ERROR : configuredColor(dateColor));
}

}  // namespace

uint8_t clockDashboardWeatherIconStyle(uint8_t configuredStyle) {
  const bool monochromeAnalogValues =
      analogLayoutEnabled() && analogMonochromeValuesEnabled;
  return redNightVisualEnabled() || monochromeAnalogValues
             ? CLOCK_WEATHER_ICON_STYLE_MONOCHROME
             : configuredStyle;
}

// Obrazovka CLOCK_STYLE_VALUES: malý čas nahoře, osm hodnot v mřížce 2 x 4 a
// devátá na středu pod mřížkou. Kruhový displej ubírá šířku u okrajů, proto
// jsou sloupce blíž ke středu a krajní řádky se drží dál od okraje než
// prostřední. Devátý řádek drží stejnou rozteč 60 px jako mřížka; uprostřed
// je kruh nejširší, takže mu okraj zbude stejný jako času nahoře.
constexpr int VALUE_SLOT_COLUMN_X[2] = {-104, 104};
constexpr int VALUE_SLOT_ROW_Y[4] = {-72, -12, 48, 108};
constexpr int VALUE_SLOT_BOTTOM_Y = 168;
constexpr int VALUE_SLOT_CELL_WIDTH = 190;

struct ValueSlotPosition {
  int x;
  int y;
};

ValueSlotPosition valueSlotPosition(size_t index) {
  if (index >= CLOCK_VALUE_GRID_SLOT_COUNT) return {0, VALUE_SLOT_BOTTOM_Y};
  return {VALUE_SLOT_COLUMN_X[index % 2], VALUE_SLOT_ROW_Y[index / 2]};
}

float valueSlotReading(size_t index) {
  const float slotValue = currentValues.slotValues[index];
  if (!std::isnan(slotValue)) return slotValue;
  // Sloty 0-3 zrcadlí původní pozice, takže ukazují hodnotu i v režimu
  // Open-Meteo, kde se nečte z Home Assistantu a slotValues zůstávají prázdné.
  switch (index) {
    case 0: return currentValues.leftTemperatureC;
    case 1: return currentValues.rightTemperatureC;
    case 2: return currentValues.metricAValue;
    case 3: return currentValues.metricBValue;
    default: return NAN;
  }
}

void makeValuesPage(lv_obj_t *screen) {
  valuesPage = lv_obj_create(screen);
  lv_obj_set_size(valuesPage, 480, 480);
  lv_obj_center(valuesPage);
  // Průhledné pozadí nechá prosvítat vteřinový prstenec, stejně jako u
  // digitálního a analogového ciferníku. Podklad kreslí sama obrazovka.
  lv_obj_set_style_bg_opa(valuesPage, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(valuesPage, 0, 0);
  lv_obj_set_style_pad_all(valuesPage, 0, 0);
  lv_obj_set_style_radius(valuesPage, 0, 0);
  lv_obj_clear_flag(valuesPage, LV_OBJ_FLAG_SCROLLABLE);

  valuesTimeLabel = makeLabel(valuesPage, &lv_font_montserrat_48, COLOR_TEXT);
  lv_label_set_text(valuesTimeLabel, "--:--");
  alignCenter(valuesTimeLabel, 0, -168);
  valuesDateLabel = makeLabel(valuesPage, &clock_czech_16, COLOR_MUTED);
  lv_label_set_text(valuesDateLabel, "");
  alignCenter(valuesDateLabel, 0, -130);
  valuesNamedayLabel = makeLabel(valuesPage, &clock_czech_16, COLOR_MUTED);
  lv_label_set_text(valuesNamedayLabel, "");
  alignCenter(valuesNamedayLabel, 0, -108);

  for (size_t index = 0; index < CLOCK_VALUE_SLOT_COUNT; ++index) {
    const ValueSlotPosition position = valueSlotPosition(index);
    const int x = position.x;
    const int y = position.y;
    lv_obj_t *title = makeLabel(valuesPage, &clock_czech_16, COLOR_MUTED);
    lv_obj_set_width(title, VALUE_SLOT_CELL_WIDTH);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, "");
    alignCenter(title, x, y - 17);
    valueSlotTitleLabels[index] = title;

    lv_obj_t *value = makeLabel(valuesPage, &lv_font_montserrat_28, COLOR_TEXT);
    lv_obj_set_width(value, VALUE_SLOT_CELL_WIDTH);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(value, "--");
    alignCenter(value, x, y + 11);
    valueSlotValueLabels[index] = value;
  }

  makeChildrenTapThrough(valuesPage);
  lv_obj_add_flag(valuesPage, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(valuesPage, LV_OBJ_FLAG_HIDDEN);
}

// Popis jedné buňky. Sloty mají vlastní entitu jen v režimu Home Assistant;
// v režimu Open-Meteo se sloty 0-3 kreslí z původních čtyř pozic, které
// clockDashboardApplyConfiguration už přemapovala na názvy, jednotky a
// desetinná místa Open-Meteo, případně TMEP. Bez toho by mřížka ukazovala
// jména a jednotky zděděná po Home Assistantu.
struct ValueSlotDisplay {
  const char *name;
  const char *suffix;
  uint8_t decimals;
  const ClockMetricColorScale *colorScale;
};

ValueSlotDisplay valueSlotDisplay(size_t index,
                                  const ClockValueSlotConfig &slot) {
  const bool openMeteo =
      dashboardRuntimeConfig.dataSource == CLOCK_DATA_SOURCE_OPEN_METEO;
  if (!openMeteo || index > 3) {
    return {slot.name[0] != '\0' ? slot.name : slot.preset, slot.suffix,
            slot.decimals, &slot.colorScale};
  }
  switch (index) {
    case 0:
      return {dashboardRuntimeConfig.openMeteoSlots[0].name, outsideUnit,
              outsideDecimals, &leftValueColorScale};
    case 1:
      return {dashboardRuntimeConfig.openMeteoSlots[1].name, roomUnit,
              roomDecimals, &rightValueColorScale};
    case 2:
      return {metricAConfig.name, metricAConfig.suffix, metricAConfig.decimals,
              &metricAColorScale};
    default:
      return {metricBConfig.name, metricBConfig.suffix, metricBConfig.decimals,
              &metricBColorScale};
  }
}

// Přepíše devět buněk podle aktuální konfigurace a naměřených hodnot. Vypnutý
// slot zůstane prázdný, aby mřížka nedržela zbytek po dřívějším nastavení.
//
// Ikona slotu (slot.icon) se zatím nekreslí: písmo clock_icons_42 je vysoké
// 42 px, zatímco řádky mřížky mají rozteč 60 px a už je zabírá název s
// hodnotou. Doplnění vyžaduje menší řez ikon, ne úpravu tohoto souboru.
void updateValuesPage() {
  if (valuesPage == nullptr || !valuesLayoutEnabled()) return;
  const bool openMeteo =
      dashboardRuntimeConfig.dataSource == CLOCK_DATA_SOURCE_OPEN_METEO;
  for (size_t index = 0; index < CLOCK_VALUE_SLOT_COUNT; ++index) {
    lv_obj_t *title = valueSlotTitleLabels[index];
    lv_obj_t *value = valueSlotValueLabels[index];
    if (title == nullptr || value == nullptr) continue;
    const ClockValueSlotConfig &slot =
        clockConfigValueSlot(dashboardRuntimeConfig, index);
    // Sloty 4-8 nemají v režimu Open-Meteo odkud brát hodnotu ani kde se
    // nastavit, takže by zůstaly natrvalo prázdné.
    if (!slot.enabled || (openMeteo && index > 3)) {
      setObjectVisible(title, false);
      setObjectVisible(value, false);
      continue;
    }
    setObjectVisible(title, true);
    setObjectVisible(value, true);

    const ValueSlotDisplay display = valueSlotDisplay(index, slot);
    lv_label_set_text(title, display.name);

    const float reading = valueSlotReading(index);
    char number[16];
    formatMetricValue(number, sizeof(number), reading, display.decimals);
    char suffix[CLOCK_METRIC_SUFFIX_LENGTH];
    strlcpy(suffix, display.suffix, sizeof(suffix));
    normalizeMicroSign(suffix);
    char text[16 + CLOCK_METRIC_SUFFIX_LENGTH + 2];
    if (std::isnan(reading) || suffix[0] == '\0') {
      snprintf(text, sizeof(text), "%s", number);
    } else {
      snprintf(text, sizeof(text), "%s %s", number, suffix);
    }
    lv_label_set_text(value, text);

    if (redNightVisualEnabled()) {
      setTextColor(value, COLOR_ERROR);
      setTextColor(title, COLOR_ERROR);
    } else {
      setTextColor(value, metricColorForValue(reading, *display.colorScale));
      setTextColor(title, COLOR_MUTED);
    }
  }
}

void clockDashboardInit(const ClockValues &values, uint8_t dayBrightness,
                        uint8_t nightBrightness, bool automaticDayNight,
                        BrightnessPreviewCallback brightnessPreview,
                        SettingsOpenCallback settingsOpen,
                        SettingsSaveCallback settingsSave,
                        SettingsActionCallback firmwareCheck,
                        SettingsActionCallback firmwareInstall,
                        RadarVisibilityCallback radarVisibility,
                        RadarRangeCallback radarRange,
                        RssVisibilityCallback rssVisibility,
                        ForecastVisibilityCallback forecastVisibility) {
  savedDayBrightness = constrain(dayBrightness, 1, 100);
  savedNightBrightness = constrain(nightBrightness, 1, 100);
  automaticDayNightEnabled = automaticDayNight;
  brightnessPreviewCallback = brightnessPreview;
  settingsOpenCallback = settingsOpen;
  settingsSaveCallback = settingsSave;
  firmwareCheckCallback = firmwareCheck;
  firmwareInstallCallback = firmwareInstall;
  radarVisibilityCallback = radarVisibility;
  radarRangeCallback = radarRange;
  rssVisibilityCallback = rssVisibility;
  forecastVisibilityCallback = forecastVisibility;
  lv_obj_t *screen = lv_scr_act();
  dashboardScreen = screen;
  lv_obj_set_style_bg_color(screen, COLOR_BACKGROUND, 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  makeSecondRing(screen);

  dashboardContent = lv_obj_create(screen);
  lv_obj_set_size(dashboardContent, 480, 480);
  lv_obj_set_pos(dashboardContent, 0, analogLayoutEnabled() ? 0 : -10);
  lv_obj_set_style_bg_opa(dashboardContent, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(dashboardContent, 0, 0);
  lv_obj_set_style_pad_all(dashboardContent, 0, 0);
  lv_obj_set_style_radius(dashboardContent, 0, 0);
  lv_obj_clear_flag(dashboardContent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(dashboardContent, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *content = dashboardContent;

  timeLabel = makeLabel(content, &lv_font_montserrat_48, COLOR_TEXT);
  lv_label_set_recolor(timeLabel, true);
  lv_label_set_text(timeLabel, "--:--");
  alignCenter(timeLabel, 0, -105);

  dateLabel = makeLabel(content, &clock_czech_18, COLOR_MUTED);
  lv_obj_set_style_text_letter_space(dateLabel, 4, 0);
  lv_label_set_text(dateLabel, "");
  alignCenter(dateLabel, 0, -43);

  // Jmeniny pod datem. Font clock_czech má jen velká písmena s diakritikou,
  // proto jsou jména v tabulce už velkými písmeny.
  namedayLabel = makeLabel(content, &clock_czech_16, COLOR_MUTED);
  lv_obj_set_style_text_letter_space(namedayLabel, 2, 0);
  lv_label_set_text(namedayLabel, "");
  alignCenter(namedayLabel, 0, -20);

  outsideTitleLabel = makeLabel(content, &clock_czech_20, COLOR_OUTSIDE);
  lv_obj_set_style_text_letter_space(outsideTitleLabel, 2, 0);
  lv_label_set_text(outsideTitleLabel, "VENKU");
  alignCenter(outsideTitleLabel, -122, 5);

  roomTitleLabel = makeLabel(content, &clock_czech_20, COLOR_ROOM);
  lv_obj_set_style_text_letter_space(roomTitleLabel, 2, 0);
  lv_label_set_text(roomTitleLabel, "MÍSTNOST");
  alignCenter(roomTitleLabel, 127, 5);

  outsideIntegerLabel = makeLabel(content, &lv_font_montserrat_48, COLOR_OUTSIDE);
  outsideDecimalLabel = makeLabel(content, &lv_font_montserrat_32, COLOR_OUTSIDE);
  outsideUnitLabel = makeLabel(content, &clock_unit_24, COLOR_OUTSIDE);
  lv_label_set_text(outsideUnitLabel, "°C");

  roomIntegerLabel = makeLabel(content, &lv_font_montserrat_48, COLOR_ROOM);
  roomDecimalLabel = makeLabel(content, &lv_font_montserrat_32, COLOR_ROOM);
  roomUnitLabel = makeLabel(content, &clock_unit_24, COLOR_ROOM);
  lv_label_set_text(roomUnitLabel, "°C");

  weatherImage = lv_img_create(content);
  alignCenter(weatherImage, -142, 107);

  roomWeatherImage = lv_img_create(content);
  alignCenter(roomWeatherImage, 142, 107);
  lv_obj_add_flag(roomWeatherImage, LV_OBJ_FLAG_HIDDEN);

  weatherAnimation = lv_gif_create(content);
  alignCenter(weatherAnimation, -142, 107);
  lv_obj_add_flag(weatherAnimation, LV_OBJ_FLAG_HIDDEN);

  roomWeatherAnimation = lv_gif_create(content);
  alignCenter(roomWeatherAnimation, 142, 107);
  lv_obj_add_flag(roomWeatherAnimation, LV_OBJ_FLAG_HIDDEN);

  outsideIconLabel = makeLabel(content, &clock_icons_42, COLOR_OUTSIDE);
  lv_label_set_text(outsideIconLabel, "");
  alignCenter(outsideIconLabel, -142, 107);
  lv_obj_add_flag(outsideIconLabel, LV_OBJ_FLAG_HIDDEN);

  roomIconLabel = makeLabel(content, &clock_icons_42, COLOR_ROOM);
  lv_label_set_text(roomIconLabel, "\xEF\x80\x95");  // home (U+F015)
  alignCenter(roomIconLabel, 142, 107);

  // Digitální středový oblouk: dvě svislé nohy, horní půlkruh a krátký dřík.
  digitalAirArc = lv_arc_create(content);
  lv_obj_set_size(digitalAirArc, 196, 196);
  lv_arc_set_bg_angles(digitalAirArc, 180, 360);
  lv_arc_set_range(digitalAirArc, 0, 100);
  lv_arc_set_value(digitalAirArc, 0);
  lv_obj_remove_style(digitalAirArc, nullptr, LV_PART_KNOB);
  lv_obj_remove_style(digitalAirArc, nullptr, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(digitalAirArc, 3, LV_PART_MAIN);
  lv_obj_set_style_arc_color(digitalAirArc, COLOR_DIVIDER, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(digitalAirArc, false, LV_PART_MAIN);
  alignCenter(digitalAirArc, 0, 142);
  lv_obj_clear_flag(digitalAirArc, LV_OBJ_FLAG_CLICKABLE);

  digitalAirStem = makeDivider(content, 3, 34, 0, 28);
  digitalAirLeftLeg = makeDivider(content, 3, 34, -97, 158);
  digitalAirRightLeg = makeDivider(content, 3, 34, 96, 158);

  co2TitleLabel = makeLabel(content, &clock_czech_16, COLOR_AIR);
  lv_label_set_text(co2TitleLabel, "CO₂");
  alignCenter(co2TitleLabel, 0, 69);

  co2ValueLabel = makeLabel(content, &lv_font_montserrat_32, COLOR_AIR);

  co2UnitLabel = makeLabel(content, &clock_czech_16, COLOR_AIR);
  lv_label_set_text(co2UnitLabel, "ppm");

  digitalMetricDivider = makeDivider(content, 152, 2, 0, 121);

  humidityTitleLabel =
      makeLabel(content, &clock_czech_16, COLOR_HUMIDITY);
  lv_label_set_text(humidityTitleLabel, "VLHKOST");
  alignCenter(humidityTitleLabel, -43, 151);

  humidityValueLabel = makeLabel(content, &lv_font_montserrat_36, COLOR_HUMIDITY);
  alignCenter(humidityValueLabel, 34, 151);

  humidityUnitLabel =
      makeLabel(content, &clock_czech_20, COLOR_HUMIDITY);
  lv_label_set_text(humidityUnitLabel, "%");
  alignCenter(humidityUnitLabel, 73, 154);

  digitalBottomDivider = makeDivider(content, 286, 2, 0, 174);

  wifiStatusLabel = makeLabel(content, &lv_font_montserrat_16, COLOR_ERROR);
  lv_label_set_text(wifiStatusLabel, LV_SYMBOL_WIFI);

  statusLabel = makeLabel(content, &lv_font_montserrat_16, COLOR_ERROR);
  lv_label_set_text(statusLabel, LV_SYMBOL_HOME);

  webStatusLabel = makeLabel(content, &lv_font_montserrat_16, COLOR_OUTSIDE);
  lv_label_set_text(webStatusLabel, LV_SYMBOL_SETTINGS);
  lv_obj_add_flag(webStatusLabel, LV_OBJ_FLAG_HIDDEN);
  alignConnectionStatusIcons();

  createAnalogLayout(content);

  clockDashboardUpdate(values);
  makeChildrenTapThrough(dashboardContent);
  createRadarPage(screen);
  createRssPage(screen);
  createSettingsPage(screen);

  firmwareUpdateOverlay = lv_obj_create(screen);
  lv_obj_set_size(firmwareUpdateOverlay, 480, 480);
  lv_obj_set_pos(firmwareUpdateOverlay, 0, 0);
  lv_obj_set_style_bg_color(firmwareUpdateOverlay, COLOR_BACKGROUND, 0);
  lv_obj_set_style_bg_opa(firmwareUpdateOverlay, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(firmwareUpdateOverlay, 0, 0);
  lv_obj_set_style_radius(firmwareUpdateOverlay, 0, 0);
  lv_obj_clear_flag(firmwareUpdateOverlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(firmwareUpdateOverlay, LV_OBJ_FLAG_HIDDEN);
  firmwareUpdateTitleLabel =
      makeLabel(firmwareUpdateOverlay, &lv_font_montserrat_32, COLOR_TEXT);
  lv_obj_set_width(firmwareUpdateTitleLabel, 460);
  lv_obj_set_style_text_align(firmwareUpdateTitleLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(firmwareUpdateTitleLabel, "AKTUALIZACE FIRMWARE");
  alignCenter(firmwareUpdateTitleLabel, 0, -38);
  firmwareUpdateCountdownLabel =
      makeLabel(firmwareUpdateOverlay, &lv_font_montserrat_48, COLOR_TEXT);
  lv_label_set_text(firmwareUpdateCountdownLabel, "5");
  alignCenter(firmwareUpdateCountdownLabel, 0, 35);
  makeValuesPage(screen);
  if (valuesLayoutEnabled()) {
    lv_obj_add_flag(dashboardContent, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(valuesPage, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(valuesPage);
  }
  createScreenDots(screen);

  // Změna vzhledu musí znovu naplnit oba framebuffery celým shodným
  // ciferníkem. Teprve potom se vrátíme k částečnému direct-mode renderu.
  displayDriverSetPartialRefresh(analogLayoutEnabled(),
                                 analogLayoutEnabled());
}

void clockDashboardApplyConfiguration(const ClockConfig &config) {
  dashboardRuntimeConfig = config;
  dashboardRuntimeConfigAvailable = true;
  radarFeatureAvailable = clockConfigRadarAvailable(config);
  radarStatusLineEnabled = config.radarStatusLine;
  clockDashboardSetRssAvailable(clockConfigRssAvailable(config));
  clockDashboardSetForecastAvailable(clockConfigForecastAvailable(config));
  clockDashboardSetPlanesAvailable(clockConfigPlanesAvailable(config));
  clockDashboardSetAgendaAvailable(clockConfigAgendaAvailable(config));
  // Počet řádků se odvíjí od kvality ovzduší a počtu dnů, takže se po každé
  // změně nastavení musí přepočítat - jinak by obrazovka kreslila hodiny do
  // místa, které si mezitím vzala spodní sekce.
  if (forecastPage != nullptr) {
    const uint8_t hours = weatherForecastHourCapacity(
        config.forecast.airQuality, config.forecast.dayCount);
    if (forecastAirQualityEnabled != config.forecast.airQuality ||
        forecastHourRowCount != hours ||
        forecastDayRowCount != config.forecast.dayCount) {
      forecastAirQualityEnabled = config.forecast.airQuality;
      rebuildForecastRows(hours, config.forecast.dayCount);
    }
  }
  if (!radarFeatureAvailable && activeScreen == DASHBOARD_SCREEN_RADAR) {
    activeScreen = DASHBOARD_SCREEN_CLOCK;
    lv_obj_add_flag(radarPage, LV_OBJ_FLAG_HIDDEN);
    if (!settingsVisible && !firmwareUpdateActive) {
      lv_obj_clear_flag(primaryClockPage(), LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(primaryClockPage());
    }
    if (radarVisibilityCallback != nullptr) radarVisibilityCallback(false);
  }
  language = constrain(config.language,
                       static_cast<uint8_t>(CLOCK_LANGUAGE_UNSET),
                       static_cast<uint8_t>(CLOCK_LANGUAGE_ENGLISH));
  applyDashboardLanguage();
  const bool wasRedNight = redNightVisualEnabled();
  const bool nightVisualChanged = nightVisualMode != config.nightVisualMode;
  nightVisualMode = config.nightVisualMode;
  metricAConfig = config.metricA;
  metricBConfig = config.metricB;
  leftValueConfig = config.leftValue;
  rightValueConfig = config.rightValue;
  normalizeMicroSign(leftValueConfig.suffix);
  normalizeMicroSign(rightValueConfig.suffix);
  normalizeMicroSign(metricAConfig.suffix);
  normalizeMicroSign(metricBConfig.suffix);
  leftValueColorScale = config.leftValueColorScale;
  rightValueColorScale = config.rightValueColorScale;
  metricAColorScale = config.metricAColorScale;
  metricBColorScale = config.metricBColorScale;
  const bool openMeteo = config.dataSource == CLOCK_DATA_SOURCE_OPEN_METEO;
  homeAssistantStatusRelevant = !openMeteo;
  const auto openMeteoUnit = [](const char *value) -> const char * {
    if (strcmp(value, "temperature_2m") == 0 ||
        strcmp(value, "apparent_temperature") == 0)
      return "°C";
    if (strcmp(value, "relative_humidity_2m") == 0 ||
        strcmp(value, "cloud_cover") == 0)
      return "%";
    if (strcmp(value, "pressure_msl") == 0 ||
        strcmp(value, "surface_pressure") == 0)
      return "hPa";
    if (strcmp(value, "wind_speed_10m") == 0 ||
        strcmp(value, "wind_gusts_10m") == 0)
      return "km/h";
    if (strcmp(value, "wind_direction_10m") == 0) return "°";
    if (strcmp(value, "snowfall") == 0) return "cm";
    if (strcmp(value, "precipitation") == 0 || strcmp(value, "rain") == 0 ||
        strcmp(value, "showers") == 0)
      return "mm";
    return "";
  };
  const auto slotUnit = [&](size_t index) -> const char * {
    return config.tmepSlots[index].enabled
               ? config.tmepSlots[index].unit
               : openMeteoUnit(config.openMeteoSlots[index].value);
  };
  const auto slotDecimals = [&](size_t index) -> uint8_t {
    return config.tmepSlots[index].decimals;
  };
  if (openMeteo) {
    clockConfigCopy(metricAConfig.name, sizeof(metricAConfig.name),
                    config.openMeteoSlots[2].name);
    clockConfigCopy(metricAConfig.suffix, sizeof(metricAConfig.suffix),
                    slotUnit(2));
    metricAConfig.decimals = slotDecimals(2);
    clockConfigCopy(metricBConfig.name, sizeof(metricBConfig.name),
                    config.openMeteoSlots[3].name);
    clockConfigCopy(metricBConfig.suffix, sizeof(metricBConfig.suffix),
                    slotUnit(3));
    metricBConfig.decimals = slotDecimals(3);
    metricAColorScale = ClockMetricColorScale{};
    metricAColorScale.points[0].color = config.openMeteoSlots[2].color;
    metricBColorScale = ClockMetricColorScale{};
    metricBColorScale.points[0].color = config.openMeteoSlots[3].color;
    leftValueColorScale = ClockMetricColorScale{};
    leftValueColorScale.points[0].color = config.openMeteoSlots[0].color;
    rightValueColorScale = ClockMetricColorScale{};
    rightValueColorScale.points[0].color = config.openMeteoSlots[1].color;
    strlcpy(outsideUnit, slotUnit(0), sizeof(outsideUnit));
    strlcpy(roomUnit, slotUnit(1), sizeof(roomUnit));
    outsideDecimals = slotDecimals(0);
    roomDecimals = slotDecimals(1);
  } else {
    strlcpy(outsideUnit, leftValueConfig.suffix, sizeof(outsideUnit));
    strlcpy(roomUnit, rightValueConfig.suffix, sizeof(roomUnit));
    outsideDecimals = leftValueConfig.decimals;
    roomDecimals = rightValueConfig.decimals;
  }
  automaticDayNightEnabled = config.automaticDayNight;
  const bool timeColonModeChanged =
      timeColonEffect != config.timeColonEffect;
  timeColonEffect = constrain(
      config.timeColonEffect, static_cast<uint8_t>(CLOCK_TIME_COLON_STEADY),
      static_cast<uint8_t>(CLOCK_TIME_COLON_FADE));
  secondRingEnabled = config.secondRingEnabled;
  secondEffect = constrain(
      config.secondEffect, static_cast<uint8_t>(CLOCK_SECOND_EFFECT_DOTS),
      static_cast<uint8_t>(CLOCK_SECOND_EFFECT_COMET));
  secondRingBackgroundColor = config.secondRingBackgroundColor & 0xFFFFFF;
  secondRingBackgroundBrightness = config.secondRingBackgroundBrightness;
  secondRingBackgroundDotSize =
      constrain(config.secondRingBackgroundDotSize, 1, 10);
  secondDotSize = constrain(config.secondDotSize, 1, 10);
  secondDotColor = config.secondDotColor & 0xFFFFFF;
  secondDotBrightness = config.secondDotBrightness;
  timeColor = config.timeColor & 0xFFFFFF;
  dateColor = config.dateColor & 0xFFFFFF;
  timeFont = constrain(config.timeFont,
                       static_cast<uint8_t>(CLOCK_TIME_FONT_BARLOW),
                       static_cast<uint8_t>(CLOCK_TIME_FONT_DOTO));
  lv_obj_set_style_text_font(timeLabel, configuredTimeFont(), 0);
  if (timeColonModeChanged) renderTimeColon(millis(), true);
  alignCenter(timeLabel, 0, -105);
  leftWeatherIconColor =
      (openMeteo ? config.openMeteoSlots[0].color
                 : config.leftWeatherIconColor) &
      0xFFFFFF;
  rightWeatherIconColor = config.rightWeatherIconColor & 0xFFFFFF;
  animatedWeatherIconsEnabled = config.animatedWeatherIcons;
  automaticFirmwareUpdateEnabled = config.automaticFirmwareUpdate;
  configuredWeatherIconStyle = constrain(
      config.weatherIconStyle,
      static_cast<uint8_t>(CLOCK_WEATHER_ICON_STYLE_MONOCHROME),
      static_cast<uint8_t>(CLOCK_WEATHER_ICON_STYLE_LINE));
  outsideUsesWeatherIcon = openMeteo || strcmp(config.leftSide.icon, "weather") == 0;
  roomUsesWeatherIcon = !openMeteo && strcmp(config.rightSide.icon, "weather") == 0;
  weatherConfigured = openMeteo || config.weatherEntityId[0] != '\0';
  outsideConfigured = openMeteo || config.leftSide.temperatureEntityId[0] != '\0';
  roomConfigured = openMeteo || config.rightSide.temperatureEntityId[0] != '\0';
  metricAConfigured = openMeteo || config.metricA.entityId[0] != '\0';
  metricBConfigured = openMeteo || config.metricB.entityId[0] != '\0';
  ensureWeatherAnimationDecoders();
  savedDayBrightness = constrain(config.dayBrightness, 1, 100);
  savedNightBrightness = constrain(config.nightBrightness, 1, 100);
  if (brightnessPreviewCallback != nullptr) {
    brightnessPreviewCallback(nightModeEnabled ? savedNightBrightness
                                               : savedDayBrightness);
  }
  renderSecondRing(millis());
  if (nightVisualChanged) {
    if (analogLayoutEnabled() && wasRedNight != redNightVisualEnabled()) {
      rebuildAnalogDialCache();
      if (analogDialLayer != nullptr) lv_obj_invalidate(analogDialLayer);
      invalidateAnalogHands();
    }
    applyDashboardColors();
  }
  lv_label_set_text(outsideTitleLabel, openMeteo
                                           ? config.openMeteoSlots[0].name
                                           : config.leftSide.name[0] == '\0'
                                           ? (englishLanguage() ? "ROOM" : "MÍSTNOST")
                                           : config.leftSide.name);
  alignCenter(outsideTitleLabel, -122, 5);
  setObjectVisible(outsideTitleLabel, outsideConfigured);
  setObjectVisible(outsideIntegerLabel, outsideConfigured);
  setObjectVisible(outsideDecimalLabel, outsideConfigured);
  setObjectVisible(outsideUnitLabel, outsideConfigured);
  lv_label_set_text(roomTitleLabel, openMeteo
                                        ? config.openMeteoSlots[1].name
                                        : config.rightSide.name[0] == '\0'
                                        ? (englishLanguage() ? "ROOM" : "MÍSTNOST")
                                        : config.rightSide.name);
  alignCenter(roomTitleLabel, 127, 5);
  setObjectVisible(roomTitleLabel, roomConfigured);
  setObjectVisible(roomIntegerLabel, roomConfigured);
  setObjectVisible(roomDecimalLabel, roomConfigured);
  setObjectVisible(roomUnitLabel, roomConfigured);
  lv_label_set_text(outsideUnitLabel, outsideUnit);
  lv_label_set_text(roomUnitLabel, roomUnit);
  const char *outsideIconGlyph = roomIconGlyph(openMeteo ? "weather" : config.leftSide.icon);
  lv_label_set_text(outsideIconLabel, outsideIconGlyph);
  if (!outsideConfigured || outsideIconGlyph[0] == '\0') {
    lv_obj_add_flag(outsideIconLabel, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(outsideIconLabel, LV_OBJ_FLAG_HIDDEN);
    alignCenter(outsideIconLabel, -142, 107);
  }
  const char *roomIconGlyphValue = roomIconGlyph(openMeteo ? "none" : config.rightSide.icon);
  lv_label_set_text(roomIconLabel, roomIconGlyphValue);
  if (!roomConfigured || roomIconGlyphValue[0] == '\0') {
    lv_obj_add_flag(roomIconLabel, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(roomIconLabel, LV_OBJ_FLAG_HIDDEN);
    alignCenter(roomIconLabel, 142, 107);
  }
  lv_label_set_text(co2TitleLabel, metricAConfig.name);
  lv_label_set_text(co2UnitLabel, metricAConfig.suffix);
  lv_label_set_text(humidityTitleLabel, metricBConfig.name);
  lv_label_set_text(humidityUnitLabel, metricBConfig.suffix);
  setObjectVisible(co2TitleLabel, metricAConfigured);
  setObjectVisible(co2ValueLabel, metricAConfigured);
  setObjectVisible(co2UnitLabel, metricAConfigured);
  setObjectVisible(humidityTitleLabel, metricBConfigured);
  setObjectVisible(humidityValueLabel, metricBConfigured);
  setObjectVisible(humidityUnitLabel, metricBConfigured);
  if (analogLayoutEnabled()) {
    lv_obj_clear_flag(analogDialLayer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(analogHandsLayer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(analogMetricDivider, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(analogOutsideTitleLabel,
                      lv_label_get_text(outsideTitleLabel));
    lv_label_set_text(analogRoomTitleLabel, lv_label_get_text(roomTitleLabel));
    lv_label_set_text(analogMetricATitleLabel, metricAConfig.name);
    lv_label_set_text(analogMetricBTitleLabel, metricBConfig.name);
    alignCenter(analogOutsideTitleLabel, -112, -8);
    alignCenter(analogRoomTitleLabel, 112, -8);
    lv_obj_update_layout(analogMetricATitleLabel);
    lv_obj_update_layout(analogMetricBTitleLabel);
    lv_obj_update_layout(analogMetricAValueLabel);
    lv_obj_update_layout(analogMetricBValueLabel);
    lv_obj_update_layout(analogMetricAUnitLabel);
    lv_obj_update_layout(analogMetricBUnitLabel);
    alignAnalogMetricLine(analogMetricATitleLabel, analogMetricAValueLabel,
                          analogMetricAUnitLabel, 98);
    alignAnalogMetricLine(analogMetricBTitleLabel, analogMetricBValueLabel,
                          analogMetricBUnitLabel, 140, 4);
    updateAnalogMetricDivider();

    setObjectVisible(analogOutsideTitleLabel, outsideConfigured);
    setObjectVisible(analogOutsideValueLabel, outsideConfigured);
    setObjectVisible(analogOutsideDecimalLabel, outsideConfigured);
    setObjectVisible(analogOutsideUnitLabel, outsideConfigured);
    setObjectVisible(analogRoomTitleLabel, roomConfigured);
    setObjectVisible(analogRoomValueLabel, roomConfigured);
    setObjectVisible(analogRoomDecimalLabel, roomConfigured);
    setObjectVisible(analogRoomUnitLabel, roomConfigured);
    setObjectVisible(analogMetricATitleLabel, metricAConfigured);
    setObjectVisible(analogMetricAValueLabel, metricAConfigured);
    setObjectVisible(analogMetricAUnitLabel, metricAConfigured);
    setObjectVisible(analogMetricBTitleLabel, metricBConfigured);
    setObjectVisible(analogMetricBValueLabel, metricBConfigured);
    setObjectVisible(analogMetricBUnitLabel, metricBConfigured);

    lv_obj_t *digitalOnly[] = {
        timeLabel,          outsideTitleLabel,   outsideIntegerLabel,
        outsideDecimalLabel, outsideUnitLabel,   roomTitleLabel,
        roomIntegerLabel,   roomDecimalLabel,    roomUnitLabel,
        outsideIconLabel,   co2TitleLabel,       co2ValueLabel,
        co2UnitLabel,       humidityTitleLabel,  humidityValueLabel,
        humidityUnitLabel,  roomWeatherImage,    weatherAnimation,
        roomWeatherAnimation, roomIconLabel, digitalAirArc,
        digitalAirStem, digitalAirLeftLeg, digitalAirRightLeg,
        digitalMetricDivider, digitalBottomDivider,
    };
    for (lv_obj_t *object : digitalOnly)
      lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(dateLabel, &clock_czech_20, 0);
    alignCenter(dateLabel, 0, -142);
    alignCenter(namedayLabel, 0, -123);
    alignCenter(weatherImage, 0, -70);
    alignCenter(weatherAnimation, 0, -70);
  } else {
    lv_obj_t *analogOnly[] = {
        analogDialLayer,          analogHandsLayer,
        analogOutsideTitleLabel, analogOutsideValueLabel,
        analogOutsideDecimalLabel, analogOutsideUnitLabel,
        analogRoomTitleLabel,    analogRoomValueLabel,
        analogRoomDecimalLabel,  analogRoomUnitLabel,
        analogMetricATitleLabel, analogMetricAValueLabel,
        analogMetricAUnitLabel,  analogMetricBTitleLabel,
        analogMetricBValueLabel, analogMetricBUnitLabel,
        analogMetricDivider,
    };
    for (lv_obj_t *object : analogOnly)
      lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(timeLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(digitalAirArc, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(digitalAirStem, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(digitalAirLeftLeg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(digitalAirRightLeg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(digitalMetricDivider, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(digitalBottomDivider, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(dateLabel, &clock_czech_18, 0);
    lv_obj_set_style_text_letter_space(dateLabel, 4, 0);
    alignCenter(dateLabel, 0, -43);
    alignCenter(namedayLabel, 0, -20);
    alignCenter(weatherImage, -142, 107);
    alignCenter(weatherAnimation, -142, 107);
    alignCenter(roomWeatherImage, 142, 107);
    alignCenter(roomWeatherAnimation, 142, 107);
  }
  lv_obj_set_pos(dashboardContent, 0, analogLayoutEnabled() ? 0 : -10);
  alignConnectionStatusIcons();
  renderSecondRing(millis());
  clockDashboardUpdate(currentValues);
  // Radar mohl přibýt nebo zmizet a stavový řádek se mohl vypnout.
  updateScreenDots();
}

void clockDashboardApplyAppearance(const ClockAppearanceConfig &appearance) {
  const uint8_t style = constrain(
      appearance.style, static_cast<uint8_t>(CLOCK_STYLE_DIGITAL),
      static_cast<uint8_t>(CLOCK_STYLE_VALUES));
  const uint32_t tone = appearance.analogToneColor & 0xFFFFFF;
  const uint32_t handTone = appearance.analogHandToneColor & 0xFFFFFF;
  const uint32_t accentColor =
      appearance.analogCardinalAccentColor & 0xFFFFFF;
  const bool accentsEnabled = appearance.analogCardinalAccentsEnabled;
  const bool outlineHandsEnabled = appearance.analogOutlineHandsEnabled;
  const bool monochromeValuesEnabled =
      appearance.analogMonochromeValuesEnabled;
  const bool valuesAboveHandsEnabled =
      appearance.analogValuesAboveHandsEnabled;
  const uint32_t appearanceDateColor =
      appearance.analogDateColor & 0xFFFFFF;
  const uint32_t weatherColor =
      appearance.monochromeWeatherIconColor & 0xFFFFFF;
  if (activeClockStyle == style && analogToneColor == tone &&
      analogHandToneColor == handTone &&
      analogCardinalAccentColor == accentColor &&
      analogCardinalAccentsEnabled == accentsEnabled &&
      analogOutlineHandsEnabled == outlineHandsEnabled &&
      analogMonochromeValuesEnabled == monochromeValuesEnabled &&
      analogValuesAboveHandsEnabled == valuesAboveHandsEnabled &&
      analogDateColor == appearanceDateColor &&
      monochromeWeatherIconColor == weatherColor &&
      dashboardContent != nullptr) {
    return;
  }
  const bool styleChanged = activeClockStyle != style;
  const bool valueLayerChanged =
      analogValuesAboveHandsEnabled != valuesAboveHandsEnabled;
  const bool dialAppearanceChanged = analogToneColor != tone ||
                                     analogCardinalAccentColor != accentColor ||
                                     analogCardinalAccentsEnabled !=
                                         accentsEnabled;
  activeClockStyle = style;
  analogToneColor = tone;
  analogHandToneColor = handTone;
  analogCardinalAccentColor = accentColor;
  analogCardinalAccentsEnabled = accentsEnabled;
  analogOutlineHandsEnabled = outlineHandsEnabled;
  analogMonochromeValuesEnabled = monochromeValuesEnabled;
  analogValuesAboveHandsEnabled = valuesAboveHandsEnabled;
  analogDateColor = appearanceDateColor;
  monochromeWeatherIconColor = weatherColor;
  if (dashboardContent == nullptr || !dashboardRuntimeConfigAvailable) return;
  // Přepnutí stylu musí odkrýt právě jednu stránku; radar ani nastavení
  // přitom nesmí zmizet, proto se sahá jen na dvojici ciferníků.
  if (styleChanged && valuesPage != nullptr &&
      activeScreen == DASHBOARD_SCREEN_CLOCK && !settingsVisible &&
      !firmwareUpdateActive) {
    lv_obj_t *hidden =
        valuesLayoutEnabled() ? dashboardContent : valuesPage;
    lv_obj_add_flag(hidden, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(primaryClockPage(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(primaryClockPage());
  }
  clockDashboardApplyConfiguration(dashboardRuntimeConfig);
  updateValuesPage();
  updateAnalogValueLayerOrder();
  if (analogLayoutEnabled() && (styleChanged || dialAppearanceChanged))
    rebuildAnalogDialCache();
  applyDashboardColors();
  displayDriverSetPartialRefresh(
      analogLayoutEnabled(), analogLayoutEnabled() && valueLayerChanged);
  if (analogDialLayer != nullptr && analogLayoutEnabled())
    lv_obj_invalidate(analogDialLayer);
  invalidateAnalogHands();
}

void clockDashboardUpdate(const ClockValues &values) {
  char text[32];
  currentValues = values;
  if (firmwareUpdateActive) return;
  updateOverlayStatusLabels();
  updateValuesPage();

  const lv_img_dsc_t *weatherIcon =
      openWeatherIconForCode(values.weatherCode, values.weatherIsDay);
  if (analogLayoutEnabled()) {
    lv_obj_add_flag(roomWeatherAnimation, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(roomWeatherImage, LV_OBJ_FLAG_HIDDEN);
    char desiredAnimationKey[48] = "";
    const uint8_t effectiveWeatherIconStyle =
        clockDashboardWeatherIconStyle(configuredWeatherIconStyle);
    const bool hasDesiredAnimation = weatherAnimationAssetKey(
        desiredAnimationKey, sizeof(desiredAnimationKey), values.weatherCode,
        values.weatherIsDay, effectiveWeatherIconStyle);
    const bool useAnimation =
        animatedWeatherIconsEnabled && weatherAnimationAvailable &&
        !weatherAnimationRevealPending && hasDesiredAnimation &&
        strcmp(desiredAnimationKey, weatherAnimationKey) == 0;
    if (!weatherConfigured || weatherIcon == nullptr || useAnimation) {
      lv_obj_add_flag(weatherImage, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_img_set_src(weatherImage, weatherIcon);
      lv_img_set_zoom(weatherImage, 256);
      alignCenter(weatherImage, 0, -70);
      lv_obj_clear_flag(weatherImage, LV_OBJ_FLAG_HIDDEN);
    }
    if (weatherConfigured && useAnimation) {
      alignCenter(weatherAnimation, 0, -70);
      lv_obj_clear_flag(weatherAnimation, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(weatherAnimation, LV_OBJ_FLAG_HIDDEN);
    }

    setAnalogTemperature(analogOutsideValueLabel, analogOutsideDecimalLabel,
                         analogOutsideUnitLabel, values.leftTemperatureC,
                         outsideDecimals, outsideUnit, -112, 28);
    setAnalogTemperature(analogRoomValueLabel, analogRoomDecimalLabel,
                         analogRoomUnitLabel, values.rightTemperatureC,
                         roomDecimals, roomUnit, 112, 28);
    const bool metricAChanged = setAnalogValue(
        analogMetricAValueLabel, analogMetricAUnitLabel, values.metricAValue,
        metricAConfig.decimals, metricAConfig.suffix, 28, 98);
    const bool metricBChanged = setAnalogValue(
        analogMetricBValueLabel, analogMetricBUnitLabel, values.metricBValue,
        metricBConfig.decimals, metricBConfig.suffix, 28, 140);
    if (metricAChanged)
      alignAnalogMetricLine(analogMetricATitleLabel, analogMetricAValueLabel,
                            analogMetricAUnitLabel, 98);
    if (metricBChanged)
      alignAnalogMetricLine(analogMetricBTitleLabel, analogMetricBValueLabel,
                            analogMetricBUnitLabel, 140, 4);
    if (metricAChanged || metricBChanged) updateAnalogMetricDivider();
    applyAnalogColors();
    applyConnectionStatusColors();
    if (automaticDayNightEnabled && currentValues.sunStateAvailable) {
      const bool lightForcesDay = currentValues.dayNightLightStateAvailable &&
                                  currentValues.dayNightLightOn;
      clockDashboardSetNightMode(!currentValues.weatherIsDay &&
                                 !lightForcesDay);
    }
    return;
  }
  char desiredAnimationKey[48] = "";
  const uint8_t effectiveWeatherIconStyle =
      clockDashboardWeatherIconStyle(configuredWeatherIconStyle);
  const bool hasDesiredAnimation = weatherAnimationAssetKey(
      desiredAnimationKey, sizeof(desiredAnimationKey), values.weatherCode,
      values.weatherIsDay, effectiveWeatherIconStyle);
  const bool useAnimation = animatedWeatherIconsEnabled &&
                            weatherAnimationAvailable &&
                            !weatherAnimationRevealPending &&
                            hasDesiredAnimation &&
                            strcmp(desiredAnimationKey, weatherAnimationKey) == 0;
  if (!outsideConfigured || !weatherConfigured || !outsideUsesWeatherIcon ||
      weatherIcon == nullptr || useAnimation) {
    lv_obj_add_flag(weatherImage, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_img_set_src(weatherImage, weatherIcon);
    lv_obj_clear_flag(weatherImage, LV_OBJ_FLAG_HIDDEN);
  }

  if (!roomConfigured || !weatherConfigured || !roomUsesWeatherIcon ||
      weatherIcon == nullptr || useAnimation) {
    lv_obj_add_flag(roomWeatherImage, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_img_set_src(roomWeatherImage, weatherIcon);
    lv_obj_clear_flag(roomWeatherImage, LV_OBJ_FLAG_HIDDEN);
  }

  if (outsideConfigured && weatherConfigured && outsideUsesWeatherIcon &&
      useAnimation) {
    lv_obj_clear_flag(weatherAnimation, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(weatherAnimation, LV_OBJ_FLAG_HIDDEN);
  }
  if (roomConfigured && weatherConfigured && roomUsesWeatherIcon &&
      useAnimation) {
    lv_obj_clear_flag(roomWeatherAnimation, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(roomWeatherAnimation, LV_OBJ_FLAG_HIDDEN);
  }

  setTopValue(values.leftTemperatureC, outsideDecimals, 118,
              outsideIntegerLabel, outsideDecimalLabel, outsideUnitLabel);
  setTopValue(values.rightTemperatureC, roomDecimals, 367, roomIntegerLabel,
              roomDecimalLabel, roomUnitLabel);
  const lv_color_t outsideValueColor =
      redNightVisualEnabled()
          ? COLOR_ERROR
          : metricColorForValue(values.leftTemperatureC,
                                leftValueColorScale);
  const lv_color_t roomValueColor =
      redNightVisualEnabled()
          ? COLOR_ERROR
          : metricColorForValue(values.rightTemperatureC,
                                rightValueColorScale);
  lv_obj_t *outsideValueLabels[] = {outsideTitleLabel, outsideIntegerLabel,
                                    outsideDecimalLabel, outsideUnitLabel};
  lv_obj_t *roomValueLabels[] = {roomTitleLabel, roomIntegerLabel,
                                 roomDecimalLabel, roomUnitLabel};
  for (lv_obj_t *label : outsideValueLabels)
    setTextColor(label, outsideValueColor);
  for (lv_obj_t *label : roomValueLabels) setTextColor(label, roomValueColor);

  formatMetricValue(text, sizeof(text), values.metricAValue,
                    metricAConfig.decimals);
  lv_label_set_text(co2ValueLabel, text);
  alignCo2Value();
  const lv_color_t co2Color =
      metricColorForValue(values.metricAValue, metricAColorScale);
  lv_obj_set_style_text_color(co2TitleLabel, co2Color, 0);
  lv_obj_set_style_text_color(co2ValueLabel, co2Color, 0);
  lv_obj_set_style_text_color(co2UnitLabel, co2Color, 0);

  formatMetricValue(text, sizeof(text), values.metricBValue,
                    metricBConfig.decimals);
  lv_label_set_text(humidityValueLabel, text);
  alignMetricBValue();
  const lv_color_t metricBValueColor =
      metricColorForValue(values.metricBValue, metricBColorScale);
  lv_obj_set_style_text_color(humidityTitleLabel, metricBValueColor, 0);
  lv_obj_set_style_text_color(humidityValueLabel, metricBValueColor, 0);
  lv_obj_set_style_text_color(humidityUnitLabel, metricBValueColor, 0);

  const lv_color_t statusColor =
      values.homeAssistantOnline ? COLOR_AIR : COLOR_ERROR;
  lv_obj_set_style_text_color(statusLabel, statusColor, 0);
  if (automaticDayNightEnabled && currentValues.sunStateAvailable) {
    const bool lightForcesDay = currentValues.dayNightLightStateAvailable &&
                                currentValues.dayNightLightOn;
    clockDashboardSetNightMode(!currentValues.weatherIsDay && !lightForcesDay);
  } else {
    applyDashboardColors();
  }
}

void clockDashboardSetWeatherAnimation(const uint8_t *gifData, size_t size,
                                       const char *iconKey) {
  if (gifData == nullptr || size == 0 || iconKey == nullptr) return;
  weatherAnimationRevealPending = true;
  weatherAnimationRevealAt = millis() + WEATHER_ANIMATION_REVEAL_DELAY_MS;
  lv_obj_add_flag(weatherAnimation, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(roomWeatherAnimation, LV_OBJ_FLAG_HIDDEN);
  weatherAnimationSource = {};
  weatherAnimationSource.header.always_zero = 0;
  weatherAnimationSource.header.w = 84;
  weatherAnimationSource.header.h = 84;
  weatherAnimationSource.header.cf = LV_IMG_CF_RAW;
  weatherAnimationSource.data_size = size;
  weatherAnimationSource.data = gifData;
  weatherAnimationAvailable = true;
  strlcpy(weatherAnimationKey, iconKey, sizeof(weatherAnimationKey));
  ensureWeatherAnimationDecoders();
  applyDashboardColors();
  clockDashboardUpdate(currentValues);
}

void clockDashboardLoop() {
  if (firmwareUpdateActive) return;
  const unsigned long now = millis();
  if (settingsVisible && settingsPageIndex == SETTINGS_PAGE_COUNT - 1 &&
      now - lastSettingsInfoRefreshAt >= 500) {
    lastSettingsInfoRefreshAt = now;
    char info[64];
    snprintf(info, sizeof(info),
             englishLanguage() ? "CPU: %u MHz   MEMORY: %lu kB"
                               : "CPU: %u MHz   PAMĚŤ: %lu kB",
             static_cast<unsigned>(getCpuFrequencyMhz()),
             static_cast<unsigned long>(ESP.getFreeHeap() / 1024));
    if (strcmp(displayedDeviceInfo, info) != 0) {
      strlcpy(displayedDeviceInfo, info, sizeof(displayedDeviceInfo));
      lv_label_set_text(deviceInfoLabel, displayedDeviceInfo);
      alignCenter(deviceInfoLabel, 0, -64);
    }
    const FirmwareUpdateSnapshot snapshot = firmwareUpdateServiceSnapshot();
    char statusText[160] = "";
    switch (snapshot.state) {
      case FirmwareUpdateState::Checking:
        strlcpy(statusText,
                englishLanguage() ? "CHECKING FOR UPDATE"
                                  : "KONTROLUJI AKTUALIZACI",
                sizeof(statusText));
        break;
      case FirmwareUpdateState::Available:
        snprintf(statusText, sizeof(statusText),
                 englishLanguage() ? "NEW VERSION  %s" : "NOVÁ VERZE  %s",
                 snapshot.serverVersion);
        break;
      case FirmwareUpdateState::Current:
        strlcpy(statusText,
                englishLanguage() ? "FIRMWARE IS UP TO DATE"
                                  : "FIRMWARE JE AKTUÁLNÍ",
                sizeof(statusText));
        break;
      case FirmwareUpdateState::Downloading:
        strlcpy(statusText,
                englishLanguage() ? "DOWNLOADING UPDATE"
                                  : "STAHUJI AKTUALIZACI",
                sizeof(statusText));
        break;
      case FirmwareUpdateState::Failed:
        strlcpy(statusText,
                englishLanguage() ? "UPDATE CHECK FAILED"
                                  : "KONTROLA SELHALA",
                sizeof(statusText));
        break;
      case FirmwareUpdateState::Restarting:
        strlcpy(statusText,
                englishLanguage() ? "RESTARTING DEVICE"
                                  : "RESTARTUJI ZAŘÍZENÍ",
                sizeof(statusText));
        break;
      default:
        strlcpy(statusText,
                snapshot.installationSupported
                    ? (englishLanguage() ? "UPDATE NOT CHECKED"
                                         : "AKTUALIZACE NEZKONTROLOVÁNA")
                    : (englishLanguage() ? "OTA IN RELEASE ONLY"
                                         : "OTA JEN V RELEASE"),
                sizeof(statusText));
        break;
    }
    if (strcmp(displayedFirmwareStatus, statusText) != 0) {
      strlcpy(displayedFirmwareStatus, statusText,
              sizeof(displayedFirmwareStatus));
      lv_label_set_text(firmwareStatusLabel, displayedFirmwareStatus);
      alignCenter(firmwareStatusLabel, 0, 123);
    }
    const bool canInstall = snapshot.updateAvailable &&
                            snapshot.installationSupported && !snapshot.busy;
    if (displayedCanInstall != canInstall) {
      displayedCanInstall = canInstall;
      setObjectVisible(firmwareInstallButton, canInstall);
      setObjectVisible(firmwareCheckButton, !canInstall);
    }
  }
  if (weatherAnimationRevealPending &&
      static_cast<long>(now - weatherAnimationRevealAt) >= 0) {
    weatherAnimationRevealPending = false;
    clockDashboardUpdate(currentValues);
  }
  const bool smoothSecondEffectActive =
      !analogLayoutEnabled() && secondRingEnabled &&
      (secondEffect == CLOCK_SECOND_EFFECT_LINE ||
       secondEffect == CLOCK_SECOND_EFFECT_COMET);
  const bool smoothTimeColonActive =
      !analogLayoutEnabled() &&
      timeColonEffect == CLOCK_TIME_COLON_FADE;
  if (!secondFadeActive && !smoothSecondEffectActive &&
      !smoothTimeColonActive)
    return;
  const unsigned long frameInterval =
      SMOOTH_EFFECT_FRAME_MS;
  if (now - lastSecondFadeFrameAt < frameInterval) return;
  lastSecondFadeFrameAt = now;
  if (secondFadeActive || smoothSecondEffectActive) renderSecondRing(now);
  if (smoothTimeColonActive) renderTimeColon(now);
}

void clockDashboardShowSettings() {
  if (firmwareUpdateActive) return;
  showSettings();
}

void clockDashboardShowSettingsPage(uint8_t page) {
  if (firmwareUpdateActive) return;
  showSettings();
  showSettingsSubpage(page);
}

bool clockDashboardSettingsVisible() { return settingsVisible; }

// Podržení prstu opouští nastavení stejnou cestou jako tlačítko Zrušit: co
// nebylo uloženo, se zahodí.
void clockDashboardCloseSettings() {
  if (!settingsVisible) return;
  closeSettings(false);
  // Zavření gestem nekončí klepnutím, není tedy co potlačovat. Bez tohoto
  // řádku by potlačení spolklo první opravdové klepnutí po návratu.
  suppressNextDashboardClick = false;
}

// Ruční přepnutí obrazovky projde i přes otevřené nastavení; zastaví ho jen
// probíhající aktualizace firmwaru, do které se vstupovat nesmí.
bool clockDashboardManualScreenChangeAllowed() { return !firmwareUpdateActive; }

void clockDashboardSetNightMode(bool enabled) {
  const bool wasRedNight = redNightVisualEnabled();
  const bool modeChanged = nightModeEnabled != enabled;
  nightModeEnabled = enabled;
  if (firmwareUpdateActive) return;
  if (analogLayoutEnabled() && wasRedNight != redNightVisualEnabled()) {
    rebuildAnalogDialCache();
    if (analogDialLayer != nullptr) lv_obj_invalidate(analogDialLayer);
    invalidateAnalogHands();
  }
  applyDashboardColors();
  if (modeChanged) clockDashboardUpdate(currentValues);
  if (brightnessPreviewCallback != nullptr) {
    brightnessPreviewCallback(nightModeEnabled ? savedNightBrightness
                                               : savedDayBrightness);
  }
}

bool clockDashboardNightModeEnabled() { return nightModeEnabled; }

namespace {

// Klepnutí, kterým se zavíralo nastavení, se nesmí propsat do stránky pod ním.
// Vrací true, když se má právě zpracovávané klepnutí zahodit.
bool consumeSuppressedTap() {
  if (!suppressNextDashboardClick) return false;
  suppressNextDashboardClick = false;
  return static_cast<long>(millis() - suppressDashboardClickUntil) < 0;
}

}  // namespace

void clockDashboardHandleDoubleTap() {
  if (settingsVisible || firmwareUpdateActive) return;
  if (consumeSuppressedTap()) return;
  if (automaticDayNightEnabled) return;
  clockDashboardSetNightMode(!nightModeEnabled);
}

bool clockDashboardRadarVisible() {
  return activeScreen == DASHBOARD_SCREEN_RADAR;
}

bool clockDashboardRssVisible() {
  return activeScreen == DASHBOARD_SCREEN_RSS;
}

void clockDashboardSetRssVisible(bool visible) {
  setActiveScreen(visible ? DASHBOARD_SCREEN_RSS : DASHBOARD_SCREEN_CLOCK);
}

void clockDashboardSetRssAvailable(bool available) {
  if (rssFeatureAvailable == available) return;
  rssFeatureAvailable = available;
  if (!available && activeScreen == DASHBOARD_SCREEN_RSS) {
    activeScreen = DASHBOARD_SCREEN_CLOCK;
    if (rssPage != nullptr) lv_obj_add_flag(rssPage, LV_OBJ_FLAG_HIDDEN);
    if (!settingsVisible && !firmwareUpdateActive) {
      lv_obj_clear_flag(primaryClockPage(), LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(primaryClockPage());
    }
  }
  // O jednu obrazovku v rotaci míň nebo víc: ukazatel musí ubrat či přidat
  // tečku, jinak by sliboval obrazovku, na kterou se nedá přepnout.
  updateScreenDots();
}

bool clockDashboardForecastVisible() {
  return activeScreen == DASHBOARD_SCREEN_FORECAST;
}

bool clockDashboardPlanesVisible() {
  return activeScreen == DASHBOARD_SCREEN_PLANES;
}

void clockDashboardSetPlanesVisible(bool visible) {
  setActiveScreen(visible ? DASHBOARD_SCREEN_PLANES : DASHBOARD_SCREEN_CLOCK);
}

void clockDashboardSetPlanesVisibilityCallback(
    RssVisibilityCallback visibility) {
  planesVisibilityCallback = visibility;
}

void clockDashboardSetPlanesAvailable(bool available) {
  // Stránka se zakládá až s prvním zapnutím obrazovky, takže dokud ji majitel
  // nechce, nestojí ani jeden objekt LVGL.
  if (available && planesPage == nullptr && dashboardScreen != nullptr) {
    createPlanesPage(dashboardScreen);
  }
  if (planesFeatureAvailable == available) return;
  planesFeatureAvailable = available;
  if (!available && activeScreen == DASHBOARD_SCREEN_PLANES) {
    activeScreen = DASHBOARD_SCREEN_CLOCK;
    if (planesPage != nullptr)
      lv_obj_add_flag(planesPage, LV_OBJ_FLAG_HIDDEN);
    if (!settingsVisible && !firmwareUpdateActive) {
      lv_obj_clear_flag(primaryClockPage(), LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(primaryClockPage());
    }
    if (planesVisibilityCallback != nullptr) planesVisibilityCallback(false);
  }
  // O jednu obrazovku v rotaci míň nebo víc: ukazatel musí ubrat či přidat
  // tečku, jinak by sliboval obrazovku, na kterou se nedá přepnout.
  updateScreenDots();
}

void clockDashboardHandleSingleTap(int16_t x, int16_t y) {
  // Nad otevřeným nastavením a během aktualizace firmwaru zůstává activeScreen
  // na letadlech, i když je vidět jiná stránka. Klepnutí na ovladač v nastavení
  // by tedy vybíralo letadlo a rozjelo dotaz na jeho trasu.
  if (settingsVisible || firmwareUpdateActive) return;
  // Zahodit se musí i klepnutí mimo letadla: jinak by potlačení čekalo dál a
  // spolklo nejbližší dvojklepnutí, kterým se přepíná denní režim.
  if (consumeSuppressedTap()) return;
  if (activeScreen != DASHBOARD_SCREEN_PLANES) return;
  planeRadarServiceHandleTap(x, y);
}

namespace {

// Nouzový squawk přebírá řádek s počtem letadel. Řádek je jen jeden, takže se
// obojí nemůže překrýt - nouze prostě počet přebije.
const char *planesEmergencyText(const char *code) {
  const bool english = englishLanguage();
  if (strcmp(code, ADSB_SQUAWK_HIJACK) == 0)
    return english ? "HIJACK" : "ÚNOS";
  if (strcmp(code, ADSB_SQUAWK_RADIO) == 0)
    return english ? "RADIO FAILURE" : "PORUCHA RÁDIA";
  return english ? "EMERGENCY" : "NOUZE";
}

void updatePlanesDetail(const PlaneRadarDetail &detail) {
  if (planesDetailPanel == nullptr) return;
  if (!detail.open) {
    lv_obj_add_flag(planesDetailPanel, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  const bool english = englishLanguage();
  const bool metric = dashboardRuntimeConfig.planes.metricUnits;

  // Nadpis: callsign, nebo ICAO adresa, když ho letadlo nevysílá.
  lv_label_set_text(planesDetailTitle,
                    detail.callsign[0] != '\0'
                        ? detail.callsign
                        : (detail.hex[0] != '\0' ? detail.hex : "?"));

  char line[48];
  uint8_t row = 0;
  if (metric) {
    snprintf(line, sizeof(line), "%s: %d m", english ? "ALTITUDE" : "VÝŠKA",
             static_cast<int>(std::lround(detail.altitudeFt * 0.3048f)));
  } else {
    snprintf(line, sizeof(line), "%s: %d ft", english ? "ALTITUDE" : "VÝŠKA",
             static_cast<int>(std::lround(detail.altitudeFt)));
  }
  lv_label_set_text(planesDetailRows[row++], line);

  if (metric) {
    snprintf(line, sizeof(line), "%s: %d km/h", english ? "SPEED" : "RYCHLOST",
             static_cast<int>(std::lround(detail.groundSpeedKt * 1.852f)));
  } else {
    snprintf(line, sizeof(line), "%s: %d kt", english ? "SPEED" : "RYCHLOST",
             static_cast<int>(std::lround(detail.groundSpeedKt)));
  }
  lv_label_set_text(planesDetailRows[row++], line);

  if (detail.hasTrack) {
    snprintf(line, sizeof(line), "%s: %d°", english ? "TRACK" : "SMĚR",
             static_cast<int>(std::lround(detail.trackDeg)));
  } else {
    snprintf(line, sizeof(line), "%s: %s", english ? "TRACK" : "SMĚR",
             english ? "unknown" : "neznámý");
  }
  lv_label_set_text(planesDetailRows[row++], line);

  // Stoupání a klesání se pozná ze znaménka. Šipka by byla hezčí, jenže písmo
  // clock_czech_16 je česká podmnožina a trojúhelníkové šipky v něm nejsou -
  // LVGL by nenakreslilo nic a řádek by končil prázdnou mezerou.
  if (metric) {
    snprintf(line, sizeof(line), "%s: %+.1f m/s",
             english ? "CLIMB" : "STOUPÁNÍ",
             static_cast<double>(detail.verticalRateFtMin * 0.00508f));
  } else {
    snprintf(line, sizeof(line), "%s: %+d ft/min",
             english ? "CLIMB" : "STOUPÁNÍ",
             static_cast<int>(std::lround(detail.verticalRateFtMin)));
  }
  lv_label_set_text(planesDetailRows[row++], line);

  // Typ a registrace na jednom řádku. Obojí veze táž odpověď jako polohu, tedy
  // zadarmo; kterékoli z nich může chybět.
  if (detail.type[0] != '\0' || detail.registration[0] != '\0') {
    snprintf(line, sizeof(line), "%s: %s%s%s", english ? "TYPE" : "TYP",
             detail.type[0] != '\0' ? detail.type : "?",
             detail.registration[0] != '\0' ? "  " : "",
             detail.registration);
  } else {
    line[0] = '\0';
  }
  lv_label_set_text(planesDetailRows[row++], line);

  // Typ vypsaný slovy pod zkratkou. "A21N" pozná málokdo, "AIRBUS A-321neo"
  // skoro každý. Server jméno pošle jen u letadel ze své databáze; když ho
  // nemá, zůstane řádek prázdný a nad ním stojí samotná zkratka.
  lv_label_set_text(planesDetailTypeName, detail.description);

  // Nouzový stav má vlastní řádek, ne náhradní místo po typu: letadlo, které
  // hlásí typ nebo registraci, by o něm jinak neřeklo vůbec nic.
  if (detail.emergency[0] != '\0') {
    snprintf(line, sizeof(line), "#FF4848 %s  %s#", detail.emergency,
             planesEmergencyText(detail.emergency));
  } else {
    line[0] = '\0';
  }
  lv_label_set_text(planesDetailRows[row++], line);

  // Trasa. Spousta letů žádnou nemá - všeobecné letectví, vojenské stroje,
  // vrtulníky - a to je normální stav, ne chyba. Dřív po nich zůstalo prázdné
  // místo, nerozeznatelné od hledání, které ještě běží; teď se to napíše.
  switch (detail.routeState) {
    case PlaneRouteState::Pending:
      lv_label_set_text(planesDetailRouteFrom,
                        english ? "Looking up route..." : "Zjišťuji trasu...");
      lv_label_set_text(planesDetailRouteTo, "");
      break;
    case PlaneRouteState::Known:
      snprintf(line, sizeof(line), "%s: %s", english ? "From" : "Z",
               detail.route.from[0] != '\0' ? detail.route.from : "?");
      lv_label_set_text(planesDetailRouteFrom, line);
      snprintf(line, sizeof(line), "%s: %s", english ? "To" : "Do",
               detail.route.to[0] != '\0' ? detail.route.to : "?");
      lv_label_set_text(planesDetailRouteTo, line);
      break;
    case PlaneRouteState::Unknown:
      lv_label_set_text(planesDetailRouteFrom,
                        english ? "Route unknown" : "Trasa neznámá");
      lv_label_set_text(planesDetailRouteTo, "");
      break;
  }

  // Dokud letadlo v datech chybí, čísla nejsou živá; přiznat to je poctivější
  // než nechat zmrzlé hodnoty vypadat jako aktuální.
  if (detail.signalLost) {
    lv_label_set_text(planesDetailSignalLost,
                      english ? "signal lost" : "signál ztracen");
    lv_obj_clear_flag(planesDetailSignalLost, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(planesDetailSignalLost, LV_OBJ_FLAG_HIDDEN);
  }

  lv_obj_clear_flag(planesDetailPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(planesDetailPanel);
}

}  // namespace

void clockDashboardSetPlanesSnapshot(const uint16_t *pixels, uint8_t shownCount,
                                     bool watchedVisible,
                                     const char *emergency, uint16_t rangeKm,
                                     const char *message, bool loading,
                                     const PlaneRadarDetail &detail) {
  if (planesCanvas == nullptr || planesStatusLabel == nullptr ||
      planesRangeLabel == nullptr) {
    return;
  }
  const bool english = englishLanguage();

  if (pixels != nullptr) {
    lv_canvas_set_buffer(planesCanvas, const_cast<uint16_t *>(pixels),
                         PLANE_RADAR_WIDTH, PLANE_RADAR_HEIGHT,
                         LV_IMG_CF_TRUE_COLOR);
    lv_obj_clear_flag(planesCanvas, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(planesCanvas);
  } else {
    lv_obj_add_flag(planesCanvas, LV_OBJ_FLAG_HIDDEN);
  }

  char text[64];
  if (emergency != nullptr && emergency[0] != '\0') {
    // Nouze přebíjí počet letadel; červeně, aby to nešlo přehlédnout.
    snprintf(text, sizeof(text), "#FF4848 %s  %s#", emergency,
             planesEmergencyText(emergency));
  } else if (message != nullptr && message[0] != '\0') {
    snprintf(text, sizeof(text), "#FFB843 %s#", message);
  } else if (pixels == nullptr) {
    snprintf(text, sizeof(text), "#B5B5B5 %s#",
             loading ? (english ? "Loading aircraft..." : "Načítám letadla...")
                     : (english ? "Waiting for data" : "Čekám na data"));
  } else {
    const char *label = english ? "AIRCRAFT" : "LETADLA";
    snprintf(text, sizeof(text), "%s%s: %u%s",
             watchedVisible ? "#65C744 " : "", label,
             static_cast<unsigned>(shownCount),
             watchedVisible ? " *#" : "");
  }
  lv_label_set_text(planesStatusLabel, text);
  alignCenter(planesStatusLabel, 0, PLANES_STATUS_OFFSET_Y);

  snprintf(text, sizeof(text), "%u km", static_cast<unsigned>(rangeKm));
  lv_label_set_text(planesRangeLabel, text);
  alignCenter(planesRangeLabel, 0, PLANES_RANGE_OFFSET_Y);

  updatePlanesClockLabel();
  updatePlanesDetail(detail);
}

void clockDashboardSetForecastVisible(bool visible) {
  setActiveScreen(visible ? DASHBOARD_SCREEN_FORECAST
                          : DASHBOARD_SCREEN_CLOCK);
}

uint8_t clockDashboardForecastHourCapacity(
    const ClockForecastConfig &forecast) {
  return weatherForecastHourCapacity(forecast.airQuality,
                                     forecast.dayCount);
}

void clockDashboardSetForecastAvailable(bool available) {
  if (available && forecastPage == nullptr && dashboardScreen != nullptr) {
    createForecastPage(dashboardScreen);
    const ClockForecastConfig &forecast = dashboardRuntimeConfig.forecast;
    forecastAirQualityEnabled = forecast.airQuality;
    rebuildForecastRows(
        weatherForecastHourCapacity(forecast.airQuality, forecast.dayCount),
        forecast.dayCount);
  }
  if (forecastFeatureAvailable == available) return;
  forecastFeatureAvailable = available;
  if (!available) {
    // Služba mezipaměť zahodí také, takže po opětovném zapnutí nesmí na
    // obrazovce bliknout předpověď z minulé konfigurace.
    forecastDisplayedAvailable = false;
    forecastFetchFailed = false;
    updateForecastPage();
  }
  if (!available && activeScreen == DASHBOARD_SCREEN_FORECAST) {
    activeScreen = DASHBOARD_SCREEN_CLOCK;
    if (forecastPage != nullptr)
      lv_obj_add_flag(forecastPage, LV_OBJ_FLAG_HIDDEN);
    if (!settingsVisible && !firmwareUpdateActive) {
      lv_obj_clear_flag(primaryClockPage(), LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(primaryClockPage());
    }
    if (forecastVisibilityCallback != nullptr)
      forecastVisibilityCallback(false);
  }
  // O jednu obrazovku v rotaci míň nebo víc: ukazatel musí ubrat či přidat
  // tečku, jinak by sliboval obrazovku, na kterou se nedá přepnout.
  updateScreenDots();
}

void clockDashboardSetForecast(const WeatherForecastData &forecast) {
  forecastDisplayed = forecast;
  forecastDisplayedAvailable = true;
  forecastFetchFailed = false;
  updateForecastPage();
}

void clockDashboardSetForecastFailed(bool failed) {
  forecastFetchFailed = failed;
  // Hláška se ukáže jen tam, kde ještě není co kreslit; stará předpověď na
  // obrazovce je pořád lepší než oznámení, že se ji nepovedlo obnovit.
  if (!forecastDisplayedAvailable) updateForecastPage();
}

void clockDashboardSetRssStatus(const char *channelTitle, const char *message,
                                uint8_t count) {
  if (rssPage == nullptr) return;
  lv_label_set_text(rssHeaderLabel,
                    channelTitle != nullptr ? channelTitle : "");
  if (count != rssVisibleItemCount) layoutRssItems(count);
  const bool showMessage = count == 0;
  setObjectVisible(rssStatusLabel, showMessage);
  if (showMessage) {
    const char *text = message != nullptr && message[0] != '\0'
                           ? message
                           : (englishLanguage() ? "Loading news..."
                                                : "Načítám zprávy...");
    lv_label_set_text(rssStatusLabel, text);
  }
  applyRssColors();
}

void clockDashboardSetRssItem(size_t index, const char *title,
                              const char *time) {
  if (rssPage == nullptr || index >= rssVisibleItemCount) return;
  lv_obj_t *titleLabel = rssTitleLabels[index];
  if (titleLabel == nullptr) return;
  const bool hasTime = time != nullptr && time[0] != '\0';
  String text;
  // Arduino String roste na přesnou délku, takže bez rezervace by každý
  // připsaný znak znamenal realloc. Zdvojené '#' se do odhadu vejdou.
  text.reserve(2 * (title != nullptr ? strlen(title) : 0) +
               2 * (hasTime ? strlen(time) : 0) + RSS_COLOR_TAG_LENGTH +
               sizeof(RSS_TIME_SEPARATOR) + 2);
  if (hasTime) {
    char tag[RSS_COLOR_TAG_LENGTH + 1];
    rssBuildColorTag(tag, rssTimeColor());
    text += tag;
    rssAppendEscaped(text, time);
    text += '#';
    text += RSS_TIME_SEPARATOR;
  }
  rssAppendEscaped(text, title != nullptr ? title : "");
  rssItemHasTime[index] = hasTime;
  lv_label_set_text(titleLabel, text.c_str());
  setObjectVisible(titleLabel, true);
}

bool clockDashboardAgendaVisible() {
  return activeScreen == DASHBOARD_SCREEN_AGENDA;
}

void clockDashboardSetAgendaVisible(bool visible) {
  setActiveScreen(visible ? DASHBOARD_SCREEN_AGENDA : DASHBOARD_SCREEN_CLOCK);
}

void clockDashboardSetAgendaVisibilityCallback(
    RssVisibilityCallback visibility) {
  agendaVisibilityCallback = visibility;
}

void clockDashboardSetAgendaAvailable(bool available) {
  // Stránka se zakládá až s prvním zapnutím obrazovky, takže dokud ji majitel
  // nechce, nestojí ani jeden objekt LVGL.
  if (available && agendaPage == nullptr && dashboardScreen != nullptr) {
    createAgendaPage(dashboardScreen);
  }
  if (agendaFeatureAvailable == available) return;
  agendaFeatureAvailable = available;
  if (!available && activeScreen == DASHBOARD_SCREEN_AGENDA) {
    activeScreen = DASHBOARD_SCREEN_CLOCK;
    if (agendaPage != nullptr)
      lv_obj_add_flag(agendaPage, LV_OBJ_FLAG_HIDDEN);
    if (!settingsVisible && !firmwareUpdateActive) {
      lv_obj_clear_flag(primaryClockPage(), LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(primaryClockPage());
    }
    if (agendaVisibilityCallback != nullptr) agendaVisibilityCallback(false);
  }
  // O jednu obrazovku v rotaci míň nebo víc: ukazatel musí ubrat či přidat
  // tečku, jinak by sliboval obrazovku, na kterou se nedá přepnout.
  updateScreenDots();
}

void clockDashboardSetAgendaStatus(const char *message, uint8_t count,
                                   bool ready) {
  if (agendaPage == nullptr) return;
  if (count > CLOCK_AGENDA_MAX_ITEMS) count = CLOCK_AGENDA_MAX_ITEMS;
  agendaVisibleItemCount = count;
  for (size_t index = 0; index < CLOCK_AGENDA_MAX_ITEMS; ++index) {
    agendaRowStartsDay[index] = false;
  }
  const bool showMessage = count == 0;
  setObjectVisible(agendaStatusLabel, showMessage);
  if (showMessage) {
    // Text si obrazovka skládá sama, aby se přepnutím jazyka přeložil i on.
    // Úspěšné stažení prázdného kalendáře není chyba: den bez události je
    // platný výsledek, takže se místo hlášky o nedostupnosti řekne, že prostě
    // nic není.
    const char *text;
    if (message != nullptr && message[0] != '\0') {
      text = message;
    } else if (ready) {
      text = englishLanguage() ? "Nothing scheduled" : "Nic naplánovaného";
    } else {
      text = englishLanguage() ? "Loading agenda..." : "Načítám agendu...";
    }
    lv_label_set_text(agendaStatusLabel, text);
    // Řádky po předchozím naplnění musí zmizet, jinak by hláška ležela přes ně.
    layoutAgendaItems();
  }
  applyAgendaColors();
}

void clockDashboardSetAgendaItem(size_t index, const char *day,
                                 const char *time, const char *title,
                                 uint8_t calendar) {
  if (agendaPage == nullptr || index >= agendaVisibleItemCount) return;
  lv_obj_t *dayLabel = agendaDayLabels[index];
  lv_obj_t *timeLabel = agendaTimeLabels[index];
  lv_obj_t *titleLabel = agendaTitleLabels[index];
  if (dayLabel == nullptr || timeLabel == nullptr || titleLabel == nullptr) {
    return;
  }

  const bool startsDay = day != nullptr && day[0] != '\0';
  agendaRowStartsDay[index] = startsDay;
  if (startsDay) lv_label_set_text(dayLabel, day);

  const bool hasTime = time != nullptr && time[0] != '\0';
  lv_label_set_text(timeLabel, hasTime ? time : AGENDA_ALL_DAY_MARK);
  agendaRowCalendar[index] = calendar;
  setTextColor(timeLabel, agendaCalendarColor(calendar));
  lv_label_set_text(titleLabel, title != nullptr ? title : "");

  // Rozmístit jde až po posledním řádku: dřív není známo, kde leží hranice dnů,
  // a řádek s hlavičkou je o její výšku vyšší než ostatní.
  if (index + 1 == agendaVisibleItemCount) layoutAgendaItems();
}

void clockDashboardSetAgendaCalendars(const char *const *names, size_t count) {
  if (count > AGENDA_MAX_CALENDARS) count = AGENDA_MAX_CALENDARS;
  agendaCalendarCount = count;
  for (size_t index = 0; index < count; ++index) {
    strlcpy(agendaCalendarNames[index], names[index] != nullptr ? names[index] : "",
            AGENDA_CALENDAR_NAME_LENGTH);
  }
  updateAgendaLegendLabel();
}

void clockDashboardSetRadarVisible(bool visible) { setRadarVisible(visible); }

bool clockDashboardAutomaticRotationAllowed() {
  return !settingsVisible && !firmwareUpdateActive;
}

void clockDashboardSetRadarSnapshot(const uint16_t *pixels,
                                    const char *frameTime, uint16_t radiusKm,
                                    const char *message, bool loading,
                                    bool fullPreparationInProgress,
                                    bool latestFrame,
                                    uint8_t currentFrameNumber,
                                    uint8_t animationFrameCount,
                                    uint16_t displayedRadiusKm) {
  if (radarCanvas == nullptr || radarStatusLabel == nullptr ||
      radarTitleLabel == nullptr || radarRangeLabel == nullptr)
    return;
  radarFullPreparationInProgress = fullPreparationInProgress;
  const bool haveFrame = pixels != nullptr;
  if (haveFrame) {
    lv_canvas_set_buffer(radarCanvas, const_cast<uint16_t *>(pixels), 480, 480,
                         LV_IMG_CF_TRUE_COLOR);
    lv_obj_clear_flag(radarCanvas, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(radarCanvas);
  }
  updateRadarFrameDots(haveFrame ? animationFrameCount : 0, currentFrameNumber);
  updateRadarRangeDots(radiusKm);
  updateOverlayStatusLabels();

  // Bez jediného snímku není co překrývat: doprostřed přijde hláška a zbytek
  // výbavy obrazovky se schová, aby prázdný kruh nevypadal jako porucha.
  if (!haveFrame) {
    lv_label_set_text(radarStatusLabel,
                      radarEmptyStateText(loading || fullPreparationInProgress));
    alignCenter(radarStatusLabel, 0, 0);
    lv_obj_clear_flag(radarStatusLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(radarTitleLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(radarRangeLabel, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_add_flag(radarStatusLabel, LV_OBJ_FLAG_HIDDEN);

  // Řádek o snímku: nejnovější se pojmenuje "NYNÍ" a zezelená, starší nese
  // svoje stáří. Bez zelené v nočním červeném režimu, kde by byla jediná
  // barevná věc na displeji.
  char frameText[48];
  const bool haveTime = frameTime != nullptr && frameTime[0] != '\0';
  if (haveTime && latestFrame) {
    const bool highlight = !redNightVisualEnabled();
    snprintf(frameText, sizeof(frameText), "%s%s %s%s",
             highlight ? "#65FF45 " : "", englishLanguage() ? "NOW" : "NYNÍ",
             frameTime, highlight ? "#" : "");
  } else if (haveTime) {
    const int minutesAgo = radarFrameMinutesAgo(frameTime);
    if (minutesAgo > 0)
      snprintf(frameText, sizeof(frameText), "-%d %s  %s", minutesAgo,
               englishLanguage() ? "min" : "min", frameTime);
    else
      snprintf(frameText, sizeof(frameText), "%s", frameTime);
  } else {
    frameText[0] = '\0';
  }
  lv_label_set_text(radarTitleLabel, frameText);
  alignCenter(radarTitleLabel, 0, RADAR_FRAME_LABEL_OFFSET_Y);
  setObjectVisible(radarTitleLabel, frameText[0] != '\0');

  // Rozsah dole nad svými tečkami. Zdroj dat se nejmenuje: vybírá se
  // v nastavení a mění se nanejvýš jednou za život hodin, takže by na každém
  // snímku jen ubíral místo. Kdo ho potřebuje ověřit, najde ho na záložce
  // Meteoradar i na stránce diagnostiky. U RainVieweru je v popisku poloměr,
  // který vybrané přiblížení opravdu dává.
  // Verzálkami: z háčků a čárek mají dashboardové fonty jen ty nad velkými
  // písmeny, takže malé "á" v "Celá ČR" by skončilo prázdným čtverečkem.
  char rangeText[40];
  if (radiusKm == 0)
    snprintf(rangeText, sizeof(rangeText), "%s",
             englishLanguage() ? "ALL OF CZECHIA" : "CELÁ ČR");
  else
    snprintf(rangeText, sizeof(rangeText), "%u km", displayedRadiusKm);
  lv_label_set_text(radarRangeLabel, rangeText);
  alignCenter(radarRangeLabel, 0, RADAR_RANGE_LABEL_OFFSET_Y);
  lv_obj_clear_flag(radarRangeLabel, LV_OBJ_FLAG_HIDDEN);
}

void clockDashboardSetWifiAddress(const char *ipAddress) {
  if (firmwareUpdateActive) return;
  if (wifiAddressLabel == nullptr) return;
  if (ipAddress == nullptr || ipAddress[0] == '\0') {
    lv_label_set_text(wifiAddressLabel, "");
    return;
  }
  char text[32];
  snprintf(text, sizeof(text), "IP  %s", ipAddress);
  lv_label_set_text(wifiAddressLabel, text);
  alignCenter(wifiAddressLabel, 0, -112);
}

void clockDashboardSetFirmwareVersion(const char *version,
                                      bool updateAvailable) {
  if (firmwareUpdateActive) return;
  if (firmwareVersionLabel == nullptr) return;
  if (version == nullptr || version[0] == '\0') {
    lv_label_set_text(firmwareVersionLabel, "");
    return;
  }
  char text[32];
  snprintf(text, sizeof(text), "FW  %s", version);
  lv_label_set_text(firmwareVersionLabel, text);
  lv_obj_set_style_text_color(
      firmwareVersionLabel, updateAvailable ? COLOR_ERROR : COLOR_MUTED, 0);
  alignCenter(firmwareVersionLabel, 0, -88);
}

void clockDashboardSetFirmwareUpdateActive(bool active) {
  if (firmwareUpdateOverlay == nullptr || firmwareUpdateActive == active) return;
  firmwareUpdateActive = active;
  if (active) {
    clockDashboardSetFirmwareUpdateBlack(false);
    lv_obj_add_flag(primaryClockPage(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(radarPage, LV_OBJ_FLAG_HIDDEN);
    if (rssPage != nullptr) lv_obj_add_flag(rssPage, LV_OBJ_FLAG_HIDDEN);
    if (agendaPage != nullptr) lv_obj_add_flag(agendaPage, LV_OBJ_FLAG_HIDDEN);
    if (forecastPage != nullptr)
      lv_obj_add_flag(forecastPage, LV_OBJ_FLAG_HIDDEN);
    if (planesPage != nullptr)
      lv_obj_add_flag(planesPage, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(settingsPage, LV_OBJ_FLAG_HIDDEN);
    if (activeScreen == DASHBOARD_SCREEN_RADAR &&
        radarVisibilityCallback != nullptr)
      radarVisibilityCallback(false);
    // Radar letadel stahuje jen zapnutý; bez tohohle by během aktualizace
    // soupeřil o síť s OTA.
    if (activeScreen == DASHBOARD_SCREEN_PLANES &&
        planesVisibilityCallback != nullptr)
      planesVisibilityCallback(false);
    lv_obj_clear_flag(firmwareUpdateOverlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(firmwareUpdateOverlay);
    updateScreenDots();
  } else {
    lv_obj_add_flag(firmwareUpdateOverlay, LV_OBJ_FLAG_HIDDEN);
    if (settingsVisible) {
      lv_obj_clear_flag(settingsPage, LV_OBJ_FLAG_HIDDEN);
    } else if (activeScreen == DASHBOARD_SCREEN_RADAR) {
      lv_obj_clear_flag(radarPage, LV_OBJ_FLAG_HIDDEN);
      if (radarVisibilityCallback != nullptr) radarVisibilityCallback(true);
    } else if (activeScreen == DASHBOARD_SCREEN_RSS && rssPage != nullptr) {
      lv_obj_clear_flag(rssPage, LV_OBJ_FLAG_HIDDEN);
    } else if (activeScreen == DASHBOARD_SCREEN_AGENDA &&
               agendaPage != nullptr) {
      lv_obj_clear_flag(agendaPage, LV_OBJ_FLAG_HIDDEN);
      // Bez tohohle by se po přerušené aktualizaci obrazovka vrátila, ale
      // služba by zůstala vypnutá a už nikdy nic nestáhla.
      if (agendaVisibilityCallback != nullptr) agendaVisibilityCallback(true);
    } else if (activeScreen == DASHBOARD_SCREEN_FORECAST &&
               forecastPage != nullptr) {
      lv_obj_clear_flag(forecastPage, LV_OBJ_FLAG_HIDDEN);
    } else if (activeScreen == DASHBOARD_SCREEN_PLANES &&
               planesPage != nullptr) {
      lv_obj_clear_flag(planesPage, LV_OBJ_FLAG_HIDDEN);
      // Bez tohohle by se po přerušené aktualizaci obrazovka vrátila, ale
      // služba by zůstala vypnutá a už nikdy nic nestáhla.
      if (planesVisibilityCallback != nullptr) planesVisibilityCallback(true);
    } else {
      lv_obj_clear_flag(primaryClockPage(), LV_OBJ_FLAG_HIDDEN);
    }
    clockDashboardUpdate(currentValues);
    updateScreenDots();
  }
  lv_obj_invalidate(lv_scr_act());
}

void clockDashboardSetFirmwareUpdateBlack(bool black) {
  if (firmwareUpdateTitleLabel == nullptr ||
      firmwareUpdateCountdownLabel == nullptr) return;
  if (black) {
    lv_obj_add_flag(firmwareUpdateTitleLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(firmwareUpdateCountdownLabel, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(firmwareUpdateTitleLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(firmwareUpdateCountdownLabel, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_invalidate(firmwareUpdateOverlay);
}

void clockDashboardSetFirmwareUpdateCountdown(uint8_t seconds) {
  if (firmwareUpdateTitleLabel == nullptr ||
      firmwareUpdateCountdownLabel == nullptr) return;
  char text[4];
  snprintf(text, sizeof(text), "%u", seconds);
  lv_obj_clear_flag(firmwareUpdateTitleLabel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(firmwareUpdateCountdownLabel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_opa(firmwareUpdateTitleLabel, LV_OPA_COVER, 0);
  lv_obj_set_style_text_opa(firmwareUpdateCountdownLabel, LV_OPA_COVER, 0);
  lv_label_set_text(firmwareUpdateCountdownLabel, text);
  alignCenter(firmwareUpdateCountdownLabel, 0, 35);
  lv_obj_move_foreground(firmwareUpdateTitleLabel);
  lv_obj_move_foreground(firmwareUpdateCountdownLabel);
  lv_obj_invalidate(firmwareUpdateOverlay);
}

void clockDashboardSetWebActive(bool active) {
  webActive = active;
  if (firmwareUpdateActive) return;
  applyDashboardColors();
}

void clockDashboardSetWifiConnected(bool connected) {
  wifiConnected = connected;
  if (firmwareUpdateActive) return;
  applyDashboardColors();
}

void clockDashboardSetWebMode(uint8_t mode) {
  selectedWebMode = constrain(mode, static_cast<uint8_t>(0),
                              static_cast<uint8_t>(2));
  if (webModeDropdown != nullptr)
    lv_dropdown_set_selected(webModeDropdown, selectedWebMode);
}

void clockDashboardSetDate(const char *dateText) {
  if (firmwareUpdateActive) return;
  if (valuesDateLabel != nullptr) lv_label_set_text(valuesDateLabel, dateText);
  if (strcmp(lv_label_get_text(dateLabel), dateText) == 0) return;
  lv_label_set_text(dateLabel, dateText);
  alignCenter(dateLabel, 0, analogLayoutEnabled() ? -142 : -43);
}

void clockDashboardSetNameday(const char *nameday) {
  if (firmwareUpdateActive) return;
  const char *value = nameday != nullptr ? nameday : "";
  if (valuesNamedayLabel != nullptr) {
    lv_label_set_text(valuesNamedayLabel, value);
    alignCenter(valuesNamedayLabel, 0, -108);
  }
  if (namedayLabel == nullptr) return;
  if (strcmp(lv_label_get_text(namedayLabel), value) == 0) return;
  lv_label_set_text(namedayLabel, value);
  alignCenter(namedayLabel, 0, analogLayoutEnabled() ? -123 : -20);
}

void clockDashboardSetSecond(uint8_t second) {
  if (firmwareUpdateActive) return;
  if (second > SECOND_DOT_COUNT) second = SECOND_DOT_COUNT;
  if (displayedSecond == second) return;
  const bool minuteRolledOver = displayedSecond >= 59 && second <= 1;
  if (analogLayoutEnabled()) invalidateAnalogHands();
  displayedSecond = second;
  secondTickStartedAt = millis();
  lastRenderedTimeColonColor = UINT32_MAX;
  if (minuteRolledOver) {
    secondFadeActive = true;
    secondFadeStartedAt = millis();
    lastSecondFadeFrameAt = 0;
  }
  if (analogLayoutEnabled()) {
    secondFadeActive = false;
    invalidateAnalogHands();
  } else {
    renderSecondRing(millis());
  }
  if (timeColonEffect != CLOCK_TIME_COLON_STEADY)
    renderTimeColon(millis(), true);
}

void clockDashboardSetTime(const char *timeText) {
  if (firmwareUpdateActive) return;
  if (valuesTimeLabel != nullptr) lv_label_set_text(valuesTimeLabel, timeText);
  if (valuesLayoutEnabled()) {
    strlcpy(displayedTimeText, timeText, sizeof(displayedTimeText));
    updateOverlayStatusLabels();
    return;
  }
  if (analogLayoutEnabled() && strcmp(displayedTimeText, timeText) == 0) {
    lv_obj_add_flag(timeLabel, LV_OBJ_FLAG_HIDDEN);
    updateOverlayStatusLabels();
    return;
  }
  lv_obj_set_style_text_font(timeLabel, configuredTimeFont(), 0);
  if (analogLayoutEnabled()) invalidateAnalogHands(true, false);
  strlcpy(displayedTimeText, timeText, sizeof(displayedTimeText));
  updateOverlayStatusLabels();
  if (analogLayoutEnabled()) {
    lv_obj_add_flag(timeLabel, LV_OBJ_FLAG_HIDDEN);
    invalidateAnalogHands(true, false);
    return;
  }
  renderTimeColon(millis(), true);
}
