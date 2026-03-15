//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// generic I/O queue based on ZmPQueue skip lists, used by ZvEngine
//
// Key / SeqNo - uint64
// Link ID - ZuID (union of 8-byte string with uint64)

#ifndef ZvIOQueue_HH
#define ZvIOQueue_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuRef.hh>
#include <zlib/ZuID.hh>

#include <zlib/ZmPQueue.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZmAtomic.hh>

#include <zlib/ZiAssert.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiIP.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/Zfb.hh>
#include <zlib/ZfbStruct.hh>

#include <zlib/ZvQueue.hh>

// #include <zlib/zv_msg_id_fbs.h>

// specific protocols should:
// - declare a message type that is derived from ZvIOMsg
// - use ZvIOMsg-derived messages with send()
// - return ZvIOMsg-derived objects from process_()
// - E.g. for REST ZvIOMsg is extended to include ZtStruct payload data and
//   virtually-dispatched compile-time functions to:
//   - build the HTTP buffer from the payload for sending
//   - parse a HTTP buffer into a payload for receiving

struct ZvIOQueueRxTx { }; // base class for both ZvIOQueueRx and ZvIOQueueTx_

using ZvSeqNo = uint64_t;
using ZvAtomicSeqNo = ZmAtomic<uint64_t>;

struct ZvIOMsg_ : public ZmPolymorph {
  // flags
  static constexpr uint32_t Skip = uint32_t(1)<<31;	// intentional skip
  static constexpr uint32_t Mask = uint32_t(1)<<31;	// flags mask

  ZvIOQueueRxTx		*owner = nullptr;
  ZvSeqNo		seqNo;
  uint32_t		length_flags = 0;

  ZvIOMsg_(ZvIOQueueRxTx *owner_) :			// used by protocol tx
    owner{owner_} { }

  ZvIOMsg_(ZvIOQueueRxTx *owner_, ZvSeqNo seqNo_) :	// used by protocol rx
    owner{owner_}, seqNo{seqNo_} { }

  template <typename T>
  T *owner_as() const { return static_cast<T *>(owner); }

  uint32_t length() const { return length_flags & ~Mask; }
  void length(uint32_t n) {
    ZiAssert(n <= ~Mask, "ZvIOQ",
      n, "length(" << n << ") max exceeded", return);
    length_flags = (length_flags & Mask) | (n & ~Mask);
  }
  bool skip() const { return length_flags & Skip; }
  void skip(bool _) {
    length_flags = _ ? (length_flags | Skip) : (length_flags & ~Skip);
  }
};

class ZvIOQFn {
public:
  using Key = ZvSeqNo;
  static Key KeyAxor(const ZvIOMsg_ &msg) { return msg.seqNo; }

  ZvIOQFn(const ZvIOMsg_ &msg) : m_msg{msg} { }

  Key key() const { return KeyAxor(m_msg); }
  unsigned length() const { return m_msg.length(); }
  unsigned clipHead(unsigned) { return length(); }
  unsigned clipTail(unsigned) { return length(); }
  void write(const ZvIOQFn &) { }

private:
  const ZvIOMsg_	&m_msg;
};

using ZvIOQueue_ = 
  ZmPQueue<ZvIOMsg_,
    ZmPQueueNode<ZvIOMsg_,
      ZmPQueueFn<ZvIOQFn,
	ZmPQueueHeapID<"ZvIOQueue">>>>;

struct ZvIOQueue : public ZvQueue, public ZvIOQueue_ {
  ZvIOQueue(ZvIOQueueRxTx *owner, ZvQueueType::T type, ZuID id, ZvSeqNo seqNo) :
    ZvIOQueue_(seqNo), m_owner(owner), m_type(type), m_id(id) { }

  template <typename T>
  T *owner_as() const { return static_cast<T *>(m_owner); }

  // ZvQueue virtual API
  ZvQueueType::T type() const { return m_type; }
  ZuCSpan id() const { return m_id; }
  void telemetry(ZvQueueTelemetry &data) const {
    data.id = m_id.span();
    data.seqNo = head();
    data.count = count_();
    stats(data.inCount, data.inElems, data.outCount, data.outElems);
    data.size = data.full = 0;
    data.type = m_type;
  }

private:
  ZvIOQueueRxTx		*m_owner = nullptr;
  ZvQueueType::T	m_type;
  ZuID			m_id;
};

using ZvIOMsg = ZvIOQueue::Node;
using ZvIOQGap = ZvIOQueue::Gap;

// ZvIOQueueRx - receive queue

// CRTP - application must conform to the following interface:
#if 0
struct Impl : public ZvIOQueueRx<Impl> {
  // allocate buffer for recv
  ZmRef<ZiIOBuf> alloc_();

  // rx process msg
  void process(ZvIOMsg *);	// ZmPQRx

  void scheduleDequeue();	// ZmPQRx - optional
  void rescheduleDequeue();	// ''
  void idleDequeue();		// ''

  void scheduleReRequest();	// ''
  void rescheduleReRequest();	// ''
  void cancelReRequest();	// ''

  void request(const ZvIOQGap &prev, const ZvIOQGap &now);
  void reRequest(const ZvIOQGap &now);
};
#endif

template <typename Impl_, typename Lock_ = ZmNoLock>
class ZvIOQueueRx :
  public ZvIOQueueRxTx,
  public ZmPQRx<Impl_, ZvIOQueue, Lock_> {
public:
  using Impl = Impl_;
  using Lock = Lock_;
  using Base = ZmPQRx<Impl, ZvIOQueue, Lock>;

  using Guard = ZmGuard<Lock>;

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  ZvIOQueueRx() :
    m_queue(new ZvIOQueue(this, ZvQueueType::Rx, impl()->id(), ZvSeqNo{})) { }
  ZvIOQueueRx(ZvSeqNo head) :
    m_queue(new ZvIOQueue(this, ZvQueueType::Rx, impl()->id(), head)) { }
  
  // ZvIOQueueRxTx virtual
  ZvIOQueue *queue() const { return m_queue; }
  // ZmPQRx CRTP
  ZvIOQueue *rxQueue() const { return m_queue; }

  void rxInit(ZvSeqNo seqNo) {
    if (seqNo > m_queue->head()) m_queue->head(seqNo);
  }

private:
  ZmRef<ZvIOQueue>	m_queue;
};

// ZvIOQueueTx - transmit queue
// ZvIOQueueTxPool - transmit fan-out queue

#define ZvIOQueueMaxPools 8	// max #pools a tx queue can be a member of

template <typename Impl, typename Tx, typename Lock> class ZvIOQueueTxPool;

// CRTP - application must conform to the following interface:
#if 0
struct Impl : public ZvIOQueueTx<Impl, Pool> {
  void archive_(ZvIOMsg *);			// tx archive (persistent)
  ZmRef<ZvIOMsg> retrieve_(ZvSeqNo, ZvSeqNo);	// tx retrieve

  void scheduleSend();
  void rescheduleSend();
  void idleSend();

  void scheduleResend();
  void rescheduleResend();
  void idleResend();

  void scheduleArchive();
  void rescheduleArchive();
  void idleArchive();

  void loaded_(ZvIOMsg *msg);		// may adjust readiness
  void unloaded_(ZvIOMsg *msg);		// ''

  // send_() must perform one of
  // 1] usual case (successful send): persist seqNo, return true
  // 2] stale message: abort message, return true
  // 3] transient failure (throttling, I/O, etc.): return false
  bool send_(ZvIOMsg *msg, bool more);
  // resend_() must perform one of
  // 1] usual case (successful resend): return true
  // 2] transient failure (throttling, I/O, etc.): return false
  bool resend_(ZvIOMsg *msg, bool more);

  // sendGap_() and resendGap_() return true, or false on transient failure
  bool sendGap_(const ZvIOQGap &gap, bool more);
  bool resendGap_(const ZvIOQGap &gap, bool more);
};
#endif

template <typename Impl_, typename Lock_ = ZmNoLock>
class ZvIOQueueTx_ :
  public ZvIOQueueRxTx,
  public ZmPQTx<Impl_, ZvIOQueue, Lock_> {
public:
  using Impl = Impl_;
  using Lock = Lock_;
  using Base = ZmPQTx<Impl, ZvIOQueue, Lock>;

  auto impl() { return static_cast<Impl *>(this); }
  auto impl() const { return static_cast<const Impl *>(this); }

  ZvIOQueueTx_() :
    m_queue(new ZvIOQueue(this, ZvQueueType::Tx, impl()->id(), ZvSeqNo{})) { }
  ZvIOQueueTx_(ZvSeqNo head) :
    m_queue(new ZvIOQueue(this, ZvQueueType::Tx, impl()->id(), head)) { }

protected:
  using Guard = ZmGuard<Lock>;

  const Lock &lock() const { return m_lock; }
  Lock &lock() { return m_lock; }

public:
  const ZvSeqNo txSeqNo() const { return m_seqNo; }

  // ZvIOQueueRxTx virtual
  ZvIOQueue *queue() const { return m_queue; }
  // ZmPQTx CRTP
  ZvIOQueue *txQueue() const { return m_queue; }

  void txInit(ZvSeqNo seqNo) {
    if (seqNo > m_seqNo) m_queue->head(m_seqNo = seqNo);
  }

  void send() { Base::send(); }
  void send(ZmRef<ZvIOMsg> msg) {
    if (ZuUnlikely(msg->skip())) {
      Guard guard(m_lock);
      if (ZuUnlikely(!m_ready)) {
	guard.unlock();
	impl()->aborted_(ZuMv(msg));
	return;
      }
    }
    msg->seqNo = m_seqNo++;
    impl()->loaded_(msg);
    Base::send(ZuMv(msg));
  }
  bool abort(ZvSeqNo seqNo) {
    ZmRef<ZvIOMsg> msg = Base::abort(seqNo);
    if (msg) {
      impl()->aborted_(msg);
      impl()->unloaded_(msg);
      msg->seqNo = {};
      return true;
    }
    return false;
  }

  // unload all messages from queue
  void unload(ZmFn<void(ZvIOMsg *)> fn) {
    while (ZmRef<ZvIOMsg> msg = m_queue->shift()) {
      impl()->unloaded_(msg);
      msg->seqNo = {};
      fn(msg);
    }
  }

  void ackd(ZvSeqNo seqNo) {
    if (m_seqNo < seqNo) m_seqNo = seqNo;
    Base::ackd(seqNo);
  }

  void txReset(ZvSeqNo seqNo = ZvSeqNo{}) {
    Base::txReset(m_seqNo = seqNo);
  }

  void ready() { // ready to send immediately
    Guard guard(m_lock);
    impl()->ready_(ZuTime(0, 1));
  }
  void ready(ZuTime next) { // ready to send at time next
    Guard guard(m_lock);
    impl()->ready_(next);
  }
  void unready() { // not ready to send
    Guard guard(m_lock);
    impl()->unready_();
  }

  const ZuTime &ready_() const { return m_ready; }
  void ready_(ZuTime next) { m_ready = next; }
  void unready_() { m_ready = ZuTime(); }

private:
  ZvSeqNo		m_seqNo;
  ZmRef<ZvIOQueue>	m_queue;

  Lock			m_lock;
    ZuTime		  m_ready;
};

template <typename Impl_, typename Pool_, typename Lock_ = ZmNoLock>
class ZvIOQueueTx : public ZvIOQueueTx_<Impl_, Lock_> {
public:
  using Impl = Impl_;
  using Pool = Pool_;
  using Lock = Lock_;
  using Base = ZvIOQueueTx_<Impl, Lock>;
  using Guard = Base::Guard;

  using Base::impl;
  using Base::lock;
  using Base::ready_;
  using Base::unready_;

  using Pools = ZuArray<Pool *, ZvIOQueueMaxPools>;

  ZvIOQueueTx() = default;

  // fails silently if ZvIOQueueMaxPools exceeded
  void join(Pool *pool) {
    Guard guard(lock());
    m_pools.push(pool);
  }
  void leave(Pool *pool) {
    Guard guard(lock());
    unsigned i, n = m_pools.length();
    for (i = 0; i < n; i++)
      if (m_pools[i] == pool) {
	m_pools.splice(i, 1);
	return;
      }
  }

protected:
  void ready_(ZuTime next);
  void unready_();

private:
  // guarded by ZmIOQueueTx_::lock()
  Pools			m_pools;
  unsigned		m_poolOffset = 0;
};

// CRTP - application must conform to the following interface:
#if 0
struct Impl : public ZvIOQueueTxPool<Impl, Tx> {
  // Note: below member functions have same signature as above
  void scheduleSend();
  void rescheduleSend();
  void idleSend();

  void scheduleResend();
  void rescheduleResend();
  void idleResend();

  void scheduleArchive();
  void rescheduleArchive();
  void idleArchive();

  void aborted_(ZvIOMsg *msg);

  void loaded_(ZvIOMsg *msg);		// may adjust readiness
  void unloaded_(ZvIOMsg *msg);		// ''
};
#endif

template <typename Impl_, typename Tx_, typename Lock_ = ZmNoLock>
class ZvIOQueueTxPool : public ZvIOQueueTx_<Impl_, Lock_> {
public:
  using Impl = Impl_;
  using Tx = Tx_;
  using Lock = Lock_;

  using Base = ZvIOQueueTx_<Impl, Lock>;
  using Guard = Base::Guard;

  using Base::impl;
  using Base::lock;
  using Base::ackd;
  using Base::archived;
  using Base::start;
  using Base::ready_;
  using Base::unready_;

  using Gap = ZvIOQGap;
  ZuDerive(Pool,
    (ZmRBTreeKV<ZuTime, ZmRef<Tx>,
      ZmRBTreeHeapID<"ZvIOQueueTxPool.Pool">>));

public:
  void loaded_(ZvIOMsg *) { }   // may be overridden by Impl
  void unloaded_(ZvIOMsg *) { } // ''

  bool send_(ZvIOMsg *msg, bool more) {
    if (ZmRef<Tx> next = next_()) {
      next->send(msg);
      sent_(msg);
      return true;
    }
    return false;
  }
  bool resend_(ZvIOMsg *, bool) { return true; } // unused
  void aborted_(ZvIOMsg *) { } // unused

  bool sendGap_(const Gap &, bool) { return true; } // unused
  bool resendGap_(const Gap &, bool) { return true; } // unused

  void sent_(ZvIOMsg *msg) {
    // this is a synthetic ack that calls ZmPQTx::ackd()
    // via ZvIOQueueTx_::ackd(), which in turn causes
    // archive/archive_/archived (see below), which calls
    // ZmPQueue::shift() to remove the msg from the queue
    ackd(msg->seqNo + 1);
  }
  void archive_(ZvIOMsg *msg) { archived(msg->seqNo + 1); }
  ZmRef<ZvIOMsg> retrieve_(ZvSeqNo, ZvSeqNo) { return nullptr; } // unused

  ZmRef<Tx> next_() {
    Guard guard(this->lock());
    return m_pool.minimumVal();
  }

  void ready_(Tx *tx, ZuTime prev, ZuTime next) {
    Guard guard(this->lock());
    typename Pool::Node *node = 0;
    if (!prev || !(node = m_pool.del(prev, tx))) {
      if (node) delete node;
      m_pool.add(next, tx);
      if (m_pool.count() == 1) {
	Base::ready_(next);
	guard.unlock();
	start();
	return;
      }
    } else {
      node->key() = next;
      m_pool.add(node);
    }
    Base::ready_(m_pool.minimumKey());
  }

  void unready_(Tx *tx, ZuTime prev) {
    Guard guard(this->lock());
    typename Pool::Node *node = 0;
    if (!prev || !(node = m_pool.del(prev, tx))) return;
    delete node;
    if (!m_pool.count()) Base::unready_();
  }

private:
  // guarded by ZmIOQueueTx_::lock()
  Pool	m_pool;
};

template <typename Impl, typename Pool, typename Lock>
void ZvIOQueueTx<Impl, Pool, Lock>::ready_(ZuTime next)
{
  unsigned n = m_pools.length();
  if (++m_poolOffset >= n) m_poolOffset = 0;
  for (unsigned i = 0 ; i < n; i++) {
    auto j = i + m_poolOffset; if (j > n) j -= n;
    m_pools[j]->ready_(impl(), ready_(), next);
  }
  Base::ready_(next);
}

template <typename Impl, typename Pool, typename Lock>
void ZvIOQueueTx<Impl, Pool, Lock>::unready_()
{
  unsigned n = m_pools.length();
  if (++m_poolOffset >= n) m_poolOffset = 0;
  for (unsigned i = 0 ; i < n; i++) {
    auto j = i + m_poolOffset; if (j > n) j -= n;
    m_pools[j]->unready_(impl(), ready_());
  }
  Base::unready_();
}

#endif /* ZvIOQueue_HH */
