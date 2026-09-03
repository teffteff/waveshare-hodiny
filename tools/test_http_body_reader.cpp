#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "Arduino.h"
#include "HttpBodyReader.h"

namespace {

constexpr uint32_t IDLE_TIMEOUT_MS = 8000;

// Zdroj, který vydává předem daný proud po dávkách. Po vyčerpání se ve
// výchozím stavu tváří jako spojení, které je pořád otevřené, ale už nic
// neposílá - přesně to zaseklo HTTPClient::writeToStream().
class ScriptedSource : public HttpByteSource {
 public:
  ScriptedSource(std::string data, size_t batchSize)
      : data_(std::move(data)), batchSize_(batchSize) {}

  int available() override {
    const size_t remaining = data_.size() - position_;
    if (remaining == 0) return stallReportsData_ ? 1 : 0;
    return static_cast<int>(remaining < batchSize_ ? remaining : batchSize_);
  }

  int read(uint8_t *buffer, size_t size) override {
    const size_t remaining = data_.size() - position_;
    if (remaining == 0) return 0;
    size_t count = remaining < size ? remaining : size;
    if (count > batchSize_) count = batchSize_;
    memcpy(buffer, data_.data() + position_, count);
    position_ += count;
    ++readCalls_;
    return static_cast<int>(count);
  }

  bool connected() override {
    return !closeAfterEnd_ || position_ < data_.size();
  }

  // Zapne lhaní: available() hlásí data, read() žádná nevydá.
  void reportDataAfterEnd() { stallReportsData_ = true; }
  // Server po odeslání dat spojení zavře, jako to dělá odpověď bez
  // Content-Length.
  void closeAfterEnd() { closeAfterEnd_ = true; }
  size_t readCalls() const { return readCalls_; }

 private:
  std::string data_;
  size_t batchSize_;
  size_t position_ = 0;
  size_t readCalls_ = 0;
  bool stallReportsData_ = false;
  bool closeAfterEnd_ = false;
};

class StringSink : public HttpByteSink {
 public:
  size_t write(const uint8_t *data, size_t size) override {
    const size_t remaining = capacity_ - content_.size();
    const size_t accepted = size < remaining ? size : remaining;
    content_.append(reinterpret_cast<const char *>(data), accepted);
    return accepted;
  }

  void setCapacity(size_t capacity) { capacity_ = capacity; }
  const std::string &content() const { return content_; }

 private:
  std::string content_;
  size_t capacity_ = 1024 * 1024;
};

int readChunked(const std::string &wire, std::string &output,
                size_t batchSize = 512) {
  ScriptedSource source(wire, batchSize);
  StringSink sink;
  const int result = httpBodyRead(source, sink, true, -1, IDLE_TIMEOUT_MS);
  output = sink.content();
  return result;
}

void testChunkedBody() {
  std::string body;
  // Tvar odpovědi iROZHLAS.cz: chunked, bez Content-Length.
  assert(readChunked("5\r\nHello\r\n6\r\n World\r\n0\r\n\r\n", body) == 11);
  assert(body == "Hello World");

  // Rámování musí zmizet i při čtení po jednom bajtu.
  assert(readChunked("5\r\nHello\r\n6\r\n World\r\n0\r\n\r\n", body, 1) == 11);
  assert(body == "Hello World");

  // Velikost části je šestnáctkově a smí nést rozšíření za středníkem.
  assert(readChunked("1a;jmeno=hodnota\r\nabcdefghijklmnopqrstuvwxyz\r\n0\r\n\r\n",
                     body) == 26);
  assert(body == "abcdefghijklmnopqrstuvwxyz");

  // Prázdné tělo je platná odpověď.
  assert(readChunked("0\r\n\r\n", body) == 0);
  assert(body.empty());
}

void testIdentityBody() {
  // Známá délka z Content-Length: přečte se přesně tolik bajtů.
  {
    ScriptedSource source("0123456789zbytek", 4);
    StringSink sink;
    assert(httpBodyRead(source, sink, false, 10, IDLE_TIMEOUT_MS) == 10);
    assert(sink.content() == "0123456789");
  }
  // Bez Content-Length je řádným koncem to, že server přestane posílat.
  {
    ScriptedSource source("bez delky", 3);
    StringSink sink;
    assert(httpBodyRead(source, sink, false, -1, IDLE_TIMEOUT_MS) == 9);
    assert(sink.content() == "bez delky");
  }
  // Zavřené spojení musí přenos ukončit hned, ne až po limitu nečinnosti.
  // Jinak by každá odpověď bez Content-Length stála celý limit navíc.
  {
    ScriptedSource source("bez delky", 3);
    source.closeAfterEnd();
    StringSink sink;
    const uint32_t startedAt = millis();
    assert(httpBodyRead(source, sink, false, -1, IDLE_TIMEOUT_MS) == 9);
    assert(sink.content() == "bez delky");
    assert(millis() - startedAt < IDLE_TIMEOUT_MS);
  }
  // Useknutá odpověď se známou délkou je chyba i tehdy, když spojení skončilo.
  {
    ScriptedSource source("kratke", 3);
    source.closeAfterEnd();
    StringSink sink;
    assert(httpBodyRead(source, sink, false, 100, IDLE_TIMEOUT_MS) ==
           HTTP_BODY_TIMEOUT);
  }
}

void testStalledTransferGivesUp() {
  // Regrese na zaseknuté stahování: zdroj hlásí dostupná data, žádná nevydá.
  // Dřívější řešení přes HTTPClient::writeToStream() se tady točilo donekonečna
  // a drželo TLS relaci i s interní RAM až do restartu.
  ScriptedSource source("5\r\nHello\r\n6\r\n Wor", 512);
  source.reportDataAfterEnd();
  StringSink sink;
  const uint32_t startedAt = millis();
  assert(httpBodyRead(source, sink, true, -1, IDLE_TIMEOUT_MS) ==
         HTTP_BODY_TIMEOUT);
  // Skončit musí právě po limitu nečinnosti, ne dřív a hlavně ne nikdy.
  assert(millis() - startedAt >= IDLE_TIMEOUT_MS);

  // Totéž u nerámovaného přenosu se známou délkou.
  ScriptedSource identity("kratke", 512);
  identity.reportDataAfterEnd();
  StringSink identitySink;
  assert(httpBodyRead(identity, identitySink, false, 100, IDLE_TIMEOUT_MS) ==
         HTTP_BODY_TIMEOUT);
}

void testTruncatedTransferGivesUp() {
  // Server zavřel spojení uprostřed části: chybí zbytek i ukončující nula.
  ScriptedSource source("5\r\nHel", 512);
  StringSink sink;
  assert(httpBodyRead(source, sink, true, -1, IDLE_TIMEOUT_MS) ==
         HTTP_BODY_TIMEOUT);

  // Totéž, když se navíc zavře spojení: useknuté rámování nesmí projít jako
  // řádně dokončené tělo.
  ScriptedSource closed("5\r\nHel", 512);
  closed.closeAfterEnd();
  StringSink closedSink;
  assert(httpBodyRead(closed, closedSink, true, -1, IDLE_TIMEOUT_MS) ==
         HTTP_BODY_TIMEOUT);
}

void testBrokenFramingIsRejected() {
  // Velikost části není šestnáctkové číslo.
  {
    ScriptedSource source("nesmysl\r\nHello\r\n0\r\n\r\n", 512);
    StringSink sink;
    assert(httpBodyRead(source, sink, true, -1, IDLE_TIMEOUT_MS) ==
           HTTP_BODY_ENCODING);
  }
  // Za tělem části musí následovat prázdný řádek.
  {
    ScriptedSource source("5\r\nHelloXX\r\n0\r\n\r\n", 512);
    StringSink sink;
    assert(httpBodyRead(source, sink, true, -1, IDLE_TIMEOUT_MS) ==
           HTTP_BODY_ENCODING);
  }
  // Nesmyslně dlouhé záhlaví části se nesmí vejít do zásobníku.
  {
    ScriptedSource source(std::string(64, 'a') + "\r\n", 512);
    StringSink sink;
    assert(httpBodyRead(source, sink, true, -1, IDLE_TIMEOUT_MS) ==
           HTTP_BODY_ENCODING);
  }
}

void testSinkRefusalStopsDownload() {
  // Strop velikosti: cíl přijme jen část a stahování se nesmí protáčet dál.
  ScriptedSource source("a\r\n0123456789\r\n0\r\n\r\n", 512);
  StringSink sink;
  sink.setCapacity(4);
  assert(httpBodyRead(source, sink, true, -1, IDLE_TIMEOUT_MS) ==
         HTTP_BODY_WRITE);
  assert(sink.content() == "0123");
}

void testLargeBodyAcrossManyChunks() {
  // Skutečný kanál má kolem 18 kB rozdělených do mnoha částí.
  std::string wire;
  std::string expected;
  for (int index = 0; index < 40; ++index) {
    const std::string part(450, static_cast<char>('A' + index % 26));
    char header[16];
    snprintf(header, sizeof(header), "%x\r\n", static_cast<unsigned>(part.size()));
    wire += header;
    wire += part;
    wire += "\r\n";
    expected += part;
  }
  wire += "0\r\n\r\n";
  std::string body;
  assert(readChunked(wire, body, 300) == static_cast<int>(expected.size()));
  assert(body == expected);
}

}  // namespace

int main() {
  testChunkedBody();
  testIdentityBody();
  testStalledTransferGivesUp();
  testTruncatedTransferGivesUp();
  testBrokenFramingIsRejected();
  testSinkRefusalStopsDownload();
  testLargeBodyAcrossManyChunks();
  printf("http_body_reader: vsechny kontroly prosly\n");
  return 0;
}
