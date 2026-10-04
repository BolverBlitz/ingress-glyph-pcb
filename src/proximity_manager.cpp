#include "proximity_manager.h"

#include <NimBLEDevice.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "app_state.h"
#include "config.h"
#include "encounter_timing.h"
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
//
// Coordinated friendship adds GATT status (...03, READ) and commands (...04, WRITE). See friend_pairing_protocol.h for the versioned FP packet format.
// Legacy broadcasts/identity reads remain compatible; pairing needs the new protocol on both devices and never falls back to unsynchronized timers.
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
uint64_t encounterNowMs() {
  return static_cast<uint64_t>(esp_timer_get_time()) / 1000U;
}
uint32_t newRequestId() {
  uint32_t id;
  do { id = esp_random(); } while (!id);
  return id;
}
static_assert(REMOTE_USERNAME_MAX_BYTES == FriendProtocol::NAME_BYTES,
              "Pairing username size needs a protocol version change");

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
  localDeviceId(ownDeviceId_);
  workerReady_ = queue_ && pairingServer_.begin() && worker_.begin();
  if (!workerReady_) {
    Serial.println("BLE: exchange task allocation failed; BLE remains disabled");
    storeReady_ = false;
    return;
  }
  publishPairingStatus();
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
    cancelFriendPairing();
    for (NearbyEntry &entry : nearby_) clearFriendRequests(entry);
    worker_.cancel();
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
  if (!active && localFriendRequest_) pairingRequestGate_.released();
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

void ProximityManager::startBle() {
  if (initialized_) {
    NimBLEDevice::getServer()->advertiseOnDisconnect(true);
    NimBLEDevice::getAdvertising()->start();
    startScan();
    return;
  }
  NimBLEDevice::init("");
  NimBLEServer *server = NimBLEDevice::createServer();
  server->advertiseOnDisconnect(true);
  NimBLEService *service = server->createService(SERVICE_UUID);
  usernameCharacteristic_ = service->createCharacteristic(
      USERNAME_UUID, NIMBLE_PROPERTY::READ, REMOTE_USERNAME_MAX_BYTES);
  agentGlyphCharacteristic_ = service->createCharacteristic(
      AGENT_GLYPH_UUID, NIMBLE_PROPERTY::READ, AGENT_GLYPH_BYTES);
  auto *status = service->createCharacteristic(
      BleExchange::STATUS_UUID, NIMBLE_PROPERTY::READ,
      FriendProtocol::HEADER_BYTES);
  auto *command = service->createCharacteristic(
      BleExchange::COMMAND_UUID, NIMBLE_PROPERTY::WRITE,
      FriendProtocol::OFFER_BYTES);
  auto *friendship = service->createCharacteristic(
      BleExchange::FRIENDSHIP_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE,
      FriendSync::PACKET_BYTES);
  status->setCallbacks(&pairingServer_);
  command->setCallbacks(&pairingServer_);
  friendship->setCallbacks(&pairingServer_);
  advertisedUsername_ = globalSettings.username;
  usernameCharacteristic_->setValue(advertisedUsername_.c_str());
  refreshAgentGlyph();
  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->setAdvertisementData(
      makeAdvertisementData(globalSettings.enlightened, localMessage_));
  advertising->enableScanResponse(false);
  // NimBLE uses 0.625 ms units. Round the millisecond setting to the nearest representable BLE advertising interval.
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
  NimBLEDevice::getServer()->advertiseOnDisconnect(false);
  NimBLEDevice::getAdvertising()->stop();
  // ATT operations may still be waiting in the worker. Never destroy NimBLE objects until its final result has been received, even on a single core.
  if (workerBusy_) return;
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
  if (!enabled_ || !initialized_ || !scan_ || workerBusy_) return;
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
  observation.receivedMs = millis();
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
    entry->firstSeenMonotonicMs = encounterNowMs();
    if (utcTimeValid()) {
      entry->firstSeenUnix = utcNow();
      entry->firstLogged = appendEncounter(entry->userId, false,
                                           entry->firstSeenUnix);
      entry->friendSyncPending = entry->firstLogged && entry->friendFlag;
      entry->visitLogged = entry->firstLogged;
    }
  }
  entry->lastSeenMs = observation.receivedMs;
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
    if (entry.firstLogged) {
      const uint32_t elapsed = EncounterTiming::elapsedSeconds(
          entry.firstSeenMonotonicMs, encounterNowMs());
      if (!appendEncounter(entry.userId, true,
                           EncounterTiming::endTimestamp(entry.firstSeenUnix, elapsed))) {
        continue;
      }
    }
    entry = NearbyEntry{};
  }
}

void ProximityManager::loop() {
  processExchangeResults();
  if (!enabled_) {
    BleExchange::Incoming discarded;
    while (pairingServer_.receive(discarded)) {}
    if (initialized_ && !workerBusy_) stopBle();
    return;
  }
  Observation observation;
  uint8_t processed = 0;
  while (queue_ && processed++ < 8 &&
         xQueueReceive(static_cast<QueueHandle_t>(queue_), &observation, 0) ==
             pdTRUE) {
    processObservation(observation);
  }
  expireNearby();
  expireFriendRequests();
  processPairingCommands();
  loopFriendPairing();
  // Pairing takes priority over optional background discovery. Both username and glyph are obtained in one connection without blocking LED rendering.
  if (!workerBusy_ && !localFriendRequest_ && !pairingSession_ &&
      FriendProtocol::reached(millis(), backgroundResumeMs_)) {
    const uint32_t now = millis();
    for (uint8_t offset = 0; offset < MAX_NEARBY; ++offset) {
      const uint8_t index = (backgroundCursor_ + offset) % MAX_NEARBY;
      NearbyEntry &entry = nearby_[index];
      if (!entry.used || now - entry.lastSeenMs >= FRIEND_PRESENT_TIMEOUT_MS) continue;
      const bool needName = entry.username.isEmpty() &&
          (!entry.lastNameAttemptMs || now - entry.lastNameAttemptMs >= NAME_RETRY_MS);
      const bool needGlyph = globalSettings.friendModeEnabled && entry.friendFlag && !entry.glyphFetched &&
          (!entry.lastGlyphAttemptMs || now - entry.lastGlyphAttemptMs >= FRIEND_GLYPH_RETRY_MS);
      const bool requestDue = !entry.lastRequestAttemptMs ||
          now - entry.lastRequestAttemptMs >= FriendSync::RETRY_MS;
      uint8_t command = 0;
      if (entry.removalNotice.due(now)) command = FriendSync::Sync;
      else if (requestDue && entry.cancelRequestId) command = FriendSync::Cancel;
      else if (requestDue && entry.acceptPending && entry.incomingRequest.active(now)) command = FriendSync::Accept;
      else if (requestDue && entry.outgoingRequest.active(now) && !entry.webConsent.active(now)) command = FriendSync::Request;
      else if (entry.friendFlag && entry.friendSyncPending) command = FriendSync::Sync;
      if ((command || needName || needGlyph) && queueExchange(entry, false, command)) {
        backgroundCursor_ = (index + 1) % MAX_NEARBY;
        break;
      }
    }
  }
  refreshIdentity();
  if (scanRestartNeeded_) startScan();
}

void ProximityManager::refreshIdentity() {
  if (!initialized_) return;
  if (!pairingSession_ && advertisedUsername_ != globalSettings.username) {
    advertisedUsername_ = globalSettings.username;
    usernameCharacteristic_->setValue(advertisedUsername_.c_str());
  }
  if (!pairingSession_ && advertisedGlyphRevision_ != displayRevision) refreshAgentGlyph();
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

bool ProximityManager::onTimeChanging() {
  const uint64_t nowMs = encounterNowMs();
  bool ok = true;
  for (NearbyEntry &entry : nearby_) {
    if (!entry.used || !entry.firstLogged) continue;
    const uint32_t elapsed = EncounterTiming::elapsedSeconds(
        entry.firstSeenMonotonicMs, nowMs);
    if (!appendEncounter(entry.userId, true,
                         EncounterTiming::endTimestamp(entry.firstSeenUnix, elapsed))) {
      ok = false;
      continue;
    }
    entry.firstLogged = false;
    entry.firstSeenUnix = 0;
    entry.firstSeenMonotonicMs = nowMs;
  }
  return ok;
}

void ProximityManager::onTimeSynchronized() {
  if (!utcTimeValid()) return;
  const uint32_t now = utcNow();
  const uint64_t nowMs = encounterNowMs();
  for (NearbyEntry &entry : nearby_) {
    if (!entry.used || entry.firstLogged) continue;
    // If this visit began with no usable UTC, retain its elapsed uptime too.
    entry.firstSeenUnix = EncounterTiming::backdatedStart(
        now, EncounterTiming::elapsedSeconds(entry.firstSeenMonotonicMs, nowMs));
    entry.firstLogged = appendEncounter(entry.userId, false, entry.firstSeenUnix);
    if (entry.firstLogged) {
      if (!entry.visitLogged) entry.friendSyncPending = entry.friendFlag;
      entry.visitLogged = true;
    }
  }
}

bool ProximityManager::resetStoredData() {
  cancelFriendPairing();
  worker_.cancel();
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
    if (entry.used && entry.userId == userId) {
      entry.friendFlag = friendFlag;
      entry.friendSyncPending = false;
      if (!friendFlag) {
        if (pairingSession_ && memcmp(pairingPeer_, entry.deviceId, 6) == 0) cancelFriendPairing();
        clearFriendRequests(entry);
        entry.removalNotice.clear();
        memset(entry.glyph, 0, sizeof(entry.glyph));
        entry.glyphFetched = entry.glyphLoaded = false;
      }
    }
  }
  return true;
}

void ProximityManager::clearFriendRequests(NearbyEntry &entry) {
  entry.outgoingRequest.clear();
  entry.incomingRequest.clear();
  entry.webConsent.clear();
  entry.acceptPending = false;
  entry.cancelRequestId = 0;
  entry.lastRequestAttemptMs = 0;
}

void ProximityManager::expireFriendRequests() {
  const uint32_t now = millis();
  for (NearbyEntry &entry : nearby_) {
    if (!entry.used) continue;
    if (!entry.removalNotice.pending.active(now)) entry.removalNotice.clear();
    if (!entry.outgoingRequest.active(now)) entry.outgoingRequest.clear();
    if (!entry.incomingRequest.active(now)) {
      if (entry.incomingRequest.id) entry.closedRequestId = entry.incomingRequest.id;
      entry.incomingRequest.clear();
      entry.acceptPending = false;
    }
    if (!entry.webConsent.active(now)) entry.webConsent.clear();
    if (entry.cancelRequestId && now - entry.cancelRequestMs >= FriendSync::REQUEST_TTL_MS)
      entry.cancelRequestId = 0;
  }
}

const char *ProximityManager::friendshipAction(uint16_t userId, const String &action) {
  KnownUser user;
  if (!userId || !findKnownUserById(userId, user)) return "Agent not found";
  NearbyEntry *entry = findNearby(user.deviceId);
  const uint32_t now = millis();
  if (action == "remove") {
    if (!setFriend(userId, false)) return "Could not save friendship";
    if (enabled_ && entry && now - entry->lastSeenMs < FRIEND_PRESENT_TIMEOUT_MS)
      entry->removalNotice.start(newRequestId(), now);
    return nullptr;
  }
  if (action == "cancel" && entry) {
    const uint32_t id = entry->outgoingRequest.active(now) ? entry->outgoingRequest.id :
        (entry->incomingRequest.active(now) ? entry->incomingRequest.id : entry->webConsent.id);
    if (!id) return "No pending request";
    if (pairingSession_ && memcmp(pairingPeer_, entry->deviceId, 6) == 0) cancelFriendPairing();
    clearFriendRequests(*entry);
    entry->cancelRequestId = id;
    entry->cancelRequestMs = now;
    entry->closedRequestId = id;
    return nullptr;
  }
  if (action != "request" && action != "accept") return "Invalid friendship action";
  if (!enabled_ || !entry || now - entry->lastSeenMs >= FRIEND_PRESENT_TIMEOUT_MS)
    return "Agent must be nearby with Bluetooth enabled";
  if (localEmergency_ || remoteEmergencyActive() || pairingSession_ || localFriendRequest_)
    return "Friend pairing is busy";
  if (entry->friendFlag) return "Agent is already a friend";
  if (action == "request") {
    if (entry->outgoingRequest.active(now) || entry->incomingRequest.active(now) ||
        entry->webConsent.active(now)) return "A friendship request is already pending";
    entry->cancelRequestId = 0;
    entry->removalNotice.clear();
    entry->outgoingRequest.set(newRequestId(), now);
  } else {
    if (!entry->incomingRequest.active(now)) return "Friendship request has expired";
    entry->acceptPending = true;
  }
  entry->lastRequestAttemptMs = 0;
  return nullptr;
}

bool ProximityManager::queueExchange(NearbyEntry &entry, bool pairing,
                                     uint8_t friendshipCommand) {
  if (!workerReady_ || workerBusy_ || !client_ || entry.address.isEmpty()) return false;
  BleExchange::Job job;
  job.client = client_;
  memcpy(job.peer, entry.deviceId, 6);
  entry.address.toCharArray(job.address, sizeof(job.address));
  job.addressType = entry.addressType;
  job.pairing = pairing;
  job.session = pairing ? pairingSession_ : 0;
  memcpy(job.ownId, ownDeviceId_, 6);
  job.own = pairingOwn_;
  job.webRequestId = pairing ? pairingWebRequestId_ : 0;
  job.readGlyph = pairing || (globalSettings.friendModeEnabled && entry.friendFlag && !entry.glyphFetched);
  job.friendshipCommand = friendshipCommand;
  job.friendFlag = entry.friendFlag;
  job.queuedMs = millis();
  if (friendshipCommand == FriendSync::Request) {
    job.requestId = entry.outgoingRequest.id;
    job.requestRemainingMs = entry.outgoingRequest.remaining(millis());
  } else if (friendshipCommand == FriendSync::Accept) {
    job.requestId = entry.incomingRequest.id;
  } else if (friendshipCommand == FriendSync::Cancel) {
    job.requestId = entry.cancelRequestId;
  } else if (friendshipCommand == FriendSync::Sync) {
    job.requestId = entry.removalNotice.due(millis()) ? entry.removalNotice.pending.id : newRequestId();
  }
  if (scan_) scan_->stop();
  if (!worker_.submit(job)) {
    scanRestartNeeded_ = true;
    return false;
  }
  workerBusy_ = true;
  entry.lastNameAttemptMs = entry.lastGlyphAttemptMs = millis();
  if (friendshipCommand == FriendSync::Sync) {
    entry.friendSyncPending = false;
    if (job.requestId == entry.removalNotice.pending.id) entry.removalNotice.sent(millis());
  }
  else if (friendshipCommand) entry.lastRequestAttemptMs = millis();
  return true;
}

void ProximityManager::applyIdentity(NearbyEntry &entry,
    const BleExchange::Identity &identity, bool glyphValid, bool persistName) {
  const String username = safeRemoteUsername(std::string(identity.username));
  if (!username.isEmpty() && username != entry.username) {
    entry.username = username;
    KnownUser user;
    if (persistName && findKnownUserById(entry.userId, user)) {
      user.username = username;
      updateKnownUser(user);
    }
  }
  if (glyphValid) {
    memcpy(entry.glyph, identity.glyph, sizeof(entry.glyph));
    entry.glyphFetched = true;
    entry.glyphLoaded = identity.hasGlyph;
  }
}

void ProximityManager::publishPairingStatus() {
  const uint8_t phase = !pairingSession_ || pairingCoordinator_ ? FriendProtocol::Idle :
      (pairingScheduled_ ? FriendProtocol::Scheduled : FriendProtocol::Ready);
  pairingServer_.publish(phase, pairingSession_, ownDeviceId_);
}

void ProximityManager::cancelFriendPairing(bool retryPreparation) {
  if (pairingSession_) {
    Serial.printf("Friends: session %lu ended%s\n", (unsigned long)pairingSession_,
                  pairingCompleted_ ? "" : " before completion");
    worker_.cancel();
    const uint32_t now = millis();
    const bool animationStarted = pairingScheduled_ &&
        FriendProtocol::reached(now, pairingStartMs_);
    pairingRequestGate_.sessionEnded(now, pairingCompleted_, localFriendRequest_,
                                     retryPreparation, animationStarted);
    if (pairingWebRequestId_) {
      pairingRequestGate_.sessionEnded(now, pairingCompleted_, true,
                                       retryPreparation, animationStarted);
      if (auto *entry = findNearby(pairingPeer_)) {
        if (pairingCompleted_ || !retryPreparation || animationStarted) {
          entry->closedRequestId = pairingWebRequestId_;
          clearFriendRequests(*entry);
        }
      }
    }
  }
  pairingSession_ = 0;
  pairingWebRequestId_ = 0;
  pairingScheduled_ = pairingCompleted_ = pairingDelivered_ = false;
  pairingOwn_ = BleExchange::Identity{};
  pairingRemote_ = BleExchange::Identity{};
  publishPairingStatus();
}

void ProximityManager::processExchangeResults() {
  BleExchange::Result result;
  while (worker_.receive(result)) {
    if (result.finished) {
      workerBusy_ = false;
      scanRestartNeeded_ = true;
      // Leave time to observe fresh advertisements between background reads.
      backgroundResumeMs_ = millis() + BLE_ADVERTISING_INTERVAL_MS + 100;
    }
    if (!enabled_ || result.generation != worker_.generation()) continue;
    NearbyEntry *entry = findNearby(result.peer);
    if (!result.session) {
      if (!entry) continue;
      applyIdentity(*entry, result.remote, result.identityValid);
      if (!result.friendshipValid) continue;
      const uint32_t now = millis();
      if (result.friendshipCommand == FriendSync::Sync)
        entry->removalNotice.confirm(result.requestId, result.remoteFriend, result.friendshipResult);
      if (result.friendshipCommand == FriendSync::Sync &&
          result.friendshipResult == FriendSync::Ok && !result.remoteFriend && entry->friendFlag) {
        if (!pairingSession_ || memcmp(pairingPeer_, entry->deviceId, 6) != 0) setFriend(entry->userId, false);
      } else if (result.friendshipCommand == FriendSync::Request &&
                 entry->outgoingRequest.canConsent(result.requestId, now)) {
        if (result.friendshipResult == FriendSync::Rejected) {
          entry->closedRequestId = result.requestId;
          entry->outgoingRequest.clear();
        }
        // An accepted request is still the sender's original consent.
        else if (result.friendshipResult == FriendSync::Accepted) {
          entry->outgoingRequest.grantConsent(result.requestId, now, entry->webConsent);
          pairingRequestGate_.released();
        }
      } else if (result.friendshipCommand == FriendSync::Accept && entry->acceptPending &&
                 entry->incomingRequest.canConsent(result.requestId, now)) {
        if (result.friendshipResult == FriendSync::Accepted) {
          entry->incomingRequest.grantConsent(result.requestId, now, entry->webConsent);
          entry->acceptPending = false;
          pairingRequestGate_.released();
        } else if (result.friendshipResult == FriendSync::Rejected) {
          entry->closedRequestId = result.requestId;
          entry->incomingRequest.clear();
          entry->acceptPending = false;
        }
      } else if (result.friendshipCommand == FriendSync::Cancel &&
                 entry->cancelRequestId == result.requestId && result.friendshipResult == FriendSync::Ok) {
        entry->cancelRequestId = 0;
      }
      continue;
    }
    if (result.session != pairingSession_ || memcmp(result.peer, pairingPeer_, 6) != 0) continue;
    if (result.failed && !pairingCompleted_) {
      cancelFriendPairing(true);
      continue;
    }
    if (result.contactMs) pairingContactMs_ = result.contactMs;
    if (result.scheduled && !pairingScheduled_) {
      if (!FriendProtocol::validStart(millis(), result.startMs)) {
        cancelFriendPairing(true);
        continue;
      }
      pairingRemote_ = result.remote;
      pairingStartMs_ = result.startMs;
      pairingScheduled_ = true;
    }
  }
}

void ProximityManager::processFriendshipCommand(const BleExchange::Incoming &incoming,
                                               NearbyEntry &entry) {
  FriendSync::Packet request;
  if (!FriendSync::decode(incoming.bytes, incoming.size, request) || request.type == FriendSync::Response) return;
  if (memcmp(request.sender, entry.deviceId, 6) != 0) return;
  const uint32_t now = millis();
  const uint32_t queued = now - incoming.receivedMs;
  request.remainingMs = request.remainingMs > queued ? request.remainingMs - queued : 0;
  uint8_t result = FriendSync::Rejected;
  const bool sameSession = pairingSession_ && memcmp(pairingPeer_, entry.deviceId, 6) == 0;
  const bool busy = pairingSession_ || localFriendRequest_ || localEmergency_ || remoteEmergencyActive();
  if (request.type == FriendSync::Sync) {
    if (!request.friendFlag && entry.friendFlag) {
      if (setFriend(entry.userId, false)) result = FriendSync::Ok;
    } else if (sameSession) result = FriendSync::Busy;
    else result = FriendSync::Ok;
    // A successful inbound check also verifies this visit's friendship.
    if (result == FriendSync::Ok) entry.friendSyncPending = false;
  } else if (request.type == FriendSync::Request) {
    if (entry.webConsent.canConsent(request.nonce, now)) result = FriendSync::Accepted;
    else if (entry.closedRequestId == request.nonce || !request.remainingMs) result = FriendSync::Rejected;
    else if (busy || (entry.incomingRequest.active(now) && entry.incomingRequest.id != request.nonce)) result = FriendSync::Busy;
    else if (entry.outgoingRequest.active(now) && memcmp(ownDeviceId_, entry.deviceId, 6) < 0) result = FriendSync::Rejected;
    else {
      // Crossed requests converge on the request sent by the lower device ID.
      entry.outgoingRequest.clear();
      if (!entry.incomingRequest.canConsent(request.nonce, now)) {
        if (entry.friendFlag && !setFriend(entry.userId, false)) result = FriendSync::Busy;
        else {
          entry.incomingRequest.observe(request.nonce, now, request.remainingMs);
          result = FriendSync::Pending;
        }
      } else result = FriendSync::Pending; // Retries never extend the deadline.
    }
  } else if (request.type == FriendSync::Accept) {
    if (entry.webConsent.canConsent(request.nonce, now)) result = FriendSync::Accepted;
    else if (busy) result = FriendSync::Busy;
    else if (entry.outgoingRequest.canConsent(request.nonce, now)) {
      entry.outgoingRequest.grantConsent(request.nonce, now, entry.webConsent);
      pairingRequestGate_.released();
      result = FriendSync::Accepted;
    }
  } else if (request.type == FriendSync::Cancel) {
    if (entry.outgoingRequest.id == request.nonce || entry.incomingRequest.id == request.nonce ||
        entry.webConsent.id == request.nonce) {
      if (sameSession && !pairingCompleted_) cancelFriendPairing();
      clearFriendRequests(entry);
    }
    entry.closedRequestId = request.nonce;
    result = FriendSync::Ok;
  }
  uint8_t response[FriendSync::PACKET_BYTES];
  FriendSync::packet(response, FriendSync::Response, request.nonce, ownDeviceId_, 0, entry.friendFlag, result);
  pairingServer_.publishFriendship(response);
}

void ProximityManager::processPairingCommands() {
  using namespace FriendProtocol;
  BleExchange::Incoming incoming;
  while (pairingServer_.receive(incoming)) {
    if (millis() - incoming.receivedMs >= FriendProtocol::HEARTBEAT_TIMEOUT_MS) continue;
    const uint8_t *bytes = incoming.bytes;
    NearbyEntry *entry = findNearby(bytes + 8);
    if (!entry || !entry->address.equalsIgnoreCase(incoming.address)) continue;
    if (incoming.friendship) {
      processFriendshipCommand(incoming, *entry);
      continue;
    }
    const uint32_t session = read32(bytes + 4);
    if (bytes[3] == Offer || bytes[3] == WebOffer) {
      const bool web = bytes[3] == WebOffer;
      const uint32_t requestId = web ? read32(bytes + 14) : 0;
      const bool consent = web ? entry->webConsent.canConsent(requestId, millis()) : localFriendRequest_;
      // A fresh eligible gesture after the success animation may replace the
      // retained completion status; the ACK retention is not a UI cooldown.
      if (pairingCompleted_ && localFriendRequest_) cancelFriendPairing();
      const uint32_t now = millis();
      const OfferDecision decision = offerDecision(pairingSession_ != 0,
          consent, localEmergency_ || remoteEmergencyActive(),
          pairingRequestGate_.canAttempt(now), entry->deviceId, ownDeviceId_,
          now - entry->lastSeenMs, FRIEND_PRESENT_TIMEOUT_MS, entry->rssi,
          FRIEND_PAIRING_MIN_RSSI_DBM);
      if (decision != OfferDecision::Accept) {
        Serial.printf("Friends: rejected session %lu (offer reason %u)\n",
                      (unsigned long)session, static_cast<unsigned>(decision));
        continue;
      }
      // A background read may be finishing, but never start a second client.
      worker_.cancel();
      pairingSession_ = session;
      pairingWebRequestId_ = requestId;
      memcpy(pairingPeer_, entry->deviceId, 6);
      pairingCoordinator_ = false;
      pairingPreparedMs_ = incoming.receivedMs;
      pairingRequestGate_.attempted(now);
      pairingRemote_ = BleExchange::Identity{};
      pairingRemote_.hasGlyph = bytes[18];
      memcpy(pairingRemote_.username, bytes + 20, bytes[19]);
      if (pairingRemote_.hasGlyph) memcpy(pairingRemote_.glyph, bytes + 20 + NAME_BYTES, sizeof(pairingRemote_.glyph));
      pairingOwn_ = BleExchange::Identity{};
      safeRemoteUsername(std::string(globalSettings.username.c_str())).toCharArray(
          pairingOwn_.username, sizeof(pairingOwn_.username));
      pairingOwn_.hasGlyph = copyOwnAgentGlyph(pairingOwn_.glyph);
      usernameCharacteristic_->setValue(pairingOwn_.username);
      advertisedUsername_ = pairingOwn_.username;
      refreshAgentGlyph();
      publishPairingStatus();
      Serial.printf("Friends: accepted session %lu\n", (unsigned long)session);
      continue;
    }
    if (pairingCoordinator_ || session != pairingSession_ || memcmp(bytes + 8, pairingPeer_, 6) != 0) continue;
    if (bytes[3] == Cancel) {
      if (!pairingCompleted_) cancelFriendPairing(true);
    } else if (bytes[3] == Start && !pairingScheduled_) {
      const uint32_t start = read32(bytes + 14);
      if ((!localFriendRequest_ && !pairingWebRequestId_) || localEmergency_ || !validStart(millis(), start)) {
        cancelFriendPairing(true);
        continue;
      }
      pairingStartMs_ = start;
      pairingContactMs_ = incoming.receivedMs;
      pairingScheduled_ = true;
      publishPairingStatus();
    } else if (bytes[3] == Heartbeat && pairingScheduled_) {
      pairingContactMs_ = incoming.receivedMs;
    }
  }
}

void ProximityManager::loopFriendPairing() {
  using namespace FriendProtocol;
  const uint32_t now = millis();
  if (pairingCompleted_ && localFriendRequest_) cancelFriendPairing();
  if (pairingSession_) {
    const HoldDecision decision = holdDecision(now, pairingPreparedMs_, pairingStartMs_,
        pairingContactMs_, pairingScheduled_, pairingCompleted_, localFriendRequest_ || pairingWebRequestId_,
        localEmergency_ || remoteEmergencyActive(), FRIEND_PAIRING_EXCHANGE_TIMEOUT_MS,
        FRIEND_PAIRING_HOLD_MS, FRIEND_PAIRING_ACK_MS);
    if (decision == HoldDecision::Cancel || decision == HoldDecision::Expire) {
      const bool retry = decision == HoldDecision::Cancel &&
          (localFriendRequest_ || pairingWebRequestId_) && !localEmergency_ && !remoteEmergencyActive();
      cancelFriendPairing(retry);
      return;
    }
    if (decision == HoldDecision::Complete) {
      auto *entry = findNearby(pairingPeer_);
      KnownUser user;
      if (!entry || !findKnownUserById(entry->userId, user)) {
        cancelFriendPairing();
        return;
      }
      const String name = safeRemoteUsername(std::string(pairingRemote_.username));
      const bool changed = !user.friendFlag || (!name.isEmpty() && name != user.username);
      user.friendFlag = true;
      if (!name.isEmpty()) user.username = name;
      // Commit friend flag and name together. Avoid two flash flushes at the success boundary (and avoid writing an unchanged friendship).
      if (changed && !updateKnownUser(user)) {
        cancelFriendPairing();
        return;
      }
      entry->friendFlag = true;
      entry->friendSyncPending = false;
      entry->removalNotice.clear();
      entry->closedRequestId = pairingWebRequestId_ ? pairingWebRequestId_ :
          (entry->incomingRequest.id ? entry->incomingRequest.id : entry->outgoingRequest.id);
      clearFriendRequests(*entry);
      applyIdentity(*entry, pairingRemote_, true, false);
      pairingCompleted_ = true;
      friendPairingAckUntilMs_ = now + FRIEND_PAIRING_ACK_MS;
    }
    return;
  }
  NearbyEntry *webPeer = nullptr;
  for (NearbyEntry &entry : nearby_) {
    if (entry.used && entry.webConsent.active(now) &&
        now - entry.lastSeenMs < FRIEND_PRESENT_TIMEOUT_MS &&
        entry.rssi >= FRIEND_PAIRING_MIN_RSSI_DBM) {
      webPeer = &entry;
      break;
    }
  }
  if (webPeer && pairingRequestGate_.exhausted(now)) {
    const uint32_t id = webPeer->webConsent.id;
    clearFriendRequests(*webPeer);
    webPeer->closedRequestId = webPeer->cancelRequestId = id;
    webPeer->cancelRequestMs = now;
    webPeer = nullptr;
  }
  if ((!localFriendRequest_ && !webPeer) || localEmergency_ || remoteEmergencyActive() ||
      !pairingRequestGate_.canAttempt(now)) return;
  if (workerBusy_) {
    worker_.cancel();  // Let a pending background read finish before pairing.
    return;
  }
  // Lowest ID wins; nearest eligible peer wins when several people request.
  NearbyEntry *peer = webPeer;
  if (!peer) for (NearbyEntry &entry : nearby_) {
    if (!entry.used || entry.message != MESSAGE_FRIEND_REQUEST ||
        now - entry.lastSeenMs >= FRIEND_PRESENT_TIMEOUT_MS ||
        entry.rssi < FRIEND_PAIRING_MIN_RSSI_DBM) continue;
    if (!peer || entry.rssi > peer->rssi ||
        (entry.rssi == peer->rssi && memcmp(entry.deviceId, peer->deviceId, 6) < 0)) peer = &entry;
  }
  if (!peer || memcmp(ownDeviceId_, peer->deviceId, 6) >= 0) return;
  do { pairingSession_ = esp_random(); } while (!pairingSession_);
  memcpy(pairingPeer_, peer->deviceId, 6);
  pairingWebRequestId_ = webPeer ? webPeer->webConsent.id : 0;
  pairingCoordinator_ = true;
  pairingPreparedMs_ = now;
  pairingRequestGate_.attempted(now);
  pairingOwn_ = BleExchange::Identity{};
  safeRemoteUsername(std::string(globalSettings.username.c_str())).toCharArray(
      pairingOwn_.username, sizeof(pairingOwn_.username));
  pairingOwn_.hasGlyph = copyOwnAgentGlyph(pairingOwn_.glyph);
  if (!queueExchange(*peer, true)) cancelFriendPairing(true);
}

bool ProximityManager::friendPairingStart(uint32_t &startMs) const {
  startMs = pairingStartMs_;
  return enabled_ && pairingScheduled_ && !pairingDelivered_;
}

bool ProximityManager::takeFriendPairingSuccess(Rgbw remoteGlyph[LED_COUNT],
    Rgbw ownGlyph[LED_COUNT], String &username, uint32_t &successMs) {
  if (!pairingCompleted_ || pairingDelivered_) return false;
  memcpy(remoteGlyph, pairingRemote_.glyph, sizeof(pairingRemote_.glyph));
  memcpy(ownGlyph, pairingOwn_.glyph, sizeof(pairingOwn_.glyph));
  username = safeRemoteUsername(std::string(pairingRemote_.username));
  if (username.isEmpty()) username = "Unknown Agent";
  successMs = pairingStartMs_ + FRIEND_PAIRING_HOLD_MS;
  pairingDelivered_ = true;
  return true;
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
      const uint32_t now = millis();
      const bool pairing = pairingSession_ && memcmp(pairingPeer_, entry.deviceId, 6) == 0;
      item["nearby"] = enabled_ && now - entry.lastSeenMs < FRIEND_PRESENT_TIMEOUT_MS;
      item["friendRequest"] = pairing || entry.webConsent.active(now) || entry.acceptPending ? "pairing" :
          (entry.incomingRequest.active(now) ? "incoming" :
          (entry.outgoingRequest.active(now) ? "outgoing" : ""));
      const uint32_t remaining = entry.incomingRequest.active(now) ? entry.incomingRequest.remaining(now) :
          entry.outgoingRequest.remaining(now);
      item["requestExpiresIn"] = (remaining + 999U) / 1000U;
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
  uint32_t activeElapsed[32];
  uint8_t activeCount;
  uint8_t resultCount;
  uint16_t resultIds[100];
  uint32_t resultActiveSince[100];
  uint32_t resultActiveElapsed[100];
};

void addMatchingUser(const KnownUser &user, void *opaque) {
  PeopleContext &context = *static_cast<PeopleContext *>(opaque);
  if (context.resultCount >= 100) return;
  String searchable = user.username;
  searchable.toLowerCase();
  if (!context.query.isEmpty() && searchable.indexOf(context.query) < 0) return;
  uint32_t activeSince = 0;
  uint32_t activeElapsed = 0;
  for (uint8_t i = 0; i < context.activeCount; ++i) {
    if (context.activeIds[i] == user.userId) {
      activeSince = context.activeSince[i];
      activeElapsed = context.activeElapsed[i];
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
  context.resultActiveElapsed[context.resultCount] = activeElapsed;
  ++context.resultCount;
}
}  // namespace

void ProximityManager::writePeople(JsonArray array, const String &query) const {
  PeopleContext context{array, query, {}, {}, {}, 0, 0, {}, {}, {}};
  context.query.toLowerCase();
  const uint64_t nowMs = encounterNowMs();
  for (const NearbyEntry &entry : nearby_) {
    if (!entry.used || context.activeCount >= MAX_NEARBY) continue;
    context.activeIds[context.activeCount] = entry.userId;
    context.activeSince[context.activeCount] = entry.firstLogged ? entry.firstSeenUnix : 0;
    context.activeElapsed[context.activeCount] = EncounterTiming::elapsedSeconds(
        entry.firstSeenMonotonicMs, nowMs);
    ++context.activeCount;
  }
  forEachKnownUser(addMatchingUser, &context);
  UserHistory histories[100] = {};
  const uint32_t now = utcTimeValid() ? utcNow() : 0;
  readUserHistories(context.resultIds, histories, context.resultActiveSince,
                    context.resultCount, now, context.resultActiveElapsed);
  for (uint8_t i = 0; i < context.resultCount; ++i) {
    JsonObject item = array[i].as<JsonObject>();
    item["lastSeen"] = histories[i].lastSeen;
    item["totalSeconds"] = histories[i].totalSeconds;
  }
}
