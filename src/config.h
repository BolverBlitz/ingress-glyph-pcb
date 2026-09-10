#pragma once

#include <Arduino.h>

#define FIRMWARE_VERSION "0.9.6"

// I/O Configuration
constexpr size_t LED_COUNT = 69;
constexpr uint8_t LED_PIN_REV1 = 2;  // WROOM-02 pad 16
constexpr uint8_t LED_PIN_REV2 = 19;  // WROOM-02 pad 14
constexpr uint8_t BUTTON_1_PIN = 1;  // WROOM-02 pad 17, active low
constexpr uint8_t BUTTON_2_PIN = 4;  // WROOM-02 pad 3, active low
constexpr uint8_t I2C_SDA_PIN = 5;
constexpr uint8_t I2C_SCL_PIN = 6;

// Hardware power limits
#define REV1_MAX_LED_POWER_MW 4500UL
#define REV2_MAX_LED_POWER_MW 8000UL

constexpr uint32_t hardwareLedLimitMw(uint8_t hardwareRevision) {
  return hardwareRevision == 2 ? REV2_MAX_LED_POWER_MW
                               : REV1_MAX_LED_POWER_MW;
}

// Default Configuration
constexpr uint32_t DEFAULT_MIN_POWER_MW = 0UL;
constexpr uint32_t DEFAULT_MAX_POWER_MW = REV1_MAX_LED_POWER_MW;
constexpr uint8_t DEFAULT_BRIGHTNESS_PERCENT = 50;

// LED Configuration
constexpr uint32_t RGB_CHANNEL_CURRENT_UA = 8000UL;
constexpr uint32_t WHITE_CHANNEL_CURRENT_UA = 16500UL;
constexpr uint32_t LED_SUPPLY_MV = 5000UL;
constexpr uint8_t MAX_PAGES = 5;
constexpr uint32_t DEFAULT_INTERVAL_MS = 2000UL;

// Rev 2 board temperature protection. The hottest LED-board sensor controls
// a linear power derating curve; an unavailable sensor produces a 0 mW cap.
constexpr int32_t LED_THERMAL_DERATE_START_MC = 50000;
constexpr int32_t LED_THERMAL_SHUTDOWN_MC = 70000;
constexpr uint32_t TEMPERATURE_SAMPLE_INTERVAL_MS = 1000UL;
constexpr uint32_t TEMPERATURE_SENSOR_STALE_MS = 5000UL;
constexpr uint32_t TEMPERATURE_SENSOR_RESCAN_MS = 5000UL;

// NTP
constexpr uint32_t UTC_CHECKPOINT_INTERVAL_MS = 7UL * 24UL * 60UL * 60UL * 1000UL;
constexpr uint32_t NTP_SYNC_INTERVAL_MS = 7UL * 24UL * 60UL * 60UL * 1000UL;
constexpr char NTP_SERVER_PRIMARY[] = "pool.ntp.org";
constexpr char NTP_SERVER_SECONDARY[] = "time.nist.gov";

// BLE
constexpr uint32_t BLE_ADVERTISING_INTERVAL_MS = 500UL;
static_assert(BLE_ADVERTISING_INTERVAL_MS >= 20UL && BLE_ADVERTISING_INTERVAL_MS <= 10240UL, "BLE advertising interval must be between 20 and 10240 ms");
constexpr uint32_t BLE_CONNECT_TIMEOUT_MS = 3000UL;
constexpr uint32_t REMOTE_EMERGENCY_WAVE_MS = 2400UL;
constexpr uint32_t REMOTE_EMERGENCY_FRAME_MS = 40UL;
// BLE Friendship
constexpr int16_t FRIEND_PAIRING_MIN_RSSI_DBM = -80;
constexpr uint32_t FRIEND_PAIRING_HOLD_MS = 5000UL;
constexpr uint32_t FRIEND_PAIRING_ACK_MS = 6000UL;
constexpr uint32_t FRIEND_PAIRING_EXCHANGE_TIMEOUT_MS = 8000UL;
constexpr uint32_t FRIEND_GLYPH_RETRY_MS = 15000UL;
constexpr uint32_t FRIEND_PRESENT_TIMEOUT_MS = 15000UL;
constexpr uint32_t NEARBY_EXPIRY_MS = 10UL * 60UL * 1000UL;
constexpr uint32_t FRIEND_MODE_FADE_MS = 2500UL;
constexpr uint32_t FRIEND_MODE_HOLD_MS = 1000UL;
// BLE Data
constexpr uint8_t NEARBY_RSSI_AVERAGE_PACKETS = 5;
static_assert(NEARBY_RSSI_AVERAGE_PACKETS >= 1 && NEARBY_RSSI_AVERAGE_PACKETS <= 32, "Nearby RSSI average must use between 1 and 32 packets");
constexpr size_t PROXIMITY_USERS_MAX_BYTES = 64UL * 1024UL; // 64 KB (LittleFS Max File Size)
constexpr size_t PROXIMITY_HISTORY_MAX_BYTES = 800UL * 1024UL; // 800 KB (LittleFS Max File Size)

// Button
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30UL;
constexpr uint32_t BUTTON_DOUBLE_PRESS_MS = 500UL;
constexpr uint32_t BUTTON_LONG_PRESS_MS = 2000UL;
constexpr uint32_t BUTTON_EMERGENCY_PRESS_MS = 10000UL;
constexpr uint32_t BUTTON_MODE_SAVE_DELAY_MS = 10000UL;

// Hotspot & Wi-Fi
constexpr char DEFAULT_AP_SSID_PREFIX[] = "DigitalBiocard ";
constexpr char DEFAULT_AP_PASSWORD[] = "Ingress1337";
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 12000UL;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 10000UL;
