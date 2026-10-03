#pragma once

#include "config.h"
#include "effect_settings.h"
#include "glyph_drawer.h"
#include "rgbw.h"

// Owns the unscaled playback frame. Power/temperature limiting is applied later.
class FrameTransition {
 public:
  bool setTarget(const Rgbw *target, const EffectSettings &settings,
                 uint32_t now, uint8_t rotationQuarterTurns = 0,
                 bool forceTransition = false);
  void reset(const Rgbw *frame);
  void clear();
  void advance(uint32_t now);
  bool active() const { return active_; }
  const Rgbw *frame() const { return frame_; }

 private:
  Rgbw frame_[LED_COUNT] = {};
  Rgbw from_[LED_COUNT] = {};
  Rgbw target_[LED_COUNT] = {};
  uint16_t phase_[LED_COUNT] = {};
  bool darkShown_[LED_COUNT] = {};
  EffectSettings settings_;
  uint32_t startedMs_ = 0;
  uint8_t rotation_ = 0;
  bool initialized_ = false;
  bool active_ = false;
  bool blankShown_ = false;

  void buildPhases();
};
