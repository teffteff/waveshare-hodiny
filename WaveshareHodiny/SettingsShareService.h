#pragma once

#include <Arduino.h>

#include "SettingsBackup.h"

// Přenos zálohy nastavení na vlastní server a zpět (infra/settings/).
//
// Adresa má tvar https://hodiny:heslo@server/settings, stejně jako agenda:
// HTTPClient si jméno a heslo vytáhne sám a přiloží je jako basic auth.
// Seznam leží na <adresa>/, jednotlivá záloha na <adresa>/<název>.
//
// Ověření proti svazku kořenů Mozilly potřebuje přes 16 kB zásobníku, takže se
// volá výhradně z úlohy agendy, nikdy ze smyčky displeje ani z web serveru.

constexpr size_t SETTINGS_SHARE_MESSAGE_LENGTH = 96;
// Obálka zálohy má kolem 13 kB, seznam s desítkami položek pár kilobajtů.
constexpr size_t SETTINGS_SHARE_MAX_RESPONSE_BYTES = 32 * 1024;

enum class SettingsShareOperation : uint8_t { List, Upload, Download };

struct SettingsShareRequest {
  SettingsShareOperation operation = SettingsShareOperation::List;
  char url[SETTINGS_SHARE_URL_LENGTH] = "";
  char name[SETTINGS_BACKUP_NAME_LENGTH] = "";
  // Tělo pro Upload. Patří volajícímu a musí žít až do dokončení.
  const char *body = nullptr;
  size_t bodyLength = 0;
};

struct SettingsShareResult {
  bool ok = false;
  int httpStatus = 0;
  char error[SETTINGS_SHARE_MESSAGE_LENGTH] = "";
  // Tělo odpovědi u List a Download. Leží v bufferu služby a platí do další
  // operace.
  const char *body = nullptr;
  size_t bodyLength = 0;
};

void settingsShareExecute(const SettingsShareRequest &request,
                          SettingsShareResult &result);
