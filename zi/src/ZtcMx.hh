//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// generic command/telemetry for I/O multiplexers and connections

#ifndef ZtcMx_HH
#define ZtcMx_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZuID.hh>
#include <zlib/ZuTuple.hh>

#include <zlib/ZmEngine.hh>
#include <zlib/ZmFn_.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmRWLock.hh>

#include <zlib/ZiIP.hh>
#include <zlib/ZtcQueue.hh>
#include <zlib/ZtcTypes.hh>

namespace Ztc {

// display sequence:
//   mxID, type, remoteIP, remotePort, localIP, localPort,
//   socket, flags, mreqAddr, mreqIf, mreqIfIndex,
//   mif, mifIndex, ttl,
//   rxBufSize, rxBufLen, txBufSize, txBufLen
struct CxnTelemetry {
  ZuID		mxID;		// multiplexer ID
  uint64_t	socket = 0;	// Unix file descriptor / Winsock SOCKET
  uint64_t	rxCalls = 0;	// graphable
  uint64_t	rxBytes = 0;	// graphable
  uint64_t	txCalls = 0;	// graphable
  uint64_t	txBytes = 0;	// graphable
  uint32_t	rxBufSize = 0;	// graphable - getsockopt(..., SO_RCVBUF, ...)
  uint32_t	rxBufLen = 0;	// graphable (*) - ioctl(..., SIOCINQ, ...)
  uint32_t	txBufSize = 0;	// graphable - getsockopt(..., SO_SNDBUF, ...)
  uint32_t	txBufLen = 0;	// graphable (*) - ioctl(..., SIOCOUTQ, ...)
  ZiIP		mreqAddr;	// mreqs[0]
  ZiIP		mreqIf;		// mreqs[0]
  uint32_t	mreqIfIndex = 0;
  ZiIP		mif;
  uint32_t	mifIndex = 0;
  uint32_t	ttl = 0;
  ZiIP		localIP;	// primary key
  ZiIP		remoteIP;	// primary key
  uint16_t	localPort = 0;	// primary key
  uint16_t	remotePort = 0;	// primary key
  uint8_t	flags = 0;	// ZiCxnFlags
  int8_t	type = -1;	// ZiCxnType
};

struct Connection {
  using Key = ZuTuple<ZiIP, uint16_t, ZiIP, uint16_t>;

  virtual Key telKey() const = 0;
  virtual void telemetry(CxnTelemetry &data) const = 0;
};

// display sequence:
//   id, state, nThreads, rxThread, txThread,
//   priority, stackSize, partition, rxBufSize, txBufSize,
//   queueSize, ll, spin, timeout
struct MxTelemetry { // not graphable
  ZuID		id;		// primary key
  uint32_t	stackSize = 0;
  uint32_t	queueSize = 0;
  uint32_t	spin = 0;
  uint32_t	timeout = 0;
  uint32_t	rxBufSize = 0;
  uint32_t	txBufSize = 0;
  uint16_t	rxThread = 0;
  uint16_t	txThread = 0;
  uint16_t	partition = 0;
  int8_t	state = ZmEngineState::Stopped;
  uint8_t	ll = 0;
  uint8_t	priority = 0;
  uint8_t	nThreads = 0;
};

struct Mx {
  using AllCxnsFn =
    ZmFn<void(Connection *), AllFnHeapID>;
  using AddCxnFn =
    ZmFn<void(Connection *), WatchFnHeapID>;
  using DelCxnFn =
    ZmFn<void(Connection *), WatchFnHeapID>;
  using AddQueueFn =
    ZmFn<void(Queue *), WatchFnHeapID>;
  using DelQueueFn =
    ZmFn<void(Queue *), WatchFnHeapID>;

  virtual const ZuID &telKey() const = 0;
  virtual void telemetry(MxTelemetry &data) const = 0;
  virtual unsigned allCxns(AllCxnsFn fn) const = 0;
  virtual unsigned allQueues(QueueMgr::AllFn fn) const = 0;
  virtual void watch(
    AddCxnFn, DelCxnFn, AddQueueFn, DelQueueFn) = 0;
  virtual void unwatch() = 0;
};

struct MxMgr {
private:
  using WatchLock = ZmRWLock;
  using WatchGuard = ZmGuard<WatchLock>;

  static WatchLock &watchLock_();

public:
  using AllFn = ZmFn<void(Mx *), AllFnHeapID>;
  using AddFn = ZmFn<void(Mx *), WatchFnHeapID>;
  using DelFn = ZmFn<void(Mx *), WatchFnHeapID>;

  static unsigned all(AllFn);
  template <typename L> static void guard(L &&l) {
    WatchGuard guard(watchLock_());
    ZuFwd<L>(l)();
  }
  static void watch(AddFn, DelFn);
  static void unwatch();
};

} // Ztc

#endif /* ZtcMx_HH */
