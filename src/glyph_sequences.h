#pragma once

#include <Arduino.h>

struct GlyphSequenceDefinition {
  uint16_t offset;
  uint8_t length;
};

extern const uint8_t GLYPH_SEQUENCE_IDS[];
extern const GlyphSequenceDefinition GLYPH_SEQUENCES[];
extern const size_t GLYPH_SEQUENCE_COUNT;
