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

#include <zlib/ZmList.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmEngine.hh>
#include <zlib/ZtArray.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiAssert.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiEventLoop.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiRx.hh>
#include <zlib/ZiTx.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/ZiTxStream.hh>
#include <zlib/ZiTransport.hh>

#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsBackend.hh>

#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <fcntl.h>
#endif

namespace Ztls_ {

ZuDerive(IOQueue,
  (ZmList<ZiIOBuf, ZmListNode<ZiIOBuf, ZmListHeapID<"">>>));

using RxStream = ZiRxStream<IOQueue>;

template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = ZiIOBuf_HeapID{}()>
using BufAlloc =
  Zi::IOBufAlloc<IOQueue::Node, Size, MaxSize, ZuStringT<HeapID>>;

} // namespace Ztls_

namespace Ztls {

// heap-allocated vocabulary types

ZuDerive(LogMsg, ZtString<ZtStringHeapID<"Ztls.Log">>);
ZuDerive(Host, ZtString<ZtStringHeapID<"Ztls.Host">>);
ZuDerive(Ticket, (ZtArray<uint8_t, ZtArrayHeapID<"Ztls.Ticket">>));
ZuDerive(ALPNData, (ZtArray<uint8_t, ZtArrayHeapID<"Ztls.ALPNData">>));
ZuDerive(ALPN, (ZtArray<ptls_iovec_t, ZtArrayHeapID<"Ztls.ALPN">>));
using ErrorFn = ZmFn<void(ZeException)>;
ZuDerive(ParamString, ZtString<ZtStringHeapID<"Ztls.Param">>);
ZuDerive(ParamStrings,
  (ZtArray<ParamString, ZtArrayHeapID<"Ztls.ParamStrings">>));

inline ErrorFn defaultErrorFn()
{
  return ErrorFn{[](ZeException e) { ZiLogEvent(ZuMv(e)); }};
}

struct EngineParams {
  EngineParams(
    ZiMultiplex *mx = nullptr,
    ZuCSpan rxThread = {},
    ZuCSpan txThread = {}) :
      m_mx{mx}, m_rxThread{rxThread}, m_txThread{txThread},
      m_errorFn{defaultErrorFn()} { }

  EngineParams &&caPath(ZuCSpan v) { m_caPath = v; return ZuMv(*this); }
  EngineParams &&certPath(ZuCSpan v) { m_certPath = v; return ZuMv(*this); }
  EngineParams &&keyPath(ZuCSpan v) { m_keyPath = v; return ZuMv(*this); }
  EngineParams &&asyncThread(ZuCSpan v) {
    m_asyncThread = v;
    return ZuMv(*this);
  }
  EngineParams &&alpn(ZuSpan<ZuCSpan> v) {
    m_alpn.length(0);
    m_alpn.ensure(v.length());
    for (auto &s : v) m_alpn.push(ParamString{s});
    return ZuMv(*this);
  }
  EngineParams &&errorFn(ErrorFn v) { m_errorFn = ZuMv(v); return ZuMv(*this); }

  ZiMultiplex *mx() const { return m_mx; }
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
  ZiMultiplex	*m_mx = nullptr;
  ParamString	m_rxThread;
  ParamString	m_txThread;
  ParamStrings	m_alpn;
  ParamString	m_caPath;
  ParamString	m_certPath;
  ParamString	m_keyPath;
  ParamString	m_asyncThread;
  ErrorFn	m_errorFn;
};

using ClientParams = EngineParams;

struct ServerParams : public EngineParams {
  using EngineParams::EngineParams;
  using EngineParams::caPath;
  using EngineParams::certPath;
  using EngineParams::keyPath;
  using EngineParams::asyncThread;
  using EngineParams::alpn;
  using EngineParams::errorFn;

  ServerParams &&caPath(ZuCSpan v)
    { EngineParams::caPath(v); return ZuMv(*this); }
  ServerParams &&certPath(ZuCSpan v)
    { EngineParams::certPath(v); return ZuMv(*this); }
  ServerParams &&keyPath(ZuCSpan v)
    { EngineParams::keyPath(v); return ZuMv(*this); }
  ServerParams &&asyncThread(ZuCSpan v)
    { EngineParams::asyncThread(v); return ZuMv(*this); }
  ServerParams &&alpn(ZuSpan<ZuCSpan> v)
    { EngineParams::alpn(v); return ZuMv(*this); }
  ServerParams &&errorFn(ErrorFn v)
    { EngineParams::errorFn(ZuMv(v)); return ZuMv(*this); }
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

// zpicotls control state runs on the Ztls Rx thread. Application Tx is posted
// to the Ztls Tx thread; later record-protection phases remove shared ptls_t
// use from application data records.

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
    ZiConnection(link->app()->mx(), ci), m_link(ZuMv(link)) { }

  void connected(ZiIOContext &io) { m_link->connected_0(this, io); }
  void disconnected(bool peer) {
    if (Link *link = m_link) link->disconnected_0(this, ZuMv(m_link), peer);
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
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = "Ztls.RxBuf">
using RxBufAlloc = Ztls_::BufAlloc<Size, MaxSize, HeapID>;

template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = "Ztls.TxBuf">
using TxBufAlloc = Ztls_::BufAlloc<Size, MaxSize, HeapID>;

template <typename RxBufAlloc>
inline int parseHdr(const ZiIOContext &, ZiIOBuf *buf) {
  if (ZuUnlikely(buf->length < 5)) return INT_MAX;
  auto hdr = buf->data();
  auto n = 5U + ((unsigned(hdr[3]) << 8) | unsigned(hdr[4]));
  if (n > RxBufAlloc::MaxSize) return -1;
  return n;
}

template <
  typename App, typename Impl, typename RxBufAlloc_, typename TxBufAlloc_,
  typename Cxn_, typename CxnRef_>
class Link :
  public ZmPolymorph,
  public ZiRx<Impl, RxBufAlloc_>,
  public ZiTx<Impl>
{
  ZuAssert((ZuIs_<RxBufAlloc_, Ztls_::IOQueue::Node>{}));
  ZuAssert((ZuIs_<TxBufAlloc_, Ztls_::IOQueue::Node>{}));

public:
  using RxBufAlloc = RxBufAlloc_;
  using TxBufAlloc = TxBufAlloc_;
  using Cxn = Cxn_;
  using CxnRef = CxnRef_;
  using Rx = ZiRx<Impl, RxBufAlloc>;
  using Tx = ZiTx<Impl>;
  using ImplRef = typename Cxn::LinkRef;
  using Stream = Impl;
  using StreamRef = Impl *;

friend Cxn;

private:
  struct AsyncJob;
  using AsyncJobRef = ZmRef<AsyncJob>;

public:
  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  Link(App *app, bool isServer) : m_app(app), m_isServer(isServer) { }
  ~Link() {
    if (m_tls) {
      if (ZuUnlikely(asyncPending_()))
	app()->error_(ZeEXCEPT(Error, "Ztls",
	  "TLS link destroyed with async job pending"));
      else
	ptls_free(m_tls);
    }
  }

  App *app() const { return m_app; }
  Cxn *cxn() const { return m_cxn; }
  StreamRef stream(Zi::StreamType::T type = Zi::StreamType::Duplex) {
    return type == Zi::StreamType::Duplex ? impl() : nullptr;
  }
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
  void reset_handshake_props_() { memset(&m_props, 0, sizeof(m_props)); }

private:
  static constexpr unsigned TxRecordCapacity = 16 * 1024;
  static constexpr unsigned TxMaxOverhead = 325;
  static constexpr unsigned TxMaxPlaintext = TxRecordCapacity - TxMaxOverhead;

  // called from Cxn::connected(ZiIOContext &)
  // this is the internal TCP-level connected; once handshake is completed,
  // the application's connected_() will be called
  void connected_0(Cxn *cxn, ZiIOContext &io) { // runs on I/O Rx thread
    app()->rxRun([impl = ZmMkRef(this->impl()), cxn = ZmMkRef(cxn)]() {
      impl->connected_1(ZuMv(cxn));
    });
    Rx::template recv<parseHdr<RxBufAlloc>, &Impl::recvRecord>(io);
  }

  void connected_1(ZmRef<Cxn> cxn) {
    // direct call from within rx thread
    ZiAssert(app()->rxInvoked(), "Ztls", (),
      "TLS connected dispatch outside Rx thread", return);
    // idempotent
    if (ZuUnlikely(m_cxn == cxn)) return;
    // handle overlapping connections
    if (ZuUnlikely(m_cxn)) { auto cxn_ = ZuMv(m_cxn); cxn_->close(); }
    m_cxn = ZuMv(cxn);
    impl()->connected_(); // client initiates handshake
  }

  int recvRecord(const ZiIOContext &io, ZmRef<ZiIOBuf> buf) {
    auto n = int(buf->length);
    auto cxn = static_cast<Cxn *>(io.cxn);
    app()->rxRun([
      impl = ZmMkRef(this->impl()),
      cxn = ZmMkRef(cxn),
      buf = ZuMv(buf)
    ]() mutable {
      if (ZuUnlikely(impl->m_cxn != cxn.ptr())) return;
      impl->record_(ZuMv(buf));
    });
    return n;
  }

  void record_(ZmRef<ZiIOBuf> buf) {
    // direct call from within rx thread
    ZiAssert(app()->rxInvoked(), "Ztls", (),
      "TLS record dispatch outside Rx thread", return);
    if (ZuUnlikely(!m_tls)) return;
    // Handshake records do not enter the application Rx path.
    if (ptls_handshake_is_complete(m_tls))
      rcvd_(ZuMv(buf));
    else
      handshake_(ZuMv(buf));
  }

protected:
  bool handshake_(ZmRef<ZiIOBuf> buf) {
    // direct call from within rx thread
    ZiAssert(app()->rxInvoked(), "Ztls", (),
      "TLS handshake outside Rx thread", return false);
    const uint8_t *input = nullptr;
    size_t inlen_;
    size_t *inlen = nullptr;
    if (buf) {
      input = buf->data();
      inlen_ = buf->length;
      inlen = &inlen_;
    }
    if (buf && ZuUnlikely(asyncPending_())) {
      app()->error_(ZeEXCEPT(Error, "Ztls",
	"TLS handshake input received while async operation is pending"));
      disconnect_(false);
      return false;
    }

    ptls_buffer_t pbuf;
    auto txbuf = txBuf(pbuf);
    int n;
    if (ZuUnlikely(!txbuf))
      n = PTLS_ERROR_NO_MEMORY;
    else {
      // Handshake input is read-only here; pbuf is only handshake Tx output.
      n = ptls_handshake(m_tls, &pbuf, input, inlen, &m_props);
      finalizeTxBuf_(pbuf, ZuMv(txbuf));
    }
    if (!n) return finishHandshake_();
    if (n == PTLS_ERROR_IN_PROGRESS) return true;
    if (n == PTLS_ERROR_ASYNC_OPERATION) return asyncHandshake_();
    if (PTLS_ERROR_GET_CLASS(n) == PTLS_ERROR_CLASS_PEER_ALERT &&
	PTLS_ERROR_TO_ALERT(n) == PTLS_ALERT_CLOSE_NOTIFY) {
      disconnect_(true);
      return false;
    }
    app()->error_(ZeEXCEPT(Error, "Ztls", ([n](auto &s) {
      s << "ptls_handshake(): " << strerror_(n);
    })));
    disconnect_(false);
    return false;
  }

private:
  bool finishHandshake_() {
    if (m_handshook) return true;
    asyncCleanup_();
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
    m_txSeqEst = 0;
    m_txControlPending = false;
    m_handshook = true;
    const char *alpn = ptls_get_negotiated_protocol(m_tls);
    impl()->connected(Zi::Connected{
      .transport = Zi::Transport::TLS,
      .alpn = alpn ? ZuCSpan{alpn, unsigned(strlen(alpn))} : ZuCSpan{},
      .version = tlsver_(tlsver)
    });
    return true;
  }

  ptls_cipher_suite_t *cipher_() const {
    return m_tls ? ptls_get_cipher(const_cast<ptls_t *>(m_tls)) : nullptr;
  }

  unsigned alignBits_() const {
    if (auto cipher = cipher_())
      if (cipher->aead) return cipher->aead->align_bits;
    return 0;
  }

  bool aligned_(const uint8_t *base, unsigned align_bits) {
    if (!align_bits) return true;
    uintptr_t mask = (uintptr_t(1) << align_bits) - 1;
    ZiAssert(!(reinterpret_cast<uintptr_t>(base) & mask), "Ztls", (),
      "TLS buffer misaligned", return false);
    return true;
  }

  bool assertTxBuf_(ptls_buffer_t &pbuf, ZiIOBuf *buf) {
    // Ztls-owned Tx output is always the origin buffer at data_().
    auto raw = buf->data_();
    ZiAssert(pbuf.origin == buf, "Ztls", (),
      "TLS Tx buffer origin mismatch", return false);
    auto offset = pbuf.base - raw;
    ZiAssert(!offset, "Ztls", (),
      "TLS Tx buffer base shifted", return false);
    ZiAssert(pbuf.off <= UINT32_MAX && pbuf.off <= buf->size,
      "Ztls", (), "TLS Tx buffer bounds exceeded", return false);
    return true;
  }

  bool finalizeTxBuf_(ptls_buffer_t &pbuf, ZmRef<ZiIOBuf> buf) {
    if (!assertTxBuf_(pbuf, buf.ptr())) return false;
    if (!pbuf.off) return true;
    // Publish exactly the serialized TLS record bytes.
    buf->skip = 0;
    buf->length = uint32_t(pbuf.off);
    Tx::send(ZuMv(buf));
    return true;
  }

  bool assertRxBuf_(ptls_buffer_t &pbuf, ZiIOBuf *buf, uint8_t *plain) {
    // Ztls-owned Rx output is plaintext behind the record headroom.
    ZiAssert(pbuf.origin == buf, "Ztls", (),
      "TLS Rx buffer origin mismatch", return false);
    ZiAssert(pbuf.base == plain, "Ztls", (),
      "TLS Rx buffer base mismatch", return false);
    ZiAssert(pbuf.off <= UINT32_MAX && pbuf.off <= buf->size - m_headroom,
      "Ztls", (), "TLS Rx buffer bounds exceeded", return false);
    return true;
  }

  // Handshake/control Tx starts at data_(); application Tx uses its own buffer.
  ZmRef<ZiIOBuf> txBuf(ptls_buffer_t &pbuf) {
    ZmRef<ZiIOBuf> buf = new TxBufAlloc{impl()};
    if (ZuUnlikely(buf->size < TxRecordCapacity))
      if (ZuUnlikely(!buf->ensure(TxRecordCapacity))) return nullptr;
    auto base = buf->data_(); // record base; skip is 0 on this path
    auto align_bits = alignBits_();
    if (!aligned_(base, align_bits)) return nullptr;
    ptls_buffer_init_tx(&pbuf, base, TxRecordCapacity);
    pbuf.origin = buf.ptr();
    pbuf.align_bits = align_bits;
    return buf;
  }

  void rcvd_(ZmRef<ZiIOBuf> buf) {
    ptls_buffer_t pbuf;
    auto base = rxBuf(pbuf, buf);
    if (ZuUnlikely(!base)) return;
    size_t inlen = buf->length;
    // Decrypt in-place from record body to plaintext behind headroom.
    int n = ptls_receive(m_tls, &pbuf, base, &inlen); // in-place overwrite
    auto plain = base + m_headroom;
    if (!assertRxBuf_(pbuf, buf.ptr(), plain)) return;
    // Transport framing supplies exactly one complete TLS record.
    if (ZuUnlikely(inlen != buf->length)) {
      app()->error_(ZeEXCEPT(Error, "Ztls",
	"ptls_receive() partial record"));
      disconnect_(false);
      return;
    }
    if (!n) {
      if (pbuf.off) {
	// Application plaintext becomes the active ZiIOBuf span.
	buf->skip = m_headroom;
	buf->length = uint32_t(pbuf.off);
	m_rxStream.push(ZuMv(buf));
      } else {
	// A zero-output post-handshake control record can queue a reciprocal
	// KeyUpdate inside zpicotls; flush it before the next application Tx.
	m_txControlPending = true;
      }
      while (m_rxStream) {
	int n = impl()->process(m_rxStream);
	if (ZuUnlikely(!n)) return;
	if (ZuUnlikely(n < 0)) {
	  disconnect_(true);
	  return;
	}
      }
      return;
    }
    if (PTLS_ERROR_GET_CLASS(n) == PTLS_ERROR_CLASS_PEER_ALERT &&
	PTLS_ERROR_TO_ALERT(n) == PTLS_ALERT_CLOSE_NOTIFY) {
      disconnect_(true);
      return;
    }
    app()->error_(ZeEXCEPT(Error, "Ztls", ([n](auto &s) {
      s << "ptls_receive(): " << strerror_(n);
    })));
    disconnect_(false);
    return;
  }

  uint8_t *rxBuf(ptls_buffer_t &pbuf, ZiIOBuf *buf) {
    ZiAssert(!buf->skip, "Ztls", (),
      "TLS Rx buffer skip is non-zero", return nullptr);
    ZiAssert(buf->size >= ptls_get_record_overhead(m_tls), "Ztls", (),
      "TLS Rx buffer smaller than record overhead", return nullptr);
    ZiAssert(buf->length <= UINT32_MAX - m_headroom, "Ztls", (),
      "TLS Rx buffer length overflow", return nullptr);
    auto required = buf->length + m_headroom;
    // Reserve before ptls_receive(); zpicotls parses input pointers first.
    if (ZuUnlikely(buf->size < required))
      if (ZuUnlikely(!buf->ensure(required))) {
	app()->error_(ZeEXCEPT(Error, "Ztls",
	  "TLS Rx buffer growth failed"));
	disconnect_(false);
	return nullptr;
      }
    auto base = buf->data_(); // record base; skip is asserted 0 above
    auto align_bits = alignBits_();
    if (!aligned_(base, align_bits)) return nullptr;
    // Capacity starts at plaintext base but spans the full record reserve.
    ptls_buffer_init_rx(&pbuf, base + m_headroom, buf->size - m_headroom);
    pbuf.origin = buf;
    pbuf.align_bits = align_bits;
    return base;
  }

  template <typename ImplRef_>
  void disconnected_0(Cxn *cxn, ImplRef_ impl_, bool peer) {
    ZmRef<Impl> impl{ZuMv(impl_)};
    app()->rxRun([impl = ZuMv(impl), cxn = ZmMkRef(cxn), peer]() {
      impl->disconnected_(cxn, peer);
      auto mx = cxn->mx();
      // drain Tx while keeping cxn/impl referenced
      mx->txRun([impl = ZuMv(impl), cxn = ZuMv(cxn)]() { });
    });
  }
  void disconnected_(Cxn *cxn, bool peer) {
    // direct call from within rx thread
    ZiAssert(app()->rxInvoked(), "Ztls", (),
      "TLS disconnected dispatch outside Rx thread", return);
    if (m_cxn == cxn) m_cxn = nullptr;
    reset_tls_();
    impl()->disconnected(peer);
  }

private:
  ZmRef<ZiIOBuf> allocTxBuf_() { // App/Rx/Tx threads
    ZmRef<ZiIOBuf> buf = new TxBufAlloc{impl()};
    if (ZuUnlikely(buf->size < TxRecordCapacity))
      if (ZuUnlikely(!buf->ensure(TxRecordCapacity))) return nullptr;
    // Application plaintext is staged after record headroom.
    buf->skip = m_headroom;
    buf->length = 0;
    return buf;
  }

  template <bool AppThread>
  class TxStream_ : public Zi::TxStream<TxStream_<AppThread>> {
    using Base = Zi::TxStream<TxStream_<AppThread>>;

  public:
    TxStream_(Link &link) :
      Base(
	unsigned(TxRecordCapacity),
	unsigned(link.m_headroom),
	unsigned(TxMaxOverhead - link.m_headroom)),
      m_link{&link},
      m_gen{link.m_tlsGen.load_()}
    {
    }

    ZmRef<ZiIOBuf> allocBuf_(unsigned skip) {
      auto buf = m_link->allocTxBuf_();
      if constexpr (AppThread) {
	if (ZuUnlikely(!buf || buf->skip > skip))
	  throw TxStreamAllocFailure{};
	buf->skip = skip;
      } else {
	if (ZuUnlikely(!buf || buf->skip != skip))
	  throw TxStreamAllocFailure{};
      }
      return buf;
    }

    void sendBuf_(ZmRef<ZiIOBuf> buf) {
      buf->owner = m_link->impl();
      auto link = static_cast<Impl *>(buf->owner);
      if constexpr (AppThread)
	link->send(ZuMv(buf), m_gen);
      else
	link->send_(ZuMv(buf), m_gen);
    }

  private:
    // immutable
    Link	*m_link;
    uint64_t	m_gen = 0;
  };

public:
  auto txStream() { // App thread(s)
    return TxStream_<true>{*this};
  }
  auto txStream_() { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Ztls", (),
      "TLS txStream_ outside Tx thread", return TxStream_<false>{*this});
    return TxStream_<false>{*this};
  }

protected:
  bool updateKey_(bool requestUpdate = false) {
    // direct call from within rx thread
    ZiAssert(app()->rxInvoked(), "Ztls", (),
      "TLS KeyUpdate outside Rx thread", return false);
    if (ZuUnlikely(!m_tls || !cipher_())) return false;
    ZiAssert(ptls_get_protocol_version(m_tls) == PTLS_PROTOCOL_VERSION_TLS13,
      "Ztls", (),
      "TLS KeyUpdate requires TLS 1.3", return false);
    int n = ptls_update_key(m_tls, requestUpdate ? 1 : 0);
    if (n) {
      app()->error_(ZeEXCEPT(Error, "Ztls", ([n](auto &s) {
	s << "ptls_update_key(): " << strerror_(n);
      })));
      disconnect_(false);
      return false;
    }
    return flushTxControl_(true);
  }

private:
  struct TxStreamAllocFailure { };

public:
  void send(ZmRef<ZiIOBuf> buf) {
    send(ZuMv(buf), m_tlsGen.load_());
  }
  void send(ZmRef<ZiIOBuf> buf, uint64_t gen) {
    if (ZuUnlikely(!buf || !buf->length)) return;
    if (ZuUnlikely(m_disconnecting.load_())) return;
    if (ZuUnlikely(gen != m_tlsGen.load_())) return;
    buf->owner = impl();
    app()->txInvoke([buf = ZuMv(buf), gen]() mutable {
      auto link = static_cast<Impl *>(buf->owner);
      link->send_(ZuMv(buf), gen);
    });
  }

private:
  bool flushTxControl_(bool expectOutput) {
    ptls_buffer_t pbuf;
    auto buf = txBuf(pbuf);
    if (ZuUnlikely(!buf)) {
      app()->error_(ZeEXCEPT(Error, "Ztls",
	"TLS Tx buffer allocation failed"));
      disconnect_(false);
      return false;
    }
    // KeyUpdate/control records are serialized before application data.
    int n = ptls_send(m_tls, &pbuf, nullptr, 0);
    if (!assertTxBuf_(pbuf, buf.ptr())) return false;
    if (n) {
      app()->error_(ZeEXCEPT(Error, "Ztls", ([n](auto &s) {
	s << "ptls_send(): " << strerror_(n);
      })));
      disconnect_(false);
      return false;
    }
    ZiAssert(!expectOutput || pbuf.off, "Ztls", (),
      "TLS control Tx produced no record", return false);
    bool sent = pbuf.off;
    if (!finalizeTxBuf_(pbuf, ZuMv(buf))) return false;
    if (sent) m_txSeqEst = 0;
    m_txControlPending = false;
    return true;
  }

protected:
  void send_(ZmRef<ZiIOBuf> buf) { // direct call from within tx thread
    send_(ZuMv(buf), m_tlsGen.load_());
  }
  void send_(ZmRef<ZiIOBuf> buf, uint64_t gen) {
    // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Ztls", (),
      "TLS send_ outside Tx thread", return);
    if (ZuUnlikely(!buf || !buf->length)) return;
    if (ZuUnlikely(m_disconnecting.load_())) return;
    if (ZuUnlikely(gen != m_tlsGen.load_())) return;
    buf->owner = impl();
    app()->rxRun([buf = ZuMv(buf), gen]() mutable {
      auto link = static_cast<Impl *>(buf->owner);
      link->sendRx_(ZuMv(buf), gen);
    });
  }

private:
  void sendRx_(ZmRef<ZiIOBuf> buf, uint64_t gen) {
    // direct call from within rx thread
    ZiAssert(app()->rxInvoked(), "Ztls", (),
      "TLS legacy send outside Rx thread", return);
    if (ZuUnlikely(!buf || !buf->length)) return;
    if (ZuUnlikely(m_disconnecting.load_())) return;
    if (ZuUnlikely(gen != m_tlsGen.load_())) return;
    if (ZuUnlikely(!m_tls || !cipher_())) return; // FIXME - log diagnostic

    ZiAssert(buf->length <= TxMaxPlaintext, "Ztls", (),
      "TLS Tx plaintext exceeds record limit", return);
    ZiAssert(buf->skip == m_headroom, "Ztls", (),
      "TLS Tx buffer missing headroom", return);

    if (ZuUnlikely(buf->size < TxRecordCapacity))
      if (ZuUnlikely(!buf->ensure(TxRecordCapacity))) {
	app()->error_(ZeEXCEPT(Error, "Ztls",
	  "TLS Tx buffer growth failed"));
	disconnect_(false);
	return;
      }

    ptls_buffer_t pbuf;
    constexpr uint64_t Threshold = (1ULL<<24);
    bool expectControl = false;
    if (ZuUnlikely(m_txSeqEst >= Threshold - 1)) {
      int n = ptls_update_key(m_tls, 0);
      if (n) {
	app()->error_(ZeEXCEPT(Error, "Ztls", ([n](auto &s) {
	  s << "ptls_update_key(): " << strerror_(n);
	})));
	disconnect_(false);
	return;
      }
      expectControl = true;
    }
    if (ZuUnlikely(m_txControlPending || expectControl))
      if (!flushTxControl_(expectControl)) return;

    // Application Tx encrypts data() into a serialized record at data_().
    auto data = buf->data();
    auto length = buf->length;
    auto base = buf->data_();
    auto align_bits = alignBits_();
    if (!aligned_(base, align_bits)) return;
    ptls_buffer_init_tx(&pbuf, base, TxRecordCapacity);
    pbuf.origin = buf.ptr();
    pbuf.align_bits = align_bits;
    int n = ptls_send(m_tls, &pbuf, data, length); // in-place overwrite
    if (pbuf.off) ++m_txSeqEst;
    if (!assertTxBuf_(pbuf, buf.ptr())) return;
    if (n) {
      app()->error_(ZeEXCEPT(Error, "Ztls", ([n](auto &s) {
	s << "ptls_send(): " << strerror_(n);
      })));
      disconnect_(false);
      return;
    }
    finalizeTxBuf_(pbuf, ZuMv(buf));
  }

  int send_alert_(uint8_t level, uint8_t desc) {
    // direct call from within rx thread
    ZiAssert(app()->rxInvoked(), "Ztls", (),
      "TLS alert outside Rx thread", return PTLS_ERROR_LIBRARY);
    ptls_buffer_t pbuf;
    auto buf = txBuf(pbuf);
    if (ZuUnlikely(!buf)) return PTLS_ERROR_NO_MEMORY;
    // Alerts share the handshake/control Tx buffer invariant.
    int n = ptls_send_alert(m_tls, &pbuf, level, desc);
    finalizeTxBuf_(pbuf, ZuMv(buf));
    return n;
  }

private:
  bool asyncPending_() const {
    return m_asyncJob && m_asyncJob->tls == m_tls;
  }

  void clearAsync_() {
    m_asyncJob = nullptr;
  }

  bool asyncHandshake_() {
    if (ZuUnlikely(!app()->asyncConfigured_())) {
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

    asyncCleanup_();

    AsyncJobRef async = new AsyncJob{
      ZmMkRef(impl()), tls(), job, handle, m_tlsGen.load_()};
    m_asyncJob = async.ptr();

    if (ZuUnlikely(!app()->asyncAddHandle_(
	  handle,
	  ZiEvent::HandleSendFn{[](Zi::Handle) { }},
	  ZiEvent::HandleRecvFn{
	    async, ZmFnPtr<&AsyncJob::ready>{}}))) {
      clearAsync_();
      app()->error_(ZeEXCEPT(Error, "Ztls",
	"ZiEventLoop::addHandle() failed for zpicotls async job"));
      disconnect_(false);
      return false;
    }

    return true;
  }

  void asyncReady_(AsyncJobRef async, Zi::Handle handle_) {
    if (ZuUnlikely(handle_ != async->handle)) return;
    if (++async->recvCount != 2) return;
    async->handle = Zi::nullHandle();
    app()->asyncDelHandleNow_(handle_);
    app()->rxRun(ZmFn<>{
      ZuMv(async), [](AsyncJob *async) {
	async->link->asyncResume_(async);
      }});
  }

  void asyncResume_(AsyncJob *async) {
    if (async->tls == m_tls &&
	async->gen == m_tlsGen.load_() &&
	m_asyncJob == async) {
      clearAsync_();
      handshake_(nullptr);
      return;
    }
    if (async->retired && async->tls) {
      ptls_free(async->tls);
      async->tls = nullptr;
    }
  }

  void asyncCleanup_() {
    AsyncJobRef async;
    if (m_asyncJob) async = ZmMkRef(m_asyncJob);
    clearAsync_();
    if (!async) return;
    Zi::Handle handle = async->handle;
    async->handle = Zi::nullHandle();
    if (!Zi::nullHandle(handle)) app()->asyncDelHandle_(handle);
  }

  bool asyncRetireTLS_() {
    if (!asyncPending_()) return false;
    m_asyncJob->retired = true;
    clearAsync_();
    return true;
  }

  void asyncDestroyed_(AsyncJob *async) {
    if (m_asyncJob == async) m_asyncJob = nullptr;
  }

public:
  void disconnect() { // App thread(s)
    m_disconnecting = 1;
    app()->rxInvoke([this]() { disconnect_(); });
  }
  void disconnect_(bool notify = true) { // direct call from within rx thread
    ZiAssert(app()->rxInvoked(), "Ztls", (),
      "TLS disconnect outside Rx thread", return);
    m_disconnecting = 1; // disconnect() might be bypassed
    app()->mx()->del(&m_reconnTimer);
    bool asyncPending = asyncPending_();
    if (notify && !asyncPending) {
      int n = m_tls ?
	send_alert_(PTLS_ALERT_LEVEL_WARNING, PTLS_ALERT_CLOSE_NOTIFY) :
	0;
      if (n) ZiLOG(Warning, "Ztls", ([n](auto &s) {
	s << "ptls_send_alert(): " << strerror_(n);
      }));
    }
    if (asyncPending) {
      asyncRetireTLS_();
      m_tls = nullptr;
    }
    auto cxn = ZmRef<Cxn>{ZuMv(m_cxn)};
    m_cxn = nullptr;
    if (cxn) {
      auto mx = cxn->mx();
      if (notify) {
	// drain Tx while keeping cxn referenced
	mx->txRun([cxn = ZuMv(cxn)]() { cxn->disconnect(); });
      } else
	cxn->close();
    }
  }

protected:
  static int tlsver_(uint16_t v) {
    switch (v) {
      case 0x0303: return 12;
      case 0x0304: return 13;
      default: return 0;
    }
  }

  void reset_tls_() {
    m_tlsGen.store_(m_tlsGen.load_() + 1);
    if (m_tls) {
      if (!asyncRetireTLS_())
	ptls_free(m_tls);
    }
    m_tls = ptls_new(app()->ctx(), m_isServer);
    if (!m_tls) {
      app()->error_(ZeEXCEPT(Error, "Ztls", "ptls_new() failed"));
      return;
    }
    *ptls_get_data_ptr(m_tls) = impl();
    m_headroom = 0;
    m_txSeqEst = 0;
    m_txControlPending = false;
    m_handshook = false;
    m_disconnecting = 0;
    reset_handshake_props_();

    m_rxStream.clean();
  }

private:
  // immutable
  App			*m_app = nullptr;
  bool			m_isServer = false;

  // Rx thread exclusive
  ZmScheduler::Timer	m_reconnTimer;

  struct AsyncJob : public ZmPolymorph {
    AsyncJob(
	ZmRef<Impl> link_, ptls_t *tls_, ptls_async_job_t *job_,
	Zi::Handle handle_, uint64_t gen_) :
	link{ZuMv(link_)}, tls{tls_}, job{job_}, handle{handle_}, gen{gen_} {
    }
    ~AsyncJob() {
      link->asyncDestroyed_(this);
      if (retired && tls)
	link->app()->error_(ZeEXCEPT(Error, "Ztls",
	  "TLS async job destroyed while still pending"));
    }
    void ready(Zi::Handle handle_) {
      link->asyncReady_(ZmMkRef(this), handle_);
    }

    ZmRef<Impl>		link;
    ptls_t		*tls = nullptr;
    ptls_async_job_t	*job = nullptr;
    Zi::Handle		handle = Zi::nullHandle();
    uint64_t		gen = 0;
    ZmAtomic<unsigned>	recvCount = 0;
    bool		retired = false;
  };

  ptls_t		*m_tls = nullptr;
  unsigned		m_headroom = 0;
  uint64_t		m_txSeqEst = 0;
  bool			m_txControlPending = false;
  bool			m_handshook = false;
  ptls_handshake_properties_t m_props{};
  AsyncJob		*m_asyncJob = nullptr;
  CxnRef		m_cxn = nullptr;	// read by Tx thread
  RxStream		m_rxStream;

  // shared
  ZmAtomic<uint64_t>	m_tlsGen = 0;
  ZmAtomic<unsigned>	m_disconnecting = 0;
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
  using Base::reset_tls_;
  using Base::handshake_props;
  using Base::reset_handshake_props_;
  using Base::handshake_;
  using Base::tls;

friend Base;
template <typename> friend class Client;

  CliLink(App *app) : Base{app, false} { }
  CliLink(App *app, Host server, uint16_t port) :
      Base{app, false}, m_server{ZuMv(server)}, m_port{port} { }
  ~CliLink() { }

  void connect() { // App thread(s)
    app()->rxInvoke([this]() mutable { connect_(); });
  }
  void connect(Host server, uint16_t port) { // App thread(s)
    m_server = ZuMv(server);
    m_port = port;
    app()->rxInvoke([this]() mutable { connect_(); });
  }

  const Host &server() const { return m_server; }
  uint16_t port() const { return m_port; }

  void connect_() { // direct call from within rx thread
    ZiAssert(app()->rxInvoked(), "Ztls", (),
      "TLS connect outside Rx thread", return);
    ZiIP ip = m_server;
    if (!ip) {
      app()->error_(ZeEXCEPT(Error, "Ztls", ([server = LogMsg{m_server}](auto &s) {
	s << '"' << server << "\": hostname lookup failure";
      })));
      impl()->connectFailed(true);
      return;
    }
    reset_tls_();
    {
      int n = ptls_set_server_name(
	tls(), m_server.data(), m_server.length());
      if (n) {
	app()->error_(ZeEXCEPT(Error, "Ztls", ([server = LogMsg{m_server}, n](auto &s) {
	  s << "ptls_set_server_name(\"" << server << "\"): " <<
	    strerror_(n);
	})));
	impl()->connectFailed(true);
	return;
      }
    }

    app()->mx()->connect(
      ZiConnectFn{ZmMkRef(impl()),
	[](Impl *impl, const ZiCxnInfo &ci) -> ZiConnection * {
	  return new Cxn(impl, ci);
	}},
      ZiFailFn{ZmMkRef(impl()), [](Impl *impl, bool transient) {
	impl->connectFailed(transient);
      }},
      ZiIP(), 0, ip, m_port);
  }

private:
  void save_ticket(ptls_iovec_t input) {
    if (!input.len || !input.base) {
      m_ticket.clear();
      return;
    }
    m_ticket.length(0);
    m_ticket.append(input.base, input.len);
  }

  // client connected variant - initiate new handshake
  void connected_() {
    reset_handshake_props_();

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
    unsigned reconnFreq = app()->reconnFreq();
    if (transient && reconnFreq > 0)
	app()->mx()->add(
	  &m_reconnTimer, Zm::now(reconnFreq), ZmScheduler::Update,
	  [this](auto &&arm) {
	    return arm(ZmFn<>{this, [](CliLink *link) { link->connect_(); }});
	  }, app()->rxThread());
    else
      app()->error_(ZeEXCEPT(Error, "Ztls", "connect failed"));
  }

private:
  // Rx thread exclusive
  ZmScheduler::Timer	m_reconnTimer;
  Ticket		m_ticket;
  size_t		m_maxEarlyData = 0;
  Host			m_server;
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
  using Base::reset_tls_;

friend Base;
template <typename> friend class Server;

  SrvLink(App *app) : Base(app, true) { }

private:
  // client connected variant - reset TLS
  void connected_() {
    reset_tls_();
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

template <typename App_> class Engine :
  public Random,
  public ZmEngine<App_> {
friend ZmEngine<App_>;
template <typename, typename, typename, typename, typename, typename>
friend class Link;
template <typename, typename, typename, typename> friend class CliLink;
template <typename, typename, typename, typename> friend class SrvLink;

public:
  using App = App_;
  using EngineCtl = ZmEngine<App>;

  using EngineCtl::start;
  using EngineCtl::stop;
  using EngineCtl::state;

  const App *app() const { return static_cast<const App *>(this); }
  App *app() { return static_cast<App *>(this); }

  enum { Transport = Zi::Transport::TLS };

  Engine() {
    memset(&m_ctx, 0, sizeof(m_ctx));
  }
  ~Engine() {
    if (m_ctx.certificates.list) {
      for (unsigned i = 0; i < m_ctx.certificates.count; ++i)
	::free(m_ctx.certificates.list[i].base);
      ::free(m_ctx.certificates.list);
    }
    Backend::verify_cert_free(m_verify);
    Backend::cert_store_free(m_cacert);
  }

  bool init(EngineParams params) {
    return init_(ZuMv(params), [](const EngineParams &) { return true; });
  }

protected:
  template <typename Params, typename L>
  bool init_(Params params, L l) {
    m_errorFn = ZuMv(params.errorFn());
    if (!m_errorFn) m_errorFn = defaultErrorFn();
    if (!validate_(params)) return false;
    m_mx = params.mx();
    m_rxThread = thread_(params.rxThread(), m_mx->rxThread());
    m_txThread = thread_(params.txThread(), m_mx->txThread());
    m_asyncThread = params.asyncThread() ?
      m_mx->sid(params.asyncThread()) : 0;

    return ZmBlock<bool>{}([
      this, params = ZuMv(params), l = ZuMv(l)
    ](auto wake) mutable {
      rxInvoke([
	this, params = ZuMv(params), l = ZuMv(l), wake = ZuMv(wake)
      ]() mutable {
	wake(init_context_(params, l));
      });
    });
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
    if (!params.mx()->running()) {
      error_(ZeEXCEPT(Error, "Ztls", "multiplexer not running"));
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
  bool init_context_(Params &params, L &l) {
    if (!Random::init()) {
      error_(ZeEXCEPT(Error, "Ztls", "backend init failed"));
      return false;
    }
    memset(&m_ctx, 0, sizeof(m_ctx));
    m_ctx.random_bytes = Backend::random_bytes_cb();
    m_ctx.get_time = &ptls_get_time;
    m_ctx.key_exchanges = Backend::key_exchanges();
    init_cipher_suites_();
    m_ctx.cipher_suites = m_cipherSuites;
    m_ctx.server_cipher_preference = 1;
    if (!init_alpn_(params.alpn())) return false;
    if (asyncConfigured_())
      if (!startAsyncLoop_()) return false;
    if (!l(params)) {
      stopAsyncLoop_();
      return false;
    }
    return true;
  }

public:
  void final() {
    bool ok = EngineCtl::lock(ZmEngineState::Stopped, [this]() {
      final_();
      return true;
    });
    ZiAssert(ok, "Ztls", (),
      "TLS engine finalization while not stopped", return);
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

private:
  bool asyncConfigured_() const { return m_asyncThread != 0; }

protected:
  void error_(ZeException e) {
    if (m_errorFn)
      m_errorFn(ZuMv(e));
    else
      ZiLogEvent(ZuMv(e));
  }

private:
  bool startAsyncLoop_() {
    if (!asyncConfigured_() || m_eventLoopStarted) return true;
    m_eventLoop.init(m_mx, m_asyncThread,
      ZiEvent::FailFn{[this](ZeException e) { error_(ZuMv(e)); }});
    m_eventLoopInit = true;
    bool ok = ZmBlock<bool>{}([this](auto wake) mutable {
      m_eventLoop.start(ZiEvent::StartFn{
	[this, wake = ZuMv(wake)](ZiEvent::StartResult result) mutable {
	  if (result.is<ZiEvent::Exception>()) {
	    error_(ZuMv(result).p<ZiEvent::Exception>());
	    wake(false);
	    return;
	  }
	  wake(true);
	}});
    });
    m_eventLoopStarted = ok;
    if (!ok) {
      m_eventLoop.final();
      m_eventLoopInit = false;
    }
    return ok;
  }

  void stopAsyncLoop_() {
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

  void final_() { // direct call from within rx thread
    stopAsyncLoop_();
    m_errorFn = ErrorFn{};
  }

  template <typename SendFn, typename RecvFn>
  bool asyncAddHandle_(
      Zi::Handle handle, SendFn send, RecvFn recv) {
    if (!m_eventLoopStarted) return false;
    return ZmBlock<bool>{}([
      this, handle, send = ZuMv(send), recv = ZuMv(recv)
    ](auto wake) mutable {
      m_eventLoop.run([
	this, handle, send = ZuMv(send), recv = ZuMv(recv),
	wake = ZuMv(wake)
      ]() mutable {
	bool ok = m_eventLoop.addHandle(handle, ZuMv(send), ZuMv(recv));
	wake(ok);
      });
    });
  }

  void asyncRun_(ZmFn<> fn) {
    if (!m_eventLoopStarted) return;
    m_eventLoop.run(ZuMv(fn));
  }

  void asyncDelHandleNow_(Zi::Handle handle) {
    m_eventLoop.delHandle(handle);
  }

  void asyncDelHandle_(Zi::Handle handle) {
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
    this->started(true);
  }

  void stop_() {
    rxRun([this]() {
      txRun([this]() { this->stopped(true); });
    });
  }

  template <typename L>
  bool spawn(L l) {
    if (!m_mx || !m_mx->running()) return false;
    rxRun(ZuMv(l));
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
    return loadCA(path ? path.data() : nullptr);
  }
  bool loadCA(const char *path) {
    if (!m_cacert) m_cacert = Backend::cert_store_new();
    if (!m_cacert) {
      error_(ZeEXCEPT(Error, "Ztls", "cert_store_new() failed"));
      return false;
    }
    if (!path) {
#ifndef _WIN32
      if (ZiFile::exists("/etc/lsb-release")) // Arch/Ubuntu/Debian/SLES
	path = "/etc/ssl/certs";
      else if (ZiFile::exists("/etc/redhat-release")) // Fedora/CentOS/RHEL
	path = "/etc/pki/tls/certs";
      else if (ZiFile::isdir("/system/etc/security/cacerts")) // Android
	path = "/system/etc/security/cacerts";
      else if (ZiFile::isdir("/usr/local/share/certs")) // FreeBSD
	path = "/usr/local/share/certs";
      else if (ZiFile::isdir("/etc/openssl/certs")) // NetBSD
	path = "/etc/openssl/certs";
      else if (ZiFile::isdir("/var/ssl/certs")) // AIX
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
	if (!Backend::cert_store_add_der(
	      m_cacert, context->pbCertEncoded, context->cbCertEncoded)) {
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
      if (ZiFile::isdir(path)) {
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
  bool init_alpn_(const ParamStrings &alpn) {
    m_alpn.length(0);
    m_alpnData.length(0);
    if (!alpn.length()) return true;
    unsigned bytes = 0;
    for (auto &s : alpn) bytes += s.length();
    if (bytes && !m_alpnData.ensure(bytes)) return false;
    m_alpn.ensure(alpn.length());
    for (auto &s : alpn) {
      unsigned offset = m_alpnData.length();
      m_alpnData << ZuBSpan{s};
      m_alpn.push(ptls_iovec_t{
	m_alpnData.data() + offset,
	s.length()});
    }
    return true;
  }

private:
  void init_cipher_suites_() {
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

  // immutable after init()
  ZiMultiplex			*m_mx = nullptr;
  unsigned			m_rxThread = 0;
  unsigned			m_txThread = 0;
  unsigned			m_asyncThread = 0;

  // Rx thread exclusive after init()
  ZiEventLoop			m_eventLoop;
  bool				m_eventLoopInit = false;
  bool				m_eventLoopStarted = false;
  ErrorFn			m_errorFn;

  ptls_context_t		m_ctx{};
  ptls_cipher_suite_t		*m_cipherSuites[16]{};
  ALPNData			m_alpnData;
  ALPN				m_alpn;
  Backend::CertStore		*m_cacert = nullptr;
  Backend::VerifyCert		*m_verify = nullptr;
};

// CRTP - implementation must conform to the following interface:
#if 0
struct App : public Client<App> {
  using RxBufAlloc = Ztls::RxBufAlloc<BufSize>;
  using TxBufAlloc = Ztls::TxBufAlloc<BufSize>;

  void exception(ZmRef<ZeEvent>); // optional

  struct Link : public CliLink<App, Link, RxBufAlloc, TxBufAlloc> {
    // Ztls Rx thread - handshake completed
    void connected(Zi::Connected);

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
template <typename App> class Client : public Engine<App> {
public:
  using Base = Engine<App>;
friend Base;

  using Base::error_;
  using Base::loadCA;

  const App *app() const { return static_cast<const App *>(this); }
  App *app() { return static_cast<App *>(this); }

  Client() { }
  ~Client() {
    Backend::sign_cert_free(m_sign);
    Backend::pkey_free(m_key);
  }

  // specify certPath and keyPath for mTLS
  bool init(ClientParams params);

  void final() { Base::final(); }

protected:
  unsigned reconnFreq() const { return 0; } // default

private:
  // Rx thread exclusive
  Backend::PKey			*m_key = nullptr;
  Backend::SignCert		*m_sign = nullptr;
};

template <typename App>
bool Client<App>::init(ClientParams params)
{
  using Link = typename App::Link;

  if (bool(params.certPath()) != bool(params.keyPath())) {
    auto errorFn = params.errorFn() ? params.errorFn() : defaultErrorFn();
    errorFn(ZeEXCEPT(Error, "Ztls",
      "client certPath and keyPath must be configured together"));
    return false;
  }

  return Base::init_(ZuMv(params), [this](const ClientParams &params) -> bool {
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
    if (!loadCA(ZuCSpan{params.caPath()})) return false;

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
    }
    return true;
  });
}

// CRTP - implementation must conform to the following interface:
#if 0
struct App : public Server<App> {
  using RxBufAlloc = Ztls::RxBufAlloc<BufSize>;
  using TxBufAlloc = Ztls::TxBufAlloc<BufSize>;

  void exception(ZmRef<ZeEvent>); // optional

  struct Link : public SrvLink<App, Link, RxBufAlloc, TxBufAlloc> {
    // Ztls Rx thread - handshake completed
    void connected(Zi::Connected);

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
class Server : public Engine<App_> {
public:
  using App = App_;
  using Base = Engine<App>;
friend Base;

  using Base::loadCA;
  using Base::mx;
  using Base::error_;

  const App *app() const { return static_cast<const App *>(this); }
  App *app() { return static_cast<App *>(this); }

  Server() { }
  ~Server() {
    Backend::ticket_key_free(m_ticketKey);
    Backend::sign_cert_free(m_sign);
    Backend::pkey_free(m_key);
  }

  bool init(ServerParams params);

  void final() { Base::final(); }

  void listen() {
    mx()->listen(
      ZiListenFn{app(),
	[](App *app, const ZiListenInfo &info) { app->listening(info); }},
      ZiFailFn{app(),
	[](App *app, bool transient) { app->listenFailed(transient); }},
      ZiConnectFn{app(),
	[](App *app, const ZiCxnInfo &ci) -> ZiConnection * {
	  return app->accepted(ci);
	}},
      app()->localIP(), app()->localPort(), app()->nAccepts(), ZiCxnOptions());
  }

  void stopListening() {
    mx()->del(&m_rebindTimer);
    if (m_listening)
      mx()->stopListening(app()->localIP(), app()->localPort());
    m_listening = false;
  }

protected:
  unsigned nAccepts() const { return 8; } // default
  unsigned rebindFreq() const { return 0; } // default

  void listening(const ZiListenInfo &info) { // default
    m_listening = true;
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

private:
  // Rx thread exclusive
  Backend::PKey			*m_key = nullptr;
  Backend::SignCert		*m_sign = nullptr;
  Backend::TicketKey		*m_ticketKey = nullptr;
  ZmScheduler::Timer		m_rebindTimer;
  bool				m_listening = false;
};

template <typename App>
bool Server<App>::init(ServerParams params)
{
  using Link = typename App::Link;

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
