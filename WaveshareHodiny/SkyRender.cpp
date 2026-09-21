#include "SkyRender.h"

#include <cmath>
#include <cstring>

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

// Planety, které jde vidět okem; jen ty se počítají do "nad obzorem".
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

// Místo pro jméno tělesa: vpravo, vlevo, pod a nad tečkou, uvnitř kruhu
// displeje a mimo pásy textu.
bool placeLabel(MapLabelPlacer &placer, int x, int y, int radius,
                const char *name, MapLabelBox &box) {
  int characters = 0;
  for (const char *cursor = name; *cursor != '\0'; ++cursor)
    if ((static_cast<uint8_t>(*cursor) & 0xC0) != 0x80) ++characters;
  // clock_czech_14 má průměrně sedm pixelů na znak.
  const int width = characters * 7 + 4;
  constexpr int HEIGHT = 17;
  const int gap = radius + 3;
  const MapLabelBox candidates[] = {
      {x + gap, y - HEIGHT / 2, width, HEIGHT},
      {x - gap - width, y - HEIGHT / 2, width, HEIGHT},
      {x - width / 2, y + gap, width, HEIGHT},
      {x - width / 2, y - gap - HEIGHT, width, HEIGHT},
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

void reserveChromeBands(MapLabelPlacer &placer, bool eventsAtTop) {
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
  for (const MapLabelBox &band : bands) placer.claim(band);
}

}  // namespace

void skyRender(uint16_t *pixels, const SkyFeed *feed, float latitude,
               float longitude, double epoch, uint16_t topBearingDeg,
               bool night, bool english, const char *selectedId,
               SkyRenderResult &result) {
  result = SkyRenderResult{};
  if (pixels == nullptr) return;
  const SkyCanvas canvas(pixels, topBearingDeg, night);
  canvas.clear();
  canvas.drawGrid(0, english);
  if (feed == nullptr) return;

  // Slunce rozhoduje o tom, jak moc jsou hvězdy vidět, a svítí na Měsíc.
  float sunAltitude = -90.0f;
  int sunX = SKY_CANVAS_CENTER_X;
  int sunY = SKY_CANVAS_CENTER_Y + SKY_CANVAS_RADIUS * 2;
  for (size_t index = 0; index < feed->bodyCount; ++index) {
    const SkyBody &body = feed->bodies[index];
    if (body.kind != SKY_BODY_SUN) continue;
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
  for (size_t index = 0; index < feed->starCount; ++index) {
    if (!starUp[index]) continue;
    const float magnitude = feed->stars[index].magnitudeTenths / 10.0f;
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

  MapLabelPlacer placer;
  reserveChromeBands(placer, skyEventsAtTop(topBearingDeg, latitude));
  const bool haveSelection = selectedId != nullptr && selectedId[0] != '\0';
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
    if (nakedEyePlanet(body)) ++result.planetsUp;
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
    if (result.tapCount < SKY_MAX_BODIES) {
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
        placeLabel(placer, x, y, radius, body.name, box)) {
      SkyLabel &label = result.labels[result.labelCount++];
      label.x = static_cast<int16_t>(box.x + 2);
      label.y = static_cast<int16_t>(box.y);
      strncpy(label.name, body.name, sizeof(label.name) - 1);
      // V červeném nočním režimu je všechno červené, i popisky.
      label.color = night ? 0xFF4848 : color.rgb888;
    }
  }
}
