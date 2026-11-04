//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>

#include <zlib/ZtString.hh>
#include <zlib/ZtArray.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiIOContext.hh>
#include <zlib/ZiPlatform.hh>

using namespace ZuTestUtil;

void testSliceAndEnsurePreserveSpan()
{
  ZuTestScope(testSliceAndEnsurePreserveSpan);

  ZmRef<ZiIOBuf> buf = new Zi::IOBufAlloc<ZiIOBuf, ZiIOBuf_DefltSize, ZiIOBuf_DefltMaxSize, ZiIOBuf_HeapID>{nullptr, 0};
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

  ZmRef<ZiIOBuf> buf = new Zi::IOBufAlloc<ZiIOBuf, ZiIOBuf_DefltSize, ZiIOBuf_DefltMaxSize, ZiIOBuf_HeapID>{nullptr, 0};
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

  ZmRef<ZiIOBuf> buf = new Zi::IOBufAlloc<ZiIOBuf, 8, 4096, ZiIOBuf_HeapID>{nullptr, 0};
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

  ZmRef<ZiIOBuf> buf = new Zi::IOBufAlloc<ZiIOBuf, 16, 1<<20, ZiIOBuf_HeapID>{nullptr, 0};
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

  ZmRef<ZiIOBuf> buf = new Zi::IOBufAlloc<ZiIOBuf, 8, 4096, ZiIOBuf_HeapID>{nullptr, 0};
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

  ZmRef<ZiIOBuf> buf = new Zi::IOBufAlloc<ZiIOBuf, ZiIOBuf_DefltSize, ZiIOBuf_DefltMaxSize, ZiIOBuf_HeapID>{nullptr, 0};
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

// Ordinary allocators carry only the callable/refcount interface, on Linux too.
ZuAssert(sizeof(ZiAppBufAllocator<>) == sizeof(Zi::IOBufAllocator));

template <typename Allocator>
bool checkAllocator(Zi::IOBufType::T type, bool hasRing)
{
  ZmRef<Allocator> concrete = new Allocator;
  ZmRef<Zi::IOBufAllocator> allocator = ZuMv(concrete);
  if (concrete || allocator->refCount() != 1 || allocator->type() != type)
    return false;
  auto ring = allocator->ring();
  if (bool(ring) != hasRing) return false;
#if Zi_URing
  if (ring && static_cast<Zi::IOBufAllocator *>(ring) != allocator.ptr())
    return false;
#endif
  auto buf = allocator->alloc(allocator.ptr(), 0);
#if Zi_URing
  if (buf->uring() != bool(ring && ring->fd() >= 0)) return false;
  if (buf->uring() && buf->ring() != ring) return false;
#endif
  if (buf->owner != allocator.ptr()) return false;
  *buf << "payload";
  if (buf->cspan() != "payload") return false;
  // A short-lived buffer has no owning reference back to its allocator.
  if (allocator->refCount() != 1) return false;
  buf = nullptr;
  return allocator->refCount() == 1;
}

void testAllocatorHierarchy()
{
  ZuTestScope(testAllocatorHierarchy);

#if Zi_URing
  log(Zi::uring() ? "io_uring available: checking registered allocator backing" :
    "io_uring unavailable: checking ordinary backing; native allocation untested");
#endif

  ZuCheck(checkAllocator<ZiAppBufAllocator<>>(Zi::IOBufType::App, false));
#if Zi_URing
  enum { hasRing = true };
#else
  enum { hasRing = false };
#endif
  ZuCheck(checkAllocator<ZiRxUDPBufAllocator<>>(Zi::IOBufType::RxUDP, hasRing));
  ZuCheck(checkAllocator<ZiTxUDPBufAllocator<>>(Zi::IOBufType::TxUDP, hasRing));
  ZuCheck(checkAllocator<ZiRxTCPBufAllocator<>>(Zi::IOBufType::RxTCP, hasRing));
  ZuCheck(checkAllocator<ZiTxTCPBufAllocator<>>(Zi::IOBufType::TxTCP, hasRing));
  ZuCheck(checkAllocator<ZiRxTLSBufAllocator<>>(Zi::IOBufType::RxTLS, hasRing));
  ZuCheck(checkAllocator<ZiTxTLSBufAllocator<>>(Zi::IOBufType::TxTLS, hasRing));
}

struct PolicyBuf : ZiIOBuf {
  uint64_t cookie = 0x1234;
  using ZiIOBuf::ZiIOBuf;
};
using PolicyHeap = ZuStringT<"Zi.PolicyTest">;

template <typename Allocator>
bool checkPolicy()
{
  // Two slots let the test distinguish one returned slot from a double return.
  enum { Slots = 2 };
  ZmRef<Zi::IOBufAllocator> pool = new Allocator{Slots};
  ZmRef<Zi::IOBufAllocator> other = new Allocator{Slots};
  auto shared = pool;
  auto moved = ZuMv(shared);
  if (shared || moved != pool || pool->refCount() != 2) return false;
  moved = nullptr;

  ZiTxContext io;
  io.buf = pool->alloc(pool.ptr());
  auto second = pool->alloc();
  auto separate = other->alloc();
  auto fallback = pool->alloc();
  auto original = io.buf.ptr();
  auto slot = original->data_();
  auto size = original->size;
  if (pool->refCount() != 1 || other->refCount() != 1 ||
      original->owner != pool.ptr() || original->skip || original->length ||
      static_cast<PolicyBuf *>(original)->cookie != 0x1234 ||
      static_cast<PolicyBuf *>(separate.ptr())->cookie != 0x1234)
    return false;
#if Zi_URing
  if (Zi::uring()) {
    using Descriptor = Zi::IOBufURing<PolicyBuf, PolicyHeap>;
    using Builtin = Zi::IOBufAlloc<PolicyBuf, 37, 4096, PolicyHeap>;
    ZuAssert(sizeof(Descriptor) < sizeof(Builtin));
    if (!original->uring() || original->ring() != pool->ring() ||
	!second->uring() || !separate->uring() ||
	separate->ring() != other->ring() || pool->ring() == other->ring() ||
	slot == separate->data_() || fallback->uring() || !fallback->jumbo() ||
	original->memory() - pool->ring()->slotSize() != ZmHeapAllocSize<sizeof(Descriptor)>{})
      return false;
  } else
#endif
  if (original->uring() || original->jumbo() ||
      size != Zi::BuiltinSize<PolicyBuf>(37)) return false;

  original->skip = 8;
  *original << "original";
  original->failed(true);
  if (original->data_() != slot || original->data() != slot + 8) return false;
  original->failed(false);
  if (!original->ensure(size + 1)) return false;
  if (io.buf.ptr() != original || original->uring() || original->ring() ||
      !original->jumbo() || original->data_() == slot ||
      original->skip != 8 || original->cspan() != "original" ||
      static_cast<PolicyBuf *>(original)->cookie != 0x1234) return false;
  auto grown = original->data_();
  original->failed(true);
  if (original->data_() != grown || original->data() != grown + 8) return false;
  original->failed(false);

#if Zi_URing
  if (Zi::uring()) {
    auto reused = pool->alloc();
    auto exhausted = pool->alloc();
    if (!reused->uring() || reused->data_() != slot || reused->skip ||
	reused->length || exhausted->uring()) return false;
    io.complete(); // destroying the grown descriptor must not return it twice
    auto duplicate = pool->alloc();
    if (duplicate->uring() || pool->refCount() != 1) return false;
    reused = nullptr;
    reused = pool->alloc();
    if (!reused->uring() || reused->data_() != slot) return false;
  }
#endif
  return pool->refCount() == 1 && other->refCount() == 1;
}

void testCustomPolicies()
{
  ZuTestScope(testCustomPolicies);
  ZuCheck((checkPolicy<Zi::RxTCPBufAllocator<PolicyBuf, 37, 4096, PolicyHeap>>()));
  ZuCheck((checkPolicy<Zi::TxTCPBufAllocator<PolicyBuf, 37, 4096, PolicyHeap>>()));
  ZuCheck((checkPolicy<Zi::RxUDPBufAllocator<PolicyBuf, 37, 4096, PolicyHeap>>()));
  ZuCheck((checkPolicy<Zi::TxUDPBufAllocator<PolicyBuf, 37, 4096, PolicyHeap>>()));
  ZuCheck((checkPolicy<Zi::RxTLSBufAllocator<PolicyBuf, 37, 4096, PolicyHeap>>()));
  ZuCheck((checkPolicy<Zi::TxTLSBufAllocator<PolicyBuf, 37, 4096, PolicyHeap>>()));
}

#if Zi_URing
template <typename Allocator>
bool concurrentAllocations()
{
  // Exhaust a non-power-of-two arena on every round. Workers allocate
  // together, retain their buffers for inspection, then free
  // a different worker's batch together. No slot may have two live owners.
  enum { Workers = 8, Batch = 9, Slots = 65, Rounds = 1024 };
  ZmRef<Zi::IOBufAllocator> pool = new Allocator{Slots};
  ZmRef<ZiIOBuf> bufs[Workers][Batch];
  ZmSemaphore go[Workers], ready[Workers];
  ZmAtomic<unsigned> used[Slots];
  uintptr_t base = ~uintptr_t(0);
  ZmThread threads[Workers];
  for (unsigned i = 0; i < Workers; ++i) {
    threads[i] = ZmThread{[&pool, &bufs, &go, &ready, &used, &base, i]() {
      for (unsigned n = 0; n < Rounds; ++n) {
	go[i].wait();
	for (unsigned j = 0; j < Batch; ++j) {
	  auto &buf = bufs[i][j];
	  buf = pool->alloc();
	  memset(buf->data(), uint8_t(n + i * Batch + j), 64);
	  buf->length = 64;
	}
	ready[i].post();
	go[i].wait();
	for (auto &buf : bufs[(i + 1) % Workers]) buf = nullptr;
	ready[i].post();
      }
      go[i].wait();
      for (unsigned n = 0; n < (Rounds<<4); ++n) {
	auto buf = pool->alloc();
	if (!buf->uring()) ::abort();
	unsigned slot = (uintptr_t(buf->data_()) - base)/pool->ring()->slotSize();
	if (slot >= Slots || used[slot].xch(1)) ::abort();
	memset(buf->data(), i, 64);
	for (unsigned j = 0; j < 64; ++j)
	  if (buf->data()[j] != i) ::abort();
	used[slot] = 0;
	buf = nullptr;
      }
    }};
  }
  bool intact = true;
  for (unsigned n = 0; n < Rounds; ++n) {
    for (auto &gate : go) gate.post();
    for (auto &gate : ready) gate.wait();
    unsigned native = 0;
    for (unsigned i = 0; i < Workers * Batch; ++i) {
      auto &buf = bufs[i/Batch][i % Batch];
      native += buf->uring();
      if (!n && buf->uring() && uintptr_t(buf->data_()) < base) base = uintptr_t(buf->data_());
      for (unsigned j = 0; j < i; ++j) {
	// Fail before destructing duplicate owners, which would double-return
	// the slot and obscure the allocation failure being tested.
	if (buf->data_() == bufs[j/Batch][j % Batch]->data_()) {
	  log("concurrent allocation duplicated a live slot");
	  ::abort();
	}
      }
      for (unsigned j = 0; j < buf->length; ++j)
	if (buf->data()[j] != uint8_t(n + i)) intact = false;
    }
    if (native != Slots || pool->ring()->available()) intact = false;
    for (auto &gate : go) gate.post();
    for (auto &gate : ready) gate.wait();
    if (!pool->ring()->available()) intact = false;
  }
  for (auto &gate : go) gate.post();
  for (auto &thread : threads) thread.join();
  return intact;
}

void testConcurrentAllocations()
{
  ZuTestScope(testConcurrentAllocations);
  if (!Zi::uring()) { log("native concurrent allocation unavailable"); return; }
  ZuCheck((concurrentAllocations<ZiRxTCPBufAllocator<1024>>()));
  ZuCheck((concurrentAllocations<ZiTxTCPBufAllocator<1024>>()));
  ZuCheck((concurrentAllocations<ZiRxUDPBufAllocator<1024>>()));
  ZuCheck((concurrentAllocations<ZiTxUDPBufAllocator<1024>>()));
  ZuCheck((concurrentAllocations<ZiRxTLSBufAllocator<1024>>()));
  ZuCheck((concurrentAllocations<ZiTxTLSBufAllocator<1024>>()));
}

void testConcurrentReturns()
{
  ZuTestScopeRT(testConcurrentReturns);
  if (!Zi::uring()) { log("native concurrent slot returns unavailable"); return; }
  // Two returning workers each own half of a saturated pool; allocation stays
  // on this thread. The gate starts both return batches without timed polling.
  enum { PerWorker = 32, Slots = PerWorker<<1 };
  using Bufs = ZtArray<ZmRef<ZiIOBuf>, ZtArrayHeapID<"Zi.ReturnTest">>;
  using Ptrs = ZtArray<uint8_t *, ZtArrayHeapID<"Zi.ReturnTest.Ptrs">>;
  ZmRef<Zi::IOBufAllocator> pool =
    new Zi::TxTCPBufAllocator<PolicyBuf, 37, 4096, PolicyHeap>{Slots};
  Bufs left, right;
  Ptrs slots;
  for (unsigned i = 0; i < Slots; ++i) {
    auto buf = pool->alloc();
    ZuCheckRT(buf->uring());
    slots.push(buf->data_());
    (i < PerWorker ? left : right).push(ZuMv(buf));
  }
  auto fallback = pool->alloc();
  ZuCheckRT(!fallback->uring() && pool->refCount() == 1);
  ZmSemaphore ready, go;
  auto worker = [&ready, &go](Bufs bufs) {
    return ZmThread{[bufs = ZuMv(bufs), &ready, &go]() mutable {
      ready.post();
      go.wait();
      bufs.null();
    }};
  };
  auto a = worker(ZuMv(left));
  auto b = worker(ZuMv(right));
  ready.wait(); ready.wait();
  go.post(); go.post();
  a.join(); b.join();
  Bufs returned;
  for (unsigned i = 0; i < Slots; ++i) {
    auto buf = pool->alloc();
    unsigned matches = 0;
    for (auto &slot : slots)
      if (buf->data_() == slot) { ++matches; slot = nullptr; }
    ZuCheckRT(buf->uring() && buf->ring() == pool->ring() && matches == 1);
    returned.push(ZuMv(buf));
  }
  fallback = pool->alloc();
  ZuCheckRT(!fallback->uring() && pool->refCount() == 1);
}

template <typename Allocator>
bool recycleSlots()
{
  ZmRef<Zi::IOBufAllocator> allocator = new Allocator;
  auto ring = allocator->ring();
  ZtArray<ZmRef<ZiIOBuf>> held;
  for (unsigned i = 0; i < ring->bufCount(); ++i) held.push(allocator->alloc());
  auto first = held[0].ptr();
  auto slot = first->data_();
  if (!first->uring()) return false;
  first->skip = 8;
  *first << "original";
  first->ensure(first->size + 1);
  if (first->uring() || first->ring() || first->cspan() != "original") return false;
  // Growth returned the registered slot immediately, although first survives.
  auto reused = allocator->alloc();
  if (!reused->uring() || reused->data_() != slot) return false;
  auto size = reused->size;
  *reused << "abcdef";
  reused->advance(2);
  reused->length = 2;
  if (reused->data_() != slot || reused->size != size ||
      reused->cspan() != "cd") return false;
  auto fallback = allocator->alloc();
  if (fallback->uring() || fallback->ring()) return false;
  reused = nullptr;
  reused = allocator->alloc();
  return reused->uring() && reused->data_() == slot && !reused->skip &&
    !reused->length && allocator->refCount() == 1;
}

void testNativeSlotReuse()
{
  ZuTestScopeRT(testNativeSlotReuse);
  if (!Zi::uring()) { log("native slot reuse unavailable"); return; }
  ZuCheckRT(recycleSlots<ZiTxTCPBufAllocator<64>>());
  ZuCheckRT(recycleSlots<ZiRxTCPBufAllocator<64>>());
}
#endif

// Sticky output failure survives backing replacement/release; only beginning
// a new output clears it. A new output resets the failure state.
void testFailureAcrossBackingChanges()
{
  ZuTestScope(testFailureAcrossBackingChanges);
  ZmRef<ZiIOBuf> buf = new Zi::IOBufAlloc<ZiIOBuf, 32, 4096,
    ZuStringT<"Zi.FailureStorage">>{nullptr, 0};
  *buf << "prefix";
  auto original = buf->data_();
  buf->failed(true);
  ZuCheck(buf->failed() && buf->data_() == original);
  ZuCheck(buf->alloc(buf->size + 1) && buf->jumbo() && buf->failed());
  ZuCheck(!buf->ensure(1));
  buf->free(buf->data_());
  ZuCheck(buf->failed() && !buf->data_() && !buf->size);
  ZuCheck(!buf->ensure(1));
  buf->clear();
  ZuCheck(!buf->failed());
  *buf << "ok";
  ZuCheck(!buf->failed() && buf->cspan() == "ok");
}

void testAllocationMemory()
{
  ZuTestScope(testAllocationMemory);
  struct CustomBuf : ZiIOBuf {
    uint64_t cookie = 0x1234;
    using ZiIOBuf::ZiIOBuf;
  };
  using Alloc = Zi::IOBufAlloc<CustomBuf, 37, 4096,
    ZuStringT<"Zi.MemoryTest">>;
  ZmRef<ZiIOBuf> buf = new Alloc{nullptr, 0};
  uint64_t descriptor = ZmHeapAllocSize<sizeof(Alloc)>{};
  ZuCheck(buf->memory() == descriptor);
  ZuCheck(descriptor >= sizeof(CustomBuf) + buf->size);
  *buf << "retained";
  ZuCheck(buf->ensure(2048));
  ZuCheck(buf->jumbo() && buf->cspan() == "retained");
  ZuCheck(static_cast<CustomBuf *>(buf.ptr())->cookie == 0x1234);
  ZuCheck(buf->memory() >= descriptor + buf->size);
  auto retained = buf->memory();
  buf->clear();
  ZuCheck(buf->memory() == retained);
}

#ifdef ZiMultiplex_DEBUG
struct CopyBuf : ZiIOBuf {
  uint64_t grown = 0;
  uint64_t moved = 0;
  using ZiIOBuf::ZiIOBuf;

protected:
  void copied_(uint64_t n, bool inPlace) override {
    if (inPlace) moved += n;
    else grown += n;
  }
};
using CopyAlloc = Zi::IOBufAlloc<CopyBuf, 16, 4096,
  ZuStringT<"Zi.CopyTest">>;

void testCopyObservations()
{
  ZuTestScope(testCopyObservations);
  ZmRef<CopyBuf> buf = new CopyAlloc{nullptr, 0};
  *buf << "abc";
  auto data = buf->data();
  ZuCheck(buf->ensure(3) == data);
  ZuCheck(!buf->grown && !buf->moved);
  ZuCheck(buf->ensure(buf->size + 1));
  ZuCheck(buf->grown == 3 && !buf->moved);
  ZuCheck(buf->cspan() == "abc");

  // Free tail space permits relocation instead of another allocation.
  memcpy(buf->prepend(1), "h", 1);
  ZuCheck(buf->cspan() == "habc");
  ZuCheck(buf->grown == 3 && buf->moved == 3);
  memcpy(buf->prepend(1), "H", 1);
  ZuCheck(buf->cspan() == "Hhabc");
  ZuCheck(buf->grown == 3 && buf->moved == 3);
  auto length = buf->length;
  buf->prepend(buf->size + 1);
  ZuCheck(buf->grown == 3 + length && buf->moved == 3);
  ZuCheck((ZuBSpan{buf->end() - length, length} == "Hhabc"));

  // Builder growth preserves just its declared head and tail regions.
  buf = new CopyAlloc{nullptr, 0};
  memcpy(buf->data(), "ab....yz", 8);
  buf->realloc(8, 12, 2, 2);
  ZuCheck(!buf->grown && buf->moved == 2);
  ZuCheck((ZuBSpan{buf->data(), 2} == "ab"));
  ZuCheck((ZuBSpan{buf->data() + 10, 2} == "yz"));
  auto size = buf->size + 1;
  auto next = buf->realloc(12, size, 2, 2);
  ZuCheck(buf->grown == 4 && buf->moved == 2);
  ZuCheck((ZuBSpan{next, 2} == "ab"));
  ZuCheck((ZuBSpan{buf->data_() + buf->size - 2, 2} == "yz"));
}
#endif

// Exercise each public storage operation on builtin, Jumbo and provided backing.
void testBackingOperations()
{
  ZuTestScopeRT(testBackingOperations);
  enum { Builtin, Jumbo, Registered, Kinds };
  enum { Allocate, Ensure, Reallocate, Prepend, Free, Clear, Destroy, Operations };
  enum { Skip = 3, Payload = sizeof("original") - 1 };
  unsigned kinds = Jumbo + 1;
#if Zi_URing
  ZuCheckRT(Zi::uring());
  if (Zi::uring()) kinds = Kinds;
#endif
  ZmRef<Zi::IOBufAllocator> pool =
    new Zi::TxTCPBufAllocator<PolicyBuf, 64, 4096, PolicyHeap>{1};
  for (unsigned kind = 0; kind < kinds; ++kind) {
    for (unsigned operation = 0; operation < Operations; ++operation) {
      ZmRef<ZiIOBuf> buf;
      if (kind == Registered) buf = pool->alloc();
      else buf = new Zi::IOBufAlloc<PolicyBuf, 64, 4096, PolicyHeap>{nullptr, 0};
      if (kind == Jumbo) ZuCheckRT(buf->ensure(buf->size + 1));
      ZuCheckRT(buf->uring() == (kind == Registered));
      ZuCheckRT(buf->jumbo() == (kind == Jumbo));
      auto backing = buf->data_();
      auto size = buf->size;
      buf->skip = Skip;
      buf->length = Payload;
      memcpy(buf->data(), "original", Payload);

      // Address extraction masks every packed bit, independently of ownership.
      auto saved = buf->data__;
      buf->data__ = uintptr_t(backing) |
	ZiIOBuf::Jumbo | ZiIOBuf::URing | ZiIOBuf::Failed;
      ZuCheckRT(buf->data_() == backing && buf->data() == backing + Skip);
      buf->data__ = saved;
      buf->failed(true);
      ZuCheckRT(buf->data_() == backing && buf->data() == backing + Skip);

      switch (operation) {
	case Allocate: {
	  auto data = buf->alloc(size + 1);
	  ZuCheckRT(data == buf->data_());
	} break;
	case Ensure:
	  ZuCheckRT(!buf->ensure(1) && buf->data_() == backing);
	  buf->failed(false);
	  ZuCheckRT(buf->ensure(size + 1));
	  ZuCheckRT(buf->cspan() == "original");
	  buf->failed(true);
	  break;
	case Reallocate: {
	  auto data = buf->realloc(Payload, size + 1, 2, 2);
	  ZuCheckRT(!memcmp(data, "or", 2));
	  ZuCheckRT(!memcmp(buf->data_() + buf->size - 2, "al", 2));
	} break;
	case Prepend:
	  buf->prepend(size + 1);
	  ZuCheckRT(!memcmp(buf->end() - Payload, "original", Payload));
	  break;
	case Free:
	  buf->free(buf->data_());
	  if (kind == Builtin) {
	    ZuCheckRT(buf->data_() == backing && buf->size == size);
	    ZuCheckRT(buf->cspan() == "original");
	  } else {
	    ZuCheckRT(!buf->data_() && !buf->size && !buf->length);
	    ZuCheckRT(!buf->uring() && !buf->jumbo() && !buf->ring());
	  }
	  break;
	case Clear:
	  buf->clear();
	  ZuCheckRT(buf->data_() == backing && buf->size == size);
	  ZuCheckRT(!buf->skip && !buf->length && !buf->failed());
	  ZuCheckRT(buf->uring() == (kind == Registered));
	  ZuCheckRT(buf->jumbo() == (kind == Jumbo));
	  break;
	case Destroy:
	  buf = nullptr;
	  break;
      }
      if (buf) {
	ZuCheckRT(static_cast<PolicyBuf *>(buf.ptr())->cookie == 0x1234);
	ZuCheckRT(buf->failed() == (operation != Clear));
	if (operation < Free) {
	  ZuCheckRT(buf->jumbo() && !buf->uring() && !buf->ring());
	  ZuCheckRT(buf->data_() != backing);
	}
      }
      if (kind == Registered) {
	if (operation == Clear) {
	  auto blocked = pool->alloc();
	  ZuCheckRT(!blocked->uring()); // clear retains the live slot
	  buf = nullptr;
	}
	// All other paths already released the slot, even if a grown descriptor
	// survives. Keeping its replacement live exposes any duplicate return.
	auto reused = pool->alloc();
	ZuCheckRT(reused->uring() && reused->data_() == backing);
	auto exhausted = pool->alloc();
	ZuCheckRT(!exhausted->uring());
	buf = nullptr;
	auto duplicate = pool->alloc();
	ZuCheckRT(!duplicate->uring());
	reused = nullptr;
	auto again = pool->alloc();
	ZuCheckRT(again->uring() && again->data_() == backing);
      }
    }
  }
  ZuCheckRT(pool->refCount() == 1);
}

#if Zi_URing
template <typename Allocator, bool Rx>
bool nativeGeometry()
{
  enum { Slots = 65, Offset = 7 };
  ZmRef<Allocator> pool = new Allocator{Slots};
  auto ring = pool->ring();
  if (!ring || ring->fd() < 0 || ring->bufCount() != Slots) return false;
  ZmRef<ZiIOBuf> held[Slots];
  uintptr_t first = 0;
  for (unsigned i = 0; i < Slots; ++i) {
    auto &buf = held[i];
    buf = pool->alloc(nullptr, 0);
    if (!buf->uring() || buf->ring() != ring) return false;
    auto address = uintptr_t(buf->data_());
    if (!i) first = address;
    unsigned stride = (ring->bufSize() + ZiIOBuf_Align - 1) &
      ~(ZiIOBuf_Align - 1);
    if (address % ZiIOBuf_Align || ring->slotSize() != stride ||
	address != first + size_t(i) * stride ||
	stride < buf->size) return false;
    // The first payload begins at the mapping base; free-list links reuse
    // buffer contents only after all application and kernel holds retire.
    auto mapping = first;
    auto extent = size_t(stride) * Slots;
    if (address < mapping ||
	address + buf->size > mapping + extent) return false;
    if constexpr (Rx) {
      auto rx = static_cast<Zi::IOBufRxRing *>(ring);
      if (!rx->bufRing() || rx->ringSize() != 128 ||
	  rx->index(buf->data_()) != i ||
	  rx->index(buf->data_() + Offset) != i) return false;
    }
    // Physical offsets belong to an operation, not a registered-buffer ID.
    ZiTxContext io;
    io.txRange(buf, Offset, 3);
    if (io.txData() != buf->data_() + Offset ||
	io.txOffset != Offset || io.txRemaining != 3) return false;
  }
  auto exhausted = pool->alloc(nullptr, 0);
  if (exhausted->uring() || ring->available()) return false;
  return true;
}

void testNativeGeometry()
{
  ZuTestScope(testNativeGeometry);
  if (!Zi::uring()) { log("native geometry unavailable"); return; }
  ZuCheck((nativeGeometry<ZiRxTCPBufAllocator<1037>, true>()));
  ZuCheck((nativeGeometry<ZiTxTCPBufAllocator<1037>, false>()));
  ZuCheck((nativeGeometry<ZiRxUDPBufAllocator<1037>, true>()));
  ZuCheck((nativeGeometry<ZiTxUDPBufAllocator<1037>, false>()));
  ZuCheck((nativeGeometry<ZiRxTLSBufAllocator<1037>, true>()));
  ZuCheck((nativeGeometry<ZiTxTLSBufAllocator<1037>, false>()));
}
#endif

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testSliceAndEnsurePreserveSpan);
  ZuTestCall(testAdvanceRewind);
  ZuTestCall(testPrependAndGrow);
  ZuTestCall(testPrependTailRoom);
  ZuTestCall(testSpanOffsetAndLargeEnsure);
  ZuTestCall(testOutputFailure);
  ZuTestCall(testAllocatorHierarchy);
  ZuTestCall(testCustomPolicies);
  ZuTestCall(testAllocationMemory);
  ZuTestCall(testFailureAcrossBackingChanges);
  ZuTestCall(testBackingOperations);
#ifdef ZiMultiplex_DEBUG
  ZuTestCall(testCopyObservations);
#endif
#if Zi_URing
  ZuTestCall(testNativeGeometry);
  ZuTestCall(testNativeSlotReuse);
  ZuTestCall(testConcurrentReturns);
  ZuTestCall(testConcurrentAllocations);
#endif
  return 0;
}
