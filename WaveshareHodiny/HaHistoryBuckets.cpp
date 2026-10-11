#include "HaHistoryBuckets.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace {
int64_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(dayOfEra) - 719468;
}

// Hodnota klíče "key":"..." v objektu, bez uvozovek.
bool stringField(const char *object, const char *key, char *output, size_t capacity) {
  const char *found = strstr(object, key);
  if (found == nullptr) return false;
  const char *value = found + strlen(key);
  const char *end = strchr(value, '"');
  if (end == nullptr || static_cast<size_t>(end - value) >= capacity) return false;
  memcpy(output, value, end - value);
  output[end - value] = '\0';
  return true;
}
}  // namespace

bool haParseTimestamp(const char *text, int64_t &timestamp) {
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  if (strlen(text) < 19 || text[4] != '-' || text[7] != '-' || text[10] != 'T')
    return false;
  year = atoi(text);
  month = atoi(text + 5);
  day = atoi(text + 8);
  hour = atoi(text + 11);
  minute = atoi(text + 14);
  second = atoi(text + 17);
  if (month < 1 || month > 12 || day < 1 || day > 31) return false;
  const char *zone = text + 19;
  if (*zone == '.')
    while (*++zone >= '0' && *zone <= '9') {
    }
  int offset = 0;
  if (*zone == '+' || *zone == '-') {
    if (strlen(zone) < 6) return false;
    offset = (atoi(zone + 1) * 60 + atoi(zone + 4)) * 60;
    if (*zone == '-') offset = -offset;
  } else if (*zone != 'Z' && *zone != '\0') {
    return false;
  }
  timestamp = daysFromCivil(year, month, day) * 86400 + hour * 3600 + minute * 60 +
              second - offset;
  return true;
}

HaHistoryBuckets::HaHistoryBuckets(int64_t start, int64_t step, size_t count)
    : start_(start),
      step_(step > 0 ? step : 1),
      count_(count < MAX_BUCKETS ? count : MAX_BUCKETS),
      current_(NAN),
      rangeLow_(NAN),
      rangeHigh_(NAN) {
  for (float &value : values_) value = NAN;
}

void HaHistoryBuckets::feed(const char *data, size_t length) {
  for (size_t index = 0; index < length; ++index) {
    const char c = data[index];
    if (inObject_) {
      if (objectLength_ + 1 < sizeof(object_))
        object_[objectLength_++] = c;
      else
        overflow_ = true;
    }
    if (inString_) {
      if (escape_)
        escape_ = false;
      else if (c == '\\')
        escape_ = true;
      else if (c == '"')
        inString_ = false;
      continue;
    }
    if (c == '"') {
      inString_ = true;
    } else if (c == '{' && !inObject_) {
      inObject_ = true;
      overflow_ = false;
      object_[0] = '{';
      objectLength_ = 1;
    } else if (c == '}' && inObject_) {
      object_[objectLength_] = '\0';
      inObject_ = false;
      if (!overflow_) parseObject();
    }
  }
}

void HaHistoryBuckets::parseObject() {
  char state[32];
  char when[40];
  if (!stringField(object_, "\"state\":\"", state, sizeof(state))) return;
  if (!stringField(object_, "\"last_changed\":\"", when, sizeof(when)) &&
      !stringField(object_, "\"last_updated\":\"", when, sizeof(when)))
    return;
  int64_t time = 0;
  if (!haParseTimestamp(when, time)) return;
  char *end = nullptr;
  float value = strtof(state, &end);
  if (end == state || *end != '\0' || !isfinite(value)) value = NAN;
  record(time, value);
}

void HaHistoryBuckets::record(int64_t time, float value) {
  ++records_;
  if (time >= start_) {
    const int64_t bucket = (time - start_) / step_;
    // Úseky, které skončily před tímto záznamem, mají stav dosavadní.
    while (static_cast<int64_t>(nextFill_) < bucket && nextFill_ < count_)
      values_[nextFill_++] = current_;
  }
  if (time >= rangeFrom_) {
    // Stav platný na začátku rozsahu trvá až do první změny po něm.
    if (!rangeOpened_) includeInRange(current_);
    rangeOpened_ = true;
    includeInRange(value);
  }
  current_ = value;
}

void HaHistoryBuckets::includeInRange(float value) {
  if (isnan(value)) return;
  rangeLow_ = isnan(rangeLow_) || value < rangeLow_ ? value : rangeLow_;
  rangeHigh_ = isnan(rangeHigh_) || value > rangeHigh_ ? value : rangeHigh_;
}

size_t HaHistoryBuckets::finish(float *output) {
  if (!rangeOpened_ && rangeFrom_ != INT64_MAX) includeInRange(current_);
  rangeOpened_ = true;
  while (nextFill_ < count_) values_[nextFill_++] = current_;
  memcpy(output, values_, count_ * sizeof(float));
  return records_;
}
