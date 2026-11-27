//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <signal.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <iostream>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmBlock.hh>

#include <zlib/ZtArray.hh>

#include <zlib/Zutcp.hh>
#include <zlib/ZutcpUDP.hh>

#include "ZutcpTestAlloc.hh"
#include "ZutcpTestMxPlugin.hh"

using namespace ZuTestUtil;

struct Endpoint : Zutcp_::Endpoint_<Endpoint> {
  enum { EndpointRef = false };
  void endpointReady(Endpoint *) { }
  void endpointDown_(Endpoint *) { }
  void endpointFailed(bool) { }
  void endpointDatagram(Zutcp::Datagram) { }
};

class BurstXdp : public ZmNHeap<BurstXdp>, public ZutcpTest::Xdp {
friend inline ZuStringT<"Zutcp.Test.BurstXdp"> ZmNHeapID(BurstXdp *);
public:
  using ZmNHeap<BurstXdp>::operator new;
  using ZmNHeap<BurstXdp>::operator delete;
  BurstXdp(ZiMultiplex *mx, ZmFn<void(unsigned)> done)
  : Xdp{mx}, m_done{ZuMv(done)} { }
  void rx() override {
    ++m_calls;
    if (m_sent) return;
    m_sent = true;
    uint8_t packet[64]{};
    ZiRxBurst burst;
    for (unsigned i = 0; i < 64; ++i) {
      packet[0] = uint8_t(i);
      receive({packet, sizeof(packet)}, &burst);
    }
    mx()->rxRun([this] { m_done(m_calls); });
  }
private:
  ZmFn<void(unsigned)>	m_done;
  unsigned		m_calls = 0;
  bool			m_sent = false;
};

static void checkBurstQuantum(unsigned quantum)
{
  auto env = ZutcpTest::fixtureEnv();
  ZmRef<Zutcp::MxPlugin> stack = new Zutcp::MxPlugin;
  ZiMultiplex mx{ZiMxParams{}.rxQuantum(quantum).scheduler(
    [](auto &s) { s.nThreads(3); }).rxThread(1).txThread(2), stack};
  ZmSemaphore done;
  unsigned calls = 0, packets = 0;
  bool ordered = true;
  ZmRef<Zpkt::Nif> device = new BurstXdp{&mx, [&done, &calls](unsigned n) {
    calls = n; done.post();
  }};
  Zutcp::MxParams params;
  params.packet = [&packets, &ordered](ZmRef<ZiIOBuf> packet) {
    ordered &= packet->length == 64 && packet->data()[0] == packets;
    ++packets;
  };
  bool initialized = false;
  try { stack->init(&mx, env, ZuMv(device), ZuMv(params)); initialized = true; }
  catch (ZeException &e) { std::cerr << e << '\n' << std::flush; }
  ZuCheckRT(initialized, log_("configure a burst device through the production plugin"));
  if (!initialized) return;
  bool started = mx.start();
  if (!quantum) {
    ZuCheckRT(!started, log_("zero receive quantum is rejected by Zi before device service"));
    mx.stop();
    return;
  }
  bool finished = started && !done.timedwait(Zm::now(5));
  mx.stop();
  std::cout << "# rx burst quantum=" << quantum << " packets=" << packets
    << " polls=" << calls << '\n';
  ZuCheckRT(finished && ordered && packets == 64 &&
      calls == 1 + unsigned(quantum <= 64),
    log_("a full burst delivers every frame with at most one quantum continuation"));
}

static void testBurstQuantum()
{
  ZuTestScopeRT(testBurstQuantum);
  constexpr unsigned quanta[] = {0, 1, 7, 32, 33, 64, 65};
  for (auto quantum : quanta) checkBurstQuantum(quantum);
}

static void testListenPublication()
{
  ZuTestScope(testListenPublication);
  auto env = ZutcpTest::fixtureEnv();
  ZmRef<Zutcp::MxPlugin> stack = new Zutcp::MxPlugin;
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(3); })
    .rxThread(1).txThread(2), stack};
  auto device = ZutcpTest::fixtureXdp(&mx);
  bool configured = false;
  try { stack->init(&mx, env, ZuMv(device), {}); configured = true; }
  catch (ZeException &e) {
    std::cerr << e << '\n' << std::flush;
  }
  ZuCheck(configured); if (!configured) return;
  bool started = mx.start(); ZuCheck(started); if (!started) return;
  ZmSemaphore held, release;
  mx.txRun([&held, &release] { held.post(); release.wait(); });
  bool blocked = held.timedwait(Zm::now(5)) == 0;
  unsigned successes = 0, failures = 0;
  bool published = false, correctInfo = true;
  if (blocked) {
    ZmBlock<>{}([stack, &mx, &successes, &failures, &published, &correctInfo](auto wake) {
      mx.rxRun([stack, &successes, &failures, &published, &correctInfo,
	wake = ZuMv(wake)]() mutable {
	ZiIP local{"198.18.92.2"};
	auto listen = [stack, local, &successes, &failures, &correctInfo] {
	  stack->listen(ZiListenFn{[local, &successes, &correctInfo](const ZiListenInfo &info) {
	    ++successes;
	    correctInfo &= info.ip == local && info.port == 18181;
	  }}, ZiFailFn{[&failures](bool) { ++failures; }}, {}, local, 18181, 1, nullptr, {});
	};
	// Hold endpoint Tx setup: publication, conflicting bind, cancellation and
	// exact-port rebind must all finish on Rx without a late listen callback.
	listen();
	published = successes == 1 && !failures;
	listen();
	published &= successes == 1 && failures == 1;
	stack->stopListening(local, 18181);
	listen();
	published &= successes == 2 && failures == 1;
	stack->stopListening(local, 18181);
	wake();
      });
    });
  }
  release.post();
  bool stopped = mx.stop();
  ZuCheck(blocked && published && correctInfo && stopped);
  ZuCheck(successes == 2 && failures == 1);
}

struct RetryDevice : ZmNHeap<RetryDevice>, ZutcpTest::Xdp {
friend inline ZuStringT<"Zutcp.Test.RetryDevice"> ZmNHeapID(RetryDevice *);
  using ZmNHeap<RetryDevice>::operator new;
  using ZmNHeap<RetryDevice>::operator delete;
  using Xdp::Xdp;
  unsigned pending = 0, completed = 0;
  ZmAtomic<unsigned> down{0};
  bool ready() const override { return !down && Xdp::ready(); }
  Zu::IO::Result send(uint8_t *, unsigned) override { ++pending; return Zi::OK; }
  unsigned pollTx() override {
    auto n = completed;
    completed = 0;
    pending -= n;
    return n;
  }
};
struct RetryCxn : Zutcp::Cxn {
  ZmFn<> retry;
  unsigned calls = 0;
  void abortRx() override { }
  Zutcp::ClosedTupleKey closedKeyRx() const override { return {}; }
  bool established() const override { return true; }
  void resumeTx(Zu::IO::Result result) override { last = result; ++calls; retry(); }
  Zu::IO::Result last = Zi::OK;
};

struct BatchDevice : ZmNHeap<BatchDevice>, ZutcpTest::Xdp {
friend inline ZuStringT<"Zutcp.Test.BatchDevice"> ZmNHeapID(BatchDevice *);
  using ZmNHeap<BatchDevice>::operator new;
  using ZmNHeap<BatchDevice>::operator delete;
  using Xdp::Xdp;

  Zutcp::MxPlugin *stack = nullptr;
  unsigned pending = 0, limit = 0, offered = 0, polls = 0, observed = 0;
  unsigned nextByte = 0;
  bool retireAll = false, exact = true, checkScalar = false;

  void checksumOffload(bool enabled) { tcpChksumOffload(enabled); }

  void check(const Zpkt::TxPacket &packet) {
    Zutcp::IPView ip;
    Zutcp::TCPView tcp;
    bool decoded = Zutcp::readIP({packet.data, packet.length}, ip) && Zutcp::readTCP(ip, tcp);
    if (!decoded) { exact = false; return; }
    exact &= tcp.header.seq == nextByte && tcp.payload.length() == 8;
    if (tcpChksumOffload()) {
      // Read the IPv4 pseudo-header's address words directly from the frame.
      const auto addresses = packet.data + 14 + 12;
      unsigned seed = ip.payload.length() + 6;
      for (unsigned i = 0; i < 8; i += 2)
	seed += (unsigned(addresses[i]) << 8) | addresses[i + 1];
      seed = (seed & 0xffff) + (seed >> 16);
      auto segment = ip.payload.data();
      exact &= seed == ((unsigned(segment[16]) << 8) | segment[17]);
    } else exact &= !Zutcp::MxPlugin::tcpChecksum(ip.local, ip.remote, ip.payload);
    for (auto byte : tcp.payload) exact &= byte == uint8_t(nextByte >> 3);
    nextByte += 8;
  }
  Zu::IO::Result send(uint8_t *packet, unsigned length) override {
    if (checkScalar) check({packet, length});
    ++pending;
    return Zi::OK;
  }
  unsigned send(ZuSpan<const Zpkt::TxPacket> packets) override {
    offered = packets.length();
    unsigned accepted = offered < limit ? offered : limit;
    for (unsigned i = 0; i < accepted; ++i) check(packets[i]);
    pending += accepted;
    return accepted;
  }
  unsigned pollTx() override {
    if (!retireAll) return 0;
    retireAll = false;
    observed = stack->txAdmission().outstanding;
    ++polls;
    unsigned retired = pending;
    pending = 0;
    return retired;
  }
};

struct BatchCxn : ZmNHeap<BatchCxn>,
  Zutcp::Cxn_<Zutcp::MxPlugin, BatchCxn, Zutcp::TxBufAlloc<>> {
friend inline ZuStringT<"Zutcp.Test.BatchCxn"> ZmNHeapID(BatchCxn *);
  using Base = Zutcp::Cxn_<Zutcp::MxPlugin, BatchCxn, Zutcp::TxBufAlloc<>>;
  using ZmNHeap<BatchCxn>::operator new;
  using ZmNHeap<BatchCxn>::operator delete;
  using Base::txNext;
  explicit BatchCxn(Zutcp::MxPlugin *app) : Base{app, true} { }
  void prepareRx() {
    Base::configLocalParams(app());
    Base::startHandshake();
    Base::setTCPOriginsRx({.local = UINT32_MAX, .peer = UINT32_MAX,
      .localKnown = true, .peerKnown = true, .server = true});
    Base::establish();
  }
  void prepareTx(const Zutcp::IPPath &path) {
    Base::initServerPathTx({path.local, 18181}, {path.remote, 18182});
    Base::extendTxCredit(80);
  }
  Zutcp::TxBatchResult batch(const Zutcp::IPPath &path) { return Base::sendTCPBatch(path); }
  bool wireFlush(const Zutcp::IPPath &path) {
    return Base::flushTCP([this, &path](
	const Zutcp::SentFrameRef &ref, bool syn, uint8_t control, bool persist) {
      return Base::sendTCP(ref, syn, control,
	[this, &path] { return app()->allocPacket(this, path.local.type(), true); },
	[this, &path](auto buf, Zutcp::EcnMark::T ecn) {
	  auto wire = Zutcp::writeTCP(
	    buf->data_(), buf->size, path, buf->tcp, buf->length, Zutcp::ecnBits(ecn));
	  if (!wire) return false;
	  buf->skip = 0;
	  buf->length = wire;
	  Zutcp::TCPLayout layout{14, uint16_t(Zutcp::ipHead(path.local.type()))};
	  return app()->sendPacket(buf, nullptr, &layout, &buf->retry);
	}, persist);
    });
  }
  unsigned limitCongestion(unsigned bytes) {
    auto charge = Base::congestionAllowance() - bytes;
    Base::m_congestion.sent(charge);
    return charge;
  }
  void releaseCongestion(unsigned bytes) { Base::m_congestion.release(bytes); }
  bool flushTx() { return false; } // The test explicitly controls each flush.
  bool retransmit(bool = false, unsigned = 2) { return false; }
  void queueRetransmit() { }
  void pto() { }
  bool sendSyn() { return false; }
  int process(Zutcp::Reader &) { return 0; }
  void clearRx() { Base::resetLink(); }
};

static void checkPartialBatch(bool offload)
{
  auto env = ZutcpTest::fixtureEnv();
  ZmRef<Zutcp::MxPlugin> stack = new Zutcp::MxPlugin;
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(3); })
    .rxThread(1).txThread(2), stack};
  ZmRef<BatchDevice> device = new BatchDevice{&mx};
  device->checksumOffload(offload);
  device->stack = stack.ptr();
  ZmRef<Zpkt::Nif> backend = device;
  stack->init(&mx, env, ZuMv(backend), {});
  bool started = mx.start();
  ZuCheckRT(started, log_("start deterministic partial-batch fixture"));
  if (!started) return;

  ZmRef<BatchCxn> cxn = new BatchCxn{stack.ptr()};
  Zutcp::IPPath path{.local = ZiIP{"198.18.0.1"}, .remote = ZiIP{"198.18.0.2"}, .mtu = 1500};
  ZmBlock<>{}([&mx, cxn](auto done) {
    mx.rxRun([&mx, cxn, done] {
      cxn->prepareRx();
      mx.txRun([done]() mutable { done(); });
    });
  });

  auto passed = ZmBlock<bool>{}([&mx, stack, cxn, device, &path](auto done) {
    mx.txRun([&mx, stack, cxn, device, &path, done]() mutable {
      cxn->prepareTx(path);
      bool queued = true;
      for (unsigned i = 0; i < 10; ++i) {
	auto buf = stack->txAllocator()->allocBuf(cxn.ptr(), 8);
	if (!buf) { queued = false; break; }
	memset(buf->data(), uint8_t(i), 8);
	queued &= cxn->enqueue_(ZuMv(buf));
      }
      bool filled = true;
      for (unsigned i = 0; i < 63; ++i) {
	auto buf = stack->allocPacket(&mx, ZiIPType::V4);
	if (!buf) { filled = false; break; }
	buf->length = 60;
	buf->skip = 0;
	memset(buf->data(), 0, 60);
	filled &= stack->sendPacket(buf);
      }
      device->limit = 8;
      device->retireAll = true;
      auto prefix = cxn->batch(path);
      bool partial = prefix.offered == 10 && prefix.accepted == 8 && prefix.blocked &&
	device->exact && device->polls == 1 && device->observed == 71 &&
	!stack->txAdmission().outstanding && cxn->txNext() == 64 && cxn->txRangeCount() == 2;
      device->limit = 2;
      auto suffix = cxn->batch(path);
      device->retireAll = true;
      stack->reconcileTx();
      bool drained = suffix.accepted == 2 && device->exact && cxn->txNext() == 80 &&
	!cxn->txRangeCount() && !stack->txAdmission().outstanding;
      done(queued && filled && partial && drained);
    });
  });
  ZuCheckRT(passed, log_("count 71 before polling once and resubmit the ordered suffix; offload=",
    offload));

  auto window = ZmBlock<bool>{}([&mx, stack, cxn, device, &path](auto done) {
    mx.txRun([stack, cxn, device, &path, done]() mutable {
      if (cxn->txNext() != 80) { done(false); return; }
      for (unsigned i = 10; i < 12; ++i) {
	auto buf = stack->txAllocator()->allocBuf(cxn.ptr(), 8);
	if (!buf) { done(false); return; }
	// The synthetic heap pool requires the scalar lease to return its base address.
	buf->skip = Zutcp::tcpHead(path.local.type());
	memset(buf->data(), uint8_t(i), 8);
	if (!cxn->enqueue_(ZuMv(buf))) { done(false); return; }
      }
      device->checkScalar = true;
      cxn->extendTxCredit(81);
      unsigned charge = cxn->limitCongestion(7);
      auto batch = cxn->batch(path);
      bool limited = !batch.offered && !cxn->wireFlush(path) &&
	cxn->txNext() == 80 && cxn->txRangeCount() == 2;
      cxn->releaseCongestion(charge);
      batch = cxn->batch(path);
      bool first = !batch.offered && cxn->wireFlush(path) &&
	cxn->txNext() == 88 && cxn->txRangeCount() == 1;
      cxn->extendTxCredit(96);
      bool second = cxn->wireFlush(path) && cxn->txNext() == 96 &&
	!cxn->txRangeCount() && device->exact;
      device->retireAll = true;
      stack->reconcileTx();
      done(limited && first && second && !stack->txAdmission().outstanding);
    });
  });
  ZuCheckRT(window, log_("a one-byte peer window admits one whole original with enough congestion credit"));

  ZmBlock<>{}([&mx, cxn](auto done) {
    mx.rxRun([&mx, cxn, done] {
      cxn->clearRx();
      mx.txRun([done]() mutable { done(); });
    });
  });
  ZuCheckRT(mx.stop(), log_("stop after all batch admissions retire"));
}

static void testPartialBatch()
{
  ZuTestScopeRT(testPartialBatch);
  checkPartialBatch(false);
  checkPartialBatch(true);
}

static void testCompletionRetries()
{
  ZuTestScopeRT(testCompletionRetries);
  auto env = ZutcpTest::fixtureEnv();
  ZmRef<Zutcp::MxPlugin> stack = new Zutcp::MxPlugin;
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(3); })
    .rxThread(1).txThread(2), stack};
  ZmRef<RetryDevice> device = new RetryDevice{&mx};
  ZmRef<Zpkt::Nif> backend = device;
  bool configured = false;
  try { stack->init(&mx, env, ZuMv(backend), {}); configured = true; }
  catch (ZeException &e) {
    std::cerr << e << '\n' << std::flush;
  }
  ZuCheckRT(configured, log_("configure completion retry service"));
  if (!configured) return;
  if (!mx.start()) { ZuCheck(false); return; }
  enum { Count = 130 }; // more than two bounded 64-connection owner turns
  RetryCxn cxns[Count];
  ZmSemaphore turn, last;
  unsigned markerCalls[2] = {};
  uint64_t markerNS[2] = {};
  bool accepted = true;
  mx.txRun([stack, &mx, &cxns, &accepted, &turn, &last, &markerCalls, &markerNS] {
    for (unsigned i = 0; i < 2; ++i) {
      auto packet = stack->allocPacket(&mx, ZiIPType::V4);
      if (!packet) { accepted = false; break; }
      memset(packet->data_(), 0, 64);
      packet->skip = 0; packet->length = 64;
      accepted &= stack->sendPacket(packet);
    }
    for (unsigned i = 0; i < Count; ++i) {
      auto cxn = &cxns[i];

      cxn->retry = [stack, cxn, i, &mx, &cxns, &last, &markerCalls, &markerNS] {
	if (!i && cxn->calls <= 2) {
	  // Ordinary owner work queued inside the first retry must run before
	  // the next retry batch, even when every connection refuses again.
	  unsigned pass = cxn->calls - 1;
	  auto start = Zm::now();
	  mx.txRun([pass, start, &cxns, &markerCalls, &markerNS] {
	    unsigned calls = 0;
	    for (auto &cxn : cxns) calls += cxn.calls;
	    markerCalls[pass] = calls - pass * Count;
	    markerNS[pass] = uint64_t((Zm::now() - start).nanosecs());
	  });
	}
	// Repeated refusal coalesces; a completion permits only a finite pass.
	stack->waitTx(cxn);
	stack->waitTx(cxn);
	if (i + 1 == Count) last.post();
      };
      stack->waitTx(cxn);
      stack->waitTx(cxn);
    }
    turn.post();
  });
  turn.wait();
  bool first = false, second = false;
  mx.txRun([device] { device->completed = 1; });
  bool reached = !last.timedwait(Zm::now(2));
  mx.txRun([stack, device, &cxns, &first, &turn] {
    first = device->pending == 1 && stack->txAdmission().outstanding == 1;
    for (auto &cxn : cxns) first &= cxn.calls == 1;
    turn.post();
  });
  turn.wait();
  mx.txRun([device] { device->completed = 1; });
  reached &= !last.timedwait(Zm::now(2));
  mx.txRun([stack, device, &cxns, &second, &turn] {
    second = !device->pending && !stack->txAdmission().outstanding;
    for (auto &cxn : cxns) {
      second &= cxn.calls == 2 && !cxn.txWaitPrev;
      stack->cancelTxWait(&cxn);
    }
    turn.post();
  });
  turn.wait();
  // The provider can disappear while a whole list awaits the last device lease.
  // With no CQ, the first poll observing failure must arrange every remaining turn.
  unsigned terminalMarker = 0;
  mx.txRun([&, stack, device] {
    auto packet = stack->allocPacket(&mx, ZiIPType::V4);
    ZmAssert_(packet);
    memset(packet->data_(), 0, 64);
    packet->skip = 0;
    packet->length = 64;
    ZmAssert_(stack->sendPacket(packet));
    for (unsigned i = 0; i < Count; ++i) {
      cxns[i].retry = [&, i] {
        if (!i) mx.txRun([&] {
          for (auto &c : cxns) terminalMarker += c.last == Zi::IOError;
        });
        if (i + 1 == Count) last.post();
      };
      stack->waitTx(&cxns[i]);
    }
    device->down = 1;
  });
  bool terminal = !last.timedwait(Zm::now(5));
  mx.txRun([&] {
    for (auto &c : cxns) {
      terminal &= c.calls == 3 && c.last == Zi::IOError && !c.txWaitPrev;
      stack->cancelTxWait(&c);
    }
    turn.post();
  });
  turn.wait();
  bool stopped = mx.stop();
  ZuCheckRT(terminal && terminalMarker == 64 && stopped,
    log_("provider-down drains all 130 waiters in bounded turns with no CQ"));
  std::cout << "# retry owner marker calls=" << markerCalls[0] << ',' << markerCalls[1]
    << " delay_ns=" << markerNS[0] << ',' << markerNS[1] << '\n';
  ZuCheckRT(accepted && reached && first && second && stopped,
    log_("130 coalesced waiters get one pass per actual completion, drain across "
      "bounded owner turns, and stop with no queued retry or lease"));
  ZuCheckRT(markerCalls[0] == 64 && markerCalls[1] == 64,
    log_("ordinary owner work runs between completion retry batches under repeated refusal"));
}

class MulticastCxn : public ZmNHeap<MulticastCxn>, public ZiConnection {
friend inline ZuStringT<"Zutcp.Test.Multicast"> ZmNHeapID(MulticastCxn *);
public:
  using ZmNHeap<MulticastCxn>::operator new;
  using ZmNHeap<MulticastCxn>::operator delete;
  MulticastCxn(ZiMultiplex *mx, const ZiCxnInfo &info) : ZiConnection{mx, info} { }
  void connected(ZiRxContext &) override { }
  void disconnected(bool) override { }
  unsigned delivered = 0, rawAtDelivery = 0;
  const unsigned *raw = nullptr;
};

enum { McastAtLimit = 7, McastOverLimit = 8 };

bool ZutcpTest::MxPlugin::multicast(
  MxPlugin &stack, unsigned mode, bool v6, unsigned &raw)
{
  auto mx = stack.mx();
  auto allocator = mx->findAllocator<Zi::IOBufPoolAllocator<Zi::IOBufType::Rx>>();
  if (!allocator) return false;
  auto pool = static_cast<Pool *>(allocator->pool.ptr());
  ZiIP group{v6 ? "ff02::1234" : "239.18.0.1"};
  ZiIP peer{v6 ? "2001:db8::1" : "198.18.0.1"};
  ZiCxnInfo info{.type = ZiCxnType::UDP, .socket = Zi::nullSocket(),
    .local = {group, 18191}, .rxAllocator = allocator};
  info.options.udp(true).multicast(true);
  if (v6) info.options.mreq(ZiMReq{group, 1U});
  else info.options.mreq(ZiMReq{group, ZiIP{in_addr{}}});
  unsigned key = (unsigned(group.type())<<16) | info.local.port();
  ZtArray<ZmRef<UdpCxn>> endpoints;
  ZtArray<ZmRef<MulticastCxn>> connections;
  unsigned count = mode < 2 ? mode : 2;
  if (mode >= McastAtLimit) count = ZutcpUDPMcastMatchMax + (mode == McastOverLimit);
  for (unsigned i = 0; i < count; ++i) {
    auto cxn = connections.push(ZmRef{new MulticastCxn{mx, info}})->ptr();
    cxn->rxUp() = 1;
    auto endpoint = endpoints.push(ZmRef{new UdpCxn{&stack, info, {}}})->ptr();
    if (mode == 4) {
      if (!stack.reserveCxn()) return false;
      ZiConnectFn connected{[cxn](const ZiCxnInfo &) { return cxn; }};
      if (!stack.Zutcp::MxPlugin::publishUDP(endpoint, connected, info)) return false;
    } else {
      endpoint->mxCxn = cxn;
      stack.m_udpBindings.addNode(endpoint);
    }
  }
  ZtArray<MulticastCxn *> order;
  {
    auto i = stack.m_udpBindings.citer(key);
    while (auto node = i())
      order.push(static_cast<MulticastCxn *>(static_cast<UdpCxn *>(node)->mxCxn));
  }
  auto cleanup = [&stack, &endpoints, &connections, mode, key] {
    for (const auto &cxn : connections) cxn->rxContext().complete();
    if (mode == 4) {
      for (const auto &endpoint : endpoints) endpoint->close(false);
    } else {
      auto i = stack.m_udpBindings.iter(key);
      while (auto node = i()) {
	static_cast<UdpCxn *>(node)->mxCxn = nullptr;
	i.del();
      }
    }
  };
  if (order.length() != count) { cleanup(); return false; }
  unsigned captured = count <= ZutcpUDPMcastMatchMax ? count : ZutcpUDPMcastMatchMax;
  ZtArray<ZmRef<ZiIOBuf>> held;
  held.length(count);
  ZtArray<MulticastCxn *> delivered;
  unsigned calls = 0, copies = 0, copyBytes = 0, wireLength = 0;
  uint8_t *origin = nullptr;
  uint8_t expectedFrame[14 + 40 + 8 + 4];
  bool exact = true;
  ZiRxBurst burst;
  auto expectedBurst = mode == 1 ? nullptr : &burst;
  auto inspect = [expectedBurst, &exact, &copies, &copyBytes, &wireLength, &origin, &expectedFrame](
      ZiRxContext &io) {
    exact &= io.burst == expectedBurst && io.buf->length == 4 && !memcmp(io.buf->data(), "data", 4) &&
      !memcmp(io.buf->data_(), expectedFrame, wireLength);
    if (io.buf->data_() != origin) { ++copies; copyBytes += wireLength; }
  };
  auto arm = [allocator, &held, &delivered, &calls, &inspect](
      MulticastCxn *cxn, unsigned slot) {
    auto receive = [cxn, slot, &held, &delivered, &calls, &inspect](ZiRxContext &io) {
      inspect(io);
      held[slot] = io.buf;
      delivered.push(cxn);
      io.buf->data()[0] = 'X';
      ++calls;
      io.complete();
      return true;
    };
    cxn->rxContext().init(ZiRxFn{ZuMv(receive)}, allocator->allocBuf(cxn));
  };
  for (unsigned i = 0; i < count; ++i)
    if (mode != 6 || i != 1) arm(order[i], i);
  if (mode == 3) order[0]->rxContext().complete();
  if (mode == 4 || mode == 6) {
    auto first = order[0], last = order[1];
    first->rxContext().init(ZiRxFn{
      [first, last, mode, &held, &delivered, &calls, &inspect, &arm](ZiRxContext &io) {
	inspect(io);
	held[0] = io.buf;
	delivered.push(first);
	io.buf->data()[0] = 'X';
	++calls;
	if (mode == 4) static_cast<UdpCxn *>(last->plugin())->close(false);
	else arm(last, 1);
	io.complete();
	return true;
      }}, allocator->allocBuf(first));
  }
  auto frame = pool->alloc();
  auto head = Zutcp::udpHead(group.type());
  memcpy(frame + head, "data", 4);
  Zutcp::IPPath path{.local = peer, .remote = group, .mtu = 1500};
  unsigned length = Zutcp::writeUDP(frame, pool->size, path, 18192, 18191, 4, 0, 64);
  origin = frame;
  wireLength = length;
  memcpy(expectedFrame, frame, length);
  unsigned allocations = pool->allocCalls, returned = pool->returns;
  if (mode == 5) pool->available = false;
  stack.received(frame, length, expectedBurst);
  pool->available = true;
  unsigned expected = captured;
  if (mode == 3 || mode == 4 || mode == 5) expected = 1;
  unsigned expectedCopies = captured && mode != 3 && mode != 5 ? captured - 1 : 0;
  unsigned expectedAllocs = captured && mode != 3 ? captured - 1 : 0;
  std::cout << "# multicast family=" << (v6 ? 6 : 4) << " mode=" << mode
    << " deliveries=" << calls << " copies=" << copies
    << " copyBytes=" << copyBytes << '\n';
  bool passed = length && calls == expected && exact &&
    copies == expectedCopies && copyBytes == expectedCopies * length &&
    raw == unsigned(mode == 0 || mode == 4) &&
    pool->allocCalls - allocations == expectedAllocs;
  if (passed) {
    for (unsigned i = 0; i < expected; ++i) {
      unsigned slot = i + unsigned(mode == 3 || mode == 5);
      bool original = slot + 1 == captured;
      passed &= delivered[i] == order[slot] && held[slot] &&
	(held[slot]->data_() == frame) == original;
      if (i && held[slot] && held[slot - 1])
	passed &= held[slot]->data_() != held[slot - 1]->data_();
    }
  }
  if (mode == 1) {
    // An unrelated Ethernet frame must publish preceding UDP work before its
    // raw callback. Even when it is the last descriptor, no later UDP callback
    // is available to drain the group.
    auto receiver = connections[0];
    receiver->raw = &raw;
    burst.append(receiver.ptr(), held[0], [](ZmPolymorph *p, Zi::IOQueue packets) {
      auto receiver = static_cast<MulticastCxn *>(p);
      receiver->delivered += packets.count_();
      receiver->rawAtDelivery = *receiver->raw;
    });
    auto rawFrame = pool->alloc();
    *rawFrame = 0;
    auto prior = raw;
    stack.received(rawFrame, 1, &burst);
    passed &= raw == prior + 1 && receiver->delivered == 1 &&
      receiver->rawAtDelivery == prior && !receiver->rxContext().burst;
    ++returned; // raw ingress lease
  }
  held.length(0);
  cleanup();
  return passed && pool->returns - returned == 1 + expectedCopies;
}

static void checkMulticast(unsigned mode, bool v6)
{
  ZiMultiplex mx{ZiMxParams{}.scheduler(
    [](auto &s) { s.nThreads(3); }).rxThread(1).txThread(2),
    ZmRef{new ZutcpTest::MxPlugin{&mx}}};
  auto stack = static_cast<ZutcpTest::MxPlugin *>(mx.plugin());
  auto config = ZutcpTest::MxPlugin::parameters(*stack);
  unsigned raw = 0;
  config.packet = [&raw](ZmRef<ZiIOBuf>) { ++raw; };
  bool initialized = false;
  try { ZutcpTest::MxPlugin::apply(*stack, config); initialized = true; }
  catch (ZeException &e) {
    std::cerr << e << '\n' << std::flush;
  }
  auto owner = stack;
  if (!initialized) { ZuCheckRT(false); return; }
  auto allocator = mx.allocator<Zi::IOBufType::Rx>();
  ZuCheckRT(initialized && allocator, log_("configure registered multicast input"));
  if (!initialized || !allocator) return;
  bool started = mx.start();
  bool passed = started && ZmBlock<bool>{}([&mx, owner, mode, v6, &raw](auto done) {
    mx.rxRun([owner, mode, v6, &raw, done = ZuMv(done)]() mutable {
      done(ZutcpTest::MxPlugin::multicast(*owner, mode, v6, raw));
    });
  });
  bool stopped = mx.stop();
  ZuCheckRT(passed && stopped,
    log_("multicast snapshots preserve mutable backing and live recipient admission"));
}

static void testMulticast()
{
  ZuTestScopeRT(testMulticast);
  for (unsigned mode = 0; mode <= McastOverLimit; ++mode) {
    if (ZutcpUDPMcastMatchMax == 1 && mode >= 2 && mode <= 6) continue;
    checkMulticast(mode, false);
    checkMulticast(mode, true);
  }
}

// Observe complete loopback frames at pool return, including a clone that is
// discarded after synchronous send refusal. The saved comparison is fixture-only.
struct LoopPool : ZmNHeap<LoopPool>, ZutcpTest::Pool {
friend inline ZuStringT<"Zutcp.Test.LoopPool"> ZmNHeapID(LoopPool *);
  using ZmNHeap<LoopPool>::operator new;
  using ZmNHeap<LoopPool>::operator delete;
  using ZutcpTest::Pool::Pool;
  uint8_t expected[1514];
  uint8_t *source = nullptr;
  unsigned length = 0;
  ZmAtomic<unsigned> copies{0}, bytes{0}, valid{1};
  void free(uint8_t *p) override {
    if (!length || p == source || memcmp(p, expected, length)) valid = 0;
    ++copies;
    bytes += length;
    ZutcpTest::Pool::free(p);
  }
};

struct LoopDevice : ZmNHeap<LoopDevice>, ZutcpTest::Xdp {
friend inline ZuStringT<"Zutcp.Test.LoopDevice"> ZmNHeapID(LoopDevice *);
  using ZmNHeap<LoopDevice>::operator new;
  using ZmNHeap<LoopDevice>::operator delete;
  using Xdp::Xdp;
  ZmRef<LoopPool> ingress;
  unsigned completions = 0;
  bool refuse = false;
  ZmRef<Zi::IOBufPool> bufPool(Zi::IOBufType::T type, unsigned count, unsigned size,
      unsigned head, unsigned tail, unsigned align) override
  {
    if (type != Zi::IOBufType::Rx)
      return Xdp::bufPool(type, count, size, head, tail, align);
    ingress = new LoopPool{type, count, size, head, tail, align, true};
    return ingress;
  }
  Zu::IO::Result send(uint8_t *p, unsigned n) override {
    ingress->source = p;
    ingress->length = n;
    memcpy(ingress->expected, p, n);
    if (!refuse) ++completions;
    return refuse ? Zi::IOError : Zi::OK;
  }
  unsigned pollTx() override { unsigned n = completions; completions = 0; return n; }
};

static void testLoopCopies()
{
  ZuTestScopeRT(testLoopCopies);
  auto env = ZutcpTest::fixtureEnv();
  ZmRef<Zutcp::MxPlugin> stack = new Zutcp::MxPlugin;
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(3); })
    .rxThread(1).txThread(2), stack};
  ZmRef<LoopDevice> device = new LoopDevice{&mx};
  ZmAtomic<unsigned> deliveries{0};
  Zutcp::MxPlugin::Params config{};
  config.packet = [&deliveries](ZmRef<ZiIOBuf>) { ++deliveries; };
  ZmRef<Zpkt::Nif> backend = device;
  bool configured = false;
  try { stack->init(&mx, env, ZuMv(backend), ZuMv(config)); configured = true; }
  catch (ZeException &e) {
    std::cerr << e << '\n' << std::flush;
  }
  auto tx = mx.allocator<Zi::IOBufType::TxUDP, ZiIOBuf, 128>(1);
  bool started = configured && tx && mx.start();
  ZuCheckRT(started, log_("start UDP copy-volume fixture with registered producer backing"));
  if (!started) return;
  for (bool v6 : {false, true}) for (unsigned mode = 0; mode < 5; ++mode) {
    bool passed = ZmBlock<bool>{}([&mx, &stack, &device, &deliveries, tx, v6, mode](auto done) {
      mx.txRun([&mx, &stack, &device, &deliveries, tx, v6, mode,
	  done = ZuMv(done)]() mutable {
	auto pool = device->ingress;
	unsigned allocs = pool->allocCalls, frees = pool->returns;
	unsigned copies = pool->copies, bytes = pool->bytes, received = deliveries;
	Zutcp::IPPath path{.local = ZiIP{v6 ? "2001:db8::1" : "198.18.0.1"},
	  .remote = ZiIP{v6 ? (mode ? "ff02::1234" : "2001:db8::2") :
	    (mode ? "239.146.0.1" : "198.18.0.2")}, .mtu = 1500};
	ZiCxnInfo info{.type = ZiCxnType::UDP, .socket = Zi::nullSocket(),
	  .options = ZiCxnOptions{}.udp(true).multicast(mode != 0).loopBack(mode >= 2),
	  .local = {path.local, 18192}, .remote = mode ? ZiSockAddr{} : ZiSockAddr{path.remote, 18191}};
	ZmRef<MulticastCxn> cxn = new MulticastCxn{&mx, info};
	auto packet = tx->allocBuf(cxn.ptr());
	bool valid = packet && packet->append("data");
	if (!valid) { done(false); return; }
	auto frame = packet->data() - Zutcp::udpHead(path.local.type());
	bool callback = false;
	cxn->txContext().init(ZiTxFn{[&callback, mode](ZiTxContext &io) {
	  callback = io.length == (mode == 3 ? int(Zi::IOError) : 4) && io.buf && io.buf->cspan() == "data";
	  io.complete();
	  return true;
	}}, ZuMv(packet), ZiSockAddr{path.remote, 18191});
	device->refuse = mode == 3;
	pool->available = mode != 4;
	ZutcpTest::MxPlugin::udpSend(*stack, cxn.ptr(), mode ? Zutcp::IPPath{} : path);
	pool->available = true;
	stack->reconcileTx();
	valid &= callback && pool->source == frame;
	if (mode) {
	  const uint8_t mac4[] = {0x01, 0x00, 0x5e, 0x12, 0x00, 0x01};
	  const uint8_t mac6[] = {0x33, 0x33, 0x00, 0x00, 0x12, 0x34};
	  valid &= !memcmp(pool->expected, v6 ? mac6 : mac4, 6) &&
	    !memcmp(pool->expected + 6, device->mac().data(), 6);
	}
	mx.rxRun([pool, &deliveries, mode, v6, allocs, frees, copies, bytes, received, valid,
	    done = ZuMv(done)]() mutable {
	  unsigned copied = unsigned(mode == 2 || mode == 3);
	  unsigned volume = pool->bytes - bytes;
	  bool exact = valid && pool->valid && pool->copies - copies == copied &&
	    volume == copied * (v6 ? 66 : 60) &&
	    pool->allocCalls - allocs == unsigned(mode >= 2) &&
	    pool->returns - frees == copied && deliveries - received == unsigned(mode == 2);
	  std::cout << "# loopback family=" << (v6 ? 6 : 4) << " mode=" << mode
	    << " copies=" << pool->copies - copies << " copyBytes=" << volume
	    << " allocations=" << pool->allocCalls - allocs << '\n';
	  done(exact);
	});
      });
    });
    ZuCheckRT(passed, log_("observe complete loopback copies, including refusal/exhaustion"));
  }
  ZuCheckRT(mx.stop(), log_("drain UDP copy fixture without retained native backing"));
}

static void testStoppedPlugin()
{
  ZuTestScope(testStoppedPlugin);
  auto env = ZutcpTest::fixtureEnv();
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(4); })
    .rxThread(1).txThread(2)};
  {
    Zutcp::MxPlugin cold;
    auto xdp = ZutcpTest::fixtureXdp(&mx);
    auto before = ZutcpTest::MxPlugin::parameters(cold);
    auto invalid = before;
    invalid.maxCxns = 0;
    bool result = false;
    try { cold.init(&mx, env, ZuMv(xdp), ZuMv(invalid)); result = true; }
    catch (ZeException &e) { std::cerr << e << '\n' << std::flush; }
    auto admission = cold.txAdmission();
    auto after = ZutcpTest::MxPlugin::parameters(cold);
    ZuCHECK(!result && !cold.ready() && !admission.capacity &&
        !admission.controlCount && !cold.mx() && xdp && after == before,
      "failed initial configuration leaves cold resources and policy untouched");
  }
  auto rx = ZutcpTest::pktRxAllocator(&mx);
  auto tx = ZutcpTest::pktTxAllocator(&mx);
  ZutcpTest::MxPlugin stack{&mx};
  auto config = ZutcpTest::MxPlugin::parameters(stack);
  bool first = false;
  try { ZutcpTest::MxPlugin::apply(stack, config); first = true; }
  catch (ZeException &e) { std::cerr << e << '\n' << std::flush; }
  ZuCheck(first);
  if (!first) return;
  auto capacity = stack.txAdmission().capacity;
  auto allocator = stack.txAllocator();
  Zutcp::ClosedTuple record{.key = {ZiSockAddr{ZiIP{"198.18.0.1"}, 18191},
    ZiSockAddr{ZiIP{"198.18.0.2"}, 18192}}, .txNext = 20, .rxNext = 30, .fin = true};
  record = stack.rememberClosed(record);
  bool repeated = false;
  try { ZutcpTest::MxPlugin::apply(stack, config); repeated = true; }
  catch (ZeException &e) { std::cerr << e << '\n' << std::flush; }
  Zutcp::ClosedTuple retained;
  ZuCHECK(first && repeated &&
      stack.txAdmission().capacity == capacity && stack.txAllocator() == allocator &&
      stack.closedTuple(record.key, retained) && retained.id == record.id,
    "repeated stopped init preserves allocator identity, capacity and closed tuple evidence");
  Endpoint endpoint;
  ZuCHECK(endpoint.init(&stack), "endpoint uses its known owning MxPlugin directly");
  ZuCHECK(!stack.ready() && !endpoint.connected() && !endpoint.listening(),
    "binding the known owner creates no transport endpoint or kernel fallback");
  ZuCHECK(rx->type == Zi::IOBufType::Rx && tx->type == Zi::IOBufType::TxTCP,
    "native protocol requests TCP network provider roles");
  stack.final();
  stack.final();
  ZuCHECK(stack.state() == ZmEngineState::Stopped && !stack.ready() &&
      !stack.txAdmission().capacity && !stack.closedTupleStats().count,
    "repeated final releases stopped configuration, pools and history");
}

static void testHandoff()
{
  ZuTestScope(testHandoff);
  auto env = ZutcpTest::fixtureEnv();
  auto tcp = new Zutcp::MxPlugin;
  ZmRef<Zutcp::MxPlugin> plugin{tcp};
  ZiMultiplex mx{{}, ZuMv(plugin)};
  auto xdp = ZutcpTest::fixtureXdp(&mx, true);
  auto fixture = ZmRef<ZutcpTest::Xdp>{static_cast<ZutcpTest::Xdp *>(xdp.ptr())};
  bool initialized = false;
  try { tcp->init(&mx, env, ZuMv(xdp), {}); initialized = true; }
  catch (ZeException &e) { std::cerr << e << '\n' << std::flush; }
  ZuCheck(initialized && !xdp && tcp->mx() == &mx && fixture->mx() == &mx);
  if (!initialized) return;
  bool repeated = false;
  try { tcp->init(&mx, env, ZuMv(xdp), {}); repeated = true; }
  catch (ZeException &e) {
    std::cerr << e << '\n' << std::flush;
  }
  ZuCheck(repeated && fixture->opens == 1);
  ZiMultiplex otherMx;
  bool otherMxRejected = false;
  try { tcp->init(&otherMx, env, ZuMv(xdp), {}); }
  catch (ZeException &e) {
    std::cerr << e << '\n' << std::flush;
    otherMxRejected = true;
  }
  ZuCheck(otherMxRejected && fixture->opens == 1);
  bool configRejected = false;
  try { tcp->init(&mx, env, ZuMv(xdp), {.packetCount = 32}); }
  catch (ZeException &e) {
    std::cerr << e << '\n' << std::flush;
    configRejected = true;
  }
  ZuCheck(configRejected);
  auto otherEnv = ZutcpTest::fixtureEnv();
  bool envRejected = false;
  try { tcp->init(&mx, otherEnv, ZuMv(xdp), {}); }
  catch (ZeException &e) {
    std::cerr << e << '\n' << std::flush;
    envRejected = true;
  }
  ZuCheck(envRejected);
  auto other = ZutcpTest::fixtureXdp(&mx);
  bool deviceRejected = false;
  try { tcp->init(&mx, env, ZuMv(other), {}); }
  catch (ZeException &e) {
    std::cerr << e << '\n' << std::flush;
    deviceRejected = true;
  }
  ZuCheck(deviceRejected && other);

  auto instance = tcp;
  ZuCheck(!plugin && mx.plugin() == instance);
  auto rx = mx.allocator<Zi::IOBufType::Rx>(1200);
  auto tcpTx = mx.allocator<Zi::IOBufType::TxTCP, ZiIOBuf, 256>(27);
  auto udpTx = mx.allocator<Zi::IOBufType::TxUDP, ZiIOBuf, 768>(35);
  ZuCheck(rx && tcpTx && udpTx);
  if (!rx || !tcpTx || !udpTx) return;
  ZuCheck(!tcpTx->allocBuf() && !udpTx->allocBuf() && !instance->txAllocator()->allocBuf());
  fixture->failAlloc = true;
  ZuCheck(mx.alloc().is<ZeException>() && !fixture->allocated() && !fixture->starts);
  ZuCheck(fixture->rxPool == rx->pool && fixture->txPools.length() == 2 &&
    fixture->txPools[0] == tcpTx->pool && fixture->txPools[1] == udpTx->pool);
  ZuCheck(rx->pool->count == 1200 && tcpTx->pool->count == 27 && udpTx->pool->count == 35);
  fixture->failAlloc = false;
  ZuCheck(mx.alloc().is<void>() && fixture->allocCalls == 2);
  auto native = tcpTx->allocBuf();
  auto udp = udpTx->allocBuf();
  auto data = instance->txAllocator()->allocBuf();
  auto txCtrlPool = instance->txCtrlPool();
  ZuCheck(txCtrlPool->size == 114 && txCtrlPool->headRoom == 114 && !txCtrlPool->tailRoom);
  auto control = txCtrlPool->alloc();
  ZuCheck(native && udp && data && control);
  if (control) txCtrlPool->free(control);
  if (!native) return;
  auto address = native->data();
  *address = 0x54;
  fixture->failStart = true;
  ZuCheck(!mx.start() && mx.state() == ZmEngineState::Stopped && fixture->starts == 1);
  fixture->failStart = false;
  bool started = mx.start();
  ZuCheck(started && fixture->starts == 2);
  if (!started) return;
  ZuCheck(mx.stop() && fixture->stops == 1);
  started = mx.start();
  ZuCheck(started && fixture->starts == 3);
  if (started) ZuCheck(mx.stop() && fixture->stops == 2);
  ZuCheck(fixture->allocCalls == 2 && native->data() == address && *address == 0x54);
}

static void testNeverStartedFinal()
{
  ZuTestScope(testNeverStartedFinal);
  for (bool allocate : {false, true}) {
    auto env = ZutcpTest::fixtureEnv();
    ZmRef<ZutcpTest::Xdp> fixture;
    {
      ZmRef<Zutcp::MxPlugin> tcp = new Zutcp::MxPlugin;
      ZiMultiplex mx{{}, tcp};
      auto xdp = ZutcpTest::fixtureXdp(&mx, true);
      fixture = static_cast<ZutcpTest::Xdp *>(xdp.ptr());
      bool result = false;
      try { tcp->init(&mx, env, ZuMv(xdp), {}); result = true; }
      catch (ZeException &e) { std::cerr << e << '\n' << std::flush; }
      ZuCheck(result);
      if (!result) return;
      if (allocate) ZuCheck(mx.alloc().is<void>());
    }
    ZuCheck(fixture->finals == 1 && !fixture->mx() && !fixture->starts && !fixture->stops);
  }
}

static void testFatalHandoffs()
{
  ZuTestScope(testFatalHandoffs);
  enum { Null, Uninitialized, DifferentMx, Allocated, RepeatAllocated };
  for (int scenario = Null; scenario <= RepeatAllocated; ++scenario) {
    auto pid = fork();
    if (!pid) {
      const rlimit limit{0, 0};
      setrlimit(RLIMIT_CORE, &limit);
      auto env = ZutcpTest::fixtureEnv();
      ZiMultiplex a, b;
      ZmRef<Zutcp::MxPlugin> tcp = new Zutcp::MxPlugin;
      ZmRef<Zpkt::Nif> xdp;
      if (scenario == Uninitialized) xdp = new ZutcpTest::Xdp{nullptr};
      else if (scenario != Null) xdp = ZutcpTest::fixtureXdp(&a);
      if (scenario == RepeatAllocated) {
        try { tcp->init(&a, env, ZuMv(xdp), {}); }
        catch (ZeException &e) { std::cerr << e << '\n' << std::flush; _exit(2); }
      }
      if (scenario == Allocated || scenario == RepeatAllocated) {
        if (!xdp) xdp = ZutcpTest::fixtureXdp(&a);
        Zi::IOBufPoolList tx;
        if (!xdp->alloc(nullptr, tx).is<void>()) _exit(3);
      }
      tcp->init(scenario == DifferentMx ? &b : &a, env, ZuMv(xdp), {});
      _exit(4);
    }
    int status = 0;
    ZuCheck(pid > 0 && waitpid(pid, &status, 0) == pid && WIFSIGNALED(status) &&
      WTERMSIG(status) == SIGABRT);
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testBurstQuantum);
  ZuTestCall(testListenPublication);
  ZuTestCall(testCompletionRetries);
  ZuTestCall(testPartialBatch);
  ZuTestCall(testMulticast);
  ZuTestCall(testLoopCopies);
  ZuTestCall(testStoppedPlugin);
  ZuTestCall(testHandoff);
  ZuTestCall(testNeverStartedFinal);
  ZuTestCall(testFatalHandoffs);
}
