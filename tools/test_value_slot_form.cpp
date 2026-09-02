// Ověří čtení osmi hodnotových slotů z konfiguračního formuláře na počítači.
// Místo WebServeru dodává pole obyčejná tabulka, takže test běží bez zařízení
// i bez síťového zásobníku.
//
// Překlad viz tools/run_host_tests.sh.

#include <cassert>
#include <cstring>
#include <map>
#include <string>

#include "ConfigurationForm.h"

namespace {

using Fields = std::map<std::string, std::string>;

bool fieldsHas(void *context, const String &name) {
  const Fields &fields = *static_cast<const Fields *>(context);
  return fields.count(name.c_str()) > 0;
}

String fieldsGet(void *context, const String &name) {
  const Fields &fields = *static_cast<const Fields *>(context);
  const auto found = fields.find(name.c_str());
  return found == fields.end() ? String("") : String(found->second);
}

ConfigurationFormSource sourceFor(Fields &fields) {
  return ConfigurationFormSource{fieldsHas, fieldsGet, &fields};
}

// Kompletní slot 0 s jedním barevným bodem; jednotlivé testy si pole upraví.
Fields customSlotFields() {
  return Fields{
      {"valueSlot0Enabled", "1"},
      {"valueSlot0Mode", "custom"},
      {"valueSlot0Entity", "sensor.loznice_teplota"},
      {"valueSlot0Name", "LOŽNICE"},
      {"valueSlot0Suffix", "°C"},
      {"valueSlot0Decimals", "1"},
      {"valueSlot0ColorCount", "1"},
      {"valueSlot0ColorValue0", "0"},
      {"valueSlot0ColorColor0", "#65c744"},
  };
}

// Stránka uložená ze starší verze firmwaru žádné pole valueSlot* neposílá.
// Uložený slot musí zůstat beze změny a uložení nesmí selhat.
void testMissingFieldsKeepStoredSlot() {
  Fields fields{{"dayBrightness", "35"}};
  ClockValueSlotConfig slot;
  slot.enabled = true;
  slot.decimals = 2;
  clockConfigCopy(slot.name, sizeof(slot.name), "VENKU");
  clockConfigCopy(slot.entityId, sizeof(slot.entityId), "sensor.venku");

  const ConfigurationFormSource source = sourceFor(fields);
  assert(readValueSlotFromSource(source, 0, slot) ==
         ValueSlotFormResult::Missing);
  assert(slot.enabled);
  assert(slot.decimals == 2);
  assert(strcmp(slot.name, "VENKU") == 0);
  assert(strcmp(slot.entityId, "sensor.venku") == 0);
}

void testCustomSlotIsRead() {
  Fields fields = customSlotFields();
  ClockValueSlotConfig slot;
  const ConfigurationFormSource source = sourceFor(fields);
  assert(readValueSlotFromSource(source, 0, slot) ==
         ValueSlotFormResult::Applied);
  assert(slot.enabled);
  assert(slot.custom);
  assert(strcmp(slot.preset, "custom") == 0);
  assert(strcmp(slot.name, "LOŽNICE") == 0);
  assert(strcmp(slot.entityId, "sensor.loznice_teplota") == 0);
  assert(strcmp(slot.suffix, "°C") == 0);
  assert(slot.decimals == 1);
  assert(slot.color == 0x65C744);
}

// Předvolba přepíše název i jednotku, aby slot odpovídal vybranému typu.
void testPresetSlotOverwritesNameAndSuffix() {
  Fields fields = customSlotFields();
  fields["valueSlot0Mode"] = "preset";
  fields["valueSlot0Preset"] = "co2";
  ClockValueSlotConfig slot;
  const ConfigurationFormSource source = sourceFor(fields);
  assert(readValueSlotFromSource(source, 0, slot) ==
         ValueSlotFormResult::Applied);
  assert(!slot.custom);
  assert(strcmp(slot.preset, "co2") == 0);
  assert(strcmp(slot.name, "CO₂") == 0);
  assert(strcmp(slot.suffix, "ppm") == 0);
}

// Vypnutý slot se stále načte, aby si zachoval nastavení pro příští zapnutí.
void testDisabledSlotKeepsItsSettings() {
  Fields fields = customSlotFields();
  fields["valueSlot0Enabled"] = "0";
  ClockValueSlotConfig slot;
  const ConfigurationFormSource source = sourceFor(fields);
  assert(readValueSlotFromSource(source, 0, slot) ==
         ValueSlotFormResult::Applied);
  assert(!slot.enabled);
  assert(strcmp(slot.name, "LOŽNICE") == 0);
}

void testDecimalsAreClamped() {
  Fields fields = customSlotFields();
  fields["valueSlot0Decimals"] = "9";
  ClockValueSlotConfig slot;
  const ConfigurationFormSource source = sourceFor(fields);
  assert(readValueSlotFromSource(source, 0, slot) ==
         ValueSlotFormResult::Applied);
  assert(slot.decimals == 2);
}

// Barevná škála se seřadí podle hodnoty, ne podle pořadí ve formuláři.
void testColorScaleIsSorted() {
  Fields fields = customSlotFields();
  fields["valueSlot0ColorCount"] = "3";
  fields["valueSlot0ColorValue0"] = "30";
  fields["valueSlot0ColorColor0"] = "#ff0000";
  fields["valueSlot0ColorValue1"] = "10";
  fields["valueSlot0ColorColor1"] = "#00ff00";
  fields["valueSlot0ColorValue2"] = "20";
  fields["valueSlot0ColorColor2"] = "#0000ff";
  ClockValueSlotConfig slot;
  const ConfigurationFormSource source = sourceFor(fields);
  assert(readValueSlotFromSource(source, 0, slot) ==
         ValueSlotFormResult::Applied);
  assert(slot.colorScale.count == 3);
  assert(slot.colorScale.points[0].value == 10.0f);
  assert(slot.colorScale.points[1].value == 20.0f);
  assert(slot.colorScale.points[2].value == 30.0f);
  assert(slot.colorScale.points[0].color == 0x00FF00);
  // Barva slotu vychází z nejnižšího bodu až po seřazení.
  assert(slot.color == 0x00FF00);
}

// Dvě stejné hodnoty by v interpolaci znamenaly dělení nulou.
void testDuplicateColorValuesAreRejected() {
  Fields fields = customSlotFields();
  fields["valueSlot0ColorCount"] = "2";
  fields["valueSlot0ColorValue1"] = "0";
  fields["valueSlot0ColorColor1"] = "#ff0000";
  ClockValueSlotConfig slot;
  const ConfigurationFormSource source = sourceFor(fields);
  assert(readValueSlotFromSource(source, 0, slot) ==
         ValueSlotFormResult::InvalidColorScale);
}

void testInvalidColorScaleIsRejected() {
  ClockValueSlotConfig slot;
  Fields missingCount = customSlotFields();
  missingCount.erase("valueSlot0ColorCount");
  ConfigurationFormSource source = sourceFor(missingCount);
  assert(readValueSlotFromSource(source, 0, slot) ==
         ValueSlotFormResult::InvalidColorScale);

  Fields badColor = customSlotFields();
  badColor["valueSlot0ColorColor0"] = "zelena";
  source = sourceFor(badColor);
  assert(readValueSlotFromSource(source, 0, slot) ==
         ValueSlotFormResult::InvalidColorScale);

  Fields tooMany = customSlotFields();
  tooMany["valueSlot0ColorCount"] = "11";
  source = sourceFor(tooMany);
  assert(readValueSlotFromSource(source, 0, slot) ==
         ValueSlotFormResult::InvalidColorScale);
}

// Prefix musí odpovídat indexu, jinak by se sloty přepisovaly navzájem.
void testSlotIndexSelectsItsOwnFields() {
  Fields fields{
      {"valueSlot7Enabled", "1"},
      {"valueSlot7Mode", "custom"},
      {"valueSlot7Name", "SKLEP"},
      {"valueSlot7Suffix", "%"},
      {"valueSlot7Decimals", "0"},
      {"valueSlot7ColorCount", "1"},
      {"valueSlot7ColorValue0", "0"},
      {"valueSlot7ColorColor0", "#ffffff"},
  };
  ClockValueSlotConfig slot;
  const ConfigurationFormSource source = sourceFor(fields);
  assert(readValueSlotFromSource(source, 0, slot) ==
         ValueSlotFormResult::Missing);
  assert(readValueSlotFromSource(source, 7, slot) ==
         ValueSlotFormResult::Applied);
  assert(strcmp(slot.name, "SKLEP") == 0);
}

}  // namespace

int main() {
  testMissingFieldsKeepStoredSlot();
  testCustomSlotIsRead();
  testPresetSlotOverwritesNameAndSuffix();
  testDisabledSlotKeepsItsSettings();
  testDecimalsAreClamped();
  testColorScaleIsSorted();
  testDuplicateColorValuesAreRejected();
  testInvalidColorScaleIsRejected();
  testSlotIndexSelectsItsOwnFields();
  return 0;
}
