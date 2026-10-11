#pragma once

#include <stddef.h>
#include <stdint.h>

// Čistá část OTA bez Arduina, aby šla testovat na počítači: adresa metadat,
// jejich rozbor včetně kontroly desky a hledání značky desky v obrazu.

// Adresa metadat pro desku. Kulatá 2,1" čte historické ota.json (tak je čte
// i starší firmware bez kontroly desky), ostatní desky ota-<deska>.json;
// interní Firmware Hub dostane desku v parametru ?board=.
bool otaMetadataUrl(const char *serverUrl, const char *metadataPath,
                    const char *projectSlug, const char *boardId, char *out,
                    size_t capacity);

// Relativní adresu doplní o server; absolutní musí ležet pod "serverUrl/".
bool otaNormalizeDownloadUrl(const char *serverUrl, const char *url, char *out,
                             size_t capacity);

struct OtaMetadata {
  char version[48] = "";
  char chipFamily[16] = "";
  char board[16] = "";
  char sha256[65] = "";
  char url[256] = "";  // už normalizovaná absolutní adresa
  uint32_t size = 0;
};

enum class OtaMetadataResult : uint8_t {
  Ok,
  Invalid,        // chybí pole, špatný typ, verze, čip nebo SHA-256
  WrongBoard,     // obraz pro jinou desku
  DisallowedUrl,  // obraz mimo povolený server
};

// Metadata bez pole "board" vznikla před 7" verzí a patří kulaté 2,1".
OtaMetadataResult otaParseMetadata(const char *json, size_t length,
                                   const char *serverUrl,
                                   const char *chipFamily, const char *boardId,
                                   OtaMetadata &out);

// Značka desky zapečená do každého obrazu (OtaMetadata.cpp drží tu svoji,
// tools/prepare_release_package.py ji v obrazu ověřuje). Podoba:
// "HODINY_BOARD_TAG=<deska>;".
#define OTA_BOARD_TAG_PREFIX "HODINY_BOARD_TAG="
extern const char OTA_BOARD_TAG[];

// Proudové hledání značky v obrazu stahovaném po kusech. Obraz se přijme
// jen tehdy, když nese značku této desky a žádnou jinou. Řetězec samotné
// předpony (bez desky a středníku) se nepočítá, takže hledání nezmate kód,
// který značku hledá.
class OtaBoardTagScanner {
 public:
  // Bez argumentu hledá značku desky, pro kterou je přeložený tento obraz.
  explicit OtaBoardTagScanner(const char *boardId = nullptr);
  void feed(const uint8_t *data, size_t length);
  bool ownTagSeen() const { return ownSeen_; }
  bool otherTagSeen() const { return otherSeen_; }
  bool accepted() const { return ownSeen_ && !otherSeen_; }

 private:
  void finishId();

  char ownId_[16];
  size_t prefixMatched_ = 0;
  bool readingId_ = false;
  char id_[16] = "";
  size_t idLength_ = 0;
  bool ownSeen_ = false;
  bool otherSeen_ = false;
};
