#include "misc_controller.h"

#include <math.h>

#include "app_state.h"
#include "display_store.h"
#include "glyph_drawer.h"

namespace {
constexpr uint16_t SOS_UNIT_MS = 250;
constexpr uint8_t SOS_DURATIONS[] = {
    1, 1, 1, 1, 1, 3,  // S
    3, 1, 3, 1, 3, 3,  // O
    1, 1, 1, 1, 1, 7,  // S and word gap
};
constexpr uint8_t FLASHLIGHT_LEVELS[] = {25, 60, 100};
constexpr uint32_t FRIEND_RAINBOW_FADE_MS = 700UL;
constexpr uint32_t FRIEND_GLYPH_FADE_MS = 700UL;
constexpr uint32_t FRIEND_GLYPH_HOLD_MS = 600UL;
constexpr uint32_t FRIEND_GLYPH_STAGE_MS =
    FRIEND_GLYPH_FADE_MS * 2UL + FRIEND_GLYPH_HOLD_MS;
constexpr uint32_t FRIEND_SUCCESS_MS =
    FRIEND_RAINBOW_FADE_MS + FRIEND_GLYPH_STAGE_MS * 2UL;

Rgbw rainbowColor(uint8_t position) {
  if (position < 85) {
    return {static_cast<uint8_t>(255U - position * 3U),
            static_cast<uint8_t>(position * 3U), 0, 0};
  }
  if (position < 170) {
    position -= 85;
    return {0, static_cast<uint8_t>(255U - position * 3U),
            static_cast<uint8_t>(position * 3U), 0};
  }
  position -= 170;
  return {static_cast<uint8_t>(position * 3U), 0,
          static_cast<uint8_t>(255U - position * 3U), 0};
}

bool hasVisiblePixels(const Rgbw pixels[LED_COUNT]) {
  for (size_t i = 0; i < LED_COUNT; ++i) {
    if (pixels[i].r || pixels[i].g || pixels[i].b || pixels[i].w) return true;
  }
  return false;
}
}  // namespace

MiscController miscController;

void MiscController::begin(LedController &leds) {
  leds_ = &leds;
  button1_.pin = BUTTON_1_PIN;
  button2_.pin = BUTTON_2_PIN;
  pinMode(button1_.pin, INPUT_PULLUP);
  pinMode(button2_.pin, INPUT_PULLUP);
  button1_.rawPressed = button1_.pressed = digitalRead(button1_.pin) == LOW;
  button2_.rawPressed = button2_.pressed = digitalRead(button2_.pin) == LOW;
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
  const int32_t centerX = (static_cast<int32_t>(minX) + maxX) / 2;
  const int32_t centerY = (static_cast<int32_t>(minY) + maxY) / 2;
  for (size_t i = 0; i < LED_COUNT; ++i) {
    const int32_t dx = LED_POSITIONS[i].x - centerX;
    const int32_t dy = LED_POSITIONS[i].y - centerY;
    remoteEmergencyRadius_[i] =
        static_cast<uint16_t>(sqrtf(static_cast<float>(dx * dx + dy * dy)));
    float angle = atan2f(static_cast<float>(dy), static_cast<float>(dx));
    if (angle < 0) angle += 2.0f * PI;
    friendAngle_[i] = static_cast<uint8_t>(angle * 255.0f / (2.0f * PI));
    maxRemoteEmergencyRadius_ =
        max(maxRemoteEmergencyRadius_, remoteEmergencyRadius_[i]);
  }
}

void MiscController::updateButton(ButtonState &button, uint32_t now) {
  button.pressedEvent = false;
  button.releasedEvent = false;
  const bool rawPressed = digitalRead(button.pin) == LOW;
  if (rawPressed != button.rawPressed) {
    button.rawPressed = rawPressed;
    button.rawChangedAtMs = now;
  }
  if (button.pressed == button.rawPressed ||
      now - button.rawChangedAtMs < BUTTON_DEBOUNCE_MS) {
    return;
  }
  button.pressed = button.rawPressed;
  if (button.pressed) {
    button.pressedEvent = true;
    button.pressedAtMs = now;
    button.longHandled = false;
    button.emergencyHandled = false;
  } else {
    button.releasedEvent = true;
  }
  noteButtonInput(now);
}

void MiscController::noteButtonInput(uint32_t now) {
  lastButtonInputMs_ = now;
}

void MiscController::cycleDisplayMode(uint32_t now) {
  mode_ = MiscMode::Normal;
  if (displaySettings.mode == DisplayMode::GlyphSequence) {
    displaySettings.mode = DisplayMode::GlyphRandom;
  } else if (displaySettings.mode == DisplayMode::GlyphRandom) {
    displaySettings.mode = DisplayMode::Draw;
  } else {
    displaySettings.mode = DisplayMode::GlyphSequence;
  }
  pendingModeSave_ = true;
  lastButtonInputMs_ = now;
  ++displayRevision;
  Serial.printf("Buttons: display mode %u selected; save pending\n",
                static_cast<unsigned>(displaySettings.mode));
}

void MiscController::cycleFlashlightBrightness() {
  for (uint8_t level : FLASHLIGHT_LEVELS) {
    if (level > flashlightBrightness_) {
      flashlightBrightness_ = level;
      renderFlashlight();
      return;
    }
  }
  flashlightBrightness_ = FLASHLIGHT_LEVELS[0];
  renderFlashlight();
}

bool MiscController::setFlashlight(bool enabled) {
  if (mode_ == MiscMode::Emergency) return false;
  const MiscMode requested = enabled ? MiscMode::Flashlight : MiscMode::Normal;
  if (mode_ == requested) return true;
  mode_ = requested;
  if (mode_ == MiscMode::Flashlight) {
    renderFlashlight();
  } else {
    ++displayRevision;
  }
  return true;
}

void MiscController::setRemoteEmergency(bool active) {
  if (remoteEmergencyActive_ == active) return;
  remoteEmergencyActive_ = active;
  if (active) {
    remoteEmergencyStartedMs_ = millis();
    lastRemoteEmergencyFrameMs_ = 0;
    if (mode_ != MiscMode::Emergency) {
      renderRemoteEmergency(remoteEmergencyStartedMs_);
    }
  } else if (mode_ == MiscMode::Flashlight) {
    renderFlashlight();
  } else if (mode_ == MiscMode::Normal) {
    ++displayRevision;
  }
}

bool MiscController::friendButtonHeld() const {
  return mode_ == MiscMode::Normal && !remoteEmergencyActive_ &&
         button2_.pressed && !button1_.pressed && !friendPairingConsumed_;
}

bool MiscController::friendBroadcastRequested() const {
  return friendButtonHeld() || friendPairingActive_;
}

void MiscController::setFriendPairing(bool active) {
  const bool forceCancel = friendSuccessActive_ ||
                           mode_ != MiscMode::Normal ||
                           remoteEmergencyActive_;
  if (forceCancel) {
    active = false;
  }
  const uint32_t now = millis();
  if (friendPairingActive_ &&
      now - friendPairingStartedMs_ >=
          FRIEND_PAIRING_HOLD_MS + FRIEND_PAIRING_EXCHANGE_TIMEOUT_MS) {
    active = false;
    friendPairingConsumed_ = true;
  }
  if (!active && friendPairingActive_ && !forceCancel &&
      now - friendPairingStartedMs_ >= FRIEND_PAIRING_HOLD_MS &&
      now - friendPairingStartedMs_ <
          FRIEND_PAIRING_HOLD_MS + FRIEND_PAIRING_EXCHANGE_TIMEOUT_MS) {
    // Five valid seconds latch the exchange. Releasing at exactly five
    // seconds must not cancel an in-flight GATT read on either device.
    return;
  }
  if (active == friendPairingActive_) return;
  friendPairingActive_ = active;
  if (active) {
    // Both devices begin when the mutual BLE request is first observed. Using
    // the local button timestamp made the device pressed first jump ahead.
    friendPairingStartedMs_ = now;
    lastFriendFrameMs_ = 0;
    renderFriendPairing(friendPairingStartedMs_);
  } else {
    ++displayRevision;
  }
}

bool MiscController::friendPairingReady() const {
  return friendPairingActive_ &&
         millis() - friendPairingStartedMs_ >= FRIEND_PAIRING_HOLD_MS;
}

void MiscController::showFriendPairingSuccess(
    const Rgbw remoteGlyph[LED_COUNT], const Rgbw ownGlyph[LED_COUNT]) {
  memcpy(friendRemoteGlyph_, remoteGlyph, sizeof(friendRemoteGlyph_));
  memcpy(friendOwnGlyph_, ownGlyph, sizeof(friendOwnGlyph_));
  // Preserve the exact last pairing frame. Recreating a color wheel here
  // changed its phase and caused a visible jump before the fade-out.
  memcpy(friendRainbowFrame_, frame_, sizeof(friendRainbowFrame_));
  friendPairingActive_ = false;
  friendSuccessActive_ = true;
  friendPairingConsumed_ = true;
  friendSuccessStartedMs_ = millis();
  lastFriendFrameMs_ = 0;
}

void MiscController::setFlashlightBrightness(uint8_t percent) {
  flashlightBrightness_ = constrain(percent, 0, 100);
  if (mode_ == MiscMode::Flashlight) renderFlashlight();
}

void MiscController::setFlashlightTemperature(uint8_t value) {
  flashlightTemperature_ = constrain(value, 0, 100);
  if (mode_ == MiscMode::Flashlight) renderFlashlight();
}

void MiscController::enterEmergency(uint32_t now) {
  mode_ = MiscMode::Emergency;
  sosStep_ = 0;
  sosStepStartedMs_ = now;
  renderEmergency(true);
  Serial.println("Buttons: emergency SOS enabled");
}

void MiscController::leaveEmergency() {
  if (mode_ != MiscMode::Emergency) return;
  mode_ = MiscMode::Normal;
  bothDisableHandled_ = true;
  button1_.longHandled = true;
  button1_.emergencyHandled = true;
  ++displayRevision;
  Serial.println("Buttons: emergency SOS disabled");
}

void MiscController::renderFlashlight() {
  if (!leds_) return;
  if (remoteEmergencyActive_) {
    renderRemoteEmergency(millis());
    return;
  }
  const uint16_t level = static_cast<uint16_t>(flashlightBrightness_) * 255U / 100U;
  const int16_t temperature = static_cast<int16_t>(flashlightTemperature_) - 50;
  const uint16_t tint = abs(temperature) * 2U;
  Rgbw color{0, 0, 0, static_cast<uint8_t>(level)};
  if (temperature < 0) {
    color.b = static_cast<uint8_t>(level * tint / 100U);
    color.g = static_cast<uint8_t>(color.b * 35U / 100U);
  } else if (temperature > 0) {
    color.r = static_cast<uint8_t>(level * tint / 100U);
    color.g = static_cast<uint8_t>(color.r * 55U / 100U);
  }
  for (Rgbw &pixel : frame_) pixel = color;
  leds_->showPowerLimited(frame_, globalSettings.maxPowerMw, true);
}

void MiscController::renderEmergency(bool on) {
  if (!leds_) return;
  const Rgbw color = on ? Rgbw{0, 0, 0, 255} : Rgbw{};
  for (Rgbw &pixel : frame_) pixel = color;
  leds_->showPowerLimited(frame_, globalSettings.maxPowerMw, false);
}

void MiscController::renderRemoteEmergency(uint32_t now) {
  if (!leds_ || !remoteEmergencyActive_ || mode_ == MiscMode::Emergency) return;
  const uint32_t elapsed = (now - remoteEmergencyStartedMs_) %
                           REMOTE_EMERGENCY_WAVE_MS;
  const uint32_t front =
      (static_cast<uint32_t>(maxRemoteEmergencyRadius_) * elapsed) /
      REMOTE_EMERGENCY_WAVE_MS;
  const uint32_t band =
      max<uint32_t>(1, static_cast<uint32_t>(maxRemoteEmergencyRadius_) * 30U /
                               100U);
  for (size_t i = 0; i < LED_COUNT; ++i) {
    const uint32_t radius = remoteEmergencyRadius_[i];
    const uint32_t linearDistance =
        radius > front ? radius - front : front - radius;
    // Treat center and outer edge as adjacent ends of a circular phase. This
    // cross-fades the outgoing edge into the next center pulse without a hard
    // frame cut at the animation wrap point.
    const uint32_t wrappedDistance =
        maxRemoteEmergencyRadius_ > linearDistance
            ? maxRemoteEmergencyRadius_ - linearDistance
            : 0;
    const uint32_t distance = min(linearDistance, wrappedDistance);
    uint8_t red = 0;
    if (distance < band) {
      const uint32_t x = 255U * (band - distance) / band;
      red = static_cast<uint8_t>(
          (x * x * (3U * 255U - 2U * x)) / (255U * 255U));
    }
    frame_[i] = {red, 0, 0, 0};
  }
  leds_->showPowerLimited(frame_, globalSettings.maxPowerMw, false);
  lastRemoteEmergencyFrameMs_ = now;
}

void MiscController::renderFriendPairing(uint32_t now) {
  if (!leds_ || !friendPairingActive_) return;
  const uint32_t elapsed = min<uint32_t>(now - friendPairingStartedMs_,
                                         FRIEND_PAIRING_HOLD_MS);
  const uint32_t front =
      static_cast<uint32_t>(maxRemoteEmergencyRadius_) * elapsed /
      FRIEND_PAIRING_HOLD_MS;
  const uint32_t feather = max<uint32_t>(1, maxRemoteEmergencyRadius_ / 5U);
  for (size_t i = 0; i < LED_COUNT; ++i) {
    uint8_t level = 0;
    const uint32_t radius = remoteEmergencyRadius_[i];
    if (radius <= front) {
      level = 255;
    } else if (radius - front < feather) {
      level = static_cast<uint8_t>(255U * (feather - (radius - front)) /
                                   feather);
    }
    const Rgbw rainbow =
        rainbowColor(static_cast<uint8_t>(friendAngle_[i] + elapsed / 20U));
    frame_[i] = scaledColor(rainbow, static_cast<uint16_t>(level) + 1U);
  }
  leds_->showPowerLimited(frame_);
  lastFriendFrameMs_ = now;
}

void MiscController::renderFriendIdentity(const Rgbw source[LED_COUNT],
                                          uint32_t elapsed) {
  if (hasVisiblePixels(source)) {
    uint16_t scale = 256;
    if (elapsed < FRIEND_GLYPH_FADE_MS) {
      scale = static_cast<uint16_t>(elapsed * 256U /
                                    FRIEND_GLYPH_FADE_MS);
    } else if (elapsed >= FRIEND_GLYPH_FADE_MS +
                              FRIEND_GLYPH_HOLD_MS) {
      scale = static_cast<uint16_t>(
          (FRIEND_GLYPH_STAGE_MS - elapsed) * 256U /
          FRIEND_GLYPH_FADE_MS);
    }
    for (size_t i = 0; i < LED_COUNT; ++i) {
      frame_[i] = scaledColor(source[i], scale);
    }
    return;
  }

  // Missing Agent Glyph: sweep a color wheel clockwise, then fade it away.
  const uint32_t swipeMs = FRIEND_GLYPH_FADE_MS + FRIEND_GLYPH_HOLD_MS;
  const uint16_t leadingAngle =
      min<uint32_t>(255U, elapsed * 255U / swipeMs);
  const uint16_t fade = elapsed <= swipeMs
                            ? 256U
                            : static_cast<uint16_t>(
                                  (FRIEND_GLYPH_STAGE_MS - elapsed) * 256U /
                                  FRIEND_GLYPH_FADE_MS);
  constexpr uint8_t FEATHER = 18;
  for (size_t i = 0; i < LED_COUNT; ++i) {
    uint16_t level = 0;
    if (friendAngle_[i] <= leadingAngle) {
      level = 256;
    } else if (friendAngle_[i] - leadingAngle < FEATHER) {
      level = static_cast<uint16_t>(
          256U * (FEATHER - (friendAngle_[i] - leadingAngle)) / FEATHER);
    }
    frame_[i] = scaledColor(rainbowColor(friendAngle_[i]),
                            static_cast<uint16_t>(level * fade / 256U));
  }
}

void MiscController::renderFriendSuccess(uint32_t now) {
  if (!leds_ || !friendSuccessActive_) return;
  const uint32_t elapsed = now - friendSuccessStartedMs_;
  if (elapsed < FRIEND_RAINBOW_FADE_MS) {
    const uint16_t scale =
        static_cast<uint16_t>((FRIEND_RAINBOW_FADE_MS - elapsed) * 256U /
                              FRIEND_RAINBOW_FADE_MS);
    for (size_t i = 0; i < LED_COUNT; ++i) {
      frame_[i] = scaledColor(friendRainbowFrame_[i], scale);
    }
  } else if (elapsed <
             FRIEND_RAINBOW_FADE_MS + FRIEND_GLYPH_STAGE_MS) {
    // First Agent Glyph: the newly paired remote Agent.
    renderFriendIdentity(friendRemoteGlyph_,
                         elapsed - FRIEND_RAINBOW_FADE_MS);
  } else if (elapsed < FRIEND_SUCCESS_MS) {
    // Second Agent Glyph: this device's own Agent Glyph.
    renderFriendIdentity(
        friendOwnGlyph_,
        elapsed - FRIEND_RAINBOW_FADE_MS - FRIEND_GLYPH_STAGE_MS);
  } else {
    for (Rgbw &pixel : frame_) pixel = {};
    leds_->showPowerLimited(frame_);
    friendSuccessActive_ = false;
    memset(friendRemoteGlyph_, 0, sizeof(friendRemoteGlyph_));
    memset(friendOwnGlyph_, 0, sizeof(friendOwnGlyph_));
    memset(friendRainbowFrame_, 0, sizeof(friendRainbowFrame_));
    ++displayRevision;
    return;
  }
  leds_->showPowerLimited(frame_);
  lastFriendFrameMs_ = now;
}

void MiscController::handleButtons(uint32_t now) {
  updateButton(button1_, now);
  updateButton(button2_, now);
  if (!button2_.pressed) friendPairingConsumed_ = false;

  // A simultaneous hold is handled before either individual button. This
  // keeps the 10-second Emergency gesture silent instead of entering
  // Flashlight when Button 1 crosses its two-second threshold.
  if (button1_.pressed && button2_.pressed) {
    if (!bothPressedAtMs_) bothPressedAtMs_ = now;
    if (!bothDisableHandled_) {
      if (mode_ == MiscMode::Emergency &&
          now - bothPressedAtMs_ >= BUTTON_LONG_PRESS_MS) {
        bothDisableHandled_ = true;
        leaveEmergency();
      } else if (mode_ != MiscMode::Emergency &&
                 now - bothPressedAtMs_ >= BUTTON_EMERGENCY_PRESS_MS) {
        bothDisableHandled_ = true;
        button1_.longHandled = true;
        button1_.emergencyHandled = true;
        enterEmergency(now);
      }
    }
    return;
  }
  bothPressedAtMs_ = 0;
  bothDisableHandled_ = false;

  if (button2_.pressedEvent && mode_ == MiscMode::Flashlight &&
      !button1_.pressed) {
    cycleFlashlightBrightness();
  }

  if (button1_.pressed) {
    const uint32_t heldMs = now - button1_.pressedAtMs;
    if (!button1_.longHandled && heldMs >= BUTTON_LONG_PRESS_MS) {
      button1_.longHandled = true;
      setFlashlight(mode_ != MiscMode::Flashlight);
    }
  }

  if (button1_.releasedEvent && !button1_.longHandled &&
      mode_ != MiscMode::Emergency) {
    if (quickPressCount_ &&
        now - lastQuickReleaseMs_ <= BUTTON_DOUBLE_PRESS_MS) {
      ++quickPressCount_;
    } else {
      quickPressCount_ = 1;
    }
    lastQuickReleaseMs_ = now;
    if (quickPressCount_ >= 2) {
      quickPressCount_ = 0;
      cycleDisplayMode(now);
    }
  }
}

void MiscController::loop() {
  const uint32_t now = millis();
  handleButtons(now);

  if (quickPressCount_ &&
      now - lastQuickReleaseMs_ > BUTTON_DOUBLE_PRESS_MS) {
    quickPressCount_ = 0;
  }
  if (pendingModeSave_ &&
      now - lastButtonInputMs_ >= BUTTON_MODE_SAVE_DELAY_MS) {
    if (saveDisplaySettings()) {
      pendingModeSave_ = false;
      Serial.println("Buttons: display mode saved");
    } else {
      lastButtonInputMs_ = now;
      Serial.println("Buttons: display mode save failed; will retry");
    }
  }
  if (mode_ == MiscMode::Emergency) {
    const uint32_t duration = SOS_DURATIONS[sosStep_] * SOS_UNIT_MS;
    if (now - sosStepStartedMs_ >= duration) {
      sosStep_ = (sosStep_ + 1) % sizeof(SOS_DURATIONS);
      sosStepStartedMs_ = now;
      renderEmergency((sosStep_ % 2) == 0);
    }
  } else if (remoteEmergencyActive_ &&
             now - lastRemoteEmergencyFrameMs_ >=
                 REMOTE_EMERGENCY_FRAME_MS) {
    renderRemoteEmergency(now);
  } else if (friendPairingActive_ && now - lastFriendFrameMs_ >= 40U) {
    renderFriendPairing(now);
  } else if (friendSuccessActive_ && now - lastFriendFrameMs_ >= 40U) {
    renderFriendSuccess(now);
  }
}
