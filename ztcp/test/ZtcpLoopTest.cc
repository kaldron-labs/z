//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <sys/socket.h>
#include <unistd.h>

#include <iostream>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/Ztcp.hh>

using namespace ZuTestUtil;

#define ZTCP_CHECK_RT(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))

namespace {

constexpr ZuString Ping{"ping\r\n"};
constexpr ZuString Pong{"pong\r\n"};

struct State {
  ZmSemaphore		done;
  ZmSemaphore		listening;
  ZmAtomic<unsigned>	errors{0};
  ZmAtomic<unsigned>	doneCount{0};
  ZmAtomic<unsigned>	clientConnected{0};
  ZmAtomic<unsigned>	serverConnected{0};
  ZiIP			ip;
  unsigned		port = 0;

  void fail() {
    errors = 1;
    done.post();
  }
  void doneOne() {
    if (doneCount.xchAdd(1) < 1) done.post();
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
	.thread(4, [](auto &t) { t.isolated(1); }); })
    .rxThread(1).txThread(2);
}

template <typename Link>
void sendBytes(Link &link, ZuCSpan s)
{
  auto tx = link.txStream();
  tx << s << Zi::flush();
}

bool consume(Ztcp::RxStream &rx, ZuCSpan expected)
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

uint16_t reserveLoopbackPort(ZiIP ip)
{
  int s = ::socket(ip.v6() ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s < 0) return 0;
  ZiSockAddr addr{ip, 0};
  uint16_t port = 0;
  if (!::bind(s, addr.sa(), addr.len())) {
    socklen_t len = addr.len();
    if (!::getsockname(s, addr.sa(), &len))
      port = addr.port();
  }
  ::close(s);
  return port;
}

struct ClientApp : public Ztcp::Client<ClientApp> {
  struct Link;

  ClientApp(State *state_) : state{state_} { }

  unsigned reconnFreq() const { return 0; }

  State *state = nullptr;
};

struct ClientApp::Link : public Ztcp::CliLink<ClientApp, Link> {
  using Base = Ztcp::CliLink<ClientApp, Link>;

  Link(ClientApp *app) : Base(app) { }

  void connected(Ztcp::Connected) {
    ZTCP_CHECK_RT(this->stream() == this, "client bidi stream is not link");
    typename Link::StreamRef stream = this->stream();
    ZTCP_CHECK_RT(stream, "client stream ref is null");
    auto tx = stream->txStream();
    (void)tx;
    app()->state->clientConnected = 1;
    sendBytes(*this, Ping);
  }
  void disconnected(bool) { }

  int process(Ztcp::RxStream &rx) {
    if (!consume(rx, Pong)) return 0;
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
    state->port = info.port;
    state->listening.post();
  }
  void listenFailed(bool) { state->fail(); }

  State *state = nullptr;
};

struct ServerApp::Link : public Ztcp::SrvLink<ServerApp, Link> {
  using Base = Ztcp::SrvLink<ServerApp, Link>;

  Link(ServerApp *app) : Base(app) { }

  void connected(Ztcp::Connected) {
    ZTCP_CHECK_RT(this->stream() == this, "server bidi stream is not link");
    typename Link::StreamRef stream = this->stream();
    ZTCP_CHECK_RT(stream, "server stream ref is null");
    auto tx = stream->txStream();
    (void)tx;
    app()->state->serverConnected = 1;
  }
  void disconnected(bool) { }

  int process(Ztcp::RxStream &rx) {
    if (!consume(rx, Ping)) return 0;
    sendBytes(*this, Pong);
    return 1;
  }
};

ZiConnection *ServerApp::accepted(const ZiCxnInfo &ci)
{
  return new Link::Cxn(new Link(this), ci);
}

void testLoop(const char *testName, ZiIP ip, const char *connectIP)
{
  ZuTestScopeRT(testName);

  State state;
  state.ip = ip;
  state.port = reserveLoopbackPort(ip);
  if (ip.v6() && !state.port) {
    std::cout << "# IPv6 loopback unavailable; skipping " << testName << '\n';
    return;
  }
  ZTCP_CHECK_RT(state.port, "failed to reserve loopback port");
  if (!state.port) return;

  ZiMultiplex mx(mxParams());
  bool mxStarted = mx.start();
  ZTCP_CHECK_RT(mxStarted, "ZiMultiplex start failed");
  if (!mxStarted) return;

  ServerApp server(&state);
  ClientApp client(&state);
  ZTCP_CHECK_RT(server.init(Ztcp::ServerParams(&mx, "3", "4")),
    "server init failed");
  ZTCP_CHECK_RT(client.init(Ztcp::ClientParams(&mx, "3", "4")),
    "client init failed");

  server.listen();
  ZTCP_CHECK_RT(state.listening.timedwait(Zm::now(5)) == 0,
    "server listen timed out");

  ZmRef<ClientApp::Link> link = new ClientApp::Link(&client);
  link->connect(connectIP, state.port);

  ZTCP_CHECK_RT(state.done.timedwait(Zm::now(5)) == 0, "loopback timed out");
  ZTCP_CHECK_RT(!state.errors.load_(), "loopback error");
  ZTCP_CHECK_RT(state.doneCount.load_() == 1, "client did not receive response");
  ZTCP_CHECK_RT(state.clientConnected.load_(), "client did not connect");
  ZTCP_CHECK_RT(state.serverConnected.load_(), "server did not connect");

  server.stopListening();
  client.final();
  server.final();
  mx.stop();
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiLog::init("ZtcpLoopTest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuTestMain();
  ZuTestCall(testLoop, "testLoopIPv4", ZiIP{"127.0.0.1"}, "127.0.0.1");
  ZuTestCall(testLoop, "testLoopIPv6", ZiIP{"::1"}, "::1");
  ZiLog::stop();
  return 0;
}
