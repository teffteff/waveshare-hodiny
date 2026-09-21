#include "SkyRender.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "MapCanvas.h"
#include "SatelliteFeed.h"
#include "SkyCanvas.h"

namespace {

constexpr float DARK_SUN_ELEVATION = -6.0f;
constexpr uint16_t COLOR_WHITE = 0xFFFF;
// Obrazce souhvězdí: tlumená modrošedá, aby hvězdy zůstaly vidět.
constexpr uint16_t COLOR_CONSTELLATION = 0x2A0A;
constexpr uint16_t COLOR_BRIGHT_STAR = 0xFFFF;
constexpr uint16_t COLOR_FAINT_STAR = 0xB5B6;
// Neosvětlená část Měsíce, aby byl vidět celý kotouč.
constexpr uint16_t COLOR_MOON_DARK = 0x31A6;
// Ekliptika: tečky ještě tlumenější než obrazce, jen vodítko pro oko.
constexpr uint16_t COLOR_ECLIPTIC = 0x8C51;
// Dráha Měsíce v barvě Měsíce, přerušovaně a průsvitně.
constexpr uint16_t COLOR_MOON_TRACK = 0xE73B;
// Radiant roje: světle zelená, jiná než všechno ostatní na obloze.
constexpr uint16_t COLOR_RADIANT = 0x8FB4;
// Jména obrazců: tlumená modrošedá, o stupeň světlejší než jejich čáry.
constexpr uint16_t COLOR_FIGURE_LABEL = 0x7435;
// Obrazec se pojmenuje, jen když jeho střed stojí aspoň tak vysoko: u obzoru
// by jména lezla do světových stran a popisků těles.
constexpr float FIGURE_LABEL_MIN_ALTITUDE = 12.0f;
// Dráha Měsíce se kreslí na tolik hodin dopředu, po pětiminutových krocích;
// každý druhý krok se vynechá, takže je čára přerušovaná.
constexpr int MOON_TRACK_HOURS = 12;
constexpr int MOON_TRACK_SUBSTEP_SECONDS = 300;
constexpr int MOON_TRACK_MAX_TICKS = 12;

struct SkyColor {
  uint16_t rgb565;
  uint32_t rgb888;
};

// Barvy těles podle toho, jak je vidí oko: Mars do červena, Jupiter krémový,
// Saturn do žluta. Uran a Neptun jen dalekohledem, tak tlumeně.
SkyColor bodyColor(const SkyBody &body) {
  static const struct {
    const char *id;
    SkyColor color;
  } COLORS[] = {
      {"sun", {0xFEE7, 0xFFDC3C}},     {"moon", {0xE73B, 0xE6E6DC}},
      {"mercury", {0xC618, 0xC0C0C0}}, {"venus", {0xFFDB, 0xFFFADC}},
      {"mars", {0xFB68, 0xFF6E46}},    {"jupiter", {0xF694, 0xF0D2A0}},
      {"saturn", {0xE64D, 0xE6C86E}},  {"uranus", {0x973C, 0x96E6E6}},
      {"neptune", {0x6C7F, 0x6E8CFF}},
  };
  for (const auto &entry : COLORS)
    if (strcmp(entry.id, body.id) == 0) return entry.color;
  return {0xFFFF, 0xFFFFFF};
}

// Poloměr tečky podle jasnosti: Venuše pět pixelů, Saturn tři, Neptun jeden.
int bodyRadius(const SkyBody &body) {
  if (body.kind != SKY_BODY_PLANET) return 8;
  if (!body.hasMagnitude) return 3;
  if (body.magnitude < -3.0f) return 5;
  if (body.magnitude < -1.0f) return 4;
  if (body.magnitude < 1.0f) return 3;
  if (body.magnitude < 5.0f) return 2;
  return 1;
}

// Planety, které jde vidět okem: jen ty mají jméno a svatozář.
bool nakedEyePlanet(const SkyBody &body) {
  return body.kind == SKY_BODY_PLANET && body.hasMagnitude &&
         body.magnitude < 5.0f;
}

// Pořadí pro kreslení a popisky: Slunce a Měsíc, pak planety od nejjasnější,
// aby jasná planeta dostala popisek dřív než slabá.
float drawPriority(const SkyBody &body) {
  if (body.kind != SKY_BODY_PLANET) return -100.0f;
  return body.hasMagnitude ? body.magnitude : 10.0f;
}

// Poloha na obloze jako bod na displeji; false pod obzorem.
bool toScreen(const SkyCanvas &canvas, double ra, double dec, float latitude,
              float longitude, double epoch, int &x, int &y, float &azimuth,
              float &altitude) {
  skyHorizontal(ra, dec, latitude, longitude, epoch, azimuth, altitude);
  float unitX = 0.0f;
  float unitY = 0.0f;
  satelliteSkyProject(azimuth, altitude < 0.0f ? 0.0f : altitude, unitX, unitY);
  canvas.project(unitX, unitY, x, y);
  return altitude >= 0.0f;
}

// Měsíc s fází: osvětlená strana míří ke Slunci, terminátor je elipsa.
// V souřadnicích kotouče s osou u ke Slunci je bod osvětlený, když
// u >= -k * sqrt(1 - v^2), kde k = 2 * osvětlená část - 1.
void drawMoon(const SkyCanvas &canvas, int centerX, int centerY, int radius,
              float towardSunX, float towardSunY, float illumination,
              uint16_t lit) {
  const float length =
      std::sqrt(towardSunX * towardSunX + towardSunY * towardSunY);
  const float ux = length > 0.001f ? towardSunX / length : 1.0f;
  const float uy = length > 0.001f ? towardSunY / length : 0.0f;
  const float k = 2.0f * illumination - 1.0f;
  const uint16_t dark = canvas.color(COLOR_MOON_DARK);
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      if (dx * dx + dy * dy > radius * radius + radius) continue;
      const float u = (dx * ux + dy * uy) / radius;
      const float v = (-dx * uy + dy * ux) / radius;
      const float half = 1.0f - v * v;
      const bool on = u >= -k * std::sqrt(half > 0.0f ? half : 0.0f);
      setMapPixel(canvas.pixels(), centerX + dx, centerY + dy, on ? lit : dark,
                  on ? 100 : 60);
    }
  }
}

// Šířka popisku LVGL: clock_czech_14 má průměrně sedm pixelů na znak.
constexpr int LABEL_HEIGHT = 17;
int labelWidth(const char *name) {
  int characters = 0;
  for (const char *cursor = name; *cursor != '\0'; ++cursor)
    if ((static_cast<uint8_t>(*cursor) & 0xC0) != 0x80) ++characters;
  return characters * 7 + 4;
}

// Místo pro popisek tečky: vpravo, vlevo, pod a nad ní, uvnitř kruhu
// displeje a mimo pásy textu.
bool placeLabel(MapLabelPlacer &placer, int x, int y, int radius, int width,
                int height, MapLabelBox &box) {
  const int gap = radius + 3;
  const MapLabelBox candidates[] = {
      {x + gap, y - height / 2, width, height},
      {x - gap - width, y - height / 2, width, height},
      {x - width / 2, y + gap, width, height},
      {x - width / 2, y - gap - height, width, height},
  };
  for (const MapLabelBox &candidate : candidates) {
    bool inside = true;
    const int corners[4][2] = {
        {candidate.x, candidate.y},
        {candidate.x + candidate.width, candidate.y},
        {candidate.x, candidate.y + candidate.height},
        {candidate.x + candidate.width, candidate.y + candidate.height}};
    for (const auto &corner : corners) {
      const int dx = corner[0] - SKY_CANVAS_CENTER_X;
      const int dy = corner[1] - SKY_CANVAS_CENTER_Y;
      if (dx * dx + dy * dy > 232 * 232) inside = false;
    }
    if (inside && placer.claim(candidate)) {
      box = candidate;
      return true;
    }
  }
  return false;
}

// Jméno se středem v bodě (obrazec souhvězdí): na místě, nebo kousek pod
// ním či nad ním, uvnitř kruhu a mimo zabraná místa.
bool placeCenteredLabel(MapLabelPlacer &placer, int x, int y, const char *name,
                        MapLabelBox &box) {
  const int width = labelWidth(name);
  const int shifts[] = {0, 14, -14};
  for (int shift : shifts) {
    const MapLabelBox candidate = {x - width / 2, y - LABEL_HEIGHT / 2 + shift,
                                   width, LABEL_HEIGHT};
    bool inside = true;
    for (int corner = 0; corner < 4; ++corner) {
      const int dx = candidate.x + (corner & 1 ? candidate.width : 0) -
                     SKY_CANVAS_CENTER_X;
      const int dy = candidate.y + (corner & 2 ? candidate.height : 0) -
                     SKY_CANVAS_CENTER_Y;
      if (dx * dx + dy * dy > 225 * 225) inside = false;
    }
    if (inside && placer.claim(candidate)) {
      box = candidate;
      return true;
    }
  }
  return false;
}

// Ekliptika po stupni a půl: tečky jen nad obzorem.
void drawEcliptic(const SkyCanvas &canvas, float latitude, float longitude,
                  double epoch, uint8_t opacity) {
  const uint16_t color = canvas.color(COLOR_ECLIPTIC);
  for (int step = 0; step < 240; ++step) {
    float ra = 0.0f;
    float dec = 0.0f;
    skyEclipticPoint(step * 1.5f, ra, dec);
    int x = 0;
    int y = 0;
    float azimuth = 0.0f;
    float altitude = 0.0f;
    if (toScreen(canvas, ra, dec, latitude, longitude, epoch, x, y, azimuth,
                 altitude))
      setMapPixel(canvas.pixels(), x, y, color, opacity);
  }
}

struct TrackTick {
  int x;
  int y;
  int hour;
};

// Poloha Měsíce v čase moment lineárně mezi body dráhy; false mimo dráhu.
bool moonTrackAt(const SkyFeed &feed, double moment, double &ra, double &dec) {
  if (feed.moonTrackCount < 2 || feed.moonTrackStep == 0) return false;
  const double offset =
      (moment - static_cast<double>(feed.moonTrackStart)) / feed.moonTrackStep;
  if (offset < 0.0 || offset > feed.moonTrackCount - 1) return false;
  size_t index = static_cast<size_t>(offset);
  if (index >= feed.moonTrackCount - 1) index = feed.moonTrackCount - 2;
  const double fraction = offset - index;
  const SkyTrackPoint &from = feed.moonTrack[index];
  const SkyTrackPoint &to = feed.moonTrack[index + 1];
  // Přes nulu rektascenze se jde kratší cestou (23,9 h -> 0,1 h).
  double raDelta = (to.raMilliHours - from.raMilliHours) / 1000.0;
  if (raDelta > 12.0) raDelta -= 24.0;
  if (raDelta < -12.0) raDelta += 24.0;
  ra = std::fmod(from.raMilliHours / 1000.0 + raDelta * fraction + 24.0, 24.0);
  dec = (from.decCentiDeg + (to.decCentiDeg - from.decCentiDeg) * fraction) /
        100.0;
  return true;
}

// Kudy Měsíc půjde do času end: přerušovaná čára nad obzorem a na každé
// celé hodině tečka. Hodiny, kde se tečka dá popsat, vrací v ticks.
size_t drawMoonTrack(const SkyCanvas &canvas, const SkyFeed &feed,
                     float latitude, float longitude, double epoch, double end,
                     TrackTick *ticks) {
  size_t tickCount = 0;
  const uint16_t color = canvas.color(COLOR_MOON_TRACK);
  const int steps = MOON_TRACK_HOURS * 3600 / MOON_TRACK_SUBSTEP_SECONDS;
  int previousX = 0;
  int previousY = 0;
  bool previousUp = false;
  // Kroky jdou od celých pěti minut, aby hodinové tečky padly přesně.
  const double first =
      std::floor(epoch / MOON_TRACK_SUBSTEP_SECONDS) * MOON_TRACK_SUBSTEP_SECONDS;
  for (int step = 0; step <= steps; ++step) {
    const double moment =
        step == 0 ? epoch : first + step * MOON_TRACK_SUBSTEP_SECONDS;
    if (moment > end) break;
    double ra = 0.0;
    double dec = 0.0;
    if (!moonTrackAt(feed, moment, ra, dec)) {
      previousUp = false;
      continue;
    }
    // Hvězdný čas se bere v čase bodu dráhy, ne teď: Měsíc se posune po
    // obloze hlavně tím, jak se Země otočí.
    int x = 0;
    int y = 0;
    float azimuth = 0.0f;
    float altitude = 0.0f;
    const bool up = toScreen(canvas, ra, dec, latitude, longitude, moment, x, y,
                             azimuth, altitude);
    if (up && previousUp && step % 2 == 1)
      drawMapLine(canvas.pixels(), previousX, previousY, x, y, color, 55);
    const long long seconds = static_cast<long long>(moment);
    if (up && step > 0 && seconds % 3600 == 0) {
      fillMapCircle(canvas.pixels(), x, y, 1, color, 90);
      if (tickCount < MOON_TRACK_MAX_TICKS) {
        const time_t when = static_cast<time_t>(seconds);
        struct tm local;
        localtime_r(&when, &local);
        ticks[tickCount++] = {x, y, local.tm_hour};
      }
    }
    previousX = x;
    previousY = y;
    previousUp = up;
  }
  return tickCount;
}

// Radiant roje: čtyři paprsky ze společného bodu, uprostřed prázdno.
void drawRadiant(const SkyCanvas &canvas, int x, int y) {
  const uint16_t color = canvas.color(COLOR_RADIANT);
  static const int RAYS[8][2] = {{1, 0},  {-1, 0}, {0, 1},  {0, -1},
                                 {1, 1},  {-1, 1}, {1, -1}, {-1, -1}};
  for (int ray = 0; ray < 8; ++ray) {
    const int inner = ray < 4 ? 3 : 2;
    const int outer = ray < 4 ? 8 : 6;
    drawMapLine(canvas.pixels(), x + RAYS[ray][0] * inner,
                y + RAYS[ray][1] * inner, x + RAYS[ray][0] * outer,
                y + RAYS[ray][1] * outer, color, 100);
  }
}

void reserveChromeBands(MapLabelPlacer &placer, bool showEvents,
                        bool eventsAtTop) {
  const int eventsTop =
      SKY_CANVAS_CENTER_Y + skyEventRowOffsetY(eventsAtTop, 0) - 12;
  const int eventsBottom = SKY_CANVAS_CENTER_Y +
                           skyEventRowOffsetY(eventsAtTop, SKY_MAX_EVENTS - 1) +
                           14;
  const MapLabelBox bands[] = {
      {0, 18, SKY_CANVAS_SIZE, 22},  // tečky obrazovek
      {0, 40, SKY_CANVAS_SIZE, 28},  // čas
      {0, 69, SKY_CANVAS_SIZE, 26},  // index Kp
      {0, eventsTop, SKY_CANVAS_SIZE, eventsBottom - eventsTop},  // úkazy
  };
  // Pásy se navzájem překrývají (Kp a úkazy), takže claim by druhý odmítl;
  // zapisují se napřímo.
  const size_t count = showEvents ? 4 : 3;
  for (size_t index = 0; index < count && placer.count < MAP_LABEL_CAPACITY;
       ++index)
    placer.occupied[placer.count++] = bands[index];
}

}  // namespace

void skyRender(uint16_t *pixels, const SkyFeed *feed, float latitude,
               float longitude, double epoch, uint16_t topBearingDeg,
               bool night, bool english, bool showEvents,
               const char *selectedId, SkyRenderResult &result) {
  result = SkyRenderResult{};
  if (pixels == nullptr) return;
  const SkyCanvas canvas(pixels, topBearingDeg, night);
  canvas.clear();
  canvas.drawGrid(0, english);
  if (feed == nullptr) return;

  // Slunce rozhoduje o tom, jak moc jsou hvězdy vidět, a svítí na Měsíc.
  float sunAltitude = -90.0f;
  int64_t sunRise = 0;
  int sunX = SKY_CANVAS_CENTER_X;
  int sunY = SKY_CANVAS_CENTER_Y + SKY_CANVAS_RADIUS * 2;
  for (size_t index = 0; index < feed->bodyCount; ++index) {
    const SkyBody &body = feed->bodies[index];
    if (body.kind != SKY_BODY_SUN) continue;
    sunRise = body.rise;
    float azimuth = 0.0f;
    toScreen(canvas, body.raHours, body.decDeg, latitude, longitude, epoch,
             sunX, sunY, azimuth, sunAltitude);
    // Pod obzorem se Slunce promítne na okraj; pro směr osvětlení Měsíce
    // stačí azimut, tak se bod posune za obzor.
    if (sunAltitude < 0.0f) {
      float unitX = 0.0f;
      float unitY = 0.0f;
      satelliteSkyProject(azimuth, 0.0f, unitX, unitY);
      canvas.project(unitX * 2.0f, unitY * 2.0f, sunX, sunY);
    }
  }
  result.dark = sunAltitude <= DARK_SUN_ELEVATION;
  result.sunUp = sunAltitude > 0.0f;
  // Za dne jsou hvězdy jen orientace, ne to, co je vidět.
  const uint8_t starOpacity = result.sunUp ? 30 : result.dark ? 100 : 60;
  const bool haveSelection = selectedId != nullptr && selectedId[0] != '\0';

  drawEcliptic(canvas, latitude, longitude, epoch, result.sunUp ? 35 : 70);

  // Obrazce souhvězdí pod hvězdami.
  int starX[SKY_MAX_STARS];
  int starY[SKY_MAX_STARS];
  bool starUp[SKY_MAX_STARS];
  for (size_t index = 0; index < feed->starCount; ++index) {
    const SkyStar &star = feed->stars[index];
    float azimuth = 0.0f;
    float altitude = 0.0f;
    starUp[index] = toScreen(canvas, star.raMilliHours / 1000.0,
                             star.decCentiDeg / 100.0, latitude, longitude,
                             epoch, starX[index], starY[index], azimuth,
                             altitude);
  }
  const uint16_t lineColor = canvas.color(COLOR_CONSTELLATION);
  for (size_t index = 0; index < feed->lineCount; ++index) {
    const SkyLine &line = feed->lines[index];
    if (!starUp[line.first] || !starUp[line.second]) continue;
    drawMapLine(pixels, starX[line.first], starY[line.first],
                starX[line.second], starY[line.second], lineColor,
                result.sunUp ? 40 : 90);
  }
  // Vybraná hvězda má id "*" a pořadí.
  long selectedStar = -1;
  if (haveSelection && selectedId[0] == '*') {
    char *end = nullptr;
    selectedStar = strtol(selectedId + 1, &end, 10);
    if (end == selectedId + 1 || *end != '\0') selectedStar = -1;
  }
  for (size_t index = 0; index < feed->starCount; ++index) {
    const SkyStar &star = feed->stars[index];
    if (static_cast<long>(index) == selectedStar && star.name[0] != '\0') {
      SkyDetail &detail = result.detail;
      detail.open = true;
      strncpy(detail.name, star.name, sizeof(detail.name) - 1);
      detail.kind = SKY_BODY_STAR;
      skyHorizontal(star.raMilliHours / 1000.0, star.decCentiDeg / 100.0,
                    latitude, longitude, epoch, detail.azimuthDeg,
                    detail.altitudeDeg);
      detail.hasMagnitude = true;
      detail.magnitude = star.magnitudeTenths / 10.0f;
      strncpy(detail.constellation, star.constellation,
              sizeof(detail.constellation) - 1);
      if (starUp[index])
        drawMapCircle(pixels, starX[index], starY[index], 7,
                      canvas.color(COLOR_WHITE), 100);
    }
    if (!starUp[index]) continue;
    // Bez jména by detail neměl co ukázat.
    if (star.name[0] != '\0' && result.tapCount < SKY_MAX_TAPS) {
      SkyTapPoint &tap = result.taps[result.tapCount++];
      tap.x = static_cast<int16_t>(starX[index]);
      tap.y = static_cast<int16_t>(starY[index]);
      snprintf(tap.id, sizeof(tap.id), "*%u", static_cast<unsigned>(index));
    }
    const float magnitude = star.magnitudeTenths / 10.0f;
    const uint16_t color =
        canvas.color(magnitude < 1.5f ? COLOR_BRIGHT_STAR : COLOR_FAINT_STAR);
    if (magnitude < 0.5f) {
      fillMapCircle(pixels, starX[index], starY[index], 2, color, starOpacity);
    } else if (magnitude < 1.5f) {
      fillMapCircle(pixels, starX[index], starY[index], 1, color, starOpacity);
    } else {
      const uint8_t faint = static_cast<uint8_t>(
          starOpacity * (magnitude < 2.5f ? 90 : 60) / 100);
      setMapPixel(pixels, starX[index], starY[index], color, faint);
    }
  }

  size_t order[SKY_MAX_BODIES];
  const size_t orderCount = feed->bodyCount;
  for (size_t index = 0; index < orderCount; ++index) order[index] = index;
  for (size_t left = 1; left < orderCount; ++left) {
    for (size_t right = left; right > 0; --right) {
      if (drawPriority(feed->bodies[order[right - 1]]) <=
          drawPriority(feed->bodies[order[right]]))
        break;
      const size_t swap = order[right - 1];
      order[right - 1] = order[right];
      order[right] = swap;
    }
  }

  TrackTick ticks[MOON_TRACK_MAX_TICKS];
  // V noci jde dráha jen do východu Slunce: kudy Měsíc půjde za dne, nikoho
  // při pohledu na noční oblohu nezajímá.
  double trackEnd = epoch + MOON_TRACK_HOURS * 3600.0;
  if (!result.sunUp && sunRise > epoch && sunRise < trackEnd)
    trackEnd = static_cast<double>(sunRise);
  const size_t tickCount = drawMoonTrack(canvas, *feed, latitude, longitude,
                                         epoch, trackEnd, ticks);

  int radiantX = 0;
  int radiantY = 0;
  bool radiantUp = false;
  if (feed->hasRadiant) {
    float azimuth = 0.0f;
    float altitude = 0.0f;
    radiantUp = toScreen(canvas, feed->radiantRaHours, feed->radiantDecDeg,
                         latitude, longitude, epoch, radiantX, radiantY,
                         azimuth, altitude);
    if (radiantUp) drawRadiant(canvas, radiantX, radiantY);
  }

  MapLabelPlacer placer;
  reserveChromeBands(placer, showEvents,
                     skyEventsAtTop(topBearingDeg, latitude));
  for (size_t position = 0; position < orderCount; ++position) {
    const SkyBody &body = feed->bodies[order[position]];
    int x = 0;
    int y = 0;
    float azimuth = 0.0f;
    float altitude = 0.0f;
    const bool up = toScreen(canvas, body.raHours, body.decDeg, latitude,
                             longitude, epoch, x, y, azimuth, altitude);
    const bool selected = haveSelection && strcmp(selectedId, body.id) == 0;
    if (selected) {
      SkyDetail &detail = result.detail;
      detail.open = true;
      strncpy(detail.name, body.name, sizeof(detail.name) - 1);
      detail.kind = body.kind;
      detail.azimuthDeg = azimuth;
      detail.altitudeDeg = altitude;
      detail.hasMagnitude = body.hasMagnitude;
      detail.magnitude = body.magnitude;
      detail.illumination = body.illumination;
      detail.distanceKm = body.distanceKm;
      detail.distanceAu = body.distanceAu;
      strncpy(detail.constellation, body.constellation,
              sizeof(detail.constellation) - 1);
      detail.rise = body.rise;
      detail.set = body.set;
    }
    if (!up) continue;
    const SkyColor color = bodyColor(body);
    const uint16_t shade = canvas.color(color.rgb565);
    const int radius = bodyRadius(body);
    if (body.kind == SKY_BODY_SUN) {
      fillMapCircle(pixels, x, y, radius, shade, 100);
    } else if (body.kind == SKY_BODY_MOON) {
      drawMoon(canvas, x, y, radius, static_cast<float>(sunX - x),
               static_cast<float>(sunY - y), body.illumination, shade);
    } else {
      // Planeta, kterou jde vidět okem, dostane za tmy svatozář jako
      // viditelná družice.
      if (result.dark && nakedEyePlanet(body))
        drawMapCircle(pixels, x, y, radius + 3, shade, 40);
      fillMapCircle(pixels, x, y, radius, shade, result.sunUp ? 60 : 100);
    }
    if (selected)
      drawMapCircle(pixels, x, y, radius + 7, canvas.color(COLOR_WHITE), 100);
    if (result.tapCount < SKY_MAX_TAPS) {
      SkyTapPoint &tap = result.taps[result.tapCount++];
      tap.x = static_cast<int16_t>(x);
      tap.y = static_cast<int16_t>(y);
      strncpy(tap.id, body.id, sizeof(tap.id) - 1);
    }
    // Uran a Neptun jen dalekohledem: tečka a klepnutí ano, jméno by jen
    // tlačilo popisky jasných planet stranou.
    const bool labelled = body.kind != SKY_BODY_PLANET || nakedEyePlanet(body);
    MapLabelBox box;
    if (labelled && result.labelCount < SKY_MAX_BODIES &&
        placeLabel(placer, x, y, radius, labelWidth(body.name), LABEL_HEIGHT,
                   box)) {
      SkyLabel &label = result.labels[result.labelCount++];
      label.x = static_cast<int16_t>(box.x + 2);
      label.y = static_cast<int16_t>(box.y);
      strncpy(label.name, body.name, sizeof(label.name) - 1);
      // V červeném nočním režimu je všechno červené, i popisky.
      label.color = night ? 0xFF4848 : color.rgb888;
    }
  }

  // Popisky až po tělesech, aby jim nezabraly místo: nejdřív radiant, pak
  // hodiny na dráze Měsíce a nakonec jména obrazců.
  MapLabelBox box;
  if (radiantUp &&
      placeLabel(placer, radiantX, radiantY, 6, labelWidth(feed->radiantName),
                 LABEL_HEIGHT, box)) {
    SkyPaintedLabel &label = result.painted[result.paintedCount++];
    label.x = static_cast<int16_t>(box.x + 2);
    label.y = static_cast<int16_t>(box.y);
    strncpy(label.name, feed->radiantName, sizeof(label.name) - 1);
    label.color = canvas.color(COLOR_RADIANT);
  }
  const uint16_t tickColor = canvas.color(COLOR_MOON_TRACK);
  for (size_t index = 0; index < tickCount; ++index) {
    char hour[4];
    snprintf(hour, sizeof(hour), "%d", ticks[index].hour);
    MapLabelBox tickBox;
    if (placeLabel(placer, ticks[index].x, ticks[index].y, 0,
                   mapTextWidth(hour) + 2, 9, tickBox))
      drawMapText(pixels, tickBox.x + 1, tickBox.y + 1, hour, tickColor, 70);
  }
  // Za dne nejsou hvězdy vidět, tak ani jména obrazců.
  if (result.sunUp) return;
  for (size_t index = 0; index < feed->figureCount; ++index) {
    const SkyFigure &figure = feed->figures[index];
    int x = 0;
    int y = 0;
    float azimuth = 0.0f;
    float altitude = 0.0f;
    toScreen(canvas, figure.raHours, figure.decDeg, latitude, longitude, epoch,
             x, y, azimuth, altitude);
    if (altitude < FIGURE_LABEL_MIN_ALTITUDE ||
        result.paintedCount >= SKY_MAX_PAINTED_LABELS ||
        !placeCenteredLabel(placer, x, y, figure.name, box))
      continue;
    SkyPaintedLabel &label = result.painted[result.paintedCount++];
    label.x = static_cast<int16_t>(box.x + 2);
    label.y = static_cast<int16_t>(box.y);
    strncpy(label.name, figure.name, sizeof(label.name) - 1);
    label.color = canvas.color(COLOR_FIGURE_LABEL);
  }
}

int skyPickTap(const SkyTapPoint *taps, size_t count, int x, int y) {
  int best = -1;
  bool bestStar = true;
  long bestDistance = 0;
  for (size_t index = 0; index < count; ++index) {
    const bool star = taps[index].id[0] == '*';
    const long deltaX = taps[index].x - x;
    const long deltaY = taps[index].y - y;
    const long distance = deltaX * deltaX + deltaY * deltaY;
    if (distance >= (star ? 18L * 18L : 30L * 30L)) continue;
    // Těleso vyhrává nad hvězdou vždy, jinak rozhoduje vzdálenost.
    if (best >= 0 && (star && !bestStar)) continue;
    if (best >= 0 && star == bestStar && distance >= bestDistance) continue;
    best = static_cast<int>(index);
    bestStar = star;
    bestDistance = distance;
  }
  return best;
}
