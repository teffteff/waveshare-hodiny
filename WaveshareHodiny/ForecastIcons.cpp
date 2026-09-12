// Adapted from MeteoPlaneRadar/MeteoPlaneRadar/WxIcon.cpp and WxIcon.h.
// Copyright (c) 2026 Petr / chiptron.cz. MIT License; see THIRD_PARTY_NOTICES.md.
// Source revision: ee27c29b54ca8ffc42b65cd01c710de0b7b5e6a9
// Original shapes and WMO mapping; drawing primitives ported to LVGL 8.
#include "ForecastIcons.h"
#include <math.h>

namespace {
enum WxKind {
  WX_CLEAR, WX_PARTLY, WX_CLOUDY, WX_FOG, WX_DRIZZLE,
  WX_RAIN, WX_SNOW, WX_SHOWER, WX_STORM
};

const lv_color_t C_BLACK = lv_color_hex(0x000000);
const lv_color_t C_WHITE = lv_color_hex(0xffffff);
const lv_color_t C_YELLOW = lv_color_hex(0xffff00);
const lv_color_t C_GRAY = lv_color_hex(0x848284);
const lv_color_t C_DKGRAY = lv_color_hex(0x212421);
const lv_color_t C_CYAN = lv_color_hex(0x00beff);

// Draw directly into LVGL's current draw context: no per-row image buffers.
class IconPainter {
 public:
  IconPainter(lv_draw_ctx_t *context, bool redNight)
      : context_(context), redNight_(redNight) {}

  void fillRect(int x, int y, int w, int h, lv_color_t color,
                int radius = 0) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = tint(color);
    dsc.radius = radius;
    lv_area_t area = {static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(y),
                      static_cast<lv_coord_t>(x + w - 1),
                      static_cast<lv_coord_t>(y + h - 1)};
    lv_draw_rect(context_, &dsc, &area);
  }

  void fillCircle(int x, int y, int radius, lv_color_t color) {
    fillRect(x - radius, y - radius, 2 * radius + 1, 2 * radius + 1,
             color, LV_RADIUS_CIRCLE);
  }

  void drawLine(int x0, int y0, int x1, int y1, lv_color_t color) {
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = tint(color);
    lv_point_t start = point(x0, y0), end = point(x1, y1);
    lv_draw_line(context_, &dsc, &start, &end);
  }

  void drawFastHLine(int x, int y, int width, lv_color_t color) {
    fillRect(x, y, width, 1, color);
  }

  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2,
                    lv_color_t color) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = tint(color);
    lv_point_t points[] = {point(x0, y0), point(x1, y1), point(x2, y2)};
    lv_draw_polygon(context_, &dsc, points, 3);
  }

 private:
  static lv_point_t point(int x, int y) {
    return {static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(y)};
  }
  lv_color_t tint(lv_color_t color) const {
    // Keep the crescent's black cutout black in red night mode too.
    return redNight_ && color.full != C_BLACK.full
               ? lv_color_hex(0xff4848) : color;
  }
  lv_draw_ctx_t *context_;
  bool redNight_;
};

static WxKind WxIcon_Kind(int c) {
  switch (c) {
    case 0:               return WX_CLEAR;
    case 1: case 2:       return WX_PARTLY;
    case 3:               return WX_CLOUDY;
    case 45: case 48:     return WX_FOG;
    case 51: case 53: case 55:
    case 56: case 57:     return WX_DRIZZLE;
    case 61: case 63: case 65:
    case 66: case 67:     return WX_RAIN;
    case 71: case 73: case 75: case 77:
    case 85: case 86:     return WX_SNOW;
    case 80: case 81: case 82: return WX_SHOWER;
    case 95: case 96: case 99: return WX_STORM;
    default:              return WX_CLOUDY;
  }
}

// --- Primitives -------------------------------------------------------------
static void sun(IconPainter &gfx, int cx, int cy, int r, lv_color_t col) {
  gfx.fillCircle(cx, cy, r / 2, col);
  for (int i = 0; i < 8; i++) {
    float a = i * 0.7853981634f;                 // 45 deg apart
    int x0 = cx + (int)((r * 0.62f) * cosf(a));
    int y0 = cy + (int)((r * 0.62f) * sinf(a));
    int x1 = cx + (int)((r * 0.95f) * cosf(a));
    int y1 = cy + (int)((r * 0.95f) * sinf(a));
    gfx.drawLine(x0, y0, x1, y1, col);
  }
}

// Crescent: a filled disc with a second disc punched out of it in black. Only
// works because everything behind the icon is already black - which it is, the
// screens all start from fillScreen(C_BLACK).
static void moon(IconPainter &gfx, int cx, int cy, int r, lv_color_t col) {
  gfx.fillCircle(cx, cy, r / 2, col);
  gfx.fillCircle(cx + r / 4, cy - r / 5, r / 2, C_BLACK);
}

static void cloud(IconPainter &gfx, int cx, int cy, int r, lv_color_t col) {
  int w = r;                                     // half width of the body
  gfx.fillCircle(cx - w / 2, cy, r / 3, col);
  gfx.fillCircle(cx + w / 4, cy - r / 5, r / 2.6f, col);
  gfx.fillCircle(cx + w / 2, cy + r / 12, r / 3.4f, col);
  gfx.fillRect(cx - w / 2, cy, w, r / 3, col);
}

static void drops(IconPainter &gfx, int cx, int cy, int r, lv_color_t col, int n, bool heavy) {
  for (int i = 0; i < n; i++) {
    int x = cx - r / 2 + i * (r / (n > 1 ? (n - 1) : 1));
    gfx.drawLine(x, cy, x - r / 8, cy + r / 3, col);
    if (heavy) gfx.drawLine(x + 1, cy, x - r / 8 + 1, cy + r / 3, col);
  }
}

static void flakes(IconPainter &gfx, int cx, int cy, int r, lv_color_t col, int n) {
  for (int i = 0; i < n; i++) {
    int x = cx - r / 2 + i * (r / (n > 1 ? (n - 1) : 1));
    int y = cy + r / 6;
    int s = r / 7; if (s < 2) s = 2;
    gfx.drawLine(x - s, y, x + s, y, col);
    gfx.drawLine(x, y - s, x, y + s, col);
    gfx.drawLine(x - s / 2, y - s / 2, x + s / 2, y + s / 2, col);
    gfx.drawLine(x - s / 2, y + s / 2, x + s / 2, y - s / 2, col);
  }
}

static void bolt(IconPainter &gfx, int cx, int cy, int r, lv_color_t col) {
  int s = r / 3; if (s < 3) s = 3;
  gfx.fillTriangle(cx, cy - s, cx - s / 2, cy + s / 2, cx + s / 4, cy, col);
  gfx.fillTriangle(cx + s / 4, cy, cx - s / 4, cy + s, cx + s / 2, cy - s / 4, col);
}

// --- Composition ------------------------------------------------------------
void drawIcon(IconPainter &gfx, int cx, int cy, int r, int code, bool night) {
  if (r < 6) r = 6;
  const lv_color_t sunCol   = night ? C_WHITE : C_YELLOW;
  const lv_color_t cloudCol = C_GRAY;

  switch (WxIcon_Kind(code)) {
    case WX_CLEAR:
      if (night) moon(gfx, cx, cy, r, sunCol); else sun(gfx, cx, cy, r, sunCol);
      break;

    case WX_PARTLY:
      // Sun peeking out behind the cloud - drawn first so the cloud overlaps it.
      if (night) moon(gfx, cx - r / 4, cy - r / 4, (int)(r * 0.8f), sunCol);
      else       sun(gfx, cx - r / 4, cy - r / 4, (int)(r * 0.8f), sunCol);
      cloud(gfx, cx + r / 6, cy + r / 5, (int)(r * 0.75f), cloudCol);
      break;

    case WX_CLOUDY:
      cloud(gfx, cx, cy, r, cloudCol);
      break;

    case WX_FOG:
      cloud(gfx, cx, cy - r / 4, (int)(r * 0.8f), cloudCol);
      for (int i = 0; i < 3; i++) {
        int y = cy + r / 3 + i * (r / 5);
        int half = (i == 1) ? r / 2 : (int)(r * 0.4f);   // staggered, like haze
        gfx.drawFastHLine(cx - half, y, 2 * half, C_GRAY);
      }
      break;

    case WX_DRIZZLE:
      cloud(gfx, cx, cy - r / 5, (int)(r * 0.85f), cloudCol);
      drops(gfx, cx, cy + r / 3, r, C_CYAN, 3, false);
      break;

    case WX_RAIN:
      cloud(gfx, cx, cy - r / 5, (int)(r * 0.85f), cloudCol);
      drops(gfx, cx, cy + r / 3, r, C_CYAN, 4, true);
      break;

    case WX_SHOWER:
      // Same as rain but with the sun behind it - a shower is by definition
      // intermittent, and that is what tells them apart at a glance.
      if (night) moon(gfx, cx - r / 3, cy - r / 3, (int)(r * 0.7f), sunCol);
      else       sun(gfx, cx - r / 3, cy - r / 3, (int)(r * 0.7f), sunCol);
      cloud(gfx, cx + r / 6, cy - r / 8, (int)(r * 0.75f), cloudCol);
      drops(gfx, cx + r / 6, cy + r / 2, (int)(r * 0.8f), C_CYAN, 3, true);
      break;

    case WX_SNOW:
      cloud(gfx, cx, cy - r / 5, (int)(r * 0.85f), cloudCol);
      flakes(gfx, cx, cy + r / 4, r, C_WHITE, 3);
      break;

    case WX_STORM:
      cloud(gfx, cx, cy - r / 5, (int)(r * 0.85f), C_DKGRAY);
      bolt(gfx, cx, cy + r / 3, r, C_YELLOW);
      drops(gfx, cx - r / 3, cy + r / 3, (int)(r * 0.6f), C_CYAN, 2, true);
      break;
  }
}

}  // namespace

void drawForecastIcon(lv_event_t *event) {
  const auto *state = static_cast<const ForecastIconState *>(
      lv_event_get_user_data(event));
  lv_area_t area;
  lv_obj_get_coords(lv_event_get_target(event), &area);
  IconPainter painter(lv_event_get_draw_ctx(event), state->redNight);
  drawIcon(painter, area.x1 + FORECAST_ICON_SIZE / 2,
           area.y1 + FORECAST_ICON_SIZE / 2, FORECAST_ICON_SIZE / 2,
           state->wmoCode, !state->isDay);
}
