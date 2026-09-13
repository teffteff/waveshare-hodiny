// Ověří název hodin v síti: co projde jako jméno .local a že se uložený
// název načte, neplatný ignoruje a prázdný vrátí hodiny k výchozímu.
//
// Překlad viz tools/run_host_tests.sh.

#include <cassert>
#include <cstring>

#include "DeviceName.h"
#include "Preferences.h"

namespace {

void testValidNames() {
  assert(deviceNameValid("kuchyn"));
  assert(deviceNameValid("obyvak-2"));
  assert(deviceNameValid("3"));
  assert(deviceNameValid(DEVICE_NAME_DEFAULT));
  assert(deviceNameValid("abcdefghijklmnopqrstuvwxyz012345"));
  assert(!deviceNameValid("abcdefghijklmnopqrstuvwxyz0123456"));
  assert(!deviceNameValid(""));
  assert(!deviceNameValid(nullptr));
  assert(!deviceNameValid("-kuchyn"));
  assert(!deviceNameValid("kuchyn-"));
  assert(!deviceNameValid("Kuchyn"));
  assert(!deviceNameValid("kuchyň"));
  assert(!deviceNameValid("kuchyn.local"));
  assert(!deviceNameValid("obyvak pokoj"));
  assert(!deviceNameValid("a_b"));
}

void testLoadAndPersist() {
  hostPreferencesReset();
  char name[DEVICE_NAME_LENGTH];
  deviceNameLoad(name, sizeof(name));
  assert(strcmp(name, DEVICE_NAME_DEFAULT) == 0);

  assert(deviceNamePersist("loznice"));
  deviceNameLoad(name, sizeof(name));
  assert(strcmp(name, "loznice") == 0);

  assert(!deviceNamePersist("Loznice"));
  deviceNameLoad(name, sizeof(name));
  assert(strcmp(name, "loznice") == 0);

  assert(deviceNamePersist(""));
  deviceNameLoad(name, sizeof(name));
  assert(strcmp(name, DEVICE_NAME_DEFAULT) == 0);
  assert(hostPreferencesBlobSize("clockcfg", "device-name", "name") == 0);

  // Výchozí název se neukládá, jen smaže uložený.
  assert(deviceNamePersist("pracovna"));
  assert(deviceNamePersist(DEVICE_NAME_DEFAULT));
  assert(hostPreferencesBlobSize("clockcfg", "device-name", "name") == 0);

  // Poškozený záznam v NVS nesmí dát hodinám neplatné jméno.
  const char broken[] = "Spatne jmeno";
  hostPreferencesSeedBlob("clockcfg", "device-name", "name", broken,
                          strlen(broken));
  deviceNameLoad(name, sizeof(name));
  assert(strcmp(name, DEVICE_NAME_DEFAULT) == 0);
}

}  // namespace

int main() {
  testValidNames();
  testLoadAndPersist();
  return 0;
}
