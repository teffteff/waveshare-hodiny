#pragma once

#include <Arduino.h>

#include "ClockConfig.h"
#include "TrafficFeed.h"

// Obrazovka Doprava. Data vozí vlastní server (toulky /api/doprava?for=hodiny)
// už promítnutá do pixelů displeje; služba je stahuje ve vlastní úloze,
// rozebírá a mapu kreslí do sdíleného páru snímků (SharedFrames), stejně jako
// radar letadel. Text kolem mapy a textovou stránku skládá ClockDashboard
// z popisků LVGL.
//
// Stahuje se jednou za minutu, když je obrazovka vidět, a jednou za pět minut
// schovaná, pokud je zapojená do střídání. Po chybě se čeká dvakrát déle než
// minule, nejvýš patnáct minut; poslední platná data na obrazovce zůstávají.

constexpr uint16_t TRAFFIC_FRAME_WIDTH = 480;
constexpr uint16_t TRAFFIC_FRAME_HEIGHT = 480;

// Co potřebuje textová stránka a popisky kolem mapy. Kopíruje se jen při
// změně (textsGeneration), protože má přes dva kilobajty.
struct TrafficTexts {
  int64_t now = 0;
  int64_t sampleTime = 0;
  bool stale = false;
  uint8_t driveCount = 0;
  TrafficDrive drives[TRAFFIC_MAX_DRIVES];
  uint8_t textCount = 0;
  char texts[TRAFFIC_MAX_TEXTS][TRAFFIC_TEXT_LENGTH] = {};
  uint8_t warningCount = 0;
  char warnings[TRAFFIC_MAX_WARNINGS][TRAFFIC_TEXT_LENGTH] = {};
  uint8_t departureCount = 0;
  char departures[TRAFFIC_MAX_DEPARTURES][TRAFFIC_TEXT_LENGTH] = {};
};

struct TrafficSnapshot {
  // Hotová mapa, nebo nullptr, dokud se nenakreslila (nebo snímky právě patří
  // jiné obrazovce).
  const uint16_t *pixels = nullptr;
  // Roste s každou nakreslenou mapou i s novými daty.
  uint32_t generation = 0;
  bool loading = false;
  // Dorazila aspoň jedna platná odpověď.
  bool haveData = false;
  // Kdy dorazila (millis()); s haveData říká, jak stará data jsou.
  unsigned long lastSuccessAt = 0;
  // Chyba posledního stažení; prázdná, když je všechno v pořádku.
  char message[64] = "";
  // Texty se kopírují jen tehdy, když se tohle číslo liší od služby.
  uint32_t textsGeneration = UINT32_MAX;
  TrafficTexts texts;
};

void trafficServiceBegin();
void trafficServicePrepareForFirmwareUpdate();

// visible: obrazovka je na displeji. backgroundRefresh: je ve střídání, takže
// se stahuje i schovaná (řidčeji), aby měla rotace co ukázat.
void trafficServiceSetActive(bool visible, bool backgroundRefresh,
                             const char *url, bool english);
void trafficServiceSetRedNightMode(bool enabled);
void trafficServiceSnapshot(TrafficSnapshot &snapshot);
// Je co ukázat? Pro automatické střídání.
bool trafficServiceHasData();
