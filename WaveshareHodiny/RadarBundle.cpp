#include "RadarBundle.h"

#include <stdlib.h>
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

namespace {

// Okno zlibu: back-reference sahá nejvýš 32 kB zpět. tinfl v ROM píše do
// kruhového bufferu přesně této velikosti (mocnina dvou).
constexpr size_t WINDOW_BYTES = 32768;
constexpr size_t WINDOW_MASK = WINDOW_BYTES - 1;
constexpr size_t MAX_ROW_BYTES = 1024;

uint8_t *window = nullptr;     // WINDOW_BYTES výstupu, pak 3 buňky řádků
size_t windowRowBytes = 0;
#if defined(ESP_PLATFORM)
tinfl_decompressor *streamDecompressor = nullptr;
#else
z_stream *stream = nullptr;
#endif

uint8_t *rowSlot(int y) {
  return window + WINDOW_BYTES + static_cast<size_t>(y % 3) * windowRowBytes;
}

void *allocateLarge(size_t bytes) {
#if defined(ESP_PLATFORM)
  return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  return malloc(bytes);
#endif
}

void freeLarge(void *pointer) {
#if defined(ESP_PLATFORM)
  heap_caps_free(pointer);
#else
  free(pointer);
#endif
}

}  // namespace

RadarBundleRows::~RadarBundleRows() {
#if !defined(ESP_PLATFORM)
  if (started_ && stream != nullptr) inflateEnd(stream);
#endif
}

bool RadarBundleRows::begin(const uint8_t *data, size_t size, uint16_t width,
                            uint16_t height) {
#if !defined(ESP_PLATFORM)
  if (started_ && stream != nullptr) inflateEnd(stream);
#endif
  started_ = false;
  failed_ = true;
  streamDone_ = false;
  input_ = data;
  inputLeft_ = size;
  width_ = width;
  height_ = height;
  produced_ = 0;
  readPos_ = 0;
  rowFill_ = 0;
  rowsDone_ = 0;
  for (const uint8_t *&pointer : rowPointer_) pointer = nullptr;
  if (data == nullptr || size == 0 || width == 0 || width > MAX_ROW_BYTES ||
      height == 0)
    return false;
  if (!radarBundleRowsReserve(width)) return false;
#if defined(ESP_PLATFORM)
  tinfl_init(streamDecompressor);
#else
  memset(stream, 0, sizeof(*stream));
  if (inflateInit(stream) != Z_OK) return false;
  stream->next_in = const_cast<Bytef *>(data);
  stream->avail_in = static_cast<uInt>(size);
#endif
  started_ = true;
  failed_ = false;
  return true;
}

// Rozbalí další kus výstupu do okna, nejvýš po jeho konec. Volá se, jen když
// je všechno dřív rozbalené převzaté, takže nepřepíše nic, co ještě čeká.
bool RadarBundleRows::decompressMore() {
  if (failed_ || streamDone_) return false;
  const size_t offset = produced_ & WINDOW_MASK;
  size_t out = WINDOW_BYTES - offset;
#if defined(ESP_PLATFORM)
  size_t in = inputLeft_;
  const tinfl_status status = tinfl_decompress(
      streamDecompressor, input_, &in, window, window + offset, &out,
      TINFL_FLAG_PARSE_ZLIB_HEADER);
  input_ += in;
  inputLeft_ -= in;
  if (status == TINFL_STATUS_DONE) {
    streamDone_ = true;
  } else if (status != TINFL_STATUS_HAS_MORE_OUTPUT) {
    // Chyba dat, kontrolního součtu, nebo useknutý proud (žádný další vstup
    // nepřijde).
    failed_ = true;
    return false;
  }
#else
  stream->next_out = window + offset;
  stream->avail_out = static_cast<uInt>(out);
  const int status = inflate(stream, Z_NO_FLUSH);
  out -= stream->avail_out;
  if (status == Z_STREAM_END) {
    streamDone_ = true;
  } else if (status != Z_OK) {
    failed_ = true;
    return false;
  }
#endif
  produced_ += out;
  if (out == 0 && !streamDone_) failed_ = true;
  return out > 0;
}

// Řádky pod "below", které se čtou rovnou z okna, se před dalším rozbalením
// zkopírují do svých buněk: okno se přepíše.
void RadarBundleRows::pinLookback(int below) {
  for (int y = below - 2; y < below; ++y) {
    if (y < 0) continue;
    uint8_t *slot = rowSlot(y);
    if (rowPointer_[y % 3] != slot) {
      memcpy(slot, rowPointer_[y % 3], width_);
      rowPointer_[y % 3] = slot;
    }
  }
}

const uint8_t *RadarBundleRows::row(int y) {
  if (!started_ || failed_ || y < 0 || y >= height_ || y < rowsDone_ - 3)
    return nullptr;
  while (rowsDone_ <= y) {
    const int current = rowsDone_;
    const size_t offset = readPos_ & WINDOW_MASK;
    // Celý řádek leží v okně v kuse: bez kopírování.
    if (rowFill_ == 0 && produced_ - readPos_ >= width_ &&
        offset + width_ <= WINDOW_BYTES) {
      rowPointer_[current % 3] = window + offset;
      readPos_ += width_;
      ++rowsDone_;
      continue;
    }
    // Řádek přes konec okna nebo přes konec rozbaleného kusu: poskládá se
    // do buňky.
    uint8_t *slot = rowSlot(current);
    while (rowFill_ < width_) {
      if (readPos_ == produced_) {
        pinLookback(current);
        if (!decompressMore()) {
          failed_ = true;
          return nullptr;
        }
      }
      const size_t from = readPos_ & WINDOW_MASK;
      size_t count = width_ - rowFill_;
      if (count > produced_ - readPos_) count = produced_ - readPos_;
      if (count > WINDOW_BYTES - from) count = WINDOW_BYTES - from;
      memcpy(slot + rowFill_, window + from, count);
      rowFill_ += count;
      readPos_ += count;
    }
    rowFill_ = 0;
    rowPointer_[current % 3] = slot;
    ++rowsDone_;
  }
  return rowPointer_[y % 3];
}

bool RadarBundleRows::finish() {
  if (!started_ || failed_ || rowsDone_ != height_) return false;
  // Proud musí skončit přesně za posledním řádkem: víc dat znamená jiný
  // rozměr, než o který se hodiny ptaly.
  while (!streamDone_) {
    if (decompressMore()) {
      failed_ = true;
      return false;
    }
    if (failed_) return false;
  }
  return produced_ == readPos_;
}

bool radarBundleRowsReserve(uint16_t width) {
  if (width == 0 || width > MAX_ROW_BYTES) return false;
  if (window == nullptr || windowRowBytes < width) {
    if (window != nullptr) freeLarge(window);
    window = static_cast<uint8_t *>(allocateLarge(WINDOW_BYTES + 3 * width));
    windowRowBytes = window != nullptr ? width : 0;
    if (window == nullptr) return false;
  }
#if defined(ESP_PLATFORM)
  // Stav inflátoru (~11 kB) v PSRAM, ne na zásobníku úlohy radaru.
  if (streamDecompressor == nullptr) {
    streamDecompressor =
        static_cast<tinfl_decompressor *>(allocateLarge(sizeof(tinfl_decompressor)));
    if (streamDecompressor == nullptr) return false;
  }
#else
  if (stream == nullptr) stream = new z_stream;
#endif
  return true;
}

void radarBundleRowsRelease() {
  if (window != nullptr) freeLarge(window);
  window = nullptr;
  windowRowBytes = 0;
#if defined(ESP_PLATFORM)
  if (streamDecompressor != nullptr) freeLarge(streamDecompressor);
  streamDecompressor = nullptr;
#else
  if (stream != nullptr) delete stream;
  stream = nullptr;
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
