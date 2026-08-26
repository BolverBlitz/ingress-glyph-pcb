#pragma once

#include <Arduino.h>

constexpr uint16_t ENCOUNTER_LAST_BIT = 0x8000;
constexpr uint16_t ENCOUNTER_USER_MASK = 0x7fff;
constexpr size_t REMOTE_USERNAME_MAX_BYTES = 64;

struct KnownUser {
  uint8_t deviceId[6] = {};
  uint16_t userId = 0;
  bool faction = false;
  bool friendFlag = false;
  String username;
};

struct UserHistory {
  uint32_t lastSeen = 0;
  uint64_t totalSeconds = 0;
};

bool beginProximityStore();
bool findKnownUserByDevice(const uint8_t deviceId[6], KnownUser &user);
bool findKnownUserById(uint16_t userId, KnownUser &user);
bool createKnownUser(const uint8_t deviceId[6], bool faction, KnownUser &user);
bool updateKnownUser(const KnownUser &user);
void forEachKnownUser(void (*callback)(const KnownUser &, void *), void *context);
void forEachEncounter(void (*callback)(uint16_t, bool, uint32_t, void *),
                      void *context);
bool appendEncounter(uint16_t userId, bool last, uint32_t timestamp);
UserHistory readUserHistory(uint16_t userId, uint32_t activeSince = 0, uint32_t activeNow = 0);
void readUserHistories(const uint16_t *userIds, UserHistory *histories,
                       const uint32_t *activeSince, size_t count,
                       uint32_t activeNow);
size_t proximityUsersBytes();
size_t proximityEncountersBytes();
bool resetProximityStore();
