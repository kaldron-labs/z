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
#include <sys/syscall.h>
#include <unistd.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/if_xdp.h>

#include <zlib/ZuBox.hh>
#include <zlib/ZuID.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZuLib.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZtcQueue.hh>

#include <zlib/Zxdp.hh>
#include <zlib/ZxdpBPF.hh>

using namespace ZuTestUtil;

enum { Bytes = 96, RxSlots = 8, TxSlots = 2 }; // exact fixture workload
static ZmAtomic<unsigned> xdpKicks{0};
static ZmAtomic<int> xdpSender{0};
static ZmAtomic<unsigned> gateSender{0};
static ZmSemaphore senderEntered, senderRelease;
static ZmAtomic<unsigned> xdpSid{0}, gateAfter{0}, retryError{0}, budgetOnce{0};
static ZmAtomic<int> budgetResult{0};
static ZmSemaphore afterEntered, afterRelease;

extern "C" __attribute__((visibility("default")))
ssize_t sendto(int fd, const void *data, size_t size, int flags,
    const sockaddr *address, socklen_t length)
{
  bool xsk = !data && !size && !address && !length && (flags & MSG_DONTWAIT);
  if (xsk) {
    xdpSender = int(syscall(SYS_gettid));
    xdpSid = ZmSelf()->sid();
    ++xdpKicks;
    if (gateSender.xch(0)) {
      senderEntered.post();
      senderRelease.wait();
    }
    if (auto error = retryError.xch(0)) { errno = error; return -1; }
    if (auto budget = budgetOnce.xch(0))
      budgetResult = setsockopt(fd, SOL_XDP, XDP_MAX_TX_SKB_BUDGET, &budget, sizeof(budget));
  }
  auto n = syscall(SYS_sendto, fd, data, size, flags, address, length);
  int error = errno;
  if (xsk && (n >= 0 || error == EINTR || error == EAGAIN ||
      error == EBUSY || error == ENOBUFS) && gateAfter.xch(0)) {
    afterEntered.post();
    afterRelease.wait();
  }
  errno = error;
  return n;
}

static bool shutdown(ZmRef<Zxdp::Instance> &instance, ZeException &error)
{
  if (!instance) return true;
  auto deadline = Zm::now() + ZuTime{10};
  do {
    auto result = instance->close();
    error = {};
    if (result.is<bool>() && result.p<bool>()) { instance = nullptr; return true; }
    if (result.is<ZeException>()) error = result.p<ZeException>();
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
  ZeException error;
  Zxdp::Xdp::LinkState local, remote;
  auto localResult = Zxdp::Xdp::queryLink(ifindex);
  bool queried = false;
  if (localResult.is<ZeException>()) error = localResult.p<ZeException>();
  else {
    local = localResult.p<Zxdp::Xdp::LinkState>();
    auto remoteResult = Zxdp::Xdp::queryLink(peer);
    if (remoteResult.is<ZeException>()) error = remoteResult.p<ZeException>();
    else { remote = remoteResult.p<Zxdp::Xdp::LinkState>(); queried = true; }
  }
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
  auto instance = Zxdp::instance(Zxdp::Type::Xdp);
  auto rx = instance->bufPool(RxSlots, local.mtu + 14);
  auto tx = instance->bufPool(TxSlots, 128);
  auto control = instance->bufPool(1, 64);
  Zi::IOBufPoolList pools;
  pools.pushNode(tx.ptr());
  pools.pushNode(control.ptr());
  uint8_t *received = nullptr;
  unsigned rxBytes = 0, completions = 0;
  Zxdp::Instance *current = nullptr;
  Zxdp::Params config(
    Zxdp::Params::New<Zxdp::Params::Index<Zxdp::Xdp::Params>{}>{});
  auto &params = config.p<Zxdp::Xdp::Params>();
  params.ifindex = ifindex;
  params.txPools = ZuMv(pools);
  params.rxPool = rx;
  params.rx = [&received, &rxBytes, &rx, &current](uint8_t *p, unsigned n) {
    if (!received) { received = p; rxBytes = n; current->wake(); } else rx->free(p);
  };
  ZmScheduler scheduler{ZmSchedParams{}.nThreads(1).thread(1,
    [&params](auto &thread) { thread = params.txThread; })};
  params.scheduler = &scheduler;
  params.txSid = 1;
  bool started = scheduler.start();
  ZuCheckRT(started);
  if (!started) { close(wire); return; }
  auto initialized = instance->init(config);
  if (initialized.is<ZeException>()) error = initialized.p<ZeException>();
  bool ready = instance && instance->ready();
  ZuCheckRT(ready, log_("named parameters bind all interface queues: ", error));
  if (!ready) {
    if (instance) ZmAssert_(shutdown(instance, error));
    ZuCheckRT(scheduler.stop());
    close(wire);
    return;
  }
  std::cout << "# AF_XDP mode=" << (instance->zeroCopy() ? "zero-copy" : "copy") << '\n';
  unsigned liveQueues = 0, completionCapacity = 0;
  auto completionID = ZuID{} << "xdp." << ZuBoxed(ifindex) << ".0.cq";
  bool queueViews = true;
  unsigned enumerated = instance->allQueues([
    &liveQueues, &completionCapacity, &completionID, &queueViews
  ](Ztc::Queue *queue) {
    Ztc::QueueTelemetry data;
    queue->telemetry(data);
    ++liveQueues;
    if (data.id == completionID) completionCapacity = data.size;
    queueViews &= data.size && data.count <= data.size && !!data.id &&
      (data.type == Ztc::QueueType::Rx || data.type == Ztc::QueueType::Tx);
  });
  ZuCheckRT(queueViews && enumerated == liveQueues && liveQueues == local.rxQueues * 4,
    log_("actual RX/FILL/TX/CQ queues enumerate once per hardware queue"));
  ZuCheckRT(completionCapacity >= tx->count + control->count &&
      !(completionCapacity & (completionCapacity - 1)),
    log_("completion capacity covers every physical data/control slot "
      "with one descriptor per slot"));
  current = instance.ptr();
  auto kicks = unsigned(xdpKicks);
  ZuCheckRT(!instance->pollTx(),
    log_("empty Tx completion service reports no completions"));
  auto packet = tx->alloc();
  ZmAssert_(packet);
  auto output = packet + 1; // odd offsets exercise generic-kernel CQ address tagging
  ZuCheckRT(tx->writable(output) && !rx->writable(output) && !tx->writable(nullptr),
    log_("only a caller-held idle Tx allocation is writable"));
  memcpy(output, remote.mac, 6);
  memcpy(output + 6, local.mac, 6);
  output[12] = 0x88;
  output[13] = 0xb5;
  for (unsigned i = 14; i < Bytes; ++i) output[i] = (i * 37) ^ 0xa5;
  uint8_t expected[Bytes];
  memcpy(expected, output, Bytes);
  ZuCheckRT(!instance->send(nullptr, Bytes) &&
      !instance->send(packet, tx->size + 1) &&
      !instance->send(packet + 1, tx->size) && tx->writable(output) &&
      tx->used() == 1 && !instance->pollTx() && unsigned(xdpKicks) == kicks,
    log_("invalid submissions leave caller backing writable "
      "without a kernel lease or sender kick"));
  gateSender = 1;
  bool submitted = instance->send(output, Bytes);
  ZuCheckRT(submitted && !tx->writable(output) &&
      !instance->send(output, Bytes) && tx->used() == 1,
    log_("accepted submission holds a kernel lease and cannot resubmit before retirement"));
  if (submitted) tx->free(packet);
  ZuCheckRT(tx->used() == 1, log_("early pool return cannot recycle live kernel backing"));
  bool entered = submitted && !senderEntered.timedwait(Zm::now() + ZuTime{10});
  unsigned callbacks = 0;
  bool ordered = true;
  ZmSemaphore continued;
  if (entered) {
    for (unsigned i = 0; i < 8; ++i)
      scheduler.run([&callbacks, &ordered, i] {
	ordered &= callbacks++ == i;
      }, 1);
    scheduler.push([&continued] { continued.post(); }, 1);
  }
  gateSender = 0;
  senderRelease.post();
  bool resumed = entered && !continued.timedwait(Zm::now() + ZuTime{10});
  ZuCheckRT(resumed && ordered && callbacks == 8,
    log_("coalesced sender returns to queued callbacks in FIFO order after draining"));
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
  ZuCheckRT(captured && completions == 1 && !tx->used(),
    log_("peer sees exact arena bytes; CQ recycles backing directly to its pool"));
  ZuCheckRT(unsigned(xdpKicks) > kicks && unsigned(xdpSid) == 1 &&
      int(xdpSender) != int(syscall(SYS_gettid)),
    log_("AF_XDP sendto runs on the assigned sender sid, not the Tx owner"));
  ZmSemaphore idle;
  scheduler.run([] { }, 1);
  scheduler.push([&idle] { idle.post(); }, 1);
  bool yielded = !idle.timedwait(Zm::now() + ZuTime{10});
  ZuCheckRT(yielded,
    log_("empty Tx ring yields to queued scheduler work without another wake"));
  kicks = unsigned(xdpKicks);
  ZuCheckRT(!instance->pollTx() && unsigned(xdpKicks) == kicks,
    log_("retired Tx service makes no empty-ring sendto calls"));
  auto recycled = tx->alloc();
  ZuCheckRT(recycled == packet && tx->writable(recycled),
    log_("completion restores the exact originating slot and writable caller lease"));
  bool progressed = resumed && yielded && recycled && tx->writable(recycled);
  for (unsigned i = 0; i < 3 && progressed; ++i) {
    memcpy(recycled, expected, Bytes);
    bool sent = instance->send(recycled, Bytes);
    ZmSemaphore afterPacket;
    scheduler.run([&afterPacket] { afterPacket.post(); }, 1);
    progressed = sent && !afterPacket.timedwait(Zm::now() + ZuTime{10});
    unsigned completed = 0;
    auto deadline = Zm::now() + ZuTime{10};
    while (progressed && !completed && Zm::now() < deadline) {
      completed += instance->pollTx();
      if (!completed) poll(nullptr, 0, 1);
    }
    progressed &= completed == 1 && tx->writable(recycled);
  }
  ZuCheckRT(progressed,
    log_("successive zero-to-one publications each drain without orphaning their final packet"));
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
  auto pending = control->alloc();
  ZmAssert_(pending);
  memcpy(pending, observed, 64);
  bool queued = instance->send(pending, 64);
  if (queued) control->free(pending);
  ZuCheckRT(queued, log_("small Tx pool supplies independent registered packet backing"));
  // No CQ service: close must account for this submission using actual kernel
  // retirement, including when completion is already queued but unconsumed.
  auto retiredInstance = instance;
  config.p<Zxdp::Xdp::Params>().txPools.clean();
  config.p<Zxdp::Xdp::Params>().rxPool = nullptr;
  auto rxPtr = rx.ptr(), txPtr = tx.ptr(), controlPtr = control.ptr();
  rx = tx = control = nullptr;
  ZuCheckRT(rxPtr->refCount() > 0 && txPtr->refCount() > 0 &&
      controlPtr->refCount() > 0,
    log_("instance owns Rx and every Tx pool after caller references are released"));
  bool closed = shutdown(instance, error);
  ZuCheckRT(closed && !rxPtr->ready() && !txPtr->ready() && !controlPtr->ready(),
    log_("instance-owned pools survive kernel retirement through completed close"));
  rx = rxPtr;
  tx = txPtr;
  control = controlPtr;
  ZuCheckRT(closed && (!queued || !control->used()),
    log_("close proves kernel retirement before recycling pending backing: ", error));
  ZmAssert_(closed);
  unsigned frozenQueues = 0;
  queueViews = true;
  retiredInstance->allQueues([&frozenQueues, &queueViews](Ztc::Queue *queue) {
    Ztc::QueueTelemetry data;
    queue->telemetry(data);
    ++frozenQueues;
    queueViews &= data.size && data.count <= data.size;
  });
  ZuCheckRT(queueViews && frozenQueues == liveQueues,
    log_("closed queues retain final telemetry without descriptor mappings"));
  retiredInstance = nullptr;
  ZuCheckRT(scheduler.stop());
  if (received) {
    ZuCheckRT(!memcmp(received, observed, Bytes),
      log_("application-held Rx bytes survive instance destruction"));
    rx->free(received);
  }
  if (!queued) control->free(pending);
  if (recycled) tx->free(recycled);
  if (!submitted) tx->free(packet);
  ZuCheckRT(!rx->used() && !tx->used() && !control->used() && !rx->alloc() && !tx->alloc(),
    log_("retired pools accept final returns and refuse new allocations"));
  close(wire);
  auto after = Zxdp::Xdp::queryLink(ifindex);
  if (after.is<ZeException>()) error = after.p<ZeException>();
  ZuCheckRT(after.is<Zxdp::Xdp::LinkState>() && !after.p<Zxdp::Xdp::LinkState>().occupied(),
    log_("close removes only its own XDP attachment"));
}

static void testSenderProgress()
{
  ZuTestScopeRT(testSenderProgress);
  enum { Budget = 32, Initial = 80, More = 16, Total = Initial + More + 1 };
  auto instance = Zxdp::instance(Zxdp::Type::Xdp);
  auto rx = instance->bufPool(RxSlots, 1514);
  auto tx = instance->bufPool(Initial + More, 128);
  ZeException error;
  ZmSemaphore stopped;
  unsigned stops = 0, stopSid = 0;
  Zxdp::Params config(
    Zxdp::Params::New<Zxdp::Params::Index<Zxdp::Xdp::Params>{}>{});
  auto &params = config.p<Zxdp::Xdp::Params>();
  params.ifindex = unsigned(strtoul(getenv("ZXDP_TEST_IFINDEX"), nullptr, 10));
  params.rxPool = rx;
  params.rx = [&rx](uint8_t *p, unsigned) { rx->free(p); };
  params.txStopped = [&stopped, &stops, &stopSid] {
    ++stops;
    stopSid = ZmSelf()->sid();
    stopped.post();
  };
  params.txPools.pushNode(tx.ptr());
  ZmScheduler scheduler{ZmSchedParams{}.nThreads(1).thread(1,
    [&params](auto &thread) { thread = params.txThread; })};
  params.scheduler = &scheduler;
  params.txSid = 1;
  bool started = scheduler.start();
  ZuCheckRT(started);
  if (!started) return;
  auto initialized = instance->init(config);
  if (initialized.is<ZeException>()) error = initialized.p<ZeException>();
  bool ready = instance && instance->ready();
  ZuCheckRT(ready, log_("coalesced sender fixture: ", error));
  if (!ready) {
    if (instance) ZmAssert_(shutdown(instance, error));
    ZuCheckRT(scheduler.stop());
    return;
  }
  unsigned accepted = 0, retired = 0;
  auto publish = [&instance, &tx, &accepted](unsigned count) {
    while (count--) {
      auto p = tx->alloc();
      if (!p) return false;
      memset(p, 0, Bytes);
      memset(p, 0xff, 6);
      memcpy(p + 6, instance->mac().data(), 6);
      p[12] = 0x88; p[13] = 0xb5;
      p[14] = accepted;
      bool sent = instance->send(p, Bytes);
      tx->free(p); // only the independent kernel lease remains after acceptance
      if (!sent) return false;
      ++accepted;
    }
    return true;
  };
  auto drain = [&instance, &retired](unsigned expected) {
    unsigned n = 0;
    auto deadline = Zm::now() + ZuTime{10};
    while (n < expected && Zm::now() < deadline) {
      n += instance->pollTx();
      if (n < expected) poll(nullptr, 0, 1);
    }
    retired += n;
    return n == expected;
  };
  auto await = [](ZmSemaphore &sem) {
    return !sem.timedwait(Zm::now() + ZuTime{10});
  };
  auto kicks = unsigned(xdpKicks);
  gateSender = 1;
  gateAfter = 1;
  retryError = EAGAIN;
  budgetOnce = Budget; // test-only smaller per-syscall budget; production ring remains 32K
  budgetResult = -1;
  bool first = publish(1) && await(senderEntered);
  bool queued = first && publish(Initial - 1);
  gateSender = 0;
  senderRelease.post();
  bool partial = queued && await(afterEntered) && !int(budgetResult) &&
    !unsigned(retryError) && drain(Budget);
  // The sender is after its first real kernel drain, before consumer accounting.
  // Further producer increments must keep this same invocation responsible.
  bool appended = partial && publish(More);
  if (appended) gateAfter = 1;
  afterRelease.post();
  bool second = appended && await(afterEntered) && drain(Budget);
  if (second) gateAfter = 1;
  afterRelease.post();
  bool last = second && await(afterEntered) && drain(Budget);
  // All preceding descriptors are now consumed, but their final subtraction
  // has not happened. This publication must not disappear behind that return.
  bool tail = last && publish(1);
  unsigned callbacks = 0;
  bool ordered = true;
  ZmSemaphore done;
  if (tail) {
    for (unsigned i = 0; i < 8; ++i)
      scheduler.run([&callbacks, &ordered, i] { ordered &= callbacks++ == i; }, 1);
    scheduler.run([&done] { done.post(); }, 1);
  }
  gateAfter = 0;
  afterRelease.post();
  bool progressed = tail && await(done) && drain(1);
  ZuCheckRT(partial && second && last,
    log_("EAGAIN retry and three real bounded 32-descriptor drains preserve pending work"));
  ZuCheckRT(progressed && accepted == Total && retired == Total && !tx->used() &&
      ordered && callbacks == 8 && unsigned(xdpSid) == 1 && unsigned(xdpKicks) >= kicks + 5,
    log_("concurrent publication and the last-drain race retire all 97 leases "
      "without another wake"));
  kicks = unsigned(xdpKicks);
  if (progressed) {
    scheduler.run([&done] { done.post(); }, 1);
    progressed = await(done);
  }
  ZuCheckRT(progressed && unsigned(xdpKicks) == kicks,
    log_("idle sender performs no sendto after its final descriptor"));

  gateSender = 1;
  bool final = progressed && publish(1) && await(senderEntered);
  bool deferred = final && !instance->stopSender();
  bool earlyStop = !stopped.timedwait(Zm::now());
  gateSender = 0;
  retryError = budgetOnce = gateAfter = 0;
  senderRelease.post();
  afterRelease.post();
  bool exited = deferred && !earlyStop && await(stopped);
  ZuCheckRT(exited && stops == 1 && stopSid == 1,
    log_("shutdown waits for the active sendto callback and its FIFO sender barrier"));
  bool closed = shutdown(instance, error);
  ZuCheckRT(closed && !rx->used() && !tx->used(),
    log_("shutdown retires the final unpolled CQ lease exactly once: ", error));
  ZmAssert_(closed);
  ZuCheckRT(scheduler.stop());
}

static void testLeaseRace()
{
  ZuTestScopeRT(testLeaseRace);
  enum { Turns = 96 }; // 32 returns before CQ, after CQ, and concurrent with CQ
  auto instance = Zxdp::instance(Zxdp::Type::Xdp);
  auto rx = instance->bufPool(RxSlots, 1514);
  auto tx = instance->bufPool(1, 64);
  ZeException error;
  Zxdp::Params config(
    Zxdp::Params::New<Zxdp::Params::Index<Zxdp::Xdp::Params>{}>{});
  auto &params = config.p<Zxdp::Xdp::Params>();
  params.ifindex = unsigned(strtoul(getenv("ZXDP_TEST_IFINDEX"), nullptr, 10));
  params.rxPool = rx;
  params.rx = [&rx](uint8_t *p, unsigned) { rx->free(p); };
  params.txPools.pushNode(tx.ptr());
  ZmScheduler scheduler{ZmSchedParams{}.nThreads(1).thread(1,
    [&params](auto &thread) { thread = params.txThread; })};
  params.scheduler = &scheduler;
  params.txSid = 1;
  bool started = scheduler.start();
  ZuCheckRT(started);
  if (!started) return;
  auto initialized = instance->init(config);
  if (initialized.is<ZeException>()) error = initialized.p<ZeException>();
  bool ready = instance && instance->ready();
  ZuCheckRT(ready, log_("single-slot lease race setup: ", error));
  if (!ready) {
    if (instance) ZmAssert_(shutdown(instance, error));
    ZuCheckRT(scheduler.stop());
    return;
  }
  ZmSemaphore release, released;
  uint8_t *returned = nullptr;
  ZmThread worker{[&release, &released, &returned, &tx] {
    for (;;) {
      release.wait();
      auto p = returned;
      if (!p) break;
      tx->free(p);
      released.post();
    }
  }};
  unsigned retired = 0, turns = 0;
  bool valid = true;
  auto deadline = Zm::now() + ZuTime{10};
  for (; turns < Turns && valid; ++turns) {
    auto p = tx->alloc();
    if (!p) { valid = false; break; }
    memset(p, 0, 64);
    memset(p, 0xff, 6);
    memcpy(p + 6, instance->mac().data(), 6);
    p[12] = 0x88; p[13] = 0xb5;
    bool accepted = instance->send(p, 64);
    returned = p;
    unsigned order = turns%3;
    if (order != 1) {
      release.post();
      if (!order) {
        released.wait();
        valid &= tx->used() == 1;
      }
    }
    unsigned completed = 0;
    while (accepted && !completed && Zm::now() < deadline) {
      completed += instance->pollTx();
      if (!completed) poll(nullptr, 0, 1);
    }
    if (order == 1) {
      valid &= tx->used() == 1;
      release.post();
    }
    if (order) released.wait();
    retired += completed;
    valid &= accepted && completed == 1 && !tx->used();
  }
  returned = nullptr;
  release.post();
  worker.join();
  ZuCheckRT(valid && turns == Turns && retired == Turns && !tx->used(),
    log_("cross-thread allocation returns and CQ retire each single-slot lease once"));
  bool closed = shutdown(instance, error);
  ZuCheckRT(closed && !rx->used() && !tx->used(), log_("lease race cleanup: ", error));
  ZmAssert_(closed);
  ZuCheckRT(scheduler.stop());
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
  ZuTestCall(testSenderProgress);
  ZuTestCall(testLeaseRace);
}
