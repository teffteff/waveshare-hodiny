#pragma once

#include <stddef.h>
#include <stdint.h>

// Rozvržení obrazovky agendy, oddělené od LVGL, aby šlo testovat na počítači -
// stejně jako WeatherForecastLayout.h vedle předpovědi.
//
// Agenda plní pás mezi hlavičkou a legendou, dokud je místo. Když se všechno
// nevejde, platí dvě pravidla:
//  - Pokračuje-li poslední zobrazený den za okrajem, jeho poslední řádek se
//    nahradí třemi tečkami, aby bylo vidět, že tam něco je.
//  - Den, ze kterého by zbyla jen hlavička (a tři tečky), se nezobrazí vůbec.
//    Hlavička sama nic neříká a zabírá místo, které patří předchozímu dni.

struct AgendaLayoutMetrics {
  int lineHeight;
  int dayHeight;
  // Mezera pod každým řádkem a nad každou hlavičkou dne.
  int rowGap;
  int dayGap;
  int blockHeight;
};

struct AgendaLayoutResult {
  // Kolik řádků se kreslí, včetně řádku se třemi tečkami.
  uint8_t visible = 0;
  // Poslední kreslený řádek nese místo události tři tečky.
  bool ellipsis = false;
};

// startsDay[i] říká, že událost i otevírá nový den a má nad sebou hlavičku.
inline AgendaLayoutResult agendaLayout(const bool *startsDay, uint8_t count,
                                       const AgendaLayoutMetrics &metrics) {
  AgendaLayoutResult result;
  int total = 0;
  uint8_t fit = 0;
  while (fit < count) {
    int next = total + metrics.lineHeight + metrics.rowGap;
    if (startsDay[fit]) next += metrics.dayHeight + metrics.dayGap;
    if (next > metrics.blockHeight) break;
    total = next;
    ++fit;
  }
  result.visible = fit;
  // Vešlo se všechno, nebo první nevešlá událost začíná nový den - poslední
  // zobrazený den je tedy celý.
  if (fit == count || fit == 0 || startsDay[fit]) return result;

  const uint8_t last = fit - 1;
  if (startsDay[last] && last > 0) {
    // Z posledního dne by zbyla hlavička se třemi tečkami. Zahodí se celý;
    // den před ním skončil, protože tenhle začal.
    result.visible = last;
    return result;
  }
  // Tři tečky místo poslední události. Výjimkou je jediný den na celé
  // obrazovce: ten zůstane i s tečkami, jinak by displej zůstal prázdný.
  result.ellipsis = true;
  return result;
}

// Zkrácené jméno kalendáře do legendy: prvních `characters` znaků a "..".
// Počítá znaky, ne bajty, takže "Adámek" dá "Adám.." a vícebajtový znak se
// nikdy nerozsekne. Jméno, které se do limitu vejde celé, zůstane beze změny.
// Vrací délku zapsaného řetězce; do destination zapíše vždy platný řetězec.
inline size_t agendaAbbreviateName(const char *name, size_t characters,
                                   char *destination, size_t destinationSize) {
  if (destination == nullptr || destinationSize == 0) return 0;
  destination[0] = '\0';
  if (name == nullptr) return 0;

  size_t cut = 0;
  size_t counted = 0;
  const char *cursor = name;
  while (*cursor != '\0') {
    // Pokračovací bajty UTF-8 (10xxxxxx) patří k předchozímu znaku.
    if ((static_cast<unsigned char>(*cursor) & 0xC0) != 0x80) {
      if (counted == characters) break;
      ++counted;
    }
    ++cursor;
    cut = static_cast<size_t>(cursor - name);
  }
  const bool truncated = *cursor != '\0';
  const size_t suffix = truncated ? 2 : 0;
  // Když se do cíle nevejde ani zkrácené jméno, ubírá se po celých znacích.
  while (cut > 0 && cut + suffix + 1 > destinationSize) {
    do {
      --cut;
    } while (cut > 0 &&
             (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80);
  }
  for (size_t index = 0; index < cut; ++index) destination[index] = name[index];
  size_t length = cut;
  // Samotné tečky bez jediného znaku jména by legendě nic neřekly.
  if (truncated && length > 0 && length + suffix + 1 <= destinationSize) {
    destination[length++] = '.';
    destination[length++] = '.';
  }
  destination[length] = '\0';
  return length;
}
