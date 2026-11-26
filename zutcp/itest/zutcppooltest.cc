//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuTime.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTime.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiMultiplex.hh>

#include <zlib/Zpkt.hh>

#include <zlib/ZutcpAlloc.hh>
#include <zlib/Zutcp.hh>
#include "ZutcpTestMxPlugin.hh"

using namespace ZuTestUtil;

static void testProvider()
{
  ZuTestScopeRT(testProvider);
  enum { PacketSize = 1514, Head = 42, Tail = 8, Payload = 42, RxCount = 8 };
  ZmRef<Zpkt::Env> env = new Zpkt::Env;
  ZiMultiplex mx{ZiMxParams{}.rxThread(1).txThread(2).scheduler([](auto &s) {
    s.nThreads(3).thread(3, [](auto &t) { t.isolated(true); });
  })};
  auto index = unsigned(strtoul(getenv("ZPKT_TEST_IFINDEX"), nullptr, 10));
  auto instance = ZutcpTest::openXdp(env, &mx, index, 3);
  auto provider = instance->bufPool(Zi::IOBufType::TxTCP, 2, PacketSize, Head, Tail);
  auto control = instance->bufPool(Zi::IOBufType::TxUDP, 1, 128);
  Zi::IOBufPoolAllocator<Zi::IOBufType::TxTCP> allocator{&mx, provider};
  Zi::IOBufPoolAllocator<Zi::IOBufType::TxUDP> ctlAllocator{&mx, control};
  ZuCheckRT(allocator.size == PacketSize && allocator.headRoom == Head &&
      allocator.tailRoom == Tail && !allocator.allocBuf(nullptr, Payload),
    log_("provider publishes fixed geometry before backing is allocated"));
  auto rx = instance->bufPool(Zi::IOBufType::Rx, RxCount, PacketSize);
  Zi::IOBufPoolList tx;
  tx.pushNode(provider.ptr());
  tx.pushNode(control.ptr());
  auto allocated = instance->alloc(rx, tx);
  ZuCheckRT(allocated.is<void>());
  if (allocated.is<ZeException>()) { instance->final(); return; }
  unsigned completed = 0;
  uint8_t *canonical = nullptr;
  bool started = mx.start();
  ZuCheckRT(started);
  if (!started) { instance->final(); return; }
  bool ready = instance->start([&rx](uint8_t *p, unsigned, ZiRxBurst *) { rx->free(p); });
  ZuCheckRT(ready, log_("XDP activates both caller-owned Tx pools: ", instance->error()));
  if (ready) {
    auto buf = allocator.allocBuf(&mx, Payload);
    auto other = allocator.allocBuf(&mx, Payload);
    auto ctl = ctlAllocator.allocBuf(&mx, 64);
    ZuCheckRT(buf && other && ctl && !allocator.allocBuf(&mx, Payload) &&
        !allocator.allocBuf(&mx, allocator.capacity() + 1),
      log_("application exhaustion does not consume control backing or grow the reserve"));
    if (buf && other && ctl) {
      canonical = buf->data_();
      auto payload = buf->data();
      bool geometry = buf->registered() && buf->owner == &mx && buf->skip == Head &&
        buf->length == Payload && payload == canonical + Head;
      memset(canonical, 0, Head + Payload);
      memset(canonical, 0xff, 6);
      memcpy(canonical + 6, instance->mac().data(), 6);
      canonical[12] = 0x88;
      canonical[13] = 0xb5;
      for (unsigned i = 0; i < Payload; ++i) payload[i] = i ^ 0xa7;
      buf->rewind(Head);
      ZuCheckRT(geometry && buf->data() == canonical && buf->length == Head + Payload,
        log_("Zi payload and prepended wire headers retain the same raw pool backing"));
      uint8_t retainedBytes[64];
      memcpy(retainedBytes, canonical, sizeof(retainedBytes));
      memcpy(ctl->data(), retainedBytes, sizeof(retainedBytes));
      auto heldOrigin = ctl->data_();
      bool held = instance->send(ctl->data(), ctl->length);
      ZuCheckRT(held,
        log_("XDP accepts retained control backing"));
      bool accepted = instance->send(buf->data(), buf->length);
      if (accepted) {
        buf->data__ = 0;
        buf->size = buf->skip = buf->length = 0;
        provider->free(canonical);
      }
      ZuCheckRT(accepted && !buf->data_() && !buf->registered(),
        log_("accepted raw submission permits metadata to detach backing"));
      buf = nullptr;
      ZuCheckRT(!allocator.allocBuf(&mx, Payload),
        log_("metadata destruction does not recycle kernel-owned backing"));
      auto deadline = Zm::now() + ZuTime{10};
      while (accepted && completed < 2 && Zm::now() < deadline) {
        completed += instance->pollTx();
        if (completed < 2) poll(nullptr, 0, 1);
      }
      auto recycled = allocator.allocBuf(&mx, Payload);
      ZuCheckRT(completed == 2 && recycled && recycled->data_() == canonical &&
          ctl->data_() == heldOrigin && !memcmp(ctl->data(), retainedBytes, 64),
        log_("CQ recycles the originating pool slot without a metadata callback"));
      ZuCheckRT(instance->send(ctl->data(), ctl->length),
        log_("retained backing can be resubmitted unchanged after CQ retirement"));
      other = nullptr;
      bool stopped = instance->stop();
      ZuCheckRT(stopped && instance->allocated(),
        log_("stop retires packet service and preserves the registration"));
      ZuCheckRT(recycled && recycled->data_() == canonical && ctl->data_() == heldOrigin &&
          !memcmp(ctl->data(), retainedBytes, 64),
        log_("stop preserves caller-retained backing and bytes"));
      recycled = nullptr;
      ctl = nullptr;
      auto reused = allocator.allocBuf(&mx, Payload);
      ZuCheckRT(reused, log_("stopped allocation still returns a native slot"));
    }
  }
  if (instance->ready()) ZuCheckRT(instance->stop());
  instance->final();
  ZuCheckRT(!provider->alloc() && !control->alloc() && mx.stop());

}

static void testAllocatedHandoff(bool repeat)
{
  ZuTestScopeRT(testAllocatedHandoff);
  auto index = getenv(repeat ? "ZPKT_RX_TEST_PEER_IFINDEX" : "ZPKT_RX_TEST_IFINDEX");
  auto pid = fork();
  if (!pid) {
    const rlimit limit{0, 0};
    setrlimit(RLIMIT_CORE, &limit);
    ZmRef<Zpkt::Env> env = new Zpkt::Env;
  ZiMultiplex mx{ZiMxParams{}.rxThread(1).txThread(2).scheduler([](auto &s) {
      s.nThreads(3).thread(3, [](auto &t) { t.isolated(true); });
    })};
    auto nativeIndex = unsigned(strtoul(index, nullptr, 10));
    Zutcp::MxPlugin tcp;
    auto xdp = ZutcpTest::openXdp(env, &mx, nativeIndex, 3);
    if (repeat) {
      ZmRef<Zpkt::Nif> initial = xdp;
      try { tcp.init(&mx, env, ZuMv(initial), {}); }
      catch (ZeException &e) { std::cerr << e << '\n' << std::flush; _exit(2); }
    }
    auto rx = xdp->bufPool(Zi::IOBufType::Rx, 8, xdp->mtu() + 14);
    auto tx = xdp->bufPool(Zi::IOBufType::TxTCP, 8, xdp->mtu() + 14);
    Zi::IOBufPoolList pools;
    pools.pushNode(tx.ptr());
    if (!xdp->alloc(rx, pools).is<void>() || !xdp->allocated() || xdp->state() != ZmEngineState::Stopped) _exit(4);
    tcp.init(&mx, env, ZuMv(xdp), {});
    _exit(5);
  }
  int status = 0;
  ZuCheckRT(pid > 0 && waitpid(pid, &status, 0) == pid && WIFSIGNALED(status) &&
    WTERMSIG(status) == SIGABRT, log_("allocated XDP is rejected at the TCP handoff"));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  if (geteuid() || !getenv("ZPKT_TEST_IFINDEX") || !getenv("ZPKT_RX_TEST_IFINDEX")) {
    std::cout << "1..0 # SKIP requires privileged fixture-owned interface\n";
    return 0;
  }
  ZuTestMain();
  ZuTestCall(testProvider);
  ZuTestCall(testAllocatedHandoff, false);
  if (getenv("ZPKT_RX_TEST_PEER_IFINDEX")) ZuTestCall(testAllocatedHandoff, true);
}
