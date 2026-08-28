//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// generic command/telemetry for I/O hubs

#ifndef ZtcHub_HH
#define ZtcHub_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZuID.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuTuple.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmEngine.hh>
#include <zlib/ZmFn_.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmRWLock.hh>

#include <zlib/ZtEnum.hh>

#include <zlib/ZtcLink.hh>
#include <zlib/ZtcPool.hh>
#include <zlib/ZtcTypes.hh>

namespace Ztc {

struct HubMgr;

namespace HubState {
  using namespace ZmEngineState;
  ZtEnumNames(ZiAPI, HubState,
    Stopped, Starting, Running, Stopping, StartPending, StopPending);
}

struct HubTelemetry {
  ZuID		id;		// primary key
  ZuID		mxID;
  uint16_t	down = 0;
  uint16_t	disabled = 0;
  uint16_t	transient = 0;
  uint16_t	up = 0;
  uint16_t	reconn = 0;
  uint16_t	failed = 0;
  uint16_t	nLinks = 0;
  uint8_t	rxThread = 0;
  uint8_t	txThread = 0;
  LinkType::T	linkType = 0;	// primary key
  HubState::T	state = -1;

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

struct Hub {
  using AllLinksFn =
    ZmFn<void(Link *), AllFnHeapID>;
  using AllPoolsFn =
    ZmFn<void(Pool *), AllFnHeapID>;

  virtual ZuTuple<LinkType::T, const ZuID &> telKey() const = 0;
  virtual void telemetry(HubTelemetry &data) const = 0;
  virtual bool start() = 0;
  virtual bool stop() = 0;
  virtual unsigned allLinks(AllLinksFn fn) const = 0;
  virtual unsigned allPools(AllPoolsFn fn) const = 0;

protected:
  void linkAdded_(Link *);
  void linkDeleted_(Link *);
  void linkDownInc_() { ++m_down; }
  void linkDownDec_() { --m_down; }
  void linkTransientInc_() { ++m_transient; }
  void linkTransientDec_() { --m_transient; }
  void linkUpInc_() { ++m_up; }
  void linkUpDec_() { --m_up; }
  static void poolAdded_(Pool *);
  static void poolDeleted_(Pool *);

  ZmAtomic<unsigned>	m_down = 0;
  ZmAtomic<unsigned>	m_transient = 0;
  ZmAtomic<unsigned>	m_up = 0;
  ZmAtomic<unsigned>	m_nLinks = 0;
};

struct HubMgr {
private:
  using WatchLock = ZmRWLock;
  using WatchGuard = ZmGuard<WatchLock>;

  static WatchLock &watchLock_();

public:
  using AllFn = ZmFn<void(Hub *), AllFnHeapID>;
  using CaptureFn =
    ZmFn<void(ZuSpan<const HubTelemetry>), AllFnHeapID>;
  using AddFn = ZmFn<void(Hub *), WatchFnHeapID>;
  using DelFn = ZmFn<void(Hub *), WatchFnHeapID>;
  using AddLinkFn = ZmFn<void(Link *), WatchFnHeapID>;
  using DelLinkFn = ZmFn<void(Link *), WatchFnHeapID>;
  using AddPoolFn = ZmFn<void(Pool *), WatchFnHeapID>;
  using DelPoolFn = ZmFn<void(Pool *), WatchFnHeapID>;
  using AddQueueFn = ZmFn<void(Queue *), WatchFnHeapID>;
  using DelQueueFn = ZmFn<void(Queue *), WatchFnHeapID>;

  static void add(Hub *);
  static void del(Hub *);
  static unsigned all(AllFn);
  static void capture(CaptureFn);
  template <typename L> static void guard(L &&l) {
    WatchGuard guard(watchLock_());
    ZuFwd<L>(l)();
  }
  static void watch(
    AddFn, DelFn,
    AddLinkFn, DelLinkFn,
    AddPoolFn, DelPoolFn,
    AddQueueFn, DelQueueFn);
  static void unwatch();

private:
  friend struct Hub;
  static void linkAdded_(Link *);
  static void linkDeleted_(Link *);
  static void poolAdded_(Pool *);
  static void poolDeleted_(Pool *);
};

} // Ztc

#endif /* ZtcHub_HH */
