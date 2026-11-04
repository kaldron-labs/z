//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC private implementation helpers

#ifndef Zquic_HH
#error "include zlib/Zquic.hh before this header"
#endif

#include <zlib/ZiIOContext.hh>

namespace Zquic_ {

template <typename Link_>
struct Cxn {
  Cxn() = default;
  Cxn(
    const CxnID &id_, uint64_t sequence_, Link_ *link_,
    const ResetToken &resetToken_, CxnState::T state_) :
      id{id_},
      sequence{sequence_},
      link{link_},
      resetToken{resetToken_},
      state{state_} { }

  CxnID			id;
  uint64_t		sequence = 0;
  Link_			*link = nullptr;
  ResetToken		resetToken;
  CxnState::T		state = CxnState::Active;
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
  static constexpr unsigned TombstoneMax = 64;
  using Tombstones = ZmQueue<CxnID,
    ZmQueueHeapID<"Zquic.Endpoint.CxnRouter.Tombstones">>;

  CxnRouter() { init(); }

  void init() {
    if (!m_routes)
      m_routes = new Routes{ZmHashParams().bits(5).loadFactor(1).cBits(3)};
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
      if (route->state != CxnState::Active) addLength_(id.length());
      route->sequence = sequence;
      route->link = link;
      route->resetToken = resetToken;
      route->state = CxnState::Active;
      return true;
    }
    m_routes->add(Route{id, sequence, link, resetToken, CxnState::Active});
    addLength_(id.length());
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
    const Route *route = matchShortRoute_(packet, !resetToken);
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
    const Route *route = matchShortRoute_(packet, false);
    if (!route || !route->resetToken.valid()) return false;
    token = route->resetToken;
    return true;
  }

  bool retire(const CxnID &id) {
    auto route = m_routes->find(id);
    if (!route || route->state != CxnState::Active) return false;
    delLength_(id.length());
    m_routes->del(id);
    return true;
  }

  bool tombstone(const CxnID &id) {
    if (!id) return false;
    if (auto route = m_routes->find(id)) {
      if (route->state == CxnState::Tombstone) return true;
      if (route->state == CxnState::Active) delLength_(id.length());
      route->state = CxnState::Tombstone;
      route->link = nullptr;
      route->resetToken = {};
      m_tombstones.push(id);
      trimTombstones_();
      return true;
    }
    m_routes->add(Route{id, 0, nullptr, {}, CxnState::Tombstone});
    m_tombstones.push(id);
    trimTombstones_();
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
  void addLength_(unsigned length) {
    if (!m_lengthCount[length]++) m_lengthMask |= uint32_t(1) << length;
  }
  void delLength_(unsigned length) {
    if (!--m_lengthCount[length]) m_lengthMask &= ~(uint32_t(1) << length);
  }
  void trimTombstones_() {
    while (m_tombstones.count_() > TombstoneMax) {
      CxnID id = m_tombstones.shift();
      if (id) m_routes->del(id);
    }
  }
  const Route *matchShortRoute_(ZuBSpan packet, bool activeOnly) const {
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

struct EndpointLink : public ZmPolymorph { };

inline const EndpointLink *EndpointLinkKey(const EndpointLink &link) {
  return &link;
}

// Endpoint membership owns the link alongside outstanding callbacks. Concrete
// application links provide the allocator; this inner node only adds the link.
ZmHashDerive(ServerLinks, EndpointLink,
  (ZmHashNode<EndpointLink,
    ZmHashKey<EndpointLinkKey, ZmHashHeapID<"">>>));

using EndpointDiscFn = ZmFn<void(), ZmFnHeapID<"Zquic.Endpoint.DiscFn">>;
using EndpointRebindFn =
  ZmFn<void(bool, ZiSockAddr, bool), ZmFnHeapID<"Zquic.Endpoint.RebindFn">>;
ZmQueueDerive(EndpointDiscFns, EndpointDiscFn,
  ZmQueueHeapID<"Zquic.Endpoint.DiscFns">);

template <typename Endpoint_>
class EndpointCxn_ : public ZmNHeap<EndpointCxn_<Endpoint_>>, public ZiConnection {
friend inline ZuStringT<"Zquic.Endpoint.Cxn"> ZmNHeapID(EndpointCxn_ *);

friend Endpoint_;

public:
  static constexpr unsigned EndpointTxQueueLimit = 4096;

  struct TxData {
    TxData() = default;
    TxData(ZmRef<ZiIOBuf> buf_, ZiSockAddr addr_, EcnMark::T ecn_) :
	buf{ZuMv(buf_)}, addr{ZuMv(addr_)}, ecn{ecn_} { }

    ZmRef<ZiIOBuf>	buf;
    ZiSockAddr		addr;
    EcnMark::T		ecn = EcnMark::NotECT;
  };
  ZmListDerive(TxQueue, TxData,
    ZmListNode<TxData,
	ZmListHeapID<"Zquic.Endpoint.TxQueue">>);
  using TxNode = TxQueue::Node;

  EndpointCxn_(Endpoint_ *endpoint, const ZiCxnInfo &ci, unsigned generation) :
	ZiConnection(endpoint->mx_(), ci),
	m_endpoint{endpoint},
	m_generation{generation} { m_endpoint->cxnAdded_(); }

  void connected(ZiRxContext &io) override {
    if (mx()->backend() == ZiMxBackend::URing)
      m_sendDone = ZiTxFn{this, ZmFnPtr<&EndpointCxn_::template sendDone_<true>>{}};
    else
      m_sendDone = ZiTxFn{this, ZmFnPtr<&EndpointCxn_::template sendDone_<false>>{}};
    m_endpoint->connected_(this, io);
  }
  void disconnected(bool) override {
    auto impl = m_endpoint->impl();
    if constexpr (Endpoint_::Impl::EndpointRef)
	m_endpoint->disconnected_0(this, ZmRef(impl));
    else
	m_endpoint->disconnected_0(this, impl);
  }

  unsigned generation() const { return m_generation; }
  bool txPending() const { return m_txActive; }
  uint64_t txQueued() const { return m_txQueue.count_(); }

  bool sendPkt(
	ZmRef<ZiIOBuf> buf, ZiSockAddr addr,
	EcnMark::T ecn = EcnMark::NotECT, bool priority = false) {
    if (m_closing.load_()) {
	m_endpoint->txDropped_();
	return false;
    }
    if (!buf) {
	m_endpoint->txDropped_();
	return false;
    }
	ZiAssert(m_endpoint->endpointTxInvoked_(),
	"Zquic", (), "QUIC endpoint send outside Tx thread", return false);
    if (m_txActive) return enqueueTx_(ZuMv(buf), ZuMv(addr), ecn, priority);
	m_endpoint->txSubmitted_(buf->length);
    m_txActive = true;
    m_txBuf = ZuMv(buf);
    m_txAddr = ZuMv(addr);
    m_txECN = ecn;
    send(ZiTxFn{this, ZmFnPtr<&EndpointCxn_::sendStart_>{}});
    return true;
  }

private:
  void beginCloseRx_() {
    m_closing = true;
  }

  void closeTx_() {
    m_closing = true;
    m_txBuf = nullptr;
    m_txActive = false;
    m_txECN = EcnMark::NotECT;
    m_txQueue.clean();
  }

  void closeRx_() {
    m_closing = true;
  }

  bool enqueueTx_(
	ZmRef<ZiIOBuf> buf, ZiSockAddr addr, EcnMark::T ecn,
	bool priority = false) {
    if (m_txQueue.count_() >= EndpointTxQueueLimit)
	m_endpoint->txBackPressure_();
    m_endpoint->txSubmitted_(buf->length);
    if (priority)
	m_txQueue.unshiftNode(new TxNode{ZuMv(buf), ZuMv(addr), ecn});
    else
	m_txQueue.pushNode(new TxNode{ZuMv(buf), ZuMv(addr), ecn});
    return true;
  }

  bool dequeueTx_() {
    auto node = m_txQueue.shift();
    if (!node) return false;
    m_txBuf = ZuMv(node->buf);
    m_txAddr = ZuMv(node->addr);
    m_txECN = node->ecn;
    return true;
  }

  bool recvDone_(ZiRxContext &io) {
    if (io.buf && io.buf->length) {
	// Snapshot recvmsg metadata before transferring the provided slot.
	auto addr = io.addr;
	auto ecn = ecnMarkFromTOS(io.tos);
	m_endpoint->received_(Datagram{ZuMv(io.buf), ZuMv(addr), ecn});
    }
    if (m_closing.load_()) io.complete();
    return true;
  }

  void armRecv_(ZiRxContext &io) {
    io.init(ZiRxFn{this, ZmFnPtr<&EndpointCxn_::recvDone_>{}});
  }

  bool sendStart_(ZiTxContext &io) {
    if (!m_txBuf) {
	io.complete();
	return true;
    }
    m_txActive = true;
    m_txBytes = m_txBuf->length;
    io.init(
	m_sendDone,
	ZuMv(m_txBuf), m_txAddr, ecnTOS(m_txECN));
    return true;
  }

  template <bool Native>
  bool sendDone_(ZiTxContext &io) {
    // Native next-output callbacks run at data completion, independently of
    // the later buffer-bearing notification retirement callback.
    if constexpr (Native) if (io.buf) {
	if (io.length < 0 && !(io.flags & ZiTxContext::TxDataDone))
	  m_endpoint->ioError_();
	io.complete();
	return true;
    }
    if (io.length < 0)
	m_endpoint->ioError_();
    else if (m_txActive)
	m_endpoint->sent_(m_txBytes);
    m_txActive = false;
    if (dequeueTx_()) {
	// Drain callbacks may send synchronously; publish the selected packet
	// first so reentrant sends queue behind it.
	sendStart_(io);
	m_endpoint->txDrained_();
	return true;
    }
    m_txECN = EcnMark::NotECT;
    io.complete();
    return true;
  }

private:
  // shared: stable after construction
  Endpoint_		*m_endpoint = nullptr;
  unsigned		m_generation = 0;

  // exceptional atomic close handoff shared by Rx and Tx
  ZmAtomic<unsigned>	m_closing = 0;

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
  ZiTxFn		m_sendDone; // initialized before socket publication
  ZmRef<ZiIOBuf>	m_txBuf; // pending selection only; active output belongs to io
  unsigned		m_txBytes = 0;
  bool			m_txActive = false;
  ZiSockAddr		m_txAddr;
  EcnMark::T		m_txECN = EcnMark::NotECT;
  TxQueue		m_txQueue;
};

template <typename Impl_>
class Endpoint_ {
  template <typename> friend class EndpointCxn_;

  using Cxn_ = EndpointCxn_<Endpoint_>;

public:
  using Impl = Impl_;
  using TxPktAllocator = PktTxBufAllocator<>;
  using CxnRef = ZmRef<Cxn_>;

  Endpoint_() = default;
  ~Endpoint_() {
    ZiAssert(!m_open && !m_ioCxns && !m_unbinding, "Zquic", (),
      "QUIC endpoint destroyed before finalization", return);
  }

  Endpoint_(const Endpoint_ &) = delete;
  Endpoint_ &operator =(const Endpoint_ &) = delete;

  bool init(ZiMultiplex *mx) {
    ZiAssert(mx, "Zquic", (), "null endpoint multiplexer", return false);
    ZiAssert(mx->running(), "Zquic", (),
      "endpoint multiplexer is not running", return false);
    if (m_mx) return m_mx == mx;
    m_mx = mx;
    m_txBufAllocator = new TxPktAllocator{mx->txRingSize()};
    return true;
  }

  bool openUDP(
      PathMode::T mode,
      ZiIP localIP, uint16_t localPort,
      ZiIP remoteIP = {}, uint16_t remotePort = 0)
  {
    ZiAssert(m_mx, "Zquic", (), "endpoint multiplexer is not initialized",
      return false);
    ZiMultiplex *mx = m_mx;
    ZiAssert(mx->running(), "Zquic", (),
      "endpoint multiplexer is not running", return false);
    if (m_open || m_cxn || m_ioCxns || m_unbinding) return false;

    m_mode = mode;
    m_local.init(localIP, localPort);
    if (!!remoteIP) m_remote.init(remoteIP, remotePort); else m_remote.null();
    IPFamily::T family = IPFamily::IPv4;
    if (localIP.v6() || remoteIP.v6()) family = IPFamily::IPv6;
    m_sockConfig = SockConfig{family, mode, true, true};
    m_sockDiag = {};
    m_listening = false;
    m_open = true;
    ++m_generation;

    m_mx->txInvoke([this]() mutable {
      m_txDiag = {};
    });

    ZiCxnOptions options;
    options.udp(true);

    m_mx->udp(
      ZiConnectFn{this, [](Endpoint_ *self, const ZiCxnInfo &ci) -> ZiConnection * {
	auto cxn = new Cxn_{self, ci, self->m_generation};
	self->m_cxn = cxn;
	return cxn;
      }},
      ZiFailFn{this, [](Endpoint_ *self, bool transient) {
	self->failed_(transient);
      }},
      localIP, localPort,
      mode == PathMode::ClientConnected ? remoteIP : ZiIP{},
      mode == PathMode::ClientConnected ? remotePort : 0,
      options);

    return true;
  }

  template <typename L>
  bool rebindUDP(
      ZiIP localIP, uint16_t localPort,
      ZiIP remoteIP, uint16_t remotePort,
      L &&l) {
    EndpointRebindFn fn{ZuFwd<L>(l)};
    if (!m_mx) {
      fn(false, ZiSockAddr{}, false);
      return false;
    }
    if (!m_mx->running()) {
      fn(false, ZiSockAddr{}, false);
      return false;
    }
    if (endpointRxInvoked_())
      return rebindUDP_(localIP, localPort, remoteIP, remotePort, ZuMv(fn));
    m_mx->rxRun([
      this,
      localIP,
      localPort,
      remoteIP,
      remotePort,
      fn = ZuMv(fn)
    ]() mutable {
      rebindUDP_(localIP, localPort, remoteIP, remotePort, ZuMv(fn));
    });
    return true;
  }

private:
  bool rebindUDP_(
      ZiIP localIP, uint16_t localPort,
      ZiIP remoteIP, uint16_t remotePort,
      EndpointRebindFn fn) {
    if (!m_mx || !m_open || !m_cxn || m_rebindCxn || m_rebinding) {
      fn(false, ZiSockAddr{}, false);
      return false;
    }
    if (!m_mx->running()) {
      fn(false, ZiSockAddr{}, false);
      return false;
    }
    IPFamily::T family = IPFamily::IPv4;
    if (localIP.v6() || remoteIP.v6()) family = IPFamily::IPv6;
    m_rebindLocal.init(localIP, localPort);
    if (!!remoteIP)
      m_rebindRemote.init(remoteIP, remotePort);
    else
      m_rebindRemote.null();
    m_rebindSockConfig = SockConfig{family, m_mode, true, true};
    m_rebindGeneration = m_generation + 1;
    m_rebindFn = ZuMv(fn);
    m_rebinding = true;

    ZiCxnOptions options;
    options.udp(true);
    m_mx->udp(
      ZiConnectFn{this, [](Endpoint_ *self, const ZiCxnInfo &ci) ->
	  ZiConnection * {
	if (!self->m_rebinding) return nullptr;
	auto cxn = new Cxn_{self, ci, self->m_rebindGeneration};
	self->m_rebindCxn = cxn;
	return cxn;
      }},
      ZiFailFn{this, [](Endpoint_ *self, bool) {
	self->failedRebind_(false);
      }},
      localIP, localPort,
      m_mode == PathMode::ClientConnected ? remoteIP : ZiIP{},
      m_mode == PathMode::ClientConnected ? remotePort : 0,
      options);
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
    if (!m_mx) {
      m_listening = false;
      m_connected = false;
      m_open = false;
      l();
      return;
    }
    if (endpointRxInvoked_()) {
      disconnectRx_(EndpointDiscFn{ZuFwd<L>(l)});
      return;
    }
    m_mx->rxRun([this, l = ZuFwd<L>(l)]() mutable {
      disconnectRx_(EndpointDiscFn{ZuMv(l)});
    });
  }

  bool listening() const { return m_listening; }
  bool connected() const { return m_connected; }
  const ZiSockAddr &local() const { return m_local; }
  const ZiSockAddr &remote() const { return m_remote; }
  PathMode::T mode() const { return m_mode; }
  unsigned generation() const { return m_generation; }
  EndpointDiag diag() const {
    uint64_t txPending = 0;
    uint64_t txQueued = 0;
    CxnRef cxn = m_cxn;
    if (cxn && cxn->generation() == m_generation) {
      txPending = cxn->txPending();
      txQueued = cxn->txQueued();
    }
    return {m_rxDiag, m_txDiag, txPending, txQueued};
  }
  SockDiag sockDiag() const { return m_sockDiag; }
  void failure() { ++m_rxDiag.failures; }
  ZmRef<ZiIOBuf> allocTxPkt() { return m_txBufAllocator->alloc(this, 0); }

  bool send(
      ZmRef<ZiIOBuf> buf, ZiSockAddr addr,
      EcnMark::T ecn = EcnMark::NotECT, bool priority = false) {
    if (!buf) return false;
    ZiAssert(m_mx, "Zquic", (), "null endpoint multiplexer", return false);
    if (endpointTxInvoked_())
      return send_(ZuMv(buf), ZuMv(addr), ecn, true, priority);
    m_mx->txRun([
      this,
      buf = ZuMv(buf),
      addr = ZuMv(addr),
      ecn,
      priority
    ]() mutable {
      (void)send_(ZuMv(buf), ZuMv(addr), ecn, false, priority);
    });
    return true;
  }

private:
  void connected_(Cxn_ *cxn, ZiRxContext &io) {
    bool rebind = false;
    CxnRef oldCxn;
    if (m_rebindCxn == cxn && cxn->generation() == m_rebindGeneration) {
      rebind = true;
      oldCxn = ZuMv(m_cxn);
      m_cxn = cxn;
      m_generation = m_rebindGeneration;
      m_sockConfig = m_rebindSockConfig;
      m_sockDiag = {};
      m_local = m_rebindLocal;
      m_remote = m_rebindRemote;
      m_rebindCxn = nullptr;
      m_rebinding = false;
    } else if (m_cxn != cxn || cxn->generation() != m_generation)
      return;

    Sock::initUDP(cxn->info().socket, m_sockConfig, &m_sockDiag);

#ifndef _WIN32
    {
      ZiSockAddr local;
      local.init(
	m_sockConfig.family == IPFamily::IPv6 ? ZiIPType::V6 : ZiIPType::V4);
      socklen_t len = local.len();
      if (::getsockname(cxn->info().socket, local.sa(), &len) == 0) {
	local.sync();
	m_local = local;
      }
    }
#endif

    m_listening = true;
    m_connected = true;
    cxn->armRecv_(io);
    impl()->endpointReady_(this);
    if (rebind) {
      closeCxn_(ZuMv(oldCxn));
      EndpointRebindFn fn = ZuMv(m_rebindFn);
      if (fn) fn(true, m_local, false);
    }
  }

  void cxnAdded_() { ++m_ioCxns; }

  template <typename ImplRef_>
  void disconnected_0(Cxn_ *cxn, ImplRef_ impl_) {
    m_mx->rxRun([impl = ZuMvPtr(impl_), cxn = ZmRef(cxn)]() mutable {
      auto self = static_cast<Endpoint_ *>(static_cast<Impl *>(impl));
      --self->m_ioCxns;
      self->disconnected_(cxn.ptr());
      if (!self->m_ioCxns && !self->m_cxn && !self->m_rebindCxn) {
	self->unbindEndpoint_(ZuMvPtr(impl), ZuMv(cxn));
	return;
      }
      self->m_mx->txRun([
	impl = ZuMvPtr(impl), cxn = ZuMv(cxn)
      ]() mutable {
	if constexpr (Impl::EndpointRef) impl = nullptr;
	cxn = nullptr;
      });
    });
  }

  void disconnected_(Cxn_ *cxn) {
    if (m_cxn == cxn && cxn->generation() == m_generation) {
      cxn->closeRx_();
      m_cxn = nullptr;
      m_listening = false;
      m_connected = false;
      m_open = false;
      m_downPending = true;
    }
  }

  template <typename ImplRef_>
  void unbindEndpoint_(ImplRef_ impl_, CxnRef cxn = {}) {
    m_unbinding = true;
    // The endpoint outlives every connection (including obsolete rebinds) and
    // its allocator controls. No destructor dispatches into a live I/O loop.
    m_mx->unbind(m_txBufAllocator.ptr(), {[
      impl = ZuMvPtr(impl_), cxn = ZuMv(cxn)
    ]() mutable {
      auto self = static_cast<Endpoint_ *>(static_cast<Impl *>(impl));
      self->m_mx->rxRun([
	impl = ZuMvPtr(impl), cxn = ZuMv(cxn)
      ]() mutable {
	auto self = static_cast<Endpoint_ *>(static_cast<Impl *>(impl));
	self->m_unbinding = false;
	if (self->m_downPending) {
	  self->m_downPending = false;
	  self->impl()->endpointDown_(self);
	}
	auto fns = self->takeDiscFns_();
	self->m_mx->txRun([
	  impl = ZuMvPtr(impl), cxn = ZuMv(cxn), fns = ZuMv(fns)
	]() mutable {
	  if constexpr (Impl::EndpointRef) impl = nullptr;
	  cxn = nullptr;
	  Endpoint_::runDiscFns_(fns);
	});
      });
    }});
  }

  void failed_(bool transient) {
    ++m_rxDiag.failures;
    m_listening = false;
    m_connected = false;
    m_open = false;
    m_cxn = nullptr;
    impl()->endpointFailed_(transient);
    impl()->endpointDown_(this);
  }

  void failedRebind_(bool success) {
    if (!m_rebinding) return;
    m_rebindCxn = nullptr;
    m_rebinding = false;
    ++m_rxDiag.failures;
    EndpointRebindFn fn = ZuMv(m_rebindFn);
    if (fn) fn(success, ZiSockAddr{}, false);
  }

  void received_(Datagram datagram) {
    ++m_rxDiag.datagramsRx;
    if (datagram.buf) m_rxDiag.bytesRx += datagram.buf->length;
    impl()->endpointDatagram_(ZuMv(datagram));
  }

  void sent_(unsigned bytes) {
    ++m_txDiag.datagramsTx;
    m_txDiag.bytesTx += bytes;
  }

  void txDrained_() {
    impl()->endpointTxDrained_();
  }

  void ioError_() {
    if (endpointTxInvoked_())
      ++m_txDiag.failures;
    else
      ++m_rxDiag.failures;
  }

  bool send_(
      ZmRef<ZiIOBuf> buf, ZiSockAddr addr, EcnMark::T ecn, bool direct,
      bool priority = false) {
    ZiAssert(endpointTxInvoked_(), "Zquic", (),
      "QUIC endpoint send outside Tx thread", return false);
    ++m_txDiag.sendCalls;
    if (direct)
      ++m_txDiag.directCalls;
    else
      ++m_txDiag.asyncCalls;
    CxnRef cxn = m_cxn;
    if (!cxn) {
      ++m_txDiag.txDropped;
      return false;
    }
    if (cxn->generation() != m_generation) {
      ++m_txDiag.txDropped;
      return false;
    }
    return cxn->sendPkt(ZuMv(buf), ZuMv(addr), ecn, priority);
  }

  void disconnectRx_(EndpointDiscFn fn) {
    m_discFns.push(ZuMv(fn));
    m_listening = false;
    m_connected = false;
    m_open = false;
    EndpointRebindFn rebindFn;
    if (m_rebinding) rebindFn = ZuMv(m_rebindFn);
    m_rebinding = false;
    closeCxn_(ZuMv(m_rebindCxn));
    if (rebindFn) rebindFn(false, ZiSockAddr{}, true);
    CxnRef cxn = m_cxn;
    if (!cxn) {
      discFns_();
      return;
    }
    closeCxn_(ZuMv(cxn));
  }
  EndpointDiscFns takeDiscFns_() {
    auto fns = ZuMv(m_discFns);
    m_discFns.clean();
    return fns;
  }
  void discFns_() {
    if (m_ioCxns || m_unbinding) return;
    if constexpr (Impl::EndpointRef)
      unbindEndpoint_(ZmRef(impl()));
    else
      unbindEndpoint_(impl());
  }
  static void runDiscFns_(EndpointDiscFns &fns) {
    while (auto fn = fns.shift()) fn();
  }
  void closeCxn_(CxnRef cxn) {
    if (!cxn) return;
    cxn->beginCloseRx_();
    m_mx->txRun([cxn = ZuMv(cxn)]() mutable {
      cxn->closeTx_();
      cxn->disconnect();
    });
  }

  bool endpointRxInvoked_() const {
    return m_mx && m_mx->invoked(m_mx->rxThread());
  }
  bool endpointTxInvoked_() const {
    return m_mx && m_mx->invoked(m_mx->txThread());
  }
  ZiMultiplex *mx_() const { return m_mx; }
  void txDropped_() { ++m_txDiag.txDropped; }
  void txSubmitted_(unsigned bytes) {
    ++m_txDiag.submittedTx;
    m_txDiag.submittedBytes += bytes;
  }
  void txBackPressure_() { ++m_txDiag.txBackPressure; }
  const Impl *impl() const { return static_cast<const Impl *>(this); }
  Impl *impl() { return static_cast<Impl *>(this); }

  // shared: stable after init()
  ZiMultiplex		*m_mx = nullptr;
  ZmRef<Zi::IOBufAllocator> m_txBufAllocator;

  // shared read-mostly active connection identity; mutation is Rx-owned
  CxnRef		m_cxn;
  unsigned		m_generation = 0;

  // shared telemetry
  EndpointRxDiag	m_rxDiag;
  EndpointTxDiag	m_txDiag;

  // Shared advisory status snapshots; protocol decisions stay on Rx/Tx owners.
  PathMode::T		m_mode = PathMode::ServerUnconnected;
  ZiSockAddr		m_local;
  ZiSockAddr		m_remote;
  bool			m_connected = false;
  bool			m_listening = false;
  bool			m_open = false;

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  SockConfig		m_sockConfig;
  CxnRef		m_rebindCxn;
  EndpointDiscFns	m_discFns;
  unsigned		m_rebindGeneration = 0;
  unsigned		m_ioCxns = 0;
  bool			m_unbinding = false;
  bool			m_downPending = false;
  EndpointRebindFn	m_rebindFn;
  ZiSockAddr		m_rebindLocal;
  ZiSockAddr		m_rebindRemote;
  SockConfig		m_rebindSockConfig;
  SockDiag		m_sockDiag;
  bool			m_rebinding = false;
};

} // namespace Zquic_
