#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "AgendaLayout.h"

namespace {

// Rozměry z obrazovky: řádek 20 px + 2 px mezera, hlavička dne 17 px + 7 px.
// Pás je zmenšený na 130 px, aby se přetečení dalo vyrobit pár řádky.
constexpr AgendaLayoutMetrics METRICS{20, 17, 2, 7, 130};

AgendaLayoutResult layout(const std::string &days) {
  // "D" = událost otevírá den, "." = pokračování dne.
  bool startsDay[32] = {};
  for (size_t index = 0; index < days.size(); ++index)
    startsDay[index] = days[index] == 'D';
  return agendaLayout(startsDay, static_cast<uint8_t>(days.size()), METRICS);
}

// Řádek zabere 22 px, řádek s hlavičkou dne 46 px (22 + 17 + 7).
void testEverythingFits() {
  // 46 + 22 + 46 = 114 px z 130.
  const AgendaLayoutResult result = layout("D.D");
  assert(result.visible == 3);
  assert(!result.ellipsis);

  const AgendaLayoutResult empty = layout("");
  assert(empty.visible == 0);
  assert(!empty.ellipsis);
}

void testCompleteLastDayNeedsNoEllipsis() {
  // D... = 112 px, další den by začal hlavičkou (158 px). Poslední zobrazený
  // den je kompletní, takže žádné tečky.
  const AgendaLayoutResult result = layout("D...D.");
  assert(result.visible == 4);
  assert(!result.ellipsis);
}

void testContinuingDayEndsWithEllipsis() {
  // D... = 112 px, pátá událost téhož dne by skončila na 134 px. Den
  // pokračuje, takže čtvrtý řádek nese tři tečky.
  const AgendaLayoutResult result = layout("D.....D.");
  assert(result.visible == 4);
  assert(result.ellipsis);
}

void testDayWithOnlyHeaderLeftIsDropped() {
  // D.. = 90 px, další den i s první událostí = 136 px: nevejde se nic, ani
  // samotná hlavička.
  AgendaLayoutResult result = layout("D..D..");
  assert(result.visible == 3);
  assert(!result.ellipsis);

  // D.D = 114 px, druhý den ale pokračuje. Z něj by zbyla hlavička se třemi
  // tečkami, takže zmizí celý a předchozí (kompletní) den tečky nedostane.
  result = layout("D.D..");
  assert(result.visible == 2);
  assert(!result.ellipsis);
}

void testSingleOverfullDayKeepsEllipsis() {
  // Jediný den, který se nevejde: tečky na posledním řádku, ne prázdný displej.
  AgendaLayoutResult result = layout("D..........");
  assert(result.visible == 4);
  assert(result.ellipsis);

  // Pás, do kterého se vejde jen jedna událost: raději hlavička s tečkami
  // než nic.
  bool startsDay[] = {true, false};
  result = agendaLayout(startsDay, 2, AgendaLayoutMetrics{20, 17, 2, 7, 50});
  assert(result.visible == 1);
  assert(result.ellipsis);
}

std::string abbreviated(const char *name, size_t capacity = 32) {
  char buffer[32];
  assert(capacity <= sizeof(buffer));
  memset(buffer, '#', sizeof(buffer));
  agendaAbbreviateName(name, 4, buffer, capacity);
  return buffer;
}

void testAbbreviation() {
  assert(abbreviated("Neumannovi") == "Neum..");
  // Čtyři znaky, pět bajtů: á se nerozsekne.
  assert(abbreviated("Adámek") == "Adám..");
  assert(abbreviated("Čeněk") == "Čeně..");
  // Co se vejde, zůstane celé a bez teček.
  assert(abbreviated("Jana") == "Jana");
  assert(abbreviated("Já") == "Já");
  assert(abbreviated("") == "");
  // Malý cíl ubírá po celých znacích.
  assert(abbreviated("Čeněk", 6) == "Če..");
  assert(abbreviated("Čeněk", 3) == "");
  char tiny[1] = {'#'};
  assert(agendaAbbreviateName("Jana", 4, tiny, sizeof(tiny)) == 0);
  assert(tiny[0] == '\0');
}

}  // namespace

int main() {
  testEverythingFits();
  testCompleteLastDayNeedsNoEllipsis();
  testContinuingDayEndsWithEllipsis();
  testDayWithOnlyHeaderLeftIsDropped();
  testSingleOverfullDayKeepsEllipsis();
  testAbbreviation();
  printf("test_agenda_layout: vsechny kontroly prosly\n");
  return 0;
}
