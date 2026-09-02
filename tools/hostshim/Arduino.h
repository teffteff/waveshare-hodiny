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
  const char *c_str() const { return value_.c_str(); }
  size_t length() const { return value_.size(); }
  bool operator==(const String &other) const { return value_ == other.value_; }

 private:
  std::string value_;
};
