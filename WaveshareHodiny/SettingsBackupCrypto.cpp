#include "SettingsBackupCrypto.h"

#include <cstdio>
#include <cstring>

#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/platform_util.h>

#include "JsonScan.h"

namespace {
constexpr char FORMAT[] = "waveshare-hodiny-settings";
constexpr char CIPHER[] = "AES-256-GCM";
constexpr char KDF[] = "PBKDF2-SHA256";

bool base64Character(char character) {
  return (character >= 'A' && character <= 'Z') ||
         (character >= 'a' && character <= 'z') ||
         (character >= '0' && character <= '9') || character == '-' ||
         character == '_' || character == '+' || character == '/' ||
         character == '=';
}

// Popisné texty jdou do přidaných dat tak, jak leží v souboru. Úzká abeceda
// zaručí, že je JSON nemohl escapovat ani jinak pozměnit, a že v nich není
// oddělovač řádků kanonického tvaru.
bool plainTextCharacter(char character) {
  return (character >= 'A' && character <= 'Z') ||
         (character >= 'a' && character <= 'z') ||
         (character >= '0' && character <= '9') || character == '.' ||
         character == '-' || character == '+' || character == '_' ||
         character == ':';
}

// Textová hodnota bez escapů. Chybějící klíč je prázdný text, pokud
// `required` nechce jinak.
bool copyPlainText(const char *begin, const char *end, const char *key,
                   char *output, size_t capacity, bool required) {
  output[0] = '\0';
  const JsonValue value = jsonFindMember(begin, end, key);
  if (!value.valid()) return !required;
  if (!value.isString) return false;
  const size_t length =
      static_cast<size_t>(value.contentEnd() - value.contentBegin());
  if (length >= capacity) return false;
  for (size_t index = 0; index < length; ++index) {
    if (!plainTextCharacter(value.contentBegin()[index])) return false;
  }
  memcpy(output, value.contentBegin(), length);
  output[length] = '\0';
  return true;
}

// Celé nezáporné číslo zapsané přesně (bez desetinné čárky a exponentu).
bool readUnsigned(const char *begin, const char *end, const char *key,
                  uint32_t maximum, uint32_t &output) {
  const JsonValue value = jsonFindMember(begin, end, key);
  if (!value.valid() || value.isString || value.begin == value.end)
    return false;
  uint64_t number = 0;
  for (const char *cursor = value.begin; cursor < value.end; ++cursor) {
    if (*cursor < '0' || *cursor > '9') return false;
    number = number * 10 + static_cast<uint64_t>(*cursor - '0');
    if (number > maximum) return false;
  }
  output = static_cast<uint32_t>(number);
  return true;
}

bool readHex(const char *begin, const char *end, const char *key,
             uint8_t *output, size_t size) {
  const JsonValue value = jsonFindMember(begin, end, key);
  return value.valid() && value.isString &&
         settingsBackupHexDecode(
             value.contentBegin(),
             static_cast<size_t>(value.contentEnd() - value.contentBegin()),
             output, size);
}

int hexValue(char character) {
  if (character >= '0' && character <= '9') return character - '0';
  if (character >= 'a' && character <= 'f') return character - 'a' + 10;
  if (character >= 'A' && character <= 'F') return character - 'A' + 10;
  return -1;
}
}  // namespace

SettingsBackupEnvelopeStatus settingsBackupParseEnvelope(
    const char *begin, const char *end, SettingsBackupEnvelope &envelope) {
  envelope = SettingsBackupEnvelope{};
  if (begin == nullptr || end == nullptr || begin >= end)
    return SettingsBackupEnvelopeStatus::Malformed;
  begin = jsonSkipWhitespace(begin, end);
  if (begin >= end || *begin != '{' || !jsonTextMemberEquals(begin, end, "format", FORMAT))
    return SettingsBackupEnvelopeStatus::Malformed;
  uint32_t version = 0;
  if (!readUnsigned(begin, end, "version", 1000, version))
    return SettingsBackupEnvelopeStatus::Malformed;
  // Nešifrované obálky starších verzí hodiny už neotevřou.
  if (version < SETTINGS_BACKUP_ENVELOPE_ENCRYPTED)
    return SettingsBackupEnvelopeStatus::Malformed;
  if (version != SETTINGS_BACKUP_ENVELOPE_ENCRYPTED)
    return SettingsBackupEnvelopeStatus::UnsupportedVersion;
  envelope.version = static_cast<int>(version);

  const JsonValue data = jsonFindMember(begin, end, "data");
  if (!data.valid() || !data.isString ||
      data.contentBegin() == data.contentEnd())
    return SettingsBackupEnvelopeStatus::Malformed;
  for (const char *cursor = data.contentBegin(); cursor < data.contentEnd();
       ++cursor) {
    if (!base64Character(*cursor)) return SettingsBackupEnvelopeStatus::Malformed;
  }
  envelope.data = data.contentBegin();
  envelope.dataLength =
      static_cast<size_t>(data.contentEnd() - data.contentBegin());

  uint32_t schema = 0;
  const JsonValue secrets = jsonFindMember(begin, end, "secrets");
  if (!copyPlainText(begin, end, "firmware", envelope.firmware,
                     sizeof(envelope.firmware), true) ||
      !readUnsigned(begin, end, "schema", 65535, schema) || !secrets.valid() ||
      secrets.isString ||
      !copyPlainText(begin, end, "exportedAt", envelope.exportedAt,
                     sizeof(envelope.exportedAt), false) ||
      !jsonTextMemberEquals(begin, end, "cipher", CIPHER) ||
      !jsonTextMemberEquals(begin, end, "kdf", KDF) ||
      !readUnsigned(begin, end, "iterations", SETTINGS_BACKUP_KDF_MAX_ITERATIONS,
                    envelope.iterations) ||
      envelope.iterations < SETTINGS_BACKUP_KDF_MIN_ITERATIONS ||
      !readHex(begin, end, "salt", envelope.salt, sizeof(envelope.salt)) ||
      !readHex(begin, end, "nonce", envelope.nonce, sizeof(envelope.nonce)))
    return SettingsBackupEnvelopeStatus::Malformed;
  const size_t secretsLength = static_cast<size_t>(secrets.end - secrets.begin);
  if (secretsLength == 4 && memcmp(secrets.begin, "true", 4) == 0) {
    envelope.secrets = true;
  } else if (!(secretsLength == 5 && memcmp(secrets.begin, "false", 5) == 0)) {
    return SettingsBackupEnvelopeStatus::Malformed;
  }
  envelope.schema = static_cast<int>(schema);
  return SettingsBackupEnvelopeStatus::Ok;
}

void settingsBackupCopyLabel(char *output, size_t capacity, const char *text) {
  if (output == nullptr || capacity == 0) return;
  size_t length = 0;
  if (text != nullptr) {
    for (; text[length] != '\0' && length + 1 < capacity; ++length)
      output[length] = plainTextCharacter(text[length]) ? text[length] : '-';
  }
  output[length] = '\0';
}

size_t settingsBackupAssociatedData(const SettingsBackupEnvelope &envelope,
                                    char *output, size_t capacity) {
  if (output == nullptr || capacity == 0) return 0;
  char salt[SETTINGS_BACKUP_SALT_SIZE * 2 + 1];
  char nonce[SETTINGS_BACKUP_NONCE_SIZE * 2 + 1];
  settingsBackupHexEncode(envelope.salt, sizeof(envelope.salt), salt,
                          sizeof(salt));
  settingsBackupHexEncode(envelope.nonce, sizeof(envelope.nonce), nonce,
                          sizeof(nonce));
  const int written = snprintf(
      output, capacity, "%s\n%d\n%s\n%d\n%s\n%s\n%s\n%s\n%lu\n%s\n%s", FORMAT,
      envelope.version, envelope.firmware, envelope.schema,
      envelope.secrets ? "true" : "false", envelope.exportedAt, CIPHER, KDF,
      static_cast<unsigned long>(envelope.iterations), salt, nonce);
  if (written <= 0 || static_cast<size_t>(written) >= capacity) {
    output[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(written);
}

bool settingsBackupPasswordValid(const char *password, size_t length) {
  if (password == nullptr || length > SETTINGS_BACKUP_PASSWORD_MAX_BYTES)
    return false;
  size_t characters = 0;
  size_t index = 0;
  while (index < length) {
    const uint8_t lead = static_cast<uint8_t>(password[index]);
    size_t continuation = 0;
    uint32_t codePoint = 0;
    if (lead < 0x80) {
      // Řídicí znaky (i nula) by se v poli hesla nedaly zadat ani uvidět.
      if (lead < 0x20 || lead == 0x7F) return false;
      codePoint = lead;
    } else if (lead >= 0xC2 && lead <= 0xDF) {
      continuation = 1;
      codePoint = lead & 0x1F;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
      continuation = 2;
      codePoint = lead & 0x0F;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
      continuation = 3;
      codePoint = lead & 0x07;
    } else {
      return false;
    }
    if (continuation > length - index - 1) return false;
    for (size_t offset = 1; offset <= continuation; ++offset) {
      const uint8_t next = static_cast<uint8_t>(password[index + offset]);
      if ((next & 0xC0) != 0x80) return false;
      codePoint = (codePoint << 6) | (next & 0x3F);
    }
    // Příliš dlouhé zápisy, náhradní páry a čísla za Unicode.
    if ((continuation == 2 && (codePoint < 0x800 ||
                               (codePoint >= 0xD800 && codePoint <= 0xDFFF))) ||
        (continuation == 3 && (codePoint < 0x10000 || codePoint > 0x10FFFF)) ||
        (codePoint >= 0x80 && codePoint <= 0x9F))
      return false;
    index += continuation + 1;
    ++characters;
  }
  return characters >= SETTINGS_BACKUP_PASSWORD_MIN_CHARACTERS &&
         characters <= SETTINGS_BACKUP_PASSWORD_MAX_CHARACTERS;
}

bool settingsBackupDeriveKey(const char *password, size_t length,
                             const uint8_t *salt, uint32_t iterations,
                             uint8_t *key) {
  if (key == nullptr || salt == nullptr ||
      !settingsBackupPasswordValid(password, length) ||
      iterations < SETTINGS_BACKUP_KDF_MIN_ITERATIONS ||
      iterations > SETTINGS_BACKUP_KDF_MAX_ITERATIONS)
    return false;
  const bool ok =
      mbedtls_pkcs5_pbkdf2_hmac_ext(
          MBEDTLS_MD_SHA256, reinterpret_cast<const unsigned char *>(password),
          length, salt, SETTINGS_BACKUP_SALT_SIZE, iterations,
          SETTINGS_BACKUP_KEY_SIZE, key) == 0;
  if (!ok) mbedtls_platform_zeroize(key, SETTINGS_BACKUP_KEY_SIZE);
  return ok;
}

size_t settingsBackupSeal(const uint8_t *key, const uint8_t *nonce,
                          const char *associatedData, size_t associatedLength,
                          const uint8_t *plain, size_t plainLength,
                          uint8_t *output, size_t capacity) {
  if (key == nullptr || nonce == nullptr || associatedData == nullptr ||
      plain == nullptr || output == nullptr || plainLength == 0 ||
      capacity < SETTINGS_BACKUP_TAG_SIZE ||
      plainLength > capacity - SETTINGS_BACKUP_TAG_SIZE)
    return 0;
  mbedtls_gcm_context context;
  mbedtls_gcm_init(&context);
  const bool ok =
      mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, key,
                         SETTINGS_BACKUP_KEY_SIZE * 8) == 0 &&
      mbedtls_gcm_crypt_and_tag(
          &context, MBEDTLS_GCM_ENCRYPT, plainLength, nonce,
          SETTINGS_BACKUP_NONCE_SIZE,
          reinterpret_cast<const unsigned char *>(associatedData),
          associatedLength, plain, output, SETTINGS_BACKUP_TAG_SIZE,
          output + plainLength) == 0;
  mbedtls_gcm_free(&context);
  if (!ok) {
    mbedtls_platform_zeroize(output, capacity);
    return 0;
  }
  return plainLength + SETTINGS_BACKUP_TAG_SIZE;
}

bool settingsBackupOpen(const uint8_t *key, const uint8_t *nonce,
                        const char *associatedData, size_t associatedLength,
                        const uint8_t *sealed, size_t sealedLength,
                        uint8_t *plain, size_t capacity, size_t &plainLength) {
  plainLength = 0;
  if (key == nullptr || nonce == nullptr || associatedData == nullptr ||
      sealed == nullptr || plain == nullptr ||
      sealedLength <= SETTINGS_BACKUP_TAG_SIZE ||
      sealedLength - SETTINGS_BACKUP_TAG_SIZE > capacity)
    return false;
  const size_t length = sealedLength - SETTINGS_BACKUP_TAG_SIZE;
  mbedtls_gcm_context context;
  mbedtls_gcm_init(&context);
  const bool ok =
      mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, key,
                         SETTINGS_BACKUP_KEY_SIZE * 8) == 0 &&
      mbedtls_gcm_auth_decrypt(
          &context, length, nonce, SETTINGS_BACKUP_NONCE_SIZE,
          reinterpret_cast<const unsigned char *>(associatedData),
          associatedLength, sealed + length, SETTINGS_BACKUP_TAG_SIZE, sealed,
          plain) == 0;
  mbedtls_gcm_free(&context);
  if (!ok) {
    mbedtls_platform_zeroize(plain, capacity);
    return false;
  }
  plainLength = length;
  return true;
}

size_t settingsBackupHexEncode(const uint8_t *data, size_t size, char *output,
                               size_t capacity) {
  static constexpr char DIGITS[] = "0123456789abcdef";
  if (output == nullptr || capacity == 0) return 0;
  if (size * 2 + 1 > capacity) {
    output[0] = '\0';
    return 0;
  }
  for (size_t index = 0; index < size; ++index) {
    output[index * 2] = DIGITS[data[index] >> 4];
    output[index * 2 + 1] = DIGITS[data[index] & 0x0F];
  }
  output[size * 2] = '\0';
  return size * 2;
}

bool settingsBackupHexDecode(const char *text, size_t length, uint8_t *output,
                             size_t size) {
  if (text == nullptr || output == nullptr || length != size * 2) return false;
  for (size_t index = 0; index < size; ++index) {
    const int high = hexValue(text[index * 2]);
    const int low = hexValue(text[index * 2 + 1]);
    if (high < 0 || low < 0) return false;
    output[index] = static_cast<uint8_t>((high << 4) | low);
  }
  return true;
}

void settingsBackupZeroize(void *data, size_t size) {
  if (data != nullptr && size > 0) mbedtls_platform_zeroize(data, size);
}
