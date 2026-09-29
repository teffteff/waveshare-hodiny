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
                            "240ac41b2c3d", 49.90461f, 14.7842f, url,
                            sizeof(url)));
  assert(strcmp(url,
                "https://hodiny:heslo@example.net/alerts/config/240ac41b2c3d"
                "?lat=49.90461&lon=14.78420") == 0);
  assert(!pushAlertsBuildUrl("", "240ac41b2c3d", 0, 0, url, sizeof(url)));
  assert(!pushAlertsBuildUrl("https://example.net/alerts", "240ac41b2c3d", 0, 0,
                             url, 20));
  assert(!pushAlertsBuildUrl("https://example.net/alerts", "240ac41b2c3d", 91,
                             0, url, sizeof(url)));
  // Nejdelší adresa, kterou nastavení dovolí, se do bufferu vejde.
  char base[CLOCK_PUSH_URL_LENGTH];
  memset(base, 'a', sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  assert(pushAlertsBuildUrl(base, "240ac41b2c3d", -89.99999f, -179.99999f, url,
                            sizeof(url)));
}

const char ANSWER[] =
    "{\"elevation\": 478.0, \"portal\": \"https://portal.example.net\", "
    "\"settings\": {\"home\": {\"lat\": 49.9, \"lon\": 14.7},"
    " \"rain\": {\"enabled\": true, \"lead\": 20, \"dbz\": 28, \"radius\": 5},"
    " \"military\": {\"enabled\": true, \"radius\": 30},"
    " \"rare\": {\"enabled\": false, \"radius\": 10},"
    " \"low\": {\"enabled\": true, \"distance\": 1500, \"height\": 500},"
    " \"circling\": {\"enabled\": true, \"radius\": 10},"
    " \"warnings\": {\"enabled\": true, \"level\": 3},"
    " \"quiet\": {\"from\": 22, \"to\": 7}}}";

bool parse(const char *text, PushAlertsServerSettings &out) {
  return pushAlertsParseAnswer(text, text + strlen(text), out);
}

void testAnswer() {
  PushAlertsServerSettings settings;
  assert(parse(ANSWER, settings));
  assert(settings.known && settings.elevationKnown);
  assert(settings.elevationM == 478.0f);
  assert(strcmp(settings.portal, "https://portal.example.net") == 0);
  assert(settings.rain && settings.rainLeadMinutes == 20 &&
         settings.rainMinimumDbz == 28 && settings.rainRadiusKm == 5);
  assert(settings.military && settings.militaryRadiusKm == 30);
  assert(!settings.rare && settings.rareRadiusKm == 10);
  assert(settings.low && settings.lowDistanceM == 1500 &&
         settings.lowHeightM == 500);
  assert(settings.circling && settings.circlingRadiusKm == 10);
  assert(settings.warnings && settings.warningsLevel == 3);
  assert(settings.quietFromHour == 22 && settings.quietToHour == 7);
}

void testAnswerWithoutHome() {
  PushAlertsServerSettings settings;
  assert(parse("{\"elevation\": null, \"portal\": \"\", \"settings\": null}",
               settings));
  assert(!settings.known && !settings.elevationKnown);
  assert(settings.portal[0] == '\0');
}

void testBrokenAnswers() {
  PushAlertsServerSettings settings;
  assert(!parse("", settings));
  assert(!parse("{\"elevation\": 478.0}", settings));
  // Useknutá odpověď.
  char truncated[sizeof(ANSWER)];
  memcpy(truncated, ANSWER, sizeof(ANSWER));
  truncated[sizeof(ANSWER) - 10] = '\0';
  assert(!parse(truncated, settings));
  assert(!settings.known);
  // Hodnota mimo meze nebo špatného typu.
  const char *bad[] = {"\"level\": 3", "\"lead\": 20", "\"enabled\": true, \"radius\": 30"};
  const char *worse[] = {"\"level\": 7", "\"lead\": 20.5",
                         "\"enabled\": \"yes\", \"radius\": 30"};
  for (int i = 0; i < 3; ++i) {
    char text[sizeof(ANSWER) + 16];
    const char *at = strstr(ANSWER, bad[i]);
    assert(at != nullptr);
    const size_t prefix = static_cast<size_t>(at - ANSWER);
    memcpy(text, ANSWER, prefix);
    strcpy(text + prefix, worse[i]);
    strcat(text, at + strlen(bad[i]));
    assert(!parse(text, settings));
  }
}

void testPortalMustBePlainHttps() {
  const char *portals[] = {"http://portal.example.net",
                           "https://user:pass@portal.example.net",
                           "https://portal.example.net/\\u003cscript"};
  for (const char *portal : portals) {
    char text[sizeof(ANSWER) + 64];
    const char *at = strstr(ANSWER, "https://portal.example.net");
    const size_t prefix = static_cast<size_t>(at - ANSWER);
    memcpy(text, ANSWER, prefix);
    strcpy(text + prefix, portal);
    strcat(text, at + strlen("https://portal.example.net"));
    PushAlertsServerSettings settings;
    assert(parse(text, settings));
    assert(settings.portal[0] == '\0');
  }
}

int main() {
  testIdFromMac();
  testUrl();
  testAnswer();
  testAnswerWithoutHome();
  testBrokenAnswers();
  testPortalMustBePlainHttps();
  return 0;
}
