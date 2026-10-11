#pragma once

#include <stddef.h>
#include <stdint.h>

// Průběh entity Home Assistantu za posledních 24 h z /api/history/period
// (minimal_response, no_attributes) rozdělený do stejně dlouhých úseků.
// Odpověď se čte po kouscích, jak přichází ze sítě - u čidla, které se mění
// každou minutu, má desítky kilobajtů a celá by se do paměti nevešla.
//
// Hodnota úseku je stav platný na jeho KONCI: HA posílá jen změny, takže
// stav trvá, dokud ho nevystřídá další. Stav, který není číslo
// ("unavailable"), dává mezeru (NAN).
class HaHistoryBuckets {
 public:
  static constexpr size_t MAX_BUCKETS = 96;

  // start: začátek prvního úseku (Unix sekundy), step: délka úseku.
  HaHistoryBuckets(int64_t start, int64_t step, size_t count);

  void feed(const char *data, size_t length);
  // Doplní úseky do konce stavem platným naposledy a zapíše count hodnot
  // od nejstarší. Vrací počet přečtených záznamů s časem.
  size_t finish(float *output);

  // Nejnižší a nejvyšší stav od "from" (Unix sekundy), včetně stavu, který
  // v tu chvíli platil; počítá všechny záznamy, ne jen konce úseků - jako
  // min/max od půlnoci v Home Assistantu. Nastavit před feed(), číst po
  // finish(); NAN, když číslo není.
  void setRangeFrom(int64_t from) { rangeFrom_ = from; }
  float rangeLow() const { return rangeLow_; }
  float rangeHigh() const { return rangeHigh_; }

 private:
  void parseObject();
  void record(int64_t time, float value);
  void includeInRange(float value);

  int64_t start_;
  int64_t step_;
  size_t count_;
  float values_[MAX_BUCKETS];
  size_t nextFill_ = 0;
  float current_;
  size_t records_ = 0;
  int64_t rangeFrom_ = INT64_MAX;
  bool rangeOpened_ = false;
  float rangeLow_;
  float rangeHigh_;
  // Rozebíraný objekt {...}; objekty bez atributů mají kolem 150 B.
  char object_[384];
  size_t objectLength_ = 0;
  bool inObject_ = false;
  bool overflow_ = false;
  bool inString_ = false;
  bool escape_ = false;
};

// "2026-10-10T10:00:00.123456+00:00" nebo "...Z" na Unix sekundy.
bool haParseTimestamp(const char *text, int64_t &timestamp);
