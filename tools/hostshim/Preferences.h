#pragma once

// Paměťová náhrada ESP32 Preferences (NVS) pro testy na počítači.
// Data přežívají mezi instancemi Preferences stejně jako skutečné NVS,
// takže migrace i následné uložení lze ověřit v jednom běhu testu.

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace hostshim {

using Store = std::map<std::string, std::vector<uint8_t>>;

inline Store &store() {
  static Store instance;
  return instance;
}

inline std::string entryKey(const std::string &partition,
                            const std::string &space, const char *key) {
  return partition + '/' + space + '/' + (key == nullptr ? "" : key);
}

}  // namespace hostshim

// Testovací rozhraní: vyprázdnění paměti a vložení surového záznamu, jaký by
// v NVS zanechal starší firmware.
inline void hostPreferencesReset() { hostshim::store().clear(); }

inline void hostPreferencesSeedBlob(const char *partition, const char *space,
                                    const char *key, const void *data,
                                    size_t size) {
  const uint8_t *bytes = static_cast<const uint8_t *>(data);
  hostshim::store()[hostshim::entryKey(partition, space, key)] =
      std::vector<uint8_t>(bytes, bytes + size);
}

inline size_t hostPreferencesBlobSize(const char *partition, const char *space,
                                      const char *key) {
  const auto entry = hostshim::store().find(
      hostshim::entryKey(partition, space, key));
  return entry == hostshim::store().end() ? 0 : entry->second.size();
}

class Preferences {
 public:
  bool begin(const char *space, bool readOnly, const char *partition) {
    space_ = space == nullptr ? "" : space;
    partition_ = partition == nullptr ? "" : partition;
    readOnly_ = readOnly;
    open_ = true;
    return true;
  }

  void end() { open_ = false; }

  size_t putBytes(const char *key, const void *value, size_t size) {
    if (!writable()) return 0;
    const uint8_t *bytes = static_cast<const uint8_t *>(value);
    entry(key) = std::vector<uint8_t>(bytes, bytes + size);
    return size;
  }

  size_t getBytesLength(const char *key) {
    const std::vector<uint8_t> *found = lookup(key);
    return found == nullptr ? 0 : found->size();
  }

  size_t getBytes(const char *key, void *output, size_t maxSize) {
    const std::vector<uint8_t> *found = lookup(key);
    if (found == nullptr) return 0;
    const size_t size = found->size() < maxSize ? found->size() : maxSize;
    memcpy(output, found->data(), size);
    return size;
  }

  bool remove(const char *key) {
    if (!writable()) return false;
    return hostshim::store().erase(
               hostshim::entryKey(partition_, space_, key)) > 0;
  }

  size_t putUChar(const char *key, uint8_t value) {
    return putScalar(key, value);
  }
  size_t putUInt(const char *key, uint32_t value) {
    return putScalar(key, value);
  }
  size_t putBool(const char *key, bool value) { return putScalar(key, value); }

  uint8_t getUChar(const char *key, uint8_t fallback) {
    return getScalar(key, fallback);
  }
  uint32_t getUInt(const char *key, uint32_t fallback) {
    return getScalar(key, fallback);
  }
  bool getBool(const char *key, bool fallback) {
    return getScalar(key, fallback);
  }

 private:
  bool writable() const { return open_ && !readOnly_; }

  std::vector<uint8_t> &entry(const char *key) {
    return hostshim::store()[hostshim::entryKey(partition_, space_, key)];
  }

  const std::vector<uint8_t> *lookup(const char *key) {
    if (!open_) return nullptr;
    const auto found = hostshim::store().find(
        hostshim::entryKey(partition_, space_, key));
    return found == hostshim::store().end() ? nullptr : &found->second;
  }

  template <typename T>
  size_t putScalar(const char *key, T value) {
    if (!writable()) return 0;
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&value);
    entry(key) = std::vector<uint8_t>(bytes, bytes + sizeof(T));
    return sizeof(T);
  }

  template <typename T>
  T getScalar(const char *key, T fallback) {
    const std::vector<uint8_t> *found = lookup(key);
    if (found == nullptr || found->size() != sizeof(T)) return fallback;
    T value;
    memcpy(&value, found->data(), sizeof(T));
    return value;
  }

  std::string space_;
  std::string partition_;
  bool readOnly_ = false;
  bool open_ = false;
};
