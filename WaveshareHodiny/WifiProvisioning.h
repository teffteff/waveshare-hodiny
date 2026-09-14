#pragma once

#include <Arduino.h>

void wifiProvisioningBegin();
void wifiProvisioningLoop();
void wifiProvisioningPauseStartupRetries();
// One background attempt on the saved network while the onboarding portal is
// up; End gives up on it so the access point stays on its own channel.
bool wifiProvisioningPortalAttemptBegin();
void wifiProvisioningPortalAttemptEnd();
void wifiProvisioningStart(const String &ssid, const String &password);
bool wifiProvisioningSavePendingForRestart(const String &ssid,
                                           const String &password);
bool wifiProvisioningHasCredentials();
bool wifiProvisioningIsActive();
bool wifiProvisioningReadyForNormalStart();
