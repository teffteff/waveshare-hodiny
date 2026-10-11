#include "NetPollSchedule.h"

namespace {
// Rozdíl časů millis() přes přetečení: kladný, když a je po b.
int32_t elapsed(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b); }
}  // namespace

int NetPollSchedule::add(const char *name, uint32_t now) {
  if (count_ >= MAX_JOBS) return -1;
  NetPollJobState &state = jobs_[count_];
  state = NetPollJobState{};
  state.name = name;
  state.dueAt = now;
  state.scheduled = true;
  return static_cast<int>(count_++);
}

void NetPollSchedule::wake(int job, uint32_t now) {
  if (job < 0 || static_cast<size_t>(job) >= count_) return;
  NetPollJobState &state = jobs_[job];
  // Opakované probuzení před během kroku nic nemění; zpoždění se počítá od
  // prvního.
  if (state.wakeRequested) return;
  state.wakeRequested = true;
  state.wakeAt = now;
}

bool NetPollSchedule::due(const NetPollJobState &state, uint32_t now) const {
  return state.wakeRequested ||
         (state.scheduled && elapsed(now, state.dueAt) >= 0);
}

int NetPollSchedule::pickDue(uint32_t now, bool &woken) {
  woken = false;
  for (size_t index = 0; index < count_; ++index) {
    NetPollJobState &state = jobs_[index];
    if (!due(state, now)) continue;
    // Zpoždění se měří od dřívějšího z termínu a probuzení.
    uint32_t reference = state.wakeRequested ? state.wakeAt : state.dueAt;
    if (state.wakeRequested && state.scheduled &&
        elapsed(state.dueAt, reference) < 0 && elapsed(now, state.dueAt) >= 0)
      reference = state.dueAt;
    const int32_t late = elapsed(now, reference);
    const uint32_t lateMs = late > 0 ? static_cast<uint32_t>(late) : 0;
    if (lateMs > NET_POLL_OVERRUN_MS) ++state.overruns;
    if (lateMs > state.maxLateMs) state.maxLateMs = lateMs;
    woken = state.wakeRequested;
    state.wakeRequested = false;
    state.lastStartAt = now;
    return static_cast<int>(index);
  }
  return -1;
}

void NetPollSchedule::finish(int job, uint32_t now, uint32_t nextWaitMs) {
  if (job < 0 || static_cast<size_t>(job) >= count_) return;
  NetPollJobState &state = jobs_[job];
  const int32_t duration = elapsed(now, state.lastStartAt);
  state.lastDurationMs = duration > 0 ? static_cast<uint32_t>(duration) : 0;
  if (state.lastDurationMs > state.maxDurationMs)
    state.maxDurationMs = state.lastDurationMs;
  ++state.runs;
  state.everRun = true;
  if (nextWaitMs == NET_POLL_IDLE) {
    state.scheduled = false;
  } else {
    state.scheduled = true;
    state.dueAt = now + nextWaitMs;
  }
}

uint32_t NetPollSchedule::msUntilNext(uint32_t now) const {
  uint32_t best = NET_POLL_IDLE;
  for (size_t index = 0; index < count_; ++index) {
    const NetPollJobState &state = jobs_[index];
    if (state.wakeRequested) return 0;
    if (!state.scheduled) continue;
    const int32_t left = elapsed(state.dueAt, now);
    const uint32_t wait = left > 0 ? static_cast<uint32_t>(left) : 0;
    if (wait < best) best = wait;
  }
  return best;
}
