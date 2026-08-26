#include "app_state.h"

DisplaySettings displaySettings;
GlobalSettings globalSettings;
Rgbw customPages[MAX_PAGES][LED_COUNT] = {};
String customPageNames[MAX_PAGES];
uint32_t powerLimitMw = 0;
uint32_t displayRevision = 0;

void updatePowerLimit() {
  powerLimitMw =
      (globalSettings.maxPowerMw * globalSettings.brightnessPercent) / 100U;
}
