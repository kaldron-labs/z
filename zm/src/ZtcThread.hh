//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZmThread telemetry

#ifndef ZtcThread_HH
#define ZtcThread_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuBox.hh>
#include <zlib/ZuID.hh>
#include <zlib/ZuPrint.hh>

#include <zlib/ZmBitmap.hh>
#include <zlib/ZmFn_.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmPLock.hh>
#include <zlib/ZmRWLock.hh>

#include <zlib/ZtcTypes.hh>

namespace Ztc {

// display sequence:
//   name, sid, tid, cpuUsage, cpuset, priority, sysPriority,
//   stackSize, allocStack, allocHeap, partition, main, detached
struct ThreadTelemetry {
  ZuID		name;
  uint64_t	tid = 0;	// primary key
  uint64_t	stackSize = 0;
  ZmBitmap	cpuset;
  double	cpuUsage = 0.0;	// graphable (*)
  uint64_t	allocStack = 0;
  uint64_t	allocHeap = 0;
  int32_t	sysPriority = 0;
  uint16_t	sid = 0;	// thread container's slot ID
  uint16_t	partition = 0;
  int8_t	priority = -1;
  bool		main = 0;
  bool		detached = 0;
};

// Note: ZtStruct metadata declaration is deferred

struct Thread {
  virtual uint64_t telKey() const = 0;
  virtual void telemetry(ThreadTelemetry &data) const = 0;
};

struct ThreadMgr {
private:
  using WatchLock = ZmRWLock;
  using WatchGuard = ZmGuard<WatchLock>;

  static WatchLock &watchLock_();

public:
  using AllFn = ZmFn<void(Thread *), AllFnHeapID>;
  using AddFn = ZmFn<void(Thread *), WatchFnHeapID>;
  using DelFn = ZmFn<void(Thread *), WatchFnHeapID>;

  static unsigned all(AllFn);
  template <typename L> static void guard(L &&l) {
    WatchGuard guard(watchLock_());
    ZuFwd<L>(l)();
  }
  static void watch(AddFn, DelFn);
  static void unwatch();
};

// Thread CSV

template <class S> struct ThreadCSV_ {
  ThreadCSV_(S &stream) : m_stream(stream) { }
  void print() {
    m_stream <<
      "name,sid,tid,cpuUsage,cpuset,priority,sysPriority,"
      "stackSize,allocStack,allocHeap,partition,main,detached\n";
    ThreadMgr::all({this, ZmFnPtr<&ThreadCSV_::print_>{}});
  }
  void print_(Thread *thread) {
    ThreadTelemetry data;
    static ZmPLock lock;
    ZmGuard<ZmPLock> guard(lock);
    thread->telemetry(data);
    m_stream << data.name
      << ',' << data.sid
      << ',' << data.tid
      << ',' << ZuBoxed(data.cpuUsage * 100.0).fmt<ZuFmt::FP<2>>()
      << ",\"" << data.cpuset << '"'
      << ',' << ZuBoxed(data.priority)
      << ',' << ZuBoxed(data.sysPriority)
      << ',' << data.stackSize
      << ',' << ZuBoxed(data.allocStack)
      << ',' << ZuBoxed(data.allocHeap)
      << ',' << ZuBoxed(data.partition)
      << ',' << ZuBoxed(data.main)
      << ',' << ZuBoxed(data.detached)
      << '\n';
  }

private:
  S	&m_stream;
};
struct ThreadCSV {
  template <typename S> void print(S &s) const {
    ThreadCSV_<S>(s).print();
  }
  friend ZuPrintFn ZuPrintType(ThreadCSV *);
};
static ThreadCSV threadCSV() { return ThreadCSV(); }

} // Ztc

#endif /* ZtcThread_HH */
