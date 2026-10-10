#include "PsramGrowBuffer.h"

#include <esp_heap_caps.h>

namespace {
constexpr uint32_t PSRAM_CAPS = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
}

bool PsramGrowBuffer::ensure() {
  if (data_ != nullptr) return true;
  size_t size = initial_ < maximum_ ? initial_ : maximum_;
  while (size >= minimum_ && size > 0) {
    data_ = static_cast<uint8_t *>(heap_caps_malloc(size, PSRAM_CAPS));
    if (data_ != nullptr) {
      capacity_ = size;
      return true;
    }
    size /= 2;
  }
  return false;
}

bool PsramGrowBuffer::reserve(size_t size) {
  if (size > maximum_) return false;
  if (data_ == nullptr && !ensure()) return false;
  if (capacity_ >= size) return true;
  auto *grown =
      static_cast<uint8_t *>(heap_caps_realloc(data_, size, PSRAM_CAPS));
  if (grown == nullptr) return false;
  data_ = grown;
  capacity_ = size;
  return true;
}

bool PsramGrowBuffer::grow() {
  if (data_ == nullptr || capacity_ >= maximum_) return false;
  const size_t doubled = capacity_ * 2;
  return reserve(doubled < maximum_ ? doubled : maximum_);
}

void PsramGrowBuffer::release() {
  if (data_ != nullptr) heap_caps_free(data_);
  data_ = nullptr;
  capacity_ = 0;
}
