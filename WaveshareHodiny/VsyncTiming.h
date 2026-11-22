#pragma once

#include <stdint.h>

// Rytmus VSYNC přerušení RGB panelu (RgbPanel.cpp). Takt panelu i esp_timer
// jdou z jednoho krystalu, takže VSYNC přichází přesně po frameNs a podle
// toho jde poznat, jak pozdě se přerušení obsloužilo. Bez závislostí, aby šlo
// testovat na počítači (tools/test_vsync_timing.cpp); vkládá se přímo do
// obsluhy přerušení.

constexpr int32_t VSYNC_LATENESS_UNKNOWN = INT32_MAX;
// Po delší pauze (přerušení neběžela, panel stál) se rytmus měří znovu.
constexpr uint32_t VSYNC_MAX_GAP_FRAMES = 100;
// Přerušení do 20 us po čekaném okamžiku se bere jako včasné a stane se
// novou referencí; vyrovná se tím zaokrouhlení taktu panelu.
constexpr int32_t VSYNC_REFERENCE_SLACK_US = 20;

// O kolik mikrosekund přišlo přerušení v čase nowUs po čekaném okamžiku
// (záporně: dřív, než čekala reference). referenceUs je čas posledního
// včasného přerušení, 0 = zatím žádné; funkce ho sama posouvá. Bez platné
// reference vrátí VSYNC_LATENESS_UNKNOWN.
inline __attribute__((always_inline)) int32_t vsyncLatenessUs(
    int64_t &referenceUs, int64_t nowUs, uint32_t frameNs) {
  const uint32_t frameUs = frameNs / 1000;
  const int64_t elapsed = nowUs - referenceUs;
  if (frameUs == 0 || referenceUs == 0 || elapsed <= 0 ||
      elapsed > static_cast<int64_t>(frameUs) * VSYNC_MAX_GAP_FRAMES) {
    referenceUs = nowUs;
    return VSYNC_LATENESS_UNKNOWN;
  }
  const uint32_t frames =
      (static_cast<uint32_t>(elapsed) + frameUs / 2) / frameUs;
  const int64_t expected =
      referenceUs +
      static_cast<int64_t>(static_cast<uint64_t>(frames) * frameNs / 1000);
  const int32_t late = static_cast<int32_t>(nowUs - expected);
  // Dřívější přerušení než čekané mělo menší latenci než reference.
  if (late <= VSYNC_REFERENCE_SLACK_US) referenceUs = nowUs;
  return late;
}
