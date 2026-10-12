#include "HomeControlFeed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

struct DomainName {
  const char *prefix;
  HomeControlDomain domain;
};

constexpr DomainName DOMAINS[] = {
    {"switch.", HomeControlDomain::Switch},
    {"light.", HomeControlDomain::Light},
    {"input_boolean.", HomeControlDomain::InputBoolean},
    {"fan.", HomeControlDomain::Fan},
    {"scene.", HomeControlDomain::Scene},
    {"script.", HomeControlDomain::Script},
};

bool idCharacter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

// Šablona pro štítek %s. Skutečné tabulátory a konce řádků (\t, \n v C++)
// oddělují pole a řádky odpovědi; '\\t', '\\n' a '\\.' jsou literály Jinja
// (tabulátor, konec řádku, tečka v regulárním výrazu). Příkon se hledá mezi
// čidly téhož zařízení podle device_class power, takže funguje pro tapo-bridge
// i pro integrace, které čidlo pojmenují jinak.
constexpr char TEMPLATE_FORMAT[] =
    "{%% for e in label_entities('%s')"
    " | select('match','(switch|light|input_boolean|fan|scene|script)\\.')"
    " | sort %%}{%% set d = device_id(e) %%}"
    "{{ e }}\t{{ states(e) }}\t"
    "{{ (state_attr(e,'friendly_name') or e) | replace('\\t',' ')"
    " | replace('\\n',' ') }}\t"
    "{{ state_attr(e,'brightness') if e.startswith('light.')"
    " and is_state(e,'on') and state_attr(e,'brightness') is not none"
    " else '' }}\t"
    "{{ (expand(device_entities(d)) | selectattr('domain','eq','sensor')"
    " | selectattr('attributes.device_class','defined')"
    " | selectattr('attributes.device_class','eq','power')"
    " | map(attribute='state') | first | default('')) if d else '' }}\n"
    "{%% endfor %%}";

// Zkopíruje pole [start, end) do `out` s nulou, oříznuté na kapacitu
// (bez rozpůleného znaku UTF-8).
void copyField(const char *start, const char *end, char *out, size_t capacity) {
  size_t length = static_cast<size_t>(end - start);
  if (length >= capacity) {
    length = capacity - 1;
    while (length > 0 && (static_cast<unsigned char>(start[length]) & 0xC0) == 0x80)
      --length;
  }
  memcpy(out, start, length);
  out[length] = '\0';
}

bool parseNumber(const char *start, const char *end, float &value) {
  if (start == end) return false;
  char text[24];
  copyField(start, end, text, sizeof(text));
  char *parsedEnd = nullptr;
  value = strtof(text, &parsedEnd);
  return parsedEnd != text && *parsedEnd == '\0';
}

}  // namespace

bool homeControlLabelValid(const char *label) {
  if (label == nullptr || label[0] == '\0') return false;
  for (const char *c = label; *c != '\0'; ++c) {
    if (!idCharacter(*c)) return false;
  }
  return true;
}

bool homeControlEntityIdValid(const char *entityId) {
  if (entityId == nullptr) return false;
  const char *dot = strchr(entityId, '.');
  if (dot == nullptr || dot == entityId || dot[1] == '\0') return false;
  for (const char *c = entityId; *c != '\0'; ++c) {
    if (c != dot && !idCharacter(*c)) return false;
  }
  return true;
}

bool homeControlDomainOf(const char *entityId, HomeControlDomain &domain) {
  for (const DomainName &candidate : DOMAINS) {
    if (strncmp(entityId, candidate.prefix, strlen(candidate.prefix)) == 0) {
      domain = candidate.domain;
      return true;
    }
  }
  return false;
}

size_t homeControlBuildTemplateRequest(const char *label, char *out,
                                       size_t capacity) {
  if (!homeControlLabelValid(label) || capacity == 0) return 0;
  char jinja[1024];
  const int written = snprintf(jinja, sizeof(jinja), TEMPLATE_FORMAT, label);
  if (written <= 0 || static_cast<size_t>(written) >= sizeof(jinja)) return 0;

  static constexpr char PREFIX[] = "{\"template\":\"";
  size_t length = 0;
  auto put = [&](char c) {
    if (length + 1 >= capacity) return false;
    out[length++] = c;
    return true;
  };
  for (const char *c = PREFIX; *c != '\0'; ++c)
    if (!put(*c)) return 0;
  for (const char *c = jinja; *c != '\0'; ++c) {
    bool ok = true;
    switch (*c) {
      case '"': ok = put('\\') && put('"'); break;
      case '\\': ok = put('\\') && put('\\'); break;
      case '\n': ok = put('\\') && put('n'); break;
      case '\t': ok = put('\\') && put('t'); break;
      default: ok = put(*c); break;
    }
    if (!ok) return 0;
  }
  if (!put('"') || !put('}')) return 0;
  out[length] = '\0';
  return length;
}

size_t homeControlParseResponse(const char *body, size_t length,
                                HomeControlItem *items, size_t capacity) {
  size_t count = 0;
  const char *end = body + length;
  const char *line = body;
  while (line < end && count < capacity) {
    const char *lineEnd = static_cast<const char *>(memchr(line, '\n', end - line));
    if (lineEnd == nullptr) lineEnd = end;
    const char *fields[5] = {};
    const char *fieldEnds[5] = {};
    size_t fieldCount = 0;
    const char *field = line;
    while (fieldCount < 5) {
      const char *tab = static_cast<const char *>(memchr(field, '\t', lineEnd - field));
      fields[fieldCount] = field;
      fieldEnds[fieldCount] = tab != nullptr ? tab : lineEnd;
      ++fieldCount;
      if (tab == nullptr) break;
      field = tab + 1;
    }
    line = lineEnd + 1;
    if (fieldCount < 2) continue;

    HomeControlItem &item = items[count];
    item = HomeControlItem();
    copyField(fields[0], fieldEnds[0], item.entityId, sizeof(item.entityId));
    // Ořezání by z ID udělalo jinou entitu; takovou radši vynechat.
    if (static_cast<size_t>(fieldEnds[0] - fields[0]) >= sizeof(item.entityId) ||
        !homeControlEntityIdValid(item.entityId) ||
        !homeControlDomainOf(item.entityId, item.domain))
      continue;

    char state[24];
    copyField(fields[1], fieldEnds[1], state, sizeof(state));
    if (item.domain == HomeControlDomain::Scene ||
        item.domain == HomeControlDomain::Script) {
      item.state = strcmp(state, "unavailable") == 0
                       ? HomeControlState::Unavailable
                       : HomeControlState::Stateless;
    } else if (strcmp(state, "on") == 0) {
      item.state = HomeControlState::On;
    } else if (strcmp(state, "off") == 0) {
      item.state = HomeControlState::Off;
    } else {
      item.state = HomeControlState::Unavailable;
    }

    if (fieldCount > 2 && fieldEnds[2] > fields[2])
      copyField(fields[2], fieldEnds[2], item.name, sizeof(item.name));
    else
      copyField(fields[0], fieldEnds[0], item.name, sizeof(item.name));

    float number = 0;
    if (fieldCount > 3 && parseNumber(fields[3], fieldEnds[3], number) &&
        number >= 0 && number <= 255) {
      // Jas 1-255 na 1-100 %, zaokrouhleno nahoru, aby 1 nebylo 0 %.
      item.brightnessPercent = static_cast<int8_t>((number * 100 + 254) / 255);
    }
    if (fieldCount > 4 && parseNumber(fields[4], fieldEnds[4], number) &&
        number >= 0)
      item.powerWatts = number;
    ++count;
  }
  return count;
}

size_t homeControlServicePath(HomeControlDomain domain, char *out,
                              size_t capacity) {
  const char *path = "switch/toggle";
  switch (domain) {
    case HomeControlDomain::Switch: path = "switch/toggle"; break;
    case HomeControlDomain::Light: path = "light/toggle"; break;
    case HomeControlDomain::InputBoolean: path = "input_boolean/toggle"; break;
    case HomeControlDomain::Fan: path = "fan/toggle"; break;
    case HomeControlDomain::Scene: path = "scene/turn_on"; break;
    case HomeControlDomain::Script: path = "script/turn_on"; break;
  }
  const size_t length = strlen(path);
  if (length >= capacity) return 0;
  memcpy(out, path, length + 1);
  return length;
}

size_t homeControlBuildServiceBody(const char *entityId, char *out,
                                   size_t capacity) {
  if (!homeControlEntityIdValid(entityId)) return 0;
  const int written = snprintf(out, capacity, "{\"entity_id\":\"%s\"}", entityId);
  if (written <= 0 || static_cast<size_t>(written) >= capacity) return 0;
  return static_cast<size_t>(written);
}
