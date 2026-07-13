//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZvEngine - connectivity framework

#ifndef ZvEngine_HH
#define ZvEngine_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuLambdaTraits.hh>

#include <zlib/ZmRWLock.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmPolymorph.hh>
#include <zlib/ZmEngine.hh>
#include <zlib/ZmTime.hh>

#include <zlib/ZtEnum.hh>

#include <zlib/ZvCf.hh>
#include <zlib/ZvIOQueue.hh>
#include <zlib/ZvMxParams.hh>
#include <zlib/ZvThreadParams.hh>

class ZvEngine;

namespace ZvEngineState {
  using namespace ZmEngineState;
  using T = int8_t;
  ZtEnumNames(ZvEngineState,
    Stopped, Starting, Running, Stopping, StartPending, StopPending);
}

ZtEnumNS(ZvLinkState, int8_t,
  Down,
  Disabled,
  Deleted,
  Connecting,
  Up,
  ReconnectPending,
  Reconnecting,
  Failed,
  Disconnecting,
  ConnectPending,
  DisconnectPending);

class ZvAPI ZvAnyTx : public ZmPolymorph {
protected:
  ZvAnyTx(ZuCSpan id);
  
  ZvAnyTx(const ZvAnyTx &) = delete;
  ZvAnyTx &operator =(const ZvAnyTx &) = delete;

  void init(ZvEngine *engine);

public:
  using Mx = ZiMultiplex;

  static ZuCSpan IDAxor(const ZvAnyTx *tx) { return tx->id(); }

  ZvEngine *engine() const { return m_engine; }
  Mx *mx() const { return m_mx; }
  ZuCSpan id() const { return m_id; }

  template <typename T = uintptr_t>
  T appData() const { return static_cast<T>(m_appData); }
  template <typename T>
  void appData(T v) { m_appData = static_cast<uintptr_t>(v); }

private:
  ZuID			m_id;
  ZvEngine		*m_engine = nullptr;
  Mx			*m_mx = nullptr;
  uintptr_t		m_appData = 0;
};

class ZvAPI ZvAnyTxPool : public ZvAnyTx {
  ZvAnyTxPool(const ZvAnyTxPool &) = delete;
  ZvAnyTxPool &operator =(const ZvAnyTxPool &) = delete;

protected:
  ZvAnyTxPool(ZuCSpan id) : ZvAnyTx(id) { }

public:
  virtual ZvIOQueue *txQueue() const = 0;
};

class ZvAPI ZvAnyLink : public ZvAnyTx {
  ZvAnyLink(const ZvAnyLink &) = delete;
  ZvAnyLink &operator =(const ZvAnyLink &) = delete;

friend ZvEngine;

  using StateLock = ZmPRWLock;
  using StateGuard = ZmGuard<StateLock>;
  using StateReadGuard = ZmReadGuard<StateLock>;

protected:
  ZvAnyLink(ZuCSpan id);

public:
  int state() const { return m_state; }
  unsigned reconnects() const { return m_reconnects.load_(); }

  struct Telemetry {
    ZuID	id;
    ZuID	engineID;
    uint64_t	rxSeqNo = 0;
    uint64_t	txSeqNo = 0;
    uint32_t	reconnects = 0;
    int8_t	state = 0;
  };
  void telemetry(Telemetry &data) const;

  // up, down return true if the link state changed
  bool up() { return up_(true); }
  bool down() { return down_(true); }

  virtual void update(const ZvCf *cf) = 0;
  virtual void reset(ZvSeqNo rxSeqNo, ZvSeqNo txSeqNo) = 0;

  ZvSeqNo rxSeqNo() const {
    if (const ZvIOQueue *queue = rxQueue()) return queue->head();
    return 0;
  }
  ZvSeqNo txSeqNo() const {
    if (const ZvIOQueue *queue = txQueue()) return queue->tail();
    return 0;
  }

  virtual ZvIOQueue *rxQueue() const = 0;
  virtual ZvIOQueue *txQueue() const = 0;

protected:
  virtual void connect() = 0;
  virtual void disconnect() = 0;

  void connected();
  void disconnected(bool);
  void reconnecting();	// transition direct from Up to Connecting
  void reconnect(bool immediate);

  virtual ZuTime reconnInterval(unsigned) { return ZuTime{1}; }

private:
  bool up_(bool enable);
  bool down_(bool disable);

  void reconnect_();

  void deleted_();	// called from ZvEngine::delLink

private:
  ZmScheduler::Timer	m_reconnTimer;

  StateLock		m_stateLock;
    ZmAtomic<int>	  m_state = ZvLinkState::Down;
    ZmAtomic<unsigned>	  m_reconnects;
    bool		  m_enabled = true;
};

// Callbacks to the application from the engine implementation
struct ZvAPI ZvEngineApp {
  virtual ZmRef<ZvAnyLink> createLink(ZuCSpan) = 0;
};

// Note: When event/flow steering, referenced objects must remain
// in scope until their queued member functions are eventually called;
// this is frequently required for transient objects with short
// lifespans such as messages. However it should not be necessary to
// frequently adjust the reference count of semi-persistent objects
// such as links and engines, since their lifespans are long and cycle
// with low frequency. Furthermore, atomic operations on reference
// counts invalidate cache lines and should be minimized.
//
// ... accordingly, when enqueuing work involving links/engines, 'this'
// can be directly captured as a raw pointer, without reference counting.
// Code that (infrequently) deletes links or engines needs to take extra
// care not to destroy them while they remain referenced by outstanding
// work; to assure this, teardown is performed as follows:
// 1] The link/engine is disabled (but not destroyed) to prevent further use
// 2] A temporary semaphore is initialized
// 3] A semaphore post function is enqueued onto each of the threads
//    that could potentially do work involving the link/engine being deleted;
//    each semaphore post will be executed after all the work ahead of it in
//    each thread-specific queue has been drained
// 4] The semaphore is posted as many times as there are threads/queues
// 5] The link/engine is destroyed safe in the knowledge that no
//    outstanding work involving it can remain enqueued or in-progress on
//    any of the threads, since they've already been drained of all work
//    that was outstanding when the object was disabled

// FIXME
// - move the ZtelServer containers into EngineMgr
// - permits findQueue() -> queue->owner -> link/txPool resolution
struct ZvEngineMgr {
  // Engine Management
  virtual void addEngine(ZvEngine *) { }
  virtual void delEngine(ZvEngine *) { }
  virtual void updEngine(ZvEngine *) { }

  // Link Management
  virtual void updLink(ZvAnyLink *) { }

  // Queue Management
  virtual void addQueue(ZvQueue *queue) { }
  virtual void delQueue(ZvQueueType::T type, ZuCSpan id) { }
};

class ZvAPI ZvEngine : public ZmPolymorph, public ZmEngine<ZvEngine> {
friend ZmEngine<ZvEngine>;
friend ZvAnyLink;

  using Lock = ZmPRWLock;
  using Guard = ZmGuard<Lock>;
  using ReadGuard = ZmReadGuard<Lock>;

  using StateLock = ZmPRWLock;
  using StateGuard = ZmGuard<StateLock>;
  using StateReadGuard = ZmReadGuard<StateLock>;

public:
  static ZuCSpan IDAxor(const ZvEngine *e) { return e->id(); }

  using Mx = ZiMultiplex;
  using Mgr = ZvEngineMgr;
  using App = ZvEngineApp;

  ZvEngine() { }

  ZvEngine(const ZvEngine &) = delete;
  ZvEngine &operator =(const ZvEngine &) = delete;

  bool init(Mgr *mgr, App *app, Mx *mx, const ZvCf *cf) {
    return ZmEngine<ZvEngine>::lock(
	ZmEngineState::Stopped, [this, mgr, app, mx, cf]() {
      m_mgr = mgr;
      m_app = app;
      m_id = cf->get("id", true);
      m_mx = mx;
      if (ZuCSpan s = cf->get("rxThread"))
	m_rxThread = mx->sid(s);
      else
	m_rxThread = mx->rxThread();
      if (ZuCSpan s = cf->get("txThread"))
	m_txThread = mx->sid(s);
      else
	m_txThread = mx->txThread();
      return true;
    });
  }
  bool final();

  Mgr *mgr() const { return m_mgr; }
  ZvEngineApp *app() const { return m_app; }
  ZuCSpan id() const { return m_id; }
  Mx *mx() const { return m_mx; }
  unsigned rxThread() const { return m_rxThread; }
  unsigned txThread() const { return m_txThread; }

  template <typename ...Args>
  void rxRun(Args &&...args) {
    m_mx->run(ZuFwd<Args>(args)..., m_rxThread);
  }
  template <typename ...Args>
  void rxPush(Args &&...args) {
    m_mx->push(ZuFwd<Args>(args)..., m_rxThread);
  }
  template <typename ...Args>
  void rxInvoke(Args &&...args) {
    m_mx->invoke(ZuFwd<Args>(args)..., m_rxThread);
  }
  template <typename ...Args>
  void txRun(Args &&...args) {
    m_mx->run(ZuFwd<Args>(args)..., m_txThread);
  }
  template <typename ...Args>
  void txInvoke(Args &&...args) {
    m_mx->invoke(ZuFwd<Args>(args)..., m_txThread);
  }

  void mgrAddEngine() { mgr()->addEngine(this); }
  void mgrDelEngine() { mgr()->delEngine(this); }

  ZmRef<ZvAnyLink> appCreateLink(ZuCSpan id) {
    return app()->createLink(id);
  }
  void mgrUpdLink(ZvAnyLink *link) { mgr()->updLink(link); }

  // Note: ZvIOQueues are contained in Link and TxPool
  void mgrAddQueue(ZmRef<ZvQueue> queue) { mgr()->addQueue(ZuMv(queue)); }
  void mgrDelQueue(unsigned type, ZuCSpan id) { mgr()->delQueue(type, id); }

  // generic O.S. error logging
  auto osError(const char *op, int result, ZeError e) {
    return [id = m_id, op, result, e](auto &s, const auto &) {
      s << id << " - " << op << " - " << Zi::ioResult(result) << " - " << e;
    };
  }

  struct Telemetry {
    ZuID	id;		// primary key
    ZuID	type;
    ZuID	mxID;
    uint16_t	down = 0;
    uint16_t	disabled = 0;
    uint16_t	transient = 0;
    uint16_t	up = 0;
    uint16_t	reconn = 0;
    uint16_t	failed = 0;
    uint16_t	nLinks = 0;
    uint16_t	rxThread = 0;
    uint16_t	txThread = 0;
    int8_t	state = -1;
  };

  void telemetry(Telemetry &data) const;

private:
  ZuDerive(TxPools,
    (ZmRBTree<ZmRef<ZvAnyTxPool>,
      ZmRBTreeKey<ZvAnyTxPool::IDAxor,
	ZmRBTreeUnique<true>>>));
  ZuDerive(Links,
    (ZmRBTree<ZmRef<ZvAnyLink>,
      ZmRBTreeKey<ZvAnyLink::IDAxor,
	ZmRBTreeUnique<true>>>));

public:
  ZmRef<ZvAnyTxPool> txPool(ZuCSpan id) {
    ReadGuard guard(m_lock);
    return m_txPools.findVal(id);
  }
  template <typename TxPool>
  ZmRef<ZvAnyTxPool> updateTxPool(ZuCSpan id, const ZvCf *cf) {
    Guard guard(m_lock);
    ZmRef<TxPool> pool;
    if (pool = m_txPools.findVal(id)) {
      guard.unlock();
      pool->update(cf);
      return pool;
    }
    pool = new TxPool(*this, id);
    m_txPools.add(pool);
    guard.unlock();
    pool->update(cf);
    mgrAddQueue(pool->txQueue());
    return pool;
  }
  ZmRef<ZvAnyTxPool> delTxPool(ZuCSpan id) {
    Guard guard(m_lock);
    ZmRef<ZvAnyTxPool> txPool;
    if (txPool = m_txPools.delVal(id)) {
      guard.unlock();
      mgrDelQueue(ZvQueueType::Tx, id);
    }
    return txPool;
  }

  ZmRef<ZvAnyLink> link(ZuCSpan id) {
    ReadGuard guard(m_lock);
    return m_links.findVal(id);
  }
  ZmRef<ZvAnyLink> updateLink(ZuCSpan id, const ZvCf *cf) {
    Guard guard(m_lock);
    ZmRef<ZvAnyLink> link;
    if (link = m_links.findVal(id)) {
      guard.unlock();
      link->update(cf);
      mgrUpdLink(link);
      return link;
    }
    link = appCreateLink(id);
    link->init(this);
    m_links.add(link);
    guard.unlock();
    linkState(link, -1, link->state());
    link->update(cf);
    mgrUpdLink(link);
    mgrAddQueue(link->rxQueue());
    mgrAddQueue(link->txQueue());
    return link;
  }
  ZmRef<ZvAnyLink> delLink(ZuCSpan id) {
    Guard guard(m_lock);
    ZmRef<ZvAnyLink> link;
    if (link = m_links.delVal(id)) {
      guard.unlock();
      mgrDelQueue(ZvQueueType::Rx, id);
      mgrDelQueue(ZvQueueType::Tx, id);
      link->deleted_();	// calls linkState(), mgrUpdLink()
    }
    return link;
  }
  unsigned nLinks() const {
    ReadGuard guard(m_lock);
    return m_links.count_();
  }
  template <typename Link, typename L>
  bool allLinks(L &&l) {
    ReadGuard guard(m_lock);
    auto i = m_links.citer();
    while (ZvAnyLink *link = i.val())
      if (!ZuFwd<L>(l)(static_cast<Link *>(link))) return false;
    return true;
  }

private:
  void linkState(ZvAnyLink *, int prev, int next);

  void start_();
  void stop_();
  void stateChanged() { mgr()->updEngine(this); }

private:
  ZuID				m_id;
  Mgr				*m_mgr = 0;
  App				*m_app = 0;
  Mx				*m_mx;
  unsigned			m_rxThread = 0;
  unsigned			m_txThread = 0;

  Lock				m_lock;
    TxPools			  m_txPools;	// from csv
    Links			  m_links;	// from csv

  StateLock			m_stateLock;
    unsigned			  m_down = 0;		// #links down
    unsigned			  m_disabled = 0;	// #links disabled
    unsigned			  m_transient = 0;	// #links transient
    unsigned			  m_up = 0;		// #links up
    unsigned			  m_reconn = 0;		// #links reconnecting
    unsigned			  m_failed = 0;		// #links failed
};

template <typename Impl, typename Base>
class ZvTx : public Base {
public:
  using Tx = ZvIOQueueTx_<Impl>;
  using Mx = ZiMultiplex;
  using Span = ZvIOQueue::Span;

  using Base::engine;

  ZvTx(ZuCSpan id) : Base{id} { }
  
  void init(ZvEngine *engine) { Base::init(engine); }

  auto tx() { return static_cast<Tx *>(this); }
  auto tx() const { return static_cast<const Tx *>(this); }

  template <typename L>
  void txRun(this auto &&self, L l) {
    self.engine()->txRun([self = ZmMkRef(&self), l = ZuMv(l)]() mutable {
      l(self->tx());
    });
  }
  template <typename L>
  void txInvoke(this auto &&self, L l) {
    self.engine()->txInvoke(&self, [self = &self, l = ZuMv(l)]() mutable {
      l(self->tx());
      return self;
    });
  }

  void scheduleSend() { txInvoke([](Tx *tx) { tx->send(); }); }
  void rescheduleSend() { txRun([](Tx *tx) { tx->send(); }); }
  void idleSend() { }

  void scheduleResend() { txInvoke([](Tx *tx) { tx->resend(); }); }
  void rescheduleResend() { txRun([](Tx *tx) { tx->resend(); }); }
  void idleResend() { }

  void scheduleArchive() { rescheduleArchive(); }
  void rescheduleArchive() { txRun([](Tx *tx) { tx->archive(); }); }
  void idleArchive() { }
};

template <typename Link_>
class ZvTxPool :
  public ZvTx<ZvTxPool<Link_>, ZvAnyTxPool>,
  public ZvIOQueueTxPool<ZvTxPool<Link_>, Link_> {
public:
  using Link = Link_;
  using Base = ZvTx<ZvTxPool, ZvAnyTxPool>;

  using Tx = ZvIOQueueTxPool<ZvTxPool, Link>;
  using Tx_ = typename Tx::Base::Base;

  using Base::engine;
  using Base::txRun;
  using Base::txInvoke;

  ZvTxPool(ZuCSpan id) : Base{id} { }

  ZvIOQueue *txQueue() const { return Tx::txQueue(); }

  auto tx() { return static_cast<Tx *>(this); }
  auto tx() const { return static_cast<const Tx *>(this); }

  void send(ZvIOMsg *msg) {			// used by protocol tx
    // permit callers to use code of the form send(mkMsg(...))
    // without needing to explicitly check mkMsg() success/failure
    if (ZuUnlikely(!msg)) return;
    // msg->owner(tx()); // already performed by ZvIOMsg ctor
    engine()->txInvoke(msg, [msg]() {
      msg->owner_as<Tx *>()->send(msg);
      return msg;
    });
  }
  template <typename L>
  void abort(ZvSeqNo seqNo, L l) {
    txInvoke([seqNo, l = ZuMv(l)](Tx *tx) mutable {
      ZuMv(l)(tx->abort(seqNo));
    });
  }

private:
  // prevent direct call - must be called via txRun/txInvoke
  using Tx_::start;		// Tx - start
  using Tx_::stop;		// Tx - stop
  // using Tx::send;		// Tx - send (from app)
  // using Tx::abort;		// Tx - abort (from app)
  using Tx::unload;		// Tx - unload all messages (for reload)
  using Tx::txReset;		// Tx - reset sequence numbers
  // should not be called from Impl at all
  using Tx_::ackd;		// handled by ZvIOQueueTxPool
  using Tx_::resend;		// handled by ZvTx
  using Tx_::archive;		// handled by ZvTx
  using Tx_::archived;		// handled by ZvIOQueueTxPool
  using Tx::ready;		// handled by ZvIOQueueTxPool
  using Tx::unready;		// handled by ZvIOQueueTxPool
  using Tx::ready_;		// internal to ZvIOQueueTxPool
  using Tx::unready_;		// internal to ZvIOQueueTxPool
};

// CRTP - implementation must conform to the following interface:
// (Note: can be derived from ZvTxPool above)
#if 0
struct Link : public ZvLink<Link> {
  ZuTime reconnInterval(unsigned reconnects); // optional - defaults to 1sec

  // Rx
  ZuTime reReqInterval(); // resend request interval
  void request(const ZvIOQueue::Span &prev, const ZvIOQueue::Span &now);
  void reRequest(const ZvIOQueue::Span &now);

  // Tx
  void loaded_(ZvIOMsg *msg);
  void unloaded_(ZvIOMsg *msg);

  bool send_(ZvIOMsg *msg, bool more); // true on success
  bool resend_(ZvIOMsg *msg, bool more); // true on success
  void aborted_(ZvIOMsg *msg);

  bool sendGap_(const ZvIOQueue::Span &gap, bool more); // true on success
  bool resendGap_(const ZvIOQueue::Span &gap, bool more); // true on success
};
#endif

template <typename Impl_, typename Pool_>
class ZvLink :
  public ZvTx<Impl_, ZvAnyLink>,
  public ZvIOQueueRx<Impl_>,
  public ZvIOQueueTx<Impl_, Pool_> {

public:
  using Impl = Impl_;
  using Pool = Pool_;
  using Base = ZvTx<Impl, ZvAnyLink>;

  using Rx = ZvIOQueueRx<Impl>;
  using Rx_ = typename Rx::Base;
  using Tx = ZvIOQueueTx<Impl, Pool>;
  using Tx_ = typename Tx::Base::Base;

  auto impl() { return static_cast<Impl *>(this); }
  auto impl() const { return static_cast<const Impl *>(this); }

  auto rx() { return static_cast<Rx *>(this); }
  auto rx() const { return static_cast<const Rx *>(this); }
  auto tx() { return static_cast<Tx *>(this); }
  auto tx() const { return static_cast<const Tx *>(this); }

  using Base::mx;
  using Base::engine;
  using Base::txRun;
  using Base::txInvoke;

  ZvLink(ZuCSpan id) : Base{id} { }

  void init(ZvEngine *engine) { Base::init(engine); }

  template <typename L>
  void rxRun(this auto &&self, L l) {
    self.engine()->rxRun([self = ZmMkRef(&self), l = ZuMv(l)]() mutable {
      l(self->rx());
    });
  }
  template <typename L>
  void rxInvoke(this auto &&self, L l) {
    self.engine()->rxInvoke(&self, [self = &self, l = ZuMv(l)]() mutable {
      l(self->rx());
      return self;
    });
  }

  void scheduleDequeue() { rescheduleDequeue(); }
  void rescheduleDequeue() { rxRun([](Rx *rx) { rx->dequeue(); }); }
  void idleDequeue() { }

  using RRLock = ZmPLock;
  using RRGuard = ZmGuard<RRLock>;

  void scheduleReRequest() {
    RRGuard guard(m_rrLock);
    if (ZuLikely(!m_rrTime)) scheduleReRequest_(guard);
  }
  void rescheduleReRequest() {
    RRGuard guard(m_rrLock);
    scheduleReRequest_(guard);
  }
  void scheduleReRequest_(RRGuard &guard) {
    ZuTime interval = impl()->reReqInterval();
    if (!interval) return;
    m_rrTime = Zm::now();
    ZuTime rrTime = (m_rrTime += interval);
    guard.unlock();
    mx()->add(&m_rrTimer, rrTime, ZmScheduler::Update,
	[this](auto &&arm) {
	  return arm(ZmFn<>{rx(), [](Rx *rx) { rx->reRequest(); }});
	}, engine()->rxThread());
  }
  void cancelReRequest() {
    mx()->del(&m_rrTimer);
    {
      RRGuard guard(m_rrLock);
      m_rrTime = ZuTime();
    }
  }

  ZvIOQueue *rxQueue() const { return Rx::rxQueue(); }
  ZvIOQueue *txQueue() const { return Tx::txQueue(); }

  using ZvAnyLink::rxSeqNo;
  using ZvAnyLink::txSeqNo;

  template <typename L, typename ...Args>
  void rxRun(L &&l, Args &&...args)
    { engine()->rxRun(ZmFn<>{rx(), ZuFwd<L>(l)}, ZuFwd<Args>(args)...); }
  template <typename L, typename ...Args>
  void rxRun(L &&l, Args &&...args) const
    { engine()->rxRun(ZmFn<>{rx(), ZuFwd<L>(l)}, ZuFwd<Args>(args)...); }
  template <typename L> void rxPush(L &&l)
    { engine()->rxPush(ZmFn<>{rx(), ZuFwd<L>(l)}); }
  template <typename L> void rxPush(L &&l) const
    { engine()->rxPush(ZmFn<>{rx(), ZuFwd<L>(l)}); }
  template <typename L> void rxInvoke(L &&l)
    { engine()->rxInvoke(rx(), ZuFwd<L>(l)); }
  template <typename L> void rxInvoke(L &&l) const
    { engine()->rxInvoke(rx(), ZuFwd<L>(l)); }

  void rcvd(ZvIOMsg *msg) {		// used by protocol rx
    // msg->owner(rx());
    engine()->rxInvoke(msg, [](ZvIOMsg *msg) {
      msg->owner_as<Rx *>()->rcvd(msg); // ZmPQRx
      return msg;
    });
  }

  void send(ZvIOMsg *msg) {		// used by protocol tx
    // permit callers to use code of the form send(mkMsg(...))
    // without needing to explicitly check mkMsg() success/failure
    if (ZuUnlikely(!msg)) return;
    // msg->owner(tx()); // already performed by ZvIOMsg ctor
    engine()->txInvoke(msg, [msg]() {
      msg->owner_as<Tx *>()->send(msg);
      return msg;
    });
  }
  template <typename L>
  void abort(ZvSeqNo seqNo, L l) {
    txInvoke([seqNo, l = ZuMv(l)](Tx *tx) mutable {
      l(tx->abort(seqNo));
    });
  }
  void archived(ZvSeqNo seqNo) {
    txInvoke([seqNo](Tx *tx) { tx->archived(seqNo); return tx; });
  }

private:
  // prevent direct call from Impl - must be called via rx/tx Run/Invoke
  using Rx_::rxReset;		// Rx - reset sequence numbers
  using Rx_::startQueuing;	// Rx - start queuing
  using Rx_::stopQueuing;	// Rx - stop queuing (start processing)
  // using Rx_::rcvd;		// Rx - rcvd (from network)
  using Tx_::start;		// Tx - start
  using Tx_::stop;		// Tx - stop
  using Tx_::ackd;		// Tx - ackd (due to received message)
  using Tx_::archived;		// Tx - archived (following call to archive_)
  // using Tx::send;		// Tx - send (from app)
  // using Tx::abort;		// Tx - abort (from app)
  using Tx::unload;		// Tx - unload all messages (for reload)
  using Tx::txReset;		// Tx - reset sequence numbers
  using Tx::join;		// Tx - join pool
  using Tx::leave;		// Tx - leave pool
  using Tx::ready;		// Tx - inform pool(s) of readiness
  using Tx::unready;		// Tx - inform pool(s) not ready
  // should not be called from Impl at all
  using Rx_::dequeue;		// handled by ZvLink
  using Rx_::reRequest;		// handled by ZvLink
  using Tx_::resend;		// handled by ZvTx
  using Tx_::archive;		// handled by ZvTx
  using Tx::ready_;		// internal to ZvIOQueueTx
  using Tx::unready_;		// internal to ZvIOQueueTx

  ZmScheduler::Timer	m_rrTimer;

  RRLock		m_rrLock;
    ZuTime		  m_rrTime;
};

#endif /* ZvEngine_HH */
