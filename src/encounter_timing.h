#pragma once

#include <stdint.h>
#include <limits.h>

namespace EncounterTiming {
inline uint32_t elapsedSeconds(uint64_t startMs, uint64_t nowMs) {
  const uint64_t seconds = nowMs >= startMs ? (nowMs - startMs) / 1000U : 0;
  return seconds > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(seconds);
}

inline uint32_t endTimestamp(uint32_t first, uint32_t elapsed) {
  return elapsed > UINT32_MAX - first ? UINT32_MAX : first + elapsed;
}

inline uint32_t backdatedStart(uint32_t now, uint32_t elapsed) {
  return now > elapsed ? now - elapsed : 1;
}

inline uint32_t interruptedSeconds(uint32_t first, uint32_t next,
                                   uint32_t timeout) {
  // Reboot lost the elapsed clock. Backward UTC cannot measure the gap;
  // credit the normal absence timeout rather than discard the visit.
  if (next <= first) return timeout;
  const uint32_t gap = next - first;
  return gap < timeout ? gap : timeout;
}

inline void applyRecord(uint32_t &lastSeen, uint64_t &totalSeconds,
                        uint32_t &openSince, bool last, uint32_t timestamp,
                        uint32_t timeout) {
  if (timestamp > lastSeen) lastSeen = timestamp;
  if (!last) {
    if (openSince) totalSeconds += interruptedSeconds(openSince, timestamp, timeout);
    openSince = timestamp;
  } else if (openSince) {
    totalSeconds += timestamp >= openSince ? timestamp - openSince : timeout;
    openSince = 0;
  }
}
}  // namespace EncounterTiming
