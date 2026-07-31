//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// generic command/telemetry for queues

#ifndef ZtcQueue_HH
#define ZtcQueue_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuID.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZuSwitch.hh>
#include <zlib/ZuTuple.hh>

#include <zlib/ZmFn_.hh>

#include <zlib/ZtcTypes.hh>

namespace Ztc {

namespace QueueType {
  using T = int8_t;
  enum { Thread, IPC, Rx, Tx, N };
  using Names = ZuStringTL<"Thread", "IPC", "Rx", "Tx">;
  inline constexpr T lookup(ZuCSpan name) {
    static constexpr auto matcher = ZuMatcher<Names>();
    return matcher.exact(name);
  }
  inline constexpr ZuCSpan name(int value) {
    return ZuSwitch::dispatch<Names::N>(value, [](auto I) -> ZuCSpan {
      return ZuType<I, Names>{};
    }, ZuCSpan{});
  }
}

struct QueueTelemetry {
  ZuID		ownerID;	// primary key
  ZuID		id;		// primary key
  uint64_t	inBytes = 0;	// dynamic
  uint64_t	outBytes = 0;	// dynamic
  uint64_t	inCount = 0;	// dynamic (*)
  uint64_t	outCount = 0;	// dynamic (*)
  uint64_t	count = 0;	// dynamic - may not equal in - out
  uint32_t	size = 0;	// 0 if not fixed-size
  uint32_t	full = 0;	// dynamic - how many times queue overflowed
  QueueType::T	type = -1;	// primary key - QueueType

  RAG::T rag() const {
    if (!size) return RAG::Off;
    if (count * 10 >= (uint64_t(size)<<3)) return RAG::Red;
    if ((count<<1) >= size) return RAG::Amber;
    return RAG::Green;
  }
  void rag(RAG::T) { }
};

struct Queue {
  virtual ZuTuple<const ZuID &, const ZuID &, QueueType::T>
    telKey() const = 0;
  virtual void telemetry(QueueTelemetry &data) const = 0;
};

struct QueueMgr {
  using AllFn = ZmFn<void(Queue *), AllFnHeapID>;

  virtual unsigned allQueues(AllFn) const = 0;
};

} // Ztc

#endif /* ZtcQueue_HH */
