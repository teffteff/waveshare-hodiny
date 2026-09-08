#include "RainViewerIndex.h"

#include <stdlib.h>
#include <string.h>

namespace {
// Výchozí adresa pro případ, že by index pole "host" vynechal. RainViewer ho
// posílá vždy, ale bez něj by se nedala složit ani jedna dlaždice.
constexpr char DEFAULT_HOST[] = "https://tilecache.rainviewer.com";

// Zkopíruje hodnotu řetězce od `value` po nejbližší uvozovku. Vrací false,
// když hodnota chybí, je prázdná nebo se do cíle nevejde.
bool copyQuoted(const char *value, const char *limit, char *output,
                size_t capacity) {
  const char *end = strchr(value, '"');
  if (end == nullptr || end > limit) return false;
  const size_t length = static_cast<size_t>(end - value);
  if (length == 0 || length >= capacity) return false;
  memcpy(output, value, length);
  output[length] = '\0';
  return true;
}
}  // namespace

bool rainViewerParseIndex(const char *payload, size_t wantedFrames,
                          RainViewerIndex &index) {
  index = RainViewerIndex{};
  if (payload == nullptr || wantedFrames == 0) return false;
  const char *limit = payload + strlen(payload);

  const char *hostField = strstr(payload, "\"host\":\"");
  if (hostField == nullptr ||
      !copyQuoted(hostField + 8, limit, index.host, sizeof(index.host))) {
    memcpy(index.host, DEFAULT_HOST, sizeof(DEFAULT_HOST));
  }

  // Pole "past" leží uvnitř "radar". Hledá se až od něj, aby scanner nesebral
  // stejně pojmenované pole ze satelitních vrstev, které jsou v odpovědi taky.
  const char *radar = strstr(payload, "\"radar\"");
  if (radar == nullptr) return false;
  const char *past = strstr(radar, "\"past\":[");
  if (past == nullptr) return false;
  past += 8;
  const char *pastEnd = strchr(past, ']');
  if (pastEnd == nullptr) return false;

  const size_t take =
      wantedFrames < RAIN_VIEWER_MAX_FRAMES ? wantedFrames
                                            : RAIN_VIEWER_MAX_FRAMES;
  for (const char *entry = past; entry < pastEnd;) {
    const char *timeField = strstr(entry, "\"time\":");
    if (timeField == nullptr || timeField >= pastEnd) break;
    const char *pathField = strstr(timeField, "\"path\":\"");
    if (pathField == nullptr || pathField >= pastEnd) break;

    RainViewerFrame frame;
    frame.time = static_cast<int64_t>(strtoll(timeField + 7, nullptr, 10));
    if (!copyQuoted(pathField + 8, pastEnd, frame.path, sizeof(frame.path)))
      break;

    // Zajímá nás posledních `take` snímků, ale index je vypisuje od
    // nejstaršího. Jakmile je plno, nejstarší vypadne a zbytek se posune.
    if (index.frameCount == take) {
      for (size_t position = 1; position < take; ++position)
        index.frames[position - 1] = index.frames[position];
      --index.frameCount;
    }
    index.frames[index.frameCount++] = frame;
    entry = pathField + 8 + strlen(frame.path);
  }
  return index.frameCount > 0;
}
