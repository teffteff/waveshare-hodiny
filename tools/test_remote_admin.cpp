#include <assert.h>
#include <string.h>

#include "RemoteAdmin.h"

void testUrl() {
  RemoteAdminEndpoint endpoint;
  assert(remoteAdminParseUrl("https://majnr.example.net/fleet/", endpoint));
  assert(strcmp(endpoint.host, "majnr.example.net") == 0);
  assert(endpoint.port == 443);
  assert(strcmp(endpoint.basePath, "/fleet") == 0);
  assert(remoteAdminParseUrl("https://10.0.0.5:8443", endpoint));
  assert(strcmp(endpoint.host, "10.0.0.5") == 0);
  assert(endpoint.port == 8443);
  assert(endpoint.basePath[0] == '\0');
  assert(!remoteAdminParseUrl("http://majnr.example.net/fleet", endpoint));
  assert(!remoteAdminParseUrl("https://user:pw@majnr.example.net/fleet", endpoint));
  assert(!remoteAdminParseUrl("https://majnr.example.net/fleet?x=1", endpoint));
  assert(!remoteAdminParseUrl("https://majnr.example.net/fl eet", endpoint));
  assert(!remoteAdminParseUrl("https://:443/fleet", endpoint));
  assert(!remoteAdminParseUrl("https://host:99999/", endpoint));
  assert(!remoteAdminParseUrl("https://host:/", endpoint));
  assert(!remoteAdminParseUrl("", endpoint));
}

void testToken() {
  assert(remoteAdminValidToken("Zk3_a-9QpL0x7vYt2mN4bC8dE1fG6hJ5kR0sT3uV9wX"));
  assert(!remoteAdminValidToken("short"));
  assert(!remoteAdminValidToken("abcdefghijklmnop qrst"));
  assert(!remoteAdminValidToken("abcdefghijklmnop\r\nX: y"));
}

void testAllowedRequests() {
  assert(remoteAdminAllowedRequest("GET", "/"));
  assert(remoteAdminAllowedRequest("GET", "/ui-language.js"));
  assert(remoteAdminAllowedRequest("GET", "/diagnostics"));
  assert(remoteAdminAllowedRequest("GET", "/api/config"));
  assert(remoteAdminAllowedRequest("POST", "/api/config"));
  assert(remoteAdminAllowedRequest("POST", "/api/backup/share/list"));
  assert(remoteAdminAllowedRequest("GET", "/api/radar/state?x=1&y=a.b"));
  assert(remoteAdminAllowedRequest("GET", "/api/remote-admin"));
  assert(!remoteAdminAllowedRequest("POST", "/api/remote-admin"));
  assert(!remoteAdminAllowedRequest("POST", "/api/web-password"));
  assert(!remoteAdminAllowedRequest("GET", "/api/web-password"));
  assert(!remoteAdminAllowedRequest("POST", "/api/control/display/off"));
  assert(!remoteAdminAllowedRequest("POST", "/api/auth/login"));
  assert(!remoteAdminAllowedRequest("PUT", "/api/config"));
  assert(!remoteAdminAllowedRequest("GET", "/api/../etc"));
  assert(!remoteAdminAllowedRequest("GET", "/api/"));
  assert(!remoteAdminAllowedRequest("GET", "/api//config"));
  assert(!remoteAdminAllowedRequest("GET", "/api/config HTTP/1.1\r\nX: y"));
  assert(!remoteAdminAllowedRequest("GET", "/api/config?a=<b>"));
  assert(!remoteAdminAllowedRequest("GET", "/other"));
  assert(!remoteAdminAllowedRequest("GET", ""));
}

void testHead() {
  const char head[] =
      "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
      "content-encoding: gzip\r\nContent-Length: 1234\r\n"
      "X-Job-Id: 0123abcd\r\nX-Job-Method: POST\r\nX-Job-Path: /api/config\r\n";
  RemoteAdminHead out;
  assert(remoteAdminParseHead(head, strlen(head), out));
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
  assert(remoteAdminParseHead(chunked, strlen(chunked), out));
  assert(out.status == 204 && out.chunked && out.close && out.contentLength == -1);
  const char old[] = "HTTP/1.0 200 OK\r\n";
  assert(remoteAdminParseHead(old, strlen(old), out) && out.close);
  assert(!remoteAdminParseHead("garbage", 7, out));
  const char badLength[] = "HTTP/1.1 200 OK\r\nContent-Length: -5\r\n";
  assert(!remoteAdminParseHead(badLength, strlen(badLength), out));

  const uint8_t full[] = "HTTP/1.1 200 OK\r\nA: b\r\n\r\nbody";
  assert(remoteAdminHeadLength(full, sizeof(full) - 1) == 25);
  assert(remoteAdminHeadLength(full, 20) == 0);
}

void testDechunk() {
  char body[] = "4\r\nWiki\r\n5;ext=1\r\npedia\r\nE\r\n in\r\n\r\nchunks.\r\n0\r\n\r\n";
  const long length =
      remoteAdminDechunk(reinterpret_cast<uint8_t *>(body), strlen(body));
  assert(length == 23);
  assert(memcmp(body, "Wikipedia in\r\n\r\nchunks.", 23) == 0);
  char truncated[] = "A\r\nshort";
  assert(remoteAdminDechunk(reinterpret_cast<uint8_t *>(truncated),
                            strlen(truncated)) == -1);
  char noEnd[] = "3\r\nabc\r\n";
  assert(remoteAdminDechunk(reinterpret_cast<uint8_t *>(noEnd), strlen(noEnd)) ==
         -1);
}

void testLocalRequest() {
  char out[512];
  size_t length = remoteAdminBuildLocalRequest(
      out, sizeof(out), "POST", "/api/config", "application/x-www-form-urlencoded;charset=UTF-8",
      "k3y", 17);
  assert(length == strlen(out));
  assert(strstr(out, "POST /api/config HTTP/1.1\r\n") == out);
  assert(strstr(out, "X-Remote-Admin: k3y\r\n") != nullptr);
  assert(strstr(out, "Content-Length: 17\r\n\r\n") != nullptr);
  // Řídicí znak v typu od serveru se nepoužije.
  remoteAdminBuildLocalRequest(out, sizeof(out), "POST", "/api/config",
                               "text/plain\r\nX-Evil: 1", "k3y", 0);
  assert(strstr(out, "X-Evil") == nullptr);
  length = remoteAdminBuildLocalRequest(out, sizeof(out), "GET", "/", "", "k3y", 0);
  assert(strstr(out, "Content-Length") == nullptr);
  assert(remoteAdminBuildLocalRequest(out, 20, "GET", "/", "", "k3y", 0) == 0);
}

int main() {
  testUrl();
  testToken();
  testAllowedRequests();
  testHead();
  testDechunk();
  testLocalRequest();
  return 0;
}
