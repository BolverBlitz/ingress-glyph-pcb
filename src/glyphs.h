#pragma once

#include <Arduino.h>

struct GlyphDefinition {
  const char *name;
  const char *pattern;
};

extern const GlyphDefinition GLYPHS[];
extern const size_t GLYPH_COUNT;

