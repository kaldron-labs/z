//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// telemetry service application

#ifndef ZtcApp_HH
#define ZtcApp_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#include <zlib/ZuID.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuUnion.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZmTime.hh>

#include <zlib/ZfStruct.hh>

#include <zlib/ZiIP.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/Ztcp.hh>

#include <zlib/ZvMxParams.hh>

#include <zlib/ZtcAppTypes.hh>
#include <zlib/ZtcDB.hh>
#include <zlib/ZtcHash.hh>
#include <zlib/ZtcHeap.hh>
#include <zlib/ZtcHub.hh>
#include <zlib/ZtcMx.hh>
#include <zlib/ZtcThread.hh>
#include <zlib/ztc_msg_fbs.h>

namespace Ztc {

namespace Filter_ {
  class Filter;
}
namespace App_ {
  class AlertEvent;
  class Client_;
  class Ingress;
  class IngressData;
  class Pending_;
  class State;
  class Subscription_;
}

struct AppCf {
  enum {
    DefltAccepts = 8,
    DefltMaxFrame = 1U<<20,
    DefltMaxFilter = 1024,
    DefltMinInterval = 100,
    DefltMaxInterval = 3600000,
    DefltMaxPending = 64,
    DefltMaxSubs = 64,
    DefltMaxAlertMsg = 65536,
    DefltAlertTail = 4096,
    DefltAlertReplay = 1024,
    DefltAlertRetention = 7
  };

  ZuID		id{"ztc"};
  ZuID		version{"10.0.0"};
  ZuID		role{"server"};
  ZiIP		ip{"127.0.0.1"};
  uint16_t	port = 0;
  unsigned	nAccepts = DefltAccepts;
  unsigned	maxFrame = DefltMaxFrame;
  unsigned	maxFilter = DefltMaxFilter;
  unsigned	minInterval = DefltMinInterval;
  unsigned	maxInterval = DefltMaxInterval;
  unsigned	maxPending = DefltMaxPending;
  unsigned	maxSubs = DefltMaxSubs;
  unsigned	maxAlertMsg = DefltMaxAlertMsg;
  unsigned	alertTail = DefltAlertTail;
  unsigned	alertReplay = DefltAlertReplay;
  unsigned	alertRetention = DefltAlertRetention;
  Zi::Path	alertPrefix{"alerts"};

  ZvMxCf	mx{
    .nThreads = 4,
    .rxThread = "rx",
    .txThread = "tx"
  };
  ZtString<>	timerRole{"timer"};
  ZtString<>	workerRole{"worker"};
  unsigned	timerThread = 1;
  unsigned	rxThread = 2;
  unsigned	txThread = 3;
  unsigned	workerThread = 4;
  unsigned	rebindFreq = 0;
};

ZfStruct(AppCf,
  (((id)),					(String)),
  (((version)),					(String)),
  (((role)),					(String)),
  (((ip)),					(String)),
  (((port)),					(UInt16)),
  (((nAccepts),		((Range<1U, 1024U>))),	(UInt32, 8)),
  (((maxFrame),		((Range<64U, 1U<<30U>))),	(UInt32, 1U<<20U)),
  (((maxFilter),	((Range<1U, 1U<<20U>))),	(UInt32, 1024)),
  (((minInterval),	((Range<1U, 3600000U>))),	(UInt32, 100)),
  (((maxInterval),	((Range<1U, 3600000U>))),	(UInt32, 3600000)),
  (((maxPending),	((Range<1U, 65536U>))),	(UInt32, 64)),
  (((maxSubs),		((Range<1U, 65536U>))),	(UInt32, 64)),
  (((maxAlertMsg),	((Range<1U, 1U<<20U>))),	(UInt32, 65536)),
  (((alertTail),	((Range<1U, 1U<<20U>))),	(UInt32, 4096)),
  (((alertReplay),	((Range<1U, 1U<<20U>))),	(UInt32, 1024)),
  (((alertRetention),	((Range<1U, 3660U>))),	(UInt32, 7)),
  (((alertPrefix)),				(String, "alerts")),
  (((mx)),					(UDT)),
  (((timerRole)),				(String, "timer")),
  (((workerRole)),				(String, "worker")),
  (((timerThread),	((Range<1U, 1024U>))),	(UInt32, 1)),
  (((rxThread),		((Range<1U, 1024U>))),	(UInt32, 2)),
  (((txThread),		((Range<1U, 1024U>))),	(UInt32, 3)),
  (((workerThread),	((Range<1U, 1024U>))),	(UInt32, 4)),
  (((rebindFreq),	((Range<0U, 3600U>))),	(UInt32)));

class ZvAPI App : public Ztcp::Server<App> {
  using Base = Ztcp::Server<App>;

public:
  using CtrlFn =
    ZmFn<void(bool), ZmFnHeapID<"Ztc.App.CtrlFn">>;
  using RagFn =
    ZmFn<void(RAG::T), ZmFnHeapID<"Ztc.App.RagFn">>;

  class Link;

friend App_::Ingress;
friend App_::IngressData;

  App() = default;
  ~App();

  App(const App &) = delete;
  App &operator =(const App &) = delete;

  bool init(const AppCf &);
  void start(CtrlFn);
  bool start();
  void stop(CtrlFn);
  bool stop();
  void final();

  ZiIP localIP() const { return m_cf.ip; }
  uint16_t localPort() const {
    uint16_t port = m_boundPort.load_();
    return port ? port : m_cf.port;
  }
  unsigned nAccepts() const { return m_cf.nAccepts; }
  unsigned rebindFreq() const { return m_cf.rebindFreq; }

  void listening(const ZiListenInfo &);
  void listenFailed(bool);
  ZiConnection *accepted(const ZiCxnInfo &);

  void rag(RAG::T);
  void rag(RagFn) const;
  ZmRef<ZiSink> alertSink() const;

private:
  using CtrlLock = ZmPLock;
  using CtrlGuard = ZmGuard<CtrlLock>;

  template <typename Key, typename Value, ZuString HeapID>
  using Index = ZmRBTreeKV<Key, Value *,
    ZmRBTreeUnique<true,
      ZmRBTreeLock<ZmNoLock,
	ZmRBTreeHeapID<HeapID>>>>;

  using CxnKey =
    ZuTuple<ZuID, ZiIP, uint16_t, ZiIP, uint16_t>;
  using QueueKey = ZuTuple<ZuID, ZuID, QueueType::T>;
  using HubKey = ZuTuple<LinkType::T, ZuID>;
  using ChildKey = ZuTuple<ZuID, ZuID>;

  using MxIdx = Index<ZuID, Ztc::Mx, "Ztc.App.MxIdx">;
  using CxnIdx = Index<CxnKey, Ztc::Connection, "Ztc.App.CxnIdx">;
  using MxQueueIdx =
    Index<QueueKey, Ztc::Mx, "Ztc.App.MxQueueIdx">;
  using HubIdx = Index<HubKey, Ztc::Hub, "Ztc.App.HubIdx">;
  using LinkIdx = Index<ChildKey, Ztc::Link, "Ztc.App.LinkIdx">;
  using PoolIdx = Index<ChildKey, Ztc::Pool, "Ztc.App.PoolIdx">;
  using LinkQueueIdx =
    Index<QueueKey, Ztc::Link, "Ztc.App.LinkQueueIdx">;
  using PoolQueueIdx =
    Index<QueueKey, Ztc::Pool, "Ztc.App.PoolQueueIdx">;
  using DBIdx = Index<ZuID, DB, "Ztc.App.DBIdx">;
  using DBHostIdx = Index<ChildKey, DBHost, "Ztc.App.DBHostIdx">;
  using DBTableKey = ZuTuple<ZuID, Ztc::DBTableID>;
  using DBTableIdx =
    Index<DBTableKey, DBTable, "Ztc.App.DBTableIdx">;

  ZiMultiplex *serviceMx_() {
    return m_mx.is<ZiMultiplex>() ? &m_mx.p<ZiMultiplex>() : nullptr;
  }
  const ZiMultiplex *serviceMx_() const {
    return m_mx.is<ZiMultiplex>() ? &m_mx.p<ZiMultiplex>() : nullptr;
  }

  void request_(ZmRef<Link>, ZmRef<ZiIOBuf>);
  void startDone_(bool);
  void invalid_(ZmRef<Link>);
  void disconnected_(uint64_t);
  void timerFired_(uint64_t, ZuTime);
  void rag_(RAG::T);
  void alert_(ZmRef<App_::AlertEvent>);
  App_::Client_ *client_(ZmRef<Link>);
  void clientRelease_(App_::Client_ *);
  bool snapshot_(
    ZmRef<Link>, uint64_t, fbs::Group, const Filter_::Filter &,
    App_::Subscription_ * = nullptr);
  void snapshotBatch_(uint64_t, uint64_t);
  void snapshotDone_(App_::Pending_ *);
  void snapshotCancel_(uint64_t);
  void subscribe_(
    ZmRef<Link>, uint64_t, fbs::Group, Filter_::Filter, uint32_t,
    uint32_t = 0, uint64_t = 0);
  void unsubscribe_(
    Link *, uint64_t, fbs::Group, const Filter_::Filter &);
  void runSubscription_(App_::Subscription_ *);
  void replay_(App_::Subscription_ *);
  void replayBatch_(uint64_t);
  void finishReplay_(App_::Subscription_ *);
  void failReplay_(App_::Subscription_ *, ZuCSpan, bool = false);
  void armTimer_();
  void clearSubscriptions_();
  void sendAck_(Link *, uint64_t, fbs::AckStatus, uint32_t = 0);
  void sendError_(Link *, int32_t, ZuCSpan);
  void sendComplete_(Link *, uint64_t, uint32_t);

  void warmIndices_();
  void clearIndices_();
  void watch_();
  void unwatch_();

  void mxAdded_(Ztc::Mx *);
  void mxDeleted_(Ztc::Mx *);
  void cxnAdded_(Ztc::Connection *);
  void cxnDeleted_(Ztc::Connection *);
  void mxQueueAdded_(Ztc::Queue *);
  void mxQueueDeleted_(Ztc::Queue *);
  void hubAdded_(Ztc::Hub *);
  void hubDeleted_(Ztc::Hub *);
  void idxLinkAdded_(Ztc::Link *);
  void idxLinkDeleted_(Ztc::Link *);
  void poolAdded_(Ztc::Pool *);
  void poolDeleted_(Ztc::Pool *);
  void hubQueueAdded_(Ztc::Queue *);
  void hubQueueDeleted_(Ztc::Queue *);
  void dbAdded_(DB *);
  void dbDeleted_(DB *);
  void dbHostAdded_(DBHost *);
  void dbHostDeleted_(DBHost *);
  void dbTableAdded_(DBTable *);
  void dbTableDeleted_(DBTable *);

private:
  AppCf				m_cf;
  App_::State			*m_state = nullptr;
  MxIdx				m_mxIdx;
  CxnIdx			m_cxnIdx;
  MxQueueIdx			m_mxQueueIdx;
  HubIdx			m_hubIdx;
  LinkIdx			m_linkIdx;
  PoolIdx			m_poolIdx;
  LinkQueueIdx			m_linkQueueIdx;
  PoolQueueIdx			m_poolQueueIdx;
  DBIdx				m_dbIdx;
  DBHostIdx			m_dbHostIdx;
  DBTableIdx			m_dbTableIdx;
  ZuUnion<void, ZiMultiplex>	m_mx;
  int64_t			m_startTime = 0;
  ZmAtomic<unsigned>		m_boundPort = 0;
  ZmAtomic<unsigned>		m_rag = RAG::Off;
  ZmAtomic<uint64_t>		m_linkGeneration = 0;
  CtrlLock			m_ctrlLock;
  CtrlFn			m_startFn;
  bool				m_startPending = false;
  bool				m_initialized = false;
  bool				m_watching = false;
  bool				m_serverInitialized = false;
  ZmAtomic<unsigned>		m_running = 0;
};

} // Ztc

#endif /* ZtcApp_HH */
