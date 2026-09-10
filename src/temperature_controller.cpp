#include "temperature_controller.h"

#include <Wire.h>

#include "app_state.h"

namespace {
constexpr uint8_t TMP117_FIRST_ADDRESS = 0x48;
constexpr uint8_t TMP117_LAST_ADDRESS = 0x4b;
constexpr uint8_t TMP117_TEMPERATURE_REGISTER = 0x00;
constexpr uint8_t TMP117_DEVICE_ID_REGISTER = 0x0f;
constexpr uint16_t TMP117_DEVICE_ID = 0x0117;
constexpr uint32_t TMP117_FIRST_RESULT_DELAY_MS = 1100UL;
}

TemperatureController temperatureController;

void TemperatureController::begin() {
  enabled_ = globalSettings.hardwareRevision == 2;
  thermalPowerLimitMw = enabled_ ? 0UL : REV1_MAX_LED_POWER_MW;
  updatePowerLimit();
  if (!enabled_) {
    Serial.println("Temperature: Rev 1, external sensors disabled");
    return;
  }

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000UL);
  Wire.setTimeOut(50);
  discoverSensors();
  nextSampleMs_ = millis() + TMP117_FIRST_RESULT_DELAY_MS;
  Serial.printf("Temperature: Rev 2 I2C on SDA GPIO%u, SCL GPIO%u\n",
                I2C_SDA_PIN, I2C_SCL_PIN);
}

bool TemperatureController::loop() {
  if (!enabled_) return false;
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - nextSampleMs_) < 0) return false;
  nextSampleMs_ = now + TEMPERATURE_SAMPLE_INTERVAL_MS;

  if ((!centerAddress_ || !bottomAddress_) &&
      now - lastScanMs_ >= TEMPERATURE_SENSOR_RESCAN_MS) {
    discoverSensors();
  }
  if (centerAddress_) readTemperature(centerAddress_, center_);
  if (bottomAddress_) readTemperature(bottomAddress_, bottom_);
  return updateThermalLimit(millis());
}

bool TemperatureController::enabled() const { return enabled_; }

bool TemperatureController::sensorsReady() const {
  const uint32_t now = millis();
  return centerAddress_ && bottomAddress_ && readingValid(center_, now) &&
         readingValid(bottom_, now);
}

bool TemperatureController::centerValid() const {
  return centerAddress_ && readingValid(center_, millis());
}

bool TemperatureController::bottomValid() const {
  return bottomAddress_ && readingValid(bottom_, millis());
}

float TemperatureController::centerC() const {
  return centerValid() ? center_.raw / 128.0f : 0.0f;
}

float TemperatureController::bottomC() const {
  return bottomValid() ? bottom_.raw / 128.0f : 0.0f;
}

uint8_t TemperatureController::centerAddress() const {
  return centerAddress_;
}

uint8_t TemperatureController::bottomAddress() const {
  return bottomAddress_;
}

const char *TemperatureController::limiterState() const {
  if (!enabled_) return "disabled";
  if (!sensorsReady()) return "sensor fault - LEDs off";
  if (!thermalPowerLimitMw) return "thermal shutdown";
  if (thermalPowerLimitMw < REV2_MAX_LED_POWER_MW) return "derating";
  return "normal";
}

bool TemperatureController::discoverSensors() {
  lastScanMs_ = millis();
  uint8_t found[2] = {};
  uint8_t count = 0;
  for (uint8_t address = TMP117_FIRST_ADDRESS;
       address <= TMP117_LAST_ADDRESS && count < 2; ++address) {
    uint16_t deviceId = 0;
    if (readRegister(address, TMP117_DEVICE_ID_REGISTER, deviceId) &&
        (deviceId & 0x0fffU) == TMP117_DEVICE_ID) {
      found[count++] = address;
    }
  }
  centerAddress_ = count > 0 ? found[0] : 0;
  bottomAddress_ = count > 1 ? found[1] : 0;
  Serial.printf("Temperature: TMP117 center=%s0x%02X bottom=%s0x%02X\n",
                centerAddress_ ? "" : "missing/", centerAddress_,
                bottomAddress_ ? "" : "missing/", bottomAddress_);
  return count == 2;
}

bool TemperatureController::readRegister(uint8_t address,
                                         uint8_t registerAddress,
                                         uint16_t &value) const {
  Wire.beginTransmission(address);
  Wire.write(registerAddress);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(address, static_cast<uint8_t>(2)) != 2) return false;
  value = static_cast<uint16_t>(Wire.read()) << 8;
  value |= static_cast<uint8_t>(Wire.read());
  return true;
}

bool TemperatureController::readTemperature(uint8_t address,
                                            Reading &reading) const {
  uint16_t value = 0;
  if (!readRegister(address, TMP117_TEMPERATURE_REGISTER, value)) {
    if (!reading.hasSample) {
      Serial.printf("Temperature: read failed at 0x%02X\n", address);
    }
    return false;
  }
  const int16_t raw = static_cast<int16_t>(value);
  const int32_t milliC = static_cast<int32_t>(raw) * 1000L / 128L;
  if (milliC < -55000L || milliC > 150000L) {
    if (!reading.hasSample) {
      Serial.printf("Temperature: invalid startup value 0x%04X at 0x%02X\n",
                    value, address);
    }
    return false;
  }
  reading.raw = raw;
  reading.lastGoodMs = millis();
  reading.hasSample = true;
  return true;
}

bool TemperatureController::readingValid(const Reading &reading,
                                         uint32_t now) const {
  return reading.hasSample &&
         now - reading.lastGoodMs <= TEMPERATURE_SENSOR_STALE_MS;
}

int32_t TemperatureController::readingMilliC(const Reading &reading) const {
  return static_cast<int32_t>(reading.raw) * 1000L / 128L;
}

bool TemperatureController::updateThermalLimit(uint32_t now) {
  const uint32_t previous = thermalPowerLimitMw;
  bool readingsReady = false;
  if (!centerAddress_ || !bottomAddress_ || !readingValid(center_, now) ||
      !readingValid(bottom_, now)) {
    thermalPowerLimitMw = 0;
  } else {
    readingsReady = true;
    const int32_t hottest =
        max(readingMilliC(center_), readingMilliC(bottom_));
    thermalPowerLimitMw = thermalPowerLimitForMilliC(hottest);
  }
  updatePowerLimit();
  if (thermalPowerLimitMw != previous ||
      (readingsReady && !firstReadingReported_)) {
    Serial.printf("Temperature: center %.2f C, bottom %.2f C, LED cap %lu mW (%s)\n",
                  centerC(), bottomC(),
                  static_cast<unsigned long>(thermalPowerLimitMw),
                  limiterState());
  }
  if (readingsReady) firstReadingReported_ = true;
  return thermalPowerLimitMw != previous;
}
