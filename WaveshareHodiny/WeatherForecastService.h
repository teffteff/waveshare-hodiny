#pragma once

#include <Arduino.h>

#include "NetworkDiagnostics.h"
#include "WeatherForecast.h"

// Malý přehled bez samotné předpovědi, aby se vešel na zásobník volajícího.
struct WeatherForecastStatus {
  uint32_t generation = 0;
  bool ready = false;
  bool loading = false;
  // Poslední stahování skončilo chybou. Odlišuje "ještě jsem nezačal" od
  // "zkoušel jsem to a nepovedlo se", aby obrazovka nehlásila chybu dřív, než
  // vůbec nějaká byla.
  bool failed = false;
  size_t hourCount = 0;
  // Stáří posledního úspěšného stažení. Platí jen s lastSuccessAvailable.
  uint32_t lastSuccessAgeMs = 0;
  bool lastSuccessAvailable = false;
};

void weatherForecastServiceBegin();
void weatherForecastServiceStatus(WeatherForecastStatus &status);
// Zkopíruje předpověď pod zámkem. Vrací false, když zámek nebyl volný nebo
// mezipaměť zatím nic nemá; volající to zkusí při dalším průchodu smyčkou.
bool weatherForecastServiceSnapshot(WeatherForecastData &forecast);
// Stáhne předpověď a případně i kvalitu ovzduší. Bere jen souřadnice a
// přepínač kvality ovzduší, aby úloha nemusela nosit celou ClockConfig - ta má
// přes pět kilobajtů a na zásobník vedle TLS relace se nevejde.
bool weatherForecastServiceFetch(float latitude, float longitude,
                                 bool airQuality,
                                 NetworkDiagnosticKind diagnosticKind);
// Zahodí mezipaměť. Volá se, když se obrazovka vypne nebo změní poloha, aby na
// ní nezůstala předpověď pro jiné město.
void weatherForecastServiceClear();
