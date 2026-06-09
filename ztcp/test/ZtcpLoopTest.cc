//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

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

uint16_t reserveLoopbackPort()
{
  int s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s < 0) return 0;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  uint16_t port = 0;
  if (!::bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr))) {
    socklen_t len = sizeof(addr);
    if (!::getsockname(s, reinterpret_cast<sockaddr *>(&addr), &len))
      port = ntohs(addr.sin_port);
  }
  ::close(s);
  return port;
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

struct ClientApp : public Ztcp::Client<ClientApp> {
  struct Link;

  ClientApp(State *state_) : state{state_} { }

  unsigned reconnFreq() const { return 0; }

  State *state = nullptr;
};

struct ClientApp::Link : public Ztcp::CliLink<ClientApp, Link> {
  using Base = Ztcp::CliLink<ClientApp, Link>;

  Link(ClientApp *app) : Base(app) { }

  void connected() { sendBytes(*this, Ping); }
  void disconnected() { }

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

  ZiIP localIP() const { return ZiIP("127.0.0.1"); }
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

  void connected() { }
  void disconnected() { }

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

void testLoop()
{
  ZuTestScopeRT(testLoop);

  State state;
  state.port = reserveLoopbackPort();
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
  link->connect("127.0.0.1", state.port);

  ZTCP_CHECK_RT(state.done.timedwait(Zm::now(5)) == 0, "loopback timed out");
  ZTCP_CHECK_RT(!state.errors.load_(), "loopback error");
  ZTCP_CHECK_RT(state.doneCount.load_() == 1, "client did not receive response");

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
  ZuTestCall(testLoop);
  ZiLog::stop();
  return 0;
}
