#pragma once

#include <stddef.h>
#include <stdint.h>

// Noční obloha ze serveru družic (infra/satellites, parametr view=sky),
// oddělená od stahování i kreslení, aby šla testovat na počítači.
//
// Tělesa chodí jako rektascenze a deklinace, ne jako azimut a výška: hodiny si
// polohu na obloze dopočítají každou vteřinu z hvězdného času, takže se stačí
// ptát jednou za čtvrt hodiny a planety se přesto hýbou plynule.
//
//   {"v":1,"time":1790005900,
//    "bodies":[{"id":"moon","n":"Měsíc","k":1,"ra":20.1715,"dec":-23.659,
//               "dist":389012,"ill":0.756,"con":"Kozoroh","rise":...,"set":...},
//              {"id":"venus","n":"Venuše","k":2,"ra":14.1089,"dec":-19.252,
//               "au":0.412,"mag":-4.8,"con":"Panna",...}],
//    "stars":[6752,-1672,-15, ...],   rektascenze v tisícinách hodiny,
//                                     deklinace v setinách stupně,
//                                     velikost v desetinách - po trojicích
//    "starNames":["Sirius", ...],     jména hvězd ve stejném pořadí
//    "starCons":["Velký pes", ...],   a souhvězdí, do kterých patří
//    "lines":[25,26,26,27, ...],      obrazce souhvězdí, dvojice indexů hvězd
//    "cons":[{"n":"Orion","ra":5.6,"dec":-1.09}],   jména obrazců a jejich střed
//    "moonTrack":{"t":1790019000,"s":1800,"p":[20265,-2318, ...]},
//                                     Měsíc po půl hodině od t: rektascenze
//                                     v tisícinách hodiny, deklinace v setinách
//    "dark":[1790013000,1790050000],  astronomická tma, která je nebo přijde
//    "radiant":{"n":"Perseidy","ra":3.2,"dec":58},  jen pár dní kolem maxima
//    "events":[{"t":1790789608,"tm":1,"k":"conj","x":"Měsíc 0,2° od Plejád"}],
//    "kp":2.33,"kpMax":4.0,"kpMaxAt":1790046000}

constexpr size_t SKY_MAX_BODIES = 9;
constexpr size_t SKY_MAX_STARS = 128;
constexpr size_t SKY_MAX_LINES = 80;
constexpr size_t SKY_MAX_EVENTS = 3;
constexpr size_t SKY_NAME_LENGTH = 16;
constexpr size_t SKY_CONSTELLATION_LENGTH = 32;
constexpr size_t SKY_EVENT_TEXT_LENGTH = 72;
constexpr size_t SKY_STAR_NAME_LENGTH = 16;
// Jména obrazců a roje: "Velká medvědice" má v UTF-8 sedmnáct bajtů.
constexpr size_t SKY_LABEL_LENGTH = 24;
constexpr size_t SKY_MAX_FIGURES = 16;
constexpr size_t SKY_MOON_TRACK_POINTS = 32;

enum SkyBodyKind : uint8_t {
  SKY_BODY_SUN = 0,
  SKY_BODY_MOON = 1,
  SKY_BODY_PLANET = 2,
  // Jen pro detail po klepnutí na hvězdu; v datech hvězdy chodí zvlášť.
  SKY_BODY_STAR = 3,
};

struct SkyBody {
  char id[10] = "";
  char name[SKY_NAME_LENGTH] = "";
  uint8_t kind = SKY_BODY_PLANET;
  float raHours = 0.0f;
  float decDeg = 0.0f;
  bool hasMagnitude = false;
  float magnitude = 0.0f;
  // Jen Měsíc: osvětlená část kotouče 0 až 1 a vzdálenost v kilometrech.
  float illumination = 0.0f;
  uint32_t distanceKm = 0;
  // Ostatní: vzdálenost v astronomických jednotkách.
  float distanceAu = 0.0f;
  char constellation[SKY_CONSTELLATION_LENGTH] = "";
  // Nejbližší východ a západ v unixových sekundách, 0 když do dne nenastane.
  int64_t rise = 0;
  int64_t set = 0;
};

struct SkyStar {
  // Tisíciny hodiny rektascenze, setiny stupně deklinace, desetiny velikosti.
  int16_t raMilliHours = 0;
  int16_t decCentiDeg = 0;
  int8_t magnitudeTenths = 0;
  char name[SKY_STAR_NAME_LENGTH] = "";
  char constellation[SKY_CONSTELLATION_LENGTH] = "";
};

// Jméno obrazce souhvězdí a jeho střed na obloze.
struct SkyFigure {
  char name[SKY_LABEL_LENGTH] = "";
  float raHours = 0.0f;
  float decDeg = 0.0f;
};

struct SkyTrackPoint {
  int16_t raMilliHours = 0;
  int16_t decCentiDeg = 0;
};

struct SkyLine {
  uint8_t first = 0;
  uint8_t second = 0;
};

struct SkyEvent {
  int64_t time = 0;
  // Na úkazu záleží hodina (konjunkce, zatmění), ne jen den (roj, opozice).
  bool hasTime = false;
  char text[SKY_EVENT_TEXT_LENGTH] = "";
};

struct SkyFeed {
  uint32_t serverTime = 0;
  bool hasKp = false;
  float kp = 0.0f;
  bool hasKpMax = false;
  float kpMax = 0.0f;
  int64_t kpMaxAt = 0;
  SkyBody bodies[SKY_MAX_BODIES];
  size_t bodyCount = 0;
  SkyStar stars[SKY_MAX_STARS];
  size_t starCount = 0;
  SkyLine lines[SKY_MAX_LINES];
  size_t lineCount = 0;
  SkyEvent events[SKY_MAX_EVENTS];
  size_t eventCount = 0;
  SkyFigure figures[SKY_MAX_FIGURES];
  size_t figureCount = 0;
  // Dráha Měsíce: bod i leží v čase moonTrackStart + i * moonTrackStep.
  int64_t moonTrackStart = 0;
  uint32_t moonTrackStep = 0;
  SkyTrackPoint moonTrack[SKY_MOON_TRACK_POINTS];
  size_t moonTrackCount = 0;
  // Astronomická tma; 0, když v letní noci nenastane.
  int64_t darkFrom = 0;
  int64_t darkTo = 0;
  // Radiant meteorického roje pár dní kolem maxima.
  bool hasRadiant = false;
  char radiantName[SKY_LABEL_LENGTH] = "";
  float radiantRaHours = 0.0f;
  float radiantDecDeg = 0.0f;
};

// Rozebere odpověď. Těleso bez polohy se přeskočí, zbytek platí.
bool skyFeedParse(const char *begin, const char *end, SkyFeed &feed);

// Adresa dotazu na oblohu: adresa serveru družic z nastavení doplněná o
// view=sky, polohu a jazyk. Vrací false, když se do výstupu nevejde.
bool skyFeedBuildUrl(const char *baseUrl, float latitude, float longitude,
                     bool english, char *output, size_t capacity);

// Azimut (od severu přes východ) a výška nad obzorem v čase epoch (unixové
// sekundy). Výška je zdánlivá, s refrakcí: těleso na obzoru je vidět, i když
// geometricky leží půl stupně pod ním.
void skyHorizontal(double raHours, double decDeg, double latitudeDeg,
                   double longitudeDeg, double epoch, float &azimuthDeg,
                   float &altitudeDeg);

// Rektascenze a deklinace bodu ekliptiky s délkou longitudeDeg (šikmost
// ekliptiky J2000; za sto let se změní o setinu stupně).
void skyEclipticPoint(float longitudeDeg, float &raHours, float &decDeg);

// Datum před řádkem úkazu: "DNES 21:40", "ZÍTRA 04:12", "12. 8." nebo
// "12. 8. 21:40", anglicky "TODAY 21:40", "TMRW", "AUG 12". Místní čas.
void skyEventWhen(const SkyEvent &event, int64_t now, bool english,
                  char *output, size_t capacity);
