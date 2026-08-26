#pragma once

#include <Arduino.h>

bool utcTimeValid();
uint32_t utcNow();
bool setUtcTime(uint32_t unixSeconds);
bool beginTimeService();
bool persistUtcTime();
bool loopTimeService();
bool ntpActive();
uint32_t lastNtpSyncUnix();
