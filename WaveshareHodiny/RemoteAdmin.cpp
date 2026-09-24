#include "RemoteAdmin.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
bool startsWith(const char *text, const char *prefix) {
  return strncmp(text, prefix, strlen(prefix)) == 0;
}

bool headerNameIs(const char *line, size_t nameLength, const char *name) {
  if (strlen(name) != nameLength) return false;
  for (size_t i = 0; i < nameLength; ++i) {
    if (tolower(static_cast<unsigned char>(line[i])) !=
        tolower(static_cast<unsigned char>(name[i])))
      return false;
  }
  return true;
}

bool containsTokenIgnoreCase(const char *value, size_t length,
                             const char *token) {
  const size_t tokenLength = strlen(token);
  for (size_t start = 0; start + tokenLength <= length; ++start) {
    size_t i = 0;
    while (i < tokenLength &&
           tolower(static_cast<unsigned char>(value[start + i])) == token[i])
      ++i;
    if (i == tokenLength) return true;
  }
  return false;
}

void copyValue(char *out, size_t capacity, const char *value, size_t length) {
  if (capacity == 0) return;
  const size_t copied = length < capacity - 1 ? length : capacity - 1;
  memcpy(out, value, copied);
  out[copied] = '\0';
}

bool apiPathCharacter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '/' ||
         c == '_' || c == '-';
}

bool queryCharacter(char c) {
  return isalnum(static_cast<unsigned char>(c)) || c == '=' || c == '&' ||
         c == '%' || c == '.' || c == '_' || c == '~' || c == '+' || c == '-';
}
}  // namespace

bool remoteAdminParseUrl(const char *url, RemoteAdminEndpoint &endpoint) {
  endpoint = RemoteAdminEndpoint{};
  if (url == nullptr || !startsWith(url, "https://")) return false;
  const size_t total = strlen(url);
  if (total >= REMOTE_ADMIN_URL_LENGTH) return false;
  for (size_t i = 0; i < total; ++i) {
    const unsigned char c = static_cast<unsigned char>(url[i]);
    if (c <= ' ' || c >= 0x7F || c == '?' || c == '#' || c == '@' ||
        c == '\\')
      return false;
  }
  const char *host = url + 8;
  const char *slash = strchr(host, '/');
  const char *hostEnd = slash != nullptr ? slash : url + total;
  const char *colon = static_cast<const char *>(
      memchr(host, ':', static_cast<size_t>(hostEnd - host)));
  const char *nameEnd = colon != nullptr ? colon : hostEnd;
  const size_t hostLength = static_cast<size_t>(nameEnd - host);
  if (hostLength == 0 || hostLength >= sizeof(endpoint.host)) return false;
  for (const char *c = host; c < nameEnd; ++c) {
    if (!(isalnum(static_cast<unsigned char>(*c)) || *c == '.' || *c == '-'))
      return false;
  }
  if (colon != nullptr) {
    if (colon + 1 == hostEnd || hostEnd - colon > 6) return false;
    long port = 0;
    for (const char *c = colon + 1; c < hostEnd; ++c) {
      if (!isdigit(static_cast<unsigned char>(*c))) return false;
      port = port * 10 + (*c - '0');
    }
    if (port < 1 || port > 65535) return false;
    endpoint.port = static_cast<uint16_t>(port);
  }
  copyValue(endpoint.host, sizeof(endpoint.host), host, hostLength);
  if (slash != nullptr) {
    size_t pathLength = strlen(slash);
    while (pathLength > 0 && slash[pathLength - 1] == '/') --pathLength;
    if (pathLength >= sizeof(endpoint.basePath)) return false;
    copyValue(endpoint.basePath, sizeof(endpoint.basePath), slash, pathLength);
  }
  return true;
}

bool remoteAdminValidToken(const char *token) {
  if (token == nullptr) return false;
  const size_t length = strlen(token);
  if (length < 16 || length > REMOTE_ADMIN_TOKEN_LENGTH) return false;
  for (size_t i = 0; i < length; ++i) {
    const char c = token[i];
    if (!(isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'))
      return false;
  }
  return true;
}

bool remoteAdminAllowedRequest(const char *method, const char *path) {
  if (method == nullptr || path == nullptr) return false;
  const bool post = strcmp(method, "POST") == 0;
  if (!post && strcmp(method, "GET") != 0) return false;
  const char *query = strchr(path, '?');
  const size_t pathLength =
      query != nullptr ? static_cast<size_t>(query - path) : strlen(path);
  if (pathLength == 0 || pathLength >= REMOTE_ADMIN_PATH_LENGTH) return false;
  if (query != nullptr) {
    for (const char *c = query + 1; *c != '\0'; ++c)
      if (!queryCharacter(*c)) return false;
  }
  char bare[REMOTE_ADMIN_PATH_LENGTH];
  copyValue(bare, sizeof(bare), path, pathLength);
  if (strcmp(bare, "/") == 0 || strcmp(bare, "/ui-language.js") == 0 ||
      strcmp(bare, "/diagnostics") == 0)
    return true;
  if (!startsWith(bare, "/api/") || pathLength == 5) return false;
  for (size_t i = 5; i < pathLength; ++i)
    if (!apiPathCharacter(bare[i])) return false;
  if (bare[5] == '/' || bare[5] == '_' || bare[5] == '-') return false;
  if (startsWith(bare, "/api/control/") || startsWith(bare, "/api/auth/") ||
      strcmp(bare, "/api/control") == 0 || strcmp(bare, "/api/auth") == 0)
    return false;
  if (strcmp(bare, "/api/web-password") == 0) return false;
  // Stav vzdálené správy si stránka přečíst smí, měnit ho jde jen doma.
  if (post && strcmp(bare, "/api/remote-admin") == 0) return false;
  return true;
}

size_t remoteAdminHeadLength(const uint8_t *data, size_t length) {
  for (size_t i = 3; i < length; ++i) {
    if (data[i - 3] == '\r' && data[i - 2] == '\n' && data[i - 1] == '\r' &&
        data[i] == '\n')
      return i + 1;
  }
  return 0;
}

bool remoteAdminParseHead(const char *head, size_t length,
                          RemoteAdminHead &out) {
  out = RemoteAdminHead{};
  const char *end = head + length;
  const char *lineEnd = static_cast<const char *>(memchr(head, '\n', length));
  if (lineEnd == nullptr) lineEnd = end;
  // "HTTP/1.1 200 OK"
  if (lineEnd - head < 12 || !startsWith(head, "HTTP/1.")) return false;
  if (!isdigit(static_cast<unsigned char>(head[9])) ||
      !isdigit(static_cast<unsigned char>(head[10])) ||
      !isdigit(static_cast<unsigned char>(head[11])))
    return false;
  out.status = (head[9] - '0') * 100 + (head[10] - '0') * 10 + (head[11] - '0');
  out.close = head[7] == '0';  // HTTP/1.0 bez keep-alive
  const char *line = lineEnd < end ? lineEnd + 1 : end;
  while (line < end) {
    const char *next = static_cast<const char *>(
        memchr(line, '\n', static_cast<size_t>(end - line)));
    const char *stop = next != nullptr ? next : end;
    const char *valueEnd = stop;
    if (valueEnd > line && valueEnd[-1] == '\r') --valueEnd;
    const char *colon = static_cast<const char *>(
        memchr(line, ':', static_cast<size_t>(valueEnd - line)));
    if (colon != nullptr) {
      const size_t nameLength = static_cast<size_t>(colon - line);
      const char *value = colon + 1;
      while (value < valueEnd && (*value == ' ' || *value == '\t')) ++value;
      const size_t valueLength = static_cast<size_t>(valueEnd - value);
      if (headerNameIs(line, nameLength, "content-length")) {
        char number[16];
        copyValue(number, sizeof(number), value, valueLength);
        char *parsedEnd = nullptr;
        const long parsed = strtol(number, &parsedEnd, 10);
        if (parsedEnd == number || *parsedEnd != '\0' || parsed < 0)
          return false;
        out.contentLength = parsed;
      } else if (headerNameIs(line, nameLength, "transfer-encoding")) {
        out.chunked = containsTokenIgnoreCase(value, valueLength, "chunked");
      } else if (headerNameIs(line, nameLength, "connection")) {
        if (containsTokenIgnoreCase(value, valueLength, "close"))
          out.close = true;
        else if (containsTokenIgnoreCase(value, valueLength, "keep-alive"))
          out.close = false;
      } else if (headerNameIs(line, nameLength, "content-type")) {
        copyValue(out.contentType, sizeof(out.contentType), value, valueLength);
      } else if (headerNameIs(line, nameLength, "content-encoding")) {
        out.gzip = containsTokenIgnoreCase(value, valueLength, "gzip");
      } else if (headerNameIs(line, nameLength, "x-job-id")) {
        copyValue(out.jobId, sizeof(out.jobId), value, valueLength);
      } else if (headerNameIs(line, nameLength, "x-job-method")) {
        copyValue(out.jobMethod, sizeof(out.jobMethod), value, valueLength);
      } else if (headerNameIs(line, nameLength, "x-job-path")) {
        copyValue(out.jobPath, sizeof(out.jobPath), value, valueLength);
      } else if (headerNameIs(line, nameLength, "x-job-if-none-match")) {
        copyValue(out.jobIfNoneMatch, sizeof(out.jobIfNoneMatch), value,
                  valueLength);
      }
    }
    line = next != nullptr ? next + 1 : end;
  }
  return true;
}

long remoteAdminDechunk(uint8_t *body, size_t length) {
  size_t read = 0;
  size_t write = 0;
  for (;;) {
    size_t size = 0;
    size_t digits = 0;
    while (read < length && isxdigit(body[read])) {
      const char c = static_cast<char>(tolower(body[read]));
      size = size * 16 + static_cast<size_t>(c <= '9' ? c - '0' : c - 'a' + 10);
      if (++digits > 8) return -1;
      ++read;
    }
    if (digits == 0) return -1;
    // Rozšíření za středníkem se přeskočí.
    while (read < length && body[read] != '\r') ++read;
    if (read + 1 >= length || body[read + 1] != '\n') return -1;
    read += 2;
    if (size == 0) return static_cast<long>(write);
    if (size > length - read) return -1;
    memmove(body + write, body + read, size);
    write += size;
    read += size;
    if (read + 1 >= length || body[read] != '\r' || body[read + 1] != '\n')
      return -1;
    read += 2;
  }
}

long remoteAdminCompleteLength(const uint8_t *data, size_t length) {
  const size_t headEnd = remoteAdminHeadLength(data, length);
  if (headEnd == 0) return 0;
  RemoteAdminHead head;
  if (!remoteAdminParseHead(reinterpret_cast<const char *>(data), headEnd - 4,
                            head))
    return -1;
  if (head.chunked) {
    // Projde velikosti bloků bez rozbalování až k poslednímu (0) a prázdnému
    // řádku za případnými trailery.
    size_t read = headEnd;
    for (;;) {
      size_t size = 0;
      size_t digits = 0;
      while (read < length && isxdigit(data[read])) {
        const char c = static_cast<char>(tolower(data[read]));
        size = size * 16 + static_cast<size_t>(c <= '9' ? c - '0' : c - 'a' + 10);
        if (++digits > 8) return -1;
        ++read;
      }
      if (read >= length) return 0;
      if (digits == 0) return -1;
      while (read < length && data[read] != '\n') ++read;
      if (read >= length) return 0;
      ++read;
      if (size == 0) {
        for (;;) {
          size_t lineEnd = read;
          while (lineEnd < length && data[lineEnd] != '\n') ++lineEnd;
          if (lineEnd >= length) return 0;
          const bool empty = lineEnd == read ||
                             (lineEnd == read + 1 && data[read] == '\r');
          read = lineEnd + 1;
          if (empty) return static_cast<long>(read);
        }
      }
      if (size > length - read || length - read - size < 2) return 0;
      read += size + 2;
    }
  }
  if (head.contentLength >= 0) {
    const size_t wanted = headEnd + static_cast<size_t>(head.contentLength);
    return length >= wanted ? static_cast<long>(wanted) : 0;
  }
  if (head.status == 204 || head.status == 304 ||
      (head.status >= 100 && head.status < 200))
    return static_cast<long>(headEnd);
  return -1;
}

void remoteAdminBodyTag(const uint8_t *data, size_t length,
                        char out[REMOTE_ADMIN_TAG_LENGTH]) {
  uint64_t hash = 0xcbf29ce484222325ULL;
  for (size_t i = 0; i < length; ++i) {
    hash ^= data[i];
    hash *= 0x100000001b3ULL;
  }
  static constexpr char HEX_DIGITS[] = "0123456789abcdef";
  for (int i = 15; i >= 0; --i) {
    out[i] = HEX_DIGITS[hash & 0x0F];
    hash >>= 4;
  }
  out[16] = '\0';
}

size_t remoteAdminBuildLocalRequest(char *out, size_t capacity,
                                    const char *method, const char *path,
                                    const char *contentType,
                                    const char *loopbackKey,
                                    size_t bodyLength) {
  const bool withBody = strcmp(method, "POST") == 0;
  // Typ přišel ze serveru; řídicí znak by do požadavku propašoval hlavičku.
  bool typeUsable = contentType != nullptr && contentType[0] != '\0' &&
                    strlen(contentType) < REMOTE_ADMIN_CONTENT_TYPE_LENGTH;
  for (const char *c = contentType; typeUsable && *c != '\0'; ++c)
    if (static_cast<unsigned char>(*c) < ' ' || *c == 0x7F) typeUsable = false;
  int written;
  if (withBody) {
    written = snprintf(out, capacity,
                       "%s %s HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                       "Connection: close\r\nX-Remote-Admin: %s\r\n"
                       "Content-Type: %s\r\nContent-Length: %u\r\n\r\n",
                       method, path, loopbackKey,
                       typeUsable ? contentType
                                  : "application/x-www-form-urlencoded",
                       static_cast<unsigned>(bodyLength));
  } else {
    written = snprintf(out, capacity,
                       "%s %s HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                       "Connection: close\r\nX-Remote-Admin: %s\r\n\r\n",
                       method, path, loopbackKey);
  }
  if (written <= 0 || static_cast<size_t>(written) >= capacity) return 0;
  return static_cast<size_t>(written);
}
