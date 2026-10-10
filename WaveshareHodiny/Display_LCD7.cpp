#include "Board.h"
#if HODINY_BOARD_LCD7

// Displej, dotyk a expandér desky ESP32-S3-Touch-LCD-7 (800 x 480).
// Časování a piny odpovídají Waveshare demu a knihovně ESP32_Display_Panel;
// ověřeno bring-up sketchem na desce pod zátěží Wi-Fi a TLS (2026-10-10).

#include <Arduino.h>
#include <Wire.h>

#include "BoardDisplay.h"
#include "I2C_Driver.h"
#include "RgbPanel.h"

namespace {
constexpr int LCD_HSYNC_PIN = 46;
constexpr int LCD_VSYNC_PIN = 3;
constexpr int LCD_DE_PIN = 5;
constexpr int LCD_PCLK_PIN = 7;
// Pořadí esp_lcd pro RGB565: data0..4 = B3..B7, data5..10 = G2..G7,
// data11..15 = R3..R7.
constexpr int LCD_DATA_PINS[16] = {14, 38, 18, 17, 10, 39, 0,  45,
                                   48, 47, 21, 1,  2,  42, 41, 40};
// Při 12 MHz panel po pár minutách přešel do vlastního testu (střídání
// plných barev). Nižší takt posun obrazu při práci radaru neodstranil
// (14 MHz: stejně posunů jako 16 MHz).
constexpr uint32_t LCD_PIXEL_CLOCK_HZ = 16 * 1000 * 1000;
// Šestnáct řádků (2 x 25 kB vnitřní RAM, 480 = 30 x 16). Deset stačilo
// v bring-up testu, ale ne při dekódování radaru na druhém jádře: doplnění
// nestihlo přístup do PSRAM a obraz se posouval.
constexpr int LCD_BOUNCE_ROWS = 16;

constexpr int TOUCH_INT_PIN = 4;

// CH422G nemá registry: každá funkce má vlastní I2C adresu.
constexpr uint8_t CH422G_MODE_ADDR = 0x24;  // bit0 = EXIO0-7 jako výstupy
constexpr uint8_t CH422G_OUTPUT_ADDR = 0x38;  // úrovně EXIO0-7
constexpr uint8_t EXIO_TOUCH_RST = 1 << 1;
constexpr uint8_t EXIO_BACKLIGHT = 1 << 2;
constexpr uint8_t EXIO_LCD_RST = 1 << 3;
constexpr uint8_t EXIO_SD_CS = 1 << 4;
// EXIO5 nízko = nativní USB, vysoko = CAN. Všechny výstupy naráz vysoko by
// nativní USB odpojily.
constexpr uint8_t EXIO_USB_SEL = 1 << 5;

// Adresu 0x5D zvolí nízká úroveň INT během resetu.
constexpr uint8_t GT911_ADDR = 0x5D;
constexpr uint16_t GT911_REG_STATUS = 0x814E;
constexpr uint16_t GT911_REG_POINT1 = 0x8150;
constexpr uint16_t GT911_REG_PRODUCT_ID = 0x8140;

uint8_t exioState = 0;

bool ch422gWrite(uint8_t address, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool exioSet(uint8_t mask, bool high) {
  exioState = high ? (exioState | mask) : (exioState & ~mask);
  return ch422gWrite(CH422G_OUTPUT_ADDR, exioState);
}

bool gt911Read(uint16_t reg, uint8_t *data, size_t length) {
  Wire.beginTransmission(GT911_ADDR);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(GT911_ADDR, length) != length) return false;
  for (size_t i = 0; i < length; ++i) data[i] = Wire.read();
  return true;
}

bool gt911Write(uint16_t reg, uint8_t value) {
  Wire.beginTransmission(GT911_ADDR);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

void touchInit() {
  pinMode(TOUCH_INT_PIN, OUTPUT);
  digitalWrite(TOUCH_INT_PIN, LOW);
  exioSet(EXIO_TOUCH_RST, false);
  delay(10);
  exioSet(EXIO_TOUCH_RST, true);
  delay(60);
  pinMode(TOUCH_INT_PIN, INPUT);
  delay(50);
  uint8_t productId[4] = {0};
  if (!gt911Read(GT911_REG_PRODUCT_ID, productId, sizeof(productId))) {
    ESP_LOGE("touch", "GT911 neodpovida");
  }
}

void panelInit() {
  esp_lcd_rgb_panel_config_t config = {};
  config.clk_src = LCD_CLK_SRC_DEFAULT;
  config.timings.pclk_hz = LCD_PIXEL_CLOCK_HZ;
  config.timings.h_res = SCREEN_WIDTH;
  config.timings.v_res = SCREEN_HEIGHT;
  config.timings.hsync_pulse_width = 4;
  config.timings.hsync_back_porch = 8;
  config.timings.hsync_front_porch = 8;
  config.timings.vsync_pulse_width = 4;
  config.timings.vsync_back_porch = 8;
  config.timings.vsync_front_porch = 8;
  config.timings.flags.pclk_active_neg = 1;
  config.data_width = 16;
  config.bits_per_pixel = 16;
  config.num_fbs = 2;
  config.bounce_buffer_size_px = SCREEN_WIDTH * LCD_BOUNCE_ROWS;
  config.psram_trans_align = 64;
  config.hsync_gpio_num = LCD_HSYNC_PIN;
  config.vsync_gpio_num = LCD_VSYNC_PIN;
  config.de_gpio_num = LCD_DE_PIN;
  config.pclk_gpio_num = LCD_PCLK_PIN;
  config.disp_gpio_num = -1;
  for (int i = 0; i < 16; ++i) config.data_gpio_nums[i] = LCD_DATA_PINS[i];
  config.flags.fb_in_psram = 1;
  config.flags.double_fb = 1;
  rgbPanelCreate(config);
}
}  // namespace

void boardInit() {
  I2C_Init();
  // Dotyk a LCD mimo reset, SD karta odpojená, nativní USB místo CAN.
  // Podsvícení zůstane zhasnuté, dokud se nevykreslí první snímek.
  ch422gWrite(CH422G_MODE_ADDR, 0x01);
  exioState = EXIO_TOUCH_RST | EXIO_LCD_RST | EXIO_SD_CS;
  ch422gWrite(CH422G_OUTPUT_ADDR, exioState);
}

void LCD_Init() {
  panelInit();
  touchInit();
}

// Panel nemá příkaz pro spánek; tmu zajistí vypnuté podsvícení a obraz běží
// dál, aby po probuzení nebylo co resynchronizovat.
void LCD_Sleep() {}

void LCD_Wake() { LCD_Resync(); }

void Set_Backlight(uint8_t Light) { exioSet(EXIO_BACKLIGHT, Light > 0); }

bool boardTouchRead(uint16_t &x, uint16_t &y) {
  uint8_t status = 0;
  if (!gt911Read(GT911_REG_STATUS, &status, 1)) return false;
  // Bit 7: řadič má nová data. Bez něj jsou body z minulého čtení.
  if ((status & 0x80) == 0) return false;
  const uint8_t points = status & 0x0F;
  bool pressed = false;
  if (points > 0 && points <= 5) {
    uint8_t point[4];
    if (gt911Read(GT911_REG_POINT1, point, sizeof(point))) {
      x = point[0] | (point[1] << 8);
      y = point[2] | (point[3] << 8);
      pressed = x < SCREEN_WIDTH && y < SCREEN_HEIGHT;
    }
  }
  gt911Write(GT911_REG_STATUS, 0);
  return pressed;
}

#endif  // HODINY_BOARD_LCD7
