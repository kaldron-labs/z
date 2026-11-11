//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// zpicotls/OpenSSL wrapper - main TLS component

#ifndef Ztls_HH
#define Ztls_HH

#include <zlib/ZtlsLib.hh>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuSpan.hh>

#include <zlib/ZmHeap.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmEngine.hh>
#include <zlib/ZmPLock.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtScratch.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiAssert.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiResolver.hh>
#include <zlib/ZiEventLoop.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiTx.hh>
#include <zlib/ZiRx.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/ZiTxStream.hh>
#include <zlib/ZtcHub.hh>

#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsBackend.hh>
#include <zlib/ZtlsPK.hh>

#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <fcntl.h>
#endif

namespace Ztls_ {

// Plaintext buffers hold one complete TLS application record.
enum {
  TxRecordCapacity = 16 << 10,
  // Includes TLS 1.3 inner type/padding within zpicotls' supported bound.
  RxRecordCapacity = PTLS_RECEIVE_RECORD_CAPACITY,
  // Local packet payload geometry; independent of the peer/outbound MPS.
  RxPacketCapacity = 1460
};

using RxQueue = ZiRxQueue;
using TxQueue = ZiTxQueue;
using RxStream = ZiRxStream<RxQueue>;

template <
  unsigned Size = Ztls_::RxRecordCapacity,
  unsigned MaxSize = ZiIOBuf_DefltVHMaxSize,
  ZuString HeapID = ZiIOBuf_HeapID{}()>
using RxBufAlloc =
  Zi::IOBufBuiltinAllocator<Zi::IOBufType::RxTLS,
    RxQueue::Node, Size, ZiIOBuf_Align, ZuStringT<HeapID>, MaxSize>;

template <
  unsigned Size = TxRecordCapacity,
  unsigned MaxSize = ZiIOBuf_DefltVHMaxSize,
  ZuString HeapID = ZiIOBuf_HeapID{}()>
using TxBufAlloc =
  Zi::IOBufBuiltinAllocator<Zi::IOBufType::TxTLS,
    TxQueue::Node, Size, ZiIOBuf_Align, ZuStringT<HeapID>, MaxSize>;

} // namespace Ztls_

namespace Ztls {

using TxPacket = Backend::TxPacket;

ZuDerive(CertSpansScratch, (ZtArray<ZuBSpan,
  ZtArrayHeapID<"Ztls.CertSpans", ZtArraySharded<true>>>));

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
ZuDerive(ALPN, (ZtArray<ptls_iovec_t, ZtArrayHeapID<"Ztls.ALPN">>));
using ErrorFn = ZmFn<void(ZeException), ZmFnHeapID<"Ztls.ErrorFn">>;
ZuDerive(ParamString, ZtString<ZtStringHeapID<"Ztls.Param">>);
ZuDerive(ParamStrings,
  (ZtArray<ParamString, ZtArrayHeapID<"Ztls.ParamStrings">>));
ZuDerive(Cert, (ZtArray<uint8_t, ZtArrayHeapID<"Ztls.Cert">>));
ZuDerive(Certs, (ZtArray<Cert, ZtArrayHeapID<"Ztls.Certs">>));

inline ErrorFn defaultErrorFn()
{
  return ErrorFn{[](ZeException e) { ZiLogEvent(ZuMv(e)); }};
}

struct HubParams {
  HubParams(
    ZiMultiplex *mx = nullptr,
    ZuCSpan rxThread = {},
    ZuCSpan txThread = {}) :
      m_mx{mx}, m_rxThread{rxThread}, m_txThread{txThread},
      m_errorFn{defaultErrorFn()} { }

  HubParams &&rxAllocator(Zi::IOBufAllocator *v) {
    m_rxAllocator = v; return ZuMv(*this);
  }
  HubParams &&txAllocator(Zi::IOBufAllocator *v) {
    m_txAllocator = v; return ZuMv(*this);
  }
  HubParams &&txPacketAllocator(Zi::IOBufAllocator *v) {
    m_txPacketAllocator = v; return ZuMv(*this);
  }
  HubParams &&socketRxAllocator(Zi::IOBufAllocator *v) {
    m_socketRxAllocator = v; return ZuMv(*this);
  }

  HubParams &&caPath(ZuCSpan v) { m_caPath = v; return ZuMv(*this); }
  HubParams &&certPath(ZuCSpan v) { m_certPath = v; return ZuMv(*this); }
  HubParams &&keyPath(ZuCSpan v) { m_keyPath = v; return ZuMv(*this); }
  HubParams &&asyncThread(ZuCSpan v) {
    m_asyncThread = v;
    return ZuMv(*this);
  }
  HubParams &&alpn(ZuSpan<ZuCSpan> v) {
    m_alpn = {};
    m_alpn.ensure(v.length());
    for (auto &s : v) m_alpn.push(ParamString{s});
    return ZuMv(*this);
  }
  HubParams &&errorFn(ErrorFn v) { m_errorFn = ZuMv(v); return ZuMv(*this); }

  ZiMultiplex *multiplex() const { return m_mx; }
  ZiMultiplex *mx() const { return m_mx; }
  Zi::IOBufAllocator *rxAllocator() const { return m_rxAllocator; }
  Zi::IOBufAllocator *txAllocator() const { return m_txAllocator; }
  Zi::IOBufAllocator *txPacketAllocator() const { return m_txPacketAllocator; }
  Zi::IOBufAllocator *socketRxAllocator() const { return m_socketRxAllocator; }
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
  Allocator	*m_txAllocator = nullptr;
  Allocator	*m_txPacketAllocator = nullptr;
  Allocator	*m_socketRxAllocator = nullptr;
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
  template <typename L>
  ClientParams &&hub(L &&l) {
    ZuFwd<L>(l)(static_cast<HubParams &>(*this));
    return ZuMv(*this);
  }

  ClientParams &&key(ZmRef<PK::AnyPK> v)
    { m_key = ZuMv(v); return ZuMv(*this); }
  ClientParams &&certs(Certs v)
    { m_certs = ZuMv(v); return ZuMv(*this); }
  ClientParams &&caCerts(Certs v)
    { m_caCerts = ZuMv(v); return ZuMv(*this); }

  const ZmRef<PK::AnyPK> &key() const { return m_key; }
  const Certs &certs() const { return m_certs; }
  const Certs &caCerts() const { return m_caCerts; }

private:
  template <typename> friend class Client;

  ZmRef<PK::AnyPK> m_key;
  Certs		m_certs;
  Certs		m_caCerts;
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
    { HubParams::caPath(v); return ZuMv(*this); }
  ServerParams &&certPath(ZuCSpan v)
    { HubParams::certPath(v); return ZuMv(*this); }
  ServerParams &&keyPath(ZuCSpan v)
    { HubParams::keyPath(v); return ZuMv(*this); }
  ServerParams &&asyncThread(ZuCSpan v)
    { HubParams::asyncThread(v); return ZuMv(*this); }
  ServerParams &&alpn(ZuSpan<ZuCSpan> v)
    { HubParams::alpn(v); return ZuMv(*this); }
  ServerParams &&errorFn(ErrorFn v)
    { HubParams::errorFn(ZuMv(v)); return ZuMv(*this); }
  ServerParams &&mTLS(bool v) { m_mTLS = v; return ZuMv(*this); }
  ServerParams &&cacheTimeout(int v) { m_cacheTimeout = v; return ZuMv(*this); }

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

// The zpicotls handshake and record receive state is Rx-owned.  Established
// record transmission is detached into a Tx-owned ptls_tx_t.

// API functions: listen, connect, disconnect/disconnect_, txStream/txStream_ (Tx)
// API callbacks: accepted, connected, disconnected, process (Rx)

// Function Category | I/O Threads |        TLS threads         | App threads
// ------------------|-------------|----------------------------|------------
// Server            | accepted()  | connected() disconnected() | listen()
// Client            |             | connect_() connectFailed() | connect()
// Disconnect        |             | disconnect_()              | disconnect()
// Transmission (Tx) |             | txStream_()                | txStream()
// Reception    (Rx) |             | process()                  |

// ZiIOBuf buffers transport data between threads (--> arrows below)

// I/O Threads |                   TLS threads                   | App threads
// ------------|-------------------------------------------------|------------
//     I/O Rx --> Rx input  -> Decryption -> Rx output -> App Rx |
// ------------|-------------------------------------------------|
//     I/O Tx <-- Tx output <- Encryption <- Tx input           <-- App Tx
// ------------|-------------------------------------------------|------------

template <typename Link_, typename LinkRef_>
class Cxn : public ZiConnection {
public:
  using Link = Link_;
  using LinkRef = LinkRef_;

  Cxn(LinkRef link, const ZiCxnInfo &ci) :
    ZiConnection(link->app()->mx(), ci), m_link(ZuMvPtr(link)) { }

  void connected(ZiRxContext &io) override { m_link->connected_0(this, io); }
  void disconnected(bool peer) override {
    LinkRef link_ = ZuMvPtr(m_link);
    if (Link *link = link_)
      link->disconnected_0(this, ZuMvPtr(link_), peer);
  }
private:
  // immutable
  LinkRef	m_link = nullptr;
};

// client links are persistent, own the (transient) connection
template <typename Link> using CliCxn = Cxn<Link, Link *>;
// server links are transient, are owned by the connection
template <typename Link> using SrvCxn = Cxn<Link, ZmRef<Link>>;

using RxStream = Ztls_::RxStream;

template <
  unsigned Size = Ztls_::RxRecordCapacity,
  unsigned MaxSize = ZiIOBuf_DefltVHMaxSize,
  ZuString HeapID = "Ztls.RxBuf">
using RxBufAlloc = Ztls_::RxBufAlloc<Size, MaxSize, HeapID>;

template <
  unsigned Size = Ztls_::TxRecordCapacity,
  unsigned MaxSize = ZiIOBuf_DefltVHMaxSize,
  ZuString HeapID = "Ztls.TxBuf">
using TxBufAlloc = Ztls_::TxBufAlloc<Size, MaxSize, HeapID>;

template <
  typename App, typename Impl, typename RxBufAlloc_, typename TxBufAlloc_,
  typename Cxn_, typename CxnRef_>
class Link :
  public ZmPolymorph,
  public ZiTx<Impl>,
  public Ztc::Link
{
  ZuAssert((ZuIs_<typename RxBufAlloc_::Buf, Ztls_::RxQueue::Node>{}));
  ZuAssert((ZuIs_<typename TxBufAlloc_::Buf, Ztls_::TxQueue::Node>{}));

  class TelQueue final : public Ztc::Queue {
  public:
    TelQueue(const Link *link, Ztc::QueueType::T type) :
      m_link{link}, m_type{type} { }

    TelKey telKey() const override {
      auto linkKey = m_link->telKey();
      return {linkKey.template p<0>(), linkKey.template p<1>(), m_type};
    }
    void telemetry(Ztc::QueueTelemetry &data) const override {
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
  using TxBufAlloc = TxBufAlloc_;
  using RxAllocator = Zi::IOBufAllocator;
  using TxAllocator = Zi::IOBufAllocator;
  using Cxn = Cxn_;
  using CxnRef = CxnRef_;
  using Tx = ZiTx<Impl>;
  using ImplRef = Cxn::LinkRef;
  using Stream = Impl;
  using StreamRef = Impl *;

friend Cxn;

protected:
  using Tx::send;

private:
  struct AsyncJob;
  using AsyncJobRef = ZmRef<AsyncJob>;

public:
  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  Link(App *app, bool isServer) :
    Link{app, isServer, ZuID{} << "tls:" <<
      ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>()} { }
  Link(App *app, bool isServer, ZuID id) :
    m_app{app}, m_id{ZuMv(id)}, m_isServer{isServer} {
    app->linkAdded_(this);
  }
  ~Link() {
    ZmAssert(!m_txTLS);
    ZmAssert(!m_txCxn);
    app()->linkDeleted_(this, state_());
    if (m_tls) {
      if (ZuUnlikely(asyncPending_()))
	app()->error_(ZeEXCEPT(Error, "Ztls",
	  "TLS link destroyed with async job pending"));
      else
	ptls_free(m_tls);
    }
  }

  App *app() const { return m_app; }
  static Zi::IOBufAllocator *rxAllocator(ZiMultiplex *mx) {
    return [mx]<
      Zi::IOBufType::T Type, typename Buf, unsigned Size, unsigned Align,
      typename HeapID, unsigned VHMaxSize, auto Grow
    >(Zi::IOBufBuiltinAllocator<Type, Buf, Size, Align, HeapID, VHMaxSize, Grow> *) {
      return mx->allocator<Type, Buf, Size, Align, HeapID, VHMaxSize, Grow>();
    }(static_cast<RxBufAlloc *>(nullptr));
  }
  static Zi::IOBufAllocator *txAllocator(ZiMultiplex *mx) {
    return [mx]<
      Zi::IOBufType::T Type, typename Buf, unsigned Size, unsigned Align,
      typename HeapID, unsigned VHMaxSize, auto Grow
    >(Zi::IOBufBuiltinAllocator<Type, Buf, Size, Align, HeapID, VHMaxSize, Grow> *) {
      return mx->allocator<Type, Buf, Size, Align, HeapID, VHMaxSize, Grow>();
    }(static_cast<TxBufAlloc *>(nullptr));
  }
  static Zi::IOBufAllocator *txPacketAllocator(
    ZiMultiplex *mx, unsigned count = ZiIOBuf_DefltCount)
  {
    // Validate the actual record reservation after provider activation and
    // negotiation; allocator provisioning encapsulates packet backing.
    return mx->allocator<Zi::IOBufType::TxTCP, TxPacket>(count);
  }
  Zi::IOBufAllocator *txPacketAllocator() const {
    return app()->txPacketAllocator();
  }
  RxAllocator *rxAllocator() const {
    return app()->rxAllocator();
  }
  TxAllocator *txAllocator() const {
    return static_cast<TxAllocator *>(app()->txAllocator());
  }
  Cxn *cxn() const { return m_cxn; }
  bool disconnecting() const { return m_disconnecting.load_(); }
  StreamRef stream() { return impl(); }
  TelKey telKey() const override {
    return {app()->telKey().template p<1>(), m_id};
  }
  void telemetry(Ztc::LinkTelemetry &data) const override {
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
  unsigned allQueues(Ztc::QueueMgr::AllFn fn) const override {
    TelQueue rxQueue{this, Ztc::QueueType::Rx};
    fn(&rxQueue);
    TelQueue txQueue{this, Ztc::QueueType::Tx};
    fn(&txQueue);
    return 2;
  }
  void up() override { impl()->up_(); }
  void down() override { disconnect(); }
  TlsInfo tlsInfo() const {
    if (!m_tls || !m_handshook) return {};
    auto tls = const_cast<ptls_t *>(m_tls);
    auto cipher = ptls_get_cipher(tls);
    auto tlsver = ptls_get_protocol_version(tls);
    return TlsInfo{
      .tlsver = tlsver_(tlsver),
      .protocolVersion = tlsver,
      .cipherID = cipher ? cipher->id : uint16_t{0},
      .alpn = ptls_get_negotiated_protocol(tls),
      .psk = bool(ptls_is_psk_handshake(tls))
    };
  }

protected:
  ptls_t *tls() { return m_tls; }
  void tls(ptls_t *tls_) { m_tls = tls_; }
  ptls_handshake_properties_t *handshake_props() { return &m_props; }
  void reset_handshake_props() { memset(&m_props, 0, sizeof(m_props)); }

private:
  static constexpr unsigned TxRecordCapacity = Ztls_::TxRecordCapacity;
  static constexpr unsigned TxMaxOverhead = 325;
  static constexpr unsigned TxMaxPlaintext = TxRecordCapacity;

  // called from Cxn::connected(ZiRxContext &)
  // this is the internal TCP-level connected; once handshake is completed,
  // the application's connected_() will be called
  void connected_0(Cxn *cxn, ZiRxContext &io) { // runs on I/O Rx thread
    io.complete();
    app()->rxInvoke([impl = ZmRef(this->impl()), cxn = ZmRef(cxn)]() {
      impl->connected_1(ZuMv(cxn));
    });
  }

  void connected_1(ZmRef<Cxn> cxn) {
    // direct call from within rx thread
    ZiAssert(app()->rxInvoked(), "Ztls", (),
      "TLS connected dispatch outside Rx thread", return);
    // idempotent
    if (ZuUnlikely(m_cxn == cxn)) return;
    auto oldState = state_();
    ZmRef<Cxn> old{ZuMvPtr(m_cxn)};
    if (!old) old = m_closing.load_();
    bool replacing = bool(old);
    m_closing = nullptr;
    if (ZuUnlikely(old)) {
      old->close();
      // Retire the old physical output on I/O Tx before publishing B. This
      // neither flushes A nor waits for its disconnect callbacks. Delayed A
      // send setups must reject the closed socket before touching the queue.
      auto old_ = old.ptr();
      old_->send(ZiTxFn{[
	impl = ZmRef(this->impl()), cxn = ZuMv(old)
      ](ZiTxContext &io) {
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
    if (!impl()->connectSocket_(replacing)) { impl()->disconnect_(false); return; }
    impl()->connected_(); // client initiates handshake
    // Install the first admission with the initialized TLS generation.
    rearmInput_();
  }

  // Mux Rx owns contexts. Disarm while the original backing is on TLS Rx;
  // rearming through recv() also notifies packet providers of admission.
  void armInput(ZiRxContext &io, uint64_t gen) {
    if (!io.cxn->up()) { io.complete(); return; }
    auto fn = ZiRxFn{[this, gen](ZiRxContext &io) {
      if (io.buf && io.buf->length) {
	// complete() destroys this callback; snapshot its captures first.
	auto link = this;
	auto generation = gen;
	auto cxn = static_cast<Cxn *>(io.cxn);
	auto buf = ZuMv(io.buf);
	io.complete();
	link->app()->rxRun([link, generation, cxn = ZmRef(cxn), buf = ZuMv(buf)]() mutable {
	  if (link->m_cxn != cxn.ptr() || link->m_disconnecting.load_() ||
	      generation != link->m_tlsGen.load_()) return;
	  link->m_rxPaused = true;
	  link->m_rxInput = ZuMv(buf);
	  link->receiveInput_();
	});
	return true;
      }
      if (!io.buf) io.buf = app()->socketRxAllocator()->allocBuf(impl());
      if (!io.buf) io.disconnect();
      return true;
    }};
    if (!io.buf) io.buf = app()->socketRxAllocator()->allocBuf(impl());
    if (!io.buf) { io.disconnect(); return; }
    io.init(ZuMv(fn), ZuMv(io.buf));
  }

protected:
  bool handshake_(ZmRef<ZiIOBuf> buf) {
    if (buf) m_rxInput = ZuMv(buf);
    return receiveInput_();
  }

private:
  bool receiveInput_() {
    ZiAssert(app()->rxInvoked(), "Ztls", (),
      "TLS input outside Rx thread", return false);
    if (!m_tls || !m_cxn || m_disconnecting.load_()) return false;
    if (asyncPending_()) return true;
    unsigned records = 0;
    do {
      auto gen = m_tlsGen.load_();
      ZmRef<Cxn> target{m_cxn};
      PacketTarget output{this, target.ptr(), gen};
      auto reservation = packetSink(output);
      auto sink = reservation.sink();
      auto plaintext = m_plaintext.sink();
      size_t consumed = m_rxInput ? m_rxInput->length : 0;
      int n = ptls_receive_stream(m_tls, &plaintext, &sink,
	m_rxInput ? m_rxInput->data() : nullptr, &consumed, &m_props);
      if (m_rxInput) {
	m_rxInput->advance(unsigned(consumed));
	if (!m_rxInput->length) m_rxInput = nullptr;
      }
      if (n != 0 && n != PTLS_ERROR_IN_PROGRESS && n != PTLS_ERROR_ASYNC_OPERATION) {
	m_plaintext.clear();
	m_rxInput = nullptr;
	if (PTLS_ERROR_GET_CLASS(n) == PTLS_ERROR_CLASS_PEER_ALERT &&
	    PTLS_ERROR_TO_ALERT(n) == PTLS_ALERT_CLOSE_NOTIFY) {
	  m_peerClosed = true;
	  disconnect_(true);
	} else {
	  app()->error_(ZeEXCEPT(Error, "Ztls", ([n](auto &s) {
	    s << "ptls_receive_stream(): " << strerror_(n);
	  })));
	  disconnect_(false);
	}
	return false;
      }
      if (n == PTLS_ERROR_ASYNC_OPERATION) return asyncHandshake_0();
      if (ptls_handshake_is_complete(m_tls) && !m_handshook && !m_txPending)
	if (!finishHandshake_0()) return false;
      if (auto buf = m_plaintext.take()) {
	if (buf->length) {
	  ++m_rxInCount;
	  m_rxInBytes += buf->length;
	  m_rxStream.push(ZuMv(buf));
	}
      }
      while (ptls_take_key_update_request(m_tls))
	if (!updateKey_0()) return false;
      if (m_handshook) processRx_();
      if (gen != m_tlsGen.load_() || m_cxn != target.ptr() ||
	  m_disconnecting.load_()) return false;
      if (!m_rxInput) break;
      if (++records == 32) {
	app()->rxRun([this, gen, target = ZuMv(target)]() {
	  if (gen == m_tlsGen.load_() && m_cxn == target.ptr()) receiveInput_();
	});
	return true;
      }
    } while (true);
    if (m_rxPaused) {
      m_rxPaused = false;
      rearmInput_();
    }
    return true;
  }

  void rearmInput_() {
    if (!m_cxn || m_disconnecting.load_()) return;
    auto gen = m_tlsGen.load_();
    m_cxn->recv(ZiRxFn{[this, gen](ZiRxContext &io) {
      armInput(io, gen);
      return true;
    }});
  }

private:
  bool finishHandshake_0() {
    if (m_handshook || m_txPending) return true;
    asyncCleanup();
    auto tlsver = ptls_get_protocol_version(m_tls);
    auto cipher = ptls_get_cipher(m_tls);
    if (ZuUnlikely(!cipher)) {
      app()->error_(ZeEXCEPT(Error, "Ztls", "ptls_get_cipher() failed"));
      disconnect_(false);
      return false;
    }
    if (ZuUnlikely(!cipher->aead)) {
      app()->error_(ZeEXCEPT(Error, "Ztls",
	"ptls_get_cipher()->aead is null"));
      disconnect_(false);
      return false;
    }
    unsigned recOverhead = ptls_get_record_overhead(m_tls);
    unsigned recIVLen =
      (tlsver == PTLS_PROTOCOL_VERSION_TLS12) ?
	cipher->aead->tls12.record_iv_size : 0;
    m_headroom = 5 + recIVLen;
    // Tx buffers are pre-sized; negotiated overhead must fit the budget.
    if (ZuUnlikely(
	m_headroom > TxMaxOverhead ||
	recOverhead > TxMaxOverhead)) {
      app()->error_(ZeEXCEPT(Error, "Ztls",
	"TLS record overhead exceeds worst-case limit"));
      disconnect_(false);
      return false;
    }
    ptls_tx_t *txTLS = nullptr;
    int n = ptls_detach_tx(m_tls, &txTLS);
    // A server can finish producing its handshake flight before it has
    // consumed the client's Finished.  Its outbound application keys exist,
    // but detachment becomes valid only after that record is received.
    if (n == PTLS_ERROR_NOT_AVAILABLE && m_isServer && !m_txDeferred) {
      m_txDeferred = true;
      return true;
    }
    if (ZuUnlikely(n)) {
      app()->error_(ZeEXCEPT(Error, "Ztls", ([n](auto &s) {
	s << "ptls_detach_tx(): " << strerror_(n);
      })));
      disconnect_(false);
      return false;
    }
    uint64_t gen = m_tlsGen.load_();
    m_txDeferred = false;
    m_txPending = true;
    app()->txRun([
      impl = ZmRef(impl()), txTLS, gen,
      cxn = ZmRef<Cxn>{m_cxn}
    ]() mutable {
      impl->finishHandshake_1(txTLS, gen, ZuMv(cxn));
    });
    return true;
  }

  void finishHandshake_1(ptls_tx_t *txTLS, uint64_t gen, ZmRef<Cxn> cxn) {
    ZiAssert(app()->txInvoked(), "Ztls", (),
      "TLS Tx install outside Tx thread", ptls_tx_free(txTLS); return);
    bool installed = false;
    if (ZuLikely(gen == m_tlsGen.load_() && !m_disconnecting.load_() &&
	cxn && cxn->up())) {
      if (m_txTLSGen != gen) {
	ptls_tx_free(m_txTLS);
	m_txTLS = nullptr;
	m_txCxn = nullptr;
      }
      if (!m_txTLS) {
	// Providers are active now and the physical payload cap is known. Check
	// the complete supported record budget before exposing application Tx.
	auto mps = cxn->info().mps;
	Backend::RecordSink probe{txPacketAllocator(), impl(), mps, nullptr,
	  [](void *, ZmRef<ZiIOBuf>) -> int { return 0; }};
	auto sink = probe.sink();
	auto overhead = ptls_tx_get_record_overhead(txTLS);
	size_t maxWire = TxRecordCapacity + overhead;
	if (ptls_tx_get_protocol_version(txTLS) == PTLS_PROTOCOL_VERSION_TLS13)
	  maxWire += 5 + overhead; // KeyUpdate handshake header and request byte
	int n = sink.reserve(sink.ctx, maxWire);
	sink.discard(sink.ctx);
	if (n) {
	  cxn->close();
	} else {
	  m_txTLS = txTLS;
	  m_txCxn = cxn;
	  m_txTLSGen = gen;
	  txTLS = nullptr;
	  installed = true;
	}
      }
    }
    ptls_tx_free(txTLS);
    app()->rxRun([impl = ZmRef(impl()), cxn = ZuMv(cxn), gen, installed]() mutable {
      impl->finishHandshake_2(cxn.ptr(), gen, installed);
    });
  }

  void finishHandshake_2(Cxn *cxn, uint64_t gen, bool installed) {
    ZiAssert(app()->rxInvoked(), "Ztls", (),
      "TLS Tx acknowledgement outside Rx thread", return);
    if (ZuUnlikely(gen != m_tlsGen.load_() || m_cxn != cxn)) return;
    m_txPending = false;
    m_txDeferred = false;
    if (ZuUnlikely(!installed)) {
      if (!m_disconnecting.load_()) {
	app()->error_(ZeEXCEPT(Error, "Ztls",
	  "TLS Tx state installation failed"));
	disconnect_(false);
      }
      return;
    }
    if (ZuUnlikely(m_disconnecting.load_())) return;
    auto oldState = state_();
    m_handshook = true;
    stateChanged_(oldState);
    auto tlsver = ptls_get_protocol_version(m_tls);
    const char *alpn = ptls_get_negotiated_protocol(m_tls);
    if (!impl()->connectReady_()) { disconnect_(false); return; }
    impl()->connected(Connected{
      .alpn = alpn ? ZuCSpan{alpn, unsigned(strlen(alpn))} : ZuCSpan{},
      .version = tlsver_(tlsver)
    });
    if (ZuLikely(!m_disconnecting.load_())) processRx_();
  }

  ptls_cipher_suite_t *rxCipher() const {
    return m_tls ? ptls_get_cipher(const_cast<ptls_t *>(m_tls)) : nullptr;
  }

  struct PacketTarget {
    Link *link;
    Cxn *cxn;
    uint64_t gen;
    const ZiIOBuf *plaintext = nullptr;
  };

  Backend::RecordSink packetSink(PacketTarget &target) {
    auto cxn = target.cxn;
    auto mps = cxn ? cxn->info().mps : 0;
    return Backend::RecordSink{
      txPacketAllocator(), impl(), mps, &target,
      [](void *ctx, ZmRef<ZiIOBuf> buf) -> int {
	auto target = static_cast<PacketTarget *>(ctx);
	if (!target->cxn || !target->cxn->up() ||
	    target->gen != target->link->m_tlsGen.load_())
	  return PTLS_ERROR_LIBRARY;
	return target->link->finalizeTxBuf(ZuMv(buf), target->cxn) ?
	  0 : PTLS_ERROR_LIBRARY;
      }, target.plaintext ? +[](void *ctx, ZmRef<ZiIOBuf> last) {
	auto target = static_cast<PacketTarget *>(ctx);
	++target->link->m_txInCount;
	target->link->impl()->txProtected();
	target->link->impl()->txPacketized(target->plaintext, ZuMv(last));
      } : nullptr};
  }

  bool finalizeTxBuf(ZmRef<ZiIOBuf> buf, Cxn *cxn) {
    if (!buf->length) return true;
    m_txInBytes += buf->length;
    buf->owner = impl();
    cxn->send(ZiTxFn::mvFn(ZuMv(buf), [](ZmRef<ZiIOBuf> buf, ZiTxContext &io) {
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

  void processRx_() {
    while (m_rxStream) {
      int n = impl()->process(m_rxStream);
      if (ZuUnlikely(!n)) return;
      if (ZuUnlikely(n < 0)) {
	disconnect_(true);
	return;
      }
    }
  }

  Cxn *closing_() const { return m_closing.load_(); }
  void closing_(Cxn *cxn) { m_closing = cxn; }
  uint64_t tlsGen() const { return m_tlsGen.load_(); }

  template <typename ImplRef_>
  void disconnected_0(Cxn *cxn, ImplRef_ impl_, bool peer) {
    ZmRef<Impl> impl{ZuMvPtr(impl_)};
    app()->rxRun([impl = ZuMv(impl), cxn = ZmRef(cxn), peer]() mutable {
      // Even a stale Cxn retains its link through the local owner barriers:
      // preexisting encrypt/send posts can still refer to that physical Cxn.
      // The close token makes these barriers cleanup-free after replacement.
      impl->disconnected_(cxn);
      auto gen = impl->tlsGen();

      // Retire queued mux Rx admissions before releasing the link. Reception
      // is disarmed, and TLS Rx can no longer post a rearm for this connection.
      cxn->mx()->rxRun([impl = ZuMv(impl), cxn = ZuMv(cxn), gen, peer]() mutable {
	auto app = impl->app();
	// drain Tx while keeping cxn/impl referenced
	app->txRun([impl = ZuMv(impl), cxn = ZuMv(cxn), gen, peer]() mutable {
	  if (impl->closing_() == cxn.ptr()) impl->clearTx(gen);
	  // TLS Tx has stopped producing; drain its queued socket sends next.
	  auto mx = cxn->mx();
	  mx->txRun([
	    impl = ZuMv(impl), cxn = ZuMv(cxn), gen, peer
	  ]() mutable {
	    if (impl->closing_() == cxn.ptr() && gen == impl->tlsGen())
	      impl->Tx::txQueue.clean();
	    auto app = impl->app();
	    app->rxRun([
	      impl = ZuMv(impl), cxn = ZuMv(cxn), gen, peer
	    ]() mutable {
	      if (impl->closing_() == cxn.ptr()) {
		impl->closing_(nullptr);
		if (gen == impl->tlsGen()) impl->disconnected_1(peer);
	      }
	      impl->app()->linkDisconnected_();
	      (void)cxn;
	    });
	  });
	});
      });
    });
  }
  bool disconnected_(Cxn *cxn) {
    // direct call from within rx thread
    ZiAssert(app()->rxInvoked(), "Ztls", (),
      "TLS disconnected dispatch outside Rx thread", return false);
    if (m_cxn == cxn || m_closing.load_() == cxn) {
      auto oldState = state_();
      m_cxn = nullptr;
      m_closing = cxn;
      stateChanged_(oldState);
      return true;
    }
    return false;
  }

  void disconnected_1(bool peer) {
    ZiAssert(app()->rxInvoked(), "Ztls", (),
      "TLS disconnect completion outside Rx thread", return);
    peer |= m_peerClosed;
    reset_tls<false>();
    m_rxStream.clean();
    impl()->connectClosed_();
    impl()->disconnected(peer);
  }

public:
  // Plaintext capacity is independent of packet and TLS framing geometry.
  static constexpr unsigned txMaxSize() { return TxRecordCapacity; }
  template <bool AppThread>
  unsigned txHeadRoom() const {
    return 0;
  }
  template <bool AppThread>
  unsigned txTailRoom() const {
    return 0;
  }
  template <bool AppThread>
  ZmRef<ZiIOBuf> txAllocBuf(unsigned skip) {
    if (ZuUnlikely(skip < txHeadRoom<AppThread>() || skip > txMaxSize()))
      return nullptr;
    auto buf = allocTxBuf(skip);
    if (ZuUnlikely(!buf || skip > buf->size))
      return nullptr;
    return buf;
  }

private:
  ZmRef<ZiIOBuf> allocTxBuf(unsigned headroom) { // App/Tx threads
    auto buf = txAllocator()->allocBuf(impl(), TxRecordCapacity);
    // Preserve only headroom requested by application framing.
    buf->skip = headroom;
    buf->length = 0;
    return buf;
  }

  template <bool AppThread>
  class TxStream_ : public Zi::TxStream<TxStream_<AppThread>> {
    using Base = Zi::TxStream<TxStream_<AppThread>>;

  public:
    TxStream_(Link &link) :
      TxStream_(link, link.template txHeadRoom<AppThread>())
    {
    }

  private:
    TxStream_(Link &link, unsigned headroom) :
      Base(
	link.txMaxSize(),
	headroom,
	0),
      m_link{&link},
      m_gen{link.m_tlsGen.load_()},
      m_headroom{headroom}
    {
    }

  public:
    ~TxStream_() {
      this->flush();
      if (this->failed())
	m_link->txError(m_gen, "TLS plaintext stream failed");
    }
    TxStream_(TxStream_ &&) = default;
    TxStream_ &operator =(TxStream_ &&) = default;

    ZmRef<ZiIOBuf> allocBuf_(unsigned skip) {
      if (ZuUnlikely(skip < m_headroom))
	return nullptr;
      return m_link->template txAllocBuf<AppThread>(skip);
    }

    bool sendBuf_(ZmRef<ZiIOBuf> buf, bool) {
      buf->owner = m_link->impl();
      if constexpr (AppThread)
	return m_link->send(ZuMv(buf), m_gen);
      else
	return m_link->send_(ZuMv(buf), m_gen);
    }

  private:
    // immutable
    Link	*m_link;
    uint64_t	m_gen = 0;
    unsigned	m_headroom = 0;
  };

public:
  void txErrorFn(ZiTxErrorFn fn) { m_txErrorFn = ZuMv(fn); }

  auto txStream() { // App thread(s)
    ZmAssert(!app()->txInvoked());
    return TxStream_<true>{*this};
  }
  TxStream_<false> txStream_() { // direct call from within tx thread
    ZmAssert(app()->txInvoked());
    return TxStream_<false>{*this};
  }

protected:
  bool updateKey_0(bool requestUpdate = false) {
    // direct call from within rx thread
    ZiAssert(app()->rxInvoked(), "Ztls", (),
      "TLS KeyUpdate outside Rx thread", return false);
    if (ZuUnlikely(!m_tls || !rxCipher())) return false;
    ZiAssert(ptls_get_protocol_version(m_tls) == PTLS_PROTOCOL_VERSION_TLS13,
      "Ztls", (),
      "TLS KeyUpdate requires TLS 1.3", return false);
    uint64_t gen = m_tlsGen.load_();
    app()->txRun([impl = ZmRef(impl()), gen, requestUpdate]() mutable {
      impl->updateKey_1(gen, requestUpdate);
    });
    return true;
  }

private:

public:
  bool send(ZmRef<ZiIOBuf> buf) {
    return send(ZuMv(buf), m_tlsGen.load_());
  }
protected:
  bool send(ZmRef<ZiIOBuf> buf, uint64_t gen) {
    if (ZuUnlikely(!buf || buf->failed() || !buf->length))
      return reportTxError(
	"TLS session is closed for transmission");
    if (ZuUnlikely(m_disconnecting.load_() || gen != m_tlsGen.load_())) {
      txError(gen, "TLS session is closed for transmission"); return false;
    }
    buf->owner = impl();
    app()->txInvoke([buf = ZuMv(buf), gen]() mutable {
      auto link = static_cast<Impl *>(buf->owner);
      link->send_(ZuMv(buf), gen);
    });
    return true;
  }

protected:
  // Optional instrumentation at the application record-protection boundary.
  void txProtected() { }

  bool send_(ZmRef<ZiIOBuf> buf) { // direct call from within tx thread
    return send_(ZuMv(buf), m_tlsGen.load_());
  }
private:
  bool send_(ZmRef<ZiIOBuf> buf, uint64_t gen) {
    // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Ztls", (),
      "TLS send_ outside Tx thread", return false);
    if (ZuUnlikely(!buf || buf->failed() || !buf->length || gen != m_tlsGen.load_() ||
	!m_txTLS || m_txTLSGen != gen || m_txClosing ||
	!m_txCxn || !m_txCxn->up())) {
      txError(gen, "TLS session is closed for transmission",
	bool(m_txCxn) && m_txTLSGen == gen);
      return false;
    }

    ZiAssert(buf->length <= TxMaxPlaintext,
      "Ztls", (), "TLS Tx plaintext exceeds record limit", return false);
    ZiAssert(buf->skip <= buf->size && buf->length <= buf->size - buf->skip,
      "Ztls", (), "TLS Tx plaintext bounds exceeded", return false);
    ZmRef<Cxn> target{m_txCxn};
    PacketTarget output{this, target.ptr(), gen, buf.ptr()};
    auto reservation = packetSink(output);
    auto sink = reservation.sink();
    int n = ptls_tx_send_chunks(m_txTLS, &sink, buf->data(), buf->length);
    if (n) {
      txError(gen, "ptls_tx_send_chunks()", n);
      return false;
    }
    return true;
  }

public:
  void txPacketized(const ZiIOBuf *, ZmRef<ZiIOBuf>) { }

  void sent(ZmRef<ZiTxBuf>, bool ok) {
    // The underlying connection closes its own failed socket after this
    // diagnostic. It may already have been displaced by another TLS session.
    if (ZuUnlikely(!ok))
      reportTxError("TLS transmit failed");
  }

private:
  void updateKey_1(uint64_t gen, bool requestUpdate) {
    ZiAssert(app()->txInvoked(), "Ztls", (),
      "TLS KeyUpdate outside Tx thread", return);
    if (ZuUnlikely(gen != m_tlsGen.load_() ||
	!m_txTLS || m_txTLSGen != gen || m_txClosing)) return;
    ZmRef<Cxn> target{m_txCxn};
    PacketTarget output{this, target.ptr(), gen};
    auto reservation = packetSink(output);
    auto sink = reservation.sink();
    int n = ptls_tx_key_chunks(m_txTLS, &sink, requestUpdate ? 1 : 0);
    if (ZuUnlikely(n)) txError(gen, "ptls_tx_key_chunks()", n);
  }

  void closeTx_(ZmRef<Cxn> cxn, uint64_t gen) {
    ZiAssert(app()->txInvoked(), "Ztls", (),
      "TLS close outside Tx thread", return);
    if (gen == m_tlsGen.load_() && m_txTLS &&
	m_txTLSGen == gen && !m_txClosing) {
      PacketTarget output{this, cxn.ptr(), gen};
      auto reservation = packetSink(output);
      auto sink = reservation.sink();
      int n = ptls_tx_alert_chunks(m_txTLS, &sink,
	PTLS_ALERT_LEVEL_WARNING, PTLS_ALERT_CLOSE_NOTIFY);
      if (ZuUnlikely(n)) txError(gen, "ptls_tx_alert_chunks()", n);
      m_txClosing = true;
    }
    auto mx = cxn->mx();
    mx->txRun([cxn = ZuMv(cxn)]() { cxn->disconnect(); });
  }

  void txError(uint64_t gen, const char *message, bool origin = true) {
    ZmRef<Cxn> cxn;
    if (app()->txInvoked() && gen == m_txTLSGen) {
      m_txClosing = true;
      if (origin) cxn = m_txCxn;
    }
    app()->rxRun([impl = ZmRef(impl()), cxn = ZuMv(cxn), gen, message, origin]() {
      if (cxn) cxn->close();
      auto e = ZeEXCEPT(Error, "Ztls", message);
      if (impl->m_txErrorFn) impl->m_txErrorFn(e);
      if (origin && gen == impl->tlsGen() && impl->m_cxn)
	impl->disconnect_(false);
    });
  }

  void txError(uint64_t gen, const char *function, int n) {
    ZmRef<Cxn> cxn;
    if (app()->txInvoked() && gen == m_txTLSGen) {
      m_txClosing = true;
      cxn = m_txCxn;
    }
    app()->rxRun([impl = ZmRef(impl()), cxn = ZuMv(cxn), gen, function, n]() {
      if (cxn) cxn->close();
      auto e = ZeEXCEPT(Error, "Ztls", ([function, n](auto &s) {
	s << function << ": " << strerror_(n);
      }));
      if (impl->m_txErrorFn) impl->m_txErrorFn(e);
      if (gen == impl->tlsGen() && impl->m_cxn)
	impl->disconnect_(false);
    });
  }

  void retireTx(uint64_t gen) {
    ZiAssert(app()->txInvoked(), "Ztls", (),
      "TLS Tx retirement outside Tx thread", return);
    if (m_txTLSGen != gen) {
      ptls_tx_free(m_txTLS);
      m_txTLS = nullptr;
      m_txCxn = nullptr;
      m_txTLSGen = 0;
      m_txClosing = false;
    }
  }

  void clearTx(uint64_t gen) {
    ZiAssert(app()->txInvoked(), "Ztls", (),
      "TLS Tx cleanup outside Tx thread", return);
    if (gen != m_tlsGen.load_() || (m_txTLS && m_txTLSGen != gen)) return;
    ptls_tx_free(m_txTLS);
    m_txTLS = nullptr;
    m_txCxn = nullptr;
    m_txTLSGen = 0;
    m_txClosing = false;
  }

private:
  bool reportTxError(ZuCSpan message) {
    auto e = ZeEXCEPT(Error, "Ztls", message);
    if (m_txErrorFn) m_txErrorFn(e);
    return false;
  }

  bool asyncPending_() const {
    return m_asyncJob && m_asyncJob->tls == m_tls;
  }

  void clearAsync() {
    m_asyncJob = nullptr;
  }

  bool asyncHandshake_0() {
    if (ZuUnlikely(!app()->asyncConfigured())) {
      app()->error_(ZeEXCEPT(Error, "Ztls", ([](auto &s) {
	s << "ptls_handshake() returned PTLS_ERROR_ASYNC_OPERATION "
	  << "but asyncThread is not configured";
      })));
      disconnect_(false);
      return false;
    }

    auto job = ptls_get_async_job(tls());
    if (ZuUnlikely(!job)) {
      app()->error_(ZeEXCEPT(Error, "Ztls",
	"ptls_get_async_job() returned null"));
      disconnect_(false);
      return false;
    }
    if (ZuUnlikely(!job->get_fd)) {
      app()->error_(ZeEXCEPT(Error, "Ztls",
	"callback-only zpicotls async jobs are unsupported"));
      disconnect_(false);
      return false;
    }
    auto fd = job->get_fd(job);
    Zi::Handle handle = static_cast<Zi::Handle>(fd);
    if (ZuUnlikely(Zi::nullHandle(handle))) {
      app()->error_(ZeEXCEPT(Error, "Ztls",
	"zpicotls async job returned an invalid fd"));
      disconnect_(false);
      return false;
    }
#ifndef _WIN32
    if (ZuUnlikely(fcntl(fd, F_GETFD) < 0)) {
      app()->error_(ZeEXCEPT(Error, "Ztls", ([e = ZeError{errno}](auto &s) {
	s << "zpicotls async job fd is invalid: " << e;
      })));
      disconnect_(false);
      return false;
    }
#endif

    asyncCleanup();

    AsyncJobRef async = new AsyncJob{
      ZmRef(impl()), tls(), job, handle, m_tlsGen.load_()};
    m_asyncJob = async.ptr();

    if (ZuUnlikely(!app()->asyncAddHandle(
	  handle,
	  ZiEvent::HandleWriteFn{[](Zi::Handle) { }},
	  ZiEvent::HandleReadFn{
	    async, ZmFnPtr<&AsyncJob::ready>{}}))) {
      clearAsync();
      app()->error_(ZeEXCEPT(Error, "Ztls",
	"ZiEventLoop::addHandle() failed for zpicotls async job"));
      disconnect_(false);
      return false;
    }

    return true;
  }

  void asyncHandshake_1(AsyncJobRef async, Zi::Handle handle_) {
    if (ZuUnlikely(handle_ != async->handle)) return;
    if (++async->readCount != 2) return;
    async->handle = Zi::nullHandle();
    app()->asyncDelHandle_(handle_);
    app()->rxRun([async = ZuMv(async)]() mutable {
	async->link->asyncHandshake_2(async);
      });
  }

  void asyncHandshake_2(AsyncJob *async) {
    if (async->tls == m_tls &&
	async->gen == m_tlsGen.load_() &&
	m_asyncJob == async) {
      clearAsync();
      handshake_(nullptr);
      return;
    }
    if (async->retired && async->tls) {
      ptls_free(async->tls);
      async->tls = nullptr;
    }
  }

  void asyncCleanup() {
    AsyncJobRef async;
    if (m_asyncJob) async = ZmRef(m_asyncJob);
    clearAsync();
    if (!async) return;
    Zi::Handle handle = async->handle;
    async->handle = Zi::nullHandle();
    if (!Zi::nullHandle(handle)) app()->asyncDelHandle(handle);
  }

  bool asyncRetireTLS() {
    if (!asyncPending_()) return false;
    m_asyncJob->retired = true;
    clearAsync();
    return true;
  }

  void asyncDestroyed(AsyncJob *async) {
    if (m_asyncJob == async) m_asyncJob = nullptr;
  }

public:
  void disconnect() { // App thread(s)
    auto oldState = state_();
    m_disconnecting = 1;
    stateChanged_(oldState);
    app()->rxInvoke([impl = ZmRef(this->impl())]() {
	impl->disconnect_();
    });
  }
  void disconnect_(bool notify = true) { // direct call from within rx thread
    ZiAssert(app()->rxInvoked(), "Ztls", (),
      "TLS disconnect outside Rx thread", return);
    impl()->cancelReconn_();
    impl()->stopConnect_();
    if (m_closePending) return;
    m_closePending = true;
    auto oldState = state_();
    m_disconnecting = 1; // disconnect() might be bypassed
    stateChanged_(oldState);
    bool asyncPending = asyncPending_();
    if (asyncPending) {
      asyncRetireTLS();
      m_tls = nullptr;
    }
    ZmRef<Cxn> cxn;
    if (m_cxn) cxn = ZmRef<Cxn>{m_cxn};
    if (cxn) {
      if (notify) {
	uint64_t gen = m_tlsGen.load_();
	app()->txRun([
	  impl = ZmRef(impl()), cxn = ZuMv(cxn), gen
	]() mutable {
	  impl->closeTx_(ZuMv(cxn), gen);
	});
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


  static int tlsver_(uint16_t v) {
    switch (v) {
      case 0x0303: return 12;
      case 0x0304: return 13;
      default: return 0;
    }
  }

  template <bool CleanRx = true>
  void reset_tls() {
    uint64_t gen = m_tlsGen.load_() + 1;
    m_tlsGen.store_(gen);
    app()->txRun([impl = ZmRef(impl()), gen]() mutable {
      impl->retireTx(gen);
    });
    if (m_tls) {
      if (!asyncRetireTLS())
	ptls_free(m_tls);
    }
    m_plaintext.reset(app()->rxAllocator(), impl());
    m_rxInput = nullptr;
    m_rxPaused = false;
    m_tls = ptls_new(app()->ctx(), m_isServer);
    if (!m_tls) {
      app()->error_(ZeEXCEPT(Error, "Ztls", "ptls_new() failed"));
      return;
    }
    *ptls_get_data_ptr(m_tls) = impl();
    if (ptls_receive_stream_check(m_tls)) {
      app()->error_(ZeEXCEPT(Error, "Ztls", "selected TLS backend lacks serial decryption"));
      return;
    }
    auto oldState = state_();
    m_headroom = 0;
    m_txPending = false;
    m_txDeferred = false;
    m_closePending = false;
    m_handshook = false;
    m_peerClosed = false;
    m_disconnecting = 0;
    stateChanged_(oldState);
    reset_handshake_props();

    if constexpr (CleanRx) m_rxStream.clean();
  }

private:
  LinkState::T state_() const {
    if (m_disconnecting.load_()) return LinkState::Disconnecting;
    if (!m_cxn) return LinkState::Down;
    return m_handshook ? LinkState::Up : LinkState::Connecting;
  }

  void stateChanged_(LinkState::T oldState) {
    auto newState = state_();
    if (oldState != newState) app()->linkState_(oldState, newState);
  }

  void rxQueueTelemetry(Ztc::QueueTelemetry &data) const {
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

  void txQueueTelemetry(Ztc::QueueTelemetry &data) const {
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
  // Telemetry counters may be updated/read uncleanly across shards.
  uint64_t		m_rxInCount = 0;
  uint64_t		m_rxInBytes = 0;
  uint64_t		m_txInCount = 0;
  uint64_t		m_txInBytes = 0;
  // Configured before the link is used, then stable.
  ZiTxErrorFn		m_txErrorFn;
  // Read-mostly connection reference; mutation remains Rx-owned.
  CxnRef		m_cxn = nullptr;

  struct AsyncJob : public ZmPolymorph {
    AsyncJob(
	ZmRef<Impl> link_, ptls_t *tls_, ptls_async_job_t *job_,
	Zi::Handle handle_, uint64_t gen_) :
	link{ZuMv(link_)}, tls{tls_}, job{job_}, handle{handle_}, gen{gen_} {
    }
    ~AsyncJob() {
      link->asyncDestroyed(this);
      if (retired && tls)
	link->app()->error_(ZeEXCEPT(Error, "Ztls",
	  "TLS async job destroyed while still pending"));
    }
    void ready(Zi::Handle handle_) {
      link->asyncHandshake_1(ZmRef(this), handle_);
    }

    ZmRef<Impl>		link;
    ptls_t		*tls = nullptr;
    ptls_async_job_t	*job = nullptr;
    Zi::Handle		handle = Zi::nullHandle();
    uint64_t		gen = 0;
    ZmAtomic<unsigned>	readCount = 0;
    bool		retired = false;
  };

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  ptls_t		*m_tls = nullptr;
  unsigned		m_headroom = 0;
  bool			m_txPending = false;
  bool			m_txDeferred = false;
  bool			m_closePending = false;
  bool			m_handshook = false;
  bool			m_peerClosed = false;
  ptls_handshake_properties_t m_props{};
  AsyncJob		*m_asyncJob = nullptr;
  RxStream		m_rxStream;
  ZmRef<ZiIOBuf>	m_rxInput;
  Backend::PlaintextSink m_plaintext;
  bool			m_rxPaused = false;

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
  ptls_tx_t		*m_txTLS = nullptr;
  // Detached keys and their physical destination have the same Tx lifetime.
  ZmRef<Cxn>		m_txCxn;
  uint64_t		m_txTLSGen = 0;
  bool			m_txClosing = false;
};

// client links are persistent, own the (transient) connection
template <
  typename App, typename Impl,
  typename RxBufAlloc, typename TxBufAlloc,
  typename Cxn>
using CliLink_ = Link<App, Impl, RxBufAlloc, TxBufAlloc, Cxn, ZmRef<Cxn>>;
// server links are transient, are owned by the connection
template <
  typename App, typename Impl,
  typename RxBufAlloc, typename TxBufAlloc,
  typename Cxn>
using SrvLink_ = Link<App, Impl, RxBufAlloc, TxBufAlloc, Cxn, Cxn *>;

template <
  typename App, typename Impl,
  typename RxBufAlloc_ = Ztls::RxBufAlloc<>,
  typename TxBufAlloc_ = Ztls::TxBufAlloc<>>
class CliLink :
  public CliLink_<App, Impl, RxBufAlloc_, TxBufAlloc_, CliCxn<Impl>> {
public:
  using Cxn = CliCxn<Impl>;
  using RxBufAlloc = RxBufAlloc_;
  using TxBufAlloc = TxBufAlloc_;
  using Base = CliLink_<App, Impl, RxBufAlloc, TxBufAlloc, Cxn>;

  using Base::impl;
  using Base::app;
  using Base::reset_tls;
  using Base::handshake_props;
  using Base::reset_handshake_props;
  using Base::handshake_;
  using Base::tls;

friend Base;
template <typename> friend class Client;

  CliLink(App *app) : Base{app, false} { }
  CliLink(App *app, ZuID id) : Base{app, false, ZuMv(id)} { }
  CliLink(App *app, Host server, uint16_t port) :
      Base{app, false}, m_server{ZuMv(server)}, m_port{port} { }
  CliLink(App *app, ZuID id, Host server, uint16_t port) :
      Base{app, false, ZuMv(id)},
      m_server{ZuMv(server)}, m_port{port} { }
  ~CliLink() { }

  void connect() { // App thread(s)
    app()->rxInvoke([this]() mutable { connect_(); });
  }
  void connect(Host server, uint16_t port) { // App thread(s)
    app()->rxInvoke(
      [this, server = ZuMv(server), port]() mutable {
	if (m_connectBusy || !impl()->connectAllowed_()) return;
	m_server = ZuMv(server);
	m_port = port;
	m_remote = ZiIP{};
	connect_();
      });
  }
  void connect(Host server, uint16_t port, ZiIP remote) { // App thread(s)
    app()->rxInvoke(
      [this, server = ZuMv(server), port, remote = ZuMv(remote)]() mutable {
	if (m_connectBusy || !impl()->connectAllowed_()) return;
	m_server = ZuMv(server);
	m_port = port;
	m_remote = ZuMv(remote);
	connect_();
      });
  }

  const Host &server() const { return m_server; }
  uint16_t port() const { return m_port; }

  void connect_() { // direct call from within rx thread
    ZiAssert(app()->rxInvoked(), "Ztls", (),
      "TLS connect outside Rx thread", return);
    if (m_connectBusy) return;
    if (!impl()->connectAllowed_()) { retryConnect_0(); return; }
    cancelReconn_();
    m_connectBusy = true;
    m_connectStopping = false;
    if (auto ip = m_remote) { resolved_(ip); return; }
    // Consume the resolver through its terminal continuation, including both
    // address families. Move the retained owner into the application post.
    ZiResolver::resolve(m_server, ZiResolver_::ResolveFn{
      [owner = ZmRef(impl()), ip = ZiIP{}](auto result) mutable {
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
  void resolved_(ZiIP ip) {
    if (!ip) { setupFailed_(true); return; }
    if (m_connectStopping) {
      m_connectBusy = false; impl()->connectRetired_(); return;
    }
    impl()->connectIP_(ip);
  }
  void setupFailed_(bool transient) {
    m_connectBusy = false;
    impl()->connectRetired_();
    impl()->connectFailed(transient);
  }
  void connectIP_(ZiIP ip) {
    reset_tls();
    if (!tls()) { setupFailed_(true); return; }
    int n = ptls_set_server_name(tls(), m_server.data(), m_server.length());
    if (n) {
      app()->error_(ZeEXCEPT(Error, "Ztls", ([server = LogMsg{m_server}, n](auto &s) {
	s << "ptls_set_server_name(\"" << server << "\"): " << strerror_(n);
      })));
      setupFailed_(true); return;
    }
    app()->mx()->connect(
      ZiConnectFn{ZmRef(impl()),
	[](Impl *impl, const ZiCxnInfo &ci) -> ZiConnection * {
	  return new Cxn(impl, ci);
	}},
      ZiFailFn{ZmRef(impl()), [](Impl *impl, bool transient) {
	auto app = impl->app();
	app->rxRun([owner = ZmRef(impl), transient] { owner->setupFailed_(transient); });
      }}, ZiIP{}, 0, ip, m_port, app()->socketRxAllocator());
  }

private:
  void save_ticket(ptls_iovec_t input) {
    if (!input.len || !input.base) {
      m_ticket = {};
      return;
    }
    m_ticket = input;
  }

  // client connected variant - initiate new handshake
  void connected_() {
    reset_handshake_props();

    // set up HELO
    auto props = handshake_props();
    auto list = app()->alpn_list();
    unsigned count = app()->alpn_count();
    if (list && count) {
      props->client.negotiated_protocols.list = list;
      props->client.negotiated_protocols.count = count;
    }
    if (m_ticket.length())
      props->client.session_ticket =
	ptls_iovec_init(m_ticket.data(), m_ticket.length());
    m_maxEarlyData = 0;
    props->client.max_early_data_size = &m_maxEarlyData;
    props->client.early_data_acceptance = PTLS_EARLY_DATA_ACCEPTANCE_UNKNOWN;

    // send HELO
    handshake_(nullptr);
  }

protected:
  size_t maxEarlyData() const { return m_maxEarlyData; }

public:
  void connectFailed(bool transient) {
    ZiAssert(app()->rxInvoked(), "Ztls", (),
      "TLS connect failure outside Rx thread", return);
    if (!m_connectStopping && (!transient || !retryConnect_0()))
      app()->error_(ZeEXCEPT(Error, "Ztls", "connect failed"));
  }

protected:
  void up_() { connect(); }

private:
  void retryConnect_1(uint64_t serial) {
    if (m_reconnSerial == serial) connect_();
  }
  bool retryConnect_0() {
    unsigned freq = app()->reconnFreq();
    if (!freq) return false;
    auto serial = ++m_reconnSerial;
    app()->mx()->del(&m_reconnTimer);
    app()->mx()->add(&m_reconnTimer, Zm::now(freq), ZmScheduler::Update,
      [this, serial](auto &&arm) {
	return arm([this, serial]() { retryConnect_1(serial); });
      }, app()->rxThread());
    return true;
  }
  void cancelReconn_() {
    ZiAssert(app()->rxInvoked(), "Ztls", (),
      "TLS reconnect cancellation outside Rx thread", return);
    ++m_reconnSerial;
    app()->mx()->del(&m_reconnTimer);
  }

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  ZmScheduler::Timer	m_reconnTimer;
  Ticket		m_ticket;
  size_t		m_maxEarlyData = 0;
  uint64_t m_reconnSerial = 0;
  bool m_connectBusy = false, m_connectStopping = false;
  Host			m_server;
  ZiIP			m_remote;
  uint16_t		m_port;
};

template <
  typename App, typename Impl,
  typename RxBufAlloc_ = Ztls::RxBufAlloc<>,
  typename TxBufAlloc_ = Ztls::TxBufAlloc<>>
class SrvLink :
  public SrvLink_<App, Impl, RxBufAlloc_, TxBufAlloc_, SrvCxn<Impl>> {
public:
  using Cxn = SrvCxn<Impl>;
  using RxBufAlloc = RxBufAlloc_;
  using TxBufAlloc = TxBufAlloc_;
  using Base = SrvLink_<App, Impl, RxBufAlloc, TxBufAlloc, Cxn>;

  using Base::impl;
  using Base::app;
  using Base::reset_tls;

friend Base;
template <typename> friend class Server;

  SrvLink(App *app) : Base(app, true) { }

private:
  // client connected variant - reset TLS
  void connected_() {
    reset_tls();
  }

  int on_client_hello(
      ptls_t *tls, ptls_on_client_hello_parameters_t *params) {
    if (params->server_name.base && params->server_name.len)
      ptls_set_server_name(
	tls,
	reinterpret_cast<const char *>(params->server_name.base),
	params->server_name.len);
    auto list = app()->alpn_list();
    auto count = app()->alpn_count();
    if (!list || !count) return 0;
    if (!params->negotiated_protocols.list ||
	!params->negotiated_protocols.count)
      return PTLS_ALERT_NO_APPLICATION_PROTOCOL;
    for (unsigned i = 0; i < count; ++i) {
      for (unsigned j = 0; j < params->negotiated_protocols.count; ++j) {
	auto &proto = params->negotiated_protocols.list[j];
	if (proto.len != list[i].len) continue;
	if (!memcmp(proto.base, list[i].base, proto.len)) {
	  ptls_set_negotiated_protocol(
	    tls,
	    reinterpret_cast<const char *>(list[i].base),
	    list[i].len);
	  return 0;
	}
      }
    }
    return PTLS_ALERT_NO_APPLICATION_PROTOCOL;
  }
};

template <typename App_> class Hub :
  public Random,
  public ZmEngine<App_>,
  public Ztc::Hub {
friend ZmEngine<App_>;
template <typename, typename, typename, typename, typename, typename>
friend class Link;
template <typename, typename, typename, typename> friend class CliLink;
template <typename, typename, typename, typename> friend class SrvLink;

public:
  using App = App_;
  using HubCtl = ZmEngine<App>;
  using Links = ZmHash<Ztc::Link *,
    ZmHashLock<ZmPLock,
      ZmHashHeapID<"Ztls.Hub.Links">>>;

  using HubCtl::start;
  using HubCtl::stop;
  using HubCtl::state;

  const App *app() const { return static_cast<const App *>(this); }
  App *app() { return static_cast<App *>(this); }

  Hub() : m_id{ZuID{} << "tls:" <<
      ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>()} {
    memset(&m_ctx, 0, sizeof(m_ctx));
  }
  ~Hub() { release_(); }

private:
  void release_() {
    if (m_ctx.certificates.list) {
      for (unsigned i = 0; i < m_ctx.certificates.count; ++i)
	::free(m_ctx.certificates.list[i].base);
      ::free(m_ctx.certificates.list);
    }
    Backend::verify_cert_free(m_verify);
    m_verify = nullptr;
    Backend::cert_store_free(m_cacert);
    m_cacert = nullptr;
    memset(&m_ctx, 0, sizeof(m_ctx));
    m_alpnData.init();
    m_alpn.init();
  }

public:

  bool init(HubParams params) {
    return init_(ZuMv(params), [](const HubParams &) { return true; });
  }
  bool start() override { return HubCtl::start(); }
  bool stop() override { return HubCtl::stop(); }

  TelKey telKey() const override {
    return {Ztc::LinkType::TLS, m_id};
  }
  void telemetry(Ztc::HubTelemetry &data) const override {
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
  template <typename Params, typename L>
  bool init_(Params params, L &&l) {
    m_errorFn = ZuMv(params.errorFn());
    if (!m_errorFn) m_errorFn = defaultErrorFn();
    if (!validate_(params)) return false;
    m_mx = params.mx();
    if (params.rxAllocator()->mx != m_mx ||
	!allocator(params.txAllocator()) ||
	params.txPacketAllocator()->mx != m_mx) return false;
    m_rxAllocator = params.rxAllocator();
    m_socketRxAllocator = params.socketRxAllocator();
    if (!m_socketRxAllocator)
      m_socketRxAllocator = m_mx->allocator<Zi::IOBufType::RxTCP, ZiRxBuf, Ztls_::RxPacketCapacity>();
    m_txAllocator = params.txAllocator();
    m_txPacketAllocator = params.txPacketAllocator();
    m_rxThread = thread_(params.rxThread(), m_mx->rxThread());
    m_txThread = thread_(params.txThread(), m_mx->txThread());
    m_asyncThread = params.asyncThread() ?
      m_mx->sid(params.asyncThread()) : 0;

    if (!init_context(params, l)) return false;
    Ztc::HubMgr::add(this);
    m_registered = true;
    return true;
  }

private:
  unsigned thread_(const ParamString &id, unsigned deflt) const {
    return id ? m_mx->sid(id) : deflt;
  }

  template <typename Params>
  bool validate_(const Params &params) {
    if (ZuUnlikely(!params.mx())) {
      error_(ZeEXCEPT(Error, "Ztls", "multiplexer is null"));
      return false;
    }
    if (!params.rxAllocator() ||
	params.rxAllocator()->type != Zi::IOBufType::RxTLS ||
	!params.txAllocator() ||
	params.txAllocator()->type != Zi::IOBufType::TxTLS ||
	!params.txPacketAllocator() ||
	params.txPacketAllocator()->type != Zi::IOBufType::TxTCP) {
      error_(ZeEXCEPT(Error, "Ztls", "TLS Rx, plaintext Tx and packet Tx allocators are required"));
      return false;
    }
    if (auto a = params.socketRxAllocator(); a &&
	(a->mx != params.mx() || a->type != Zi::IOBufType::RxTCP)) {
      error_(ZeEXCEPT(Error, "Ztls", "invalid TLS socket Rx allocator"));
      return false;
    }
    if (!params.socketRxAllocator() &&
	params.mx()->state() != ZmEngineState::Stopped) {
      error_(ZeEXCEPT(Error, "Ztls",
	"running multiplexer requires a prepared TLS socket Rx allocator"));
      return false;
    }
    if (params.rxAllocator()->capacity() < Ztls_::RxRecordCapacity) {
      error_(ZeEXCEPT(Error, "Ztls",
	"RxTLS builtin capacity must be at least PTLS_RECEIVE_RECORD_CAPACITY"));
      return false;
    }
    unsigned rxThread = params.rxThread() ?
      params.mx()->sid(params.rxThread()) : params.mx()->rxThread();
    unsigned txThread = params.txThread() ?
      params.mx()->sid(params.txThread()) : params.mx()->txThread();
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
      error_(ZeEXCEPT(Error, "Ztls",
	"TLS Rx and Tx threads must differ"));
      return false;
    }
    if (params.asyncThread()) {
#ifdef _WIN32
      error_(ZeEXCEPT(Error, "Ztls", ([](auto &s) {
	s << "asyncThread is unsupported on Windows because zpicotls exposes "
	  << "an int fd while HANDLE is pointer-sized";
      })));
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
	error_(ZeEXCEPT(Error, "Ztls",
	  "async thread must differ from TLS Rx and Tx threads"));
	return false;
      }
      if (asyncThread == params.mx()->rxThread() ||
	  asyncThread == params.mx()->txThread()) {
	error_(ZeEXCEPT(Error, "Ztls",
	  "async thread must differ from I/O threads"));
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

  template <typename Params, typename L>
  bool init_context(Params &params, L &l) {
    if (!Random::init()) {
      error_(ZeEXCEPT(Error, "Ztls", "backend init failed"));
      return false;
    }
    memset(&m_ctx, 0, sizeof(m_ctx));
    m_ctx.random_bytes = Backend::random_bytes_cb();
    m_ctx.get_time = &ptls_get_time;
    m_ctx.key_exchanges = Backend::key_exchanges();
    init_cipher_suites();
    m_ctx.cipher_suites = m_cipherSuites;
    m_ctx.server_cipher_preference = 1;
    init_alpn(params.alpn());
    if (!l(params)) return false;
    auto available = [this](ptls_cipher_suite_t **suites) {
      if (!suites) return true;
      for (auto p = suites; *p; ++p) {
	uint8_t key[PTLS_MAX_SECRET_SIZE]{}, iv[PTLS_MAX_IV_SIZE]{};
	auto aead = ptls_aead_new_direct((*p)->aead, 1, key, iv);
	bool ok = aead && aead->do_enc_begin &&
	  aead->do_enc_chunk && aead->do_enc_end;
	if (aead) ptls_aead_free(aead);
	if (!ok) {
	  error_(ZeEXCEPT(Error, "Ztls", "selected AEAD lacks serial record output"));
	  return false;
	}
      }
      return true;
    };
    if (!available(m_ctx.cipher_suites) || !available(m_ctx.tls12_cipher_suites))
      return false;
    auto probe = ptls_new(&m_ctx, 0);
    if (!probe) return false;
    int status = ptls_receive_stream_check(probe);
    ptls_free(probe);
    if (status) {
      error_(ZeEXCEPT(Error, "Ztls", "selected AEAD lacks serial record decryption"));
      return false;
    }
    return true;
  }

public:
  void final() {
    bool ok = HubCtl::lock(ZmEngineState::Stopped, [this]() {
      if (m_registered) {
	Ztc::HubMgr::del(this);
	m_registered = false;
      }
      final_();
      release_();
      return true;
    });
    ZiAssert(ok, "Ztls", (),
      "TLS hub finalization while not stopped", return);
  }

  ZiMultiplex *mx() const { return m_mx; }
  unsigned rxThread() const { return m_rxThread; }
  unsigned txThread() const { return m_txThread; }
  unsigned asyncThread() const { return m_asyncThread; }

  template <typename ...Args>
  void rxRun(Args &&...args) {
    m_mx->run(ZuFwd<Args>(args)..., m_rxThread);
  }
  template <typename ...Args>
  void rxInvoke(Args &&...args) {
    m_mx->invoke(ZuFwd<Args>(args)..., m_rxThread);
  }
  bool rxInvoked() { return m_mx->invoked(m_rxThread); }
  template <typename ...Args>
  void txRun(Args &&...args) {
    m_mx->run(ZuFwd<Args>(args)..., m_txThread);
  }
  template <typename ...Args>
  void txInvoke(Args &&...args) {
    m_mx->invoke(ZuFwd<Args>(args)..., m_txThread);
  }
  bool txInvoked() { return m_mx->invoked(m_txThread); }

  // Validate Tx TLS backing created by the underlying Mx.
  bool allocator(Zi::IOBufAllocator *a) {
    return a && a->type == Zi::IOBufType::TxTLS && m_mx &&
      a->mx == m_mx;
  }

  Zi::IOBufAllocator *rxAllocator() const { return m_rxAllocator; }
  Zi::IOBufAllocator *socketRxAllocator() const { return m_socketRxAllocator; }
  Zi::IOBufAllocator *txAllocator() const { return m_txAllocator; }
  Zi::IOBufAllocator *txPacketAllocator() const { return m_txPacketAllocator; }

  void linkConnected_() { }
  void linkDisconnected_() { }

private:
  void linkAdded_(Ztc::Link *link) {
    m_links.add(link);
    Ztc::Hub::linkAdded_(link);
  }
  void linkDeleted_(Ztc::Link *link, LinkState::T state) {
    if (m_links.del(link)) {
      switch (state) {
	case LinkState::Down: Ztc::Hub::linkDownDec_(); break;
	case LinkState::Up: Ztc::Hub::linkUpDec_(); break;
	default: Ztc::Hub::linkTransientDec_(); break;
      }
      Ztc::Hub::linkDeleted_(link);
    }
  }
public:
  void linkState_(
      LinkState::T oldState, LinkState::T newState) {
    switch (oldState) {
      case LinkState::Down: Ztc::Hub::linkDownDec_(); break;
      case LinkState::Up: Ztc::Hub::linkUpDec_(); break;
      default: Ztc::Hub::linkTransientDec_(); break;
    }
    switch (newState) {
      case LinkState::Down: Ztc::Hub::linkDownInc_(); break;
      case LinkState::Up: Ztc::Hub::linkUpInc_(); break;
      default: Ztc::Hub::linkTransientInc_(); break;
    }
  }

private:
  bool asyncConfigured() const { return m_asyncThread != 0; }

protected:
  unsigned allLinks_(Ztc::Hub::AllLinksFn fn) const {
    unsigned n = 0;
    auto i = m_links.citer();
    while (Ztc::Link *link = i.key()) {
      ++n;
      fn(link);
    }
    return n;
  }
  template <typename Link>
  unsigned downLinks() {
    using DrainLinks = ZtArray<ZmRef<Link>,
      ZtArrayHeapID<"Ztls.Hub.Drain">>;
    unsigned capacity = m_links.count_();
    auto links = ZtScratch(DrainLinks, capacity);
    {
      auto i = m_links.citer();
      while (Ztc::Link *base = i.key()) {
	auto link = static_cast<Link *>(base);
	// Final deref may have started a destructor waiting on this iterator.
	link->template withRef<Link>([&links](ZmRef<Link> ref) {
	  links.push(ZuMv(ref));
	});
      }
    }
    for (auto &link: links) link->down();
    return links.length();
  }
  unsigned allPools_(Ztc::Hub::AllPoolsFn) const { return 0; }

  void error_(ZeException e) {
    if (m_errorFn)
      m_errorFn(ZuMv(e));
    else
      ZiLogEvent(ZuMv(e));
  }

private:
  void startAsyncLoop() {
    if (!asyncConfigured() || m_eventLoopStarted) {
      this->started(true);
      return;
    }
    m_eventLoop.init(m_mx, m_asyncThread,
      ZiEvent::FailFn{[this](ZeException e) { error_(ZuMv(e)); }});
    m_eventLoopInit = true;
    m_eventLoop.start(ZiEvent::StartFn{
      [this](ZiEvent::StartResult result) mutable {
	bool ok = !result.is<ZiEvent::Exception>();
	if (!ok) error_(ZuMv(result).p<ZiEvent::Exception>());
	rxRun([this, ok]() {
	  m_eventLoopStarted = ok;
	  if (!ok) {
	    m_eventLoop.final();
	    m_eventLoopInit = false;
	  }
	  this->started(ok);
	});
      }});
  }

  void stopAsyncLoop() {
    if (m_eventLoopStarted) {
      ZmBlock<>{}([this](auto wake) mutable {
	m_eventLoop.stop(ZiEvent::StopFn{
	  [this, wake = ZuMv(wake)](ZiEvent::StopResult result) mutable {
	    if (result.is<ZiEvent::Exception>())
	      error_(ZuMv(result).p<ZiEvent::Exception>());
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

  void final_() {
    stopAsyncLoop();
    m_errorFn = ErrorFn{};
  }

  template <typename WriteFn, typename ReadFn>
  bool asyncAddHandle(
      Zi::Handle handle, WriteFn write, ReadFn read) {
    if (!m_eventLoopStarted) return false;
    return ZmBlock<bool>{}([
      this, handle, write = ZuMv(write), read = ZuMv(read)
    ](auto wake) mutable {
      m_eventLoop.run([
	this, handle, write = ZuMv(write), read = ZuMv(read),
	wake = ZuMv(wake)
      ]() mutable {
	bool ok = m_eventLoop.addHandle(handle, ZuMv(write), ZuMv(read));
	wake(ok);
      });
    });
  }

  template <typename ...Args> void asyncRun(Args &&...args) {
    if (!m_eventLoopStarted) return;
    m_eventLoop.run(ZuFwd<Args>(args)...);
  }

  void asyncDelHandle_(Zi::Handle handle) {
    m_eventLoop.delHandle(handle);
  }

  void asyncDelHandle(Zi::Handle handle) {
    if (Zi::nullHandle(handle) || !m_eventLoopStarted) return;
    if (m_mx->invoked(m_asyncThread)) {
      m_eventLoop.delHandle(handle);
      return;
    }
    ZmBlock<>{}([this, handle](auto wake) mutable {
      m_eventLoop.run([this, handle, wake = ZuMv(wake)]() mutable {
	m_eventLoop.delHandle(handle);
	wake();
      });
    });
  }

protected:
  void start_() {
    startAsyncLoop();
  }

  // ZmEngine hook; public stop(done) retains done until stop_1() calls
  // stopped(true).  Returning from this function does not complete stop.
  void stop_() {		// enter Rx thread
    rxRun([this]() { stop_0(); });
  }

  void stop_0() {		// Rx thread - enter Tx thread
    txRun([this]() { stop_1(); });
  }

  void stop_1() {		// Tx thread - complete stop
    this->stopped(true);
  }

  template <typename L>
  bool spawn(L &&l) {
    if (!m_mx || !m_mx->running()) return false;
    rxRun(ZuFwd<L>(l));
    return true;
  }

  void wake() {
    if (!m_mx || !m_mx->running()) return;
    rxRun([this]() { this->stopped(); });
  }

  // Ztls Rx thread
  ptls_context_t *ctx() { return &m_ctx; }
  const ptls_context_t *ctx() const { return &m_ctx; }

  const ptls_iovec_t *alpn_list() const {
    return m_alpn.length() ? m_alpn.data() : nullptr;
  }
  unsigned alpn_count() const { return m_alpn.length(); }

  // Arch/Ubuntu/Debian/SLES - /etc/ssl/certs
  // Fedora/CentOS/RHEL - /etc/pki/tls/certs
  // Android - /system/etc/security/cacerts
  // FreeBSD - /usr/local/share/certs
  // NetBSD - /etc/openssl/certs
  // AIX - /var/ssl/certs
  // Windows - ROOT certificate store (using Cert* API)

  bool loadCA(ZuCSpan path) {
    return loadCA_(path ? path.data() : nullptr, nullptr);
  }
  bool loadCA(ZuCSpan path, const Certs &certs) {
    return loadCA_(path ? path.data() : nullptr, &certs);
  }
  bool loadCA_(const char *path, const Certs *certs) {
    if (!m_cacert) m_cacert = Backend::cert_store_new();
    if (!m_cacert) {
      error_(ZeEXCEPT(Error, "Ztls", "cert_store_new() failed"));
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
	if (!Backend::cert_store_add_der(m_cacert,
	      {context->pbCertEncoded, context->cbCertEncoded})) {
	  error_(ZeEXCEPT(Error, "Ztls", "cert_store_add_der() failed"));
	  CertFreeCertificateContext(context);
	  CertCloseStore(store, 0);
	  return false;
	}
      }

      CertCloseStore(store, 0);
#endif
    }
    if (path) {
      bool ok;
      const char *function;
      if (ZiStat{path}.isdir()) {
	function = "cert_store_load_path";
	ok = Backend::cert_store_load_path(m_cacert, path);
      } else {
	function = "cert_store_load_file";
	ok = Backend::cert_store_load_file(m_cacert, path);
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
	if (!Backend::cert_store_add_ca(m_cacert, cert)) {
	  error_(ZeEXCEPT(Error, "Ztls", "cert_store_add_ca() failed"));
	  return false;
	}
      }
    }
    if (m_verify) Backend::verify_cert_free(m_verify);
    m_verify = Backend::verify_cert_new(m_cacert);
    if (!m_verify) {
      error_(ZeEXCEPT(Error, "Ztls", "verify_cert_new() failed"));
      return false;
    }
    m_ctx.verify_certificate = Backend::verify_cert_cb(m_verify);
    return true;
  }

protected:
  void init_alpn(const ParamStrings &alpn) {
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
      m_alpn.push(ptls_iovec_t{
	m_alpnData.data() + offset,
	s.length()});
    }
  }

private:
  void init_cipher_suites() {
    unsigned n = 0;
    bool use_fusion = false;
#if Ztls_Fusion
    use_fusion = ptls_fusion_is_supported_by_cpu();
#endif
    if (use_fusion) {
#if Ztls_Fusion
      static ptls_cipher_suite_t fusion_aes256gcmsha384 = {
	.id = PTLS_CIPHER_SUITE_AES_256_GCM_SHA384,
	.aead = &ptls_fusion_aes256gcm,
	.hash = Backend::hash_algorithm(SHA384),
	.name = PTLS_CIPHER_SUITE_NAME_AES_256_GCM_SHA384
      };
      static ptls_cipher_suite_t fusion_aes128gcmsha256 = {
	.id = PTLS_CIPHER_SUITE_AES_128_GCM_SHA256,
	.aead = &ptls_fusion_aes128gcm,
	.hash = Backend::hash_algorithm(SHA256),
	.name = PTLS_CIPHER_SUITE_NAME_AES_128_GCM_SHA256
      };
      // Non-temporal fusion decrypt is not exact-in-place safe: it overwrites
      // ciphertext before GHASH finishes reading it. Use normal fusion AEADs.
      m_cipherSuites[n++] = &fusion_aes256gcmsha384;
      m_cipherSuites[n++] = &fusion_aes128gcmsha256;
#endif
    }
    for (auto p = Backend::cipher_suites(); *p; ++p) {
      if (use_fusion &&
	  ((*p)->id == PTLS_CIPHER_SUITE_AES_256_GCM_SHA384 ||
	   (*p)->id == PTLS_CIPHER_SUITE_AES_128_GCM_SHA256))
	continue;
      m_cipherSuites[n++] = *p;
    }
    m_cipherSuites[n] = nullptr;
  }

  // stable after init(); m_errorFn is cleared during final()
  const ZuID			m_id;
  using AllocatorRef = Zi::IOBufAllocator *;
  AllocatorRef		m_rxAllocator = nullptr;
  AllocatorRef		m_socketRxAllocator = nullptr;
  AllocatorRef		m_txAllocator = nullptr;
  AllocatorRef		m_txPacketAllocator = nullptr;
  ZiMultiplex			*m_mx = nullptr;
  ErrorFn			m_errorFn;
  ptls_context_t		m_ctx{};
  ptls_cipher_suite_t		*m_cipherSuites[16]{};
  ALPNData			m_alpnData;
  ALPN				m_alpn;
  Backend::CertStore		*m_cacert = nullptr;
  Backend::VerifyCert		*m_verify = nullptr;
  unsigned			m_rxThread = 0;
  unsigned			m_txThread = 0;
  unsigned			m_asyncThread = 0;
  bool				m_registered = false;

  // shared
  // Exceptional lock-guarded registry used by lifecycle and telemetry callers.
  Links				m_links;

  // Exceptional lifecycle/async coordinator.  Lifecycle code initializes,
  // starts, stops, and finalizes it; internal I/O runs on m_asyncThread.
  alignas(Zm::CacheLineSize)
  ZiEventLoop			m_eventLoop;
  bool				m_eventLoopInit = false;
  bool				m_eventLoopStarted = false;
};

// CRTP - implementation must conform to the following interface:
#if 0
struct App : public Client<App> {
  using RxBufAlloc = Ztls::RxBufAlloc<>;
  using TxBufAlloc = Ztls::TxBufAlloc<BufSize>;

  void exception(ZmRef<ZeEvent>); // optional

  struct Link : public CliLink<App, Link, RxBufAlloc, TxBufAlloc> {
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
template <typename, typename, typename, typename> friend class CliLink;
public:
  using Base = Hub<App>;
friend Base;

  using Base::error_;
  using Base::loadCA;

  const App *app() const { return static_cast<const App *>(this); }
  App *app() { return static_cast<App *>(this); }

  Client() { }
  ~Client() {
    Backend::sign_cert_free(m_sign);
    if (!m_keyRef) Backend::pkey_free(m_key);
  }

  // specify certPath and keyPath for mTLS
  bool init(ClientParams params);

  void final() { Base::final(); }

protected:
  unsigned reconnFreq() const { return 0; } // default

private:
  // stable after init(); released by the destructor
  Backend::PKey			*m_key = nullptr;
  Backend::SignCert		*m_sign = nullptr;
  ZmRef<PK::AnyPK>		m_keyRef;
};

template <typename App>
bool Client<App>::init(ClientParams params)
{
  using Link = App::Link;

  if (bool(params.certPath()) != bool(params.keyPath())) {
    auto errorFn = params.errorFn() ? params.errorFn() : defaultErrorFn();
    errorFn(ZeEXCEPT(Error, "Ztls",
      "client certPath and keyPath must be configured together"));
    return false;
  }
  if (params.certPath() && params.key()) {
    auto errorFn = params.errorFn() ? params.errorFn() : defaultErrorFn();
    errorFn(ZeEXCEPT(Error, "Ztls",
      "client path and in-memory credentials are mutually exclusive"));
    return false;
  }

  return Base::init_(ZuMv(params), [this](ClientParams &params) -> bool {
    static ptls_save_ticket_t save_ticket_cb{
      .cb = [](ptls_save_ticket_t *, ptls_t *tls, ptls_iovec_t input) -> int {
	auto link = static_cast<Link *>(*ptls_get_data_ptr(tls));
	if (link) link->save_ticket(input);
	return 0;
      }
    };
    auto ctx = this->ctx();
    ctx->on_client_hello = nullptr;
    ctx->save_ticket = &save_ticket_cb;
    ctx->sign_certificate = nullptr;
    ctx->encrypt_ticket = nullptr;
    ctx->require_client_authentication = 0;
    if (!loadCA(ZuCSpan{params.caPath()}, params.caCerts())) return false;

    if (params.certPath() && params.keyPath()) {
      if (!Backend::load_certificates(ctx, params.certPath().data()))
	return false;
      m_key = Backend::pkey_load_pem(params.keyPath().data());
      if (!m_key) return false;
      m_sign = Backend::sign_cert_new(m_key);
      if (!m_sign) {
	Backend::pkey_free(m_key);
	m_key = nullptr;
	return false;
      }
      ctx->sign_certificate = Backend::sign_cert_cb(m_sign);
    } else if (params.key()) {
      m_keyRef = ZuMv(params.m_key);
      const auto &certs = params.certs();
      unsigned n = certs.length();
      auto spans = ZtScratch(CertSpansScratch, n);
      for (unsigned i = 0; i < n; ++i) spans.push(certs[i]);
      m_key = m_keyRef->key;
      m_sign = Backend::sign_cert_new(m_key);
      if (!m_sign) {
	m_key = nullptr;
	m_keyRef = {};
	return false;
      }
      if (!Backend::load_certificates_der(ctx, spans, m_key)) {
	Backend::sign_cert_free(m_sign);
	m_sign = nullptr;
	m_key = nullptr;
	m_keyRef = {};
	return false;
      }
      ctx->sign_certificate = Backend::sign_cert_cb(m_sign);
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

  struct Link : public SrvLink<App, Link, RxBufAlloc, TxBufAlloc> {
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
template <typename App_>
class Server : public Hub<App_> {
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
  void release_() {
    Backend::ticket_key_free(m_ticketKey);
    m_ticketKey = nullptr;
    Backend::sign_cert_free(m_sign);
    m_sign = nullptr;
    Backend::pkey_free(m_key);
    m_key = nullptr;
  }

public:

  bool init(ServerParams params);

  void final() { Base::final(); release_(); }

  void listen() {
    if (!this->running()) return;
    mx()->listen(
      ZiListenFn{app(),
	[](App *app, const ZiListenInfo &info) {
	  auto server = static_cast<Server *>(app);
	  server->rxRun([server, info = ZiListenInfo{info}]() {
	    server->listening_(info);
	  });
	}},
      ZiFailFn{app(),
	[](App *app, bool transient) {
	  auto server = static_cast<Server *>(app);
	  server->rxRun([server, transient]() {
	    server->listenFailed_(transient);
	  });
	}},
      ZiConnectFn{app(),
	[](App *app, const ZiCxnInfo &ci) -> ZiConnection * {
	  return app->accepted(ci);
	}},
      app()->localIP(), app()->localPort(), app()->nAccepts(), app()->socketRxAllocator(), ZiCxnOptions());
  }

  void stopListening() {
    mx()->del(&m_rebindTimer);
    if (m_listening)
      mx()->stopListening(m_listenIP, m_listenPort);
    m_listening = false;
  }

  void linkConnected_() {
    ZiAssert(this->rxInvoked(), "Ztls", (),
      "TLS server link admission outside Rx thread", return);
    ++m_pending;
  }
  void linkDisconnected_() {
    ZiAssert(this->rxInvoked(), "Ztls", (),
      "TLS server link completion outside Rx thread", return);
    ZiAssert(m_pending, "Ztls", (),
      "TLS server disconnect without pending connection", return);
    if (!--m_pending && this->stopping() && m_stopDrained)
      Base::stop_0();
  }

protected:
  unsigned nAccepts() const { return 8; } // default
  unsigned rebindFreq() const { return 0; } // default

  void listening_(const ZiListenInfo &info) {
    ZiAssert(this->rxInvoked(), "Ztls", (),
      "TLS listen completion outside Rx thread", return);
    if (!this->running()) {
      m_listenClosing = true;
      mx()->stopListening(info.ip, info.port); return;
    }
    m_listenIP = info.ip; m_listenPort = info.port;
    m_listening = true;
    app()->listening(info);
  }
  void listenFailed_(bool transient) {
    ZiAssert(this->rxInvoked(), "Ztls", (),
      "TLS listen failure outside Rx thread", return);
    if (!this->running()) return;
    app()->listenFailed(transient);
  }

  void listening(const ZiListenInfo &info) { // default
    ZiLOG(Info, "Ztls", ([info](auto &s) {
      s << "listening(" << info.ip << ':' << info.port << ')';
    }));
  }
  void listenFailed(bool transient) { // default
    unsigned rebindFreq = app()->rebindFreq();
    if (transient && rebindFreq > 0)
      app()->mx()->add(
	  &m_rebindTimer, Zm::now(rebindFreq), ZmScheduler::Update,
	  [this](auto &&arm) { return arm([this]() { listen(); }); },
	  app()->rxThread());
    else
      app()->error_(ZeEXCEPT(Error, "Ztls", ([transient](auto &s) {
	s << "listen() failed " << (transient ? "(transient)" : "");
      })));
  }

  // ZmEngine hook.  Public stop(done) retains done until Base::stop_1()
  // calls stopped(true) after every continuation below has drained.
  void stop_() {
    this->rxRun([this]() { stopRx_(); });
  }

  void stopRx_() {
    ZiAssert(this->rxInvoked(), "Ztls", (),
      "TLS server stop initialization outside Rx thread", return);
    stopListening();
    m_stopDrained = false;
    // Stop the listener on the I/O Rx shard before draining accepted
    // connections on our Rx shard; down() requires the installed m_cxn.
    mx()->rxRun([this]() {
      this->rxRun([this]() { stop_0(); });
    });
  }

  void stop_0() {
    ZiAssert(this->rxInvoked(), "Ztls", (),
      "TLS server stop drain outside Rx thread", return);
    if (m_listenClosing) {
      m_listenClosing = false;
      // A late bind queued its close after the initial I/O barrier. Drain that
      // close and any accepted-connection notifications before downLinks().
      mx()->rxRun([this]() {
	this->rxRun([this]() { stop_0(); });
      });
      return;
    }
    // Link lifetime can extend beyond disconnect completion. Count accepted
    // connections until their completion, not retained telemetry links.
    this->template downLinks<typename App::Link>();
    this->rxRun([this]() { stop_2(); });
  }

  void stop_2() {
    ZiAssert(this->rxInvoked(), "Ztls", (),
      "TLS server stop completion outside Rx thread", return);
    m_stopDrained = true;
    if (!m_pending) Base::stop_0();
  }

private:
  // stable after init(); released by the destructor
  Backend::PKey			*m_key = nullptr;
  Backend::SignCert		*m_sign = nullptr;
  Backend::TicketKey		*m_ticketKey = nullptr;

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  ZmScheduler::Timer		m_rebindTimer;
  unsigned			m_pending = 0;
  ZiIP				m_listenIP;
  uint16_t			m_listenPort = 0;
  bool				m_listening = false;
  bool				m_listenClosing = false;
  bool				m_stopDrained = false;
};

template <typename App>
bool Server<App>::init(ServerParams params)
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

  return Base::init_(ZuMv(params), [this](const ServerParams &params) -> bool {
    static ptls_on_client_hello_t on_client_hello_cb{
      .cb = [](ptls_on_client_hello_t *, ptls_t *tls, ptls_on_client_hello_parameters_t *params) -> int {
	auto link = static_cast<Link *>(*ptls_get_data_ptr(tls));
	return link ? link->on_client_hello(tls, params) : 0;
      }
    };
    auto ctx = this->ctx();
    ctx->on_client_hello = &on_client_hello_cb;
    ctx->sign_certificate = nullptr;
    ctx->encrypt_ticket = nullptr;
    ctx->save_ticket = nullptr;
    ctx->require_client_authentication = params.mTLS() ? 1 : 0;
    ctx->max_early_data_size = 0;
    ctx->ticket_lifetime =
      params.cacheTimeout() < 0 ? 86400 : params.cacheTimeout();
    if (!loadCA(ZuCSpan{params.caPath()})) return false;

    if (!Backend::load_certificates(ctx, params.certPath().data()))
      return false;
    m_key = Backend::pkey_load_pem(params.keyPath().data());
    if (!m_key) return false;
    m_sign = Backend::sign_cert_new(m_key);
    if (!m_sign) {
      Backend::pkey_free(m_key);
      m_key = nullptr;
      return false;
    }
    if (params.asyncThread() && !Backend::sign_cert_async(m_sign, true)) {
      error_(ZeEXCEPT(Error, "Ztls",
	"async server certificate signing is unsupported"));
      return false;
    }
    ctx->sign_certificate = Backend::sign_cert_cb(m_sign);
    if (!m_ticketKey) m_ticketKey = Backend::ticket_key_new();
    if (!m_ticketKey) return false;
    ctx->encrypt_ticket = Backend::ticket_encrypt_cb(m_ticketKey);
    return true;
  });
}

}

#endif /* Ztls_HH */
