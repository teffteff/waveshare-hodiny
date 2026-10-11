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

// Index se snímky s časy start, start + 600, ... (RainViewer vydává snímek po
// deseti minutách) a cestami odvozenými z času.
RainViewerIndex makeIndex(int64_t start, size_t count) {
  RainViewerIndex index;
  for (size_t frame = 0; frame < count; ++frame) {
    const int64_t time = start + static_cast<int64_t>(frame) * 600;
    index.frames[frame].time = time;
    snprintf(index.frames[frame].path, sizeof(index.frames[frame].path),
             "/v2/radar/%lld", static_cast<long long>(time));
  }
  index.frameCount = count;
  return index;
}

void testPlanUnchanged() {
  // Pravidelná obnova po pěti minutách často žádný nový snímek nenajde.
  const RainViewerIndex held = makeIndex(1000, 6);
  const RainViewerRefreshPlan plan =
      rainViewerPlanRefresh(held.frames, 6, makeIndex(1000, 6), 1);
  assert(plan.kind == RainViewerRefreshKind::Unchanged);
  assert(plan.shift == 0);
}

void testPlanShiftsByNewFrames() {
  // Jeden nový snímek: nejstarší vypadne, stáhne se jen ten nejnovější.
  const RainViewerIndex held = makeIndex(1000, 6);
  RainViewerRefreshPlan plan =
      rainViewerPlanRefresh(held.frames, 6, makeIndex(1600, 6), 1);
  assert(plan.kind == RainViewerRefreshKind::Shift);
  assert(plan.shift == 1);

  // Dva nové snímky s dovoleným posunem o čtyři (kulatý).
  plan = rainViewerPlanRefresh(held.frames, 6, makeIndex(2200, 6), 4);
  assert(plan.kind == RainViewerRefreshKind::Shift);
  assert(plan.shift == 2);
}

void testPlanReloadsBeyondShiftLimit() {
  // Na 7" se doplňuje nejvýš jeden snímek; víc znamená celé načtení.
  const RainViewerIndex held = makeIndex(1000, 6);
  assert(rainViewerPlanRefresh(held.frames, 6, makeIndex(2200, 6), 1).kind ==
         RainViewerRefreshKind::Reload);
  // Všechno nové (radar dlouho schovaný) se načte celé i bez stropu.
  assert(rainViewerPlanRefresh(held.frames, 6, makeIndex(9000, 6), 15).kind ==
         RainViewerRefreshKind::Reload);
}

void testPlanReloadsOnMismatch() {
  const RainViewerIndex held = makeIndex(1000, 6);
  // Jiný počet snímků (index jich zrovna vydal méně).
  assert(rainViewerPlanRefresh(held.frames, 6, makeIndex(1600, 5), 4).kind ==
         RainViewerRefreshKind::Reload);
  // Nic drženo.
  assert(rainViewerPlanRefresh(held.frames, 0, makeIndex(1000, 6), 4).kind ==
         RainViewerRefreshKind::Reload);
  assert(rainViewerPlanRefresh(nullptr, 6, makeIndex(1000, 6), 4).kind ==
         RainViewerRefreshKind::Reload);
  // Stejné časy, ale snímek uprostřed dostal jinou cestu (přepočítaný).
  RainViewerIndex changed = makeIndex(1000, 6);
  strcpy(changed.frames[3].path, "/v2/radar/reprocessed");
  assert(rainViewerPlanRefresh(held.frames, 6, changed, 4).kind ==
         RainViewerRefreshKind::Reload);
  // Díra v řadě: snímky nenavazují v žádném dovoleném posunu.
  RainViewerIndex gap = makeIndex(1600, 6);
  gap.frames[2] = makeIndex(50000, 1).frames[0];
  assert(rainViewerPlanRefresh(held.frames, 6, gap, 4).kind ==
         RainViewerRefreshKind::Reload);
  // Index šel zpátky (starší snímky než držené).
  assert(rainViewerPlanRefresh(held.frames, 6, makeIndex(400, 6), 4).kind ==
         RainViewerRefreshKind::Reload);
}

void testPlanSingleFrame() {
  // Jediný snímek se neposouvá: buď je stejný, nebo se načte znovu.
  const RainViewerIndex held = makeIndex(1000, 1);
  assert(rainViewerPlanRefresh(held.frames, 1, makeIndex(1000, 1), 4).kind ==
         RainViewerRefreshKind::Unchanged);
  assert(rainViewerPlanRefresh(held.frames, 1, makeIndex(1600, 1), 4).kind ==
         RainViewerRefreshKind::Reload);
}

void testPlanOverParsedIndex() {
  // Skutečná odpověď: hodiny drží čtyři snímky z doby před tím nejnovějším
  // (první čtyři z pěti) a index teď vydá poslední čtyři. Mezi čtvrtým
  // a pátým je v řadě díra, na posunu to nic nemění - rozhoduje cesta a čas.
  RainViewerIndex all;
  assert(rainViewerParseIndex(REAL_INDEX, 5, all));
  RainViewerIndex newestFour;
  assert(rainViewerParseIndex(REAL_INDEX, 4, newestFour));
  const RainViewerRefreshPlan plan =
      rainViewerPlanRefresh(all.frames, 4, newestFour, 1);
  assert(plan.kind == RainViewerRefreshKind::Shift);
  assert(plan.shift == 1);
  assert(rainViewerPlanRefresh(newestFour.frames, 4, newestFour, 1).kind ==
         RainViewerRefreshKind::Unchanged);
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
  testPlanUnchanged();
  testPlanShiftsByNewFrames();
  testPlanReloadsBeyondShiftLimit();
  testPlanReloadsOnMismatch();
  testPlanSingleFrame();
  testPlanOverParsedIndex();
  printf("rain_viewer_index OK\n");
  return 0;
}
