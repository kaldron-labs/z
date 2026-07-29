//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// WebSocket-over-HTTP/1 engine integration tests

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/Zws.hh>

#include "ZhttpTestUtil.hh"

using namespace ZuTestUtil;

namespace ZwsH1EngineTest_ {

using Zhttp::Test::TempDir;
using Zhttp::Test::loopbackPort;

struct State {
  ZmSemaphore		listening;
  ZmSemaphore		done;
  ZmAtomic<unsigned>	connected = 0;
  ZmAtomic<unsigned>	disconnected = 0;
  ZmAtomic<unsigned>	errors = 0;
  ZmAtomic<unsigned>	handshakeErrors = 0;
  ZmAtomic<unsigned>	lastError = Zws::Failure::None;
  ZmAtomic<unsigned>	srvMessages = 0;
  ZmAtomic<unsigned>	cliMessages = 0;
  ZmAtomic<unsigned>	closes = 0;
  ZmAtomic<unsigned>	reconnects = 0;
  ZtString<>		request;
  ZtString<>		response;
  uint16_t		port = 0;

  void fail() {
    errors = 1;
    done.post();
  }
  void down() {
    if (disconnected.xchAdd(1) + 1 == 4) done.post();
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

template <typename Link>
bool validInfo(const Zhttp::ConnectedInfo &info)
{
  return info.secure == bool(Link::TLS) &&
    info.multiplexed == bool(Link::Multiplexed) &&
    (!Link::TLS || Link::Multiplexed || info.alpn == "http/1.1");
}

struct ServerApp {
  State		*state;
  ZtString<>	message;

  void listening(const ZiListenInfo &) { state->listening.post(); }
  void listening() { state->listening.post(); }
  void listenFailed(bool) { state->fail(); }

  template <typename Link>
  bool accept(
      Link &, ZuCSpan, ZuCSpan target, ZuCSpan protocols,
      Zws::HandshakeString &selected) {
    if (target != "/stream" || !Zws::token(protocols, "chat"))
      return false;
    selected = "chat";
    return true;
  }

  template <typename Link>
  void connected(Link &link, Zhttp::ConnectedInfo info) {
    if (link.protocol() != "chat" || !validInfo<Link>(info)) {
      state->fail();
      return;
    }
    ++state->connected;
  }

  template <typename Link, typename Rx>
  int process(Link &link, Rx &rx) {
    auto events = rx.events();
    if (events & Zi::RxEvent::Start()) {
      if (link.messageOpcode() != Zws::Opcode::Text) return -1;
      message.length(0);
    }
    while (rx.input()) {
      int64_t n = rx.consume(
	[](ZuBSpan span) -> int64_t { return span.length(); },
	[this](ZuBSpan span) { message << span; });
      if (n <= 0) break;
    }
    events |= rx.events();
    if (events & Zi::RxEvent::Error()) return -1;
    if (events & Zi::RxEvent::Final()) {
      ++state->srvMessages;
      if (message != state->request) return -1;
      link.txStream([this](auto &tx) {
	tx << state->response;
	tx.flush();
      }, Zws::Opcode::Text);
    }
    return 1;
  }

  template <typename Link>
  void disconnected(Link &, bool) { state->down(); }
  template <typename Link>
  void closed(Link &, uint16_t, ZuBSpan) { ++state->closes; }
  template <typename Link>
  void error(Link &, Zws::Failure::T failure) {
    if (failure == Zws::Failure::Handshake) {
      ++state->handshakeErrors;
      return;
    }
    state->lastError = failure;
    ++state->errors;
  }
};

struct ClientApp {
  State		*state;
  ZtString<>	message;

  template <typename Link>
  void connected(Link &link, Zhttp::ConnectedInfo info) {
    if (link.protocol() != "chat" || !validInfo<Link>(info)) {
      state->fail();
      return;
    }
    ++state->connected;
    link.txStream([this](auto &tx) {
      tx << state->request;
      tx.flush();
    }, Zws::Opcode::Text);
  }

  template <typename Link, typename Rx>
  int process(Link &link, Rx &rx) {
    auto events = rx.events();
    if (events & Zi::RxEvent::Start()) {
      if (link.messageOpcode() != Zws::Opcode::Text) return -1;
      message.length(0);
    }
    while (rx.input()) {
      int64_t n = rx.consume(
	[](ZuBSpan span) -> int64_t { return span.length(); },
	[this](ZuBSpan span) { message << span; });
      if (n <= 0) break;
    }
    events |= rx.events();
    if (events & Zi::RxEvent::Error()) return -1;
    if (events & Zi::RxEvent::Final()) {
      ++state->cliMessages;
      if (message != state->response) return -1;
      link.close();
    }
    return 1;
  }

  template <typename Link>
  void disconnected(Link &link, bool) {
    state->down();
    if (state->cliMessages.load_() != 1 ||
	state->reconnects.xchAdd(1))
      return;
    if constexpr (Link::Multiplexed)
      link.connect("127.0.0.1", state->port);
    else
      link.connect();
  }
  template <typename Link>
  void closed(Link &, uint16_t, ZuBSpan) { ++state->closes; }
  template <typename Link>
  void connectFailed(Link &, bool) { state->fail(); }
  template <typename Link>
  void error(Link &, Zws::Failure::T failure) {
    state->lastError = failure;
    ++state->errors;
  }
};

struct RejectState {
  ZmSemaphore	done;
  ZtString<>	response;
  bool		failed = false;
};

struct RejectClient : public Ztcp::Client<RejectClient> {
  struct Link;

  RejectClient(RejectState *state_) : state{state_} { }

  unsigned reconnFreq() const { return 0; }

  RejectState	*state;
};

struct RejectClient::Link : public Ztcp::CliLink<RejectClient, Link> {
  using Base = Ztcp::CliLink<RejectClient, Link>;

  Link(RejectClient *app) : Base{app} { }

  void connected(Ztcp::Connected) {
    auto tx = txStream();
    tx << "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
    tx.flush();
  }
  int process(Ztcp::RxStream &rx) {
    while (!rx.empty()) {
      int64_t n = rx.consume(
	[](ZuBSpan span) -> int64_t { return span.length(); },
	[this](ZuBSpan span) { app()->state->response << span; });
      if (n <= 0) break;
    }
    return 1;
  }
  void disconnected(bool) {
    if (app()->state->response.find("HTTP/1.1 400 Bad Request\r\n") != 0)
      app()->state->failed = true;
    app()->state->done.post();
  }
  void connectFailed(bool) {
    app()->state->failed = true;
    app()->state->done.post();
  }
};

template <typename Profile> struct ProfileConfig;
template <> struct ProfileConfig<Zhttp::H1TCP> {
  static auto client(const TempDir &) { return Zhttp::TCPConfig{}; }
  static auto server(const TempDir &) { return Zhttp::TCPConfig{}; }
};
template <> struct ProfileConfig<Zhttp::H1TLS> {
  static auto client(const TempDir &temp) {
    return Zhttp::TLSConfig{}.caPath(temp.certPath.cspan());
  }
  static auto server(const TempDir &temp) {
    return Zhttp::TLSConfig{}
      .certPath(temp.certPath.cspan()).keyPath(temp.keyPath.cspan());
  }
};
#ifdef ZWS_H2_TEST
template <> struct ProfileConfig<Zhttp::H2TLS> {
  static auto client(const TempDir &temp) {
    return Zhttp::H2Config{}.caPath(temp.certPath.cspan())
      .maxConcurrentStreams(4).initialWindowSize(256);
  }
  static auto server(const TempDir &temp) {
    return Zhttp::H2Config{}
      .certPath(temp.certPath.cspan()).keyPath(temp.keyPath.cspan())
      .maxConcurrentStreams(4).initialWindowSize(256);
  }
};
#endif
#ifdef ZWS_H3_TEST
template <> struct ProfileConfig<Zhttp::H3QUIC> {
  static auto client(const TempDir &temp) {
    return Zhttp::QUICConfig{}.caPath(temp.certPath.cspan())
      .maxStreamsDuplex(4).maxStreamData(256);
  }
  static auto server(const TempDir &temp) {
    return Zhttp::QUICConfig{}
      .certPath(temp.certPath.cspan()).keyPath(temp.keyPath.cspan())
      .maxStreamsDuplex(4).maxStreamData(256);
  }
};
#endif
template <typename Profile>
void run(const TempDir &temp)
{
  ZuTestScope(run);

  State state;
  state.request.length(4096);
  state.response.length(4096);
  const unsigned payloadLen = state.request.length();
  char *request = state.request.data();
  char *response = state.response.data();
  for (unsigned i = 0; i < payloadLen; ++i) {
    request[i] = 'a' + (i % 26);
    response[i] = 'A' + (i % 26);
  }
  state.port = loopbackPort();
  ZuCHECK(state.port);
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start());
  if (!mx.running()) return;

  ServerApp serverApp{&state};
  ClientApp clientApp{&state};
  Zws::Server<ServerApp, Profile> server{
    &serverApp, ZiIP{"127.0.0.1"}, state.port};
  Zws::Client<ClientApp, Profile> client{&clientApp};
  Zhttp::EngineConfig engine{&mx, "3", "4"};
  bool serverInit =
    server.init(engine, ProfileConfig<Profile>::server(temp));
  bool clientInit =
    client.init(engine, ProfileConfig<Profile>::client(temp));
  ZuCHECK(serverInit && clientInit);
  if (!serverInit || !clientInit) {
    if (clientInit) client.final();
    if (serverInit) server.final();
    mx.stop();
    return;
  }

  bool serverStart = server.start();
  bool clientStart = client.start();
  ZuCHECK(serverStart && clientStart);
  if (!serverStart || !clientStart) {
    if (clientStart) client.stop();
    if (serverStart) server.stop();
    client.final();
    server.final();
    mx.stop();
    return;
  }

  bool listening = state.listening.timedwait(Zm::now(10)) == 0;
  ZuCHECK(listening);
  using Link = typename Zws::Client<ClientApp, Profile>::Link;
  ZmRef<Link> link;
  if (listening) {
    Zws::URI uri;
    ZtString<> text;
    text <<
      (ZuIsSame<Profile, Zhttp::H1TCP>{} ? "ws" : "wss") <<
      "://127.0.0.1:" << state.port << "/stream";
    ZuCHECK(Zws::URI::parse(uri, text).ok());
    link = new Link{&client, uri, "chat"};
    if constexpr (Zhttp::ProfileTraits<Profile>::Multiplexed)
      link->connect(uri.host, uri.port);
    else
      link->connect();
    ZuCHECK(state.done.timedwait(Zm::now(10)) == 0);
  }

  if constexpr (ZuIsSame<Profile, Zhttp::H1TCP>{}) {
    RejectState rejectState;
    RejectClient rejectClient{&rejectState};
    bool rejectInit =
      rejectClient.init(Ztcp::ClientParams{&mx, "3", "4"});
    ZuCHECK(rejectInit);
    if (rejectInit) {
      ZmRef<RejectClient::Link> reject =
	new RejectClient::Link{&rejectClient};
      reject->connect("127.0.0.1", state.port);
      ZuCHECK(rejectState.done.timedwait(Zm::now(10)) == 0);
      ZuCHECK(!rejectState.failed,
	"rejected H1 opening handshake did not drain its 400 response");
      reject = nullptr;
      rejectClient.final();
    }
  }

  server.stopAccepting();
  ZuCHECK(client.stop() && server.stop());
  link = nullptr;
  client.final();
  server.final();
  mx.stop();

  if (!listening) return;
  ZuCHECK(!state.errors.load_(),
    "errors=", state.errors.load_(),
    " last=", Zws::Failure{}.name(state.lastError.load_()));
  ZuCHECK(state.connected.load_() == 4);
  ZuCHECK(state.srvMessages.load_() == 2);
  ZuCHECK(state.cliMessages.load_() == 2);
  ZuCHECK(state.closes.load_() == 4);
  ZuCHECK(state.reconnects.load_() == 1);
  if constexpr (ZuIsSame<Profile, Zhttp::H1TCP>{}) {
    ZuCHECK(state.handshakeErrors.load_() == 1);
    ZuCHECK(state.disconnected.load_() == 5);
  } else {
    ZuCHECK(!state.handshakeErrors.load_());
    ZuCHECK(state.disconnected.load_() == 4);
  }
}

} // namespace ZwsH1EngineTest_

int main(int argc, char **argv)
{
  using namespace ZwsH1EngineTest_;

  parse(argc, argv);
  ZiLog::init("ZwsH1EngineTest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  TempDir temp;
  ZuTestMain();
  ZuCHECK(temp.init());
  if (temp.certPath && temp.keyPath) {
#ifdef ZWS_H2_TEST
    ZuTestCall(run<Zhttp::H2TLS>, temp);
#elif defined(ZWS_H3_TEST)
    ZuTestCall(run<Zhttp::H3QUIC>, temp);
#else
    ZuTestCall(run<Zhttp::H1TCP>, temp);
    ZuTestCall(run<Zhttp::H1TLS>, temp);
#endif
  }

  ZiLog::stop();
  return 0;
}
