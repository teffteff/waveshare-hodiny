#include "SourceHealth.h"

#include <stdio.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include <Arduino.h>
#endif

namespace {

bool before(uint32_t a, uint32_t b) {
  return static_cast<int32_t>(a - b) < 0;
}

}  // namespace

void SourceHealth::record(uint32_t now, bool ok, uint32_t nextInMs) {
  tried = true;
  if (ok) {
    okAt = now;
    failures = 0;
    everOk = true;
  } else if (failures < UINT16_MAX) {
    ++failures;
  }
  // Delší čekání přes půl rozsahu millis() porovnání rozdílem neunese.
  if (nextInMs > 0x7FFFFFFFu) nextInMs = 0x7FFFFFFFu;
  nextAt = now + nextInMs;
}

bool SourceHealth::down() const {
  if (failures == 0) return false;
  return !everOk || failures >= SOURCE_HEALTH_DOWN_FAILURES;
}

uint32_t SourceHealth::ageMs(uint32_t now) const {
  return everOk ? now - okAt : UINT32_MAX;
}

uint32_t SourceHealth::nextInMs(uint32_t now) const {
  if (!tried || !before(now, nextAt)) return 0;
  return nextAt - now;
}

size_t sourceHealthFormat(const SourceHealth &health, uint32_t now,
                          bool staleData, bool english, char *output,
                          size_t capacity) {
  if (output == nullptr || capacity == 0) return 0;
  output[0] = '\0';
  if (!health.down()) return 0;
  const char *prefix = !staleData ? ""
                       : english  ? "Old data · "
                                  : "Stará data · ";
  const char *unavailable = english ? "source unavailable"
                                    : "zdroj nedostupný";
  // Velké písmeno na začátku řádku; za "Stará data · " pokračuje malým.
  const char *unavailableStart = english ? "Source unavailable"
                                         : "Zdroj nedostupný";
  const char *head = staleData ? unavailable : unavailableStart;
  const uint32_t waitMs = health.nextInMs(now);
  int written;
  if (waitMs == 0) {
    written = snprintf(output, capacity, "%s%s · %s", prefix, head,
                       english ? "retrying" : "zkouším znovu");
  } else if (waitMs < 60000) {
    // Nahoru, aby "za 0 s" nikdy nestálo vedle pokusu, který ještě nezačal.
    const unsigned seconds = static_cast<unsigned>((waitMs + 999) / 1000);
    written = snprintf(output, capacity, "%s%s · %s %u s", prefix, head,
                       english ? "next try in" : "další pokus za", seconds);
  } else {
    const unsigned minutes = static_cast<unsigned>((waitMs + 59999) / 60000);
    written = snprintf(output, capacity, "%s%s · %s %u min", prefix, head,
                       english ? "next try in" : "další pokus za", minutes);
  }
  if (written < 0) {
    output[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(written) < capacity ? static_cast<size_t>(written)
                                                 : capacity - 1;
}

const char *sourceHealthName(ClockSource source) {
  switch (source) {
    case ClockSource::HomeAssistant: return "homeAssistant";
    case ClockSource::OpenMeteo: return "openMeteo";
    case ClockSource::Forecast: return "forecast";
    case ClockSource::History: return "history";
    case ClockSource::Radar: return "radar";
    case ClockSource::Lightning: return "lightning";
    case ClockSource::Warnings: return "warnings";
    case ClockSource::Planes: return "planes";
    case ClockSource::Satellites: return "satellites";
    case ClockSource::School: return "school";
    case ClockSource::Rss: return "rss";
    case ClockSource::Agenda: return "agenda";
    case ClockSource::Traffic: return "traffic";
    case ClockSource::Count: break;
  }
  return "";
}

#if defined(ESP_PLATFORM)
namespace {

// 13 x 16 bajtů interní RAM; zámek drží jen kopírování.
SourceHealth sources[CLOCK_SOURCE_COUNT];
portMUX_TYPE sourcesMux = portMUX_INITIALIZER_UNLOCKED;

}  // namespace

void sourceHealthRecord(ClockSource source, bool ok, uint32_t nextInMs) {
  const size_t index = static_cast<size_t>(source);
  if (index >= CLOCK_SOURCE_COUNT) return;
  const uint32_t now = millis();
  portENTER_CRITICAL(&sourcesMux);
  sources[index].record(now, ok, nextInMs);
  portEXIT_CRITICAL(&sourcesMux);
}

void sourceHealthReset(ClockSource source) {
  const size_t index = static_cast<size_t>(source);
  if (index >= CLOCK_SOURCE_COUNT) return;
  portENTER_CRITICAL(&sourcesMux);
  sources[index].reset();
  portEXIT_CRITICAL(&sourcesMux);
}

SourceHealth sourceHealthGet(ClockSource source) {
  const size_t index = static_cast<size_t>(source);
  SourceHealth health;
  if (index >= CLOCK_SOURCE_COUNT) return health;
  portENTER_CRITICAL(&sourcesMux);
  health = sources[index];
  portEXIT_CRITICAL(&sourcesMux);
  return health;
}

size_t sourceHealthText(ClockSource source, bool staleData, bool english,
                        char *output, size_t capacity) {
  return sourceHealthFormat(sourceHealthGet(source), millis(), staleData,
                            english, output, capacity);
}
#endif
