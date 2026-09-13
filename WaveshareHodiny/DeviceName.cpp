#include "DeviceName.h"

#include <Preferences.h>

#include <cstring>

namespace {
constexpr char DEVICE_NAME_PARTITION[] = "clockcfg";
constexpr char DEVICE_NAME_NAMESPACE[] = "device-name";
constexpr char DEVICE_NAME_KEY[] = "name";

void copyName(char *target, size_t capacity, const char *source) {
  if (target == nullptr || capacity == 0) return;
  strncpy(target, source, capacity - 1);
  target[capacity - 1] = '\0';
}
}  // namespace

bool deviceNameValid(const char *name) {
  if (name == nullptr) return false;
  const size_t length = strlen(name);
  if (length == 0 || length >= DEVICE_NAME_LENGTH || name[0] == '-' ||
      name[length - 1] == '-')
    return false;
  for (size_t index = 0; index < length; ++index) {
    const char character = name[index];
    const bool allowed = (character >= 'a' && character <= 'z') ||
                         (character >= '0' && character <= '9') ||
                         character == '-';
    if (!allowed) return false;
  }
  return true;
}

void deviceNameLoad(char *name, size_t capacity) {
  copyName(name, capacity, DEVICE_NAME_DEFAULT);
  Preferences preferences;
  if (!preferences.begin(DEVICE_NAME_NAMESPACE, true, DEVICE_NAME_PARTITION))
    return;
  // Bez isKey by čtení chybějícího klíče zapsalo do logu chybu NVS při
  // každém startu hodin, které název nikdy neměnily.
  const String stored = preferences.isKey(DEVICE_NAME_KEY)
                            ? preferences.getString(DEVICE_NAME_KEY)
                            : String();
  preferences.end();
  if (deviceNameValid(stored.c_str()) && stored.length() < capacity)
    copyName(name, capacity, stored.c_str());
}

bool deviceNamePersist(const char *name) {
  if (name == nullptr || (name[0] != '\0' && !deviceNameValid(name)))
    return false;
  Preferences preferences;
  if (!preferences.begin(DEVICE_NAME_NAMESPACE, false, DEVICE_NAME_PARTITION))
    return false;
  // Výchozí název se neukládá, aby se ho týkala případná budoucí změna výchozí
  // hodnoty stejně jako hodin, které název nikdy neměnily.
  const bool resetToDefault =
      name[0] == '\0' || strcmp(name, DEVICE_NAME_DEFAULT) == 0;
  const bool saved =
      resetToDefault
          ? preferences.remove(DEVICE_NAME_KEY) ||
                !preferences.isKey(DEVICE_NAME_KEY)
          : preferences.putString(DEVICE_NAME_KEY, name) == strlen(name);
  preferences.end();
  return saved;
}
