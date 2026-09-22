#include <assert.h>
#include <string.h>

#include "PushAlerts.h"

void testIdFromMac() {
  char id[PUSH_ALERTS_ID_LENGTH];
  assert(pushAlertsIdFromMac("24:0A:C4:1B:2C:3D", id, sizeof(id)));
  assert(strcmp(id, "240ac41b2c3d") == 0);
  assert(!pushAlertsIdFromMac("24:0A:C4:1B:2C", id, sizeof(id)));
  assert(!pushAlertsIdFromMac("24:0A:C4:1B:2C:3D:4E", id, sizeof(id)));
  assert(!pushAlertsIdFromMac("24:0A:C4:1B:2C:3G", id, sizeof(id)));
  assert(!pushAlertsIdFromMac("24:0A:C4:1B:2C:3D", id, 12));
}

void testUrl() {
  char url[PUSH_ALERTS_REQUEST_URL_LENGTH];
  assert(pushAlertsBuildUrl("https://hodiny:heslo@example.net/alerts//",
                            "240ac41b2c3d", url, sizeof(url)));
  assert(strcmp(url,
                "https://hodiny:heslo@example.net/alerts/config/240ac41b2c3d") ==
         0);
  assert(!pushAlertsBuildUrl("", "240ac41b2c3d", url, sizeof(url)));
  assert(!pushAlertsBuildUrl("https://example.net/alerts", "240ac41b2c3d", url,
                             20));
}

void testBody() {
  ClockPushAlertsConfig alerts;
  alerts.enabled = true;
  alerts.rare = false;
  char body[PUSH_ALERTS_BODY_LENGTH];
  assert(pushAlertsBuildBody(alerts, 49.90461f, 14.7842f, "pracovna", body,
                             sizeof(body)));
  assert(strcmp(body,
                "{\"name\":\"pracovna\",\"enabled\":true,\"lat\":49.90461,"
                "\"lon\":14.78420,"
                "\"rain\":{\"enabled\":true,\"lead\":20,\"dbz\":28,\"radius\":5},"
                "\"planes\":{\"military\":true,\"rare\":false,\"low\":true,"
                "\"radius\":30,\"lowRadius\":1500,\"lowHeight\":500},"
                "\"quiet\":{\"from\":22,\"to\":7}}") == 0);
  // Jméno jde do JSON bez escapování, takže nic mimo znaky jména v síti.
  assert(!pushAlertsBuildBody(alerts, 0, 0, "a\"b", body, sizeof(body)));
  assert(!pushAlertsBuildBody(alerts, 0, 0, "pracovna", body, 40));
}

int main() {
  testIdFromMac();
  testUrl();
  testBody();
  return 0;
}
