#pragma once

#include <lvgl.h>

constexpr int FORECAST_ICON_SIZE = 28;

// Stored with each forecast row; the draw callback reads the latest conditions.
struct ForecastIconState {
  int wmoCode = 3;
  bool isDay = true;
  bool redNight = false;
};

void drawForecastIcon(lv_event_t *event);
