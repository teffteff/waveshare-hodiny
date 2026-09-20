#pragma once

#include "ClockConfig.h"
#include "SettingsShareService.h"

using ClockConfigLoadCallback = void (*)(ClockConfig &config);
using ClockConfigSaveCallback = bool (*)(const ClockConfig &config,
                                         bool tokenWasSubmitted);
using ConfigurationWebStatusCallback = void (*)(bool active);
using SunTransitionTimesCallback = void (*)(uint64_t &nextSunriseTimestamp,
                                            uint64_t &nextSunsetTimestamp);
using HomeAssistantRefreshCallback = bool (*)();
using DisplayPowerCallback = void (*)(bool forcedOff);
using DisplayPowerStatusCallback = bool (*)();
using RadarRangeStateCallback = void (*)(uint16_t &savedRadiusKm,
                                         uint16_t &activeRadiusKm);
using RadarRangePreviewCallback = bool (*)(uint16_t radiusKm);
using ClockAppearanceStateCallback = void (*)(
    ClockAppearanceConfig &savedAppearance,
    ClockAppearanceConfig &activeAppearance);
using ClockAppearanceChangeCallback = bool (*)(
    const ClockAppearanceConfig &appearance);
using DayNightStatusCallback = void (*)(bool &sunAvailable, bool &sunIsDay,
                                        bool &lightAvailable, bool &lightOn,
                                        bool &nightMode);

enum ConfigurationWebMode : uint8_t {
  CONFIGURATION_WEB_TIMED = 0,
  CONFIGURATION_WEB_ALWAYS = 1,
  CONFIGURATION_WEB_DISABLED = 2,
};

void configurationWebBegin(ClockConfigLoadCallback loadCallback,
                           ClockConfigSaveCallback saveCallback,
                           ConfigurationWebStatusCallback statusCallback,
                           SunTransitionTimesCallback sunTimesCallback,
                           HomeAssistantRefreshCallback refreshCallback,
                           DayNightStatusCallback dayNightStatusCallback,
                           DisplayPowerCallback displayPowerCallback,
                           DisplayPowerStatusCallback displayPowerStatusCallback,
                           RadarRangeStateCallback radarRangeStateCallback,
                           RadarRangePreviewCallback radarRangePreviewCallback,
                           ClockAppearanceStateCallback appearanceStateCallback,
                           ClockAppearanceChangeCallback appearancePreviewCallback,
                           ClockAppearanceChangeCallback appearanceSaveCallback);
// Diagnostika hlásí, kolik zásobníku úlohám nejméně zbývalo. Úloha loop se
// změří sama, protože v ní web server běží; na datovou úlohu je potřeba
// handle, který zná jen skeč.
void configurationWebSetHomeAssistantTask(TaskHandle_t task);
void configurationWebSetRssTask(TaskHandle_t task);
// Zkouška kanálu zpráv. Web server běží ve smyčce, jejíž zásobník na ověření
// proti svazku kořenů Mozilly nestačí, takže samotné stažení obstará úloha
// kanálu; skeč do téhle funkce schová předání žádosti i čekání na výsledek.
using RssProbeCallback = bool (*)(const ClockRssConfig &config,
                                  int &httpStatus, String &error);
void configurationWebSetRssProbe(RssProbeCallback callback);
void configurationWebSetAgendaTask(TaskHandle_t task);
// Zkouška adresy agendy. Platí pro ni totéž co pro kanál zpráv: stažení
// obstará úloha agendy, protože zásobník smyčky na ověření proti svazku
// kořenů Mozilly nestačí.
using AgendaProbeCallback = bool (*)(const ClockAgendaConfig &config,
                                     const ClockAgendaCalendarsConfig &calendars,
                                     int &httpStatus, String &error);
void configurationWebSetAgendaProbe(AgendaProbeCallback callback);
// Rozvrh má vlastní úlohu a zkoušku adresy, stejně jako agenda.
void configurationWebSetSchoolTask(TaskHandle_t task);
using SchoolProbeCallback = bool (*)(const ClockSchoolConfig &config,
                                     int &httpStatus, String &error);
void configurationWebSetSchoolProbe(SchoolProbeCallback callback);
// Zkouška adresy serveru družic. Stáhne ji úloha družic, skeč schová předání
// i čekání na výsledek.
struct SatelliteProbeResult;
using SatellitesProbeCallback = bool (*)(const ClockSatellitesConfig &config,
                                         SatelliteProbeResult &result);
void configurationWebSetSatellitesProbe(SatellitesProbeCallback callback);
// Přenos zálohy nastavení na server a zpět. Z téhož důvodu jako zkouška agendy
// ho provede úloha agendy; skeč schová předání žádosti i čekání na výsledek.
using SettingsShareCallback = bool (*)(const SettingsShareRequest &request,
                                       SettingsShareResult &result);
void configurationWebSetSettingsShare(SettingsShareCallback callback);
// Pomalý výpočet (odvození klíče šifrované zálohy) mimo smyčku. Skeč ho
// předá vlastní úloze a mezitím kreslí displej a krmí watchdog smyčky. Vrací
// false, jen když se práce vůbec nespustila; `work` pak neproběhla.
using BackgroundWorkCallback = bool (*)(void (*work)(void *context),
                                        void *context);
void configurationWebSetBackgroundWork(BackgroundWorkCallback callback);
// Uživatel změnil název hodin v síti (DeviceName.h). Volá se po odeslání
// odpovědi, s už uloženým názvem.
using DeviceNameChangedCallback = void (*)(const char *name);
void configurationWebSetDeviceNameChanged(DeviceNameChangedCallback callback);
// Přepnutí obrazovky z webu a z ovládacího API. show vrací false, když je
// obrazovka vypnutá nebo se teď přepnout nedá (aktualizace firmwaru);
// current vrací zobrazenou obrazovku, nebo CLOCK_SCREEN_ORDER_COUNT, když je
// otevřené nastavení na displeji.
using ScreenShowCallback = bool (*)(uint8_t screen);
using CurrentScreenCallback = uint8_t (*)();
void configurationWebSetScreenControl(ScreenShowCallback show,
                                      CurrentScreenCallback current);
void configurationWebLoop();
void configurationWebEnsureActive();
void configurationWebExtendAvailability();
ConfigurationWebMode configurationWebMode();
bool configurationWebSetMode(ConfigurationWebMode mode);
// Heslo webu chrání webové nastavení. Zapomenuté heslo jde smazat jen na
// displeji hodin: kdo na ně sáhne, má k hodinám fyzický přístup.
bool configurationWebPasswordConfigured();
bool configurationWebClearPassword();
void configurationWebLockForTest();
void configurationWebUnlockForTest();
