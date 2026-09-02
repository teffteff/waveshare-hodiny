#pragma once

#include <Arduino.h>

#include "ClockConfig.h"

// Čtení konfiguračního formuláře oddělené od WebServeru. Firmware sem pošle
// pole požadavku, hostitelský test v tools/ vlastní tabulku, takže se parsování
// dá ověřit bez zařízení i bez síťového zásobníku.
struct ConfigurationFormSource {
  bool (*has)(void *context, const String &name);
  String (*get)(void *context, const String &name);
  void *context;
};

bool parseFiniteFloat(const String &text, float &value);
bool parseHtmlColor(const String &value, uint32_t &color);

// Předvolby měřených hodnot: název, jednotka a přesnost podle zvoleného typu.
void applyMetricPreset(ClockMetricConfig &metric, const String &preset);

// Barevná škála pod zadaným prefixem: <prefix>Count a k tomu <prefix>Value<i>
// s <prefix>Color<i>. Body se seřadí; duplicitní hodnota je chyba, protože by
// v interpolaci znamenala dělení nulou.
bool readColorScaleFromSource(const ConfigurationFormSource &source,
                              const String &prefix,
                              ClockMetricColorScale &scale);

enum class ValueSlotFormResult : uint8_t {
  // Stránka uložená ze starší verze pole valueSlot* vůbec neposílá. Takový
  // formulář nesmí uložený slot přepsat ani shodit celé uložení.
  Missing,
  Applied,
  InvalidColorScale,
};

ValueSlotFormResult readValueSlotFromSource(const ConfigurationFormSource &source,
                                            size_t index,
                                            ClockValueSlotConfig &slot);
