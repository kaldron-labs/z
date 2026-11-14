//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zutcp.hh>

using namespace ZuTestUtil;

void testConnectionUtilities()
{
  ZuTestScope(testConnectionUtilities);

  Zutcp::FlowCredit credit{10};
  ZuCHECK(credit.consume(7) && credit.available() == 3, "flow consume failed");
  ZuCHECK(!credit.consume(4) && credit.blocked() == false,
    "flow over-consume handling mismatch");
  credit.extend(20);
  ZuCHECK(credit.consume(13) && credit.blocked(), "flow extension failed");

  Zutcp::RxState rx;
  ZuCHECK(rx.receive(0, 5, false), "connection receive failed");
  ZuCHECK(rx.receive(5, 5, true), "connection FIN receive failed");
  ZuCHECK(rx.complete() && rx.finalSize() == 10, "connection completion mismatch");
  ZuCHECK(!rx.receive(8, 4, false), "final-size violation not rejected");

  Zutcp::RxState reorder;
  ZuCHECK(reorder.receive(5, 5, true), "out-of-order receive failed");
  ZuCHECK(!reorder.complete() && reorder.finalSizeKnown() &&
      reorder.finalSize() == 10,
    "out-of-order final-size state mismatch");
  ZuCHECK(reorder.receive(0, 5, false), "gap fill receive failed");
  ZuCHECK(!reorder.complete() && reorder.delivered() == 5,
    "standalone connection state tracked queue delivery");
  reorder.delivered(10);
  ZuCHECK(reorder.complete(), "connection state did not complete after delivery");
  ZuCHECK(reorder.receive(2, 3, false), "duplicate range rejected");
}

static Zutcp::ClosedTuple closedTuple(unsigned n, bool v6 = false)
{
  return {.key = {ZiSockAddr{ZiIP{v6 ? "2001:db8::1" : "198.18.0.1"}, uint16_t(n)},
      ZiSockAddr{ZiIP{v6 ? "2001:db8::2" : "198.18.0.2"}, 443}},
    .txNext = n * 13, .rxNext = n * 17, .tsRecent = n * 19,
    .lastSentTS = n * 23, .timestamps = v6};
}

void testSequenceTupleReuse()
{
  ZuTestScope(testSequenceTupleReuse);
  Zutcp::ClosedTuple complete{.txNext = 100, .rxNext = 200, .fin = true};
  ZuCHECK(Zutcp::reuseCxnTuple(complete, 100, 200),
    "new ISNs at the old NEXT positions must be after the previous last-used positions");
  ZuCHECK(!Zutcp::reuseCxnTuple(complete, 99, 200) &&
      !Zutcp::reuseCxnTuple(complete, 100, 199) &&
      !Zutcp::reuseCxnTuple(complete, 98, 201) &&
      !Zutcp::reuseCxnTuple(complete, 101, 198),
    "both directions must be strictly newer; equal or older ISNs cannot authorize reuse");
  ZuCHECK(!Zutcp::reuseCxnTuple(complete, uint32_t(99 + 0x80000000u), 200) &&
      !Zutcp::reuseCxnTuple(complete, 100, uint32_t(199 + 0x80000000u)) &&
      !Zutcp::reuseSequence(0, 0x80000000u),
    "the exact half-space has no serial ordering and must remain refused");
  complete.txNext = complete.rxNext = 0;
  ZuCHECK(Zutcp::reuseCxnTuple(complete, 0, 0) &&
      Zutcp::reuseCxnTuple(complete, 1, 2) &&
      !Zutcp::reuseCxnTuple(complete, UINT32_MAX, 0) &&
      !Zutcp::reuseCxnTuple(complete, 0, UINT32_MAX),
    "last-used subtraction and strict comparisons must wrap while ISN zero stays valid");
  complete.txNext = 0xfffffff0;
  complete.rxNext = 0xfffffffe;
  ZuCHECK(Zutcp::reuseCxnTuple(complete, 0, 1) &&
      !Zutcp::reuseCxnTuple(complete, 0xffffffee, 1),
    "new sequence positions may cross zero without admitting old-side candidates");
  complete.txNext = 100;
  complete.rxNext = 200;
  complete.fin = false;
  ZuCHECK(!Zutcp::reuseCxnTuple(complete, 101, 201),
    "an incomplete receive frontier cannot bound old DATA still in flight after abort");
  complete.timestamps = true;
  complete.tsRecent = complete.lastSentTS = UINT32_MAX;
  ZuCHECK(!Zutcp::reuseCxnTuple(complete, 101, 201),
    "timestamp evidence without complete peer FIN must not invent Phase4 timestamp eligibility");
  complete.fin = true;
  ZuCHECK(!Zutcp::reuseCxnTuple(complete, 99, 201) &&
      !Zutcp::reuseCxnTuple(complete, 101, 199),
    "timestamp fields cannot override either insufficient sequence direction");
}

void testClosedTupleRing()
{
  ZuTestScope(testClosedTupleRing);
  Zutcp::ClosedTuples ring{3};
  auto a = ring.remember(closedTuple(1));
  auto b = ring.remember(closedTuple(2, true));
  auto c = ring.remember(closedTuple(3));
  Zutcp::ClosedTuple found;
  ZuCHECK(ring.stats().capacity == 3 && ring.stats().count == 3 &&
      ring.find(b.key, found) && found.id == b.id && found.txNext == b.txNext &&
      found.rxNext == b.rxNext && found.timestamps && found.tsRecent == b.tsRecent,
    "closed records preserve inline tuple, sequence and timestamp evidence");
  a.rxNext = 0xfffffff0;
  ZuCHECK(ring.update(a), "late evidence must update the existing identity");
  auto d = ring.remember(closedTuple(4, true));
  ZuCHECK(!ring.find(a.key, found) && !ring.update(a) && ring.find(b.key, found) &&
      ring.stats().count == 3 && ring.stats().evicted == 1,
    "late evidence cannot refresh closure FIFO or survive oldest-slot overwrite");
  auto replacement = ring.remember(closedTuple(2, true));
  ZuCHECK(replacement.id != b.id && !ring.update(b) && ring.stats().count == 3,
    "a newly closed incarnation replaces the matching tuple with a new identity");
  auto e = ring.remember(closedTuple(5));
  ZuCHECK(!ring.find(c.key, found) && ring.find(d.key, found) &&
      ring.find(replacement.key, found),
    "new closure of an existing tuple takes its new position in closure order");
  ring.resize(2);
  ZuCHECK(ring.stats().capacity == 2 && ring.stats().count == 2 &&
      !ring.find(d.key, found) && ring.find(replacement.key, found) &&
      found.id == replacement.id && ring.find(e.key, found) && found.id == e.id,
    "shrinking retains only the newest records in their original order and identities");
  ring.resize(5);
  auto f = ring.remember(closedTuple(6, true));
  ring.resize(5);
  ZuCHECK(ring.stats().count == 3 && ring.find(replacement.key, found) &&
      found.id == replacement.id && ring.find(f.key, found) && found.id == f.id,
    "growing or unchanged configuration preserves retained evidence and identity");
  for (unsigned n = 7; n < 1007; ++n) ring.remember(closedTuple(n, n & 1));
  bool exact = ring.stats().count == 5;
  for (unsigned n = 1000; n < 1007; ++n)
    exact &= ring.find(closedTuple(n, n & 1).key, found) == (n >= 1002);
  ZuCHECK(exact && !ring.update(f),
    "repeated slot wrap removes old lookup membership without growing retained history");
  Zutcp::ClosedTuples one{1};
  auto old = one.remember(closedTuple(9));
  auto newest = one.remember(closedTuple(10));
  ZuCHECK(!one.find(old.key, found) && !one.update(old) && one.find(newest.key, found) &&
      found.id == newest.id && one.stats().count == 1,
    "one-slot capacity obeys the same overwrite and stale-snapshot boundary");
}

void testSharedAdmissionTokens()
{
  ZuTestScope(testSharedAdmissionTokens);
  // The one Stack-owned primitive is deliberately independent of any backend.
  // Every native/mapped role transfers tokens from this same ledger.
  Zutcp::CxnSlots slots;
  auto native = slots.reserve(2, 1);
  auto deniedPending = slots.reserve(2, 1);
  auto a = slots.admission(2, 1);
  ZuCHECK(native && !deniedPending && a.total == 1 && a.pending == 1 && a.refused == 1,
    "an active cold attempt must occupy both total and shared pending capacity");
  Zutcp::CxnSlot established = ZuMv(native);
  established.established();
  established.established();
  auto mapped = slots.reserve(2, 1);
  auto deniedTotal = slots.reserve(2, 1);
  a = slots.admission(2, 1);
  ZuCHECK(!native && established && mapped && !deniedTotal &&
      a.total == 2 && a.pending == 1 && a.refused == 2,
    "transfer and duplicate establishment must not double-charge or release total capacity");
  mapped.closing();
  mapped.closing();
  a = slots.admission(2, 1);
  ZuCHECK(a.total == 2 && a.pending == 1 && a.closing == 1,
    "a failed pending handshake remains pending and total-charged during its close drain");
  mapped.release();
  mapped.release();
  auto otherActive = slots.reserve(2, 1);
  otherActive.established();
  established.closing();
  auto deniedClosing = slots.reserve(2, 1);
  a = slots.admission(2, 1);
  ZuCHECK(!mapped && !deniedClosing && a.total == 2 && !a.pending && a.closing == 1,
    "closing full transports must retain total capacity independently of pending");
  established.release();
  bool callbackRetry = false;
  auto complete = [&slots, &callbackRetry] {
    auto retry = slots.reserve(2, 1);
    callbackRetry = bool(retry);
    retry.release();
  };
  complete();
  otherActive.release();
  a = slots.admission(2, 1);
  ZuCHECK(callbackRetry && !a.total && !a.pending && !a.closing && a.refused == 3,
    "owner retirement must free capacity before a completion callback retries");
}

void testPendingAdmissionTransfer()
{
  ZuTestScope(testPendingAdmissionTransfer);
  Zutcp::CxnSlots slots;
  using Pending = ZmHashKV<Zutcp::ClosedTupleKey, Zutcp::CxnSlot,
    ZmHashHeapID<"Zutcp.Test.PendingAdmission">>;
  ZmRef<Pending> pending = new Pending;
  Zutcp::ClosedTupleKey tuple{ZiSockAddr{ZiIP{"198.18.0.1"}, 18191},
    ZiSockAddr{ZiIP{"198.18.0.2"}, 18192}};
  auto route = slots.reserve(2, 1);
  pending->add(tuple, ZuMv(route));
  auto found = pending->findPtr(tuple);
  auto a = slots.admission(2, 1);
  ZuCHECK(found && !route && a.total == 1 && a.pending == 1 && !a.refused,
    "cold route preparation must transfer one reservation to existing tuple dedup state");
  // A retransmitted SYN only finds existing state. A distinct tuple
  // competes for the same pending capacity, before any factory is invoked.
  auto duplicate = pending->findPtr(tuple);
  auto other = slots.reserve(2, 1);
  ZuCHECK(duplicate == found && !other && slots.admission(2, 1).refused == 1,
    "SYN retransmission must reuse pending state while distinct candidates share its cap");
  found->val().release();
  pending->del(tuple);
  auto retried = slots.reserve(2, 1);
  pending->add(tuple, ZuMv(retried));
  found = pending->findPtr(tuple);
  Zutcp::CxnSlot full = ZuMv(found->val());
  pending->del(tuple);
  full.established();
  a = slots.admission(2, 1);
  ZuCHECK(!retried && !pending->count_() && a.total == 1 && !a.pending,
    "failed route must free its slot; acceptance must transfer one slot into a full owner");
  full.release();
  a = slots.admission(2, 1);
  ZuCHECK(!a.total && !a.pending && !a.closing,
    "setup rejection and full connection retirement must consume each charge once");
}

static void bindInput(
  void *, ZmRef<ZiIOBuf>, ZiSockAddr, ZiSockAddr, unsigned, const Zutcp::TCPHeader &) { }

void testConservativeClientTuple(bool v6)
{
  ZuTestScope(testConservativeClientTuple);
  using Error = Zutcp::CxnBindError;
  ZiIP ip{v6 ? "2001:db8::1" : "198.18.0.1"};
  ZiSockAddr peer{ZiIP{v6 ? "2001:db8::2" : "198.18.0.2"}, 18191};
  ZiSockAddr old{ip, Zutcp::ClientPorts::First};
  Zutcp::ClosedTuples history{2};
  auto first = history.remember({.key = {old, peer}, .txNext = 20, .rxNext = 30,
    .tsRecent = 77, .fin = true});
  auto stats = history.stats();
  ZmRef<Zutcp::EndpointBindings> bindings = new Zutcp::EndpointBindings;
  unsigned next = Zutcp::ClientPorts::First;
  int owner = 0, other = 0;
  auto fixed = old;
  ZuCHECK(Zutcp::bindCxnTuple(fixed, peer, next, *bindings, history, &owner, &bindInput) ==
      Error::Retained && fixed == old && !bindings->findPtr(old),
    "retained fixed tuple must fail without binding a new owner or changing the requested tuple");
  ZiSockAddr occupied{ip, uint16_t(Zutcp::ClientPorts::First + 1)};
  bindings->add(Zutcp::EndpointBinding{occupied, &other, &bindInput});
  ZiSockAddr automatic{ip, 0};
  ZuCHECK(Zutcp::bindCxnTuple(automatic, peer, next, *bindings, history, &owner, &bindInput) ==
      Error::None && automatic.port() == Zutcp::ClientPorts::First + 2 &&
      bindings->findPtr(automatic)->owner == &owner,
    "automatic client selection must skip both retained tuples and live endpoint membership");
  auto duplicate = automatic;
  ZuCHECK(Zutcp::bindCxnTuple(duplicate, peer, next, *bindings, history, &other, &bindInput) ==
      Error::InUse && bindings->findPtr(automatic)->owner == &owner,
    "failed duplicate binding must not steal existing owner membership");
  ZiSockAddr otherPeer{peer.ip(), 18192};
  auto unrelated = old;
  ZuCHECK(Zutcp::bindCxnTuple(unrelated, otherPeer, next, *bindings, history,
      &owner, &bindInput) == Error::None,
    "a retained record for another remote tuple must not guard an unrelated connection");
  ZiIP any;
  if (v6) any = in6addr_any; else any = in_addr{};
  ZiSockAddr listener{any, old.port()};
  ZuCHECK(Zutcp::bindCxnTuple(listener, {}, next, *bindings, history,
      &owner, &bindInput) == Error::None,
    "wildcard listeners must bind independently of peer-specific closed history");
  Zutcp::ClosedTuple after;
  auto unchanged = history.stats();
  ZuCHECK(history.find(first.key, after) && after.id == first.id && after.tsRecent == 77 &&
      after.txNext == 20 && after.rxNext == 30 && unchanged.inserted == stats.inserted &&
      unchanged.evicted == stats.evicted,
    "all selection and refusal paths must preserve old record identity, evidence and FIFO order");
  history.remember({.key = {ZiSockAddr{ip, 1}, peer}});
  history.remember({.key = {ZiSockAddr{ip, 2}, peer}});
  bindings->del(old);
  fixed = old;
  ZuCHECK(!history.find(first.key, after) && Zutcp::bindCxnTuple(fixed, peer, next,
      *bindings, history, &owner, &bindInput) == Error::None,
    "actual capacity eviction removes the history guard without any time-based expiry");
}

void testClientPortExhaustion(bool v6)
{
  ZuTestScope(testClientPortExhaustion);
  ZiIP ip{v6 ? "2001:db8::1" : "198.18.0.1"};
  ZiSockAddr peer{ZiIP{v6 ? "2001:db8::2" : "198.18.0.2"}, 18191};
  Zutcp::ClosedTuples history{Zutcp::ClientPorts::Count};
  for (unsigned port = Zutcp::ClientPorts::First; port < Zutcp::ClientPorts::End; ++port)
    history.remember({.key = {ZiSockAddr{ip, uint16_t(port)}, peer}});
  ZmRef<Zutcp::EndpointBindings> bindings = new Zutcp::EndpointBindings;
  unsigned next = Zutcp::ClientPorts::First;
  int owner = 0;
  ZiSockAddr automatic{ip, 0};
  auto stats = history.stats();
  ZuCHECK(Zutcp::bindCxnTuple(automatic, peer, next, *bindings, history,
      &owner, &bindInput) == Zutcp::CxnBindError::Exhausted && !automatic.port() &&
      !bindings->count_() && next == Zutcp::ClientPorts::First,
    "one bounded full-range search must report exhaustion without claiming an endpoint");
  auto after = history.stats();
  ZuCHECK(after.inserted == stats.inserted && after.evicted == stats.evicted &&
      after.count == stats.count,
    "exhaustion must preserve every retained tuple rather than evicting history to retry");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testConnectionUtilities);
  ZuTestCall(testClosedTupleRing);
  ZuTestCall(testSequenceTupleReuse);
  ZuTestCall(testSharedAdmissionTokens);
  ZuTestCall(testPendingAdmissionTransfer);
  ZuTestCall(testConservativeClientTuple, false);
  ZuTestCall(testConservativeClientTuple, true);
  ZuTestCall(testClientPortExhaustion, false);
  ZuTestCall(testClientPortExhaustion, true);
}
