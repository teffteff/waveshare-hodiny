#include "CrashLog.h"

#include <Preferences.h>

#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#if __has_include(<esp_core_dump.h>)
#include <esp_core_dump.h>
#include <esp_system.h>
#include "FirmwareBuild.h"
#define CRASH_LOG_ON_DEVICE 1
#endif

namespace {

constexpr char PREFS_PARTITION[] = "clockcfg";
constexpr char PREFS_NAMESPACE[] = "crashlog";
constexpr char COUNT_KEY[] = "count";
constexpr char LAST_KEY[] = "last";
constexpr uint8_t RECORD_VERSION = 1;

// Hodnoty esp_reset_reason_t; na počítači hlavička ESP-IDF není.
constexpr uint8_t RESET_PANIC = 4;
constexpr uint8_t RESET_INT_WDT = 5;
constexpr uint8_t RESET_TASK_WDT = 6;
constexpr uint8_t RESET_WDT = 7;
constexpr uint8_t RESET_BROWNOUT = 9;

void copyText(char *target, size_t size, const char *source) {
  if (size == 0) return;
  snprintf(target, size, "%s", source == nullptr ? "" : source);
}

// Pole souhrnu výpisu nemusí končit nulou (jméno úlohy má přesně 16 znaků),
// takže se kopíruje nejvýš jejich délka.
void copyBounded(char *target, size_t size, const void *source,
                 size_t sourceSize) {
  const char *text = static_cast<const char *>(source);
  size_t length = 0;
  while (length < sourceSize && length + 1 < size && text[length] != '\0')
    ++length;
  memcpy(target, text, length);
  target[length] = '\0';
}

// Připíše do bufferu JSON řetězec včetně uvozovek. Jména úloh a důvod paniky
// jsou ASCII, ale výpis může být poškozený, takže se escapuje všechno.
void appendJsonString(char *buffer, size_t size, size_t &used,
                      const char *value) {
  auto put = [&](char character) {
    if (used + 1 < size) buffer[used++] = character;
  };
  put('"');
  for (const char *cursor = value; *cursor != '\0'; ++cursor) {
    const unsigned char character = static_cast<unsigned char>(*cursor);
    if (character == '"' || character == '\\') {
      put('\\');
      put(static_cast<char>(character));
    } else if (character < 0x20 || character >= 0x7F) {
      char escaped[7];
      snprintf(escaped, sizeof(escaped), "\\u%04x", character);
      for (const char *part = escaped; *part != '\0'; ++part) put(*part);
    } else {
      put(static_cast<char>(character));
    }
  }
  put('"');
  buffer[used < size ? used : size - 1] = '\0';
}

void appendFormat(char *buffer, size_t size, size_t &used, const char *format,
                  ...) __attribute__((format(printf, 4, 5)));

void appendFormat(char *buffer, size_t size, size_t &used, const char *format,
                  ...) {
  if (used >= size) return;
  va_list arguments;
  va_start(arguments, format);
  const int written = vsnprintf(buffer + used, size - used, format, arguments);
  va_end(arguments);
  if (written > 0) used += static_cast<size_t>(written);
  if (used >= size) used = size - 1;
}

}  // namespace

bool crashLogAbnormalReset(uint8_t resetReason) {
  return resetReason == RESET_PANIC || resetReason == RESET_INT_WDT ||
         resetReason == RESET_TASK_WDT || resetReason == RESET_WDT ||
         resetReason == RESET_BROWNOUT;
}

bool crashLogRecord(const CrashRecord *dump, uint8_t resetReason,
                    const char *firmwareVersion) {
  if (dump == nullptr && !crashLogAbnormalReset(resetReason)) return true;
  CrashRecord record{};
  if (dump != nullptr) {
    record = *dump;
    record.hasDump = true;
    if (record.backtraceDepth > CRASH_BACKTRACE_MAX)
      record.backtraceDepth = CRASH_BACKTRACE_MAX;
    record.task[sizeof(record.task) - 1] = '\0';
    record.elfSha[sizeof(record.elfSha) - 1] = '\0';
    record.reason[sizeof(record.reason) - 1] = '\0';
  }
  record.version = RECORD_VERSION;
  record.resetReason = resetReason;
  copyText(record.firmware, sizeof(record.firmware), firmwareVersion);

  Preferences preferences;
  if (!preferences.begin(PREFS_NAMESPACE, false, PREFS_PARTITION)) return false;
  const uint32_t count = preferences.getUInt(COUNT_KEY, 0);
  const bool stored =
      preferences.putBytes(LAST_KEY, &record, sizeof(record)) ==
          sizeof(record) &&
      preferences.putUInt(COUNT_KEY, count + 1) == sizeof(uint32_t);
  preferences.end();
  return stored;
}

String crashLogJson() {
  uint32_t count = 0;
  CrashRecord record{};
  bool haveRecord = false;
  Preferences preferences;
  if (preferences.begin(PREFS_NAMESPACE, true, PREFS_PARTITION)) {
    count = preferences.getUInt(COUNT_KEY, 0);
    haveRecord = preferences.getBytesLength(LAST_KEY) == sizeof(record) &&
                 preferences.getBytes(LAST_KEY, &record, sizeof(record)) ==
                     sizeof(record) &&
                 record.version == RECORD_VERSION;
    preferences.end();
  }

  char buffer[900];
  size_t used = 0;
  buffer[0] = '\0';
  appendFormat(buffer, sizeof(buffer), used, "{\"count\":%" PRIu32 ",\"last\":",
               count);
  if (!haveRecord) {
    appendFormat(buffer, sizeof(buffer), used, "null}");
    return String(buffer);
  }
  record.task[sizeof(record.task) - 1] = '\0';
  record.elfSha[sizeof(record.elfSha) - 1] = '\0';
  record.firmware[sizeof(record.firmware) - 1] = '\0';
  record.reason[sizeof(record.reason) - 1] = '\0';
  appendFormat(buffer, sizeof(buffer), used,
               "{\"resetReason\":%u,\"firmware\":",
               static_cast<unsigned>(record.resetReason));
  appendJsonString(buffer, sizeof(buffer), used, record.firmware);
  appendFormat(buffer, sizeof(buffer), used, ",\"dump\":%s",
               record.hasDump ? "true" : "false");
  if (record.hasDump) {
    appendFormat(buffer, sizeof(buffer), used, ",\"task\":");
    appendJsonString(buffer, sizeof(buffer), used, record.task);
    appendFormat(buffer, sizeof(buffer), used, ",\"reason\":");
    appendJsonString(buffer, sizeof(buffer), used, record.reason);
    appendFormat(buffer, sizeof(buffer), used, ",\"elfSha\":");
    appendJsonString(buffer, sizeof(buffer), used, record.elfSha);
    appendFormat(buffer, sizeof(buffer), used,
                 ",\"pc\":\"0x%08" PRIx32 "\",\"exceptionCause\":%" PRIu32
                 ",\"exceptionAddress\":\"0x%08" PRIx32
                 "\",\"backtraceCorrupted\":%s,\"backtrace\":[",
                 record.pc, record.exceptionCause, record.exceptionAddress,
                 record.backtraceCorrupted ? "true" : "false");
    const uint8_t depth = record.backtraceDepth > CRASH_BACKTRACE_MAX
                              ? CRASH_BACKTRACE_MAX
                              : record.backtraceDepth;
    for (uint8_t index = 0; index < depth; ++index) {
      appendFormat(buffer, sizeof(buffer), used, "%s\"0x%08" PRIx32 "\"",
                   index == 0 ? "" : ",", record.backtrace[index]);
    }
    appendFormat(buffer, sizeof(buffer), used, "]");
  }
  appendFormat(buffer, sizeof(buffer), used, "}}");
  return String(buffer);
}

#ifdef CRASH_LOG_ON_DEVICE
void crashLogBegin() {
  const uint8_t resetReason = static_cast<uint8_t>(esp_reset_reason());
  if (esp_core_dump_image_check() != ESP_OK) {
    crashLogRecord(nullptr, resetReason, FIRMWARE_VERSION);
    return;
  }
  // Souhrn je přes 200 B; na zásobníku setup() by zbytečně ležel vedle
  // načítání konfigurace.
  static esp_core_dump_summary_t summary;
  CrashRecord record{};
  if (esp_core_dump_get_summary(&summary) == ESP_OK) {
    copyBounded(record.task, sizeof(record.task), summary.exc_task,
                sizeof(summary.exc_task));
    copyBounded(record.elfSha, sizeof(record.elfSha), summary.app_elf_sha256,
                sizeof(summary.app_elf_sha256));
    record.pc = summary.exc_pc;
    record.exceptionCause = summary.ex_info.exc_cause;
    record.exceptionAddress = summary.ex_info.exc_vaddr;
    record.backtraceCorrupted = summary.exc_bt_info.corrupted;
    const uint32_t depth = summary.exc_bt_info.depth < CRASH_BACKTRACE_MAX
                               ? summary.exc_bt_info.depth
                               : CRASH_BACKTRACE_MAX;
    record.backtraceDepth = static_cast<uint8_t>(depth);
    memcpy(record.backtrace, summary.exc_bt_info.bt,
           depth * sizeof(record.backtrace[0]));
  }
  if (esp_core_dump_get_panic_reason(record.reason, sizeof(record.reason)) !=
      ESP_OK) {
    record.reason[0] = '\0';
  }
  // Výpis se smaže, jen když je souhrn bezpečně v NVS; jinak zůstane pro
  // esptool i pro další pokus při příštím startu.
  if (crashLogRecord(&record, resetReason, FIRMWARE_VERSION)) {
    esp_core_dump_image_erase();
  }
}
#else
void crashLogBegin() {}
#endif
