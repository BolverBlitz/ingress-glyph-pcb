#pragma once

#include "config.h"
#include "rgbw.h"

enum class DisplayMode : uint8_t { GlyphSequence, GlyphRandom, Draw };

struct DisplaySettings {
  DisplayMode mode = DisplayMode::GlyphSequence;
  uint16_t glyphs[MAX_PAGES] = {};
  uint8_t glyphCount = 1;
  uint8_t customPageCount = 1;
  uint32_t intervalMs = DEFAULT_INTERVAL_MS;
  Rgbw lineColor{0, 160, 255, 0};
  Rgbw coreColor{0, 255, 255, 0};
  uint8_t lineBrightnessPercent = 100;
  uint8_t coreBrightnessPercent = 100;
};

struct WifiProfile {
  String ssid;
  String password;
};

struct GlobalSettings {
  String username = "Agent";
  String apSsid;
  String apPassword = DEFAULT_AP_PASSWORD;
  bool enlightened = false;
  bool bleBroadcastEnabled = true;
  bool friendModeEnabled = false;
  bool use24HourTime = true;
  uint8_t rotationQuarterTurns = 0;
  uint32_t minPowerMw = DEFAULT_MIN_POWER_MW;
  uint32_t maxPowerMw = DEFAULT_MAX_POWER_MW;
  uint8_t brightnessPercent = DEFAULT_BRIGHTNESS_PERCENT;
  WifiProfile wifi[2];
};

extern DisplaySettings displaySettings;
extern GlobalSettings globalSettings;
extern Rgbw customPages[MAX_PAGES][LED_COUNT];
extern String customPageNames[MAX_PAGES];
extern uint32_t powerLimitMw;
extern uint32_t displayRevision;

void updatePowerLimit();
