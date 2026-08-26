#include "proximity_manager.h"

#include <NimBLEDevice.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "app_state.h"
#include "config.h"
#include "proximity_store.h"
#include "time_service.h"

namespace {
// BLE wire protocol
//
// Manufacturer advertising frame (9 bytes, sent every configured interval):
//   byte 0      0x44 ('D') magic
//   byte 1      0x42 ('B') magic
//   bytes 2..7  48-bit device ID derived from ESP.getEfuseMac(), MSB first
//   byte 8      bit 7: faction (0=RES, 1=ENL)
//               bits 6..0: message (0x00=normal, 0x01=emergency,
//                                   0x02=Button-2 friend request,
//                                   0x03=pairing completion acknowledgement)
//
// Username exchange (GATT read, only for an unknown device):
//   service UUID         44424700-696e-6772-6573-732d676c7970
//   characteristic UUID  44424701-696e-6772-6573-732d676c7970
//   value                0..64 UTF-8 bytes, no terminator or fixed padding
//
// Agent Glyph exchange (GATT read, requested only for friends/pairing):
//   characteristic UUID  44424702-696e-6772-6573-732d676c7970
//   value                'A','G', version 1, LED count, then RGBW pixels
//                        LED count 0 explicitly means no Agent Glyph is set
constexpr uint8_t MAGIC[2] = {'D', 'B'};
constexpr uint8_t MESSAGE_MASK = 0x7f;
constexpr uint8_t MESSAGE_NORMAL = 0x00; // Normal broadcast for device discovery
constexpr uint8_t MESSAGE_EMERGENCY = 0x01; // If received, another device is signaling an emergency
constexpr uint8_t MESSAGE_FRIEND_REQUEST = 0x02;
constexpr uint8_t MESSAGE_FRIEND_ACK = 0x03;
constexpr char SERVICE_UUID[] = "44424700-696e-6772-6573-732d676c7970";
constexpr char USERNAME_UUID[] = "44424701-696e-6772-6573-732d676c7970";
constexpr char AGENT_GLYPH_UUID[] = "44424702-696e-6772-6573-732d676c7970";
constexpr uint8_t AGENT_GLYPH_VERSION = 1;
constexpr size_t AGENT_GLYPH_HEADER_BYTES = 4;
constexpr size_t AGENT_GLYPH_BYTES = AGENT_GLYPH_HEADER_BYTES + sizeof(Rgbw) * LED_COUNT;
constexpr uint32_t NAME_RETRY_MS = 60UL * 1000UL;

class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *device) override {
    proximityManager.enqueueAdvertisement(device);
  }
  void onScanEnd(const NimBLEScanResults &, int) override {
    proximityManager.onScanEnded();
  }
};

ScanCallbacks scanCallbacks;

void localDeviceId(uint8_t output[6]) {
  const uint64_t id = ESP.getEfuseMac();
  for (uint8_t i = 0; i < 6; ++i) {
    output[i] = static_cast<uint8_t>(id >> ((5 - i) * 8));
  }
}

String formatDeviceId(const uint8_t deviceId[6]) {
  char text[13];
  snprintf(text, sizeof(text), "%02X%02X%02X%02X%02X%02X", deviceId[0],
           deviceId[1], deviceId[2], deviceId[3], deviceId[4], deviceId[5]);
  return String(text);
}

NimBLEAdvertisementData makeAdvertisementData(bool enlightened,
                                               uint8_t message) {
  uint8_t payload[9] = {MAGIC[0], MAGIC[1]};
  localDeviceId(payload + 2);
  payload[8] = (enlightened ? 0x80 : 0x00) | (message & MESSAGE_MASK);
  NimBLEAdvertisementData data;
  data.setManufacturerData(payload, sizeof(payload));
  return data;
}

constexpr uint16_t advertisingUnits(uint32_t milliseconds) {
  return static_cast<uint16_t>((milliseconds * 8UL + 2UL) / 5UL);
}
}  // namespace

ProximityManager proximityManager;

void ProximityManager::begin() {
  storeReady_ = beginProximityStore();
  if (!storeReady_) {
    Serial.println("BLE: proximity storage could not be initialized");
    return;
  }
  queue_ = xQueueCreate(16, sizeof(Observation));
  setEnabled(globalSettings.bleBroadcastEnabled);
}

void ProximityManager::setEnabled(bool enabled) {
  if (enabled && !storeReady_) {
    enabled_ = false;
    return;
  }
  if (enabled == enabled_ && initialized_ == enabled) return;
  enabled_ = enabled;
  if (enabled_) {
    WiFi.setSleep(true);
    startBle();
  } else {
    stopBle();
    WiFi.setSleep(false);
  }
}

void ProximityManager::setLocalEmergency(bool emergency) {
  localEmergency_ = emergency;
  localMessage_ = localEmergency_ ? MESSAGE_EMERGENCY
                                  : (localFriendRequest_
                                         ? MESSAGE_FRIEND_REQUEST
                                         : (static_cast<int32_t>(
                                                friendPairingAckUntilMs_ -
                                                millis()) > 0
                                                ? MESSAGE_FRIEND_ACK
                                                : MESSAGE_NORMAL));
}

void ProximityManager::setLocalFriendRequest(bool active) {
  localFriendRequest_ = active;
  localMessage_ = localEmergency_ ? MESSAGE_EMERGENCY
                                  : (localFriendRequest_
                                         ? MESSAGE_FRIEND_REQUEST
                                         : (static_cast<int32_t>(
                                                friendPairingAckUntilMs_ -
                                                millis()) > 0
                                                ? MESSAGE_FRIEND_ACK
                                                : MESSAGE_NORMAL));
}

void ProximityManager::notifyFriendPairingComplete() {
  friendPairingAckUntilMs_ = millis() + FRIEND_PAIRING_ACK_MS;
  if (!localEmergency_ && !localFriendRequest_) localMessage_ = MESSAGE_FRIEND_ACK;
}

void ProximityManager::startBle() {
  if (initialized_) return;
  NimBLEDevice::init("");
  NimBLEServer *server = NimBLEDevice::createServer();
  server->advertiseOnDisconnect(true);
  NimBLEService *service = server->createService(SERVICE_UUID);
  usernameCharacteristic_ = service->createCharacteristic(
      USERNAME_UUID, NIMBLE_PROPERTY::READ, REMOTE_USERNAME_MAX_BYTES);
  agentGlyphCharacteristic_ = service->createCharacteristic(
      AGENT_GLYPH_UUID, NIMBLE_PROPERTY::READ, AGENT_GLYPH_BYTES);
  advertisedUsername_ = globalSettings.username;
  usernameCharacteristic_->setValue(advertisedUsername_.c_str());
  refreshAgentGlyph();
  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->setAdvertisementData(
      makeAdvertisementData(globalSettings.enlightened, localMessage_));
  advertising->enableScanResponse(false);
  // NimBLE uses 0.625 ms units. Round the millisecond setting to the nearest
  // representable BLE advertising interval.
  advertising->setAdvertisingInterval(
      advertisingUnits(BLE_ADVERTISING_INTERVAL_MS));
  advertising->start();

  scan_ = NimBLEDevice::getScan();
  scan_->setActiveScan(true);
  scan_->setInterval(160);
  scan_->setWindow(80);
  scan_->setScanCallbacks(&scanCallbacks, true);
  scan_->setMaxResults(0);
  scan_->setDuplicateFilter(0);
  client_ = NimBLEDevice::createClient();
  client_->setConnectTimeout(BLE_CONNECT_TIMEOUT_MS);
  initialized_ = true;
  advertisedFaction_ = globalSettings.enlightened;
  advertisedMessage_ = localMessage_;
  startScan();
  Serial.println("BLE: proximity advertising and scanning started");
}

void ProximityManager::stopBle() {
  if (!initialized_) return;
  if (scan_) scan_->stop();
  NimBLEDevice::getAdvertising()->stop();
  NimBLEDevice::deinit(true);
  scan_ = nullptr;
  client_ = nullptr;
  usernameCharacteristic_ = nullptr;
  agentGlyphCharacteristic_ = nullptr;
  initialized_ = false;
  scanRestartNeeded_ = false;
  Serial.println("BLE: proximity disabled");
}

void ProximityManager::startScan() {
  if (!initialized_ || !scan_) return;
  scanRestartNeeded_ = false;
  scan_->clearResults();
  scan_->start(5000, false, true);
}

void ProximityManager::enqueueAdvertisement(
    const NimBLEAdvertisedDevice *device) {
  if (!enabled_ || !queue_ || !device ||
      device->getManufacturerDataCount() == 0) return;
  const std::string data = device->getManufacturerData();
  if (data.size() != 9 || static_cast<uint8_t>(data[0]) != MAGIC[0] ||
      static_cast<uint8_t>(data[1]) != MAGIC[1]) {
    return;
  }
  Observation observation{};
  memcpy(observation.deviceId, data.data() + 2, 6);
  uint8_t ownDeviceId[6];
  localDeviceId(ownDeviceId);
  if (memcmp(observation.deviceId, ownDeviceId, 6) == 0) return;
  const String address(device->getAddress().toString().c_str());
  address.toCharArray(observation.address, sizeof(observation.address));
  observation.addressType = device->getAddressType();
  observation.faction = (static_cast<uint8_t>(data[8]) & 0x80) != 0;
  observation.message = static_cast<uint8_t>(data[8]) & MESSAGE_MASK;
  observation.rssi = device->getRSSI();
  xQueueSend(static_cast<QueueHandle_t>(queue_), &observation, 0);
}

ProximityManager::NearbyEntry *ProximityManager::findNearby(
    const uint8_t deviceId[6]) {
  for (NearbyEntry &entry : nearby_) {
    if (entry.used && memcmp(entry.deviceId, deviceId, 6) == 0) return &entry;
  }
  return nullptr;
}

void ProximityManager::processObservation(const Observation &observation) {
  NearbyEntry *entry = findNearby(observation.deviceId);
  if (!entry) {
    for (NearbyEntry &candidate : nearby_) {
      if (!candidate.used) {
        entry = &candidate;
        break;
      }
    }
    if (!entry) return;
    KnownUser user;
    if (!findKnownUserByDevice(observation.deviceId, user) &&
        !createKnownUser(observation.deviceId, observation.faction, user)) {
      return;
    }
    *entry = NearbyEntry{};
    entry->used = true;
    memcpy(entry->deviceId, observation.deviceId, 6);
    entry->userId = user.userId;
    entry->faction = observation.faction;
    entry->friendFlag = user.friendFlag;
    entry->username = user.username;
    if (utcTimeValid()) {
      entry->firstSeenUnix = utcNow();
      entry->firstLogged = appendEncounter(entry->userId, false,
                                           entry->firstSeenUnix);
    }
  }
  entry->lastSeenMs = millis();
  entry->emergency = observation.message == MESSAGE_EMERGENCY;
  entry->message = observation.message;
  if (entry->rssiSampleCount < NEARBY_RSSI_AVERAGE_PACKETS) {
    entry->rssiSamples[entry->rssiSampleIndex] = observation.rssi;
    entry->rssiSum += observation.rssi;
    ++entry->rssiSampleCount;
  } else {
    entry->rssiSum -= entry->rssiSamples[entry->rssiSampleIndex];
    entry->rssiSamples[entry->rssiSampleIndex] = observation.rssi;
    entry->rssiSum += observation.rssi;
  }
  entry->rssiSampleIndex =
      (entry->rssiSampleIndex + 1) % NEARBY_RSSI_AVERAGE_PACKETS;
  entry->rssi = static_cast<int>(
      (entry->rssiSum - entry->rssiSampleCount / 2) /
      entry->rssiSampleCount);
  entry->address = observation.address;
  entry->addressType = observation.addressType;
  if (entry->faction != static_cast<bool>(observation.faction)) {
    entry->faction = observation.faction;
    KnownUser user;
    if (findKnownUserById(entry->userId, user)) {
      user.faction = entry->faction;
      updateKnownUser(user);
    }
  }
  if (entry->username.isEmpty() &&
      (!entry->lastNameAttemptMs ||
       millis() - entry->lastNameAttemptMs >= NAME_RETRY_MS)) {
    fetchUsername(*entry);
  }
  if (globalSettings.friendModeEnabled && entry->friendFlag &&
      !entry->glyphLoaded &&
      (!entry->lastGlyphAttemptMs ||
       millis() - entry->lastGlyphAttemptMs >= FRIEND_GLYPH_RETRY_MS)) {
    fetchAgentGlyph(*entry);
  }
}

String ProximityManager::safeRemoteUsername(const std::string &value) {
  size_t length = min<size_t>(value.size(), REMOTE_USERNAME_MAX_BYTES);
  if (length < value.size()) {
    while (length &&
           (static_cast<uint8_t>(value[length]) & 0xc0) == 0x80) {
      --length;
    }
  }
  String result(value.data(), length);
  result.trim();
  return result;
}

void ProximityManager::fetchUsername(NearbyEntry &entry) {
  entry.lastNameAttemptMs = millis();
  if (!client_ || entry.address.isEmpty()) return;
  if (scan_) scan_->stop();
  const bool connected = client_->connect(
      NimBLEAddress(std::string(entry.address.c_str()), entry.addressType));
  if (connected) {
    NimBLERemoteService *service = client_->getService(SERVICE_UUID);
    NimBLERemoteCharacteristic *characteristic =
        service ? service->getCharacteristic(USERNAME_UUID) : nullptr;
    if (characteristic && characteristic->canRead()) {
      const std::string rawValue = characteristic->readValue();
      const String username = safeRemoteUsername(rawValue);
      if (!username.isEmpty()) {
        entry.username = username;
        KnownUser user;
        if (findKnownUserById(entry.userId, user)) {
          user.username = username;
          user.faction = entry.faction;
          updateKnownUser(user);
        }
      }
    }
    client_->disconnect();
  } else {
    Serial.printf("BLE: username connection to %s failed (%d)\n",
                  entry.address.c_str(), client_->getLastError());
  }
  scanRestartNeeded_ = true;
}

bool ProximityManager::copyOwnAgentGlyph(Rgbw output[LED_COUNT]) const {
  for (uint8_t page = 0; page < displaySettings.customPageCount; ++page) {
    if (customPageNames[page] == globalSettings.username) {
      memcpy(output, customPages[page], sizeof(Rgbw) * LED_COUNT);
      return true;
    }
  }
  return false;
}

void ProximityManager::refreshAgentGlyph() {
  if (!agentGlyphCharacteristic_) return;
  uint8_t payload[AGENT_GLYPH_BYTES] = {'A', 'G', AGENT_GLYPH_VERSION, 0};
  Rgbw glyph[LED_COUNT];
  if (copyOwnAgentGlyph(glyph)) {
    payload[3] = LED_COUNT;
    memcpy(payload + AGENT_GLYPH_HEADER_BYTES, glyph, sizeof(glyph));
    agentGlyphCharacteristic_->setValue(payload, sizeof(payload));
  } else {
    agentGlyphCharacteristic_->setValue(payload, AGENT_GLYPH_HEADER_BYTES);
  }
  advertisedGlyphRevision_ = displayRevision;
}

bool ProximityManager::fetchAgentGlyph(NearbyEntry &entry) {
  entry.lastGlyphAttemptMs = millis();
  if (!client_ || entry.address.isEmpty()) return false;
  if (scan_) scan_->stop();
  const bool connected = client_->connect(
      NimBLEAddress(std::string(entry.address.c_str()), entry.addressType));
  bool valid = false;
  if (connected) {
    NimBLERemoteService *service = client_->getService(SERVICE_UUID);
    NimBLERemoteCharacteristic *characteristic =
        service ? service->getCharacteristic(AGENT_GLYPH_UUID) : nullptr;
    if (characteristic && characteristic->canRead()) {
      const std::string value = characteristic->readValue();
      const bool noGlyph = value.size() == AGENT_GLYPH_HEADER_BYTES &&
                           static_cast<uint8_t>(value[3]) == 0;
      const bool hasGlyph = value.size() == AGENT_GLYPH_BYTES &&
                            static_cast<uint8_t>(value[3]) == LED_COUNT;
      valid = value.size() >= AGENT_GLYPH_HEADER_BYTES && value[0] == 'A' &&
              value[1] == 'G' &&
              static_cast<uint8_t>(value[2]) == AGENT_GLYPH_VERSION &&
              (noGlyph || hasGlyph);
      if (valid) {
        memset(entry.glyph, 0, sizeof(entry.glyph));
        entry.glyphFetched = true;
        if (hasGlyph) {
          memcpy(entry.glyph, value.data() + AGENT_GLYPH_HEADER_BYTES,
                 sizeof(entry.glyph));
          entry.glyphLoaded = true;
        }
      }
    }
    client_->disconnect();
  }
  scanRestartNeeded_ = true;
  return valid;
}

void ProximityManager::expireNearby() {
  const uint32_t nowMs = millis();
  for (NearbyEntry &entry : nearby_) {
    if (entry.used && entry.glyphFetched &&
        ((!globalSettings.friendModeEnabled && !localFriendRequest_) ||
         (!entry.friendFlag && !localFriendRequest_) ||
         nowMs - entry.lastSeenMs >= FRIEND_PRESENT_TIMEOUT_MS)) {
      memset(entry.glyph, 0, sizeof(entry.glyph));
      entry.glyphLoaded = false;
      entry.glyphFetched = false;
      entry.lastGlyphAttemptMs = 0;
    }
    if (!entry.used || nowMs - entry.lastSeenMs < NEARBY_EXPIRY_MS) continue;
    if (entry.firstLogged && utcTimeValid()) {
      appendEncounter(entry.userId, true, utcNow());
    }
    entry = NearbyEntry{};
  }
}

void ProximityManager::loop() {
  if (!enabled_) return;
  Observation observation;
  uint8_t processed = 0;
  while (queue_ && processed++ < 8 &&
         xQueueReceive(static_cast<QueueHandle_t>(queue_), &observation, 0) ==
             pdTRUE) {
    processObservation(observation);
  }
  expireNearby();
  refreshIdentity();
  if (scanRestartNeeded_) startScan();
}

void ProximityManager::refreshIdentity() {
  if (!initialized_) return;
  if (advertisedUsername_ != globalSettings.username) {
    advertisedUsername_ = globalSettings.username;
    usernameCharacteristic_->setValue(advertisedUsername_.c_str());
  }
  if (advertisedGlyphRevision_ != displayRevision) refreshAgentGlyph();
  if (advertisedFaction_ != globalSettings.enlightened ||
      advertisedMessage_ != localMessage_) {
    NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
    advertising->stop();
    advertising->setAdvertisementData(
        makeAdvertisementData(globalSettings.enlightened, localMessage_));
    advertising->start();
    advertisedFaction_ = globalSettings.enlightened;
    advertisedMessage_ = localMessage_;
  }
}

void ProximityManager::onTimeSynchronized() {
  const uint32_t now = utcNow();
  for (NearbyEntry &entry : nearby_) {
    if (!entry.used || entry.firstLogged) continue;
    entry.firstSeenUnix = now;
    entry.firstLogged = appendEncounter(entry.userId, false, now);
  }
}

bool ProximityManager::resetStoredData() {
  if (!resetProximityStore()) return false;
  for (NearbyEntry &entry : nearby_) entry = NearbyEntry{};
  if (queue_) xQueueReset(static_cast<QueueHandle_t>(queue_));
  return true;
}

bool ProximityManager::setFriend(uint16_t userId, bool friendFlag) {
  KnownUser user;
  if (!findKnownUserById(userId, user)) return false;
  user.friendFlag = friendFlag;
  if (!updateKnownUser(user)) return false;
  for (NearbyEntry &entry : nearby_) {
    if (entry.used && entry.userId == userId) entry.friendFlag = friendFlag;
  }
  return true;
}

bool ProximityManager::mutualFriendRequestActive() const {
  if (!enabled_ || !localFriendRequest_) return false;
  const uint32_t now = millis();
  for (const NearbyEntry &entry : nearby_) {
    if (entry.used &&
        (entry.message == MESSAGE_FRIEND_REQUEST ||
         entry.message == MESSAGE_FRIEND_ACK) &&
        now - entry.lastSeenMs < FRIEND_PRESENT_TIMEOUT_MS &&
        entry.rssi >= FRIEND_PAIRING_MIN_RSSI_DBM) {
      return true;
    }
  }
  return false;
}

bool ProximityManager::prepareFriendPairing() {
  if (!enabled_ || !localFriendRequest_) return false;
  const uint32_t now = millis();
  for (NearbyEntry &entry : nearby_) {
    if (!entry.used ||
        (entry.message != MESSAGE_FRIEND_REQUEST &&
         entry.message != MESSAGE_FRIEND_ACK) ||
        now - entry.lastSeenMs >= FRIEND_PRESENT_TIMEOUT_MS ||
        entry.rssi < FRIEND_PAIRING_MIN_RSSI_DBM) {
      continue;
    }
    return entry.glyphFetched || fetchAgentGlyph(entry);
  }
  return false;
}

bool ProximityManager::completeFriendPairing(Rgbw remoteGlyph[LED_COUNT],
                                             Rgbw ownGlyph[LED_COUNT],
                                             String &username) {
  memset(ownGlyph, 0, sizeof(Rgbw) * LED_COUNT);
  copyOwnAgentGlyph(ownGlyph);
  const uint32_t now = millis();
  for (NearbyEntry &entry : nearby_) {
    if (!entry.used ||
        (entry.message != MESSAGE_FRIEND_REQUEST &&
         entry.message != MESSAGE_FRIEND_ACK) ||
        now - entry.lastSeenMs >= FRIEND_PRESENT_TIMEOUT_MS ||
        entry.rssi < FRIEND_PAIRING_MIN_RSSI_DBM) {
      continue;
    }
    if (!entry.glyphFetched && !fetchAgentGlyph(entry)) continue;
    if (!setFriend(entry.userId, true)) continue;
    memcpy(remoteGlyph, entry.glyph, sizeof(entry.glyph));
    username = entry.username.isEmpty() ? "Unknown Agent" : entry.username;
    return true;
  }
  return false;
}

uint8_t ProximityManager::loadedFriendGlyphCount() const {
  uint8_t count = 0;
  const uint32_t now = millis();
  for (const NearbyEntry &entry : nearby_) {
    if (entry.used && entry.friendFlag && entry.glyphLoaded &&
        now - entry.lastSeenMs < FRIEND_PRESENT_TIMEOUT_MS) {
      ++count;
    }
  }
  return count;
}

const Rgbw *ProximityManager::loadedFriendGlyph(uint8_t ordinal) const {
  const uint32_t now = millis();
  for (const NearbyEntry &entry : nearby_) {
    if (!entry.used || !entry.friendFlag || !entry.glyphLoaded ||
        now - entry.lastSeenMs >= FRIEND_PRESENT_TIMEOUT_MS) {
      continue;
    }
    if (!ordinal--) return entry.glyph;
  }
  return nullptr;
}

String ProximityManager::deviceIdText(const uint8_t deviceId[6]) {
  return formatDeviceId(deviceId);
}

void ProximityManager::writeNearby(JsonArray array) const {
  for (uint8_t friendsFirst = 0; friendsFirst < 2; ++friendsFirst) {
    const bool wantedFriend = friendsFirst == 0;
    for (const NearbyEntry &entry : nearby_) {
      if (!entry.used || entry.friendFlag != wantedFriend) continue;
      JsonObject item = array.add<JsonObject>();
      item["userId"] = entry.userId;
      item["deviceId"] = deviceIdText(entry.deviceId);
      item["username"] = entry.username;
      item["faction"] = entry.faction;
      item["friend"] = entry.friendFlag;
      item["emergency"] = entry.emergency;
      item["rssi"] = entry.rssi;
      item["firstSeen"] = entry.firstSeenUnix;
      item["lastReceivedAgo"] = (millis() - entry.lastSeenMs) / 1000U;
    }
  }
}

bool ProximityManager::remoteEmergencyActive() const {
  if (!enabled_) return false;
  for (const NearbyEntry &entry : nearby_) {
    if (!entry.used || !entry.emergency) continue;
    return true;
  }
  return false;
}

bool ProximityManager::firstRemoteEmergency(String &username,
                                            bool &faction) const {
  if (!enabled_) return false;
  for (const NearbyEntry &entry : nearby_) {
    if (!entry.used || !entry.emergency) continue;
    username = entry.username.isEmpty() ? "Unknown Agent" : entry.username;
    faction = entry.faction;
    return true;
  }
  return false;
}

namespace {
struct PeopleContext {
  JsonArray array;
  String query;
  uint16_t activeIds[32];
  uint32_t activeSince[32];
  uint8_t activeCount;
  uint8_t resultCount;
  uint16_t resultIds[100];
  uint32_t resultActiveSince[100];
};

void addMatchingUser(const KnownUser &user, void *opaque) {
  PeopleContext &context = *static_cast<PeopleContext *>(opaque);
  if (context.resultCount >= 100) return;
  String searchable = user.username;
  searchable.toLowerCase();
  if (!context.query.isEmpty() && searchable.indexOf(context.query) < 0) return;
  uint32_t activeSince = 0;
  for (uint8_t i = 0; i < context.activeCount; ++i) {
    if (context.activeIds[i] == user.userId) {
      activeSince = context.activeSince[i];
      break;
    }
  }
  JsonObject item = context.array.add<JsonObject>();
  item["userId"] = user.userId;
  item["deviceId"] = formatDeviceId(user.deviceId);
  item["username"] = user.username;
  item["faction"] = user.faction;
  item["friend"] = user.friendFlag;
  item["lastSeen"] = 0;
  item["totalSeconds"] = 0;
  context.resultIds[context.resultCount] = user.userId;
  context.resultActiveSince[context.resultCount] = activeSince;
  ++context.resultCount;
}
}  // namespace

void ProximityManager::writePeople(JsonArray array, const String &query) const {
  PeopleContext context{array, query, {}, {}, 0, 0, {}, {}};
  context.query.toLowerCase();
  for (const NearbyEntry &entry : nearby_) {
    if (!entry.used || context.activeCount >= MAX_NEARBY) continue;
    context.activeIds[context.activeCount] = entry.userId;
    context.activeSince[context.activeCount] = entry.firstSeenUnix;
    ++context.activeCount;
  }
  forEachKnownUser(addMatchingUser, &context);
  UserHistory histories[100] = {};
  const uint32_t now = utcTimeValid() ? utcNow() : 0;
  readUserHistories(context.resultIds, histories, context.resultActiveSince,
                    context.resultCount, now);
  for (uint8_t i = 0; i < context.resultCount; ++i) {
    JsonObject item = array[i].as<JsonObject>();
    item["lastSeen"] = histories[i].lastSeen;
    item["totalSeconds"] = histories[i].totalSeconds;
  }
}
