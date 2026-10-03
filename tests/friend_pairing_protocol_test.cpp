#include "friend_pairing_protocol.h"
#include <cstdlib>
#include <iostream>

using namespace FriendProtocol;
void check(bool condition, const char *message) {
  if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
HoldDecision decide(uint32_t now, uint32_t prepared, uint32_t start,
    uint32_t contact, bool scheduled = true, bool completed = false,
    bool requested = true, bool emergency = false) {
  return holdDecision(now, prepared, start, contact, scheduled, completed,
                      requested, emergency, 8000, 5000, 6000);
}

int main() {
  // The two devices can have completely different boot times. Packet delay
  // and main-loop delay must not become an extra animation phase offset.
  const uint32_t offsets[] = {0, 1000000, uint32_t(-900000), UINT32_MAX - 100};
  for (uint32_t bootOffset : offsets) {
    const uint32_t sent = UINT32_MAX - 1000;
    const uint32_t received = sent + 40;
    const uint32_t remoteClock = sent + 20 + bootOffset;
    const uint32_t offset = remoteOffset(sent, received, remoteClock);
    check(offset == bootOffset, "Clock offset was not recovered across rollover");
    const uint32_t start = received + START_LEAD_MS;
    const uint32_t peerStart = start + offset;
    check(validStart(received + 50, start), "Planned start rejected");
    check(validStart(received + 50 + offset, peerStart), "Peer planned start rejected");
    check(elapsed(start - 1, start) == 0, "Future start jumped to animation end");
    for (uint32_t progress = 0; progress <= 5000; progress += 40) {
      check(elapsed(start + progress, start) == elapsed(start + progress + offset, peerStart),
            "Animations on different device clocks diverged");
    }
    check(decide(start + 4999, sent, start, start + 4900, true, false, false) == HoldDecision::Cancel,
          "Early release did not cancel pairing");
    check(decide(start + 5000, sent, start, start + 4900, true, false, false) == HoldDecision::Complete,
          "Release at five seconds cancelled completed hold");
    check(decide(start + 5000 + offset, sent + offset, peerStart, start + 4900 + offset) == HoldDecision::Complete,
          "Peer completed at a different time");
  }
  check(!validStart(1000, 1000), "Past start accepted");
  check(!validStart(1000, 1000 + MIN_START_LEAD_MS - 1), "Late acknowledgement accepted");
  check(!validStart(1000, 100000), "Unbounded future start accepted");
  check(startLead(20) == 350, "Fast BLE link retained the long fixed delay");
  check(startLead(100) == 400, "BLE latency was not included in scheduling budget");
  check(startLead(MAX_CLOCK_RTT_MS) == MAX_START_LEAD_MS, "Slow BLE link lost its ACK margin");
  for (uint32_t rtt = 0; rtt <= MAX_CLOCK_RTT_MS; ++rtt) {
    const uint32_t lead = startLead(rtt);
    check(validStart(1000 + rtt * 2, 1000 + lead), "Write/read acknowledgement cannot fit in new lead time");
  }
  check(decide(8999, 1000, 0, 0, false) == HoldDecision::Waiting, "Prepare timeout fired early");
  check(decide(9000, 1000, 0, 0, false) == HoldDecision::Cancel, "Preparation never timed out");
  check(decide(2600, 1000, 2000, 1400) == HoldDecision::Cancel, "Missing heartbeat did not cancel");
  check(decide(7000, 1000, 2000, 5000) == HoldDecision::Cancel, "Stale peer falsely completed");
  check(decide(2100, 1000, 2000, 2100, true, false, true, true) == HoldDecision::Cancel,
        "Emergency did not cancel");
  check(decide(12999, 1000, 2000, 6500, true, true, false) == HoldDecision::Waiting,
        "Completed status expired before peer could acknowledge");
  check(decide(13000, 1000, 2000, 6500, true, true, false) == HoldDecision::Expire,
        "Completed session stayed latched forever");

  const uint8_t peer[6] = {1, 2, 3, 4, 5, 6};
  const uint8_t other[6] = {1, 2, 3, 4, 5, 7};
  // The offer carries the coordinator's intent. Its advertisement may still
  // say NORMAL/ACK, or the request packet may have been missed altogether.
  check(offerDecision(false, true, false, true, peer, other, 500, 15000, -50, -80) == OfferDecision::Accept,
        "Both held buttons were rejected while the sender's request advert was missing");
  check(offerDecision(false, false, false, true, peer, other, 500, 15000, -50, -80) == OfferDecision::NotRequested,
        "An offer started pairing without local button consent");
  check(offerDecision(true, true, false, true, peer, other, 500, 15000, -50, -80) == OfferDecision::Busy,
        "Offer replaced an active peer");
  check(offerDecision(false, true, true, true, peer, other, 500, 15000, -50, -80) == OfferDecision::Emergency,
        "Offer ignored emergency priority");
  check(offerDecision(false, true, false, true, other, peer, 500, 15000, -50, -80) == OfferDecision::WrongCoordinator,
        "Higher-ID sender became coordinator");
  check(offerDecision(false, true, false, true, peer, other, 15000, 15000, -50, -80) == OfferDecision::Stale,
        "Expired proximity allowed an offer");
  check(offerDecision(false, true, false, true, peer, other, 500, 15000, -81, -80) == OfferDecision::TooFar,
        "Offer bypassed proximity threshold");
  for (const uint32_t origin : {1000U, UINT32_MAX - 100}) {
    RequestGate gate;
    check(gate.canAttempt(origin), "Fresh press blocked");
    gate.attempted(origin);
    gate.sessionEnded(origin + 10, false, true, true, false);
    check(!gate.canAttempt(origin + 259), "Failed setup retried without backoff");
    check(gate.canAttempt(origin + 260), "Transient failure consumed the held gesture");
    gate.attempted(origin + 260);
    gate.sessionEnded(origin + 270, false, true, true, false);
    gate.attempted(origin + 520);
    gate.sessionEnded(origin + 530, false, true, true, false);
    check(!gate.canAttempt(origin + 1000), "Retry attempt limit was ignored");
    gate.released();
    check(gate.canAttempt(origin + 1001), "Release did not rearm the next press");
    gate.attempted(origin + 1100);
    gate.sessionEnded(origin + 1200, false, true, true, true);
    check(!gate.canAttempt(origin + 1500), "Cancelled visible animation restarted while held");
    gate.released();
    gate.attempted(origin + 1600);
    gate.sessionEnded(origin + 1610, false, true, true, false);
    check(!gate.canAttempt(origin + 9600), "Preparation retries continued beyond their budget");
    gate.released();
    // Old completion expires while a fresh button press is already pending.
    gate.sessionEnded(origin + 10000, true, true, false, true);
    check(gate.canAttempt(origin + 10001), "Completion cleanup swallowed a fresh gesture");
  }
  uint8_t offer[OFFER_BYTES] = {};
  header(offer, Offer, 42, peer);
  offer[18] = 1;
  offer[19] = NAME_BYTES;
  check(validCommand(offer, sizeof(offer)), "Full username/glyph offer rejected");
  for (size_t size = 0; size < sizeof(offer); ++size)
    check(!validCommand(offer, size), "Truncated offer accepted");
  offer[19] = NAME_BYTES + 1;
  check(!validCommand(offer, sizeof(offer)), "Oversized username accepted");
  offer[19] = 0; offer[18] = 2;
  check(!validCommand(offer, sizeof(offer)), "Invalid glyph marker accepted");
  uint8_t status[HEADER_BYTES];
  header(status, Scheduled, 42, peer, 123456);
  check(matches(status, sizeof(status), 42, peer), "Session acknowledgement rejected");
  check(!matches(status, sizeof(status), 43, peer), "Stale session accepted");
  check(!matches(status, sizeof(status), 42, other), "Third device joined existing session");
  status[2] = VERSION + 1;
  check(!matches(status, sizeof(status), 42, peer), "Unsupported protocol accepted");
  for (uint8_t command = Start; command <= Heartbeat; ++command) {
    header(status, command, 42, peer, 123456);
    check(validCommand(status, sizeof(status)), "Control command rejected");
    check(read32(status + 14) == 123456, "Wire clock encoding corrupted");
  }
  header(status, Start, 0, peer);
  check(!validCommand(status, sizeof(status)), "Zero session accepted");
  std::cout << "Pairing clock, hold, cancellation and wire-protocol checks passed\n";
}
