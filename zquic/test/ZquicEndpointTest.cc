//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>
#include <unistd.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZquicEndpoint.hh>

using namespace ZuTestUtil;

template <typename L>
bool waitUntil(L l)
{
  for (unsigned i = 0; i < 2000; ++i) {
    if (l()) return true;
    usleep(1000);
  }
  return false;
}

void closeEndpoint(Zquic::Endpoint &ep)
{
  ZmSemaphore done;
  ep.closeUDP(Zquic::Endpoint::CloseFn{[&done]() { done.post(); }});
  done.wait();
}

void testEndpointReadyDownCallbacks()
{
  ZuTestScope(testEndpointReadyDownCallbacks);

  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(4)
	    .thread(1, [](auto &t) { t.isolated(1); })
	    .thread(2, [](auto &t) { t.isolated(1); })
	    .thread(3, [](auto &t) { t.isolated(1); })
	    .thread(4, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));

  bool mxStarted = mx.start();
  ZuCHECK(mxStarted, "endpoint callback multiplexer start failed");
  if (!mxStarted) return;

  Zquic::Endpoint ep;
  bool ready = false;
  bool down = false;
  bool failed = false;
  ZuCHECK(ep.init(&mx), "endpoint init failed");
  ZuCHECK(ep.openUDP(
      Zquic::PathMode::ServerUnconnected,
      ZiIP("127.0.0.1"), 0, ZiIP{}, 0,
      Zquic::Endpoint::DatagramFn{},
      Zquic::Endpoint::ReadyFn{[&ready](Zquic::Endpoint *) {
	ready = true;
      }},
      Zquic::Endpoint::FailFn{[&failed](bool) { failed = true; }},
      Zquic::Endpoint::DownFn{[&down](Zquic::Endpoint *) {
	down = true;
      }}), "endpoint open failed");

  bool opened = waitUntil([&ep, &ready, &failed]() {
      return failed || (ready && ep.listening() && ep.local().port());
    });
  ZuCHECK(opened, "endpoint ready/fail callback did not fire");
  ZuCHECK(!failed || (!ep.listening() && ep.diag().failures),
    "endpoint open did not fail cleanly");

  uint64_t failures = ep.diag().failures;
  closeEndpoint(ep);
  ZuCHECK(waitUntil([&down]() { return down; }),
    "endpoint down callback did not fire");
  ZuCHECK(ep.diag().failures == failures,
    "endpoint callback test changed failure counter");

  mx.stop();
}

void testDatagramOwnership()
{
  ZuTestScope(testDatagramOwnership);

  Zquic::Endpoint ep;
  bool seen = false;
  ep.datagramFn([&seen](Zquic::Datagram d) {
    seen = true;
    ZuCHECK(d.buf && d.buf->length == 4, "datagram buffer length mismatch");
    ZuCHECK(!::memcmp(d.buf->data(), "PING", 4), "datagram payload mismatch");
    ZuCHECK(d.addr.port() == 4433, "datagram address was not preserved");
  });

  ZmRef<ZiIOBuf> tx = ep.allocTxPkt();
  ZuCHECK(tx, "endpoint Tx packet allocation failed");

  ZmRef<ZiIOBuf> buf = new Zquic::PktRxBufAlloc<>{&ep};
  *buf << "PING";
  ep.inject(Zquic::Datagram{ZuMv(buf), ZiSockAddr{ZiIP("127.0.0.1"), 4433}});

  ZuCHECK(seen, "datagram callback not invoked");
  ZuCHECK(ep.diag().datagramsRx == 1, "datagram Rx counter mismatch");
  ZuCHECK(ep.diag().bytesRx == 4, "datagram byte counter mismatch");
}

void testCloseDrainsQueuedSend()
{
  ZuTestScope(testCloseDrainsQueuedSend);

  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(4)
	    .thread(1, [](auto &t) { t.isolated(1); })
	    .thread(2, [](auto &t) { t.isolated(1); })
	    .thread(3, [](auto &t) { t.isolated(1); })
	    .thread(4, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));

  bool mxStarted = mx.start();
  ZuCHECK(mxStarted, "endpoint close-drain multiplexer start failed");
  if (!mxStarted) return;

  Zquic::Endpoint srv;
  Zquic::Endpoint cli;
  bool srvReady = false;
  bool cliReady = false;
  bool cliDown = false;
  ZuCHECK(srv.init(&mx), "server endpoint init failed");
  ZuCHECK(cli.init(&mx), "client endpoint init failed");
  ZuCHECK(srv.openUDP(
      Zquic::PathMode::ServerUnconnected,
      ZiIP("127.0.0.1"), 0, ZiIP{}, 0,
      Zquic::Endpoint::DatagramFn{},
      Zquic::Endpoint::ReadyFn{[&srvReady](Zquic::Endpoint *) {
	srvReady = true;
      }}), "server endpoint open failed");
  ZuCHECK(waitUntil([&srv, &srvReady]() {
      return srvReady && srv.listening() && srv.local().port();
    }), "server endpoint did not become ready");
  ZuCHECK(cli.openUDP(
      Zquic::PathMode::ClientConnected,
      ZiIP{}, 0, srv.local().ip(), srv.local().port(),
      Zquic::Endpoint::DatagramFn{},
      Zquic::Endpoint::ReadyFn{[&cliReady](Zquic::Endpoint *) {
	cliReady = true;
      }},
      Zquic::Endpoint::FailFn{},
      Zquic::Endpoint::DownFn{[&cliDown](Zquic::Endpoint *) {
	cliDown = true;
      }}), "client endpoint open failed");
  bool connected = waitUntil([&cliReady]() { return cliReady; });
  ZuCHECK(connected, "client endpoint did not connect");
  if (!connected) {
    closeEndpoint(cli);
    closeEndpoint(srv);
    mx.stop();
    return;
  }

  unsigned sent = 0;
  for (unsigned i = 0; i < 256; ++i) {
    auto buf = cli.allocTxPkt();
    *buf << "PING";
    if (cli.send(ZuMv(buf), srv.local())) ++sent;
  }
  ZuCHECK(sent, "client endpoint did not queue any sends");

  closeEndpoint(cli);
  ZuCHECK(!cli.connected(), "client endpoint remained connected after close");
  ZuCHECK(waitUntil([&cliDown]() { return cliDown; }),
    "client endpoint down callback did not fire during close");

  auto buf = cli.allocTxPkt();
  *buf << "PING";
  ZuCHECK(!cli.send(ZuMv(buf), srv.local()),
    "client endpoint accepted send after close drain");

  closeEndpoint(srv);
  mx.stop();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiLog::init("ZquicEndpointTest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuTestMain();
  ZuTestCall(testEndpointReadyDownCallbacks);
  ZuTestCall(testDatagramOwnership);
  ZuTestCall(testCloseDrainsQueuedSend);
  ZiLog::stop();
}
