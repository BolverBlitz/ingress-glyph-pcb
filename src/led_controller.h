#pragma once

#include <Adafruit_NeoPixel.h>

#include "config.h"
#include "rgbw.h"

class LedController {
 public:
  LedController();
  void begin();
  uint8_t pin() const;
  void showPowerLimited(const Rgbw *pixels);
  void showPowerLimited(const Rgbw *pixels, uint32_t limitMw, bool allowRgbWithFullWhite);

 private:
  Adafruit_NeoPixel strip_;
  uint8_t rotationMap_[4][LED_COUNT] = {};
  void buildRotationMaps();
  void showPowerLimitedInternal(const Rgbw *pixels, uint32_t softwareLimitMw,
                                bool allowRgbWithFullWhite,
                                bool applyGlobalBrightness);
};
