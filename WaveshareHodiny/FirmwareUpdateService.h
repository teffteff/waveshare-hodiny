#pragma once

#include <Arduino.h>

enum class FirmwareUpdateState : uint8_t {
  Idle,
  Checking,
  Available,
  Current,
  Downloading,
  Failed,
  Restarting,
};

struct FirmwareUpdateSnapshot {
  FirmwareUpdateState state = FirmwareUpdateState::Idle;
  char currentVersion[48] = "";
  char serverVersion[48] = "";
  char message[160] = "";
  uint32_t downloadedBytes = 0;
  uint32_t totalBytes = 0;
  bool updateAvailable = false;
  bool busy = false;
  bool installationSupported = false;
};

using FirmwareUpdateLifecycleCallback = void (*)(bool updating);

void firmwareUpdateServiceBegin(FirmwareUpdateLifecycleCallback callback);
bool firmwareUpdateServiceRequestCheck(bool installWhenAvailable);
// Volá se úplně na začátku loop(). Když se po OTA chystá restart, hlavní
// smyčka tu zůstane stát, tedy mezi požadavky webu, a restart počká, až tu je.
// 24. 9. 2026 spadly pracovna i barvlevo, protože restart z jádra 0
// resetoval periferie ve chvíli, kdy loopTask na jádře 1 ověřoval heslo webu
// přes hardwarové SHA (clock-sync.py se během aktualizace přihlašuje).
void firmwareUpdateServiceParkLoopBeforeRestart();
FirmwareUpdateSnapshot firmwareUpdateServiceSnapshot();
const char *firmwareUpdateStateName(FirmwareUpdateState state);
