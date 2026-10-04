#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Fixed-size, little-endian friendship synchronization packets.
namespace FriendSync {
constexpr uint8_t VERSION = 1;
constexpr size_t PACKET_BYTES = 20;
constexpr uint32_t REQUEST_TTL_MS = 300000;
constexpr uint32_t RETRY_MS = 5000;
enum Type : uint8_t { Sync = 1, Request = 2, Accept = 3, Cancel = 4, Response = 5 };
enum Result : uint8_t { Ok = 0, Pending = 1, Accepted = 2, Rejected = 3, Busy = 4 };

struct Packet {
  uint8_t type = 0;
  uint32_t nonce = 0;
  uint8_t sender[6] = {};
  uint32_t remainingMs = 0;
  bool friendFlag = false;
  uint8_t result = Rejected;
};
struct PendingRequest {
  uint32_t id = 0, createdMs = 0, lifetimeMs = 0;
  bool active(uint32_t now) const { return lifetimeMs && uint32_t(now - createdMs) < lifetimeMs; }
  uint32_t remaining(uint32_t now) const {
    if (!active(now)) return 0;
    return lifetimeMs - uint32_t(now - createdMs);
  }
  bool set(uint32_t requestId, uint32_t now, uint32_t lifetime = REQUEST_TTL_MS) {
    if (!requestId || !lifetime || lifetime > REQUEST_TTL_MS) return false;
    id = requestId; createdMs = now; lifetimeMs = lifetime; return true;
  }
  bool observe(uint32_t requestId, uint32_t now, uint32_t lifetime = REQUEST_TTL_MS) {
    if (id == requestId) return active(now);
    return set(requestId, now, lifetime);
  }
  bool grantConsent(uint32_t expectedId, uint32_t now, PendingRequest &consent) const {
    if (!canConsent(expectedId, now)) return false;
    return consent.set(expectedId, now, remaining(now));
  }
  void clear() { id = createdMs = lifetimeMs = 0; }
  bool canConsent(uint32_t expectedId, uint32_t now) const { return id == expectedId && active(now); }
};

inline uint32_t read32(const uint8_t *p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
    (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
inline void write32(uint8_t *p, uint32_t v) { for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(v >> (i * 8)); }
inline bool validType(uint8_t t) { return t >= Sync && t <= Response; }

inline void encode(uint8_t out[PACKET_BYTES], const Packet &p) {
  out[0] = 'F'; out[1] = 'S'; out[2] = VERSION; out[3] = p.type;
  write32(out + 4, p.nonce); memcpy(out + 8, p.sender, 6); write32(out + 14, p.remainingMs);
  out[18] = p.friendFlag ? 1 : 0; out[19] = p.result;
}
inline void packet(uint8_t out[PACKET_BYTES], uint8_t type, uint32_t nonce,
                   const uint8_t sender[6], uint32_t remainingMs = 0,
                   bool friendFlag = false, uint8_t result = Rejected) {
  Packet p; p.type = type; p.nonce = nonce; memcpy(p.sender, sender, 6);
  p.remainingMs = remainingMs; p.friendFlag = friendFlag; p.result = result; encode(out, p);
}
inline bool decode(const uint8_t *in, size_t size, Packet &p) {
  if (!in || size != PACKET_BYTES || in[0] != 'F' || in[1] != 'S' || in[2] != VERSION ||
      !validType(in[3]) || !read32(in + 4) || in[18] > 1) return false;
  const uint32_t ttl = read32(in + 14);
  if (in[19] > Busy) return false;
  if (in[3] == Request && ttl > REQUEST_TTL_MS) return false;
  p.type = in[3]; p.nonce = read32(in + 4); memcpy(p.sender, in + 8, 6);
  p.remainingMs = ttl; p.friendFlag = in[18] != 0; p.result = in[19];
  return true;
}
inline bool matches(const Packet &p, uint8_t type, uint32_t nonce, const uint8_t sender[6]) {
  return p.type == type && p.nonce == nonce && sender && memcmp(p.sender, sender, 6) == 0;
}
inline bool validResponse(const Packet &p, uint32_t nonce, const uint8_t remote[6]) {
  return p.type == Response && p.nonce == nonce && remote && memcmp(p.sender, remote, 6) == 0;
}

// A removal is an explicit event, with a bounded retry budget rather than polling.
struct RemovalNotice {
  static constexpr uint8_t MAX_ATTEMPTS = 3;
  PendingRequest pending;
  uint32_t lastAttemptMs = 0;
  uint8_t attempts = 0;

  void clear() { pending.clear(); lastAttemptMs = 0; attempts = 0; }
  bool start(uint32_t nonce, uint32_t now) {
    clear();
    return pending.set(nonce, now);
  }
  bool due(uint32_t now) const {
    return pending.active(now) && attempts < MAX_ATTEMPTS &&
        (!attempts || uint32_t(now - lastAttemptMs) >= RETRY_MS);
  }
  void sent(uint32_t now) { lastAttemptMs = now; ++attempts; }
  bool confirm(uint32_t nonce, bool remoteFriend, uint8_t result) {
    if (!pending.id || nonce != pending.id || remoteFriend || result != Ok) return false;
    clear();
    return true;
  }
};
} // namespace FriendSync
