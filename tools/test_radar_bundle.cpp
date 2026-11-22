// Snímky radaru ze serveru (RadarBundle): hlavička, meze snímků, rozbalení
// a odvození adresy z adresy srážkové předpovědi. Skutečná odpověď serveru
// (infra/rain v majnr, 320 x 220, tři snímky) leží v tools/fixtures.
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <zlib.h>

#include "RadarBundle.h"

#ifndef RADAR_BUNDLE_FIXTURE
#error "RADAR_BUNDLE_FIXTURE musí ukazovat na tools/fixtures/radar_bundle.bin"
#endif

namespace {

constexpr uint16_t WIDTH = 320;
constexpr uint16_t HEIGHT = 220;

void putU16(std::vector<uint8_t> &out, uint16_t value) {
  out.push_back(value & 0xff);
  out.push_back(value >> 8);
}

void putU32(std::vector<uint8_t> &out, uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) out.push_back((value >> shift) & 0xff);
}

std::vector<uint8_t> compress(const std::vector<uint8_t> &raw) {
  uLongf size = compressBound(raw.size());
  std::vector<uint8_t> out(size);
  assert(compress2(out.data(), &size, raw.data(), raw.size(), 9) == Z_OK);
  out.resize(size);
  return out;
}

std::vector<uint8_t> bundle(const std::vector<uint32_t> &slots,
                            const std::vector<std::vector<uint8_t>> &frames,
                            uint8_t version = RADAR_BUNDLE_VERSION) {
  std::vector<uint8_t> out = {'W', 'H', 'R', '1'};
  putU16(out, WIDTH);
  putU16(out, HEIGHT);
  out.push_back(static_cast<uint8_t>(slots.size()));
  out.push_back(version);
  putU16(out, 0);
  putU32(out, slots.empty() ? 0 : slots.back());
  for (size_t index = 0; index < slots.size(); ++index) {
    const std::vector<uint8_t> packed = compress(frames[index]);
    putU32(out, slots[index]);
    putU32(out, static_cast<uint32_t>(packed.size()));
    out.insert(out.end(), packed.begin(), packed.end());
  }
  return out;
}

std::vector<uint8_t> levels(uint8_t value) {
  return std::vector<uint8_t>(WIDTH * HEIGHT, value);
}

void testRoundTrip() {
  std::vector<uint8_t> second = levels(0);
  second[5] = 255;
  second[WIDTH * HEIGHT - 1] = 7 * 16;
  const std::vector<uint8_t> data =
      bundle({1000, 1300}, {levels(0), second});
  RadarBundle parsed;
  assert(radarBundleParse(data.data(), data.size(), WIDTH, HEIGHT, parsed));
  assert(parsed.count == 2);
  assert(parsed.newestSlot == 1300);
  assert(parsed.frames[0].slot == 1000 && parsed.frames[1].slot == 1300);
  std::vector<uint8_t> out(WIDTH * HEIGHT, 1);
  const RadarBundleFrame &frame = parsed.frames[1];
  assert(radarBundleInflate(data.data() + frame.offset, frame.size, out.data(),
                            out.size()));
  assert(out == second);
  // Snímek jiného rozměru se nerozbalí do bufferu, který hodiny mají.
  std::vector<uint8_t> small(WIDTH * HEIGHT - 1);
  assert(!radarBundleInflate(data.data() + frame.offset, frame.size,
                             small.data(), small.size()));
}

void testRejectsBrokenBundles() {
  const std::vector<uint8_t> good = bundle({1000, 1300}, {levels(0), levels(16)});
  RadarBundle parsed;
  // Jiný rozměr, než o který se hodiny ptaly.
  assert(!radarBundleParse(good.data(), good.size(), WIDTH, HEIGHT + 1, parsed));
  // Useknutá odpověď i bajt navíc.
  assert(!radarBundleParse(good.data(), good.size() - 1, WIDTH, HEIGHT, parsed));
  std::vector<uint8_t> longer = good;
  longer.push_back(0);
  assert(!radarBundleParse(longer.data(), longer.size(), WIDTH, HEIGHT, parsed));
  // Neznámá verze, sloty mimo pořadí, nula snímků, cizí hlavička.
  const std::vector<uint8_t> future = bundle({1000}, {levels(0)}, 2);
  assert(!radarBundleParse(future.data(), future.size(), WIDTH, HEIGHT, parsed));
  const std::vector<uint8_t> unordered = bundle({1300, 1000}, {levels(0), levels(0)});
  assert(!radarBundleParse(unordered.data(), unordered.size(), WIDTH, HEIGHT, parsed));
  const std::vector<uint8_t> empty = bundle({}, {});
  assert(!radarBundleParse(empty.data(), empty.size(), WIDTH, HEIGHT, parsed));
  std::vector<uint8_t> foreign = good;
  foreign[3] = '2';
  assert(!radarBundleParse(foreign.data(), foreign.size(), WIDTH, HEIGHT, parsed));
  // Délka snímku přes konec odpovědi.
  std::vector<uint8_t> overlong = good;
  overlong[16 + 4] = 0xff;
  overlong[16 + 5] = 0xff;
  assert(!radarBundleParse(overlong.data(), overlong.size(), WIDTH, HEIGHT, parsed));
  // Poškozená data se poznají při rozbalení (kontrolní součet zlibu).
  std::vector<uint8_t> corrupt = good;
  assert(radarBundleParse(corrupt.data(), corrupt.size(), WIDTH, HEIGHT, parsed));
  corrupt[parsed.frames[1].offset + parsed.frames[1].size - 1] ^= 0xff;
  std::vector<uint8_t> out(WIDTH * HEIGHT);
  assert(!radarBundleInflate(corrupt.data() + parsed.frames[1].offset,
                             parsed.frames[1].size, out.data(), out.size()));
}

void testServerFixture() {
  std::ifstream file(RADAR_BUNDLE_FIXTURE, std::ios::binary);
  const std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
  RadarBundle parsed;
  assert(radarBundleParse(data.data(), data.size(), WIDTH, HEIGHT, parsed));
  assert(parsed.count == 3);
  std::vector<uint8_t> out(WIDTH * HEIGHT);
  size_t rain = 0;
  for (uint8_t index = 0; index < parsed.count; ++index) {
    assert(radarBundleInflate(data.data() + parsed.frames[index].offset,
                              parsed.frames[index].size, out.data(), out.size()));
    for (uint8_t value : out) {
      // Stupně krát 16 se po vyhlazení můžou dostat mezi násobky, ale nikdy
      // nad stupeň 15; 255 je bez dat.
      assert(value == 255 || value <= 15 * 16);
      if (value != 0 && value != 255) ++rain;
    }
  }
  assert(rain > 0);
}

void testUrlFromRainUrl() {
  char url[96];
  assert(radarBundleUrlFromRainUrl("https://hodiny:heslo@example.net/rain.json",
                                   url, sizeof(url)));
  assert(strcmp(url, "https://hodiny:heslo@example.net/radar.bin") == 0);
  // Parametry v adrese předpovědi do adresy snímků nepatří.
  assert(radarBundleUrlFromRainUrl("http://example.net/x/rain.json?r=5", url,
                                   sizeof(url)));
  assert(strcmp(url, "http://example.net/x/radar.bin") == 0);
  assert(!radarBundleUrlFromRainUrl("", url, sizeof(url)));
  assert(url[0] == '\0');
  assert(!radarBundleUrlFromRainUrl("https://example.net/forecast", url,
                                    sizeof(url)));
  assert(!radarBundleUrlFromRainUrl("https://example.net/rain.json", url, 20));
}

// --- Rozbalování po řádcích (RadarBundleRows) --------------------------------
// Hodiny rozbalují snímek po řádcích přes okno 32 kB místo celého bufferu.
// Výsledek musí být bajt po bajtu stejný jako radarBundleInflate.

std::vector<uint8_t> inflateWhole(const uint8_t *data, size_t size,
                                  uint16_t width, uint16_t height) {
  std::vector<uint8_t> out(static_cast<size_t>(width) * height);
  assert(radarBundleInflate(data, size, out.data(), out.size()));
  return out;
}

// Čtení jako kulatý: řádek po řádku.
bool streamSequential(const uint8_t *data, size_t size, uint16_t width,
                      uint16_t height, const std::vector<uint8_t> &expected) {
  RadarBundleRows rows;
  if (!rows.begin(data, size, width, height)) return false;
  for (int y = 0; y < height; ++y) {
    const uint8_t *row = rows.row(y);
    if (row == nullptr) return false;
    assert(memcmp(row, expected.data() + static_cast<size_t>(y) * width,
                  width) == 0);
  }
  return rows.finish();
}

// Čtení jako 7" (composeLevels): pro každý řádek obrazu dvojnásobné výšky
// řádek snímku a jeho soused nad nebo pod, nejvyšší se žádá první.
bool streamLikeLcd7(const uint8_t *data, size_t size, uint16_t width,
                    uint16_t height, const std::vector<uint8_t> &expected) {
  RadarBundleRows rows;
  if (!rows.begin(data, size, width, height)) return false;
  for (int y = 0; y < height * 2; ++y) {
    const int nearY = y >> 1;
    int farY = (y & 1) ? nearY + 1 : nearY - 1;
    if (farY < 0) farY = 0;
    if (farY > height - 1) farY = height - 1;
    if (rows.row(nearY > farY ? nearY : farY) == nullptr) return false;
    const uint8_t *nearRow = rows.row(nearY);
    const uint8_t *farRow = rows.row(farY);
    if (nearRow == nullptr || farRow == nullptr) return false;
    assert(memcmp(nearRow, expected.data() + static_cast<size_t>(nearY) * width,
                  width) == 0);
    assert(memcmp(farRow, expected.data() + static_cast<size_t>(farY) * width,
                  width) == 0);
  }
  return rows.finish();
}

std::vector<uint8_t> compressedFrame(const std::vector<uint8_t> &raw) {
  return compress(raw);
}

void checkStreaming(const std::vector<uint8_t> &raw, uint16_t width,
                    uint16_t height) {
  const std::vector<uint8_t> packed = compressedFrame(raw);
  const std::vector<uint8_t> whole =
      inflateWhole(packed.data(), packed.size(), width, height);
  assert(whole == raw);
  assert(streamSequential(packed.data(), packed.size(), width, height, whole));
  assert(streamLikeLcd7(packed.data(), packed.size(), width, height, whole));
}

void testStreamingServerFixture() {
  std::ifstream file(RADAR_BUNDLE_FIXTURE, std::ios::binary);
  const std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
  RadarBundle parsed;
  assert(radarBundleParse(data.data(), data.size(), WIDTH, HEIGHT, parsed));
  for (uint8_t index = 0; index < parsed.count; ++index) {
    const uint8_t *frame = data.data() + parsed.frames[index].offset;
    const size_t size = parsed.frames[index].size;
    const std::vector<uint8_t> whole = inflateWhole(frame, size, WIDTH, HEIGHT);
    assert(streamSequential(frame, size, WIDTH, HEIGHT, whole));
    assert(streamLikeLcd7(frame, size, WIDTH, HEIGHT, whole));
  }
}

void testStreamingRoundFrames() {
  // Kulatý: 480 x 480 (230 kB, sedm oken). 32 768 není násobek 480 ani 320,
  // takže řádky padají i přes konec okna.
  uint32_t seed = 12345;
  const auto next = [&seed] {
    seed = seed * 1103515245u + 12345u;
    return static_cast<uint8_t>(seed >> 16);
  };
  for (uint16_t width : {480, 320}) {
    const uint16_t height = width == 480 ? 480 : 220;
    const size_t pixels = static_cast<size_t>(width) * height;
    // Bez srážek, se šumem (zlib nic nezabalí, každé volání plní okno do
    // konce) a s ostrůvky deště a oblastí bez dat jako skutečný snímek.
    checkStreaming(std::vector<uint8_t>(pixels, 0), width, height);
    std::vector<uint8_t> noise(pixels);
    for (uint8_t &value : noise) value = next();
    checkStreaming(noise, width, height);
    std::vector<uint8_t> rain(pixels, 0);
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x) {
        const int dx = x - width / 3;
        const int dy = y - height / 2;
        uint8_t &value = rain[static_cast<size_t>(y) * width + x];
        if (dx * dx + dy * dy < 90 * 90)
          value = static_cast<uint8_t>(((dx * dx + dy * dy) / 600 % 15 + 1) * 16 +
                                       (next() & 7));
        if (x > width - 40) value = 255;
      }
    checkStreaming(rain, width, height);
  }
}

void testStreamingRejectsBrokenFrames() {
  std::vector<uint8_t> raw(static_cast<size_t>(WIDTH) * HEIGHT, 0);
  for (size_t index = 0; index < raw.size(); index += 7) raw[index] = 16 * (index % 15);
  const std::vector<uint8_t> packed = compress(raw);
  std::vector<uint8_t> expected = raw;
  // Poškozený kontrolní součet: řádky projdou, finish() ne.
  std::vector<uint8_t> badSum = packed;
  badSum.back() ^= 0xff;
  {
    RadarBundleRows rows;
    assert(rows.begin(badSum.data(), badSum.size(), WIDTH, HEIGHT));
    bool all = true;
    for (int y = 0; y < HEIGHT && all; ++y) all = rows.row(y) != nullptr;
    assert(!all || !rows.finish());
  }
  // Useknutý proud.
  assert(!streamSequential(packed.data(), packed.size() / 2, WIDTH, HEIGHT,
                           expected));
  // Snímek vyšší, než se čeká (proud skončí dřív), i nižší (data zbudou).
  {
    RadarBundleRows rows;
    assert(rows.begin(packed.data(), packed.size(), WIDTH, HEIGHT + 1));
    bool all = true;
    for (int y = 0; y <= HEIGHT && all; ++y) all = rows.row(y) != nullptr;
    assert(!all || !rows.finish());
  }
  {
    RadarBundleRows rows;
    assert(rows.begin(packed.data(), packed.size(), WIDTH, HEIGHT - 1));
    for (int y = 0; y < HEIGHT - 1; ++y) assert(rows.row(y) != nullptr);
    assert(!rows.finish());
  }
  // Řádky starší než dva předchozí už nejsou.
  {
    RadarBundleRows rows;
    assert(rows.begin(packed.data(), packed.size(), WIDTH, HEIGHT));
    for (int y = 0; y <= 10; ++y) assert(rows.row(y) != nullptr);
    assert(rows.row(8) != nullptr);
    assert(rows.row(7) == nullptr);
  }
  // Jeden objekt znovu pro další snímek.
  {
    RadarBundleRows rows;
    for (int pass = 0; pass < 3; ++pass) {
      assert(rows.begin(packed.data(), packed.size(), WIDTH, HEIGHT));
      for (int y = 0; y < HEIGHT; ++y)
        assert(memcmp(rows.row(y), raw.data() + static_cast<size_t>(y) * WIDTH,
                      WIDTH) == 0);
      assert(rows.finish());
    }
  }
  radarBundleRowsRelease();
}

}  // namespace

int main() {
  testRoundTrip();
  testRejectsBrokenBundles();
  testServerFixture();
  testUrlFromRainUrl();
  testStreamingServerFixture();
  testStreamingRoundFrames();
  testStreamingRejectsBrokenFrames();
  return 0;
}
