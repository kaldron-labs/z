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
  void endpointFailed_(bool) {
    ++failed;
    ReadyFn fn = ZuMv(readyFn);
    if (fn) fn(false);
  }
  void endpointDatagram_(Zquic::Datagram) { ++datagrams; }

  ReadyFn readyFn;
  unsigned ready = 0;
  unsigned down = 0;
  unsigned failed = 0;
  unsigned datagrams = 0;
};

} // namespace

// Exercise the real endpoint connection's queue and completion callback with
// synthetic I/O results. The endpoint is already its friend; no socket, thread,
// private-data access or production test interface is needed.
struct QueuedEndpoint {
  using Impl = QueuedEndpoint;
  using Cxn = Zquic_::EndpointCxn_<QueuedEndpoint>;
  enum { EndpointRef = false };

  unsigned scheduled = 0;
  unsigned submitted = 0;
  unsigned sent = 0;
  unsigned errors = 0;
  unsigned wakeups = 0;

  ZiMultiplex *mx_() const { return nullptr; }
  QueuedEndpoint *impl() { return this; }
  bool endpointTxInvoked_() const { return true; }
  void connected_(Cxn *, ZiIOContext &) { }
  void disconnected_0(Cxn *, QueuedEndpoint *) { }
  void txSubmitted_(unsigned) { ++submitted; }
  void txBackPressure_() { }
  void sent_(unsigned) { ++sent; }
  void ioError_() { ++errors; }
  // Negative sentinel: the pre-fix completion path calls this for every
  // queued datagram. Protocol producers must not be woken by socket progress.
  void txDrained_() { ++wakeups; }
  template <typename L> void txRun_(L) { ++scheduled; }

  void exercise(bool fail)
  {
    ZuTestScopeRT(queuedSendCompletion);
    enum { N = 3 }; // active packet plus two FIFO followers
    ZiCxnInfo ci{.type = ZiCxnType::UDP, .socket = Zi::nullSocket()};
    ZmRef<Cxn> cxn = new Cxn{this, ci, 1};
    for (unsigned i = 0; i < N; ++i) {
      ZmRef<ZiIOBuf> buf = new Zquic::PktTxBufAlloc<>{nullptr};
      uint8_t value = i;
      buf->append(&value, 1);
      ZuCheckRT(cxn->enqueueTx_(ZuMv(buf), ZiSockAddr{}, Zquic::EcnMark::ECT0),
        log_("queued datagram admission failed"));
    }
    ZuCheckRT(submitted == N && cxn->dequeueTx_(),
      log_("queued datagrams did not select their first send"));
    ZiIOContext io;
    for (unsigned i = 0; i < N; ++i) {
      bool started = cxn->sendStart_(io);
      bool ready = started && io.ptr && io.size == 1;
      ZuCheckRT(ready && *io.ptr == i,
        log_("UDP completion did not advance queued datagrams in FIFO order"));
      if (!ready) return;
      io.length = fail && !i ? -1 : 1;
      ZuCheckRT(io() && io.completed(),
        log_("UDP completion did not retire its I/O context"));
      ZuCheckRT(scheduled == (i + 1 < N ? i + 1 : N - 1) && !wakeups,
        log_("UDP completion must schedule only the next socket send"));
    }
    ZuCheckRT(sent == N - unsigned(fail) && errors == unsigned(fail) &&
        !cxn->txPending() && !cxn->txQueued() && !wakeups,
      log_("UDP queue did not drain independently of protocol producers"));
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
      ZiMxParams().udpBufAllocator<Zquic::PktRxBufAllocator<>>()
	.scheduler([](auto &s) {
	  s.nThreads(5)
	    .thread(1, [](auto &t) { t.isolated(1); })
	    .thread(2, [](auto &t) { t.isolated(1); })
	    .thread(3, [](auto &t) { t.isolated(1); })
	    .thread(4, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));
  ZuCHECK(mx.start(), "endpoint rebind multiplexer start failed");

  TestEndpoint endpoint;
  ZuCHECK(endpoint.init(&mx), "endpoint rebind init failed");
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
  ZuTestCall(testEndpointRebind);
}
