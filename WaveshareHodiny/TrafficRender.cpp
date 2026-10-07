#include "TrafficRender.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "MapLabelFont.h"

namespace {

constexpr uint16_t COLOR_BLACK = 0x0000;
constexpr uint16_t COLOR_WHITE = 0xFFFF;

// Tloušťky čar (poloměr v pixelech). Cesty z domova jsou hlavní, D1 má dva
// pruhy vedle sebe, objížďka přes Ondřejov je tenká.
constexpr float DRIVE_RADIUS = 2.2f;
constexpr float D1_RADIUS = 1.8f;
// Odsazení středu každého směru D1 doprava od společné osy.
constexpr float D1_OFFSET = 2.6f;
constexpr float VIA_RADIUS = 1.1f;
// Uzavírka se kreslí čárkovaně: tolik pixelů čárka, tolik mezera.
constexpr float DASH_ON = 6.0f;
constexpr float DASH_PERIOD = 10.0f;

constexpr int EXIT_RADIUS = 7;
constexpr int BUS_RADIUS = 9;
constexpr int PLACE_RADIUS = 4;

bool nightPalette = false;

uint16_t rgb565(uint32_t rgb) {
  return static_cast<uint16_t>(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) |
                               ((rgb >> 3) & 0x001F));
}

// Noční režim: odstín červené podle jasu, stejně jako radar letadel.
uint16_t palette(uint16_t color) {
  if (!nightPalette) return color;
  const uint16_t red = ((color >> 11) & 0x1f) << 3;
  const uint16_t green = ((color >> 5) & 0x3f) << 2;
  const uint16_t blue = (color & 0x1f) << 3;
  const uint16_t luma = (77 * red + 150 * green + 29 * blue) >> 8;
  return static_cast<uint16_t>((luma >> 3) << 11);
}

bool insideLabelCircle(const MapLabelBox &box) {
  const int corners[4][2] = {{box.x, box.y},
                             {box.x + box.width, box.y},
                             {box.x, box.y + box.height},
                             {box.x + box.width, box.y + box.height}};
  for (const auto &corner : corners) {
    const int dx = corner[0] - TRAFFIC_MAP_CENTER;
    const int dy = corner[1] - TRAFFIC_MAP_CENTER;
    if (dx * dx + dy * dy > TRAFFIC_MAP_LABEL_RADIUS * TRAFFIC_MAP_LABEL_RADIUS)
      return false;
  }
  return true;
}

// Úsečka jako kapsle o poloměru `radius` s vyhlazeným okrajem. `dashStart` je
// délka čáry před začátkem úsečky; záporná hodnota znamená plnou čáru.
void drawCapsule(uint16_t *pixels, float x0, float y0, float x1, float y1,
                 float radius, uint16_t color, float dashStart) {
  const float dx = x1 - x0;
  const float dy = y1 - y0;
  const float lengthSquared = dx * dx + dy * dy;
  const float length = sqrtf(lengthSquared);
  const int left = static_cast<int>(floorf(fminf(x0, x1) - radius - 1.0f));
  const int right = static_cast<int>(ceilf(fmaxf(x0, x1) + radius + 1.0f));
  const int top = static_cast<int>(floorf(fminf(y0, y1) - radius - 1.0f));
  const int bottom = static_cast<int>(ceilf(fmaxf(y0, y1) + radius + 1.0f));
  for (int y = top < 0 ? 0 : top; y <= bottom && y < MAP_CANVAS_HEIGHT; ++y) {
    for (int x = left < 0 ? 0 : left; x <= right && x < MAP_CANVAS_WIDTH; ++x) {
      const float px = static_cast<float>(x) - x0;
      const float py = static_cast<float>(y) - y0;
      float t = lengthSquared > 0.0f ? (px * dx + py * dy) / lengthSquared : 0.0f;
      if (t < 0.0f) t = 0.0f;
      if (t > 1.0f) t = 1.0f;
      const float ex = px - t * dx;
      const float ey = py - t * dy;
      const float distance = sqrtf(ex * ex + ey * ey);
      const float coverage = radius + 0.5f - distance;
      if (coverage <= 0.0f) continue;
      if (dashStart >= 0.0f) {
        const float along = dashStart + t * length;
        if (fmodf(along, DASH_PERIOD) >= DASH_ON) continue;
      }
      const uint8_t opacity =
          coverage >= 1.0f ? 100 : static_cast<uint8_t>(coverage * 100.0f);
      setMapPixel(pixels, x, y, color, opacity);
    }
  }
}

// Body čáry, případně odsazené doprava po směru jízdy (y roste dolů, takže
// pravá normála směru (dx, dy) je (-dy, dx)).
void linePoints(const TrafficLine &line, float offset, float *xs, float *ys) {
  for (uint8_t index = 0; index < line.pointCount; ++index) {
    xs[index] = line.x[index];
    ys[index] = line.y[index];
  }
  if (offset == 0.0f) return;
  for (uint8_t index = 0; index < line.pointCount; ++index) {
    float nx = 0.0f;
    float ny = 0.0f;
    for (int side = -1; side <= 0; ++side) {
      const int a = index + side;
      const int b = a + 1;
      if (a < 0 || b >= line.pointCount) continue;
      const float dx = static_cast<float>(line.x[b] - line.x[a]);
      const float dy = static_cast<float>(line.y[b] - line.y[a]);
      const float length = sqrtf(dx * dx + dy * dy);
      if (length <= 0.0f) continue;
      nx += -dy / length;
      ny += dx / length;
    }
    const float length = sqrtf(nx * nx + ny * ny);
    if (length <= 0.0f) continue;
    xs[index] += offset * nx / length;
    ys[index] += offset * ny / length;
  }
}

// Úsek čáry od bodu `from` do `to` v barvě úrovně. Uzavírka se nejdřív
// vymaže na černo a pak se kreslí čárkovaně.
void drawStretch(uint16_t *pixels, const float *xs, const float *ys,
                 uint8_t from, uint8_t to, float radius, uint8_t level) {
  const uint16_t color = palette(trafficLevelColor(level));
  if (level == TRAFFIC_LEVEL_CLOSED) {
    for (uint8_t index = from; index < to; ++index)
      drawCapsule(pixels, xs[index], ys[index], xs[index + 1], ys[index + 1],
                  radius + 0.6f, palette(COLOR_BLACK), -1.0f);
  }
  float along = 0.0f;
  for (uint8_t index = from; index < to; ++index) {
    const float dx = xs[index + 1] - xs[index];
    const float dy = ys[index + 1] - ys[index];
    drawCapsule(pixels, xs[index], ys[index], xs[index + 1], ys[index + 1],
                radius, color, level == TRAFFIC_LEVEL_CLOSED ? along : -1.0f);
    along += sqrtf(dx * dx + dy * dy);
  }
}

void drawLine(uint16_t *pixels, const TrafficLine &line, float radius,
              float offset) {
  float xs[TRAFFIC_MAX_POINTS];
  float ys[TRAFFIC_MAX_POINTS];
  linePoints(line, offset, xs, ys);
  // Nejdřív celá čára zeleně, pak přes ni úseky v barvě provozu.
  drawStretch(pixels, xs, ys, 0, static_cast<uint8_t>(line.pointCount - 1),
              radius, TRAFFIC_LEVEL_FREE);
  for (uint8_t index = 0; index < line.segmentCount; ++index) {
    const TrafficSegment &segment = line.segments[index];
    if (segment.level == TRAFFIC_LEVEL_FREE) continue;
    drawStretch(pixels, xs, ys, segment.from, segment.to, radius,
                segment.level);
  }
}

// Text pixelovým písmem 5x7 vystředěný na bod.
void drawCenteredDigits(uint16_t *pixels, int x, int y, const char *text,
                        uint16_t color) {
  const int width = mapTextWidth(text);
  drawMapText(pixels, x - width / 2, y - MAP_CANVAS_GLYPH_HEIGHT / 2, text,
              color, 100);
}

MapLabelBox boxAround(int x, int y, int radius) {
  return MapLabelBox{x - radius - 1, y - radius - 1, 2 * radius + 3,
                     2 * radius + 3};
}

// Popisek místa: zkouší místa vpravo, vlevo, nad a pod tečkou. Obálka musí
// ležet v kruhu displeje a nesmí zakrýt nic zabraného dřív; z možných vyhraje
// ta, pod kterou je nejméně nakreslených čar - jméno nesmí schovat kolonu.
bool placeLabel(const uint16_t *pixels, uint16_t background,
                MapLabelPlacer &placer, int x, int y, int width, int height,
                MapLabelBox &box) {
  const int gap = PLACE_RADIUS + 3;
  const MapLabelBox candidates[] = {
      {x + gap, y - height / 2, width, height},
      {x - gap - width, y - height / 2, width, height},
      {x - width / 2, y - gap - height, width, height},
      {x - width / 2, y + gap, width, height},
      {x + gap, y - height - 2, width, height},
      {x + gap, y + 2, width, height},
      {x - gap - width, y - height - 2, width, height},
      {x - gap - width, y + 2, width, height},
      {x + gap, y + gap, width, height},
      {x + gap, y - gap - height, width, height},
      // O kus dál od tečky, když hned vedle ní začíná čára.
      {x + gap + 10, y - height / 2 + 1, width, height},
      {x - gap - 10 - width, y - height / 2 + 1, width, height},
  };
  int best = -1;
  int bestCovered = 0;
  for (int index = 0; index < static_cast<int>(sizeof(candidates) /
                                               sizeof(candidates[0]));
       ++index) {
    const MapLabelBox &candidate = candidates[index];
    if (!insideLabelCircle(candidate)) continue;
    bool free = true;
    for (size_t other = 0; free && other < placer.count; ++other)
      free = !mapBoxesOverlap(candidate, placer.occupied[other]);
    if (!free) continue;
    int covered = 0;
    for (int row = candidate.y; row < candidate.y + candidate.height; ++row)
      for (int column = candidate.x; column < candidate.x + candidate.width;
           ++column)
        if (row >= 0 && row < MAP_CANVAS_HEIGHT && column >= 0 &&
            column < MAP_CANVAS_WIDTH &&
            pixels[row * MAP_CANVAS_WIDTH + column] != background)
          ++covered;
    if (best < 0 || covered < bestCovered) {
      best = index;
      bestCovered = covered;
    }
    if (covered == 0) break;
  }
  if (best < 0 || !placer.claim(candidates[best])) return false;
  box = candidates[best];
  return true;
}

const char *levelName(uint8_t level, bool english) {
  switch (level) {
    case TRAFFIC_LEVEL_FREE: return english ? "free" : "volno";
    case TRAFFIC_LEVEL_SLOW: return english ? "slow" : "pomalu";
    case TRAFFIC_LEVEL_JAM: return english ? "jam" : "kolona";
    case TRAFFIC_LEVEL_STANDING: return english ? "stopped" : "stojí";
    default: return english ? "closed" : "zavřeno";
  }
}

void drawLegend(uint16_t *pixels, MapLabelFont &font, bool english,
                TrafficRenderResult &result) {
  constexpr int SWATCH = 12;
  constexpr int SWATCH_GAP = 4;
  constexpr int ITEM_GAP = 10;
  const uint8_t levels[] = {TRAFFIC_LEVEL_FREE, TRAFFIC_LEVEL_SLOW,
                            TRAFFIC_LEVEL_JAM, TRAFFIC_LEVEL_STANDING};
  int total = 0;
  for (uint8_t level : levels)
    total += SWATCH + SWATCH_GAP + font.width(levelName(level, english));
  total += ITEM_GAP * (static_cast<int>(sizeof(levels)) - 1);
  const int height = font.labelHeight();
  int x = TRAFFIC_MAP_CENTER - total / 2;
  const MapLabelBox box{x, TRAFFIC_LEGEND_Y, total, height};
  if (!insideLabelCircle(box)) return;
  const int middle = TRAFFIC_LEGEND_Y + height / 2;
  for (uint8_t level : levels) {
    drawCapsule(pixels, static_cast<float>(x + 2), static_cast<float>(middle),
                static_cast<float>(x + SWATCH - 2), static_cast<float>(middle),
                2.0f, palette(trafficLevelColor(level)), -1.0f);
    x += SWATCH + SWATCH_GAP;
    const char *name = levelName(level, english);
    font.draw(pixels, x, TRAFFIC_LEGEND_Y + 3, name, palette(rgb565(0xB5B5B5)));
    x += font.width(name) + ITEM_GAP;
  }
  result.legendDrawn = true;
}

}  // namespace

uint32_t trafficLevelRgb(uint8_t level) {
  switch (level) {
    case TRAFFIC_LEVEL_FREE: return 0x3CC850;
    case TRAFFIC_LEVEL_SLOW: return 0xFFD600;
    case TRAFFIC_LEVEL_JAM: return 0xFF8C00;
    case TRAFFIC_LEVEL_STANDING: return 0xF02020;
    default: return 0xA00000;
  }
}

uint16_t trafficLevelColor(uint8_t level) {
  return rgb565(trafficLevelRgb(level));
}

void trafficRender(uint16_t *pixels, const TrafficData *data, bool night,
                   bool english, MapLabelFont &labelFont, void (*yield)(),
                   TrafficRenderResult &result) {
  result = TrafficRenderResult{};
  nightPalette = night;
  const uint16_t background = palette(COLOR_BLACK);
  for (size_t index = 0;
       index < static_cast<size_t>(TRAFFIC_MAP_SIZE) * TRAFFIC_MAP_SIZE; ++index)
    pixels[index] = background;
  if (yield != nullptr) yield();
  if (data == nullptr) return;

  // Objížďka dospod, nad ní D1 oběma směry, navrch cesty z domova.
  for (uint8_t pass = 0; pass < 3; ++pass) {
    for (uint8_t index = 0; index < data->lineCount; ++index) {
      const TrafficLine &line = data->lines[index];
      const bool via = line.kind == TrafficLineKind::Via;
      const bool d1 = line.kind == TrafficLineKind::D1Prague ||
                      line.kind == TrafficLineKind::D1Brno;
      const uint8_t linePass = via ? 0 : d1 ? 1 : 2;
      if (linePass != pass) continue;
      if (via) drawLine(pixels, line, VIA_RADIUS, 0.0f);
      else if (d1) drawLine(pixels, line, D1_RADIUS, D1_OFFSET);
      else drawLine(pixels, line, DRIVE_RADIUS, 0.0f);
      if (yield != nullptr) yield();
    }
  }

  MapLabelPlacer placer;
  // Pásy LVGL popisků: tečky obrazovek, stavový řádek, nadpis a dole stavová
  // hláška s legendou.
  placer.claim({0, 16, TRAFFIC_MAP_SIZE, TRAFFIC_TOP_BAND_END_Y - 16});
  placer.claim({0, TRAFFIC_LEGEND_Y - 2, TRAFFIC_MAP_SIZE,
                TRAFFIC_MAP_SIZE - (TRAFFIC_LEGEND_Y - 2)});

  // Sjezdy D1: kroužek s číslem přímo na dálnici.
  for (uint8_t index = 0; index < data->exitCount; ++index) {
    const TrafficExit &exit = data->exits[index];
    fillMapCircle(pixels, exit.x, exit.y, EXIT_RADIUS, palette(rgb565(0x1E2A3A)),
                  100);
    drawMapCircle(pixels, exit.x, exit.y, EXIT_RADIUS, palette(rgb565(0xC8C8C8)),
                  100);
    char number[6];
    snprintf(number, sizeof(number), "%u", static_cast<unsigned>(exit.number));
    drawCenteredDigits(pixels, exit.x, exit.y, number, palette(COLOR_WHITE));
    placer.claim(boxAround(exit.x, exit.y, EXIT_RADIUS));
  }

  // Místa: bílá tečka a české jméno vedle ní.
  const int labelHeight = labelFont.labelHeight();
  for (uint8_t index = 0; index < data->placeCount; ++index) {
    const TrafficPlace &place = data->places[index];
    fillMapCircle(pixels, place.x, place.y, PLACE_RADIUS + 1, background, 100);
    fillMapCircle(pixels, place.x, place.y, PLACE_RADIUS, palette(COLOR_WHITE),
                  100);
    placer.claim(boxAround(place.x, place.y, PLACE_RADIUS));
  }

  // Autobusy navrch, aby je čáry nezakryly.
  for (uint8_t index = 0; index < data->busCount; ++index) {
    const TrafficBus &bus = data->buses[index];
    if (bus.hasDelay && bus.delaySeconds >= 180) {
      const uint16_t ring = palette(trafficLevelColor(
          bus.delaySeconds >= 300 ? TRAFFIC_LEVEL_STANDING : TRAFFIC_LEVEL_JAM));
      fillMapCircle(pixels, bus.x, bus.y, BUS_RADIUS + 3, ring, 100);
    } else {
      fillMapCircle(pixels, bus.x, bus.y, BUS_RADIUS + 1, background, 100);
    }
    fillMapCircle(pixels, bus.x, bus.y, BUS_RADIUS, palette(rgb565(0x1E78DC)),
                  100);
    drawCenteredDigits(pixels, bus.x, bus.y, bus.line, palette(COLOR_WHITE));
    placer.claim(boxAround(bus.x, bus.y, BUS_RADIUS));
    ++result.busCount;
  }
  if (yield != nullptr) yield();

  // Jména míst až nakonec, kdy je známé, co všechno už na mapě leží. Tmavý
  // podklad drží text čitelný i nad barevnou čarou.
  for (uint8_t index = 0; index < data->placeCount; ++index) {
    const TrafficPlace &place = data->places[index];
    const int width = labelFont.width(place.name) + 6;
    MapLabelBox box;
    if (!placeLabel(pixels, background, placer, place.x, place.y, width,
                    labelHeight, box))
      continue;
    fillMapRect(pixels, box.x, box.y, box.width, box.height, background, 70);
    labelFont.draw(pixels, box.x + 3, box.y + 3, place.name,
                   palette(COLOR_WHITE));
    result.placeLabels[result.placeLabelCount++] = box;
  }

  drawLegend(pixels, labelFont, english, result);
}
