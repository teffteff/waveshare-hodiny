#pragma once

#include <stddef.h>
#include <stdint.h>

// Vzdálené nastavení přes server (infra/remote-config): čisté části protokolu bez sítě,
// aby šly vyzkoušet na počítači (tools/test_remote_config.cpp). Síť a úloha
// jsou v RemoteConfigService.
//
// Hodiny drží k serveru jedno odchozí spojení a ptají se ho, jestli pro ně
// prohlížeč něco nemá. Požadavek předají vlastnímu web serveru přes loopback
// a odpověď pošlou zpátky. Tady je rozbor adresy serveru, seznam cest, které
// smí server chtít, a rozbor hlaviček a těla HTTP odpovědí.

constexpr size_t REMOTE_CONFIG_URL_LENGTH = 128;
constexpr size_t REMOTE_CONFIG_TOKEN_LENGTH = 64;
constexpr size_t REMOTE_CONFIG_HOST_LENGTH = 96;
constexpr size_t REMOTE_CONFIG_PATH_LENGTH = 160;
constexpr size_t REMOTE_CONFIG_JOB_ID_LENGTH = 33;
constexpr size_t REMOTE_CONFIG_CONTENT_TYPE_LENGTH = 100;
// Otisk těla odpovědi: 64bitové FNV-1a v šestnácti hex znacích.
constexpr size_t REMOTE_CONFIG_TAG_LENGTH = 17;

struct RemoteConfigEndpoint {
  char host[REMOTE_CONFIG_HOST_LENGTH] = "";
  uint16_t port = 443;
  // Cesta bez koncového lomítka, třeba "/remote-config"; může být prázdná.
  char basePath[REMOTE_CONFIG_PATH_LENGTH] = "";
};

// Jen https:// bez jména a hesla, dotazu, fragmentu a mezer.
bool remoteConfigParseUrl(const char *url, RemoteConfigEndpoint &endpoint);

// Token od `serve.py add-device`: base64url, 16 až 64 znaků.
bool remoteConfigValidToken(const char *token);

// Smí server tuhle cestu po hodinách chtít? Stejný seznam hlídá i server;
// tady je pro případ, že by server patřil někomu jinému. Ovládací API,
// přihlášení do webu, heslo webu ani uložení vzdáleného nastavení ne.
bool remoteConfigAllowedRequest(const char *method, const char *path);

struct RemoteConfigHead {
  int status = 0;
  // -1, když odpověď délku neuvádí.
  long contentLength = -1;
  bool chunked = false;
  bool close = false;
  char contentType[REMOTE_CONFIG_CONTENT_TYPE_LENGTH] = "";
  bool gzip = false;
  // Hlavičky úlohy od serveru (X-Job-*).
  char jobId[REMOTE_CONFIG_JOB_ID_LENGTH] = "";
  char jobMethod[8] = "";
  char jobPath[REMOTE_CONFIG_PATH_LENGTH] = "";
  // Otisk odpovědi, kterou server už má (X-Job-If-None-Match).
  char jobIfNoneMatch[REMOTE_CONFIG_TAG_LENGTH] = "";
};

// Rozebere stavový řádek a hlavičky HTTP/1.x až po prázdný řádek (bez něj).
bool remoteConfigParseHead(const char *head, size_t length, RemoteConfigHead &out);

// Najde konec hlaviček (\r\n\r\n) a vrátí délku včetně něj, jinak 0.
size_t remoteConfigHeadLength(const uint8_t *data, size_t length);

// Rozbalí tělo s Transfer-Encoding: chunked na místě. Vrací novou délku, nebo
// -1, když je tělo useknuté nebo poškozené.
long remoteConfigDechunk(uint8_t *body, size_t length);

// Délka celé odpovědi (hlavičky i tělo), jakmile je v bufferu úplná; 0, když
// ještě není, -1, když se konec pozná jen zavřením spojení. Web hodin totiž
// spojení sám nezavře, dokud ho nezavře klient, i když posílá Connection: close.
long remoteConfigCompleteLength(const uint8_t *data, size_t length);

// Otisk těla, podle kterého server pozná, že má stejnou odpověď uloženou, a
// hodiny ji pak nemusí posílat znovu. Nejde o bezpečnost, jen o shodu obsahu.
void remoteConfigBodyTag(const uint8_t *data, size_t length,
                        char out[REMOTE_CONFIG_TAG_LENGTH]);

// Hlavička požadavku na vlastní web: metoda, cesta, klíč loopbacku a délka.
// Vrací délku, nebo 0, když se nevejde.
size_t remoteConfigBuildLocalRequest(char *out, size_t capacity,
                                    const char *method, const char *path,
                                    const char *contentType,
                                    const char *loopbackKey,
                                    size_t bodyLength);
