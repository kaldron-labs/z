//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// generic command/telemetry for I/O multiplexers

#ifndef ZtcMx_HH
#define ZtcMx_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuID.hh>
#include <zlib/ZuSpan.hh>
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
//   id, state, nThreads, rxThread, txThread,
//   priority, stackSize, partition, rxBufSize, txBufSize,
//   queueSize, ll, spin, timeout
struct MxTelemetry { // not graphable
  ZuID		id;		// primary key
  uint32_t	stackSize = 0;
  uint32_t	queueSize = 0;
  uint32_t	spin = 0;
  uint32_t	timeout = 0;	// milliseconds
  uint32_t	rxBufSize = 0;
  uint32_t	txBufSize = 0;
  uint16_t	rxThread = 0;
  uint16_t	txThread = 0;
  uint16_t	partition = 0;
  int8_t	state = ZmEngineState::Stopped;
  uint8_t	ll = 0;
  uint8_t	priority = 0;
  uint8_t	nThreads = 0;

  RAG::T rag() const {
    switch (state) {
      case ZmEngineState::Stopped:
      case ZmEngineState::Stopping:
      case ZmEngineState::StopPending:
	return RAG::Red;
      case ZmEngineState::Starting:
      case ZmEngineState::StartPending:
	return RAG::Amber;
      case ZmEngineState::Running:
	return RAG::Green;
      default:
	return RAG::Off;
    }
  }
  void rag(RAG::T) { }
};

struct Mx {
  ZuDerive(TelKey, (ZuTuple<ZuCSpan>));
  virtual TelKey telKey() const = 0;
  virtual void telemetry(MxTelemetry &data) const = 0;
};

struct ZiAPI MxMgr {
private:
  using WatchLock = ZmRWLock;
  using WatchGuard = ZmGuard<WatchLock>;

  static WatchLock &watchLock_();

public:
  using AllFn = ZmFn<void(Mx *), AllFnHeapID>;
  using CaptureFn =
    ZmFn<void(ZuSpan<const MxTelemetry>), AllFnHeapID>;
  using AddFn = ZmFn<void(Mx *), WatchFnHeapID>;
  using DelFn = ZmFn<void(Mx *), WatchFnHeapID>;

  static unsigned all(AllFn);
  static void capture(CaptureFn);
  template <typename L> static void guard(L &&l) {
    WatchGuard guard(watchLock_());
    ZuFwd<L>(l)();
  }
  static void watch(AddFn, DelFn);
  static void unwatch();
};

} // Ztc

#endif /* ZtcMx_HH */
