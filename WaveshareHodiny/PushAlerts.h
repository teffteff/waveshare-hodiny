#pragma once

#include <stddef.h>

#include "ClockConfig.h"

// Nastavení upozornění na telefon tak, jak ho čte infra/alerts/serve.py
// (parse_config). Bez FreeRTOS a sítě, aby se dalo testovat na počítači.

// Identifikátor hodin na serveru: MAC bez dvojteček, malými písmeny. Jméno
// v síti by nestačilo - dvoje hodiny s výchozím jménem by si nastavení
// přepisovaly.
constexpr size_t PUSH_ALERTS_ID_LENGTH = 13;
constexpr size_t PUSH_ALERTS_BODY_LENGTH = 512;
constexpr size_t PUSH_ALERTS_REQUEST_URL_LENGTH =
    CLOCK_PUSH_URL_LENGTH + sizeof("/config/") + PUSH_ALERTS_ID_LENGTH;

// "AA:BB:CC:DD:EE:FF" -> "aabbccddeeff". False, když MAC nemá tenhle tvar.
bool pushAlertsIdFromMac(const char *mac, char *id, size_t capacity);

// <adresa bez koncových lomítek>/config/<id>.
bool pushAlertsBuildUrl(const char *base, const char *id, char *url,
                        size_t capacity);

// JSON tělo pro PUT. Jméno je jen popisek do přehledu serveru; smí obsahovat
// jen znaky jména v síti, takže se nic neescapuje.
bool pushAlertsBuildBody(const ClockPushAlertsConfig &alerts, float latitude,
                         float longitude, const char *name, char *body,
                         size_t capacity);
