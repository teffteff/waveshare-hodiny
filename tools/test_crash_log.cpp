// Ověří záznam o pádu: co se počítá jako pád, že se souhrn výpisu uloží
// a přežije do diagnostiky, a že JSON zůstane platný i s poškozeným textem.
//
// Překlad viz tools/run_host_tests.sh.

#include <cassert>
#include <cstring>
#include <string>

#include "CrashLog.h"
#include "Preferences.h"

namespace {

bool contains(const String &haystack, const char *needle) {
  return std::string(haystack.c_str()).find(needle) != std::string::npos;
}

void testAbnormalResets() {
  assert(crashLogAbnormalReset(4));   // panika
  assert(crashLogAbnormalReset(5));   // watchdog přerušení
  assert(crashLogAbnormalReset(6));   // watchdog úloh
  assert(crashLogAbnormalReset(7));   // ostatní watchdogy
  assert(crashLogAbnormalReset(9));   // podpětí
  assert(!crashLogAbnormalReset(1));  // zapnutí
  assert(!crashLogAbnormalReset(3));  // softwarový restart (OTA, uložení)
}

void testNothingToRecord() {
  hostPreferencesReset();
  assert(crashLogRecord(nullptr, 1, "2.2.10"));
  assert(crashLogRecord(nullptr, 3, "2.2.10"));
  assert(std::string(crashLogJson().c_str()) == "{\"count\":0,\"last\":null}");
}

void testWatchdogWithoutDump() {
  hostPreferencesReset();
  assert(crashLogRecord(nullptr, 6, "2.2.10"));
  const String json = crashLogJson();
  assert(contains(json, "\"count\":1"));
  assert(contains(json, "\"resetReason\":6"));
  assert(contains(json, "\"firmware\":\"2.2.10\""));
  assert(contains(json, "\"dump\":false"));
  assert(!contains(json, "\"backtrace\""));
}

void testDumpFoundAfterSoftwareRestart() {
  hostPreferencesReset();
  CrashRecord dump{};
  strcpy(dump.task, "rss");
  strcpy(dump.elfSha, "0123456789abcdef");
  strcpy(dump.reason, "LoadProhibited");
  dump.pc = 0x42012345;
  dump.exceptionCause = 28;
  dump.exceptionAddress = 0x10;
  dump.backtraceDepth = 3;
  dump.backtrace[0] = 0x42012345;
  dump.backtrace[1] = 0x42000abc;
  dump.backtrace[2] = 0x40379def;
  // Starý výpis nalezený až po OTA: reset je softwarový, ale pád se počítá.
  assert(crashLogRecord(&dump, 3, "2.2.11"));
  const String json = crashLogJson();
  assert(contains(json, "\"count\":1"));
  assert(contains(json, "\"resetReason\":3"));
  assert(contains(json, "\"dump\":true"));
  assert(contains(json, "\"task\":\"rss\""));
  assert(contains(json, "\"reason\":\"LoadProhibited\""));
  assert(contains(json, "\"elfSha\":\"0123456789abcdef\""));
  assert(contains(json, "\"pc\":\"0x42012345\""));
  assert(contains(json, "\"exceptionCause\":28"));
  assert(contains(json, "\"exceptionAddress\":\"0x00000010\""));
  assert(contains(json,
                  "\"backtrace\":[\"0x42012345\",\"0x42000abc\",\"0x40379def\"]"));
}

void testCountAccumulatesAndLastWins() {
  hostPreferencesReset();
  assert(crashLogRecord(nullptr, 4, "2.2.10"));
  assert(crashLogRecord(nullptr, 9, "2.2.11"));
  const String json = crashLogJson();
  assert(contains(json, "\"count\":2"));
  assert(contains(json, "\"resetReason\":9"));
  assert(contains(json, "\"firmware\":\"2.2.11\""));
}

void testCorruptTextStaysValidJson() {
  hostPreferencesReset();
  CrashRecord dump{};
  memset(dump.task, 'x', sizeof(dump.task));  // bez koncové nuly
  strcpy(dump.reason, "a\"b\\c\n\x01\xff");
  dump.backtraceDepth = 200;  // víc, než se vejde
  assert(crashLogRecord(&dump, 4, "2.2.10"));
  const String json = crashLogJson();
  assert(contains(json, "\"task\":\"xxxxxxxxxxxxxxx\""));
  assert(contains(json, "\"reason\":\"a\\\"b\\\\c\\u000a\\u0001\\u00ff\""));
  // 16 adres, ne 200.
  size_t addresses = 0;
  for (const char *cursor = strstr(json.c_str(), "\"0x");
       cursor != nullptr; cursor = strstr(cursor + 1, "\"0x"))
    ++addresses;
  assert(addresses == CRASH_BACKTRACE_MAX + 2);  // + pc a exceptionAddress
  assert(std::string(json.c_str()).back() == '}');
}

void testUnknownRecordVersionIgnored() {
  hostPreferencesReset();
  CrashRecord future{};
  future.version = 99;
  hostPreferencesSeedBlob("clockcfg", "crashlog", "last", &future,
                          sizeof(future));
  assert(std::string(crashLogJson().c_str()) == "{\"count\":0,\"last\":null}");
}

void testFirmwareOnlyForSameBuild() {
  const char *running = "8a523fcc7f892856aa11bb22cc33dd44ee55ff6600112233445566778899aabb";
  assert(std::string(crashLogFirmwareFor("8a523fcc7f892856", running, "2.2.14")) == "2.2.14");
  // Pad v predchozim sestaveni (OTA restart): verze bezici by lhala.
  assert(std::string(crashLogFirmwareFor("88960743a89ad7f8", running, "2.2.15")).empty());
  assert(std::string(crashLogFirmwareFor("", running, "2.2.15")).empty());
  assert(std::string(crashLogFirmwareFor("8a523fcc7f892856", "", "2.2.15")).empty());
}

}  // namespace

int main() {
  testFirmwareOnlyForSameBuild();
  testAbnormalResets();
  testNothingToRecord();
  testWatchdogWithoutDump();
  testDumpFoundAfterSoftwareRestart();
  testCountAccumulatesAndLastWins();
  testCorruptTextStaysValidJson();
  testUnknownRecordVersionIgnored();
  return 0;
}
