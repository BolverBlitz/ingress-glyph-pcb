#include "sk6812_rmt.h"

#include <driver/rmt.h>

#include "config.h"

namespace {

constexpr rmt_channel_t SK6812_RMT_CHANNEL = RMT_CHANNEL_0;
constexpr uint8_t SK6812_RMT_CLOCK_DIVIDER = 2;
constexpr uint16_t SK6812_T0H_TICKS = 12;  // 300 ns at 40 MHz
constexpr uint16_t SK6812_T0L_TICKS = 36;  // 900 ns at 40 MHz
constexpr uint16_t SK6812_T1H_TICKS = 28;  // 700 ns at 40 MHz
constexpr uint16_t SK6812_T1L_TICKS = 20;  // 500 ns at 40 MHz
constexpr uint32_t SK6812_RESET_US = 300;
constexpr size_t SK6812_ITEM_COUNT = LED_COUNT * 4U * 8U;

rmt_item32_t rmtItems[SK6812_ITEM_COUNT];
bool initialized = false;
uint32_t lastShowFinishedUs = 0;

}  // namespace

bool beginSk6812Rmt(uint8_t pin) {
  rmt_config_t config = RMT_DEFAULT_CONFIG_TX(
      static_cast<gpio_num_t>(pin), SK6812_RMT_CHANNEL);
  config.clk_div = SK6812_RMT_CLOCK_DIVIDER;
  // ESP32-C3 has two 48-symbol TX memory blocks. Reserve both so the driver
  // has twice as long to refill RMT memory while Wi-Fi and BLE are active.
  config.mem_block_num = 2;
  config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
  config.tx_config.idle_output_en = true;

  esp_err_t result = rmt_config(&config);
  if (result == ESP_OK) {
    result = rmt_driver_install(SK6812_RMT_CHANNEL, 0, 0);
  }
  initialized = result == ESP_OK;
  if (!initialized) {
    Serial.printf("LED: RMT initialization failed (%d)\n", result);
  }
  return initialized;
}

bool showSk6812Rmt(const uint8_t *bytes, size_t byteCount) {
  if (!initialized || bytes == nullptr || byteCount * 8U > SK6812_ITEM_COUNT) {
    return false;
  }

  size_t itemIndex = 0;
  for (size_t byteIndex = 0; byteIndex < byteCount; ++byteIndex) {
    for (uint8_t mask = 0x80; mask; mask >>= 1) {
      rmt_item32_t &item = rmtItems[itemIndex++];
      item.level0 = 1;
      item.level1 = 0;
      if (bytes[byteIndex] & mask) {
        item.duration0 = SK6812_T1H_TICKS;
        item.duration1 = SK6812_T1L_TICKS;
      } else {
        item.duration0 = SK6812_T0H_TICKS;
        item.duration1 = SK6812_T0L_TICKS;
      }
    }
  }

  while (static_cast<uint32_t>(micros() - lastShowFinishedUs) <
         SK6812_RESET_US) {
    delayMicroseconds(1);
  }
  const esp_err_t result =
      rmt_write_items(SK6812_RMT_CHANNEL, rmtItems, itemIndex, true);
  lastShowFinishedUs = micros();
  if (result != ESP_OK) {
    Serial.printf("LED: RMT transmission failed (%d)\n", result);
    return false;
  }
  return true;
}

