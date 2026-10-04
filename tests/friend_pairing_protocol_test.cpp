#include "friend_pairing_protocol.h"
#include "friend_sync_protocol.h"
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
  const uint8_t peer[6] = {1, 2, 3, 4, 5, 6};
  const uint8_t other[6] = {1, 2, 3, 4, 5, 7};
  FriendSync::Packet sync;
  uint8_t fs[FriendSync::PACKET_BYTES];
  FriendSync::packet(fs, FriendSync::Request, 77, peer, FriendSync::REQUEST_TTL_MS, true);
  check(FriendSync::decode(fs, sizeof(fs), sync), "Valid friendship request rejected");
  check(sync.type == FriendSync::Request && sync.nonce == 77 && sync.friendFlag &&
        sync.remainingMs == FriendSync::REQUEST_TTL_MS, "Friendship request fields corrupted");
  check(FriendSync::matches(sync, FriendSync::Request, 77, peer), "Friendship request correlation failed");
  check(!FriendSync::matches(sync, FriendSync::Request, 78, peer) &&
        !FriendSync::matches(sync, FriendSync::Request, 77, other), "Mismatched friendship packet accepted");
  fs[0] = 'X'; check(!FriendSync::decode(fs, sizeof(fs), sync), "Bad friendship magic accepted");
  FriendSync::packet(fs, FriendSync::Request, 77, peer, FriendSync::REQUEST_TTL_MS + 1);
  check(!FriendSync::decode(fs, sizeof(fs), sync), "Overlong friendship request accepted");
  FriendSync::packet(fs, FriendSync::Sync, 77, peer); fs[18] = 2;
  check(!FriendSync::decode(fs, sizeof(fs), sync), "Invalid friendship boolean accepted");
  FriendSync::packet(fs, FriendSync::Response, 77, peer, 0, false, 9);
  check(!FriendSync::decode(fs, sizeof(fs), sync), "Invalid friendship result accepted");
  FriendSync::packet(fs, FriendSync::Response, 77, peer, 0, true, FriendSync::Accepted);
  check(FriendSync::decode(fs, sizeof(fs), sync) && FriendSync::validResponse(sync, 77, peer),
        "Valid friendship response rejected");
  check(!FriendSync::validResponse(sync, 78, peer) && !FriendSync::validResponse(sync, 77, other),
        "Stale friendship response accepted");
  FriendSync::packet(fs, FriendSync::Sync, 77, peer);
  for (size_t n = 0; n < FriendSync::PACKET_BYTES; ++n)
    check(!FriendSync::decode(fs, n, sync), "Truncated friendship packet accepted");
  fs[2] = FriendSync::VERSION + 1; check(!FriendSync::decode(fs, sizeof(fs), sync), "Unknown FS version accepted");
  fs[2] = FriendSync::VERSION; fs[3] = 99; check(!FriendSync::decode(fs, sizeof(fs), sync), "Unknown FS type accepted");
  FriendSync::packet(fs, FriendSync::Sync, 0, peer); check(!FriendSync::decode(fs, sizeof(fs), sync), "Zero FS nonce accepted");
  FriendSync::PendingRequest pending;
  check(!pending.set(0, 100), "Zero request ID accepted");
  check(pending.set(123, UINT32_MAX - 100, 300000), "Pending request not created");
  check(pending.active(199) && pending.remaining(199) == 299700, "Pending request rollover failed");
  check(!pending.active(199 + 300000) && !pending.canConsent(123, 199 + 300000), "Expired request accepted");
  check(pending.canConsent(123, 199) && !pending.canConsent(124, 199), "Consent correlation failed");
  FriendSync::PendingRequest observed, consent;
  check(observed.observe(900, 1000, FriendSync::REQUEST_TTL_MS), "Initial request was not observed");
  check(observed.observe(900, 241000), "Duplicate request was not accepted while pending");
  check(observed.remaining(241000) == FriendSync::REQUEST_TTL_MS - 240000,
        "Duplicate request incorrectly extended its deadline");
  check(observed.grantConsent(900, 241000, consent) && consent.remaining(241000) == observed.remaining(241000),
        "Consent did not preserve request expiry");
  check(!observed.grantConsent(901, 241000, consent), "Wrong request nonce granted consent");
  observed.clear(); check(!observed.active(241000), "Cleared request remained active");
  FriendSync::PendingRequest sender, receiver, granted;
  check(sender.set(501, UINT32_MAX - 50), "Sender request setup failed");
  check(receiver.observe(501, 25), "Receiver failed to observe across rollover");
  check(!receiver.canConsent(502, 25) && receiver.grantConsent(501, 25, granted),
        "Consent correlation failed across device clocks");
  check(!granted.canConsent(501, 25 + FriendSync::REQUEST_TTL_MS), "Consent survived exact expiry");
  // Unfriending is sent immediately and stops after an acknowledgement or a
  // finite retry budget, even when boards remain nearby indefinitely.
  for (const uint32_t origin : {1000U, UINT32_MAX - 100}) {
    FriendSync::RemovalNotice removal;
    check(removal.start(601, origin) && removal.due(origin), "Removal was not ready immediately");
    removal.sent(origin);
    check(!removal.due(origin + FriendSync::RETRY_MS - 1), "Removal retried without backoff");
    check(!removal.confirm(602, false, FriendSync::Ok), "Stale removal acknowledgement accepted");
    check(!removal.confirm(601, true, FriendSync::Ok), "Peer still a friend acknowledged removal");
    check(!removal.confirm(601, false, FriendSync::Busy), "Busy peer acknowledged removal");
    check(removal.due(origin + FriendSync::RETRY_MS), "Failed removal could not retry");
    check(removal.confirm(601, false, FriendSync::Ok), "Confirmed peer removal was ignored");
    check(!removal.due(origin + FriendSync::RETRY_MS * 2), "Confirmed removal kept sending");
    removal.start(603, origin);
    for (uint8_t attempt = 0; attempt < FriendSync::RemovalNotice::MAX_ATTEMPTS; ++attempt) {
      const uint32_t now = origin + FriendSync::RETRY_MS * attempt;
      check(removal.due(now), "Removal retry budget ended early");
      removal.sent(now);
    }
    check(!removal.due(origin + FriendSync::RETRY_MS * 10), "Removal retried indefinitely");
    removal.start(604, origin);
    check(!removal.due(origin + FriendSync::REQUEST_TTL_MS), "Expired removal was sent");
    removal.clear();
    check(!removal.confirm(604, false, FriendSync::Ok), "Cancelled removal accepted a late acknowledgement");
  }
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
    check(!gate.exhausted(origin + 1), "Retry backoff treated as exhaustion");
    gate.sessionEnded(origin + 10, false, true, true, false);
    check(!gate.canAttempt(origin + 259), "Failed setup retried without backoff");
    check(gate.canAttempt(origin + 260), "Transient failure consumed the held gesture");
    gate.attempted(origin + 260);
    gate.sessionEnded(origin + 270, false, true, true, false);
    gate.attempted(origin + 520);
    check(gate.exhausted(origin + 520), "Retry attempt limit was not exhausted");
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
  header(offer, WebOffer, 42, peer, 1234);
  offer[18] = 0; offer[19] = 0;
  check(validCommand(offer, sizeof(offer)) && read32(offer + 14) == 1234,
        "Web friendship offer rejected");
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
