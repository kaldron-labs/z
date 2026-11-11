//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/Zquic.hh>

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
    ++sendProbe.blocked;
    errno = EAGAIN;
    return -1;
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
  ZmSemaphore done;
  unsigned received = 0, target = 3, last = 0;
  unsigned length = 0;
  uint8_t bytes[sizeof(unsigned)]{};
  uint16_t source = 0;
  bool ordered = true, gaps = false;
  ZiConnection *physical = nullptr; // valid until endpoint close/rebind

  void endpointReady_(Base *) { auto fn = ZuMv(readyFn); fn(true); }
  void endpointFailed(bool) { auto fn = ZuMv(readyFn); fn(false); }
  void endpointDown_(Base *) { }
  void endpointDatagram(Zquic::Datagram d) {
    unsigned value = 0;
    length = d.buf ? d.buf->length : 0;
    if (length) memcpy(bytes, d.buf->data(), length < sizeof(bytes) ? length : sizeof(bytes));
    if (d.buf && d.buf->length == sizeof(value))
      memcpy(&value, d.buf->data(), sizeof(value));
    if (!value || (gaps ? value <= last : value != last + 1))
      ordered = false;
    last = value;
    source = d.addr.port();
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
  bool packet(unsigned value, ZiSockAddr addr) {
    auto buf = allocTxPkt();
    if (!buf) return false;
    memcpy(buf->end(), &value, sizeof(value));
    buf->length = sizeof(value);
    // Retain only for inspection of ZiTx's final physical owner handoff.
    auto observer = buf;
    bool ok = send(ZuMv(buf), ZuMv(addr));
    if (ok && observer->owner != static_cast<Base *>(this))
      physical = static_cast<Zquic_::EndpointCxn<Base> *>(observer->owner);
    return ok;
  }
  void close() {
    ZmBlock<>{}([this](auto wake) {
      disconnect([wake = ZuMv(wake)]() mutable { wake(); });
    });
  }

};

static ZiMxParams params()
{
  return ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(2).thread(1, [](auto &t) { t.isolated(1); })
      .thread(2, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2);
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
	bounded &= rejected.tx.submittedTx == uint64_t(round + 1)*Limit &&
	  rejected.tx.submittedBytes == uint64_t(round + 1)*Limit*sizeof(unsigned) &&
	  rejected.tx.txBackPressure == round + 1 && rejected.tx.txDropped == round + 1 &&
	  sendProbe.blocked >= Limit && sendProbe.sent == 0;
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
#ifdef linux
  ZuTestCall(testSustainedDrain);
  ZuTestCall(testRebindQueued);
  ZuTestCall(testBadUDPBackend);
#endif
}
