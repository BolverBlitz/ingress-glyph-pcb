#include "ble_exchange.h"
#include <freertos/task.h>

namespace BleExchange {
namespace {
bool readIdentity(NimBLERemoteService *service, Identity &identity, bool readGlyph = true) {
  auto *name = service->getCharacteristic(USERNAME_UUID);
  auto *glyph = service->getCharacteristic(GLYPH_UUID);
  if (name && name->canRead()) {
    const auto value = name->readValue();
    size_t length = min<size_t>(value.size(), FriendProtocol::NAME_BYTES);
    if (length < value.size()) {
      while (length && (value.data()[length] & 0xc0) == 0x80) --length;
    }
    memcpy(identity.username, value.data(), length);
  }
  // The username was read successfully, but identityValid historically means
  // that a glyph snapshot is available. Keeping it false lets Friend Mode
  // fetch the glyph later when rendering is enabled.
  if (!readGlyph) return false;
  if (!glyph || !glyph->canRead()) return false;
  const auto value = glyph->readValue();
  if (value.size() < 4 || value.data()[0] != 'A' || value.data()[1] != 'G' || value.data()[2] != 1) return false;
  identity.hasGlyph = value.data()[3] == LED_COUNT;
  if ((!identity.hasGlyph && (value.data()[3] != 0 || value.size() != 4)) ||
      (identity.hasGlyph && value.size() != 4 + sizeof(identity.glyph))) return false;
  if (identity.hasGlyph) memcpy(identity.glyph, value.data() + 4, sizeof(identity.glyph));
  return true;
}
bool exchangeFriendship(const Job &job, NimBLERemoteService *service, Result &result) {
  if (!job.friendshipCommand) return true;
  auto *remote = service->getCharacteristic(FRIENDSHIP_UUID);
  if (!remote || !remote->canWrite() || !remote->canRead()) return false;
  FriendSync::Packet packet;
  packet.type = job.friendshipCommand; packet.nonce = job.requestId;
  memcpy(packet.sender, job.ownId, 6);
  const uint32_t spent = millis() - job.queuedMs;
  packet.remainingMs = job.requestRemainingMs > spent ? job.requestRemainingMs - spent : 0;
  packet.friendFlag = job.friendFlag;
  uint8_t bytes[FriendSync::PACKET_BYTES]; FriendSync::encode(bytes, packet);
  if (!remote->writeValue(bytes, sizeof(bytes), true)) return false;
  const uint32_t deadline = millis() + 1000;
  while (static_cast<int32_t>(millis() - deadline) < 0) {
    const auto value = remote->readValue(); FriendSync::Packet response;
    if (FriendSync::decode(value.data(), value.size(), response) &&
        FriendSync::validResponse(response, job.requestId, job.peer)) {
      result.friendshipValid = true; result.remoteFriend = response.friendFlag;
      result.friendshipResult = response.result; result.friendshipCommand = job.friendshipCommand;
      result.requestId = job.requestId; return true;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  return false;
}
}

bool Worker::begin() {
  jobs_ = xQueueCreate(1, sizeof(Job));
  results_ = xQueueCreate(4, sizeof(Result));
  if (jobs_ && results_ && xTaskCreate(task, "ble-exchange", 6144, this, 1, nullptr) == pdPASS) return true;
  if (jobs_) vQueueDelete(jobs_);
  if (results_) vQueueDelete(results_);
  jobs_ = results_ = nullptr;
  return false;
}
bool Worker::submit(Job job) {
  job.generation = generation();
  return jobs_ && xQueueSend(jobs_, &job, 0) == pdTRUE;
}
bool Worker::receive(Result &result) {
  return results_ && xQueueReceive(results_, &result, 0) == pdTRUE;
}
void Worker::task(void *context) {
  auto &worker = *static_cast<Worker *>(context);
  Job job;
  for (;;) {
    if (xQueueReceive(worker.jobs_, &job, portMAX_DELAY) == pdTRUE) worker.run(job);
  }
}
bool Worker::waitForPhase(const Job &job, NimBLERemoteCharacteristic *status,
                          uint8_t phase, uint32_t deadline) {
  while (current(job) && !FriendProtocol::reached(millis(), deadline)) {
    const auto value = status->readValue();
    if (FriendProtocol::matches(value.data(), value.size(), job.session, job.peer) &&
        value.data()[3] == phase) return current(job);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  return false;
}

bool Worker::keepAlive(const Job &job, NimBLERemoteCharacteristic *status,
                       NimBLERemoteCharacteristic *command, uint32_t startMs) {
  using namespace FriendProtocol;
  // Keep one connection through the hold gesture. A release, emergency or
  // stale session cancels both participants without blocking LED rendering.
  const uint32_t finish = startMs + FRIEND_PAIRING_HOLD_MS;
  while (current(job)) {
    uint8_t packet[HEADER_BYTES];
    header(packet, Heartbeat, job.session, job.ownId);
    if (!command->writeValue(packet, sizeof(packet), true)) return false;
    const auto value = status->readValue();
    if (!matches(value.data(), value.size(), job.session, job.peer) || value.data()[3] != Scheduled) return false;
    Result contact;
    contact.generation = job.generation;
    contact.session = job.session;
    contact.finished = false;
    contact.contactMs = millis();
    memcpy(contact.peer, job.peer, 6);
    xQueueSend(results_, &contact, 0);
    if (reached(millis(), finish)) return current(job);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  return false;
}

bool Worker::pair(const Job &job, NimBLERemoteService *service, Result &result,
                  NimBLERemoteCharacteristic *&command) {
  using namespace FriendProtocol;
  auto *status = service->getCharacteristic(STATUS_UUID);
  command = service->getCharacteristic(COMMAND_UUID);
  if (!status || !status->canRead() || !command || !command->canWrite() || !current(job)) return false;

  uint8_t offer[OFFER_BYTES] = {};
  header(offer, job.webRequestId ? WebOffer : Offer, job.session, job.ownId,
         job.webRequestId);
  offer[18] = job.own.hasGlyph;
  offer[19] = strlen(job.own.username);
  memcpy(offer + 20, job.own.username, NAME_BYTES);
  memcpy(offer + 20 + NAME_BYTES, job.own.glyph, sizeof(job.own.glyph));
  if (!command->writeValue(offer, sizeof(offer), true) ||
      !waitForPhase(job, status, Ready, millis() + 2000)) return false;
  // READY freezes the peer's identity until the session ends. Read it after
  // the offer so both sides animate the same snapshots, even during UI edits.
  result.identityValid = readIdentity(service, result.remote, job.readGlyph);
  if (!result.identityValid || !current(job)) return false;

  // Use the lowest RTT of three samples to reduce BLE scheduling asymmetry.
  // Devices need neither a common boot time nor an NTP clock.
  uint32_t bestRtt = UINT32_MAX;
  uint32_t offset = 0;
  for (unsigned sample = 0; sample < 3 && current(job); ++sample) {
    const uint32_t sent = millis();
    const auto value = status->readValue();
    const uint32_t received = millis();
    if (matches(value.data(), value.size(), job.session, job.peer) &&
        value.data()[3] == Ready && received - sent < bestRtt) {
      bestRtt = received - sent;
      offset = remoteOffset(sent, received, read32(value.data() + 14));
    }
  }
  if (bestRtt > MAX_CLOCK_RTT_MS || !current(job)) return false;

  const uint32_t lead = startLead(bestRtt);
  result.startMs = millis() + lead;
  uint8_t packet[HEADER_BYTES];
  header(packet, Start, job.session, job.ownId, result.startMs + offset);
  if (!command->writeValue(packet, sizeof(packet), true) ||
      !waitForPhase(job, status, Scheduled, result.startMs - MIN_START_LEAD_MS) ||
      reached(millis() + MIN_START_LEAD_MS, result.startMs)) return false;

  result.scheduled = true;
  result.finished = false;
  result.contactMs = millis();
  xQueueSend(results_, &result, portMAX_DELAY);
  Serial.printf("Friends: session %lu clock RTT %lu ms, lead %lu ms, start %lu\n",
                (unsigned long)job.session, (unsigned long)bestRtt,
                (unsigned long)lead, (unsigned long)result.startMs);
  return keepAlive(job, status, command, result.startMs);
}

void Worker::run(const Job &job) {
  Result result;
  result.generation = job.generation;
  result.session = job.session;
  memcpy(result.peer, job.peer, 6);
  auto *client = job.client;
  NimBLERemoteCharacteristic *command = nullptr;
  bool completed = false;
  if (current(job) && client && client->connect(NimBLEAddress(std::string(job.address), job.addressType))) {
    auto *service = client->getService(SERVICE_UUID);
    if (service && current(job)) {
      if (job.pairing) completed = pair(job, service, result, command);
      else {
        exchangeFriendship(job, service, result);
        result.identityValid = readIdentity(service, result.remote, job.readGlyph);
      }
    }
    if (job.pairing && !completed && command) {
      // Best effort even after the main task invalidated this job.
      uint8_t packet[FriendProtocol::HEADER_BYTES];
      FriendProtocol::header(packet, FriendProtocol::Cancel, job.session, job.ownId);
      command->writeValue(packet, sizeof(packet), true);
    }
    client->disconnect();
    // disconnect() only sends the GAP command; isConnected() already becomes
    // false while DISCONNECTING. Retain ownership until the handle is cleared
    // by the disconnect event, before reusing or destroying this client.
    while (client->getConnHandle() != BLE_HS_CONN_HANDLE_NONE) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
  result.finished = true;
  result.failed = job.pairing && !completed;
  xQueueSend(results_, &result, portMAX_DELAY);
}

bool Server::begin() {
  incoming_ = xQueueCreate(6, sizeof(Incoming));
  return incoming_ != nullptr;
}
void Server::publish(uint8_t phase, uint32_t session, const uint8_t ownId[6]) {
  portENTER_CRITICAL(&mutex_);
  FriendProtocol::header(status_, phase, session, ownId);
  portEXIT_CRITICAL(&mutex_);
}
void Server::publishFriendship(const uint8_t bytes[FriendSync::PACKET_BYTES]) {
  portENTER_CRITICAL(&mutex_); memcpy(friendship_, bytes, FriendSync::PACKET_BYTES); portEXIT_CRITICAL(&mutex_);
}
bool Server::receive(Incoming &incoming) {
  return incoming_ && xQueueReceive(incoming_, &incoming, 0) == pdTRUE;
}
void Server::onRead(NimBLECharacteristic *characteristic, NimBLEConnInfo &) {
  if (strcmp(characteristic->getUUID().toString().c_str(), FRIENDSHIP_UUID) == 0) {
    uint8_t bytes[FriendSync::PACKET_BYTES]; portENTER_CRITICAL(&mutex_); memcpy(bytes, friendship_, sizeof(bytes)); portEXIT_CRITICAL(&mutex_);
    characteristic->setValue(bytes, sizeof(bytes)); return;
  }
  uint8_t bytes[FriendProtocol::HEADER_BYTES];
  portENTER_CRITICAL(&mutex_);
  memcpy(bytes, status_, sizeof(bytes));
  portEXIT_CRITICAL(&mutex_);
  FriendProtocol::write32(bytes + 14, millis());
  characteristic->setValue(bytes, sizeof(bytes));
}
void Server::onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connection) {
  const auto value = characteristic->getValue();
  if (!incoming_) return;
  const bool friendship = strcmp(characteristic->getUUID().toString().c_str(), FRIENDSHIP_UUID) == 0;
  FriendSync::Packet sync;
  if (friendship) { if (!FriendSync::decode(value.data(), value.size(), sync)) return; }
  else if (!FriendProtocol::validCommand(value.data(), value.size())) return;
  Incoming incoming;
  incoming.size = value.size();
  incoming.receivedMs = millis();
  incoming.friendship = friendship;
  if (friendship) {
    // A response belongs to one command. Clear the previous response before
    // queueing this command so a rapid retry cannot consume stale state.
    portENTER_CRITICAL(&mutex_); memset(friendship_, 0, sizeof(friendship_)); portEXIT_CRITICAL(&mutex_);
  }
  memcpy(incoming.bytes, value.data(), value.size());
  const auto address = connection.getAddress().toString();
  memcpy(incoming.address, address.c_str(), min<size_t>(address.size(), sizeof(incoming.address) - 1));
  xQueueSend(incoming_, &incoming, 0);
}
}  // namespace BleExchange
