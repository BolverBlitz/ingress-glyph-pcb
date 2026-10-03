#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Explicit byte encoding keeps this protocol independent of compiler padding.
namespace FriendProtocol {
constexpr uint8_t VERSION = 1;
enum Command : uint8_t { Offer = 1, Start = 2, Cancel = 3, Heartbeat = 4 };
enum Phase : uint8_t { Idle = 0, Ready = 1, Scheduled = 2 };
constexpr size_t HEADER_BYTES = 18;
constexpr size_t NAME_BYTES = 64;
constexpr size_t GLYPH_BYTES = 69 * 4;
constexpr size_t OFFER_BYTES = HEADER_BYTES + 2 + NAME_BYTES + GLYPH_BYTES;
constexpr uint32_t START_LEAD_MS = 350;
constexpr uint32_t MAX_START_LEAD_MS = 700;
constexpr uint32_t MIN_START_LEAD_MS = 100;
constexpr uint32_t MAX_CLOCK_RTT_MS = 200;
constexpr uint32_t HEARTBEAT_TIMEOUT_MS = 1200;
constexpr uint32_t PREPARE_RETRY_MS = 250;
constexpr uint32_t PREPARE_RETRY_WINDOW_MS = 8000;
constexpr uint8_t MAX_PREPARE_ATTEMPTS = 3;
enum class HoldDecision { Waiting, Complete, Cancel, Expire };

inline uint32_t read32(const uint8_t *bytes) {
  return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 |
         uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
}
inline void write32(uint8_t *bytes, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) bytes[i] = uint8_t(value >> (8 * i));
}
inline bool reached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}
inline uint32_t elapsed(uint32_t now, uint32_t start) {
  return reached(now, start) ? now - start : 0;
}
inline bool validStart(uint32_t now, uint32_t start) {
  const int32_t lead = static_cast<int32_t>(start - now);
  return lead >= int32_t(MIN_START_LEAD_MS) && lead <= int32_t(MAX_START_LEAD_MS + MAX_CLOCK_RTT_MS);
}
inline uint32_t startLead(uint32_t clockRtt) {
  const uint32_t budget = clockRtt * 3 + MIN_START_LEAD_MS;
  return budget < START_LEAD_MS ? START_LEAD_MS :
         (budget > MAX_START_LEAD_MS ? MAX_START_LEAD_MS : budget);
}
inline uint32_t remoteOffset(uint32_t sent, uint32_t received, uint32_t remote) {
  return remote - (sent + (received - sent) / 2);
}
inline HoldDecision holdDecision(uint32_t now, uint32_t prepared, uint32_t start,
    uint32_t contact, bool scheduled, bool completed, bool requested, bool emergency,
    uint32_t prepareTimeout, uint32_t hold, uint32_t acknowledgement) {
  const bool finished = scheduled && reached(now, start + hold);
  if (emergency || (!requested && !finished) ||
      (!scheduled && now - prepared >= prepareTimeout) ||
      (!completed && scheduled && now - contact >= HEARTBEAT_TIMEOUT_MS)) return HoldDecision::Cancel;
  if (finished && !completed) return HoldDecision::Complete;
  if (completed && reached(now, start + hold + acknowledgement)) return HoldDecision::Expire;
  return HoldDecision::Waiting;
}

// A failed setup can retry while both buttons remain held. Once an animation
// has actually started, a cancellation requires a new gesture instead.
class RequestGate {
 public:
  void released() { blocked_ = false; attempts_ = 0; }
  bool canAttempt(uint32_t now) const {
    if (blocked_) return false;
    return !attempts_ || (attempts_ < MAX_PREPARE_ATTEMPTS &&
        now - firstAttemptMs_ < PREPARE_RETRY_WINDOW_MS && reached(now, retryAtMs_));
  }
  void attempted(uint32_t now) {
    if (!attempts_) firstAttemptMs_ = now;
    ++attempts_;
    retryAtMs_ = now;
  }
  void cancelled(uint32_t now, bool animationStarted) {
    if (animationStarted) blocked_ = true;
    else retryAtMs_ = now + PREPARE_RETRY_MS;
  }
  void sessionEnded(uint32_t now, bool completed, bool held,
                    bool retryPreparation, bool animationStarted) {
    if (held && !completed) cancelled(now, !retryPreparation || animationStarted);
  }
 private:
  bool blocked_ = false;
  uint8_t attempts_ = 0;
  uint32_t firstAttemptMs_ = 0;
  uint32_t retryAtMs_ = 0;
};

enum class OfferDecision { Accept, Busy, NotRequested, Emergency, Backoff,
                           WrongCoordinator, Stale, TooFar };
inline OfferDecision offerDecision(bool busy, bool requested, bool emergency,
    bool attemptAllowed, const uint8_t sender[6], const uint8_t own[6],
    uint32_t peerAge, uint32_t presenceTimeout, int rssi, int minimumRssi) {
  if (busy) return OfferDecision::Busy;
  if (!requested) return OfferDecision::NotRequested;
  if (emergency) return OfferDecision::Emergency;
  if (!attemptAllowed) return OfferDecision::Backoff;
  if (memcmp(sender, own, 6) >= 0) return OfferDecision::WrongCoordinator;
  if (peerAge >= presenceTimeout) return OfferDecision::Stale;
  if (rssi < minimumRssi) return OfferDecision::TooFar;
  // The GATT offer carries the sender's intent. Requiring its request advert
  // here races against connection establishment, which stops advertisements.
  return OfferDecision::Accept;
}
inline void header(uint8_t *bytes, uint8_t type, uint32_t session,
                   const uint8_t sender[6], uint32_t time = 0) {
  bytes[0] = 'F'; bytes[1] = 'P'; bytes[2] = VERSION; bytes[3] = type;
  write32(bytes + 4, session);
  memcpy(bytes + 8, sender, 6); // Copy the sender's MAC address
  write32(bytes + 14, time);
}
inline bool validHeader(const uint8_t *bytes, size_t size) {
  return size >= HEADER_BYTES && bytes[0] == 'F' && bytes[1] == 'P' && bytes[2] == VERSION;
}
inline bool matches(const uint8_t *bytes, size_t size, uint32_t session,
                    const uint8_t peer[6]) {
  return size == HEADER_BYTES && validHeader(bytes, size) && read32(bytes + 4) == session &&
         memcmp(bytes + 8, peer, 6) == 0;
}
inline bool validCommand(const uint8_t *bytes, size_t size) {
  if (!validHeader(bytes, size) || !read32(bytes + 4)) return false;
  if (bytes[3] == Offer) {
    return size == OFFER_BYTES && bytes[18] <= 1 && bytes[19] <= NAME_BYTES;
  }
  return size == HEADER_BYTES && bytes[3] >= Start && bytes[3] <= Heartbeat;
}
}  // namespace FriendProtocol
