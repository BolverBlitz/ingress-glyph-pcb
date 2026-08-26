#pragma once

#include <Arduino.h>

#include "config.h"
#include "led_controller.h"

enum class MiscMode : uint8_t { Normal, Flashlight, Emergency };

class MiscController {
 public:
  void begin(LedController &leds);
  void loop();
  MiscMode mode() const { return mode_; }
  bool active() const {
    return mode_ != MiscMode::Normal || remoteEmergencyActive_ ||
           friendPairingActive_ || friendSuccessActive_;
  }
  bool friendButtonHeld() const;
  bool friendBroadcastRequested() const;
  void setFriendPairing(bool active);
  bool friendPairingReady() const;
  bool friendPairingInProgress() const { return friendPairingActive_; }
  void showFriendPairingSuccess(const Rgbw remoteGlyph[LED_COUNT],
                                const Rgbw ownGlyph[LED_COUNT]);
  uint8_t flashlightBrightness() const { return flashlightBrightness_; }
  uint8_t flashlightTemperature() const { return flashlightTemperature_; }
  bool setFlashlight(bool enabled);
  void setFlashlightBrightness(uint8_t percent);
  void setFlashlightTemperature(uint8_t value);
  void setRemoteEmergency(bool active);

 private:
  struct ButtonState {
    uint8_t pin = 0;
    bool rawPressed = false;
    bool pressed = false;
    bool pressedEvent = false;
    bool releasedEvent = false;
    bool longHandled = false;
    bool emergencyHandled = false;
    uint32_t rawChangedAtMs = 0;
    uint32_t pressedAtMs = 0;
  };

  LedController *leds_ = nullptr;
  MiscMode mode_ = MiscMode::Normal;
  ButtonState button1_;
  ButtonState button2_;
  Rgbw frame_[LED_COUNT] = {};
  uint8_t flashlightBrightness_ = 60;
  uint8_t flashlightTemperature_ = 50;
  uint8_t quickPressCount_ = 0;
  uint32_t lastQuickReleaseMs_ = 0;
  bool pendingModeSave_ = false;
  uint32_t lastButtonInputMs_ = 0;
  uint32_t bothPressedAtMs_ = 0;
  bool bothDisableHandled_ = false;
  uint8_t sosStep_ = 0;
  uint32_t sosStepStartedMs_ = 0;
  bool remoteEmergencyActive_ = false;
  uint32_t remoteEmergencyStartedMs_ = 0;
  uint32_t lastRemoteEmergencyFrameMs_ = 0;
  uint16_t remoteEmergencyRadius_[LED_COUNT] = {};
  uint8_t friendAngle_[LED_COUNT] = {};
  uint16_t maxRemoteEmergencyRadius_ = 1;
  bool friendPairingActive_ = false;
  uint32_t friendPairingStartedMs_ = 0;
  uint32_t lastFriendFrameMs_ = 0;
  bool friendSuccessActive_ = false;
  bool friendPairingConsumed_ = false;
  uint32_t friendSuccessStartedMs_ = 0;
  Rgbw friendRemoteGlyph_[LED_COUNT] = {};
  Rgbw friendOwnGlyph_[LED_COUNT] = {};
  Rgbw friendRainbowFrame_[LED_COUNT] = {};

  void updateButton(ButtonState &button, uint32_t now);
  void handleButtons(uint32_t now);
  void noteButtonInput(uint32_t now);
  void cycleDisplayMode(uint32_t now);
  void cycleFlashlightBrightness();
  void enterEmergency(uint32_t now);
  void leaveEmergency();
  void renderFlashlight();
  void renderEmergency(bool on);
  void renderRemoteEmergency(uint32_t now);
  void renderFriendPairing(uint32_t now);
  void renderFriendSuccess(uint32_t now);
  void renderFriendIdentity(const Rgbw source[LED_COUNT],
                            uint32_t elapsed);
};

extern MiscController miscController;
