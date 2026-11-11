//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/Zquic.hh>

#include "ZquicTestPorts.hh"

using namespace ZuTestUtil;

namespace {

struct TestEndpoint : public Zquic_::Endpoint_<TestEndpoint> {
  using Endpoint = Zquic_::Endpoint_<TestEndpoint>;
  using ReadyFn = ZmFn<void(bool), ZmFnHeapID<"Zquic.Endpoint.TestReady">>;
  static constexpr bool EndpointRef = false;

  void endpointReady_(Endpoint *) {
    ++ready;
    ReadyFn fn = ZuMv(readyFn);
    if (fn) fn(true);
  }
  void endpointDown_(Endpoint *) { ++down; }
  void endpointFailed(bool) {
    ++failed;
    ReadyFn fn = ZuMv(readyFn);
    if (fn) fn(false);
  }
  void endpointDatagram(Zquic::Datagram) { ++datagrams; }

  ReadyFn readyFn;
  unsigned ready = 0;
  unsigned down = 0;
  unsigned failed = 0;
  unsigned datagrams = 0;
};

} // namespace

// Hold physical output while exercising the real ZiTx admission/completion path.
struct HeldUDP : ZmNHeap<HeldUDP>, ZiMxPlugin {
friend inline ZuStringT<"Zquic.HeldUDP"> ZmNHeapID(HeldUDP *);
  ZmSemaphore event;
  ZmRef<ZiConnection> connection;
  unsigned calls = 0, closes = 0;
  ZiConnection *closing = nullptr;
  ZmFn<> beforeBind;
  explicit HeldUDP(ZiMultiplex *mx) : ZiMxPlugin{mx} { }
  Zi::IOBufPlugin *allocator(Zi::IOBufType::T, unsigned, unsigned, unsigned) override {
    return nullptr;
  }
  void start() override { mx()->pluginStarted({}); }
  void stop() override { mx()->pluginStopped({}); }
  void rx() override { event.wait(); }
  void wake() override { event.post(); }
  void listen(ZiListenFn, ZiFailFn, ZiConnectFn, ZiIP, uint16_t, unsigned,
    Zi::IOBufAllocator *, ZiCxnOptions) override { }
  void stopListening(ZiIP, uint16_t) override { }
  void connect(ZiConnectFn, ZiFailFn, ZiIPType::T, ZiIP, uint16_t, ZiIP,
    uint16_t, Zi::IOBufAllocator *, ZiCxnOptions) override { }
  void udp(ZiConnectFn fn, ZiFailFn, ZiIPType::T, ZiIP localIP,
    uint16_t localPort, ZiIP remoteIP, uint16_t remotePort,
    Zi::IOBufAllocator *allocator_, ZiCxnOptions options) override {
    ZiCxnInfo ci{.type = ZiCxnType::UDP, .socket = Zi::nullSocket(),
      .options = options, .local = {localIP, localPort},
      .remote = {remoteIP, remotePort}, .rxAllocator = allocator_};
    ZmFn<> before = ZuMv(beforeBind);
    if (before) before();
    connection = fn(ci);
    connection->connected(connection->rxContext());
  }
  void recv(ZiConnection *) override { }
  // An EAGAIN-equivalent hold does not change the installed context.
  void send(ZiConnection *) override { ++calls; }
  void disconnect(ZiConnection *cxn, bool peer) override { close(cxn, peer); }
  void close(ZiConnection *cxn, bool peer) override {
    ++closes;
    // Record duplicate physical close requests, but retire each socket once.
    if (closing == cxn) return;
    closing = cxn;
    cxn->txContext().complete();
    mx()->rxRun([this, cxn = ZmRef(cxn), peer]() {
      cxn->rxContext().complete();
      if (connection == cxn) connection = nullptr;
      cxn->disconnected(peer);
    });
  }
  TelKey telKey() const override { return {}; }
  void telemetry(Ztc::MxTelemetry &) const override { }
  unsigned allCxns(AllCxnsFn) const override { return 0; }
  unsigned allQueues(Ztc::QueueMgr::AllFn) const override { return 0; }
  void watch(AddCxnFn, DelCxnFn) override { }
  void unwatch() override { }
};

struct QueuedEndpoint {
  using Impl = QueuedEndpoint;
  using Cxn = Zquic_::EndpointCxn<QueuedEndpoint>;
  enum { EndpointRef = false };

  ZiMultiplex *mxPtr = nullptr;
  unsigned submitted = 0;
  unsigned submittedBytes = 0;
  unsigned sent = 0;
  unsigned sentBytes = 0;
  unsigned errors = 0;
  unsigned pressure = 0;
  unsigned drops = 0;
  unsigned wakeups = 0;

  ZiMultiplex *mx_() const { return mxPtr; }
  QueuedEndpoint *impl() { return this; }
  bool endpointTxInvoked() const { return mxPtr->invoked(mxPtr->txThread()); }
  void cxnAdded() { }
  void connected_(Cxn *, ZiRxContext &) { }
  void disconnected_0(Cxn *, QueuedEndpoint *) { }
  void txSubmitted(unsigned n) { ++submitted; submittedBytes += n; }
  void txBackPressure_() { ++pressure; }
  void txDropped_() { ++drops; }
  void sent_(unsigned n) { ++sent; sentBytes += n; }
  void ioError_() { ++errors; }
  void txDrained_() { ++wakeups; }

  void exercise(bool fail)
  {
    ZuTestScopeRT(queuedSendCompletion);
    ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(2); })
      .rxThread(1).txThread(2)};
    auto allocator = Zquic::pktTxAllocator(&mx);
    ZmRef<HeldUDP> provider = new HeldUDP{&mx};
    mx.plugin(provider);
    bool started = mx.start();
    ZuCheckRT(started, log_("synthetic UDP dispatcher start failed"));
    if (!started) return;
    mxPtr = &mx;
    ZiCxnInfo ci{.type = ZiCxnType::UDP, .socket = Zi::nullSocket()};
    ZmRef<Cxn> cxn = new Cxn{this, ci, 1};
    struct Result {
      bool admitted = true, order = true, metadata = true, bounded = true;
      bool retired = true, held = true, shortResult = true, isolated = true;
    };
    auto result = ZmBlock<Result>{}([this, &mx, cxn, fail, allocator, provider](auto wake) mutable {
      // Posting here also verifies setup/admission executes on the Tx owner.
      mx.txRun([this, cxn = ZuMv(cxn), fail, allocator, provider,
	wake = ZuMv(wake)]() mutable {
	Result r;
	auto packet = [allocator](unsigned value) {
	  auto buf = allocator->allocBuf();
	  uint8_t bytes[] = {uint8_t(value), uint8_t(value + 1)};
	  buf->append(bytes, sizeof(bytes));
	  return buf;
	};
	auto address = [](unsigned value) {
	  return ZiSockAddr{ZiIP{"127.0.0.1"}, uint16_t(12000 + value)};
	};
	auto mark = [](unsigned value) {
	  return value % 2 ? Zquic::EcnMark::ECT0 : Zquic::EcnMark::NotECT;
	};
	auto submit = [&cxn, &packet, &address, &mark](unsigned value, bool priority = false) {
	  return cxn->sendPkt(packet(value), address(value), mark(value), priority);
	};
	auto &io = cxn->txContext();
	r.admitted &= submit(0) && submit(1) && submit(2) &&
	  submit(3, true) && submit(4, true);
	auto head = io.buf;
	auto data = io.buf->data();
	auto count = cxn->txQueue.count_();
	provider->send(cxn);
	r.held &= io.buf == head && io.buf->data() == data &&
	  !io.offset && cxn->txQueue.count_() == count &&
	  cxn->txPending() && cxn->txQueued() == 4;
	r.order &= io.buf->data()[0] == 0;
	// A backend reporting a positive short UDP result leaves a suffix.
	// Record the frozen dependency limitation instead of emitting it.
	io.length = 1;
	io();
	r.shortResult &= io.offset == 1 && io.buf == head && !io.completed();
	r.held &= io.addr == address(0) && !io.tos.template is<uint8_t>();
	io.length = fail ? -1 : 1;
	io();
	if (fail) {
	  r.retired &= io.completed() && !cxn->txQueue.count_() &&
	    errors == 1 && !sent;
	} else {
	  unsigned expected[] = {4, 3, 1, 2};
	  for (auto value : expected) {
	    r.order &= io.buf && io.buf->data()[0] == value;
	    r.metadata &= io.addr == address(value) &&
	      (value % 2 ? io.tos.template is<uint8_t>() &&
		io.tos.template p<uint8_t>() == 2 :
		!io.tos.template is<uint8_t>());
	    io.length = 2;
	    io();
	  }
	  r.retired &= io.completed() && !cxn->txQueue.count_() &&
	    sent == 5 && sentBytes == 10 && !errors;
	}
	head = nullptr;
	unsigned before = submitted;
	auto rejected = packet(99);
	auto active = packet(7);
	r.admitted &= cxn->sendPkt(active, address(7), mark(7));
	for (unsigned i = 1; i < cxn->maxQueueLen(); ++i)
	  r.admitted &= submit(i);
	// End the call's full expression before checking its by-value reference.
	bool rejectedOK = cxn->sendPkt(rejected, address(99), mark(99), true);
	r.bounded &= cxn->txQueue.count_() == cxn->maxQueueLen() &&
	  cxn->txQueued() == cxn->maxQueueLen() - 1 && !rejectedOK &&
	  rejected->refCount() == 1 && pressure == 1 && drops == 1 &&
	  submitted == before + cxn->maxQueueLen() &&
	  submittedBytes == (submitted<<1) && io.buf == active;
	// A replacement physical connection has an independent queue/context.
	ZmRef<Cxn> next = new Cxn{this, cxn->info(), 2};
	r.isolated &= next->sendPkt(packet(8), address(8), mark(8)) &&
	  next->txQueue.count_() == 1 && cxn->txQueue.count_() == cxn->maxQueueLen();
	// A backend may retain backing after application queue retirement.
	ZmRef<ZiIOBuf> kernelHold = io.buf;
	cxn->closeTx();
	r.retired &= !cxn->sendPkt(packet(9), address(9)) && drops == 2;
	// A completion arriving after close admission retires only the old head.
	auto nextHead = next->txContext().buf;
	auto oldSent = sent;
	io.length = 2;
	io();
	r.isolated &= sent == oldSent + 1 && !wakeups &&
	  cxn->txQueue.count_() == cxn->maxQueueLen() - 1 &&
	  next->txContext().buf == nextHead && next->txQueue.count_() == 1;
	cxn->retireTx();
	r.retired &= !cxn->txPending() && !cxn->txQueued() &&
	  io.completed() && active->refCount() == 2 && kernelHold == active;
	kernelHold = nullptr;
	r.retired &= active->refCount() == 1;
	r.isolated &= next->txPending() && next->txContext().buf->data()[0] == 8;
	auto &nextIO = next->txContext();
	nextIO.length = 2;
	nextIO();
	r.isolated &= nextIO.completed() && !next->txQueued();
	next->retireTx();
	r.retired &= !wakeups;
	wake(r);
      });
    });
    ZuCheckRT(mx.stop(), log_("synthetic UDP dispatcher stop failed"));
    mxPtr = nullptr;
    ZuCheckRT(result.admitted, log_("packet admission failed"));
    ZuCheckRT(result.order, log_("active/FIFO/repeated-priority order mismatch"));
    ZuCheckRT(result.metadata, log_("successor destination or ECN inherited"));
    ZuCheckRT(result.bounded, log_("total queue capacity/accounting/rejection mismatch"));
    ZuCheckRT(result.held, log_("held packet backing or metadata changed"));
    ZuCheckRT(result.shortResult,
      log_("frozen positive-short UDP dependency characterization changed"));
    ZuCheckRT(result.isolated, log_("physical endpoint queues or completion crossed"));
    ZuCheckRT(result.retired, log_("completion/retirement leaked references or woke producers"));
  }
};

static void testQueuedSendCompletion()
{
  ZuTestScope(testQueuedSendCompletion);
  QueuedEndpoint success;
  ZuTestCall(success.exercise, false);
  QueuedEndpoint failure;
  ZuTestCall(failure.exercise, true);
}

static void testPostedAdmission()
{
  ZuTestScope(testPostedAdmission);
  using Cxn = Zquic_::EndpointCxn<TestEndpoint::Endpoint>;
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(2); })
    .rxThread(1).txThread(2)};
  auto rxAllocator = Zquic::pktRxAllocator(&mx);
  auto txAllocator = Zquic::pktTxAllocator(&mx);
  ZmRef<HeldUDP> provider = new HeldUDP{&mx};
  mx.plugin(provider);
  TestEndpoint endpoint;
  ZuCheck(endpoint.init(&mx, rxAllocator, txAllocator));
  ZuCheck(mx.start());
  auto open = ZmBlock<bool>{}([&endpoint](auto wake) {
    endpoint.readyFn = [wake = ZuMv(wake)](bool ok) mutable { wake(ok); };
    endpoint.openUDP(Zquic::PathMode::ServerUnconnected, ZiIP{"127.0.0.1"}, 12001);
  });
  ZuCheck(open);
  ZiSockAddr destination{ZiIP{"127.0.0.1"}, 12002};
  auto filled = ZmBlock<bool>{}([&endpoint, &mx, destination](auto wake) {
    mx.txRun([&endpoint, destination, wake = ZuMv(wake)]() mutable {
      bool ok = true;
      for (unsigned i = 0; i < Cxn::EndpointTxQueueLimit; ++i) {
	auto buf = endpoint.allocTxPkt();
	uint8_t value = 'x';
	buf->append(&value, 1);
	ok &= endpoint.send(ZuMv(buf), destination);
      }
      wake(ok);
    });
  });
  ZuCheck(filled);
  auto rejected = endpoint.allocTxPkt();
  uint8_t value = 'y';
  rejected->append(&value, 1);
  // Public off-owner submission acknowledges posting, then Tx rejects capacity.
  ZuCheck(endpoint.send(rejected, destination, Zquic::EcnMark::ECT1, true));
  auto pressure = ZmBlock<bool>{}([&endpoint, &mx, &rejected](auto wake) {
    mx.txRun([&endpoint, &rejected, wake = ZuMv(wake)]() mutable {
      auto d = endpoint.diag();
      wake(d.tx.asyncCalls == 1 && d.tx.txBackPressure == 1 &&
	d.tx.txDropped == 1 && d.tx.submittedTx == Cxn::EndpointTxQueueLimit &&
	d.txPending == 1 && d.txQueued == Cxn::EndpointTxQueueLimit - 1 &&
	rejected->refCount() == 1);
    });
  });
  ZuCheck(pressure);
  // Post from the rebind provider's Rx turn before it publishes replacement.
  // Tx may reject the full old queue before publication or drop the stale post
  // afterward; neither legitimate interleaving may send on the new socket.
  auto stale = endpoint.allocTxPkt();
  stale->append(&value, 1);
  provider->beforeBind = [&endpoint, &stale, destination]() {
    endpoint.send(stale, destination);
  };
  auto generation = endpoint.generation();
  auto rebound = ZmBlock<bool>{}([&endpoint](auto wake) {
    endpoint.rebindUDP(ZiIP{"127.0.0.1"}, 12003, ZiIP{}, 0,
      [wake = ZuMv(wake)](bool ok, ZiSockAddr, bool) mutable { wake(ok); });
  });
  ZuCheck(rebound && endpoint.generation() == generation + 1);
  unsigned pressureAfter = 0;
  auto isolated = ZmBlock<bool>{}([&endpoint, &mx, &stale, &pressureAfter](auto wake) {
    mx.txRun([&endpoint, &stale, &pressureAfter, wake = ZuMv(wake)]() mutable {
      auto d = endpoint.diag();
      pressureAfter = d.tx.txBackPressure;
      wake(!d.txPending && !d.txQueued && !d.tx.datagramsTx &&
        d.tx.asyncCalls == 2 && d.tx.txDropped == 2 && stale->refCount() == 1);
    });
  });
  ZuCheck(isolated);
  ZuCheck(pressureAfter == 1 || pressureAfter == 2);
  log("# rebind post: stale=", pressureAfter == 1, " physical pressure=", pressureAfter);
  unsigned notified = 0;
  ZmBlock<>{}([&endpoint, &mx, &notified](auto wake) {
    mx.rxRun([&endpoint, &notified, wake = ZuMv(wake)]() mutable {
      endpoint.disconnect([&notified]() { ++notified; });
      endpoint.disconnect([&notified, wake = ZuMv(wake)]() mutable {
        ++notified;
        wake();
      });
    });
  });
  ZuCheck(endpoint.down == 1 && !endpoint.connected() && notified == 2);
  log("# physical close requests=", provider->closes);
  ZuCheck(provider->closes == 2); // one old rebind socket and one current socket
  ZuCheck(mx.stop());
}

void testPlans()
{
  ZuTestScope(testPlans);

  Zquic::SockConfig client;
  client.mode = Zquic::PathMode::ClientConnected;
  client.probe = true;
  auto c = Zquic::Sock::plan(client);
  ZuCHECK(c.noFragment, "client plan should disable fragmentation");
  ZuCHECK(c.probeMode, "client probe mode missing");
  ZuCHECK(c.pmtuQuery, "client PMTU query branch missing");

  Zquic::SockConfig server;
  server.mode = Zquic::PathMode::ServerUnconnected;
  auto s = Zquic::Sock::plan(server);
  ZuCHECK(s.noFragment, "server plan should disable fragmentation");
  ZuCHECK(!s.pmtuQuery, "server must not query connected-socket PMTU");

  Zquic::SockDiag diag;
  auto hint = Zquic::Sock::pathHint(Zi::nullSocket(), client, &diag);
  ZuCHECK(!hint, "invalid socket unexpectedly produced PMTU hint");
  ZuCHECK(diag.mtuQueryErrors == 1, "PMTU query error not counted");
}

void testECNTOS()
{
  ZuTestScope(testECNTOS);

  ZuCHECK(Zquic::ecnBits(Zquic::EcnMark::NotECT) == 0x00,
    "NotECT bits mismatch");
  ZuCHECK(Zquic::ecnBits(Zquic::EcnMark::ECT1) == 0x01,
    "ECT1 bits mismatch");
  ZuCHECK(Zquic::ecnBits(Zquic::EcnMark::ECT0) == 0x02,
    "ECT0 bits mismatch");
  ZuCHECK(Zquic::ecnBits(Zquic::EcnMark::CE) == 0x03,
    "CE bits mismatch");

  ZuCHECK(!Zquic::ecnTOS(Zquic::EcnMark::NotECT).template is<uint8_t>(),
    "NotECT should not force TOS ancillary data");
  ZuCHECK(Zquic::ecnTOS(Zquic::EcnMark::ECT0).template is<uint8_t>() &&
      Zquic::ecnTOS(Zquic::EcnMark::ECT0).template p<uint8_t>() == 0x02,
    "ECT0 TOS mismatch");

  ZuCHECK(Zquic::ecnMarkFromTOS({}) == Zquic::EcnMark::NotECT,
    "empty TOS should decode as NotECT");
  ZuCHECK(Zquic::ecnMarkFromTOS(uint8_t(0x00)) == Zquic::EcnMark::NotECT,
    "TOS NotECT decode mismatch");
  ZuCHECK(Zquic::ecnMarkFromTOS(uint8_t(0x2d)) == Zquic::EcnMark::ECT1,
    "TOS ECT1 decode mismatch");
  ZuCHECK(Zquic::ecnMarkFromTOS(uint8_t(0x2e)) == Zquic::EcnMark::ECT0,
    "TOS ECT0 decode mismatch");
  ZuCHECK(Zquic::ecnMarkFromTOS(uint8_t(0x2f)) == Zquic::EcnMark::CE,
    "TOS CE decode mismatch");
}

void testEndpointRebind()
{
  ZuTestScope(testEndpointRebind);

  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(5)
	    .thread(1, [](auto &t) { t.isolated(1); })
	    .thread(2, [](auto &t) { t.isolated(1); })
	    .thread(3, [](auto &t) { t.isolated(1); })
	    .thread(4, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));

  Zi::IOBufAllocator *rxAllocator = Zquic::pktRxAllocator(&mx);
  Zi::IOBufAllocator *txAllocator = Zquic::pktTxAllocator(&mx);
  ZuCHECK(rxAllocator && txAllocator,
    "endpoint packet allocator registration failed");
  TestEndpoint endpoint;
  ZuCHECK(endpoint.init(&mx, rxAllocator, txAllocator), "endpoint rebind init failed");
  ZuCHECK(mx.start(), "endpoint rebind multiplexer start failed");
  ZiIP localIP{"127.0.0.1"};
  bool openReady = ZmBlock<bool>{}([&endpoint, localIP](auto wake) {
    endpoint.readyFn = [wake = ZuMv(wake)](bool ok) mutable {
      wake(ok);
    };
    if (!endpoint.openUDP(
	Zquic::PathMode::ServerUnconnected,
	localIP, ZquicTestPort::Sock)) {
      auto fn = ZuMv(endpoint.readyFn);
      if (fn) fn(false);
    }
  });
  ZuCHECK(openReady && endpoint.connected(),
    "endpoint initial UDP open did not connect");
  unsigned oldGeneration = endpoint.generation();
  uint16_t oldPort = endpoint.local().port();
  ZuCHECK(endpoint.ready == 1 && oldGeneration && oldPort,
    "endpoint initial UDP open state mismatch");

  bool rebindOK = ZmBlock<bool>{}([&mx, &endpoint](auto wake) {
    mx.rxRun([&endpoint, wake = ZuMv(wake)]() mutable {
      endpoint.rebindUDP(
	ZiIP{"127.0.0.1"}, ZquicTestPort::SockRebind, ZiIP{}, 0,
	[&endpoint, wake = ZuMv(wake)](
	  bool ok, ZiSockAddr local, bool) mutable {
	  const ZiSockAddr &now = endpoint.local();
	  wake(ok && local && local == now);
	});
    });
  });
  ZuCHECK(rebindOK, "endpoint UDP rebind failed");
  ZuCHECK(endpoint.connected() && endpoint.generation() == oldGeneration + 1 &&
      endpoint.local().port() && endpoint.local().port() != oldPort,
    "endpoint UDP rebind did not refresh local endpoint");

  ZmBlock<>{}([&endpoint](auto wake) {
    endpoint.disconnect([wake = ZuMv(wake)]() mutable { wake(); });
  });
  mx.stop();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testPlans);
  ZuTestCall(testECNTOS);
  ZuTestCall(testQueuedSendCompletion);
  ZuTestCall(testPostedAdmission);
  ZuTestCall(testEndpointRebind);
}
