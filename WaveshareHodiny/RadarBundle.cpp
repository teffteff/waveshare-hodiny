#include "RadarBundle.h"

#include <string.h>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#include "miniz.h"  // inflátor v ROM (esp_rom)
#else
#include <zlib.h>
#endif

namespace {

uint16_t readU16(const uint8_t *data) {
  return static_cast<uint16_t>(data[0] | (data[1] << 8));
}

uint32_t readU32(const uint8_t *data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

constexpr size_t HEADER_BYTES = 16;
constexpr size_t FRAME_HEADER_BYTES = 8;

}  // namespace

bool radarBundleParse(const uint8_t *data, size_t size, uint16_t width,
                      uint16_t height, RadarBundle &bundle) {
  bundle = RadarBundle{};
  if (data == nullptr || size < HEADER_BYTES || memcmp(data, "WHR1", 4) != 0)
    return false;
  bundle.width = readU16(data + 4);
  bundle.height = readU16(data + 6);
  bundle.count = data[8];
  const uint8_t version = data[9];
  bundle.newestSlot = readU32(data + 12);
  if (version != RADAR_BUNDLE_VERSION || bundle.width != width ||
      bundle.height != height || bundle.count == 0 ||
      bundle.count > RADAR_BUNDLE_MAX_FRAMES)
    return false;
  size_t offset = HEADER_BYTES;
  for (uint8_t index = 0; index < bundle.count; ++index) {
    if (size - offset < FRAME_HEADER_BYTES) return false;
    RadarBundleFrame &frame = bundle.frames[index];
    frame.slot = readU32(data + offset);
    frame.size = readU32(data + offset + 4);
    offset += FRAME_HEADER_BYTES;
    if (frame.size == 0 || frame.size > size - offset) return false;
    if (index > 0 && frame.slot <= bundle.frames[index - 1].slot) return false;
    frame.offset = static_cast<uint32_t>(offset);
    offset += frame.size;
  }
  return offset == size &&
         bundle.frames[bundle.count - 1].slot == bundle.newestSlot;
}

bool radarBundleInflate(const uint8_t *data, size_t size, uint8_t *output,
                        size_t outputSize) {
  if (data == nullptr || output == nullptr || size == 0) return false;
#if defined(ESP_PLATFORM)
  // Inflátor z ROM; jeho stav (~11 kB) se drží v PSRAM, aby nezatěžoval
  // zásobník úlohy radaru. Celý výstup se vejde najednou, takže okno slovníku
  // je přímo výstupní buffer.
  static tinfl_decompressor *decompressor = nullptr;
  if (decompressor == nullptr) {
    decompressor = static_cast<tinfl_decompressor *>(heap_caps_malloc(
        sizeof(tinfl_decompressor), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (decompressor == nullptr) return false;
  }
  tinfl_init(decompressor);
  size_t inBytes = size;
  size_t outBytes = outputSize;
  const tinfl_status status = tinfl_decompress(
      decompressor, data, &inBytes, output, output, &outBytes,
      TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  return status == TINFL_STATUS_DONE && outBytes == outputSize;
#else
  uLongf outBytes = static_cast<uLongf>(outputSize);
  return uncompress(output, &outBytes, data, static_cast<uLong>(size)) == Z_OK &&
         outBytes == outputSize;
#endif
}

bool radarBundleUrlFromRainUrl(const char *rainUrl, char *output,
                               size_t capacity) {
  if (output == nullptr || capacity == 0) return false;
  output[0] = '\0';
  if (rainUrl == nullptr) return false;
  static const char rainPath[] = "/rain.json";
  static const char radarPath[] = "/radar.bin";
  const char *query = strchr(rainUrl, '?');
  const size_t length = query != nullptr ? static_cast<size_t>(query - rainUrl)
                                         : strlen(rainUrl);
  const size_t pathLength = sizeof(rainPath) - 1;
  if (length < pathLength ||
      memcmp(rainUrl + length - pathLength, rainPath, pathLength) != 0)
    return false;
  const size_t prefix = length - pathLength;
  if (prefix + sizeof(radarPath) > capacity) return false;
  memcpy(output, rainUrl, prefix);
  memcpy(output + prefix, radarPath, sizeof(radarPath));
  return true;
}
