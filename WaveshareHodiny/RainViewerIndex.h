#pragma once

#include <stddef.h>
#include <stdint.h>

// Rozbor indexu RainVieweru, oddělený od stahování, aby šel testovat na
// počítači - stejně jako RssParser vedle RssService.
//
// Index má pod kilobajt a pevný tvar:
//   {"version":"2.0","generated":...,"host":"https://tilecache.rainviewer.com",
//    "radar":{"past":[{"time":1788802200,"path":"/v2/radar/27d7b3eab0ad"},...],
//             "nowcast":[...]},"satellite":{...}}
// Ze tří polí se nevyplatí tahat do firmwaru celý parser JSON, takže se klíče
// hledají v textu jako u ostatních odpovědí, které hodiny čtou.

// Strop odpovídá největšímu počtu snímků, který umí nastavení radaru.
constexpr size_t RAIN_VIEWER_MAX_FRAMES = 15;
constexpr size_t RAIN_VIEWER_PATH_LENGTH = 48;
constexpr size_t RAIN_VIEWER_HOST_LENGTH = 64;

struct RainViewerFrame {
  char path[RAIN_VIEWER_PATH_LENGTH] = "";
  // Sekundy od epochy UTC, jak je vydává RainViewer.
  int64_t time = 0;
};

struct RainViewerIndex {
  char host[RAIN_VIEWER_HOST_LENGTH] = "";
  // Seřazeno od nejstaršího po nejnovější, stejně jako u ČHMÚ, aby animace
  // běžela v obou zdrojích stejným směrem.
  RainViewerFrame frames[RAIN_VIEWER_MAX_FRAMES];
  size_t frameCount = 0;
};

// Vytáhne z odpovědi adresu dlaždicového serveru a posledních `wantedFrames`
// snímků radaru. Vrací false, když odpověď nemá ani jeden použitelný snímek.
bool rainViewerParseIndex(const char *payload, size_t wantedFrames,
                          RainViewerIndex &index);
