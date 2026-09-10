#pragma once

#include <Arduino.h>

bool beginSk6812Rmt(uint8_t pin);
bool showSk6812Rmt(const uint8_t *bytes, size_t byteCount);

