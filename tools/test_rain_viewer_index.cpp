#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "RainViewerIndex.h"

namespace {

// Zkrácená, jinak doslovná odpověď z api.rainviewer.com. Pořadí klíčů i tvar
// cest odpovídá tomu, co server opravdu posílá; satelitní vrstva je tu kvůli
// tomu, že za polem radaru následuje další "past" se stejným tvarem.
const char *const REAL_INDEX =
    "{\"version\":\"2.0\",\"generated\":1788809424,"
    "\"host\":\"https://tilecache.rainviewer.com\","
    "\"radar\":{\"past\":["
    "{\"time\":1788802200,\"path\":\"/v2/radar/27d7b3eab0ad\"},"
    "{\"time\":1788802800,\"path\":\"/v2/radar/75b84ad60545\"},"
    "{\"time\":1788803400,\"path\":\"/v2/radar/bfa8913ee381\"},"
    "{\"time\":1788804000,\"path\":\"/v2/radar/4b9a35f91c22\"},"
    "{\"time\":1788809400,\"path\":\"/v2/radar/875a0979e8a3\"}"
    "],\"nowcast\":[]},"
    "\"satellite\":{\"infrared\":[{\"time\":1788809000,"
    "\"path\":\"/v2/satellite/aaaaaaaaaaaa\"}]}}";

void testRealResponse() {
  RainViewerIndex index;
  assert(rainViewerParseIndex(REAL_INDEX, 6, index));
  assert(std::string(index.host) == "https://tilecache.rainviewer.com");
  assert(index.frameCount == 5);
  // Od nejstaršího po nejnovější, stejně jako u ČHMÚ.
  assert(index.frames[0].time == 1788802200);
  assert(std::string(index.frames[0].path) == "/v2/radar/27d7b3eab0ad");
  assert(index.frames[4].time == 1788809400);
  assert(std::string(index.frames[4].path) == "/v2/radar/875a0979e8a3");
}

void testKeepsNewestFrames() {
  // Když si obrazovka řekne o méně snímků, než index nabízí, musí zůstat ty
  // nejnovější - starý konec animace nikoho nezajímá.
  RainViewerIndex index;
  assert(rainViewerParseIndex(REAL_INDEX, 2, index));
  assert(index.frameCount == 2);
  assert(index.frames[0].time == 1788804000);
  assert(index.frames[1].time == 1788809400);
}

void testStopsAtRadarSection() {
  // Satelitní snímek leží za koncem pole radaru a nesmí se do animace dostat.
  RainViewerIndex index;
  assert(rainViewerParseIndex(REAL_INDEX, 15, index));
  for (size_t frame = 0; frame < index.frameCount; ++frame)
    assert(std::string(index.frames[frame].path).find("/v2/radar/") == 0);
}

void testHostFallback() {
  // Bez pole "host" se použije výchozí adresa, jinak by nešla složit ani jedna
  // dlaždice.
  const char *payload =
      "{\"radar\":{\"past\":[{\"time\":1,\"path\":\"/v2/radar/x\"}]}}";
  RainViewerIndex index;
  assert(rainViewerParseIndex(payload, 6, index));
  assert(std::string(index.host) == "https://tilecache.rainviewer.com");
  assert(index.frameCount == 1);
}

void testRejectsUnusableResponses() {
  RainViewerIndex index;
  // Prázdný vstup, chybějící radar, prázdné pole i neuzavřené pole.
  assert(!rainViewerParseIndex("", 6, index));
  assert(!rainViewerParseIndex("{\"host\":\"https://x\"}", 6, index));
  assert(!rainViewerParseIndex("{\"radar\":{\"past\":[]}}", 6, index));
  assert(!rainViewerParseIndex(
      "{\"radar\":{\"past\":[{\"time\":1,\"path\":\"/v2/radar/x\"", 6, index));
  // Nulový počet snímků je vždy odmítnutí, ne pád.
  assert(!rainViewerParseIndex(REAL_INDEX, 0, index));
  assert(!rainViewerParseIndex(nullptr, 6, index));
}

void testRejectsOverlongPath() {
  // Cesta delší než buffer se zahodí, místo aby se ořízlá stáhla ze špatné
  // adresy.
  std::string payload =
      "{\"radar\":{\"past\":[{\"time\":1,\"path\":\"/v2/radar/";
  payload.append(RAIN_VIEWER_PATH_LENGTH + 8, 'a');
  payload += "\"}]}}";
  RainViewerIndex index;
  assert(!rainViewerParseIndex(payload.c_str(), 6, index));
}

void testCapsAtFrameLimit() {
  // Index umí vydat víc snímků, než kolik jich firmware drží.
  std::string payload = "{\"host\":\"https://h\",\"radar\":{\"past\":[";
  for (int frame = 0; frame < 30; ++frame) {
    if (frame > 0) payload += ",";
    payload += "{\"time\":" + std::to_string(1000 + frame) +
               ",\"path\":\"/v2/radar/f" + std::to_string(frame) + "\"}";
  }
  payload += "]}}";
  RainViewerIndex index;
  assert(rainViewerParseIndex(payload.c_str(), 99, index));
  assert(index.frameCount == RAIN_VIEWER_MAX_FRAMES);
  // Zůstat musí konec řady, tedy nejnovější snímky.
  assert(index.frames[index.frameCount - 1].time == 1029);
}

}  // namespace

int main() {
  testRealResponse();
  testKeepsNewestFrames();
  testStopsAtRadarSection();
  testHostFallback();
  testRejectsUnusableResponses();
  testRejectsOverlongPath();
  testCapsAtFrameLimit();
  printf("rain_viewer_index OK\n");
  return 0;
}
