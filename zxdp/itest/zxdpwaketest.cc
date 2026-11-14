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
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmTime.hh>
#include <zlib/Zxdp.hh>

using namespace ZuTestUtil;

// Test-only observation/gating of the real blocking syscall. No synthetic
// readiness or production wait hooks: the kernel still handles epoll/eventfd.
static ZmSemaphore *entered = nullptr, *gate = nullptr;
static unsigned waits = 0;

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
  ZtString<> error;
  while (!instance->close(error)) { }
  instance = nullptr;
}

// Kernel queue release can outlast a completed close. Retry from this test's
// control caller with the same drained pool, never from packet service.
static ZmRef<Zxdp::Instance> createInstance(
  const Zxdp::Params &params, ZtString<> &error)
{
  auto deadline = Zm::now() + ZuTime{2};
  unsigned attempts = 0;
  do {
    ++attempts;
    auto instance = Zxdp::Instance::create(params, error);
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
  Zxdp::BufPool pool{8, 1514}; // two queues, four receive slots each
  ZtString<> error;
  ZmSemaphore packet;
  auto instance = createInstance({
    .interface = unsigned(strtoul(getenv("ZXDP_TEST_IFINDEX"), nullptr, 10)),
    .rxPool = &pool, .rx = [&pool, &packet](uint8_t *p, unsigned) {
      pool.free(p);
      packet.post();
    }, .spin = 0}, error);
  bool ready = instance && instance->ready();
  ZuCheckRT(ready, log_("zero-spin receive instance starts: ", error));
  if (!ready) { if (instance) closeInstance(instance); alarm(0); return; }

  ZmSemaphore parked, release;
  entered = &parked;
  waits = 0;
  // Preserve one return for each queued scheduler wake, including before entry.
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
  memcpy(bytes, instance->mac(), 6);
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
  if (wire >= 0) close(wire);
  entered = nullptr;
  closeInstance(instance);
  ZuCheckRT(!pool.used(), log_("receive/wake resources retire with all kernel-held packets"));
  alarm(0);
}

static void testActiveWake()
{
  ZuTestScopeRT(testActiveWake);
  alarm(20);
  Zxdp::BufPool pool{8, 1514};
  ZtString<> error;
  ZmSemaphore active, release;
  Zxdp::Instance *current = nullptr;
  unsigned packets = 0;
  auto instance = createInstance({
    .interface = unsigned(strtoul(getenv("ZXDP_TEST_IFINDEX"), nullptr, 10)),
    .rxPool = &pool, .rx = [&pool, &packets, &active, &release](uint8_t *p, unsigned) {
      ++packets;
      active.post();
      release.wait();
      pool.free(p);
    }, .spin = unsigned(-1)}, error);
  bool ready = instance && instance->ready();
  ZuCheckRT(ready, log_("long-spin receive instance starts: ", error));
  if (!ready) { if (instance) closeInstance(instance); alarm(0); return; }
  current = instance.ptr();
  int wire = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC, htons(ETH_P_ALL));
  sockaddr_ll address{};
  address.sll_family = AF_PACKET;
  address.sll_protocol = htons(ETH_P_ALL);
  address.sll_ifindex = strtoul(getenv("ZXDP_TEST_PEER_IFINDEX"), nullptr, 10);
  uint8_t bytes[64]{};
  memcpy(bytes, instance->mac(), 6);
  bytes[6] = 2; bytes[12] = 0x88; bytes[13] = 0xb5;
  ZmSemaphore parked;
  entered = &parked;
  waits = 0;
  ZmThread receiver{[current] { current->rx(); }};
  bool sent = wire >= 0 && sendto(wire, bytes, sizeof(bytes), 0,
    reinterpret_cast<sockaddr *>(&address), sizeof(address)) == sizeof(bytes);
  ZuCheckRT(sent, log_("peer supplies input during configured spinning"));
  if (sent) active.wait();
  current->wake();
  release.post();
  receiver.join();
  ZuCheckRT(sent && packets == 1 && !waits,
    log_("a wake during active callback processing returns without entering epoll"));
  entered = nullptr;
  if (wire >= 0) close(wire);
  closeInstance(instance);
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
