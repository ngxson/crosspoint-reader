#include "EpdFontFamily.h"
#include <cassert>
#include <cstring>
#include <Arduino.h>

// note: this is a hack, just for demo
struct Assets {
  size_t baseOffset = 0;
  uint8_t buffer[1024];
  void begin() {
    if (baseOffset == 0) {
      baseOffset = ESP.getSketchSize() + 0x10000;
    }
  }
  uint8_t* read(size_t offset, size_t length) {
    begin();
    assert(length <= sizeof(buffer));
    // maybe using mmap in the future; for now, this is enough for a demo
    ESP.flashRead(baseOffset + offset, (uint32_t*)buffer, length);
    return buffer;
  }
};

static Assets ASSETS;

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
    return ASSETS.read(data->assetsOffset + offset, glyph->dataLength);
  } else {
    return &data->bitmap[offset];
  }
}

const EpdGlyph* EpdFontFamily::getGlyph(const uint32_t cp, const Style style) const {
  return getFont(style)->getGlyph(cp);
};
