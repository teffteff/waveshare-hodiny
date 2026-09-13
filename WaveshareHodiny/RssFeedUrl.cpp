#include "RssFeedUrl.h"

#include <stdio.h>
#include <string.h>

namespace {

class UrlWriter {
 public:
  UrlWriter(char *output, size_t capacity)
      : output_(output), capacity_(capacity) {}

  void append(const char *text, size_t length) {
    if (overflowed_) return;
    // Jeden bajt zůstává na ukončovací nulu.
    if (length >= capacity_ - length_) {
      overflowed_ = true;
      return;
    }
    memcpy(output_ + length_, text, length);
    length_ += length;
  }

  void append(const char *text) { append(text, strlen(text)); }

  // Jméno místa jde do dotazu, takže se kóduje všechno kromě nevyhrazených
  // znaků z RFC 3986. Diakritika odchází jako bajty UTF-8, jak ji čte každý
  // server; mezera jako %20, ne "+", aby platila i v cestě.
  void appendEncoded(const char *text) {
    static constexpr char HEX[] = "0123456789ABCDEF";
    for (const unsigned char *cursor =
             reinterpret_cast<const unsigned char *>(text);
         *cursor != '\0'; ++cursor) {
      const unsigned char value = *cursor;
      const bool unreserved = (value >= 'A' && value <= 'Z') ||
                              (value >= 'a' && value <= 'z') ||
                              (value >= '0' && value <= '9') ||
                              value == '-' || value == '.' || value == '_' ||
                              value == '~';
      if (unreserved) {
        const char plain = static_cast<char>(value);
        append(&plain, 1);
      } else {
        const char encoded[3] = {'%', HEX[value >> 4], HEX[value & 0x0F]};
        append(encoded, sizeof(encoded));
      }
    }
  }

  void appendCoordinate(float value) {
    char number[24];
    const int written =
        snprintf(number, sizeof(number), "%.5f", static_cast<double>(value));
    if (written < 0 || static_cast<size_t>(written) >= sizeof(number)) {
      overflowed_ = true;
      return;
    }
    append(number, static_cast<size_t>(written));
  }

  bool finish() {
    if (overflowed_) {
      output_[0] = '\0';
      return false;
    }
    output_[length_] = '\0';
    return true;
  }

 private:
  char *output_;
  size_t capacity_;
  size_t length_ = 0;
  bool overflowed_ = false;
};

bool startsWith(const char *text, const char *prefix, size_t prefixLength) {
  return strncmp(text, prefix, prefixLength) == 0;
}

}  // namespace

bool rssFeedBuildUrl(const char *templateUrl, const char *city, float latitude,
                     float longitude, char *output, size_t capacity) {
  if (output == nullptr || capacity == 0) return false;
  output[0] = '\0';
  if (templateUrl == nullptr) return false;
  if (city == nullptr) city = "";

  const size_t cityTokenLength = sizeof(RSS_FEED_URL_CITY_TOKEN) - 1;
  const size_t latitudeTokenLength = sizeof(RSS_FEED_URL_LATITUDE_TOKEN) - 1;
  const size_t longitudeTokenLength = sizeof(RSS_FEED_URL_LONGITUDE_TOKEN) - 1;

  UrlWriter writer(output, capacity);
  const char *cursor = templateUrl;
  while (*cursor != '\0') {
    if (startsWith(cursor, RSS_FEED_URL_CITY_TOKEN, cityTokenLength)) {
      writer.appendEncoded(city);
      cursor += cityTokenLength;
    } else if (startsWith(cursor, RSS_FEED_URL_LATITUDE_TOKEN,
                          latitudeTokenLength)) {
      writer.appendCoordinate(latitude);
      cursor += latitudeTokenLength;
    } else if (startsWith(cursor, RSS_FEED_URL_LONGITUDE_TOKEN,
                          longitudeTokenLength)) {
      writer.appendCoordinate(longitude);
      cursor += longitudeTokenLength;
    } else {
      writer.append(cursor, 1);
      ++cursor;
    }
  }
  return writer.finish();
}
