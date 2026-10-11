// Rozdělení historie Home Assistantu do úseků (HaHistoryBuckets).
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <string>

#include "HaHistoryBuckets.h"

int main() {
  int64_t t = 0;
  assert(haParseTimestamp("2026-10-10T10:00:00+00:00", t) && t == 1791626400);
  assert(haParseTimestamp("2026-10-10T12:00:00.123456+02:00", t) && t == 1791626400);
  assert(haParseTimestamp("2026-10-10T10:00:00Z", t) && t == 1791626400);
  assert(!haParseTimestamp("nesmysl", t));

  // Čtyři hodinové úseky od 10:00 UTC. Stav 20 platí od rána, ve 11:30 se
  // změní na 21, ve 12:10 čidlo vypadne a ve 13:20 se vrátí s 22.
  const char *payload =
      "[[{\"entity_id\":\"sensor.t\",\"state\":\"20.0\",\"last_changed\":\"2026-10-10T09:00:00+00:00\"},"
      "{\"state\":\"21.5\",\"last_changed\":\"2026-10-10T11:30:00.5+00:00\"},"
      "{\"state\":\"unavailable\",\"last_changed\":\"2026-10-10T12:10:00+00:00\"},"
      "{\"state\":\"22\",\"last_changed\":\"2026-10-10T13:20:00Z\"}]]";
  HaHistoryBuckets buckets(1791626400, 3600, 4);
  // Po malých kouscích, jako ze sítě, i se zlomem uprostřed klíče.
  for (size_t offset = 0; offset < strlen(payload); offset += 7)
    buckets.feed(payload + offset, std::min<size_t>(7, strlen(payload) - offset));
  float values[4];
  assert(buckets.finish(values) == 4);
  assert(values[0] == 20.0f);         // 10-11: ještě ranní stav
  assert(values[1] == 21.5f);         // 11-12: změna v 11:30
  assert(std::isnan(values[2]));      // 12-13: čidlo nedostupné
  assert(values[3] == 22.0f);         // 13-14: zpět

  // Bez záznamů zůstane všechno NAN.
  HaHistoryBuckets empty(0, 900, 3);
  empty.feed("[[]]", 4);
  float none[3];
  assert(empty.finish(none) == 0 && std::isnan(none[0]) && std::isnan(none[2]));
  return 0;
}
