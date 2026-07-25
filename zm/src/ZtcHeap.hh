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

#include <zlib/ZuTuple.hh>

#include <zlib/ZmFn_.hh>

namespace Ztc {

// display sequence:
//   id, size, alignment, partition, sharded,
//   cacheSize, cpuset, cacheAllocs, heapAllocs,
//   frees, crossFrees, allocated (*)
// derived display fields:
//   allocated = (heapAllocs + cacheAllocs) - frees
struct HeapTelemetry {
  ZuID		id;		// primary key
  uint64_t	cacheSize = 0;
  ZmBitmap	cpuset;
  uint64_t	cacheAllocs = 0;// graphable (*)
  uint64_t	heapAllocs = 0;	// graphable (*)
  uint64_t	frees = 0;	// graphable
  uint64_t	crossFrees = 0;	// graphable
  uint32_t	size = 0;	// primary key
  uint16_t	partition = 0;	// primary key
  uint8_t	sharded = 0;	// primary key
  uint8_t	alignment = 0;	// primary key
};

// Note: ZtStruct metadata declaration is deferred

struct Heap {
  virtual ZuTuple<ZuID, uint32_t, uint8_t, uint16_t, uint8_t>
    key() const = 0;
  virtual void telemetry(HeapTelemetry &data) const = 0;
};

struct HeapMgr {
  using AllFn = ZmFn<void(Heap *), ZmFnHeapID<"Ztc.Heap.AllFn">>;

  static void all(AllFn);
};

// Heap CSV

template <class S> struct HeapCSV_ {
  HeapCSV_(S &stream) : m_stream(stream) { }
  void print() {
    m_stream <<
      "ID,size,partition,sharded,alignment,cacheSize,cpuset,"
      "cacheAllocs,heapAllocs,frees,crossFrees\n";
    HeapMgr::all({this, ZmFnPtr<&HeapCSV_::print_>{}});
  }
  void print_(Heap *heap) {
    HeapTelemetry data;
    heap->telemetry(data);
    if (!data.cacheAllocs && !data.heapAllocs) return;
    m_stream <<
      '"' << data.id << "\"," <<	// assume no need to quote embedded "
      ZuBoxed(data.size) << ',' <<
      ZuBoxed(data.partition) << ',' <<
      ZuBoxed(data.sharded) << ',' <<
      ZuBoxed(data.alignment) << ',' <<
      ZuBoxed(data.cacheSize) << ',' <<
      data.cpuset << ',' <<
      ZuBoxed(data.cacheAllocs) << ',' <<
      ZuBoxed(data.heapAllocs) << ',' <<
      ZuBoxed(data.frees) << ',' <<
      ZuBoxed(data.crossFrees) << '\n';
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
