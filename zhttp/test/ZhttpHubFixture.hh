//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Common HTTP hub/link integration fixture

#ifndef ZhttpHubFixture_HH
#define ZhttpHubFixture_HH

#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpServer.hh>

#include "ZhttpTestUtil.hh"

using namespace ZuTestUtil;

namespace ZhttpH1HubTest_ {

inline unsigned testPort()
{
#ifdef ZHTTP_H3_HUB_TEST
  static unsigned port = ZhttpTestPort::H3Hub;
#else
  static unsigned port = ZhttpTestPort::H1Hub;
#endif
  return Zhttp::Test::loopbackPort(port++);
}

using namespace Zhttp::Test;

constexpr ZuString Ping{"ping"};
constexpr ZuString Pong{"pong"};

struct State {
  LifeTrace		trace;
  ZmSemaphore		listening;
  ZmSemaphore		messageDone;
  ZmSemaphore		done;
  ZmAtomic<unsigned>	closed = 0;
  ZmAtomic<unsigned>	completed = 0;
  ZmAtomic<unsigned>	connectFailures = 0;
  ZmAtomic<unsigned>	errors = 0;
  unsigned		rounds = 2;
  unsigned		links = 1;
  uint16_t		port = 0;
  bool			keepAlive = false;

  void fail() {
    errors = 1;
    done.post();
  }
  void close(int event) {
    trace.push(event);
    if (closed.xchAdd(1) + 1 == links * 2) done.post();
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

template <typename Rx>
bool consumeStream(Rx &rx, ZuCSpan expected, unsigned &offset)
{
  while (rx) {
    int64_t n = rx.consume(
      [&expected, &offset](ZuSpan<uint8_t> span) -> int64_t {
	unsigned remain = expected.length() - offset;
	return span.length() < remain ? span.length() : remain;
      },
      [&expected, &offset](ZuSpan<uint8_t> span) {
	if (::memcmp(span.data(), expected.data() + offset, span.length()))
	  offset = expected.length() + 1;
	else
	  offset += span.length();
      });
    if (n < 0 || offset > expected.length()) return false;
    if (!n) break;
  }
  return true;
}

template <typename Link>
void send(Link &link, State &state, int ready, int sent, ZuCSpan data)
{
  auto tx = link.txStream();
  state.trace.push(ready);
  state.trace.push(sent);
  tx << data << Zi::flush();
}

template <typename Profile> struct Server;
template <typename Profile> struct ServerLink;

struct ServerSession {
  template <typename Link>
  void connected(Link &link) {
    auto &state = *link.app()->state;
    state.trace.push(LifeEvt::SrvConnected);
    auto tx = link.txStream();
    (void)tx;
    state.trace.push(LifeEvt::SrvTxReady);
  }
  template <typename Link>
  void disconnected(Link &, bool) { }
  template <typename Link, typename Rx>
  int process(Link &link, Rx &rx) {
    if (!consume(rx, Ping)) return 0;
    auto &state = *link.app()->state;
    state.trace.push(LifeEvt::SrvProcess);
    auto tx = link.txStream();
    state.trace.push(LifeEvt::SrvSend);
    tx << Pong << Zi::flush();
    return 1;
  }
};

template <typename Profile>
struct Server :
  public Zhttp::ProtocolServer<Server<Profile>, Profile> {
  using Link = ServerLink<Profile>;

  State	*state = nullptr;

  Server(State *state_) : state{state_} { }

  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  unsigned localPort() const { return state->port; }
  void listening(const ZiListenInfo &) { state->listening.post(); }
  void listening() { state->listening.post(); }
  void listenFailed(bool) { state->fail(); }
  void disconnected(Link &, bool) {
    state->close(LifeEvt::SrvDisconnected);
  }
};

template <typename Profile>
struct ServerLink :
  public Zhttp::ServerLink<
    Server<Profile>, ServerLink<Profile>, Profile, ServerSession> {
  using Base = Zhttp::ServerLink<
    Server<Profile>, ServerLink<Profile>, Profile, ServerSession>;
  using Base::Base;
};

template <typename Profile>
struct Client :
  public Zhttp::ClientHub<Client<Profile>, Profile> {
  struct Link :
    public Zhttp::ClientLink<Client, Link, Profile> {
    using Base = Zhttp::ClientLink<Client, Link, Profile>;
    using Base::Base;

    unsigned responses = 0;
    unsigned id = 0;
  };

  State	*state = nullptr;

  Client(State *state_) : state{state_} { }

  void connected(Link &link, Zhttp::ConnectedInfo) {
    state->trace.push(LifeEvt::CliConnected);
    send(link, *state, LifeEvt::CliTxReady, LifeEvt::CliSend, Ping);
  }
  void disconnected(Link &, bool) {
    state->close(LifeEvt::CliDisconnected);
  }
  void connectFailed(Link &, bool) { state->fail(); }
  template <typename Rx>
  int process(Link &link, Rx &rx) {
    if (!consume(rx, Pong)) return 0;
    state->trace.push(LifeEvt::CliProcess);
    if (++link.responses < state->rounds)
      send(link, *state, LifeEvt::CliTxReady, LifeEvt::CliSend, Ping);
    else {
      if (state->completed.xchAdd(1) + 1 == state->links)
	state->messageDone.post();
      if (!state->keepAlive) link.disconnect();
    }
    return 1;
  }
};

struct FailClient :
  public Zhttp::ClientHub<FailClient, Zhttp::H1TLS> {
  struct Link :
    public Zhttp::ClientLink<FailClient, Link, Zhttp::H1TLS> {
    using Base = Zhttp::ClientLink<FailClient, Link, Zhttp::H1TLS>;
    using Base::Base;
  };

  State	*state = nullptr;

  FailClient(State *state_) : state{state_} { }

  void connected(Link &, Zhttp::ConnectedInfo) {
    state->trace.push(LifeEvt::CliConnected);
    state->fail();
  }
  void disconnected(Link &, bool) { }
  void connectFailed(Link &, bool) {
    ++state->connectFailures;
    state->done.post();
  }
  template <typename Rx>
  int process(Link &, Rx &) { state->fail(); return -1; }
};

struct UpgradeState {
  ZmSemaphore		listening;
  ZmSemaphore		done;
  ZmAtomic<unsigned>	closed = 0;
  ZmAtomic<unsigned>	errors = 0;
  ZmAtomic<unsigned>	srvStarts = 0;
  ZmAtomic<unsigned>	cliStarts = 0;
  ZmAtomic<unsigned>	srvFinals = 0;
  uint16_t		port = 0;

  void fail() {
    errors = 1;
    done.post();
  }
  void close() {
    if (closed.xchAdd(1) + 1 == 2) done.post();
  }
};

struct UpgradeReq :
  public Zhttp::Parser,
  public Zhttp::H1::Parser<UpgradeReq, true> {
  using Base = Zhttp::H1::Parser<UpgradeReq, true>;
  using State = Zhttp::H1::ParserState;
  using Headers = ZuTypeList<>;
  using Zhttp::Parser::header;

  bool operation(
    Zhttp::Method::T method_, Zhttp::Target &target) {
    method = method_;
    path = target.path == "/stream";
    return true;
  }
  void header(
      Zhttp::FieldSection::T, ZuBSpan key, ZuSpan<uint8_t> value) {
    if (key == "upgrade" && value == "opaque") upgrade = true;
    if (key == "connection" && value == "Upgrade") connection = true;
  }
  void complete(State::T) { }

  Zhttp::Method::T	method = -1;
  bool			path = false;
  bool			upgrade = false;
  bool			connection = false;
};

struct UpgradeResp :
  public Zhttp::Parser,
  public Zhttp::H1::Parser<UpgradeResp, false> {
  using Base = Zhttp::H1::Parser<UpgradeResp, false>;
  using State = Zhttp::H1::ParserState;
  using Headers = ZuTypeList<>;
  using Zhttp::Parser::header;

  bool enable1xx() const { return true; }
  void status(unsigned v) { statusCode = v; }
  void header(
      Zhttp::FieldSection::T, ZuBSpan key, ZuSpan<uint8_t> value) {
    if (key == "upgrade" && value == "opaque") upgrade = true;
    if (key == "connection" && value == "Upgrade") connection = true;
  }
  void complete(State::T) { }

  unsigned	statusCode = 0;
  bool		upgrade = false;
  bool		connection = false;
};

template <typename Profile> struct UpgradeServer;
template <typename Profile> struct UpgradeSrvLink;

struct UpgradeSession {
  UpgradeState	*state = nullptr;
  UpgradeReq	parser;
  unsigned	offset = 0;
  bool		replied = false;
  bool		started = false;

  template <typename Link>
  void connected(Link &link) {
    state = link.app()->state;
    if (!link.streamEnable()) state->fail();
  }
  template <typename Link>
  void disconnected(Link &, bool) { }
  template <typename Link, typename Rx>
  int process(Link &link, Rx &rx) {
    auto state = parser.process(rx);
    if (state != UpgradeReq::State::Complete)
      return state == UpgradeReq::State::Error ? -1 : 0;
    if (parser.method != Zhttp::Method::GET || !parser.path ||
	!parser.upgrade || !parser.connection) {
      link.app()->state->fail();
      return -1;
    }
    auto tx = link.txStream();
    tx << "HTTP/1.1 101 Switching Protocols\r\n"
	  "Upgrade: opaque\r\n"
	  "Connection: Upgrade\r\n\r\n";
    tx.flush();
    if (!link.streamAccept()) {
      link.app()->state->fail();
      return -1;
    }
    return 1;
  }
  template <typename Link, typename Rx>
  int process(Zhttp::Stream<Link> stream, Rx &rx) {
    if (!started) { started = true; ++state->srvStarts; }
    if (!consumeStream(rx, Ping, offset)) {
      state->fail();
      return -1;
    }
    if (!replied && offset == Ping.length()) {
      replied = true;
      stream.txStream([](auto &tx) {
	tx << Pong;
	tx.flush();
      });
    }
    return 1;
  }
  template <typename Link>
  void peerEnd(Zhttp::Stream<Link>) { ++state->srvFinals; }
  template <typename Link>
  void error(Zhttp::Stream<Link>) { state->fail(); }
};

template <typename Profile>
struct UpgradeServer :
  public Zhttp::ProtocolServer<UpgradeServer<Profile>, Profile> {
  using Link = UpgradeSrvLink<Profile>;

  UpgradeState	*state = nullptr;

  UpgradeServer(UpgradeState *state_) : state{state_} { }

  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  unsigned localPort() const { return state->port; }
  void listening(const ZiListenInfo &) { state->listening.post(); }
  void listening() { state->listening.post(); }
  void listenFailed(bool) { state->fail(); }
  void disconnected(Link &, bool) { state->close(); }
};

template <typename Profile>
struct UpgradeSrvLink :
  public Zhttp::ServerLink<
    UpgradeServer<Profile>, UpgradeSrvLink<Profile>,
    Profile, UpgradeSession> {
  using Base = Zhttp::ServerLink<
    UpgradeServer<Profile>, UpgradeSrvLink<Profile>,
    Profile, UpgradeSession>;
  using Base::Base;
};

template <typename Profile>
struct UpgradeClient :
  public Zhttp::ClientHub<UpgradeClient<Profile>, Profile> {
  struct Link :
    public Zhttp::ClientLink<UpgradeClient, Link, Profile> {
    using Base = Zhttp::ClientLink<UpgradeClient, Link, Profile>;
    using Base::Base;

    UpgradeResp	parser;
    unsigned	id = 0;
  };

  UpgradeState	*state = nullptr;
  unsigned	offset = 0;
  bool		started = false;

  UpgradeClient(UpgradeState *state_) : state{state_} { }

  void connected(Link &link, Zhttp::ConnectedInfo) {
    offset = 0;
    started = false;
    if (!link.streamEnable()) {
      state->fail();
      return;
    }
    auto tx = link.txStream();
    tx << "GET /stream HTTP/1.1\r\n"
	  "Host: localhost\r\n"
	  "Upgrade: opaque\r\n"
	  "Connection: Upgrade\r\n\r\n"
	  "ping";
    tx.flush();
  }
  void disconnected(Link &, bool) { state->close(); }
  void connectFailed(Link &, bool) { state->fail(); }
  template <typename Rx>
  int process(Link &link, Rx &rx) {
    auto parserState = link.parser.process(rx);
    if (parserState != UpgradeResp::State::Complete)
      return parserState == UpgradeResp::State::Error ? -1 : 0;
    if (link.parser.statusCode != 101 ||
	!link.parser.upgrade || !link.parser.connection ||
	!link.streamAccept()) {
      state->fail();
      return -1;
    }
    return 1;
  }
  template <typename Link, typename Rx>
  int process(Zhttp::Stream<Link> stream, Rx &rx) {
    if (!started) { started = true; ++state->cliStarts; }
    if (!consumeStream(rx, Pong, offset)) {
      state->fail();
      return -1;
    }
    if (offset == Pong.length()) stream.end();
    return 1;
  }
  template <typename Link>
  void peerEnd(Zhttp::Stream<Link>) { }
  template <typename Link>
  void error(Zhttp::Stream<Link>) { state->fail(); }
};

template <typename Profile> struct Config;
template <> struct Config<Zhttp::H1TCP> {
  static auto client(const TempDir &) { return Zhttp::TCPConfig{}; }
  static auto server(const TempDir &) { return Zhttp::TCPConfig{}; }
};
template <> struct Config<Zhttp::H1TLS> {
  static auto client(const TempDir &temp) {
    return Zhttp::TLSConfig{}.caPath(temp.certPath.cspan());
  }
  static auto server(const TempDir &temp) {
    return Zhttp::TLSConfig{}
      .certPath(temp.certPath.cspan()).keyPath(temp.keyPath.cspan());
  }
};
template <> struct Config<Zhttp::H3QUIC> {
  static auto client(const TempDir &temp) {
    return Zhttp::QUICConfig{}.caPath(temp.certPath.cspan());
  }
  static auto server(const TempDir &temp) {
    return Zhttp::QUICConfig{}
      .certPath(temp.certPath.cspan()).keyPath(temp.keyPath.cspan());
  }
};

template <typename Profile>
void runUpgrade(const TempDir &temp)
{
  ZuTestScope(runUpgrade);

  UpgradeState state;
  state.port = testPort();
  ZuCHECK(state.port, "Upgrade loopback port allocation failed");
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "Upgrade multiplexer start failed");
  if (!mx.running()) return;

  Zhttp::HubConfig hub{&mx, "3", "4"};
  UpgradeServer<Profile> server{&state};
  UpgradeClient<Profile> client{&state};
  bool serverInit = server.init(hub, Config<Profile>::server(temp));
  bool clientInit = client.init(hub, Config<Profile>::client(temp));
  ZuCHECK(serverInit && clientInit, "Upgrade hub initialization failed");
  if (!serverInit || !clientInit) {
    if (clientInit) client.final();
    if (serverInit) server.final();
    mx.stop();
    return;
  }

  bool serverStart = server.start();
  bool clientStart = client.start();
  ZuCHECK(serverStart && clientStart, "Upgrade hub start failed");
  if (!serverStart || !clientStart) {
    if (clientStart) client.stop();
    if (serverStart) server.stop();
    client.final();
    server.final();
    mx.stop();
    return;
  }

  bool listening = state.listening.timedwait(Zm::now(10)) == 0;
  ZuCHECK(listening, "Upgrade listen timed out");
  using Link = typename UpgradeClient<Profile>::Link;
  ZmRef<Link> link;
  if (listening) {
    link = new Link{&client};
    link->connect("127.0.0.1", state.port);
    ZuCHECK(state.done.timedwait(Zm::now(10)) == 0,
      "Upgrade stream lifecycle timed out");
  }

  server.stopAccepting();
  ZmSemaphore stopDone;
  ZmAtomic<unsigned> stopErrors = 0;
  client.stop([&stopDone, &stopErrors](bool ok) {
    if (!ok) ++stopErrors;
    stopDone.post();
  });
  server.stop([&stopDone, &stopErrors](bool ok) {
    if (!ok) ++stopErrors;
    stopDone.post();
  });
  bool stopped =
    stopDone.timedwait(Zm::now(10)) == 0 &&
    stopDone.timedwait(Zm::now(10)) == 0 &&
    !stopErrors.load_();
  ZuCHECK(stopped, "Upgrade hub stop failed");
  link = nullptr;
  client.final();
  server.final();
  mx.stop();

  if (!listening) return;
  ZuCHECK(!state.errors.load_(), "Upgrade stream application error");
  ZuCHECK(state.closed.load_() == 2,
    "Upgrade stream disconnect count mismatch");
  ZuCHECK(state.cliStarts.load_() == 1,
    "Upgrade client stream start event mismatch");
  ZuCHECK(state.srvStarts.load_() == 1,
    "Upgrade server stream start event mismatch");
  ZuCHECK(state.srvFinals.load_() == 1,
    "Upgrade server stream terminal event mismatch");
}

template <typename Profile>
void run(
  const TempDir &temp, unsigned rounds = 2, unsigned links = 1,
  bool asyncStop = false)
{
  ZuTestScope(run);

  State state;
  state.rounds = rounds;
  state.links = links;
  state.port = testPort();
  ZuCHECK(state.port, "H1 loopback port allocation failed");
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "H1 multiplexer start failed");
  if (!mx.running()) return;

  Zhttp::HubConfig hub{&mx, "3", "4"};
  Server<Profile> server{&state};
  Client<Profile> client{&state};
  bool serverInit = server.init(hub, Config<Profile>::server(temp));
  bool clientInit = client.init(hub, Config<Profile>::client(temp));
  ZuCHECK(serverInit && clientInit, "H1 hub initialization failed");
  if (!serverInit || !clientInit) {
    if (clientInit) client.final();
    if (serverInit) server.final();
    mx.stop();
    return;
  }
  state.trace.push(LifeEvt::Init);

  bool serverStart = server.start();
  bool clientStart = client.start();
  ZuCHECK(serverStart && clientStart, "H1 hub start failed");
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
  ZuCHECK(listening, "H1 listen timed out");
  using Link = typename Client<Profile>::Link;
  using Links =
    ZtArray<ZmRef<Link>, ZtArrayHeapID<"Zhttp.Test.HubLinks">>;
  Links links_;
  if (listening) {
    for (unsigned i = 0; i < state.links; ++i) {
      ZmRef<Link> link = new Link{&client};
      state.trace.push(LifeEvt::CliConnect);
      link->connect("127.0.0.1", state.port);
      links_.push(ZuMv(link));
    }
    auto &complete =
      Client<Profile>::Multiplexed ? state.messageDone : state.done;
    ZuCHECK(complete.timedwait(Zm::now(10)) == 0,
      "H1 message lifecycle timed out");
  }

  server.stopAccepting();
  bool stopped = false;
  if constexpr (ZuIsSame<Profile, Zhttp::H3QUIC>{}) {
    if (asyncStop) {
      ZmSemaphore stopDone;
      ZmAtomic<unsigned> stopErrors = 0;
      client.stop([&stopDone, &stopErrors](bool ok) {
	if (!ok) ++stopErrors;
	stopDone.post();
      });
      server.stop([&stopDone, &stopErrors](bool ok) {
	if (!ok) ++stopErrors;
	stopDone.post();
      });
      stopped =
	stopDone.timedwait(Zm::now(10)) == 0 &&
	stopDone.timedwait(Zm::now(10)) == 0 &&
	!stopErrors.load_();
    } else
      stopped = client.stop() && server.stop();
  } else
    stopped = client.stop() && server.stop();
  ZuCHECK(stopped, "H1 hub stop failed");
  if constexpr (Client<Profile>::Multiplexed) {
    bool closed = state.done.timedwait(Zm::now(10)) == 0;
    ZuCHECK(closed, "H3 disconnect lifecycle timed out");
  }
  state.trace.push(LifeEvt::Stop);
  links_.length(0);
  client.final();
  server.final();
  state.trace.push(LifeEvt::Final);
  mx.stop();

  if (!listening) return;
  ZuCHECK(!state.errors.load_(), "H1 application error");
  ZuCHECK(state.trace.count(LifeEvt::CliConnected) == state.links &&
      state.trace.count(LifeEvt::SrvConnected) == state.links &&
      state.trace.count(LifeEvt::CliProcess) == state.rounds * state.links &&
      state.trace.count(LifeEvt::SrvProcess) == state.rounds * state.links &&
      state.trace.count(LifeEvt::CliDisconnected) == state.links &&
      state.trace.count(LifeEvt::SrvDisconnected) == state.links,
    "H1 application event count mismatch");
  ZuCHECK(state.trace.before(LifeEvt::CliConnect, LifeEvt::CliConnected) &&
      state.trace.before(LifeEvt::CliConnected, LifeEvt::CliTxReady) &&
      state.trace.before(LifeEvt::SrvConnected, LifeEvt::SrvTxReady) &&
      state.trace.before(LifeEvt::SrvProcess, LifeEvt::SrvSend) &&
      state.trace.before(LifeEvt::SrvSend, LifeEvt::CliProcess),
    "H1 application event order mismatch");
}

template <typename Profile>
void runServerStop(const TempDir &temp)
{
  ZuTestScope(runServerStop);

  State state;
  state.rounds = 1;
  state.keepAlive = true;
  state.port = testPort();
  ZuCHECK(state.port, "active-stop loopback port allocation failed");
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "active-stop multiplexer start failed");
  if (!mx.running()) return;

  Zhttp::HubConfig hub{&mx, "3", "4"};
  Server<Profile> server{&state};
  Client<Profile> client{&state};
  bool serverInit = server.init(hub, Config<Profile>::server(temp));
  bool clientInit = client.init(hub, Config<Profile>::client(temp));
  ZuCHECK(serverInit && clientInit,
    "active-stop hub initialization failed");
  if (!serverInit || !clientInit) {
    if (clientInit) client.final();
    if (serverInit) server.final();
    mx.stop();
    return;
  }

  bool serverStart = server.start();
  bool clientStart = client.start();
  ZuCHECK(serverStart && clientStart, "active-stop hub start failed");
  if (!serverStart || !clientStart) {
    if (clientStart) client.stop();
    if (serverStart) server.stop();
    client.final();
    server.final();
    mx.stop();
    return;
  }

  bool listening = state.listening.timedwait(Zm::now(10)) == 0;
  ZuCHECK(listening, "active-stop listen timed out");
  using Link = typename Client<Profile>::Link;
  ZmRef<Link> link;
  bool active = false;
  if (listening) {
    link = new Link{&client};
    link->connect("127.0.0.1", state.port);
    active = state.messageDone.timedwait(Zm::now(10)) == 0;
    ZuCHECK(active, "active-stop request timed out");
  }

  ZmSemaphore stopDone;
  ZmAtomic<unsigned> stopErrors = 0;
  ZmAtomic<unsigned> stopReturned = 0;
  ZmAtomic<unsigned> stopWasAsync = 1;
  server.stop([
    &state, &stopDone, &stopErrors, &stopReturned, &stopWasAsync
  ](bool ok) {
    if (!stopReturned.load_()) stopWasAsync = false;
    if (!ok) ++stopErrors;
    state.trace.push(LifeEvt::Stop);
    stopDone.post();
  });
  stopReturned = true;
  bool stopped = stopDone.timedwait(Zm::now(10)) == 0;
  ZuCHECK(stopped && !stopErrors.load_(),
    "active-stop server completion timed out");
  ZuCHECK(stopWasAsync.load_(),
    "active-stop server completion was synchronous");
  if (!stopped) {
    client.stop();
    (void)stopDone.timedwait(Zm::now(10));
  } else
    client.stop();
  bool disconnected = state.done.timedwait(Zm::now(10)) == 0;
  ZuCHECK(disconnected, "active-stop link completion timed out");

  link = nullptr;
  client.final();
  server.final();
  mx.stop();

  if (!listening || !active) return;
  ZuCHECK(!state.errors.load_(), "active-stop application error");
  unsigned cliDisconnected = state.trace.count(LifeEvt::CliDisconnected);
  ZuCHECK(cliDisconnected, "active-stop client disconnect callback missing");
  ZuCHECK(cliDisconnected == 1,
    "active-stop client disconnect callback duplicated");
  ZuCHECK(state.trace.count(LifeEvt::SrvDisconnected) == 1,
    "active-stop server disconnect callback count mismatch");
  ZuCHECK(state.trace.before(LifeEvt::SrvDisconnected, LifeEvt::Stop),
    "server stop completed before its active link drained");
}

void testTLSFailure(const TempDir &cert, const TempDir &otherCA)
{
  ZuTestScope(testTLSFailure);

  State state;
  state.port = testPort();
  ZuCHECK(state.port, "TLS failure loopback port allocation failed");
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "TLS failure multiplexer start failed");
  if (!mx.running()) return;

  Zhttp::HubConfig hub{&mx, "3", "4"};
  Server<Zhttp::H1TLS> server{&state};
  FailClient client{&state};
  bool serverInit = server.init(hub, Config<Zhttp::H1TLS>::server(cert));
  bool clientInit = client.init(
    hub, Zhttp::TLSConfig{}.caPath(otherCA.certPath.cspan()));
  ZuCHECK(serverInit && clientInit, "TLS failure hub initialization failed");
  if (!serverInit || !clientInit) {
    if (clientInit) client.final();
    if (serverInit) server.final();
    mx.stop();
    return;
  }

  bool serverStart = server.start();
  bool clientStart = client.start();
  ZuCHECK(serverStart && clientStart, "TLS failure hub start failed");
  if (!serverStart || !clientStart) {
    if (clientStart) client.stop();
    if (serverStart) server.stop();
    client.final();
    server.final();
    mx.stop();
    return;
  }

  bool listening = state.listening.timedwait(Zm::now(10)) == 0;
  ZuCHECK(listening, "TLS failure listen timed out");
  ZmRef<FailClient::Link> link;
  if (listening) {
    link = new FailClient::Link{&client};
    link->connect("127.0.0.1", state.port);
    ZuCHECK(state.done.timedwait(Zm::now(10)) == 0,
      "TLS handshake failure callback timed out");
  }

  server.stopAccepting();
  client.stop();
  server.stop();
  link = nullptr;
  client.final();
  server.final();
  mx.stop();

  if (!listening) return;
  ZuCHECK(state.connectFailures.load_() == 1,
    "TLS handshake failure callback count mismatch");
  ZuCHECK(state.trace.count(LifeEvt::CliConnected) == 0,
    "TLS failure emitted connected callback");
}

} // namespace ZhttpH1HubTest_

#endif /* ZhttpHubFixture_HH */
