#include <cassert>
#include <cstdint>

#include "../WaveshareHodiny/NetPollSchedule.h"

int main() {
  bool woken = false;

  // Nové úlohy jsou splatné hned a vybírají se podle pořadí registrace.
  {
    NetPollSchedule schedule;
    const int lightning = schedule.add("lightning", 1000);
    const int rain = schedule.add("rain", 1000);
    assert(lightning == 0 && rain == 1);
    assert(schedule.msUntilNext(1000) == 0);
    assert(schedule.pickDue(1000, woken) == lightning && !woken);
    schedule.finish(lightning, 1200, 500);
    assert(schedule.pickDue(1200, woken) == rain && !woken);
    schedule.finish(rain, 1300, 1000);
    // Nic není splatné; nejbližší je blesk v 1700.
    assert(schedule.pickDue(1300, woken) == -1);
    assert(schedule.msUntilNext(1300) == 400);
    assert(schedule.pickDue(1700, woken) == lightning);
    schedule.finish(lightning, 1700, 500);
    // Obě splatné naráz: přednost má dřív zaregistrovaná.
    assert(schedule.pickDue(2400, woken) == lightning);
    schedule.finish(lightning, 2400, 500);
    assert(schedule.pickDue(2400, woken) == rain);
    schedule.finish(rain, 2400, 1000);
    assert(schedule.job(rain).runs == 2 && schedule.job(lightning).runs == 3);
  }

  // Probuzení: úloha běží hned a krok se to dozví; opakované probuzení se
  // slije do jednoho.
  {
    NetPollSchedule schedule;
    const int rss = schedule.add("rss", 0);
    assert(schedule.pickDue(0, woken) == rss);
    schedule.finish(rss, 10, 60000);
    assert(schedule.msUntilNext(10) == 60000);
    schedule.wake(rss, 100);
    schedule.wake(rss, 150);
    assert(schedule.msUntilNext(150) == 0);
    assert(schedule.pickDue(200, woken) == rss && woken);
    schedule.finish(rss, 300, 60000);
    assert(schedule.pickDue(300, woken) == -1 && !woken);
    // Neplatné číslo úlohy se ignoruje.
    schedule.wake(-1, 300);
    schedule.wake(7, 300);
    assert(schedule.pickDue(300, woken) == -1);
  }

  // Probuzení během běhu kroku se neztratí: krok se zavolá hned znovu.
  {
    NetPollSchedule schedule;
    const int agenda = schedule.add("agenda", 0);
    assert(schedule.pickDue(0, woken) == agenda && !woken);
    schedule.wake(agenda, 5);
    schedule.finish(agenda, 20, 3600000);
    assert(schedule.pickDue(20, woken) == agenda && woken);
    schedule.finish(agenda, 30, 3600000);
    assert(schedule.pickDue(30, woken) == -1);
  }

  // NET_POLL_IDLE: bez termínu, jen na probuzení.
  {
    NetPollSchedule schedule;
    const int off = schedule.add("off", 0);
    assert(schedule.pickDue(0, woken) == off);
    schedule.finish(off, 0, NET_POLL_IDLE);
    assert(schedule.msUntilNext(0) == NET_POLL_IDLE);
    assert(schedule.pickDue(10000000, woken) == -1);
    schedule.wake(off, 10000000);
    assert(schedule.pickDue(10000001, woken) == off && woken);
    schedule.finish(off, 10000001, 1000);
    assert(schedule.msUntilNext(10000001) == 1000);
  }

  // Zpoždění: úloha, která čekala na jinou přes NET_POLL_OVERRUN_MS, se
  // počítá; doba běhu se zapisuje.
  {
    NetPollSchedule schedule;
    const int text = schedule.add("text", 0);
    const int rain = schedule.add("rain", 0);
    assert(schedule.pickDue(0, woken) == text);
    // Zprávy se stahují 25 s; déšť byl splatný od začátku.
    schedule.finish(text, 25000, 600000);
    assert(schedule.job(text).lastDurationMs == 25000);
    assert(schedule.job(text).maxDurationMs == 25000);
    assert(schedule.pickDue(25000, woken) == rain);
    assert(schedule.job(rain).overruns == 1);
    assert(schedule.job(rain).maxLateMs == 25000);
    schedule.finish(rain, 25100, 1000);
    // Včas spuštěný krok se nepočítá.
    assert(schedule.pickDue(26100 + NET_POLL_OVERRUN_MS, woken) == rain);
    assert(schedule.job(rain).overruns == 1);
    schedule.finish(rain, 26200 + NET_POLL_OVERRUN_MS, 1000);
    // Probuzení se měří od chvíle probuzení.
    schedule.wake(text, 30000);
    assert(schedule.pickDue(30000 + NET_POLL_OVERRUN_MS + 1, woken) == text);
    assert(woken && schedule.job(text).overruns == 1);
  }

  // Přetečení millis(): termíny přes nulu platí dál.
  {
    NetPollSchedule schedule;
    const uint32_t nearWrap = UINT32_MAX - 100;
    const int job = schedule.add("wrap", nearWrap);
    assert(schedule.pickDue(nearWrap, woken) == job);
    schedule.finish(job, nearWrap, 1000);
    assert(schedule.msUntilNext(nearWrap + 50) == 950);
    assert(schedule.pickDue(nearWrap + 500, woken) == -1);
    assert(schedule.pickDue(nearWrap + 1000, woken) == job);
    assert(schedule.job(job).overruns == 0);
  }

  // Kapacita.
  {
    NetPollSchedule schedule;
    for (size_t index = 0; index < NetPollSchedule::MAX_JOBS; ++index)
      assert(schedule.add("job", 0) == static_cast<int>(index));
    assert(schedule.add("extra", 0) == -1);
    assert(schedule.count() == NetPollSchedule::MAX_JOBS);
  }
  return 0;
}
