// Stavy entit Home Assistantu jedním dotazem na /api/template
// (HomeAssistantTemplate).
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "HomeAssistantTemplate.h"

namespace {

std::string text(const JsonValue &value) {
  char buffer[300];
  haTemplateCopyText(value, buffer, sizeof(buffer));
  return buffer;
}

bool contains(const char *haystack, const char *needle) {
  return strstr(haystack, needle) != nullptr;
}

}  // namespace

int main() {
  // Platná ID: doména.objekt z malých písmen, číslic a podtržítek.
  assert(haTemplateEntityIdValid("sensor.teplota_venku"));
  assert(haTemplateEntityIdValid("sun.sun"));
  assert(haTemplateEntityIdValid("sensor.1st_floor"));
  assert(!haTemplateEntityIdValid(""));
  assert(!haTemplateEntityIdValid(nullptr));
  assert(!haTemplateEntityIdValid("sensor"));
  assert(!haTemplateEntityIdValid(".x"));
  assert(!haTemplateEntityIdValid("sensor."));
  assert(!haTemplateEntityIdValid("sensor.a.b"));
  assert(!haTemplateEntityIdValid("Sensor.A"));
  // Pokusy dostat se ven z řetězce v šabloně nebo z JSON těla.
  assert(!haTemplateEntityIdValid("sensor.x'] %}{{ 7*7 }}"));
  assert(!haTemplateEntityIdValid("sensor.x\"}"));
  assert(!haTemplateEntityIdValid("sensor.x\\"));
  assert(!haTemplateEntityIdValid("sensor.x y"));

  // Seznam jako ve firmwaru: prázdné pozice, duplicita a slunce na pozici 5.
  const char *ids[] = {"weather.domov",  "sensor.obyvak", "",
                       "sensor.obyvak",  "sensor.chybi",  "sun.sun",
                       "sensor.x'] %}",  "sensor.baterie"};
  const size_t count = sizeof(ids) / sizeof(ids[0]);
  assert(haTemplateKey(ids, count, 0) == 0);
  assert(haTemplateKey(ids, count, 2) == -1);
  assert(haTemplateKey(ids, count, 3) == 1);
  assert(haTemplateKey(ids, count, 6) == -1);
  assert(haTemplateKey(ids, count, 7) == 7);

  char request[HA_TEMPLATE_BUFFER_BYTES];
  const size_t requestLength =
      haTemplateBuildRequest(ids, count, 5, request, sizeof(request));
  assert(requestLength == strlen(request));
  printf("dotaz (%zu B): %s\n", requestLength, request);
  assert(contains(request,
                  "{% set ids = {'weather.domov':0,'sensor.obyvak':1,"
                  "'sensor.chybi':4,'sun.sun':5,'sensor.baterie':7} %}"));
  assert(contains(request, "{% set a = 5 %}"));
  assert(strncmp(request, "{\"template\":\"", 13) == 0);
  assert(strcmp(request + requestLength - 2, "\"}") == 0);
  // Uvnitř hodnoty "template" nesmí být uvozovka ani zpětné lomítko.
  for (size_t index = 13; index + 2 < requestLength; ++index) {
    assert(request[index] != '"' && request[index] != '\\');
  }
  assert(!contains(request, "7*7"));

  // Bez slunce se atributy nežádají u nikoho.
  char noSun[HA_TEMPLATE_BUFFER_BYTES];
  assert(haTemplateBuildRequest(ids, count, HA_TEMPLATE_NO_ATTRIBUTES, noSun,
                                sizeof(noSun)) > 0);
  assert(contains(noSun, "{% set a = -1 %}"));
  // Slunce zadané stejně jako jiná entita dostane klíč prvního výskytu.
  const char *sameAsSun[] = {"sun.sun", "", "", "", "", "sun.sun"};
  assert(haTemplateBuildRequest(sameAsSun, 6, 5, noSun, sizeof(noSun)) > 0);
  assert(contains(noSun, "{% set ids = {'sun.sun':0} %}{% set a = 0 %}"));
  // Žádná platná entita, malý buffer.
  const char *empty[] = {"", "neplatne"};
  assert(haTemplateBuildRequest(empty, 2, 0, noSun, sizeof(noSun)) == 0);
  assert(haTemplateBuildRequest(ids, count, 5, noSun, 64) == 0);
  assert(noSun[0] == '\0');

  // Odpověď: HA vrací entity ve svém pořadí, chybějící vynechá,
  // "unavailable" projde jako stav.
  const char *response =
      "[[5, \"above_horizon\", \"2026-10-11T05:12:00.123456+00:00\", "
      "\"2026-10-12T05:14:01.5+00:00\", \"2026-10-11T16:20:00+00:00\"], "
      "[1, \"21.4\"], [0, \"partlycloudy\"], [7, \"unavailable\"]]";
  HaTemplateEntity entities[count];
  assert(haTemplateParseResponse(response, strlen(response), ids, count,
                                 entities));
  assert(entities[0].found && text(entities[0].state) == "partlycloudy");
  assert(entities[1].found && text(entities[1].state) == "21.4");
  assert(!entities[2].found);
  // Duplicita dostane údaje prvního výskytu.
  assert(entities[3].found && text(entities[3].state) == "21.4");
  assert(!entities[4].found);
  assert(entities[5].found && text(entities[5].state) == "above_horizon");
  assert(text(entities[5].lastChanged) == "2026-10-11T05:12:00.123456+00:00");
  assert(text(entities[5].nextRising) == "2026-10-12T05:14:01.5+00:00");
  assert(text(entities[5].nextSetting) == "2026-10-11T16:20:00+00:00");
  assert(!entities[1].lastChanged.valid());
  assert(text(entities[1].lastChanged).empty());
  assert(!entities[6].found);
  assert(entities[7].found && text(entities[7].state) == "unavailable");

  // Prázdné pole = HA nezná žádnou entitu; to je platná odpověď.
  assert(haTemplateParseResponse(" [] \n", 5, ids, count, entities));
  for (const HaTemplateEntity &entity : entities) assert(!entity.found);

  // Escapy se rozbalí stejně jako dřív ze /api/states.
  const char *escaped = "[[1,\"a\\\"b\\\\c\"]]";
  assert(haTemplateParseResponse(escaped, strlen(escaped), ids, count,
                                 entities));
  assert(text(entities[1].state) == "a\"b\\c");

  // Cokoli neúplného nebo cizího se odmítne celé.
  const char *bad[] = {
      "",
      "[[1,\"21.4\"],[0,\"cloudy\"",                // useknutá
      "[[1,\"21.4\"]] navic",                        // smetí za polem
      "{\"message\":\"Unauthorized\"}",              // chybová odpověď
      "Error rendering template",                    // chyba šablony
      "[[1,21.4]]",                                  // stav není řetězec
      "[[\"1\",\"21.4\"]]",                          // klíč není číslo
      "[[99,\"21.4\"]]",                             // klíč mimo seznam
      "[[2,\"21.4\"]]",                              // prázdná pozice
      "[[3,\"21.4\"]]",                              // klíč duplicity
      "[[1]]",                                       // chybí stav
      "[[1,\"a\",\"b\"]]",                           // špatný počet polí
      "[[1,\"21.4\"][0,\"x\"]]",                     // chybí čárka
      "[[-1,\"21.4\"]]",
      "[1,\"21.4\"]",
  };
  for (const char *candidate : bad) {
    assert(!haTemplateParseResponse(candidate, strlen(candidate), ids, count,
                                    entities));
  }

  // Nejhorší případ: všech 44 pozic (8 pevných + 18 slotů + 18 baterií)
  // s různými ID nejdelší povolené délky (127 znaků).
  constexpr size_t MAX_ENTITIES = 44;
  std::vector<std::string> longIds;
  for (size_t index = 0; index < MAX_ENTITIES; ++index) {
    char prefix[24];
    snprintf(prefix, sizeof(prefix), "sensor.e%02zu_", index);
    std::string id = prefix;
    id.append(127 - id.size(), 'x');
    longIds.push_back(id);
  }
  const char *longIdPointers[MAX_ENTITIES];
  for (size_t index = 0; index < MAX_ENTITIES; ++index)
    longIdPointers[index] = longIds[index].c_str();
  const size_t worstRequest =
      haTemplateBuildRequest(longIdPointers, MAX_ENTITIES, 5, request,
                             sizeof(request));
  assert(worstRequest > 0);
  // Odpověď se stavem o 255 znacích (strop HA) u každé entity.
  std::string worstResponse = "[";
  for (size_t index = 0; index < MAX_ENTITIES; ++index) {
    if (index > 0) worstResponse += ", ";
    worstResponse += "[" + std::to_string(index) + ", \"" +
                     std::string(255, '9') + "\"";
    if (index == 5) {
      for (int field = 0; field < 3; ++field)
        worstResponse += ", \"" + std::string(40, 'x') + "\"";
    }
    worstResponse += "]";
  }
  worstResponse += "]";
  assert(worstResponse.size() <= HA_TEMPLATE_BUFFER_BYTES);
  HaTemplateEntity worstEntities[MAX_ENTITIES];
  assert(haTemplateParseResponse(worstResponse.data(), worstResponse.size(),
                                 longIdPointers, MAX_ENTITIES, worstEntities));
  // Obvyklý případ: 23 entit s krátkými stavy.
  std::string typical = "[";
  for (size_t index = 0; index < 23; ++index) {
    if (index > 0) typical += ", ";
    typical += "[" + std::to_string(index) + ", \"21.4\"]";
  }
  typical += "]";
  printf("nejhorší dotaz %zu B, nejhorší odpověď %zu B, "
         "obvyklá odpověď (23 entit) %zu B, buffer %zu B\n",
         worstRequest, worstResponse.size(), typical.size(),
         HA_TEMPLATE_BUFFER_BYTES);
  return 0;
}
