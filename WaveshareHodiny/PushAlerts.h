#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ClockConfig.h"

// Nastavení upozornění na telefon tak, jak ho vrací server (majnr
// infra/alerts, GET /alerts/config/<id>). Nastavení je jedno pro celou
// domácnost a mění se na webu serveru; hodiny ho jen čtou. Bez FreeRTOS
// a sítě, aby se dalo testovat na počítači.

// Identifikátor hodin na serveru: MAC bez dvojteček, malými písmeny.
constexpr size_t PUSH_ALERTS_ID_LENGTH = 13;
// "?lat=-90.00000&lon=-180.00000"
constexpr size_t PUSH_ALERTS_QUERY_LENGTH = 32;
constexpr size_t PUSH_ALERTS_REQUEST_URL_LENGTH =
    CLOCK_PUSH_URL_LENGTH + sizeof("/config/") + PUSH_ALERTS_ID_LENGTH +
    PUSH_ALERTS_QUERY_LENGTH;
constexpr size_t PUSH_ALERTS_PORTAL_LENGTH = 96;

// "AA:BB:CC:DD:EE:FF" -> "aabbccddeeff". False, když MAC nemá tenhle tvar.
bool pushAlertsIdFromMac(const char *mac, char *id, size_t capacity);

// <adresa bez koncových lomítek>/config/<id>?lat=..&lon=..
bool pushAlertsBuildUrl(const char *base, const char *id, float latitude,
                        float longitude, char *url, size_t capacity);

struct PushAlertsServerSettings {
  // Server odpověděl a nastavení domácnosti zná.
  bool known = false;
  bool elevationKnown = false;
  float elevationM = 0.0f;
  // Web, kde se nastavení mění; prázdný, když ho server neposlal.
  char portal[PUSH_ALERTS_PORTAL_LENGTH] = "";

  bool rain = false;
  uint8_t rainLeadMinutes = 0;
  uint8_t rainMinimumDbz = 0;
  uint8_t rainRadiusKm = 0;
  bool military = false;
  uint8_t militaryRadiusKm = 0;
  bool rare = false;
  uint8_t rareRadiusKm = 0;
  bool low = false;
  uint16_t lowDistanceM = 0;
  uint16_t lowHeightM = 0;
  bool circling = false;
  uint8_t circlingRadiusKm = 0;
  bool warnings = false;
  // 2 žlutá, 3 oranžová, 4 červená.
  uint8_t warningsLevel = 0;
  uint8_t quietFromHour = 0;
  uint8_t quietToHour = 0;
};

// Odpověď {"elevation":478.0,"portal":"https://..","settings":{...}}.
// False u rozbité odpovědi; "settings":null (server nezná polohu domu) je
// platná odpověď s known = false.
bool pushAlertsParseAnswer(const char *begin, const char *end,
                           PushAlertsServerSettings &out);
