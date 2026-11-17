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
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiIOBuf.hh>

#include <zlib/Zxdp.hh>

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

static void closeInstance(ZmRef<Zxdp::Instance> &instance)
{
  for (;;) {
    auto result = instance->close();
    if (result.is<bool>() && result.p<bool>()) break;
  }
  instance = nullptr;
}

// Kernel queue release can outlast a completed close. Retry from this test's
// control caller with the same drained pool, never from packet service.
static ZmRef<Zxdp::Instance> createInstance(
  const Zxdp::Params &params, ZeException &error)
{
  auto deadline = Zm::now() + ZuTime{2};
  unsigned attempts = 0;
  do {
    ++attempts;
    wakeFD = -1;
    captureWake = true;
    auto instance = Zxdp::instance(Zxdp::Type::Xdp);
    auto initialized = instance->init(params);
    if (initialized.is<ZeException>()) error = initialized.p<ZeException>();
    else error = {};
    captureWake = false;
    if (instance && instance->ready()) {
      std::cout << "# raw wake startup attempts=" << attempts << '\n';
      return instance;
    }
    if (instance) closeInstance(instance);
    poll(nullptr, 0, 1);
  } while (Zm::now() < deadline);
  return nullptr;
}

static void testWake()
{
  ZuTestScopeRT(testWake);
  alarm(20); // a missing wake fails the fixture; time does not establish success
  auto backend = Zxdp::instance(Zxdp::Type::Xdp);
  auto pool = backend->bufPool(8, 1514); // two queues, four receive slots each
  auto tx = backend->bufPool(1, 128);
  ZeException error;
  ZmSemaphore packet;
  Zxdp::Params config(
    Zxdp::Params::New<Zxdp::Params::Index<Zxdp::Xdp::Params>{}>{});
  auto &params = config.p<Zxdp::Xdp::Params>();
  params.ifindex = unsigned(strtoul(getenv("ZXDP_TEST_IFINDEX"), nullptr, 10));
  params.rxPool = pool;
  params.txPools.pushNode(tx.ptr());
  params.rx = [&pool, &packet](uint8_t *p, unsigned) {
    pool->free(p);
    packet.post();
  };
  params.spin = 0;
  ZmScheduler scheduler{ZmSchedParams{}.nThreads(1).thread(1,
    [&params](auto &thread) { thread = params.txThread; })};
  params.scheduler = &scheduler;
  params.txSid = 1;
  bool started = scheduler.start();
  ZuCheckRT(started);
  if (!started) { alarm(0); return; }
  auto instance = createInstance(config, error);
  bool ready = instance && instance->ready();
  ZuCheckRT(ready, log_("zero-spin receive instance starts: ", error));
  if (!ready) {
    if (instance) closeInstance(instance);
    ZuCheckRT(scheduler.stop());
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
  address.sll_ifindex = strtoul(getenv("ZXDP_TEST_PEER_IFINDEX"), nullptr, 10);
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
  if (wire >= 0) close(wire);
  entered = nullptr;
  closeInstance(instance);
  ZuCheckRT(scheduler.stop());
  ZuCheckRT(!pool->alloc(), log_("receive/wake resources retire with all kernel-held packets"));
  alarm(0);
}

static void testActiveWake()
{
  ZuTestScopeRT(testActiveWake);
  alarm(20);
  auto backend = Zxdp::instance(Zxdp::Type::Xdp);
  auto pool = backend->bufPool(8, 1514);
  auto tx = backend->bufPool(1, 128);
  ZeException error;
  ZmSemaphore active, release;
  Zxdp::Instance *current = nullptr;
  unsigned packets = 0;
  Zxdp::Params config(
    Zxdp::Params::New<Zxdp::Params::Index<Zxdp::Xdp::Params>{}>{});
  auto &params = config.p<Zxdp::Xdp::Params>();
  params.ifindex = unsigned(strtoul(getenv("ZXDP_TEST_IFINDEX"), nullptr, 10));
  params.rxPool = pool;
  params.txPools.pushNode(tx.ptr());
  params.rx = [&pool, &packets, &active, &release](uint8_t *p, unsigned) {
    ++packets;
    active.post();
    release.wait();
    pool->free(p);
  };
  params.spin = unsigned(-1);
  ZmScheduler scheduler{ZmSchedParams{}.nThreads(1).thread(1,
    [&params](auto &thread) { thread = params.txThread; })};
  params.scheduler = &scheduler;
  params.txSid = 1;
  bool started = scheduler.start();
  ZuCheckRT(started);
  if (!started) { alarm(0); return; }
  auto instance = createInstance(config, error);
  bool ready = instance && instance->ready();
  ZuCheckRT(ready, log_("long-spin receive instance starts: ", error));
  if (!ready) {
    if (instance) closeInstance(instance);
    ZuCheckRT(scheduler.stop());
    alarm(0);
    return;
  }
  current = instance.ptr();
  int wire = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC, htons(ETH_P_ALL));
  sockaddr_ll address{};
  address.sll_family = AF_PACKET;
  address.sll_protocol = htons(ETH_P_ALL);
  address.sll_ifindex = strtoul(getenv("ZXDP_TEST_PEER_IFINDEX"), nullptr, 10);
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
  ZuCheckRT(scheduler.stop());
  alarm(0);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  if (geteuid() || !getenv("ZXDP_TEST_IFINDEX") || !getenv("ZXDP_TEST_PEER_IFINDEX")) {
    std::cout << "1..0 # SKIP requires privileged fixture-owned interface pair\n";
    return 0;
  }
  ZuTestMain();
  ZuTestCall(testWake);
  ZuTestCall(testActiveWake);
}
