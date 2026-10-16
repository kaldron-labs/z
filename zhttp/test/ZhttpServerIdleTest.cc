//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Zhttp Server HTTP/3 idle-expiry test

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiResolver.hh>

#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpServer.hh>

#include "ZhttpTestUtil.hh"

using namespace ZuTestUtil;

namespace ZhttpServerIdleTest_ {

using RequestHeaderList = ZhttpHeaders("content-length");
ZhttpHdrCatalogDerive(RequestHeaders, RequestHeaderList);
ZhttpHdrCatalogImpl(RequestHeaders)

unsigned testPort()
{
  static unsigned port = ZhttpTestPort::ServerIdle;
  return Zhttp::Test::loopbackPort(port++);
}

namespace ResponseKind {
  // Synchronous Builder policies exercised by the heterogeneous workload.
  enum { None, Fixed, OptionalFixed, Stream, OptionalStream };
}

struct State {
  ZiMultiplex	*mx = nullptr;
  ZmSemaphore	listening;
  ZmSemaphore	admitted;
  ZmSemaphore	connected;
  ZmSemaphore	requestStarted;
  ZmSemaphore	responseStarted;
  ZmSemaphore	disconnected;
  ZmSemaphore	released;
  ZmSemaphore	stopped;
  ZmSemaphore	completed;
  ZmAtomic<unsigned> errors = 0;
  ZmAtomic<unsigned> releaseCount = 0;
  ZmAtomic<unsigned> sessionConnectCount = 0;
  ZmAtomic<unsigned> sessionDisconnectCount = 0;
  ZmAtomic<unsigned> stopCount = 0;
  ZmAtomic<unsigned> stopBeforeRelease = 0;
  ZmAtomic<unsigned> completedCount = 0;
  ZmAtomic<unsigned> responseRetired = 0;
  ZmAtomic<unsigned> responseSubmitted = 0;
  uint16_t	port = 0;
  int8_t	expectedTransport = Zhttp::Transport::QUIC;
  bool		openRequest = false;
  bool		completeRequest = false;
  bool		emitOptional = false;
  bool		checkThreads = false;
  bool		responseNotified = false;
  int8_t	responseKind = ResponseKind::None;

  void rxCallback() {
    if (checkThreads && (!mx || !mx->invoked(3))) fail();
  }
  void txCallback() {
    if (checkThreads && (!mx || !mx->invoked(4))) fail();
  }

  void responseCallback() {
    txCallback();
    if (!responseNotified) {
      responseNotified = true;
      responseStarted.post();
    }
  }

  void fail() {
    errors = 1;
    listening.post();
    admitted.post();
    connected.post();
    requestStarted.post();
    responseStarted.post();
    disconnected.post();
    released.post();
    stopped.post();
    completed.post();
  }
};

struct App;

struct ResBuilder_ : public ZmObject, public Zhttp::ResBuilder {
  using HdrCatalog = Zhttp::DefltHdrCatalog;
  Zhttp::BodyPolicy::T bodyPolicy() const {
    switch (state->responseKind) {
      case ResponseKind::Fixed: return Zhttp::BodyPolicy::Fixed;
      case ResponseKind::OptionalFixed:
	  return Zhttp::BodyPolicy::OptionalFixed;
      case ResponseKind::Stream: return Zhttp::BodyPolicy::Stream;
      case ResponseKind::OptionalStream:
	  return Zhttp::BodyPolicy::OptionalStream;
      default: return Zhttp::BodyPolicy::None;
    }
  }
  Zhttp::Method::T method() const { return Zhttp::Method::POST; }
  unsigned status() const { state->responseCallback(); return 200; }
  template <typename Key, typename L> void header(L &&) const {
    state->responseCallback();
  }
  template <typename L> void header(L &&) const {
    state->responseCallback();
  }
  bool disconnect() const { state->responseCallback(); return false; }
  void completed(const Zhttp::ResponseResult &result) {
    state->txCallback();
    ++state->responseRetired;
    if (result.ok()) ++state->responseSubmitted;
  }
  template <typename Emit>
  void body(Emit &&emit) {
    state->responseCallback();
    if (bodyPolicy() == Zhttp::BodyPolicy::OptionalFixed &&
	  !state->emitOptional)
	return;
    emit([](auto &body) {
	body << 'x';
	return Zhttp::WriteOutcome::End;
    });
  }

  State *state = nullptr;
};
using ResBuilderQ = ZmList<ResBuilder_,
  ZmListNode<ResBuilder_, ZmListHeapID<"Zhttp.Server.Idle.ResBuilder">>>;
using ResBuilder = ResBuilderQ::Node;

struct Parser : public Zhttp::Parser {
  using HdrCatalog = Zhttp::DefltHdrCatalog;

  void init(App &app);

  bool operation(Zhttp::Method::T, Zhttp::Target &) {
    if (state) {
	state->rxCallback();
	state->requestStarted.post();
    }
    return true;
  }
  bool bodyInfo(Zhttp::BodyType::T, uint64_t) {
    if (state) state->rxCallback();
    return true;
  }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t>) {
    if (state) state->rxCallback();
  }
  template <typename Rx>
  bool body(Rx &rx) {
    if (state) state->rxCallback();
    return Zhttp::bodyDrain(rx);
  }
  template <typename Link>
  void complete(Link *link, bool ok) {
    if (state) state->rxCallback();
    if (ok) {
	ZmRef<ResBuilder> response = new ResBuilder{};
	response->state = state;
	Zhttp::Route<ResBuilder> route{link, Zhttp::Method::POST};
	route.send(ZuMv(response));
    }
    if (state->openRequest) {
	++state->completedCount;
	state->completed.post();
    }
  }

  void reset() { state = nullptr; }

  State *state = nullptr;
};

struct App : public Zhttp::Server<App, ZhttpServerIdleTest_::Parser, ResBuilderQ> {
  using HTTP = Zhttp::Server<App, ZhttpServerIdleTest_::Parser, ResBuilderQ>;
  using HTTP::connected;
  using HTTP::disconnected;

  App(State *state_) : testState{state_} { }

  void listening(int8_t transport, uint16_t port) {
    if (transport != testState->expectedTransport || port != testState->port)
      testState->fail();
    else
      testState->listening.post();
  }
  void listenFailed(int8_t, bool) { testState->fail(); }
  void connected(int8_t transport) {
    if (transport != testState->expectedTransport)
      testState->fail();
    else
      testState->admitted.post();
  }
  void disconnected(int8_t transport) {
    if (transport != testState->expectedTransport)
      testState->fail();
    else {
      ++testState->releaseCount;
      testState->released.post();
    }
  }
  void connected(Zhttp::Session session) {
    if (!session || session.transport != testState->expectedTransport)
      testState->fail();
    ++testState->sessionConnectCount;
  }
  void disconnected(Zhttp::Session session) {
    if (!session || session.transport != testState->expectedTransport)
      testState->fail();
    ++testState->sessionDisconnectCount;
  }

  State	*testState;
};

ZuAssert((ZuIsSame<App::ResponseHeaders, ZuTypeList<>>{}));

struct SeedHeaders {
  using List = ZhttpHeaders(("x-server-seed", "seed"));
};
struct SeedBuilder : ResBuilder_ {
  using ResponseHeaders = ZuTypeList<SeedHeaders>;
};
using SeedQ = ZmList<SeedBuilder,
  ZmListNode<SeedBuilder, ZmListHeapID<"Zhttp.Server.SeedBuilder">>>;
struct SeedApp;
struct SeedParser : Parser {
  void init(SeedApp &) { state = nullptr; }
  template <typename Link> void complete(Link *, bool) { }
};
using SeedServer = Zhttp::Server<SeedApp, SeedParser, SeedQ>;
ZuAssert((ZuIsSame<SeedServer::ResponseHeaders,
  ZuTypeList<SeedHeaders>>{}));

struct SeedApp : SeedServer {
  using SeedServer::connected;
  using SeedServer::disconnected;
  void listening(int8_t, uint16_t) { }
  void listenFailed(int8_t, bool) { }
  void connected(int8_t) { }
  void disconnected(int8_t) { }
};

inline void Parser::init(App &app) { state = app.testState; }

template <typename Profile>
struct Client : public Zhttp::ClientHub<Client<Profile>, Profile> {
  struct Builder :
    public Zhttp::Builder,
    public Zhttp::MessageTraits<Profile>::template Request<
      Builder, RequestHeaders, true, false> {
    using Base = Zhttp::MessageTraits<Profile>::template Request<
      Builder, RequestHeaders, true, false>;
    using HdrCatalog = RequestHeaders;
    using Base::body;

    template <typename L>
    void operation(L &&l) const {
      l(Zhttp::Method::POST, [](auto &&emit) {
	emit([](auto &tx) { tx << '/'; });
      });
    }
    template <typename L>
    void host(L &&l) const { l("localhost"); }
    template <typename Key, typename L>
    void header(L &&l) const {
      if constexpr (Key{}() == "content-length") l("1");
    }
    template <typename L> void header(L &&) const { }
  };

  struct Link :
    public Zhttp::ClientLink<Client, Link, Profile> {
    using Base = Zhttp::ClientLink<Client, Link, Profile>;
    using Base::Base;

    unsigned id = 0;
  };

  Client(State *state_) : state{state_} { }

  void connected(Link &link, const Zhttp::ConnectedInfo &) {
    if (state->openRequest) {
      this->txRun([link = &link, complete = state->completeRequest]() {
	Builder builder;
	auto tx = link->transmit_(builder);
	builder.begin(tx);
	if (complete) {
	  auto body = builder.body(tx, 1);
	  body << 'x';
	  body.flush();
	  builder.finish(tx);
	  link->finish();
	}
      });
    }
    state->connected.post();
  }
  void disconnected(Link &, bool) {
    state->disconnected.post();
  }
  void connectFailed(Link &, bool) { state->fail(); }
  template <typename Rx>
  int process(Link &, Rx &) {
    return 0;
  }

  State	*state;
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

void responseTypes()
{
  ZuTestScope(responseTypes);

  State state;
  bool buildersOK = true;
  for (unsigned kind = ResponseKind::None;
      kind <= ResponseKind::OptionalStream; ++kind) {
    state.responseKind = kind;
    unsigned seen = unsigned(-1);
    App::ResBuilder_ response;
    response.state = &state;
    auto policy = response.bodyPolicy();
    (void)response.close();
    (void)response.status();
    response.header([](auto &&, auto &&) { });
    if (policy == Zhttp::BodyPolicy::None)
      seen = ResponseKind::None;
    else if (policy == Zhttp::BodyPolicy::Fixed)
      seen = ResponseKind::Fixed;
    else if (policy == Zhttp::BodyPolicy::OptionalFixed)
      seen = ResponseKind::OptionalFixed;
    else if (policy == Zhttp::BodyPolicy::Stream)
      seen = ResponseKind::Stream;
    else if (policy == Zhttp::BodyPolicy::OptionalStream)
      seen = ResponseKind::OptionalStream;
    if (seen != kind) buildersOK = false;
  }
  ZuCHECK(buildersOK,
    "workload emits the selected concrete response Builder");
  ZuCHECK(!state.errors.load_(),
    "heterogeneous response Builders satisfy the common contract");
}

void idle()
{
  ZuTestScope(idle);

  Zhttp::Test::TempDir temp;
  bool tempOK = temp.init("ZhttpServerIdle");
  ZuCHECK(tempOK, "create temporary directory");
  if (!tempOK) return;
  ZtString<> cert, key;
  bool certOK = Zhttp::Test::writeLocalhostCert(temp, cert, key);
  ZuCHECK(certOK, "create localhost certificate");
  if (!certOK) return;

  State state;
  state.port = testPort();
  state.expectedTransport = Zhttp::Transport::QUIC;
  ZuCHECK(state.port, "allocate loopback port");
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};
  state.mx = &mx;
  state.checkThreads = true;
  bool mxUp = mx.start();
  ZuCHECK(mxUp, "start multiplexer");
  if (!mxUp) return;
  Zhttp::HubConfig hub{&mx, "3", "4"};

  App app{&state};
  auto serverQUIC = Zhttp::QUICConfig{}.certPath(cert).keyPath(key);
  auto serverConfig = Zhttp::ServerConfig()
    .localIP(ZiIP{"127.0.0.1"}).port(state.port)
    .idleTimeout(1).quic(ZuMv(serverQUIC));
  bool serverInited =
    app.init(hub, ZuMv(serverConfig));
  ZuCHECK(serverInited, "initialize server");
  bool serverUp = serverInited && app.start();
  ZuCHECK(serverUp, "start server");
  bool listening = serverUp &&
    state.listening.timedwait(Zm::now(10)) == 0;
  ZuCHECK(listening, "server listens for HTTP/3");

  Client<Zhttp::H3QUIC> client{&state};
  bool clientInited = listening && client.init(
    hub, Zhttp::QUICConfig{}.caPath(cert).maxIdleTimeout(10000));
  ZuCHECK(clientInited, "initialize client");
  bool clientUp = clientInited && client.start();
  ZuCHECK(clientUp, "start client");
  using ClientLink = Client<Zhttp::H3QUIC>::Link;
  ZmRef<ClientLink> link;
  if (clientUp) link = new ClientLink{&client};
#ifdef ZmObject_DEBUG
  if (link) link->ZmObject::debug();
#endif
  if (link) {
    link->connect("localhost", state.port);
    bool connected = state.connected.timedwait(Zm::now(10)) == 0;
    ZuCHECK(connected, "establish idle HTTP/3 logical link");
    if (connected) {
      ZuCHECK(app.activeConnections() == 1,
	"server accounts for active H3 link");
      ZuCHECK(state.disconnected.timedwait(Zm::now(10)) == 0,
	"QUIC transport idle timeout disconnects client");
      ZuCHECK(state.released.timedwait(Zm::now(10)) == 0,
	"server reports idle H3 admission release");
      ZuCHECK(!app.activeConnections(),
	"idle H3 link releases server admission");
      ZuCHECK(state.sessionConnectCount.load_() == 1,
	"session arrival callback is delivered once");
      ZuCHECK(state.sessionDisconnectCount.load_() == 1,
	"session departure callback is delivered once");
    }
  }

  link = nullptr;
  bool clientStopped = !clientInited || client.stop();
  ZuCHECK(clientStopped, "stop client after idle expiry");
  bool serverStopped = !serverInited || app.stop();
  ZuCHECK(serverStopped, "stop server after idle expiry");
  if (clientInited) client.final();
  if (serverInited) app.final();
  ZiResolver::stop();
  ZiResolver::final();
  mx.stop();
  ZuCHECK(!state.errors.load_(), "idle expiry has no transport error");
}

template <typename Profile, bool Limit = false, bool Sync = false>
void activeStop()
{
  ZuTestScope(activeStop);

  Zhttp::Test::TempDir temp;
  bool tempOK = temp.init("ZhttpServerStop");
  ZuCHECK(tempOK, "create temporary directory");
  if (!tempOK) return;
  ZtString<> cert, key;
  bool certOK = Zhttp::Test::writeLocalhostCert(temp, cert, key);
  ZuCHECK(certOK, "create localhost certificate");
  if (!certOK) return;

  State state;
  state.port = testPort();
  using HTTP = Zhttp::ProfileTraits<Profile>;
  using Protocol = HTTP::Protocol;
  state.expectedTransport = HTTP::Transport::ID;
  state.openRequest = true;
  state.completeRequest = Sync;
  ZuCHECK(state.port, "allocate loopback port");
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};
  state.mx = &mx;
  state.checkThreads = true;
  bool mxUp = mx.start();
  ZuCHECK(mxUp, "start multiplexer");
  if (!mxUp) return;
  Zhttp::HubConfig hub{&mx, "3", "4"};

  App app{&state};
  auto serverConfig = Zhttp::ServerConfig()
    .localIP(ZiIP{"127.0.0.1"}).port(state.port)
    .maxRequests(1).retainedBytesMax(17);
  if constexpr (ZuIsSame<Protocol, Zhttp::TCP>{})
    serverConfig.tcp();
  else if constexpr (ZuIsSame<Protocol, Zhttp::TLS>{})
    serverConfig.tls(
      Zhttp::H2Config{}.certPath(cert).keyPath(key)
	.policy(Zhttp::H2Policy::Prefer));
  else
    serverConfig.quic(
      Zhttp::QUICConfig{}.certPath(cert).keyPath(key).maxIdleTimeout(10000));
  bool serverInited =
    app.init(hub, ZuMv(serverConfig));
  ZuCHECK(serverInited, "initialize server");
  bool serverUp = serverInited && app.start();
  ZuCHECK(serverUp, "start server");
  bool listening = serverUp &&
    state.listening.timedwait(Zm::now(10)) == 0;
  ZuCHECK(listening, "server listens for HTTP/3");

  Client<Profile> client{&state};
  bool clientInited = false;
  if constexpr (ZuIsSame<Protocol, Zhttp::TCP>{})
    clientInited = listening && client.init(hub, Zhttp::TCPConfig{});
  else if constexpr (ZuIsSame<Protocol, Zhttp::TLS>{})
    if constexpr (HTTP::Multiplexed)
      clientInited = listening && client.init(
	hub, Zhttp::H2Config{}.caPath(cert)
	  .policy(Zhttp::H2Policy::Force));
    else
      clientInited =
	listening && client.init(hub, Zhttp::TLSConfig{}.caPath(cert));
  else
    clientInited = listening && client.init(
      hub, Zhttp::QUICConfig{}.caPath(cert).maxIdleTimeout(10000));
  ZuCHECK(clientInited, "initialize client");
  bool clientUp = clientInited && client.start();
  ZuCHECK(clientUp, "start client");
  using ClientLink = Client<Profile>::Link;
  ZmRef<ClientLink> link;
  if (clientUp) link = new ClientLink{&client};
#ifdef ZmObject_DEBUG
  if (link) {
    if constexpr (HTTP::Multiplexed ||
	ZuIsSame<Protocol, Zhttp::QUIC>{})
      link->ZmObject::debug();
    else
      link->ZmPolymorph::debug();
  }
#endif
  if (link) link->connect("localhost", state.port);
  bool connected = link &&
    state.connected.timedwait(Zm::now(10)) == 0;
  ZuCHECK(connected, "establish active HTTP/3 logical link");
  bool admitted = connected &&
    state.admitted.timedwait(Zm::now(10)) == 0;
  ZuCHECK(admitted, "server admits active link");
  if (admitted)
    ZuCHECK(app.activeConnections() == 1,
	"server accounts for active link");
  bool requestStarted = admitted &&
    state.requestStarted.timedwait(Zm::now(10)) == 0;
  ZuCHECK(requestStarted, "server admits partial request");
  if (requestStarted)
    ZuCHECK(state.completeRequest || app.activeRequests() == 1,
	"server accounts for admitted request");
  bool responseStarted = !Sync ||
    state.responseStarted.timedwait(Zm::now(10)) == 0;
  ZuCHECK(responseStarted, "synchronous response construction starts");

  ZmRef<ClientLink> overflowLink;
  if constexpr (Limit) {
    overflowLink = new ClientLink{&client};
    overflowLink->connect("localhost", state.port);
    bool overflowConnected =
      state.connected.timedwait(Zm::now(10)) == 0;
    ZuCHECK(overflowConnected, "establish request-limit connection");
    bool overflowAdmitted = overflowConnected &&
      state.admitted.timedwait(Zm::now(10)) == 0;
    ZuCHECK(overflowAdmitted, "admit request-limit connection");
    ZuCHECK(state.disconnected.timedwait(Zm::now(10)) == 0,
	"request-limit connection is refused");
    ZuCHECK(state.released.timedwait(Zm::now(10)) == 0,
	"request-limit connection credit is released");
    ZuCHECK(app.activeRequests() == 1 &&
	app.activeConnections() == 1,
	"request limit preserves the admitted request only");
    ZuCHECK(app.rejectedRequests() == 1,
	"request-limit refusal is accounted");
    ZuCHECK(state.requestStarted.trywait() != 0,
	"request-limit refusal allocates no application request");
  }

  ZmAtomic<unsigned> stopReturned = 0;
  constexpr unsigned ExpectedReleases = Limit ? 2 : 1;
  if (serverInited) app.stop([
    &state, &stopReturned
  ](bool ok) {
    if (!ok) state.errors = 1;
    if (!stopReturned.load_()) state.errors = 1;
    if (state.releaseCount.load_() != ExpectedReleases)
      ++state.stopBeforeRelease;
    ++state.stopCount;
    state.stopped.post();
  });
  stopReturned = 1;
  bool stopped = !serverInited;
  if (!stopped) {
    auto timeout = Zm::now(10);
    do {
      if (state.stopCount.load_()) {
	stopped = true;
	break;
      }
    } while (state.stopped.timedwait(timeout) == 0);
  }
  if (!stopped && serverInited) (void)app.stop();
  ZuCHECK(stopped, "active server stop completes");
  ZuCHECK(state.stopCount.load_() == 1,
    "active server stop callback completes exactly once");
  ZuCHECK(!state.stopBeforeRelease.load_(),
    "active server admission releases before stop completion");
  ZuCHECK(state.completedCount.load_() == 1,
    "active server stop completes partial request exactly once");
  if constexpr (Sync) {
    ZuCHECK(state.responseRetired.load_() == 1,
      "response builder retires exactly once during stop");
  }
  ZuCHECK(app.activeRequests() == 0,
	"active server stop releases request admission");
  ZuCHECK(!app.serverFaults() && !app.parseFailures() &&
      !app.bodyFailures() &&
      !app.responseBuildFailures(),
    "normal shutdown does not report server, parse, or build failures");
  ZuCHECK(!app.transportFailures(),
    "partial request shutdown is not a response transport failure");
  if (clientInited) (void)client.stop();
  ZuCHECK(state.disconnected.timedwait(Zm::now(10)) == 0,
    "client stop completes active disconnect");
  ZuCHECK(app.activeConnections() == 0,
	"active server stop drains admission");

  link = nullptr;
  overflowLink = nullptr;
  if (clientInited) client.final();
  if (serverInited) app.final();
  ZiResolver::stop();
  ZiResolver::final();
  mx.stop();
  ZuCHECK(!state.errors.load_(), "active server stop has no error");
}

void retryInit()
{
  ZuTestScope(retryInit);
  State state;
  state.port = testPort();
  state.expectedTransport = Zhttp::Transport::TCP;
  ZuCHECK(state.port, "allocate retry listener port");
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};
  state.mx = &mx;
  bool mxUp = mx.start();
  ZuCHECK(mxUp, "start retry multiplexer");
  if (!mxUp) return;
  Zhttp::HubConfig hub{&mx, "3", "4"};
  App app{&state};
  ZuCHECK(app.state() == ZmEngineState::Stopped,
    "consolidated application exposes engine state");
  auto invalid = Zhttp::ServerConfig()
    .localIP(ZiIP{"127.0.0.1"}).port(state.port).tcp()
    .tls(Zhttp::H2Config{}.certPath("/nonexistent/zhttp-retry-cert.pem")
      .keyPath("/nonexistent/zhttp-retry-key.pem"));
  ZuCHECK(!app.init(hub, ZuMv(invalid)),
    "initialization fails after the TCP hub is acquired");
  ZuCHECK(!app.hubCount(), "failed initialization releases registered hubs");

  auto valid = Zhttp::ServerConfig()
    .localIP(ZiIP{"127.0.0.1"}).port(state.port).tcp();
  bool initialized = app.init(hub, ZuMv(valid));
  ZuCHECK(initialized, "retry initialization succeeds");
  if (initialized) {
    auto repeated = Zhttp::ServerConfig()
      .localIP(ZiIP{"127.0.0.1"}).port(state.port).tcp();
    ZuCHECK(app.init(hub, ZuMv(repeated)), "initialized retry is idempotent");
    ZuCHECK(app.hubCount() == 1, "retry does not duplicate hub registration");
    app.final();
  }

  auto reused = Zhttp::ServerConfig()
    .localIP(ZiIP{"127.0.0.1"}).port(state.port).tcp();
  bool reusedInit = app.init(hub, ZuMv(reused));
  ZuCHECK(reusedInit, "initialize after final succeeds");
  ZuCHECK(app.hubCount() == 1, "new lifecycle registers one hub");
  bool started = reusedInit && app.start();
  ZuCHECK(started, "reinitialized server starts");
  if (started) {
    ZuCHECK(state.listening.timedwait(Zm::now(10)) == 0,
      "reinitialized server listens");
    ZuCHECK(app.stop(), "reinitialized server stops");
  }
  app.final();

  App partial{&state};
  auto invalidAgain = Zhttp::ServerConfig()
    .localIP(ZiIP{"127.0.0.1"}).port(state.port).tcp()
    .tls(Zhttp::H2Config{}.certPath("/nonexistent/zhttp-retry-cert.pem")
      .keyPath("/nonexistent/zhttp-retry-key.pem"));
  ZuCHECK(!partial.init(hub, ZuMv(invalidAgain)),
    "second initialization fails after acquiring TCP");
  partial.final();
  ZuCHECK(!partial.hubCount(), "final handles partial initialization");
  mx.stop();
}

void seedSets()
{
  ZuTestScope(seedSets);
  Zhttp::Test::TempDir temp;
  bool tempOK = temp.init("ZhttpServerSeeds");
  ZuCHECK(tempOK, "create seed fixture directory");
  if (!tempOK) return;
  ZtString<> cert, key;
  bool certOK = Zhttp::Test::writeLocalhostCert(temp, cert, key);
  ZuCHECK(certOK, "create seed fixture certificate");
  if (!certOK) return;

  ZiMultiplex mx{mxParams()};
  bool mxUp = mx.start();
  ZuCHECK(mxUp, "start seed multiplexer");
  if (!mxUp) return;
  Zhttp::HubConfig hub{&mx, "3", "4"};
  SeedApp app;
  unsigned port = testPort();
  constexpr unsigned SeedQPackCapacity =
    sizeof("x-server-seed") - 1 + sizeof("seed") - 1 + 32;
  auto config = [&]() {
    Zhttp::ServerConfig result;
    result.localIP(ZiIP{"127.0.0.1"}).port(port)
      .tls(Zhttp::H2Config{}.certPath(cert).keyPath(key))
      .quic(Zhttp::QUICConfig{}.certPath(cert).keyPath(key)
        .qpackTxCapacity(SeedQPackCapacity));
    return result;
  };
  bool initialized = app.init(hub, config());
  ZuCHECK(initialized, "explicit response seed server initializes");
  if (initialized) {
    ZuCHECK(app.hpackSeedPlans().length() == 1,
      "builder header catalog supplies one seed plan");
    ZuCHECK(app.qpackSeeds().length() == 1,
      "builder header catalog supplies one QPACK seed set");
    ZuCHECK(app.init(hub, config()), "seed initialization is idempotent");
    ZuCHECK(app.hpackSeedPlans().length() == 1,
      "repeated init does not duplicate the seed plan");
    ZuCHECK(app.qpackSeeds().length() == 1,
      "repeated init does not duplicate QPACK seeds");
  }
  app.final();
  ZuCHECK(!app.hpackSeedPlans().length(), "final releases seed plans");
  ZuCHECK(!app.qpackSeeds().length(), "final releases QPACK seeds");
  bool reused = app.init(hub, config());
  ZuCHECK(reused, "seed server initializes after final");
  if (reused)
    ZuCHECK(app.hpackSeedPlans().length() == 1,
      "new lifecycle rebuilds one seed plan");
  if (reused)
    ZuCHECK(app.qpackSeeds().length() == 1,
      "new lifecycle rebuilds one QPACK seed set");
  app.final();
  mx.stop();
}

} // namespace ZhttpServerIdleTest_

int main(int argc, char **argv)
{
  using namespace ZhttpServerIdleTest_;

  (void)argc;
  (void)argv;
  ZiTestResidue::init("ZhttpServerIdleTest");
  ZuTestMain();
  ZuTestCall(responseTypes);
  ZuTestCall(retryInit);
  ZuTestCall(seedSets);
  ZuTestCall(idle);
  ZuTestCall((activeStop<Zhttp::H1TCP>));
  ZuTestCall((activeStop<Zhttp::H1TLS>));
  ZuTestCall((activeStop<Zhttp::H2TLS>));
  ZuTestCall((activeStop<Zhttp::H3QUIC>));
  ZuTestCall((activeStop<Zhttp::H1TCP, false, true>));
  ZuTestCall((activeStop<Zhttp::H1TLS, false, true>));
  ZuTestCall((activeStop<Zhttp::H2TLS, false, true>));
  ZuTestCall((activeStop<Zhttp::H3QUIC, false, true>));
  ZuTestCall((activeStop<Zhttp::H1TCP, true>));
  return 0;
}
