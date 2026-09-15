// Ověří šifrovanou obálku zálohy nastavení na počítači: že klíč a šifra
// odpovídají nezávislé implementaci, že špatné heslo ani změna v popisu
// zálohu neotevřou a že se cizí nebo přehnaný soubor odmítne dřív, než by
// hodiny začaly odvozovat klíč.
//
// Referenční hodnoty spočítal Python (hashlib.pbkdf2_hmac) a Node
// (crypto.createCipheriv "aes-256-gcm"), ne tenhle kód.
//
// Překlad viz tools/run_host_tests.sh; potřebuje mbedTLS 3.

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

#include "ClockConfig.h"
#include "SettingsBackup.h"
#include "SettingsBackupCrypto.h"

namespace {

std::string hex(const uint8_t *data, size_t size) {
  std::string text(size * 2, '\0');
  assert(settingsBackupHexEncode(data, size, &text[0], text.size() + 1) ==
         size * 2);
  return text;
}

bool passwordValid(const std::string &password) {
  return settingsBackupPasswordValid(password.data(), password.size());
}

void testPasswordRules() {
  assert(passwordValid("12345678"));
  assert(!passwordValid("1234567"));
  // Znaky se počítají, ne bajty: osm znaků s diakritikou je šestnáct bajtů.
  assert(passwordValid("žluťoučk"));
  assert(!passwordValid("žluťouč"));
  assert(passwordValid(std::string(64, 'a')));
  assert(!passwordValid(std::string(65, 'a')));
  assert(passwordValid("heslo s mezerou"));
  assert(!passwordValid(std::string("heslo\0heslo", 11)));
  assert(!passwordValid("heslo\theslo"));
  assert(!passwordValid("heslo\x7f" "heslo"));
  // Neplatné UTF-8: osamocený pokračovací bajt, useknutý znak, přehnaně
  // dlouhý zápis lomítka a náhradní pár.
  assert(!passwordValid("heslo\x80heslo"));
  assert(!passwordValid("hesloheslo\xc5"));
  assert(!passwordValid("heslo\xc0\xafheslo"));
  assert(!passwordValid("heslo\xed\xa0\x80heslo"));
  assert(passwordValid("emoji 😀😀"));
}

void testHex() {
  const uint8_t bytes[] = {0x00, 0x7f, 0x80, 0xff};
  assert(hex(bytes, sizeof(bytes)) == "007f80ff");
  uint8_t decoded[4] = {};
  assert(settingsBackupHexDecode("007F80ff", 8, decoded, sizeof(decoded)));
  assert(memcmp(decoded, bytes, sizeof(bytes)) == 0);
  assert(!settingsBackupHexDecode("007f80f", 7, decoded, sizeof(decoded)));
  assert(!settingsBackupHexDecode("007f80fg", 8, decoded, sizeof(decoded)));
  char small[8];
  assert(settingsBackupHexEncode(bytes, sizeof(bytes), small, sizeof(small)) == 0);
}

void testKeyMatchesReference() {
  uint8_t salt[SETTINGS_BACKUP_SALT_SIZE];
  for (size_t index = 0; index < sizeof(salt); ++index)
    salt[index] = static_cast<uint8_t>(index);
  const std::string password = "heslo-zálohy";
  uint8_t key[SETTINGS_BACKUP_KEY_SIZE];
  assert(settingsBackupDeriveKey(password.data(), password.size(), salt, 10000,
                                 key));
  assert(hex(key, sizeof(key)) ==
         "223d116461d68a45cf22784b468aead624bf368bb183742c5aaed72e48fa77a4");

  // Mimo povolený rozsah se klíč vůbec nepočítá.
  assert(!settingsBackupDeriveKey(password.data(), password.size(), salt,
                                  SETTINGS_BACKUP_KDF_MIN_ITERATIONS - 1, key));
  assert(!settingsBackupDeriveKey(password.data(), password.size(), salt,
                                  SETTINGS_BACKUP_KDF_MAX_ITERATIONS + 1, key));
  assert(!settingsBackupDeriveKey("kratke", 6, salt, 10000, key));
}

void testCipherMatchesReference() {
  uint8_t key[SETTINGS_BACKUP_KEY_SIZE];
  for (size_t index = 0; index < sizeof(key); ++index)
    key[index] = static_cast<uint8_t>(index);
  uint8_t nonce[SETTINGS_BACKUP_NONCE_SIZE];
  for (size_t index = 0; index < sizeof(nonce); ++index)
    nonce[index] = static_cast<uint8_t>(0xA0 + index);
  const char aad[] = "waveshare-hodiny";
  const char plainText[] = "Nastaveni hodin";
  const size_t plainLength = strlen(plainText);

  uint8_t sealed[64];
  const size_t sealedLength = settingsBackupSeal(
      key, nonce, aad, strlen(aad), reinterpret_cast<const uint8_t *>(plainText),
      plainLength, sealed, sizeof(sealed));
  assert(sealedLength == plainLength + SETTINGS_BACKUP_TAG_SIZE);
  assert(hex(sealed, plainLength) == "a8790f5924bd67d10b45efbc6313ae");
  assert(hex(sealed + plainLength, SETTINGS_BACKUP_TAG_SIZE) ==
         "e0ca957b0a2d8417211a5a9a87902b52");

  uint8_t plain[64];
  size_t opened = 0;
  assert(settingsBackupOpen(key, nonce, aad, strlen(aad), sealed, sealedLength,
                            plain, sizeof(plain), opened));
  assert(opened == plainLength && memcmp(plain, plainText, plainLength) == 0);

  // Změněný bajt dat, značky i přidaných dat zálohu neotevře a výstup
  // nezůstane napůl dešifrovaný.
  for (size_t position : {size_t{0}, sealedLength - 1}) {
    std::vector<uint8_t> damaged(sealed, sealed + sealedLength);
    damaged[position] ^= 0x01;
    memset(plain, 0x55, sizeof(plain));
    assert(!settingsBackupOpen(key, nonce, aad, strlen(aad), damaged.data(),
                               damaged.size(), plain, sizeof(plain), opened));
    assert(opened == 0);
    for (uint8_t byte : plain) assert(byte == 0);
  }
  assert(!settingsBackupOpen(key, nonce, "waveshare-hodinY", strlen(aad),
                             sealed, sealedLength, plain, sizeof(plain), opened));
  key[0] ^= 0x01;
  assert(!settingsBackupOpen(key, nonce, aad, strlen(aad), sealed, sealedLength,
                             plain, sizeof(plain), opened));
  // Výstup menší než data se odmítne ještě před dešifrováním.
  key[0] ^= 0x01;
  assert(!settingsBackupOpen(key, nonce, aad, strlen(aad), sealed, sealedLength,
                             plain, plainLength - 1, opened));
}

SettingsBackupEnvelope sampleEnvelope() {
  SettingsBackupEnvelope envelope;
  envelope.version = SETTINGS_BACKUP_ENVELOPE_ENCRYPTED;
  strcpy(envelope.firmware, "2.2.0");
  envelope.schema = 41;
  envelope.secrets = true;
  strcpy(envelope.exportedAt, "2026-09-14T10:00:00Z");
  envelope.iterations = SETTINGS_BACKUP_KDF_ITERATIONS;
  for (size_t index = 0; index < sizeof(envelope.salt); ++index)
    envelope.salt[index] = static_cast<uint8_t>(0x10 + index);
  for (size_t index = 0; index < sizeof(envelope.nonce); ++index)
    envelope.nonce[index] = static_cast<uint8_t>(0x80 + index);
  return envelope;
}

// Obálka tak, jak ji skládá ConfigurationWeb.cpp.
std::string envelopeJson(const SettingsBackupEnvelope &envelope,
                         const std::string &data, bool pretty = false) {
  const std::string separator = pretty ? ",\n  " : ",";
  const std::string colon = pretty ? ": " : ":";
  std::string json = pretty ? "{\n  " : "{";
  auto member = [&](const char *key, const std::string &value, bool quoted) {
    if (json.size() > (pretty ? 4u : 1u)) json += separator;
    json += std::string("\"") + key + "\"" + colon;
    json += quoted ? "\"" + value + "\"" : value;
  };
  member("format", "waveshare-hodiny-settings", true);
  member("version", std::to_string(envelope.version), false);
  member("firmware", envelope.firmware, true);
  member("schema", std::to_string(envelope.schema), false);
  member("secrets", envelope.secrets ? "true" : "false", false);
  if (envelope.exportedAt[0] != '\0')
    member("exportedAt", envelope.exportedAt, true);
  member("cipher", "AES-256-GCM", true);
  member("kdf", "PBKDF2-SHA256", true);
  member("iterations", std::to_string(envelope.iterations), false);
  member("salt", hex(envelope.salt, sizeof(envelope.salt)), true);
  member("nonce", hex(envelope.nonce, sizeof(envelope.nonce)), true);
  member("data", data, true);
  json += pretty ? "\n}\n" : "}";
  return json;
}

SettingsBackupEnvelopeStatus parse(const std::string &json,
                                   SettingsBackupEnvelope &envelope) {
  return settingsBackupParseEnvelope(json.data(), json.data() + json.size(),
                                     envelope);
}

std::string replaced(std::string text, const std::string &from,
                     const std::string &to) {
  const size_t position = text.find(from);
  assert(position != std::string::npos);
  return text.replace(position, from.size(), to);
}

void testEnvelopeParsing() {
  const SettingsBackupEnvelope source = sampleEnvelope();
  char expectedAad[SETTINGS_BACKUP_ASSOCIATED_DATA_LENGTH];
  assert(settingsBackupAssociatedData(source, expectedAad, sizeof(expectedAad)) > 0);

  for (bool pretty : {false, true}) {
    const std::string json = envelopeJson(source, "QUJD-_", pretty);
    SettingsBackupEnvelope parsed;
    assert(parse(json, parsed) == SettingsBackupEnvelopeStatus::Ok);
    assert(parsed.version == 4 && parsed.schema == 41 && parsed.secrets);
    assert(strcmp(parsed.firmware, "2.2.0") == 0);
    assert(parsed.iterations == SETTINGS_BACKUP_KDF_ITERATIONS);
    assert(std::string(parsed.data, parsed.dataLength) == "QUJD-_");
    // Přeformátovaný soubor dá stejná přidaná data jako původní.
    char aad[SETTINGS_BACKUP_ASSOCIATED_DATA_LENGTH];
    assert(settingsBackupAssociatedData(parsed, aad, sizeof(aad)) > 0);
    assert(strcmp(aad, expectedAad) == 0);
  }

  const std::string json = envelopeJson(source, "QUJD");
  SettingsBackupEnvelope parsed;
  assert(parse(replaced(json, "\"version\":4", "\"version\":5"), parsed) ==
         SettingsBackupEnvelopeStatus::UnsupportedVersion);
  assert(parse(replaced(json, "waveshare-hodiny-settings", "jiny-format"),
               parsed) == SettingsBackupEnvelopeStatus::Malformed);
  // Nulová, desetinná i přehnaná práce se odmítne před odvozením klíče.
  for (const char *iterations : {"0", "9999", "100001", "20000.5", "2e4",
                                 "\"20000\"", "4294967296000"}) {
    assert(parse(replaced(json, "\"iterations\":20000",
                          std::string("\"iterations\":") + iterations),
                 parsed) == SettingsBackupEnvelopeStatus::Malformed);
  }
  assert(parse(replaced(json, "\"cipher\":\"AES-256-GCM\"",
                        "\"cipher\":\"AES-128-GCM\""),
               parsed) == SettingsBackupEnvelopeStatus::Malformed);
  assert(parse(replaced(json, "\"salt\":\"10", "\"salt\":\"1"), parsed) ==
         SettingsBackupEnvelopeStatus::Malformed);
  assert(parse(replaced(json, "\"nonce\":\"80", "\"nonce\":\"zz"), parsed) ==
         SettingsBackupEnvelopeStatus::Malformed);
  assert(parse(replaced(json, "\"secrets\":true", "\"secrets\":1"), parsed) ==
         SettingsBackupEnvelopeStatus::Malformed);
  assert(parse(replaced(json, "\"firmware\":\"2.2.0\"",
                        "\"firmware\":\"2.2\\n0\""),
               parsed) == SettingsBackupEnvelopeStatus::Malformed);
  assert(parse(replaced(json, "\"data\":\"QUJD\"", "\"data\":\"QU JD\""),
               parsed) == SettingsBackupEnvelopeStatus::Malformed);
  assert(parse(replaced(json, "\"data\":\"QUJD\"", "\"data\":\"\""), parsed) ==
         SettingsBackupEnvelopeStatus::Malformed);
  assert(parse(json.substr(0, json.size() / 2), parsed) !=
         SettingsBackupEnvelopeStatus::Ok);
  assert(parse("[]", parsed) == SettingsBackupEnvelopeStatus::Malformed);

  // Bez času exportu je pole prázdné, ne chybějící hodnota v přidaných datech.
  SettingsBackupEnvelope untimed = source;
  untimed.exportedAt[0] = '\0';
  assert(parse(envelopeJson(untimed, "QUJD"), parsed) ==
         SettingsBackupEnvelopeStatus::Ok);
  assert(parsed.exportedAt[0] == '\0');

  // Nešifrovanou verzi 3 hodiny neotevřou.
  const std::string plain =
      "{\"format\":\"waveshare-hodiny-settings\",\"version\":3,"
      "\"firmware\":\"2.1.2\",\"schema\":40,\"secrets\":true,\"data\":\"QUJD\"}";
  assert(parse(plain, parsed) == SettingsBackupEnvelopeStatus::Malformed);
}

// Celá cesta jako na hodinách: záloha, zapečetění, JSON, přečtení a otevření.
void testEncryptedBackupRoundTrip() {
  ClockConfig source;
  clockConfigApplyDefaults(source);
  clockConfigCopy(source.homeAssistantToken, sizeof(source.homeAssistantToken),
                  "secret-ha-token");
  source.dayBrightness = 42;
  ClockConfig expected = source;
  SettingsBackupContent content;
  content.secrets = true;
  std::vector<uint8_t> plain(SETTINGS_BACKUP_MAX_BYTES);
  const size_t plainLength =
      settingsBackupEncode(source, content, plain.data(), plain.size());
  assert(plainLength > 0);

  SettingsBackupEnvelope envelope = sampleEnvelope();
  envelope.iterations = SETTINGS_BACKUP_KDF_MIN_ITERATIONS;
  const std::string password = "správné heslo";
  uint8_t key[SETTINGS_BACKUP_KEY_SIZE];
  assert(settingsBackupDeriveKey(password.data(), password.size(), envelope.salt,
                                 envelope.iterations, key));
  char aad[SETTINGS_BACKUP_ASSOCIATED_DATA_LENGTH];
  size_t aadLength = settingsBackupAssociatedData(envelope, aad, sizeof(aad));
  std::vector<uint8_t> sealed(plainLength + SETTINGS_BACKUP_TAG_SIZE);
  assert(settingsBackupSeal(key, envelope.nonce, aad, aadLength, plain.data(),
                            plainLength, sealed.data(), sealed.size()) ==
         sealed.size());
  std::vector<char> text(SETTINGS_BACKUP_MAX_TEXT_BYTES + 64);
  assert(settingsBackupBase64Encode(sealed.data(), sealed.size(), text.data(),
                                    text.size()) > 0);
  const std::string json = envelopeJson(envelope, text.data(), true);
  // Tajemství nesmí v souboru ležet čitelně.
  assert(json.find("secret-ha-token") == std::string::npos);

  auto open = [&](const std::string &file, const std::string &attempt,
                  ClockConfig &restored) {
    SettingsBackupEnvelope parsed;
    if (parse(file, parsed) != SettingsBackupEnvelopeStatus::Ok) return false;
    std::vector<uint8_t> decoded(SETTINGS_BACKUP_MAX_BYTES + SETTINGS_BACKUP_TAG_SIZE);
    size_t decodedLength = 0;
    assert(settingsBackupBase64Decode(parsed.data, parsed.dataLength,
                                      decoded.data(), decoded.size(),
                                      decodedLength));
    uint8_t attemptKey[SETTINGS_BACKUP_KEY_SIZE];
    assert(settingsBackupDeriveKey(attempt.data(), attempt.size(), parsed.salt,
                                   parsed.iterations, attemptKey));
    char parsedAad[SETTINGS_BACKUP_ASSOCIATED_DATA_LENGTH];
    const size_t parsedAadLength =
        settingsBackupAssociatedData(parsed, parsedAad, sizeof(parsedAad));
    std::vector<uint8_t> opened(SETTINGS_BACKUP_MAX_BYTES);
    size_t openedLength = 0;
    if (!settingsBackupOpen(attemptKey, parsed.nonce, parsedAad, parsedAadLength,
                            decoded.data(), decodedLength, opened.data(),
                            opened.size(), openedLength))
      return false;
    SettingsBackupContent restoredContent;
    return settingsBackupDecode(opened.data(), openedLength, restored,
                                restoredContent) == SettingsBackupStatus::Ok &&
           restoredContent.secrets == parsed.secrets;
  };

  ClockConfig restored;
  assert(open(json, password, restored));
  assert(strcmp(restored.homeAssistantToken, "secret-ha-token") == 0);
  assert(restored.dayBrightness == expected.dayBrightness);
  assert(!open(json, "spravne heslo", restored));
  // Přepsaný popis zálohu neotevře, i když šifrový text zůstal netknutý.
  assert(!open(replaced(json, "\"secrets\": true", "\"secrets\": false"),
               password, restored));
  assert(!open(replaced(json, "\"schema\": 41", "\"schema\": 40"), password,
               restored));
  assert(!open(replaced(json, "\"firmware\": \"2.2.0\"",
                        "\"firmware\": \"9.9.9\""),
               password, restored));
}

}  // namespace

int main() {
  testPasswordRules();
  testHex();
  testKeyMatchesReference();
  testCipherMatchesReference();
  testEnvelopeParsing();
  testEncryptedBackupRoundTrip();
  return 0;
}
