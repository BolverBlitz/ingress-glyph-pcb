#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>

#include "app_state.h"
#include "config.h"
#include "glyph_drawer.h"
#include "glyphs.h"
#include "led_controller.h"
#include "misc_controller.h"
#include "proximity_manager.h"
#include "settings_store.h"
#include "serial_config.h"
#include "temperature_controller.h"
#include "draw_store.h"
#include "display_store.h"
#include "captive_portal.h"
#include "time_service.h"
#include "frame_transition.h"

LedController leds;
GlyphDrawer drawer;
GlyphWebServer web;
Rgbw outputFrame[LED_COUNT] = {};
FrameTransition frameTransition;
uint32_t lastEffectFrameMs = 0;
bool playbackSuspended = false;
uint8_t activePage = 0;
uint32_t lastPageChangeMs = 0;
uint32_t renderedRevision = UINT32_MAX;
uint16_t randomGlyph = 0;
DisplayMode renderedMode = DisplayMode::GlyphSequence;
uint32_t lastFriendPairAttemptMs = 0;
uint32_t lastFriendPrepareAttemptMs = 0;
bool friendModeWasRendering = false;
uint8_t friendModeIndex = 0;
uint32_t friendModeStartedMs = 0;
uint32_t lastFriendModeFrameMs = 0;

static uint8_t pageCount() {
  return displaySettings.mode == DisplayMode::Draw ? displaySettings.customPageCount : displaySettings.glyphCount;
}

static void chooseRandomGlyph() {
  if (GLYPH_COUNT < 2) {
    randomGlyph = 0;
    return;
  }
  uint16_t next;
  do {
    next = random(GLYPH_COUNT);
  } while (next == randomGlyph);
  randomGlyph = next;
}

static bool renderPage(bool forceTransition = false) {
  const uint8_t count = max<uint8_t>(1, pageCount());
  activePage %= count;
  if (displaySettings.mode == DisplayMode::Draw) {
    memcpy(outputFrame, customPages[activePage], sizeof(outputFrame));
  } else {
    const uint16_t glyphIndex = displaySettings.mode == DisplayMode::GlyphRandom ? randomGlyph : min<uint16_t>(displaySettings.glyphs[activePage], GLYPH_COUNT - 1);
    const Rgbw line = scaledColor(
        displaySettings.lineColor,
        static_cast<uint16_t>(displaySettings.lineBrightnessPercent) * 256U / 100U);
    const Rgbw core = scaledColor(
        displaySettings.coreColor,
        static_cast<uint16_t>(displaySettings.coreBrightnessPercent) * 256U / 100U);
    drawer.draw(GLYPHS[glyphIndex].pattern, line, core, outputFrame);
  }
  const uint32_t now = millis();
  const bool changed = frameTransition.setTarget(
      outputFrame, displaySettings.effects, now,
      globalSettings.rotationQuarterTurns, forceTransition);
  leds.showPowerLimited(frameTransition.frame());
  lastEffectFrameMs = now;
  renderedRevision = displayRevision;
  renderedMode = displaySettings.mode;
  return changed;
}

static bool advancePlayback(uint32_t now) {
  if (!frameTransition.active() || now - lastEffectFrameMs < 20U) return false;
  frameTransition.advance(now);
  leds.showPowerLimited(frameTransition.frame());
  lastEffectFrameMs = now;
  return !frameTransition.active();
}

static bool renderFriendMode() {
  const uint8_t count = globalSettings.friendModeEnabled
                            ? proximityManager.loadedFriendGlyphCount()
                            : 0;
  if (!count) {
    if (friendModeWasRendering) {
      friendModeWasRendering = false;
      renderedRevision = UINT32_MAX;
    }
    return false;
  }
  const uint32_t now = millis();
  const bool entering = !friendModeWasRendering;
  if (!friendModeWasRendering) {
    friendModeWasRendering = true;
    friendModeIndex = 0;
    friendModeStartedMs = now;
    lastFriendModeFrameMs = 0;
  }
  if (displaySettings.effects.effect == DisplayEffect::Fade) {
    bool cycle = false;
    if (!entering && !frameTransition.active() &&
        now - friendModeStartedMs >= displaySettings.intervalMs) {
      friendModeIndex = (friendModeIndex + 1) % count;
      friendModeStartedMs = now;
      cycle = true;
    }
    const Rgbw *glyph = proximityManager.loadedFriendGlyph(friendModeIndex % count);
    if (!glyph) return true;
    if (frameTransition.setTarget(glyph, displaySettings.effects, now,
                                  globalSettings.rotationQuarterTurns, cycle)) {
      friendModeStartedMs = now;
      leds.showPowerLimited(frameTransition.frame());
      lastEffectFrameMs = now;
    }
    if (advancePlayback(now)) friendModeStartedMs = now;
    return true;
  }
  constexpr uint32_t CYCLE_MS =
      FRIEND_MODE_FADE_MS * 2U + FRIEND_MODE_HOLD_MS;
  if (now - friendModeStartedMs >= CYCLE_MS) {
    friendModeIndex = (friendModeIndex + 1) % count;
    friendModeStartedMs = now;
  }
  if (now - lastFriendModeFrameMs < 40U) return true;
  const Rgbw *glyph = proximityManager.loadedFriendGlyph(friendModeIndex % count);
  if (!glyph) return true;
  const uint32_t elapsed = now - friendModeStartedMs;
  uint16_t scale = 256;
  if (elapsed < FRIEND_MODE_FADE_MS) {
    scale = static_cast<uint16_t>(elapsed * 256U / FRIEND_MODE_FADE_MS);
  } else if (elapsed > FRIEND_MODE_FADE_MS + FRIEND_MODE_HOLD_MS) {
    scale = static_cast<uint16_t>((CYCLE_MS - elapsed) * 256U /
                                  FRIEND_MODE_FADE_MS);
  }
  for (size_t i = 0; i < LED_COUNT; ++i) {
    outputFrame[i] = scaledColor(glyph[i], scale);
  }
  frameTransition.reset(outputFrame);
  leds.showPowerLimited(outputFrame);
  lastFriendModeFrameMs = now;
  return true;
}

void setup() {
  Serial.begin(115200);
  loadSettings();
  beginSerialConfig();
  temperatureController.begin();
  beginTimeService();
  loadDrawPages();
  loadDisplaySettings();
  randomSeed(esp_random());
  chooseRandomGlyph();
  leds.begin();
  miscController.begin(leds);
  web.begin();
  proximityManager.begin();
  renderPage();
  Serial.println("Web server ready.");
}

void loop() {
  loopSerialConfig();
  if (temperatureController.loop()) {
    // Apply thermal changes before network work or any mode-specific early exit.
    leds.refreshPowerLimit();
    ++displayRevision;
  }
  if (loopTimeService()) proximityManager.onTimeSynchronized();
  web.loop();
  miscController.loop();
  proximityManager.setLocalEmergency(
      miscController.mode() == MiscMode::Emergency);
  proximityManager.setLocalFriendRequest(
      miscController.friendBroadcastRequested());
  proximityManager.loop();
  miscController.setRemoteEmergency(proximityManager.remoteEmergencyActive());

  miscController.setFriendPairing(
      miscController.friendButtonHeld() &&
      proximityManager.mutualFriendRequestActive());
  if (miscController.friendPairingInProgress() &&
      millis() - lastFriendPrepareAttemptMs >= 1000U) {
    lastFriendPrepareAttemptMs = millis();
    proximityManager.prepareFriendPairing();
  }
  if (miscController.friendPairingReady() &&
      millis() - lastFriendPairAttemptMs >= 1000U) {
    lastFriendPairAttemptMs = millis();
    Rgbw remoteGlyph[LED_COUNT];
    Rgbw ownGlyph[LED_COUNT];
    String remoteUsername;
    if (proximityManager.completeFriendPairing(remoteGlyph, ownGlyph,
                                               remoteUsername)) {
      Serial.printf("Friends: paired with %s\n", remoteUsername.c_str());
      proximityManager.notifyFriendPairingComplete();
      miscController.showFriendPairingSuccess(remoteGlyph, ownGlyph);
    }
  }

  if (miscController.active()) {
    frameTransition.clear();
    playbackSuspended = true;
    return;
  }
  if (playbackSuspended) {
    playbackSuspended = false;
    renderedRevision = UINT32_MAX;
    lastPageChangeMs = millis();
  }
  if (renderFriendMode()) return;

  if (renderedRevision != displayRevision) {
    const bool modeChanged = renderedMode != displaySettings.mode;
    if (modeChanged) activePage = 0;
    if (modeChanged && displaySettings.mode == DisplayMode::GlyphRandom) {
      chooseRandomGlyph();
    }
    if (renderPage()) lastPageChangeMs = millis();
  }

  const uint32_t now = millis();
  if (advancePlayback(now)) lastPageChangeMs = now;

  const bool shouldCycle = displaySettings.mode == DisplayMode::GlyphRandom || pageCount() > 1;
  if (shouldCycle && !frameTransition.active() &&
      now - lastPageChangeMs >= displaySettings.intervalMs) {
    if (displaySettings.mode == DisplayMode::GlyphRandom) {
      chooseRandomGlyph();
    } else {
      activePage = (activePage + 1) % pageCount();
    }
    lastPageChangeMs = millis();
    renderPage(true);
  }
}
