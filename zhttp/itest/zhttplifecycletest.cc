//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// HTTP transport lifecycle characterization

#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/Ztcp.hh>
#include <zlib/Ztls.hh>
#include <zlib/Zquic.hh>

#include "ZhttpTestUtil.hh"
#include "zhttpitestports.hh"

using namespace ZuTestUtil;

namespace zhttplifecycletest_ {

using namespace Zhttp::Test;

constexpr ZuString Ping{"ping"};
constexpr ZuString Pong{"pong"};

struct State {
  LifeTrace		trace;
  ZmSemaphore		listening;
  ZmSemaphore		done;
  ZmSemaphore		cliClosed;
  ZmAtomic<unsigned>	logicalClosed = 0;
  ZmAtomic<unsigned>	errors = 0;
  uint16_t		port = 0;

  void fail() {
    errors = 1;
    done.post();
    cliClosed.post();
  }
  void closed(int event) {
    trace.push(event);
    if (logicalClosed.xchAdd(1) == 1) done.post();
  }
};

ZiMxParams mxParams()
{
  return ZiMxParams()
    .scheduler([](auto &s) {
      s.nThreads(4)
	.thread(1, [](auto &t) { t.isolated(1); })
	.thread(2, [](auto &t) { t.isolated(1); })
	.thread(3, [](auto &t) { t.isolated(1); })
	.thread(4, [](auto &t) { t.isolated(1); });
    })
    .rxThread(1).txThread(2);
}

template <typename Rx>
bool consume(Rx &rx, ZuCSpan expected)
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

template <typename Stream>
void txReady(Stream &stream, State &state, int event)
{
  auto tx = stream.txStream();
  (void)tx;
  state.trace.push(event);
}

template <typename Stream>
void sendMsg(Stream &stream, State &state, int event, ZuCSpan data)
{
  auto tx = stream.txStream();
  state.trace.push(event);
  tx << data << Zi::flush();
}

void checkTrace(State &state)
{
  ZuTestScopeRT(checkTrace);

  auto &trace = state.trace;
  ZuCheckRT(!state.errors.load_(), log_("lifecycle transport error"));
  ZuCheckRT(trace.count(LifeEvt::Init) == 1, log_("init event count mismatch"));
  ZuCheckRT(trace.count(LifeEvt::Start) == 1, log_("start event count mismatch"));
  ZuCheckRT(trace.count(LifeEvt::CliConnect) == 1,
    log_("client connect event count mismatch"));
  ZuCheckRT(trace.count(LifeEvt::CliConnected) == 1,
    log_("client connected event count mismatch"));
  ZuCheckRT(trace.count(LifeEvt::SrvConnected) == 1,
    log_("server connected event count mismatch"));
  ZuCheckRT(trace.count(LifeEvt::CliTxReady) == 1,
    log_("client Tx readiness event count mismatch"));
  ZuCheckRT(trace.count(LifeEvt::CliSend) == 1,
    log_("client send event count mismatch"));
  ZuCheckRT(trace.count(LifeEvt::SrvTxReady) == 1,
    log_("server Tx readiness event count mismatch"));
  ZuCheckRT(trace.count(LifeEvt::CliProcess) == 1,
    log_("client process event count mismatch"));
  ZuCheckRT(trace.count(LifeEvt::SrvProcess) == 1,
    log_("server process event count mismatch"));
  ZuCheckRT(trace.count(LifeEvt::SrvSend) == 1,
    log_("server send event count mismatch"));
  ZuCheckRT(trace.count(LifeEvt::CliDisconnected) == 1,
    log_("client disconnected event count mismatch"));
  ZuCheckRT(trace.count(LifeEvt::SrvDisconnected) == 1,
    log_("server disconnected event count mismatch"));
  ZuCheckRT(trace.count(LifeEvt::Stop) == 1, log_("stop event count mismatch"));
  ZuCheckRT(trace.count(LifeEvt::Final) == 1, log_("final event count mismatch"));
  ZuCheckRT(trace.before(LifeEvt::Init, LifeEvt::Start),
    log_("hub started before initialization"));
  ZuCheckRT(trace.before(LifeEvt::Start, LifeEvt::CliConnected),
    log_("client connected before hub start"));
  ZuCheckRT(trace.before(LifeEvt::Start, LifeEvt::CliConnect),
    log_("client connect attempted before hub start"));
  ZuCheckRT(trace.before(LifeEvt::CliConnect, LifeEvt::CliConnected),
    log_("client connected before connect attempt"));
  ZuCheckRT(trace.before(LifeEvt::Start, LifeEvt::SrvConnected),
    log_("server connected before hub start"));
  ZuCheckRT(trace.before(LifeEvt::CliConnected, LifeEvt::CliTxReady),
    log_("client Tx stream acquired before connected"));
  ZuCheckRT(trace.before(LifeEvt::CliTxReady, LifeEvt::CliSend),
    log_("client sent before Tx stream readiness"));
  ZuCheckRT(trace.before(LifeEvt::SrvConnected, LifeEvt::SrvTxReady),
    log_("server Tx stream acquired before connected"));
  ZuCheckRT(trace.before(LifeEvt::SrvTxReady, LifeEvt::SrvProcess),
    log_("server processed before Tx stream readiness"));
  ZuCheckRT(trace.before(LifeEvt::SrvProcess, LifeEvt::SrvSend),
    log_("server sent before processing initial message"));
  ZuCheckRT(trace.before(LifeEvt::SrvSend, LifeEvt::CliProcess),
    log_("client processed before server response"));
  ZuCheckRT(trace.before(LifeEvt::CliProcess, LifeEvt::CliDisconnected),
    log_("client disconnected before processing response"));
  ZuCheckRT(trace.before(LifeEvt::SrvProcess, LifeEvt::SrvDisconnected),
    log_("server disconnected before processing request"));
  ZuCheckRT(trace.before(LifeEvt::CliDisconnected, LifeEvt::Stop),
    log_("client connection remained live at stop"));
  ZuCheckRT(trace.before(LifeEvt::SrvDisconnected, LifeEvt::Stop),
    log_("server connection remained live at stop"));
  ZuCheckRT(trace.before(LifeEvt::Stop, LifeEvt::Final),
    log_("hub finalized before stop"));
}

struct TCPClient : public Ztcp::Client<TCPClient> {
  struct Link;

  State	*state = nullptr;

  TCPClient(State *state_) : state{state_} { }
  unsigned reconnFreq() const { return 0; }
};

struct TCPClient::Link : public Ztcp::CliLink<TCPClient, Link> {
  using Base = Ztcp::CliLink<TCPClient, Link>;

  Link(TCPClient *app) : Base{app} { }

  void connected(Ztcp::Connected) {
    auto &state = *app()->state;
    state.trace.push(LifeEvt::CliConnected);
    txReady(*this, state, LifeEvt::CliTxReady);
    sendMsg(*this, state, LifeEvt::CliSend, Ping);
  }
  void disconnected(bool) {
    app()->state->closed(LifeEvt::CliDisconnected);
  }
  void connectFailed(bool) { app()->state->fail(); }
  int process(Ztcp::RxStream &rx) {
    if (!consume(rx, Pong)) return 0;
    app()->state->trace.push(LifeEvt::CliProcess);
    disconnect();
    return 1;
  }
};

struct TCPServer : public Ztcp::Server<TCPServer> {
  struct Link;

  State	*state = nullptr;

  TCPServer(State *state_) : state{state_} { }

  ZiConnection *accepted(const ZiCxnInfo &);
  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  unsigned localPort() const { return state->port; }
  unsigned nAccepts() const { return 1; }
  void listening(const ZiListenInfo &) { state->listening.post(); }
  void listenFailed(bool) { state->fail(); }
};

struct TCPServer::Link : public Ztcp::SrvLink<TCPServer, Link> {
  using Base = Ztcp::SrvLink<TCPServer, Link>;

  Link(TCPServer *app) : Base{app} { }

  void connected(Ztcp::Connected) {
    auto &state = *app()->state;
    state.trace.push(LifeEvt::SrvConnected);
    txReady(*this, state, LifeEvt::SrvTxReady);
  }
  void disconnected(bool) {
    app()->state->closed(LifeEvt::SrvDisconnected);
  }
  int process(Ztcp::RxStream &rx) {
    if (!consume(rx, Ping)) return 0;
    auto &state = *app()->state;
    state.trace.push(LifeEvt::SrvProcess);
    sendMsg(*this, state, LifeEvt::SrvSend, Pong);
    return 1;
  }
};

ZiConnection *TCPServer::accepted(const ZiCxnInfo &ci)
{
  return new Link::Cxn(new Link{this}, ci);
}

struct TLSClient : public Ztls::Client<TLSClient> {
  using TxBufAlloc = Ztls::TxBufAlloc<>;
  struct Link;

  State	*state = nullptr;

  TLSClient(State *state_) : state{state_} { }
  unsigned reconnFreq() const { return 0; }
};

struct TLSClient::Link :
  public Ztls::CliLink<TLSClient, Link, Ztls::RxBufAlloc<>, TxBufAlloc> {
  using Base = Ztls::CliLink<TLSClient, Link, Ztls::RxBufAlloc<>, TxBufAlloc>;

  Link(TLSClient *app) : Base{app} { }

  void connected(Ztls::Connected) {
    auto &state = *app()->state;
    state.trace.push(LifeEvt::CliConnected);
    txReady(*this, state, LifeEvt::CliTxReady);
    sendMsg(*this, state, LifeEvt::CliSend, Ping);
  }
  void disconnected(bool) {
    app()->state->closed(LifeEvt::CliDisconnected);
  }
  void connectFailed(bool) { app()->state->fail(); }
  int process(Ztls::RxStream &rx) {
    if (!consume(rx, Pong)) return 0;
    app()->state->trace.push(LifeEvt::CliProcess);
    disconnect();
    return 1;
  }
};

struct TLSServer : public Ztls::Server<TLSServer> {
  using TxBufAlloc = Ztls::TxBufAlloc<>;
  struct Link;

  State	*state = nullptr;

  TLSServer(State *state_) : state{state_} { }

  ZiConnection *accepted(const ZiCxnInfo &);
  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  unsigned localPort() const { return state->port; }
  unsigned nAccepts() const { return 1; }
  void listening(const ZiListenInfo &) { state->listening.post(); }
  void listenFailed(bool) { state->fail(); }
};

struct TLSServer::Link :
  public Ztls::SrvLink<TLSServer, Link, Ztls::RxBufAlloc<>, TxBufAlloc> {
  using Base = Ztls::SrvLink<TLSServer, Link, Ztls::RxBufAlloc<>, TxBufAlloc>;

  Link(TLSServer *app) : Base{app} { }

  void connected(Ztls::Connected) {
    auto &state = *app()->state;
    state.trace.push(LifeEvt::SrvConnected);
    txReady(*this, state, LifeEvt::SrvTxReady);
  }
  void disconnected(bool) {
    app()->state->closed(LifeEvt::SrvDisconnected);
  }
  int process(Ztls::RxStream &rx) {
    if (!consume(rx, Ping)) return 0;
    auto &state = *app()->state;
    state.trace.push(LifeEvt::SrvProcess);
    sendMsg(*this, state, LifeEvt::SrvSend, Pong);
    return 1;
  }
};

ZiConnection *TLSServer::accepted(const ZiCxnInfo &ci)
{
  return new Link::Cxn(new Link{this}, ci);
}

struct QUICClient : public Zquic::Client<QUICClient> {
  struct Link;
  struct Stream;

  State	*state = nullptr;

  QUICClient(State *state_) : state{state_} { }
  unsigned reconnFreq() const { return 0; }
};

struct QUICClient::Stream :
  public Zquic::CliStream<QUICClient::Link, QUICClient::Stream> {
  using Base = Zquic::CliStream<QUICClient::Link, QUICClient::Stream>;
  using Base::Base;

  int process(Zquic::RxStream &);
};

struct QUICClient::Link :
  public Zquic::CliLink<QUICClient, QUICClient::Link, QUICClient::Stream> {
  using Base =
    Zquic::CliLink<QUICClient, QUICClient::Link, QUICClient::Stream>;
  using Base::Base;

  Link(QUICClient *app) : Base{app} { }

  void connected(Zquic::Connected) {
    auto &state = *app()->state;
    state.trace.push(LifeEvt::H3CliConnected);
    app()->txRun([link = ZmRef(this)]() mutable {
      auto &state = *link->app()->state;
      auto stream = link->stream(Zquic::StreamType::Duplex);
      if (!stream) {
	state.fail();
	return;
      }
      state.trace.push(LifeEvt::CliConnected);
      auto tx = stream->txStream_();
      state.trace.push(LifeEvt::CliTxReady);
      state.trace.push(LifeEvt::CliSend);
      tx << Ping << Zi::flush();
    });
  }
  void disconnected(bool) { }
  void connectFailed(bool) { app()->state->fail(); }
  void streamed(ZmRef<Stream>) { }
};

int QUICClient::Stream::process(Zquic::RxStream &rx)
{
  uint64_t before = rx.length();
  if (!consume(rx, Pong)) return 0;
  uint64_t length = before - rx.length();
  if (!retireRx(length)) return -1;
  auto &state = *link()->app()->state;
  state.trace.push(LifeEvt::CliProcess);
  state.closed(LifeEvt::CliDisconnected);
  link()->disconnect([state_ = &state]() {
    state_->cliClosed.post();
  });
  return 1;
}

struct QUICServerLink;
struct QUICServerStream;

struct QUICServer : public Zquic::Server<QUICServer, QUICServerLink> {
  using Link = QUICServerLink;
  using Stream = QUICServerStream;

  State		*state = nullptr;
  ZmRef<Link>	link;

  QUICServer(State *state_) : state{state_} { }

  ZmRef<Link> accepted(const Zquic::InitialInfo &);
  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  uint16_t localPort() const { return state->port; }
  void listening() { state->listening.post(); }
  void listenFailed(bool) { state->fail(); }
};

struct QUICServerStream :
  public Zquic::SrvStream<QUICServerLink, QUICServerStream> {
  using Base = Zquic::SrvStream<QUICServerLink, QUICServerStream>;
  using Base::Base;

  int process(Zquic::RxStream &rx);
};

struct QUICServerLink :
  public Zquic::SrvLink<QUICServer, QUICServerLink, QUICServerStream> {
  using Base =
    Zquic::SrvLink<QUICServer, QUICServerLink, QUICServerStream>;
  using Base::Base;

  QUICServerLink(QUICServer *app) : Base{app} { }

  void connected(Zquic::Connected) {
    app()->state->trace.push(LifeEvt::H3SrvConnected);
  }
  void disconnected(bool) { }
  void streamed(ZmRef<Stream> stream) {
    auto &state = *app()->state;
    state.trace.push(LifeEvt::SrvConnected);
    txReady(*stream, state, LifeEvt::SrvTxReady);
  }
};

int QUICServerStream::process(Zquic::RxStream &rx)
{
  uint64_t before = rx.length();
  if (!consume(rx, Ping)) return 0;
  uint64_t length = before - rx.length();
  if (!retireRx(length)) return -1;
  auto &state = *link()->app()->state;
  state.trace.push(LifeEvt::SrvProcess);
  sendMsg(*this, state, LifeEvt::SrvSend, Pong);
  state.closed(LifeEvt::SrvDisconnected);
  return 1;
}

ZmRef<QUICServer::Link> QUICServer::accepted(const Zquic::InitialInfo &)
{
  link = new Link{this};
  return link;
}

template <typename Client, typename Server,
  Zi::IOBufType::T RxType, Zi::IOBufType::T TxType, typename ClientParams,
  typename ServerParams>
void runH1(
  ZiMultiplex &mx, ClientParams cliParams, ServerParams srvParams,
  State &state)
{
  ZuTestScopeRT(runH1);

  Client client{&state};
  Server server{&state};
  auto cliRx = Client::Link::rxAllocator(&mx);
  auto cliTx = Client::Link::txAllocator(&mx);
  auto srvRx = Server::Link::rxAllocator(&mx);
  auto srvTx = Server::Link::txAllocator(&mx);
  cliParams.rxAllocator(cliRx).txAllocator(cliTx);
  srvParams.rxAllocator(srvRx).txAllocator(srvTx);
  if constexpr (TxType == Zi::IOBufType::TxTLS) {
    auto cliPacket = Client::Link::txPacketAllocator(&mx);
    auto srvPacket = Server::Link::txPacketAllocator(&mx);
    cliParams.txPacketAllocator(cliPacket);
    srvParams.txPacketAllocator(srvPacket);
    ZuCheckRT(cliTx->type == Zi::IOBufType::TxTLS &&
      srvTx->type == Zi::IOBufType::TxTLS &&
      cliPacket->type == Zi::IOBufType::TxTCP &&
      srvPacket->type == Zi::IOBufType::TxTCP, log_("TLS allocator role mismatch"));
  }
  bool serverInit = server.init(ZuMv(srvParams));
  bool clientInit = client.init(ZuMv(cliParams));
  ZuCheckRT(serverInit && clientInit, log_("hub initialization failed"));
  if (!serverInit || !clientInit) {
    if (clientInit) client.final();
    if (serverInit) server.final();
    return;
  }
  state.trace.push(LifeEvt::Init);
  ZuCheckRT(mx.start(), log_("multiplexer start failed"));
  if (!mx.running()) return;

  bool serverStart = server.start();
  bool clientStart = client.start();
  ZuCheckRT(serverStart && clientStart, log_("hub start failed"));
  if (!serverStart || !clientStart) {
    if (clientStart) client.stop();
    if (serverStart) server.stop();
    client.final();
    server.final();
    return;
  }
  state.trace.push(LifeEvt::Start);

  server.listen();
  bool listening = state.listening.timedwait(Zm::now(10)) == 0;
  ZuCheckRT(listening, log_("server listen timed out"));

  ZmRef<typename Client::Link> link;
  if (listening) {
    link = new Client::Link{&client};
    state.trace.push(LifeEvt::CliConnect);
    link->connect("127.0.0.1", state.port);
    bool done = state.done.timedwait(Zm::now(10)) == 0;
    ZuCheckRT(done, log_("message lifecycle timed out"));
    if (!done) state.fail();
  }

  server.stopListening();
  bool clientStopped = client.stop();
  bool serverStopped = server.stop();
  ZuCheckRT(clientStopped && serverStopped, log_("hub stop failed"));
  state.trace.push(LifeEvt::Stop);
  link = nullptr;
  client.final();
  server.final();
  state.trace.push(LifeEvt::Final);
  if (listening) ZuTestCallRT(checkTrace, state);
}

void testTCP()
{
  ZuTestScopeRT(testTCP);

  State state;
  state.port = loopbackPort(ZhttpITestPort::Lifecycle);
  ZuCheckRT(state.port, log_("TCP loopback port allocation failed"));
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};
  ZuTestCallRT((runH1<TCPClient, TCPServer,
    Zi::IOBufType::RxTCP, Zi::IOBufType::TxTCP>),
    mx, Ztcp::ClientParams(&mx, "3", "4"),
    Ztcp::ServerParams(&mx, "3", "4"), state);
  mx.stop();
}

void testTLS(const TempDir &temp)
{
  ZuTestScopeRT(testTLS);

  State state;
  state.port = loopbackPort(ZhttpITestPort::Lifecycle + 1);
  ZuCheckRT(state.port, log_("TLS loopback port allocation failed"));
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};
  ZuTestCallRT((runH1<TLSClient, TLSServer,
    Zi::IOBufType::RxTLS, Zi::IOBufType::TxTLS>),
    mx, Ztls::ClientParams(&mx, "3", "4").hub(
      [&temp](auto &p) { p.caPath(temp.certPath.cspan()); }),
    Ztls::ServerParams(&mx, "3", "4")
      .certPath(temp.certPath.cspan()).keyPath(temp.keyPath.cspan()),
    state);
  mx.stop();
}

void testQUIC(const TempDir &temp)
{
  ZuTestScopeRT(testQUIC);

  State state;
  state.port = loopbackPort(ZhttpITestPort::Lifecycle + 2);
  ZuCheckRT(state.port, log_("QUIC loopback port allocation failed"));
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};

  QUICServer server{&state};
  QUICClient client{&state};
  bool serverInit = server.init(
    Zquic::ServerParams(&mx, "3", "4")
      .certPath(temp.certPath.cspan()).keyPath(temp.keyPath.cspan())
      .alpn(ZuSpan<ZuCSpan>{"h3"})
      .maxStreamsDuplex(4).maxStreamsSimplex(4));
  bool clientInit = client.init(
    Zquic::ClientParams(&mx, "3", "4")
      .caPath(temp.certPath.cspan()).alpn(ZuSpan<ZuCSpan>{"h3"})
      .maxStreamsDuplex(4).maxStreamsSimplex(4));
  ZuCheckRT(serverInit && clientInit, log_("QUIC hub initialization failed"));
  if (!serverInit || !clientInit) {
    if (clientInit) client.final();
    if (serverInit) server.final();
    mx.stop();
    return;
  }
  state.trace.push(LifeEvt::Init);
  ZuCheckRT(mx.start(), log_("multiplexer start failed"));
  if (!mx.running()) return;

  bool serverStart = server.start();
  bool clientStart = client.start();
  ZuCheckRT(serverStart && clientStart, log_("QUIC hub start failed"));
  if (!serverStart || !clientStart) {
    if (clientStart) client.stop();
    if (serverStart) server.stop();
    client.final();
    server.final();
    mx.stop();
    return;
  }
  state.trace.push(LifeEvt::Start);

  bool listening = state.listening.timedwait(Zm::now(10)) == 0;
  ZuCheckRT(listening, log_("QUIC server listen timed out"));

  ZmRef<QUICClient::Link> link;
  if (listening) {
    link = new QUICClient::Link{&client};
    state.trace.push(LifeEvt::CliConnect);
    link->connect(Zquic::Host{"127.0.0.1"}, state.port);
    bool done = state.done.timedwait(Zm::now(10)) == 0;
    ZuCheckRT(done, log_("QUIC message lifecycle timed out"));
    if (!done) state.fail();
    ZuCheckRT(state.cliClosed.timedwait(Zm::now(10)) == 0,
      log_("QUIC client close timed out"));
  }

  bool clientStopped = client.stop();
  bool serverStopped = server.stop();
  ZuCheckRT(clientStopped && serverStopped, log_("QUIC hub stop failed"));
  state.trace.push(LifeEvt::Stop);
  link = nullptr;
  server.link = nullptr;
  client.final();
  server.final();
  state.trace.push(LifeEvt::Final);
  if (listening) {
    ZuTestCallRT(checkTrace, state);
    ZuCheckRT(state.trace.count(LifeEvt::H3CliConnected) == 1,
      log_("QUIC private client session connected count mismatch"));
    ZuCheckRT(state.trace.count(LifeEvt::H3SrvConnected) == 1,
      log_("QUIC private server session connected count mismatch"));
  }
  mx.stop();
}

} // namespace zhttplifecycletest_

int main(int argc, char **argv)
{
  using namespace zhttplifecycletest_;

  parse(argc, argv);

  ZiTestResidue::init("zhttplifecycletest");

  ZiLog::init("zhttplifecycletest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  TempDir temp;
  ZuTestMain();
  ZuTestCall(testTCP);
  ZuCHECK(temp.init(), "temporary certificate creation failed");
  if (temp.certPath && temp.keyPath) {
    ZuTestCall(testTLS, temp);
    ZuTestCall(testQUIC, temp);
  }

  ZiLog::stop();
  return 0;
}
