#include "RgbPanel.h"

#include <algorithm>
#include <inttypes.h>
#include <stdlib.h>

#include "BoardDisplay.h"
#include "VsyncTiming.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

esp_lcd_panel_handle_t panel_handle = NULL;

namespace {
esp_lcd_rgb_timing_t panelTimings = {};
size_t panelBounceRows = 1;
uint32_t currentPixelClockFrequencyHz = 0;

StaticSemaphore_t frameFinishedSemaphoreStorage;
SemaphoreHandle_t frameFinishedSemaphore = nullptr;
portMUX_TYPE frameFinishedMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t finishedFrameCount = 0;

// Hlídání fáze bounce bufferů. Driver RGB panelu určuje, kterou část
// framebufferu kopírovat do dalšího bounce bufferu, jen podle vlastního
// čítače. Jeho přerušení není v IRAM (Arduino core má vypnuté
// CONFIG_LCD_RGB_ISR_IRAM_SAFE i CONFIG_LCD_RGB_RESTART_IN_VSYNC), takže při
// zápisu do flash nebo velké latenci přerušení pod Wi-Fi jedno doplnění
// vypadne, čítač se opozdí o celý bounce buffer a obraz zůstane trvale
// posunutý o jeho násobek. Konec průchodu čítače je proto při správné
// synchronizaci vždy stejně daleko za VSYNC; posun o blok jej oddálí
// o dobu vykreslení řádků jednoho bounce bufferu.
constexpr size_t PHASE_WINDOW = 8;
constexpr uint32_t PHASE_INVALID = UINT32_MAX;
volatile int64_t lastVsyncUs = 0;
volatile uint32_t phaseSamples[PHASE_WINDOW];
volatile uint32_t phaseSampleCount = 0;
uint32_t phaseCheckedCount = 0;
uint32_t phaseBaselineUs = 0;
bool phaseBaselineValid = false;
uint8_t phaseStrikes = 0;
uint32_t syncRepairCount = 0;
// Kopie výchozí fáze pro přerušení a počet jednotlivých snímků mimo ni.
// Hlídání v LCD_MaintainSync krátké výpadky záměrně filtruje, tady se počítá
// každý snímek, aby šlo měřit, kolik škubnutí je opravdu vidět.
volatile uint32_t isrPhaseBaselineUs = PHASE_INVALID;
volatile uint32_t isrFrameUs = 0;
volatile uint32_t isrToleranceUs = 0;
volatile uint32_t phaseGlitchCount = 0;
// Dva snímky po sobě mimo fázi znamenají trvalý posun (vypadlé doplnění
// bounce bufferu). Opraví ho restart v příštím včasném VSYNC, který proběhne
// tak jako tak; tady se posun jen započítá (displaySyncRepairs).
constexpr uint8_t ISR_STRIKES_TO_RESTART = 2;
constexpr uint8_t ISR_RESTART_SETTLE_FRAMES = 3;
volatile uint8_t isrStrikes = 0;
volatile uint8_t isrSettleFrames = 0;
volatile uint32_t isrRepairCount = 0;
// Restart panelu (DMA znovu od začátku snímku) provede driver ve VSYNC
// přerušení hned po callbacku onVsync. Do LCD FIFO ale mezitím došel začátek
// dalšího snímku a restart počítá s tím, že z něj panel ještě nic neodeslal.
// Přijde-li přerušení pozdě, až když panel začal kreslit (na 7" asi 0,58 ms
// po VSYNC, změřeno), DMA se rozjede o kus za panelem a obraz zůstane trvale
// posunutý do strany o tolik pixelů, kolik panel mezitím odeslal (16 na
// mikrosekundu). Fáze bounce bufferů se tím skoro nezmění, takže ho
// hlídání fáze nepozná; při 0,6 ms je posun ~80 px a fáze se pohne o 5 us.
// Dřív tak mohl posunout obraz každý LCD_Resync nebo oprava fáze, které
// trefily pozdní přerušení (výpadek přerušení, PSRAM pod zátěží).
//
// Restart se proto dělá jen v přerušení, které podle pravidelného rytmu
// snímků přišlo včas, a to v každém takovém snímku (jako
// CONFIG_LCD_RGB_RESTART_IN_VSYNC, který Arduino core nemá zapnutý). Včasný
// restart obraz nijak nezmění, a cokoli by se přesto posunulo (vypadlé
// doplnění bounce bufferu, podtečení DMA), drží nejvýš do dalšího včasného
// snímku. Pozdní snímky se jen přeskočí a počítají.
volatile uint32_t isrFrameNs = 0;
volatile int32_t isrRestartSlackUs = 0;
int64_t vsyncReferenceUs = 0;  // VsyncTiming.h, pod frameFinishedMux
volatile uint32_t lateVsyncCount = 0;
// Kdy (sekundy od startu) proběhly poslední opravy, ať jde dohledat, při čem
// obraz ujíždí (diagnostika displayRepairTimes).
constexpr size_t REPAIR_HISTORY = 16;
volatile uint32_t repairTimes[REPAIR_HISTORY] = {};

uint32_t lineDurationUs() {
  return (panelTimings.h_res + panelTimings.hsync_pulse_width +
          panelTimings.hsync_back_porch + panelTimings.hsync_front_porch) *
         1000000ULL / currentPixelClockFrequencyHz;
}

uint32_t frameDurationUs() {
  return lineDurationUs() *
         (panelTimings.v_res + panelTimings.vsync_pulse_width +
          panelTimings.vsync_back_porch + panelTimings.vsync_front_porch);
}

// Délka snímku v ns: v mikrosekundách by zaokrouhlení rytmus VSYNC za pár
// sekund rozladilo.
uint32_t frameDurationNs() {
  const uint64_t pixels =
      static_cast<uint64_t>(panelTimings.h_res + panelTimings.hsync_pulse_width +
                            panelTimings.hsync_back_porch +
                            panelTimings.hsync_front_porch) *
      (panelTimings.v_res + panelTimings.vsync_pulse_width +
       panelTimings.vsync_back_porch + panelTimings.vsync_front_porch);
  return static_cast<uint32_t>(pixels * 1000000000ULL /
                               currentPixelClockFrequencyHz);
}

void updateIsrPhaseLimits() {
  isrFrameUs = frameDurationUs();
  isrToleranceUs = lineDurationUs() * panelBounceRows / 2;
  isrFrameNs = frameDurationNs();
  // VSYNC přerušení přichází zhruba se začátkem synchronizačního pulzu, panel
  // začne kreslit po pulzu a zadním okraji (7": 12 řádků = 0,6 ms). Restart
  // jen do čtvrtiny té doby nechává rezervu i pro odhad zpoždění.
  isrRestartSlackUs = static_cast<int32_t>(
      lineDurationUs() *
      (panelTimings.vsync_pulse_width + panelTimings.vsync_back_porch) / 4);
  portENTER_CRITICAL(&frameFinishedMux);
  vsyncReferenceUs = 0;
  portEXIT_CRITICAL(&frameFinishedMux);
}

// Odchylka fáze od výchozí, srovnaná do rozsahu +-půl snímku.
int32_t IRAM_ATTR phaseDrift(uint32_t phaseUs, uint32_t baselineUs,
                             uint32_t frameUs) {
  int32_t drift = static_cast<int32_t>(phaseUs - baselineUs);
  drift %= static_cast<int32_t>(frameUs);
  if (drift > static_cast<int32_t>(frameUs / 2)) drift -= frameUs;
  if (drift < -static_cast<int32_t>(frameUs / 2)) drift += frameUs;
  return drift;
}

bool IRAM_ATTR onVsync(
    esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void*) {
  const int64_t now = esp_timer_get_time();
  portENTER_CRITICAL_ISR(&frameFinishedMux);
  lastVsyncUs = now;
  const bool onTime =
      vsyncLatenessUs(vsyncReferenceUs, now, isrFrameNs) <= isrRestartSlackUs;
  if (!onTime) ++lateVsyncCount;
  portEXIT_CRITICAL_ISR(&frameFinishedMux);
  // Jen nastaví příznak; driver restart provede hned po tomto callbacku.
  if (onTime) esp_lcd_rgb_panel_restart(panel_handle);
  return false;
}

bool IRAM_ATTR onBounceFrameFinished(
    esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void*) {
  BaseType_t highPriorityTaskWoken = pdFALSE;
  const int64_t now = esp_timer_get_time();
  portENTER_CRITICAL_ISR(&frameFinishedMux);
  ++finishedFrameCount;
  const int64_t sinceVsync = now - lastVsyncUs;
  // Chybějící VSYNC (dlouho zakázaná přerušení) by dal nesmyslnou fázi.
  const uint32_t phase =
      lastVsyncUs != 0 && sinceVsync >= 0 && sinceVsync < 100000
          ? static_cast<uint32_t>(sinceVsync)
          : PHASE_INVALID;
  phaseSamples[phaseSampleCount % PHASE_WINDOW] = phase;
  ++phaseSampleCount;
  const uint32_t baseline = isrPhaseBaselineUs;
  if (isrSettleFrames > 0) {
    --isrSettleFrames;
  } else if (phase != PHASE_INVALID && baseline != PHASE_INVALID &&
             isrFrameUs != 0 &&
             static_cast<uint32_t>(abs(phaseDrift(phase, baseline, isrFrameUs))) >
                 isrToleranceUs) {
    ++phaseGlitchCount;
    if (++isrStrikes >= ISR_STRIKES_TO_RESTART) {
      isrStrikes = 0;
      isrSettleFrames = ISR_RESTART_SETTLE_FRAMES;
      repairTimes[isrRepairCount % REPAIR_HISTORY] =
          static_cast<uint32_t>(now / 1000000);
      ++isrRepairCount;
    }
  } else {
    isrStrikes = 0;
  }
  portEXIT_CRITICAL_ISR(&frameFinishedMux);
  if (frameFinishedSemaphore != nullptr) {
    xSemaphoreGiveFromISR(frameFinishedSemaphore, &highPriorityTaskWoken);
  }
  return highPriorityTaskWoken == pdTRUE;
}
}  // namespace

void rgbPanelCreate(const esp_lcd_rgb_panel_config_t &config) {
  panelTimings = config.timings;
  currentPixelClockFrequencyHz = config.timings.pclk_hz;
  panelBounceRows =
      std::max<size_t>(1, config.bounce_buffer_size_px / config.timings.h_res);
  updateIsrPhaseLimits();
  ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&config, &panel_handle));
  frameFinishedSemaphore =
      xSemaphoreCreateBinaryStatic(&frameFinishedSemaphoreStorage);
  const esp_lcd_rgb_panel_event_callbacks_t callbacks = {
    .on_vsync = onVsync,
    .on_bounce_frame_finish = onBounceFrameFinished,
  };
  ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(
      panel_handle, &callbacks, nullptr));
  esp_lcd_panel_reset(panel_handle);
  esp_lcd_panel_init(panel_handle);
}

namespace {
// Restart sám proběhne v nejbližším včasném VSYNC (onVsync); dva snímky po
// něm ještě můžou patřit starému stavu, takže je hlídání vynechá.
//
// Správná fáze se restartem nemění - je daná taktem a časováním panelu -,
// takže výchozí fáze zůstává a oprava v přerušení hlídá dál. Dřív se
// zahazovala při každém restartu: hlídání pak deset snímků spalo, a když se
// během nich obraz posunul (třeba pod provozem vzdáleného nastavení), posun
// zůstal, nebo se z něj dokonce stala nová "správná" fáze. Zapomenout ji je
// potřeba jen po změně taktu.
void restartPanel(bool forgetBaseline) {
  portENTER_CRITICAL(&frameFinishedMux);
  phaseCheckedCount = phaseSampleCount + 2;
  isrStrikes = 0;
  isrSettleFrames = ISR_RESTART_SETTLE_FRAMES;
  portEXIT_CRITICAL(&frameFinishedMux);
  if (forgetBaseline) {
    phaseBaselineValid = false;
    isrPhaseBaselineUs = PHASE_INVALID;
  }
  phaseStrikes = 0;
}
}  // namespace

void LCD_Resync() { restartPanel(false); }

bool LCD_MaintainSync() {
  if (panel_handle == nullptr) return false;
  uint32_t samples[PHASE_WINDOW];
  uint32_t count = 0;
  portENTER_CRITICAL(&frameFinishedMux);
  count = phaseSampleCount;
  for (size_t i = 0; i < PHASE_WINDOW; ++i) samples[i] = phaseSamples[i];
  portEXIT_CRITICAL(&frameFinishedMux);
  if (static_cast<int32_t>(count - phaseCheckedCount) <
      static_cast<int32_t>(PHASE_WINDOW)) {
    return false;
  }
  phaseCheckedCount = count;

  // Medián odfiltruje jednotlivé opožděné obsluhy přerušení.
  size_t valid = 0;
  for (size_t i = 0; i < PHASE_WINDOW; ++i) {
    if (samples[i] != PHASE_INVALID) samples[valid++] = samples[i];
  }
  if (valid < PHASE_WINDOW / 2 + 1) return false;
  std::sort(samples, samples + valid);
  const uint32_t phaseUs = samples[valid / 2];

  if (!phaseBaselineValid) {
    // Po (re)startu je čítač driveru srovnaný s panelem, takže první změřená
    // fáze je ta správná.
    phaseBaselineUs = phaseUs;
    phaseBaselineValid = true;
    isrPhaseBaselineUs = phaseUs;
    return false;
  }

  // Fáze se měří od posledního VSYNC, takže je periodická po snímcích.
  const int32_t drift = phaseDrift(phaseUs, phaseBaselineUs, frameDurationUs());
  const uint32_t toleranceUs = lineDurationUs() * panelBounceRows / 2;
  if (static_cast<uint32_t>(abs(drift)) <= toleranceUs) {
    phaseStrikes = 0;
    return false;
  }
  // Dvě po sobě jdoucí okna mimo toleranci: jde o trvalý posun, ne o shluk
  // pozdních přerušení.
  if (++phaseStrikes < 2) return false;
  ESP_LOGW("lcd", "RGB panel posunut (faze %" PRIu32 " us, ocekavano %" PRIu32
                  " us), resynchronizuji",
           phaseUs, phaseBaselineUs);
  ++syncRepairCount;
  LCD_Resync();
  return true;
}

uint32_t LCD_SyncRepairCount() { return syncRepairCount + isrRepairCount; }

size_t LCD_RecentRepairTimes(uint32_t *output, size_t capacity) {
  portENTER_CRITICAL(&frameFinishedMux);
  const uint32_t total = isrRepairCount;
  const size_t count =
      std::min<size_t>(capacity, std::min<size_t>(total, REPAIR_HISTORY));
  for (size_t index = 0; index < count; ++index)
    output[index] = repairTimes[(total - count + index) % REPAIR_HISTORY];
  portEXIT_CRITICAL(&frameFinishedMux);
  return count;
}

uint32_t LCD_PhaseGlitchCount() { return phaseGlitchCount; }

uint32_t LCD_LateVsyncCount() { return lateVsyncCount; }

bool LCD_SetPixelClock(uint32_t frequencyHz) {
  if (panel_handle == nullptr) return false;
  if (frequencyHz == currentPixelClockFrequencyHz) return true;
  if (esp_lcd_rgb_panel_set_pclk(panel_handle, frequencyHz) != ESP_OK)
    return false;
  currentPixelClockFrequencyHz = frequencyHz;
  updateIsrPhaseLimits();
  // Jiný takt posune i správnou fázi; změřit znovu.
  restartPanel(true);
  return true;
}

uint32_t LCD_GetPixelClock() { return currentPixelClockFrequencyHz; }

bool LCD_addWindow(uint16_t Xstart, uint16_t Ystart, uint16_t Xend,
                   uint16_t Yend, uint8_t* color) {
  Xend = Xend + 1;      // esp_lcd_panel_draw_bitmap: x_end End index on x-axis (x_end not included)
  Yend = Yend + 1;      // esp_lcd_panel_draw_bitmap: y_end End index on y-axis (y_end not included)
  if (Xend >= panelTimings.h_res)
    Xend = panelTimings.h_res;
  if (Yend >= panelTimings.v_res)
    Yend = panelTimings.v_res;

  if (frameFinishedSemaphore == nullptr) return false;
  xSemaphoreTake(frameFinishedSemaphore, 0);

  // V bounce-buffer režimu draw_bitmap pouze zvolí PSRAM framebuffer pro
  // následující snímek. LVGL smí předchozí framebuffer znovu použít teprve
  // poté, co jej RGB driver celý překopíruje do interních bounce bufferů.
  // Společný critical section zaručí, že započítaný callback nemohl nastat
  // mezi pořízením čítače a předáním nového framebufferu panelu.
  uint32_t frameCountBeforeDraw = 0;
  esp_err_t drawResult = ESP_FAIL;
  portENTER_CRITICAL(&frameFinishedMux);
  frameCountBeforeDraw = finishedFrameCount;
  drawResult = esp_lcd_panel_draw_bitmap(panel_handle, Xstart, Ystart, Xend,
                                         Yend, color);
  portEXIT_CRITICAL(&frameFinishedMux);
  if (drawResult != ESP_OK) return false;

  const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(100);
  while (finishedFrameCount == frameCountBeforeDraw) {
    const TickType_t now = xTaskGetTickCount();
    if (static_cast<int32_t>(deadline - now) <= 0 ||
        xSemaphoreTake(frameFinishedSemaphore, deadline - now) != pdTRUE) {
      ESP_LOGE("lcd", "Timeout pri synchronizaci RGB framebufferu");
      return false;
    }
  }
  return true;
}
