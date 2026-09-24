#include "Display_ST7701.h"

#include <algorithm>
#include <stdlib.h>

#include "esp_timer.h"

spi_device_handle_t SPI_handle = NULL;
esp_lcd_panel_handle_t panel_handle = NULL;
static uint32_t currentPixelClockFrequencyHz =
    ESP_PANEL_LCD_RGB_TIMING_FREQ_HZ;

namespace {
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
// o dobu vykreslení dvaceti řádků.
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
  phaseSamples[phaseSampleCount % PHASE_WINDOW] =
      lastVsyncUs != 0 && sinceVsync >= 0 && sinceVsync < 100000
          ? static_cast<uint32_t>(sinceVsync)
          : PHASE_INVALID;
  ++phaseSampleCount;
  portEXIT_CRITICAL_ISR(&frameFinishedMux);
  if (frameFinishedSemaphore != nullptr) {
    xSemaphoreGiveFromISR(frameFinishedSemaphore, &highPriorityTaskWoken);
  }
  return highPriorityTaskWoken == pdTRUE;
}
}  // namespace

void ST7701_WriteCommand(uint8_t cmd)
{
  spi_transaction_t spi_tran = {
    .cmd = 0,
    .addr = cmd,
    .length = 0,
    .rxlength = 0,
  };
  spi_device_transmit(SPI_handle, &spi_tran);
}
void ST7701_WriteData(uint8_t data)
{
  spi_transaction_t spi_tran = {
    .cmd = 1,
    .addr = data,
    .length = 0,
    .rxlength = 0,
  };
  spi_device_transmit(SPI_handle, &spi_tran);
}

void ST7701_CS_EN(){
  Set_EXIO(EXIO_PIN3,Low);
  vTaskDelay(pdMS_TO_TICKS(10));
}
void ST7701_CS_Dis(){
  Set_EXIO(EXIO_PIN3,High);
  vTaskDelay(pdMS_TO_TICKS(10));
}
void ST7701_Reset(){
  Set_EXIO(EXIO_PIN1,Low);
  vTaskDelay(pdMS_TO_TICKS(10));
  Set_EXIO(EXIO_PIN1,High);
  vTaskDelay(pdMS_TO_TICKS(50));
}
void ST7701_Init()
{
  spi_bus_config_t buscfg = {
    .mosi_io_num = LCD_MOSI_PIN,
    .miso_io_num = -1,
    .sclk_io_num = LCD_CLK_PIN,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = 64, // ESP32 S3 max size is 64Kbytes
  };
  spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
  spi_device_interface_config_t devcfg = {
    .command_bits = 1,
    .address_bits = 8,
    .mode = SPI_MODE0,
    .clock_speed_hz = 40000000,
    .spics_io_num = -1,
    .queue_size = 1,            // Not using queues
  };
  spi_bus_add_device(SPI2_HOST, &devcfg, &SPI_handle);

  ST7701_CS_EN();
  ST7701_WriteCommand(0xFF);
  ST7701_WriteData(0x77);
  ST7701_WriteData(0x01);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x10);

  ST7701_WriteCommand(0xC0);
  ST7701_WriteData(0x3B);//Scan line
  ST7701_WriteData(0x00);

  ST7701_WriteCommand(0xC1);
  ST7701_WriteData(0x0B);	//VBP
  ST7701_WriteData(0x02);

  ST7701_WriteCommand(0xC2);
  ST7701_WriteData(0x07);
  ST7701_WriteData(0x02);

  ST7701_WriteCommand(0xCC);
  ST7701_WriteData(0x10);

  ST7701_WriteCommand(0xCD);//RGB format
  ST7701_WriteData(0x08);

  ST7701_WriteCommand(0xB0); // IPS
  ST7701_WriteData(0x00); // 255
  ST7701_WriteData(0x11); // 251
  ST7701_WriteData(0x16); // 247  down
  ST7701_WriteData(0x0e); // 239
  ST7701_WriteData(0x11); // 231
  ST7701_WriteData(0x06); // 203
  ST7701_WriteData(0x05); // 175
  ST7701_WriteData(0x09); // 147
  ST7701_WriteData(0x08); // 108
  ST7701_WriteData(0x21); // 80
  ST7701_WriteData(0x06); // 52
  ST7701_WriteData(0x13); // 24
  ST7701_WriteData(0x10); // 16
  ST7701_WriteData(0x29); // 8    down
  ST7701_WriteData(0x31); // 4
  ST7701_WriteData(0x18); // 0

  ST7701_WriteCommand(0xB1);//  IPS
  ST7701_WriteData(0x00);//  255
  ST7701_WriteData(0x11);//  251
  ST7701_WriteData(0x16);//  247   down
  ST7701_WriteData(0x0e);//  239
  ST7701_WriteData(0x11);//  231
  ST7701_WriteData(0x07);//  203
  ST7701_WriteData(0x05);//  175
  ST7701_WriteData(0x09);//  147
  ST7701_WriteData(0x09);//  108
  ST7701_WriteData(0x21);//  80
  ST7701_WriteData(0x05);//  52
  ST7701_WriteData(0x13);//  24
  ST7701_WriteData(0x11);//  16
  ST7701_WriteData(0x2a);//  8  down
  ST7701_WriteData(0x31);//  4
  ST7701_WriteData(0x18);//  0

  ST7701_WriteCommand(0xFF);
  ST7701_WriteData(0x77);
  ST7701_WriteData(0x01);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x11);

  ST7701_WriteCommand(0xB0);  //VOP  3.5375+ *x 0.0125
  ST7701_WriteData(0x6d);  //5D

  ST7701_WriteCommand(0xB1); 	//VCOM amplitude setting
  ST7701_WriteData(0x37);  //

  ST7701_WriteCommand(0xB2); 	//VGH Voltage setting
  ST7701_WriteData(0x81);	//12V

  ST7701_WriteCommand(0xB3);
  ST7701_WriteData(0x80);

  ST7701_WriteCommand(0xB5); 	//VGL Voltage setting
  ST7701_WriteData(0x43);	//-8.3V

  ST7701_WriteCommand(0xB7);
  ST7701_WriteData(0x85);

  ST7701_WriteCommand(0xB8);
  ST7701_WriteData(0x20);

  ST7701_WriteCommand(0xC1);
  ST7701_WriteData(0x78);

  ST7701_WriteCommand(0xC2);
  ST7701_WriteData(0x78);

  ST7701_WriteCommand(0xD0);
  ST7701_WriteData(0x88);

  ST7701_WriteCommand(0xE0);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x02);

  ST7701_WriteCommand(0xE1);
  ST7701_WriteData(0x03);
  ST7701_WriteData(0xA0);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x04);
  ST7701_WriteData(0xA0);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x20);
  ST7701_WriteData(0x20);

  ST7701_WriteCommand(0xE2);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);

  ST7701_WriteCommand(0xE3);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x11);
  ST7701_WriteData(0x00);

  ST7701_WriteCommand(0xE4);
  ST7701_WriteData(0x22);
  ST7701_WriteData(0x00);

  ST7701_WriteCommand(0xE5);
  ST7701_WriteData(0x05);
  ST7701_WriteData(0xEC);
  ST7701_WriteData(0xA0);
  ST7701_WriteData(0xA0);
  ST7701_WriteData(0x07);
  ST7701_WriteData(0xEE);
  ST7701_WriteData(0xA0);
  ST7701_WriteData(0xA0);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);

  ST7701_WriteCommand(0xE6);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x11);
  ST7701_WriteData(0x00);

  ST7701_WriteCommand(0xE7);
  ST7701_WriteData(0x22);
  ST7701_WriteData(0x00);

  ST7701_WriteCommand(0xE8);
  ST7701_WriteData(0x06);
  ST7701_WriteData(0xED);
  ST7701_WriteData(0xA0);
  ST7701_WriteData(0xA0);
  ST7701_WriteData(0x08);
  ST7701_WriteData(0xEF);
  ST7701_WriteData(0xA0);
  ST7701_WriteData(0xA0);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);

  ST7701_WriteCommand(0xEB);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x40);
  ST7701_WriteData(0x40);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);

  ST7701_WriteCommand(0xED);
  ST7701_WriteData(0xFF);
  ST7701_WriteData(0xFF);
  ST7701_WriteData(0xFF);
  ST7701_WriteData(0xBA);
  ST7701_WriteData(0x0A);
  ST7701_WriteData(0xBF);
  ST7701_WriteData(0x45);
  ST7701_WriteData(0xFF);
  ST7701_WriteData(0xFF);
  ST7701_WriteData(0x54);
  ST7701_WriteData(0xFB);
  ST7701_WriteData(0xA0);
  ST7701_WriteData(0xAB);
  ST7701_WriteData(0xFF);
  ST7701_WriteData(0xFF);
  ST7701_WriteData(0xFF);

  ST7701_WriteCommand(0xEF);
  ST7701_WriteData(0x10);
  ST7701_WriteData(0x0D);
  ST7701_WriteData(0x04);
  ST7701_WriteData(0x08);
  ST7701_WriteData(0x3F);
  ST7701_WriteData(0x1F);

  ST7701_WriteCommand(0xFF);
  ST7701_WriteData(0x77);
  ST7701_WriteData(0x01);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x13);

  ST7701_WriteCommand(0xEF);
  ST7701_WriteData(0x08);

  ST7701_WriteCommand(0xFF);
  ST7701_WriteData(0x77);
  ST7701_WriteData(0x01);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);
  ST7701_WriteData(0x00);


  ST7701_WriteCommand(0x36);
  ST7701_WriteData(0x00);

  ST7701_WriteCommand(0x3A);
  ST7701_WriteData(0x66);

  ST7701_WriteCommand(0x11);

  vTaskDelay(pdMS_TO_TICKS(480));

  ST7701_WriteCommand(0x20); //
  vTaskDelay(pdMS_TO_TICKS(120));
  ST7701_WriteCommand(0x29);
  ST7701_CS_Dis();

  //  RGB
  esp_lcd_rgb_panel_config_t rgb_config = {
    .clk_src = LCD_CLK_SRC_DEFAULT,
    .timings =  {
      .pclk_hz = ESP_PANEL_LCD_RGB_TIMING_FREQ_HZ,
      .h_res = ESP_PANEL_LCD_HEIGHT,
      .v_res = ESP_PANEL_LCD_WIDTH,
      .hsync_pulse_width = ESP_PANEL_LCD_RGB_TIMING_HPW,
      .hsync_back_porch = ESP_PANEL_LCD_RGB_TIMING_HBP,
      .hsync_front_porch = ESP_PANEL_LCD_RGB_TIMING_HFP,
      .vsync_pulse_width = ESP_PANEL_LCD_RGB_TIMING_VPW,
      .vsync_back_porch = ESP_PANEL_LCD_RGB_TIMING_VBP,
      .vsync_front_porch = ESP_PANEL_LCD_RGB_TIMING_VFP,
      .flags = {
        .hsync_idle_low = 0,  /*!< The hsync signal is low in IDLE state */
        .vsync_idle_low = 0,  /*!< The vsync signal is low in IDLE state */
        .de_idle_high = 0,    /*!< The de signal is high in IDLE state */
        .pclk_active_neg = false,
        .pclk_idle_high = 0,  /*!< The PCLK stays at high level in IDLE phase */
      },
    },
    .data_width = ESP_PANEL_LCD_RGB_DATA_WIDTH,
    .bits_per_pixel = ESP_PANEL_LCD_RGB_PIXEL_BITS,
    .num_fbs = ESP_PANEL_LCD_RGB_FRAME_BUF_NUM,
    .bounce_buffer_size_px = ESP_PANEL_LCD_RGB_BOUNCE_BUF_SIZE,
    .psram_trans_align = 64,
    .hsync_gpio_num = ESP_PANEL_LCD_PIN_NUM_RGB_HSYNC,
    .vsync_gpio_num = ESP_PANEL_LCD_PIN_NUM_RGB_VSYNC,
    .de_gpio_num = ESP_PANEL_LCD_PIN_NUM_RGB_DE,
    .pclk_gpio_num = ESP_PANEL_LCD_PIN_NUM_RGB_PCLK,
    .disp_gpio_num = ESP_PANEL_LCD_PIN_NUM_RGB_DISP,
    .data_gpio_nums = {
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA0,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA1,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA2,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA3,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA4,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA5,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA6,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA7,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA8,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA9,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA10,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA11,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA12,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA13,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA14,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA15,
    },
    .flags = {
      .disp_active_low = 0,
      .refresh_on_demand = 0,
      .fb_in_psram = true,
      .double_fb = true,
      .no_fb = 0,
      .bb_invalidate_cache = 0,
    },
  };
  ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&rgb_config, &panel_handle));
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

void LCD_Init() {
  ST7701_Reset();
  ST7701_Init();
  Touch_Init();
  Backlight_Init();
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
    return false;
  }

  const uint32_t lineUs =
      (ESP_PANEL_LCD_HEIGHT + ESP_PANEL_LCD_RGB_TIMING_HPW +
       ESP_PANEL_LCD_RGB_TIMING_HBP + ESP_PANEL_LCD_RGB_TIMING_HFP) *
      1000000ULL / currentPixelClockFrequencyHz;
  const uint32_t frameUs =
      lineUs * (ESP_PANEL_LCD_WIDTH + ESP_PANEL_LCD_RGB_TIMING_VPW +
                ESP_PANEL_LCD_RGB_TIMING_VBP + ESP_PANEL_LCD_RGB_TIMING_VFP);
  const uint32_t bounceRows =
      ESP_PANEL_LCD_RGB_BOUNCE_BUF_SIZE / ESP_PANEL_LCD_HEIGHT;
  // Fáze se měří od posledního VSYNC, takže je periodická po snímcích.
  int32_t drift = static_cast<int32_t>(phaseUs - phaseBaselineUs);
  drift %= static_cast<int32_t>(frameUs);
  if (drift > static_cast<int32_t>(frameUs / 2)) drift -= frameUs;
  if (drift < -static_cast<int32_t>(frameUs / 2)) drift += frameUs;
  const uint32_t toleranceUs = lineUs * bounceRows / 2;
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

uint32_t LCD_SyncRepairCount() { return syncRepairCount; }

bool LCD_SetPixelClock(uint32_t frequencyHz) {
  if (panel_handle == nullptr) return false;
  if (frequencyHz == currentPixelClockFrequencyHz) return true;
  if (esp_lcd_rgb_panel_set_pclk(panel_handle, frequencyHz) != ESP_OK)
    return false;
  currentPixelClockFrequencyHz = frequencyHz;
  return true;
}

uint32_t LCD_GetPixelClock() { return currentPixelClockFrequencyHz; }

void LCD_Sleep() {
  ST7701_CS_EN();
  ST7701_WriteCommand(0x28);  // Display Off
  vTaskDelay(pdMS_TO_TICKS(20));
  ST7701_WriteCommand(0x10);  // Sleep In
  ST7701_CS_Dis();
  vTaskDelay(pdMS_TO_TICKS(120));
}

void LCD_Wake() {
  ST7701_CS_EN();
  ST7701_WriteCommand(0x11);  // Sleep Out
  vTaskDelay(pdMS_TO_TICKS(120));
  ST7701_WriteCommand(0x29);  // Display On
  ST7701_CS_Dis();
  vTaskDelay(pdMS_TO_TICKS(20));
  LCD_Resync();
}

bool LCD_addWindow(uint16_t Xstart, uint16_t Ystart, uint16_t Xend,
                   uint16_t Yend, uint8_t* color) {
  Xend = Xend + 1;      // esp_lcd_panel_draw_bitmap: x_end End index on x-axis (x_end not included)
  Yend = Yend + 1;      // esp_lcd_panel_draw_bitmap: y_end End index on y-axis (y_end not included)
  if (Xend >= ESP_PANEL_LCD_WIDTH)
    Xend = ESP_PANEL_LCD_WIDTH;
  if (Yend >= ESP_PANEL_LCD_HEIGHT)
    Yend = ESP_PANEL_LCD_HEIGHT;

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


// backlight
uint8_t LCD_Backlight = 50;
void Backlight_Init()
{
  ledcAttach(LCD_Backlight_PIN, Frequency, Resolution);
  Set_Backlight(LCD_Backlight);      //0~100
}

void Set_Backlight(uint8_t Light)                        //
{
  if(Light > Backlight_MAX || Light < 0)
    printf("Set Backlight parameters in the range of 0 to 100 \r\n");
  else{
    uint32_t Backlight = Light*10;
    if(Backlight == 1000)
      Backlight = 1024;
    ledcWrite(LCD_Backlight_PIN, Backlight);
  }
}
