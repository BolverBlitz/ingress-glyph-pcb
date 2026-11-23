#pragma once

// Firmware IDs, timestamps and settings fit 32-bit integers. Sensor and power
// values are already float. The one 64-bit output counter is written as raw
// decimal JSON below, so the general-purpose extension pool is unnecessary.
#define ARDUINOJSON_USE_DOUBLE 0
#define ARDUINOJSON_USE_LONG_LONG 0
#include <ArduinoJson.h>
#include <cstdint>
#include <cstdio>

#if defined(ESP_PLATFORM)
static_assert(sizeof(ArduinoJson::JsonUInt) == 4, "JSON IDs must retain uint32_t range");
static_assert(sizeof(ArduinoJson::JsonFloat) == 4, "JSON must match sensor float precision");
#endif

inline bool setJsonUint64(JsonVariant target, uint64_t value) {
  char decimal[21];  // UINT64_MAX has 20 decimal digits, plus the terminator.
  std::snprintf(decimal, sizeof(decimal), "%llu",
                static_cast<unsigned long long>(value));
  // ArduinoJson copies serialized buffers into the document. The output is
  // still a JSON number, with no quoting, truncation or floating-point roundoff.
  return target.set(serialized(decimal));
}
