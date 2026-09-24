#pragma once

#include <Arduino.h>

#include "RemoteAdmin.h"

// Vzdálená správa přes server (infra/fleet). Protokol viz RemoteAdmin.h.
//
// Zapíná se jen na hodinách samých, z domácí sítě: adresa serveru a token od
// `serve.py add-device` leží v NVS (oddíl clockcfg, jmenný prostor
// remote-admin), nejsou v ClockConfig ani v záloze a přes server je změnit
// nejde. Požadavky ze serveru jdou na vlastní web přes loopback s klíčem,
// který zná jen tahle úloha (configurationWebLoopbackKey), takže se na ně
// vztahují všechny kontroly webu kromě přihlášení heslem webu: tu nahrazuje
// přihlášení na serveru (heslo + TOTP).
//
// Vlastní úloha se zásobníkem v PSRAM: spojení se ověřuje proti svazku kořenů
// Mozilly a drží se otevřené, dokud je správa zapnutá.

struct RemoteAdminSettings {
  bool enabled = false;
  char url[REMOTE_ADMIN_URL_LENGTH] = "";
  bool tokenSet = false;
};

struct RemoteAdminStatus {
  bool connected = false;
  // Kolik sekund uplynulo od poslední odpovědi serveru; UINT32_MAX = nikdy.
  uint32_t secondsSinceContact = UINT32_MAX;
  uint32_t requestsServed = 0;
  char message[80] = "";
};

enum class RemoteAdminSaveResult : uint8_t {
  Ok,
  InvalidUrl,
  InvalidToken,
  MissingToken,
  StorageFailed,
};

void remoteAdminServiceBegin();
void remoteAdminServicePrepareForFirmwareUpdate();
// Nové jméno hodin po přejmenování na webu; úloha samotná NVS číst nesmí.
void remoteAdminServiceSetDeviceName(const char *name);

void remoteAdminServiceSettings(RemoteAdminSettings &settings);
// Prázdný token nechá uložený. Zapnout bez adresy a tokenu nejde.
RemoteAdminSaveResult remoteAdminServiceSave(bool enabled, const char *url,
                                             const char *token);
// Smaže adresu i token a spojení zavře.
bool remoteAdminServiceForget();
void remoteAdminServiceStatus(RemoteAdminStatus &status);
