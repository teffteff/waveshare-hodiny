#pragma once

#include <stddef.h>
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

// Druhá stránka obrazovky Škola: zprávy, pod nimi známky a nástěnka školky,
// každá sekce s hlavičkou. Prázdná sekce má jen hlavičku ("ŽÁDNÉ NOVÉ
// ZPRÁVY"), vypnutá (server data neposílá) se vynechá. Vyšší sekce dostane
// řádky dřív, ale každé zapnuté pod sebou nechá hlavičku a až dva řádky, aby
// ji úplně nevytlačila. Tečky nahradí poslední řádek sekce, když se všechny
// položky nevejdou.
constexpr size_t SCHOOL_LIST_SECTIONS = 3;

struct SchoolListInput {
  bool enabled = false;
  uint8_t count = 0;
  // Položek celkem podle serveru, i těch, které hodiny neuložily.
  uint8_t total = 0;
};

struct SchoolListResult {
  bool heading = false;
  uint8_t rows = 0;
  bool ellipsis = false;
};

struct SchoolListsResult {
  SchoolListResult sections[SCHOOL_LIST_SECTIONS];
};

inline SchoolListsResult schoolListsLayout(
    const SchoolListInput sections[SCHOOL_LIST_SECTIONS],
    const SchoolLayoutMetrics &metrics) {
  SchoolListsResult result;
  const int row = metrics.lineHeight + metrics.rowGap;
  const auto heading = [&](uint8_t count) {
    return metrics.headingHeight + (count > 0 ? metrics.rowGap : 0);
  };
  constexpr uint8_t RESERVED_ROWS = 2;
  int used = 0;
  bool anyHeading = false;
  for (size_t index = 0; index < SCHOOL_LIST_SECTIONS; ++index) {
    const SchoolListInput &section = sections[index];
    if (!section.enabled) continue;
    const int start = used + (anyHeading ? metrics.sectionGap : 0);
    if (start + metrics.headingHeight > metrics.blockHeight) break;
    // Místo pro hlavičky a první řádky zapnutých sekcí níž.
    int reserve = 0;
    for (size_t later = index + 1; later < SCHOOL_LIST_SECTIONS; ++later) {
      const SchoolListInput &below = sections[later];
      if (!below.enabled) continue;
      const uint8_t rows =
          below.count < RESERVED_ROWS ? below.count : RESERVED_ROWS;
      reserve += metrics.sectionGap + heading(below.count) + rows * row;
    }
    SchoolListResult &shown = result.sections[index];
    shown.heading = anyHeading = true;
    used = start + heading(section.count);
    while (shown.rows < section.count &&
           used + row + reserve <= metrics.blockHeight) {
      used += row;
      ++shown.rows;
    }
    const uint8_t total =
        section.total > section.count ? section.total : section.count;
    shown.ellipsis = shown.rows > 0 && shown.rows < total;
  }
  return result;
}
