//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <arpa/inet.h>
#include <iostream>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuTime.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiIOBuf.hh>

#include <zlib/Zpkt.hh>

#include "zpktfixture.hh"

using namespace ZuTestUtil;

// Test-only observation/gating of the real blocking syscall. No synthetic
// readiness or production wait hooks: the kernel still handles epoll/eventfd.
static ZmSemaphore *entered = nullptr, *gate = nullptr;
static unsigned waits = 0;
static bool captureWake = false;
static int wakeFD = -1;
static ZmAtomic<unsigned> wakeReads{0}, wakeWrites{0};
static ZmSemaphore *writing = nullptr, *writeGate = nullptr;

extern "C" __attribute__((visibility("default")))
int eventfd(unsigned value, int flags) noexcept
{
  int fd = syscall(SYS_eventfd2, value, flags);
  if (captureWake && wakeFD < 0) wakeFD = fd;
  return fd;
}

extern "C" __attribute__((visibility("default")))
ssize_t write(int fd, const void *data, size_t size)
{
  if (fd == wakeFD) {
    ++wakeWrites;
    if (writing) { writing->post(); writeGate->wait(); }
  }
  return syscall(SYS_write, fd, data, size);
}

extern "C" __attribute__((visibility("default")))
ssize_t read(int fd, void *data, size_t size)
{
  if (fd == wakeFD) ++wakeReads;
  return syscall(SYS_read, fd, data, size);
}

extern "C" __attribute__((visibility("default")))
int epoll_wait(int fd, epoll_event *events, int count, int timeout)
{
  if (timeout == -1 && entered) {
    ++waits;
    entered->post();
    if (gate) gate->wait();
  }
  return syscall(SYS_epoll_wait, fd, events, count, timeout);
}

static void closeInstance(ZmRef<Zpkt::Nif> &instance)
{
  ZmAssert_(instance->stop());
  instance->final();
  instance = nullptr;
}

static bool setup(
  ZpktTest::Environment &env, ZmRef<Zpkt::Nif> &instance, ZiMultiplex &mx, const ZpktTest::Interface &params,
  Zi::IOBufPool *ingress, const Zi::IOBufPoolList &tx,
  Zpkt::RecvFn rx, ZeException &error)
{
  wakeFD = -1;
  captureWake = true;
  auto initialized = env.open(instance, &mx, params);
  captureWake = false;
  error = ZpktTest::start(instance, mx, ZuMv(initialized), ingress, tx, ZuMv(rx));
  return instance && instance->ready();
}

static void testWake()
{
  ZuTestScopeRT(testWake);
  alarm(20); // a missing wake fails the fixture; time does not establish success
  ZpktTest::Environment env;
  ZmRef<Zpkt::Nif> instance;
  auto pool = ZpktTest::pool(Zi::IOBufType::Rx, 8, 1514); // two queues, four receive slots each
  auto tx = ZpktTest::pool(Zi::IOBufType::TxUDP, 1, 128);
  ZeException error;
  ZmSemaphore packet;
  ZmAtomic<unsigned> retain{0};
  uint8_t *held = nullptr;
  ZpktTest::Interface config;
  auto &params = config;
  params.ifindex = unsigned(strtoul(getenv("ZPKT_TEST_IFINDEX"), nullptr, 10));
  Zi::IOBufPoolList pools;
  pools.pushNode(tx.ptr());
  Zpkt::RecvFn receive = [&pool, &packet, &held, &retain](uint8_t *p, unsigned, ZiRxBurst *) {
    if (retain.xch(0)) held = p;
    else pool->free(p);
    packet.post();
  };
  params.rxSpin = 0;
  params.txSid = 3;
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.thread(3, [](auto &t) { t.isolated(true); });
  })};
  bool ready = setup(env, instance, mx, config, pool, pools, ZuMv(receive), error);
  ZuCheckRT(ready, log_("zero-spin receive instance starts: ", error));
  if (!ready) {
    if (instance) closeInstance(instance);
    if (mx.running()) ZuCheckRT(mx.stop());
    alarm(0);
    return;
  }

  ZmSemaphore parked, release;
  entered = &parked;
  waits = 0;
  // Preserve one return for each queued scheduler wake, including before entry.
  wakeReads = wakeWrites = 0;
  instance->wake(); instance->wake(); instance->wake();
  instance->rx(); instance->rx(); instance->rx();
  ZuCheckRT(!waits, log_("preposted wake pulses return separate receive calls without blocking"));

  enum { Wakers = 4, Pulses = 32 }; // independent concurrent scheduler producers
  ZmThread wakers[Wakers];
  for (auto &waker : wakers) waker = ZmThread{[&instance] {
    for (unsigned i = 0; i < Pulses; ++i) instance->wake();
  }};
  for (auto &waker : wakers) waker.join();
  for (unsigned i = 0; i < Wakers * Pulses; ++i) instance->rx();
  ZuCheckRT(!waits, log_("concurrent wake producers preserve every queued return"));
  ZuCheckRT(!unsigned(wakeReads) && !unsigned(wakeWrites),
    log_("preposted/concurrent pulses use only userspace while Rx is not parked"));

  // Post the wake precisely after the loop decided to sleep but before the
  // actual syscall. Eventfd readiness must close this check-to-wait race.
  gate = &release;
  ZmThread receiver{[&instance] { instance->rx(); }};
  parked.wait();
  instance->wake();
  release.post();
  receiver.join();
  ZuCheckRT(waits == 1,
    log_("zero spin selects blocking epoll and an entry-racing wake returns"));
  gate = nullptr;

  // Run many receive/wake cycles, allowing the actual wait to race naturally
  // with the waker. No sleep interval is used to synchronize the threads.
  enum { Turns = 128 }; // repeated entry/exit race fixture
  ZmThread repeated{[&instance] { for (unsigned i = 0; i < Turns; ++i) instance->rx(); }};
  for (unsigned i = 0; i < Turns; ++i) { parked.wait(); instance->wake(); }
  repeated.join();
  ZuCheckRT(waits == Turns + 1,
    log_("repeated eventfd wakeups neither disappear nor return extra calls"));

  int wire = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC, htons(ETH_P_ALL));
  sockaddr_ll address{};
  address.sll_family = AF_PACKET;
  address.sll_protocol = htons(ETH_P_ALL);
  address.sll_ifindex = strtoul(getenv("ZPKT_TEST_PEER_IFINDEX"), nullptr, 10);
  uint8_t bytes[64]{}; // minimum Ethernet fixture frame
  memcpy(bytes, instance->mac().data(), 6);
  bytes[6] = 2;
  bytes[12] = 0x88; bytes[13] = 0xb5;
  unsigned before = waits;
  ZmThread receiving{[&instance] { instance->rx(); }};
  parked.wait();
  bool sent = wire >= 0 && sendto(wire, bytes, sizeof(bytes), 0,
    reinterpret_cast<sockaddr *>(&address), sizeof(address)) == sizeof(bytes);
  ZuCheckRT(sent, log_("independent peer injects a real frame into the blocked receiver"));
  if (sent) {
    packet.wait();
    parked.wait(); // packet processing continues into another wait in the same call
  }
  instance->wake();
  receiving.join();
  ZuCheckRT(sent && waits >= before + 2,
    log_("packet readiness resumes processing; rx returns only on explicit wake"));
  // Delay a real notifier after it observed the published park. Packet
  // readiness lets Rx consume the pulse before that writer reaches eventfd.
  ZmSemaphore writeStarted, releaseWrite;
  ZmThread racingRx{[&instance] { instance->rx(); }};
  parked.wait();
  writing = &writeStarted; writeGate = &releaseWrite;
  ZmThread notifier{[&instance] { instance->wake(); }};
  writeStarted.wait();
  bool raced = wire >= 0 && sendto(wire, bytes, sizeof(bytes), 0,
    reinterpret_cast<sockaddr *>(&address), sizeof(address)) == sizeof(bytes);
  ZuCheckRT(raced, log_("packet readiness releases Rx before a delayed notifier write"));
  racingRx.join();
  releaseWrite.post(); notifier.join();
  writing = writeGate = nullptr;
  before = waits;
  ZmThread staleRx{[&instance] { instance->rx(); }};
  packet.wait();
  parked.wait(); // stale readiness is drained but must not return rx()
  parked.wait(); // an actual wait follows, needing its own new pulse
  instance->wake(); staleRx.join();
  ZuCheckRT(waits == before + 2,
    log_("stale eventfd readiness neither invents a return nor loses the next pulse"));

  auto posted = [&instance] {
    uint64_t count = 0;
    instance->allQueues([&count](Ztc::Queue *queue) {
      Ztc::QueueTelemetry data;
      queue->telemetry(data);
      if (data.type == Ztc::QueueType::Tx) count += data.inCount;
    });
    return count;
  };
  auto sendFrame = [&] {
    return wire >= 0 && sendto(wire, bytes, sizeof(bytes), 0,
      reinterpret_cast<sockaddr *>(&address), sizeof(address)) == sizeof(bytes);
  };
  ZmThread idleRx{[&instance] { instance->rx(); }};
  parked.wait();
  retain = 1;
  bool loaned = sendFrame();
  ZuCheckRT(loaned, log_("peer sends one frame while the receiver is parked"));
  if (loaned) {
    packet.wait();
    parked.wait();
    auto writes = unsigned(wakeWrites);
    pool->free(held);
    ZuCheckRT(unsigned(wakeWrites) == writes,
      log_("return above the FILL low watermark makes no eventfd write"));
  }
  instance->wake();
  idleRx.join();

  unsigned queues = (instance->allQueues([](Ztc::Queue *) {}) - 2) / 2;
  unsigned quota = pool->count / queues;
  uint8_t *loans[8]{};
  ZmThread starvedRx{[&instance] { instance->rx(); }};
  parked.wait();
  unsigned n = 0;
  while (n < quota) {
    retain = 1;
    if (!sendFrame()) break;
    packet.wait();
    parked.wait();
    loans[n++] = held;
  }
  auto spare = pool->alloc();
  ZuCheckRT(n == quota && !spare,
    log_("peer's FILL supply is held by the application and the ingress pool is empty"));
  if (spare) pool->free(spare);
  if (n) {
    auto before = posted();
    auto writes = unsigned(wakeWrites);
    pool->free(loans[--n]);
    parked.wait();
    ZuCheckRT(unsigned(wakeWrites) == writes + 1 && posted() == before + 1,
      log_("one off-owner return wakes the starved receiver and reposts the sole frame"));
    bool resumed = sendFrame();
    if (resumed) { packet.wait(); parked.wait(); }
    ZuCheckRT(resumed, log_("peer traffic resumes on the one replenished frame"));
  }
  instance->wake();
  starvedRx.join();
  auto writes = unsigned(wakeWrites);
  while (n) pool->free(loans[--n]);
  ZuCheckRT(unsigned(wakeWrites) == writes,
    log_("returning outstanding frames after Rx exits makes no wake write"));
  if (queues == 2 && getenv("ZPKT_TEST_QUEUE_STEERING")) {
    auto posts = [&instance, &params](unsigned q) {
      auto fill = ZuID{} << "xdp." << ZuBoxed(params.ifindex) << '.'
        << ZuBoxed(q) << ".fill";
      uint64_t count = 0;
      instance->allQueues([&](Ztc::Queue *queue) {
        Ztc::QueueTelemetry data;
        queue->telemetry(data);
        if (data.id == fill) count = data.inCount;
      });
      return count;
    };
    ZmThread twoQueues{[&instance] { instance->rx(); }};
    parked.wait();
    for (unsigned q = 0; q < queues; ++q) {
      bytes[13] = 0xb5 + q;
      for (unsigned i = 0; i < quota; ++i) {
        retain = 1;
        if (!sendFrame()) break;
        packet.wait();
        parked.wait();
        loans[n++] = held;
      }
    }
    ZuCheckRT(n == pool->count, log_("both hardware queues run out of receive frames"));
    unsigned first = 0;
    for (unsigned i = 0; i < 2 && n; ++i) {
      auto q0 = posts(0), q1 = posts(1);
      pool->free(loans[--n]);
      parked.wait();
      unsigned q = posts(0) == q0 + 1 ? 0 : 1;
      ZuCheckRT(q ? posts(1) == q1 + 1 && posts(0) == q0 : posts(1) == q1,
        log_("return restores one empty queue"));
      if (i) ZuCheckRT(q != first, log_("successive sole returns serve both empty queues"));
      first = q;
      bytes[13] = 0xb5 + q;
      retain = 1;
      if (sendFrame()) { packet.wait(); parked.wait(); loans[n++] = held; }
    }
    instance->wake();
    twoQueues.join();
    while (n) pool->free(loans[--n]);
  }
  if (wire >= 0) close(wire);
  entered = nullptr;
  closeInstance(instance);
  if (mx.running()) ZuCheckRT(mx.stop());
  ZuCheckRT(!pool->alloc(), log_("receive/wake resources retire with all kernel-held packets"));
  alarm(0);
}

static void testActiveWake()
{
  ZuTestScopeRT(testActiveWake);
  alarm(20);
  ZpktTest::Environment env;
  ZmRef<Zpkt::Nif> instance;
  auto pool = ZpktTest::pool(Zi::IOBufType::Rx, 8, 1514);
  auto tx = ZpktTest::pool(Zi::IOBufType::TxUDP, 1, 128);
  ZeException error;
  ZmSemaphore active, release;
  Zpkt::Nif *current = nullptr;
  unsigned packets = 0;
  ZpktTest::Interface config;
  auto &params = config;
  // Use the other tuple. Linux asynchronously releases a former owner's
  // device pool after final close, independent of the stopped service.
  params.ifindex = unsigned(strtoul(getenv("ZPKT_TEST_PEER_IFINDEX"), nullptr, 10));
  Zi::IOBufPoolList pools;
  pools.pushNode(tx.ptr());
  Zpkt::RecvFn receive = [&pool, &packets, &active, &release](uint8_t *p, unsigned, ZiRxBurst *) {
    ++packets;
    active.post();
    release.wait();
    pool->free(p);
  };
  params.rxSpin = unsigned(-1);
  params.txSid = 3;
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.thread(3, [](auto &t) { t.isolated(true); });
  })};
  bool ready = setup(env, instance, mx, config, pool, pools, ZuMv(receive), error);
  ZuCheckRT(ready, log_("long-spin receive instance starts: ", error));
  if (!ready) {
    if (instance) closeInstance(instance);
    if (mx.running()) ZuCheckRT(mx.stop());
    alarm(0);
    return;
  }
  current = instance.ptr();
  int wire = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC, htons(ETH_P_ALL));
  sockaddr_ll address{};
  address.sll_family = AF_PACKET;
  address.sll_protocol = htons(ETH_P_ALL);
  address.sll_ifindex = strtoul(getenv("ZPKT_TEST_IFINDEX"), nullptr, 10);
  uint8_t bytes[64]{};
  memcpy(bytes, instance->mac().data(), 6);
  bytes[6] = 2; bytes[12] = 0x88; bytes[13] = 0xb5;
  ZmSemaphore parked;
  entered = &parked;
  waits = 0;
  wakeReads = wakeWrites = 0;
  ZmThread receiver{[current] { current->rx(); }};
  bool sent = wire >= 0 && sendto(wire, bytes, sizeof(bytes), 0,
    reinterpret_cast<sockaddr *>(&address), sizeof(address)) == sizeof(bytes);
  ZuCheckRT(sent, log_("peer supplies input during configured spinning"));
  if (sent) active.wait();
  current->wake();
  release.post();
  receiver.join();
  ZuCheckRT(sent && packets == 1 && !waits &&
      !unsigned(wakeReads) && !unsigned(wakeWrites),
    log_("active callback wake returns without epoll or eventfd syscalls"));
  entered = nullptr;
  if (wire >= 0) close(wire);
  closeInstance(instance);
  if (mx.running()) ZuCheckRT(mx.stop());
  alarm(0);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  if (geteuid() || !getenv("ZPKT_TEST_IFINDEX") || !getenv("ZPKT_TEST_PEER_IFINDEX")) {
    std::cout << "1..0 # SKIP requires privileged fixture-owned interface pair\n";
    return 0;
  }
  ZuTestMain();
  ZuTestCall(testWake);
  ZuTestCall(testActiveWake);
}
