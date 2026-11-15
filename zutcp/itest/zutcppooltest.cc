//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZutcpAlloc.hh>
#include <zlib/ZutcpMx.hh>
#include <zlib/Zxdp.hh>

using namespace ZuTestUtil;

static bool closeInstance(ZmRef<Zxdp::Instance> &instance, ZtString<> &error)
{
  auto limit = Zm::now() + ZuTime{10};
  do {
    if (instance->close(error)) { instance = nullptr; return true; }
    // Fixture main only; expiry never authorizes retirement of live backing.
    poll(nullptr, 0, 1);
  } while (Zm::now() < limit);
  return false;
}

static void testProvider()
{
  ZuTestScopeRT(testProvider);
  enum { PacketSize = 1514, Head = 42, Tail = 8, Payload = 42, RxCount = 8 };
  // Provider ownership is deliberately independent of backend ownership. Zi's
  // adapter owns the provider; the raw receive callback exchanges addresses only.
  ZiMultiplex mx;
  ZmRef<Zutcp::Allocator> provider = new Zutcp::Allocator{2, PacketSize, Head, Tail};
  ZmRef<Zutcp::Allocator> control = new Zutcp::Allocator{1, 128};
  provider->pool.next = &control->pool;
  Zi::IOBufPluginAllocator<Zi::IOBufType::TxTCP> allocator{&mx, provider};
  Zi::IOBufPluginAllocator<Zi::IOBufType::TxTCP> ctlAllocator{&mx, control};
  ZuCheckRT(allocator.size == PacketSize && allocator.headRoom == Head &&
      allocator.tailRoom == Tail && !allocator.allocBuf(nullptr, Payload),
    log_("provider publishes fixed geometry before backing is activated"));
  Zxdp::BufPool rx{RxCount, PacketSize};
  unsigned completed = 0;
  uint8_t *canonical = nullptr;
  ZtString<> error;
  Zxdp::Params params{
    .interface = unsigned(strtoul(getenv("ZXDP_TEST_IFINDEX"), nullptr, 10)),
    .txPools = &provider->pool, .rxPool = &rx,
    .rx = [&rx](uint8_t *p, unsigned) { rx.free(p); }};
  ZmScheduler scheduler{ZmSchedParams{}.nThreads(1).thread(1,
    [&params](auto &thread) { thread = params.txThread; })};
  params.scheduler = &scheduler;
  params.txSid = 1;
  bool started = scheduler.start();
  ZuCheckRT(started);
  if (!started) return;
  auto instance = Zxdp::Instance::create(ZuMv(params), error);
  bool ready = instance && instance->ready();
  ZuCheckRT(ready, log_("raw instance activates caller-owned Zutcp provider pools: ", error));
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
        buf->length == Payload && payload == canonical + Head &&
        provider->pool.owns(canonical) && provider->pool.used() == 2;
      memset(canonical, 0, Head + Payload);
      memset(canonical, 0xff, 6);
      memcpy(canonical + 6, instance->mac(), 6);
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
      ZuCheckRT(held && !control->pool.writable(heldOrigin),
        log_("a caller allocation remains unwritable through its kernel submission"));
      bool accepted = instance->send(buf->data(), buf->length);
      if (accepted) {
        buf->data__ = 0;
        buf->size = buf->skip = buf->length = 0;
        provider->free(canonical);
      }
      ZuCheckRT(accepted && !buf->data_() && !buf->registered(),
        log_("accepted raw submission permits metadata to detach backing"));
      buf = nullptr;
      ZuCheckRT(provider->pool.used() == 2 && !allocator.allocBuf(&mx, Payload),
        log_("metadata destruction does not recycle kernel-owned backing"));
      auto deadline = Zm::now() + ZuTime{10};
      while (accepted && completed < 2 && Zm::now() < deadline) {
        completed += instance->pollTx();
        if (completed < 2) poll(nullptr, 0, 1);
      }
      auto recycled = allocator.allocBuf(&mx, Payload);
      ZuCheckRT(completed == 2 && recycled && recycled->data_() == canonical &&
          provider->pool.used() == 2 && control->pool.used() == 1 &&
          ctl->data_() == heldOrigin && control->pool.writable(heldOrigin) &&
          !memcmp(ctl->data(), retainedBytes, 64),
        log_("CQ recycles the originating pool slot without a metadata callback"));
      ZuCheckRT(instance->send(ctl->data(), ctl->length),
        log_("retained backing can be resubmitted unchanged after CQ retirement"));
      other = nullptr;
      // Close below while recycled remains application-owned, then release it through
      // the still-live Zi provider. No connection object is needed for that free.
      bool closed = closeInstance(instance, error);
      ZuCheckRT(closed,
	log_("kernel retirement completes independently of the application: ", error));
      ZmAssert_(closed);
      ZuCheckRT(provider->pool.used() == 1 && recycled && recycled->data_() == canonical &&
          control->pool.used() == 1 && ctl->data_() == heldOrigin &&
          !memcmp(ctl->data(), retainedBytes, 64),
        log_("kernel close preserves caller-retained original backing and bytes"));
      recycled = nullptr;
      ctl = nullptr;
      ZuCheckRT(!provider->pool.used() && !control->pool.used() && !provider->alloc(),
        log_("final Zi release returns exactly once to the retired originating pool"));
    }
  }
  if (instance) ZmAssert_(closeInstance(instance, error));
  ZuCheckRT(scheduler.stop());
}

// All fixture waits run on main. Application allocation callbacks run on slot 3,
// independently of the multiplexer Tx owner on slot 2 and raw sender on slot 4.
template <typename Fn>
static void ownerRun(ZiMultiplex &mx, unsigned sid, Fn fn)
{
  ZmBlock<>{}([&mx, sid, fn = ZuMv(fn)](auto done) mutable {
    mx.run([fn = ZuMv(fn), done = ZuMv(done)]() mutable {
      fn(); done();
    }, sid);
  });
}

struct WaitProbe {
  ZiMultiplex &mx;
  Zi::IOBufAllocator *allocator;
  Zi::IOBufWait request;
  ZmRef<ZiIOBuf> granted;
  ZmSemaphore completed, drained;
  Zi::IOBufWaitResult::T result = Zi::IOBufWaitResult::Failed;
  unsigned calls = 0, acknowledgments = 0, order = 0, ackOrder = 0;
  bool onOwner = true;

  WaitProbe(ZiMultiplex &mx_, Zi::IOBufAllocator *allocator_) :
    mx{mx_}, allocator{allocator_} { }

  void start(unsigned length = 32, unsigned mps = 96) {
    ZmAssert_(mx.invoked(3));
    allocator->waitBuf(request, 3, this, length, mps,
      [this](auto outcome, ZmRef<ZiIOBuf> buf) {
	onOwner &= mx.invoked(3) && !mx.invoked(mx.txThread());
	result = outcome;
	granted = ZuMv(buf);
	++calls; ++order;
	completed.post();
      });
  }

  void cancel() {
    ZmAssert_(mx.invoked(3));
    allocator->cancelBuf(request, [this] {
      onOwner &= mx.invoked(3) && !request.busy();
      ++acknowledgments;
      ackOrder = ++order;
      drained.post();
    });
  }

  bool wait() { return !completed.timedwait(Zm::now() + ZuTime{10}); }
  bool waitCancel() { return !drained.timedwait(Zm::now() + ZuTime{10}); }
};

static void testProviderWait()
{
  ZuTestScopeRT(testProviderWait);
  alarm(45);
  auto params = ZiMxParams{}.rxBufSize(8).rxThread(1).txThread(2)
    .scheduler([](auto &s) {
      s.nThreads(4)
	.thread(1, [](auto &t) { t.isolated(true); })
	.thread(2, [](auto &t) { t.isolated(true); })
	.thread(3, [](auto &t) { t.isolated(true); })
	.thread(4, [](auto &t) { t.isolated(true); });
    });
  ZiMultiplex mx{params};
  ZmRef<Zutcp::Stack> stack = new Zutcp::Stack{&mx, {
    .interface = unsigned(strtoul(getenv("ZXDP_RX_TEST_IFINDEX"), nullptr, 10)),
    .packetCount = 8, .txSid = 4}};
  mx.plugin(stack);
  auto allocator = mx.allocator<Zi::IOBufType::TxTCP, ZiIOBuf, 128>(1);
  auto provider = static_cast<Zutcp::Allocator *>(allocator->provider());
  bool started = mx.start();
  ZuCheckRT(started, log_("one-slot native application pool starts with separate owners"));
  if (!started) { alarm(0); return; }

  auto held = allocator->allocBuf(nullptr, 32);
  auto canonical = held ? held->data_() : nullptr;
  ZuCheckRT(held && provider->pool.count == 1 && provider->pool.used() == 1);
  ZmAssert_(held);
  WaitProbe first{mx, allocator}, second{mx, allocator};
  ownerRun(mx, 3, [&first, &second] { first.start(); second.start(); });
  ownerRun(mx, 2, [] {}); // both registrations have reached their provider
  ownerRun(mx, 3, [] {});
  ZuCheckRT(!first.calls && !second.calls && !allocator->allocBuf(),
    log_("full pool retains both producers without allocating or completing"));
  held = nullptr; // off-owner final return, with no subsequent network traffic
  bool firstDone = first.wait();
  ZuCheckRT(firstDone && first.result == Zi::IOBufWaitResult::Ready && first.onOwner &&
      first.granted && first.granted->data_() == canonical &&
      first.granted->owner == &first && first.granted->length == 32 &&
      first.granted->size == 96 + allocator->headRoom + allocator->tailRoom &&
      !allocator->allocBuf(),
    log_("return grants the exact original slot and requested geometry to the first producer"));
  ZmAssert_(firstDone);
  ownerRun(mx, 3, [] {});
  ZuCheckRT(!second.calls && provider->pool.used() == 1,
    log_("the reserved grant cannot be stolen and does not wake the second producer"));
  first.granted = nullptr;
  bool secondDone = second.wait();
  ZuCheckRT(secondDone && second.result == Zi::IOBufWaitResult::Ready && second.onOwner &&
      second.granted && second.granted->data_() == canonical,
    log_("the next physical return advances the native waiter FIFO"));
  ZmAssert_(secondDone);

  WaitProbe cancelled{mx, allocator};
  ownerRun(mx, 3, [&cancelled] { cancelled.start(); });
  ownerRun(mx, 2, [] {});
  ownerRun(mx, 3, [&cancelled] { cancelled.cancel(); });
  bool cancelDone = cancelled.waitCancel();
  ZuCheckRT(cancelDone && cancelled.calls == 1 && cancelled.acknowledgments == 1 &&
      cancelled.ackOrder == 2 && cancelled.result == Zi::IOBufWaitResult::Cancelled &&
      !cancelled.granted && cancelled.onOwner && provider->pool.used() == 1,
    log_("cancellation unlinks a blocked producer before acknowledging its callback drain"));
  ZmAssert_(cancelDone);
  second.granted = nullptr;
  ownerRun(mx, 2, [] {});

  WaitProbe raced{mx, allocator};
  ownerRun(mx, 3, [&raced] { raced.start(); raced.cancel(); });
  bool raceDone = raced.waitCancel();
  ZuCheckRT(raceDone && raced.calls == 1 && raced.acknowledgments == 1 &&
      raced.ackOrder == 2 && raced.result == Zi::IOBufWaitResult::Ready &&
      raced.granted && raced.granted->data_() == canonical && raced.onOwner,
    log_("already available backing is granted before the ordered cancellation acknowledgment"));
  ZmAssert_(raceDone);
  raced.granted = nullptr;

  WaitProbe invalid{mx, allocator};
  ownerRun(mx, 3, [&invalid] { invalid.start(97, 96); });
  bool invalidDone = invalid.wait();
  ZuCheckRT(invalidDone && invalid.result == Zi::IOBufWaitResult::Failed &&
      !invalid.granted && !provider->pool.used(),
    log_("impossible geometry fails without acquiring or waiting for a raw slot"));
  ZmAssert_(invalidDone);

  // Both orders use real raw submissions. The first releases the caller lease
  // inside sendPacket, so only subsequent CQ service can grant the waiter.
  held = allocator->allocBuf(nullptr, 64);
  ZmAssert_(held);
  memset(held->data(), 0, held->length);
  memset(held->data(), 0xff, 6);
  held->data()[6] = 2;
  held->data()[12] = 0x88; held->data()[13] = 0xb5;
  WaitProbe cqLast{mx, allocator};
  ownerRun(mx, 3, [&cqLast] { cqLast.start(); });
  ownerRun(mx, 2, [] {});
  bool sent = false, kernelHeld = false;
  ownerRun(mx, 2, [&stack, &held, provider, &sent, &kernelHeld] {
    sent = stack->sendPacket(held);
    kernelHeld = provider->pool.used() == 1;
  });
  bool cqDone = cqLast.wait();
  ZuCheckRT(sent && kernelHeld && cqDone && cqLast.onOwner &&
      cqLast.result == Zi::IOBufWaitResult::Ready && cqLast.granted &&
      cqLast.granted->data_() == canonical && !held->data_(),
    log_("CQ-last retirement grants native backing after caller metadata detached"));
  ZmAssert_(cqDone);
  held = nullptr;
  cqLast.granted = nullptr;
  ownerRun(mx, 2, [] {});

  held = allocator->allocBuf(nullptr, 64);
  ZmAssert_(held);
  memset(held->data(), 0, held->length);
  memset(held->data(), 0xff, 6);
  held->data()[6] = 2;
  held->data()[12] = 0x88; held->data()[13] = 0xb5;
  WaitProbe callerLast{mx, allocator};
  Zutcp::PacketLease retained;
  ownerRun(mx, 3, [&callerLast] { callerLast.start(); });
  ownerRun(mx, 2, [] {});
  ownerRun(mx, 2, [&stack, &held, &retained, &sent] {
    sent = stack->sendPacket(held, &retained);
  });
  // Raw CQ has no callback by design. Observe it on its owner from main;
  // never block or retry inside an I/O task. The retained caller lease prevents
  // reuse while this fixture checks the independent kernel-retirement boundary.
  bool writable = false;
  auto deadline = Zm::now() + ZuTime{10};
  while (sent && !writable && Zm::now() < deadline) {
    ownerRun(mx, 2, [&stack, provider, canonical, &writable] {
      stack->reconcileTx();
      writable = provider->pool.writable(canonical);
    });
    if (!writable) poll(nullptr, 0, 1);
  }
  ownerRun(mx, 3, [] {});
  ZuCheckRT(sent && writable && !callerLast.calls && !allocator->allocBuf() &&
      provider->pool.used() == 1,
    log_("CQ-first retirement alone cannot grant caller-retained backing"));
  retained.reset();
  bool callerDone = callerLast.wait();
  ZuCheckRT(callerDone && callerLast.onOwner &&
      callerLast.result == Zi::IOBufWaitResult::Ready && callerLast.granted &&
      callerLast.granted->data_() == canonical,
    log_("the final caller return resumes the lone producer without new traffic"));
  ZmAssert_(callerDone);
  held = nullptr;
  callerLast.granted = nullptr;
  ownerRun(mx, 2, [] {});
  ownerRun(mx, 3, [] {});
  bool empty = !provider->pool.used();
  bool stopped = mx.stop();
  ZuCheckRT(empty && stopped && !stack->ready(),
    log_("all waiters, reserved grants and raw submissions drain before native shutdown"));
  alarm(0);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  if (geteuid() || !getenv("ZXDP_TEST_IFINDEX") || !getenv("ZXDP_RX_TEST_IFINDEX")) {
    std::cout << "1..0 # SKIP requires privileged fixture-owned interface\n";
    return 0;
  }
  ZuTestMain();
  ZuTestCall(testProvider);
  ZuTestCall(testProviderWait);
}
