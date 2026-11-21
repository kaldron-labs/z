//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// TLS connection integration

#ifndef Ztls_HH
#define Ztls_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuSpan.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmEngine.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmPLock.hh>
#include <zlib/ZmRBTree.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZtcHub.hh>

#include <zlib/ZiAssert.hh>
#include <zlib/ZiEventLoop.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiResolver.hh>
#include <zlib/ZiRx.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/ZiTx.hh>
#include <zlib/ZiTxStream.hh>

#include <zlib/ZtlsCredentials.hh>
#include <zlib/ZtlsCxn.hh>
#include <zlib/ZtlsEngineOpenSSL.hh>
#include <zlib/ZtlsError.hh>
#include <zlib/ZtlsRandom.hh>

#if ZtlsFusion
#include <zlib/ZtlsFusion.hh>

#endif

#include <stdlib.h>
#include <string.h>

#include <zlib/ZtlsPK.hh>

#ifndef _WIN32
#include <fcntl.h>

#endif

namespace Ztls_ {

// Plaintext buffers hold one complete TLS application record.
enum {
  TxRecordCapacity = 16 << 10,
  // Includes TLS 1.3 inner type/padding within the TLS record bound.
  RxRecordCapacity = Ztls_RxRecordCapacity,
  // Local packet payload geometry; independent of the peer/outbound MPS.
  RxPacketCapacity = 1460
};

using RxQueue = ZiRxQueue;
using TxQueue = Zi::IOQueue;
using RxStream = ZiRxStream<RxQueue>;

template <
  unsigned Size = Ztls_::RxRecordCapacity, unsigned MaxSize = ZiIOBuf_DefltVHMaxSize,
  ZuString HeapID = ZiIOBuf_HeapID{}()>
using RxBufAlloc = Zi::IOBufBuiltinAllocator<
  Zi::IOBufType::RxTLS, RxQueue::Node, Size, ZiIOBuf_Align, ZuStringT<HeapID>, MaxSize>;

template <
  unsigned Size = TxRecordCapacity, unsigned MaxSize = ZiIOBuf_DefltVHMaxSize,
  ZuString HeapID = ZiIOBuf_HeapID{}()>
using TxBufAlloc = Zi::IOBufBuiltinAllocator<
  Zi::IOBufType::TxTLS, ZiIOBuf, Size, ZiIOBuf_Align, ZuStringT<HeapID>, MaxSize>;

} // namespace Ztls_

namespace Ztls {

ZuDerive(
  CertSpansScratch, (ZtArray<ZuBSpan, ZtArrayHeapID<"Ztls.CertSpans", ZtArraySharded<true>>>));

namespace LinkState {
  using namespace Ztc::LinkState;
}

struct Connected {
  ZuCSpan	alpn;
  int		version = 0;
};

// heap-allocated vocabulary types

ZuDerive(LogMsg, ZtString<ZtStringHeapID<"Ztls.Log">>);
ZuDerive(Host, ZtString<ZtStringHeapID<"Ztls.Host">>);
ZuDerive(Ticket, (ZtArray<uint8_t, ZtArrayHeapID<"Ztls.Ticket">>));
ZuDerive(ALPNData, (ZtArray<uint8_t, ZtArrayHeapID<"Ztls.ALPNData">>));
ZuDerive(ALPN, (ZtArray<Ztls_::Bytes, ZtArrayHeapID<"Ztls.ALPN">>));
using ErrorFn = ZmFn<void(ZeException), ZmFnHeapID<"Ztls.ErrorFn">>;
ZuDerive(ParamString, ZtString<ZtStringHeapID<"Ztls.Param">>);
ZuDerive(ParamStrings, (ZtArray<ParamString, ZtArrayHeapID<"Ztls.ParamStrings">>));
ZuDerive(Cert, (ZtArray<uint8_t, ZtArrayHeapID<"Ztls.Cert">>));
ZuDerive(Certs, (ZtArray<Cert, ZtArrayHeapID<"Ztls.Certs">>));

inline ErrorFn defaultErrorFn()
{
  return ErrorFn{[](ZeException e) { ZiLogEvent(ZuMv(e)); }};
}

namespace Alloc_ {
  template <
    Zi::IOBufType::T Type, typename Buf, unsigned Size, unsigned Align, typename HeapID,
    unsigned VHMaxSize, auto Grow>
  Zi::IOBufAllocator *txPktAllocator(
    ZiMultiplex *mx,
    Zi::IOBufBuiltinAllocator<Type, Buf, Size, Align, HeapID, VHMaxSize, Grow> *)
  {
    ZuAssert(Type == Zi::IOBufType::TxTCP);
    using Builtin = Zi::IOBufBuiltinAllocator<Type, Buf, Size, Align, HeapID, VHMaxSize, Grow>;
    using Pool = Zi::IOBufPoolAllocator<Type, Buf, Size, Align, HeapID, VHMaxSize, Grow>;
    if (auto allocator = mx->template findAllocator<Builtin>()) return allocator;
    if (auto allocator = mx->template findAllocator<Pool>()) return allocator;
    return mx->template allocator<Type, Buf, Size, Align, HeapID, VHMaxSize, Grow>();
  }
}

// Register a buffer allocation policy.
template <typename Policy> Zi::IOBufAllocator *allocator(ZiMultiplex *mx)
{
  return [mx]<
    Zi::IOBufType::T Type, typename Buf, unsigned Size, unsigned Align, typename HeapID,
    unsigned VHMaxSize, auto Grow>(
    Zi::IOBufBuiltinAllocator<Type, Buf, Size, Align, HeapID, VHMaxSize, Grow> *)
  {
    return mx->allocator<Type, Buf, Size, Align, HeapID, VHMaxSize, Grow>();
  }(static_cast<Policy *>(nullptr));
}

template <
  typename Plain,
  typename Packet = Zi::IOBufBuiltinAllocator<Zi::IOBufType::TxTCP, TxPacket>>
Zi::IOBufAllocator *rxAllocator(ZiMultiplex *mx)
{
  if (!Alloc_::txPktAllocator(mx, static_cast<Packet *>(nullptr))) return nullptr;
  return allocator<Plain>(mx);
}

struct HubParams {
  HubParams(ZiMultiplex *mx = nullptr, ZuCSpan rxThread = {}, ZuCSpan txThread = {})
  :
    m_mx{mx}, m_rxThread{rxThread}, m_txThread{txThread}, m_errorFn{defaultErrorFn()}
  { }

  HubParams &&rxAllocator(Zi::IOBufAllocator *v)
  {
    m_rxAllocator = v;
    return ZuMv(*this);
  }
  HubParams &&rxPktAllocator(Zi::IOBufAllocator *v)
  {
    m_rxPktAllocator = v;
    return ZuMv(*this);
  }

  HubParams &&caPath(ZuCSpan v)
  {
    m_caPath = v;
    return ZuMv(*this);
  }
  HubParams &&certPath(ZuCSpan v)
  {
    m_certPath = v;
    return ZuMv(*this);
  }
  HubParams &&keyPath(ZuCSpan v)
  {
    m_keyPath = v;
    return ZuMv(*this);
  }
  HubParams &&asyncThread(ZuCSpan v)
  {
    m_asyncThread = v;
    return ZuMv(*this);
  }
  HubParams &&alpn(ZuSpan<ZuCSpan> v)
  {
    m_alpn = {};
    m_alpn.ensure(v.length());
    for (auto &s : v) m_alpn.push(ParamString{s});
    return ZuMv(*this);
  }
  HubParams &&errorFn(ErrorFn v)
  {
    m_errorFn = ZuMv(v);
    return ZuMv(*this);
  }

  ZiMultiplex *multiplex() const { return m_mx; }
  ZiMultiplex *mx() const { return m_mx; }
  Zi::IOBufAllocator *rxAllocator() const { return m_rxAllocator; }
  Zi::IOBufAllocator *rxPktAllocator() const { return m_rxPktAllocator; }
  ZuCSpan rxThread() const { return m_rxThread; }
  ZuCSpan txThread() const { return m_txThread; }
  const ParamStrings &alpn() const { return m_alpn; }
  ZuCSpan caPath() const { return m_caPath; }
  ZuCSpan certPath() const { return m_certPath; }
  ZuCSpan keyPath() const { return m_keyPath; }
  ZuCSpan asyncThread() const { return m_asyncThread; }
  const ErrorFn &errorFn() const { return m_errorFn; }
  ErrorFn &errorFn() { return m_errorFn; }
private:
  using Allocator = Zi::IOBufAllocator;
  ZiMultiplex	*m_mx = nullptr;
  Allocator	*m_rxAllocator = nullptr;
  Allocator	*m_rxPktAllocator = nullptr;
  ParamString	m_rxThread;
  ParamString	m_txThread;
  ParamStrings	m_alpn;
  ParamString	m_caPath;
  ParamString	m_certPath;
  ParamString	m_keyPath;
  ParamString	m_asyncThread;
  ErrorFn	m_errorFn;
};

class ClientParams : public HubParams {
public:
  using HubParams::HubParams;

  HubParams &hub() { return *this; }
  const HubParams &hub() const { return *this; }
  template <typename L> ClientParams &&hub(L &&l)
  {
    ZuFwd<L>(l)(static_cast<HubParams &>(*this));
    return ZuMv(*this);
  }

  ClientParams &&key(ZmRef<PK::AnyPK> v)
  {
    m_key = ZuMv(v);
    return ZuMv(*this);
  }
  ClientParams &&certs(Certs v)
  {
    m_certs = ZuMv(v);
    return ZuMv(*this);
  }
  ClientParams &&caCerts(Certs v)
  {
    m_caCerts = ZuMv(v);
    return ZuMv(*this);
  }

  const ZmRef<PK::AnyPK> &key() const & { return m_key; }
  ZmRef<PK::AnyPK> key() && { return ZuMv(m_key); }
  const Certs &certs() const { return m_certs; }
  const Certs &caCerts() const { return m_caCerts; }
private:
  ZmRef<PK::AnyPK>	m_key;
  Certs			m_certs;
  Certs			m_caCerts;
};

struct ServerParams : public HubParams {
  using HubParams::HubParams;
  using HubParams::caPath;
  using HubParams::certPath;
  using HubParams::keyPath;
  using HubParams::asyncThread;
  using HubParams::alpn;
  using HubParams::errorFn;

  ServerParams &&caPath(ZuCSpan v)
  {
    HubParams::caPath(v);
    return ZuMv(*this);
  }
  ServerParams &&certPath(ZuCSpan v)
  {
    HubParams::certPath(v);
    return ZuMv(*this);
  }
  ServerParams &&keyPath(ZuCSpan v)
  {
    HubParams::keyPath(v);
    return ZuMv(*this);
  }
  ServerParams &&asyncThread(ZuCSpan v)
  {
    HubParams::asyncThread(v);
    return ZuMv(*this);
  }
  ServerParams &&alpn(ZuSpan<ZuCSpan> v)
  {
    HubParams::alpn(v);
    return ZuMv(*this);
  }
  ServerParams &&errorFn(ErrorFn v)
  {
    HubParams::errorFn(ZuMv(v));
    return ZuMv(*this);
  }
  ServerParams &&mTLS(bool v)
  {
    m_mTLS = v;
    return ZuMv(*this);
  }
  ServerParams &&cacheTimeout(int v)
  {
    m_cacheTimeout = v;
    return ZuMv(*this);
  }

  bool mTLS() const { return m_mTLS; }
  int cacheTimeout() const { return m_cacheTimeout; }
private:
  bool	m_mTLS = false;
  int	m_cacheTimeout = -1;
};

struct TlsInfo {
  int		tlsver = 0;
  uint16_t	protocolVersion = 0;
  uint16_t	cipherID = 0;
  const char	*alpn = nullptr;
  bool		psk = false;
};

// The TLS handshake and record receive state is Rx-owned.  Established
// record transmission uses the connection's Tx-owned members.

// API functions: listen, connect, disconnect/disconnect_, txStream/txStream_ (Tx)
// API callbacks: accepted, connected, disconnected, process (Rx)

// Function Category | I/O Threads |        TLS threads         | App threads
// ------------------|-------------|----------------------------|------------
// Server            | accepted()  | connected() disconnected() | listen()
// Client            |             | connect_() connectFailed() | connect()
// Disconnect        |             | disconnect_()              | disconnect()
// Transmission (Tx) |             | txStream_(allocator)       | txStream(allocator)
// Reception    (Rx) |             | process()                  |

// ZiIOBuf buffers transport data between threads (--> arrows below)

// I/O Threads |                   TLS threads                   | App threads
// ------------|-------------------------------------------------|------------
//     I/O Rx --> Rx input  -> Decryption -> Rx output -> App Rx |
// ------------|-------------------------------------------------|
//     I/O Tx <-- Tx output <- Encryption <- Tx input           <-- App Tx
// ------------|-------------------------------------------------|------------

template <typename Link_, typename LinkRef_>
class LinkCxn : public ZmNHeap<LinkCxn<Link_, LinkRef_>>, public ZiConnection, public Ztls::Cxn {
friend inline ZuStringT<"Ztls.Cxn"> ZmNHeapID(LinkCxn *);
public:
  using Link = Link_;
  using LinkRef = LinkRef_;

  LinkCxn(LinkRef link, const ZiCxnInfo &ci)
  :
    ZiConnection(link->app()->mx(), ci), m_link(ZuMvPtr(link))
  { }

  void connected(ZiRxContext &io) override { m_link->connected_0(this, io); }
  void disconnected(bool peer) override
  {
    LinkRef link_ = ZuMvPtr(m_link);
    if (Link *link = link_) link->disconnected_0(this, ZuMvPtr(link_), peer);
  }
private:
  // immutable
  LinkRef	m_link = nullptr;
};

// client links are persistent, own the (transient) connection
template <typename Link> using CliCxn = LinkCxn<Link, Link *>;
// server links are transient, are owned by the connection
template <typename Link> using SrvCxn = LinkCxn<Link, ZmRef<Link>>;

using RxStream = Ztls_::RxStream;

template <
  unsigned Size = Ztls_::RxRecordCapacity, unsigned MaxSize = ZiIOBuf_DefltVHMaxSize,
  ZuString HeapID = "Ztls.RxBuf">
using RxBufAlloc = Ztls_::RxBufAlloc<Size, MaxSize, HeapID>;

template <
  unsigned Size = Ztls_::TxRecordCapacity, unsigned MaxSize = ZiIOBuf_DefltVHMaxSize,
  ZuString HeapID = "Ztls.TxBuf">
using TxBufAlloc = Ztls_::TxBufAlloc<Size, MaxSize, HeapID>;

template <
  typename App, typename Impl, typename RxBufAlloc_, typename Cxn_,
  typename CxnRef_>
class Link :
  public ZmPolymorph, public ZiRx<Impl, Ztls_::RxRecordCapacity>,
  public ZiTx<Impl>, public Ztc::Link {
  ZuAssert((ZuIs_<typename RxBufAlloc_::Buf, Ztls_::RxQueue::Node>{}));

  class TelQueue final : public Ztc::Queue {
  public:
    TelQueue(const Link *link, Ztc::QueueType::T type) : m_link{link}, m_type{type} { }

    TelKey telKey() const override
    {
      auto linkKey = m_link->telKey();
      return {linkKey.template p<0>(), linkKey.template p<1>(), m_type};
    }
    void telemetry(Ztc::QueueTelemetry &data) const override
    {
      if (m_type == Ztc::QueueType::Rx)
	m_link->rxQueueTelemetry(data);
      else
	m_link->txQueueTelemetry(data);
    }
  private:
    const Link			*m_link;
    const Ztc::QueueType::T	m_type;
  };
public:
  using RxBufAlloc = RxBufAlloc_;
  using RxAllocator = Zi::IOBufAllocator;
  using Cxn = Cxn_;
  using CxnRef = CxnRef_;
  using Rx = ZiRx<Impl, Ztls_::RxRecordCapacity>;
  using Tx = ZiTx<Impl>;
  using ImplRef = Cxn::LinkRef;
  using Stream = Impl;
  using StreamRef = Impl *;

friend Cxn;
protected:
  using Tx::send;
private:
  struct SignatureWatch;
  using SignatureWatchRef = ZmRef<SignatureWatch>;
public:
  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  Link(App *app, bool isServer)
  :
    Link{app, isServer, ZuID{} << "tls:" << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>()}
  { }
  Link(App *app, bool isServer, ZuID id) : m_app{app}, m_id{ZuMv(id)}, m_isServer{isServer}
  { app->linkAdded_(this); }
  ~Link()
  {
    ZmAssert(!m_txCxn);
    app()->linkDeleted_(this, state_());
    ZmAssert(!m_asyncJob);
  }

  App *app() const { return m_app; }
  static Zi::IOBufAllocator *rxAllocator(ZiMultiplex *mx)
  { return Ztls::rxAllocator<RxBufAlloc, typename App::TxPktAlloc>(mx); }
  RxAllocator *rxAllocator() const { return app()->rxAllocator(); }
  Cxn *cxn() const { return m_cxn; }
  bool disconnecting() const { return m_disconnecting.load_(); }
  StreamRef stream() { return impl(); }
  TelKey telKey() const override { return {app()->telKey().template p<1>(), m_id}; }
  void telemetry(Ztc::LinkTelemetry &data) const override
  {
    data.hubID = app()->telKey().template p<1>();
    data.id = m_id;
    data.rxCalls = 0;
    data.txCalls = 0;
    data.rxBytes = 0;
    data.txBytes = 0;
    data.reconnects = 0;
    if (Cxn *cxn = m_cxn) {
      data.rxCalls = cxn->rxCalls();
      data.txCalls = cxn->txCalls();
      data.rxBytes = cxn->rxBytes();
      data.txBytes = cxn->txBytes();
    }
    data.type = Ztc::LinkType::TLS;
    data.state = state_();
  }
  unsigned allQueues(Ztc::QueueMgr::AllFn fn) const override
  {
    TelQueue rxQueue{this, Ztc::QueueType::Rx};
    fn(&rxQueue);
    TelQueue txQueue{this, Ztc::QueueType::Tx};
    fn(&txQueue);
    return 2;
  }
  void up() override { impl()->up_(); }
  void down() override { disconnect(); }
  TlsInfo tlsInfo() const
  {
    if (!m_tls || !m_handshook) return {};
    auto tls = m_tls;
    auto cipher = Ztls_::cipher(tls);
    auto version = Ztls_::protocolVersion(tls);
    return TlsInfo{.tlsver = tlsver(version),
      .protocolVersion = version,
      .cipherID = cipher ? cipher->id : uint16_t{0},
      .alpn = Ztls_::negotiatedProtocol(tls),
      .psk = bool(Ztls_::isPskHandshake(tls))};
  }
protected:
  Ztls::Cxn *tls() { return m_tls; }
  Ztls_::HandshakeProps *handshakeProps() { return &m_tls->handshakeProps; }
  void resetHandshakeProps()
  {
    if (m_tls) m_tls->handshakeProps = {};
  }
private:
  static constexpr unsigned TxRecordCapacity = Ztls_::TxRecordCapacity;
  static constexpr unsigned TxMaxOverhead = 325;
  static constexpr unsigned TxMaxPlaintext = TxRecordCapacity;

  // called from Cxn::connected(ZiRxContext &)
  // this is the internal TCP-level connected; once handshake is completed,
  // the application's connected_() will be called
  void connected_0(Cxn *cxn, ZiRxContext &io)
  { // runs on I/O Rx thread
    io.complete();
    app()->rxInvoke(
      [impl = ZmRef(this->impl()), cxn = ZmRef(cxn)]() { impl->connected_1(ZuMv(cxn)); });
  }

  void connected_1(ZmRef<Cxn> cxn)
  {
    // direct call from within rx thread
    ZiAssert(app()->rxInvoked(), "Ztls", (), "TLS connected dispatch outside Rx thread", return);
    // idempotent
    if (ZuUnlikely(m_cxn == cxn)) return;
    auto       oldState = state_();
    ZmRef<Cxn> old{ZuMvPtr(m_cxn)};
    if (!old) old = m_closing.load_();
    bool replacing = bool(old);
    m_closing = nullptr;
    if (m_isServer)
      resetTLS();
    else
      retireTLS();
    if (ZuUnlikely(old)) {
      old->close();
      // Retire the old physical output on I/O Tx before publishing B. This
      // neither flushes A nor waits for its disconnect callbacks. Delayed A
      // send setups must reject the closed socket before touching the queue.
      auto old_ = old.ptr();
      old_->send(
	ZiTxFn{[impl = ZmRef(this->impl()), cxn = ZuMv(old)](ZiTxContext &io)
      {
	io.complete();
	impl->Tx::txQueue.clean();
	(void)cxn;
	return false;
      }});
    }
    m_cxn = ZuMv(cxn);
    m_closePending = m_peerClosed = false;
    stateChanged_(oldState);
    app()->linkConnected_();
    if (!impl()->connectSocket_(replacing)) {
      impl()->disconnect_(false);
      return;
    }
    m_tls = m_cxn;
    m_tls->initialize(app()->ctx(), m_isServer, impl());
    m_tls->plaintext.reset(app()->rxAllocator(), impl());
    if (Ztls_::rxStreamCheck(m_tls)) {
      app()->error_(ZeEXCEPT(Error, "Ztls", "selected TLS backend lacks serial decryption"));
      disconnect_(false);
      return;
    }
    impl()->connected_(); // client initiates handshake
    // Install the first admission with the initialized TLS generation.
    if (m_tls && m_cxn && !m_disconnecting.load_())
      Rx::template recv<&Link::input_>(m_cxn, m_tlsGen.load_());
  }

private:
  void input_(ZiConnection *physical, ZmRef<ZiIOBuf> buf, uint64_t gen)
  {
    auto cxn = static_cast<Cxn *>(physical);
    app()->rxRun([this, gen, cxn = ZmRef(cxn), buf = ZuMv(buf)]() mutable
    {
      if (m_cxn != cxn.ptr() || m_tls != cxn.ptr() || m_disconnecting.load_() ||
	gen != m_tlsGen.load_())
	return;
      cxn->rxPaused = true;
      cxn->rxInput = ZuMv(buf);
      processRx_();
    });
  }
protected:
  bool processRx_(SignatureWatch *resuming = nullptr)
  {
    ZiAssert(app()->rxInvoked(), "Ztls", (), "TLS input outside Rx thread", return false);
    if (!m_tls || !m_cxn || m_disconnecting.load_()) return false;
    if (asyncPending()) return true;
    auto	 gen = m_tlsGen.load_();
    ZmRef<Cxn>	 target{m_cxn};
    PacketTarget output{this, target.ptr(), gen};
    auto	 reservation = packetSink(output);
    unsigned	 records = 0;
    do {
      size_t consumed = m_tls->rxInput ? m_tls->rxInput->length : 0;
      cryptoClearError();
      int n = Ztls_::rxStream(
	m_tls, &reservation, m_tls->rxInput ? m_tls->rxInput->data() : nullptr, &consumed,
	&m_tls->handshakeProps);
      if (m_tls->rxInput) {
	m_tls->rxInput->advance(unsigned(consumed));
	if (!m_tls->rxInput->length) m_tls->rxInput = nullptr;
      }
      if (n != 0 && n != Ztls_ErrorInProgress && n != Ztls_ErrorAsyncOperation) {
	m_tls->plaintext.clear();
	m_tls->rxInput = nullptr;
	if (Ztls_ErrorGetClass(n) == Ztls_ErrorClassPeerAlert &&
	  Ztls_ErrorToAlert(n) == Ztls_AlertCloseNotify) {
	  m_peerClosed = true;
	  disconnect_(true);
	} else {
	  app()->error_(ZeEXCEPT(Error, "Ztls", Ztls::Error("Ztls_::rxStream()", n)));
	  disconnect_(false);
	}
	return false;
      }
      if (n == Ztls_ErrorAsyncOperation) return asyncHandshake_0(resuming);
      if (Ztls_::handshakeComplete(m_tls) && !m_handshook && !m_txPending)
	if (!finishHandshake_0()) return false;
      if (auto buf = m_tls->plaintext.take()) {
	if (buf->length) {
	  ++m_rxInCount;
	  m_rxInBytes += buf->length;
	  m_rxStream.push(ZuMv(buf));
	}
      }
      while (Ztls_::takeKeyUpdateReq(m_tls))
	if (!updateKey_0()) return false;
      if (m_handshook) {
	while (m_rxStream) {
	  int n = impl()->process(m_rxStream);
	  if (!n) break;
	  if (n < 0) { disconnect_(true); return false; }
	}
      }
      if (gen != m_tlsGen.load_() || m_cxn != target.ptr() || m_disconnecting.load_())
	return false;
      if (!m_tls->rxInput) break;
      if (++records == 32) {
	app()->rxRun([this, gen, target = ZuMv(target)]()
	{
	  if (gen == m_tlsGen.load_() && m_cxn == target.ptr()) processRx_();
	});
	return true;
      }
    } while (true);
    if (m_tls->rxPaused) {
      m_tls->rxPaused = false;
      Rx::template recv<&Link::input_>(m_cxn, m_tlsGen.load_());
    }
    return true;
  }

private:
  bool finishHandshake_0()
  {
    if (m_handshook || m_txPending) return true;
    ZmAssert(!m_asyncJob);
    auto cipher = Ztls_::cipher(m_tls);
    if (ZuUnlikely(!cipher)) {
      app()->error_(ZeEXCEPT(Error, "Ztls", "Ztls_::cipher() failed"));
      disconnect_(false);
      return false;
    }
    if (ZuUnlikely(!cipher->aead)) {
      app()->error_(ZeEXCEPT(Error, "Ztls", "Ztls_::cipher()->aead is null"));
      disconnect_(false);
      return false;
    }
    unsigned recOverhead = Ztls_::recordOverhead(m_tls);
    // Tx buffers are pre-sized; negotiated overhead must fit the budget.
    if (ZuUnlikely(recOverhead > TxMaxOverhead)) {
      app()->error_(ZeEXCEPT(Error, "Ztls", "TLS record overhead exceeds worst-case limit"));
      disconnect_(false);
      return false;
    }
    cryptoClearError();
    int n = Ztls_::handoffTx(m_tls);
    // A server can finish producing its handshake flight before it has
    // consumed the client's Finished.  Its outbound application keys exist,
    // but Tx handoff becomes valid only after that record is received.
    if (n == Ztls_ErrorNotAvailable && m_isServer && !m_txDeferred) {
      m_txDeferred = true;
      return true;
    }
    if (ZuUnlikely(n)) {
      app()->error_(ZeEXCEPT(Error, "Ztls", Ztls::Error("Ztls_::handoffTx()", n)));
      disconnect_(false);
      return false;
    }
    uint64_t gen = m_tlsGen.load_();
    m_txDeferred = false;
    m_txPending = true;
    app()->txRun([impl = ZmRef(impl()), gen, cxn = ZmRef<Cxn>{m_cxn}]() mutable
    { impl->finishHandshake_1(gen, ZuMv(cxn)); });
    return true;
  }

  void finishHandshake_1(uint64_t gen, ZmRef<Cxn> cxn)
  {
    ZiAssert(app()->txInvoked(), "Ztls", (), "TLS Tx install outside Tx thread", return);
    bool installed = false;
    if (ZuLikely(gen == m_tlsGen.load_() && !m_disconnecting.load_() && cxn && cxn->up())) {
      if (m_txCxn && m_txCxn->txGeneration != gen) retireTx(gen);
      if (!m_txCxn) {
	cxn->txGeneration = gen;
	m_txCxn = cxn;
	installed = true;
      } else {
	installed = m_txCxn == cxn;
      }
    }
    if (!installed && cxn && m_txCxn != cxn) cxn->retireTx();
    app()->rxRun([impl = ZmRef(impl()), cxn = ZuMv(cxn), gen, installed]() mutable
    { impl->finishHandshake_2(cxn.ptr(), gen, installed); });
  }

  void finishHandshake_2(Cxn *cxn, uint64_t gen, bool installed)
  {
    ZiAssert(app()->rxInvoked(), "Ztls", (), "TLS Tx acknowledgement outside Rx thread", return);
    if (ZuUnlikely(gen != m_tlsGen.load_() || m_cxn != cxn)) return;
    m_txPending = false;
    m_txDeferred = false;
    if (ZuUnlikely(!installed)) {
      if (!m_disconnecting.load_()) {
	app()->error_(ZeEXCEPT(Error, "Ztls", "TLS Tx state installation failed"));
	disconnect_(false);
      }
      return;
    }
    if (ZuUnlikely(m_disconnecting.load_())) return;
    auto oldState = state_();
    m_handshook = true;
    stateChanged_(oldState);
    auto	version = Ztls_::protocolVersion(m_tls);
    const char *alpn = Ztls_::negotiatedProtocol(m_tls);
    if (!impl()->connectReady_()) {
      disconnect_(false);
      return;
    }
    impl()->connected(
      Connected{
	.alpn = alpn ? ZuCSpan{alpn, unsigned(strlen(alpn))}
	     :
	       ZuCSpan{},
	.version = tlsver(version)
	  });
    if (ZuLikely(!m_disconnecting.load_())) processRx_();
  }

  const Ztls_::CipherSuite *rxCipher() const { return m_tls ? Ztls_::cipher(m_tls) : nullptr; }

  struct PacketTarget : RecordPublisher {
    Link		*link;
    Cxn			*cxn;
    uint64_t		gen;
    const ZiIOBuf	*plaintext;

    PacketTarget(Link *link, Cxn *cxn, uint64_t gen, const ZiIOBuf *plaintext = nullptr)
    :
      link{link}, cxn{cxn}, gen{gen}, plaintext{plaintext}
    { }

    int publish(ZmRef<ZiIOBuf> buf) override
    {
      if (!cxn || !cxn->up() || gen != link->m_tlsGen.load_()) return Ztls_ErrorLibrary;
      return link->finalizeTxBuf(ZuMv(buf), cxn) ? 0 : Ztls_ErrorLibrary;
    }
    void ready(ZmRef<ZiIOBuf> last) override
    {
      if (!plaintext) return;
      ++link->m_txInCount;
      link->m_txInBytes += plaintext->length;
      link->impl()->txProtected();
      link->impl()->txPacketized(plaintext, ZuMv(last));
    }
  };

  RecordSink packetSink(PacketTarget &target)
  {
    auto cxn = target.cxn;
    auto mps = cxn ? cxn->info().mps : 0;
    return RecordSink{app()->txPktAllocator(), impl(), mps, target};
  }

  bool finalizeTxBuf(ZmRef<ZiIOBuf> buf, Cxn *cxn)
  {
    if (!buf->length) return true;
    buf->owner = impl();
    cxn->send(
      ZiTxFn::mvFn(
	ZuMv(buf), [](ZmRef<ZiIOBuf> buf, ZiTxContext &io)
    {
      auto cxn = static_cast<Cxn *>(io.cxn);
      if (ZuUnlikely(!cxn->up())) return false;
      auto link = Tx::impl(buf);
      if (auto count = link->Tx::txQueue.count_();
	ZuUnlikely(count && count >= link->maxQueueLen())) {
	// Complete the rejection callback before asynchronous teardown.
	link->sent(ZuMv(buf), false);
	cxn->close();
	return false;
      }
      link->Tx::send(cxn, ZuMv(buf));
      return false;
    }));
    // Dispatch has transferred ownership. A peer can consume this last packet
    // and close before this thread resumes; sent() owns its completion result.
    return true;
  }

  Cxn *closing_() const { return m_closing.load_(); }
  void closing_(Cxn *cxn) { m_closing = cxn; }
  uint64_t tlsGen() const { return m_tlsGen.load_(); }

  template <typename ImplRef_> void disconnected_0(Cxn *cxn, ImplRef_ impl_, bool peer)
  {
    ZmRef<Impl> impl{ZuMvPtr(impl_)};
    app()->rxRun([impl = ZuMv(impl), cxn = ZmRef(cxn), peer]() mutable
    {
      // Even a stale Cxn retains its link through the local owner barriers:
      // preexisting encrypt/send posts can still refer to that physical Cxn.
      // The close token makes these barriers cleanup-free after replacement.
      impl->disconnected_(cxn);
      auto gen = impl->tlsGen();

      // Retire queued mux Rx admissions before releasing the link. Reception
      // is disarmed, and TLS Rx can no longer post a rearm for this connection.
      cxn->mx()->rxRun([impl = ZuMv(impl), cxn = ZuMv(cxn), gen, peer]() mutable
      {
	auto app = impl->app();
	// drain Tx while keeping cxn/impl referenced
	app->txRun([impl = ZuMv(impl), cxn = ZuMv(cxn), gen, peer]() mutable
	{
	  if (impl->closing_() == cxn.ptr()) impl->clearTx(gen);
	  // TLS Tx has stopped producing; drain its queued socket sends next.
	  cxn->mx()->txRun([impl = ZuMv(impl), cxn = ZuMv(cxn), gen, peer]() mutable
	  {
	    if (impl->closing_() == cxn.ptr() && gen == impl->tlsGen()) impl->Tx::txQueue.clean();
	    auto app = impl->app();
	    app->rxRun([impl = ZuMv(impl), cxn = ZuMv(cxn), gen, peer]() mutable
	    {
	      if (impl->closing_() == cxn.ptr()) {
		impl->closing_(nullptr);
		if (gen == impl->tlsGen()) impl->disconnected_1(peer);
	      }
	      auto app = impl->app();
	      // Release the connection and link before signalling the stop barrier.
	      cxn = nullptr;
	      impl = nullptr;
	      app->linkDisconnected_();
	    });
	  });
	});
      });
    });
  }
  bool disconnected_(Cxn *cxn)
  {
    // direct call from within rx thread
    ZiAssert(
      app()->rxInvoked(), "Ztls", (), "TLS disconnected dispatch outside Rx thread",
      return false);
    if (m_cxn == cxn || m_closing.load_() == cxn) {
      auto oldState = state_();
      m_cxn = nullptr;
      m_closing = cxn;
      stateChanged_(oldState);
      return true;
    }
    return false;
  }

  void disconnected_1(bool peer)
  {
    ZiAssert(
      app()->rxInvoked(), "Ztls", (), "TLS disconnect completion outside Rx thread", return);
    peer |= m_peerClosed;
    resetTLS<false>();
    m_rxStream.clean();
    impl()->connectClosed_();
    impl()->disconnected(peer);
  }
public:
  // Plaintext capacity is independent of packet and TLS framing geometry.
  static constexpr unsigned txMaxSize() { return TxRecordCapacity; }
  template <bool AppThread> unsigned txHeadRoom() const { return 0; }
  template <bool AppThread> unsigned txTailRoom() const { return 0; }
  template <bool AppThread>
  ZmRef<ZiIOBuf> txAllocBuf(Zi::IOBufAllocator *allocator, unsigned skip)
  {
    if (ZuUnlikely(skip < txHeadRoom<AppThread>() || skip > txMaxSize())) return nullptr;
    auto buf = allocTxBuf(allocator, skip);
    if (ZuUnlikely(!buf || skip > buf->size)) return nullptr;
    return buf;
  }
private:
  ZmRef<ZiIOBuf> allocTxBuf(Zi::IOBufAllocator *allocator, unsigned headroom)
  { // App/Tx threads
    auto buf = allocator->allocBuf(impl(), TxRecordCapacity);
    if (!buf) return nullptr;
    // Preserve only headroom requested by application framing.
    buf->skip = headroom;
    buf->length = 0;
    return buf;
  }

  template <bool AppThread> class TxStream_ : public Zi::TxStream<TxStream_<AppThread>> {
    using Base = Zi::TxStream<TxStream_<AppThread>>;
  public:
    TxStream_(Link &link, Zi::IOBufAllocator *allocator)
    :
      TxStream_(link, allocator, link.template txHeadRoom<AppThread>())
    { }
  private:
    TxStream_(Link &link, Zi::IOBufAllocator *allocator, unsigned headroom)
    :
      Base(link.txMaxSize(), headroom, 0), m_link{&link}, m_allocator{allocator},
      m_gen{link.m_tlsGen.load_()}, m_headroom{headroom}
    { }
  public:
    ~TxStream_()
    {
      this->flush();
      if (this->failed())
	m_link->txError(m_gen, ZeEXCEPT(Error, "Ztls", "TLS plaintext stream failed"));
    }
    TxStream_(TxStream_ &&) = default;
    TxStream_ &operator=(TxStream_ &&) = default;

    ZmRef<ZiIOBuf> allocBuf_(unsigned skip)
    {
      if (ZuUnlikely(skip < m_headroom)) return nullptr;
      return m_link->template txAllocBuf<AppThread>(m_allocator, skip);
    }

    bool sendBuf_(ZmRef<ZiIOBuf> buf, bool)
    {
      buf->owner = m_link->impl();
      if constexpr (AppThread)
	return m_link->send(ZuMv(buf), m_gen);
      else
	return m_link->send_(ZuMv(buf), m_gen);
    }
  private:
    // immutable
    Link		*m_link;
    Zi::IOBufAllocator	*m_allocator;
    uint64_t		m_gen = 0;
    unsigned		m_headroom = 0;
  };
public:
  void txErrorFn(ZiTxErrorFn fn) { m_txErrorFn = ZuMv(fn); }

  auto txStream(Zi::IOBufAllocator *allocator)
  { // App thread(s)
    ZmAssert(!app()->txInvoked());
    return TxStream_<true>{*this, allocator};
  }
  TxStream_<false> txStream_(Zi::IOBufAllocator *allocator)
  { // direct call from within tx thread
    ZmAssert(app()->txInvoked());
    return TxStream_<false>{*this, allocator};
  }
protected:
  bool updateKey_0(bool requestUpdate = false)
  {
    // direct call from within rx thread
    ZiAssert(app()->rxInvoked(), "Ztls", (), "TLS KeyUpdate outside Rx thread", return false);
    if (ZuUnlikely(!m_tls || !rxCipher())) return false;
    ZiAssert(
      Ztls_::protocolVersion(m_tls) == Ztls_ProtoVerTls13, "Ztls", (),
      "TLS KeyUpdate requires TLS 1.3", return false);
    uint64_t gen = m_tlsGen.load_();
    app()->txRun([impl = ZmRef(impl()), gen, requestUpdate]() mutable
    { impl->updateKey_1(gen, requestUpdate); });
    return true;
  }
private:
public:
  bool send(ZmRef<ZiIOBuf> buf) { return send(ZuMv(buf), m_tlsGen.load_()); }
protected:
  bool send(ZmRef<ZiIOBuf> buf, uint64_t gen)
  {
    if (ZuUnlikely(!buf || buf->failed() || !buf->length))
      return reportTxError(ZeEXCEPT(Error, "Ztls", "TLS session is closed for transmission"));
    if (ZuUnlikely(m_disconnecting.load_() || gen != m_tlsGen.load_())) {
      txError(gen, ZeEXCEPT(Error, "Ztls", "TLS session is closed for transmission"));
      return false;
    }
    buf->owner = impl();
    app()->txInvoke([buf = ZuMv(buf), gen]() mutable
    {
      auto link = static_cast<Impl *>(buf->owner);
      link->send_(ZuMv(buf), gen);
    });
    return true;
  }
protected:
  // Optional instrumentation at the application record-protection boundary.
  void txProtected() { }

  bool send_(ZmRef<ZiIOBuf> buf)
  { // direct call from within tx thread
    return send_(ZuMv(buf), m_tlsGen.load_());
  }
private:
  bool send_(ZmRef<ZiIOBuf> buf, uint64_t gen)
  {
    // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Ztls", (), "TLS send_ outside Tx thread", return false);
    if (ZuUnlikely(
	  !buf || buf->failed() || !buf->length || gen != m_tlsGen.load_() || !m_txCxn ||
	  m_txCxn->txGeneration != gen || m_txCxn->txClosing || !m_txCxn->up())) {
      txError(
	gen, ZeEXCEPT(Error, "Ztls", "TLS session is closed for transmission"),
	m_txCxn && m_txCxn->txGeneration == gen);
      return false;
    }

    ZiAssert(
      buf->length <= TxMaxPlaintext, "Ztls", (), "TLS Tx plaintext exceeds record limit",
      return false);
    ZiAssert(
      buf->skip <= buf->size && buf->length <= buf->size - buf->skip, "Ztls", (),
      "TLS Tx plaintext bounds exceeded", return false);
    ZmRef<Cxn>	 target{m_txCxn};
    PacketTarget output{this, target.ptr(), gen, buf.ptr()};
    auto	 reservation = packetSink(output);

    cryptoClearError();
    int n = Ztls_::txSendChunks(target.ptr(), &reservation, buf->data(), buf->length);
    if (n) {
      txError(gen, ZeEXCEPT(Error, "Ztls", Ztls::Error("Ztls_::txSendChunks()", n)));
      return false;
    }
    return true;
  }
public:
  void txPacketized(const ZiIOBuf *, ZmRef<ZiIOBuf>) { }

  void sent(ZmRef<ZiIOBuf>, bool ok)
  {
    // The underlying connection closes its own failed socket after this
    // diagnostic. It may already have been displaced by another TLS session.
    if (ZuUnlikely(!ok)) reportTxError(ZeEXCEPT(Error, "Ztls", "TLS transmit failed"));
  }
private:
  void updateKey_1(uint64_t gen, bool requestUpdate)
  {
    ZiAssert(app()->txInvoked(), "Ztls", (), "TLS KeyUpdate outside Tx thread", return);
    if (ZuUnlikely(
	  gen != m_tlsGen.load_() || !m_txCxn || m_txCxn->txGeneration != gen ||
	  m_txCxn->txClosing))
      return;
    ZmRef<Cxn>	 target{m_txCxn};
    PacketTarget output{this, target.ptr(), gen};
    auto	 reservation = packetSink(output);

    cryptoClearError();
    int n = Ztls_::txKeyChunks(target.ptr(), &reservation, requestUpdate ? 1 : 0);
    if (ZuUnlikely(n))
      txError(gen, ZeEXCEPT(Error, "Ztls", Ztls::Error("Ztls_::txKeyChunks()", n)));
  }

  void closeTx_(ZmRef<Cxn> cxn, uint64_t gen)
  {
    ZiAssert(app()->txInvoked(), "Ztls", (), "TLS close outside Tx thread", return);
    if (
      gen == m_tlsGen.load_() && m_txCxn && m_txCxn->txGeneration == gen && !m_txCxn->txClosing) {
      PacketTarget output{this, cxn.ptr(), gen};
      auto	   reservation = packetSink(output);

      cryptoClearError();
      int n = Ztls_::txAlertChunks(
	m_txCxn.ptr(), &reservation, Ztls_AlertLevelWarning, Ztls_AlertCloseNotify);
      if (ZuUnlikely(n))
	txError(gen, ZeEXCEPT(Error, "Ztls", Ztls::Error("Ztls_::txAlertChunks()", n)));
      m_txCxn->txClosing = true;
    }
    auto mx = cxn->mx();
    mx->txRun([cxn = ZuMv(cxn)]() { cxn->disconnect(); });
  }

  void txError(uint64_t gen, ZeException e, bool origin = true)
  {
    ZmRef<Cxn> cxn;
    if (origin && app()->txInvoked() && m_txCxn && gen == m_txCxn->txGeneration) {
      m_txCxn->txClosing = true;
      cxn = m_txCxn;
    }
    app()->rxRun([impl = ZmRef(impl()), cxn = ZuMv(cxn), gen, e = ZuMv(e), origin]() mutable
    {
      if (cxn) cxn->close();
      if (impl->m_txErrorFn) impl->m_txErrorFn(e);
      if (origin && gen == impl->tlsGen() && impl->m_cxn) impl->disconnect_(false);
    });
  }

  void retireTx(uint64_t gen)
  {
    ZiAssert(app()->txInvoked(), "Ztls", (), "TLS Tx retirement outside Tx thread", return);
    if (m_txCxn && m_txCxn->txGeneration != gen) {
      m_txCxn->retireTx();
      m_txCxn = nullptr;
    }
  }

  void clearTx(uint64_t gen)
  {
    ZiAssert(app()->txInvoked(), "Ztls", (), "TLS Tx cleanup outside Tx thread", return);
    if (!m_txCxn || gen != m_tlsGen.load_() || m_txCxn->txGeneration != gen) return;
    m_txCxn->retireTx();
    m_txCxn = nullptr;
  }
private:
  bool reportTxError(ZeException e)
  {
    if (m_txErrorFn) m_txErrorFn(e);
    return false;
  }

  bool asyncPending() const { return m_asyncJob && m_asyncJob->cxn.ptr() == m_tls; }

  void clearAsync() { m_asyncJob = nullptr; }

  bool asyncHandshake_0(SignatureWatch *resuming)
  {
#ifdef _WIN32
    app()->error_(ZeEXCEPT(Fatal, "Ztls", "TLS async signing is unsupported on Windows"));
    return false;
#else
    ZmAssert(!m_asyncJob);
    SignatureWatchRef async = resuming;
    if (!async) {
      async = new SignatureWatch{ZmRef(impl()), ZmRef<Cxn>{m_tls}, m_tlsGen.load_()};
      app()->asyncSignatureStarted();
    }
    m_asyncJob = async.ptr();
    armSignature(async);
    return true;
#endif
  }

  void armSignature(SignatureWatchRef async)
  {
    async->armed = true;
#ifndef _WIN32
    async->handle = async->cxn->signatureFd;
#endif
    async->readCount = 0;
    app()->asyncAddHandle(
      async->handle, ZiEvent::HandleReadFn{async, ZmFnPtr<&SignatureWatch::ready>{}});
  }

  void asyncHandshake_1(SignatureWatchRef async, Zi::Handle handle_)
  {
    if (ZuUnlikely(handle_ != async->handle)) return;
    if (++async->readCount != 2) return;
    app()->asyncDelHandle(handle_);
    // Return from the readiness callback before Rx can retire its last watch.
    app()->asyncRun([async = ZuMv(async)]() mutable
    {
      auto app = async->link->app();
      app->rxRun([async = ZuMv(async)]() mutable
      {
	auto app = async->link->app();
	bool finished = async->link->asyncHandshake_2(async);
	async = nullptr;
	if (finished) app->asyncSignatureFinished();
      });
    });
  }

  bool asyncHandshake_2(SignatureWatch *async)
  {
    async->armed = false;
    if (async->cxn.ptr() == m_tls && async->gen == m_tlsGen.load_() && m_asyncJob == async) {
      clearAsync();
      processRx_(async);
    } else if (async->retired) {
      if (!async->cxn->retireSignature()) {
	armSignature(ZmRef(async));
	return false;
      }
      async->cxn->retireRx();
    }
    return !async->armed;
  }

  bool asyncRetireTLS()
  {
    if (!asyncPending()) return false;
    m_asyncJob->retired = true;
    clearAsync();
    return true;
  }
public:
  void disconnect()
  { // App thread(s)
    auto oldState = state_();
    m_disconnecting = 1;
    stateChanged_(oldState);
    app()->rxInvoke([impl = ZmRef(this->impl())]() { impl->disconnect_(); });
  }
  void disconnect_(bool notify = true)
  { // direct call from within rx thread
    ZiAssert(app()->rxInvoked(), "Ztls", (), "TLS disconnect outside Rx thread", return);
    impl()->cancelReconn_();
    impl()->stopConnect_();
    if (m_closePending) return;
    m_closePending = true;
    auto oldState = state_();
    m_disconnecting = 1; // disconnect() might be bypassed
    stateChanged_(oldState);
    if (asyncRetireTLS()) m_tls = nullptr;
    ZmRef<Cxn> cxn;
    if (m_cxn) cxn = ZmRef<Cxn>{m_cxn};
    if (cxn) {
      if (notify) {
	uint64_t gen = m_tlsGen.load_();
	app()->txRun([impl = ZmRef(impl()), cxn = ZuMv(cxn), gen]() mutable
	{ impl->closeTx_(ZuMv(cxn), gen); });
      } else
	cxn->close();
    }
  }
protected:
  void up_() { }
  void cancelReconn_() { }
  void stopConnect_() { }
  bool connectSocket_(bool) { return true; }
  bool connectReady_() { return true; }
  void connectClosed_() { }

  static int tlsver(uint16_t v)
  {
    switch (v) {
    case 0x0304:
      return 13;
    default:
      return 0;
    }
  }

  void retireTLS()
  {
    if (!m_tls) return;
    if (!asyncRetireTLS()) m_tls->retireRx();
    m_tls = nullptr;
  }

  template <bool CleanRx = true> void resetTLS()
  {
    uint64_t gen = m_tlsGen.load_() + 1;
    m_tlsGen.store_(gen);
    app()->txRun([impl = ZmRef(impl()), gen]() mutable { impl->retireTx(gen); });
    retireTLS();
    auto oldState = state_();
    m_txPending = false;
    m_txDeferred = false;
    m_closePending = false;
    m_handshook = false;
    m_peerClosed = false;
    m_disconnecting = 0;
    stateChanged_(oldState);
    if constexpr (CleanRx) m_rxStream.clean();
  }
private:
  LinkState::T state_() const
  {
    if (m_disconnecting.load_()) return LinkState::Disconnecting;
    if (!m_cxn) return LinkState::Down;
    return m_handshook ? LinkState::Up : LinkState::Connecting;
  }

  void stateChanged_(LinkState::T oldState)
  {
    auto newState = state_();
    if (oldState != newState) app()->linkState_(oldState, newState);
  }

  void rxQueueTelemetry(Ztc::QueueTelemetry &data) const
  {
    data.ownerID = app()->telKey().template p<1>();
    data.id = m_id;
    data.inCount = m_rxInCount;
    data.inBytes = m_rxInBytes;
    data.outBytes = 0;
    data.outCount = 0;
    data.count = m_rxStream.count_();
    data.size = 0;
    data.full = 0;
    data.type = Ztc::QueueType::Rx;
  }

  void txQueueTelemetry(Ztc::QueueTelemetry &data) const
  {
    Cxn *cxn = m_cxn;
    data.ownerID = app()->telKey().template p<1>();
    data.id = m_id;
    data.inCount = m_txInCount;
    data.inBytes = m_txInBytes;
    data.outCount = cxn ? cxn->txCalls() : 0;
    data.outBytes = cxn ? cxn->txBytes() : 0;
    data.count = Tx::txQueue.count_();
    data.size = 0;
    data.full = 0;
    data.type = Ztc::QueueType::Tx;
  }

  // immutable
  App			*m_app = nullptr;
  const ZuID		m_id;
  bool			m_isServer = false;

  // shared
  ZmAtomic<uint64_t>	m_tlsGen = 0;
  ZmAtomic<unsigned>	m_disconnecting = 0;
  // Physical close obligation only; invalidated before successor association.
  ZmAtomic<Cxn *>	m_closing = nullptr;
  // Configured before the link is used, then stable.
  ZiTxErrorFn		m_txErrorFn;
  // Read-mostly connection reference; mutation remains Rx-owned.
  CxnRef		m_cxn = nullptr;

  struct SignatureWatch : public ZmNHeap<SignatureWatch>, public ZmPolymorph {
  friend inline ZuStringT<"Ztls.AsyncWatch"> ZmNHeapID(SignatureWatch *);
    ZmRef<Impl>		link;
    ZmRef<Cxn>		cxn;
    Zi::Handle		handle{};
    const uint64_t	gen;
    unsigned		readCount = 0;
    bool		retired = false;
    bool		armed = false;

    SignatureWatch(ZmRef<Impl> link_, ZmRef<Cxn> cxn_, uint64_t gen_)
    :
      link{ZuMv(link_)}, cxn{ZuMv(cxn_)}, gen{gen_}
    { }
    void ready(Zi::Handle handle_) { link->asyncHandshake_1(ZmRef(this), handle_); }
  };

  // Rx borrows the current physical connection until protocol retirement.
  alignas(Zm::CacheLineSize)
  Cxn			*m_tls = nullptr;
  bool			m_txPending = false;
  bool			m_txDeferred = false;
  bool			m_closePending = false;
  bool			m_handshook = false;
  bool			m_peerClosed = false;
  SignatureWatch	*m_asyncJob = nullptr;
  RxStream		m_rxStream;
  uint64_t		m_rxInCount = 0;
  uint64_t		m_rxInBytes = 0;

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
    // Keeps the Tx members and their physical destination alive together.
    ZmRef<Cxn> m_txCxn;
  uint64_t		m_txInCount = 0;
  uint64_t		m_txInBytes = 0;
};

// client links are persistent, own the (transient) connection
template <typename App, typename Impl, typename RxBufAlloc, typename Cxn>
using CliLink_ = Link<App, Impl, RxBufAlloc, Cxn, ZmRef<Cxn>>;
// server links are transient, are owned by the connection
template <typename App, typename Impl, typename RxBufAlloc, typename Cxn>
using SrvLink_ = Link<App, Impl, RxBufAlloc, Cxn, Cxn *>;

template <
  typename App, typename Impl, typename RxBufAlloc_ = Ztls::RxBufAlloc<>>
class CliLink : public CliLink_<App, Impl, RxBufAlloc_, CliCxn<Impl>> {
public:
  using Cxn = CliCxn<Impl>;
  using RxBufAlloc = RxBufAlloc_;
  using Base = CliLink_<App, Impl, RxBufAlloc, Cxn>;

  using Base::impl;
  using Base::app;
  using Base::resetTLS;
  using Base::handshakeProps;
  using Base::resetHandshakeProps;
  using Base::processRx_;
  using Base::tls;

friend Base;
template <typename> friend class Client;

  CliLink(App *app) : Base{app, false} { }
  CliLink(App *app, ZuID id) : Base{app, false, ZuMv(id)} { }
  CliLink(App *app, Host server, uint16_t port)
  :
    Base{app, false}, m_server{ZuMv(server)}, m_port{port}
  { }
  CliLink(App *app, ZuID id, Host server, uint16_t port)
  :
    Base{app, false, ZuMv(id)}, m_server{ZuMv(server)}, m_port{port}
  { }
  ~CliLink() { }

  void connect()
  { // App thread(s)
    app()->rxInvoke([this]() mutable { connect_(); });
  }
  void connect(Host server, uint16_t port)
  { // App thread(s)
    app()->rxInvoke([this, server = ZuMv(server), port]() mutable
    {
      if (m_connectBusy || !impl()->connectAllowed_()) return;
      m_server = ZuMv(server);
      m_port = port;
      m_remote = ZiIP{};
      connect_();
    });
  }
  void connect(Host server, uint16_t port, ZiIP remote)
  { // App thread(s)
    app()->rxInvoke([this, server = ZuMv(server), port, remote = ZuMv(remote)]() mutable
    {
      if (m_connectBusy || !impl()->connectAllowed_()) return;
      m_server = ZuMv(server);
      m_port = port;
      m_remote = ZuMv(remote);
      connect_();
    });
  }

  const Host &server() const { return m_server; }
  uint16_t port() const { return m_port; }

  void connect_()
  { // direct call from within rx thread
    ZiAssert(app()->rxInvoked(), "Ztls", (), "TLS connect outside Rx thread", return);
    if (m_connectBusy) return;
    if (!impl()->connectAllowed_()) {
      retryConnect_0();
      return;
    }
    cancelReconn_();
    m_connectBusy = true;
    m_connectStopping = false;
    if (auto ip = m_remote) {
      resolved_(ip);
      return;
    }
    // Consume the resolver through its terminal continuation, including both
    // address families. Move the retained owner into the application post.
    ZiResolver::resolve(
      m_server,
      ZiResolver_::ResolveFn{[owner = ZmRef(impl()), ip = ZiIP{}](auto result) mutable
    {
      if (result.template is<ZiIP>()) {
	if (!ip) ip = result.template p<ZiIP>();
	return true;
      }
      auto app = owner->app();
      app->rxRun([owner = ZuMv(owner), ip] { owner->resolved_(ip); });
      return false;
    }});
  }

  bool connectAllowed_() const { return true; }
  void connectRetired_() { }
  bool connectSocket_(bool) { return !m_connectStopping; }
  bool connectReady_() { return !m_connectStopping; }
  void connectClosed_() { m_connectBusy = false; }
  // Shutdown stops new setup work, but submitted DNS/socket operations drain
  // through their ordinary completion. The hub/multiplex must remain alive.
  void stopConnect_() { m_connectStopping = true; }
  void resolved_(ZiIP ip)
  {
    if (!ip) {
      setupFailed_(true);
      return;
    }
    if (m_connectStopping) {
      m_connectBusy = false;
      impl()->connectRetired_();
      return;
    }
    impl()->connectIP_(ip);
  }
  void setupFailed_(bool transient)
  {
    m_connectBusy = false;
    impl()->connectRetired_();
    impl()->connectFailed(transient);
  }
  void connectIP_(ZiIP ip)
  {
    resetTLS();
    app()->mx()->connect(
      ZiConnectFn{ZmRef(impl()),
	[](Impl *impl, const ZiCxnInfo &ci) -> ZiConnection * { return new Cxn(impl, ci); }},
      ZiFailFn{ZmRef(impl()),
	[](Impl *impl, bool transient)
    {
      auto app = impl->app();
      app->rxRun([owner = ZmRef(impl), transient] { owner->setupFailed_(transient); });
    }},
      ZiIP{}, 0, ip, m_port, app()->rxPktAllocator());
  }
private:
  void saveTicket(Ztls_::Bytes input)
  {
    if (!input.length() || !input.data()) {
      m_ticket = {};
      return;
    }
    m_ticket = input;
  }

  // client connected variant - initiate new handshake
  void connected_()
  {
    Ztls_::serverName(tls(), m_server);
    resetHandshakeProps();

    // set up HELO
    auto props = handshakeProps();
    props->client.negotiatedProtocols = app()->alpnList();
    if (m_ticket.length())
      props->client.sessionTicket = Ztls_::wireBytes(m_ticket.data(), m_ticket.length());
    m_maxEarlyData = 0;
    props->client.maxEarlyDataSize = &m_maxEarlyData;
    props->client.earlyDataAcceptance = Ztls_::EarlyDataAcceptance::Unknown;

    // send HELO
    processRx_();
  }
protected:
  size_t maxEarlyData() const { return m_maxEarlyData; }
public:
  void connectFailed(bool transient)
  {
    ZiAssert(app()->rxInvoked(), "Ztls", (), "TLS connect failure outside Rx thread", return);
    if (!m_connectStopping && (!transient || !retryConnect_0()))
      app()->error_(ZeEXCEPT(Error, "Ztls", "connect failed"));
  }
protected:
  void up_() { connect(); }
private:
  void retryConnect_1(uint64_t serial)
  {
    if (m_reconnSerial == serial) connect_();
  }
  bool retryConnect_0()
  {
    unsigned freq = app()->reconnFreq();
    if (!freq) return false;
    auto serial = ++m_reconnSerial;
    app()->mx()->del(&m_reconnTimer);
    app()->mx()->add(
      &m_reconnTimer, Zm::now(freq), ZmScheduler::Update, [this, serial](auto &&arm)
    { return arm([this, serial]() { retryConnect_1(serial); }); }, app()->rxThread());
    return true;
  }
  void cancelReconn_()
  {
    ZiAssert(
      app()->rxInvoked(), "Ztls", (), "TLS reconnect cancellation outside Rx thread", return);
    ++m_reconnSerial;
    app()->mx()->del(&m_reconnTimer);
  }

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  ZmScheduler::Timer	m_reconnTimer;
  Ticket		m_ticket;
  size_t		m_maxEarlyData = 0;
  uint64_t		m_reconnSerial = 0;
  bool			m_connectBusy = false, m_connectStopping = false;
  Host			m_server;
  ZiIP			m_remote;
  uint16_t		m_port;
};

template <
  typename App, typename Impl, typename RxBufAlloc_ = Ztls::RxBufAlloc<>>
class SrvLink : public SrvLink_<App, Impl, RxBufAlloc_, SrvCxn<Impl>> {
public:
  using Cxn = SrvCxn<Impl>;
  using RxBufAlloc = RxBufAlloc_;
  using Base = SrvLink_<App, Impl, RxBufAlloc, Cxn>;

  using Base::impl;
  using Base::app;
  using Base::resetTLS;

friend Base;
template <typename> friend class Server;

  SrvLink(App *app) : Base(app, true) { }
private:
  // The server waits for the client's initial handshake message.
  void connected_() { }

  int onClientHello(Ztls::Cxn *tls, Ztls_::CliHelloParams *params)
  {
    if (params->serverName.data() && params->serverName.length())
      Ztls_::serverName(tls, ZuCSpan{params->serverName});
    return app()->selectALPN(tls, params->negotiatedProtocols);
  }
};

ZuDerive(ALPNRanks,
  (ZmRBTreeKV<ZuCSpan, unsigned,
    ZmRBTreeUnique<true,
      ZmRBTreeHeapID<"Ztls.ALPNRanks">>>));

ZuDerive(
  FailedSignatureReads,
  (ZtArray<ZiEvent::HandleReadFn, ZtArrayHeapID<"Ztls.FailedSignatureReads">>));

template <typename App_> class Hub : public Random, public ZmEngine<App_>, public Ztc::Hub {
friend ZmEngine<App_>;
template <typename, typename, typename, typename, typename> friend class Link;
template <typename, typename, typename> friend class CliLink;
template <typename, typename, typename> friend class SrvLink;
public:
  using App = App_;
  using TxPktAlloc = Zi::IOBufBuiltinAllocator<Zi::IOBufType::TxTCP, TxPacket>;
  using HubCtl = ZmEngine<App>;
  using Links = ZmHash<Ztc::Link *, ZmHashLock<ZmPLock, ZmHashHeapID<"Ztls.Hub.Links">>>;

  using HubCtl::start;
  using HubCtl::stop;
  using HubCtl::state;

  const App *app() const { return static_cast<const App *>(this); }
  App *app() { return static_cast<App *>(this); }

  Hub() : m_id{ZuID{} << "tls:" << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>()} { }
  ~Hub() { release_(); }
private:
  void release_()
  {
    verifyCertFree(m_verify);
    m_verify = nullptr;
    certStoreFree(m_cacert);
    m_cacert = nullptr;
    m_ctx = {};
    m_alpnRanks.clean();
    m_alpnData.init();
    m_alpn.init();
  }
public:
  bool init(HubParams params)
  {
    return init_(ZuMv(params), [](const HubParams &) { return true; });
  }
  bool start() override { return HubCtl::start(); }
  bool stop() override { return HubCtl::stop(); }

  TelKey telKey() const override { return {Ztc::LinkType::TLS, m_id}; }
  void telemetry(Ztc::HubTelemetry &data) const override
  {
    data.id = m_id;
    if (m_mx) data.mxID = m_mx->id();
    data.down = m_down.load_();
    data.disabled = 0;
    data.transient = m_transient.load_();
    data.up = m_up.load_();
    data.reconn = 0;
    data.failed = 0;
    data.nLinks = m_nLinks.load_();
    data.rxThread = m_rxThread;
    data.txThread = m_txThread;
    data.linkType = Ztc::LinkType::TLS;
    data.state = state();
  }
  unsigned allLinks(Ztc::Hub::AllLinksFn fn) const override {
    return app()->allLinks_(ZuMv(fn));
  }
  unsigned allPools(Ztc::Hub::AllPoolsFn fn) const override {
    return app()->allPools_(ZuMv(fn));
  }
protected:
  template <typename Params, typename L> bool init_(Params params, L &&l)
  {
    m_errorFn = ZuMv(params.errorFn());
    if (!m_errorFn) m_errorFn = defaultErrorFn();
    if (!validate_(params)) return false;
    auto mx = params.mx();
    if (params.rxAllocator()->mx != mx) return false;
    auto packet = Alloc_::txPktAllocator(
      mx, static_cast<typename App::TxPktAlloc *>(nullptr));
    if (!packet) {
      error_(ZeEXCEPT(Error, "Ztls", "TLS packet allocator is unavailable"));
      return false;
    }
    auto rxPktAllocator = params.rxPktAllocator();
    if (!rxPktAllocator) {
      rxPktAllocator =
	mx->template allocator<Zi::IOBufType::Rx, ZiRxBuf, Ztls_::RxPacketCapacity>();
      if (!rxPktAllocator) {
	error_(ZeEXCEPT(Error, "Ztls", "TLS Rx packet allocator is unavailable"));
	return false;
      }
    }
    m_mx = mx;
    m_rxAllocator = params.rxAllocator();
    m_rxPktAllocator = rxPktAllocator;
    m_txPktAllocator = packet;
    m_rxThread = thread_(params.rxThread(), m_mx->rxThread());
    m_txThread = thread_(params.txThread(), m_mx->txThread());
    m_asyncThread = params.asyncThread() ? m_mx->sid(params.asyncThread()) : 0;

    if (!initContext(params, l)) return false;
    Ztc::HubMgr::add(this);
    m_registered = true;
    return true;
  }
private:
  unsigned thread_(const ParamString &id, unsigned deflt) const
  { return id ? m_mx->sid(id) : deflt; }

  template <typename Params> bool validate_(const Params &params)
  {
    if (ZuUnlikely(!params.mx())) {
      error_(ZeEXCEPT(Error, "Ztls", "multiplexer is null"));
      return false;
    }
    if (!params.rxAllocator() || params.rxAllocator()->type != Zi::IOBufType::RxTLS) {
      error_(
	ZeEXCEPT(Error, "Ztls", "TLS Rx allocator is required"));
      return false;
    }
    if (auto a = params.rxPktAllocator();
      a && (a->mx != params.mx() || a->type != Zi::IOBufType::Rx)) {
      error_(ZeEXCEPT(Error, "Ztls", "invalid TLS Rx packet allocator"));
      return false;
    }
    if (!params.rxPktAllocator() && params.mx()->state() != ZmEngineState::Stopped) {
      error_(ZeEXCEPT(
	Error, "Ztls", "running multiplexer requires a prepared TLS Rx packet allocator"));
      return false;
    }
    if (params.rxAllocator()->capacity() < Ztls_::RxRecordCapacity) {
      error_(ZeEXCEPT(
	Error, "Ztls", "RxTLS builtin capacity must be at least Ztls_::RxRecordCapacity"));
      return false;
    }
    unsigned rxThread = params.rxThread() ? params.mx()->sid(params.rxThread()) :
					    params.mx()->rxThread();
    unsigned txThread = params.txThread() ? params.mx()->sid(params.txThread()) :
					    params.mx()->txThread();
    if (!rxThread || rxThread > params.mx()->params().nThreads()) {
      error_(ZeEXCEPT(Error, "Ztls", ([thread = LogMsg{params.rxThread()}](auto &s) {
	s << "invalid TLS Rx thread ID \"" << thread << '"';
      })));
      return false;
    }
    if (!txThread || txThread > params.mx()->params().nThreads()) {
      error_(ZeEXCEPT(Error, "Ztls", ([thread = LogMsg{params.txThread()}](auto &s) {
	s << "invalid TLS Tx thread ID \"" << thread << '"';
      })));
      return false;
    }
    if (rxThread == txThread) {
      error_(ZeEXCEPT(Error, "Ztls", "TLS Rx and Tx threads must differ"));
      return false;
    }
    if (params.asyncThread()) {
#ifdef _WIN32
      error_(ZeEXCEPT(
	Error, "Ztls",
	"asyncThread is unsupported on Windows because OpenSSL asynchronous "
	"signing exposes an int fd while HANDLE is pointer-sized"));
      return false;
#else
      unsigned asyncThread = params.mx()->sid(params.asyncThread());
      if (!asyncThread || asyncThread > params.mx()->params().nThreads()) {
	error_(ZeEXCEPT(Error, "Ztls", ([thread = LogMsg{params.asyncThread()}](auto &s) {
	  s << "invalid async thread ID \"" << thread << '"';
	})));
	return false;
      }
      if (asyncThread == rxThread || asyncThread == txThread) {
	error_(ZeEXCEPT(Error, "Ztls", "async thread must differ from TLS Rx and Tx threads"));
	return false;
      }
      if (asyncThread == params.mx()->rxThread() || asyncThread == params.mx()->txThread()) {
	error_(ZeEXCEPT(Error, "Ztls", "async thread must differ from I/O threads"));
	return false;
      }
      if (!params.mx()->params().thread(asyncThread).isolated()) {
	error_(ZeEXCEPT(Error, "Ztls", "async thread must be isolated"));
	return false;
      }
#endif
    }
    return true;
  }

  template <typename Params, typename L> bool initContext(Params &params, L &l)
  {
    if (!cryptoInit()) {
      error_(ZeEXCEPT(Error, "Ztls", "protocol initialization failed"));
      return false;
    }
    m_ctx = {};
    m_ctx.random = &Ztls_::opensslRandom;
    m_ctx.getTime = &Ztls_::getTime;
    m_ctx.keyExchanges = Ztls_::opensslKeyExchangesAll;
    initCipherSuites();
    m_ctx.serverCipherPreference = 1;
    initALPN(params.alpn(), ZuIsSame<ServerParams, Params>{});
    if (!l(params)) return false;
    if (!m_ctx.cipherSuites.serialTx() || !m_ctx.cipherSuites.serialRx()) {
      error_(ZeEXCEPT(Error, "Ztls", "selected AEAD lacks serial record protection"));
      return false;
    }
    return true;
  }
public:
  void final()
  {
    bool ok = HubCtl::lock(
      ZmEngineState::Stopped, [this]()
    {
      if (m_registered) {
	Ztc::HubMgr::del(this);
	m_registered = false;
      }
      final_();
      release_();
      return true;
    });
    ZiAssert(ok, "Ztls", (), "TLS hub finalization while not stopped", return);
  }

  ZiMultiplex *mx() const { return m_mx; }
  unsigned rxThread() const { return m_rxThread; }
  unsigned txThread() const { return m_txThread; }
  unsigned asyncThread() const { return m_asyncThread; }

  template <typename... Args> void rxRun(Args &&...args)
  { m_mx->run(ZuFwd<Args>(args)..., m_rxThread); }
  template <typename... Args> void rxInvoke(Args &&...args)
  { m_mx->invoke(ZuFwd<Args>(args)..., m_rxThread); }
  bool rxInvoked() { return m_mx->invoked(m_rxThread); }
  template <typename... Args> void txRun(Args &&...args)
  { m_mx->run(ZuFwd<Args>(args)..., m_txThread); }
  template <typename... Args> void txInvoke(Args &&...args)
  { m_mx->invoke(ZuFwd<Args>(args)..., m_txThread); }
  bool txInvoked() { return m_mx->invoked(m_txThread); }

  Zi::IOBufAllocator *rxAllocator() const { return m_rxAllocator; }
  Zi::IOBufAllocator *rxPktAllocator() const { return m_rxPktAllocator; }

  void linkConnected_() { }
  void linkDisconnected_() { }
private:
  Zi::IOBufAllocator *txPktAllocator() const { return m_txPktAllocator; }

  void linkAdded_(Ztc::Link *link)
  {
    m_links.add(link);
    Ztc::Hub::linkAdded_(link);
  }
  void linkDeleted_(Ztc::Link *link, LinkState::T state)
  {
    if (m_links.del(link)) {
      switch (state) {
      case LinkState::Down:
	Ztc::Hub::linkDownDec_();
	break;
      case LinkState::Up:
	Ztc::Hub::linkUpDec_();
	break;
      default:
	Ztc::Hub::linkTransientDec_();
	break;
      }
      Ztc::Hub::linkDeleted_(link);
    }
  }
public:
  void linkState_(LinkState::T oldState, LinkState::T newState)
  {
    switch (oldState) {
    case LinkState::Down:
      Ztc::Hub::linkDownDec_();
      break;
    case LinkState::Up:
      Ztc::Hub::linkUpDec_();
      break;
    default:
      Ztc::Hub::linkTransientDec_();
      break;
    }
    switch (newState) {
    case LinkState::Down:
      Ztc::Hub::linkDownInc_();
      break;
    case LinkState::Up:
      Ztc::Hub::linkUpInc_();
      break;
    default:
      Ztc::Hub::linkTransientInc_();
      break;
    }
  }
private:
  bool asyncConfigured() const { return m_asyncThread != 0; }
protected:
  unsigned allLinks_(Ztc::Hub::AllLinksFn fn) const
  {
    unsigned n = 0;
    auto     i = m_links.citer();
    while (Ztc::Link *link = i.key()) {
      ++n;
      fn(link);
    }
    return n;
  }
  template <typename Link> unsigned downLinks()
  {
    using DrainLinks = ZtArray<ZmRef<Link>, ZtArrayHeapID<"Ztls.Hub.Drain">>;
    unsigned capacity = m_links.count_();
    auto     links = ZtScratch(DrainLinks, capacity);
    {
      auto i = m_links.citer();
      while (Ztc::Link *base = i.key()) {
	auto link = static_cast<Link *>(base);
	// Final deref may have started a destructor waiting on this iterator.
	link->template withRef<Link>([&links](ZmRef<Link> ref) { links.push(ZuMv(ref)); });
      }
    }
    for (auto &link : links) link->down();
    return links.length();
  }
  unsigned allPools_(Ztc::Hub::AllPoolsFn) const { return 0; }

  void error_(ZeException e)
  {
    if (m_errorFn)
      m_errorFn(ZuMv(e));
    else
      ZiLogEvent(ZuMv(e));
  }
private:
  void startAsyncLoop()
  {
    if (!asyncConfigured() || m_eventLoopStarted) {
      this->started(true);
      return;
    }
    m_eventLoop.init(
      m_mx, m_asyncThread, ZiEvent::FailFn{[this](ZeException e) { error_(ZuMv(e)); }});
    m_eventLoopInit = true;
    m_eventLoop.start(
      ZiEvent::StartFn{[this](ZiEvent::StartResult result) mutable
    {
      bool ok = !result.is<ZiEvent::Exception>();
      if (!ok) error_(ZuMv(result).p<ZiEvent::Exception>());
      rxRun([this, ok]()
      {
	m_eventLoopStarted = ok;
	if (!ok) {
	  m_eventLoop.final();
	  m_eventLoopInit = false;
	}
	this->started(ok);
      });
    }});
  }

  void stopAsyncLoop()
  {
    if (m_eventLoopStarted) {
      ZmBlock<>{}([this](auto wake) mutable
      {
	m_eventLoop.stop(
	  ZiEvent::StopFn{[this, wake = ZuMv(wake)](ZiEvent::StopResult result) mutable
	{
	  if (result.is<ZiEvent::Exception>()) error_(ZuMv(result).p<ZiEvent::Exception>());
	  wake();
	}});
      });
      m_eventLoopStarted = false;
    }
    if (m_eventLoopInit) {
      m_eventLoop.final();
      m_eventLoopInit = false;
    }
  }

  void final_()
  {
    stopAsyncLoop();
    m_errorFn = ErrorFn{};
  }

  void asyncAddHandle(Zi::Handle handle, ZiEvent::HandleReadFn read)
  {
    if (ZuUnlikely(!m_eventLoopStarted)) {
      m_failedSignatureReads.push(ZuMv(read));
      error_(ZeEXCEPT(Fatal, "Ztls", "async signature registration before event-loop startup"));
      return;
    }
    m_eventLoop.run([this, handle, read = ZuMv(read)]() mutable
    {
      // A returning fatal handler still leaves a provider job outstanding.
      if (m_eventLoop.addHandle(handle, {}, read)) {
	read = {};
	return;
      }
      rxRun([this, read = ZuMv(read)]() mutable { m_failedSignatureReads.push(ZuMv(read)); });
      // addHandle already reported its fatal diagnostic, including the
      // native error where available. A returning handler retains the job.
    });
  }

  template <typename... Args> void asyncRun(Args &&...args)
  {
    if (!m_eventLoopStarted) return;
    m_eventLoop.run(ZuFwd<Args>(args)...);
  }

  void asyncDelHandle(Zi::Handle handle) { m_eventLoop.delHandle(handle); }

  void asyncSignatureStarted() { ++m_asyncSignatures; }

  void asyncSignatureFinished()
  {
    ZmAssert(m_asyncSignatures);
    if (--m_asyncSignatures || !m_stopWaitingForSignatures) return;
    m_stopWaitingForSignatures = false;
    stop_0();
  }
protected:
  void start_() { startAsyncLoop(); }

  // ZmEngine hook; public stop(done) retains done until stop_1() calls
  // stopped(true).  Returning from this function does not complete stop.
  void stop_()
  { // enter Rx thread
    rxRun([this]() { stop_0(); });
  }

  void stop_0()
  { // Rx thread - enter Tx thread
    if (m_asyncSignatures) {
      m_stopWaitingForSignatures = true;
      return;
    }
    txRun([this]() { stop_1(); });
  }

  void stop_1()
  { // Tx thread - complete stop
    this->stopped(true);
  }

  template <typename L> bool spawn(L &&l)
  {
    if (!m_mx || !m_mx->running()) return false;
    rxRun(ZuFwd<L>(l));
    return true;
  }

  void wake()
  {
    if (!m_mx || !m_mx->running()) return;
    rxRun([this]() { this->stopped(); });
  }

  // Ztls Rx thread
  Ztls_::Context *ctx() { return &m_ctx; }
  const Ztls_::Context *ctx() const { return &m_ctx; }

  ZuSpan<const Ztls_::Bytes> alpnList() const { return m_alpn; }

  int selectALPN(Ztls::Cxn *tls, ZuSpan<const Ztls_::Bytes> offers) const
  {
    if (!m_alpn.length()) return 0;
    unsigned best = m_alpn.length();
    for (auto offer : offers) {
      auto rank = m_alpnRanks.find(ZuCSpan{offer});
      if (rank && rank->val() < best) {
	best = rank->val();
	if (!best) break;
      }
    }
    if (best == m_alpn.length()) return Ztls_AlertNoAppProto;
    return Ztls_::negotiatedProtocol(tls, ZuCSpan{m_alpn[best]});
  }

  // Arch/Ubuntu/Debian/SLES - /etc/ssl/certs
  // Fedora/CentOS/RHEL - /etc/pki/tls/certs
  // Android - /system/etc/security/cacerts
  // FreeBSD - /usr/local/share/certs
  // NetBSD - /etc/openssl/certs
  // AIX - /var/ssl/certs
  // Windows - ROOT certificate store (using Cert* API)

  bool loadCA(ZuCSpan path) { return loadCA_(path ? path.data() : nullptr, nullptr); }
  bool loadCA(ZuCSpan path, const Certs &certs)
  { return loadCA_(path ? path.data() : nullptr, &certs); }
  bool loadCA_(const char *path, const Certs *certs)
  {
    if (!m_cacert) m_cacert = certStoreNew();
    if (!m_cacert) {
      error_(ZeEXCEPT(Error, "Ztls", "certStoreNew() failed"));
      return false;
    }
    if (!path) {
#ifndef _WIN32
      if (ZiStat{"/etc/lsb-release"}.exists()) // Arch/Ubuntu/Debian/SLES
	path = "/etc/ssl/certs";
      else if (ZiStat{"/etc/redhat-release"}.exists()) // Fedora/CentOS/RHEL
	path = "/etc/pki/tls/certs";
      else if (ZiStat{"/system/etc/security/cacerts"}.isdir()) // Android
	path = "/system/etc/security/cacerts";
      else if (ZiStat{"/usr/local/share/certs"}.isdir()) // FreeBSD
	path = "/usr/local/share/certs";
      else if (ZiStat{"/etc/openssl/certs"}.isdir()) // NetBSD
	path = "/etc/openssl/certs";
      else if (ZiStat{"/var/ssl/certs"}.isdir()) // AIX
	path = "/var/ssl/certs";
      else // unknown - default to LSB
	path = "/etc/ssl/certs";
#else
      auto store = CertOpenSystemStore(nullptr, "ROOT"); // Windows
      if (!store) {
	error_(ZeEXCEPT(Error, "Ztls", ([e = ZeLastError](auto &s) {
	  s << "CertOpenSystemStore(nullptr, \"ROOT\") failed: " << e;
	})));
	return false;
      }

      PCCERT_CONTEXT context = nullptr;
      while (context = CertEnumCertificatesInStore(store, context)) {
	if (!certStoreAddDER(m_cacert, {context->pbCertEncoded, context->cbCertEncoded})) {
	  error_(ZeEXCEPT(Error, "Ztls", "certStoreAddDER() failed"));
	  CertFreeCertificateContext(context);
	  CertCloseStore(store, 0);
	  return false;
	}
      }

      CertCloseStore(store, 0);
#endif
    }
    if (path) {
      bool	  ok;
      const char *function;
      if (ZiStat{path}.isdir()) {
	function = "certStoreLoadPath";
	ok = certStoreLoadPath(m_cacert, path);
      } else {
	function = "certStoreLoadFile";
	ok = certStoreLoadFile(m_cacert, path);
      }
      if (!ok) {
	error_(ZeEXCEPT(Error, "Ztls", ([function, path](auto &s) {
	  s << function << "(\"" << path << "\") failed";
	})));
	return false;
      }
    }
    if (certs) {
      unsigned n = certs->length();
      for (unsigned i = 0; i < n; ++i) {
	const auto &cert = (*certs)[i];
	if (!certStoreAddCA(m_cacert, cert)) {
	  error_(ZeEXCEPT(Error, "Ztls", "certStoreAddCA() failed"));
	  return false;
	}
      }
    }
    if (m_verify) verifyCertFree(m_verify);
    m_verify = verifyCertNew(m_cacert);
    if (!m_verify) {
      error_(ZeEXCEPT(Error, "Ztls", "verifyCertNew() failed"));
      return false;
    }
    m_ctx.verifyCertificate = verifyCertHook(m_verify);
    return true;
  }
protected:
  void initALPN(const ParamStrings &alpn, bool server)
  {
    m_alpnRanks.clean();
    m_alpn = {};
    m_alpnData = {};
    if (!alpn.length()) return;
    unsigned bytes = 0;
    for (auto &s : alpn) bytes += s.length();
    if (bytes) m_alpnData.ensure(bytes);
    m_alpn.ensure(alpn.length());
    for (auto &s : alpn) {
      unsigned offset = m_alpnData.length();
      m_alpnData << ZuBSpan{s};
      m_alpn.push(Ztls_::Bytes{m_alpnData.data() + offset, s.length()});
      if (server) {
	ZuCSpan name{m_alpnData.data() + offset, s.length()};
	if (!m_alpnRanks.find(name)) m_alpnRanks.add(name, m_alpn.length() - 1);
      }
    }
  }
private:
  void initCipherSuites()
  {
#if ZtlsFusion
    if (Ztls_::fusionSupported()) {
      static const Ztls_::CipherSuite fusionAes256gcmsha384 = {.id = Ztls_SuiteAes256gcmSHA384,
	.aead = &Ztls_::fusionAes256gcm,
	.hash = &Ztls_::opensslSha384,
	.name = Ztls_SuiteStrAes256gcm};
      static const Ztls_::CipherSuite fusionAes128gcmsha256 = {.id = Ztls_SuiteAes128gcmSHA256,
	.aead = &Ztls_::fusionAes128gcm,
	.hash = &Ztls_::opensslSha256,
	.name = Ztls_SuiteStrAes128gcm};
      // Non-temporal fusion decrypt is not exact-in-place safe: it overwrites
      // ciphertext before GHASH finishes reading it. Use normal fusion AEADs.
      const Ztls_::CipherSuite *const suites[] = {
	&fusionAes256gcmsha384, &fusionAes128gcmsha256,
#if ZtlsOpenSSLHaveChaCha
	&Ztls_::opensslChachasha256,
#endif
      };
      m_ctx.cipherSuites = Ztls_::SuiteSpan{suites};
      return;
    }
#endif
    m_ctx.cipherSuites = Ztls_::opensslCipherSuites;
  }

  using AllocatorRef = Zi::IOBufAllocator *;

  // stable after init(); m_errorFn is cleared during final()
  const ZuID		m_id;
  AllocatorRef		m_rxAllocator = nullptr;
  AllocatorRef		m_rxPktAllocator = nullptr;
  AllocatorRef		m_txPktAllocator = nullptr;
  ZiMultiplex		*m_mx = nullptr;
  ErrorFn		m_errorFn;
  Ztls_::Context	m_ctx{};
  ALPNData		m_alpnData;
  ALPN			m_alpn;
  ALPNRanks		m_alpnRanks;
  CertStore		*m_cacert = nullptr;
  VerifyCert		*m_verify = nullptr;
  unsigned		m_rxThread = 0;
  unsigned		m_txThread = 0;
  // Rx-owned. Stop keeps readiness delivery active through retirement.
  FailedSignatureReads	m_failedSignatureReads;
  unsigned		m_asyncSignatures = 0;
  bool			m_stopWaitingForSignatures = false;
  unsigned		m_asyncThread = 0;
  bool			m_registered = false;

  // shared
  // Exceptional lock-guarded registry used by lifecycle and telemetry callers.
  Links			m_links;

  // Exceptional lifecycle/async coordinator.  Lifecycle code initializes,
  // starts, stops, and finalizes it; internal I/O runs on m_asyncThread.
  alignas(Zm::CacheLineSize)
  ZiEventLoop		m_eventLoop;
  bool			m_eventLoopInit = false;
  bool			m_eventLoopStarted = false;
};

// CRTP - implementation must conform to the following interface:
#if 0
struct App : public Client<App> {
  using RxBufAlloc = Ztls::RxBufAlloc<>;
  using TxBufAlloc = Ztls::TxBufAlloc<BufSize>;

  void exception(ZmRef<ZeEvent>); // optional

  struct Link : public CliLink<App, Link, RxBufAlloc> {
    // Ztls Rx thread - handshake completed
    void connected(Ztls::Connected);

    void disconnected(bool peer); // Ztls Rx thread
    void connectFailed(bool transient); // I/O Tx thread

    // process() should return:
    // +ve - consumed some data and can continue
    // 0   - more data needed - leave buffers queued
    // -ve - disconnect, abandon any remaining Rx data
    int process(RxStream &); // process received data

    unsigned reconnFreq() const; // optional
  };
};
#endif
template <typename App> class Client : public Hub<App> {
template <typename, typename, typename> friend class CliLink;
public:
  using Base = Hub<App>;
friend Base;

  using Base::error_;
  using Base::loadCA;

  const App *app() const { return static_cast<const App *>(this); }
  App *app() { return static_cast<App *>(this); }

  Client() { }
  ~Client() { signCertFree(m_sign); }

  // specify certPath and keyPath for mTLS
  bool init(ClientParams params);

  void final() { Base::final(); }
protected:
  unsigned reconnFreq() const { return 0; } // default
private:
  // stable after init(); released by the destructor
  PK::Key		m_ownedKey;
  const PK::Key		*m_key = nullptr;
  SignCert		*m_sign = nullptr;
  ZmRef<PK::AnyPK>	m_keyRef;
};

template <typename App> bool Client<App>::init(ClientParams params)
{
  using Link = App::Link;

  if (bool(params.certPath()) != bool(params.keyPath())) {
    auto errorFn = params.errorFn() ? params.errorFn() : defaultErrorFn();
    errorFn(ZeEXCEPT(Error, "Ztls", "client certPath and keyPath must be configured together"));
    return false;
  }
  if (params.certPath() && params.key()) {
    auto errorFn = params.errorFn() ? params.errorFn() : defaultErrorFn();
    errorFn(
      ZeEXCEPT(Error, "Ztls", "client path and in-memory credentials are mutually exclusive"));
    return false;
  }

  return Base::init_(
    ZuMv(params), [this](ClientParams &params) -> bool
  {
    struct SaveSessionTicket : Ztls_::SaveTicket {
      int call(Ztls::Cxn *tls, Ztls_::Bytes input) override
      {
	auto link = static_cast<Link *>(*Ztls_::dataPtr(tls));
	if (link) link->saveTicket(input);
	return 0;
      }
    };
    static SaveSessionTicket saveTicketHook;
    auto		     ctx = this->ctx();
    ctx->onClientHello = nullptr;
    ctx->saveTicket = &saveTicketHook;
    ctx->signCertificate = nullptr;
    ctx->encryptTicket = nullptr;
    ctx->requireCliAuth = 0;
    if (!loadCA(ZuCSpan{params.caPath()}, params.caCerts())) return false;

    if (params.certPath() && params.keyPath()) {
      if (!loadCerts(ctx, params.certPath().data())) return false;
      if (!PK::loadPEM(m_ownedKey, params.keyPath().data())) return false;
      m_key = &m_ownedKey;
      m_sign = signCertNew(*m_key);
      if (!m_sign) {
	m_ownedKey.reset(nullptr);
	m_key = nullptr;
	return false;
      }
      ctx->signCertificate = signCertHook(m_sign);
    } else if (params.key()) {
      m_keyRef = ZuMv(params).key();
      const auto &certs = params.certs();
      unsigned	  n = certs.length();
      auto	  spans = ZtScratch(CertSpansScratch, n);
      for (unsigned i = 0; i < n; ++i) spans.push(certs[i]);
      m_key = &m_keyRef->key;
      m_sign = signCertNew(*m_key);
      if (!m_sign) {
	m_key = nullptr;
	m_keyRef = {};
	return false;
      }
      if (!loadCertsDER(ctx, spans, *m_key)) {
	signCertFree(m_sign);
	m_sign = nullptr;
	m_key = nullptr;
	m_keyRef = {};
	return false;
      }
      ctx->signCertificate = signCertHook(m_sign);
    }
    return true;
  });
}

// CRTP - implementation must conform to the following interface:
#if 0
struct App : public Server<App> {
  using RxBufAlloc = Ztls::RxBufAlloc<>;
  using TxBufAlloc = Ztls::TxBufAlloc<BufSize>;

  void exception(ZmRef<ZeEvent>); // optional

  struct Link : public SrvLink<App, Link, RxBufAlloc> {
    // Ztls Rx thread - handshake completed
    void connected(Ztls::Connected);

    void disconnected(bool peer); // Ztls Rx thread
    void connectFailed(bool transient); // I/O Tx thread

    // process() should return:
    // +ve - consumed some data and can continue
    // 0   - more data needed - leave buffers queued
    // -ve - disconnect, abandon any remaining Rx data
    int process(RxStream &); // process received data
  };

  Link::Cxn *accepted(const ZiCxnInfo &ci) {
    // ... potentially return nullptr if too many open connections
    return new Link::Cxn(new Link(this), ci);
  }

  ZiIP localIP() const;
  unsigned localPort() const;
  unsigned nAccepts() const; // optional
  unsigned rebindFreq() const; // optional

  void listening(const ZiListenInfo &info); // optional
  void listenFailed(bool transient); // optional - can re-schedule listen()
};
#endif
template <typename App_> class Server : public Hub<App_> {
friend ZmEngine<App_>;
public:
  using App = App_;
  using Base = Hub<App>;
friend Base;

  using Base::loadCA;
  using Base::mx;
  using Base::error_;
  using Base::stop;

  const App *app() const { return static_cast<const App *>(this); }
  App *app() { return static_cast<App *>(this); }

  Server() { }
  ~Server() { release_(); }
private:
  void release_()
  {
    ticketKeyFree(m_ticketKey);
    m_ticketKey = nullptr;
    signCertFree(m_sign);
    m_sign = nullptr;
    m_ownedKey.reset(nullptr);
    m_key = nullptr;
  }
public:
  bool init(ServerParams params);

  void final()
  {
    Base::final();
    release_();
  }

  void listen()
  {
    if (!this->running()) return;
    mx()->listen(
      ZiListenFn{app(),
	[](App *app, const ZiListenInfo &info)
    {
      auto server = static_cast<Server *>(app);
      server->rxRun([server, info = ZiListenInfo{info}]() { server->listening_(info); });
    }},
      ZiFailFn{app(),
	[](App *app, bool transient)
    {
      auto server = static_cast<Server *>(app);
      server->rxRun([server, transient]() { server->listenFailed_(transient); });
    }},
      ZiConnectFn{
	app(), [](App *app, const ZiCxnInfo &ci) -> ZiConnection * { return app->accepted(ci); }},
      app()->localIP(), app()->localPort(), app()->nAccepts(), app()->rxPktAllocator(),
      ZiCxnOptions());
  }

  void stopListening()
  {
    mx()->del(&m_rebindTimer);
    if (m_listening) mx()->stopListening(m_listenIP, m_listenPort);
    m_listening = false;
  }

  void linkConnected_()
  {
    ZiAssert(
      this->rxInvoked(), "Ztls", (), "TLS server link admission outside Rx thread", return);
    ++m_pending;
  }
  void linkDisconnected_()
  {
    ZiAssert(
      this->rxInvoked(), "Ztls", (), "TLS server link completion outside Rx thread", return);
    ZiAssert(m_pending, "Ztls", (), "TLS server disconnect without pending connection", return);
    if (!--m_pending && this->stopping() && m_stopDrained) Base::stop_0();
  }
protected:
  unsigned nAccepts() const { return 8; }   // default
  unsigned rebindFreq() const { return 0; } // default

  void listening_(const ZiListenInfo &info)
  {
    ZiAssert(this->rxInvoked(), "Ztls", (), "TLS listen completion outside Rx thread", return);
    if (!this->running()) {
      m_listenClosing = true;
      mx()->stopListening(info.ip, info.port);
      return;
    }
    m_listenIP = info.ip;
    m_listenPort = info.port;
    m_listening = true;
    app()->listening(info);
  }
  void listenFailed_(bool transient)
  {
    ZiAssert(this->rxInvoked(), "Ztls", (), "TLS listen failure outside Rx thread", return);
    if (!this->running()) return;
    app()->listenFailed(transient);
  }

  void listening(const ZiListenInfo &info)
  { // default
    ZiLOG(Info, "Ztls", ([info](auto &s) {
      s << "listening(" << info.ip << ':' << info.port << ')';
    }));
  }
  void listenFailed(bool transient)
  { // default
    unsigned rebindFreq = app()->rebindFreq();
    if (transient && rebindFreq > 0)
      app()->mx()->add(
	&m_rebindTimer, Zm::now(rebindFreq), ZmScheduler::Update,
	[this](auto &&arm) { return arm([this]() { listen(); }); }, app()->rxThread());
    else
      app()->error_(ZeEXCEPT(Error, "Ztls", ([transient](auto &s) {
	s << "listen() failed " << (transient ? "(transient)" : "");
      })));
  }

  // ZmEngine hook.  Public stop(done) retains done until Base::stop_1()
  // calls stopped(true) after every continuation below has drained.
  void stop_()
  {
    this->rxRun([this]() { stopRx_(); });
  }

  void stopRx_()
  {
    ZiAssert(
      this->rxInvoked(), "Ztls", (), "TLS server stop initialization outside Rx thread", return);
    stopListening();
    m_stopDrained = false;
    // Stop the listener on the I/O Rx shard before draining accepted
    // connections on our Rx shard; down() requires the installed m_cxn.
    mx()->rxRun([this]() { this->rxRun([this]() { stop_0(); }); });
  }

  void stop_0()
  {
    ZiAssert(this->rxInvoked(), "Ztls", (), "TLS server stop drain outside Rx thread", return);
    if (m_listenClosing) {
      m_listenClosing = false;
      // A late bind queued its close after the initial I/O barrier. Drain that
      // close and any accepted-connection notifications before downLinks().
      mx()->rxRun([this]() { this->rxRun([this]() { stop_0(); }); });
      return;
    }
    // Link lifetime can extend beyond disconnect completion. Count accepted
    // connections until their completion, not retained telemetry links.
    this->template downLinks<typename App::Link>();
    this->rxRun([this]() { stop_2(); });
  }

  void stop_2()
  {
    ZiAssert(
      this->rxInvoked(), "Ztls", (), "TLS server stop completion outside Rx thread", return);
    m_stopDrained = true;
    if (!m_pending) Base::stop_0();
  }
private:
  // stable after init(); released by the destructor
  PK::Key		m_ownedKey;
  const PK::Key		*m_key = nullptr;
  SignCert		*m_sign = nullptr;
  TicketKey		*m_ticketKey = nullptr;

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  ZmScheduler::Timer	m_rebindTimer;
  unsigned		m_pending = 0;
  ZiIP			m_listenIP;
  uint16_t		m_listenPort = 0;
  bool			m_listening = false;
  bool			m_listenClosing = false;
  bool			m_stopDrained = false;
};

template <typename App> bool Server<App>::init(ServerParams params)
{
  using Link = App::Link;

  m_stopDrained = false;

  if (!params.certPath()) {
    auto errorFn = params.errorFn() ? params.errorFn() : defaultErrorFn();
    errorFn(ZeEXCEPT(Error, "Ztls", "server certPath is required"));
    return false;
  }
  if (!params.keyPath()) {
    auto errorFn = params.errorFn() ? params.errorFn() : defaultErrorFn();
    errorFn(ZeEXCEPT(Error, "Ztls", "server keyPath is required"));
    return false;
  }

  return Base::init_(
    ZuMv(params), [this](const ServerParams &params) -> bool
  {
    struct ClientHelloHook : Ztls_::OnClientHello {
      int call(Ztls::Cxn *tls, Ztls_::CliHelloParams *params) override
      {
	auto link = static_cast<Link *>(*Ztls_::dataPtr(tls));
	return link ? link->onClientHello(tls, params) : 0;
      }
    };
    static ClientHelloHook cliHelloHook;
    auto		   ctx = this->ctx();
    ctx->onClientHello = &cliHelloHook;
    ctx->signCertificate = nullptr;
    ctx->encryptTicket = nullptr;
    ctx->saveTicket = nullptr;
    ctx->requireCliAuth = params.mTLS() ? 1 : 0;
    ctx->maxEarlyDataSize = 0;
    ctx->ticketLifetime = params.cacheTimeout() < 0 ? 86400 : params.cacheTimeout();
    if (!loadCA(ZuCSpan{params.caPath()})) return false;

    if (!loadCerts(ctx, params.certPath().data())) return false;
    if (!PK::loadPEM(m_ownedKey, params.keyPath().data())) return false;
    m_key = &m_ownedKey;
    m_sign = signCertNew(*m_key);
    if (!m_sign) {
      m_ownedKey.reset(nullptr);
      m_key = nullptr;
      return false;
    }
    if (params.asyncThread() && !signCertAsync(m_sign, true)) {
      error_(ZeEXCEPT(Error, "Ztls", "async server certificate signing is unsupported"));
      return false;
    }
    ctx->signCertificate = signCertHook(m_sign);
    if (!m_ticketKey) m_ticketKey = ticketKeyNew();
    if (!m_ticketKey) return false;
    ctx->encryptTicket = ticketEncryptHook(m_ticketKey);
    return true;
  });
}

} // namespace Ztls

// Ztls_HH
#endif
