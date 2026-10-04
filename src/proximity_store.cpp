#include "proximity_store.h"

#include <LittleFS.h>

#include "config.h"
#include "encounter_timing.h"

namespace {
constexpr char USERS_FILE[] = "/ble-users.bin";
constexpr char ENCOUNTERS_FILE[] = "/ble-encounters.bin";
constexpr char ENCOUNTERS_META_FILE[] = "/ble-encounters.meta";
constexpr char ENCOUNTERS_TEMP_FILE[] = "/ble-encounters.tmp";
constexpr uint32_t ENCOUNTER_META_MAGIC = 0x31454c42UL;  // "BLE1"

#pragma pack(push, 1)
// LittleFS record layouts (not transmitted over BLE):
// UserRecordDisk, 75 bytes:
//   [device ID:6][user ID LE:2][faction:1][friend:1][name length:1][name:64]
struct UserRecordDisk {
  uint8_t deviceId[6];
  uint16_t userId;
  uint8_t faction;
  uint8_t friendFlag;
  uint8_t usernameLength;
  char username[REMOTE_USERNAME_MAX_BYTES];
};

// EncounterRecordDisk, 6 bytes:
//   [bit 15 FIRST=0/LAST=1 + user ID bits 14..0 LE:2][Unix UTC seconds LE:4]
struct EncounterRecordDisk {
  uint16_t userAndType;
  uint32_t timestamp;
};

struct EncounterMetaDisk {
  uint32_t magic;
  uint32_t count;
  uint32_t nextIndex;
};
#pragma pack(pop)

static_assert(sizeof(UserRecordDisk) == 75, "Unexpected BLE user record size");
static_assert(sizeof(EncounterRecordDisk) == 6, "Unexpected encounter record size");
static_assert(PROXIMITY_USERS_MAX_BYTES >= sizeof(UserRecordDisk), "Known-user store is too small");
static_assert(PROXIMITY_HISTORY_MAX_BYTES >= sizeof(EncounterRecordDisk), "Encounter store is too small");

constexpr size_t MAX_USER_RECORDS = PROXIMITY_USERS_MAX_BYTES / sizeof(UserRecordDisk);
constexpr size_t MAX_ENCOUNTER_RECORDS = PROXIMITY_HISTORY_MAX_BYTES / sizeof(EncounterRecordDisk);
constexpr size_t MAX_ENCOUNTER_FILE_BYTES = MAX_ENCOUNTER_RECORDS * sizeof(EncounterRecordDisk);

bool sameDevice(const uint8_t a[6], const uint8_t b[6]) {
  return memcmp(a, b, 6) == 0;
}

KnownUser fromDisk(const UserRecordDisk &record) {
  KnownUser user;
  memcpy(user.deviceId, record.deviceId, 6);
  user.userId = record.userId & ENCOUNTER_USER_MASK;
  user.faction = record.faction != 0;
  user.friendFlag = record.friendFlag != 0;
  const uint8_t length = min<uint8_t>(record.usernameLength, REMOTE_USERNAME_MAX_BYTES);
  user.username = String(record.username, length);
  return user;
}

UserRecordDisk toDisk(const KnownUser &user) {
  UserRecordDisk record{};
  memcpy(record.deviceId, user.deviceId, 6);
  record.userId = user.userId & ENCOUNTER_USER_MASK;
  record.faction = user.faction;
  record.friendFlag = user.friendFlag;
  record.usernameLength = min<size_t>(user.username.length(), REMOTE_USERNAME_MAX_BYTES);
  memcpy(record.username, user.username.c_str(), record.usernameLength);
  return record;
}

bool writeEncounterMeta(const EncounterMetaDisk &meta) {
  File file = LittleFS.open(ENCOUNTERS_META_FILE, "w");
  if (!file) return false;
  const bool ok = file.write(reinterpret_cast<const uint8_t *>(&meta), sizeof(meta)) == sizeof(meta);
  file.flush();
  file.close();
  return ok;
}

EncounterMetaDisk loadEncounterMeta() {
  File data = LittleFS.open(ENCOUNTERS_FILE, "r");
  const size_t records = data ? min<size_t>(data.size() / sizeof(EncounterRecordDisk), MAX_ENCOUNTER_RECORDS) : 0;
  if (data) data.close();

  EncounterMetaDisk meta{};
  File file = LittleFS.open(ENCOUNTERS_META_FILE, "r");
  const bool valid = file && file.read(reinterpret_cast<uint8_t *>(&meta), sizeof(meta)) == sizeof(meta) &&
                     meta.magic == ENCOUNTER_META_MAGIC && meta.count <= records &&
                     meta.count <= MAX_ENCOUNTER_RECORDS && meta.nextIndex < MAX_ENCOUNTER_RECORDS;
  if (file) file.close();
  if (valid &&
      ((records == MAX_ENCOUNTER_RECORDS) ||
       (meta.count == records && meta.nextIndex == records))) {
    return meta;
  }

  // Before the ring is full, file length is authoritative.
  // Keep memory of the next index to write, but reset the count to the file length.
  meta.magic = ENCOUNTER_META_MAGIC;
  meta.count = records;
  meta.nextIndex = records % MAX_ENCOUNTER_RECORDS;
  writeEncounterMeta(meta);
  return meta;
}

bool limitLegacyEncounterFile() {
  File source = LittleFS.open(ENCOUNTERS_FILE, "r");
  if (!source) return false;
  const size_t sourceBytes = source.size();
  const size_t sourceRecords = sourceBytes / sizeof(EncounterRecordDisk);
  if (sourceBytes <= MAX_ENCOUNTER_FILE_BYTES && sourceBytes % sizeof(EncounterRecordDisk) == 0) {
    source.close();
    return true;
  }

  // one six-byte buffer instead of loading the file into RAM.
  const size_t keepRecords = min(sourceRecords, MAX_ENCOUNTER_RECORDS);
  const size_t firstRecord = sourceRecords - keepRecords;
  if (!source.seek(firstRecord * sizeof(EncounterRecordDisk))) {
    source.close();
    return false;
  }
  LittleFS.remove(ENCOUNTERS_TEMP_FILE);
  File target = LittleFS.open(ENCOUNTERS_TEMP_FILE, "w");
  EncounterRecordDisk record;
  size_t copied = 0;
  while (target && copied < keepRecords &&
         source.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) == sizeof(record) &&
         target.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record)) == sizeof(record)) {
    ++copied;
  }
  source.close();
  if (target) {
    target.flush();
    target.close();
  }
  if (copied != keepRecords) {
    LittleFS.remove(ENCOUNTERS_TEMP_FILE);
    return false;
  }
  LittleFS.remove(ENCOUNTERS_FILE);
  return LittleFS.rename(ENCOUNTERS_TEMP_FILE, ENCOUNTERS_FILE);
}

bool limitLegacyUserFile() {
  File source = LittleFS.open(USERS_FILE, "r");
  if (!source) return false;
  const size_t sourceBytes = source.size();
  if (sourceBytes <= PROXIMITY_USERS_MAX_BYTES && sourceBytes % sizeof(UserRecordDisk) == 0) {
    source.close();
    return true;
  }
  constexpr char tempFile[] = "/ble-users.tmp";
  LittleFS.remove(tempFile);
  File target = LittleFS.open(tempFile, "w");
  UserRecordDisk record;
  size_t copied = 0;
  while (target && copied < MAX_USER_RECORDS &&
         source.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) == sizeof(record) &&
         target.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record)) == sizeof(record)) {
    ++copied;
  }
  source.close();
  if (target) {
    target.flush();
    target.close();
  }
  if (!copied && sourceBytes >= sizeof(UserRecordDisk)) {
    LittleFS.remove(tempFile);
    return false;
  }
  LittleFS.remove(USERS_FILE);
  return LittleFS.rename(tempFile, USERS_FILE);
}

template <typename Callback>
void streamEncounterRecords(Callback callback) {
  const EncounterMetaDisk meta = loadEncounterMeta();
  if (!meta.count) return;
  File file = LittleFS.open(ENCOUNTERS_FILE, "r");
  if (!file) return;
  const size_t startIndex = meta.count == MAX_ENCOUNTER_RECORDS ? meta.nextIndex : 0;
  EncounterRecordDisk record;
  const size_t firstCount = min<size_t>(meta.count, MAX_ENCOUNTER_RECORDS - startIndex);
  if (file.seek(startIndex * sizeof(record))) {
    for (size_t i = 0; i < firstCount; ++i) {
      if (file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) != sizeof(record)) break;
      callback(record);
    }
  }
  const size_t secondCount = meta.count - firstCount;
  if (secondCount && file.seek(0)) {
    for (size_t i = 0; i < secondCount; ++i) {
      if (file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) != sizeof(record)) break;
      callback(record);
    }
  }
  file.close();
}

void applyEncounter(UserHistory &history, uint32_t &openSince, const EncounterRecordDisk &record) {
  const bool last = (record.userAndType & ENCOUNTER_LAST_BIT) != 0;
  EncounterTiming::applyRecord(history.lastSeen, history.totalSeconds, openSince,
                              last, record.timestamp, NEARBY_EXPIRY_MS / 1000UL);
}
}  // namespace

bool beginProximityStore() {
  if (!LittleFS.begin(true)) return false;
  if (!LittleFS.exists(USERS_FILE)) {
    File file = LittleFS.open(USERS_FILE, "w");
    if (!file) return false;
    file.close();
  }
  if (!LittleFS.exists(ENCOUNTERS_FILE)) {
    File file = LittleFS.open(ENCOUNTERS_FILE, "w");
    if (!file) return false;
    file.close();
  }
  if (!limitLegacyUserFile()) return false;
  if (!limitLegacyEncounterFile()) return false;
  loadEncounterMeta();
  return true;
}

bool findKnownUserByDevice(const uint8_t deviceId[6], KnownUser &user) {
  File file = LittleFS.open(USERS_FILE, "r");
  if (!file) return false;
  UserRecordDisk record;
  while (file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) == sizeof(record)) {
    if (sameDevice(record.deviceId, deviceId)) {
      user = fromDisk(record);
      file.close();
      return true;
    }
  }
  file.close();
  return false;
}

bool findKnownUserById(uint16_t userId, KnownUser &user) {
  if (!userId || userId > MAX_USER_RECORDS) return false;
  File file = LittleFS.open(USERS_FILE, "r");
  if (!file || !file.seek((userId - 1U) * sizeof(UserRecordDisk))) return false;
  UserRecordDisk record;
  const bool ok = file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) == sizeof(record) &&
                  (record.userId & ENCOUNTER_USER_MASK) == userId;
  file.close();
  if (ok) user = fromDisk(record);
  return ok;
}

bool createKnownUser(const uint8_t deviceId[6], bool faction, KnownUser &user) {
  if (findKnownUserByDevice(deviceId, user)) return true;
  File file = LittleFS.open(USERS_FILE, "a");
  if (!file) return false;
  const size_t existingRecords = file.size() / sizeof(UserRecordDisk);
  if (existingRecords >= MAX_USER_RECORDS || file.size() + sizeof(UserRecordDisk) > PROXIMITY_USERS_MAX_BYTES) {
    file.close();
    return false;
  }
  memcpy(user.deviceId, deviceId, 6);
  user.userId = existingRecords + 1U;
  user.faction = faction;
  user.friendFlag = false;
  user.username = "";
  const UserRecordDisk record = toDisk(user);
  const bool ok = file.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record)) == sizeof(record);
  file.flush();
  file.close();
  return ok;
}

bool updateKnownUser(const KnownUser &user) {
  if (!user.userId || user.userId > MAX_USER_RECORDS) return false;
  File file = LittleFS.open(USERS_FILE, "r+");
  const size_t offset = (user.userId - 1U) * sizeof(UserRecordDisk);
  if (!file || offset + sizeof(UserRecordDisk) > file.size() || !file.seek(offset)) {
    if (file) file.close();
    return false;
  }
  UserRecordDisk existing;
  if (file.read(reinterpret_cast<uint8_t *>(&existing), sizeof(existing)) != sizeof(existing) ||
      (existing.userId & ENCOUNTER_USER_MASK) != user.userId || !file.seek(offset)) {
    file.close();
    return false;
  }
  const UserRecordDisk replacement = toDisk(user);
  const bool ok = file.write(reinterpret_cast<const uint8_t *>(&replacement), sizeof(replacement)) == sizeof(replacement);
  file.flush();
  file.close();
  return ok;
}

void forEachKnownUser(void (*callback)(const KnownUser &, void *), void *context) {
  File file = LittleFS.open(USERS_FILE, "r");
  if (!file) return;
  UserRecordDisk record;
  size_t count = 0;
  while (count++ < MAX_USER_RECORDS &&
         file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) == sizeof(record)) {
    const KnownUser user = fromDisk(record);
    callback(user, context);
  }
  file.close();
}

bool appendEncounter(uint16_t userId, bool last, uint32_t timestamp) {
  if (!timestamp || !userId || userId > ENCOUNTER_USER_MASK) return false;
  EncounterMetaDisk meta = loadEncounterMeta();
  File file = LittleFS.open(ENCOUNTERS_FILE, "r+");
  if (!file || !file.seek(meta.nextIndex * sizeof(EncounterRecordDisk))) {
    if (file) file.close();
    return false;
  }
  const EncounterRecordDisk record{
      static_cast<uint16_t>((userId & ENCOUNTER_USER_MASK) | (last ? ENCOUNTER_LAST_BIT : 0)), timestamp};
  const bool ok = file.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record)) == sizeof(record);
  file.flush();
  file.close();
  if (!ok) return false;
  meta.count = min<uint32_t>(meta.count + 1U, MAX_ENCOUNTER_RECORDS);
  meta.nextIndex = (meta.nextIndex + 1U) % MAX_ENCOUNTER_RECORDS;
  return writeEncounterMeta(meta);
}

UserHistory readUserHistory(uint16_t userId, uint32_t activeSince, uint32_t activeNow) {
  UserHistory history;
  uint32_t openSince = 0;
  streamEncounterRecords([&](const EncounterRecordDisk &record) {
    if ((record.userAndType & ENCOUNTER_USER_MASK) == userId) applyEncounter(history, openSince, record);
  });
  if (activeSince && activeNow >= activeSince) {
    history.totalSeconds += activeNow - activeSince;
    history.lastSeen = max(history.lastSeen, activeNow);
  } else if (openSince && activeNow) {
    history.totalSeconds += EncounterTiming::interruptedSeconds(
        openSince, activeNow, NEARBY_EXPIRY_MS / 1000UL);
  }
  return history;
}

void readUserHistories(const uint16_t *userIds, UserHistory *histories,
                       const uint32_t *activeSince, size_t count,
                       uint32_t activeNow, const uint32_t *activeElapsedSeconds) {
  if (!userIds || !histories || !count) return;
  count = min<size_t>(count, 100);
  uint32_t openSince[100] = {};
  streamEncounterRecords([&](const EncounterRecordDisk &record) {
    const uint16_t userId = record.userAndType & ENCOUNTER_USER_MASK;
    size_t low = 0;
    size_t high = count;
    while (low < high) {
      const size_t middle = low + (high - low) / 2;
      if (userIds[middle] < userId) low = middle + 1;
      else high = middle;
    }
    if (low < count && userIds[low] == userId) {
      applyEncounter(histories[low], openSince[low], record);
    }
  });
  for (size_t i = 0; i < count; ++i) {
    if (activeSince && activeSince[i] &&
        (activeElapsedSeconds || activeNow >= activeSince[i])) {
      histories[i].totalSeconds += activeElapsedSeconds
                                      ? activeElapsedSeconds[i]
                                      : activeNow - activeSince[i];
      histories[i].lastSeen = max(histories[i].lastSeen, activeNow);
    } else if (openSince[i] && activeNow) {
      histories[i].totalSeconds += EncounterTiming::interruptedSeconds(
          openSince[i], activeNow, NEARBY_EXPIRY_MS / 1000UL);
    }
  }
}

uint32_t latestEncounterTimestamp() {
  uint32_t latest = 0;
  streamEncounterRecords([&](const EncounterRecordDisk &record) {
    latest = max(latest, record.timestamp);
  });
  return latest;
}

size_t proximityUsersBytes() {
  File file = LittleFS.open(USERS_FILE, "r");
  if (!file) return 0;
  const size_t size = file.size();
  file.close();
  return size;
}

size_t proximityEncountersBytes() {
  File file = LittleFS.open(ENCOUNTERS_FILE, "r");
  if (!file) return 0;
  const size_t size = file.size();
  file.close();
  return size;
}

void forEachEncounter(
    void (*callback)(uint16_t, bool, uint32_t, void *), void *context) {
  if (!callback) return;
  streamEncounterRecords([&](const EncounterRecordDisk &record) {
    callback(record.userAndType & ENCOUNTER_USER_MASK,
             (record.userAndType & ENCOUNTER_LAST_BIT) != 0,
             record.timestamp, context);
  });
}

bool resetProximityStore() {
  LittleFS.remove(ENCOUNTERS_TEMP_FILE);
  LittleFS.remove(ENCOUNTERS_META_FILE);
  LittleFS.remove(ENCOUNTERS_FILE);
  LittleFS.remove("/ble-users.tmp");
  LittleFS.remove(USERS_FILE);

  File users = LittleFS.open(USERS_FILE, "w");
  if (!users) return false;
  users.close();
  File encounters = LittleFS.open(ENCOUNTERS_FILE, "w");
  if (!encounters) return false;
  encounters.close();
  const EncounterMetaDisk meta{ENCOUNTER_META_MAGIC, 0, 0};
  return writeEncounterMeta(meta);
}
