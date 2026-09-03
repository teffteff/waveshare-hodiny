#include "HttpBodyReader.h"

#include <Arduino.h>
#include <stdlib.h>

namespace {
// Vejde se na zásobník i úlohám, které souběžně drží TLS relaci.
constexpr size_t COPY_BUFFER_BYTES = 512;
// "1a2b;jmeno=hodnota" je nejdelší rozumné záhlaví části; delší je chyba.
constexpr size_t FRAMING_LINE_CAPACITY = 32;
// Jak dlouho se čeká mezi dotazy, když zrovna žádný bajt nedorazil.
constexpr uint32_t POLL_INTERVAL_MS = 2;

// Přenese `count` bajtů. Záporný `count` znamená "dokud server posílá", což je
// řádný konec těla u odpovědi bez Content-Length.
//
// Limit nečinnosti se počítá od posledního přeneseného bajtu, ne od začátku,
// takže pomalé, ale postupující stahování se nepřeruší. Zároveň se nemůže
// zacyklit: zdroj, který hlásí dostupná data a žádná nevydá, limit vyčerpá.
// Zavřené spojení je řádný konec, ne důvod čekat celý limit; bez toho by každá
// odpověď bez Content-Length stála osm sekund navíc.
int copySegment(HttpByteSource &source, HttpByteSink &sink, long count,
                uint32_t idleTimeoutMs, size_t &written) {
  uint8_t buffer[COPY_BUFFER_BYTES];
  long remaining = count;
  uint32_t lastProgressAt = millis();
  while (remaining != 0) {
    const int availableBytes = source.available();
    int received = 0;
    if (availableBytes > 0) {
      size_t wanted = static_cast<size_t>(availableBytes);
      if (wanted > sizeof(buffer)) wanted = sizeof(buffer);
      if (remaining > 0 && wanted > static_cast<size_t>(remaining))
        wanted = static_cast<size_t>(remaining);
      received = source.read(buffer, wanted);
    }
    if (received <= 0) {
      if (!source.connected()) return count < 0 ? HTTP_BODY_OK
                                                : HTTP_BODY_TIMEOUT;
      if (millis() - lastProgressAt >= idleTimeoutMs)
        return count < 0 ? HTTP_BODY_OK : HTTP_BODY_TIMEOUT;
      delay(POLL_INTERVAL_MS);
      continue;
    }
    if (sink.write(buffer, static_cast<size_t>(received)) !=
        static_cast<size_t>(received)) {
      return HTTP_BODY_WRITE;
    }
    written += static_cast<size_t>(received);
    if (remaining > 0) remaining -= received;
    lastProgressAt = millis();
  }
  return HTTP_BODY_OK;
}

// Přečte jeden řádek rámování zakončený \n a odstraní z něj \r. Vrací délku
// bez zakončení, nebo záporný HttpBodyStatus.
int readFramingLine(HttpByteSource &source, char *destination, size_t capacity,
                    uint32_t idleTimeoutMs) {
  size_t length = 0;
  uint32_t lastProgressAt = millis();
  for (;;) {
    uint8_t value = 0;
    const int received =
        source.available() > 0 ? source.read(&value, 1) : 0;
    if (received <= 0) {
      // Uprostřed rámování je zavřené spojení useknutý přenos, ne řádný konec.
      if (!source.connected()) return HTTP_BODY_TIMEOUT;
      if (millis() - lastProgressAt >= idleTimeoutMs) return HTTP_BODY_TIMEOUT;
      delay(POLL_INTERVAL_MS);
      continue;
    }
    lastProgressAt = millis();
    if (value == '\n') {
      while (length > 0 && destination[length - 1] == '\r') --length;
      destination[length] = '\0';
      return static_cast<int>(length);
    }
    if (length + 1 >= capacity) return HTTP_BODY_ENCODING;
    destination[length++] = static_cast<char>(value);
  }
}
}  // namespace

int httpBodyRead(HttpByteSource &source, HttpByteSink &sink, bool chunked,
                 long declaredLength, uint32_t idleTimeoutMs) {
  size_t written = 0;
  if (!chunked) {
    const int result =
        copySegment(source, sink, declaredLength, idleTimeoutMs, written);
    if (result < 0) return result;
    return static_cast<int>(written);
  }

  char line[FRAMING_LINE_CAPACITY];
  for (;;) {
    const int lineLength =
        readFramingLine(source, line, sizeof(line), idleTimeoutMs);
    if (lineLength < 0) return lineLength;
    // Prázdný řádek před velikostí posílají některé servery navíc.
    if (lineLength == 0) continue;
    char *end = nullptr;
    const long chunkLength = strtol(line, &end, 16);
    if (end == line || chunkLength < 0) return HTTP_BODY_ENCODING;
    // Nulová část ukončuje tělo; případné trailery už nikdo nečte.
    if (chunkLength == 0) break;
    const int result =
        copySegment(source, sink, chunkLength, idleTimeoutMs, written);
    if (result < 0) return result;
    const int trailerLength =
        readFramingLine(source, line, sizeof(line), idleTimeoutMs);
    if (trailerLength < 0) return trailerLength;
    if (trailerLength != 0) return HTTP_BODY_ENCODING;
  }
  return static_cast<int>(written);
}
