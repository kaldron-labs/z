//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtString.hh>
#include <zlib/ZtArray.hh>

#include <zlib/ZiIOBuf.hh>
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
#ifndef _WIN32
  if (ring && static_cast<Zi::IOBufAllocator *>(ring) != allocator.ptr())
    return false;
#endif
  auto buf = allocator->alloc(allocator.ptr(), 0);
#ifndef _WIN32
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

#ifndef _WIN32
  log(Zi::uring() ? "io_uring available: checking registered allocator backing" :
    "io_uring unavailable: checking ordinary backing; native allocation untested");
#endif

  ZuCheck(checkAllocator<ZiAppBufAllocator<>>(Zi::IOBufType::App, false));
#ifndef _WIN32
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

#ifndef _WIN32
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
  auto fallback = allocator->alloc();
  if (fallback->uring() || fallback->ring()) return false;
  reused = nullptr;
  reused = allocator->alloc();
  return reused->uring() && reused->data_() == slot && allocator->refCount() == 1;
}

void testNativeSlotReuse()
{
  ZuTestScopeRT(testNativeSlotReuse);
  if (!Zi::uring()) { log("native slot reuse unavailable"); return; }
  ZuCheckRT(recycleSlots<ZiTxTCPBufAllocator<64>>());
  ZuCheckRT(recycleSlots<ZiRxTCPBufAllocator<64>>());
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
#ifndef _WIN32
  ZuTestCall(testNativeSlotReuse);
#endif
  return 0;
}
