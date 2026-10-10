#include "RgbPanel.h"

#include <algorithm>
#include <inttypes.h>
#include <stdlib.h>

#include "BoardDisplay.h"
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
// Oprava přímo v přerušení: dva snímky po sobě mimo fázi znamenají trvalý
// posun a panel se restartuje s příštím VSYNC. Obraz je tak posunutý dva až
// tři snímky místo desítek, než by posun našla hlavní smyčka.
constexpr uint8_t ISR_STRIKES_TO_RESTART = 2;
constexpr uint8_t ISR_RESTART_SETTLE_FRAMES = 3;
volatile uint8_t isrStrikes = 0;
volatile uint8_t isrSettleFrames = 0;
volatile uint32_t isrRepairCount = 0;

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

void updateIsrPhaseLimits() {
  isrFrameUs = frameDurationUs();
  isrToleranceUs = lineDurationUs() * panelBounceRows / 2;
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
  portEXIT_CRITICAL_ISR(&frameFinishedMux);
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
  bool restart = false;
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
      ++isrRepairCount;
      restart = true;
    }
  } else {
    isrStrikes = 0;
  }
  portEXIT_CRITICAL_ISR(&frameFinishedMux);
  // Restart jen nastaví příznak; driver ho provede na konci snímku (VSYNC).
  if (restart) esp_lcd_rgb_panel_restart(panel_handle);
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

void LCD_Resync() {
  if (panel_handle != nullptr) {
    esp_lcd_rgb_panel_restart(panel_handle);
  }
  // Restart proběhne až na konci právě vysílaného snímku. Fázi pak změříme
  // znovu a vynecháme dva snímky, které mohou ještě patřit starému stavu.
  portENTER_CRITICAL(&frameFinishedMux);
  phaseCheckedCount = phaseSampleCount + 2;
  portEXIT_CRITICAL(&frameFinishedMux);
  phaseBaselineValid = false;
  isrPhaseBaselineUs = PHASE_INVALID;
  phaseStrikes = 0;
}

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

uint32_t LCD_PhaseGlitchCount() { return phaseGlitchCount; }

bool LCD_SetPixelClock(uint32_t frequencyHz) {
  if (panel_handle == nullptr) return false;
  if (frequencyHz == currentPixelClockFrequencyHz) return true;
  if (esp_lcd_rgb_panel_set_pclk(panel_handle, frequencyHz) != ESP_OK)
    return false;
  currentPixelClockFrequencyHz = frequencyHz;
  updateIsrPhaseLimits();
  // Jiný takt posune i správnou fázi; změřit znovu.
  LCD_Resync();
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
