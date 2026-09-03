#include "HttpDownload.h"

#include "HttpBodyReader.h"

namespace {
constexpr char TRANSFER_ENCODING_HEADER[] = "Transfer-Encoding";

class NetworkClientByteSource : public HttpByteSource {
 public:
  explicit NetworkClientByteSource(NetworkClient &client) : client_(client) {}

  int available() override { return client_.available(); }

  int read(uint8_t *buffer, size_t size) override {
    return client_.read(buffer, size);
  }

  bool connected() override { return client_.connected(); }

 private:
  NetworkClient &client_;
};

class PrintByteSink : public HttpByteSink {
 public:
  explicit PrintByteSink(Print &destination) : destination_(destination) {}

  size_t write(const uint8_t *data, size_t size) override {
    return destination_.write(data, size);
  }

 private:
  Print &destination_;
};
}  // namespace

void httpDownloadPrepare(HTTPClient &http) {
  static const char *headerKeys[] = {TRANSFER_ENCODING_HEADER};
  http.collectHeaders(headerKeys, 1);
}

int httpDownloadBody(HTTPClient &http, Print &destination,
                     uint32_t idleTimeoutMs) {
  NetworkClient *stream = http.getStreamPtr();
  if (stream == nullptr) return HTTP_BODY_TIMEOUT;
  String encoding = http.header(TRANSFER_ENCODING_HEADER);
  encoding.toLowerCase();
  const bool chunked = encoding.indexOf("chunked") >= 0;
  NetworkClientByteSource source(*stream);
  PrintByteSink sink(destination);
  return httpBodyRead(source, sink, chunked, http.getSize(), idleTimeoutMs);
}
