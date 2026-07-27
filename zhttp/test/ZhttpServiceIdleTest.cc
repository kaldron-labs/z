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
  struct Response { };

  Workload(State *state_) : state{state_} { }

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

  Response request(const Zhttp::RequestInfo &) {
    state->fail();
    return {};
  }
  unsigned status(const Response &) const { return 200; }
  template <typename L>
  void reason(const Response &, L &&l) const { l("OK"); }
  uint64_t contentLength(const Response &) const { return 0; }
  template <typename Key, typename L>
  void header(const Response &, L &&l) const { l(""); }
  template <typename Tx, typename Builder>
  void body(Tx &, Builder &, const Response &) { }
  void complete(const Zhttp::RequestInfo &, const Response &) { }
  bool close(const Response &) const { return false; }

  State	*state;
};

using Service = Zhttp::Service<
  Workload, ZuTypeList<>, ZuTypeList<>, 1024>;

template <typename Protocol>
struct Client : public Zhttp::Client<Client<Protocol>, Protocol> {
  struct Builder :
    public Zhttp::MessageTraits<Protocol>::template Builder<
      Builder, ZuTypeList<>, ZuTypeList<>, true, false> {
    using Base = typename Zhttp::MessageTraits<Protocol>::template Builder<
      Builder, ZuTypeList<>, ZuTypeList<>, true, false>;

    template <typename L>
    void operation(L &&l) const { l(Zhttp::Method::POST, "/", ""); }
    template <typename L>
    void host(L &&l) const { l("localhost"); }
    uint64_t contentLength() const { return 1; }
    template <typename Key, typename L>
    void header(L &&l) const { l(""); }
  };

  struct Link :
    public Zhttp::ClientLink<Client, Link, Protocol> {
    using Base = Zhttp::ClientLink<Client, Link, Protocol>;
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
  int process(
      Link &, typename Zhttp::Transport_::Traits<Protocol>::RxStream &) {
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
  Zhttp::EngineConfig engine{&mx, "3", "4"};

  Workload workload{&state};
  Service service;
  auto serverQUIC = Zhttp::QUICConfig{}.certPath(cert).keyPath(key);
  Zhttp::ServiceConfig serviceConfig;
  serviceConfig
    .localIP(ZiIP{"127.0.0.1"}).port(state.port)
    .idleTimeout(1).quic(ZuMv(serverQUIC));
  bool serviceInited =
    service.init(engine, ZuMv(serviceConfig), &workload);
  ZuCHECK(serviceInited, "initialize service");
  bool serviceUp = serviceInited && service.start();
  ZuCHECK(serviceUp, "start service");
  bool listening = serviceUp &&
    state.listening.timedwait(Zm::now(10)) == 0;
  ZuCHECK(listening, "service listens for HTTP/3");

  Client<Zhttp::QUIC> client{&state};
  bool clientInited = listening && client.init(
    engine, Zhttp::QUICConfig{}.caPath(cert).maxIdleTimeout(10000));
  ZuCHECK(clientInited, "initialize client");
  bool clientUp = clientInited && client.start();
  ZuCHECK(clientUp, "start client");
  using ClientLink = typename Client<Zhttp::QUIC>::Link;
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

template <typename Protocol>
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
  state.expectedTransport = Zhttp::Transport_::Traits<Protocol>::ID;
  state.openRequest = true;
  ZuCHECK(state.port, "allocate loopback port");
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};
  bool mxUp = mx.start();
  ZuCHECK(mxUp, "start multiplexer");
  if (!mxUp) return;
  Zhttp::EngineConfig engine{&mx, "3", "4"};

  Workload workload{&state};
  Service service;
  Zhttp::ServiceConfig serviceConfig;
  serviceConfig.localIP(ZiIP{"127.0.0.1"}).port(state.port);
  if constexpr (ZuIsSame<Protocol, Zhttp::TCP>{})
    serviceConfig.tcp();
  else if constexpr (ZuIsSame<Protocol, Zhttp::TLS>{})
    serviceConfig.tls(
      Zhttp::TLSConfig{}.certPath(cert).keyPath(key));
  else
    serviceConfig.quic(
      Zhttp::QUICConfig{}.certPath(cert).keyPath(key).maxIdleTimeout(10000));
  bool serviceInited =
    service.init(engine, ZuMv(serviceConfig), &workload);
  ZuCHECK(serviceInited, "initialize service");
  bool serviceUp = serviceInited && service.start();
  ZuCHECK(serviceUp, "start service");
  bool listening = serviceUp &&
    state.listening.timedwait(Zm::now(10)) == 0;
  ZuCHECK(listening, "service listens for HTTP/3");

  Client<Protocol> client{&state};
  bool clientInited = false;
  if constexpr (ZuIsSame<Protocol, Zhttp::TCP>{})
    clientInited = listening && client.init(engine, Zhttp::TCPConfig{});
  else if constexpr (ZuIsSame<Protocol, Zhttp::TLS>{})
    clientInited =
      listening && client.init(engine, Zhttp::TLSConfig{}.caPath(cert));
  else
    clientInited = listening && client.init(
      engine, Zhttp::QUICConfig{}.caPath(cert).maxIdleTimeout(10000));
  ZuCHECK(clientInited, "initialize client");
  bool clientUp = clientInited && client.start();
  ZuCHECK(clientUp, "start client");
  using ClientLink = typename Client<Protocol>::Link;
  ZmRef<ClientLink> link;
  if (clientUp) link = new ClientLink{&client};
#ifdef ZmObject_DEBUG
  if (link) {
    if constexpr (ZuIsSame<Protocol, Zhttp::QUIC>{})
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
  ZuTestCall((activeStop<Zhttp::TCP>));
  ZuTestCall((activeStop<Zhttp::TLS>));
  ZuTestCall((activeStop<Zhttp::QUIC>));
  return 0;
}
