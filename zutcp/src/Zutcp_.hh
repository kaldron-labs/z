//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Native TCP endpoint and tuple routing helpers

#ifndef Zutcp_HH
#error "include zlib/Zutcp.hh before this header"
#endif

#include <zlib/ZutcpMx.hh>

namespace Zutcp_ {

template <typename Link_>
struct TupleRoute {
  ClosedTupleKey tuple;
  Link_ *link = nullptr;
};

template <typename Link_>
inline const ClosedTupleKey &tupleRouteKey(const TupleRoute<Link_> &route) {
  return route.tuple;
}

ZmHashDeriveT((Link_), TupleRoutes, TupleRoute<Link_>,
  (ZmHashNode<TupleRoute<Link_>,
    ZmHashKey<tupleRouteKey<Link_>, ZmHashHeapID<"Zutcp.Endpoint.TupleRouter">>>));

// Rx-owned lookup only. The existing ServerLinks membership owns each link.
template <typename Link_>
class TupleRouter {
public:
  TupleRouter() { init(); }
  void init() {
    if (!m_routes)
      m_routes = new Routes{ZmHashParams().bits(5).loadFactor(1).cBits(3)};
  }
  bool add(const ClosedTupleKey &tuple, Link_ *link) {
    if (!link || !tuple.template p<0>().port() || !tuple.template p<1>().port()) return false;
    if (auto route = m_routes->findPtr(tuple)) return route->data().link == link;
    m_routes->add(TupleRoute<Link_>{tuple, link});
    return true;
  }
  Link_ *find(const ClosedTupleKey &tuple) const {
    auto route = m_routes->findPtr(tuple);
    return route ? route->data().link : nullptr;
  }
  bool retire(const ClosedTupleKey &tuple, Link_ *link) {
    auto route = m_routes->findPtr(tuple);
    if (!route || route->data().link != link) return false;
    m_routes->del(tuple);
    return true;
  }
  unsigned count() const { return m_routes->count_(); }
  void clear() { m_routes->clean(); }

private:
  using Routes = TupleRoutes<Link_>;
  ZmRef<Routes> m_routes;
};

struct EndpointLink : public ZmPolymorph {
  ZiConnection *mxCxn = nullptr;
  // Cold observation links; the native transport retains ownership.
  EndpointLink *observedNext = nullptr;
  EndpointLink **observedPrev = nullptr;
};

inline const EndpointLink *EndpointLinkKey(const EndpointLink &link) {
  return &link;
}

// Endpoint membership owns the link alongside outstanding callbacks. Concrete
// application links provide the allocator; this inner node only adds the link.
ZmHashDerive(ServerLinks, EndpointLink,
  (ZmHashNode<EndpointLink,
    ZmHashKey<EndpointLinkKey, ZmHashHeapID<"">>>));

using EndpointDiscFn = ZmFn<void(), ZmFnHeapID<"Zutcp.Endpoint.DiscFn">>;
ZmQueueDerive(EndpointDiscFns, EndpointDiscFn,
  ZmQueueHeapID<"Zutcp.Endpoint.DiscFns">);

// Native endpoint membership and submission. This owns no kernel socket or
// ZiConnection. The endpoint owner drains setup and posted Tx work before release.
template <typename Impl_>
class Endpoint_ {
public:
  using Impl = Impl_;

  Endpoint_() = default;
  ~Endpoint_() {
    ZiAssert(!m_open && !m_bound && !m_setupPending && !m_draining,
      "Zutcp", (), "endpoint destroyed before native drain", return);
  }
  Endpoint_(const Endpoint_ &) = delete;
  Endpoint_ &operator =(const Endpoint_ &) = delete;

  bool init(ZiMultiplex *mx) {
    if (!mx) return false;
    auto stack = dynamic_cast<Stack *>(mx->plugin());
    if (!stack) return false;
    if (m_mx) return m_mx == mx && m_stack == stack;
    m_mx = mx;
    m_stack = stack;
    return true;
  }

  // Rx owner. Active setup resolves its path before installing membership;
  // passive setup binds immediately and resolves each accepted peer separately.
  bool open(
    PathMode::T mode, ZiIP localIP, uint16_t localPort,
    ZiIP remoteIP = {}, uint16_t remotePort = 0)
  {
    ZiAssert(endpointRxInvoked(), "Zutcp", (), "endpoint open outside Rx", return false);
    if (!m_stack->ready() || m_open || m_draining || m_setupPending) return false;
    if (mode == PathMode::ClientConnected && (!remoteIP || !remotePort)) return false;
    if (!localIP) localIP = remoteIP.v6() ? ZiIP{in6addr_any} : ZiIP{in_addr{}};
    if (remoteIP && localIP.type() != remoteIP.type()) return false;
    m_mode = mode;
    m_tupleBound = false;
    m_family = localIP.type();
    m_payloadLimit = tcpPayload(m_stack->mtu(), m_family);
    m_local.init(localIP, localPort);
    if (remoteIP) m_remote.init(remoteIP, remotePort); else m_remote.null();
    m_rxDiag = {};
    m_gate = 0;
    m_open = true;
    unsigned generation = ++m_generation;
    ++m_setupPending;
    if (mode == PathMode::ClientConnected) {
      IPPath path{localIP, remoteIP, {}, {}, 0};
      if (!m_stack->resolvePath(path,
	  [this, generation](IPPath resolved, ZtString<> error) mutable {
	    if (!m_open || generation != unsigned(m_generation)) {
	      setupDone(); return;
	    }
	    if (!!error || !tcpPayload(resolved.mtu, resolved.local.type())) {
	      setupDone(); setupFailed(); return;
	    }
	    m_payloadLimit = tcpPayload(resolved.mtu, resolved.local.type());
	    m_local.init(resolved.local, m_local.port());
	    install(generation, resolved);
	  })) {
	--m_setupPending;
	m_open = false;
	m_gate = 1;
	return false;
      }
    } else install(generation, {});
    return true;
  }

  void disconnect() { disconnect_(EndpointDiscFn{}); }
  template <typename L> void disconnect(L &&l) { disconnect_(ZuFwd<L>(l)); }
  template <typename L> void disconnect_(L &&l) {
    if (!m_mx) { l(); return; }
    m_mx->rxInvoke([this, fn = EndpointDiscFn{ZuFwd<L>(l)}]() mutable {
      m_discFns.push(ZuMv(fn));
      if (m_draining) return;
      m_draining = true;
      m_gate = 1;
      m_downPending = m_open || m_connected;
      m_open = m_listening = m_connected = false;
      ++m_generation;
      if (m_bound) {
	m_stack->unbind(m_local, this);
	m_bound = false;
      }
      if (!m_setupPending) drainTx();
    });
  }

  bool listening() const { return m_listening; }
  bool connected() const { return m_connected; }
  const ZiSockAddr &local() const { return m_local; }
  const ZiSockAddr &remote() const { return m_remote; }
  bool tupleBound() const { return m_tupleBound; }
  PathMode::T mode() const { return m_mode; }
  unsigned generation() const { return m_generation; }
  unsigned payloadLimit() const { return m_payloadLimit; }
  EndpointDiag diag() const { return {m_rxDiag, m_txDiag, 0, 0}; }
  void failure() { ++m_rxDiag.failures; }

  ZmRef<PacketBuf> allocTxPkt(bool control = false) {
    return m_stack->allocPacket(this, m_family, control);
  }

  bool send(
    ZmRef<ZiIOBuf> buf, ZiSockAddr addr,
    EcnMark::T ecn = EcnMark::NotECT, bool priority = false,
    const IPPath *path = nullptr)
  {
    if (!buf || !m_mx || unsigned(m_gate)) return false;
    unsigned generation = m_generation;
    if (endpointTxInvoked())
      return send_(ZuMv(buf), ZuMv(addr), ecn, generation, path, true);
    // Path metadata is bounded. This post retains the submitted buffer only
    // until admission; accepted Stack output detaches its backing immediately.
    IPPath route = path ? *path : IPPath{};
    m_mx->txRun([this, buf = ZuMv(buf), addr = ZuMv(addr), route,
	supplied = bool(path), ecn, generation]() mutable {
      send_(ZuMv(buf), ZuMv(addr), ecn, generation, supplied ? &route : nullptr, false);
    });
    return true;
  }

private:
  void install(unsigned generation, IPPath path) {
    ZtString<> error;
    ZiSockAddr remote = m_mode == PathMode::ClientConnected ? m_remote : ZiSockAddr{};
    if (!m_stack->bind(m_local, this, &Endpoint_::receivePacket, remote, &error)) {
      ZiLOG(Error, "Zutcp", ([text = ZeString{error}](auto &out) { out << text; }));
      setupDone(); setupFailed(); return;
    }
    m_bound = m_tupleBound = true;
    m_mx->txRun([this, generation, path] {
      m_txDiag = {};
      m_txPath = path;
      m_txGeneration = generation;
      m_txOpen = true;
      m_mx->rxRun([this, generation] {
	bool ready = m_open && generation == unsigned(m_generation);
	setupDone();
	if (!ready) return;
	m_connected = m_listening = true;
	impl()->endpointReady(this);
      });
    });
  }

  void setupDone() {
    ZiAssert(endpointRxInvoked() && m_setupPending, "Zutcp", (),
      "unbalanced native setup completion", return);
    if (!--m_setupPending && m_draining) drainTx();
  }

  void setupFailed() {
    ++m_rxDiag.failures;
    m_gate = 1;
    m_open = m_connected = m_listening = false;
    impl()->endpointFailed(false);
  }

  static void receivePacket(
    void *owner, ZmRef<ZiIOBuf> buf, ZiSockAddr local, ZiSockAddr peer, unsigned ecn,
    const TCPHeader &tcp)
  {
    auto self = static_cast<Endpoint_ *>(owner);
    if (!self->m_open || unsigned(self->m_gate) ||
	(self->m_remote && peer != self->m_remote)) return;
    ++self->m_rxDiag.datagramsRx;
    self->m_rxDiag.bytesRx += buf->length;
    Datagram datagram{ZuMv(buf), ZuMv(peer), ecnMarkFromTOS(uint8_t(ecn))};
    datagram.local = ZuMv(local);
    datagram.tcp = tcp;
    self->impl()->endpointDatagram(ZuMv(datagram));
  }

  bool send_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr, EcnMark::T ecn,
    unsigned generation, const IPPath *supplied, bool direct)
  {
    ZiAssert(endpointTxInvoked(), "Zutcp", (), "native send outside Tx", return false);
    ++m_txDiag.sendCalls;
    if (direct) ++m_txDiag.directCalls; else ++m_txDiag.asyncCalls;
    const auto &path = supplied ? *supplied : m_txPath;
    auto header = static_cast<PacketBuf *>(buf.ptr())->tcp;
    unsigned head = tcpHead(path.local.type(), header);
    if (unsigned(m_gate) || !m_txOpen || generation != m_txGeneration ||
	!buf->registered() || !head || buf->skip != head ||
	path.remote != addr.ip() || !addr.port()) {
      ++m_txDiag.txDropped;
      return false;
    }
    unsigned bytes = buf->length;
    header.srcPort = m_local.port();
    header.dstPort = addr.port();
    unsigned wire = writeTCP(buf->data_(), buf->size, path, header, bytes, ecnBits(ecn));
    if (!wire) { ++m_txDiag.txDropped; return false; }
    buf->skip = 0;
    buf->length = wire;
    // Native endpoint output is built with PacketBuf metadata, including the
    // retained source node's borrowed wire view.
    if (!m_stack->sendPacket(buf, static_cast<PacketBuf *>(buf.ptr())->retained)) {
      buf->skip = head;
      buf->length = bytes;
      ++m_txDiag.txBackPressure;
      ++m_txDiag.txDropped;
      return false;
    }
    ++m_txDiag.submittedTx;
    m_txDiag.submittedBytes += bytes;
    ++m_txDiag.datagramsTx;
    m_txDiag.bytesTx += bytes;
    return true;
  }

  void drainTx() {
    // One lifecycle reference across the drain is permitted; packet callbacks
    // retain no endpoint owner. Server ownership is external to this helper.
    if constexpr (Impl::EndpointRef) drainTx_(ZmRef(impl()));
    else drainTx_(impl());
  }
  template <typename Owner> void drainTx_(Owner owner) {
    m_mx->txRun([this, owner = ZuMvPtr(owner)]() mutable {
      m_txOpen = false;
      m_txPath = {};
      m_mx->rxRun([this, owner = ZuMvPtr(owner)]() mutable {
	m_draining = false;
	if (m_downPending) {
	  m_downPending = false;
	  impl()->endpointDown_(this);
	}
	auto fns = ZuMv(m_discFns);
	m_discFns.clean();
	m_mx->txRun([owner = ZuMvPtr(owner), fns = ZuMv(fns)]() mutable {
	  while (auto fn = fns.shift()) if (fn) fn();
	  if constexpr (Impl::EndpointRef) owner = nullptr;
	});
      });
    });
  }

  bool endpointRxInvoked() const {
    return m_mx && m_mx->invoked(m_mx->rxThread());
  }
  bool endpointTxInvoked() const {
    return m_mx && m_mx->invoked(m_mx->txThread());
  }
  Impl *impl() { return static_cast<Impl *>(this); }

  // Stable configuration; publish before setup/packet servicing.
  ZiMultiplex *m_mx = nullptr;
  Stack *m_stack = nullptr;
  ZiIPType::T m_family = ZiIPType::Null;
  unsigned m_payloadLimit = 0;
  PathMode::T m_mode = PathMode::ServerUnconnected;
  ZiSockAddr m_local, m_remote;
  ZmAtomic<unsigned> m_generation{0}, m_gate{1};
  ZmAtomic<unsigned> m_connected{0}, m_listening{0};

  // Rx-owned setup and membership.
  alignas(Zm::CacheLineSize)
  EndpointRxDiag m_rxDiag;
  bool m_open = false, m_bound = false, m_draining = false, m_downPending = false;
  // Retain successful assignment after unbind for initialized, unpublished history.
  bool m_tupleBound = false;
  unsigned m_setupPending = 0;
  EndpointDiscFns m_discFns;
  // Tx-owned route and admission generation.
  alignas(Zm::CacheLineSize)
  EndpointTxDiag m_txDiag;
  IPPath m_txPath{};
  unsigned m_txGeneration = 0;
  bool m_txOpen = false;
};

} // namespace Zutcp_
