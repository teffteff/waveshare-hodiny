#pragma once

// Stub NVS vrstvy. Testy pracují s pamětí v Preferences.h, inicializace
// oddílu proto vždy uspěje.

#include <cstdint>

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;

inline esp_err_t nvs_flash_init_partition(const char *) { return ESP_OK; }
