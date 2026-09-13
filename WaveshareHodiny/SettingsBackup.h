#pragma once

#include <Arduino.h>

#include "ClockConfig.h"

// Záloha nastavení pro přenos mezi hodinami - do souboru i přes server.
//
// Dřívější záloha vznikala v prohlížeči z GET /api/config, takže nesla jen to,
// co web umí zobrazit, a formulář ji musel znovu poskládat. Tahle ji skládá
// firmware z toho, co opravdu leží v NVS: celý záznam ClockConfig bajt po
// bajtu, vzhled hodin, režim webu a volitelně tajemství. Nic, co hodiny
// uloží, se tak cestou neztratí.
//
// Formát je binární a malý, do JSONu se vkládá jako base64url:
//
//   "WHSB"  u8 verze  u8 příznaky  u16 rezerva
//   oddíly: u16 značka, u16 délka, data     (little endian)
//   u32 FNV-1a přes všechno předtím
//
// Neznámé značky se přeskočí, takže novější firmware může přidat oddíl, aniž by
// starší přestal zálohu číst.
//
// Co v záloze schválně není: Wi-Fi (bez ní se k hodinám nejde dostat, a leží
// v jiném oddílu flash) a secret ovládacího API - ten je identitou konkrétních
// hodin, na kterou míří automatizace, ne nastavením ke sdílení.

constexpr size_t SETTINGS_SHARE_URL_LENGTH = 192;
// Název zálohy na serveru včetně ukončovací nuly: malá písmena, číslice a
// pomlčka, nejvýš 32 znaků. Server stejný tvar vynucuje sám.
constexpr size_t SETTINGS_BACKUP_NAME_LENGTH = 33;
// Uložené heslo webu je sůl a odvozený hash (WebPasswordRecord
// v ConfigurationWeb.cpp). Pro zálohu je to neprůhledný blok pevné délky;
// platnost ověří až web, který mu rozumí.
constexpr size_t SETTINGS_BACKUP_WEB_PASSWORD_SIZE = 56;
// Záznam konfigurace má přes 8,7 kB, zbytek oddílů pár set bajtů. Rezerva
// pokryje několik dalších schémat.
constexpr size_t SETTINGS_BACKUP_MAX_BYTES = 16 * 1024;
// base64 ze stropu výše i s ukončovací nulou.
constexpr size_t SETTINGS_BACKUP_MAX_TEXT_BYTES =
    (SETTINGS_BACKUP_MAX_BYTES + 2) / 3 * 4 + 1;

struct SettingsBackupContent {
  // Záloha nese token Home Assistantu, exportní klíč TMEP, heslo k soukromým
  // kalendářům agendy, heslo webu a adresu serveru pro sdílení. Bez příznaku jsou pole v konfiguraci
  // prázdná a oddíly s heslem a adresou chybí.
  bool secrets = false;
  ClockAppearanceConfig appearance;
  uint8_t webMode = 1;
  bool webPasswordPresent = false;
  uint8_t webPassword[SETTINGS_BACKUP_WEB_PASSWORD_SIZE] = {};
  char shareUrl[SETTINGS_SHARE_URL_LENGTH] = "";
};

enum class SettingsBackupStatus : uint8_t {
  Ok,
  // Nejde o zálohu hodin, nebo je useknutá.
  Malformed,
  // Tvar sedí, ale kontrolní součet ne.
  Corrupted,
  // Záznam konfigurace je z novějšího firmwaru.
  NewerFirmware,
};

// Bez content.secrets vymaže tajemství přímo v `config`, proto není const -
// volající předává vlastní kopii. Vrací délku zálohy, nebo 0, když se nevejde.
size_t settingsBackupEncode(ClockConfig &config,
                            const SettingsBackupContent &content,
                            uint8_t *output, size_t capacity);

SettingsBackupStatus settingsBackupDecode(const uint8_t *data, size_t size,
                                          ClockConfig &config,
                                          SettingsBackupContent &content);

// Vymaže z konfigurace to, co se bez hesla ze zařízení nesmí dostat ven.
void settingsBackupStripSecrets(ClockConfig &config);

// Části nastavení, které jde při obnově vybrat zvlášť. Záloha nese vždycky
// všechno; o tom, co z ní hodiny převezmou, se rozhoduje až při importu, takže
// jeden soubor poslouží k úplnému klonu i k přenesení jen hodnot na hodiny
// s jiným připojením. Wi-Fi v záloze není nikdy, výběr se jí netýká.
enum SettingsBackupPart : uint8_t {
  // Zdroj dat, Home Assistant s tokenem, TMEP.cz a poloha hodin.
  SETTINGS_BACKUP_PART_CONNECTION = 0x01,
  // Hodnoty na obrazovce hodin: strany, měřené hodnoty, obě stránky mřížky,
  // sloty Open-Meteo a TMEP i entita počasí pro jejich ikonu.
  SETTINGS_BACKUP_PART_VALUES = 0x02,
  // Předpověď, meteoradar, letadla, zprávy, agenda a pořadí a střídání
  // obrazovek.
  SETTINGS_BACKUP_PART_SCREENS = 0x04,
  // Vzhled hodin, barvy, fonty, vteřiny, ikony počasí, jas a režim den/noc.
  SETTINGS_BACKUP_PART_DISPLAY = 0x08,
  // Jazyk, automatická aktualizace, režim webu, heslo webu a server záloh.
  SETTINGS_BACKUP_PART_SYSTEM = 0x10,
};
constexpr uint8_t SETTINGS_BACKUP_PARTS_ALL = 0x1F;

// Vrátí do `incoming` hodnoty z `current` u všech částí konfigurace, které
// v `parts` nejsou. Vzhled, režim webu, heslo a server záloh leží mimo
// ClockConfig a o ty se stará volající.
void settingsBackupKeepCurrentParts(ClockConfig &incoming,
                                    const ClockConfig &current, uint8_t parts);

// Čárkami oddělené názvy částí z webu ("connection,values,..."). Prázdný text
// znamená všechno, aby starší stránka a skripty obnovovaly jako dřív. Vrací
// false u neznámého názvu nebo prázdného výběru.
bool settingsBackupParseParts(const char *text, uint8_t &parts);

// base64url bez doplnění: nepotřebuje escapovat v JSONu ani ve formuláři.
// Vrací délku textu bez nuly, nebo 0, když se nevejde.
size_t settingsBackupBase64Encode(const uint8_t *data, size_t size,
                                  char *output, size_t capacity);
// Přijme base64url i klasické base64, s doplněním i bez. Vrací false u
// jakéhokoli jiného znaku nebo když se výsledek nevejde.
bool settingsBackupBase64Decode(const char *text, size_t length,
                                uint8_t *output, size_t capacity,
                                size_t &written);

bool settingsBackupValidName(const char *name);
// Adresa serveru pro sdílení. Musí to být https://: nese heslo a po ní jde
// token Home Assistantu.
bool settingsShareValidUrl(const char *url);
// Adresa bez jména a hesla, aby se dala ukázat na webu.
String settingsShareDisplayUrl(const char *url);
