//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
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
#include <linux/netlink.h>

#include <zlib/ZuBox.hh>
#include <zlib/ZuID.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZuLib.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZiMultiplex.hh>
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

#include "zxdpfixture.hh"

using namespace ZuTestUtil;

enum { Bytes = 96, RxSlots = 8, TxSlots = 2, Sender = 3 }; // exact fixture workload
static ZmAtomic<unsigned> xdpKicks{0};
static ZmAtomic<int> xdpSender{0};
static ZmAtomic<unsigned> gateSender{0};
static ZmSemaphore senderEntered, senderRelease;
static ZmAtomic<unsigned> xdpSid{0}, gateAfter{0}, retryError{0}, budgetOnce{0};
static ZmAtomic<int> budgetResult{0};
static ZmSemaphore afterEntered, afterRelease;
static ZmAtomic<unsigned> xdpSockets{0}, xdpBinds{0}, xdpRegistrations{0};
static ZmAtomic<unsigned> routeSockets{0};
static int registrationFD = -1;
static unsigned failServiceBind = 0;
static bool failRegistration = false;

extern "C" __attribute__((visibility("default")))
int socket(int domain, int type, int protocol) noexcept
{
  if (domain == AF_XDP) ++xdpSockets;
  if (domain == AF_NETLINK && protocol == NETLINK_ROUTE) ++routeSockets;
  return syscall(SYS_socket, domain, type, protocol);
}

extern "C" __attribute__((visibility("default")))
int bind(int fd, const sockaddr *address, socklen_t length) noexcept
{
  if (address && length >= sizeof(sa_family_t) && address->sa_family == AF_XDP) {
    ++xdpBinds;
    if (failServiceBind && !--failServiceBind) { errno = EBUSY; return -1; }
  }
  return syscall(SYS_bind, fd, address, length);
}

extern "C" __attribute__((visibility("default")))
int setsockopt(int fd, int level, int option, const void *value, socklen_t length) noexcept
{
  if (level == SOL_XDP && option == XDP_UMEM_REG) {
    ++xdpRegistrations;
    registrationFD = fd;
    if (failRegistration) { errno = ENOMEM; return -1; }
  }
  return syscall(SYS_setsockopt, fd, level, option, value, length);
}

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
  bool ok = instance->stop();
  error = instance->error();
  ZmAssert_(instance->state() == ZmEngineState::Stopped);
  return ok;
}

static void configureSender(ZmSchedParams &scheduler)
{
  scheduler.thread(Sender, [](auto &thread) { thread.isolated(true); });
}

static bool setup(
  Zxdp::Instance *instance, ZiMultiplex &mx, const Zxdp::Params &config,
  Zi::IOBufPool *rx, const Zi::IOBufPoolList &pools, Zxdp::RecvFn receive,
  ZeException &error, Zxdp::StopFn stopped = {})
{
  error = ZxdpTest::start(instance, mx, instance->init(&mx, config), rx, pools,
    ZuMv(receive), ZuMv(stopped));
  return instance->ready();
}

static unsigned waitTx(Zxdp::Instance *instance, unsigned expected, ZuTime deadline)
{
  // AF_XDP provides no user-space wait for the CQ. Fixture-controlled ordering
  // is gated separately before entering this bounded kernel completion probe.
  unsigned completed = 0;
  while (completed < expected && Zm::now() < deadline) {
    completed += instance->pollTx();
    if (completed < expected) poll(nullptr, 0, 1);
  }
  return completed;
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
  auto rx = instance->bufPool(Zi::IOBufType::Rx, RxSlots, local.mtu + 14);
  auto tx = instance->bufPool(Zi::IOBufType::TxTCP, TxSlots, 128);
  auto control = instance->bufPool(Zi::IOBufType::TxUDP, 1, 64);
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
  params.txSid = Sender;
  ZiMultiplex mx{ZiMxParams{}.scheduler(configureSender)};
  auto receive = [&received, &rxBytes, &rx, &current](uint8_t *p, unsigned n) {
    if (!received) { received = p; rxBytes = n; current->wake(); } else rx->free(p);
  };
  auto beforeInit = unsigned(routeSockets);
  auto initialized = instance->init(&mx, config);
  ZuCheckRT(initialized.is<void>() && instance->mx() == &mx &&
      instance->ifindex() == ifindex && instance->mtu() == local.mtu &&
      instance->rxQueues() == local.rxQueues && !instance->allocated(),
    log_("synchronous init exposes link metadata before registration"));
  if (initialized.is<ZeException>()) { close(wire); return; }
  auto repeated = instance->init(&mx, config);
  ++params.spin;
  auto conflict = instance->init(&mx, config);
  --params.spin;
  ZuCheckRT(repeated.is<void>() && conflict.is<ZeException>() && instance->ifindex() == ifindex &&
      routeSockets == beforeInit + 1,
    log_("identical stopped init succeeds and conflicting config preserves the link"));
  auto registrations = unsigned(xdpRegistrations);
  failRegistration = true;
  auto failed = instance->alloc(rx, pools);
  failRegistration = false;
  ZuCheckRT(failed.is<ZeException>() && !instance->allocated() &&
      !rx->alloc() && !tx->alloc() && !control->alloc(),
    log_("registration failure leaves pool backing unpublished"));
  ++registrations;
  auto allocated = instance->alloc(rx, pools);
  int fd = registrationFD;
  ZuCheckRT(allocated.is<void>() && instance->alloc(rx, pools).is<void>() &&
      unsigned(xdpRegistrations) == registrations + 1 &&
      instance->alloc(rx, {}).is<ZeException>(),
    log_("same pools reuse the sole registration and conflicting allocation fails"));
  if (allocated.is<ZeException>()) { instance->final(); close(wire); return; }
  auto held = tx->alloc();
  ZmAssert_(held);
  memset(held, 0x5a, tx->size);
  bool started = mx.start();
  if (started) {
    failServiceBind = 2;
    auto activated = instance->start(receive);
    failServiceBind = 0;
    ZuCheckRT(!activated && instance->allocated() && instance->error() &&
        !instance->allQueues([](Ztc::Queue *) { }) && held[0] == 0x5a,
      log_("partial activation rolls back the run and keeps the registration live"));
  }
  bool ready = started && instance->start(receive);
  ZuCheckRT(ready, log_("registered backing activates every interface queue: ", instance->error()));
  if (!ready) {
    tx->free(held);
    instance->final();
    if (started) ZuCheckRT(mx.stop());
    close(wire);
    return;
  }
  auto idleSockets = unsigned(xdpSockets), idleBinds = unsigned(xdpBinds);
  ZuCheckRT(instance->stop() && instance->allocated() &&
      !instance->allQueues([](Ztc::Queue *) { }) &&
      unsigned(xdpBinds) == idleBinds && unsigned(xdpSockets) == idleSockets &&
      held[0] == 0x5a && held[tx->size - 1] == 0x5a,
    log_("idle stop preserves registration and held backing without socket creation or bind"));
  ZuCheckRT(instance->start(receive) && unsigned(xdpRegistrations) == registrations + 1 &&
      fcntl(fd, F_GETFD) >= 0 && held[0] == 0x5a && held[tx->size - 1] == 0x5a,
    log_("restart reopens service on the same registered packet storage"));
  tx->free(held);
  std::cout << "# AF_XDP mode=" << (instance->zeroCopy() ? "zero-copy" : "copy") << '\n';
  unsigned liveQueues = 0, completionCapacity = 0, txCapacity = 0;
  auto completionID = ZuID{} << "xdp." << ZuBoxed(ifindex) << ".0.cq";
  auto txID = ZuID{} << "xdp." << ZuBoxed(ifindex) << ".0.tx";
  bool queueViews = true;
  unsigned enumerated = instance->allQueues([
    &liveQueues, &completionCapacity, &completionID, &txCapacity, &txID, &queueViews
  ](Ztc::Queue *queue) {
    Ztc::QueueTelemetry data;
    queue->telemetry(data);
    ++liveQueues;
    if (data.id == completionID) completionCapacity = data.size;
    if (data.id == txID) txCapacity = data.size;
    queueViews &= data.size && data.count <= data.size && !!data.id &&
      (data.type == Ztc::QueueType::Rx || data.type == Ztc::QueueType::Tx);
  });
  ZuCheckRT(queueViews && enumerated == liveQueues && liveQueues == local.rxQueues * 2 + 2,
    log_("RX/FILL enumerate per queue and one TX/CQ pair serves the sender"));
  ZuCheckRT(txCapacity == completionCapacity &&
      completionCapacity >= tx->count + control->count &&
      completionCapacity/2 < tx->count + control->count &&
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
  uint8_t expected[Bytes];
  memcpy(expected, output, Bytes);
  ZuCheckRT(!instance->send(nullptr, Bytes) &&
      !instance->send(packet, tx->size + 1) &&
      !instance->send(packet + 1, tx->size) && !instance->pollTx() && unsigned(xdpKicks) == kicks,
    log_("invalid submissions leave caller backing unchanged "
      "without a kernel lease or sender kick"));
  gateSender = 1;
  bool submitted = instance->send(output, Bytes);
  ZuCheckRT(submitted && !instance->send(output, Bytes),
    log_("accepted submission holds a kernel lease and cannot resubmit before retirement"));
  if (submitted) tx->free(packet);
  auto spare = tx->alloc();
  ZuCheckRT(spare && spare != packet && !tx->alloc(),
    log_("early pool return cannot recycle live kernel backing"));
  if (spare) tx->free(spare);
  bool entered = submitted && !senderEntered.timedwait(Zm::now() + ZuTime{10});
  unsigned callbacks = 0;
  bool ordered = true;
  ZmSemaphore continued;
  if (entered) {
    for (unsigned i = 0; i < 8; ++i)
      mx.run([&callbacks, &ordered, i] {
	ordered &= callbacks++ == i;
      }, Sender);
    mx.push([&continued] { continued.post(); }, Sender);
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
  ZuCheckRT(captured && completions == 1,
    log_("peer sees exact arena bytes; CQ recycles backing directly to its pool"));
  ZuCheckRT(unsigned(xdpKicks) > kicks && unsigned(xdpSid) == Sender &&
      int(xdpSender) != int(syscall(SYS_gettid)),
    log_("AF_XDP sendto runs on the assigned sender sid, not the Tx owner"));
  ZmSemaphore idle;
  mx.run([] { }, Sender);
  mx.push([&idle] { idle.post(); }, Sender);
  bool yielded = !idle.timedwait(Zm::now() + ZuTime{10});
  ZuCheckRT(yielded,
    log_("empty Tx ring yields to queued scheduler work without another wake"));
  kicks = unsigned(xdpKicks);
  ZuCheckRT(!instance->pollTx() && unsigned(xdpKicks) == kicks,
    log_("retired Tx service makes no empty-ring sendto calls"));
  auto recycled = tx->alloc();
  ZuCheckRT(recycled == packet,
    log_("completion restores the exact originating slot and caller lease"));
  bool progressed = resumed && yielded && recycled;
  for (unsigned i = 0; i < 3 && progressed; ++i) {
    memcpy(recycled, expected, Bytes);
    bool sent = instance->send(recycled, Bytes);
    ZmSemaphore afterPacket;
    mx.run([&afterPacket] { afterPacket.post(); }, Sender);
    progressed = sent && !afterPacket.timedwait(Zm::now() + ZuTime{10});
    if (progressed) progressed = waitTx(instance, 1, Zm::now() + ZuTime{10}) == 1;
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
  // Stop must finish the accepted submission even without an application CQ poll.
  auto sockets = unsigned(xdpSockets), binds = unsigned(xdpBinds);
  bool closed = shutdown(instance, error);
  ZuCheckRT(closed && !instance->allQueues([](Ztc::Queue *) { }) &&
      unsigned(xdpSockets) == sockets && unsigned(xdpBinds) == binds &&
      unsigned(xdpRegistrations) == registrations + 1,
    log_("stop drains the pending completion and releases active queues: ", error));
  if (queued) pending = control->alloc();
  ZuCheckRT(pending, log_("completed backing returns to the original pool after stop"));
  if (pending) control->free(pending);
  if (received) {
    ZuCheckRT(!memcmp(received, observed, Bytes),
      log_("application-held receive bytes survive stop"));
    rx->free(received);
  }
  if (recycled) tx->free(recycled);
  if (!submitted) tx->free(packet);
  instance->final();
  ZuCheckRT(!rx->alloc() && !tx->alloc() && !instance->allocated() &&
      fcntl(fd, F_GETFD) < 0 && errno == EBADF,
    log_("final retires pool backing and registration"));
  ZuCheckRT(mx.stop());
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
  auto rx = instance->bufPool(Zi::IOBufType::Rx, RxSlots, 1514);
  auto tx = instance->bufPool(Zi::IOBufType::TxTCP, Initial + More, 128);
  ZeException error;
  ZmSemaphore stopped;
  unsigned stops = 0, stopSid = 0;
  Zxdp::Params config(
    Zxdp::Params::New<Zxdp::Params::Index<Zxdp::Xdp::Params>{}>{});
  auto &params = config.p<Zxdp::Xdp::Params>();
  params.ifindex = unsigned(strtoul(getenv("ZXDP_TEST_IFINDEX"), nullptr, 10));
  params.txSid = Sender;
  ZiMultiplex mx{ZiMxParams{}.scheduler(configureSender)};
  Zi::IOBufPoolList pools;
  pools.pushNode(tx.ptr());
  bool ready = setup(instance, mx, config, rx, pools,
    [&rx](uint8_t *p, unsigned) { rx->free(p); }, error, [&stopped, &stops, &stopSid] {
      ++stops;
      stopSid = ZmSelf()->sid();
      stopped.post();
    });
  ZuCheckRT(ready, log_("coalesced sender fixture: ", error));
  if (!ready) {
    ZmAssert_(shutdown(instance, error));
    instance->final();
    if (mx.running()) ZuCheckRT(mx.stop());
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
    auto n = waitTx(instance, expected, Zm::now() + ZuTime{10});
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
  budgetOnce = Budget; // test-only override of the per-syscall budget
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
      mx.run([&callbacks, &ordered, i] { ordered &= callbacks++ == i; }, Sender);
    mx.run([&done] { done.post(); }, Sender);
  }
  gateAfter = 0;
  afterRelease.post();
  bool progressed = tail && await(done) && drain(1);
  ZuCheckRT(partial && second && last,
    log_("EAGAIN retry and three real bounded 32-descriptor drains preserve pending work"));
  ZuCheckRT(progressed && accepted == Total && retired == Total &&
      ordered && callbacks == 8 && unsigned(xdpSid) == Sender && unsigned(xdpKicks) >= kicks + 5,
    log_("concurrent publication and the last-drain race retire all 97 leases "
      "without another wake"));
  kicks = unsigned(xdpKicks);
  if (progressed) {
    mx.run([&done] { done.post(); }, Sender);
    progressed = await(done);
  }
  ZuCheckRT(progressed && unsigned(xdpKicks) == kicks,
    log_("idle sender performs no sendto after its final descriptor"));

  gateSender = 1;
  bool final = progressed && publish(1) && await(senderEntered);
  ZmSemaphore stopComplete;
  ZmAtomic<unsigned> stopResult{0};
  if (final) instance->stop([&stopComplete, &stopResult](bool ok) {
    stopResult = ok ? 2 : 1;
    stopComplete.post();
  });
  bool earlyStop = !stopped.timedwait(Zm::now());
  gateSender = 0;
  retryError = budgetOnce = gateAfter = 0;
  senderRelease.post();
  afterRelease.post();
  bool exited = final && !earlyStop && await(stopped) && await(stopComplete) && stopResult == 2;
  ZuCheckRT(exited && stops == 1 && stopSid == Sender,
    log_("shutdown waits for the active sendto callback and its FIFO sender barrier"));
  bool closed = shutdown(instance, error);
  ZuCheckRT(closed,
    log_("shutdown retires the final unpolled CQ lease exactly once: ", error));
  ZmAssert_(closed);
  instance->final();
  ZuCheckRT(mx.stop());
}

static void testLeaseRace()
{
  ZuTestScopeRT(testLeaseRace);
  enum { Turns = 96 }; // 32 returns before CQ, after CQ, and concurrent with CQ
  auto instance = Zxdp::instance(Zxdp::Type::Xdp);
  auto rx = instance->bufPool(Zi::IOBufType::Rx, RxSlots, 1514);
  auto tx = instance->bufPool(Zi::IOBufType::TxTCP, 1, 64);
  ZeException error;
  Zxdp::Params config(
    Zxdp::Params::New<Zxdp::Params::Index<Zxdp::Xdp::Params>{}>{});
  auto &params = config.p<Zxdp::Xdp::Params>();
  params.ifindex = unsigned(strtoul(getenv("ZXDP_TEST_IFINDEX"), nullptr, 10));
  params.txSid = Sender;
  ZiMultiplex mx{ZiMxParams{}.scheduler(configureSender)};
  Zi::IOBufPoolList pools;
  pools.pushNode(tx.ptr());
  auto receive = [&rx](uint8_t *p, unsigned) { rx->free(p); };
  bool ready = setup(instance, mx, config, rx, pools, receive, error);
  ZuCheckRT(ready, log_("single-slot lease race setup: ", error));
  if (!ready) {
    ZmAssert_(shutdown(instance, error));
    instance->final();
    if (mx.running()) ZuCheckRT(mx.stop());
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
        valid &= !tx->alloc();
      }
    }
    auto completed = accepted ? waitTx(instance, 1, deadline) : 0;
    if (order == 1) {
      valid &= !tx->alloc();
      release.post();
    }
    if (order) released.wait();
    retired += completed;
    valid &= accepted && completed == 1;
  }
  returned = nullptr;
  release.post();
  worker.join();
  ZuCheckRT(valid && turns == Turns && retired == Turns,
    log_("cross-thread allocation returns and CQ retire each single-slot lease once"));
  bool closed = shutdown(instance, error);
  ZuCheckRT(closed, log_("lease race cleanup: ", error));
  ZmAssert_(closed);
  bool reopened = instance->start(receive);
  ZuCheckRT(reopened, log_("same XDP instance reopens with its original drained pools"));
  bool sent = false;
  unsigned completed = 0;
  if (reopened) {
    if (auto p = tx->alloc()) {
      memset(p, 0, 64);
      memset(p, 0xff, 6);
      memcpy(p + 6, instance->mac().data(), 6);
      p[12] = 0x88; p[13] = 0xb5;
      sent = instance->send(p, 64);
      tx->free(p);
    }
    if (sent) completed = waitTx(instance, 1, Zm::now() + ZuTime{10});
  }
  ZuCheckRT(sent && completed == 1,
    log_("reopened instance restarts sender accounting and retires its first submission once"));
  ZmAssert_(shutdown(instance, error));
  instance->final();
  ZuCheckRT(mx.stop());
}

static void testNeverStarted()
{
  ZuTestScopeRT(testNeverStarted);
  ZiMultiplex mx{ZiMxParams{}.scheduler(configureSender)};
  auto instance = Zxdp::instance(Zxdp::Type::Xdp);
  Zxdp::Params config(
    Zxdp::Params::New<Zxdp::Params::Index<Zxdp::Xdp::Params>{}>{});
  auto &params = config.p<Zxdp::Xdp::Params>();
  params.ifindex = unsigned(strtoul(getenv("ZXDP_TEST_IFINDEX"), nullptr, 10));
  params.txSid = Sender;
  auto initialized = instance->init(&mx, config);
  ZuCheckRT(initialized.is<void>() && instance->mx() == &mx);
  if (initialized.is<ZeException>()) return;
  auto rx = instance->bufPool(Zi::IOBufType::Rx, instance->rxQueues(), instance->mtu() + 14);
  auto tx = instance->bufPool(Zi::IOBufType::TxUDP, 2, 128);
  Zi::IOBufPoolList pools;
  pools.pushNode(tx.ptr());
  auto allocated = instance->alloc(rx, pools);
  bool ready = allocated.is<void>() && instance->allocated() && !instance->everStarted();
  ZuCheckRT(ready);
  if (!ready) { instance->final(); return; }
  int fd = registrationFD;
  auto buf = tx->alloc();
  ZuCheckRT(buf && fcntl(fd, F_GETFD) >= 0 && !instance->ready());
  if (buf) tx->free(buf);
  instance->final();
  ZuCheckRT(!instance->allocated() && !instance->mx() && !tx->alloc() &&
      fcntl(fd, F_GETFD) < 0 && errno == EBADF);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  if (geteuid() || !getenv("ZXDP_TEST_IFINDEX") || !getenv("ZXDP_TEST_PEER_IFINDEX")) {
    std::cout << "1..0 # SKIP requires privileged fixture-owned veth indices\n";
    return 0;
  }
  auto test = getenv("ZXDP_PACKET_CASE");
  if (!test || !strcmp(test, "packets")) {
    ZuTestMain();
    ZuTestCall(testPackets);
  } else if (!strcmp(test, "sender")) {
    ZuTestMain();
    ZuTestCall(testSenderProgress);
  } else if (!strcmp(test, "leases")) {
    ZuTestMain();
    ZuTestCall(testLeaseRace);
  } else if (!strcmp(test, "unstarted")) {
    ZuTestMain();
    ZuTestCall(testNeverStarted);
  } else {
    std::cerr << "ZXDP_PACKET_CASE must be packets, sender, leases, or unstarted\n";
    return 2;
  }
}
