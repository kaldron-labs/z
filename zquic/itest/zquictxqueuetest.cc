//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/Zquic.hh>

#include "../test/ZquicFiniteProvider.hh"

#ifdef linux
#include <errno.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

// Tx-owner-only fault injection around the real epoll UDP system call. Once
// released, all packets traverse ZiConnection's unmodified common send loop.
struct SendProbe {
  uint16_t port = 0;
  int socket = -1;
  bool hold = false;
  unsigned blocked = 0, sent = 0;
  bool shortResult = false, suffix = false;
  const uint8_t *original = nullptr;
  size_t originalLength = 0, suffixLength = 0;
  unsigned allowance = 0;
};
static SendProbe sendProbe;

extern "C" __attribute__((visibility("default")))
ssize_t sendmsg(int socket, const struct msghdr *msg, int flags)
{
  if (sendProbe.port && sendProbe.socket < 0) {
    struct sockaddr_in addr;
    socklen_t size = sizeof(addr);
    if (!getsockname(socket, reinterpret_cast<struct sockaddr *>(&addr), &size) &&
	addr.sin_family == AF_INET && ntohs(addr.sin_port) == sendProbe.port)
      sendProbe.socket = socket;
  }
  if (sendProbe.socket == socket && sendProbe.hold) {
    if (sendProbe.allowance) --sendProbe.allowance;
    else {
      ++sendProbe.blocked;
      errno = EAGAIN;
      return -1;
    }
  }
  if (sendProbe.socket == socket && sendProbe.shortResult && msg->msg_iovlen == 1) {
    auto &iov = msg->msg_iov[0];
    if (!sendProbe.original) {
      sendProbe.original = static_cast<const uint8_t *>(iov.iov_base);
      sendProbe.originalLength = iov.iov_len;
      return 1; // characterize the frozen writer's positive-short contract
    }
    sendProbe.suffixLength = iov.iov_len;
    sendProbe.suffix = iov.iov_base == sendProbe.original + 1 &&
      iov.iov_len == sendProbe.originalLength - 1;
  }
  auto n = syscall(SYS_sendmsg, socket, msg, flags);
  if (sendProbe.socket == socket && n >= 0) ++sendProbe.sent;
  return n;
}

static uint64_t nanosecs(clockid_t clock = CLOCK_MONOTONIC)
{
  struct timespec t;
  clock_gettime(clock, &t);
  return uint64_t(t.tv_sec)*1000000000 + t.tv_nsec;
}
#endif

using namespace ZuTestUtil;

struct Endpoint : public Zquic_::Endpoint_<Endpoint> {
  using Base = Zquic_::Endpoint_<Endpoint>;
  using ReadyFn = ZmFn<void(bool), ZmFnHeapID<"Zquic.TxQueue.Ready">>;
  enum { EndpointRef = false };

  ReadyFn readyFn;
  ZmFn<> downFn;
  ZmSemaphore done;
  unsigned received = 0, target = 3, last = 0;
  unsigned length = 0;
  uint8_t bytes[sizeof(unsigned)]{};
  uint16_t source = 0;
  bool ordered = true, gaps = false;
  ZiConnection *physical = nullptr; // valid until endpoint close/rebind

  void endpointReady(Base *) { auto fn = ZuMv(readyFn); fn(true); }
  void endpointFailed(bool) { auto fn = ZuMv(readyFn); fn(false); }
  void endpointDown_(Base *) { auto fn = ZuMv(downFn); if (fn) fn(); }
  template <typename Packets>
  void endpointPacket(Packets packets) {
    Zquic_::forRxPackets(ZuMv(packets), [this](ZmRef<Zquic::RxPkt> packet, bool) {
      datagram(ZuMv(packet));
    });
  }
  void datagram(ZmRef<Zquic::RxPkt> d) {
    unsigned value = 0;
    length = d ? d->length : 0;
    if (length) memcpy(bytes, d->data(), length < sizeof(bytes) ? length : sizeof(bytes));
    if (d && d->length == sizeof(value))
      memcpy(&value, d->data(), sizeof(value));
    if (!value || (gaps ? value <= last : value != last + 1))
      ordered = false;
    last = value;
    source = d->addr.port();
    if (++received == target) done.post();
  }
  bool open() {
    return ZmBlock<bool>{}([this](auto wake) {
      readyFn = [wake = ZuMv(wake)](bool ok) mutable { wake(ok); };
      if (!openUDP(Zquic::PathMode::ServerUnconnected,
	  ZiIP{"127.0.0.1"}, 0)) {
	auto fn = ZuMv(readyFn);
	fn(false);
      }
    });
  }
  bool packet(unsigned value, ZiSockAddr addr,
      Zquic_::EndpointTxToken token = {}, bool *blocked = nullptr) {
    auto buf = allocTxPkt();
    if (!buf) return false;
    memcpy(buf->end(), &value, sizeof(value));
    buf->length = sizeof(value);
    // Retain only for inspection of ZiTx's final physical owner handoff.
    auto observer = buf;
    bool ok = send(ZuMv(buf), ZuMv(addr), Zquic::EcnMark::NotECT, false, token, blocked);
    if (ok && observer->owner != static_cast<Base *>(this))
      physical = static_cast<Zquic_::EndpointCxn<Base> *>(observer->owner);
    return ok;
  }
  void close() {
    ZmBlock<>{}([this](auto wake) {
      disconnect([wake = ZuMv(wake)]() mutable { wake(); });
    });
  }
  bool queueIsolated() const {
    using Cxn = Zquic_::EndpointCxn<Base>;
    auto cxn = static_cast<Cxn *>(physical);
    if (!cxn) return false;
    bool owners = true;
    auto check = [&owners](const void *address, size_t size) {
      owners &= !(uintptr_t(address) % Zm::CacheLineSize) && !(size % Zm::CacheLineSize);
    };
    Base::ownerLayout(check);
    cxn->ownerLayout(check);
    auto queue = &cxn->txQueue;
    return owners && !(uintptr_t(cxn) % Zm::CacheLineSize) &&
      !(uintptr_t(queue) % Zm::CacheLineSize) &&
      uintptr_t(queue) >= uintptr_t(static_cast<ZiConnection *>(cxn)) + sizeof(ZiConnection);
  }

};

static ZiMxParams params()
{
  return ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(2).thread(1, [](auto &t) { t.isolated(1); })
      .thread(2, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2);
}

static void testReopenFromDown()
{
  ZuTestScope(testReopenFromDown);
  ZiMultiplex mx{params()};
  auto rxAllocator = Zquic::pktRxAllocator(&mx);
  auto txAllocator = Zquic::pktTxAllocator(&mx);
  Endpoint endpoint;
  ZuCheck(mx.start(), (return));
  bool opened = endpoint.init(&mx, rxAllocator, txAllocator) && endpoint.open();
  ZuCheck(opened);
  if (opened) {
    bool reopened = false, started = false;
    ZmSemaphore ready;
    endpoint.downFn = [&endpoint, &reopened, &started, &ready] {
      endpoint.readyFn = [&reopened, &ready](bool ok) { reopened = ok; ready.post(); };
      started = endpoint.openUDP(
	Zquic::PathMode::ServerUnconnected, ZiIP{"127.0.0.1"}, 0);
    };
    endpoint.close();
    ZuCheck(ready.timedwait(Zm::now(3)) == 0 && started && reopened);
  }
  endpoint.close();
  ZuCheck(mx.stop());
}

static void testDrain()
{
  ZuTestScope(testDrain);
  ZiMultiplex mx{params()};
  Zi::IOBufAllocator *rxAllocator = Zquic::pktRxAllocator(&mx);
  Zi::IOBufAllocator *txAllocator = Zquic::pktTxAllocator(&mx);
  bool registered = rxAllocator && txAllocator;
  Endpoint rx, tx;
  ZuCheck(mx.start(), (return));
  bool opened = registered && rx.init(&mx, rxAllocator, txAllocator) &&
    tx.init(&mx, rxAllocator, txAllocator) && rx.open() && tx.open();
  ZuCheck(opened);
  bool active = false, admitted = false, received = false;
  if (opened) {
    auto addr = rx.local();
    ZmBlock<>{}([&mx, &tx, &active, &admitted, addr](auto wake) {
      mx.txRun([&tx, &active, &admitted, addr,
	  wake = ZuMv(wake)]() mutable {
	active = true;
	admitted = tx.packet(1, addr) && tx.packet(2, addr) && tx.packet(3, addr);
	wake();
      });
    });
    received = rx.done.timedwait(Zm::now(3)) == 0;
  }
  ZuCheck(received);
  tx.close();
  rx.close();
  ZuCheck(mx.stop());
  ZuCheck(admitted);
  ZuCheck(active);
  ZuCheck(rx.received == 3 && rx.ordered);
}

#ifdef linux
static void testSustainedDrain()
{
  ZuTestScope(testSustainedDrain);
  enum { Limit = 4096, Rounds = 4 };
  ZiMultiplex mx{params()};
  auto rxAllocator = Zquic::pktRxAllocator(&mx);
  auto txAllocator = Zquic::pktTxAllocator(&mx);
  Endpoint rx, tx, otherRx, otherTx;
  rx.gaps = true;
  rx.target = unsigned(-1);
  otherRx.target = Rounds;
  ZuCheck(mx.start(), (return));
  bool opened = rx.init(&mx, rxAllocator, txAllocator) &&
    tx.init(&mx, rxAllocator, txAllocator) &&
    otherRx.init(&mx, rxAllocator, txAllocator) &&
    otherTx.init(&mx, rxAllocator, txAllocator) &&
    rx.open() && tx.open() && otherRx.open() && otherTx.open();
  ZuCheck(opened);
  bool bounded = opened, completed = opened, progress = opened;
  uint64_t maxDrain = 0, maxCPU = 0, maxDelay = 0;
  if (opened) for (unsigned round = 0; round < Rounds; ++round) {
    ZmBlock<>{}([
      &mx, &rx, &tx, &otherRx, &otherTx, &bounded, &completed,
      &progress, &maxDrain, &maxCPU, &maxDelay, round
    ](auto wake) {
      mx.txRun([
	&mx, &rx, &tx, &otherRx, &otherTx, &bounded, &completed,
	&progress, &maxDrain, &maxCPU, &maxDelay, round, wake = ZuMv(wake)
      ]() mutable {
	sendProbe = {tx.local().port(), -1, true};
	for (unsigned i = 0; i < Limit; ++i)
	  bounded &= tx.packet(round*Limit + i + 1, rx.local());
	auto full = tx.diag();
	bounded &= full.txPending == 1 && full.txQueued == Limit - 1 &&
	  !tx.packet(round*Limit + Limit + 1, rx.local());
	auto rejected = tx.diag();
	// Only installing the head attempts sendmsg. The remaining admissions
	// leave the blocked datagram armed for the next writable notification.
	bounded &= rejected.tx.submittedTx == uint64_t(round + 1)*Limit &&
	  rejected.tx.submittedBytes == uint64_t(round + 1)*Limit*sizeof(unsigned) &&
	  rejected.tx.txBackPressure == round + 1 && rejected.tx.txDropped == round + 1 &&
	  sendProbe.blocked == 1 && sendProbe.sent == 0;
	uint64_t start = nanosecs();
	uint64_t cpuStart = nanosecs(CLOCK_THREAD_CPUTIME_ID);
	// This task and another endpoint must run after the bounded drain turn.
	mx.txRun([
	  &otherRx, &otherTx, &progress, &maxDelay, round, start,
	  wake = ZuMv(wake)
	]() mutable {
	  uint64_t delay = nanosecs() - start;
	  if (delay > maxDelay) maxDelay = delay;
	  progress &= otherTx.packet(round + 1, otherRx.local());
	  wake();
	});
	sendProbe.hold = false;
	if (tx.physical)
	  tx.physical->send(ZiTxFn{[](ZiTxContext &) { return true; }});
	uint64_t drain = nanosecs() - start;
	uint64_t cpu = nanosecs(CLOCK_THREAD_CPUTIME_ID) - cpuStart;
	if (drain > maxDrain) maxDrain = drain;
	if (cpu > maxCPU) maxCPU = cpu;
	auto empty = tx.diag();
	completed &= !empty.txPending && !empty.txQueued &&
	  empty.tx.datagramsTx == uint64_t(round + 1)*Limit &&
	  sendProbe.sent == Limit && !empty.tx.failures;
	sendProbe = {};
      });
    });
  }
  bool received = opened && otherRx.done.timedwait(Zm::now(3)) == 0;
  tx.close(); rx.close(); otherTx.close(); otherRx.close();
  ZuCheck(mx.stop());
  ZuCheck(bounded);
  ZuCheck(completed);
  ZuCheck(progress && received && otherRx.received == Rounds && otherRx.ordered);
  ZuCheck(rx.received && rx.ordered);
  log("4096-packet common UDP drain: rounds=", Rounds,
    " maxTurnUS=", maxDrain/1000, " maxThreadCPUUS=", maxCPU/1000,
    " maxScheduledDelayUS=", maxDelay/1000,
    " observedRx=", rx.received);
}

// Exercise reservations against the actual ZiTx queue, with one kernel
// completion permitted at a time. No traffic after the return drives a grant.
static void testCapacityReturn()
{
  ZuTestScope(testCapacityReturn);
  ZiMultiplex mx{params()};
  auto rxAllocator = Zquic::pktRxAllocator(&mx);
  auto txAllocator = Zquic::pktTxAllocator(&mx);
  Endpoint rx, tx;
  rx.gaps = true;
  rx.target = unsigned(-1);
  Zquic_::EndpointTxWait first, second;
  Zquic_::EndpointTxToken tokens[2];
  unsigned ready[2]{}, retired[2]{}, cancelled[2]{};
  ZmSemaphore terminal;
  auto onTx = [&mx](auto fn) {
    ZmBlock<>{}([&mx, fn = ZuMv(fn)](auto wake) mutable {
      mx.txRun([fn = ZuMv(fn), wake = ZuMv(wake)]() mutable {
	fn(); wake();
      });
    });
  };
  auto drain = [&onTx] {
    // Registration -> grant -> caller terminal -> physical drain acknowledgment.
    for (unsigned i = 0; i < 4; ++i) onTx([] {});
  };
  auto subscribe = [&tx, &tokens, &ready, &retired, &cancelled, &terminal](
      Zquic_::EndpointTxWait &wait, unsigned i) {
    tx.waitTx(wait, 2, 1,
      [&tokens, &ready, &retired, &cancelled, &terminal, i](int result, auto token) {
	if (result == Zquic_::EndpointTxResult::Ready) {
	  tokens[i] = token;
	  ++ready[i];
	} else if (result == Zquic_::EndpointTxResult::Retired) ++retired[i];
	else { ++cancelled[i]; terminal.post(); }
      });
  };
  ZuCheck(mx.start(), (return));
  bool opened = rx.init(&mx, rxAllocator, txAllocator) &&
    tx.init(&mx, rxAllocator, txAllocator) && rx.open() && tx.open();
  ZuCheck(opened);
  if (opened) {
    bool filled = true;
    onTx([&rx, &tx, &first, &second, &subscribe, &filled] {
      sendProbe = {tx.local().port(), -1, true};
      for (unsigned i = 0; i < 4096; ++i) filled &= tx.packet(i + 1, rx.local());
      bool blocked = false;
      filled &= !tx.packet(4097, rx.local(), {}, &blocked) && blocked;
      auto diag = tx.diag();
      filled &= diag.txPending == 1 && diag.txQueued == 4095;
      subscribe(first, 0);
      subscribe(second, 1);
    });
    drain();
    ZuCheck(filled && !ready[0] && !ready[1]);
    ZuCheck(tx.queueIsolated());
    bool reserved = false;
    onTx([&rx, &tx, &reserved] {
      sendProbe.allowance = 1;
      tx.physical->send(ZiTxFn{[](ZiTxContext &) { return true; }});
      bool blocked = false;
      reserved = !tx.packet(4097, rx.local(), {}, &blocked) && blocked;
      auto diag = tx.diag();
      reserved &= sendProbe.sent == 1 && diag.txPending == 1 && diag.txQueued == 4094;
    });
    drain();
    ZuCheck(reserved && ready[0] == 1 && !ready[1]);
    auto stale = tokens[0];
    bool consumed = false;
    onTx([&rx, &tx, &tokens, &consumed] {
      consumed = tx.packet(4097, rx.local(), tokens[0]) &&
	!tx.packet(4098, rx.local(), tokens[0]);
    });
    drain();
    ZuCheck(consumed && retired[0] == 1 && !first.busy() && !ready[1]);
    onTx([&tx] {
      sendProbe.allowance = 1;
      tx.physical->send(ZiTxFn{[](ZiTxContext &) { return true; }});
    });
    drain();
    ZuCheck(ready[1] == 1 && second.busy());
    bool released = false;
    onTx([&tx, &second, &released] {
      tx.cancelTx(second, [&released] { released = true; });
    });
    drain();
    ZuCheck(terminal.timedwait(Zm::now(3)) == 0 &&
      released && cancelled[1] == 1 && !second.busy());
    // Reusing a caller epoch still issues a new, single-use request serial.
    onTx([&first, &subscribe] { subscribe(first, 0); });
    drain();
    bool reused = false;
    onTx([&rx, &tx, &tokens, &reused, stale] {
      reused = tokens[0].serial != stale.serial && !tx.packet(4098, rx.local(), stale);
      bool blocked = false;
      reused &= !tx.packet(4098, rx.local(), {}, &blocked) && blocked;
    });
    ZuCheck(reused && ready[0] == 2);
    onTx([&second, &subscribe] { subscribe(second, 1); });
    drain();
    // Rebinding retires the old capacity requests and exposes the replacement
    // through the endpoint's existing physical connection references.
    bool rebound = ZmBlock<bool>{}([&tx](auto wake) {
      tx.rebindUDP(ZiIP{"127.0.0.1"}, 0, {}, 0,
	[wake = ZuMv(wake)](bool ok, ZiSockAddr, bool) mutable { wake(ok); });
    });
    bool drained = terminal.timedwait(Zm::now(3)) == 0 &&
      terminal.timedwait(Zm::now(3)) == 0;
    ZuCheck(rebound && drained && cancelled[0] == 1 && cancelled[1] == 2 &&
      !first.busy() && !second.busy());
    bool replacement = false;
    onTx([&rx, &tx, &tokens, &replacement] {
      sendProbe = {};
      replacement = !tx.packet(4098, rx.local(), tokens[0]) && tx.packet(4098, rx.local());
    });
    ZuCheck(replacement);
    ZuCheck(tx.queueIsolated());
    // Closing an idle queue with a reserved slot drains its caller too.
    onTx([&first, &subscribe] { subscribe(first, 0); });
    drain();
    ZuCheck(ready[0] == 3 && first.busy());
    tx.close();
    ZuCheck(cancelled[0] == 2 && !first.busy());
  }
  tx.close(); rx.close();
  ZuCheck(mx.stop());
  sendProbe = {};
}

static void testRebindQueued()
{
  ZuTestScope(testRebindQueued);
  ZiMultiplex mx{params()};
  auto rxAllocator = Zquic::pktRxAllocator(&mx);
  auto txAllocator = Zquic::pktTxAllocator(&mx);
  Endpoint rx, tx;
  rx.target = 1;
  ZuCheck(mx.start(), (return));
  bool opened = rx.init(&mx, rxAllocator, txAllocator) &&
    tx.init(&mx, rxAllocator, txAllocator) && rx.open() && tx.open();
  ZuCheck(opened);
  bool queued = false, rebound = false, resumed = false, received = false;
  bool stopped = false, restarted = false;
  uint16_t replacement = 0;
  if (opened) {
    ZmBlock<>{}([&mx, &rx, &tx, &queued](auto wake) {
      mx.txRun([&rx, &tx, &queued, wake = ZuMv(wake)]() mutable {
	sendProbe = {tx.local().port(), -1, true};
	queued = true;
	for (unsigned i = 0; i < 32; ++i) queued &= tx.packet(i + 100, rx.local());
	auto full = tx.diag();
	queued &= full.txPending == 1 && full.txQueued == 31 && !sendProbe.sent;
	wake();
      });
    });
    rebound = ZmBlock<bool>{}([&tx](auto wake) {
      tx.rebindUDP(ZiIP{"127.0.0.1"}, 0, {}, 0,
	[wake = ZuMv(wake)](bool ok, ZiSockAddr, bool) mutable { wake(ok); });
    });
    replacement = tx.local().port();
    if (rebound) {
      ZmBlock<>{}([&mx, &rx, &tx, &resumed](auto wake) {
	mx.txRun([&rx, &tx, &resumed, wake = ZuMv(wake)]() mutable {
	  resumed = tx.packet(1, rx.local());
	  wake();
	});
      });
      received = rx.done.timedwait(Zm::now(3)) == 0;
      received &= rx.source == replacement;
      ZmBlock<>{}([&mx, &rx, &tx, &stopped](auto wake) {
	mx.txRun([&rx, &tx, &stopped, wake = ZuMv(wake)]() mutable {
	  sendProbe = {tx.local().port(), -1, true};
	  stopped = true;
	  for (unsigned i = 0; i < 16; ++i) stopped &= tx.packet(i + 200, rx.local());
	  auto full = tx.diag();
	  stopped &= full.txPending == 1 && full.txQueued == 15;
	  wake();
	});
      });
      tx.close();
      stopped &= !tx.diag().txPending && !tx.diag().txQueued;
      ZmBlock<>{}([&mx, &rx](auto wake) {
	mx.rxRun([&rx, wake = ZuMv(wake)]() mutable { rx.target = 2; wake(); });
      });
      restarted = tx.open();
      replacement = tx.local().port();
      if (restarted) {
	ZmBlock<>{}([&mx, &rx, &tx, &restarted](auto wake) {
	  mx.txRun([&rx, &tx, &restarted, wake = ZuMv(wake)]() mutable {
	    sendProbe = {};
	    restarted &= tx.packet(2, rx.local());
	    wake();
	  });
	});
	restarted &= rx.done.timedwait(Zm::now(3)) == 0;
      }
    }
  }
  tx.close(); rx.close();
  ZuCheck(mx.stop());
  sendProbe = {};
  ZuCheck(queued && rebound && resumed && received);
  ZuCheck(stopped && restarted);
  ZuCheck(rx.received == 2 && rx.ordered && rx.source == replacement);
}

static void testSharedBindingCompletion()
{
  ZuTestScope(testSharedBindingCompletion);
  ZiMultiplex mx{params()};
  auto rxAllocator = Zquic::pktRxAllocator(&mx);
  ZmRef<ZquicFiniteProvider> provider = new ZquicFiniteProvider{&mx, 4};
  Zi::IOBufPoolAllocator<Zi::IOBufType::TxUDP, Zquic_::TxPkt> allocator{&mx, provider};
  Endpoint original, preferred, originalRx, preferredRx;
  originalRx.target = 2;
  preferredRx.target = 1;
  ZuCheck(mx.start(), (return));
  bool opened = original.init(&mx, rxAllocator, &allocator) &&
    preferred.init(&mx, rxAllocator, &allocator) &&
    originalRx.init(&mx, rxAllocator, &allocator) &&
    preferredRx.init(&mx, rxAllocator, &allocator) &&
    original.open() && preferred.open() && originalRx.open() && preferredRx.open();
  ZuCheck(opened);
  ZmRef<ZiIOBuf> observed[2];
  const uint8_t *data[2] = {};
  bool held = false, promoted = false, completed = false;
  if (opened) {
    held = ZmBlock<bool>{}([&mx, &original, &originalRx, &allocator,
	&observed, &data](auto wake) {
      mx.txRun([&original, &originalRx, &allocator, &observed, &data,
	  wake = ZuMv(wake)]() mutable {
	sendProbe = {original.local().port(), -1, true};
	bool ok = true;
	for (unsigned i = 0; i < 2; ++i) {
	  auto target = original.pktAllocTarget();
	  auto buf = allocator.allocBuf(target.owner, 0, target.mps);
	  if (!buf) { ok = false; break; }
	  unsigned value = i + 1;
	  memcpy(buf->end(), &value, sizeof(value));
	  buf->length = sizeof(value);
	  observed[i] = buf;
	  data[i] = buf->data();
	  ok &= buf->registered() && buf->skip == ZquicFiniteProvider::HeadRoom &&
	    original.send(ZuMv(buf), originalRx.local(), Zquic::EcnMark::NotECT,
	      false, {}, nullptr, target);
	}
	auto d = original.diag();
	if (observed[0]) original.physical =
	  static_cast<Zquic_::EndpointCxn<Endpoint::Base> *>(observed[0]->owner);
	wake(ok && d.txPending == 1 && d.txQueued == 1 && !d.tx.datagramsTx);
      });
    });
    ZuCheck(held);
    promoted = ZmBlock<bool>{}([&mx, &preferred, &preferredRx, &original,
	&originalRx, &allocator, &observed, &data](auto wake) {
      mx.txRun([&preferred, &preferredRx, &original, &originalRx, &allocator,
	  &observed, &data, wake = ZuMv(wake)]() mutable {
	// Both bindings retain shared provider geometry. Admission credits do
	// not create new backing when the common finite pool is exhausted.
	auto spare0 = allocator.allocBuf(), spare1 = allocator.allocBuf();
	bool bounded = spare0 && spare1 && !allocator.allocBuf();
	spare0 = nullptr;
	// Promotion changes the selector for newly constructed output only.
	Endpoint *active = &preferred;
	bool sent = active->packet(1, preferredRx.local());
	bool immutable = true;
	for (unsigned i = 0; i < 2; ++i) {
	  auto pkt = static_cast<Zquic_::TxPkt *>(observed[i].ptr());
	  unsigned value = 0;
	  memcpy(&value, pkt->data(), sizeof(value));
	  immutable &= pkt->data() == data[i] && value == i + 1 &&
	    pkt->addr == originalRx.local() && pkt->ecn == Zquic::EcnMark::NotECT;
	}
	wake(bounded && sent && immutable && original.diag().txQueued == 1);
      });
    });
    ZuCheck(promoted && preferredRx.done.timedwait(Zm::now(3)) == 0 &&
      preferredRx.source == preferred.local().port());
    completed = ZmBlock<bool>{}([&mx, &original, &observed, &data](auto wake) {
      mx.txRun([&original, &observed, &data, wake = ZuMv(wake)]() mutable {
	sendProbe.hold = false;
	original.physical->send(ZiTxFn{[](ZiTxContext &) { return true; }});
	auto d = original.diag();
	bool same = true;
	for (unsigned i = 0; i < 2; ++i)
	  same &= observed[i]->data() == data[i] && observed[i]->refCount() == 1;
	wake(same && d.tx.datagramsTx == 2 && !d.txPending && !d.txQueued);
      });
    });
    ZuCheck(completed && originalRx.done.timedwait(Zm::now(3)) == 0 &&
      originalRx.received == 2 && originalRx.ordered &&
      originalRx.source == original.local().port());
  }
  for (auto &buf : observed) buf = nullptr;
  original.close(); preferred.close(); originalRx.close(); preferredRx.close();
  sendProbe = {};
  ZuCheck(mx.stop());
}

// Characterization of a backend violating the UDP all-or-error contract.
// Positive short results are invalid backend completions; ZiTx needs no repair.
static void testBadUDPBackend()
{
  ZuTestScope(testBadUDPBackend);
  ZiMultiplex mx{params()};
  auto rxAllocator = Zquic::pktRxAllocator(&mx);
  auto txAllocator = Zquic::pktTxAllocator(&mx);
  Endpoint rx, tx;
  rx.target = 1;
  ZuCheck(mx.start(), (return));
  bool opened = rx.init(&mx, rxAllocator, txAllocator) &&
    tx.init(&mx, rxAllocator, txAllocator) && rx.open() && tx.open();
  ZuCheck(opened);
  bool shortRetried = false, accounted = false, received = false;
  unsigned value = 0x04030201;
  if (opened) {
    ZmBlock<>{}([&mx, &rx, &tx, &shortRetried, &accounted, value](auto wake) {
      mx.txRun([
	&rx, &tx, &shortRetried, &accounted, value, wake = ZuMv(wake)
      ]() mutable {
	sendProbe = {tx.local().port()};
	sendProbe.shortResult = true;
	bool admitted = tx.packet(value, rx.local());
	shortRetried = admitted && sendProbe.originalLength == sizeof(value) &&
	  sendProbe.suffix && sendProbe.suffixLength == sizeof(value) - 1 && sendProbe.sent == 1;
	auto completed = tx.diag();
	accounted = completed.tx.datagramsTx == 1 && completed.tx.bytesTx == sizeof(value) &&
	  !completed.txPending && !completed.txQueued;
	sendProbe = {};
	wake();
      });
    });
    received = rx.done.timedwait(Zm::now(3)) == 0;
  }
  tx.close(); rx.close();
  ZuCheck(mx.stop());
  ZuCheck(shortRetried && accounted);
  ZuCheck(received && rx.received == 1 && rx.length == sizeof(value) - 1 &&
    !memcmp(rx.bytes, reinterpret_cast<const uint8_t *>(&value) + 1, sizeof(value) - 1));
  log("UDP backend contract violation: injected sendmsg result1/4; common writer ",
    "retried data+1 length3 as another datagram; local completion counted4 bytes.");
}
#endif

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testDrain);
  ZuTestCall(testReopenFromDown);
#ifdef linux
  ZuTestCall(testSustainedDrain);
  ZuTestCall(testCapacityReturn);
  ZuTestCall(testRebindQueued);
  ZuTestCall(testSharedBindingCompletion);
  ZuTestCall(testBadUDPBackend);
#endif
}
