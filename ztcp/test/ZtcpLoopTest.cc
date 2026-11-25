//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#ifndef _WIN32
#include <sys/socket.h>
#endif

#include <iostream>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmPlatform.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/Ztcp.hh>

#include "ZtcpTestPorts.hh"

using namespace ZuTestUtil;

namespace ZtcpLoopTest {

static Zm::ThreadID mainThread = 0;
static ZmAtomic<unsigned> cbErrors{0}, cbChecks{0};

// ZuTestMgr is single-threaded. Callbacks collect results; only the test thread
// emits TAP, after the native owners have drained.
#define ZTCP_CHECK_RT(x, ...) do { \
  if (Zm::getTID() == mainThread) { ZuCheckRT(x, log_(__VA_ARGS__)); } \
  else { \
    ++cbChecks; \
    if (!(x)) { ++cbErrors; log_(__VA_ARGS__); } \
  } \
} while (0)

constexpr ZuString Ping{"ping\r\n"};
constexpr ZuString Pong{"pong\r\n"};
enum class FinishCase { None, Data, FlushData, EmptyAfterFlush, Empty };
constexpr unsigned FinishPings = ZiIOBuf_DefltSize / Ping.length() * 3 + 1;
constexpr unsigned QueuedPings = 128 * 1024 / Ping.length();
bool pendingCase(FinishCase mode) {
  return mode == FinishCase::FlushData || mode == FinishCase::EmptyAfterFlush;
}
unsigned finishedBytes(FinishCase mode) {
  unsigned pings = pendingCase(mode) ? QueuedPings : 0;
  if (mode == FinishCase::Data || mode == FinishCase::FlushData) pings += FinishPings;
  return pings * Ping.length();
}

struct State {
  ZmSemaphore		done;
  ZmSemaphore		listening;
  ZmSemaphore		creditParked;
  ZmSemaphore		finishQueued;
  ZmAtomic<unsigned>	errors{0};
  ZmAtomic<unsigned>	doneCount{0};
  ZmAtomic<unsigned>	clientConnected{0};
  ZmAtomic<unsigned>	serverConnected{0};
  ZmAtomic<unsigned>	creditHold{0}, creditSeen{0}, creditReplies{0};
  ZmAtomic<void *>	serverLink{nullptr};
  ZmAtomic<ZiConnection *>	serverCxn{nullptr};
  ZmAtomic<unsigned>	finishCase{unsigned(FinishCase::None)}, finishOnTx{0};
  Ztc::QueueTelemetry	clientRx;
  Ztc::QueueTelemetry	clientTx;
  ZiIP			ip;
  unsigned		port = 0;
  unsigned		creditMessages = 0;
  unsigned		creditCap = 128;

  void fail() {
    errors = 1;
    done.post();
  }
  void doneOne() {
    if (doneCount.xchAdd(1) < 1) done.post();
  }
};

template <typename Link>
void queueTelemetry(
  Link &link, Ztc::QueueTelemetry &rx, Ztc::QueueTelemetry &tx)
{
  auto linkKey = link.telKey();
  unsigned count = 0;
  unsigned allQueues = link.allQueues(
      [&linkKey, &rx, &tx, &count](Ztc::Queue *queue) {
    auto key = queue->telKey();
    ZTCP_CHECK_RT(key.template p<0>() == linkKey.template p<0>(),
      "telemetry queue owner ID mismatch");
    ZTCP_CHECK_RT(key.template p<1>() == linkKey.template p<1>(),
      "telemetry queue ID mismatch");
    ZTCP_CHECK_RT(key.template p<2>() ==
	(count ? Ztc::QueueType::Tx : Ztc::QueueType::Rx),
      "telemetry queue iteration order mismatch");

    Ztc::QueueTelemetry data;
    data.ownerID = ZuID{} << "dirtyOwner";
    data.id = ZuID{} << "dirty";
    data.inBytes = data.outBytes = data.inCount = data.outCount = 1;
    data.count = data.size = data.full = 1;
    data.type = Ztc::QueueType::Thread;
    queue->telemetry(data);
    ZTCP_CHECK_RT(data.ownerID == key.template p<0>(),
      "queue telemetry owner ID mismatch");
    ZTCP_CHECK_RT(data.id == key.template p<1>(),
      "queue telemetry ID mismatch");
    ZTCP_CHECK_RT(data.type == key.template p<2>(),
      "queue telemetry type mismatch");
    ZTCP_CHECK_RT(!data.size && !data.full,
      "queue telemetry fields were not overwritten");
    if (data.type == Ztc::QueueType::Rx) {
      ZTCP_CHECK_RT(!data.outBytes && !data.outCount,
	"Rx telemetry output fields were not overwritten");
      rx = data;
    } else {
      tx = data;
    }
    ++count;
  });
  ZTCP_CHECK_RT(allQueues == 2, "allQueues returned incorrect count");
  ZTCP_CHECK_RT(count == allQueues,
    "allQueues did not enumerate returned count");
  unsigned repeat = 0;
  link.allQueues([&repeat](Ztc::Queue *queue) {
    auto key = queue->telKey();
    ZTCP_CHECK_RT(key.template p<2>() ==
	(repeat++ ? Ztc::QueueType::Tx : Ztc::QueueType::Rx),
      "allQueues queue order is not stable");
  });
  ZTCP_CHECK_RT(rx.ownerID == linkKey.template p<0>() &&
      tx.ownerID == linkKey.template p<0>(),
    "queue owner ID mismatch");
  ZTCP_CHECK_RT(rx.id == linkKey.template p<1>() &&
      tx.id == linkKey.template p<1>(),
    "queue ID mismatch");
  ZTCP_CHECK_RT(rx.type != tx.type,
    "Rx and Tx queue types do not discriminate the keys");
}

template <typename Link>
void checkQueues(Link &link)
{
  Ztc::QueueTelemetry rx;
  Ztc::QueueTelemetry tx;
  queueTelemetry(link, rx, tx);
}

ZiMxParams mxParams(bool credit = false)
{
  auto params = ZiMxParams()
    .scheduler([](auto &s) {
      s.nThreads(4)
	.thread(1, [](auto &t) { t.isolated(1); })
	.thread(2, [](auto &t) { t.isolated(1); })
	.thread(3, [](auto &t) { t.isolated(1); })
	.thread(4, [](auto &t) { t.isolated(1); }); })
    .rxThread(1).txThread(2);
  if (credit) params.rxBufSize(128);
  return params;
}

template <typename Link>
struct ReserveLayer : public ZiTxLayer<ReserveLayer<Link>, Link> {
  using Base = ZiTxLayer<ReserveLayer<Link>, Link>;

  ReserveLayer(Link &link, unsigned headRoom, unsigned tailRoom) :
    Base{link, headRoom, tailRoom} { }

  ~ReserveLayer() { this->flush(); }

  bool prepareBuf_(ZiIOBuf *, bool) { return true; }
};

template <typename Link>
void sendBytes(Link &link, ZuCSpan s)
{
  auto tx = link.txStream();
  ReserveLayer first{tx, 7, 3};
  ReserveLayer second{first, 11, 5};
  second << s << Zi::flush();
}

bool consume(Ztcp::RxStream &rx, ZuCSpan expected)
{
  unsigned remaining = expected.length();
  bool ok = false;
  rx.consume(
    [&remaining](ZuSpan<uint8_t> span) -> int64_t {
      if (remaining > span.length()) {
	remaining -= span.length();
	return 0;
      }
      return remaining;
    },
    [&ok, expected](ZuSpan<uint8_t> span) {
      ok = span.length() == expected.length() &&
	!memcmp(span.data(), expected.data(), expected.length());
    });
  return ok;
}

struct ClientApp : public Ztcp::Client<ClientApp> {
  struct Link;

  ClientApp(State *state_) : state{state_} { }

  unsigned reconnFreq() const { return 0; }

  State *state = nullptr;
};

struct ClientApp::Link : public Ztcp::CliLink<ClientApp, Link> {
  using Base = Ztcp::CliLink<ClientApp, Link>;

  Link(ClientApp *app) : Base(app, ZuID{"client"}) { }

  void connected(Ztcp::Connected) {
    ZTCP_CHECK_RT(this->stream() == this, "client bidi stream is not link");
    ZTCP_CHECK_RT(cxn() && cxn()->info().rxAllocator == app()->rxAllocator(),
      "outbound connection did not use the hub Rx allocator");
    Link::StreamRef stream = this->stream();
    ZTCP_CHECK_RT(stream, "client stream ref is null");
    auto tx = stream->txStream();
    (void)tx;
    app()->state->clientConnected = 1;
    unsigned count = app()->state->creditMessages;
    if (!count) count = 1;
    for (unsigned i = 0; i < count; ++i) sendBytes(*this, Ping);
  }
  void disconnected(bool) {
    ZTCP_CHECK_RT(this->txQueue.empty_(),
      "disconnect completed before I/O Tx drained");
  }

  int process(Ztcp::RxStream &rx) {
    if (!consume(rx, Pong)) return 0;
    if (app()->state->creditMessages &&
	app()->state->creditReplies.xchAdd(1) + 1 < app()->state->creditMessages)
      return 1;
    queueTelemetry(
      *this, app()->state->clientRx, app()->state->clientTx);
    app()->state->doneOne();
    disconnect();
    return 1;
  }
};

struct ServerApp : public Ztcp::Server<ServerApp> {
  struct Link;

  ServerApp(State *state_) : state{state_} { }

  ZiConnection *accepted(const ZiCxnInfo &ci);

  ZiIP localIP() const { return state->ip; }
  unsigned localPort() const { return state->port; }
  unsigned nAccepts() const { return 1; }

  void listening(const ZiListenInfo &info) {
    ZTCP_CHECK_RT(info.rxAllocator == rxAllocator(), "listener did not use the hub Rx allocator");
    state->port = info.port;
    state->listening.post();
  }
  void listenFailed(bool) { state->fail(); }

  State *state = nullptr;
};

// Small receive backing makes the credit boundary deterministic with a bounded
// 100-message offer; the kernel TCP MPS itself may be much larger.
struct ServerApp::Link : public Ztcp::SrvLink<ServerApp, Link, Ztcp::RxBufAlloc<128>> {
  using Base = Ztcp::SrvLink<ServerApp, Link, Ztcp::RxBufAlloc<128>>;

  Link(ServerApp *app) : Base(app),
      m_finishCase{FinishCase(unsigned(app->state->finishCase))},
      m_finishOnTx{bool(app->state->finishOnTx)} {
    if (app->state->creditMessages) rxCap_(app->state->creditCap);
  }

  void connected(Ztcp::Connected) {
    ZTCP_CHECK_RT(this->stream() == this, "server bidi stream is not link");
    Link::StreamRef stream = this->stream();
    ZTCP_CHECK_RT(stream, "server stream ref is null");
    auto tx = stream->txStream();
    (void)tx;
    app()->state->serverConnected = 1;
    if (m_finishCase != FinishCase::None) {
      if (m_finishOnTx)
	app()->txRun([link = ZmRef(this)] { link->finishOutput(link->txStream_()); });
      else
	finishOutput(txStream());
    }
  }
  template <typename Out>
  void finishOutput(Out out) {
    auto mode = m_finishCase;
    if (pendingCase(mode)) {
      for (unsigned i = 0; i < QueuedPings; ++i) out << Ping;
      out << Zi::flush();
    }
    if (mode == FinishCase::Data || mode == FinishCase::FlushData)
      for (unsigned i = 0; i < FinishPings; ++i) out << Ping;
    out << Zi::finish();
    ZTCP_CHECK_RT(!out.failed(), "public TCP stream rejected finish");
    if (pendingCase(mode)) {
      app()->txRun([link = ZmRef(this)] {
	link->app()->mx()->txRun([link] {
	  ZTCP_CHECK_RT(link->txQueue.count_() > 1, "final TCP entry did not queue behind data");
	  link->app()->state->finishQueued.post();
	});
      });
    }
  }
  void sending(ZiTxContext &io) {
    auto mode = m_finishCase;
    if (mode == FinishCase::None || mode == FinishCase::Empty) return;
    unsigned total = finishedBytes(mode);
    m_submitted += io.buf->length;
    ZTCP_CHECK_RT(io.disconnected() == (m_submitted == total),
      "physical buffer has incorrect finish flag");
  }
  void disconnected(bool) {
    ZTCP_CHECK_RT(this->txQueue.empty_(),
      "disconnect completed before I/O Tx drained");
    if (m_finishCase != FinishCase::None) app()->state->done.post();
  }

  int process(Ztcp::RxStream &rx) {
    if (app()->state->creditMessages && app()->state->creditHold.load_()) {
      ZTCP_CHECK_RT(!!cxn(), "credit input arrived before connection publication");
      if (cxn() && rx.length() >= app()->state->creditCap +
	  cxn()->info().rxAllocator->capacity() &&
	  !app()->state->creditSeen.xch(1)) app()->state->creditParked.post();
      return 0;
    }
    if (!consume(rx, Ping)) return 0;
    sendBytes(*this, Pong);
    return 1;
  }
private:
  const FinishCase m_finishCase;
  const bool m_finishOnTx;
  unsigned m_submitted = 0;	// physical Tx owner
};

ZiConnection *ServerApp::accepted(const ZiCxnInfo &ci)
{
  ZTCP_CHECK_RT(ci.rxAllocator == rxAllocator(), "accepted connection lost the hub Rx allocator");
  auto link = new Link(this);
  auto cxn = new Link::Cxn(link, ci);
  state->serverLink = link;
  state->serverCxn = cxn;
  return cxn;
}

void testLoop(
  const char *testName, ZiIP ip, const char *connectIP, unsigned port,
  unsigned creditMessages = 0)
{
  ZuTestScopeRT(testName);
  cbErrors = 0; auto priorChecks = cbChecks.load_();

  State state;
  state.ip = ip;
  state.port = port;
  state.creditMessages = creditMessages;
  state.creditHold = !!creditMessages;

  ServerApp server(&state);
  ClientApp client(&state);
  ZiMultiplex mx(mxParams(!!creditMessages));
  Zi::IOBufAllocator *serverTx = ServerApp::Link::txAllocator(&mx);
  Ztcp::ServerParams serverParams(&mx, "3", "4");
  serverParams.txAllocator(serverTx);
  ZTCP_CHECK_RT(server.init(ZuMv(serverParams)),
    "server init failed");
  auto serverRx = server.rxAllocator();
  ZTCP_CHECK_RT(serverRx && serverRx->capacity() == ServerApp::Link::RxBufAlloc::Size,
    "server init did not acquire the link's Rx policy");
  Zi::IOBufAllocator *clientRx = ClientApp::Link::rxAllocator(&mx);
  Zi::IOBufAllocator *clientTx = ClientApp::Link::txAllocator(&mx);
  Ztcp::ClientParams clientParams(&mx, "3", "4");
  clientParams.rxAllocator(clientRx).txAllocator(clientTx);
  ZTCP_CHECK_RT(client.init(ZuMv(clientParams)),
    "client init failed");
  ZTCP_CHECK_RT(client.rxAllocator() == clientRx && clientRx != serverRx,
    "client did not retain the supplied Rx allocator");
  bool mxStarted = mx.start();
  ZTCP_CHECK_RT(mxStarted, "ZiMultiplex start failed");
  if (!mxStarted) return;

  bool serverStarted = server.start();
  bool clientStarted = client.start();
  ZTCP_CHECK_RT(serverStarted && clientStarted,
    "TCP hubs failed to start");
  if (!serverStarted || !clientStarted) {
    if (clientStarted) client.stop();
    if (serverStarted) server.stop();
    client.final();
    server.final();
    mx.stop();
    return;
  }

  server.listen();
  ZTCP_CHECK_RT(state.listening.timedwait(Zm::now(5)) == 0,
    "server listen timed out");

  ZmRef<ClientApp::Link> link = new ClientApp::Link(&client);
  checkQueues(*link);
  link->connect(connectIP, state.port);

  if (creditMessages) {
    ZTCP_CHECK_RT(state.creditParked.timedwait(Zm::now(5)) == 0,
      "bounded receive did not park");
    ZmBlock{}([&mx](auto wake) { mx.rxRun([wake = ZuMv(wake)]() mutable { wake(); }); });
    auto cxn = static_cast<ServerApp::Link::Cxn *>(state.serverCxn.load_());
    ZTCP_CHECK_RT(cxn && cxn->rxParked(), "bounded receive did not disarm ingress");
    ZTCP_CHECK_RT(cxn && cxn->rxQueued() <=
	state.creditCap + (uint64_t(cxn->info().rxAllocator->capacity())<<1),
      "queued ingress exceeded credit plus two receive buffers");
    state.creditHold = 0;
    auto serverLink = static_cast<ServerApp::Link *>(state.serverLink.load_());
    if (serverLink) server.rxRun([link = ZmRef<ServerApp::Link>{serverLink}] {
      link->drainRx_();
    });
  }
  ZTCP_CHECK_RT(state.done.timedwait(Zm::now(5)) == 0, "loopback timed out");
  ZTCP_CHECK_RT(!state.errors.load_(), "loopback error");
  ZTCP_CHECK_RT(state.doneCount.load_() == 1, "client did not receive response");
  ZTCP_CHECK_RT(state.clientConnected.load_(), "client did not connect");
  ZTCP_CHECK_RT(state.serverConnected.load_(), "server did not connect");
  ZTCP_CHECK_RT(state.clientTx.inCount >= 1, "Tx ingress count is zero");
  ZTCP_CHECK_RT(state.clientTx.inBytes >= Ping.length(),
    "Tx ingress bytes omit ping");
  ZTCP_CHECK_RT(!state.clientRx.count, "Rx queue did not drain");
  ZTCP_CHECK_RT(!state.clientTx.count, "Tx queue did not drain");

  bool clientStopped = client.stop();
  bool serverStopped = server.stop();
  ZTCP_CHECK_RT(clientStopped && serverStopped, "TCP hubs failed to stop");
  client.final();
  server.final();
  mx.stop();
  link = nullptr;
  ZuCheckRT(!cbErrors.load_(), log_("callback checks failed"));
  ZuCheckRT(cbChecks.load_() > priorChecks, log_("callback checks did not run"));
}

void testFinish()
{
  ZuTestScopeRT(testFinish);
  cbErrors = 0;
  State state;
  state.ip = ZiIP{"127.0.0.1"};
  ServerApp server(&state);
  ZiMultiplex mx(mxParams().txBufSize(4096));
  Ztcp::ServerParams params(&mx, "3", "4");
  params.txAllocator(ServerApp::Link::txAllocator(&mx));
  if (!server.init(ZuMv(params)) || !mx.start()) {
    ZuCheckRT(false, log_("finish fixture initialization failed"));
    return;
  }
  if (!server.start()) {
    ZuCheckRT(false, log_("finish server start failed"));
    server.final();
    mx.stop();
    return;
  }
  server.listen();
  bool listening = !state.listening.timedwait(Zm::now(5));
  ZuCheckRT(listening, log_("finish server listen timed out"));
  sockaddr_in peer{};
  peer.sin_family = AF_INET;
  peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  peer.sin_port = htons(state.port);
  for (bool onTx : {false, true}) {
    for (auto mode : {FinishCase::Data, FinishCase::FlushData,
	FinishCase::EmptyAfterFlush, FinishCase::Empty}) {
      if (!listening) break;
      state.finishOnTx = onTx;
      state.finishCase = unsigned(mode);
      Zi::Socket fd = ::socket(AF_INET, SOCK_STREAM, 0);
#ifdef _WIN32
      DWORD timeout = 5000;
#else
      timeval timeout{5, 0};
#endif
      int receiveBytes = 4096;
      if (Zi::nullSocket(fd) || ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
	    reinterpret_cast<const char *>(&receiveBytes), sizeof(receiveBytes)) ||
	  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
	    reinterpret_cast<const char *>(&timeout), sizeof(timeout)) ||
	  ::connect(fd, reinterpret_cast<sockaddr *>(&peer), sizeof(peer))) {
	ZuCheckRT(false, log_("finish peer connect failed"));
	if (!Zi::nullSocket(fd)) Zi::closeSocket(fd);
	break;
      }
      if (pendingCase(mode))
	ZuCheckRT(!state.finishQueued.timedwait(Zm::now(5)), log_("final TCP queue timed out"));
      unsigned expected = finishedBytes(mode);
      unsigned bytes = 0;
      bool ordered = true;
      char buf[512];
      int n;
      while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0) {
	for (int i = 0; i < n; ++i, ++bytes)
	  if (buf[i] != Ping[bytes % Ping.length()]) ordered = false;
      }
      Zi::closeSocket(fd);
      ZuCheckRT(!n && ordered && bytes == expected,
	log_("finish peer expected ", expected, " bytes then EOF, received ", bytes, ", recv ", n));
      listening = !state.done.timedwait(Zm::now(5));
      ZuCheckRT(listening, log_("finish teardown timed out"));
    }
  }
  server.stop();
  server.final();
  mx.stop();
  ZuCheckRT(!cbErrors.load_(), log_("finish callback checks failed"));
}

} // namespace ZtcpLoopTest

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZtcpLoopTest::mainThread = Zm::getTID();
  ZiLog::init("ZtcpLoopTest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuTestMain();
  ZuTestCall(ZtcpLoopTest::testLoop, "testLoopIPv4", ZiIP{"127.0.0.1"}, "127.0.0.1",
    ZtcpTestPort::Loop4);
  ZuTestCall(ZtcpLoopTest::testLoop, "testLoopIPv6", ZiIP{"::1"}, "::1",
    ZtcpTestPort::Loop6);
  ZuTestCall(ZtcpLoopTest::testLoop, "testCreditIPv4", ZiIP{"127.0.0.1"}, "127.0.0.1",
    ZtcpTestPort::Loop4, 100);
  ZuTestCall(ZtcpLoopTest::testFinish);
  ZiLog::stop();
  return 0;
}
