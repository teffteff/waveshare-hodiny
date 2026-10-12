// Šablona a rozbor odpovědi obrazovky Ovládání (štítek HA "hodiny").
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "../WaveshareHodiny/HomeControlFeed.h"

namespace {

void testLabelAndEntityValidation() {
  assert(homeControlLabelValid("hodiny"));
  assert(homeControlLabelValid("hodiny_pracovna2"));
  assert(!homeControlLabelValid(""));
  assert(!homeControlLabelValid("Hodiny"));
  assert(!homeControlLabelValid("ho'diny"));
  assert(homeControlEntityIdValid("switch.lampa_tv"));
  assert(!homeControlEntityIdValid("switch"));
  assert(!homeControlEntityIdValid("switch."));
  assert(!homeControlEntityIdValid(".lampa"));
  assert(!homeControlEntityIdValid("switch.lampa\"tv"));
  assert(!homeControlEntityIdValid("switch.lampa.tv"));
}

void testTemplateRequest() {
  char body[HOME_CONTROL_BUFFER_BYTES];
  const size_t length = homeControlBuildTemplateRequest("hodiny", body, sizeof(body));
  assert(length > 0 && length == strlen(body));
  const std::string text(body);
  assert(text.rfind("{\"template\":\"", 0) == 0);
  assert(text.substr(text.size() - 2) == "\"}");
  assert(text.find("label_entities('hodiny')") != std::string::npos);
  // Pole odděluje tabulátor (v JSON \t), Jinja dostane literál '\t' (v JSON \\t).
  assert(text.find("{{ e }}\\t{{ states(e) }}\\t") != std::string::npos);
  assert(text.find("replace('\\\\t',' ')") != std::string::npos);
  assert(text.find("script)\\\\.')") != std::string::npos);
  // Žádný neescapovaný konec řádku ani uvozovka uvnitř.
  assert(text.find('\n') == std::string::npos);
  assert(text.find('"', 13) == text.size() - 2);
  assert(text.find("{% endfor %}") != std::string::npos);

  assert(homeControlBuildTemplateRequest("Hodiny", body, sizeof(body)) == 0);
  assert(homeControlBuildTemplateRequest("hodiny", body, 100) == 0);
}

void testParse() {
  const std::string response =
      "light.chytra_zarovka\ton\tChytrá žárovka\t178\t\n"
      "switch.lampa_tv\toff\tLampa TV\t\t0.0\n"
      "switch.zelvy\tunavailable\tZelvy\t\t\n"
      "sensor.teplota\t21.4\tTeplota\t\t\n"          // neznámá doména
      "scene.noc\t2026-10-12T20:00:00+00:00\tNoc\t\t\n"
      "switch.bez_jmena\ton\t\t\t1840.5\n"
      "\n";
  HomeControlItem items[HOME_CONTROL_MAX_ITEMS];
  const size_t count =
      homeControlParseResponse(response.data(), response.size(), items, HOME_CONTROL_MAX_ITEMS);
  assert(count == 5);

  assert(strcmp(items[0].entityId, "light.chytra_zarovka") == 0);
  assert(items[0].domain == HomeControlDomain::Light);
  assert(items[0].state == HomeControlState::On);
  assert(strcmp(items[0].name, "Chytrá žárovka") == 0);
  assert(items[0].brightnessPercent == 70);
  assert(items[0].powerWatts < 0);

  assert(items[1].state == HomeControlState::Off);
  assert(items[1].brightnessPercent == -1);
  assert(std::fabs(items[1].powerWatts) < 0.001f);

  assert(items[2].state == HomeControlState::Unavailable);

  assert(items[3].domain == HomeControlDomain::Scene);
  assert(items[3].state == HomeControlState::Stateless);

  // Bez jména se ukáže ID entity.
  assert(strcmp(items[4].name, "switch.bez_jmena") == 0);
  assert(std::fabs(items[4].powerWatts - 1840.5f) < 0.01f);
}

void testParseLimitsAndTruncation() {
  std::string response;
  for (int index = 0; index < 20; ++index) {
    char line[96];
    snprintf(line, sizeof(line), "switch.s%02d\ton\tVypínač %d\t\t\n", index, index);
    response += line;
  }
  HomeControlItem items[HOME_CONTROL_MAX_ITEMS];
  assert(homeControlParseResponse(response.data(), response.size(), items,
                                  HOME_CONTROL_MAX_ITEMS) == HOME_CONTROL_MAX_ITEMS);
  assert(strcmp(items[11].entityId, "switch.s11") == 0);

  // Dlouhé jméno se ořízne bez rozpůleného znaku UTF-8; poslední řádek bez \n.
  std::string longName = "switch.dlouhe\toff\t";
  for (int index = 0; index < 40; ++index) longName += "ž";
  HomeControlItem one[1];
  assert(homeControlParseResponse(longName.data(), longName.size(), one, 1) == 1);
  const size_t nameLength = strlen(one[0].name);
  assert(nameLength < HOME_CONTROL_NAME_LENGTH && nameLength % 2 == 0);

  // Příliš dlouhé ID se nesmí oříznout na jinou entitu.
  std::string longId = "switch." + std::string(120, 'a') + "\ton\tX\t\t\n";
  assert(homeControlParseResponse(longId.data(), longId.size(), one, 1) == 0);
}

void testServiceCalls() {
  char path[32];
  assert(homeControlServicePath(HomeControlDomain::Light, path, sizeof(path)) > 0);
  assert(strcmp(path, "light/toggle") == 0);
  assert(homeControlServicePath(HomeControlDomain::Scene, path, sizeof(path)) > 0);
  assert(strcmp(path, "scene/turn_on") == 0);
  char body[128];
  assert(homeControlBuildServiceBody("switch.lampa_tv", body, sizeof(body)) > 0);
  assert(strcmp(body, "{\"entity_id\":\"switch.lampa_tv\"}") == 0);
  assert(homeControlBuildServiceBody("switch.\"x", body, sizeof(body)) == 0);
  assert(homeControlBuildServiceBody("switch.lampa_tv", body, 10) == 0);
}

}  // namespace

int main() {
  testLabelAndEntityValidation();
  testTemplateRequest();
  testParse();
  testParseLimitsAndTruncation();
  testServiceCalls();
  puts("home_control_feed OK");
  return 0;
}
