#include "MapLabelFont.h"

#include <ctype.h>

#include "MapCanvas.h"

namespace {
uint32_t labelLetter(char character) {
  return static_cast<uint32_t>(toupper(static_cast<unsigned char>(character)));
}
}  // namespace

// Volá se jen z úlohy, které instance patří, takže líná inicializace
// nepotřebuje zámek.
void MapLabelFont::ensureInitialized() {
  if (initialized_) return;
  dsc_ = *static_cast<const lv_font_fmt_txt_dsc_t *>(lv_font_montserrat_12.dsc);
  dsc_.cache = &cache_;
  font_ = lv_font_montserrat_12;
  font_.dsc = &dsc_;
  lv_font_glyph_dsc_t glyph;
  capHeight_ = lv_font_get_glyph_dsc(&font_, &glyph, 'H', 0)
                   ? glyph.box_h + glyph.ofs_y
                   : MAP_CANVAS_GLYPH_HEIGHT;
  initialized_ = true;
}

int MapLabelFont::labelHeight() {
  ensureInitialized();
  return capHeight_ + 6;
}

int MapLabelFont::width(const char *text) {
  ensureInitialized();
  int total = 0;
  for (size_t index = 0; text[index] != '\0'; ++index) {
    lv_font_glyph_dsc_t glyph;
    const uint32_t next =
        text[index + 1] != '\0' ? labelLetter(text[index + 1]) : 0;
    if (lv_font_get_glyph_dsc(&font_, &glyph, labelLetter(text[index]), next))
      total += glyph.adv_w;
  }
  return total;
}

void MapLabelFont::draw(uint16_t *pixels, int x, int y, const char *text,
                        uint16_t color, uint8_t opacity) {
  ensureInitialized();
  const int baseline = y + capHeight_;
  int penX = x;
  for (size_t index = 0; text[index] != '\0'; ++index) {
    const uint32_t letter = labelLetter(text[index]);
    const uint32_t next =
        text[index + 1] != '\0' ? labelLetter(text[index + 1]) : 0;
    lv_font_glyph_dsc_t glyph;
    if (!lv_font_get_glyph_dsc(&font_, &glyph, letter, next)) continue;
    const uint8_t *bitmap =
        glyph.bpp == 4 ? lv_font_get_glyph_bitmap(&font_, letter) : nullptr;
    if (bitmap != nullptr) {
      const int left = penX + glyph.ofs_x;
      const int top = baseline - glyph.ofs_y - glyph.box_h;
      // 4 bity na pixel, řádky za sebou bez zarovnání na celé bajty.
      for (int row = 0; row < glyph.box_h; ++row) {
        for (int column = 0; column < glyph.box_w; ++column) {
          const size_t bit =
              static_cast<size_t>(row * glyph.box_w + column) * 4;
          const uint8_t coverage =
              (bitmap[bit >> 3] >> ((bit & 4) != 0 ? 0 : 4)) & 0x0f;
          if (coverage != 0)
            setMapPixel(pixels, left + column, top + row, color,
                        static_cast<uint8_t>(coverage * opacity / 15));
        }
      }
    }
    penX += glyph.adv_w;
  }
}
