#pragma once

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"

// Společná část RGB panelu pro obě desky: vytvoření panelu s bounce buffery,
// předávání framebufferu LVGL a hlídání posunu obrazu. Ovladač konkrétní
// desky jen připraví konfiguraci (piny, časování) a panel zapne.

extern esp_lcd_panel_handle_t panel_handle;

// Vytvoří panel, zaregistruje callbacky pro hlídání fáze a panel inicializuje.
void rgbPanelCreate(const esp_lcd_rgb_panel_config_t &config);
