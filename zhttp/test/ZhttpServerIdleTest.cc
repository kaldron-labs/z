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

using ProducerDone = ZmFn<void(ZmRef<ZiIOBuf>, bool),
  ZmFnHeapID<"Zhttp.Server.BodyChunk">>;

namespace ResponseKind {
  // Synchronous Builder policies exercised by the heterogeneous workload.
  enum { None, Fixed, OptionalFixed, Stream, OptionalStream };
}

struct State {
  ZmSemaphore	listening;
  ZmSemaphore	admitted;
  ZmSemaphore	connected;
  ZmSemaphore	requestStarted;
  ZmSemaphore	responseStarted;
  ZmSemaphore	producerStarted;
  ZmSemaphore	disconnected;
  ZmSemaphore	released;
  ZmSemaphore	stopped;
  ZmSemaphore	completed;
  ZmAtomic<unsigned> errors = 0;
  ZmAtomic<unsigned> releaseCount = 0;
  ZmAtomic<unsigned> stopCount = 0;
  ZmAtomic<unsigned> stopBeforeRelease = 0;
  ZmAtomic<unsigned> completedCount = 0;
  ZmAtomic<unsigned> responseResetCount = 0;
  uint16_t	port = 0;
  int8_t	expectedTransport = Zhttp::Transport::QUIC;
  bool		openRequest = false;
  bool		completeRequest = false;
  bool		asyncResponse = false;
  bool		emitOptional = false;
  int8_t	responseKind = ResponseKind::None;
  ProducerDone	producerDone;

  void responseReset() {
    if (++responseResetCount != 1) fail();
    responseStarted.post();
  }
  void responseCallback() {
    if (responseResetCount.load_() != 1) fail();
  }

  void fail() {
    errors = 1;
    listening.post();
    admitted.post();
    connected.post();
    requestStarted.post();
    responseStarted.post();
    producerStarted.post();
    disconnected.post();
    released.post();
    stopped.post();
    completed.post();
  }
};

struct Workload {
  template <Zhttp::BodyPolicy::T Policy>
  struct Response_ {
    using Headers = ZuTypeList<>;
    constexpr Zhttp::BodyPolicy::T bodyPolicy() const { return Policy; }
    void reset() { state->responseReset(); }
    unsigned status() const { state->responseCallback(); return 200; }
    template <typename L> void reason(L &&l) const {
      state->responseCallback();
      l("OK");
    }
    template <typename Key, typename L> void header(L &&) const {
      state->responseCallback();
    }
    template <typename L> void header(L &&) const {
      state->responseCallback();
    }
    bool close() const { state->responseCallback(); return false; }

    State *state = nullptr;
  };
  struct Response : public Response_<Zhttp::BodyPolicy::None> { };
  struct FixedResponse : public Response_<Zhttp::BodyPolicy::Fixed> {
    template <typename Emit>
    void body(Emit &&emit) {
      this->state->responseCallback();
      emit([](auto &body) { body << 'x'; });
    }
    template <typename L> void bodyHdrs(L &&) {
      this->state->responseCallback();
    }
  };
  struct OptionalFixedResponse :
      public Response_<Zhttp::BodyPolicy::OptionalFixed> {
    template <typename Emit>
    void body(Emit &&emit) {
      this->state->responseCallback();
      if (this->state->emitOptional)
	emit([](auto &body) { body << 'x'; });
    }
    template <typename L> void bodyHdrs(L &&) {
      this->state->responseCallback();
    }
  };
  struct StreamResponse : public Response_<Zhttp::BodyPolicy::Stream> {
    template <typename Emit>
    void body(Emit &&emit) {
      this->state->responseCallback();
      emit([](auto &body) { body << 'x'; });
    }
  };
  struct OptionalStreamResponse :
      public Response_<Zhttp::BodyPolicy::OptionalStream> {
    template <typename Emit>
    void body(Emit &&emit) {
      this->state->responseCallback();
      if (this->state->emitOptional)
	emit([](auto &body) { body << 'x'; });
    }
  };
  struct Request {
    using Headers = ZuTypeList<>;
    void operation(Zhttp::Method::T, const Zhttp::RequestTarget &) {
      if (state) state->requestStarted.post();
    }
    void bodyInfo(Zhttp::BodyType::T, uint64_t) { }
    template <typename Key>
    void header(Zhttp::HdrSection, ZuBSpan) { }
    template <typename Rx> void body(Rx &rx) { Zhttp::bodyDrain(rx); }
    void complete(bool) { }

    State *state = nullptr;
  };
  struct AsyncResponse {
    using Headers = ZuTypeList<>;
    constexpr Zhttp::BodyPolicy::T bodyPolicy() const {
      return Zhttp::BodyPolicy::Stream;
    }
    void reset() { state->responseReset(); }
    unsigned status() const { state->responseCallback(); return 200; }
    template <typename L> void reason(L &&l) const {
      state->responseCallback();
      l("OK");
    }
    template <typename Key, typename L> void header(L &&) const {
      state->responseCallback();
    }
    template <typename L> void header(L &&) const {
      state->responseCallback();
    }
    bool close() const { state->responseCallback(); return false; }
    template <typename Done>
    void next(unsigned, Done done) {
      state->responseCallback();
      state->producerDone = ProducerDone{ZuMv(done)};
      state->producerStarted.post();
    }

    State *state = nullptr;
  };

  Workload(State *state_) : state{state_} { }
  Request request() { return {state}; }
  Zhttp::RequestDisposition::T requestError(
    const Zhttp::RequestMeta &, Request &,
    const Zhttp::RequestError &) {
    return Zhttp::RequestDisposition::Disconnect;
  }

  void listening(int8_t transport, uint16_t port) {
    if (transport != state->expectedTransport || port != state->port)
      state->fail();
    else
      state->listening.post();
  }
  void listenFailed(int8_t, bool) { state->fail(); }
  void connected(int8_t transport) {
    if (transport != state->expectedTransport)
      state->fail();
    else
      state->admitted.post();
  }
  void disconnected(int8_t transport) {
    if (transport != state->expectedTransport)
      state->fail();
    else {
      ++state->releaseCount;
      state->released.post();
    }
  }

  template <typename Emit>
  void respond(const Zhttp::RequestMeta &, Request &, Emit &&emit) {
    if (state->asyncResponse) {
      emit(AsyncResponse{state});
      return;
    }
    switch (state->responseKind) {
      case ResponseKind::Fixed:
	emit(FixedResponse{{state}});
	break;
      case ResponseKind::OptionalFixed:
	emit(OptionalFixedResponse{{state}});
	break;
      case ResponseKind::Stream:
	emit(StreamResponse{{state}});
	break;
      case ResponseKind::OptionalStream:
	emit(OptionalStreamResponse{{state}});
	break;
      default:
	emit(Response{{state}});
	break;
    }
  }
  void committed(
    const Zhttp::RequestMeta &, Request &, const Zhttp::BodyCommit &) { }
  void completed(
    const Zhttp::RequestMeta &, Request &,
    const Zhttp::ResponseResult &result) {
    if (!state->openRequest) return;
    auto expected = state->asyncResponse ?
      Zhttp::ResponseOutcome::Cancelled : state->completeRequest ?
      Zhttp::ResponseOutcome::Success : Zhttp::ResponseOutcome::Reset;
    if (result.outcome != expected) state->fail();
    ++state->completedCount;
    state->completed.post();
  }
  bool close(const Response &) const { return false; }

  State	*state;
};

using Server = Zhttp::Server<Workload>;

template <typename Profile>
struct Client : public Zhttp::ClientHub<Client<Profile>, Profile> {
  using RequestHeaders = ZuTypeList<
    ZuStringT<"content-length">, void>;

  struct Builder :
    public Zhttp::MessageTraits<Profile>::template Request<
      Builder, RequestHeaders, ZuTypeList<>, true, false> {
    using Base = typename Zhttp::MessageTraits<Profile>::template Request<
      Builder, RequestHeaders, ZuTypeList<>, true, false>;

    template <typename L>
    void operation(L &&l) const { l(Zhttp::Method::POST, "/"); }
    template <typename L>
    void host(L &&l) const { l("localhost"); }
    template <typename Key, typename L>
    void header(L &&l) const {
      if constexpr (Key{}() == "content-length") l("1");
    }
  };

  struct Link :
    public Zhttp::ClientLink<Client, Link, Profile> {
    using Base = Zhttp::ClientLink<Client, Link, Profile>;
    using Base::Base;

    unsigned id = 0;
  };

  Client(State *state_) : state{state_} { }

  void connected(Link &link, const Zhttp::ConnectedInfo &) {
    state->connected.post();
    if (state->openRequest) {
      Builder builder;
      auto tx = link.transmit(builder);
      builder.begin(tx);
      if (state->completeRequest) {
	auto body = builder.body(tx, 1);
	body << 'x';
	body.flush();
	builder.finish(tx);
	link.finish();
      }
    }
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
  Workload workload{&state};
  Workload::Request request{&state};
  Zhttp::RequestMeta meta;
  bool buildersOK = true;
  bool resetsOK = true;
  for (unsigned kind = ResponseKind::None;
      kind <= ResponseKind::OptionalStream; ++kind) {
    state.responseKind = kind;
    state.responseResetCount = 0;
    unsigned seen = unsigned(-1);
    workload.respond(meta, request,
      [&seen]<typename Response>(Response &&response) {
	auto policy = response.bodyPolicy();
	response.reset();
	(void)response.close();
	(void)response.status();
	response.reason([](auto &&) { });
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
      });
    if (seen != kind) buildersOK = false;
    if (state.responseResetCount.load_() != 1) resetsOK = false;
  }
  ZuCHECK(buildersOK,
    "workload emits the selected concrete response Builder");
  ZuCHECK(resetsOK,
    "response Builder reset precedes common callbacks");
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
  state.port = Zhttp::Test::loopbackPort();
  state.expectedTransport = Zhttp::Transport::QUIC;
  ZuCHECK(state.port, "allocate loopback port");
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};
  bool mxUp = mx.start();
  ZuCHECK(mxUp, "start multiplexer");
  if (!mxUp) return;
  Zhttp::HubConfig hub{&mx, "3", "4"};

  Workload workload{&state};
  Server server;
  auto serverQUIC = Zhttp::QUICConfig{}.certPath(cert).keyPath(key);
  auto serverConfig = Zhttp::ServerConfig()
    .localIP(ZiIP{"127.0.0.1"}).port(state.port)
    .idleTimeout(1).quic(ZuMv(serverQUIC));
  bool serverInited =
    server.init(hub, ZuMv(serverConfig), &workload);
  ZuCHECK(serverInited, "initialize server");
  bool serverUp = serverInited && server.start();
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
  using ClientLink = typename Client<Zhttp::H3QUIC>::Link;
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
      ZuCHECK(server.activeConnections() == 1,
	"server accounts for active H3 link");
      ZuCHECK(state.disconnected.timedwait(Zm::now(10)) == 0,
	"QUIC transport idle timeout disconnects client");
      ZuCHECK(state.released.timedwait(Zm::now(10)) == 0,
	"server reports idle H3 admission release");
      ZuCHECK(!server.activeConnections(),
	"idle H3 link releases server admission");
    }
  }

  link = nullptr;
  bool clientStopped = !clientInited || client.stop();
  ZuCHECK(clientStopped, "stop client after idle expiry");
  bool serverStopped = !serverInited || server.stop();
  ZuCHECK(serverStopped, "stop server after idle expiry");
  if (clientInited) client.final();
  if (serverInited) server.final();
  ZiResolver::stop();
  ZiResolver::final();
  mx.stop();
  ZuCHECK(!state.errors.load_(), "idle expiry has no transport error");
}

template <
  typename Profile, bool Async = false, bool Limit = false, bool Sync = false>
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
  state.port = Zhttp::Test::loopbackPort();
  using HTTP = Zhttp::ProfileTraits<Profile>;
  using Protocol = typename HTTP::Protocol;
  state.expectedTransport = HTTP::Transport::ID;
  state.openRequest = true;
  state.asyncResponse = Async;
  state.completeRequest = Async || Sync;
  ZuCHECK(state.port, "allocate loopback port");
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};
  bool mxUp = mx.start();
  ZuCHECK(mxUp, "start multiplexer");
  if (!mxUp) return;
  Zhttp::HubConfig hub{&mx, "3", "4"};

  Workload workload{&state};
  Server server;
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
    server.init(hub, ZuMv(serverConfig), &workload);
  ZuCHECK(serverInited, "initialize server");
  bool serverUp = serverInited && server.start();
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
  using ClientLink = typename Client<Profile>::Link;
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
    ZuCHECK(server.activeConnections() == 1,
	"server accounts for active link");
  bool requestStarted = admitted &&
    state.requestStarted.timedwait(Zm::now(10)) == 0;
  ZuCHECK(requestStarted, "server admits partial request");
  if (requestStarted)
    ZuCHECK(state.completeRequest || server.activeRequests() == 1,
	"server accounts for admitted request");
  bool producerStarted = !Async ||
    state.producerStarted.timedwait(Zm::now(10)) == 0;
  ZuCHECK(producerStarted, "asynchronous body producer starts");
  if constexpr (Async)
    ZuCHECK(server.retainedBytes() == 17,
	"asynchronous producer respects aggregate retained-byte limit");
  if constexpr (Async)
    ZuCHECK(state.responseResetCount.load_() == 1,
	"server resets asynchronous response exactly once before use");
  bool responseStarted = !Sync ||
    state.responseStarted.timedwait(Zm::now(10)) == 0;
  ZuCHECK(responseStarted, "synchronous response construction starts");
  if constexpr (Sync)
    ZuCHECK(state.responseResetCount.load_() == 1,
	"server resets synchronous response exactly once before use");

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
    ZuCHECK(server.activeRequests() == 1 &&
	server.activeConnections() == 1,
	"request limit preserves the admitted request only");
    ZuCHECK(server.rejectedRequests() == 1,
	"request-limit refusal is accounted");
    ZuCHECK(state.requestStarted.trywait() != 0,
	"request-limit refusal allocates no application request");
  }

  ZmAtomic<unsigned> stopReturned = 0;
  constexpr unsigned ExpectedReleases = Limit ? 2 : 1;
  if (serverInited) server.stop([
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
  bool stoppedEarly = false;
  if constexpr (Async) {
    if (state.stopped.trywait() == 0)
      stoppedEarly = state.stopCount.load_();
    ZuCHECK(!stoppedEarly,
	"server stop waits for outstanding body producer");
    bool cancelled = state.completedCount.load_();
    if (!cancelled) {
      auto timeout = Zm::now(10);
      do {
	cancelled = state.completedCount.load_();
      } while (!cancelled && state.completed.timedwait(timeout) == 0);
    }
    ZuCHECK(cancelled,
	"server stop cancels body production before producer release");
    ProducerDone done{ZuMv(state.producerDone)};
    if (done) done(ZmRef<ZiIOBuf>{}, false);
  }
  bool stopped = !serverInited || stoppedEarly;
  if (!stopped) {
    auto timeout = Zm::now(10);
    do {
      if (state.stopCount.load_()) {
	stopped = true;
	break;
      }
    } while (state.stopped.timedwait(timeout) == 0);
  }
  if (!stopped && serverInited) (void)server.stop();
  ZuCHECK(stopped, "active server stop completes");
  ZuCHECK(state.stopCount.load_() == 1,
    "active server stop callback completes exactly once");
  ZuCHECK(!state.stopBeforeRelease.load_(),
    "active server admission releases before stop completion");
  ZuCHECK(state.completedCount.load_() == 1,
    "active server stop completes partial request exactly once");
  ZuCHECK(server.activeRequests() == 0,
	"active server stop releases request admission");
  ZuCHECK(!server.serverFaults() && !server.parseFailures() &&
      !server.responseBuildFailures(),
    "normal shutdown does not report server, parse, or build failures");
  ZuCHECK(server.transportFailures() == unsigned(!state.completeRequest),
    "partial-request reset is the only transport failure");
  if (clientInited) (void)client.stop();
  ZuCHECK(state.disconnected.timedwait(Zm::now(10)) == 0,
    "client stop completes active disconnect");
  ZuCHECK(server.activeConnections() == 0,
	"active server stop drains admission");

  link = nullptr;
  overflowLink = nullptr;
  if (clientInited) client.final();
  if (serverInited) server.final();
  ZiResolver::stop();
  ZiResolver::final();
  mx.stop();
  ZuCHECK(!state.errors.load_(), "active server stop has no error");
}

} // namespace ZhttpServerIdleTest_

int main(int argc, char **argv)
{
  using namespace ZhttpServerIdleTest_;

  (void)argc;
  (void)argv;
  ZuTestMain();
  ZuTestCall(responseTypes);
  ZuTestCall(idle);
  ZuTestCall((activeStop<Zhttp::H1TCP>));
  ZuTestCall((activeStop<Zhttp::H1TLS>));
  ZuTestCall((activeStop<Zhttp::H2TLS>));
  ZuTestCall((activeStop<Zhttp::H3QUIC>));
  ZuTestCall((activeStop<Zhttp::H1TCP, true>));
  ZuTestCall((activeStop<Zhttp::H1TCP, false, false, true>));
  ZuTestCall((activeStop<Zhttp::H1TLS, false, false, true>));
  ZuTestCall((activeStop<Zhttp::H1TCP, false, true>));
  return 0;
}
