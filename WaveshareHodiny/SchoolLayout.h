#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>

// Svislé rozvržení obrazovky Škola, oddělené od LVGL, aby šlo testovat na
// počítači - stejně jako AgendaLayout.h.
//
// Pás pod hlavičkou dostane nejdřív rozvrh: ten je na obrazovce hlavní a den
// s osmi hodinami se má vejít celý. Pod ním obědy (nejbližší dva dny, kdy se
// vaří, pro školu i školku), pak úkoly z toho, co zbude. Když se všechny úkoly nevejdou,
// poslední viditelný řádek nahradí tři tečky - úkol, který by jen zmizel, je
// horší než upozornění, že jich je víc. Obědy tečky nemají: chybějící řádek
// je zítřek, a ten řekne i prázdné místo. Kam by se vešla jen hlavička sekce
// bez jediného řádku, nekreslí se ani ta.
//
// Jídlo zabírá celý řádek od levého okraje rozvrhu: vedle dne zbývalo na název
// sotva dvě třetiny šířky a delší jídelníček se do řádku nevešel. Den proto
// stojí na vlastním řádku nad jídly toho dne a dlouhý název se zalomí do dvou
// řádků. Kolik řádků které jídlo potřebuje, spočítá volající: jen on umí změřit
// text ve svém fontu.

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
  // Kolik řádků obědů se kreslí; nula znamená ani hlavičku.
  uint8_t meals = 0;
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
// mealRows[i] je, kolik řádků i-té jídlo zabere: řádek se dnem, když ho
// otevírá, plus jeden nebo dva řádky názvu. Bez pole připadá na jídlo řádek.
inline SchoolLayoutResult schoolLayout(uint8_t lessonCount,
                                       uint8_t homeworkCount,
                                       uint8_t homeworkTotal,
                                       bool emptyNotice,
                                       const SchoolLayoutMetrics &metrics,
                                       uint8_t mealCount = 0,
                                       const uint8_t *mealRows = nullptr) {
  SchoolLayoutResult result;
  const int row = metrics.lineHeight + metrics.rowGap;
  int used = metrics.headingHeight + metrics.rowGap;
  while (result.lessons < lessonCount && used + row <= metrics.blockHeight) {
    used += row;
    ++result.lessons;
  }
  if (mealCount > 0) {
    int mealsUsed =
        used + metrics.sectionGap + metrics.headingHeight + metrics.rowGap;
    while (result.meals < mealCount) {
      const int rows = mealRows != nullptr ? mealRows[result.meals] : 1;
      const int needed = (rows > 0 ? rows : 1) * row;
      if (mealsUsed + needed > metrics.blockHeight) break;
      mealsUsed += needed;
      ++result.meals;
    }
    if (result.meals > 0) used = mealsUsed;
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

// Kolik jídel na začátku seznamu se přeskočí: po nastavené hodině je dnešní
// oběd dávno snědený a místo pod rozvrhem patří zítřku. Server posílá jídla
// seřazená ode dneška, takže stačí zahodit ta, která nesou `today`.
//
// `hour` je hodina přepnutí z nastavení; nula znamená nepřepínat (přepnutí
// o půlnoci je totéž co nepřepínat, den se tam mění sám). `minuteOfDay` je
// místní čas v minutách, nebo -1, když hodiny čas ještě neznají - dokud ho
// neznají, nezahazuje se nic.
inline size_t schoolMealsHiddenByHour(const bool *today, size_t count,
                                      int minuteOfDay, uint8_t hour) {
  if (today == nullptr || hour == 0 || hour > 23) return 0;
  if (minuteOfDay < 0 || minuteOfDay < hour * 60) return 0;
  size_t hidden = 0;
  while (hidden < count && today[hidden]) ++hidden;
  return hidden;
}

// Levý okraj řádku na kruhovém displeji. Pás druhé stránky sahá od horního
// okraje kruhu k dolnímu, takže nahoře a dole je kruh užší než uprostřed.
// Řádek se posune doleva až na `target` (levý okraj sloupců rozvrhu, aby obě
// stránky začínaly na stejné svislici), ale jen tam, kam se celý vejde;
// u horního a dolního okraje zůstane o pár pixelů vpravo. `top` je horní
// hrana řádku vůči středu kruhu, `height` jeho výška.
inline int schoolRowLeft(int radius, int target, int top, int height,
                         int margin = 1) {
  const auto halfChord = [radius](int y) {
    const int square = radius * radius - y * y;
    return square <= 0 ? 0 : static_cast<int>(sqrt(static_cast<double>(square)));
  };
  // Užší ze dvou hran řádku: nad středem kroužek svírá horní, pod ním dolní.
  const int top_half = halfChord(top);
  const int bottom_half = halfChord(top + height);
  int limit = (top_half < bottom_half ? top_half : bottom_half) - margin;
  // Řádek mimo kruh: nesahat doleva vůbec, radši nic než mimo displej.
  if (limit < 0) limit = 0;
  return -limit < target ? target : -limit;
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
