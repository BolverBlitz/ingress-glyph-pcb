#include "time_service.h"

#include <LittleFS.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <sys/time.h>
#include <time.h>

#include "config.h"
#include "proximity_manager.h"
#include "proximity_store.h"

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
portMUX_TYPE ntpMutex = portMUX_INITIALIZER_UNLOCKED;
bool ntpSyncPending = false;
timeval pendingNtpTime{};
int64_t pendingNtpReceivedUs = 0;

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

bool changeSystemTime(const timeval &value) {
  // All history/file access stays on the main task, before changing UTC.
  const int64_t startedUs = esp_timer_get_time();
  if (!proximityManager.onTimeChanging()) {
    proximityManager.onTimeSynchronized();  // Reopen any successfully closed segments.
    return false;
  }
  timeval corrected = value;
  const int64_t microseconds = corrected.tv_usec + esp_timer_get_time() - startedUs;
  corrected.tv_sec += microseconds / 1000000;
  corrected.tv_usec = microseconds % 1000000;
  const bool applied = settimeofday(&corrected, nullptr) == 0;
  // Reopen on the unchanged clock too if applying UTC failed.
  proximityManager.onTimeSynchronized();
  return applied;
}
}  // namespace

// ESP-IDF explicitly provides this weak hook for replacing SNTP's time update.
// Defer applying it until the main task has flushed END records with old UTC.
extern "C" void sntp_sync_time(timeval *value) {
  if (!value || value->tv_sec < MIN_VALID_UNIX_TIME) return;
  const int64_t receivedUs = esp_timer_get_time();
  portENTER_CRITICAL(&ntpMutex);
  pendingNtpTime = *value;
  pendingNtpReceivedUs = receivedUs;
  ntpSyncPending = true;
  portEXIT_CRITICAL(&ntpMutex);
}

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
  const timeval value{static_cast<time_t>(unixSeconds), 0};
  if (unixSeconds < MIN_VALID_UNIX_TIME || !changeSystemTime(value)) {
    return false;
  }
  return persistUtcTime();
}

bool beginTimeService() {
  if (!LittleFS.begin(true)) return false;
  TimeCheckpoint checkpoint{};
  bool recoveredTemp = false;
  bool haveCheckpoint = readCheckpoint(TIME_FILE, checkpoint);
  if (!haveCheckpoint) {
    haveCheckpoint = readCheckpoint(TIME_TEMP, checkpoint);
    recoveredTemp = haveCheckpoint;
  }
  // Encounters can be newer than the weekly UTC checkpoint after a power loss.
  const uint32_t restoreTime = max<uint32_t>(haveCheckpoint ? checkpoint.unixSeconds : 0,
                                            latestEncounterTimestamp());
  if (restoreTime < MIN_VALID_UNIX_TIME) return false;
  if ((!utcTimeValid() || utcNow() < restoreTime) &&
      !applySystemTime(restoreTime)) {
    return false;
  }
  if (recoveredTemp) {
    LittleFS.remove(TIME_FILE);
    LittleFS.rename(TIME_TEMP, TIME_FILE);
  }
  lastCheckpointMs = millis();
  Serial.printf("UTC: restored checkpoint/history floor %lu\n",
                static_cast<unsigned long>(restoreTime));
  return true;
}

bool loopTimeService() {
  bool synchronized = false;
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  if (wifiConnected && !wifiWasConnected) {
    configTime(0, 0, NTP_SERVER_PRIMARY, NTP_SERVER_SECONDARY);
    sntp_set_sync_interval(NTP_SYNC_INTERVAL_MS);
    ntpStarted = true;
    Serial.printf("UTC: NTP sync started; next periodic sync in %lu ms\n",
                  static_cast<unsigned long>(NTP_SYNC_INTERVAL_MS));
  }
  wifiWasConnected = wifiConnected;
  timeval ntpTime{};
  int64_t receivedUs = 0;
  portENTER_CRITICAL(&ntpMutex);
  const bool haveNtpTime = ntpSyncPending;
  if (haveNtpTime) {
    ntpTime = pendingNtpTime;
    receivedUs = pendingNtpReceivedUs;
    ntpSyncPending = false;
  }
  portEXIT_CRITICAL(&ntpMutex);
  if (haveNtpTime) {
    // Include time spent waiting for the main loop, not just the server sample.
    const int64_t microseconds = ntpTime.tv_usec + esp_timer_get_time() - receivedUs;
    ntpTime.tv_sec += microseconds / 1000000;
    ntpTime.tv_usec = microseconds % 1000000;
    if (changeSystemTime(ntpTime)) {
      lastNtpSync = utcNow();
      persistUtcTime();
      sntp_set_sync_status(SNTP_SYNC_STATUS_COMPLETED);
      synchronized = true;
      Serial.printf("UTC: synchronized by NTP at %lu\n",
                    static_cast<unsigned long>(lastNtpSync));
    } else {
      Serial.println("UTC: NTP update deferred; encounter history could not be closed");
      // Retry the sample later, without overwriting a newer response.
      portENTER_CRITICAL(&ntpMutex);
      if (!ntpSyncPending) {
        pendingNtpTime = ntpTime;
        pendingNtpReceivedUs = esp_timer_get_time();
        ntpSyncPending = true;
      }
      portEXIT_CRITICAL(&ntpMutex);
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
