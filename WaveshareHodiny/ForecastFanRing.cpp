// Prstenec předpovědi kolem kulatého ciferníku (viz ForecastFanRing.h).
// Postupné kreslení a přepočet jen změněných výseků vychází z upstreamu
// CooLajz/waveshare-hodiny (ForecastDial.cpp, ForecastFanRegion.h, MIT).

#include "ForecastFanRing.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>
#include <new>

namespace {

constexpr int SIZE = 480;
constexpr int CENTER = 240;
constexpr int INNER = FORECAST_FAN_RING_INNER;
constexpr int OUTER = FORECAST_FAN_RING_OUTER;
constexpr int ROWS = 2 * OUTER + 1;

lv_obj_t *ring = nullptr;

// Pixely mezikruží po řádcích: nejdřív levý úsek, pak pravý (nebo jeden
// souvislý přes střed). rowStart[r] je index prvního pixelu řádku dy = r - OUTER.
lv_color_t *pixels = nullptr;
uint32_t *rowStart = nullptr;
int16_t *rowInner = nullptr;
int16_t *rowOuter = nullptr;
size_t pixelCount = 0;

// Klíče mají po sto bajtech; patří do PSRAM k pixelům, ne do interní RAM.
struct Keys {
  ForecastFanKey rendered;
  ForecastFanKey target;
};
Keys *keys = nullptr;
bool initialized = false;
bool pending = false;
uint32_t pendingMask = 0;
int pendingRow = 0;
ForecastFanBox pendingBox = {0, 0, 0, 0};

bool allocate() {
  if (pixels != nullptr) return true;
  pixelCount = forecastFanPixelCount(INNER, OUTER);
  pixels = static_cast<lv_color_t *>(heap_caps_malloc(
      pixelCount * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  rowStart = static_cast<uint32_t *>(heap_caps_malloc(
      (ROWS + 1) * sizeof(uint32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  rowInner = static_cast<int16_t *>(heap_caps_malloc(
      ROWS * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  rowOuter = static_cast<int16_t *>(heap_caps_malloc(
      ROWS * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (keys == nullptr) {
    void *memory = heap_caps_malloc(sizeof(Keys), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (memory != nullptr) keys = new (memory) Keys();
  }
  if (pixels == nullptr || rowStart == nullptr || rowInner == nullptr ||
      rowOuter == nullptr || keys == nullptr) {
    forecastFanRingRelease();
    return false;
  }
  uint32_t index = 0;
  for (int r = 0; r < ROWS; ++r) {
    int innerHalf = 0, outerHalf = 0;
    rowStart[r] = index;
    index += forecastFanRow(INNER, OUTER, r - OUTER, innerHalf, outerHalf);
    rowInner[r] = static_cast<int16_t>(innerHalf);
    rowOuter[r] = static_cast<int16_t>(outerHalf);
  }
  rowStart[ROWS] = index;
  memset(pixels, 0, pixelCount * sizeof(lv_color_t));
  initialized = false;
  Serial.printf("[fan] Prstenec predpovedi: %u px, %u B PSRAM\n",
                static_cast<unsigned>(pixelCount),
                static_cast<unsigned>(forecastFanRingBytes()));
  return true;
}

// Index pixelu dx v řádku r, nebo -1 mimo mezikruží.
int pixelIndex(int r, int dx) {
  const int xo = rowOuter[r];
  const int xi = rowInner[r];
  if (xo < 0 || dx < -xo || dx > xo) return -1;
  if (xi == 0) return static_cast<int>(rowStart[r]) + dx + xo;
  if (dx > -xi && dx < xi) return -1;
  const int leftLength = xo - xi + 1;
  if (dx <= -xi) return static_cast<int>(rowStart[r]) + dx + xo;
  return static_cast<int>(rowStart[r]) + leftLength + dx - xi;
}

ForecastFanBox maskBox(uint32_t mask, int sectors) {
  ForecastFanBox box = {OUTER, OUTER, -OUTER, -OUTER};
  for (int s = 0; s < sectors; ++s) {
    if (!(mask & (1u << s))) continue;
    const ForecastFanBox part = forecastFanSectorBox(sectors, s, INNER, OUTER);
    box.x1 = std::min(box.x1, part.x1);
    box.y1 = std::min(box.y1, part.y1);
    box.x2 = std::max(box.x2, part.x2);
    box.y2 = std::max(box.y2, part.y2);
  }
  return box;
}

void invalidateBox(const ForecastFanBox &box) {
  if (ring == nullptr || box.x1 > box.x2) return;
  lv_area_t area;
  lv_obj_get_coords(ring, &area);
  const lv_coord_t left = area.x1 + CENTER;
  const lv_coord_t top = area.y1 + CENTER;
  area = {static_cast<lv_coord_t>(left + box.x1 - 2),
          static_cast<lv_coord_t>(top + box.y1 - 2),
          static_cast<lv_coord_t>(left + box.x2 + 2),
          static_cast<lv_coord_t>(top + box.y2 + 2)};
  lv_obj_invalidate_area(ring, &area);
}

// Značka "teď" přes celou šířku mezikruží.
ForecastFanBox nowMarkerBox(const ForecastFanKey &key) {
  const float radians = forecastFanNowAngle(key) * 3.14159265f / 180.0f;
  const float s = std::sin(radians), c = -std::cos(radians);
  const int x1 = static_cast<int>(std::lround(s * (INNER - 2)));
  const int y1 = static_cast<int>(std::lround(c * (INNER - 2)));
  const int x2 = static_cast<int>(std::lround(s * OUTER));
  const int y2 = static_cast<int>(std::lround(c * OUTER));
  return {std::min(x1, x2) - 3, std::min(y1, y2) - 3, std::max(x1, x2) + 3,
          std::max(y1, y2) + 3};
}

void drawEvent(lv_event_t *event) {
  if (pixels == nullptr || !initialized) return;
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(event);
  lv_area_t coords;
  lv_obj_get_coords(ring, &coords);
  const int cx = coords.x1 + CENTER;
  const int cy = coords.y1 + CENTER;
  const lv_area_t *clip = ctx->clip_area;
  const lv_coord_t stride = lv_area_get_width(ctx->buf_area);
  auto *buffer = static_cast<lv_color_t *>(ctx->buf);
  const int firstRow = std::max<int>(clip->y1 - cy + OUTER, 0);
  const int lastRow = std::min<int>(clip->y2 - cy + OUTER, ROWS - 1);
  for (int r = firstRow; r <= lastRow; ++r) {
    const int xo = rowOuter[r];
    if (xo < 0) continue;
    const int xi = rowInner[r];
    const int y = cy + r - OUTER;
    lv_color_t *row = buffer + (y - ctx->buf_area->y1) * stride - ctx->buf_area->x1;
    auto copy = [&](int from, int to, uint32_t source) {
      const int x1 = std::max<int>(cx + from, clip->x1);
      const int x2 = std::min<int>(cx + to, clip->x2);
      if (x1 > x2) return;
      memcpy(row + x1, pixels + source + (x1 - cx - from),
             static_cast<size_t>(x2 - x1 + 1) * sizeof(lv_color_t));
    };
    if (xi == 0) {
      copy(-xo, xo, rowStart[r]);
    } else {
      copy(-xo, -xi, rowStart[r]);
      copy(xi, xo, rowStart[r] + (xo - xi + 1));
    }
  }
  if (!keys->rendered.valid) return;
  // Značka "teď": černý stín a světlá čárka přes prstenec.
  const float radians = forecastFanNowAngle(keys->rendered) * 3.14159265f / 180.0f;
  const float s = std::sin(radians), c = -std::cos(radians);
  lv_point_t from = {static_cast<lv_coord_t>(cx + std::lround(s * (INNER - 2))),
                     static_cast<lv_coord_t>(cy + std::lround(c * (INNER - 2)))};
  lv_point_t to = {static_cast<lv_coord_t>(cx + std::lround(s * OUTER)),
                   static_cast<lv_coord_t>(cy + std::lround(c * OUTER))};
  lv_draw_line_dsc_t line;
  lv_draw_line_dsc_init(&line);
  line.width = 4;
  line.color = lv_color_black();
  lv_draw_line(ctx, &line, &from, &to);
  line.width = 2;
  line.color = lv_color_make(246, 246, 246);
  lv_draw_line(ctx, &line, &from, &to);
}

}  // namespace

lv_obj_t *forecastFanRingCreate(lv_obj_t *parent) {
  if (ring != nullptr) return ring;
  ring = lv_obj_create(parent);
  lv_obj_remove_style_all(ring);
  lv_obj_set_size(ring, SIZE, SIZE);
  lv_obj_set_pos(ring, 0, 0);
  lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(ring, drawEvent, LV_EVENT_DRAW_MAIN, nullptr);
  lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN);
  return ring;
}

lv_obj_t *forecastFanRingObject() { return ring; }

void forecastFanRingSetKey(const ForecastFanKey &key) {
  if (!allocate()) return;
  if (pending ? key == keys->target : (initialized && key == keys->rendered)) return;
  // Rozpracovaný přepočet s jiným cílem: pixely uvnitř staré masky můžou nést
  // směs obou klíčů, přepočítají se proto také.
  const uint32_t mask =
      forecastFanDirtySectors(pending ? keys->target : keys->rendered, key,
                              initialized && (!pending || key.sectors == keys->target.sectors)) |
      (pending ? pendingMask : 0u);
  keys->target = key;
  pendingMask = mask;
  pendingBox = maskBox(mask, key.sectors);
  pendingRow = pendingBox.y1;
  pending = mask != 0;
  if (!pending) keys->rendered = key;
}

bool forecastFanRingMaintain(bool visible, int rowBudget) {
  if (!pending || pixels == nullptr) return true;
  const int lastRow = visible ? pendingBox.y2 : std::min(pendingBox.y2, pendingRow + rowBudget - 1);
  for (int dy = pendingRow; dy <= lastRow; ++dy) {
    const int r = dy + OUTER;
    if (r < 0 || r >= ROWS || rowOuter[r] < 0) continue;
    const int fromX = std::max<int>(pendingBox.x1, -rowOuter[r]);
    const int toX = std::min<int>(pendingBox.x2, rowOuter[r]);
    for (int dx = fromX; dx <= toX; ++dx) {
      const int index = pixelIndex(r, dx);
      if (index < 0) {
        // Uvnitř díry rovnou na pravý úsek.
        if (dx < 0 && rowInner[r] > 0) dx = rowInner[r] - 1;
        continue;
      }
      const float angle = forecastFanAngle(dx, dy);
      int sector = static_cast<int>(angle * keys->target.sectors / 360.0f);
      if (sector >= keys->target.sectors) sector = keys->target.sectors - 1;
      if (initialized && !(pendingMask & (1u << sector))) continue;
      const uint32_t color =
          forecastFanPixelAt(keys->target, angle, CENTER + dx, CENTER + dy);
      pixels[index] = lv_color_make((color >> 16) & 255, (color >> 8) & 255,
                                    color & 255);
    }
  }
  pendingRow = lastRow + 1;
  if (pendingRow <= pendingBox.y2) return false;
  const ForecastFanBox changed = pendingBox;
  const ForecastFanBox oldMarker = nowMarkerBox(keys->rendered);
  const bool hadMarker = initialized && keys->rendered.valid;
  keys->rendered = keys->target;
  pending = false;
  pendingMask = 0;
  initialized = true;
  invalidateBox(changed);
  if (hadMarker) invalidateBox(oldMarker);
  if (keys->rendered.valid) invalidateBox(nowMarkerBox(keys->rendered));
  return true;
}

void forecastFanRingRelease() {
  if (pixels != nullptr) heap_caps_free(pixels);
  if (rowStart != nullptr) heap_caps_free(rowStart);
  if (rowInner != nullptr) heap_caps_free(rowInner);
  if (rowOuter != nullptr) heap_caps_free(rowOuter);
  pixels = nullptr;
  rowStart = nullptr;
  rowInner = nullptr;
  rowOuter = nullptr;
  pixelCount = 0;
  initialized = false;
  pending = false;
  pendingMask = 0;
  if (ring != nullptr) lv_obj_invalidate(ring);
}

size_t forecastFanRingBytes() {
  if (pixels == nullptr) return 0;
  return pixelCount * sizeof(lv_color_t) + (ROWS + 1) * sizeof(uint32_t) +
         2 * ROWS * sizeof(int16_t);
}
