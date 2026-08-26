#include "led_controller.h"

#include <cfloat>

#include "app_state.h"
#include "glyph_drawer.h"

LedController::LedController()
    : strip_(LED_COUNT, LED_PIN, NEO_GRBW + NEO_KHZ800) {}

void LedController::begin() {
  buildRotationMaps();
  strip_.begin();
  strip_.clear();
  strip_.show();
}

void LedController::buildRotationMaps() {
  int16_t minX = LED_POSITIONS[0].x;
  int16_t maxX = minX;
  int16_t minY = LED_POSITIONS[0].y;
  int16_t maxY = minY;
  for (size_t i = 1; i < LED_COUNT; ++i) {
    minX = min(minX, LED_POSITIONS[i].x);
    maxX = max(maxX, LED_POSITIONS[i].x);
    minY = min(minY, LED_POSITIONS[i].y);
    maxY = max(maxY, LED_POSITIONS[i].y);
  }
  const float centerX = (minX + maxX) * 0.5f;
  const float centerY = (minY + maxY) * 0.5f;
  const float halfWidth = (maxX - minX) * 0.5f;
  const float halfHeight = (maxY - minY) * 0.5f;

  for (uint8_t turn = 0; turn < 4; ++turn) {
    for (size_t destination = 0; destination < LED_COUNT; ++destination) {
      const float x = (LED_POSITIONS[destination].x - centerX) / halfWidth;
      const float y = (LED_POSITIONS[destination].y - centerY) / halfHeight;
      float sourceX = x;
      float sourceY = y;
      if (turn == 1) {
        sourceX = y;
        sourceY = -x;
      } else if (turn == 2) {
        sourceX = -x;
        sourceY = -y;
      } else if (turn == 3) {
        sourceX = -y;
        sourceY = x;
      }

      float nearestDistance = FLT_MAX;
      uint8_t nearest = 0;
      for (size_t source = 0; source < LED_COUNT; ++source) {
        const float candidateX =
            (LED_POSITIONS[source].x - centerX) / halfWidth;
        const float candidateY =
            (LED_POSITIONS[source].y - centerY) / halfHeight;
        const float dx = candidateX - sourceX;
        const float dy = candidateY - sourceY;
        const float distance = dx * dx + dy * dy;
        if (distance < nearestDistance) {
          nearestDistance = distance;
          nearest = source;
        }
      }
      rotationMap_[turn][destination] = nearest;
    }
  }
}

void LedController::showPowerLimited(const Rgbw *pixels) {
  showPowerLimitedInternal(pixels, powerLimitMw, false);
}

void LedController::showPowerLimited(const Rgbw *pixels, uint32_t limitMw,
                                     bool allowRgbWithFullWhite) {
  showPowerLimitedInternal(pixels, limitMw, allowRgbWithFullWhite);
}

void LedController::showPowerLimitedInternal(const Rgbw *pixels,
                                             uint32_t limitMw,
                                             bool allowRgbWithFullWhite) {
  // Use the brightest requested pixel as the global reference and budget as if
  // all 69 LEDs displayed it. The limiter scale therefore does not jump when a
  // glyph changes from, for example, three lit LEDs to nine.
  uint64_t maxPixelCurrentUaTimes255 = 0;
  for (size_t i = 0; i < LED_COUNT; ++i) {
    // Full white is emitted by the dedicated W die only. Below 255, white is
    // intentionally mixed with RGB so users can still create tinted whites.
    const bool pureWhite = pixels[i].w == 255 && !allowRgbWithFullWhite;
    uint64_t pixelCurrentUaTimes255 =
        static_cast<uint64_t>(pureWhite ? 0 :
                              pixels[i].r + pixels[i].g + pixels[i].b) *
        RGB_CHANNEL_CURRENT_UA;
    pixelCurrentUaTimes255 +=
        static_cast<uint64_t>(pixels[i].w) * WHITE_CHANNEL_CURRENT_UA;
    maxPixelCurrentUaTimes255 =
        max(maxPixelCurrentUaTimes255, pixelCurrentUaTimes255);
  }

  const uint64_t assumedCurrentUaTimes255 =
      maxPixelCurrentUaTimes255 * LED_COUNT;

  const uint64_t maxCurrentUaTimes255 =
      (static_cast<uint64_t>(limitMw) * 1000000ULL * 255ULL) /
      LED_SUPPLY_MV;
  uint32_t limiterScale = 255;
  if (assumedCurrentUaTimes255 > maxCurrentUaTimes255) {
    limiterScale = static_cast<uint32_t>(
        (maxCurrentUaTimes255 * 255ULL) / assumedCurrentUaTimes255);
  }
  for (size_t i = 0; i < LED_COUNT; ++i) {
    const Rgbw &pixel =
        pixels[rotationMap_[globalSettings.rotationQuarterTurns][i]];
    const bool pureWhite = pixel.w == 255 && !allowRgbWithFullWhite;
    strip_.setPixelColor(
        i, strip_.Color(
               pureWhite ? 0 :
                   (static_cast<uint16_t>(pixel.r) * limiterScale) / 255,
               pureWhite ? 0 :
                   (static_cast<uint16_t>(pixel.g) * limiterScale) / 255,
               pureWhite ? 0 :
                   (static_cast<uint16_t>(pixel.b) * limiterScale) / 255,
               (static_cast<uint16_t>(pixel.w) * limiterScale) / 255));
  }
  strip_.show();
}
