//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Zhttp Service HTTP/3 idle-expiry test

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiResolver.hh>

#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpService.hh>

#include "ZhttpTestUtil.hh"

using namespace ZuTestUtil;

namespace ZhttpServiceIdleTest_ {

struct State {
  ZmSemaphore	listening;
  ZmSemaphore	admitted;
  ZmSemaphore	connected;
  ZmSemaphore	disconnected;
  ZmSemaphore	released;
  ZmSemaphore	stopped;
  ZmAtomic<unsigned> errors = 0;
  ZmAtomic<unsigned> releaseCount = 0;
  ZmAtomic<unsigned> stopCount = 0;
  ZmAtomic<unsigned> stopBeforeRelease = 0;
  uint16_t	port = 0;
  int8_t	expectedTransport = Zhttp::Transport::QUIC;
  bool		openRequest = false;

  void fail() {
    errors = 1;
    listening.post();
    admitted.post();
    connected.post();
    disconnected.post();
    released.post();
    stopped.post();
  }
};

struct Workload {
  struct Response {
    using Headers = ZuTypeList<>;
    using BodyPolicy = Zhttp::Body::None;
    unsigned status() const { return 200; }
    template <typename L> void reason(L &&l) const { l("OK"); }
    template <typename Key, typename L> void header(L &&) const { }
    template <typename L> void header(L &&) const { }
    bool close() const { return false; }
  };
  struct RequestParser {
    using Headers = ZuTypeList<>;
    static constexpr uint64_t BodyMax = 1024;
    void operation(Zhttp::Method::T, const Zhttp::RequestTarget &) { }
    void version(ZuBSpan) { }
    void contentLength(uint64_t) { }
    void chunked() { }
    template <typename Key> void header(ZuBSpan) { }
    template <typename Rx> void body(Rx &rx) { Zhttp::bodyDrain(rx); }
    void complete(bool) { }
  };

  Workload(State *state_) : state{state_} { }
  RequestParser requestParser() { return {}; }

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
  void response(const Zhttp::RequestInfo &, RequestParser &, Emit &&emit) {
    state->fail();
    emit(Response{}, false);
  }
  void complete(const Zhttp::RequestInfo &, bool &, bool) { }
  bool close(const Response &) const { return false; }

  State	*state;
};

using Service = Zhttp::Service<Workload>;

template <typename Profile>
struct Client : public Zhttp::ClientHub<Client<Profile>, Profile> {
  using RequestHeaders = ZuTypeList<
    ZuStringT<"content-length">, void>;

  struct Builder :
    public Zhttp::MessageTraits<Profile>::template RequestBuilder<
      Builder, RequestHeaders, ZuTypeList<>, true, false> {
    using Base = typename Zhttp::MessageTraits<Profile>::template RequestBuilder<
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
  };

  Client(State *state_) : state{state_} { }

  void connected(Link &link, const Zhttp::ConnectedInfo &) {
    state->connected.post();
    if (state->openRequest) {
      Builder builder;
      auto tx = link.transmit(builder);
      builder.request(tx);
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

void idle()
{
  ZuTestScope(idle);

  Zhttp::Test::TempDir temp;
  bool tempOK = temp.init("ZhttpServiceIdle");
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
  Service service;
  auto serverQUIC = Zhttp::QUICConfig{}.certPath(cert).keyPath(key);
  auto serviceConfig = Zhttp::ServiceConfig()
    .localIP(ZiIP{"127.0.0.1"}).port(state.port)
    .idleTimeout(1).quic(ZuMv(serverQUIC));
  bool serviceInited =
    service.init(hub, ZuMv(serviceConfig), &workload);
  ZuCHECK(serviceInited, "initialize service");
  bool serviceUp = serviceInited && service.start();
  ZuCHECK(serviceUp, "start service");
  bool listening = serviceUp &&
    state.listening.timedwait(Zm::now(10)) == 0;
  ZuCHECK(listening, "service listens for HTTP/3");

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
      ZuCHECK(service.active() == 1,
	"service accounts for active H3 link");
      ZuCHECK(state.disconnected.timedwait(Zm::now(10)) == 0,
	"QUIC transport idle timeout disconnects client");
      ZuCHECK(state.released.timedwait(Zm::now(10)) == 0,
	"service reports idle H3 admission release");
      ZuCHECK(!service.active(), "idle H3 link releases service admission");
    }
  }

  link = nullptr;
  bool clientStopped = !clientInited || client.stop();
  ZuCHECK(clientStopped, "stop client after idle expiry");
  bool serviceStopped = !serviceInited || service.stop();
  ZuCHECK(serviceStopped, "stop service after idle expiry");
  if (clientInited) client.final();
  if (serviceInited) service.final();
  ZiResolver::stop();
  ZiResolver::final();
  mx.stop();
  ZuCHECK(!state.errors.load_(), "idle expiry has no transport error");
}

template <typename Profile>
void activeStop()
{
  ZuTestScope(activeStop);

  Zhttp::Test::TempDir temp;
  bool tempOK = temp.init("ZhttpServiceStop");
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
  ZuCHECK(state.port, "allocate loopback port");
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};
  bool mxUp = mx.start();
  ZuCHECK(mxUp, "start multiplexer");
  if (!mxUp) return;
  Zhttp::HubConfig hub{&mx, "3", "4"};

  Workload workload{&state};
  Service service;
  auto serviceConfig = Zhttp::ServiceConfig()
    .localIP(ZiIP{"127.0.0.1"}).port(state.port);
  if constexpr (ZuIsSame<Protocol, Zhttp::TCP>{})
    serviceConfig.tcp();
  else if constexpr (ZuIsSame<Protocol, Zhttp::TLS>{})
    serviceConfig.tls(
      Zhttp::H2Config{}.certPath(cert).keyPath(key)
	.policy(Zhttp::H2Policy::Prefer));
  else
    serviceConfig.quic(
      Zhttp::QUICConfig{}.certPath(cert).keyPath(key).maxIdleTimeout(10000));
  bool serviceInited =
    service.init(hub, ZuMv(serviceConfig), &workload);
  ZuCHECK(serviceInited, "initialize service");
  bool serviceUp = serviceInited && service.start();
  ZuCHECK(serviceUp, "start service");
  bool listening = serviceUp &&
    state.listening.timedwait(Zm::now(10)) == 0;
  ZuCHECK(listening, "service listens for HTTP/3");

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
  ZuCHECK(admitted, "service admits active link");
  if (admitted)
    ZuCHECK(service.active() == 1, "service accounts for active link");

  ZmAtomic<unsigned> stopReturned = 0;
  if (serviceInited) service.stop([
    &state, &stopReturned
  ](bool ok) {
    if (!ok) state.errors = 1;
    if (!stopReturned.load_()) state.errors = 1;
    if (state.releaseCount.load_() != 1) ++state.stopBeforeRelease;
    ++state.stopCount;
    state.stopped.post();
  });
  stopReturned = 1;
  bool stopped = !serviceInited ||
    state.stopped.timedwait(Zm::now(10)) == 0;
  if (!stopped && serviceInited) (void)service.stop();
  ZuCHECK(stopped, "active service stop completes");
  ZuCHECK(state.stopCount.load_() == 1,
    "active service stop callback completes exactly once");
  ZuCHECK(!state.stopBeforeRelease.load_(),
    "active service admission releases before stop completion");
  if (clientInited) (void)client.stop();
  ZuCHECK(state.disconnected.timedwait(Zm::now(10)) == 0,
    "client stop completes active disconnect");
  ZuCHECK(service.active() == 0,
    "active service stop drains admission");

  link = nullptr;
  if (clientInited) client.final();
  if (serviceInited) service.final();
  ZiResolver::stop();
  ZiResolver::final();
  mx.stop();
  ZuCHECK(!state.errors.load_(), "active service stop has no error");
}

} // namespace ZhttpServiceIdleTest_

int main(int argc, char **argv)
{
  using namespace ZhttpServiceIdleTest_;

  (void)argc;
  (void)argv;
  ZuTestMain();
  ZuTestCall(idle);
  ZuTestCall((activeStop<Zhttp::H1TCP>));
  ZuTestCall((activeStop<Zhttp::H1TLS>));
  ZuTestCall((activeStop<Zhttp::H2TLS>));
  ZuTestCall((activeStop<Zhttp::H3QUIC>));
  return 0;
}
