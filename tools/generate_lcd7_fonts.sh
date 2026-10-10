#!/usr/bin/env bash
# Písma Barlow pro rozhraní 7" displeje (800 x 480). Kulatý 2,1" je nepoužívá
# a linker je z jeho obrazu vypustí, protože na ně nic neodkazuje.
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"
FONTS="assets/fonts/barlow"
OUT="WaveshareHodiny"
CONV=(npx -y lv_font_conv@1.5.3)

# Text: ASCII, čeština, ° ² ³ µ · – „ “ …. Barlow nemá dolní index ₂ (CO₂),
# ten doplní záložní písmo clock_czech (Montserrat).
TEXT_RANGE="0x20-0x7E,0xB0,0xB2,0xB3,0xB5,0xB7,0xC1,0xC9,0xCD,0xD3,0xDA,0xDD,0xE1,0xE9,0xED,0xF3,0xFA,0xFD,0x10C,0x10D,0x10E,0x10F,0x11A,0x11B,0x147,0x148,0x158,0x159,0x160,0x161,0x164,0x165,0x16E,0x16F,0x17D,0x17E,0x2013,0x201C,0x201E,0x2026"
# Velká čísla: číslice, desetinná čárka, mínus a pomlčky pro chybějící hodnotu.
NUMBER_SYMBOLS="0123456789,.:-–"

font() {
  local name="$1" weight="$2" size="$3" fallback="$4"
  shift 4
  local file="$OUT/Lcd7Font${name}.c"
  "${CONV[@]}" --size "$size" --bpp 4 --format lvgl --no-compress \
    --font "$FONTS/Barlow-$weight.ttf" "$@" \
    --lv-font-name "lcd7_$(echo "$name" | tr '[:upper:]' '[:lower:]')" -o "$file"
  # Arduino knihovna lvgl se vkládá jako "lvgl.h" (stejně jako u ostatních písem).
  sed -i '' 's|#include "lvgl/lvgl.h"|#include "lvgl.h"|' "$file"
  if [[ -n "$fallback" ]]; then
    sed -i '' "s/\.fallback = NULL,/.fallback = \&$fallback,/" "$file"
    sed -i '' "s|^#include \"lvgl.h\"\$|#include \"lvgl.h\"\\
#include \"ClockFonts.h\"|" "$file"
  fi
}

font Time176 SemiBold 176 "" --symbols "0123456789:" --no-kerning
font Number72 Medium 72 "" --symbols "$NUMBER_SYMBOLS"
font Number44 Medium 44 "" --symbols "$NUMBER_SYMBOLS"
font Number38 Medium 38 "" --symbols "$NUMBER_SYMBOLS"
font Text28 Medium 28 clock_czech_20 -r "$TEXT_RANGE"
font Text22 Medium 22 clock_czech_20 -r "$TEXT_RANGE"
font Text18 Medium 18 clock_czech_16 -r "$TEXT_RANGE"
font Text15 Medium 15 clock_czech_14 -r "$TEXT_RANGE"
