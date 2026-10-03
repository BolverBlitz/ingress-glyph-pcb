#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "friend_pairing_protocol.h"
#include "config.h"
#include "rgbw.h"

namespace BleExchange {
constexpr char SERVICE_UUID[] = "44424700-696e-6772-6573-732d676c7970";
constexpr char USERNAME_UUID[] = "44424701-696e-6772-6573-732d676c7970";
constexpr char GLYPH_UUID[] = "44424702-696e-6772-6573-732d676c7970";
constexpr char STATUS_UUID[] = "44424703-696e-6772-6573-732d676c7970";
constexpr char COMMAND_UUID[] = "44424704-696e-6772-6573-732d676c7970";
static_assert(LED_COUNT * sizeof(Rgbw) == FriendProtocol::GLYPH_BYTES, "Pairing wire format needs a version change");
struct Identity {
  char username[FriendProtocol::NAME_BYTES + 1] = {};
  Rgbw glyph[LED_COUNT] = {};
  bool hasGlyph = false;
};
struct Job {
  NimBLEClient *client = nullptr;
  uint32_t generation = 0;
  uint8_t peer[6] = {};
  char address[18] = {};
  uint8_t addressType = 0;
  bool pairing = false;
  uint32_t session = 0;
  uint8_t ownId[6] = {};
  Identity own;
};
struct Result {
  uint32_t generation = 0;
  uint8_t peer[6] = {};
  Identity remote;
  bool identityValid = false;
  bool scheduled = false;
  bool finished = true;
  bool failed = false;
  uint32_t session = 0;
  uint32_t startMs = 0;
  uint32_t contactMs = 0;
};
struct Incoming {
  uint8_t bytes[FriendProtocol::OFFER_BYTES] = {};
  size_t size = 0;
  char address[18] = {};
  uint32_t receivedMs = 0;
};

// One worker owns all outgoing client operations. Queues only contain snapshots;
// application settings, LED frames and LittleFS remain on the main task.
class Worker {
 public:
  bool begin();
  bool submit(Job job);
  bool receive(Result &result);
  void cancel() { ++generation_; }
  uint32_t generation() const { return generation_.load(); }
 private:
  QueueHandle_t jobs_ = nullptr;
  QueueHandle_t results_ = nullptr;
  std::atomic<uint32_t> generation_{1};
  static void task(void *context);
  void run(const Job &job);
  bool waitForPhase(const Job &job, NimBLERemoteCharacteristic *status,
                    uint8_t phase, uint32_t deadline);
  bool pair(const Job &job, NimBLERemoteService *service, Result &result,
            NimBLERemoteCharacteristic *&command);
  bool keepAlive(const Job &job, NimBLERemoteCharacteristic *status,
                 NimBLERemoteCharacteristic *command, uint32_t startMs);
  bool current(const Job &job) const { return job.generation == generation(); }
};

class Server : public NimBLECharacteristicCallbacks {
 public:
  bool begin();
  void publish(uint8_t phase, uint32_t session, const uint8_t ownId[6]);
  bool receive(Incoming &incoming);
  void onRead(NimBLECharacteristic *characteristic, NimBLEConnInfo &) override;
  void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connection) override;
 private:
  QueueHandle_t incoming_ = nullptr;
  portMUX_TYPE mutex_ = portMUX_INITIALIZER_UNLOCKED;
  uint8_t status_[FriendProtocol::HEADER_BYTES] = {};
};
}  // namespace BleExchange
