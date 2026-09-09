#include "PlaneFeedUrl.h"

#include <stdio.h>
#include <string.h>

bool planeFeedBuildUrl(const char *feedUrl, float latitude, float longitude,
                       float distanceNm, char *output, size_t capacity) {
  if (output == nullptr || capacity == 0) return false;
  output[0] = '\0';
  int written = 0;
  if (feedUrl != nullptr && feedUrl[0] != '\0') {
    // Vlastní zdroj může mít v adrese vlastní dotaz (a s ním třeba klíč nebo
    // jméno souboru), takže se parametry připojí za něj, ne místo něj.
    const char separator = strchr(feedUrl, '?') != nullptr ? '&' : '?';
    written = snprintf(output, capacity, "%s%clat=%.5f&lon=%.5f&dist=%.1f",
                       feedUrl, separator, static_cast<double>(latitude),
                       static_cast<double>(longitude),
                       static_cast<double>(distanceNm));
  } else {
    written = snprintf(output, capacity, "%s%.5f/lon/%.5f/dist/%.1f",
                       PLANE_FEED_ADSB_HOST, static_cast<double>(latitude),
                       static_cast<double>(longitude),
                       static_cast<double>(distanceNm));
  }
  // Useknutá adresa je horší než žádná: vedla by na cizí místo, nebo by se
  // ptala na jiný dosah, než jaký je na displeji.
  if (written < 0 || static_cast<size_t>(written) >= capacity) {
    output[0] = '\0';
    return false;
  }
  return true;
}

bool planeFeedBuildRouteUrl(const char *feedUrl, const char *callsign,
                            float latitude, float longitude, char *output,
                            size_t capacity) {
  if (output == nullptr || capacity == 0) return false;
  output[0] = '\0';
  if (callsign == nullptr || callsign[0] == '\0') return false;
  int written = 0;
  if (feedUrl != nullptr && feedUrl[0] != '\0') {
    const char separator = strchr(feedUrl, '?') != nullptr ? '&' : '?';
    written = snprintf(output, capacity, "%s%clat=%.4f&lon=%.4f&route=%s",
                       feedUrl, separator, static_cast<double>(latitude),
                       static_cast<double>(longitude), callsign);
  } else {
    written = snprintf(output, capacity, "%s%s/%.4f/%.4f",
                       PLANE_FEED_ROUTE_HOST, callsign,
                       static_cast<double>(latitude),
                       static_cast<double>(longitude));
  }
  if (written < 0 || static_cast<size_t>(written) >= capacity) {
    output[0] = '\0';
    return false;
  }
  return true;
}
