//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmBlock.hh>

#include <zlib/Zutcp.hh>

#include "ZutcpTestAlloc.hh"
#include "ZutcpTestInstance.hh"

using namespace ZuTestUtil;

struct Endpoint : Zutcp_::Endpoint_<Endpoint> {
  enum { EndpointRef = false };
  void endpointReady(Endpoint *) { }
  void endpointDown_(Endpoint *) { }
  void endpointFailed(bool) { }
  void endpointDatagram(Zutcp::Datagram) { }
};

static void testStoppedInstance()
{
  ZuTestScope(testStoppedInstance);
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(4); })
    .rxThread(1).txThread(2)};
  {
    Zutcp::Instance cold;
    auto xdp = ZutcpTest::fixtureXdp(&mx);
    auto before = ZutcpTest::Instance::parameters(cold);
    auto invalid = before;
    invalid.maxCxns = 0;
    auto result = cold.init(&mx, ZuMv(xdp), ZuMv(invalid));
    auto admission = cold.txAdmission();
    auto after = ZutcpTest::Instance::parameters(cold);
    ZuCHECK(result.is<ZeException>() && !cold.ready() && !admission.capacity &&
        !admission.controlCount && !cold.mx() && xdp && after == before,
      "failed initial configuration leaves cold resources and policy untouched");
  }
  auto rx = ZutcpTest::pktRxAllocator(&mx);
  auto tx = ZutcpTest::pktTxAllocator(&mx);
  ZutcpTest::Instance stack{&mx};
  auto config = ZutcpTest::Instance::parameters(stack);
  auto first = ZutcpTest::Instance::apply(stack, config);
  ZuCheck(first.is<void>());
  if (first.is<ZeException>()) return;
  auto capacity = stack.txAdmission().capacity;
  auto allocator = stack.dataTxAllocator();
  Zutcp::ClosedTuple record{.key = {ZiSockAddr{ZiIP{"198.18.0.1"}, 18191},
    ZiSockAddr{ZiIP{"198.18.0.2"}, 18192}}, .txNext = 20, .rxNext = 30, .fin = true};
  record = stack.rememberClosed(record);
  auto repeated = ZutcpTest::Instance::apply(stack, config);
  Zutcp::ClosedTuple retained;
  ZuCHECK(first.is<void>() && repeated.is<void>() &&
      stack.txAdmission().capacity == capacity && stack.dataTxAllocator() == allocator &&
      stack.closedTuple(record.key, retained) && retained.id == record.id,
    "repeated stopped init preserves allocator identity, capacity and closed tuple evidence");
  Endpoint endpoint;
  ZuCHECK(endpoint.init(&stack), "endpoint uses its known owning Instance directly");
  ZuCHECK(!stack.ready() && !endpoint.connected() && !endpoint.listening(),
    "binding the known owner creates no transport endpoint or kernel fallback");
  ZuCHECK(rx->type == Zi::IOBufType::Rx && tx->type == Zi::IOBufType::TxTCP,
    "native protocol requests TCP network provider roles");
  stack.final();
  stack.final();
  ZuCHECK(stack.state() == ZmEngineState::Stopped && !stack.ready() &&
      !stack.txAdmission().capacity && !stack.closedTupleStats().count,
    "repeated final releases stopped configuration, pools and history");
}

static void testHandoff()
{
  ZuTestScope(testHandoff);
  ZiMultiplex mx;
  auto xdp = ZutcpTest::fixtureXdp(&mx, true);
  auto fixture = ZmRef<ZutcpTest::Xdp>{static_cast<ZutcpTest::Xdp *>(xdp.ptr())};
  ZmRef<Zutcp::Instance> tcp = new Zutcp::Instance;
  auto initialized = tcp->init(&mx, ZuMv(xdp), {});
  ZuCheck(initialized.is<void>() && !xdp && tcp->mx() == &mx && fixture->mx() == &mx);
  if (initialized.is<ZeException>()) return;
  ZuCheck(tcp->init(&mx, ZuMv(xdp), {}).is<void>() && fixture->opens == 1);
  ZuCheck(tcp->init(&mx, ZuMv(xdp), {.packetCount = 32}).is<ZeException>());
  auto other = ZutcpTest::fixtureXdp(&mx);
  ZuCheck(tcp->init(&mx, ZuMv(other), {}).is<ZeException>() && other);

  auto instance = tcp.ptr();
  mx.plugin(ZuMv(tcp));
  ZuCheck(!tcp && mx.plugin() == instance);
  auto rx = mx.allocator<Zi::IOBufType::Rx>(1200);
  auto tcpTx = mx.allocator<Zi::IOBufType::TxTCP, ZiIOBuf, 256>(27);
  auto udpTx = mx.allocator<Zi::IOBufType::TxUDP, ZiIOBuf, 768>(35);
  ZuCheck(rx && tcpTx && udpTx);
  if (!rx || !tcpTx || !udpTx) return;
  ZuCheck(!tcpTx->allocBuf() && !udpTx->allocBuf() && !instance->dataTxAllocator()->allocBuf());
  fixture->failAlloc = true;
  ZuCheck(mx.alloc().is<ZeException>() && !fixture->allocated() && !fixture->starts);
  ZuCheck(fixture->rxPool == rx->pool && fixture->txPools.length() == 2 &&
    fixture->txPools[0] == tcpTx->pool && fixture->txPools[1] == udpTx->pool);
  ZuCheck(rx->pool->count == 1200 && tcpTx->pool->count == 27 && udpTx->pool->count == 35);
  fixture->failAlloc = false;
  ZuCheck(mx.alloc().is<void>() && fixture->allocCalls == 2);
  auto native = tcpTx->allocBuf();
  auto udp = udpTx->allocBuf();
  auto data = instance->dataTxAllocator()->allocBuf();
  auto control = instance->control()->alloc();
  ZuCheck(native && udp && data && control);
  if (control) instance->control()->free(control);
  if (!native) return;
  auto address = native->data();
  *address = 0x54;
  fixture->failStart = true;
  ZuCheck(!mx.start() && mx.state() == ZmEngineState::Stopped && fixture->starts == 1);
  fixture->failStart = false;
  bool started = mx.start();
  ZuCheck(started && fixture->starts == 2);
  if (!started) return;
  ZuCheck(mx.stop() && fixture->stops == 1);
  started = mx.start();
  ZuCheck(started && fixture->starts == 3);
  if (started) ZuCheck(mx.stop() && fixture->stops == 2);
  ZuCheck(fixture->allocCalls == 2 && native->data() == address && *address == 0x54);
}

static void testNeverStartedFinal()
{
  ZuTestScope(testNeverStartedFinal);
  for (bool allocate : {false, true}) {
    ZmRef<ZutcpTest::Xdp> fixture;
    {
      ZiMultiplex mx;
      auto xdp = ZutcpTest::fixtureXdp(&mx, true);
      fixture = static_cast<ZutcpTest::Xdp *>(xdp.ptr());
      ZmRef<Zutcp::Instance> tcp = new Zutcp::Instance;
      auto result = tcp->init(&mx, ZuMv(xdp), {});
      ZuCheck(result.is<void>());
      if (result.is<ZeException>()) return;
      mx.plugin(ZuMv(tcp));
      if (allocate) ZuCheck(mx.alloc().is<void>());
    }
    ZuCheck(fixture->finals == 1 && !fixture->mx() && !fixture->starts && !fixture->stops);
  }
}

static void testFatalHandoffs()
{
  ZuTestScope(testFatalHandoffs);
  enum { Null, Uninitialized, DifferentMx, PluginMismatch, Allocated, RepeatAllocated };
  for (int scenario = Null; scenario <= RepeatAllocated; ++scenario) {
    auto pid = fork();
    if (!pid) {
      const rlimit limit{0, 0};
      setrlimit(RLIMIT_CORE, &limit);
      ZiMultiplex a, b;
      ZmRef<Zutcp::Instance> tcp = new Zutcp::Instance;
      ZmRef<Zxdp::Instance> xdp;
      if (scenario == Uninitialized) xdp = new ZutcpTest::Xdp;
      else if (scenario != Null) xdp = ZutcpTest::fixtureXdp(&a);
      if (scenario == RepeatAllocated)
        if (!tcp->init(&a, ZuMv(xdp), {}).is<void>()) _exit(2);
      if (scenario == Allocated || scenario == RepeatAllocated) {
        if (!xdp) xdp = ZutcpTest::fixtureXdp(&a);
        Zi::IOBufPoolList tx;
        if (!xdp->alloc(nullptr, tx).is<void>()) _exit(3);
      }
      auto result = tcp->init(scenario == DifferentMx ? &b : &a, ZuMv(xdp), {});
      if (scenario == PluginMismatch && result.is<void>()) b.plugin(ZuMv(tcp));
      _exit(4);
    }
    int status = 0;
    ZuCheck(pid > 0 && waitpid(pid, &status, 0) == pid && WIFSIGNALED(status) &&
      WTERMSIG(status) == SIGABRT);
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testStoppedInstance);
  ZuTestCall(testHandoff);
  ZuTestCall(testNeverStartedFinal);
  ZuTestCall(testFatalHandoffs);
}
