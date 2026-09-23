#pragma once

#include <stddef.h>
#include <stdint.h>

// Vzdálená správa přes server (infra/fleet): čisté části protokolu bez sítě,
// aby šly vyzkoušet na počítači (tools/test_remote_admin.cpp). Síť a úloha
// jsou v RemoteAdminService.
//
// Hodiny drží k serveru jedno odchozí spojení a ptají se ho, jestli pro ně
// prohlížeč něco nemá. Požadavek předají vlastnímu web serveru přes loopback
// a odpověď pošlou zpátky. Tady je rozbor adresy serveru, seznam cest, které
// smí server chtít, a rozbor hlaviček a těla HTTP odpovědí.

constexpr size_t REMOTE_ADMIN_URL_LENGTH = 128;
constexpr size_t REMOTE_ADMIN_TOKEN_LENGTH = 64;
constexpr size_t REMOTE_ADMIN_HOST_LENGTH = 96;
constexpr size_t REMOTE_ADMIN_PATH_LENGTH = 160;
constexpr size_t REMOTE_ADMIN_JOB_ID_LENGTH = 33;
constexpr size_t REMOTE_ADMIN_CONTENT_TYPE_LENGTH = 100;
// Otisk těla odpovědi: 64bitové FNV-1a v šestnácti hex znacích.
constexpr size_t REMOTE_ADMIN_TAG_LENGTH = 17;

struct RemoteAdminEndpoint {
  char host[REMOTE_ADMIN_HOST_LENGTH] = "";
  uint16_t port = 443;
  // Cesta bez koncového lomítka, třeba "/fleet"; může být prázdná.
  char basePath[REMOTE_ADMIN_PATH_LENGTH] = "";
};

// Jen https:// bez jména a hesla, dotazu, fragmentu a mezer.
bool remoteAdminParseUrl(const char *url, RemoteAdminEndpoint &endpoint);

// Token od `serve.py add-device`: base64url, 16 až 64 znaků.
bool remoteAdminValidToken(const char *token);

// Smí server tuhle cestu po hodinách chtít? Stejný seznam hlídá i server;
// tady je pro případ, že by server patřil někomu jinému. Ovládací API,
// přihlášení do webu, heslo webu ani uložení vzdálené správy ne.
bool remoteAdminAllowedRequest(const char *method, const char *path);

struct RemoteAdminHead {
  int status = 0;
  // -1, když odpověď délku neuvádí.
  long contentLength = -1;
  bool chunked = false;
  bool close = false;
  char contentType[REMOTE_ADMIN_CONTENT_TYPE_LENGTH] = "";
  bool gzip = false;
  // Hlavičky úlohy od serveru (X-Job-*).
  char jobId[REMOTE_ADMIN_JOB_ID_LENGTH] = "";
  char jobMethod[8] = "";
  char jobPath[REMOTE_ADMIN_PATH_LENGTH] = "";
  // Otisk odpovědi, kterou server už má (X-Job-If-None-Match).
  char jobIfNoneMatch[REMOTE_ADMIN_TAG_LENGTH] = "";
};

// Rozebere stavový řádek a hlavičky HTTP/1.x až po prázdný řádek (bez něj).
bool remoteAdminParseHead(const char *head, size_t length, RemoteAdminHead &out);

// Najde konec hlaviček (\r\n\r\n) a vrátí délku včetně něj, jinak 0.
size_t remoteAdminHeadLength(const uint8_t *data, size_t length);

// Rozbalí tělo s Transfer-Encoding: chunked na místě. Vrací novou délku, nebo
// -1, když je tělo useknuté nebo poškozené.
long remoteAdminDechunk(uint8_t *body, size_t length);

// Délka celé odpovědi (hlavičky i tělo), jakmile je v bufferu úplná; 0, když
// ještě není, -1, když se konec pozná jen zavřením spojení. Web hodin totiž
// spojení sám nezavře, dokud ho nezavře klient, i když posílá Connection: close.
long remoteAdminCompleteLength(const uint8_t *data, size_t length);

// Otisk těla, podle kterého server pozná, že má stejnou odpověď uloženou, a
// hodiny ji pak nemusí posílat znovu. Nejde o bezpečnost, jen o shodu obsahu.
void remoteAdminBodyTag(const uint8_t *data, size_t length,
                        char out[REMOTE_ADMIN_TAG_LENGTH]);

// Hlavička požadavku na vlastní web: metoda, cesta, klíč loopbacku a délka.
// Vrací délku, nebo 0, když se nevejde.
size_t remoteAdminBuildLocalRequest(char *out, size_t capacity,
                                    const char *method, const char *path,
                                    const char *contentType,
                                    const char *loopbackKey,
                                    size_t bodyLength);
