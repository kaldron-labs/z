//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Common HTTP engine/link integration fixture

#ifndef ZhttpEngineFixture_HH
#define ZhttpEngineFixture_HH

#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/ZhttpH3Engine.hh>

#include "ZhttpTestUtil.hh"

using namespace ZuTestUtil;

namespace ZhttpH1EngineTest_ {

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
  bool ok = false;
  rx.consume(
    [expected](ZuBSpan span) -> int64_t {
      if (span.length() < expected.length()) return 0;
      return expected.length();
    },
    [&ok, expected](ZuBSpan span) {
      ok = span.length() == expected.length() &&
	!memcmp(span.data(), expected.data(), expected.length());
    });
  return ok;
}

template <typename Link>
void send(Link &link, State &state, int ready, int sent, ZuCSpan data)
{
  auto tx = link.txStream();
  state.trace.push(ready);
  state.trace.push(sent);
  tx << data << Zi::flush();
}

template <typename Protocol> struct Server;
template <typename Protocol> struct ServerLink;

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

template <typename Protocol>
struct Server :
  public Zhttp::Server<Server<Protocol>, Protocol> {
  using Link = ServerLink<Protocol>;

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

template <typename Protocol>
struct ServerLink :
  public Zhttp::ServerLink<
    Server<Protocol>, ServerLink<Protocol>, Protocol, ServerSession> {
  using Base = Zhttp::ServerLink<
    Server<Protocol>, ServerLink<Protocol>, Protocol, ServerSession>;
  using Base::Base;
};

template <typename Protocol>
struct Client :
  public Zhttp::Client<Client<Protocol>, Protocol> {
  struct Link :
    public Zhttp::ClientLink<Client, Link, Protocol> {
    using Base = Zhttp::ClientLink<Client, Link, Protocol>;
    using Base::Base;

    unsigned responses = 0;
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
  public Zhttp::Client<FailClient, Zhttp::TLS> {
  struct Link :
    public Zhttp::ClientLink<FailClient, Link, Zhttp::TLS> {
    using Base = Zhttp::ClientLink<FailClient, Link, Zhttp::TLS>;
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

template <typename Protocol> struct Config;
template <> struct Config<Zhttp::TCP> {
  static auto client(const TempDir &) { return Zhttp::TCPConfig{}; }
  static auto server(const TempDir &) { return Zhttp::TCPConfig{}; }
};
template <> struct Config<Zhttp::TLS> {
  static auto client(const TempDir &temp) {
    return Zhttp::TLSConfig{}.caPath(temp.certPath.cspan());
  }
  static auto server(const TempDir &temp) {
    return Zhttp::TLSConfig{}
      .certPath(temp.certPath.cspan()).keyPath(temp.keyPath.cspan());
  }
};
template <> struct Config<Zhttp::QUIC> {
  static auto client(const TempDir &temp) {
    return Zhttp::QUICConfig{}.caPath(temp.certPath.cspan());
  }
  static auto server(const TempDir &temp) {
    return Zhttp::QUICConfig{}
      .certPath(temp.certPath.cspan()).keyPath(temp.keyPath.cspan());
  }
};

template <typename Protocol>
void run(
  const TempDir &temp, unsigned rounds = 2, unsigned links = 1,
  bool asyncStop = false)
{
  ZuTestScope(run);

  State state;
  state.rounds = rounds;
  state.links = links;
  state.port = loopbackPort();
  ZuCHECK(state.port, "H1 loopback port allocation failed");
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "H1 multiplexer start failed");
  if (!mx.running()) return;

  Zhttp::EngineConfig engine{&mx, "3", "4"};
  Server<Protocol> server{&state};
  Client<Protocol> client{&state};
  bool serverInit = server.init(engine, Config<Protocol>::server(temp));
  bool clientInit = client.init(engine, Config<Protocol>::client(temp));
  ZuCHECK(serverInit && clientInit, "H1 engine initialization failed");
  if (!serverInit || !clientInit) {
    if (clientInit) client.final();
    if (serverInit) server.final();
    mx.stop();
    return;
  }
  state.trace.push(LifeEvt::Init);

  bool serverStart = server.start();
  bool clientStart = client.start();
  ZuCHECK(serverStart && clientStart, "H1 engine start failed");
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
  using Link = typename Client<Protocol>::Link;
  using Links =
    ZtArray<ZmRef<Link>, ZtArrayHeapID<"Zhttp.Test.EngineLinks">>;
  Links links_;
  if (listening) {
    for (unsigned i = 0; i < state.links; ++i) {
      ZmRef<Link> link = new Link{&client};
      state.trace.push(LifeEvt::CliConnect);
      link->connect("127.0.0.1", state.port);
      links_.push(ZuMv(link));
    }
    auto &complete =
      Client<Protocol>::Multiplexed ? state.messageDone : state.done;
    ZuCHECK(complete.timedwait(Zm::now(10)) == 0,
      "H1 message lifecycle timed out");
  }

  server.stopAccepting();
  bool stopped = false;
  if constexpr (ZuIsSame<Protocol, Zhttp::QUIC>{}) {
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
  ZuCHECK(stopped, "H1 engine stop failed");
  if constexpr (Client<Protocol>::Multiplexed) {
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

template <typename Protocol>
void runServerStop(const TempDir &temp)
{
  ZuTestScope(runServerStop);

  State state;
  state.rounds = 1;
  state.keepAlive = true;
  state.port = loopbackPort();
  ZuCHECK(state.port, "active-stop loopback port allocation failed");
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "active-stop multiplexer start failed");
  if (!mx.running()) return;

  Zhttp::EngineConfig engine{&mx, "3", "4"};
  Server<Protocol> server{&state};
  Client<Protocol> client{&state};
  bool serverInit = server.init(engine, Config<Protocol>::server(temp));
  bool clientInit = client.init(engine, Config<Protocol>::client(temp));
  ZuCHECK(serverInit && clientInit,
    "active-stop engine initialization failed");
  if (!serverInit || !clientInit) {
    if (clientInit) client.final();
    if (serverInit) server.final();
    mx.stop();
    return;
  }

  bool serverStart = server.start();
  bool clientStart = client.start();
  ZuCHECK(serverStart && clientStart, "active-stop engine start failed");
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
  using Link = typename Client<Protocol>::Link;
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
  state.port = loopbackPort();
  ZuCHECK(state.port, "TLS failure loopback port allocation failed");
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "TLS failure multiplexer start failed");
  if (!mx.running()) return;

  Zhttp::EngineConfig engine{&mx, "3", "4"};
  Server<Zhttp::TLS> server{&state};
  FailClient client{&state};
  bool serverInit = server.init(engine, Config<Zhttp::TLS>::server(cert));
  bool clientInit = client.init(
    engine, Zhttp::TLSConfig{}.caPath(otherCA.certPath.cspan()));
  ZuCHECK(serverInit && clientInit, "TLS failure engine initialization failed");
  if (!serverInit || !clientInit) {
    if (clientInit) client.final();
    if (serverInit) server.final();
    mx.stop();
    return;
  }

  bool serverStart = server.start();
  bool clientStart = client.start();
  ZuCHECK(serverStart && clientStart, "TLS failure engine start failed");
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

} // namespace ZhttpH1EngineTest_

#endif /* ZhttpEngineFixture_HH */
