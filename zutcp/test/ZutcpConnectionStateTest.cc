//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/Zutcp.hh>
#include <zlib/ZutcpConnectionUtil.hh>

#include "ZutcpTestMxPlugin.hh"

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
  Ztc::QueueTelemetry telemetry;
  ring.telemetry(telemetry);
  ZuCHECK(telemetry.size == 3 && telemetry.count == 3 && telemetry.full == 1,
    "closed tuple capacity overwrite increments normalized full");
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

struct AdmissionCxn : public Zutcp::Cxn {
  void abortRx() override { }
  Zutcp::ClosedTupleKey closedKeyRx() const override { return {}; }
  bool established() const override { return false; }
};

void testSharedAdmission()
{
  ZuTestScope(testSharedAdmission);
  ZutcpTest::Context<> stack;
  bool initialized = stack.init(ZutcpTest::Params{stack.mx()}.maxCxns(2).maxPendingCxns(1));
  ZuCHECK(initialized, "configure shared admission limits on the actual MxPlugin");
  if (!initialized) return;
  bool reserved = stack.reserveCxn();
  bool deniedPending = stack.reserveCxn();
  auto a = stack.cxnAdmission();
  ZuCHECK(reserved && !deniedPending && a.total == 1 && a.pending == 1 && a.refused == 1,
    "a cold attempt occupies total and shared pending capacity before constructing its owner");
  AdmissionCxn active;
  stack.admitCxn(active);
  stack.establishedCxn(active);
  stack.establishedCxn(active);
  reserved = stack.reserveCxn();
  AdmissionCxn mapped;
  stack.admitCxn(mapped);
  bool deniedTotal = stack.reserveCxn();
  a = stack.cxnAdmission();
  ZuCHECK(active.admission && mapped.admission && reserved && !deniedTotal &&
      a.total == 2 && a.pending == 1 && a.refused == 2,
    "attaching a reservation and duplicate establishment do not double-charge total capacity");
  stack.closingCxn(mapped);
  stack.closingCxn(mapped);
  a = stack.cxnAdmission();
  ZuCHECK(a.total == 2 && a.pending == 1 && a.closing == 1,
    "a failed pending handshake remains pending and total-charged during its close drain");
  stack.releaseCxn(mapped);
  stack.releaseCxn(mapped);
  reserved = stack.reserveCxn();
  AdmissionCxn otherActive;
  stack.admitCxn(otherActive);
  stack.establishedCxn(otherActive);
  stack.closingCxn(active);
  bool deniedClosing = stack.reserveCxn();
  a = stack.cxnAdmission();
  ZuCHECK(reserved && !mapped.admission && !deniedClosing &&
      a.total == 2 && !a.pending && a.closing == 1,
    "closing full transports retain total capacity independently of pending");
  stack.releaseCxn(active);
  bool callbackRetry = false;
  auto complete = [&stack, &callbackRetry] {
    callbackRetry = stack.reserveCxn();
    if (callbackRetry) stack.cancelCxn();
  };
  complete();
  stack.releaseCxn(otherActive);
  a = stack.cxnAdmission();
  ZuCHECK(callbackRetry && !a.total && !a.pending && !a.closing && a.refused == 3,
    "owner retirement frees capacity before a completion callback retries");
}

void testPendingAdmissionTransfer()
{
  ZuTestScope(testPendingAdmissionTransfer);
  ZutcpTest::Context<> stack;
  bool initialized = stack.init(ZutcpTest::Params{stack.mx()}.maxCxns(2).maxPendingCxns(1));
  ZuCHECK(initialized, "configure passive admission limits on the actual MxPlugin");
  if (!initialized) return;
  ZmRef<Zutcp_::PendingTable> pending = new Zutcp_::PendingTable;
  Zutcp::ClosedTupleKey tuple{ZiSockAddr{ZiIP{"198.18.0.1"}, 18191},
    ZiSockAddr{ZiIP{"198.18.0.2"}, 18192}};
  bool reserved = stack.reserveCxn();
  pending->add(tuple, Zutcp_::PendingInitial{tuple});
  auto found = pending->findPtr(tuple);
  auto a = stack.cxnAdmission();
  ZuCHECK(reserved && found && a.total == 1 && a.pending == 1 && !a.refused,
    "cold route preparation holds capacity while the existing table deduplicates its tuple");
  auto duplicate = pending->findPtr(tuple);
  bool other = stack.reserveCxn();
  ZuCHECK(duplicate == found && !other && stack.cxnAdmission().refused == 1,
    "SYN retransmission reuses pending state while distinct candidates share its cap");
  stack.cancelCxn();
  pending->del(tuple);
  reserved = stack.reserveCxn();
  pending->add(tuple, Zutcp_::PendingInitial{tuple});
  AdmissionCxn full;
  stack.admitCxn(full);
  stack.establishedCxn(full);
  a = stack.cxnAdmission();
  ZuCHECK(reserved && pending->findPtr(tuple) && a.total == 1 && !a.pending,
    "acceptance attaches one reservation while retaining tuple deduplication for first delivery");
  stack.releaseCxn(full);
  other = stack.reserveCxn();
  a = stack.cxnAdmission();
  ZuCHECK(other && pending->findPtr(tuple) && a.total == 1 && a.pending == 1,
    "first-delivery closure can release capacity while the pending tuple remains protected");
  if (other) stack.cancelCxn();
  pending->del(tuple);
  a = stack.cxnAdmission();
  ZuCHECK(!pending->count_() && !a.total && !a.pending && !a.closing,
    "setup rejection and full connection retirement consume each charge once");
}

static void bindInput(
  Zutcp::Endpoint *, ZmRef<ZiIOBuf>, ZiSockAddr, ZiSockAddr, unsigned,
  const Zutcp::TCPHeader &) { }

struct BindEndpoint : public Zutcp::EndpointIndex::Node {
  explicit BindEndpoint(ZiSockAddr local) {
    bindAddr = ZuMv(local);
    receive = &bindInput;
  }
};

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
  ZiIP any;
  if (v6) any = in6addr_any; else any = in_addr{};
  BindEndpoint fixed{old}, occupied{{ip, uint16_t(Zutcp::ClientPorts::First + 1)}},
    automatic{{ip, 0}}, duplicate{{ip, 0}}, unrelated{old}, listener{{any, old.port()}};
  // The shadow index is destroyed before the endpoint objects owning its nodes.
  Zutcp::EndpointIndex bindings;
  unsigned next = Zutcp::ClientPorts::First;
  ZuCHECK(Zutcp::bindCxnTuple(fixed, peer, next, bindings, history) == Error::Retained &&
      fixed.bindAddr == old && !bindings.findPtr(old),
    "retained fixed tuple fails without binding an endpoint or changing its requested tuple");
  bindings.addNode(&occupied);
  ZuCHECK(Zutcp::bindCxnTuple(automatic, peer, next, bindings, history) == Error::None &&
      automatic.bindAddr.port() == Zutcp::ClientPorts::First + 2 &&
      bindings.findPtr(automatic.bindAddr) == &automatic,
    "automatic selection skips retained tuples and indexes the endpoint's own node");
  duplicate.bindAddr = automatic.bindAddr;
  ZuCHECK(Zutcp::bindCxnTuple(duplicate, peer, next, bindings, history) == Error::InUse &&
      bindings.findPtr(automatic.bindAddr) == &automatic,
    "failed duplicate binding cannot replace another endpoint's embedded node");
  ZiSockAddr otherPeer{peer.ip(), 18192};
  ZuCHECK(Zutcp::bindCxnTuple(unrelated, otherPeer, next, bindings, history) == Error::None,
    "a retained record for another remote tuple does not guard an unrelated connection");
  ZuCHECK(Zutcp::bindCxnTuple(listener, {}, next, bindings, history) == Error::None,
    "wildcard listeners bind independently of peer-specific closed history");
  Zutcp::ClosedTuple after;
  auto unchanged = history.stats();
  ZuCHECK(history.find(first.key, after) && after.id == first.id && after.tsRecent == 77 &&
      after.txNext == 20 && after.rxNext == 30 && unchanged.inserted == stats.inserted &&
      unchanged.evicted == stats.evicted,
    "selection and refusal preserve old record identity, evidence and FIFO order");
  history.remember({.key = {ZiSockAddr{ip, 1}, peer}});
  history.remember({.key = {ZiSockAddr{ip, 2}, peer}});
  ZuCHECK(bindings.delNode(&unrelated) == &unrelated,
    "unbinding returns the endpoint's node without deleting its owner");
  ZuCHECK(!history.find(first.key, after) &&
      Zutcp::bindCxnTuple(fixed, peer, next, bindings, history) == Error::None &&
      bindings.findPtr(old) == &fixed,
    "capacity eviction permits rebinding through the endpoint's existing intrusive storage");
}

void testClientPortExhaustion(bool v6)
{
  ZuTestScope(testClientPortExhaustion);
  ZiIP ip{v6 ? "2001:db8::1" : "198.18.0.1"};
  ZiSockAddr peer{ZiIP{v6 ? "2001:db8::2" : "198.18.0.2"}, 18191};
  Zutcp::ClosedTuples history{Zutcp::ClientPorts::Count};
  for (unsigned port = Zutcp::ClientPorts::First; port < Zutcp::ClientPorts::End; ++port)
    history.remember({.key = {ZiSockAddr{ip, uint16_t(port)}, peer}});
  BindEndpoint automatic{{ip, 0}};
  Zutcp::EndpointIndex bindings;
  unsigned next = Zutcp::ClientPorts::First;
  auto stats = history.stats();
  ZuCHECK(Zutcp::bindCxnTuple(automatic, peer, next, bindings, history) ==
      Zutcp::CxnBindError::Exhausted && !automatic.bindAddr.port() &&
      !bindings.count_() && next == Zutcp::ClientPorts::First,
    "one bounded full-range search reports exhaustion without claiming an endpoint");
  auto after = history.stats();
  ZuCHECK(after.inserted == stats.inserted && after.evicted == stats.evicted &&
      after.count == stats.count,
    "exhaustion preserves every retained tuple without evicting history to retry");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testConnectionUtilities);
  ZuTestCall(testClosedTupleRing);
  ZuTestCall(testSequenceTupleReuse);
  ZuTestCall(testSharedAdmission);
  ZuTestCall(testPendingAdmissionTransfer);
  ZuTestCall(testConservativeClientTuple, false);
  ZuTestCall(testConservativeClientTuple, true);
  ZuTestCall(testClientPortExhaustion, false);
  ZuTestCall(testClientPortExhaustion, true);
}
