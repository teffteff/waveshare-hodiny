#pragma once

#include <Arduino.h>

#include "RemoteConfig.h"

// Vzdálené nastavení přes server (infra/remote-config). Protokol viz RemoteConfig.h.
//
// Zapíná se jen na hodinách samých, z domácí sítě: adresa serveru a token od
// `serve.py add-device` leží v NVS (oddíl clockcfg, jmenný prostor
// remote-config), nejsou v ClockConfig ani v záloze a přes server je změnit
// nejde. Požadavky ze serveru jdou na vlastní web přes loopback s klíčem,
// který zná jen tahle úloha (configurationWebLoopbackKey), takže se na ně
// vztahují všechny kontroly webu kromě přihlášení heslem webu: tu nahrazuje
// přihlášení na serveru (heslo + TOTP).
//
// Vlastní úloha se zásobníkem v PSRAM: spojení se ověřuje proti svazku kořenů
// Mozilly a drží se otevřené, dokud je vzdálené nastavení zapnuté.

struct RemoteConfigSettings {
  bool enabled = false;
  char url[REMOTE_CONFIG_URL_LENGTH] = "";
  bool tokenSet = false;
};

struct RemoteConfigStatus {
  bool connected = false;
  // Kolik sekund uplynulo od poslední odpovědi serveru; UINT32_MAX = nikdy.
  uint32_t secondsSinceContact = UINT32_MAX;
  uint32_t requestsServed = 0;
  char message[80] = "";
};

enum class RemoteConfigSaveResult : uint8_t {
  Ok,
  InvalidUrl,
  InvalidToken,
  MissingToken,
  StorageFailed,
};

void remoteConfigServiceBegin();
void remoteConfigServicePrepareForFirmwareUpdate();
// Nové jméno hodin po přejmenování na webu; úloha samotná NVS číst nesmí.
void remoteConfigServiceSetDeviceName(const char *name);

void remoteConfigServiceSettings(RemoteConfigSettings &settings);
// Prázdný token nechá uložený. Zapnout bez adresy a tokenu nejde.
RemoteConfigSaveResult remoteConfigServiceSave(bool enabled, const char *url,
                                             const char *token);
// Smaže adresu i token a spojení zavře.
bool remoteConfigServiceForget();
void remoteConfigServiceStatus(RemoteConfigStatus &status);
