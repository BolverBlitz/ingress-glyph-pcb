#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <NimBLEDevice.h>

#include "config.h"
#include "rgbw.h"
#include "ble_exchange.h"
#include <atomic>

class NimBLECharacteristic;
class NimBLEClient;
class NimBLEScan;

class ProximityManager {
 public:
  void begin();
  void loop();
  void setEnabled(bool enabled);
  void setLocalEmergency(bool emergency);
  void setLocalFriendRequest(bool active);
  void refreshIdentity();
  void onTimeSynchronized();
  bool resetStoredData();
  bool setFriend(uint16_t userId, bool friendFlag);
  bool friendPairingStart(uint32_t &startMs) const;
  bool friendPairingBusy() const {
    return enabled_ && (localFriendRequest_ || pairingSession_);
  }
  bool takeFriendPairingSuccess(Rgbw remoteGlyph[LED_COUNT],
                               Rgbw ownGlyph[LED_COUNT], String &username,
                               uint32_t &successMs);
  uint8_t loadedFriendGlyphCount() const;
  const Rgbw *loadedFriendGlyph(uint8_t ordinal) const;
  void enqueueAdvertisement(const NimBLEAdvertisedDevice *device);
  void onScanEnded() { scanRestartNeeded_ = true; }
  void writeNearby(JsonArray array) const;
  void writePeople(JsonArray array, const String &query) const;
  bool remoteEmergencyActive() const;
  bool firstRemoteEmergency(String &username, bool &faction) const;
  bool enabled() const { return enabled_; }

 private:
  static constexpr uint8_t MAX_NEARBY = 32;
  struct NearbyEntry {
    bool used = false;
    uint8_t deviceId[6] = {};
    uint16_t userId = 0;
    bool faction = false;
    bool friendFlag = false;
    bool emergency = false;
    uint8_t message = 0;
    bool glyphLoaded = false;
    bool glyphFetched = false;
    Rgbw glyph[LED_COUNT] = {};
    String username;
    String address;
    uint8_t addressType = 0;
    int rssi = 0;
    int16_t rssiSamples[NEARBY_RSSI_AVERAGE_PACKETS] = {};
    int32_t rssiSum = 0;
    uint8_t rssiSampleCount = 0;
    uint8_t rssiSampleIndex = 0;
    uint32_t firstSeenUnix = 0;
    uint32_t lastSeenMs = 0;
    uint32_t lastNameAttemptMs = 0;
    uint32_t lastGlyphAttemptMs = 0;
    bool firstLogged = false;
  };
  struct Observation {
    uint8_t deviceId[6];
    char address[18];
    uint8_t addressType;
    uint8_t faction;
    uint8_t message;
    int16_t rssi;
    uint32_t receivedMs;
  };

  NearbyEntry nearby_[MAX_NEARBY];
  void *queue_ = nullptr;
  NimBLEScan *scan_ = nullptr;
  NimBLEClient *client_ = nullptr;
  NimBLECharacteristic *usernameCharacteristic_ = nullptr;
  NimBLECharacteristic *agentGlyphCharacteristic_ = nullptr;
  std::atomic<bool> enabled_{false};
  bool storeReady_ = false;
  bool initialized_ = false;
  std::atomic<bool> scanRestartNeeded_{false};
  String advertisedUsername_;
  bool advertisedFaction_ = false;
  uint8_t localMessage_ = 0;
  uint8_t advertisedMessage_ = 0;
  uint32_t advertisedGlyphRevision_ = UINT32_MAX;
  bool localEmergency_ = false;
  bool localFriendRequest_ = false;
  uint32_t friendPairingAckUntilMs_ = 0;
  BleExchange::Worker worker_;
  BleExchange::Server pairingServer_;
  bool workerReady_ = false;
  bool workerBusy_ = false;
  uint8_t ownDeviceId_[6] = {};
  uint8_t pairingPeer_[6] = {};
  uint32_t pairingSession_ = 0;
  uint32_t pairingPreparedMs_ = 0;
  uint32_t pairingStartMs_ = 0;
  uint32_t pairingContactMs_ = 0;
  bool pairingScheduled_ = false;
  bool pairingCompleted_ = false;
  bool pairingDelivered_ = false;
  FriendProtocol::RequestGate pairingRequestGate_;
  bool pairingCoordinator_ = false;
  BleExchange::Identity pairingOwn_;
  BleExchange::Identity pairingRemote_;

  void startBle();
  void stopBle();
  void startScan();
  void processObservation(const Observation &observation);
  bool queueExchange(NearbyEntry &entry, bool pairing);
  void processExchangeResults();
  void processPairingCommands();
  void loopFriendPairing();
  void cancelFriendPairing(bool retryPreparation = false);
  void publishPairingStatus();
  void applyIdentity(NearbyEntry &entry, const BleExchange::Identity &identity,
                     bool glyphValid, bool persistName = true);
  bool copyOwnAgentGlyph(Rgbw output[LED_COUNT]) const;
  void refreshAgentGlyph();
  void expireNearby();
  NearbyEntry *findNearby(const uint8_t deviceId[6]);
  static String deviceIdText(const uint8_t deviceId[6]);
  static String safeRemoteUsername(const std::string &value);
};

extern ProximityManager proximityManager;
