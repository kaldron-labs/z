//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC private implementation helpers

#ifndef Zquic_HH
#error "include zlib/Zquic.hh before this header"
#endif

#include <zlib/ZiIOContext.hh>
#include <zlib/ZiTx.hh>

namespace Zquic_ {

template <typename Link_>
struct Cxn {
  CxnID			id;
  uint64_t		sequence = 0;
  Link_			*link = nullptr;
  ResetToken		resetToken;
  CxnState::T		state = CxnState::Active;

  Cxn() = default;
  Cxn(
    const CxnID &id_, uint64_t sequence_, Link_ *link_,
    const ResetToken &resetToken_, CxnState::T state_) :
      id{id_},
      sequence{sequence_},
      link{link_},
      resetToken{resetToken_},
      state{state_} { }
};

template <typename Link_>
inline const CxnID &Cxn_IDAxor(const Cxn<Link_> &cxn) { return cxn.id; }

ZmHashDeriveT((Link_), CxnRoutes_, Cxn<Link_>,
  (ZmHashNode<Cxn<Link_>,
    ZmHashKey<Cxn_IDAxor<Link_>,
	ZmHashHeapID<"Zquic.Endpoint.CxnRouter">>>));

template <typename Link_>
class CxnRouter {
public:
  using Route = Cxn<Link_>;
  using Routes = CxnRoutes_<Link_>;
  enum { TombstoneMax = 64 };
  using Tombstones = Queue<ZmQueue<CxnID,
    ZmQueueHeapID<"Zquic.Endpoint.CxnRouter.Tombstones">>>;

  CxnRouter() { init(); }

  void init() {
    if (!m_routes)
      m_routes = new Routes{ZmHashParams().bits(5).loadFactor(1).cBits(3)
	.init(typename Routes::ID{}())};
  }
  void initQueue(Ztc::Hub *hub) {
    m_tombstones.initQueue(hub, "quic:router", "tombstones",
	Ztc::QueueType::Rx, TombstoneMax);
  }

  unsigned allQueues(Ztc::QueueMgr::AllFn fn) const {
    fn(const_cast<decltype(m_tombstones) *>(&m_tombstones));
    return 1;
  }

  bool add(const CxnID &id, uint64_t sequence, Link_ *link) {
    return add(id, sequence, link, {});
  }
  bool add(
    const CxnID &id, uint64_t sequence, Link_ *link,
    const ResetToken &resetToken) {
    if (!id || !link) return false;
    if (auto route = m_routes->find(id)) {
      if (route->state == CxnState::Tombstone) return false;
      if (route->state != CxnState::Active) addLength(id.length());
      route->sequence = sequence;
      route->link = link;
      route->resetToken = resetToken;
      route->state = CxnState::Active;
      return true;
    }
    m_routes->add(Route{id, sequence, link, resetToken, CxnState::Active});
    addLength(id.length());
    return true;
  }

  Link_ *find(const CxnID &id) const {
    auto route = m_routes->find(id);
    if (!route || route->state != CxnState::Active) return nullptr;
    return route->link;
  }
  Link_ *matchShort(
      ZuBSpan packet, CxnID *id = nullptr,
      ResetToken *resetToken = nullptr) const {
    if (resetToken) *resetToken = {};
    const Route *route = matchShortRoute(packet, !resetToken);
    if (!route) return nullptr;
    if (resetToken && route->resetToken.valid())
      *resetToken = route->resetToken;
    if (route->state != CxnState::Active) return nullptr;
    if (id) *id = route->id;
    return route->link;
  }
  bool resetToken(const CxnID &id, ResetToken &token) const {
    auto route = m_routes->find(id);
    if (!route || route->state != CxnState::Active ||
	!route->resetToken.valid())
      return false;
    token = route->resetToken;
    return true;
  }
  bool resetTokenForShort(ZuBSpan packet, ResetToken &token) const {
    const Route *route = matchShortRoute(packet, false);
    if (!route || !route->resetToken.valid()) return false;
    token = route->resetToken;
    return true;
  }

  bool retire(const CxnID &id) {
    auto route = m_routes->find(id);
    if (!route || route->state != CxnState::Active) return false;
    delLength(id.length());
    m_routes->del(id);
    return true;
  }

  bool tombstone(const CxnID &id) {
    if (!id) return false;
    if (auto route = m_routes->find(id)) {
      if (route->state == CxnState::Tombstone) return true;
      if (route->state == CxnState::Active) delLength(id.length());
      route->state = CxnState::Tombstone;
      route->link = nullptr;
      route->resetToken = {};
      m_tombstones.push(id);
      trimTombstones();
      return true;
    }
    m_routes->add(Route{id, 0, nullptr, {}, CxnState::Tombstone});
    m_tombstones.push(id);
    trimTombstones();
    return true;
  }

  CxnState::T state(const CxnID &id) const {
    auto route = m_routes->find(id);
    return route ? route->state : CxnState::Tombstone;
  }

  void clear() {
    if (!m_routes) return;
    m_routes->clean();
    m_tombstones.clean();
    m_lengthMask = 0;
    m_lengthCount = fixedArray<unsigned, CxnIDMax + 1>();
  }
  void final() {
    clear();
    m_routes = nullptr;
  }

  unsigned count() const { return m_routes->count_(); }
  unsigned tombstoneCount() const { return m_tombstones.count_(); }
#ifdef ZDEBUG
  uint64_t shortProbes() const { return m_shortProbes; }
  void resetShortProbes() const { m_shortProbes = 0; }
#endif
  unsigned active() const {
    unsigned n = 0;
    all([&n](const Route &) { ++n; });
    return n;
  }
  template <typename L>
  void all(L &&l) const {
    auto i = m_routes->citer();
    while (auto node = i()) {
      const auto &route = *node;
      if (route.state == CxnState::Active) l(route);
    }
  }

private:
  void addLength(unsigned length) {
    if (!m_lengthCount[length]++) m_lengthMask |= uint32_t(1) << length;
  }
  void delLength(unsigned length) {
    if (!--m_lengthCount[length]) m_lengthMask &= ~(uint32_t(1) << length);
  }
  void trimTombstones() {
    while (m_tombstones.count_() > TombstoneMax) {
      m_tombstones.queueFull();
      CxnID id = m_tombstones.shift();
      if (id) m_routes->del(id);
    }
  }
  const Route *matchShortRoute(ZuBSpan packet, bool activeOnly) const {
    if (!packet || packet.length() < 2 || Pkt::isLong(packet)) return nullptr;
    unsigned max = packet.length() - 1;
    if (max > CxnIDMax) max = CxnIDMax;
    uint32_t lengths = m_lengthMask;
    while (max) {
      if (!(lengths & (uint32_t(1) << max))) {
	--max;
	continue;
      }
      CxnID id{ZuBSpan{packet.data() + 1, max}};
#ifdef ZDEBUG
      ++m_shortProbes;
#endif
      if (auto route = m_routes->find(id)) {
	if (route->state != CxnState::Tombstone &&
	    (!activeOnly || route->state == CxnState::Active))
	  return route;
      }
      --max;
    }
    return nullptr;
  }

  // Rx thread exclusive
  ZmRef<Routes>	m_routes;
  Tombstones	m_tombstones{ZmQueueParams{}.initial(TombstoneMax)};
  ZuArray<unsigned, CxnIDMax + 1>
		m_lengthCount = fixedArray<unsigned, CxnIDMax + 1>();
  uint32_t	m_lengthMask = 0;
#ifdef ZDEBUG
  mutable uint64_t m_shortProbes = 0;
#endif
};

struct alignas(Zm::CacheLineSize) EndpointLink : public ZmPolymorph {
  // The following endpoint registry node is Rx-owned; its links must not
  // share the reference count's line.
  char padding[Zm::CacheLineSize - sizeof(ZmPolymorph)];
};

inline const EndpointLink *EndpointLinkKey(const EndpointLink &link) {
  return &link;
}

// Endpoint membership owns the link alongside outstanding callbacks. Concrete
// application links provide the allocator; this inner node only adds the link.
ZmHashDerive(ServerLinks, EndpointLink,
  (ZmHashNode<EndpointLink,
    ZmHashKey<EndpointLinkKey,
      ZmHashID<"Zquic.Server.Links", ZmHashHeapID<"">>>>));

using EndpointProbeFn = ZmFn<void(bool), ZmFnHeapID<"Zquic.Endpoint.ProbeFn">>;
using EndpointDiscFn = ZmFn<void(), ZmFnHeapID<"Zquic.Endpoint.DiscFn">>;
using EndpointRebindFn =
  ZmFn<void(bool, ZiSockAddr, bool), ZmFnHeapID<"Zquic.Endpoint.RebindFn">>;
ZmQueueDerive(EndpointDiscFns_, EndpointDiscFn,
  ZmQueueHeapID<"Zquic.Endpoint.DiscFns">);
using EndpointDiscFns = Queue<EndpointDiscFns_>;

class EndpointTxWait;
class EndpointTxGate;

struct EndpointTxToken {
  EndpointTxWait *wait = nullptr;
  void *physical = nullptr;
  uint64_t serial = 0;
  uint64_t epoch = 0;
  unsigned generation = 0;

  bool operator !() const { return !wait; }
  ZuOpBool
};

struct EndpointTxResult { enum { Ready, Retired, Cancelled }; };
using EndpointTxFn = ZmFn<void(int, EndpointTxToken),
  ZmFnHeapID<"Zquic.Endpoint.TxWait">>;

struct EndpointTxWaitData {
  enum { Idle, Pending, Reserved, Terminal };
  EndpointTxGate *gate = nullptr;
  unsigned phase = Idle; // mx Tx, including intrusive membership below
};
using EndpointTxWaitList = ZmList<EndpointTxWaitData,
  ZmListNode<EndpointTxWaitData, ZmListShadow<>>>;

class alignas(Zm::CacheLineSize) EndpointTxWait : public EndpointTxWaitList::Node {
public:
  EndpointTxWait() = default;
  EndpointTxWait(const EndpointTxWait &) = delete;
  EndpointTxWait &operator =(const EndpointTxWait &) = delete;
  ~EndpointTxWait() { assert(!busy()); }

  // Caller SID. The request remains busy while its reservation is outstanding.
  bool busy() const { return m_caller.active || m_caller.cancelling; }
  void start(ZiMultiplex *mx, unsigned sid, uint64_t epoch, EndpointTxFn fn) {
    assert(mx->invoked(sid) && !busy());
    m_config = {mx, sid, ++m_caller.serial, epoch};
    m_caller.fn = ZuMv(fn);
    m_caller.active = true;
  }
  void cancel(EndpointDiscFn done = {});

  // Immutable until terminal delivery. Only mx Tx calls these methods.
  unsigned sid() const { return m_config.sid; }
  EndpointTxToken token(void *physical, unsigned generation) {
    return {this, physical, m_config.serial, m_config.epoch, generation};
  }
  bool matches(const EndpointTxToken &token) const {
    return token.serial == m_config.serial && token.epoch == m_config.epoch;
  }
  void grant(EndpointTxToken token) {
    m_config.mx->run([this, token] {
      if (!m_caller.cancelling && m_caller.fn)
	m_caller.fn(EndpointTxResult::Ready, token);
    }, m_config.sid);
  }
  void complete(int result, EndpointDiscFn ack = {}) {
    auto mx = m_config.mx;
    mx->run([this, mx, result, ack = ZuMv(ack)]() mutable {
      m_caller.active = false;
      auto fn = ZuMv(m_caller.fn);
      if (fn) fn(result, EndpointTxToken{});
      // fn may reuse or destroy this request; the acknowledgment touches only
      // the physical gate, which cannot retire until it receives this callback.
      if (ack) mx->txRun([ack = ZuMv(ack)]() mutable { ack(); });
    }, m_config.sid);
  }
  void reject() {
    gate = nullptr;
    phase = Terminal;
    complete(EndpointTxResult::Cancelled);
  }

private:
  struct alignas(Zm::CacheLineSize) Config {
    ZiMultiplex *mx = nullptr;
    unsigned sid = 0;
    uint64_t serial = 0, epoch = 0;
  } m_config;
  struct alignas(Zm::CacheLineSize) Caller {
    EndpointTxFn fn;
    uint64_t serial = 0;
    bool active = false, cancelling = false;
  } m_caller;
};

// One gate per physical connection. Its owner retains it through close's
// asynchronous completion, including all caller-side terminal callbacks.
class EndpointTxGate {
public:
  // Bound grants and terminal callbacks per mx Tx turn.
  enum { TurnBatch = 64 };
  using CountFn = ZmFn<unsigned(), ZmFnHeapID<"Zquic.Endpoint.TxWait">>;

  EndpointTxGate(ZiMultiplex *mx, void *physical, unsigned generation,
    unsigned limit, CountFn count) :
    m_mx{mx}, m_physical{physical}, m_generation{generation},
    m_limit{limit}, m_count{ZuMv(count)} { }
  ~EndpointTxGate() {
    assert(m_pending.empty_() && m_reserved.empty_() && !m_draining && !m_posted);
  }

  void initQueues(Ztc::Hub *hub, ZuCSpan prefix) {
    m_pending.initQueue(hub, prefix, "wait", Ztc::QueueType::Tx);
    m_reserved.initQueue(hub, prefix, "reserved", Ztc::QueueType::Tx, m_limit);
  }
  void joinQueues(QueueCatalog &queues) {
    m_pending.joinQueue(queues);
    m_reserved.joinQueue(queues);
  }
  void finalQueues() {
    m_pending.finalQueue();
    m_reserved.finalQueue();
  }
  unsigned allQueues(Ztc::QueueMgr::AllFn fn) const {
    fn(const_cast<decltype(m_pending) *>(&m_pending));
    fn(const_cast<decltype(m_reserved) *>(&m_reserved));
    return 2;
  }

  unsigned reservations() const { return m_reserved.count_(); }
  unsigned waiting() const { return m_pending.count_(); }
  bool available(unsigned queued) const {
    return !m_closing && uint64_t(queued) + reservations() < m_limit;
  }
  bool available() const {
    return available(m_count());
  }
  void waitTx(EndpointTxWait &wait) {
    assert(m_mx->invoked(m_mx->txThread()));
    if (m_closing) { wait.reject(); return; }
    if (reservations() >= m_limit) m_reserved.queueFull();
    wait.gate = this;
    wait.phase = EndpointTxWaitData::Pending;
    m_pending.pushNode(&wait);
    service();
  }
  bool consume(const EndpointTxToken &token) {
    assert(m_mx->invoked(m_mx->txThread()));
    // Reject stale physical identity before touching the request.
    if (!token || token.physical != m_physical ||
	token.generation != m_generation || m_closing) return false;
    EndpointTxWait *wait = nullptr;
    {
      auto i = m_reserved.citer();
      while (auto node = i())
	if (node == token.wait) { wait = static_cast<EndpointTxWait *>(node); break; }
    }
    // A consumed token may contain the address of an already retired request.
    // Only dereference current reservation members, then check request reuse.
    if (!wait || !wait->matches(token)) return false;
    m_reserved.delNode(wait);
    terminal(*wait, EndpointTxResult::Retired);
    // The caller admits its datagram before servicing any newly free capacity.
    return true;
  }
  void cancelTx(EndpointTxWait &wait) {
    assert(m_mx->invoked(m_mx->txThread()));
    if (wait.gate != this) return;
    if (wait.phase == EndpointTxWaitData::Pending) m_pending.delNode(&wait);
    else if (wait.phase == EndpointTxWaitData::Reserved) m_reserved.delNode(&wait);
    else return;
    terminal(wait, EndpointTxResult::Cancelled);
    service();
  }
  void service() {
    assert(m_mx->invoked(m_mx->txThread()));
    if (m_closing) return;
    unsigned n = 0;
    while (n < TurnBatch && !m_pending.empty_() && available()) {
      auto wait = static_cast<EndpointTxWait *>(m_pending.shift());
      wait->phase = EndpointTxWaitData::Reserved;
      m_reserved.pushNode(wait); // reserve before posting the grant
      wait->grant(wait->token(m_physical, m_generation));
      ++n;
    }
    if (!m_posted && !m_pending.empty_() && available()) {
      m_posted = true;
      m_mx->txRun([this] {
	m_posted = false;
	if (m_closing) closeBatch();
	else service();
      });
    }
  }
  void close(EndpointDiscFn done = {}) {
    assert(m_mx->invoked(m_mx->txThread()));
    m_closing = true;
    if (done) { assert(!m_done); m_done = ZuMv(done); }
    closeBatch();
  }

private:
  void closeBatch() {
    for (unsigned n = 0; n < TurnBatch; ++n) {
      auto node = m_pending.shift();
      if (!node) node = m_reserved.shift();
      if (!node) break;
      terminal(*static_cast<EndpointTxWait *>(node), EndpointTxResult::Cancelled);
    }
    if (!m_pending.empty_() || !m_reserved.empty_()) {
      if (!m_posted) {
	m_posted = true;
	m_mx->txRun([this] { m_posted = false; closeBatch(); });
      }
      return;
    }
    closed();
  }
  void terminal(EndpointTxWait &wait, int result) {
    wait.phase = EndpointTxWaitData::Terminal;
    ++m_draining;
    wait.complete(result, [this] { --m_draining; closed(); });
  }
  void closed() {
    if (!m_closing || m_draining || m_posted ||
	!m_pending.empty_() || !m_reserved.empty_()) return;
    auto done = ZuMv(m_done);
    if (done) done();
  }

  ZiMultiplex *m_mx;
  void *m_physical;
  unsigned m_generation, m_limit;
  CountFn m_count;
  Queue<EndpointTxWaitList> m_pending, m_reserved;
  EndpointDiscFn m_done;
  unsigned m_draining = 0;
  bool m_closing = false, m_posted = false;
};

inline void EndpointTxWait::cancel(EndpointDiscFn done)
{
  auto mx = m_config.mx;
  assert(mx && mx->invoked(m_config.sid) && !m_caller.cancelling);
  if (!m_caller.active) {
    if (done) mx->run([done = ZuMv(done)]() mutable { done(); }, m_config.sid);
    return;
  }
  m_caller.cancelling = true;
  mx->txRun([this, done = ZuMv(done)]() mutable {
    if (gate) gate->cancelTx(*this);
    m_config.mx->run([this, done = ZuMv(done)]() mutable {
      m_caller.cancelling = false;
      if (done) done();
    }, m_config.sid);
  });
}

struct PktAllocTarget {
  Zi::IOBufAllocator *allocator = nullptr;
  void *owner = nullptr;
  void *physical = nullptr;
  unsigned generation = 0;
  unsigned mps = ~unsigned(0);

  bool operator ==(const PktAllocTarget &) const = default;
};

template <typename Impl>
struct alignas(Zm::CacheLineSize) EndpointTx : ZiTx<Impl> { };

struct alignas(Zm::CacheLineSize) EndpointClose {
  ZmAtomic<unsigned> closing = 0;
};

// Total datagrams, including the installed active head.
enum { EndpointTxQueueLimit = 4096 };

struct alignas(Zm::CacheLineSize) EndpointCxnTx {
  EndpointTxGate	gate;
  uint32_t		full = 0;

  EndpointCxnTx(
    ZiMultiplex *mx, void *physical, unsigned generation,
    EndpointTxGate::CountFn count)
  :
    gate{mx, physical, generation, EndpointTxQueueLimit, ZuMv(count)} { }
};

struct alignas(Zm::CacheLineSize) EndpointConfig {
  ZiMultiplex		*mx = nullptr;
  Zi::IOBufAllocator	*rxBufAllocator = nullptr;
  Zi::IOBufAllocator	*txBufAllocator = nullptr;
  Ztc::Hub		*hub = nullptr;
  ZuID		queueID;
};

struct alignas(Zm::CacheLineSize) EndpointTargetPub {
  ZmAtomic<uint128_t> value = 0;
  ZmAtomic<unsigned> ready = 0;
};

struct alignas(Zm::CacheLineSize) EndpointRxStats : EndpointRxDiag { };

template <typename Endpoint_>
class EndpointCxn :
  public ZmNHeap<EndpointCxn<Endpoint_>>,
  public ZiConnection, public EndpointTx<EndpointCxn<Endpoint_>> {
friend inline ZuStringT<"Zquic.Endpoint.Cxn"> ZmNHeapID(EndpointCxn *);

friend Endpoint_;

  class UDPQueue final : public QueueInfo {
  public:
    explicit UDPQueue(const EndpointCxn *cxn) : m_cxn{cxn} { }
    ~UDPQueue() { finalQueue(); }
    void telemetry(Ztc::QueueTelemetry &data) const override {
      queueTelemetry(data);
      m_cxn->udpTelemetry(data);
    }
  private:
    const EndpointCxn *m_cxn;
  };

public:
  enum { EndpointTxQueueLimit = Zquic_::EndpointTxQueueLimit };
  using Tx = ZiTx<EndpointCxn>;
  using Tx::txQueue;

  template <typename L> void ownerLayout(L l) const {
    l(&m_config, sizeof(m_config));
    l(&m_closeState, sizeof(m_closeState));
    l(&m_tx, sizeof(m_tx));
    l(static_cast<const EndpointTx<EndpointCxn> *>(this),
      sizeof(EndpointTx<EndpointCxn>));
  }

  EndpointCxn(Endpoint_ *endpoint, const ZiCxnInfo &ci, unsigned generation) :
	ZiConnection(endpoint->mx_(), ci),
	m_config{endpoint, generation},
	m_udp{this},
	m_tx{endpoint->mx_(), this, generation,
	  [this] { return unsigned(txQueue.count_()); }}
  {
    auto hub = endpoint->queueHub();
    QueueString prefix;
    prefix << endpoint->queueID() << ':' << generation;
    auto &queues = endpoint->queueCatalog();
    // Endpoint diagnostics need queue lifetime protection even without a hub.
    m_udp.initQueue(hub, prefix, "udp", Ztc::QueueType::Tx, EndpointTxQueueLimit);
    m_udp.joinQueue(queues);
    if (hub) {
      m_tx.gate.initQueues(hub, prefix);
      m_tx.gate.joinQueues(queues);
    }
    m_config.endpoint->cxnAdded();
  }
  ~EndpointCxn() {
    finalQueues();
  }

  void connected(ZiRxContext &io) override {
    m_config.endpoint->connected_(this, io);
  }
  void disconnected(bool) override {
    // Physical retirement precedes release of the intrusive active head.
    mx()->txRun([cxn = ZmRef(this)]() mutable {
      cxn->retireTx();
      cxn->m_tx.gate.close([cxn = ZuMv(cxn)]() mutable {
	auto endpoint = cxn->m_config.endpoint;
	auto impl = endpoint->impl();
	if constexpr (Endpoint_::Impl::EndpointRef)
	  endpoint->disconnected_0(cxn.ptr(), ZmRef(impl));
	else
	  endpoint->disconnected_0(cxn.ptr(), impl);
      });
    });
  }

  unsigned allQueues(Ztc::QueueMgr::AllFn fn) const {
    fn(const_cast<UDPQueue *>(&m_udp));
    return 1 + m_tx.gate.allQueues(ZuMv(fn));
  }
  void udpTelemetry(Ztc::QueueTelemetry &data) const {
    data.count = txQueue.count_();
    data.size = EndpointTxQueueLimit;
    data.full = m_tx.full;
  }

  unsigned generation() const { return m_config.generation; }
  void local(ZiSockAddr addr) { m_config.local = ZuMv(addr); }
  bool txPending() {
    auto &io = txContext();
    return !io.completed() && io.buf && io.buf.ptr() == txQueue.headPtr();
  }
  uint64_t txQueued() {
    auto count = txQueue.count_();
    return count ? count - unsigned(txPending()) : 0;
  }
  unsigned maxQueueLen() const { return EndpointTxQueueLimit; }

  void sending(ZiTxContext &io) {
    auto pkt = io.buf.template ptr<TxPkt>();
    io.addr = pkt->addr;
    io.tos = ecnTOS(pkt->ecn);
  }

  void sent(ZmRef<ZiIOBuf> buf, bool ok) {
    if (ok) {
      m_config.endpoint->sent_(buf->length);
      m_tx.gate.service();
    } else {
      m_config.endpoint->ioError();
      // ZiTx cleans the failed queue after this callback. A completion error
      // alone does not close the physical endpoint; service returned capacity
      // after that cleanup. Only this exceptional turn retains the connection.
      if (m_tx.gate.waiting())
	mx()->txRun([cxn = ZmRef(this)] {
	  if (cxn->up()) cxn->m_tx.gate.service();
	});
    }
  }

  void waitTx(EndpointTxWait &wait) {
    if (m_closeState.closing.load_() || !up()) { wait.reject(); return; }
    m_tx.gate.waitTx(wait);
  }

  bool sendPkt(
      ZmRef<ZiIOBuf> buf, ZiSockAddr addr,
      EcnMark::T ecn = EcnMark::NotECT, bool priority = false,
      EndpointTxToken token = {}, bool *blocked = nullptr) {
    ZiAssert(m_config.endpoint->endpointTxInvoked(),
      "Zquic", (), "QUIC endpoint send outside Tx thread", return false);
    if (blocked) *blocked = false;
    if (m_closeState.closing.load_() || !up() || !buf || !buf->length) {
      m_config.endpoint->noteTxDrop();
      return false;
    }
    bool reserved = token && m_tx.gate.consume(token);
    if (token && !reserved) {
      m_config.endpoint->noteTxDrop();
      return false;
    }
    if (!reserved && !m_tx.gate.available(unsigned(txQueue.count_()))) {
      ++m_tx.full;
      if (blocked) *blocked = true;
      m_config.endpoint->noteTxBackPressure();
      m_config.endpoint->noteTxDrop();
      return false;
    }
    auto pkt = buf.template ptr<TxPkt>();
    pkt->addr = ZuMv(addr);
    pkt->ecn = ecn;
    pkt->owner = this;
    m_config.endpoint->txSubmitted(pkt->length);
    if (priority && txQueue.count_()) {
      // Insert after the active head; repeated priorities precede pending work.
      Zi::IOQueue::Iter i{txQueue};
      i();
      i.pushNode(pkt);
    } else {
      Tx::send(ZmRef<ZiIOBuf>{pkt});
    }
    if (reserved) m_tx.gate.service();
    return true;
  }

private:
  void finalQueues() {
    m_udp.finalQueue();
    m_tx.gate.finalQueues();
  }
  bool beginCloseRx() { return !m_closeState.closing.xch(1); }
  void closeTx() { m_closeState.closing = true; m_tx.gate.close(); }
  void closeRx() { m_closeState.closing = true; }

  void retireTx() {
    m_closeState.closing = true;
    txContext().complete();
    txQueue.clean();
  }

  bool recv_1(ZiRxContext &io) {
    if (io.buf && io.buf->length) {
	// Snapshot recvmsg metadata before transferring the provided slot.
	auto addr = io.addr;
	auto ecn = ecnMarkFromTOS(io.tos);
	Datagram d{ZuMv(io.buf), ZuMv(addr), ecn};
	d.local = m_config.local;
	m_config.endpoint->received_(ZuMv(d));
    }
    if (m_closeState.closing.load_()) io.complete();
    else if (!io.buf) {
	auto allocator = m_config.endpoint->endpointRxAllocator();
	io.buf = allocator->allocBuf(this, 0, info().mps);
	if (ZuUnlikely(!io.buf)) io.disconnect();
    }
    return true;
  }

  void recv_0(ZiRxContext &io) {
    auto allocator = m_config.endpoint->endpointRxAllocator();
    auto buf = allocator->allocBuf(this, 0, info().mps);
    if (ZuUnlikely(!buf)) { io.disconnect(); return; }
    io.init(ZiRxFn{this, ZmFnPtr<&EndpointCxn::recv_1>{}}, ZuMv(buf));
  }


private:
  // shared: stable after construction
  struct alignas(Zm::CacheLineSize) Config {
    Endpoint_		*endpoint = nullptr;
    unsigned		generation = 0;
    ZiSockAddr		local;
  } m_config;

  // exceptional atomic close handoff shared by Rx and Tx
  Zquic_::EndpointClose m_closeState;

  UDPQueue m_udp;

  Zquic_::EndpointCxnTx m_tx;


};

template <typename Impl_>
class Endpoint_ {
  template <typename> friend class EndpointCxn;

  using Cxn_ = EndpointCxn<Endpoint_>;

public:
  using Impl = Impl_;
  using RxPktAllocator = Zi::IOBufAllocator;
  using TxPktAllocator = Zi::IOBufAllocator;
  using CxnRef = ZmRef<Cxn_>;

  Endpoint_() = default;
  ~Endpoint_() {
    ZiAssert(!m_endpointRx.open && !m_endpointRx.ioCxns && !m_endpointRx.unbinding, "Zquic", (),
      "QUIC endpoint destroyed before finalization", return);
  }

  Endpoint_(const Endpoint_ &) = delete;
  Endpoint_ &operator =(const Endpoint_ &) = delete;

  bool init(ZiMultiplex *mx, RxPktAllocator *rx, TxPktAllocator *tx,
      Ztc::Hub *hub = nullptr, uint64_t ordinal = 0) {
    ZiAssert(mx, "Zquic", (), "null endpoint multiplexer", return false);
    if (!rx || !tx || rx->mx != mx || tx->mx != mx) return false;
    if (m_config.mx) return m_config.mx == mx && m_config.rxBufAllocator == rx &&
      m_config.txBufAllocator == tx && m_config.hub == hub;
    m_config.mx = mx;
    m_config.rxBufAllocator = rx;
    m_config.txBufAllocator = tx;
    m_config.hub = hub;
    if (hub) m_config.queueID = ZuID{} << "quic:endpoint:" << ZuBox<uint64_t>{ordinal}.hex();
    initDiscQueue();
    return true;
  }

  bool openUDP(
      PathMode::T mode,
      ZiIP localIP, uint16_t localPort,
      ZiIP remoteIP = {}, uint16_t remotePort = 0)
  {
    ZiAssert(m_config.mx, "Zquic", (), "endpoint multiplexer is not initialized",
      return false);
    ZiMultiplex *mx = m_config.mx;
    ZiAssert(mx->running(), "Zquic", (),
      "endpoint multiplexer is not running", return false);
    if (m_endpointRx.open || m_endpointRx.cxn ||
	m_endpointRx.ioCxns || m_endpointRx.unbinding) return false;


    m_endpointRx.mode = mode;
    m_endpointRx.local.init(localIP, localPort);
    if (!!remoteIP) m_endpointRx.remote.init(remoteIP, remotePort);
    else m_endpointRx.remote.null();
    IPFamily::T family = IPFamily::IPv4;
    if (localIP.v6() || remoteIP.v6()) family = IPFamily::IPv6;
    m_endpointRx.sockConfig = SockConfig{family, mode, true, true};
    m_endpointRx.sockDiag = {};
    m_target.ready = false;
    m_endpointRx.open = true;
    ++m_endpointRx.generation;

    m_config.mx->txInvoke([this]() mutable {
      m_endpointTx.diag = {};
    });

    ZiCxnOptions options;
    options.udp(true);

    m_config.mx->udp(
      ZiConnectFn{this, [](Endpoint_ *self, const ZiCxnInfo &ci) -> ZiConnection * {
	auto cxn = new Cxn_{self, ci, self->m_endpointRx.generation};
	self->m_endpointRx.cxn = cxn;
	return cxn;
      }},
      ZiFailFn{this, [](Endpoint_ *self, bool transient) {
	self->failed_(transient);
      }},
      localIP, localPort,
      mode == PathMode::ClientConnected ? remoteIP : ZiIP{},
      mode == PathMode::ClientConnected ? remotePort : 0,
      m_config.rxBufAllocator, options);

    return true;
  }

  template <typename L>
  bool rebindUDP(
      ZiIP localIP, uint16_t localPort,
      ZiIP remoteIP, uint16_t remotePort,
      L &&l, bool probe = false) {
    EndpointRebindFn fn{ZuFwd<L>(l)};
    if (!m_config.mx) {
      fn(false, ZiSockAddr{}, false);
      return false;
    }
    if (!m_config.mx->running()) {
      fn(false, ZiSockAddr{}, false);
      return false;
    }
    if (endpointRxInvoked())
      return rebindUDP_(localIP, localPort, remoteIP, remotePort, ZuMv(fn), probe);
    m_config.mx->rxRun([
      this,
      localIP,
      localPort,
      remoteIP,
      remotePort,
      probe,
      fn = ZuMv(fn)
    ]() mutable {
      rebindUDP_(localIP, localPort, remoteIP, remotePort, ZuMv(fn), probe);
    });
    return true;
  }

  // Rx snapshot, transferred once to protocol Tx when the probe is ready.
  PktAllocTarget probeTarget() const {
    auto cxn = m_endpointRx.rebindCxn.ptr();
    return {m_config.txBufAllocator, const_cast<Endpoint_ *>(this), cxn,
      m_endpointRx.rebindGeneration, cxn ? cxn->info().mps : ~unsigned(0)};
  }
  void finishProbe(bool promote, EndpointProbeFn done = {}) {
    if (!endpointRxInvoked()) {
      m_config.mx->rxRun([this, promote, done = ZuMv(done)]() mutable {
	finishProbe(promote, ZuMv(done));
      });
      return;
    }
    CxnRef close;
    bool promoted = false;
    if (m_endpointRx.probing) {
      if (promote && m_endpointRx.rebindCxn && !m_endpointRx.rebinding) {
	close = ZuMv(m_endpointRx.cxn);
	m_endpointRx.cxn = ZuMv(m_endpointRx.rebindCxn);
	m_endpointRx.generation = m_endpointRx.rebindGeneration;
	m_endpointRx.local = m_endpointRx.rebindLocal;
	m_endpointRx.remote = m_endpointRx.rebindRemote;
	m_endpointRx.sockConfig = m_endpointRx.rebindSockConfig;
	promoted = true;
      } else {
	close = ZuMv(m_endpointRx.rebindCxn);
      }
      m_endpointRx.probing = false;
      m_endpointRx.rebinding = false;
      auto fn = ZuMv(m_endpointRx.rebindFn);
      if (fn) fn(false, ZiSockAddr{}, !m_endpointRx.open);
    }
    if (promoted) publishTargetRx();
    closeCxn(ZuMv(close));
    m_config.mx->txRun([this, promoted, done = ZuMv(done)]() mutable {
      m_endpointTx.probe = nullptr;
      if (done) done(promoted);
    });
  }

protected:
  template <typename L> void ownerLayout(L l) const {
    l(&m_config, sizeof(m_config));
    l(&m_endpointRx, sizeof(m_endpointRx));
    l(&m_endpointRx.diag, sizeof(m_endpointRx.diag));
    l(&m_endpointTx, sizeof(m_endpointTx));
    l(&m_target, sizeof(m_target));
  }

private:
  bool rebindUDP_(
      ZiIP localIP, uint16_t localPort,
      ZiIP remoteIP, uint16_t remotePort,
      EndpointRebindFn fn, bool probe) {
    if (!m_config.mx || !m_endpointRx.open || !m_endpointRx.cxn ||
	m_endpointRx.rebindCxn || m_endpointRx.rebinding) {
      fn(false, ZiSockAddr{}, false);
      return false;
    }
    if (!m_config.mx->running()) {
      fn(false, ZiSockAddr{}, false);
      return false;
    }
    IPFamily::T family = IPFamily::IPv4;
    if (localIP.v6() || remoteIP.v6()) family = IPFamily::IPv6;
    m_endpointRx.rebindLocal.init(localIP, localPort);
    if (!!remoteIP)
      m_endpointRx.rebindRemote.init(remoteIP, remotePort);
    else
      m_endpointRx.rebindRemote.null();
    m_endpointRx.rebindSockConfig = SockConfig{family, m_endpointRx.mode, true, true};
    if (m_endpointRx.rebindGeneration < m_endpointRx.generation)
      m_endpointRx.rebindGeneration = m_endpointRx.generation;
    ++m_endpointRx.rebindGeneration;
    m_endpointRx.rebindFn = ZuMv(fn);
    m_endpointRx.rebinding = true;
    m_endpointRx.probing = probe;

    ZiCxnOptions options;
    options.udp(true);
    m_config.mx->udp(
      ZiConnectFn{this, [](Endpoint_ *self, const ZiCxnInfo &ci) ->
	  ZiConnection * {
	if (!self->m_endpointRx.rebinding) return nullptr;
	auto cxn = new Cxn_{self, ci, self->m_endpointRx.rebindGeneration};
	self->m_endpointRx.rebindCxn = cxn;
	return cxn;
      }},
      ZiFailFn{this, [](Endpoint_ *self, bool) {
	self->failedRebind(false);
      }},
      localIP, localPort,
      m_endpointRx.mode == PathMode::ClientConnected ? remoteIP : ZiIP{},
      m_endpointRx.mode == PathMode::ClientConnected ? remotePort : 0,
      m_config.rxBufAllocator, options);
    return true;
  }

public:
  void disconnect() {
    disconnect_(EndpointDiscFn{});
  }
  template <typename L>
  void disconnect(L &&l) {
    disconnect_(ZuFwd<L>(l));
  }
  template <typename L>
  void disconnect_(L &&l) {
    if (!m_config.mx) {
      m_target.ready = false;
      m_endpointRx.open = false;
      l();
      return;
    }
    if (endpointRxInvoked()) {
      disconnectRx(EndpointDiscFn{ZuFwd<L>(l)});
      return;
    }
    m_config.mx->rxRun([this, l = ZuFwd<L>(l)]() mutable {
      disconnectRx(EndpointDiscFn{ZuMv(l)});
    });
  }

  QueueCatalog &queueCatalog() { return m_queues; }
  unsigned allQueues(Ztc::QueueMgr::AllFn fn) const {
    if (!m_config.hub) return 0;
    fn(const_cast<EndpointDiscFns *>(&m_endpointRx.discFns));
    return 1 + m_queues.allQueues(ZuMv(fn));
  }

  bool listening() const { return m_target.ready; }
  bool connected() const { return m_target.ready; }
  const ZiSockAddr &local() const { return m_endpointRx.local; }
  const ZiSockAddr &remote() const { return m_endpointRx.remote; }
  PathMode::T mode() const { return m_endpointRx.mode; }
  unsigned generation() const { return m_endpointRx.generation; }
  EndpointDiag diag() const {
    uint64_t txPending = 0;
    uint64_t txQueued = 0;
    uint128_t target = m_target.value;
    if (uintptr_t(target)) {
      // Report the active socket only; detached queues remain registered
      // through retirement, and a probing socket is not active yet.
      ZuID id;
      id << m_config.queueID << ':' << unsigned(target >> 64) << ":udp";
      m_queues.allQueues([&txPending, &txQueued, id](Ztc::Queue *queue) {
	if (queue->telKey().p<1>() != id) return;
	Ztc::QueueTelemetry data;
	queue->telemetry(data);
	if (data.count) { ++txPending; txQueued += data.count - 1; }
      });
    }
    return {m_endpointRx.diag, m_endpointTx.diag, txPending, txQueued};
  }
  SockDiag sockDiag() const { return m_endpointRx.sockDiag; }
  void failure() { ++m_endpointRx.diag.failures; }
  PktAllocTarget pktAllocTarget() const {
    uint128_t target = m_target.value;
    return {m_config.txBufAllocator, const_cast<Endpoint_ *>(this),
      reinterpret_cast<void *>(uintptr_t(target)),
      unsigned(target >> 64), unsigned(target >> 96)};
  }

  ZmRef<ZiIOBuf> allocTxPkt() {
    auto target = pktAllocTarget();
    return target.allocator->allocBuf(target.owner, 0, target.mps);
  }

  void waitTx(EndpointTxWait &wait, unsigned sid, uint64_t epoch, EndpointTxFn fn,
      PktAllocTarget target = {}) {
    wait.start(m_config.mx, sid, epoch, ZuMv(fn));
    if (!target.physical) target = pktAllocTarget();
    m_config.mx->txRun([this, wait = &wait, target] {
      auto cxn = txCxn(target.physical, target.generation);
      if (!cxn) {
	wait->reject();
	return;
      }
      cxn->waitTx(*wait);
    });
  }
  void cancelTx(EndpointTxWait &wait, EndpointDiscFn done = {}) {
    wait.cancel(ZuMv(done));
  }

  bool send(
      ZmRef<ZiIOBuf> buf, ZiSockAddr addr,
      EcnMark::T ecn = EcnMark::NotECT, bool priority = false,
      EndpointTxToken token = {}, bool *blocked = nullptr,
      PktAllocTarget target = {}) {
    if (blocked) *blocked = false;
    if (!buf) return false;
    ZiAssert(m_config.mx, "Zquic", (), "null endpoint multiplexer", return false);
    if (!target.physical) target = pktAllocTarget();
    if (endpointTxInvoked())
      return send_(
	ZuMv(buf), ZuMv(addr), ecn, true, priority,
	static_cast<Cxn_ *>(target.physical), target.generation, token, blocked);
    buf->owner = this;
    if (!token) {
      // Keep the ordinary posted packet's capture unchanged by reservations.
      m_config.mx->txRun([buf = ZuMv(buf), addr = ZuMv(addr),
	expected = static_cast<Cxn_ *>(target.physical), generation = target.generation,
	ecn, priority]() mutable {
	auto endpoint = static_cast<Endpoint_ *>(buf->owner);
	(void)endpoint->send_(
	  ZuMv(buf), ZuMv(addr), ecn, false, priority, expected, generation);
      });
      return true;
    }
    m_config.mx->txRun([
      buf = ZuMv(buf),
      addr = ZuMv(addr),
      expected = static_cast<Cxn_ *>(target.physical),
      generation = target.generation,
      ecn,
      priority, token
    ]() mutable {
      auto endpoint = static_cast<Endpoint_ *>(buf->owner);
      (void)endpoint->send_(
	ZuMv(buf), ZuMv(addr), ecn, false, priority, expected, generation, token);
    });
    return true;
  }

private:
  void connected_(Cxn_ *cxn, ZiRxContext &io) {
    bool rebind = false;
    CxnRef oldCxn;
    if (m_endpointRx.rebindCxn == cxn && cxn->generation() == m_endpointRx.rebindGeneration) {
      if (m_endpointRx.probing) {
	Sock::initUDP(cxn->info().socket, m_endpointRx.rebindSockConfig);
	ZiSockAddr local = cxnLocal(cxn, m_endpointRx.rebindLocal);
	m_endpointRx.rebindLocal = local;
	m_endpointRx.rebinding = false;
	cxn->local(local);
	cxn->recv_0(io);
	m_config.mx->txRun([this, cxn = ZmRef(cxn), local] {
	  m_endpointTx.probe = cxn;
	  m_config.mx->rxRun([this, cxn, local] {
	    if (m_endpointRx.rebindCxn != cxn || !m_endpointRx.probing) return;
	    auto fn = ZuMv(m_endpointRx.rebindFn);
	    if (fn) fn(true, local, false);
	  });
	});
	return;
      }
      rebind = true;
      oldCxn = ZuMv(m_endpointRx.cxn);
      m_endpointRx.cxn = cxn;
      m_endpointRx.generation = m_endpointRx.rebindGeneration;
      m_endpointRx.sockConfig = m_endpointRx.rebindSockConfig;
      m_endpointRx.sockDiag = {};
      m_endpointRx.local = m_endpointRx.rebindLocal;
      m_endpointRx.remote = m_endpointRx.rebindRemote;
      m_endpointRx.rebindCxn = nullptr;
      m_endpointRx.rebinding = false;
    } else if (m_endpointRx.cxn != cxn || cxn->generation() != m_endpointRx.generation)
      return;

    Sock::initUDP(cxn->info().socket, m_endpointRx.sockConfig, &m_endpointRx.sockDiag);

    m_endpointRx.local = cxnLocal(cxn, m_endpointRx.local);
    cxn->local(m_endpointRx.local);

    publishTargetRx();
    m_target.ready = true;
    cxn->recv_0(io);
    impl()->endpointReady(this);
    if (rebind) {
      closeCxn(ZuMv(oldCxn));
      EndpointRebindFn fn = ZuMv(m_endpointRx.rebindFn);
      if (fn) fn(true, m_endpointRx.local, false);
    }
  }

  static ZiSockAddr cxnLocal(Cxn_ *cxn, ZiSockAddr local) {
    ZiSockAddr actual;
    actual.init(cxn->info().remote.ip().v6() || local.ip().v6() ?
      ZiIPType::V6 : ZiIPType::V4);
#ifdef _WIN32
    int len = actual.len();
#else
    socklen_t len = actual.len();
#endif
    if (::getsockname(cxn->info().socket, actual.sa(), &len) == 0) {
      actual.sync();
      return actual;
    }
    return local;
  }

  void cxnAdded() { ++m_endpointRx.ioCxns; }
  Ztc::Hub *queueHub() const { return m_config.hub; }
  RxPktAllocator *endpointRxAllocator() const { return m_config.rxBufAllocator; }
  const ZuID &queueID() const { return m_config.queueID; }

  template <typename ImplRef_>
  void disconnected_0(Cxn_ *cxn, ImplRef_ impl_) {
    m_config.mx->rxRun([impl = ZuMvPtr(impl_), cxn = ZmRef(cxn)]() mutable {
      auto self = static_cast<Endpoint_ *>(static_cast<Impl *>(impl));
      // Serialize membership with Rx detach; the captured reference keeps
      // the queues alive through the preceding Tx drain and this removal.
      cxn->finalQueues();
      --self->m_endpointRx.ioCxns;
      self->disconnected_(cxn.ptr());
      if (!self->m_endpointRx.ioCxns &&
	  !self->m_endpointRx.cxn && !self->m_endpointRx.rebindCxn) {
	self->unbindEndpoint(ZuMvPtr(impl), ZuMv(cxn));
	return;
      }
      self->m_config.mx->txRun([
	impl = ZuMvPtr(impl), cxn = ZuMv(cxn)
      ]() mutable {
	if constexpr (Impl::EndpointRef) impl = nullptr;
	cxn = nullptr;
      });
    });
  }

  void disconnected_(Cxn_ *cxn) {
    if (m_endpointRx.cxn == cxn && cxn->generation() == m_endpointRx.generation) {
      cxn->closeRx();
      m_endpointRx.cxn = nullptr;
      publishTargetRx();
      m_target.ready = false;
      m_endpointRx.open = false;
      m_endpointRx.downPending = true;
      closeCxn(ZuMv(m_endpointRx.rebindCxn));
      m_endpointRx.probing = false;
      m_endpointRx.rebinding = false;
      auto fn = ZuMv(m_endpointRx.rebindFn);
      if (fn) fn(false, ZiSockAddr{}, true);
      m_config.mx->txRun([this] { m_endpointTx.probe = nullptr; });
    } else if (m_endpointRx.rebindCxn == cxn) {
      m_endpointRx.rebindCxn = nullptr;
    }
  }

  template <typename ImplRef_>
  void unbindEndpoint(ImplRef_ impl_, CxnRef cxn = {}) {
    m_endpointRx.unbinding = true;
    // Allocators remain registered until multiplexer finalization. Drain Tx
    // before the endpoint completion returns to Rx.
    m_config.mx->txRun([
      impl = ZuMvPtr(impl_), cxn = ZuMv(cxn)
    ]() mutable {
      auto self = static_cast<Endpoint_ *>(static_cast<Impl *>(impl));
      self->m_config.mx->rxRun([
	impl = ZuMvPtr(impl), cxn = ZuMv(cxn)
      ]() mutable {
	auto self = static_cast<Endpoint_ *>(static_cast<Impl *>(impl));
	self->m_endpointRx.unbinding = false;
	if (self->m_endpointRx.downPending) {
	  self->m_endpointRx.downPending = false;
	  self->impl()->endpointDown_(self);
	}
	auto fns = self->takeDiscFns();
	self->m_config.mx->txRun([
	  impl = ZuMvPtr(impl), cxn = ZuMv(cxn), fns = ZuMv(fns)
	]() mutable {
	  if constexpr (Impl::EndpointRef) impl = nullptr;
	  cxn = nullptr;
	  Endpoint_::runDiscFns(fns);
	});
      });
    });
  }

  void failed_(bool transient) {
    ++m_endpointRx.diag.failures;
    m_target.ready = false;
    m_endpointRx.open = false;
    m_endpointRx.cxn = nullptr;
    publishTargetRx();
    impl()->endpointFailed(transient);
    impl()->endpointDown_(this);
  }

  void failedRebind(bool success) {
    if (!m_endpointRx.rebinding) return;
    m_endpointRx.rebindCxn = nullptr;
    m_endpointRx.rebinding = false;
    m_endpointRx.probing = false;
    ++m_endpointRx.diag.failures;
    EndpointRebindFn fn = ZuMv(m_endpointRx.rebindFn);
    if (fn) fn(success, ZiSockAddr{}, false);
  }

  void received_(Datagram datagram) {
    ++m_endpointRx.diag.datagramsRx;
    if (datagram.buf) m_endpointRx.diag.bytesRx += datagram.buf->length;
    impl()->endpointDatagram(ZuMv(datagram));
  }

  void sent_(unsigned bytes) {
    ++m_endpointTx.diag.datagramsTx;
    m_endpointTx.diag.bytesTx += bytes;
  }

  void ioError() {
    if (endpointTxInvoked())
      ++m_endpointTx.diag.failures;
    else
      ++m_endpointRx.diag.failures;
  }

  Cxn_ *txCxn(void *expected, unsigned generation) {
    auto cxn = m_endpointTx.active.ptr();
    if (!cxn || cxn != expected) cxn = m_endpointTx.probe.ptr();
    if (!cxn || cxn != expected || cxn->generation() != generation) return nullptr;
    return cxn;
  }
  void publishTargetRx() {
    // Identity only: a sender never dereferences this cross-owner snapshot.
    // mx Tx admits against its own retained connection and immutable generation.
    auto cxn = m_endpointRx.cxn.ptr();
    uint128_t target = uintptr_t(cxn);
    target |= uint128_t(m_endpointRx.generation) << 64;
    target |= uint128_t(cxn ? cxn->info().mps : ~unsigned(0)) << 96;
    m_config.mx->txRun([this, cxn = m_endpointRx.cxn] {
      m_endpointTx.active = cxn;
    });
    m_target.value = target;
  }

  bool send_(
      ZmRef<ZiIOBuf> buf, ZiSockAddr addr, EcnMark::T ecn, bool direct,
      bool priority, Cxn_ *expected, unsigned generation,
      EndpointTxToken token = {}, bool *blocked = nullptr) {
    ZiAssert(endpointTxInvoked(), "Zquic", (),
      "QUIC endpoint send outside Tx thread", return false);
    ++m_endpointTx.diag.sendCalls;
    if (direct)
      ++m_endpointTx.diag.directCalls;
    else
      ++m_endpointTx.diag.asyncCalls;
    // The posted identity is only compared, never dereferenced or retained.
    auto cxn = txCxn(expected, generation);
    if (!cxn) {
      ++m_endpointTx.diag.txDropped;
      return false;
    }
    return cxn->sendPkt(ZuMv(buf), ZuMv(addr), ecn, priority, token, blocked);
  }

  void disconnectRx(EndpointDiscFn fn) {
    m_endpointRx.discFns.push(ZuMv(fn));
    m_target.ready = false;
    m_endpointRx.open = false;
    EndpointRebindFn rebindFn;
    rebindFn = ZuMv(m_endpointRx.rebindFn);
    m_endpointRx.rebinding = false;
    m_endpointRx.probing = false;
    closeCxn(ZuMv(m_endpointRx.rebindCxn));
    m_config.mx->txRun([this] { m_endpointTx.probe = nullptr; });
    if (rebindFn) rebindFn(false, ZiSockAddr{}, true);
    CxnRef cxn = m_endpointRx.cxn;
    if (!cxn) {
      discFns();
      return;
    }
    closeCxn(ZuMv(cxn));
  }
  EndpointDiscFns takeDiscFns() {
    // The whole callback batch is dequeued here. Its later dispatch owns the
    // moved contents; the endpoint queue's identity remains with the endpoint.
    // endpointDown_ may already have reopened the endpoint. Keep the same
    // queue identity for that runtime while dequeuing this completed batch.
    auto fns = ZuMv(m_endpointRx.discFns);
    m_endpointRx.discFns.clean();
    return fns;
  }
  void discFns() {
    if (m_endpointRx.ioCxns || m_endpointRx.unbinding) return;
    if constexpr (Impl::EndpointRef)
      unbindEndpoint(ZmRef(impl()));
    else
      unbindEndpoint(impl());
  }
  static void runDiscFns(EndpointDiscFns &fns) {
    while (auto fn = fns.shift()) fn();
  }
  void closeCxn(CxnRef cxn) {
    if (!cxn) return;
    // A detached socket is no longer part of endpoint telemetry, even while
    // its physical close is pending. Membership changes remain on mx Rx.
    if (cxn != m_endpointRx.cxn && cxn != m_endpointRx.rebindCxn)
      cxn->finalQueues();
    if (!cxn->beginCloseRx()) return;
    m_config.mx->txRun([cxn = ZuMv(cxn)]() mutable {
      cxn->closeTx();
      cxn->disconnect();
    });
  }

  bool endpointRxInvoked() const {
    return m_config.mx && m_config.mx->invoked(m_config.mx->rxThread());
  }
  bool endpointTxInvoked() const {
    return m_config.mx && m_config.mx->invoked(m_config.mx->txThread());
  }
  ZiMultiplex *mx_() const { return m_config.mx; }
  void noteTxDrop() { ++m_endpointTx.diag.txDropped; }
  void txSubmitted(unsigned bytes) {
    ++m_endpointTx.diag.submittedTx;
    m_endpointTx.diag.submittedBytes += bytes;
  }
  void noteTxBackPressure() { ++m_endpointTx.diag.txBackPressure; }
  const Impl *impl() const { return static_cast<const Impl *>(this); }
  Impl *impl() { return static_cast<Impl *>(this); }

  void initDiscQueue() {
    if (m_config.hub)
      m_endpointRx.discFns.initQueue(
	m_config.hub, m_config.queueID, "disconnect", Ztc::QueueType::Rx);
  }

  // Stable after init; the preceding application/base tail has its own extent.
  Zquic_::EndpointConfig m_config;

  QueueCatalog m_queues;
  Zquic_::EndpointTargetPub m_target;

  // Physical identity and status are Rx-written with existing advisory readers.
  struct alignas(Zm::CacheLineSize) RxStorage {
    CxnRef		cxn;
    unsigned		generation = 0;
    EndpointRxStats	diag;
    PathMode::T		mode = PathMode::ServerUnconnected;
    ZiSockAddr		local;
    ZiSockAddr		remote;
    bool			open = false;
    SockConfig		sockConfig;
    CxnRef		rebindCxn;
    EndpointDiscFns	discFns;
    unsigned		rebindGeneration = 0;
    unsigned		ioCxns = 0;
    bool			unbinding = false;
    bool			downPending = false;
    EndpointRebindFn	rebindFn;
    ZiSockAddr		rebindLocal;
    ZiSockAddr		rebindRemote;
    SockConfig		rebindSockConfig;
    SockDiag		sockDiag;
    bool			rebinding = false;
    bool			probing = false;
  } m_endpointRx;

  struct alignas(Zm::CacheLineSize) TxStorage {
    EndpointTxDiag diag;
    CxnRef active;
    CxnRef probe;
  } m_endpointTx;
};

} // namespace Zquic_
