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
  void refreshPowerLimit();

 private:
  Adafruit_NeoPixel strip_;
  uint8_t rotationMap_[4][LED_COUNT] = {};
  Rgbw lastRequestedFrame_[LED_COUNT] = {};
  uint32_t lastSoftwareLimitMw_ = 0;
  bool lastAllowRgbWithFullWhite_ = false;
  bool lastApplyGlobalBrightness_ = true;
  bool hasRequestedFrame_ = false;
  void buildRotationMaps();
  void showPowerLimitedInternal(const Rgbw *pixels, uint32_t softwareLimitMw,
                                bool allowRgbWithFullWhite,
                                bool applyGlobalBrightness);
};
