#pragma once

#include <Arduino.h>

struct Rgbw {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t w;

  constexpr Rgbw(uint8_t red = 0, uint8_t green = 0, uint8_t blue = 0, uint8_t white = 0) : r(red), g(green), b(blue), w(white) {}
};

inline Rgbw scaledColor(const Rgbw &c, uint16_t scale256) {
  const bool pureWhite = c.w == 255;
  return {
      static_cast<uint8_t>(pureWhite ? 0 : min<uint32_t>(255, (static_cast<uint32_t>(c.r) * scale256) >> 8)),
      static_cast<uint8_t>(pureWhite ? 0 : min<uint32_t>(255, (static_cast<uint32_t>(c.g) * scale256) >> 8)),
      static_cast<uint8_t>(pureWhite ? 0 : min<uint32_t>(255, (static_cast<uint32_t>(c.b) * scale256) >> 8)),
      static_cast<uint8_t>(min<uint32_t>(255, (static_cast<uint32_t>(c.w) * scale256) >> 8)),
  };
}
