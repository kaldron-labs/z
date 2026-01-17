//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZvQueue
// - generic base class for queue observability and telemetry

#ifndef ZvQueue_HH
#define ZvQueue_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#include <zlib/ZuInt.hh>

#include <zlib/ZtEnum.hh>

namespace ZvQueueType {
  ZtEnum(ZvQueueType, int8_t, Thread, IPC, Rx, Tx);
}

struct ZvQueueTelemetry {
  ZmIDString	id;		// primary key
  uint64_t	seqNo = 0;	// 0 for Thread, IPC
  uint64_t	count = 0;	// dynamic - may not equal in - out
  uint64_t	inCount = 0;	// dynamic (*)
  uint64_t	inElems = 0;	// dynamic
  uint64_t	outCount = 0;	// dynamic (*)
  uint64_t	outElems = 0;	// dynamic
  uint32_t	size = 0;	// 0 for Rx, Tx
  uint32_t	full = 0;	// dynamic - how many times queue overflowed
  int8_t	type = -1;	// primary key - QueueType
};

// vtbl bloat and call overhead is not a significant concern here
struct ZvQueue : public ZmPolymorph {
  virtual ZvQueueType::T type() const = 0;
  virtual ZuCSpan id() const = 0;
  virtual void telemetry(ZvQueueTelemetry &) const = 0;
};

#endif /* ZvQueue_HH */
