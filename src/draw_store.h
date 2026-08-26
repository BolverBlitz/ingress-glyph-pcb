#pragma once

#include <Arduino.h>

constexpr size_t DRAW_PAGE_NAME_MAX_BYTES = 64;

void loadDrawPages();
bool saveDrawPages();

