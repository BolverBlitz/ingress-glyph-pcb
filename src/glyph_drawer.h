#pragma once

#include "rgbw.h"

struct LedPoint {
  int16_t x;
  int16_t y;
};

extern const LedPoint LED_POSITIONS[69];

class GlyphDrawer {
 public:
  void draw(const char *encodedPattern, const Rgbw &lineColor, const Rgbw &coreColor, Rgbw *output) const;

 private:
  static int8_t decodeNode(char c);
  static void drawSegment(uint8_t a, uint8_t b, const Rgbw &color, Rgbw *output);
};

