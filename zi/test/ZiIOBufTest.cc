//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <errno.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuPtr.hh>

#include <zlib/ZmBlock.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiMultiplex.hh>

using namespace ZuTestUtil;

void testSliceEnsureSpan()
{
  ZuTestScope(testSliceEnsureSpan);

  ZiMultiplex allocatorMx;
  auto allocator = allocatorMx.allocator<>();
  ZmRef<ZiIOBuf> buf = allocator->allocBuf();
  *buf << "fbah";
  ZtString<> s;
  s << ZuCSpan(buf->cspan());
  ZuCheck(s == "fbah");
  ++buf->skip, --buf->length;
  s.clear();
  s << ZuCSpan(buf->cspan());
  ZuCheck(s == "bah");

  buf->clear();
  *buf << "0123456789";
  buf->skip = 2;
  buf->length = 5;
  ZtString<> before;
  before << ZuCSpan(buf->cspan());
  auto old_skip = buf->skip;
  auto old_length = buf->length;
  ZuCheck(buf->ensure(buf->size + 128));
  ZuCheck(buf->skip == old_skip);
  ZuCheck(buf->length == old_length);
  ZtString<> after;
  after << ZuCSpan(buf->cspan());
  ZuCheck(after == before);
}

void testAdvanceRewind()
{
  ZuTestScope(testAdvanceRewind);

  ZiMultiplex allocatorMx;
  auto allocator = allocatorMx.allocator<>();
  ZmRef<ZiIOBuf> buf = allocator->allocBuf();
  *buf << "abcdef";
  buf->advance(2);
  ZuCheck(buf->length == 4);
  ZuCheck(buf->skip == 2);
  ZtString<> s;
  s << ZuCSpan(buf->cspan());
  ZuCheck(s == "cdef");

  buf->rewind(1);
  ZuCheck(buf->length == 5);
  ZuCheck(buf->skip == 1);
  s.clear();
  s << ZuCSpan(buf->cspan());
  ZuCheck(s == "bcdef");

  buf->advance(1000);
  ZuCheck(buf->length == 0);
}

void testPrependAndGrow()
{
  ZuTestScope(testPrependAndGrow);

  ZiMultiplex allocatorMx;
  auto allocator = allocatorMx.allocator<
    Zi::IOBufType::App, ZiIOBuf, 8, ZiIOBuf_DefltAlign, ZiIOBuf_HeapID, 4096>();
  ZmRef<ZiIOBuf> buf = allocator->allocBuf();
  buf->clear();
  buf->skip = buf->size;
  uint8_t *tail = buf->prepend(4);
  memcpy(tail, "tail", 4);

  auto oldSize = buf->size;

  // Force a prepend requiring growth.
  buf->prepend(32);
  ZuCheck(buf->length == 36);
  ZuCheck(buf->size >= oldSize);
}

void testSpanOffsetAndLargeEnsure()
{
  ZuTestScope(testSpanOffsetAndLargeEnsure);

  ZiMultiplex allocatorMx;
  auto allocator = allocatorMx.allocator<
    Zi::IOBufType::App, ZiIOBuf, 16, ZiIOBuf_DefltAlign, ZiIOBuf_HeapID, 1<<20>();
  ZmRef<ZiIOBuf> buf = allocator->allocBuf();
  *buf << "0123456789";

  auto s = buf->cspan(1000);
  ZuCheck(s.length() == 0);

  unsigned large = 256 * 1024;
  ZuCheck(buf->ensure(large));
  ZuCheck(buf->size >= large);

  ZtString<> out;
  out << ZuCSpan(buf->cspan());
  ZuCheck(out == "0123456789");
}

void testPrependTailRoom()
{
  ZuTestScope(testPrependTailRoom);

  ZiMultiplex allocatorMx;
  auto allocator = allocatorMx.allocator<
    Zi::IOBufType::App, ZiIOBuf, 8, ZiIOBuf_DefltAlign, ZiIOBuf_HeapID, 4096>();
  ZmRef<ZiIOBuf> buf = allocator->allocBuf();
  buf->owner = &buf;
  auto data = buf->data_();
  auto size = buf->size;
  buf->skip = 8;
  buf->length = 4;
  memcpy(buf->data(), "tail", 4);
  auto end = buf->end();
  auto avail = buf->avail();
  memcpy(buf->prepend(4), "head", 4);
  ZuCheck(buf->cspan() == "headtail");
  ZuCheck(buf->data_() == data && buf->size == size);
  ZuCheck(buf->end() == end && buf->avail() == avail);
  ZuCheck(buf->owner == &buf);

  // Insufficient headroom, but the combined free space avoids allocation.
  buf->skip = 2;
  buf->length = 4;
  memcpy(buf->data(), "tail", 4);
  memcpy(buf->prepend(4), "head", 4);
  ZuCheck(buf->cspan() == "headtail");
  ZuCheck(buf->data_() == data && buf->size == size);
  ZuCheck(buf->skip + buf->length == size && buf->owner == &buf);

  // Existing headroom must not be subtracted from the required capacity.
  buf->skip = 2;
  buf->length = size - buf->skip;
  auto length = buf->length;
  for (unsigned i = 0; i < length; ++i) buf->data()[i] = i;
  memcpy(buf->prepend(4), "head", 4);
  ZuCheck(buf->size >= length + 4 && buf->length == length + 4);
  ZuCheck((ZuBSpan{buf->data(), 4} == "head"));
  bool intact = true;
  for (unsigned i = 0; i < length; ++i)
    if (buf->data()[i + 4] != uint8_t(i)) intact = false;
  ZuCheck(intact && buf->owner == &buf);
}

void testAllocatorPolicy()
{
  ZuTestScope(testAllocatorPolicy);
  ZiMultiplex mx;
  auto allocator =
    mx.allocator<Zi::IOBufType::TxTCP, ZiIOBuf, 32, ZiIOBuf_DefltAlign, ZiIOBuf_HeapID, 64>();
  auto buf = allocator->allocBuf();
  ZuCheck(allocator->mx == &mx && allocator->type == Zi::IOBufType::TxTCP);
  ZuCheck(buf->builtin() && !buf->registered() && buf->size == allocator->size);
  int owner = 0;
  auto length = allocator->size + 1;
  auto large = allocator->allocBuf(&owner, length);
  ZuCheck(large->allocator() == allocator && large->owner == &owner);
  ZuCheck(!large->builtin() && !large->registered() && large->length == length);

  // A packet's exposed capacity must honor MPS even with larger inline backing.
  unsigned mps = allocator->capacity() - 1;
  auto packet = allocator->allocBuf(&owner, mps, mps);
  ZuCheck(bool(packet));
  if (!packet) return;
  ZuCheck(packet->builtin() && packet->size == mps && packet->length == mps);
  ZuCheck(packet->owner == &owner && !packet->avail());
  ZuCheck(!allocator->allocBuf(&owner, mps + 1, mps));
}

struct BufPool : ZmNHeap<BufPool>, Zi::IOBufPool {
friend inline ZuStringT<"ZiIOBufTest.Pool"> ZmNHeapID(BufPool *);
  using Heap = ZmVHeap<"ZiIOBufTest.Backing", 32, 64, ZiIOBuf_DefltAlign>;
  uint8_t *registeredPtr = nullptr;
  unsigned registeredAllocs = 0, registeredFrees = 0;
  unsigned *destroyed = nullptr;
  bool exhausted = false, valid = true, ready;

  BufPool(Zi::IOBufType::T type, unsigned size_ = 32, unsigned align_ = ZiIOBuf_DefltAlign,
    unsigned count_ = 1, bool ready_ = true) : Zi::IOBufPool{type}, ready{ready_}
  {
    count = count_;
    size = size_;
    align = align_;
    headRoom = 3;
    tailRoom = 4;
  }

  ~BufPool() override { if (destroyed) ++*destroyed; }

  uint8_t *alloc() override {
    if (!ready || exhausted || registeredPtr) return nullptr;
    ++registeredAllocs;
    registeredPtr = static_cast<uint8_t *>(Heap::alloc(size));
    return registeredPtr;
  }
  void free(uint8_t *ptr) override {
    ++registeredFrees;
    valid &= ptr == registeredPtr;
    registeredPtr = nullptr;
    Heap::free(ptr, size);
  }
};

ZuAssert((ZuIsSame<decltype(Zi::IOBufPool::type), const Zi::IOBufType::T>{}));

void testPoolList()
{
  ZuTestScope(testPoolList);
  unsigned destroyed = 0;
  Zi::IOBufPoolList pools;
  ZmRef<BufPool> first = new BufPool{Zi::IOBufType::TxTCP};
  ZmRef<BufPool> second = new BufPool{Zi::IOBufType::TxUDP};
  first->destroyed = second->destroyed = &destroyed;
  auto backing = first.ptr();
  pools.pushNode(first.ptr());
  pools.pushNode(second.ptr());
  ZuCheck(pools.headPtr() == first.ptr() && pools.tailPtr() == second.ptr());
  ZuCheck(first->next == second.ptr() && second->prev == first.ptr());
  ZuPtr<Zi::IOBufAllocator> allocator =
    new Zi::IOBufPoolAllocator<Zi::IOBufType::TxTCP, ZiIOBuf, 32>{nullptr, first};
  first = second = nullptr;
  ZuCheck(!destroyed && pools.count_() == 2);
  auto buf = allocator->allocBuf(nullptr, allocator->capacity());
  ZuCheck(buf && buf->registered() && buf->length == allocator->capacity());
  pools.clean();
  ZuCheck(destroyed == 1 && !pools.count_() && backing->registeredAllocs == 1);
  buf = nullptr;
  ZuCheck(backing->valid && backing->registeredFrees == 1);
  allocator = nullptr;
  ZuCheck(destroyed == 2);
}

struct MxPlugin : ZmNHeap<MxPlugin>, ZiMxPlugin {
friend inline ZuStringT<"ZiIOBufTest.MxPlugin"> ZmNHeapID(MxPlugin *);

  struct Lifecycle {
    unsigned allocations = 0, starts = 0, stops = 0, finals = 0;
    ZmAtomic<unsigned> workersStarted = 0;
    bool finalLive = false;
  };

  enum { MaxPools = 5 };
  BufPool *pools[MaxPools]{};
  unsigned nPools = 0, requests = 0;
  bool deferred = false, failPool = false, failAlloc = false, failStart = false;
  bool sawPools = false, allocatedBeforeWorker = true, startedAllocated = false;
  Lifecycle *lifecycle = nullptr;
  Zi::IOBufAllocator *finalAllocator = nullptr;

  unsigned syncMode = 0, sends = 0, completions = 0;
  bool progressed = false, spun = false, error = false, closed = false;

  explicit MxPlugin(ZiMultiplex *mx, Lifecycle *lifecycle_ = nullptr) :
    ZiMxPlugin(mx), lifecycle{lifecycle_}
  {
    if (lifecycle)
      mx->threadInit([trace = lifecycle] { ++trace->workersStarted; });
  }

  Zi::IOBufPool *allocator(
    Zi::IOBufType::T type, unsigned size, unsigned align, unsigned count) override
  {
    ++requests;
    if (failPool || nPools == MaxPools) return nullptr;
    return pools[nPools++] = new BufPool{type, size, align, count, !deferred};
  }
  bool deferredBacking() const override { return deferred; }
  AllocResult alloc(Zi::IOBufPool *rx, const Zi::IOBufPoolList &tx) override {
    if (lifecycle) {
      ++lifecycle->allocations;
      allocatedBeforeWorker &= !lifecycle->workersStarted.load_();
    }
    auto i = tx.citer();
    sawPools = true;
    bool sawRx = false;
    for (unsigned n = 0; n < nPools; ++n) {
      auto pool = pools[n];
      if (pool->type == Zi::IOBufType::Rx) {
	sawPools &= rx == pool;
	sawRx = true;
      } else
	sawPools &= i() == pool;
    }
    sawPools &= (sawRx || !rx) && !i();
    if (failAlloc) return ZeEXCEPT(Error, "ZiIOBufTest", "fixture allocation failure");
    for (unsigned n = 0; n < nPools; ++n) pools[n]->ready = true;
    return {};
  }
  void start() override {
    if (lifecycle) ++lifecycle->starts;
    startedAllocated = true;
    for (unsigned n = 0; n < nPools; ++n) startedAllocated &= pools[n]->ready;
    if (failStart)
      mx()->pluginStarted(ZeEXCEPT(Error, "ZiIOBufTest", "fixture start failure"));
    else
      mx()->pluginStarted({});
  }
  void stop() override {
    if (lifecycle) ++lifecycle->stops;
    mx()->pluginStopped({});
  }
  void final() override {
    if (!lifecycle) return;
    ++lifecycle->finals;
    lifecycle->finalLive = mx() && mx()->state() == ZmEngineState::Stopped &&
      (!finalAllocator || (finalAllocator->mx == mx() && finalAllocator->allocBuf()));
  }

  ZmSemaphore event;
  void rx() override { event.wait(); }

  void incoming(ZiConnectFn fn, int type, Zi::IOBufAllocator *allocator, ZiCxnOptions options) {
    ZiCxnInfo info{
      .type = type, .socket = Zi::nullSocket(), .options = ZuMv(options), .rxAllocator = allocator};
    ZmRef<ZiConnection> cxn = fn(info);
    if (!cxn) return;
    auto &io = cxn->rxContext();
    cxn->connected(io);
    if (!io.buf || io.buf->data_() || !allocator->pool) return;
    auto pool = allocator->pool.ptr();
    auto data = pool->alloc();
    if (!data) return;
    io.buf->data__ = uintptr_t(data) | ZiIOBuf::Registered;
    io.buf->size = pool->size;
    *io.buf << "frame";
    io();
  }
  void listen(
    ZiListenFn listening, ZiFailFn, ZiConnectFn accept, ZiIP ip, uint16_t port, unsigned nAccepts,
    Zi::IOBufAllocator *allocator, ZiCxnOptions options) override
  {
    ZiListenInfo info{Zi::nullSocket(), nAccepts, ip, port, options, allocator};
    listening(info);
    incoming(ZuMv(accept), ZiCxnType::TCPIn, info.rxAllocator, ZuMv(options));
  }
  void stopListening(ZiIP, uint16_t) override { }
  void connect(
    ZiConnectFn fn, ZiFailFn, ZiIPType::T, ZiIP, uint16_t, ZiIP, uint16_t,
    Zi::IOBufAllocator *allocator, ZiCxnOptions options) override
  {
    incoming(ZuMv(fn), ZiCxnType::TCPOut, allocator, ZuMv(options));
  }
  void udp(
    ZiConnectFn fn, ZiFailFn, ZiIPType::T, ZiIP, uint16_t, ZiIP, uint16_t,
    Zi::IOBufAllocator *allocator, ZiCxnOptions options) override
  {
    incoming(ZuMv(fn), ZiCxnType::UDP, allocator, ZuMv(options));
  }
  void recv(ZiConnection *) override { }
  void send(ZiConnection *cxn) override {
    if (!syncMode) return;
    if (++sends > 1) {
      // The old sync loop would repeat before its queued completion could run.
      // Terminate that bad path deterministically rather than hang the suite.
      spun = true;
      cxn->txContext().complete();
      return;
    }
    auto saved = cxn->txContext();
    saved.complete(); // completing a copy must not notify the active context's waiters
    mx()->txRun([this, cxn] {
      progressed = true;
      if (syncMode == 3) { cxn->close(); return; }
      auto &context = cxn->txContext();
      if (context.completed()) return;
      context.length = syncMode == 2 ? -1 : 1;
      context();
    });
  }
  void disconnect(ZiConnection *, bool) override { }
  void close(ZiConnection *cxn, bool) override {
    closed = true;
    cxn->txContext().complete();
  }
  void wake() override { event.post(); }
  TelKey telKey() const override { return {}; }
  void telemetry(Ztc::MxTelemetry &) const override { }
  unsigned allCxns(AllCxnsFn) const override { return 0; }
  unsigned allQueues(Ztc::QueueMgr::AllFn) const override { return 0; }
  void watch(AddCxnFn, DelCxnFn) override { }
  void unwatch() override { }
};


class SyncCxn : public ZmNHeap<SyncCxn>, public ZiConnection {
friend inline ZuStringT<"ZiIOBufTest.SyncCxn"> ZmNHeapID(SyncCxn *);
public:
  explicit SyncCxn(ZiMultiplex *mx) : ZiConnection{mx,
    {.type = ZiCxnType::TCPOut, .socket = Zi::nullSocket()}} { }
  void connected(ZiRxContext &) override { }
  void disconnected(bool) override { }
};

void testSyncOwnerProgress()
{
  ZuTestScope(testSyncOwnerProgress);
  for (unsigned mode = 1; mode <= 3; ++mode) {
    ZiMultiplex mx;
    ZmRef<MxPlugin> transport = new MxPlugin{&mx};
    transport->syncMode = mode;
    auto state = transport.ptr();
    mx.plugin(ZuMv(transport));
    auto allocator = mx.allocator<>();
    auto buf = allocator->allocBuf(nullptr, 1);
    ZuCheck(bool(buf));
    if (!buf) return;
    ZmRef<SyncCxn> cxn = new SyncCxn{&mx};
    bool started = mx.start();
    ZuCheck(started);
    if (!started) return;
    mx.txRun([cxn = cxn.ptr(), state, buf = ZuMv(buf)]() mutable {
      cxn->txContext().init(ZiTxFn{[state](ZiTxContext &context) {
	++state->completions;
	state->error = context.length < 0;
	context.complete();
	return true;
      }}, ZuMv(buf));
    });
    cxn->sync(); // must yield Tx until the already-posted owner progress completes
    cxn->sync(); // an already-completed context needs no further send or waiter
    ZuCheck(mx.stop());
    ZuCheck(!state->spun && state->sends == 1 && state->progressed);
    ZuCheck(state->closed == (mode == 3));
    ZuCheck(state->completions == unsigned(mode != 3));
    ZuCheck(state->error == (mode == 2));
    ZuCheck(cxn->txContext().completed() && !cxn->txContext().buf);
  }
}

void testPoolBacking(bool grow)
{
  ZuTestScopeRT(testPoolBacking);
  ZiMultiplex mx;
  ZmRef<MxPlugin> transport = new MxPlugin{&mx};
  mx.plugin(ZuMv(transport));
  auto allocator = mx.allocator<Zi::IOBufType::TxTCP, ZiIOBuf, 32>();
  auto plugin = static_cast<BufPool *>(allocator->pool.ptr());
  int owner = 0;
  auto buf = allocator->allocBuf(&owner);
  ZuCheckRT(bool(buf));
  if (!buf) return;
  ZuCheckRT(buf->registered() && !buf->builtin() && buf->size == plugin->size);
  ZuCheckRT(buf->skip == plugin->headRoom);
  ZuCheckRT(plugin->registeredAllocs == 1 && !plugin->registeredFrees);
  *buf << "payload";
  if (grow) {
    ZuCheckRT(buf->ensure(128));
    ZuCheckRT(!buf->registered() && !buf->builtin() && buf->size >= 131);
    ZuCheckRT(buf->skip == 3 && buf->cspan() == "payload" && buf->owner == &owner);
    ZuCheckRT(plugin->registeredAllocs == 1 && plugin->registeredFrees == 1);
  }
  buf = nullptr;
  ZuCheckRT(plugin->valid && plugin->registeredAllocs == 1 && plugin->registeredFrees == 1);
}

void testPoolAllocation()
{
  ZuTestScope(testPoolAllocation);
  ZiMultiplex mx;
  ZmRef<MxPlugin> transport = new MxPlugin{&mx};
  auto state = transport.ptr();
  mx.plugin(ZuMv(transport));
  auto tx = mx.allocator<Zi::IOBufType::TxTCP, ZiIOBuf, 32>();
  auto rx = mx.allocator<Zi::IOBufType::Rx, ZiIOBuf, 32>();
  auto plugin = state->pools[0];
  auto ingress = state->pools[1];
  ZuCheck(tx->pool == plugin && rx->pool == ingress && ingress != plugin);
  int owner = 0;
  ZuCheck(!tx->allocBuf(&owner, tx->capacity() + 1));
  ZuCheck(!rx->allocBuf(&owner, 1));
  plugin->exhausted = true;
  ZuCheck(!tx->allocBuf(&owner));
  {
    auto buf = rx->allocBuf(&owner);
    ZuCheck(bool(buf));
    if (!buf) return;
    ZuCheck(buf->registered() && !buf->builtin() && !buf->data_() && !buf->size);
    ZuCheck(buf->owner == &owner && buf->skip == ingress->headRoom);
    ZuCheck(buf->ensure(8) && !buf->registered() && buf->avail() >= 8);
  }
  ZuCheck(!plugin->registeredAllocs && !plugin->registeredFrees);
  ZuCheck(!ingress->registeredAllocs && !ingress->registeredFrees);
  plugin->exhausted = false;
  {
    auto buf = tx->allocBuf(&owner, tx->capacity());
    ZuCheck(bool(buf));
    if (!buf) return;
    ZuCheck(buf->registered() && buf->length == tx->capacity());
    ZuCheck(buf->avail() == plugin->tailRoom);
  }
  ZuCheck(plugin->valid && plugin->registeredAllocs == 1 && plugin->registeredFrees == 1);

  // MPS excludes the plugin's reserved bytes on both sides of the payload.
  unsigned mps = tx->capacity() - 1;
  ZuCheck(!tx->allocBuf(&owner, mps + 1, mps));
  {
    auto buf = tx->allocBuf(&owner, mps, mps);
    ZuCheck(bool(buf));
    if (!buf) return;
    ZuCheck(buf->registered() && buf->length == mps);
    ZuCheck(buf->size == mps + plugin->headRoom + plugin->tailRoom);
    ZuCheck(buf->skip == plugin->headRoom && buf->avail() == plugin->tailRoom);
  }
  ZuCheck(plugin->valid && plugin->registeredAllocs == 2 && plugin->registeredFrees == 2);
}

template <unsigned Tag> struct TestRxBuf : ZiIOBuf { using ZiIOBuf::ZiIOBuf; };

struct RxDelivery {
  bool setup = false, stub = false, received = false;
  ZiIOBuf *prepared = nullptr;
};

template <typename Buf>
class RxCxn : public ZmNHeap<RxCxn<Buf>>, public ZiConnection {
friend inline ZuStringT<"ZiIOBufTest.RxCxn"> ZmNHeapID(RxCxn *);
public:
  RxCxn(ZiMultiplex *mx, const ZiCxnInfo &ci, RxDelivery *delivery) :
    ZiConnection{mx, ci}, m_delivery{delivery} { }

  void connected(ZiRxContext &io) override {
    auto buf = info().rxAllocator->allocBuf(this);
    m_delivery->prepared = buf.ptr();
    m_delivery->stub = buf && dynamic_cast<Buf *>(buf.ptr()) &&
      !buf->data_() && !buf->size && buf->registered() && !buf->builtin() &&
      buf->allocator() == info().rxAllocator && buf->owner == this;
    if (buf)
      io.init(ZiRxFn{this, ZmFnPtr<&RxCxn::received>{}}, ZuMv(buf));
  }
  void disconnected(bool) override { }

private:
  bool received(ZiRxContext &io) {
    m_delivery->received = io.buf && io.buf.ptr() == m_delivery->prepared &&
      dynamic_cast<Buf *>(io.buf.ptr()) && io.buf->owner == this && io.cxn == this &&
      io.buf->allocator() == info().rxAllocator && io.buf->registered() &&
      io.buf->cspan() == "frame";
    io.complete();
    return true;
  }

  RxDelivery *m_delivery;
};

template <typename Buf>
ZiConnectFn rxSetup(ZiMultiplex *mx, Zi::IOBufAllocator *allocator, int type,
  RxDelivery *delivery)
{
  return ZiConnectFn{[mx, allocator, type, delivery](const ZiCxnInfo &ci) -> ZiConnection * {
    delivery->setup = ci.rxAllocator == allocator && ci.type == type;
    if (!delivery->setup) return nullptr;
    return new RxCxn<Buf>{mx, ci, delivery};
  }};
}

bool allocatorsClosed(ZiMultiplex &mx)
{
  return !mx.allocator<>() &&
    !mx.allocator<Zi::IOBufType::Rx, TestRxBuf<0>, 64>(7) &&
    !mx.allocator<Zi::IOBufType::Rx, TestRxBuf<1>, 128>(11) &&
    !mx.allocator<Zi::IOBufType::TxTCP, ZiIOBuf, 32>(5) &&
    !mx.allocator<Zi::IOBufType::TxUDP, ZiIOBuf, 32>(5) &&
    !mx.allocator<Zi::IOBufType::RxTLS>() && !mx.allocator<Zi::IOBufType::TxTLS>();
}

bool poolGeometry(
  Zi::IOBufAllocator *allocator, Zi::IOBufType::T type,
  unsigned size, unsigned align, unsigned count)
{
  if (!allocator || !allocator->pool) return false;
  auto pool = allocator->pool.ptr();
  return allocator->type == type && allocator->size == size &&
    pool->type == type && pool->count == count && pool->size == size &&
    pool->align == align && pool->headRoom == 3 && pool->tailRoom == 4;
}

enum { FixtureTCP = 1, FixtureUDP = 2 };

void testPluginPools(unsigned protocols)
{
  ZuTestScopeRT(testPluginPools);
  bool tcp = protocols & FixtureTCP, udp = protocols & FixtureUDP;
  MxPlugin::Lifecycle trace;
  {
    ZiMultiplex mx;
    auto state = new MxPlugin{&mx, &trace};
    state->deferred = true;
    ZmRef<ZiMxPlugin> transport = state;
    mx.plugin(ZuMv(transport));
    ZuCheckRT(!transport && mx.plugin() == state);
    transport = state;
    mx.plugin(ZuMv(transport));
    ZuCheckRT(mx.plugin() == state);

    auto rx0 = mx.allocator<Zi::IOBufType::Rx, TestRxBuf<0>, 64>(7);
    auto repeat = mx.allocator<Zi::IOBufType::Rx, TestRxBuf<0>, 64>(7);
    auto rx1 = tcp ? mx.allocator<Zi::IOBufType::Rx, TestRxBuf<1>, 128>(11) : nullptr;
    auto rx2 = udp ? mx.allocator<Zi::IOBufType::Rx, TestRxBuf<2>, 96>(9) : nullptr;
    ZuCheckRT(rx0 && repeat && (!tcp || rx1) && (!udp || rx2));
    if (!rx0 || !repeat || (tcp && !rx1) || (udp && !rx2)) return;
    auto ingress = state->pools[0];
    ZuCheckRT(rx0 != repeat && repeat->pool == ingress &&
      (!tcp || (rx1 != rx0 && rx1->pool == ingress)) &&
      (!udp || (rx2 != rx0 && rx2 != rx1 && rx2->pool == ingress)));
    ZuCheckRT(poolGeometry(rx0, Zi::IOBufType::Rx, 64, ZiIOBuf_DefltAlign, 7));
    ZuCheckRT(state->requests == 1 && state->nPools == 1);
    {
      auto stub = rx0->allocBuf();
      ZuCheckRT(stub && dynamic_cast<TestRxBuf<0> *>(stub.ptr()) &&
	!stub->data_() && !stub->size && stub->registered());
    }
    ZuCheckRT(ingress && !ingress->registeredAllocs && !ingress->registeredFrees);

    auto tx0 = tcp ? mx.allocator<Zi::IOBufType::TxTCP, ZiIOBuf, 32>(5) : nullptr;
    auto tx1 = tcp ? mx.allocator<Zi::IOBufType::TxTCP, ZiIOBuf, 48, 16>(6) : nullptr;
    auto tx2 = udp ? mx.allocator<Zi::IOBufType::TxUDP, ZiIOBuf, 32>(5) : nullptr;
    auto tx3 = udp ? mx.allocator<Zi::IOBufType::TxUDP, ZiIOBuf, 56, 16>(8) : nullptr;
    if (tcp) {
      ZuCheckRT(poolGeometry(tx0, Zi::IOBufType::TxTCP, 32, ZiIOBuf_DefltAlign, 5));
      ZuCheckRT(poolGeometry(tx1, Zi::IOBufType::TxTCP, 48, 16, 6));
      if (!tx0 || !tx1) return;
      ZuCheckRT(tx0->pool != tx1->pool && !tx0->allocBuf() && !tx1->allocBuf());
    }
    if (udp) {
      ZuCheckRT(poolGeometry(tx2, Zi::IOBufType::TxUDP, 32, ZiIOBuf_DefltAlign, 5));
      ZuCheckRT(poolGeometry(tx3, Zi::IOBufType::TxUDP, 56, 16, 8));
      if (!tx2 || !tx3) return;
      ZuCheckRT(tx2->pool != tx3->pool && !tx2->allocBuf() && !tx3->allocBuf());
    }
    auto app = mx.allocator<>();
    ZuCheckRT(app && !app->pool && app->allocBuf());
    state->finalAllocator = tcp ? tx0 : tx2;
    auto requests = 1U + (tcp ? 2U : 0U) + (udp ? 2U : 0U);
    ZuCheckRT(state->requests == requests && state->nPools == requests);
    bool explicitAlloc = tcp && udp;
    if (explicitAlloc) {
      ZuCheckRT(!mx.alloc().is<ZeException>() && state->sawPools && trace.allocations == 1);
      ZuCheckRT(allocatorsClosed(mx));
      ZuCheckRT(!mx.alloc().is<ZeException>() && trace.allocations == 1);
    }
    bool started = tcp ? mx.start() :
      ZmBlock<bool>{}([&mx](auto wake) { mx.start(ZuMv(wake)); });
    ZuCheckRT(started && state->startedAllocated && trace.starts == 1 &&
      state->sawPools && state->allocatedBeforeWorker && trace.allocations == 1);
    if (!started) return;
    ZuCheckRT(allocatorsClosed(mx));
    ZuCheckRT((!tcp || (tx0->allocBuf() && tx1->allocBuf())) &&
      (!udp || (tx2->allocBuf() && tx3->allocBuf())) && app->allocBuf());

    RxDelivery listener, outbound, datagram;
    bool listening = false, failed = false;
    ZmBlock<>{}([&mx, &listener, &outbound, &datagram, &listening, &failed,
      rx0, rx1, rx2, tcp, udp](auto wake) {
      mx.rxRun([&mx, &listener, &outbound, &datagram, &listening, &failed,
	rx0, rx1, rx2, tcp, udp, wake = ZuMv(wake)]() mutable {
	ZiIP ip{"127.0.0.1"};
	ZiFailFn fail{[&failed](bool) { failed = true; }};
	if (tcp) {
	  mx.listen(
	    ZiListenFn{[&listening, rx0](const ZiListenInfo &info) {
	      listening = info.rxAllocator == rx0;
	    }},
	    fail, rxSetup<TestRxBuf<0>>(&mx, rx0, ZiCxnType::TCPIn, &listener),
	    ip, 1, 1, rx0);
	  mx.connect(rxSetup<TestRxBuf<1>>(&mx, rx1, ZiCxnType::TCPOut, &outbound),
	    fail, ip, 0, ip, 1, rx1);
	}
	if (udp)
	  mx.udp(rxSetup<TestRxBuf<2>>(&mx, rx2, ZiCxnType::UDP, &datagram),
	    fail, ip, 2, {}, 0, rx2, ZiCxnOptions{}.udp(true));
	wake();
      });
    });
    ZuCheckRT(!failed);
    if (tcp) {
      ZuCheckRT(listening && listener.setup && listener.stub && listener.received);
      ZuCheckRT(outbound.setup && outbound.stub && outbound.received);
    }
    if (udp) ZuCheckRT(datagram.setup && datagram.stub && datagram.received);
    unsigned delivered = (tcp ? 2U : 0U) + (udp ? 1U : 0U);
    ZuCheckRT(ingress->valid && ingress->registeredAllocs == delivered &&
      ingress->registeredFrees == delivered && !ingress->registeredPtr);
    ZuCheckRT(mx.stop() && allocatorsClosed(mx));
    ZuCheckRT(mx.start() && trace.starts == 2 && trace.allocations == 1);
    ZuCheckRT(mx.stop() && trace.stops == 2 && state->requests == requests);
  }
  ZuCheckRT(trace.finals == 1 && trace.finalLive);
}

void testAllocationRetry(bool implicit)
{
  ZuTestScopeRT(testAllocationRetry);
  MxPlugin::Lifecycle trace;
  {
    ZiMultiplex mx;
    auto state = new MxPlugin{&mx, &trace};
    ZmRef<ZiMxPlugin> transport = state;
    state->deferred = state->failAlloc = true;
    mx.plugin(ZuMv(transport));
    auto rx = mx.allocator<Zi::IOBufType::Rx, TestRxBuf<0>, 64>(7);
    auto tx = mx.allocator<Zi::IOBufType::TxTCP, ZiIOBuf, 32>(5);
    auto app = mx.allocator<>();
    ZuCheckRT(rx && tx && app);
    if (!rx || !tx || !app) return;
    state->finalAllocator = tx;
    bool failed;
    if (implicit) {
      bool ok = ZmBlock<bool>{}([&mx](auto wake) { mx.start(ZuMv(wake)); });
      failed = !ok;
      if (ok) mx.stop();
    } else
      failed = mx.alloc().is<ZeException>();
    ZuCheckRT(failed && mx.state() == ZmEngineState::Stopped);
    ZuCheckRT(state->sawPools && state->allocatedBeforeWorker && trace.allocations == 1 &&
      !trace.starts && !trace.workersStarted.load_());
    ZuCheckRT(allocatorsClosed(mx) && state->requests == 2);
    ZuCheckRT(!tx->allocBuf() && app->allocBuf());

    state->failAlloc = false;
    state->failStart = true;
    bool started = mx.start();
    ZuCheckRT(!started && mx.state() == ZmEngineState::Stopped);
    if (started) mx.stop();
    ZuCheckRT(state->sawPools && state->allocatedBeforeWorker && state->startedAllocated &&
      trace.allocations == 2 && trace.starts == 1 && !trace.stops);
    ZuCheckRT(allocatorsClosed(mx) && state->requests == 2 && tx->allocBuf());

    state->failStart = false;
    started = ZmBlock<bool>{}([&mx](auto wake) { mx.start(ZuMv(wake)); });
    ZuCheckRT(started && trace.allocations == 2 && trace.starts == 2);
    if (started) ZuCheckRT(mx.stop());
    ZuCheckRT(allocatorsClosed(mx) && tx->allocBuf() && app->allocBuf());
    ZuCheckRT(state->requests == 2 && trace.stops == 1);
  }
  ZuCheckRT(trace.finals == 1 && trace.finalLive);
}

void testNeverStarted(bool explicitAlloc, bool deferred)
{
  ZuTestScopeRT(testNeverStarted);
  MxPlugin::Lifecycle trace;
  {
    ZiMultiplex mx;
    auto state = new MxPlugin{&mx, &trace};
    state->deferred = deferred;
    ZmRef<ZiMxPlugin> transport = state;
    mx.plugin(ZuMv(transport));
    auto app = mx.allocator<>();
    auto tx = mx.allocator<Zi::IOBufType::TxTCP, ZiIOBuf, 32>(5);
    ZuCheckRT(app && tx);
    if (!app || !tx) return;
    state->finalAllocator = app;
    ZuCheckRT(bool(tx->allocBuf()) == !deferred);
    if (explicitAlloc) {
      ZuCheckRT(!mx.alloc().is<ZeException>());
      ZuCheckRT(allocatorsClosed(mx) && state->requests == 1 && tx->allocBuf());
      state->finalAllocator = tx;
    }
  }
  ZuCheckRT(trace.allocations == unsigned(explicitAlloc && deferred));
  ZuCheckRT(!trace.starts && !trace.stops && trace.finals == 1 && trace.finalLive);
}

#ifndef _WIN32
int fatalRegistration(const char *mode)
{
  rlimit limit{};
  if (::setrlimit(RLIMIT_CORE, &limit)) return 2;
  ::alarm(15);
  ZiMultiplex mx{ZiMxParams{}.rxQuantum(!strcmp(mode, "late-start") ? 0 : 8)}, other;
  ZmRef<ZiMxPlugin> plugin = new MxPlugin{&mx};
  auto state = static_cast<MxPlugin *>(plugin.ptr());
  if (!strcmp(mode, "null")) {
    plugin = nullptr;
  } else if (!strcmp(mode, "different-mx")) {
    other.plugin(ZuMv(plugin));
    return 3;
  } else if (!strcmp(mode, "late-app")) {
    if (!mx.allocator<>()) return 3;
  } else if (!strcmp(mode, "late-kernel-alloc")) {
    if (mx.alloc().is<ZeException>()) return 3;
  } else if (!strcmp(mode, "late-start")) {
    if (mx.start()) { mx.stop(); return 3; }
  } else {
    state->deferred = true;
    mx.plugin(ZuMv(plugin));
    if (plugin) return 3;
    if (strcmp(mode, "moved-from")) plugin = state;
    if (!strcmp(mode, "switch"))
      plugin = new MxPlugin{&mx};
    else if (!strcmp(mode, "late-provider-error")) {
      state->failPool = true;
      if (mx.allocator<Zi::IOBufType::Rx>()) return 3;
    } else if (!strcmp(mode, "late-native")) {
      if (!mx.allocator<Zi::IOBufType::Rx>()) return 3;
    } else if (!strcmp(mode, "late-alloc") || !strcmp(mode, "late-failed-alloc")) {
      state->failAlloc = !strcmp(mode, "late-failed-alloc");
      if (mx.alloc().is<ZeException>() != state->failAlloc) return 3;
    } else if (!strcmp(mode, "late-stop")) {
      if (!mx.start() || !mx.stop()) return 3;
    } else if (strcmp(mode, "moved-from"))
      return 3;
  }
  mx.plugin(ZuMv(plugin));
  return 3;
}

void testFatalRegistration(const char *exe)
{
  ZuTestScopeRT(testFatalRegistration);
  for (auto mode : {"null", "moved-from", "different-mx", "switch", "late-app",
    "late-native", "late-provider-error", "late-kernel-alloc", "late-alloc",
    "late-failed-alloc", "late-start", "late-stop"}) {
    int output[2];
    if (::pipe(output)) { ZuCheckRT(false, log_("pipe failed")); return; }
    auto pid = ::fork();
    if (!pid) {
      ::close(output[0]);
      if (::dup2(output[1], STDERR_FILENO) < 0) _exit(126);
      ::close(output[1]);
      ::execlp(exe, exe, "--fatal-registration", mode, nullptr);
      _exit(127);
    }
    ::close(output[1]);
    if (pid < 0) { ::close(output[0]); ZuCheckRT(false, log_("fork failed")); return; }
    ZtString<> diagnostics;
    char data[1024];
    for (;;) {
      auto n = ::read(output[0], data, sizeof(data));
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) break;
      diagnostics << ZuCSpan{data, unsigned(n)};
    }
    ::close(output[0]);
    int status = 0;
    pid_t waited;
    do waited = ::waitpid(pid, &status, 0); while (waited < 0 && errno == EINTR);
    bool atSetter = diagnostics.data() && strstr(diagnostics.data(), "ZiMultiplex::attachPlugin");
    ZuCheckRT(waited == pid && WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT && atSetter,
      log_(mode, " status ", status, diagnostics));
  }
}
#endif

struct HugePrint;
struct HugePrintFn : ZuPrintBuffer {
  static unsigned length(const HugePrint &) { return UINT_MAX; }
  static unsigned print(char *, unsigned, const HugePrint &) { ZmAssert(false); return 0; }
};
struct HugePrint { friend HugePrintFn ZuPrintType(HugePrint *); };
struct FailingDelegate {
  unsigned *calls;
  template <typename S> void print(S &s) const {
    ++*calls;
    s << HugePrint{} << "ignored";
  }
  friend ZuPrintFn ZuPrintType(FailingDelegate *);
};

// The deliberate UINT_MAX append is rejected before reading the short source.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 15
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wstringop-overread"
#endif
void testOutputFailure()
{
  ZuTestScope(testOutputFailure);

  ZiMultiplex allocatorMx;
  auto allocator = allocatorMx.allocator<>();
  ZmRef<ZiIOBuf> buf = allocator->allocBuf();
  *buf << "prefix";
  ZuCheck(!buf->append(reinterpret_cast<const uint8_t *>("x"), UINT_MAX));
  ZuCheck(buf->failed());
  *buf << "ignored" << '!' << 42;
  ZuCheck(buf->cspan() == "prefix");
  ZuCheck(!buf->ensure(buf->length));

  buf->clear();
  ZuCheck(!buf->failed());
  *buf << 'a' << 42;
  ZuCheck(buf->cspan() == "a42");
  unsigned calls = 0;
  *buf << FailingDelegate{&calls};
  ZuCheck(buf->failed() && calls == 1);
  *buf << FailingDelegate{&calls};
  ZuCheck(calls == 1 && buf->cspan() == "a42");

  buf->clear();
  buf->skip = 1;
  *buf << HugePrint{};
  ZuCheck(buf->failed() && !buf->length);
  buf->clear();
  *buf << "prefix";
  auto deniedGrowth = [](unsigned size, unsigned) { return size; };
  ZuCheck(!buf->ensure<deniedGrowth>(buf->size + 1));
  ZuCheck(buf->failed() && buf->cspan() == "prefix");
  *buf << "ignored";
  ZuCheck(buf->cspan() == "prefix");

  buf->clear();
  *buf << "ok";
  ZuCheck(!buf->failed() && buf->cspan() == "ok");
}


#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 15
#pragma GCC diagnostic pop
#endif

int main(int argc, char **argv)
{
#ifndef _WIN32
  if (argc == 3 && !strcmp(argv[1], "--fatal-registration"))
    return fatalRegistration(argv[2]);
#endif
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testSliceEnsureSpan);
  ZuTestCall(testAdvanceRewind);
  ZuTestCall(testPrependAndGrow);
  ZuTestCall(testPrependTailRoom);
  ZuTestCall(testSpanOffsetAndLargeEnsure);
  ZuTestCall(testOutputFailure);
  ZuTestCall(testPoolList);
  ZuTestCall(testPoolAllocation);
  ZuTestCall(testAllocatorPolicy);
  ZuTestCall(testSyncOwnerProgress);
  ZuTestCall(testPoolBacking, false);
  ZuTestCall(testPoolBacking, true);
  ZuTestCall(testPluginPools, FixtureTCP);
  ZuTestCall(testPluginPools, FixtureUDP);
  ZuTestCall(testPluginPools, FixtureTCP | FixtureUDP);
  ZuTestCall(testAllocationRetry, false);
  ZuTestCall(testAllocationRetry, true);
  ZuTestCall(testNeverStarted, false, true);
  ZuTestCall(testNeverStarted, true, true);
  ZuTestCall(testNeverStarted, true, false);
#ifndef _WIN32
  ZuTestCall(testFatalRegistration, argv[0]);
#endif
  return 0;
}
