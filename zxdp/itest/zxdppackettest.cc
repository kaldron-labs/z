//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <arpa/inet.h>
#include <errno.h>
#include <iostream>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/Zxdp.hh>
#include <zlib/ZxdpBPF.hh>

using namespace ZuTestUtil;

enum { Bytes = 96, RxSlots = 8, TxSlots = 2 }; // exact fixture workload

static bool shutdown(ZmRef<Zxdp::Instance> &instance, ZtString<> &error)
{
  if (!instance) return true;
  auto deadline = Zm::now() + ZuTime{10};
  do {
    if (instance->close(error)) { instance = nullptr; return true; }
    // Main-thread fixture loop; retry only deferred kernel retirement. No
    // concurrent application service remains, and no timeout authorizes free.
    poll(nullptr, 0, 1);
  } while (Zm::now() < deadline);
  return false;
}

static void testPackets()
{
  ZuTestScopeRT(testPackets);
  unsigned ifindex = strtoul(getenv("ZXDP_TEST_IFINDEX"), nullptr, 10);
  unsigned peer = strtoul(getenv("ZXDP_TEST_PEER_IFINDEX"), nullptr, 10);
  ZtString<> error;
  Zxdp::LinkState local, remote;
  bool queried = Zxdp::queryLink(ifindex, local, error) &&
    Zxdp::queryLink(peer, remote, error);
  ZuCheckRT(queried && local.rxQueues == 2, log_("fixture provides two Rx queues: ", error));
  if (!queried) return;
  int wire = socket(AF_PACKET, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, htons(ETH_P_ALL));
  sockaddr_ll addr{};
  addr.sll_family = AF_PACKET;
  addr.sll_protocol = htons(ETH_P_ALL);
  addr.sll_ifindex = peer;
  bool bound = wire >= 0 && !bind(wire, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
  ZuCheckRT(bound, log_("independent peer packet socket bound"));
  if (!bound) { if (wire >= 0) close(wire); return; }
  Zxdp::BufPool rx{RxSlots, local.mtu + 14}, tx{TxSlots, 128}, control{1, 64};
  tx.next = &control;
  uint8_t *received = nullptr;
  unsigned rxBytes = 0, completions = 0;
  Zxdp::Instance *current = nullptr;
  auto instance = Zxdp::Instance::create({.interface = ifindex,
    .txPools = &tx, .rxPool = &rx,
    .rx = [&received, &rxBytes, &rx, &current](uint8_t *p, unsigned n) {
      if (!received) { received = p; rxBytes = n; current->wake(); } else rx.free(p);
    }}, error);
  bool ready = instance && instance->ready();
  ZuCheckRT(ready, log_("named parameters bind all interface queues: ", error));
  if (!ready) {
    if (instance) ZmAssert_(shutdown(instance, error));
    close(wire);
    return;
  }
  current = instance.ptr();
  auto packet = tx.alloc();
  ZmAssert_(packet);
  auto output = packet + 1; // odd offsets exercise generic-kernel CQ address tagging
  ZuCheckRT(tx.writable(output) && !rx.writable(output) && !tx.writable(nullptr),
    log_("only a caller-held idle Tx allocation is writable"));
  memcpy(output, remote.mac, 6);
  memcpy(output + 6, local.mac, 6);
  output[12] = 0x88;
  output[13] = 0xb5;
  for (unsigned i = 14; i < Bytes; ++i) output[i] = (i * 37) ^ 0xa5;
  uint8_t expected[Bytes];
  memcpy(expected, output, Bytes);
  ZuCheckRT(!instance->send(nullptr, Bytes) && !instance->send(packet, tx.size + 1) &&
      !instance->send(packet + 1, tx.size), log_("invalid submissions publish no descriptor"));
  bool submitted = instance->send(output, Bytes);
  ZuCheckRT(submitted && !tx.writable(output) &&
      !instance->send(output, Bytes) && tx.used() == 1,
    log_("accepted backing has one descriptor owner and cannot be resubmitted"));
  if (submitted) tx.free(packet);
  ZuCheckRT(tx.used() == 1, log_("early pool return cannot recycle live kernel backing"));
  bool captured = false;
  uint8_t observed[2048]; // fixture link MTU is 1500
  auto deadline = Zm::now() + ZuTime{10};
  while ((!captured || !completions) && Zm::now() < deadline) {
    completions += instance->pollTx();
    pollfd event{wire, POLLIN, 0};
    poll(&event, 1, 1);
    auto n = recv(wire, observed, sizeof(observed), MSG_DONTWAIT);
    captured |= n == Bytes && !memcmp(observed, expected, Bytes);
  }
  ZuCheckRT(captured && completions == 1 && !tx.used(),
    log_("peer sees exact arena bytes; CQ recycles backing directly to its pool"));
  auto recycled = tx.alloc();
  ZuCheckRT(recycled == packet && tx.writable(recycled),
    log_("completion restores the exact originating slot and writable caller lease"));
  // Send independent raw Ethernet traffic back. No IP/TCP/UDP selection exists
  // without a mask, so the experimental EtherType must reach the raw callback.
  memcpy(observed, local.mac, 6);
  memcpy(observed + 6, remote.mac, 6);
  observed[12] = 0x88;
  observed[13] = 0xb5;
  for (unsigned i = 14; i < Bytes; ++i) observed[i] = i ^ 0x6d;
  bool injected = sendto(wire, observed, Bytes, 0,
    reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == Bytes;
  ZmSemaphore returned;
  ZmThread watchdog{[&returned, &instance] {
    if (returned.timedwait(Zm::now() + ZuTime{10})) instance->wake();
  }};
  instance->rx(); // the packet callback explicitly wakes this receive call
  returned.post();
  watchdog.join();
  ZuCheckRT(injected && received && rxBytes == Bytes && !memcmp(received, observed, Bytes),
    log_("unmasked callback receives raw Ethernet bytes from the dedicated Rx pool"));
  auto pending = control.alloc();
  ZmAssert_(pending);
  memcpy(pending, observed, 64);
  bool queued = instance->send(pending, 64);
  if (queued) control.free(pending);
  ZuCheckRT(queued, log_("small Tx pool supplies independent registered packet backing"));
  // No CQ service: close must account for this submission using actual kernel
  // retirement, including when completion is already queued but unconsumed.
  bool closed = shutdown(instance, error);
  ZuCheckRT(closed && (!queued || !control.used()),
    log_("close proves kernel retirement before recycling pending backing: ", error));
  ZmAssert_(closed);
  if (received) {
    ZuCheckRT(!memcmp(received, observed, Bytes),
      log_("application-held Rx bytes survive instance destruction"));
    rx.free(received);
  }
  if (!queued) control.free(pending);
  if (recycled) tx.free(recycled);
  if (!submitted) tx.free(packet);
  ZuCheckRT(!rx.used() && !tx.used() && !control.used() && !rx.alloc() && !tx.alloc(),
    log_("retired pools accept final returns and refuse new allocations"));
  close(wire);
  Zxdp::LinkState after;
  ZuCheckRT(Zxdp::queryLink(ifindex, after, error) && !after.occupied(),
    log_("close removes only its own XDP attachment"));
}

static void testLeaseRace()
{
  ZuTestScopeRT(testLeaseRace);
  enum { Turns = 96 }; // 32 returns before CQ, after CQ, and concurrent with CQ
  Zxdp::BufPool rx{RxSlots, 1514}, tx{1, 64};
  ZtString<> error;
  auto instance = Zxdp::Instance::create({
    .interface = unsigned(strtoul(getenv("ZXDP_TEST_IFINDEX"), nullptr, 10)),
    .txPools = &tx, .rxPool = &rx,
    .rx = [&rx](uint8_t *p, unsigned) { rx.free(p); }}, error);
  bool ready = instance && instance->ready();
  ZuCheckRT(ready, log_("single-slot lease race setup: ", error));
  if (!ready) {
    if (instance) ZmAssert_(shutdown(instance, error));
    return;
  }
  ZmSemaphore release, released;
  uint8_t *returned = nullptr;
  ZmThread worker{[&release, &released, &returned, &tx] {
    for (;;) {
      release.wait();
      auto p = returned;
      if (!p) break;
      tx.free(p);
      released.post();
    }
  }};
  unsigned retired = 0, turns = 0;
  bool valid = true;
  auto deadline = Zm::now() + ZuTime{10};
  for (; turns < Turns && valid; ++turns) {
    auto p = tx.alloc();
    if (!p) { valid = false; break; }
    memset(p, 0, 64);
    memset(p, 0xff, 6);
    memcpy(p + 6, instance->mac(), 6);
    p[12] = 0x88; p[13] = 0xb5;
    bool accepted = instance->send(p, 64);
    returned = p;
    unsigned order = turns%3;
    if (order != 1) {
      release.post();
      if (!order) {
        released.wait();
        valid &= tx.used() == 1;
      }
    }
    unsigned completed = 0;
    while (accepted && !completed && Zm::now() < deadline) {
      completed += instance->pollTx();
      if (!completed) poll(nullptr, 0, 1);
    }
    if (order == 1) {
      valid &= tx.used() == 1;
      release.post();
    }
    if (order) released.wait();
    retired += completed;
    valid &= accepted && completed == 1 && !tx.used();
  }
  returned = nullptr;
  release.post();
  worker.join();
  ZuCheckRT(valid && turns == Turns && retired == Turns && !tx.used(),
    log_("cross-thread allocation returns and CQ retire each single-slot lease once"));
  bool closed = shutdown(instance, error);
  ZuCheckRT(closed && !rx.used() && !tx.used(), log_("lease race cleanup: ", error));
  ZmAssert_(closed);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  if (geteuid() || !getenv("ZXDP_TEST_IFINDEX") || !getenv("ZXDP_TEST_PEER_IFINDEX")) {
    std::cout << "1..0 # SKIP requires privileged fixture-owned veth indices\n";
    return 0;
  }
  ZuTestMain();
  ZuTestCall(testPackets);
  ZuTestCall(testLeaseRace);
}
