#pragma once

#include <stddef.h>
#include <stdint.h>

// Šifrovaná obálka zálohy nastavení (verze 4).
//
// Samotná záloha (SettingsBackup.h) nese token Home Assistantu, heslo agendy
// a adresy s hesly k serverům letadel a blesků. Do verze 3 ležela v souboru
// i na serveru jen v base64, takže ji přečetl každý, kdo soubor získal. Od
// verze 4 je zašifrovaná heslem zálohy, které hodiny nikam neukládají:
//
//   {"format":"waveshare-hodiny-settings","version":4,
//    "firmware":"2.2.0","schema":41,"secrets":true,
//    "exportedAt":"2026-09-14T10:00:00Z",
//    "cipher":"AES-256-GCM","kdf":"PBKDF2-SHA256","iterations":20000,
//    "salt":"<16 B hex>","nonce":"<12 B hex>",
//    "data":"<base64url(šifrový text + 16 B značky)>"}
//
// Klíč je PBKDF2-HMAC-SHA256 z hesla a náhodné soli, šifra AES-256-GCM
// s náhodnou nonce. Popis kolem dat zůstává čitelný, aby ho server mohl ukázat
// v seznamu, ale je autentizovaný: jde do GCM jako přidaná data v kanonickém
// tvaru (settingsBackupAssociatedData), takže přepsaná verze, schéma nebo
// příznak tajemství zálohu neotevře. Kanonický tvar místo přesných bajtů
// souboru znamená, že zálohu nerozbije ani prohlížeč, který JSON přeformátuje.
//
// Modul nesahá na Arduino ani na NVS a sůl s nonce dostává od volajícího, aby
// šel celý otestovat na počítači proti mbedTLS.

constexpr int SETTINGS_BACKUP_ENVELOPE_PLAIN = 3;
constexpr int SETTINGS_BACKUP_ENVELOPE_ENCRYPTED = 4;

constexpr size_t SETTINGS_BACKUP_SALT_SIZE = 16;
constexpr size_t SETTINGS_BACKUP_NONCE_SIZE = 12;
constexpr size_t SETTINGS_BACKUP_KEY_SIZE = 32;
constexpr size_t SETTINGS_BACKUP_TAG_SIZE = 16;

// Odvození klíče běží na ESP32-S3 mimo smyčku displeje a trvá zhruba
// sekundu na deset tisíc iterací. Víc iterací by obnovu protáhlo, aniž by
// zachránilo slabé heslo; proti hádání chrání hlavně délka hesla, proto
// stránka umí vygenerovat silné.
constexpr uint32_t SETTINGS_BACKUP_KDF_ITERATIONS = 20000;
// Obnova přijme jen rozumný rozsah: cizí soubor s miliardou iterací by jinak
// zaměstnal hodiny na hodiny.
constexpr uint32_t SETTINGS_BACKUP_KDF_MIN_ITERATIONS = 10000;
constexpr uint32_t SETTINGS_BACKUP_KDF_MAX_ITERATIONS = 100000;

// Heslo zálohy: 8 až 64 znaků Unicode, platné UTF-8 bez řídicích znaků.
// Hodiny ho nenormalizují, klíč vzniká z přesných bajtů.
constexpr size_t SETTINGS_BACKUP_PASSWORD_MIN_CHARACTERS = 8;
constexpr size_t SETTINGS_BACKUP_PASSWORD_MAX_CHARACTERS = 64;
constexpr size_t SETTINGS_BACKUP_PASSWORD_MAX_BYTES = 256;

constexpr size_t SETTINGS_BACKUP_FIRMWARE_LENGTH = 32;
constexpr size_t SETTINGS_BACKUP_TIMESTAMP_LENGTH = 24;
// Kanonická přidaná data včetně ukončovací nuly.
constexpr size_t SETTINGS_BACKUP_ASSOCIATED_DATA_LENGTH = 192;

struct SettingsBackupEnvelope {
  int version = 0;
  char firmware[SETTINGS_BACKUP_FIRMWARE_LENGTH] = "";
  int schema = 0;
  bool secrets = false;
  // Prázdné, když hodiny při zálohování neměly synchronizovaný čas.
  char exportedAt[SETTINGS_BACKUP_TIMESTAMP_LENGTH] = "";
  uint32_t iterations = 0;
  uint8_t salt[SETTINGS_BACKUP_SALT_SIZE] = {};
  uint8_t nonce[SETTINGS_BACKUP_NONCE_SIZE] = {};
  // base64url dat uvnitř zdrojového textu; platí, dokud žije ten text.
  const char *data = nullptr;
  size_t dataLength = 0;
};

enum class SettingsBackupEnvelopeStatus : uint8_t {
  Ok,
  // Nejde o obálku zálohy hodin, nebo v ní něco chybí či přebývá.
  Malformed,
  // Obálka hodin, ale verze, kterou tenhle firmware nezná.
  UnsupportedVersion,
};

// Přečte obálku verze 3 i 4 z JSONu. U verze 3 zůstanou šifrovací pole
// prázdná.
SettingsBackupEnvelopeStatus settingsBackupParseEnvelope(
    const char *begin, const char *end, SettingsBackupEnvelope &envelope);

// Zkopíruje verzi firmwaru nebo čas do popisu obálky. Znaky, které by JSON
// musel escapovat nebo které by čtení odmítlo, nahradí pomlčkou, aby hodiny
// nikdy nevyrobily zálohu, kterou samy neotevřou.
void settingsBackupCopyLabel(char *output, size_t capacity, const char *text);

// Kanonický text popisu obálky 4 pro GCM. Vrací délku bez nuly, nebo 0.
size_t settingsBackupAssociatedData(const SettingsBackupEnvelope &envelope,
                                    char *output, size_t capacity);

bool settingsBackupPasswordValid(const char *password, size_t length);

// Pomalá část: volat mimo smyčku displeje.
bool settingsBackupDeriveKey(const char *password, size_t length,
                             const uint8_t *salt, uint32_t iterations,
                             uint8_t *key);

// Zašifruje `plain` a za šifrový text připojí značku. Vrací délku výstupu
// (plainLength + SETTINGS_BACKUP_TAG_SIZE), nebo 0. Výstup se nesmí překrývat
// se vstupem.
size_t settingsBackupSeal(const uint8_t *key, const uint8_t *nonce,
                          const char *associatedData, size_t associatedLength,
                          const uint8_t *plain, size_t plainLength,
                          uint8_t *output, size_t capacity);

// Ověří značku a dešifruje. False u špatného hesla i u jakékoli změny
// v datech nebo v popisu - GCM ty dvě věci nerozliší. Při neúspěchu je
// výstup vymazaný.
bool settingsBackupOpen(const uint8_t *key, const uint8_t *nonce,
                        const char *associatedData, size_t associatedLength,
                        const uint8_t *sealed, size_t sealedLength,
                        uint8_t *plain, size_t capacity, size_t &plainLength);

// Malá písmena, bez nuly na konci výstupu se nepočítá. Vrací délku textu,
// nebo 0, když se nevejde.
size_t settingsBackupHexEncode(const uint8_t *data, size_t size, char *output,
                               size_t capacity);
// Přijme přesně 2 * size hexadecimálních znaků.
bool settingsBackupHexDecode(const char *text, size_t length, uint8_t *output,
                             size_t size);

void settingsBackupZeroize(void *data, size_t size);
