#pragma once

#include <stdint.h>

// Ikony letadel podle typu draku: vrtulník vypadá jako vrtulník, A380 je
// vidět větší než Cessna, kluzák má dlouhá tenká křídla.
//
// Převzato z MeteoPlaneRadar 0.8.5 (Petr / chiptron.cz, MIT, viz
// THIRD_PARTY_NOTICES.md): seznamy typů, pořadí rozhodování i tvary. Rozdělení
// typů do ikon vychází ze schématu ADS-B Radar
// (https://adsb-radar.com/help/icons.html). Kreslí se primitivy MapCanvas.
//
// Druh se určí jednou při čtení dat (AdsbAircraft::iconKind), první shoda
// vyhrává:
//   1. typ draku ICAO z pole "t" (A20N, B738, EC35) proti seznamům typů,
//   2. pár předpon pro typy, které seznamy nejmenují (B73x, A32x, E1xx...),
//   3. ADS-B kategorie ("A1" lehké ... "A7" vrtulník, "B1" kluzák...), kterou
//      pošle každý transpondér, i když databáze draků letadlo nezná,
//   4. původní šipka.

enum PlaneIconKind : uint8_t {
  PI_GENERIC = 0,   // původní šipka
  PI_LIGHT,         // Cessna, Piper, ultralighty - malé, rovné horní křídlo
  PI_TWINPROP,      // Dash 8, ATR, King Air, L410
  PI_BIZJET,        // Learjet, Citation, Falcon, Gulfstream - motory vzadu
  PI_REGIONAL,      // CRJ, ERJ, E-Jets, Fokker 100, BAe 146
  PI_NARROW,        // A320, B737, B757
  PI_WIDE,          // A330, A350, B767, B777, B787, MD-11
  PI_QUAD,          // A340, A380, B747
  PI_MILTRANS,      // C-130, C-160, A400M
  PI_FREIGHTER,     // An-124, C-5, C-17, Il-76
  PI_FIGHTER,       // stíhačky a proudové cvičné
  PI_HELI,          // vrtulníky
  PI_TILTROTOR,     // V-22, AW609
  PI_GLIDER,        // kluzáky, motorové kluzáky
  PI_BALLOON,       // balony
  PI_DRONE,         // drony
  PI_KIND_N
};

// type = typ draku ICAO ("" když není), cat = ADS-B kategorie ("" když není).
// Na velikosti písmen nezáleží.
uint8_t PlaneIcon_Classify(const char *type, const char *cat);

// Ikona se středem v (x, y), nos ve směru trackDeg (0 = nahoru, po směru
// hodin, už opravený o natočení mapy). Bez směru kroužek s tečkou jako dřív,
// ať je druh jakýkoli; balon a dron se kreslí vždy svisle.
void PlaneIcon_Draw(uint16_t *buffer, int x, int y, float trackDeg,
                    bool hasTrack, uint8_t kind, uint16_t color);

// Krátký název druhu ("helicopter") pro diagnostiku a testy.
const char *PlaneIcon_Name(uint8_t kind);
