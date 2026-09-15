#pragma once

#include <stdint.h>

// Svislé rozvržení obrazovky Škola, oddělené od LVGL, aby šlo testovat na
// počítači - stejně jako AgendaLayout.h.
//
// Pás pod hlavičkou dostane nejdřív rozvrh: ten je na obrazovce hlavní a den
// s osmi hodinami se má vejít celý. Úkoly dostanou, co zbude. Když se všechny
// nevejdou, poslední viditelný řádek nahradí tři tečky - úkol, který by jen
// zmizel, je horší než upozornění, že jich je víc. Kam by se vešla jen
// hlavička úkolů bez jediného řádku, nekreslí se ani ta.

struct SchoolLayoutMetrics {
  int lineHeight;
  int headingHeight;
  int rowGap;
  // Mezera nad hlavičkou úkolů. Nad hlavičkou rozvrhu žádná není, ta stojí
  // na horním okraji pásu.
  int sectionGap;
  int blockHeight;
};

struct SchoolLayoutResult {
  uint8_t lessons = 0;
  // Kolik řádků úkolů se kreslí, včetně řádku se třemi tečkami.
  uint8_t homework = 0;
  bool homeworkEllipsis = false;
  // Úkoly jsou zapnuté, ale žádné nejsou: místo nich jediná hlavička
  // "ŽÁDNÉ ÚKOLY". Když se pod rozvrh nevejde, nekreslí se.
  bool homeworkEmpty = false;
};

// Hlavička dne se kreslí vždycky: i o prázdninách nese "ŽÁDNÉ VYUČOVÁNÍ".
// homeworkCount je, kolik úkolů hodiny mají uložených; homeworkTotal, kolik
// jich server poslal. Tečky přijdou, když se nevejdou všechny uložené, nebo
// když server poslal víc, než se uložilo. emptyNotice říká, že se prázdné
// úkoly mají ohlásit hlavičkou; bez ní se sekce úkolů vynechá.
inline SchoolLayoutResult schoolLayout(uint8_t lessonCount,
                                       uint8_t homeworkCount,
                                       uint8_t homeworkTotal,
                                       bool emptyNotice,
                                       const SchoolLayoutMetrics &metrics) {
  SchoolLayoutResult result;
  const int row = metrics.lineHeight + metrics.rowGap;
  int used = metrics.headingHeight + metrics.rowGap;
  while (result.lessons < lessonCount && used + row <= metrics.blockHeight) {
    used += row;
    ++result.lessons;
  }
  if (homeworkCount == 0) {
    result.homeworkEmpty =
        emptyNotice && used + metrics.sectionGap + metrics.headingHeight <=
                           metrics.blockHeight;
    return result;
  }

  used += metrics.sectionGap + metrics.headingHeight + metrics.rowGap;
  uint8_t fit = 0;
  while (fit < homeworkCount && used + row <= metrics.blockHeight) {
    used += row;
    ++fit;
  }
  // Samotná hlavička bez úkolu by nic neřekla.
  if (fit == 0) return result;
  result.homework = fit;
  // I jediný řádek se třemi tečkami řekne víc než nic: dítě pozná, že nějaké
  // úkoly má, a podívá se do aplikace.
  result.homeworkEllipsis = fit < homeworkCount || fit < homeworkTotal;
  return result;
}

// Druhá stránka obrazovky Škola: zprávy nahoře, známky pod nimi, každá sekce
// s hlavičkou. Prázdná sekce má jen hlavičku ("ŽÁDNÉ NOVÉ ZPRÁVY"), vypnutá
// (server data neposílá) se vynechá. Zprávy dostanou řádky první, ale nechají
// známkám hlavičku a až dva řádky, aby je úplně nevytlačily. Tečky nahradí
// poslední řádek sekce, když se všechny položky nevejdou.
struct SchoolListsResult {
  bool firstHeading = false;
  uint8_t first = 0;
  bool firstEllipsis = false;
  bool secondHeading = false;
  uint8_t second = 0;
  bool secondEllipsis = false;
};

inline SchoolListsResult schoolListsLayout(bool firstEnabled,
                                           uint8_t firstCount,
                                           uint8_t firstTotal,
                                           bool secondEnabled,
                                           uint8_t secondCount,
                                           uint8_t secondTotal,
                                           const SchoolLayoutMetrics &metrics) {
  SchoolListsResult result;
  const int row = metrics.lineHeight + metrics.rowGap;
  const auto heading = [&](uint8_t count) {
    return metrics.headingHeight + (count > 0 ? metrics.rowGap : 0);
  };
  constexpr uint8_t SECOND_RESERVED_ROWS = 2;
  int reserve = 0;
  if (secondEnabled) {
    const uint8_t rows =
        secondCount < SECOND_RESERVED_ROWS ? secondCount : SECOND_RESERVED_ROWS;
    reserve = (firstEnabled ? metrics.sectionGap : 0) + heading(secondCount) +
              rows * row;
  }
  int used = 0;
  if (firstEnabled && metrics.headingHeight <= metrics.blockHeight) {
    result.firstHeading = true;
    used = heading(firstCount);
    while (result.first < firstCount &&
           used + row + reserve <= metrics.blockHeight) {
      used += row;
      ++result.first;
    }
    const uint8_t total = firstTotal > firstCount ? firstTotal : firstCount;
    result.firstEllipsis = result.first > 0 && result.first < total;
  }
  if (secondEnabled) {
    const int start = used + (result.firstHeading ? metrics.sectionGap : 0);
    if (start + metrics.headingHeight <= metrics.blockHeight) {
      result.secondHeading = true;
      used = start + heading(secondCount);
      while (result.second < secondCount && used + row <= metrics.blockHeight) {
        used += row;
        ++result.second;
      }
      const uint8_t total =
          secondTotal > secondCount ? secondTotal : secondCount;
      result.secondEllipsis = result.second > 0 && result.second < total;
    }
  }
  return result;
}
