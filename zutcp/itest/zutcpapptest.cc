//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <iostream>
#include <net/if.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSemaphore.hh>
#ifdef ZUTCP_APP_TLS
#include <zlib/Ztls.hh>
#else
#include <zlib/Ztcp.hh>
#endif
#include <zlib/Zutcp.hh>
#include "ZiTestResidue.hh"

using namespace ZuTestUtil;

#ifdef ZUTCP_APP_TLS
namespace AppIO = Ztls;
constexpr auto Protocol = "Ztls"_z;
constexpr auto Program = "zutcptlsapptest"_z;
#else
namespace AppIO = Ztcp;
constexpr auto Protocol = "Ztcp"_z;
constexpr auto Program = "zutcpapptest"_z;
#endif

struct AppState {
  bool native;
  ZmSemaphore done;
  ZmAtomic<unsigned> errors = 0, connected = 0, received = 0;
  ZtString<ZtStringHeapID<"Zutcp.App.Payload">> payload;

  AppState(bool native_) : native{native_} {
    for (unsigned i = 0; i < 4096; ++i) payload << char('a' + i % 26);
  }
};

static bool consume(AppIO::RxStream &rx, AppState *state)
{
  unsigned remaining = state->payload.length();
  return rx.consume([&remaining](auto part) -> int64_t {
    if (remaining > part.length()) { remaining -= part.length(); return 0; }
    return remaining;
  }, [state](auto part) {
    if (part.length() != state->payload.length() ||
        memcmp(part.data(), state->payload.data(), part.length())) ++state->errors;
    ++state->received;
  }) > 0;
}

template <typename Link> static void connected(Link *link, AppIO::Connected info)
{
  auto state = link->app()->state;
  auto cxn = link->cxn();
  if (!link->app()->rxInvoked() || !cxn || !cxn->up() ||
      bool(cxn->plugin()) != state->native ||
      Zi::nullSocket(cxn->info().socket) != state->native) ++state->errors;
#ifdef ZUTCP_APP_TLS
  if (!info.version || info.alpn != "zutcp-test") ++state->errors;
#endif
  ++state->connected;
}

struct ClientApp : public AppIO::Client<ClientApp> {
  struct Link;
  AppState *state;
  ClientApp(AppState *state_) : state{state_} { }
  unsigned reconnFreq() const { return 0; }
};

struct ClientApp::Link : public ZmNHeap<Link>, public AppIO::CliLink<ClientApp, Link> {
friend inline ZuStringT<"Zutcp.App.Client"> ZmNHeapID(Link *);
  using Base = AppIO::CliLink<ClientApp, Link>;
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete;
  Link(ClientApp *app) : Base{app} { }
  void connected(AppIO::Connected info) {
    ::connected(this, info);
    auto tx = txStream();
    tx << app()->state->payload << Zi::flush();
  }
  int process(AppIO::RxStream &rx) {
    if (!consume(rx, app()->state)) return 0;
    disconnect();
    return 1;
  }
  void disconnected(bool) {
    app()->state->done.post();
  }
  void connectFailed(bool) { ++app()->state->errors; app()->state->done.post(); }
};

struct ServerApp : public AppIO::Server<ServerApp> {
  struct Link;
  AppState *state;
  ZiIP ip;
  uint16_t port;
  ServerApp(AppState *state_, ZiIP ip_, uint16_t port_) : state{state_}, ip{ip_}, port{port_} { }
  ZiIP localIP() const { return ip; }
  uint16_t localPort() const { return port; }
  unsigned nAccepts() const { return 4; }
  ZiConnection *accepted(const ZiCxnInfo &);
  void listening(const ZiListenInfo &) { std::cerr << "READY\n" << std::flush; }
  void listenFailed(bool) { ++state->errors; state->done.post(); }
};

struct ServerApp::Link : public ZmNHeap<Link>, public AppIO::SrvLink<ServerApp, Link> {
friend inline ZuStringT<"Zutcp.App.Server"> ZmNHeapID(Link *);
  using Base = AppIO::SrvLink<ServerApp, Link>;
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete;
  Link(ServerApp *app) : Base{app} { }
  void connected(AppIO::Connected info) { ::connected(this, info); }
  int process(AppIO::RxStream &rx) {
    if (!consume(rx, app()->state)) return 0;
    auto tx = txStream();
    tx << app()->state->payload << Zi::flush();
    return 1;
  }
  void disconnected(bool) {
    app()->state->done.post();
  }
};

ZiConnection *ServerApp::accepted(const ZiCxnInfo &info)
{
  return new Link::Cxn{new Link{this}, info};
}

static bool service(
  bool server, bool native, unsigned interface, const char *peerText, unsigned port)
{
  AppState state{native};
  ZiIP peer{peerText};
  ZiMultiplex mx{ZiMxParams{}.rxBufSize(128).rxThread(1).txThread(2)
    .scheduler([](auto &s) {
      s.nThreads(5);
      for (unsigned i = 1; i <= 4; ++i) s.thread(i, [](auto &t) { t.isolated(1); });
    })};
  ZmRef<Zutcp::Stack> stack;
  if (native) {
    stack = new Zutcp::Stack{&mx, {.interface = interface}};
    mx.plugin(stack);
    ZtString<> error;
    if (!stack->configure({.rxRetainFrames = 96, .rxRetainFramesPerCxn = 64,
        .rxReserveFrames = 16}, error)) return false;
  }
#ifdef ZUTCP_APP_TLS
  ZuCSpan alpn[] = {"zutcp-test"};
#endif
  auto txCapacity = native ? stack->txAdmission().capacity : 0;
  bool ok = false;
  if (server) {
    ServerApp app{&state, peer, uint16_t(port)};
    AppIO::ServerParams params{&mx, "3", "4"};
    params.rxAllocator(ServerApp::Link::rxAllocator(&mx))
      .txAllocator(ServerApp::Link::txAllocator(&mx));
#ifdef ZUTCP_APP_TLS
    params.alpn(alpn).certPath(getenv("ZUTCP_TEST_CERT")).keyPath(getenv("ZUTCP_TEST_KEY"))
      .txPacketAllocator(ServerApp::Link::txPacketAllocator(&mx));
#endif
    bool initialized = app.init(ZuMv(params));
    if (native && stack->txAdmission().capacity != txCapacity + ZiIOBuf_DefltCount)
      ++state.errors;
    if (!initialized || !mx.start() || !app.start()) return false;
    app.listen();
    ok = !state.done.timedwait(Zm::now() + ZuTime{20});
    ok = app.stop() && ok;
    app.final();
    ok = mx.stop() && ok;
  } else {
    ClientApp app{&state};
    AppIO::ClientParams params{&mx, "3", "4"};
    params.rxAllocator(ClientApp::Link::rxAllocator(&mx))
      .txAllocator(ClientApp::Link::txAllocator(&mx));
#ifdef ZUTCP_APP_TLS
    params.alpn(alpn).caPath(getenv("ZUTCP_TEST_CERT"))
      .txPacketAllocator(ClientApp::Link::txPacketAllocator(&mx));
#endif
    bool initialized = app.init(ZuMv(params));
    if (native && stack->txAdmission().capacity != txCapacity + ZiIOBuf_DefltCount)
      ++state.errors;
    if (!initialized || !mx.start()) return false;
    ZmRef<ClientApp::Link> link = new ClientApp::Link{&app};
    link->connect(AppIO::Host{peerText}, port);
    ok = !state.done.timedwait(Zm::now() + ZuTime{20});
    if (!ok)
      std::cerr << "WAIT errors=" << unsigned(state.errors) <<
	" connected=" << unsigned(state.connected) <<
	" received=" << unsigned(state.received) << '\n';
    app.final();
    ok = mx.stop() && ok;
    link = nullptr;
  }
  bool valid = ok && !unsigned(state.errors) && state.connected == 1 && state.received == 1;
  if (!valid)
    std::cerr << "RESULT ok=" << ok << " errors=" << unsigned(state.errors) <<
      " connected=" << unsigned(state.connected) <<
      " received=" << unsigned(state.received) << '\n';
  return valid;
}

static void testApp(bool v6, bool native)
{
  ZuTestScopeRT(testApp);
  auto directory = ZiTestResidue::dir(native ?
    (v6 ? "native-v6" : "native-v4") : (v6 ? "kernel-v6" : "kernel-v4"));
  auto child = fork();
  if (!child) {
    execl("/bin/bash", "bash", "./zutcpappfixture.sh", v6 ? "6" : "4",
      directory.data(), Protocol.data(), native ? "native" : "kernel",
      static_cast<char *>(nullptr));
    _exit(127);
  }
  int status = -1;
  if (child > 0) {
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    if (result != child) status = -1;
  }
  ZuCheckRT(!status, log_(Protocol, " application exchange/drain over ",
    native ? "native" : "kernel", " IPv", v6 ? 6 : 4, "; logs ", directory));
}

int main(int argc, char **argv)
{
  if (argc == 5 && (!strcmp(argv[1], "--server") || !strcmp(argv[1], "--client") ||
      !strcmp(argv[1], "--kernel-server") || !strcmp(argv[1], "--kernel-client"))) {
    ZiLog::init(Program.data());
    ZiLog::level(0);
    ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
    ZiLog::start();
    bool native = !strcmp(argv[1], "--server") || !strcmp(argv[1], "--client");
    bool server = !strcmp(argv[1], "--server") || !strcmp(argv[1], "--kernel-server");
    bool ok = service(server, native, if_nametoindex(argv[2]),
      argv[3], ZuBox<unsigned>{argv[4]});
    ZiLog::stop();
    std::cout << "TAP version 14\n1..1\n" << (ok ? "ok" : "not ok") <<
      " 1 - " << (native ? "native " : "kernel ") << Protocol <<
      " application byte exchange and drain\n";
    return !ok;
  }
  parse(argc, argv);
  if (geteuid() || !getenv("ZXDP_RUN_FIXTURE")) {
    std::cout << "1..0 # SKIP requires private namespace fixture\n";
    return 0;
  }
  ZiTestResidue::init(Program.data());
  ZuTestMain();
  ZuTestCall(testApp, false, true);
  ZuTestCall(testApp, true, true);
  ZuTestCall(testApp, false, false);
  ZuTestCall(testApp, true, false);
}
