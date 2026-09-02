#pragma once

// Minimální náhrada Arduino API pro překlad firmwarových modulů na počítači.
// Slouží pouze testům v tools/ a nikdy se nepřekládá do firmwaru.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#ifndef constrain
#define constrain(value, low, high) \
  ((value) < (low) ? (low) : ((value) > (high) ? (high) : (value)))
#endif

class String {
 public:
  String() = default;
  String(const char *value) : value_(value == nullptr ? "" : value) {}
  String(const std::string &value) : value_(value) {}
  // Jediný číselný konstruktor, aby String(index) nebyl nejednoznačný pro
  // uint8_t ani size_t.
  explicit String(unsigned long long value) : value_(std::to_string(value)) {}
  const char *c_str() const { return value_.c_str(); }
  size_t length() const { return value_.size(); }
  bool isEmpty() const { return value_.empty(); }
  char operator[](size_t index) const {
    return index < value_.size() ? value_[index] : '\0';
  }
  long toInt() const { return strtol(value_.c_str(), nullptr, 10); }
  bool operator==(const String &other) const { return value_ == other.value_; }
  bool operator!=(const String &other) const { return value_ != other.value_; }
  String &operator+=(const String &other) {
    value_ += other.value_;
    return *this;
  }

 private:
  std::string value_;
};

inline String operator+(const String &left, const String &right) {
  String result = left;
  result += right;
  return result;
}

inline String operator+(const String &left, unsigned long long right) {
  return left + String(right);
}
