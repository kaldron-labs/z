//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZmHeap telemetry

#ifndef ZtcHeap_HH
#define ZtcHeap_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuSpan.hh>
#include <zlib/ZuTuple.hh>

#include <zlib/ZmBitmap.hh>
#include <zlib/ZmFn_.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmPLock.hh>

#include <zlib/ZtcTypes.hh>

namespace Ztc {

// display sequence:
//   id, size, alignment, partition, sharded,
//   cacheSize, cpuset, cacheAllocs, cacheFrees, crossFrees,
//   heapAllocs, heapFrees, globalHeapAllocs, globalHeapFrees,
//   globalHeapMax, allocated (*)
// derived display fields:
//   allocated = (heapAllocs + cacheAllocs) - (cacheFrees + heapFrees)
// globalHeapAllocs, globalHeapFrees and globalHeapMax are per-ID,
// repeated in each arena row; the other counters are per-arena.
struct HeapTelemetry {
  ZuID		id;		// primary key
  uint64_t	cacheSize = 0;
  ZmBitmap	cpuset;
  uint64_t	cacheAllocs = 0;// graphable (*)
  uint64_t	cacheFrees = 0;	// cache returns, including crossFrees
  uint64_t	crossFrees = 0;	// graphable
  uint64_t	heapAllocs = 0;	// graphable (*)
  uint64_t	heapFrees = 0;	// graphable
  uint64_t	globalHeapAllocs = 0;
  uint64_t	globalHeapFrees = 0;
  uint64_t	globalHeapMax = 0;
  uint32_t	size = 0;	// primary key
  uint16_t	partition = 0;	// primary key
  uint8_t	sharded = 0;	// primary key
  uint8_t	alignment = 0;	// primary key

  uint64_t allocated() const {
    return (cacheAllocs + heapAllocs) - (cacheFrees + heapFrees);
  }
  void allocated(uint64_t) { }
  RAG::T rag() const {
    if (!cacheSize) return RAG::Off;
    if (allocated() > cacheSize) return RAG::Red;
    if (globalHeapMax > 0) return RAG::Amber;
    return RAG::Green;
  }
  void rag(RAG::T) { }
};

// Note: ZtStruct metadata declaration is deferred

struct Heap {
  virtual ZuTuple<ZuCSpan, uint32_t, uint8_t, uint16_t, uint8_t>
    telKey() const = 0;
  virtual void telemetry(HeapTelemetry &data) const = 0;
};

struct ZmAPI HeapMgr {
private:
  using WatchLock = ZmPLock;
  using WatchGuard = ZmGuard<WatchLock>;

public:
  using MatchFn = ZmFn<bool(Heap *), AllFnHeapID>;
  using CaptureFn =
    ZmFn<void(ZuSpan<const HeapTelemetry>), AllFnHeapID>;

private:
  static WatchLock &watchLock_();

public:
  using AllFn = ZmFn<void(Heap *), AllFnHeapID>;
  using AddFn = ZmFn<void(Heap *), WatchFnHeapID>;
  using DelFn = ZmFn<void(Heap *), WatchFnHeapID>;

  // Visits arenas in key order, with all arenas of an ID contiguous.
  static unsigned all(AllFn);
  static void capture(MatchFn, CaptureFn);
  template <typename L> static void guard(L &&l) {
    WatchGuard guard(watchLock_());
    ZuFwd<L>(l)();
  }
  static void watch(AddFn, DelFn);
  static void unwatch();
};

// Heap CSV

template <class S> struct HeapCSV_ {
  HeapCSV_(S &stream) : m_stream(stream) { }
  void print() {
    m_stream <<
      "ID,size,partition,sharded,alignment,cacheSize,cpuset,"
      "cacheAllocs,cacheFrees,crossFrees,heapAllocs,heapFrees,"
      "globalHeapAllocs,globalHeapFrees,globalHeapMax\n";
    HeapMgr::all({this, ZmFnPtr<&HeapCSV_::print_>{}});
  }
  void print_(Heap *heap) {
    HeapTelemetry data;
    heap->telemetry(data);
    if (!data.cacheAllocs && !data.heapAllocs &&
	!data.cacheFrees && !data.heapFrees) return;
    m_stream <<
      '"' << data.id << "\"," <<	// assume no need to quote embedded "
      ZuBoxed(data.size) << ',' <<
      ZuBoxed(data.partition) << ',' <<
      ZuBoxed(data.sharded) << ',' <<
      ZuBoxed(data.alignment) << ',' <<
      ZuBoxed(data.cacheSize) << ',' <<
      data.cpuset << ',' <<
      ZuBoxed(data.cacheAllocs) << ',' <<
      ZuBoxed(data.cacheFrees) << ',' <<
      ZuBoxed(data.crossFrees) << ',' <<
      ZuBoxed(data.heapAllocs) << ',' <<
      ZuBoxed(data.heapFrees) << ',' <<
      ZuBoxed(data.globalHeapAllocs) << ',' <<
      ZuBoxed(data.globalHeapFrees) << ',' <<
      ZuBoxed(data.globalHeapMax) << '\n';
  }

private:
  S	&m_stream;
};
struct HeapCSV {
  template <typename S> void print(S &s) const {
    HeapCSV_<S>(s).print();
  }
  friend ZuPrintFn ZuPrintType(HeapCSV *);
};
static HeapCSV heapCSV() { return HeapCSV(); }

} // Ztc

#endif /* ZtcHeap_HH */
