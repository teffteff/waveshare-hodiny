#pragma once

#include <cstddef>
#include <cstdint>

// Plánovač malých síťových úloh (NetPoll.h) bez FreeRTOS, aby šel otestovat
// na počítači (tools/test_net_poll_schedule.cpp).
//
// Každá úloha má termín, kdy se má krok zavolat znovu, a příznak probuzení
// (změna nastavení, otevřená obrazovka, zkouška z webu). Ze splatných úloh
// se bere ta s nejnižším indexem: pořadí registrace je priorita. Plánovač
// sám není vláknově bezpečný; zamyká ho NetPoll.cpp.

// Krok nechce být zavolán, dokud ho někdo neprobudí.
constexpr uint32_t NET_POLL_IDLE = UINT32_MAX;
// Krok, který začne o víc než tohle po svém termínu, se počítá jako
// zpožděný: pracovník mezitím obsluhoval jinou úlohu.
constexpr uint32_t NET_POLL_OVERRUN_MS = 2000;

struct NetPollJobState {
  const char *name = "";
  // Termín v millis(); platí jen se scheduled.
  uint32_t dueAt = 0;
  bool scheduled = false;
  bool wakeRequested = false;
  uint32_t wakeAt = 0;
  // Přehled pro diagnostiku.
  uint32_t runs = 0;
  bool everRun = false;
  uint32_t lastStartAt = 0;
  uint32_t lastDurationMs = 0;
  uint32_t maxDurationMs = 0;
  uint32_t overruns = 0;
  uint32_t maxLateMs = 0;
};

class NetPollSchedule {
 public:
  static constexpr size_t MAX_JOBS = 6;

  // Nová úloha je splatná hned. Vrací její index, nebo -1, když je plno.
  int add(const char *name, uint32_t now);
  // Úloha má běžet co nejdřív a krok se dozví, že ho někdo probudil.
  void wake(int job, uint32_t now);
  // Vybere splatnou úlohu s nejvyšší prioritou, nebo -1. Příznak probuzení
  // se tím spotřebuje a vrátí ve woken; probuzení během běhu kroku zůstane
  // na další průchod.
  int pickDue(uint32_t now, bool &woken);
  // Zapíše konec kroku vybraného pickDue(): nextWaitMs je to, co krok vrátil
  // (NET_POLL_IDLE = jen na probuzení), now čas jeho konce.
  void finish(int job, uint32_t now, uint32_t nextWaitMs);
  // Za jak dlouho je nejbližší termín; NET_POLL_IDLE, když žádný není.
  uint32_t msUntilNext(uint32_t now) const;

  size_t count() const { return count_; }
  const NetPollJobState &job(size_t index) const { return jobs_[index]; }

 private:
  bool due(const NetPollJobState &state, uint32_t now) const;

  NetPollJobState jobs_[MAX_JOBS];
  size_t count_ = 0;
};
