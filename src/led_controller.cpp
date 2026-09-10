#include "led_controller.h"

#include <cfloat>

#include "app_state.h"
#include "glyph_drawer.h"
#include "sk6812_rmt.h"

LedController::LedController()
    : strip_(LED_COUNT, LED_PIN_REV1, NEO_GRBW + NEO_KHZ800) {}

void LedController::begin() {
  strip_.setPin(globalSettings.hardwareRevision == 2 ? LED_PIN_REV2
                                                     : LED_PIN_REV1);
  buildRotationMaps();
  strip_.begin();
  strip_.clear();
  beginSk6812Rmt(pin());
  showSk6812Rmt(strip_.getPixels(), LED_COUNT * 4U);
  Serial.printf("LED: hardware revision %u, data GPIO%u\n",
                globalSettings.hardwareRevision, pin());
}

uint8_t LedController::pin() const {
  return static_cast<uint8_t>(strip_.getPin());
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
  showPowerLimitedInternal(pixels, globalSettings.maxPowerMw, false, true);
}

void LedController::showPowerLimited(const Rgbw *pixels, uint32_t limitMw,
                                     bool allowRgbWithFullWhite) {
  showPowerLimitedInternal(pixels, limitMw, allowRgbWithFullWhite, false);
}

void LedController::showPowerLimitedInternal(const Rgbw *pixels,
                                             uint32_t softwareLimitMw,
                                             bool allowRgbWithFullWhite,
                                             bool applyGlobalBrightness) {
  const uint32_t hardwareLimitMw = min(
      hardwareLedLimitMw(globalSettings.hardwareRevision),
      thermalPowerLimitMw);
  softwareLimitMw = min(softwareLimitMw,
                        hardwareLedLimitMw(globalSettings.hardwareRevision));
  const uint32_t limitMw = min(softwareLimitMw, hardwareLimitMw);
  const uint16_t brightnessScale =
      applyGlobalBrightness
          ? static_cast<uint16_t>(globalSettings.brightnessPercent) * 255U /
                100U
          : 255U;
  uint64_t requestedCurrentUaTimes255 = 0;
  for (size_t i = 0; i < LED_COUNT; ++i) {
    // Full white is emitted by the dedicated W die only. Below 255, white is
    // intentionally mixed with RGB so users can still create tinted whites.
    const bool pureWhite = pixels[i].w == 255 && !allowRgbWithFullWhite;
    const uint16_t red =
        static_cast<uint16_t>(pixels[i].r) * brightnessScale / 255U;
    const uint16_t green =
        static_cast<uint16_t>(pixels[i].g) * brightnessScale / 255U;
    const uint16_t blue =
        static_cast<uint16_t>(pixels[i].b) * brightnessScale / 255U;
    const uint16_t white =
        static_cast<uint16_t>(pixels[i].w) * brightnessScale / 255U;
    uint64_t pixelCurrentUaTimes255 =
        static_cast<uint64_t>(pureWhite ? 0 : red + green + blue) *
        RGB_CHANNEL_CURRENT_UA;
    pixelCurrentUaTimes255 +=
        static_cast<uint64_t>(white) * WHITE_CHANNEL_CURRENT_UA;
    requestedCurrentUaTimes255 += pixelCurrentUaTimes255;
  }

  const uint64_t maxCurrentUaTimes255 =
      (static_cast<uint64_t>(limitMw) * 1000000ULL * 255ULL) /
      LED_SUPPLY_MV;
  uint32_t limiterScale = 255;
  if (requestedCurrentUaTimes255 > maxCurrentUaTimes255) {
    limiterScale = static_cast<uint32_t>(
        (maxCurrentUaTimes255 * 255ULL) / requestedCurrentUaTimes255);
  }
  const uint64_t requestedPowerMw =
      requestedCurrentUaTimes255 * LED_SUPPLY_MV /
      (1000000ULL * 255ULL);
  currentLedPowerMw = static_cast<uint32_t>(
      requestedPowerMw * limiterScale / 255ULL);
  ledPowerLimitState =
      limiterScale == 255
          ? LedPowerLimitState::NotLimited
          : softwareLimitMw < hardwareLimitMw
                ? LedPowerLimitState::Software
                : LedPowerLimitState::Hardware;
  for (size_t i = 0; i < LED_COUNT; ++i) {
    const Rgbw &pixel =
        pixels[rotationMap_[globalSettings.rotationQuarterTurns][i]];
    const bool pureWhite = pixel.w == 255 && !allowRgbWithFullWhite;
    const uint32_t outputScale = brightnessScale * limiterScale;
    strip_.setPixelColor(
        i, strip_.Color(
               pureWhite ? 0 : static_cast<uint32_t>(pixel.r) * outputScale /
                                       (255UL * 255UL),
               pureWhite ? 0 : static_cast<uint32_t>(pixel.g) * outputScale /
                                       (255UL * 255UL),
               pureWhite ? 0 : static_cast<uint32_t>(pixel.b) * outputScale /
                                       (255UL * 255UL),
               static_cast<uint32_t>(pixel.w) * outputScale /
                   (255UL * 255UL)));
  }
  showSk6812Rmt(strip_.getPixels(), LED_COUNT * 4U);
}
