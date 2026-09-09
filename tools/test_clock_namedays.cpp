#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "ClockNamedays.h"

namespace {

// Kolik dní má který měsíc v pevném kalendáři jmenin. Únor má 29, protože
// Horymír na přestupný den patří a v přestupném roce se musí najít.
const int DAYS_IN_MONTH[13] = {0, 31, 29, 31, 30, 31, 30,
                               31, 31, 30, 31, 30, 31};

// Znaky, které projektová písma opravdu obsahují. Odpovídá parametru -r
// v hlavičce ClockCzechFont16.c; kdyby tabulku někdo přegeneroval a přinesl
// jméno mimo tenhle rozsah, LVGL by místo písmene nakreslil obdélníček.
std::set<uint32_t> supportedCodePoints() {
  std::set<uint32_t> allowed;
  for (uint32_t value = 0x20; value <= 0x7E; ++value) allowed.insert(value);
  for (uint32_t value : {0x00B0u, 0x00B3u, 0x00B5u, 0x00C1u, 0x00C9u, 0x00CDu,
                         0x00D3u, 0x00DAu, 0x00DDu, 0x00E1u, 0x00E9u, 0x00EDu,
                         0x00F3u, 0x00FAu, 0x00FDu, 0x010Cu, 0x010Du, 0x010Eu,
                         0x010Fu, 0x011Au, 0x011Bu, 0x0147u, 0x0148u, 0x0158u,
                         0x0159u, 0x0160u, 0x0161u, 0x0164u, 0x0165u, 0x016Eu,
                         0x016Fu, 0x017Du, 0x017Eu, 0x2082u}) {
    allowed.insert(value);
  }
  return allowed;
}

// Minimální dekodér UTF-8. Tabulka je zdrojový soubor projektu, takže vstup je
// vždy platný; neplatná sekvence tady rovnou shodí test.
std::vector<uint32_t> decodeUtf8(const char *text) {
  std::vector<uint32_t> points;
  const unsigned char *cursor = reinterpret_cast<const unsigned char *>(text);
  while (*cursor != 0) {
    uint32_t value = 0;
    int extra = 0;
    if (*cursor < 0x80) {
      value = *cursor;
    } else if ((*cursor & 0xE0) == 0xC0) {
      value = *cursor & 0x1F;
      extra = 1;
    } else if ((*cursor & 0xF0) == 0xE0) {
      value = *cursor & 0x0F;
      extra = 2;
    } else if ((*cursor & 0xF8) == 0xF0) {
      value = *cursor & 0x07;
      extra = 3;
    } else {
      assert(false && "neplatny vodici bajt UTF-8");
    }
    ++cursor;
    for (int index = 0; index < extra; ++index) {
      assert((*cursor & 0xC0) == 0x80 && "neplatny pokracovaci bajt UTF-8");
      value = (value << 6) | (*cursor & 0x3F);
      ++cursor;
    }
    points.push_back(value);
  }
  return points;
}

void testBounds() {
  // Mimo kalendář se nesmí sáhnout do pole ani vrátit smetí.
  assert(czechNamedayFor(0, 1) == nullptr);
  assert(czechNamedayFor(13, 1) == nullptr);
  assert(czechNamedayFor(-1, 1) == nullptr);
  assert(czechNamedayFor(1, 0) == nullptr);
  assert(czechNamedayFor(1, 32) == nullptr);
  assert(czechNamedayFor(1, -5) == nullptr);

  // Den, který v měsíci neexistuje, jmeniny nemá.
  for (int month = 1; month <= 12; ++month) {
    for (int day = DAYS_IN_MONTH[month] + 1; day <= 31; ++day) {
      assert(czechNamedayFor(month, day) == nullptr);
    }
  }
}

void testKnownDays() {
  // Kotvy, podle kterých se pozná posun tabulky o den.
  assert(czechNamedayFor(1, 1) == nullptr);  // Nový rok jmeniny nemá
  assert(strcmp(czechNamedayFor(1, 2), "KARINA") == 0);
  assert(strcmp(czechNamedayFor(2, 29), "HORYMÍR") == 0);
  assert(czechNamedayFor(5, 1) == nullptr);  // Svátek práce jmeniny nemá
  assert(strcmp(czechNamedayFor(7, 5), "CYRIL, METODĚJ") == 0);
  assert(strcmp(czechNamedayFor(9, 28), "VÁCLAV, VÁCLAVA") == 0);
  assert(strcmp(czechNamedayFor(12, 24), "ADAM, EVA") == 0);
  assert(czechNamedayFor(12, 25) == nullptr);  // Boží hod jmeniny nemá
  assert(strcmp(czechNamedayFor(12, 26), "ŠTĚPÁN") == 0);
  assert(strcmp(czechNamedayFor(12, 31), "SILVESTR") == 0);
}

void testFontCoverage() {
  const std::set<uint32_t> allowed = supportedCodePoints();
  int named = 0;
  for (int month = 1; month <= 12; ++month) {
    for (int day = 1; day <= DAYS_IN_MONTH[month]; ++day) {
      const char *nameday = czechNamedayFor(month, day);
      if (nameday == nullptr) continue;
      ++named;
      // Prázdný řetězec by na displeji vypadal jako chybějící jméno, ale
      // choval se jako vyplněné; do tabulky patří nullptr.
      assert(nameday[0] != '\0');
      for (uint32_t codePoint : decodeUtf8(nameday)) {
        if (allowed.count(codePoint) != 0) continue;
        std::printf("\n    %d. %d. obsahuje U+%04X mimo rozsah písma: %s\n",
                    day, month, codePoint, nameday);
        assert(false && "jmeno obsahuje znak, ktery pismo clock_czech nema");
      }
      // Malá písmena font sice má, ale jména jsou schválně velkými, aby
      // diakritika nechyběla. Kontrola drží tabulku v jednom tvaru.
      for (const char *cursor = nameday; *cursor != '\0'; ++cursor) {
        assert(!(*cursor >= 'a' && *cursor <= 'z') &&
               "jmena patri do tabulky velkymi pismeny");
      }
    }
  }
  // Kalendář má 366 dní a jen hrstka jich zůstává bez jmen; výrazně nižší
  // počet by znamenal useknutou tabulku.
  assert(named > 350);
}

}  // namespace

int main() {
  testBounds();
  testKnownDays();
  testFontCoverage();
  std::printf("clock_namedays OK\n");
  return 0;
}
