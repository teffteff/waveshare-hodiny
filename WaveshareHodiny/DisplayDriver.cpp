#include "DisplayDriver.h"

#include <esp_heap_caps.h>
#include <esp_timer.h>

#include <cstring>

#include "Display_ST7701.h"
#include "FirmwareBuild.h"
#include "Touch_CST820.h"

namespace {
lv_disp_draw_buf_t drawBuffer;
lv_disp_drv_t displayDriver;

void *frameBuffer1 = nullptr;
void *frameBuffer2 = nullptr;
uint8_t *screenshotBuffer = nullptr;
size_t screenshotOffset = 0;
int8_t screenHoldPending = 0;
int8_t rangeSwipePending = 0;
bool shortTapPending = false;
// Kam se klepnulo. Radar letadel podle toho vybírá letadlo, takže samo "někdo
// klepl" nestačí.
uint16_t shortTapX = 0;
uint16_t shortTapY = 0;
bool doubleTapPending = false;
// Klepnutí, které ještě čeká, jestli z něj nebude dvojklepnutí. Hlásí se až po
// vypršení okna, takže se jedno gesto nikdy nezapočítá dvakrát.
bool heldTapPending = false;
uint16_t heldTapX = 0;
uint16_t heldTapY = 0;
uint32_t heldTapAt = 0;
bool touchDown = false;
uint16_t touchStartX = 0;
uint16_t touchStartY = 0;
uint16_t touchLastX = 0;
uint16_t touchLastY = 0;
uint32_t touchStartedAt = 0;
uint32_t touchLastSeenAt = 0;
uint8_t partialRefreshWarmupFrames = 0;
bool partialRefreshWarmupRequested = false;
bool partialRefreshEnableRequested = false;

constexpr size_t FRAMEBUFFER_BYTES = 480 * 480 * sizeof(lv_color_t);
constexpr size_t SCREENSHOT_CHUNK_BYTES = 2048;

// Řadič občas jeden vzorek vynechá; prázdné čtení uprostřed tahu tedy není
// zvednutý prst. Gesto ukončíme teprve po této době ticha, jinak by se jeden
// tah rozpadl na několik klepnutí.
constexpr uint32_t TOUCH_RELEASE_MS = 60;
// Podržení prstu na místě. Kratší dotyk je klepnutí.
constexpr uint32_t TOUCH_HOLD_MS = 500;
// Do jaké vzdálenosti se dotyk ještě považuje za dotyk na jednom místě.
constexpr int32_t TOUCH_STILL_PX = 60;
// Přetažení musí být dost dlouhé v jedné ose, málo šikmé a rychlé.
constexpr int32_t TOUCH_SWIPE_PX = 70;
constexpr int32_t TOUCH_SWIPE_CROSS_PX = 90;
constexpr uint32_t TOUCH_SWIPE_MS = 700;
// Svislá osa displeje; podržení vlevo od ní znamená zpět, vpravo vpřed.
constexpr int32_t TOUCH_MIDDLE_X = 240;
// Do kdy po prvním klepnutí musí dorazit druhé, aby z nich bylo dvojklepnutí.
// Měří se od posledního vzorku prvního dotyku po poslední vzorek druhého, takže
// se do okna vejde i doba, po kterou prst leží podruhé - proto je delší, než
// jak dlouhá pauza mezi klepnutími vypadá. Po celou tu dobu se první klepnutí
// drží, jinak by dvojklepnutí na radaru letadel stihlo vybrat letadlo dřív, než
// se pozná jako dvojklepnutí.
constexpr uint32_t TOUCH_DOUBLE_TAP_MS = 400;
// Jak daleko od prvního klepnutí smí druhé dopadnout. Volnější než dotyk na
// jednom místě: mezi dvěma klepnutími se prst zvedá a vrací.
constexpr int32_t TOUCH_DOUBLE_TAP_PX = 90;
// Hranice dlouhého stisku pro LVGL. Musí padnout přesně tam, kde si dotyk
// přebírá podržení, jinak by se na jedno gesto stalo dvakrát: LVGL posílá
// LV_EVENT_SHORT_CLICKED jen do své hranice, ale my držíme stisk ještě
// TOUCH_RELEASE_MS po posledním vzorku, takže LVGL vidí dotyk vždy o tuhle
// dobu delší. Bez zarovnání by podržení nad tlačítkem v nastavení současně
// přepnulo obrazovku i zmáčklo tlačítko pod prstem - a jedním z nich je
// aktualizace firmwaru. LVGL porovnává ostrým ">", proto o milisekundu níž.
constexpr uint16_t TOUCH_LVGL_LONG_PRESS_MS =
    TOUCH_HOLD_MS + TOUCH_RELEASE_MS - 1;

void flushDisplay(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels) {
  // V direct mode může LVGL zavolat flush pro několik samostatných
  // invalidovaných oblastí téhož snímku. Fyzický framebuffer přepneme až po
  // vykreslení poslední z nich; jinak by LCD zobrazilo rozpracovaný mezistav a
  // čekání na každý dílčí flush by zbytečně blokovalo hlavní smyčku.
  if (!lv_disp_flush_is_last(driver)) {
    lv_disp_flush_ready(driver);
    return;
  }

  // Oba draw buffery jsou přímo fyzické framebuffery RGB panelu. I při
  // částečném LVGL renderu proto panelu předáváme začátek celého hotového
  // framebufferu; area popisuje pouze oblast, kterou LVGL uvnitř něj změnilo.
  const bool framePresented = LCD_addWindow(
      0, 0, 479, 479, reinterpret_cast<uint8_t *>(&pixels->full));
  if (!framePresented) {
    // Při chybě zachováme LVGL živé; následný resync obnoví RGB DMA bez
    // předstírání, že čekání na bezpečné uvolnění framebufferu uspělo.
    LCD_Resync();
  }

  if (framePresented && partialRefreshWarmupFrames > 0) {
    // Direct mode předpokládá, že oba framebuffery obsahují stejný výchozí
    // snímek. Než jej zapneme, necháme LVGL oba buffery po jednom kompletně
    // vyrenderovat. Vyhneme se tak kopírování do framebufferu, který může panel
    // právě číst, a tedy i jednorázovému roztržení obrazu při přepnutí.
    --partialRefreshWarmupFrames;
    if (partialRefreshWarmupFrames == 0) {
      // Mezi dvěma plnými zahřívacími snímky může přeskočit sekunda nebo se
      // změnit jiný dynamický obsah. Oba buffery by pak před zapnutím LVGL
      // direct mode nebyly totožné a panel by mohl krátce střídat dvě polohy
      // ručičky. Po dokončení fyzického snímku už panel předchozí framebuffer
      // nečte, proto jej bezpečně sjednotíme s právě dokončeným obrazem.
      void *otherBuffer = nullptr;
      if (pixels == frameBuffer1)
        otherBuffer = frameBuffer2;
      else if (pixels == frameBuffer2)
        otherBuffer = frameBuffer1;
      if (otherBuffer == nullptr) {
        partialRefreshWarmupFrames = 2;
        partialRefreshWarmupRequested = true;
      } else {
        memcpy(otherBuffer, pixels, FRAMEBUFFER_BYTES);
        partialRefreshEnableRequested = true;
      }
    } else {
      partialRefreshWarmupRequested = true;
    }
  }
  lv_disp_flush_ready(driver);
}

// Zadržené klepnutí projde dál jako samostatné, jakmile je jisté, že druhé
// nepřijde.
void releaseHeldTap() {
  if (!heldTapPending) return;
  heldTapPending = false;
  shortTapPending = true;
  shortTapX = heldTapX;
  shortTapY = heldTapY;
}

void expireHeldTap(uint32_t now) {
  if (heldTapPending && now - heldTapAt > TOUCH_DOUBLE_TAP_MS) releaseHeldTap();
}

// Druhé klepnutí blízko prvního a včas znamená dvojklepnutí; cokoliv jiného je
// nové samostatné klepnutí, které se zase zadrží.
void registerTap(uint16_t x, uint16_t y, uint32_t at) {
  if (heldTapPending && at - heldTapAt <= TOUCH_DOUBLE_TAP_MS &&
      abs(static_cast<int32_t>(x) - heldTapX) <= TOUCH_DOUBLE_TAP_PX &&
      abs(static_cast<int32_t>(y) - heldTapY) <= TOUCH_DOUBLE_TAP_PX) {
    heldTapPending = false;
    doubleTapPending = true;
    return;
  }
  releaseHeldTap();
  heldTapPending = true;
  heldTapX = x;
  heldTapY = y;
  heldTapAt = at;
}

// Gesto se pozná až po zvednutí prstu z celého tahu, ne z gestového registru
// CST820. Ten hlásí směr už v průběhu tahu a při každém dalším čtení znovu, což
// se muselo zamykat, a krátké tahy po zaobleném displeji často propásl. Tady
// se drží jen začátek a konec tahu a rozhodne se jednou.
void classifyTouchGesture() {
  const int32_t dx = static_cast<int32_t>(touchLastX) - touchStartX;
  const int32_t dy = static_cast<int32_t>(touchLastY) - touchStartY;
  const uint32_t duration = touchLastSeenAt - touchStartedAt;
  const bool stillFinger =
      abs(dx) < TOUCH_STILL_PX && abs(dy) < TOUCH_STILL_PX;

  if (duration <= TOUCH_SWIPE_MS && abs(dx) >= TOUCH_SWIPE_PX &&
      abs(dy) <= TOUCH_SWIPE_CROSS_PX) {
    // Tažení doleva rozsah oddálí, doprava přiblíží.
    rangeSwipePending = dx < 0 ? 1 : -1;
  } else if (duration <= TOUCH_SWIPE_MS && abs(dy) >= TOUCH_SWIPE_PX &&
             abs(dx) <= TOUCH_SWIPE_CROSS_PX) {
    rangeSwipePending = dy < 0 ? -1 : 1;
  } else if (stillFinger && duration >= TOUCH_HOLD_MS) {
    // Podržení v levé polovině vrací zpět, v pravé jde vpřed.
    screenHoldPending = touchLastX < TOUCH_MIDDLE_X ? -1 : 1;
  } else if (stillFinger) {
    registerTap(touchLastX, touchLastY, touchLastSeenAt);
  }
}

void readTouch(lv_indev_drv_t *, lv_indev_data_t *data) {
  Touch_Read_Data();
  const uint32_t now = millis();
  expireHeldTap(now);

  // Přenos po I2C může uspět a přesto vrátit nesmysl, typicky samé 0xFF.
  // Dekóduje se jako dotyk daleko mimo panel; takový vzorek zahodíme, jinak by
  // uprostřed tahu posunul jeho konec a gesto by vyšlo úplně jinak.
  const bool validSample =
      touch_data.points > 0 && touch_data.x < 480 && touch_data.y < 480;

  if (validSample) {
    if (!touchDown) {
      touchDown = true;
      touchStartX = touch_data.x;
      touchStartY = touch_data.y;
      touchStartedAt = now;
    }
    touchLastX = touch_data.x;
    touchLastY = touch_data.y;
    touchLastSeenAt = now;
  } else if (touchDown && now - touchLastSeenAt >= TOUCH_RELEASE_MS) {
    touchDown = false;
    classifyTouchGesture();
  }

  // LVGL dostává dotyk beze změny, aby nastavení na displeji zůstalo plně
  // ovladatelné. Během krátkého ticha po vzorku držíme poslední souřadnici
  // stisknutou, jinak by se jeden tah po stránce rozpadl na několik stisků.
  if (touchDown) {
    data->point.x = touchLastX;
    data->point.y = touchLastY;
    data->state = LV_INDEV_STATE_PR;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }

  touch_data.points = 0;
  touch_data.gesture = NONE;
}

void increaseTick(void *) {
  lv_tick_inc(2);
}
}  // namespace

void displayDriverInit() {
  lv_init();

  ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(
      panel_handle, 2, &frameBuffer1, &frameBuffer2));
#if !FIRMWARE_RELEASE
  screenshotBuffer = static_cast<uint8_t *>(heap_caps_malloc(
      FRAMEBUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif
  lv_disp_draw_buf_init(&drawBuffer, frameBuffer1, frameBuffer2, 480 * 480);

  lv_disp_drv_init(&displayDriver);
  displayDriver.hor_res = 480;
  displayDriver.ver_res = 480;
  displayDriver.flush_cb = flushDisplay;
  displayDriver.full_refresh = 1;
  displayDriver.draw_buf = &drawBuffer;
  lv_disp_drv_register(&displayDriver);

  static lv_indev_drv_t inputDriver;
  lv_indev_drv_init(&inputDriver);
  inputDriver.type = LV_INDEV_TYPE_POINTER;
  inputDriver.read_cb = readTouch;
  inputDriver.long_press_time = TOUCH_LVGL_LONG_PRESS_MS;
  lv_indev_drv_register(&inputDriver);

  const esp_timer_create_args_t tickTimerArgs = {
      .callback = increaseTick,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "lvgl-tick",
      .skip_unhandled_events = true,
  };
  esp_timer_handle_t tickTimer = nullptr;
  ESP_ERROR_CHECK(esp_timer_create(&tickTimerArgs, &tickTimer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(tickTimer, 2000));
}

void displayDriverLoop() {
  lv_timer_handler();
  if (partialRefreshEnableRequested) {
    partialRefreshEnableRequested = false;
    displayDriver.full_refresh = 0;
    displayDriver.direct_mode = 1;
  }
  if (partialRefreshWarmupRequested) {
    partialRefreshWarmupRequested = false;
    displayDriverRefresh();
  }
}

void displayDriverRefresh() {
  lv_obj_invalidate(lv_scr_act());
}

void displayDriverSetPartialRefresh(bool enabled, bool rebuildBuffers) {
  if (enabled) {
    if (!rebuildBuffers &&
        (displayDriver.direct_mode || partialRefreshWarmupFrames > 0)) {
      return;
    }
    partialRefreshWarmupFrames = 2;
    partialRefreshWarmupRequested = false;
    partialRefreshEnableRequested = false;
    displayDriver.direct_mode = 0;
    displayDriver.full_refresh = 1;
  } else {
    if (!displayDriver.direct_mode && partialRefreshWarmupFrames == 0) return;
    partialRefreshWarmupFrames = 0;
    partialRefreshWarmupRequested = false;
    partialRefreshEnableRequested = false;
    displayDriver.direct_mode = 0;
    displayDriver.full_refresh = 1;
  }
  displayDriverRefresh();
}

int8_t displayDriverTakeScreenHold() {
  const int8_t direction = screenHoldPending;
  screenHoldPending = 0;
  return direction;
}

bool displayDriverTakeDoubleTap() {
  const bool pending = doubleTapPending;
  doubleTapPending = false;
  return pending;
}

int8_t displayDriverTakeRangeSwipe() {
  const int8_t direction = rangeSwipePending;
  rangeSwipePending = 0;
  return direction;
}

bool displayDriverTakeShortTap(int16_t &x, int16_t &y) {
  // Hlavní smyčka se ptá častěji, než LVGL čte dotyk, takže okno dvojklepnutí
  // vyprší i tady - jinak by klepnutí čekalo na první pohyb prstu.
  expireHeldTap(millis());
  if (!shortTapPending) return false;
  shortTapPending = false;
  x = static_cast<int16_t>(shortTapX);
  y = static_cast<int16_t>(shortTapY);
  return true;
}

bool displayDriverBeginFramebufferCapture(Print &output) {
  if (drawBuffer.buf1 == nullptr || drawBuffer.buf2 == nullptr ||
      screenshotBuffer == nullptr) {
    return false;
  }

  // LVGL kreslí do buf_act; druhý plný framebuffer je právě zobrazený panelem.
  const void *displayedBuffer = drawBuffer.buf_act == drawBuffer.buf1
                                    ? drawBuffer.buf2
                                    : drawBuffer.buf1;
  memcpy(screenshotBuffer, displayedBuffer, FRAMEBUFFER_BYTES);
  screenshotOffset = 0;

  output.println("WSFB1 480 480 RGB565LE 460800");
  return true;
}

bool displayDriverStreamFramebufferChunk(Print &output) {
  if (screenshotBuffer == nullptr || screenshotOffset >= FRAMEBUFFER_BYTES) {
    return true;
  }

  const size_t count =
      min(SCREENSHOT_CHUNK_BYTES, FRAMEBUFFER_BYTES - screenshotOffset);
  // USB CDC může při souběhu s animací přijmout jen část 2kB bloku. Dříve se
  // částečný zápis považoval za dokončený přenos, takže na hostiteli chyběl
  // konec framebufferu. Posuneme se pouze o skutečně přijaté bajty a zbytek
  // stejného bloku odešleme v některém z dalších průchodů hlavní smyčkou.
  screenshotOffset +=
      output.write(screenshotBuffer + screenshotOffset, count);
  return screenshotOffset >= FRAMEBUFFER_BYTES;
}
