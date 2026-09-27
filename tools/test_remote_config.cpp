#include <assert.h>
#include <string.h>

#include "RemoteConfig.h"

void testUrl() {
  RemoteConfigEndpoint endpoint;
  assert(remoteConfigParseUrl("https://majnr.example.net/remote-config/", endpoint));
  assert(strcmp(endpoint.host, "majnr.example.net") == 0);
  assert(endpoint.port == 443);
  assert(strcmp(endpoint.basePath, "/remote-config") == 0);
  assert(remoteConfigParseUrl("https://10.0.0.5:8443", endpoint));
  assert(strcmp(endpoint.host, "10.0.0.5") == 0);
  assert(endpoint.port == 8443);
  assert(endpoint.basePath[0] == '\0');
  assert(!remoteConfigParseUrl("http://majnr.example.net/remote-config", endpoint));
  assert(!remoteConfigParseUrl("https://user:pw@majnr.example.net/remote-config", endpoint));
  assert(!remoteConfigParseUrl("https://majnr.example.net/remote-config?x=1", endpoint));
  assert(!remoteConfigParseUrl("https://majnr.example.net/fl eet", endpoint));
  assert(!remoteConfigParseUrl("https://:443/remote-config", endpoint));
  assert(!remoteConfigParseUrl("https://host:99999/", endpoint));
  assert(!remoteConfigParseUrl("https://host:/", endpoint));
  assert(!remoteConfigParseUrl("", endpoint));
}

void testToken() {
  assert(remoteConfigValidToken("Zk3_a-9QpL0x7vYt2mN4bC8dE1fG6hJ5kR0sT3uV9wX"));
  assert(!remoteConfigValidToken("short"));
  assert(!remoteConfigValidToken("abcdefghijklmnop qrst"));
  assert(!remoteConfigValidToken("abcdefghijklmnop\r\nX: y"));
}

void testAllowedRequests() {
  assert(remoteConfigAllowedRequest("GET", "/"));
  assert(remoteConfigAllowedRequest("GET", "/ui-language.js"));
  assert(remoteConfigAllowedRequest("GET", "/diagnostics"));
  assert(remoteConfigAllowedRequest("GET", "/api/config"));
  assert(remoteConfigAllowedRequest("POST", "/api/config"));
  assert(remoteConfigAllowedRequest("POST", "/api/backup/share/list"));
  assert(remoteConfigAllowedRequest("GET", "/api/radar/state?x=1&y=a.b"));
  assert(remoteConfigAllowedRequest("GET", "/api/remote-config"));
  assert(!remoteConfigAllowedRequest("POST", "/api/remote-config"));
  assert(!remoteConfigAllowedRequest("POST", "/api/web-password"));
  assert(!remoteConfigAllowedRequest("GET", "/api/web-password"));
  assert(!remoteConfigAllowedRequest("POST", "/api/control/display/off"));
  assert(!remoteConfigAllowedRequest("POST", "/api/auth/login"));
  assert(!remoteConfigAllowedRequest("PUT", "/api/config"));
  assert(!remoteConfigAllowedRequest("GET", "/api/../etc"));
  assert(!remoteConfigAllowedRequest("GET", "/api/"));
  assert(!remoteConfigAllowedRequest("GET", "/api//config"));
  assert(!remoteConfigAllowedRequest("GET", "/api/config HTTP/1.1\r\nX: y"));
  assert(!remoteConfigAllowedRequest("GET", "/api/config?a=<b>"));
  assert(!remoteConfigAllowedRequest("GET", "/other"));
  assert(!remoteConfigAllowedRequest("GET", ""));
}

void testHead() {
  const char head[] =
      "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
      "content-encoding: gzip\r\nContent-Length: 1234\r\n"
      "X-Job-Id: 0123abcd\r\nX-Job-Method: POST\r\nX-Job-Path: /api/config\r\n";
  RemoteConfigHead out;
  assert(remoteConfigParseHead(head, strlen(head), out));
  assert(out.status == 200);
  assert(out.contentLength == 1234);
  assert(out.gzip);
  assert(!out.chunked);
  assert(!out.close);
  assert(strcmp(out.contentType, "text/html; charset=utf-8") == 0);
  assert(strcmp(out.jobId, "0123abcd") == 0);
  assert(strcmp(out.jobMethod, "POST") == 0);
  assert(strcmp(out.jobPath, "/api/config") == 0);

  const char chunked[] =
      "HTTP/1.1 204 No Content\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n";
  assert(remoteConfigParseHead(chunked, strlen(chunked), out));
  assert(out.status == 204 && out.chunked && out.close && out.contentLength == -1);
  const char old[] = "HTTP/1.0 200 OK\r\n";
  assert(remoteConfigParseHead(old, strlen(old), out) && out.close);
  assert(!remoteConfigParseHead("garbage", 7, out));
  const char badLength[] = "HTTP/1.1 200 OK\r\nContent-Length: -5\r\n";
  assert(!remoteConfigParseHead(badLength, strlen(badLength), out));

  const uint8_t full[] = "HTTP/1.1 200 OK\r\nA: b\r\n\r\nbody";
  assert(remoteConfigHeadLength(full, sizeof(full) - 1) == 25);
  assert(remoteConfigHeadLength(full, 20) == 0);
}

void testDechunk() {
  char body[] = "4\r\nWiki\r\n5;ext=1\r\npedia\r\nE\r\n in\r\n\r\nchunks.\r\n0\r\n\r\n";
  const long length =
      remoteConfigDechunk(reinterpret_cast<uint8_t *>(body), strlen(body));
  assert(length == 23);
  assert(memcmp(body, "Wikipedia in\r\n\r\nchunks.", 23) == 0);
  char truncated[] = "A\r\nshort";
  assert(remoteConfigDechunk(reinterpret_cast<uint8_t *>(truncated),
                            strlen(truncated)) == -1);
  char noEnd[] = "3\r\nabc\r\n";
  assert(remoteConfigDechunk(reinterpret_cast<uint8_t *>(noEnd), strlen(noEnd)) ==
         -1);
}

long completeLength(const char *text) {
  return remoteConfigCompleteLength(reinterpret_cast<const uint8_t *>(text),
                                   strlen(text));
}

void testCompleteLength() {
  const char *sized = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";
  assert(completeLength(sized) == static_cast<long>(strlen(sized)));
  assert(completeLength("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhel") == 0);
  assert(completeLength("HTTP/1.1 200 OK\r\nContent-Len") == 0);
  const char *empty = "HTTP/1.1 204 No Content\r\n\r\n";
  assert(completeLength(empty) == static_cast<long>(strlen(empty)));
  const char *chunked =
      "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
      "5\r\nhello\r\n3;x=1\r\nabc\r\n0\r\n\r\n";
  assert(completeLength(chunked) == static_cast<long>(strlen(chunked)));
  // Každý kratší prefix je neúplný.
  const size_t chunkedLength = strlen(chunked);
  for (size_t cut = 0; cut < chunkedLength; ++cut)
    assert(remoteConfigCompleteLength(reinterpret_cast<const uint8_t *>(chunked),
                                     cut) == 0);
  const char *trailer =
      "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
      "0\r\nX-Done: 1\r\n\r\n";
  assert(completeLength(trailer) == static_cast<long>(strlen(trailer)));
  assert(completeLength("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n") == -1);
  // Bez délky a bez chunked rozhodne až zavření spojení.
  assert(completeLength("HTTP/1.1 200 OK\r\n\r\nabc") == -1);
}

void testBodyTag() {
  char tag[REMOTE_CONFIG_TAG_LENGTH];
  remoteConfigBodyTag(nullptr, 0, tag);
  assert(strcmp(tag, "cbf29ce484222325") == 0);
  // Známá hodnota FNV-1a 64 pro "a".
  remoteConfigBodyTag(reinterpret_cast<const uint8_t *>("a"), 1, tag);
  assert(strcmp(tag, "af63dc4c8601ec8c") == 0);
  char other[REMOTE_CONFIG_TAG_LENGTH];
  remoteConfigBodyTag(reinterpret_cast<const uint8_t *>("b"), 1, other);
  assert(strcmp(tag, other) != 0);
  RemoteConfigHead head;
  const char *text = "HTTP/1.1 200 OK\r\nX-Job-Id: abc\r\n"
                     "X-Job-If-None-Match: af63dc4c8601ec8c";
  assert(remoteConfigParseHead(text, strlen(text), head));
  assert(strcmp(head.jobIfNoneMatch, "af63dc4c8601ec8c") == 0);
}

void testLocalRequest() {
  char out[512];
  size_t length = remoteConfigBuildLocalRequest(
      out, sizeof(out), "POST", "/api/config", "application/x-www-form-urlencoded;charset=UTF-8",
      "k3y", 17);
  assert(length == strlen(out));
  assert(strstr(out, "POST /api/config HTTP/1.1\r\n") == out);
  assert(strstr(out, "X-Remote-Config: k3y\r\n") != nullptr);
  assert(strstr(out, "Content-Length: 17\r\n\r\n") != nullptr);
  // Řídicí znak v typu od serveru se nepoužije.
  remoteConfigBuildLocalRequest(out, sizeof(out), "POST", "/api/config",
                               "text/plain\r\nX-Evil: 1", "k3y", 0);
  assert(strstr(out, "X-Evil") == nullptr);
  length = remoteConfigBuildLocalRequest(out, sizeof(out), "GET", "/", "", "k3y", 0);
  assert(strstr(out, "Content-Length") == nullptr);
  assert(remoteConfigBuildLocalRequest(out, 20, "GET", "/", "", "k3y", 0) == 0);
}

int main() {
  testUrl();
  testToken();
  testAllowedRequests();
  testHead();
  testDechunk();
  testCompleteLength();
  testBodyTag();
  testLocalRequest();
  return 0;
}
