#include "NetPoll.h"

#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>

#include <new>

#include "Board.h"

namespace {

// Zásobník se vyměřuje podle nejhlubšího kroku. Nejvíc stojí TLS handshake
// proti svazku kořenů Mozilly (všechny adresy zadává majitel): úlohy zpráv,
// agendy a rozvrhu podle diagnostiky (taskStacks) nespotřebovaly ani 5 kB,
// i s vlastním rámcem úlohy (~0,6 kB) a stažením (~0,5 kB). Rámce kroků podle
// -fstack-usage (7", 10. 2026): upozornění 2,0 kB (odpověď 1 kB na
// zásobníku), výstrahy 1,5 kB, blesky 1,2 kB, déšť 1,0 kB, tedy kolem 6 kB
// s handshakem; předpověď 0,7 kB + rozbor 2,1 kB a historie 1,9 kB, jenže
// rozbor běží až po zavření spojení, takže hloubku handshaku nepřeroste.
// Proto 12 kB s rezervou přes 6 kB. Kulatý 2,1" PSRAM nešetří a textu
// nechává 20 kB jako dřív.
constexpr uint32_t ALERTS_STACK = 12288;
constexpr uint32_t TEXT_STACK = HODINY_BOARD_LCD7 ? 12288 : 20480;
// Strop jednoho spánku: pdMS_TO_TICKS by u hodinových čekání přetekl a
// minuta navíc nic nestojí.
constexpr uint32_t MAX_SLEEP_MS = 60UL * 1000UL;

struct WorkerKind {
  const char *taskName;
  uint32_t stackBytes;
};

constexpr WorkerKind WORKER_KINDS[] = {
    {"net-poll", ALERTS_STACK},
    {"net-text", TEXT_STACK},
};
constexpr size_t WORKER_COUNT = sizeof(WORKER_KINDS) / sizeof(WORKER_KINDS[0]);

struct Worker {
  TaskHandle_t task = nullptr;
  NetPollSchedule schedule;
  NetPollStep steps[NetPollSchedule::MAX_JOBS] = {};
};

// Plánovače mají kolem 350 B; leží v PSRAM jako dřív zásobníky úloh, aby
// nebraly vnitřní RAM, kterou potřebuje TLS a Wi-Fi.
Worker *workers[WORKER_COUNT] = {};

Worker *ensureWorker(size_t index) {
  if (workers[index] != nullptr) return workers[index];
  void *memory = heap_caps_calloc(1, sizeof(Worker),
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (memory == nullptr) return nullptr;
  workers[index] = new (memory) Worker();
  return workers[index];
}

// Plánovače obou pracovníků; krátké kopie a porovnání, takže stačí spinlock.
portMUX_TYPE scheduleMux = portMUX_INITIALIZER_UNLOCKED;

// Číslo úlohy nese pracovníka v horních bitech, aby netPollWake stačilo jedno
// číslo.
constexpr int JOB_SHIFT = 8;

void workerTask(void *parameter) {
  Worker &worker = *static_cast<Worker *>(parameter);
  for (;;) {
    bool woken = false;
    portENTER_CRITICAL(&scheduleMux);
    const uint32_t now = millis();
    const int job = worker.schedule.pickDue(now, woken);
    const uint32_t wait = job < 0 ? worker.schedule.msUntilNext(now) : 0;
    const NetPollStep step = job < 0 ? nullptr : worker.steps[job];
    portEXIT_CRITICAL(&scheduleMux);
    if (step == nullptr) {
      // Probuzení mezi odemčením a usnutím zůstane v notifikaci, takže se
      // neztratí.
      const uint32_t sleepMs = wait < MAX_SLEEP_MS ? wait : MAX_SLEEP_MS;
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(sleepMs));
      continue;
    }
    const uint32_t next = step(woken);
    portENTER_CRITICAL(&scheduleMux);
    worker.schedule.finish(job, millis(), next);
    portEXIT_CRITICAL(&scheduleMux);
  }
}

}  // namespace

int netPollRegister(NetPollWorker which, const char *name, NetPollStep step) {
  const size_t index = static_cast<size_t>(which);
  if (index >= WORKER_COUNT || step == nullptr) return -1;
  Worker *created = ensureWorker(index);
  if (created == nullptr) return -1;
  Worker &worker = *created;
  // Volá se jen při startu ze setup(), takže registrace nesoupeří samy se
  // sebou; zámek je kvůli pracovníkovi, který už může běžet.
  portENTER_CRITICAL(&scheduleMux);
  const int job = worker.schedule.add(name, millis());
  if (job >= 0) worker.steps[job] = step;
  portEXIT_CRITICAL(&scheduleMux);
  if (job < 0) return -1;
  if (worker.task == nullptr &&
      xTaskCreatePinnedToCoreWithCaps(workerTask, WORKER_KINDS[index].taskName,
                                      WORKER_KINDS[index].stackBytes, &worker,
                                      1, &worker.task, 0,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) !=
          pdPASS) {
    worker.task = nullptr;
    return -1;
  }
  xTaskNotifyGive(worker.task);
  return static_cast<int>(index << JOB_SHIFT) | job;
}

void netPollWake(int job) {
  if (job < 0) return;
  const size_t index = static_cast<size_t>(job) >> JOB_SHIFT;
  if (index >= WORKER_COUNT || workers[index] == nullptr) return;
  Worker &worker = *workers[index];
  portENTER_CRITICAL(&scheduleMux);
  worker.schedule.wake(job & ((1 << JOB_SHIFT) - 1), millis());
  portEXIT_CRITICAL(&scheduleMux);
  if (worker.task != nullptr) xTaskNotifyGive(worker.task);
}

TaskHandle_t netPollWorkerTask(NetPollWorker which) {
  const size_t index = static_cast<size_t>(which);
  return index < WORKER_COUNT && workers[index] != nullptr
             ? workers[index]->task
             : nullptr;
}

void netPollAppendDiagnosticsJson(String &result) {
  result += F("\"netPoll\":[");
  for (size_t index = 0; index < WORKER_COUNT; ++index) {
    const Worker *worker = workers[index];
    // Kopie pod zámkem, JSON až po něm: String alokuje.
    NetPollJobState jobs[NetPollSchedule::MAX_JOBS];
    size_t count = 0;
    portENTER_CRITICAL(&scheduleMux);
    const uint32_t now = millis();
    if (worker != nullptr) {
      count = worker->schedule.count();
      for (size_t job = 0; job < count; ++job)
        jobs[job] = worker->schedule.job(job);
    }
    const TaskHandle_t task = worker != nullptr ? worker->task : nullptr;
    portEXIT_CRITICAL(&scheduleMux);
    if (index > 0) result += ',';
    result += F("{\"name\":\"");
    result += WORKER_KINDS[index].taskName;
    result += F("\",\"stackBytes\":");
    result += WORKER_KINDS[index].stackBytes;
    result += F(",\"stackFree\":");
    result += task == nullptr
                  ? 0
                  : static_cast<uint32_t>(uxTaskGetStackHighWaterMark(task));
    result += F(",\"jobs\":[");
    for (size_t job = 0; job < count; ++job) {
      const NetPollJobState &state = jobs[job];
      if (job > 0) result += ',';
      result += F("{\"name\":\"");
      result += state.name;
      result += F("\",\"runs\":");
      result += state.runs;
      result += F(",\"lastRunAgoMs\":");
      result += state.everRun ? now - state.lastStartAt : 0;
      result += F(",\"lastDurationMs\":");
      result += state.lastDurationMs;
      result += F(",\"maxDurationMs\":");
      result += state.maxDurationMs;
      result += F(",\"overruns\":");
      result += state.overruns;
      result += F(",\"maxLateMs\":");
      result += state.maxLateMs;
      result += F(",\"nextInMs\":");
      if (state.wakeRequested) {
        result += 0;
      } else if (!state.scheduled) {
        result += F("null");
      } else {
        const int32_t left = static_cast<int32_t>(state.dueAt - now);
        result += left > 0 ? static_cast<uint32_t>(left) : 0;
      }
      result += '}';
    }
    result += F("]}");
  }
  result += ']';
}
