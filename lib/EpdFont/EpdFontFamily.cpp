#include "EpdFontFamily.h"
#include <cassert>
#include <cstring>

const EpdFont* EpdFontFamily::getFont(const Style style) const {
  // Extract font style bits (ignore UNDERLINE bit for font selection)
  const bool hasBold = (style & BOLD) != 0;
  const bool hasItalic = (style & ITALIC) != 0;

  if (hasBold && hasItalic) {
    if (boldItalic) return boldItalic;
    if (bold) return bold;
    if (italic) return italic;
  } else if (hasBold && bold) {
    return bold;
  } else if (hasItalic && italic) {
    return italic;
  }

  return regular;
}

void EpdFontFamily::getTextDimensions(const char* string, int* w, int* h, const Style style) const {
  getFont(style)->getTextDimensions(string, w, h);
}

bool EpdFontFamily::hasPrintableChars(const char* string, const Style style) const {
  return getFont(style)->hasPrintableChars(string);
}

const EpdFontData* EpdFontFamily::getData(const Style style) const { return getFont(style)->data; }

const uint8_t* EpdFontFamily::getBitmap(const EpdGlyph* glyph, Style style) const {
  const uint32_t offset = glyph->dataOffset;
  const auto* data = getData(style);
  if (data->assetsOffset) {
    // TODO
    return &data->bitmap[offset];
  } else {
    return &data->bitmap[offset];
  }
}

const EpdGlyph* EpdFontFamily::getGlyph(const uint32_t cp, const Style style) const {
  return getFont(style)->getGlyph(cp);
};
