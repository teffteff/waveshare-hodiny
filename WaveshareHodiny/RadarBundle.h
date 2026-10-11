#pragma once

#include <stddef.h>
#include <stdint.h>

// Snímky meteoradaru vykreslené serverem (/radar.bin vedle /rain.json, viz
// infra/rain v majnr). Server dělá přesně to, co by jinak dělaly hodiny:
// stáhne kompozici ČHMÚ, vyřízne pohled a převzorkuje ho na stupně odrazivosti
// (stupeň krát 16, 255 = bez dat), jen jednou pro všechny hodiny. Každý snímek
// přijde zabalený zlibem a hodiny ho tak i drží; rozbalí se jen ten, který se
// právě kreslí.
//
// Odpověď (little endian):
//   "WHR1", u16 šířka, u16 výška, u8 počet, u8 verze, u16 0, u32 nejnovější slot
//   počet x (u32 slot UTC, u32 délka, délka bajtů zlib), od nejstaršího

constexpr size_t RADAR_BUNDLE_MAX_FRAMES = 15;
constexpr uint8_t RADAR_BUNDLE_VERSION = 1;

struct RadarBundleFrame {
  uint32_t slot = 0;
  uint32_t offset = 0;
  uint32_t size = 0;
};

struct RadarBundle {
  uint16_t width = 0;
  uint16_t height = 0;
  uint8_t count = 0;
  uint32_t newestSlot = 0;
  RadarBundleFrame frames[RADAR_BUNDLE_MAX_FRAMES];
};

// Ověří hlavičku i meze všech snímků. Rozměr musí sedět na ten, o který se
// hodiny ptaly, a sloty musí jít vzestupně.
bool radarBundleParse(const uint8_t *data, size_t size, uint16_t width,
                      uint16_t height, RadarBundle &bundle);

// Rozbalí jeden snímek přesně do output (width x height bajtů). False, když
// data nejsou celý zlib proud právě té délky.
bool radarBundleInflate(const uint8_t *data, size_t size, uint8_t *output,
                        size_t outputSize);

// Adresa snímků odvozená z adresy srážkové předpovědi: stejný server, stejné
// heslo, jen /radar.bin místo /rain.json. Prázdná, když adresa na /rain.json
// nekončí (jiný server snímky neumí).
bool radarBundleUrlFromRainUrl(const char *rainUrl, char *output,
                               size_t capacity);
