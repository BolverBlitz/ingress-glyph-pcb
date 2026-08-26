#include "time_service.h"

#include <LittleFS.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>

#include "config.h"

namespace {
constexpr uint32_t MIN_VALID_UNIX_TIME = 1700000000UL;
constexpr uint32_t TIME_MAGIC = 0x31435455UL;  // "UTC1"
constexpr char TIME_FILE[] = "/utc-time.bin";
constexpr char TIME_TEMP[] = "/utc-time.tmp";

#pragma pack(push, 1)
struct TimeCheckpoint {
  uint32_t magic;
  uint32_t unixSeconds;
  uint32_t checksum;
};
#pragma pack(pop)

uint32_t lastCheckpointMs = 0;
uint32_t lastNtpSync = 0;
bool ntpStarted = false;
bool wifiWasConnected = false;
volatile bool ntpSyncPending = false;

void onNtpTimeSynchronized(timeval *) { ntpSyncPending = true; }

uint32_t checkpointChecksum(uint32_t unixSeconds) {
  return TIME_MAGIC ^ unixSeconds ^ 0xa59c3e71UL;
}

bool validCheckpoint(const TimeCheckpoint &checkpoint) {
  return checkpoint.magic == TIME_MAGIC &&
         checkpoint.unixSeconds >= MIN_VALID_UNIX_TIME &&
         checkpoint.checksum == checkpointChecksum(checkpoint.unixSeconds);
}

bool readCheckpoint(const char *path, TimeCheckpoint &checkpoint) {
  File file = LittleFS.open(path, "r");
  if (!file) return false;
  const bool ok = file.size() == sizeof(checkpoint) &&
                  file.read(reinterpret_cast<uint8_t *>(&checkpoint),
                            sizeof(checkpoint)) == sizeof(checkpoint) &&
                  validCheckpoint(checkpoint);
  file.close();
  return ok;
}

bool applySystemTime(uint32_t unixSeconds) {
  timeval value{static_cast<time_t>(unixSeconds), 0};
  return settimeofday(&value, nullptr) == 0;
}
}  // namespace

bool utcTimeValid() {
  return static_cast<uint64_t>(time(nullptr)) >= MIN_VALID_UNIX_TIME;
}

uint32_t utcNow() {
  const time_t now = time(nullptr);
  return now > 0 ? static_cast<uint32_t>(now) : 0;
}

bool persistUtcTime() {
  if (!utcTimeValid() || !LittleFS.begin(true)) return false;
  const uint32_t now = utcNow();
  const TimeCheckpoint checkpoint{TIME_MAGIC, now, checkpointChecksum(now)};
  LittleFS.remove(TIME_TEMP);
  File file = LittleFS.open(TIME_TEMP, "w");
  if (!file) return false;
  const bool written =
      file.write(reinterpret_cast<const uint8_t *>(&checkpoint),
                 sizeof(checkpoint)) == sizeof(checkpoint);
  file.flush();
  file.close();
  if (!written) {
    LittleFS.remove(TIME_TEMP);
    return false;
  }
  LittleFS.remove(TIME_FILE);
  const bool saved = LittleFS.rename(TIME_TEMP, TIME_FILE);
  if (saved) lastCheckpointMs = millis();
  return saved;
}

bool setUtcTime(uint32_t unixSeconds) {
  if (unixSeconds < MIN_VALID_UNIX_TIME || !applySystemTime(unixSeconds)) {
    return false;
  }
  return persistUtcTime();
}

bool beginTimeService() {
  if (!LittleFS.begin(true)) return false;
  TimeCheckpoint checkpoint{};
  bool recoveredTemp = false;
  if (!readCheckpoint(TIME_FILE, checkpoint)) {
    if (!readCheckpoint(TIME_TEMP, checkpoint)) return false;
    recoveredTemp = true;
  }
  if ((!utcTimeValid() || utcNow() < checkpoint.unixSeconds) &&
      !applySystemTime(checkpoint.unixSeconds)) {
    return false;
  }
  if (recoveredTemp) {
    LittleFS.remove(TIME_FILE);
    LittleFS.rename(TIME_TEMP, TIME_FILE);
  }
  lastCheckpointMs = millis();
  Serial.printf("UTC: restored persistent checkpoint %lu\n",
                static_cast<unsigned long>(checkpoint.unixSeconds));
  return true;
}

bool loopTimeService() {
  bool synchronized = false;
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  if (wifiConnected && !wifiWasConnected) {
    sntp_set_time_sync_notification_cb(onNtpTimeSynchronized);
    configTime(0, 0, NTP_SERVER_PRIMARY, NTP_SERVER_SECONDARY);
    sntp_set_sync_interval(NTP_SYNC_INTERVAL_MS);
    ntpStarted = true;
    Serial.printf("UTC: NTP sync started; next periodic sync in %lu ms\n",
                  static_cast<unsigned long>(NTP_SYNC_INTERVAL_MS));
  }
  wifiWasConnected = wifiConnected;
  if (ntpSyncPending) {
    ntpSyncPending = false;
    if (utcTimeValid()) {
      lastNtpSync = utcNow();
      persistUtcTime();
      synchronized = true;
      Serial.printf("UTC: synchronized by NTP at %lu\n",
                    static_cast<unsigned long>(lastNtpSync));
    }
  }
  if (utcTimeValid() &&
      millis() - lastCheckpointMs >= UTC_CHECKPOINT_INTERVAL_MS) {
    persistUtcTime();
  }
  return synchronized;
}

bool ntpActive() { return ntpStarted && WiFi.status() == WL_CONNECTED; }

uint32_t lastNtpSyncUnix() { return lastNtpSync; }
