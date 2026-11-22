// Rytmus VSYNC přerušení a jejich zpoždění (VsyncTiming.h, RgbPanel.cpp).
#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "VsyncTiming.h"

namespace {

// 7": 820 x 500 pixelů při 16 MHz; kulatý 548 x 499 při 12 MHz (necelé us).
constexpr uint32_t LCD7_FRAME_NS = 25625000;
constexpr uint32_t ROUND_FRAME_NS = 22788666;

int64_t frameTime(int64_t startUs, uint64_t frame, uint32_t frameNs) {
  return startUs + static_cast<int64_t>(frame * frameNs / 1000);
}

void testFirstCallOnlySetsReference() {
  int64_t reference = 0;
  assert(vsyncLatenessUs(reference, 1000000, LCD7_FRAME_NS) ==
         VSYNC_LATENESS_UNKNOWN);
  assert(reference == 1000000);
}

void testSteadyRhythmIsOnTime(uint32_t frameNs) {
  int64_t reference = 0;
  const int64_t start = 5000000;
  vsyncLatenessUs(reference, start, frameNs);
  // Hodina snímků: ani necelé mikrosekundy délky snímku se nenasčítají.
  for (uint64_t frame = 1; frame < 140000; ++frame) {
    const int32_t late =
        vsyncLatenessUs(reference, frameTime(start, frame, frameNs), frameNs);
    assert(std::abs(late) <= 1);
  }
}

void testLateInterruptKeepsReference() {
  int64_t reference = 0;
  const int64_t start = 2000000;
  vsyncLatenessUs(reference, start, LCD7_FRAME_NS);
  assert(vsyncLatenessUs(reference, frameTime(start, 1, LCD7_FRAME_NS),
                         LCD7_FRAME_NS) == 0);
  // Opožděné přerušení se pozná a referenci neposune.
  assert(vsyncLatenessUs(reference, frameTime(start, 2, LCD7_FRAME_NS) + 700,
                         LCD7_FRAME_NS) == 700);
  assert(reference == frameTime(start, 1, LCD7_FRAME_NS));
  // Další snímek včas, i když mezi nimi jeden VSYNC vypadl úplně.
  assert(vsyncLatenessUs(reference, frameTime(start, 4, LCD7_FRAME_NS),
                         LCD7_FRAME_NS) == 0);
  // Malá latence do 20 us referenci posune, větší ne.
  assert(vsyncLatenessUs(reference, frameTime(start, 5, LCD7_FRAME_NS) + 15,
                         LCD7_FRAME_NS) == 15);
  assert(reference == frameTime(start, 5, LCD7_FRAME_NS) + 15);
  assert(vsyncLatenessUs(reference, frameTime(start, 6, LCD7_FRAME_NS) + 40,
                         LCD7_FRAME_NS) == 25);
  assert(reference == frameTime(start, 5, LCD7_FRAME_NS) + 15);
}

void testLateReferenceIsReplacedByEarlierInterrupt() {
  int64_t reference = 0;
  const int64_t start = 3000000;
  // První změřené přerušení přišlo pozdě; další včasné se ukáže jako dřívější
  // a stane se referencí.
  vsyncLatenessUs(reference, start + 400, LCD7_FRAME_NS);
  assert(vsyncLatenessUs(reference, frameTime(start, 1, LCD7_FRAME_NS),
                         LCD7_FRAME_NS) == -400);
  assert(vsyncLatenessUs(reference, frameTime(start, 2, LCD7_FRAME_NS),
                         LCD7_FRAME_NS) == 0);
}

void testLongGapOrClockJumpMeasuresAgain() {
  int64_t reference = 0;
  const int64_t start = 4000000;
  vsyncLatenessUs(reference, start, LCD7_FRAME_NS);
  const int64_t later = frameTime(start, VSYNC_MAX_GAP_FRAMES + 1, LCD7_FRAME_NS);
  assert(vsyncLatenessUs(reference, later, LCD7_FRAME_NS) ==
         VSYNC_LATENESS_UNKNOWN);
  assert(reference == later);
  assert(vsyncLatenessUs(reference, later - 10, LCD7_FRAME_NS) ==
         VSYNC_LATENESS_UNKNOWN);
  // Bez známé délky snímku se nic neměří.
  int64_t none = 0;
  vsyncLatenessUs(none, start, 0);
  assert(vsyncLatenessUs(none, start + 25625, 0) == VSYNC_LATENESS_UNKNOWN);
}

}  // namespace

int main() {
  testFirstCallOnlySetsReference();
  testSteadyRhythmIsOnTime(LCD7_FRAME_NS);
  testSteadyRhythmIsOnTime(ROUND_FRAME_NS);
  testLateInterruptKeepsReference();
  testLateReferenceIsReplacedByEarlierInterrupt();
  testLongGapOrClockJumpMeasuresAgain();
  std::puts("vsync_timing OK");
  return 0;
}
