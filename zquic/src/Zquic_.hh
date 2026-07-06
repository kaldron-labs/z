//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC private implementation helpers

#ifndef Zquic_HH
#error "include zlib/Zquic.hh before this header"
#endif

namespace Zquic_ {

template <typename Link_>
struct Cxn : public ZuObject {
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
inline const CxnID &Cxn_IDAxor(const Cxn<Link_> *cxn) { return cxn->id; }

template <typename Link_>
ZuDerive(CxnRoutes_,
  (ZmHash<ZmRef<Cxn<Link_>>,
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
    if (auto route = m_routes->findVal(id)) {
      if (route->state == CxnState::Tombstone) return false;
      route->sequence = sequence;
      route->link = link;
      route->resetToken = resetToken;
      route->state = CxnState::Active;
      return true;
    }
    m_routes->add(new Route{id, sequence, link, resetToken, CxnState::Active});
    return true;
  }

  Link_ *find(const CxnID &id) const {
    auto route = m_routes->findVal(id);
    if (!route || route->state != CxnState::Active) return nullptr;
    return route->link;
  }
  Link_ *matchShort(ZuBSpan packet, CxnID *id = nullptr) const {
    const Route *route = matchShortRoute_(packet, true);
    if (!route) return nullptr;
    if (id) *id = route->id;
    return route->link;
  }
  bool resetToken(const CxnID &id, ResetToken &token) const {
    auto route = m_routes->findVal(id);
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
    auto route = m_routes->findVal(id);
    if (!route || route->state != CxnState::Active) return false;
    m_routes->del(id);
    return true;
  }

  bool tombstone(const CxnID &id) {
    if (!id) return false;
    if (auto route = m_routes->findVal(id)) {
      if (route->state == CxnState::Tombstone) return true;
      route->state = CxnState::Tombstone;
      route->link = nullptr;
      route->resetToken = {};
      m_tombstones.push(id);
      trimTombstones_();
      return true;
    }
    auto route = new Route{id, 0, nullptr, {}, CxnState::Tombstone};
    m_routes->add(route);
    m_tombstones.push(id);
    trimTombstones_();
    return true;
  }

  CxnState::T state(const CxnID &id) const {
    auto route = m_routes->findVal(id);
    return route ? route->state : CxnState::Tombstone;
  }

  void clear() {
    if (!m_routes) return;
    m_routes->clean();
    m_tombstones.clean();
  }
  void final() {
    clear();
    m_routes = nullptr;
  }

  unsigned count() const { return m_routes->count_(); }
  unsigned tombstoneCount() const { return m_tombstones.count_(); }
  unsigned active() const {
    unsigned n = 0;
    all([&n](const Route &) { ++n; });
    return n;
  }
  template <typename L>
  void all(L &&l) const {
    auto i = m_routes->citer();
    while (auto node = i()) {
      const auto &route = *node->val();
      if (route.state == CxnState::Active) l(route);
    }
  }

private:
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
    while (max) {
      CxnID id{ZuBSpan{packet.data() + 1, max}};
      if (auto route = m_routes->findVal(id)) {
	if (route->state != CxnState::Tombstone &&
	    (!activeOnly || route->state == CxnState::Active))
	  return route;
      }
      --max;
    }
    return nullptr;
  }

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  ZmRef<Routes>	m_routes;
  Tombstones	m_tombstones{ZmQueueParams{}.initial(TombstoneMax)};
};

template <typename Link_>
ZuDerive(ServerLinks_,
  (ZmHash<ZmRef<Link_>,
    ZmHashHeapID<"Zquic.Server.LinkHash">>));

using EndpointDiscFn = ZmFn<void(), ZmFnHeapID<"Zquic.Endpoint.DiscFn">>;
ZuDerive(EndpointDiscFns,
  (ZmQueue<EndpointDiscFn, ZmQueueHeapID<"Zquic.Endpoint.DiscFns">>));

template <typename Impl_>
class Endpoint_ {
  class Cxn_ : public ZiConnection {
  friend Endpoint_;

  public:
    static constexpr unsigned EndpointTxQueueLimit = 4096;

    struct TxNode : public ZuObject {
      TxNode() = default;
      TxNode(ZmRef<ZiIOBuf> buf_, ZiSockAddr addr_, EcnMark::T ecn_) :
	buf{ZuMv(buf_)}, addr{ZuMv(addr_)}, ecn{ecn_} { }

      ZmRef<ZiIOBuf>	buf;
      ZiSockAddr		addr;
      EcnMark::T		ecn = EcnMark::NotECT;
    };
    ZuDerive(TxQueue,
      (ZmList<ZmRef<TxNode>,
	ZmListNode<ZmRef<TxNode>,
	  ZmListHeapID<"Zquic.Endpoint.TxQueue">>>));

    void *operator new(size_t s) {
      using Heap = ZmHeap<"Zquic.Endpoint.Cxn", Cxn_>;
      return Heap::operator new(s);
    }
    void operator delete(void *p) noexcept {
      using Heap = ZmHeap<"Zquic.Endpoint.Cxn", Cxn_>;
      Heap::operator delete(p);
    }

    Cxn_(Endpoint_ *endpoint, const ZiCxnInfo &ci, unsigned generation) :
	ZiConnection(endpoint->m_mx, ci),
	m_endpoint{endpoint},
	m_generation{generation} { }

    void connected(ZiIOContext &io) override {
      m_endpoint->connected_(this, io);
    }
    void disconnected(bool) override {
      auto impl = m_endpoint->impl();
      if constexpr (Impl::EndpointRef)
	m_endpoint->disconnected_0(this, ZmMkRef(impl));
      else
	m_endpoint->disconnected_0(this, impl);
    }

    unsigned generation() const { return m_generation; }
    bool txPending() const { return !!m_txBuf; }
    uint64_t txQueued() const { return m_txQueue.count_(); }

    bool sendPkt(
	ZmRef<ZiIOBuf> buf, ZiSockAddr addr,
	EcnMark::T ecn = EcnMark::NotECT) {
      if (m_closing.load_()) {
	++m_endpoint->m_txDiag.txDropped;
	return false;
      }
      if (!buf) {
	++m_endpoint->m_txDiag.txDropped;
	return false;
      }
      ZiAssert(m_endpoint->m_mx &&
	  m_endpoint->m_mx->invoked(m_endpoint->m_mx->txThread()),
	"Zquic", (), "QUIC endpoint send outside Tx thread", return false);
      if (m_txBuf) return enqueueTx_(ZuMv(buf), ZuMv(addr), ecn);
      ++m_endpoint->m_txDiag.submittedTx;
      m_endpoint->m_txDiag.submittedBytes += buf->length;
      m_txBuf = ZuMv(buf);
      m_txAddr = ZuMv(addr);
      m_txECN = ecn;
      send(ZiIOFn{this, ZmFnPtr<&Cxn_::sendStart_>{}});
      return true;
    }

  private:
    void beginCloseRx_() {
      m_closing = true;
    }

    void closeTx_() {
      m_closing = true;
      m_txBuf = nullptr;
      m_txECN = EcnMark::NotECT;
      m_txQueue.clean();
    }

    void closeRx_() {
      m_closing = true;
      m_rxBuf = nullptr;
    }

    bool enqueueTx_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr, EcnMark::T ecn) {
      if (m_txQueue.count_() >= EndpointTxQueueLimit)
	++m_endpoint->m_txDiag.txBackPressure;
      ++m_endpoint->m_txDiag.submittedTx;
      m_endpoint->m_txDiag.submittedBytes += buf->length;
      m_txQueue.push(new TxNode{ZuMv(buf), ZuMv(addr), ecn});
      return true;
    }

    bool dequeueTx_() {
      auto node = m_txQueue.shiftVal();
      if (!node) return false;
      m_txBuf = ZuMv(node->buf);
      m_txAddr = ZuMv(node->addr);
      m_txECN = node->ecn;
      return true;
    }

    void scheduleTx_() {
      m_endpoint->m_mx->txRun([cxn = ZmMkRef(this)]() mutable {
	if (cxn->m_closing.load_() || !cxn->m_txBuf) return;
	cxn->send(ZiIOFn{cxn.ptr(), ZmFnPtr<&Cxn_::sendStart_>{}});
      });
    }

    bool recvDone_(ZiIOContext &io) {
      if (io.length < 0) {
	m_endpoint->ioError_();
	io.disconnect();
	return true;
      }
      if (io.length > 0 && m_rxBuf) {
	m_rxBuf->skip = 0;
	m_rxBuf->length = unsigned(io.length);
	auto buf = ZuMv(m_rxBuf);
	m_endpoint->received_(
	  Datagram{ZuMv(buf), io.addr, ecnMarkFromTOS(io.tos)});
      }
      if (m_closing.load_()) {
	io.complete();
	return true;
      }
      armRecv_(io);
      return true;
    }

    void armRecv_(ZiIOContext &io) {
      m_rxBuf = m_endpoint->allocRxPkt_();
      io.init(
	ZiIOFn{this, ZmFnPtr<&Cxn_::recvDone_>{}},
	m_rxBuf->data_(), m_rxBuf->size, 0);
    }

    bool sendStart_(ZiIOContext &io) {
      if (!m_txBuf) {
	io.complete();
	return true;
      }
      io.init(
	ZiIOFn{this, ZmFnPtr<&Cxn_::sendDone_>{}},
	m_txBuf->data(), m_txBuf->length, 0, m_txAddr, ecnTOS(m_txECN));
      return true;
    }

    bool sendDone_(ZiIOContext &io) {
      if (io.length < 0) {
	m_endpoint->ioError_();
	bool hadQueue = m_txQueue.count_();
	m_txBuf = nullptr;
	if (dequeueTx_()) {
	  if (hadQueue) m_endpoint->txDrained_();
	  io.complete();
	  scheduleTx_();
	  return true;
	}
	if (hadQueue) m_endpoint->txDrained_();
	io.complete();
	return true;
      }
      if ((io.offset += io.length) < io.size) return true;
      bool hadQueue = m_txQueue.count_();
      m_endpoint->sent_(io.size);
      m_txBuf = nullptr;
      if (dequeueTx_()) {
	if (hadQueue) m_endpoint->txDrained_();
	io.complete();
	scheduleTx_();
	return true;
      }
      m_txECN = EcnMark::NotECT;
      if (hadQueue) m_endpoint->txDrained_();
      io.complete();
      return true;
    }

  private:
    Endpoint_		*m_endpoint = nullptr;
    unsigned		m_generation = 0;
    ZmAtomic<unsigned>	m_closing = 0;

    ZmRef<ZiIOBuf>	m_rxBuf;

    ZmRef<ZiIOBuf>	m_txBuf;
    ZiSockAddr		m_txAddr;
    EcnMark::T		m_txECN = EcnMark::NotECT;
    TxQueue		m_txQueue;
  };

public:
  using Impl = Impl_;
  using RxPktAlloc = PktRxBufAlloc<>;
  using TxPktAlloc = PktTxBufAlloc<>;
  using CxnRef = ZmRef<Cxn_>;

  Endpoint_() = default;
  ~Endpoint_() {
    if (!m_open) return;
    ZiAssert(false, "Zquic", (),
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
    if (m_open || m_cxn) return false;

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
  ZmRef<ZiIOBuf> allocTxPkt() { return new TxPktAlloc{this}; }

  bool send(
      ZmRef<ZiIOBuf> buf, ZiSockAddr addr,
      EcnMark::T ecn = EcnMark::NotECT) {
    if (!buf) return false;
    ZiAssert(m_mx, "Zquic", (), "null endpoint multiplexer", return false);
    if (endpointTxInvoked_())
      return send_(ZuMv(buf), ZuMv(addr), ecn, true);
    m_mx->txRun([
      this,
      buf = ZuMv(buf),
      addr = ZuMv(addr),
      ecn
    ]() mutable {
      (void)send_(ZuMv(buf), ZuMv(addr), ecn, false);
    });
    return true;
  }

private:
  void connected_(Cxn_ *cxn, ZiIOContext &io) {
    if (m_cxn != cxn || cxn->generation() != m_generation) return;

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
  }

  template <typename ImplRef_>
  void disconnected_0(Cxn_ *cxn, ImplRef_ impl_) {
    m_mx->rxRun([impl = ZuMv(impl_), cxn = ZmMkRef(cxn)]() mutable {
      auto fns = static_cast<Impl *>(impl)->
	Endpoint_::disconnected_(cxn.ptr());
      auto mx = cxn->mx();
      mx->txRun([
	impl = ZuMv(impl), cxn = ZuMv(cxn), fns = ZuMv(fns)
      ]() mutable {
	if constexpr (Impl::EndpointRef) impl = nullptr;
	cxn = nullptr;
	Endpoint_::runDiscFns_(fns);
      });
    });
  }

  EndpointDiscFns disconnected_(Cxn_ *cxn) {
    if (m_cxn == cxn && cxn->generation() == m_generation) {
      cxn->closeRx_();
      m_cxn = nullptr;
      m_listening = false;
      m_connected = false;
      m_open = false;
      impl()->endpointDown_(this);
      return takeDiscFns_();
    }
    if (!m_cxn) return takeDiscFns_();
    return {};
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
      ZmRef<ZiIOBuf> buf, ZiSockAddr addr, EcnMark::T ecn, bool direct) {
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
    return cxn->sendPkt(ZuMv(buf), ZuMv(addr), ecn);
  }

  void disconnectRx_(EndpointDiscFn fn) {
    m_discFns.push(ZuMv(fn));
    m_listening = false;
    m_connected = false;
    m_open = false;
    CxnRef cxn = m_cxn;
    if (!cxn) {
      discFns_();
      return;
    }
    cxn->beginCloseRx_();
    m_mx->txRun([cxn = ZuMv(cxn)]() mutable {
      cxn->closeTx_();
      cxn->disconnect();
    });
  }
  EndpointDiscFns takeDiscFns_() {
    auto fns = ZuMv(m_discFns);
    m_discFns.clean();
    return fns;
  }
  void discFns_() {
    auto fns = takeDiscFns_();
    m_mx->txRun([fns = ZuMv(fns)]() mutable {
      Endpoint_::runDiscFns_(fns);
    });
  }
  static void runDiscFns_(EndpointDiscFns &fns) {
    while (auto fn = fns.shift()) fn();
  }

  bool endpointRxInvoked_() const {
    return m_mx && m_mx->invoked(m_mx->rxThread());
  }
  bool endpointTxInvoked_() const {
    return m_mx && m_mx->invoked(m_mx->txThread());
  }
  ZmRef<ZiIOBuf> allocRxPkt_() { return new RxPktAlloc{this}; }
  const Impl *impl() const { return static_cast<const Impl *>(this); }
  Impl *impl() { return static_cast<Impl *>(this); }

  ZiMultiplex		*m_mx = nullptr;
  PathMode::T		m_mode = PathMode::ServerUnconnected;
  ZiSockAddr		m_local;
  ZiSockAddr		m_remote;
  SockConfig		m_sockConfig;

  CxnRef		m_cxn;
  EndpointDiscFns	m_discFns;
  unsigned		m_generation = 0;
  EndpointRxDiag	m_rxDiag;
  SockDiag		m_sockDiag;

  EndpointTxDiag	m_txDiag;

  // Advisory status snapshots; protocol decisions stay on Rx/Tx owners.
  bool			m_connected = false;
  bool			m_listening = false;
  bool			m_open = false;
};

} // namespace Zquic_
