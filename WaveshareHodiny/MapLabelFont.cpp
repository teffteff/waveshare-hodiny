#include "MapLabelFont.h"

#include <ctype.h>

#include "MapCanvas.h"

namespace {
// Další znak UTF-8 od text[index]; index se posune za něj. Rozbitá sekvence
// dá bajt tak, jak je, aby text nezmizel celý.
uint32_t nextLetter(const char *text, size_t &index, bool uppercase) {
  const uint8_t lead = static_cast<uint8_t>(text[index++]);
  if (lead < 0x80)
    return uppercase ? static_cast<uint32_t>(toupper(lead)) : lead;
  int extra = (lead & 0xE0) == 0xC0 ? 1 : (lead & 0xF0) == 0xE0 ? 2
            : (lead & 0xF8) == 0xF0 ? 3 : 0;
  uint32_t letter = lead & (0x3F >> extra);
  for (int count = 0; count < extra; ++count) {
    const uint8_t next = static_cast<uint8_t>(text[index]);
    if ((next & 0xC0) != 0x80) return lead;
    letter = (letter << 6) | (next & 0x3F);
    ++index;
  }
  return letter;
}

uint32_t peekLetter(const char *text, size_t index, bool uppercase) {
  return text[index] != '\0' ? nextLetter(text, index, uppercase) : 0;
}
}  // namespace

// Volá se jen z úlohy, které instance patří, takže líná inicializace
// nepotřebuje zámek.
void MapLabelFont::ensureInitialized() {
  if (initialized_) return;
  dsc_ = *static_cast<const lv_font_fmt_txt_dsc_t *>(source_->dsc);
  dsc_.cache = &cache_;
  font_ = *source_;
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
  for (size_t index = 0; text[index] != '\0';) {
    const uint32_t letter = nextLetter(text, index, uppercase_);
    const uint32_t next = peekLetter(text, index, uppercase_);
    lv_font_glyph_dsc_t glyph;
    if (lv_font_get_glyph_dsc(&font_, &glyph, letter, next))
      total += glyph.adv_w;
  }
  return total;
}

void MapLabelFont::draw(uint16_t *pixels, int x, int y, const char *text,
                        uint16_t color, uint8_t opacity) {
  ensureInitialized();
  const int baseline = y + capHeight_;
  int penX = x;
  for (size_t index = 0; text[index] != '\0';) {
    const uint32_t letter = nextLetter(text, index, uppercase_);
    const uint32_t next = peekLetter(text, index, uppercase_);
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
