#include "frame_transition.h"

#include <math.h>
#include <string.h>

namespace {
constexpr uint32_t FULL = 4096;

Rgbw visibleColor(const Rgbw &color) {
  // The normal renderer treats W=255 as dedicated white, ignoring RGB.
  // Normalize before interpolation so scaling W does not expose hidden RGB.
  return color.w == 255 ? Rgbw{0, 0, 0, 255} : color;
}

bool sameSettings(const EffectSettings &a, const EffectSettings &b) {
  return a.effect == b.effect && a.direction == b.direction &&
         a.type == b.type && a.durationMs == b.durationMs;
}

uint32_t ramp(uint32_t progress, uint32_t start, uint32_t length) {
  if (progress <= start) return 0;
  if (progress - start >= length) return FULL;
  return (progress - start) * FULL / length;
}

uint8_t blendChannel(uint8_t oldValue, uint8_t newValue,
                     uint32_t oldWeight, uint32_t newWeight) {
  return static_cast<uint8_t>((oldValue * oldWeight + newValue * newWeight +
                               FULL / 2) / FULL);
}

Rgbw blend(const Rgbw &oldColor, const Rgbw &newColor,
           uint32_t oldWeight, uint32_t newWeight) {
  return {blendChannel(oldColor.r, newColor.r, oldWeight, newWeight),
          blendChannel(oldColor.g, newColor.g, oldWeight, newWeight),
          blendChannel(oldColor.b, newColor.b, oldWeight, newWeight),
          blendChannel(oldColor.w, newColor.w, oldWeight, newWeight)};
}
}  // namespace

void FrameTransition::clear() {
  initialized_ = false;
  active_ = false;
}

void FrameTransition::reset(const Rgbw *frame) {
  for (size_t i = 0; i < LED_COUNT; ++i) {
    frame_[i] = target_[i] = visibleColor(frame[i]);
  }
  initialized_ = true;
  active_ = false;
}

bool FrameTransition::setTarget(const Rgbw *target,
                                const EffectSettings &settings,
                                uint32_t now, uint8_t rotationQuarterTurns,
                                bool forceTransition) {
  bool changed = forceTransition || !initialized_ || !sameSettings(settings_, settings) ||
                 rotation_ != rotationQuarterTurns;
  for (size_t i = 0; i < LED_COUNT; ++i) {
    const Rgbw pixel = visibleColor(target[i]);
    if (memcmp(&pixel, &target_[i], sizeof(pixel))) changed = true;
  }
  if (!changed) return false;
  // Retarget from the last frame actually displayed, even during a live preview.
  memcpy(from_, frame_, sizeof(from_));
  settings_ = settings;
  rotation_ = rotationQuarterTurns;
  if (!initialized_ || settings.effect == DisplayEffect::None ||
      settings.durationMs == 0) {
    reset(target);
    return true;
  }
  for (size_t i = 0; i < LED_COUNT; ++i) target_[i] = visibleColor(target[i]);
  buildPhases();
  memset(darkShown_, 0, sizeof(darkShown_));
  blankShown_ = false;
  startedMs_ = now;
  active_ = true;
  return true;
}

void FrameTransition::buildPhases() {
  int16_t minX = LED_POSITIONS[0].x, maxX = minX;
  int16_t minY = LED_POSITIONS[0].y, maxY = minY;
  for (const LedPoint &point : LED_POSITIONS) {
    minX = min(minX, point.x);
    maxX = max(maxX, point.x);
    minY = min(minY, point.y);
    maxY = max(maxY, point.y);
  }
  const float centerX = (minX + maxX) * 0.5f;
  const float centerY = (minY + maxY) * 0.5f;
  const float halfWidth = max(1.0f, (maxX - minX) * 0.5f);
  const float halfHeight = max(1.0f, (maxY - minY) * 0.5f);
  float positions[LED_COUNT];
  float first = 0, last = 0;
  for (size_t i = 0; i < LED_COUNT; ++i) {
    // Match the normalized coordinates used by the LED rotation mapping.
    const float x = (LED_POSITIONS[i].x - centerX) / halfWidth;
    const float y = (LED_POSITIONS[i].y - centerY) / halfHeight;
    float rotatedX = x, rotatedY = y;
    if (rotation_ == 1) { rotatedX = -y; rotatedY = x; }
    else if (rotation_ == 2) { rotatedX = -x; rotatedY = -y; }
    else if (rotation_ == 3) { rotatedX = y; rotatedY = -x; }
    float position = 0;
    const float radialX = LED_POSITIONS[i].x - centerX;
    const float radialY = LED_POSITIONS[i].y - centerY;
    switch (settings_.direction) {
      case FadeDirection::CenterOutward:
        position = sqrtf(radialX * radialX + radialY * radialY); break;
      case FadeDirection::CenterInward:
        position = -sqrtf(radialX * radialX + radialY * radialY); break;
      case FadeDirection::Down: position = rotatedY; break;
      case FadeDirection::Up: position = -rotatedY; break;
      case FadeDirection::Left: position = -rotatedX; break;
      case FadeDirection::Right: position = rotatedX; break;
    }
    positions[i] = position;
    if (!i) first = last = position;
    first = min(first, position);
    last = max(last, position);
  }
  for (size_t i = 0; i < LED_COUNT; ++i) {
    phase_[i] = last > first
                    ? static_cast<uint16_t>((positions[i] - first) * 256 /
                                            (last - first) + 0.5f)
                    : 0;
  }
}

void FrameTransition::advance(uint32_t now) {
  if (!active_) return;
  const uint32_t elapsed = now - startedMs_;  // millis() wrap-safe.
  const uint32_t progress = elapsed >= settings_.durationMs
                                ? FULL
                                : elapsed * FULL / settings_.durationMs;
  if (settings_.type == FadeType::Slowfade && progress >= FULL / 2 &&
      !blankShown_) {
    // Guarantee a real all-black output, even when a loop skipped the midpoint.
    memset(frame_, 0, sizeof(frame_));
    blankShown_ = true;
    return;
  }
  bool emittedDarkStep = false;
  for (size_t i = 0; i < LED_COUNT; ++i) {
    uint32_t oldWeight = 0, newWeight = 0;
    const uint32_t delay = phase_[i] * (FULL / 2) / 256;
    if (settings_.type == FadeType::Mix) {
      const uint32_t window = FULL / 3;
      newWeight = ramp(progress, phase_[i] * (FULL - window) / 256, window);
      oldWeight = FULL - newWeight;
    } else if (settings_.type == FadeType::Quickfade) {
      oldWeight = FULL - ramp(progress, delay, FULL / 4);
      if (progress >= delay + FULL / 4 && !darkShown_[i]) {
        darkShown_[i] = true;
        emittedDarkStep = true;
        frame_[i] = {};
        continue;
      }
      // Short dark gap between local fade-out and fade-in; waves overlap.
      constexpr uint32_t GAP = FULL / 64;
      newWeight = ramp(progress, delay + FULL / 4 + GAP, FULL / 4 - GAP);
    } else {
      const uint32_t halfDelay = delay / 2;
      oldWeight = FULL - ramp(progress, halfDelay, FULL / 4);
      newWeight = blankShown_
                      ? ramp(progress, FULL / 2 + halfDelay, FULL / 4)
                      : 0;
    }
    frame_[i] = blend(from_[i], target_[i], oldWeight, newWeight);
  }
  // A delayed loop must still send black once before showing the target.
  if (progress == FULL && !emittedDarkStep) {
    memcpy(frame_, target_, sizeof(frame_));
    active_ = false;
  }
}
