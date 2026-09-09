#include "ConfigurationForm.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {

String field(const ConfigurationFormSource &source, const String &name) {
  return source.get(source.context, name);
}

}  // namespace

bool parseFiniteFloat(const String &text, float &value) {
  char *end = nullptr;
  value = strtof(text.c_str(), &end);
  return end != text.c_str() && *end == '\0' && std::isfinite(value);
}

bool parseHtmlColor(const String &value, uint32_t &color) {
  if (value.length() != 7 || value[0] != '#') return false;
  char *end = nullptr;
  const unsigned long parsed = strtoul(value.c_str() + 1, &end, 16);
  if (end == value.c_str() + 1 || *end != '\0' || parsed > 0xFFFFFF) {
    return false;
  }
  color = static_cast<uint32_t>(parsed);
  return true;
}

const char *clockScreenName(uint8_t screen) {
  switch (screen) {
    case CLOCK_SCREEN_RADAR: return "radar";
    case CLOCK_SCREEN_RSS: return "rss";
    case CLOCK_SCREEN_FORECAST: return "forecast";
    case CLOCK_SCREEN_PLANES: return "planes";
    default: return "clock";
  }
}

bool parseScreenOrder(const String &text, uint8_t *order) {
  bool seen[CLOCK_SCREEN_ORDER_COUNT] = {};
  size_t count = 0;
  const char *cursor = text.c_str();
  while (*cursor != '\0') {
    while (*cursor == ' ') ++cursor;
    const char *end = cursor;
    while (*end != '\0' && *end != ',') ++end;
    const char *tokenEnd = end;
    while (tokenEnd > cursor && tokenEnd[-1] == ' ') --tokenEnd;
    const size_t length = static_cast<size_t>(tokenEnd - cursor);
    if (count >= CLOCK_SCREEN_ORDER_COUNT) return false;
    uint8_t screen = CLOCK_SCREEN_ORDER_COUNT;
    for (uint8_t candidate = 0; candidate < CLOCK_SCREEN_ORDER_COUNT;
         ++candidate) {
      const char *name = clockScreenName(candidate);
      if (strlen(name) == length && strncmp(name, cursor, length) == 0) {
        screen = candidate;
        break;
      }
    }
    if (screen >= CLOCK_SCREEN_ORDER_COUNT || seen[screen]) return false;
    seen[screen] = true;
    order[count++] = screen;
    if (*end == '\0') break;
    cursor = end + 1;
  }
  return count == CLOCK_SCREEN_ORDER_COUNT;
}

void applyMetricPreset(ClockMetricConfig &metric, const String &preset) {
  struct Preset {
    const char *id;
    const char *name;
    const char *suffix;
    uint8_t decimals;
  };
  static const Preset presets[] = {
      {"temperature", "TEPLOTA", "°C", 1},
      {"co2", "CO₂", "ppm", 0},       {"voc", "VOC", "ppb", 0},
      {"pm25", "PM2.5", "µg/m³", 0}, {"pm10", "PM10", "µg/m³", 0},
      {"humidity", "VLHKOST", "%", 0}, {"pressure", "TLAK", "hPa", 0},
      {"aqi", "AQI", "", 0},          {"illuminance", "SVĚTLO", "lx", 0},
      {"noise", "HLUK", "dB", 0},     {"battery", "BATERIE", "%", 0},
  };
  const Preset *selected = &presets[0];
  for (const Preset &candidate : presets) {
    if (preset == candidate.id) {
      selected = &candidate;
      break;
    }
  }
  clockConfigCopy(metric.preset, sizeof(metric.preset), selected->id);
  clockConfigCopy(metric.name, sizeof(metric.name), selected->name);
  clockConfigCopy(metric.suffix, sizeof(metric.suffix), selected->suffix);
  metric.decimals = selected->decimals;
}

bool readColorScaleFromSource(const ConfigurationFormSource &source,
                              const String &prefix,
                              ClockMetricColorScale &scale) {
  const int count = field(source, prefix + "Count").toInt();
  if (count < 1 || count > static_cast<int>(CLOCK_METRIC_COLOR_POINT_COUNT)) {
    return false;
  }
  scale = ClockMetricColorScale{};
  scale.count = static_cast<uint8_t>(count);
  for (uint8_t index = 0; index < scale.count; ++index) {
    const String suffix = String(index);
    if (!parseFiniteFloat(field(source, prefix + "Value" + suffix),
                          scale.points[index].value) ||
        !parseHtmlColor(field(source, prefix + "Color" + suffix),
                        scale.points[index].color)) {
      return false;
    }
  }
  for (uint8_t index = 1; index < scale.count; ++index) {
    const ClockMetricColorPoint point = scale.points[index];
    uint8_t position = index;
    while (position > 0 && scale.points[position - 1].value > point.value) {
      scale.points[position] = scale.points[position - 1];
      --position;
    }
    scale.points[position] = point;
  }
  for (uint8_t index = 1; index < scale.count; ++index) {
    if (scale.points[index - 1].value == scale.points[index].value) {
      return false;
    }
  }
  return true;
}

ValueSlotFormResult readValueSlotFromSource(const ConfigurationFormSource &source,
                                            size_t index,
                                            ClockValueSlotConfig &slot) {
  const String prefix = String("valueSlot") + index;
  if (!source.has(source.context, prefix + "Enabled")) {
    return ValueSlotFormResult::Missing;
  }
  slot.enabled = field(source, prefix + "Enabled") == "1";
  slot.custom = field(source, prefix + "Mode") == "custom";
  clockConfigCopy(slot.entityId, sizeof(slot.entityId),
                  field(source, prefix + "Entity"));
  if (slot.custom) {
    clockConfigCopy(slot.preset, sizeof(slot.preset), "custom");
    clockConfigCopy(slot.name, sizeof(slot.name), field(source, prefix + "Name"));
    clockConfigCopy(slot.suffix, sizeof(slot.suffix),
                    field(source, prefix + "Suffix"));
  } else {
    ClockMetricConfig presetConfig;
    applyMetricPreset(presetConfig, field(source, prefix + "Preset"));
    clockConfigCopy(slot.preset, sizeof(slot.preset), presetConfig.preset);
    clockConfigCopy(slot.name, sizeof(slot.name), presetConfig.name);
    clockConfigCopy(slot.suffix, sizeof(slot.suffix), presetConfig.suffix);
  }
  slot.decimals = constrain(field(source, prefix + "Decimals").toInt(), 0, 2);
  if (!readColorScaleFromSource(source, prefix + "Color", slot.colorScale)) {
    return ValueSlotFormResult::InvalidColorScale;
  }
  slot.color = slot.colorScale.points[0].color;
  return ValueSlotFormResult::Applied;
}
