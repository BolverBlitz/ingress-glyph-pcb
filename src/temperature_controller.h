#pragma once

#include <Arduino.h>

#include "config.h"

constexpr uint32_t thermalPowerLimitForMilliC(int32_t temperatureMilliC) {
  return temperatureMilliC <= LED_THERMAL_DERATE_START_MC
             ? REV2_MAX_LED_POWER_MW
             : temperatureMilliC >= LED_THERMAL_SHUTDOWN_MC
                   ? 0UL
                   : ((static_cast<uint32_t>(LED_THERMAL_SHUTDOWN_MC -
                                             temperatureMilliC) *
                       REV2_MAX_LED_POWER_MW /
                       static_cast<uint32_t>(LED_THERMAL_SHUTDOWN_MC -
                                             LED_THERMAL_DERATE_START_MC)) /
                      50UL) *
                         50UL;
}

static_assert(thermalPowerLimitForMilliC(50000) == 8000,
              "Thermal derating must start at full power");
static_assert(thermalPowerLimitForMilliC(60000) == 4000,
              "Thermal derating midpoint must be half power");
static_assert(thermalPowerLimitForMilliC(70000) == 0,
              "Thermal shutdown must remove LED power");

class TemperatureController {
 public:
  void begin();
  bool loop();
  bool enabled() const;
  bool sensorsReady() const;
  bool centerValid() const;
  bool bottomValid() const;
  float centerC() const;
  float bottomC() const;
  uint8_t centerAddress() const;
  uint8_t bottomAddress() const;
  const char *limiterState() const;

 private:
  struct Reading {
    int16_t raw = 0;
    uint32_t lastGoodMs = 0;
    bool hasSample = false;
  };

  bool enabled_ = false;
  uint8_t centerAddress_ = 0;
  uint8_t bottomAddress_ = 0;
  Reading center_;
  Reading bottom_;
  uint32_t nextSampleMs_ = 0;
  uint32_t lastScanMs_ = 0;
  bool firstReadingReported_ = false;

  bool discoverSensors();
  bool readRegister(uint8_t address, uint8_t registerAddress,
                    uint16_t &value) const;
  bool readTemperature(uint8_t address, Reading &reading) const;
  bool readingValid(const Reading &reading, uint32_t now) const;
  int32_t readingMilliC(const Reading &reading) const;
  bool updateThermalLimit(uint32_t now);
};

extern TemperatureController temperatureController;
