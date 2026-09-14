#pragma once

#include <Arduino.h>

#include "LightningFeed.h"

// Realtime blesky z vlastního serveru (infra/lightning).
//
// Trvalé spojení na LightningMaps drží server, jedno pro všechny hodiny
// v domácnosti; hodiny se ho jen ptají na nové údery v kruhu kolem sebe. Kruh je
// sjednocení okolí polohy, které hlídá výstraha, a pohledu meteoradaru, na
// který se údery kreslí - obojí se tak vejde do jednoho dotazu.
//
// Údery se drží v kruhovém bufferu v PSRAM nejvýš LIGHTNING_RETENTION_SECONDS.

// Radar kreslí dvacet minut, výstraha se dá nastavit nejvýš na třicet.
constexpr uint32_t LIGHTNING_RETENTION_SECONDS = 30 * 60;

struct LightningProximity {
  // Údery v kruhu výstrahy za sledované okno.
  uint16_t count = 0;
  float nearestKm = NAN;
  // Stáří nejnovějšího úderu v kruhu, v sekundách.
  uint32_t newestAgeSeconds = 0;
};

struct LightningDiagnostics {
  bool enabled = false;
  // Poslední odpověď přišla a server hlásil živé spojení na LightningMaps.
  bool live = false;
  uint32_t attempts = 0;
  uint32_t successes = 0;
  int lastHttpStatus = 0;
  uint32_t lastDownloadedBytes = 0;
  uint32_t lastSuccessAgeMs = 0;
  uint32_t nextFetchInMs = 0;
  uint32_t strokesReceived = 0;
  uint16_t bufferedStrokes = 0;
  float requestRadiusKm = 0.0f;
  char message[64] = "";
};

void lightningServiceBegin();
// Zastaví dotazy před aktualizací firmwaru; znovu se nerozběhnou.
void lightningServicePrepareForFirmwareUpdate();

// Zapnutí, adresa serveru a oba kruhy. Volá se klidně v každé smyčce - dotaz
// navíc vyvolá jen skutečná změna. viewRadiusKm == 0 znamená, že se údery na
// radar nekreslí a stačí okolí polohy. radarVisible zkracuje interval dotazů.
void lightningServiceSetActive(bool enabled, const char *feedUrl,
                               float alarmLatitude, float alarmLongitude,
                               float alarmRadiusKm, float viewLatitude,
                               float viewLongitude, float viewRadiusKm,
                               bool radarVisible);

// Roste s každou změnou uložených úderů, aby kreslení poznalo, že má co
// překreslit.
uint32_t lightningServiceGeneration();

// Zkopíruje údery novější než minEpochSeconds. Vrací jejich počet.
size_t lightningServiceCopyStrokes(LightningStroke *output, size_t capacity,
                                   uint32_t minEpochSeconds);

// Údery v kruhu za posledních maxAgeSeconds. Vrací false, když služba neběží
// nebo nemá čerstvá data - "neblýská se" a "nevím" nejsou totéž.
bool lightningServiceProximity(float latitude, float longitude, float radiusKm,
                               uint32_t maxAgeSeconds,
                               LightningProximity &proximity);

void lightningServiceDiagnostics(LightningDiagnostics &diagnostics);
