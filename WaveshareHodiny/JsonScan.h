#pragma once

#include <stddef.h>
#include <stdint.h>

// Drobný průchod JSONem pro odpovědi pevného tvaru. Firmware žádnou JSON
// knihovnu nemá - RssParser, WeatherForecast i RainViewerIndex si čtou odpověď
// samy, aby šly testovat na počítači a nestály flash ani haldu. Radar letadel
// potřebuje totéž na dvou místech naráz (seznam letadel a trasa letu), takže
// tenhle společný průchod stojí za to.
//
// Prochází se po dvojicích klíč-hodnota, ne hledáním podřetězce: klíč schovaný
// uvnitř textové hodnoty se tak nedá zaměnit za ten pravý. Řetězce se
// přeskakují i s escapovanými uvozovkami, vnořené objekty a pole se počítají,
// takže useknutá odpověď skončí neplatnou hodnotou místo čtení za koncem.

struct JsonValue {
  const char *begin = nullptr;
  const char *end = nullptr;
  bool isString = false;

  bool valid() const { return begin != nullptr; }
  // Vnitřek řetězce bez uvozovek; u ostatních hodnot celý zápis.
  const char *contentBegin() const { return isString ? begin + 1 : begin; }
  const char *contentEnd() const { return isString ? end - 1 : end; }
  bool isArray() const { return valid() && begin < end && *begin == '['; }
  bool isObject() const { return valid() && begin < end && *begin == '{'; }
};

const char *jsonSkipWhitespace(const char *cursor, const char *end);

// Konec libovolné hodnoty: řetězec, číslo, literál, objekt i pole. Vrací
// nullptr, když hodnota v daném rozsahu nekončí - tedy u useknuté odpovědi.
const char *jsonValueEnd(const char *cursor, const char *end);

// Hodnota klíče uvnitř JEDNOHO objektu.
JsonValue jsonFindMember(const char *objectBegin, const char *objectEnd,
                         const char *key);

// Číslo, i když ho server pošle jako řetězec. Text, který číslo není, se
// odmítne - atof("ground") vracelo nulu a tvářilo se to jako úspěch.
bool jsonReadNumber(const JsonValue &value, float &out);
bool jsonReadNumberMember(const char *objectBegin, const char *objectEnd,
                          const char *key, float &out);

// true jen pro literál true; cokoli jiného včetně chybějícího klíče je false.
bool jsonReadBoolMember(const char *objectBegin, const char *objectEnd,
                        const char *key);

// Textová hodnota bez okolních mezer, zkrácená na kapacitu cíle.
void jsonCopyTextMember(const char *objectBegin, const char *objectEnd,
                        const char *key, char *destination, size_t capacity);

bool jsonTextMemberEquals(const char *objectBegin, const char *objectEnd,
                          const char *key, const char *expected);

// Průchod polem objektů. Založí se z hodnoty, která je polem; next() posouvá
// na další prvek a vrací false, jakmile pole skončí nebo se rozbije.
struct JsonArrayCursor {
  const char *cursor = nullptr;
  const char *end = nullptr;
  // Rozsah aktuálního prvku.
  const char *itemBegin = nullptr;
  const char *itemEnd = nullptr;

  bool valid() const { return cursor != nullptr; }
};

JsonArrayCursor jsonOpenArray(const JsonValue &value);
bool jsonNextItem(JsonArrayCursor &cursor);
