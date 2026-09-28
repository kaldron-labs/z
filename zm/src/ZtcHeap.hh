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
#include <zlib/ZuID.hh>
#include <zlib/ZuVStream.hh>

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
  uint32_t	size = 0;		// primary key
  uint16_t	partition = 0;		// primary key
  uint16_t	alignment = 0;		// primary key
  uint8_t	vshift = 0;
  bool		sharded = false;	// primary key

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
  // id, partition, size, alignment, sharded
  ZuDerive(TelKey, (ZuTuple<ZuCSpan, uint16_t, uint32_t, uint16_t, bool>));
  virtual TelKey telKey() const = 0;
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

// heapCSV() dumps heap telemetry for all current heaps

ZmExtern void HeapCSV_print(ZuVStream);
struct HeapCSV {
  template <typename S>
  void print(S &s) const { HeapCSV_print(s); }
  friend ZuPrintFn ZuPrintType(HeapCSV *);
};
static HeapCSV heapCSV() { return HeapCSV(); }

// heapTuneCSV(headroom) outputs a tuned heap configuration CSV,
// based on observed peak heap allocations, that can be loaded at
// startup with ZiHeapTune::init or ZiHeapTune::load
// - headroom defaults to 0.05, i.e. 5% headroom above the observed peak

ZmExtern void HeapTuneCSV_print(ZuVStream, double headroom);
struct HeapTuneCSV {
  double headroom;
  HeapTuneCSV(double headroom_) : headroom{headroom_} { }
  template <typename S>
  void print(S &s) const { HeapTuneCSV_print(s, headroom); }
  friend ZuPrintFn ZuPrintType(HeapTuneCSV *);
};
static HeapTuneCSV heapTuneCSV(double headroom = 0.05) {
  return HeapTuneCSV(headroom);
}

} // Ztc

#endif /* ZtcHeap_HH */
