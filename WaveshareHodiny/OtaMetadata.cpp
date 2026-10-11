#include "OtaMetadata.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "Board.h"
#include "JsonScan.h"
#include "SemVer.h"

// Značka desky v obrazu. Na ni se odkazuje OtaBoardTagScanner, takže ji
// linker nevyhodí; release balíček bez ní neprojde.
__attribute__((used)) const char OTA_BOARD_TAG[] =
    OTA_BOARD_TAG_PREFIX BOARD_ID ";";

namespace {

bool endsWith(const char *text, const char *suffix) {
  const size_t textLength = strlen(text);
  const size_t suffixLength = strlen(suffix);
  return textLength >= suffixLength &&
         strcmp(text + textLength - suffixLength, suffix) == 0;
}

bool fits(int written, size_t capacity) {
  return written >= 0 && static_cast<size_t>(written) < capacity;
}

// Textová hodnota přísněji než jsonCopyText: zná jen escapy \" \\ \/ (víc
// metadata nepotřebují), řídicí znaky i hodnotu delší než cíl odmítne.
// Useknutá adresa nebo SHA-256 by jinak prošla jako platná.
bool readString(const char *objectBegin, const char *objectEnd,
                const char *key, char *destination, size_t capacity) {
  const JsonValue value = jsonFindMember(objectBegin, objectEnd, key);
  if (!value.valid() || !value.isString) return false;
  size_t written = 0;
  for (const char *cursor = value.contentBegin(); cursor < value.contentEnd();
       ++cursor) {
    char character = *cursor;
    if (character == '\\') {
      if (++cursor >= value.contentEnd()) return false;
      character = *cursor;
      if (character != '"' && character != '\\' && character != '/') return false;
    } else if (static_cast<uint8_t>(character) < 0x20) {
      return false;
    }
    if (written + 1 >= capacity) return false;
    destination[written++] = character;
  }
  destination[written] = '\0';
  return true;
}

// Kladné celé číslo do 32 bitů, bez znaménka, desetinné čárky i exponentu.
bool readUnsigned(const char *objectBegin, const char *objectEnd,
                  const char *key, uint32_t &out) {
  const JsonValue value = jsonFindMember(objectBegin, objectEnd, key);
  if (!value.valid() || value.isString) return false;
  const size_t length = static_cast<size_t>(value.end - value.begin);
  if (length == 0 || length > 10) return false;
  uint64_t parsed = 0;
  for (const char *cursor = value.begin; cursor < value.end; ++cursor) {
    if (!isdigit(static_cast<unsigned char>(*cursor))) return false;
    parsed = parsed * 10 + static_cast<uint64_t>(*cursor - '0');
  }
  if (parsed == 0 || parsed > UINT32_MAX) return false;
  out = static_cast<uint32_t>(parsed);
  return true;
}

bool validSha256(const char *text) {
  if (strlen(text) != 64) return false;
  for (const char *cursor = text; *cursor != '\0'; ++cursor) {
    if (!isxdigit(static_cast<unsigned char>(*cursor))) return false;
  }
  return true;
}

void lowercase(char *text) {
  for (; *text != '\0'; ++text) {
    *text = static_cast<char>(tolower(static_cast<unsigned char>(*text)));
  }
}

bool isIdCharacter(uint8_t character) {
  return (character >= 'a' && character <= 'z') ||
         (character >= '0' && character <= '9');
}

constexpr char TAG_PREFIX[] = OTA_BOARD_TAG_PREFIX;
constexpr size_t TAG_PREFIX_LENGTH = sizeof(TAG_PREFIX) - 1;

}  // namespace

bool otaMetadataUrl(const char *serverUrl, const char *metadataPath,
                    const char *projectSlug, const char *boardId, char *out,
                    size_t capacity) {
  if (out == nullptr || capacity == 0) return false;
  out[0] = '\0';
  const bool otherBoard = strcmp(boardId, "lcd21") != 0;
  int written;
  if (metadataPath != nullptr && metadataPath[0] != '\0') {
    if (otherBoard && endsWith(metadataPath, "/ota.json")) {
      const int stem = static_cast<int>(strlen(metadataPath) - strlen(".json"));
      written = snprintf(out, capacity, "%s%.*s-%s.json", serverUrl, stem,
                         metadataPath, boardId);
    } else {
      written = snprintf(out, capacity, "%s%s", serverUrl, metadataPath);
    }
  } else if (otherBoard) {
    written = snprintf(out, capacity, "%s/api/v1/projects/%s/ota?board=%s",
                       serverUrl, projectSlug, boardId);
  } else {
    written = snprintf(out, capacity, "%s/api/v1/projects/%s/ota", serverUrl,
                       projectSlug);
  }
  if (!fits(written, capacity)) {
    out[0] = '\0';
    return false;
  }
  return true;
}

bool otaNormalizeDownloadUrl(const char *serverUrl, const char *url, char *out,
                             size_t capacity) {
  if (out == nullptr || capacity == 0) return false;
  out[0] = '\0';
  if (serverUrl[0] == '\0' || url[0] == '\0') return false;
  int written;
  if (url[0] == '/') {
    written = snprintf(out, capacity, "%s%s", serverUrl, url);
  } else {
    const size_t serverLength = strlen(serverUrl);
    // Lomítko za serverem: "https://server.evil" pod "https://server" neprojde.
    if (strncmp(url, serverUrl, serverLength) != 0 || url[serverLength] != '/')
      return false;
    written = snprintf(out, capacity, "%s", url);
  }
  if (!fits(written, capacity)) {
    out[0] = '\0';
    return false;
  }
  return true;
}

OtaMetadataResult otaParseMetadata(const char *json, size_t length,
                                   const char *serverUrl,
                                   const char *chipFamily, const char *boardId,
                                   OtaMetadata &out) {
  out = OtaMetadata{};
  if (json == nullptr) return OtaMetadataResult::Invalid;
  const char *begin = json;
  const char *end = json + length;
  char rawUrl[sizeof(out.url)];
  if (!readString(begin, end, "version", out.version, sizeof(out.version)) ||
      !readString(begin, end, "chipFamily", out.chipFamily,
                  sizeof(out.chipFamily)) ||
      !readString(begin, end, "sha256", out.sha256, sizeof(out.sha256)) ||
      !readString(begin, end, "url", rawUrl, sizeof(rawUrl)) ||
      !readUnsigned(begin, end, "size", out.size) ||
      !semVerIsValid(out.version) || strcmp(out.chipFamily, chipFamily) != 0 ||
      !validSha256(out.sha256)) {
    return OtaMetadataResult::Invalid;
  }
  lowercase(out.sha256);
  // Obě desky mají stejný čip, takže chipFamily je nerozliší.
  if (jsonFindMember(begin, end, "board").valid()) {
    if (!readString(begin, end, "board", out.board, sizeof(out.board)))
      return OtaMetadataResult::Invalid;
  } else {
    strcpy(out.board, "lcd21");
  }
  if (strcmp(out.board, boardId) != 0) return OtaMetadataResult::WrongBoard;
  if (!otaNormalizeDownloadUrl(serverUrl, rawUrl, out.url, sizeof(out.url)))
    return OtaMetadataResult::DisallowedUrl;
  return OtaMetadataResult::Ok;
}

OtaBoardTagScanner::OtaBoardTagScanner(const char *boardId) {
  ownId_[0] = '\0';
  if (boardId == nullptr) {
    // Vlastní deska ze značky v tomto obrazu. Volatilní ukazatel drží odkaz
    // na OTA_BOARD_TAG, aby ji linker (--gc-sections) nevyhodil.
    const char *volatile tag = OTA_BOARD_TAG;
    boardId = tag + TAG_PREFIX_LENGTH;
  }
  size_t length = 0;
  while (boardId[length] != '\0' && boardId[length] != ';' &&
         length + 1 < sizeof(ownId_)) {
    ownId_[length] = boardId[length];
    ++length;
  }
  ownId_[length] = '\0';
}

void OtaBoardTagScanner::finishId() {
  id_[idLength_] = '\0';
  if (strcmp(id_, ownId_) == 0) {
    ownSeen_ = true;
  } else {
    otherSeen_ = true;
  }
}

void OtaBoardTagScanner::feed(const uint8_t *data, size_t length) {
  for (size_t index = 0; index < length; ++index) {
    const uint8_t character = data[index];
    if (readingId_) {
      if (character == ';' && idLength_ > 0) {
        finishId();
        readingId_ = false;
        idLength_ = 0;
        continue;
      }
      readingId_ = false;
      if (isIdCharacter(character) && idLength_ + 1 < sizeof(id_)) {
        id_[idLength_++] = static_cast<char>(character);
        readingId_ = true;
        continue;
      }
      idLength_ = 0;
      // Znak, který značku ukončil, může začínat další.
    }
    if (character == static_cast<uint8_t>(TAG_PREFIX[prefixMatched_])) {
      if (++prefixMatched_ == TAG_PREFIX_LENGTH) {
        prefixMatched_ = 0;
        readingId_ = true;
        idLength_ = 0;
      }
    } else {
      // "H" je v předponě jen na začátku, takže stačí začít znovu.
      prefixMatched_ =
          character == static_cast<uint8_t>(TAG_PREFIX[0]) ? 1 : 0;
    }
  }
}
