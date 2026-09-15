#include "app_state.h"

DisplaySettings displaySettings;
GlobalSettings globalSettings;
Rgbw customPages[MAX_DRAW_PAGES][LED_COUNT] = {};
String customPageNames[MAX_DRAW_PAGES];
uint32_t powerLimitMw = 0;
uint32_t thermalPowerLimitMw = REV1_MAX_LED_POWER_MW;
uint32_t currentLedPowerMw = 0;
LedPowerLimitState ledPowerLimitState = LedPowerLimitState::NotLimited;
uint32_t displayRevision = 0;

void updatePowerLimit() {
  const uint32_t hardwareLimitMw =
      hardwareLedLimitMw(globalSettings.hardwareRevision);
  const uint32_t configuredPowerMw =
      min(globalSettings.maxPowerMw, hardwareLimitMw);
  powerLimitMw = min(configuredPowerMw, thermalPowerLimitMw);
}
