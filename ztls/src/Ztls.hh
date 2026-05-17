//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// picotls/OpenSSL wrapper - main TLS component

#ifndef Ztls_HH
#define Ztls_HH

#include <zlib/ZtlsLib.hh>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuSpan.hh>

#include <zlib/ZmList.hh>
#include <zlib/ZmHeap.hh>
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
  EngineParams(ZiMultiplex *mx_, ZuCSpan thread_, ZuSpan<ZuCSpan> alpn_) :
      mx{mx_}, thread{thread_}, errorFn_{defaultErrorFn()} {
    alpn.ensure(alpn_.length());
    for (auto &s : alpn_) alpn.push(ParamString{s});
  }

  EngineParams &&caPath(ZuCSpan v) { caPath_ = v; return ZuMv(*this); }
  EngineParams &&certPath(ZuCSpan v) { certPath_ = v; return ZuMv(*this); }
  EngineParams &&keyPath(ZuCSpan v) { keyPath_ = v; return ZuMv(*this); }
  EngineParams &&asyncThread(ZuCSpan v) {
    asyncThread_ = v;
    return ZuMv(*this);
  }
  EngineParams &&errorFn(ErrorFn v) { errorFn_ = ZuMv(v); return ZuMv(*this); }

  ZiMultiplex *mx = nullptr;
  ParamString thread;
  ParamStrings alpn;
  ParamString caPath_;
  ParamString certPath_;
  ParamString keyPath_;
  ParamString asyncThread_;
  ErrorFn errorFn_;
};

struct ClientParams : public EngineParams {
  using EngineParams::EngineParams;

  ClientParams &&caPath(ZuCSpan v)
    { EngineParams::caPath(v); return ZuMv(*this); }
  ClientParams &&certPath(ZuCSpan v)
    { EngineParams::certPath(v); return ZuMv(*this); }
  ClientParams &&keyPath(ZuCSpan v)
    { EngineParams::keyPath(v); return ZuMv(*this); }
  ClientParams &&asyncThread(ZuCSpan v)
    { EngineParams::asyncThread(v); return ZuMv(*this); }
  ClientParams &&errorFn(ErrorFn v)
    { EngineParams::errorFn(ZuMv(v)); return ZuMv(*this); }
};

struct ServerParams : public EngineParams {
  using EngineParams::EngineParams;

  ServerParams &&caPath(ZuCSpan v)
    { EngineParams::caPath(v); return ZuMv(*this); }
  ServerParams &&certPath(ZuCSpan v)
    { EngineParams::certPath(v); return ZuMv(*this); }
  ServerParams &&keyPath(ZuCSpan v)
    { EngineParams::keyPath(v); return ZuMv(*this); }
  ServerParams &&asyncThread(ZuCSpan v)
    { EngineParams::asyncThread(v); return ZuMv(*this); }
  ServerParams &&errorFn(ErrorFn v)
    { EngineParams::errorFn(ZuMv(v)); return ZuMv(*this); }
  ServerParams &&mTLS(bool v) { mTLS_ = v; return ZuMv(*this); }
  ServerParams &&cacheTimeout(int v) { cacheTimeout_ = v; return ZuMv(*this); }

  bool mTLS_ = false;
  int cacheTimeout_ = -1;
};

// picotls runs within a single dedicated thread, without lock contention

// API functions: listen, connect, disconnect/disconnect_, txStream/txStream_ (Tx)
// API callbacks: accepted, connected, disconnected, process (Rx)

// Function Category | I/O Threads |        TLS thread          | App threads
// ------------------|-------------|----------------------------|------------
// Server            | accepted()  | connected() disconnected() | listen()
// Client            |             | connect_() connectFailed() | connect()
// Disconnect        |             | disconnect_()              | disconnect()
// Transmission (Tx) |             | txStream_()                | txStream()
// Reception    (Rx) |             | process()                  |

// ZiIOBuf buffers transport data between threads (--> arrows below)

// I/O Threads |                    TLS thread                   | App threads
// ------------|-------------------------------------------------|------------
//     I/O Rx --> Rx input  -> Decryption -> Rx output -> App Rx |
// ------------|                                                 |
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
  void disconnected() {
    if (Link *link = m_link) link->disconnected_0(this, ZuMv(m_link));
  }

private:
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
  ZuString HeapID = ZiIOBuf_HeapID{}()>
using BufAlloc = Ztls_::BufAlloc<Size, MaxSize, HeapID>;

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

friend Cxn;

private:
  struct AsyncReg;
  using AsyncRegRef = ZmRef<AsyncReg>;

public:
  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  Link(App *app, bool isServer) : m_app(app), m_isServer(isServer) {
  }
  ~Link() {
    if (m_tls) {
      if (ZuUnlikely(asyncPending_())) {
	ZiLOG(Error, "Ztls", "TLS link destroyed with async job pending");
      } else
	ptls_free(m_tls);
    }
    for (unsigned i = 0, n = m_asyncRetired.length(); i < n; i++)
      ZiLOG(Error, "Ztls", "TLS link destroyed with retired async job pending");
  }

  App *app() const { return m_app; }
  Cxn *cxn() const { return m_cxn; }

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
  // this is the internal TCP-level connected_(); once handshake is completed,
  // the application's connected() will be called
  void connected_0(Cxn *cxn, ZiIOContext &io) { // runs on I/O Rx thread
    app()->run([impl = ZmMkRef(this->impl()), cxn = ZmMkRef(cxn)]() {
      impl->connected_1(ZuMv(cxn));
    });
    Rx::template recv<parseHdr<RxBufAlloc>, &Impl::recvRecord>(io);
  }

  void connected_1(ZmRef<Cxn> cxn) { // runs on TLS thread
    // idempotent
    if (ZuUnlikely(m_cxn == cxn)) return;
    // handle overlapping connections
    if (ZuUnlikely(m_cxn)) { auto cxn_ = ZuMv(m_cxn); cxn_->close(); }
    m_cxn = ZuMv(cxn);
    impl()->connected_();
  }

  int recvRecord(const ZiIOContext &io, ZmRef<ZiIOBuf> buf) {
    auto n = int(buf->length);
    auto cxn = static_cast<Cxn *>(io.cxn);
    app()->run([
      impl = ZmMkRef(this->impl()),
      cxn = ZmMkRef(cxn),
      buf = ZuMv(buf)
    ]() mutable {
      if (ZuUnlikely(impl->m_cxn != cxn.ptr())) return;
      impl->record_(ZuMv(buf));
    });
    return n;
  }

  void record_(ZmRef<ZiIOBuf> buf) { // runs on TLS thread
    if (ZuUnlikely(!m_tls)) return;
    if (ptls_handshake_is_complete(m_tls))
      rcvd_(ZuMv(buf));
    else
      handshake_(ZuMv(buf));
  }

  void handshake_(ZmRef<ZiIOBuf> buf) {
    if (ZuUnlikely(asyncPending_())) {
      app()->error_(ZeEXCEPT(Error, "Ztls",
	"TLS handshake input received while async operation is pending"));
      disconnect_(false);
      return;
    }
    size_t inlen = buf->length;
    int n = handshake__(buf->data(), &inlen);
    handleHandshakeResult_(n);
  }

  static bool isCloseNotify_(int n) {
    if (PTLS_ERROR_GET_CLASS(n) == PTLS_ERROR_CLASS_PEER_ALERT &&
	PTLS_ERROR_TO_ALERT(n) == PTLS_ALERT_CLOSE_NOTIFY) return true;
    return false;
  }

protected:
  bool handleHandshakeResult_(int n) {
    if (!n) return finishHandshake_();
    if (n == PTLS_ERROR_IN_PROGRESS) return true;
    if (n == PTLS_ERROR_ASYNC_OPERATION) return asyncHandshake_();
    if (isCloseNotify_(n)) {
      disconnect_(true);
      return false;
    }
    ZiLOG(Error, "Ztls", ([n](auto &s) {
      s << "ptls_handshake(): " << strerror_(n);
    }));
    disconnect_(false);
    return false;
  }

private:
  bool finishHandshake_() {
    if (m_handshook) return true;
    asyncCleanup_();
    m_tlsver = ptls_get_protocol_version(m_tls);
    m_cipher = ptls_get_cipher(m_tls);
    if (ZuUnlikely(!m_cipher)) {
      ZiLOG(Error, "Ztls", "ptls_get_cipher() failed");
      disconnect_(false);
      return false;
    }
    if (ZuUnlikely(!m_cipher->aead)) {
      ZiLOG(Error, "Ztls", "ptls_get_cipher()->aead is null");
      disconnect_(false);
      return false;
    }
    m_rec_hdr_len = 5;
    m_rec_tag_len = m_cipher->aead->tag_size;
    m_rec_iv_len =
      (m_tlsver == PTLS_PROTOCOL_VERSION_TLS12) ?
	m_cipher->aead->tls12.record_iv_size : 0;
    m_rec_overhead = ptls_get_record_overhead(m_tls);
    m_headroom = m_rec_hdr_len + m_rec_iv_len;
    if (ZuUnlikely(
	m_headroom > TxMaxOverhead ||
	m_rec_overhead > TxMaxOverhead)) {
      ZiLOG(Error, "Ztls", "TLS record overhead exceeds worst-case limit");
      disconnect_(false);
      return false;
    }
    m_tx_seq_est = 0;
    m_tx_need_key_update = false;
    m_handshook = true;
    impl()->connected(
      ptls_get_negotiated_protocol(m_tls),
      tlsver_(ptls_get_protocol_version(m_tls)));
    return true;
  }

  // txBuf() is used for handshake, re-keying, alerts
  // - it is NOT used for application data
  ZmRef<ZiIOBuf> txBuf(ptls_buffer_t &pbuf) {
    ZmRef<ZiIOBuf> buf = new TxBufAlloc{impl()};
    if (ZuUnlikely(buf->size < TxRecordCapacity))
      if (ZuUnlikely(!buf->ensure(TxRecordCapacity))) return nullptr;
    auto base = buf->data(); // - buf->skip; // skip will be 0
    ptls_buffer_init_tx(&pbuf, base, TxRecordCapacity);
    pbuf.origin = buf.ptr();
    return buf;
  }

  void flushTxBuf_(ptls_buffer_t &pbuf, ZmRef<ZiIOBuf> buf) {
    auto base = buf->data();
    bool origin_match = pbuf.origin == buf.ptr();
    if (!pbuf.off) {
      if (pbuf.base != base) ptls_buffer_dispose(&pbuf);
      return;
    }
    if (ZuUnlikely(!origin_match || pbuf.base != base)) {
      ZiLOG(Error, "Ztls", "TLS TX buffer origin mismatch");
      disconnect_(false);
      if (pbuf.base != base) ptls_buffer_dispose(&pbuf);
      return;
    }
    buf->skip = 0;
    buf->length = pbuf.off;
    Tx::send(ZuMv(buf));
  }

protected:
  int handshake__(const uint8_t *input, size_t *inlen) { // TLS thread
    ptls_buffer_t pbuf;
    auto buf = txBuf(pbuf);
    if (ZuUnlikely(!buf)) return PTLS_ERROR_NO_MEMORY;
    int n = ptls_handshake(m_tls, &pbuf, input, inlen, &m_props);
    flushTxBuf_(pbuf, ZuMv(buf));
    return n;
  }

private:
  void rcvd_(ZmRef<ZiIOBuf> buf) {
    ptls_buffer_t pbuf;
    auto base = rxBuf(pbuf, buf);
    if (ZuUnlikely(!base)) return;
    size_t inlen = buf->length;
    int n = ptls_receive(m_tls, &pbuf, base, &inlen); // in-place overwrite
    if (ZuUnlikely(inlen != buf->length)) {
      ZiLOG(Error, "Ztls", "ptls_receive() partial record");
      disconnect_(false);
      return;
    }
    auto plain = buf->data() + m_headroom;
    bool origin_match = pbuf.origin == buf.ptr();
    if (!n) {
      if (ZuUnlikely(!origin_match || pbuf.base != plain)) {
	ZiLOG(Error, "Ztls", "TLS RX buffer origin mismatch");
	disconnect_(false);
	if (pbuf.base != plain) ptls_buffer_dispose(&pbuf);
	return;
      }
      if (pbuf.off) {
	buf->skip = m_headroom;
	buf->length = pbuf.off;
	m_rxStream.push(ZuMv(buf));
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
    if (pbuf.base != plain) ptls_buffer_dispose(&pbuf);
    if (PTLS_ERROR_GET_CLASS(n) == PTLS_ERROR_CLASS_PEER_ALERT &&
	PTLS_ERROR_TO_ALERT(n) == PTLS_ALERT_CLOSE_NOTIFY) {
      disconnect_(true);
      return;
    }
    ZiLOG(Error, "Ztls", ([n](auto &s) {
      s << "ptls_receive(): " << strerror_(n);
    }));
    disconnect_(false);
    return;
  }

  uint8_t *rxBuf(ptls_buffer_t &pbuf, ZiIOBuf *buf) {
    auto base = buf->data(); //  - buf->skip; // skip will be 0
    unsigned align_bits = 0;
    if (m_cipher) align_bits = m_cipher->aead->align_bits;
    if (align_bits) {
      uintptr_t mask = (uintptr_t(1) << align_bits) - 1;
      if (ZuUnlikely(reinterpret_cast<uintptr_t>(base) & mask)) {
	ZiLOG(Error, "Ztls", "TLS Rx buffer misaligned");
	disconnect_(false);
	return nullptr;
      }
    }
    ptls_buffer_init_rx(&pbuf, base + m_headroom, buf->size - m_rec_overhead);
    pbuf.origin = buf;
    pbuf.align_bits = align_bits;
    return base;
  }

  template <typename ImplRef_>
  void disconnected_0(Cxn *cxn, ImplRef_ impl_) {
    ZmRef<Impl> impl{ZuMv(impl_)};
    app()->run([impl = ZuMv(impl), cxn = ZmMkRef(cxn)]() {
      impl->disconnected_(cxn);
      auto mx = cxn->mx();
      // drain Tx while keeping cxn referenced
      mx->txRun([cxn = ZuMv(cxn)]() { });
    });
  }
  void disconnected_(Cxn *cxn) { // TLS thread
    if (m_cxn == cxn) m_cxn = nullptr;
    reset_tls_();
    impl()->disconnected();
  }

private:
  ZmRef<ZiIOBuf> allocTxBuf_() { // App/TLS threads
    ZmRef<ZiIOBuf> buf = new TxBufAlloc{impl()};
    if (ZuUnlikely(buf->size < TxRecordCapacity))
      if (ZuUnlikely(!buf->ensure(TxRecordCapacity))) return nullptr;
    buf->skip = m_headroom;
    buf->length = 0;
    return buf;
  }

public:
  auto txStream() { // App thread(s)
    return Zi::txStream(
      unsigned(TxRecordCapacity),
      unsigned(m_headroom),
      unsigned(TxMaxOverhead - m_headroom),
      [this](unsigned skip) -> ZmRef<ZiIOBuf> {
	auto buf = allocTxBuf_();
	if (ZuUnlikely(!buf || buf->skip != skip))
	  throw TxStreamAllocFailure{};
	return buf;
      },
      [](ZmRef<ZiIOBuf> buf) {
	auto link = static_cast<Impl *>(buf->owner);
	link->sendBuf(ZuMv(buf));
      });
  }
  auto txStream_() { // TLS thread
    return Zi::txStream(
      unsigned(TxRecordCapacity),
      unsigned(m_headroom),
      unsigned(TxMaxOverhead - m_headroom),
      [this](unsigned skip) -> ZmRef<ZiIOBuf> {
	auto buf = allocTxBuf_();
	if (ZuUnlikely(!buf || buf->skip != skip))
	  throw TxStreamAllocFailure{};
	return buf;
      },
      [](ZmRef<ZiIOBuf> buf) {
	auto link = static_cast<Impl *>(buf->owner);
	link->sendBuf_(ZuMv(buf));
      });
  }

private:
  struct TxStreamAllocFailure { };

  void sendBuf(ZmRef<ZiIOBuf> buf) {
    if (ZuUnlikely(!buf || !buf->length)) return;
    if (ZuUnlikely(m_disconnecting.load_())) return;
    app()->invoke([buf = ZuMv(buf)]() mutable {
      auto link = static_cast<Impl *>(buf->owner);
      link->sendBuf_(ZuMv(buf));
    });
  }

  void sendBuf_(ZmRef<ZiIOBuf> buf) { // TLS thread
    if (ZuUnlikely(!buf || !buf->length)) return;
    if (ZuUnlikely(m_disconnecting.load_())) return;
    if (ZuUnlikely(!m_tls || !m_cipher)) return; // FIXME - log diagnostic

    if (ZuUnlikely(buf->length > TxMaxPlaintext)) {
      ZiLOG(Error, "Ztls", "TLS TX plaintext exceeds record limit");
      disconnect_(false);
      return;
    }

    if (ZuUnlikely(buf->skip != m_headroom)) {
      ZiLOG(Error, "Ztls", "TLS TX buffer missing headroom");
      disconnect_(false);
      return;
    }

    if (ZuUnlikely(buf->size < TxRecordCapacity))
      if (ZuUnlikely(!buf->ensure(TxRecordCapacity))) {
	ZiLOG(Error, "Ztls", "TLS TX buffer growth failed");
	disconnect_(false);
	return;
      }

    ptls_buffer_t pbuf;
    constexpr uint64_t Threshold = (1ULL<<24);
    if (ZuUnlikely(m_tx_need_key_update || m_tx_seq_est >= Threshold - 1)) {
      m_tx_need_key_update = false;
      int n = ptls_update_key(m_tls, 0);
      if (n) {
	ZiLOG(Error, "Ztls", ([n](auto &s) {
	  s << "ptls_update_key(): " << strerror_(n);
	}));
	disconnect_(false);
	return;
      }
      ZmRef<ZiIOBuf> kbuf = txBuf(pbuf);
      if (ZuUnlikely(!kbuf)) {
	ZiLOG(Error, "Ztls", "TLS TX buffer growth failed");
	disconnect_(false);
	return;
      }
      n = ptls_send(m_tls, &pbuf, nullptr, 0);
      if (n) {
	ZiLOG(Error, "Ztls", ([n](auto &s) {
	  s << "ptls_send(): " << strerror_(n);
	}));
	disconnect_(false);
	return;
      }
      flushTxBuf_(pbuf, ZuMv(kbuf));
      m_tx_seq_est = 0;
    }

    auto data = buf->data();
    auto length = buf->length;
    auto base = data - buf->skip;
    auto align_bits = m_cipher->aead->align_bits;
    if (align_bits) {
      uintptr_t mask = (uintptr_t(1) << align_bits) - 1;
      if (ZuUnlikely(reinterpret_cast<uintptr_t>(base) & mask)) {
	ZiLOG(Error, "Ztls", "TLS Tx buffer misaligned");
	disconnect_(false);
	return;
      }
    }
    ptls_buffer_init_tx(&pbuf, base, TxRecordCapacity);
    pbuf.origin = buf.ptr();
    pbuf.align_bits = align_bits;
    int n = ptls_send(m_tls, &pbuf, data, length); // in-place overwrite
    if (pbuf.off) ++m_tx_seq_est;
    if (n) {
      ZiLOG(Error, "Ztls", ([n](auto &s) {
	s << "ptls_send(): " << strerror_(n);
      }));
      disconnect_(false);
      if (pbuf.base != base) ptls_buffer_dispose(&pbuf);
      return;
    }
    flushTxBuf_(pbuf, ZuMv(buf));
  }

public:
  int send_alert_(uint8_t level, uint8_t desc) { // TLS thread
    ptls_buffer_t pbuf;
    auto buf = txBuf(pbuf);
    if (ZuUnlikely(!buf)) return PTLS_ERROR_NO_MEMORY;
    int n = ptls_send_alert(m_tls, &pbuf, level, desc);
    flushTxBuf_(pbuf, ZuMv(buf));
    return n;
  }

private:
  bool asyncPending_() const {
    return m_asyncJob && m_asyncTLS == m_tls;
  }

  void clearAsync_() {
    m_asyncJob = nullptr;
    m_asyncHandle = Zi::nullHandle();
    m_asyncTLS = nullptr;
    m_asyncGen = 0;
    m_asyncReg = nullptr;
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
	"callback-only picotls async jobs are unsupported"));
      disconnect_(false);
      return false;
    }
    int fd = job->get_fd(job);
    Zi::Handle handle = static_cast<Zi::Handle>(fd);
    if (ZuUnlikely(Zi::nullHandle(handle))) {
      app()->error_(ZeEXCEPT(Error, "Ztls",
	"picotls async job returned an invalid fd"));
      disconnect_(false);
      return false;
    }
#ifndef _WIN32
    if (ZuUnlikely(fcntl(fd, F_GETFD) < 0)) {
      app()->error_(ZeEXCEPT(Error, "Ztls", ([e = ZeError{errno}](auto &s) {
	s << "picotls async job fd is invalid: " << e;
      })));
      disconnect_(false);
      return false;
    }
#endif

    asyncCleanup_();

    auto gen = m_tlsGen;
    auto tls_ = tls();
    AsyncRegRef reg = new AsyncReg{};
    m_asyncJob = job;
    m_asyncHandle = handle;
    m_asyncTLS = tls_;
    m_asyncGen = gen;
    m_asyncReg = reg;

    auto link = ZmMkRef(impl());
    if (ZuUnlikely(!app()->asyncAddHandle_(
	  handle,
	  ZiEvent::HandleSendFn{[](Zi::Handle) { }},
	  ZiEvent::HandleRecvFn{
	    [link = ZuMv(link), tls_, job, handle, gen, reg](Zi::Handle) mutable {
	      link->asyncReady_(tls_, job, handle, gen, reg);
	    }},
	  [reg]() mutable { reg->armed = 1; }))) {
      clearAsync_();
      app()->error_(ZeEXCEPT(Error, "Ztls",
	"ZiEventLoop::addHandle() failed for picotls async job"));
      disconnect_(false);
      return false;
    }

    return true;
  }

  void asyncReady_(
      ptls_t *tls_, ptls_async_job_t *job_,
      Zi::Handle handle_, uint64_t gen_, AsyncRegRef reg_) {
    if (!reg_->armed.xch(0)) return;
    app()->asyncRun_([
      link = ZmMkRef(impl()), tls_, job_, handle_, gen_, reg_
    ]() mutable {
      link->app()->asyncDelHandleNow_(handle_);
      link->app()->run([
	link = ZuMv(link), tls_, job_, handle_, gen_, reg_ = ZuMv(reg_)
      ]() mutable {
	link->asyncResume_(tls_, job_, handle_, gen_, ZuMv(reg_));
      });
    });
  }

  void asyncResume_(
      ptls_t *tls_, ptls_async_job_t *job_,
      Zi::Handle handle_, uint64_t gen_, AsyncRegRef reg_) {
    if (m_asyncTLS == tls_ &&
	m_asyncJob == job_ &&
	m_asyncHandle == handle_ &&
	m_asyncGen == gen_ &&
	m_asyncReg.ptr() == reg_.ptr() &&
	m_tls == tls_) {
      clearAsync_();
      int n = handshake__(nullptr, nullptr);
      handleHandshakeResult_(n);
      return;
    }
    if (asyncRetiredReady_(tls_, job_, handle_, gen_, ZuMv(reg_))) return;
  }

  void asyncCleanup_() {
    Zi::Handle handle = m_asyncHandle;
    if (m_asyncReg) m_asyncReg->armed = 0;
    clearAsync_();
    if (!Zi::nullHandle(handle)) app()->asyncDelHandle_(handle);
  }

  bool asyncRetireTLS_() {
    if (!asyncPending_()) return false;
    m_asyncRetired.push(AsyncRetired{
      m_tls, m_asyncJob, m_asyncHandle, m_asyncGen, m_asyncReg});
    clearAsync_();
    return true;
  }

  bool asyncRetiredReady_(
      ptls_t *tls_, ptls_async_job_t *job_,
      Zi::Handle handle_, uint64_t gen_, AsyncRegRef reg_) {
    for (unsigned i = 0, n = m_asyncRetired.length(); i < n; i++) {
      auto &retired = m_asyncRetired[i];
      if (retired.tls != tls_ ||
	  retired.job != job_ ||
	  retired.handle != handle_ ||
	  retired.gen != gen_ ||
	  retired.reg.ptr() != reg_.ptr())
	continue;
      ptls_free(retired.tls);
      m_asyncRetired.splice(i, 1);
      return true;
    }
    return false;
  }

public:
  void disconnect() { // App thread(s)
    m_disconnecting = 1;
    app()->invoke([this]() { disconnect_(); });
  }
  void disconnect_(bool notify = true) { // TLS thread
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
    ++m_tlsGen;
    if (m_tls) {
      if (!asyncRetireTLS_())
	ptls_free(m_tls);
    }
    m_tls = ptls_new(app()->ctx(), m_isServer);
    if (!m_tls) {
      ZiLOG(Error, "Ztls", "ptls_new() failed");
      return;
    }
    *ptls_get_data_ptr(m_tls) = impl();
    m_tlsver = 0;
    m_cipher = nullptr;
    m_rec_hdr_len = 5;
    m_rec_iv_len = 0;
    m_rec_tag_len = 0;
    m_rec_overhead = 0;
    m_headroom = 0;
    m_tx_seq_est = 0;
    m_tx_need_key_update = false;
    m_handshook = false;
    m_disconnecting = 0;
    reset_handshake_props_();

    m_rxStream.clean();
  }

private:
  App			*m_app = nullptr;
  bool			m_isServer = false;
  ZmScheduler::Timer	m_reconnTimer;

  // TLS thread
  struct AsyncReg : public ZuObject {
    ZmAtomic<unsigned>	armed = 0;
  };

  struct AsyncRetired {
    ptls_t		*tls = nullptr;
    ptls_async_job_t	*job = nullptr;
    Zi::Handle		handle = Zi::nullHandle();
    uint64_t		gen = 0;
    AsyncRegRef		reg;
  };
  using AsyncRetireds =
    ZtArray<AsyncRetired, ZtArrayHeapID<"Ztls.AsyncRetired">>;

  ptls_t		*m_tls = nullptr;
  uint64_t		m_tlsGen = 0;
  uint16_t		m_tlsver = 0;
  ptls_cipher_suite_t	*m_cipher = nullptr;
  unsigned		m_rec_hdr_len = 5;
  unsigned		m_rec_iv_len = 0;
  unsigned		m_rec_tag_len = 0;
  unsigned		m_rec_overhead = 0;
  unsigned		m_headroom = 0;
  uint64_t		m_tx_seq_est = 0;
  bool			m_tx_need_key_update = false;
  bool			m_handshook = false;
  ptls_handshake_properties_t m_props{};
  ptls_async_job_t	*m_asyncJob = nullptr;
  Zi::Handle		m_asyncHandle = Zi::nullHandle();
  ptls_t		*m_asyncTLS = nullptr;
  uint64_t		m_asyncGen = 0;
  AsyncRegRef		m_asyncReg;
  AsyncRetireds		m_asyncRetired;
  CxnRef		m_cxn = nullptr;
  RxStream		m_rxStream;

  // Contended
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
  typename RxBufAlloc = BufAlloc<>,
  typename TxBufAlloc = RxBufAlloc>
class CliLink : public CliLink_<App, Impl, RxBufAlloc, TxBufAlloc, CliCxn<Impl>> {
public:
  using Cxn = CliCxn<Impl>;
  using Base = CliLink_<App, Impl, RxBufAlloc, TxBufAlloc, Cxn>;

  using Base::impl;
  using Base::app;
  using Base::handshake_props;
  using Base::reset_handshake_props_;
  using Base::reset_tls_;
  using Base::tls;

friend Base;
template <typename> friend class Client;

  CliLink(App *app) : Base{app, false} {
  }
  CliLink(App *app, Host server, uint16_t port) :
      Base{app, false}, m_server{ZuMv(server)}, m_port{port} { }
  ~CliLink() { }

  void connect() { // App thread(s)
    app()->invoke([this]() mutable { connect_(); });
  }
  void connect(Host server, uint16_t port) { // App thread(s)
    m_server = ZuMv(server);
    m_port = port;
    app()->invoke([this]() mutable { connect_(); });
  }

  const Host &server() const { return m_server; }
  uint16_t port() const { return m_port; }

  void connect_() { // TLS thread
    ZiIP ip = m_server;
    if (!ip) {
      ZiLOG(Error, "Ztls", ([server = LogMsg{m_server}](auto &s) {
	s << '"' << server << "\": hostname lookup failure";
      }));
      impl()->connectFailed(true);
      return;
    }
    reset_tls_();
    {
      int n = ptls_set_server_name(
	tls(), m_server.data(), m_server.length());
      if (n) {
	ZiLOG(Error, "Ztls", ([server = LogMsg{m_server}, n](auto &s) {
	  s << "ptls_set_server_name(\"" << server << "\"): " <<
	    strerror_(n);
	}));
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
    m_ticket.length(input.len);
    memcpy(m_ticket.data(), input.base, input.len);
  }

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
    int n = this->handshake__(nullptr, nullptr);
    this->handleHandshakeResult_(n);
  }

protected:
  size_t maxEarlyData() const { return m_maxEarlyData; }

public:
  void connectFailed(bool transient) {
    unsigned reconnFreq = app()->reconnFreq();
    if (transient && reconnFreq > 0)
      app()->run(
	  ZmFn<>{this, [](CliLink *link) { link->connect_(); }},
	  Zm::now(reconnFreq), ZmScheduler::Update, &m_reconnTimer);
    else
      ZiLOG(Error, "Ztls", "connect failed");
  }

private:
  ZmScheduler::Timer	m_reconnTimer;
  Ticket		m_ticket;
  size_t		m_maxEarlyData = 0;
  Host			m_server;
  uint16_t		m_port;
};

template <
  typename App, typename Impl,
  typename RxBufAlloc = BufAlloc<>,
  typename TxBufAlloc = RxBufAlloc>
class SrvLink : public SrvLink_<App, Impl, RxBufAlloc, TxBufAlloc, SrvCxn<Impl>> {
public:
  using Cxn = SrvCxn<Impl>;
  using Base = SrvLink_<App, Impl, RxBufAlloc, TxBufAlloc, Cxn>;

  using Base::impl;
  using Base::app;

friend Base;
template <typename> friend class Server;

  SrvLink(App *app) : Base(app, true) { }

private:
  void connected_() {
    this->reset_tls_();
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

template <typename App_> class Engine : public Random {
public:
  using App = App_;
template <typename, typename, typename, typename, typename, typename>
friend class Link;
template <typename, typename, typename, typename> friend class CliLink;
template <typename, typename, typename, typename> friend class SrvLink;

  const App *app() const { return static_cast<const App *>(this); }
  App *app() { return static_cast<App *>(this); }

  Engine() {
    memset(&m_ctx, 0, sizeof(m_ctx));
  }
  ~Engine() {
    if (m_ctx.certificates.list) ::free(m_ctx.certificates.list);
    Backend::verify_cert_free(m_verify);
    Backend::cert_store_free(m_cacert);
  }

  bool init(EngineParams params) {
    return init_(ZuMv(params), [](const EngineParams &) { return true; });
  }

protected:
  template <typename Params, typename L>
  bool init_(Params params, L l) {
    if (!validate_(params)) return false;
    m_mx = params.mx;
    m_thread = m_mx->sid(params.thread);
    m_asyncThread = 0;
    if (params.asyncThread_) m_asyncThread = m_mx->sid(params.asyncThread_);
    m_errorFn = ZuMv(params.errorFn_);
    if (!m_errorFn) m_errorFn = defaultErrorFn();

    return ZmBlock<bool>{}([
      this, params = ZuMv(params), l = ZuMv(l)
    ](auto wake) mutable {
      invoke([
	this, params = ZuMv(params), l = ZuMv(l), wake = ZuMv(wake)
      ]() mutable {
	wake(init_context_(params, l));
      });
    });
  }

private:
  template <typename Params>
  bool validate_(const Params &params) {
    if (ZuUnlikely(!params.mx)) {
      ZiLOG(Error, "Ztls", "multiplexer is null");
      return false;
    }
    unsigned thread = params.mx->sid(params.thread);
    if (!thread || thread > params.mx->params().nThreads()) {
      ZiLOG(Error, "Ztls", ([thread = LogMsg{params.thread}](auto &s) {
	s << "invalid thread ID \"" << thread << '"';
      }));
      return false;
    }
    if (!params.mx->running()) {
      ZiLOG(Error, "Ztls", "multiplexer not running");
      return false;
    }
    if (params.asyncThread_) {
#ifdef _WIN32
      ZiLOG(Error, "Ztls", ([](auto &s) {
	s << "asyncThread is unsupported on Windows because picotls exposes "
	  << "an int fd while HANDLE is pointer-sized";
      }));
      return false;
#else
      unsigned asyncThread = params.mx->sid(params.asyncThread_);
      if (!asyncThread || asyncThread > params.mx->params().nThreads()) {
	ZiLOG(Error, "Ztls", ([thread = LogMsg{params.asyncThread_}](auto &s) {
	  s << "invalid async thread ID \"" << thread << '"';
	}));
	return false;
      }
      if (asyncThread == thread) {
	ZiLOG(Error, "Ztls", "async thread must differ from TLS thread");
	return false;
      }
      if (asyncThread == params.mx->rxThread() ||
	  asyncThread == params.mx->txThread()) {
	ZiLOG(Error, "Ztls", "async thread must differ from I/O threads");
	return false;
      }
      if (!params.mx->params().thread(asyncThread).isolated()) {
	ZiLOG(Error, "Ztls", "async thread must be isolated");
	return false;
      }
#endif
    }
    return true;
  }

  template <typename Params, typename L>
  bool init_context_(Params &params, L &l) {
    if (!Random::init()) {
      ZiLOG(Error, "Ztls", "backend init failed");
      return false;
    }
    memset(&m_ctx, 0, sizeof(m_ctx));
    m_ctx.random_bytes = ptls_openssl_random_bytes;
    m_ctx.get_time = &ptls_get_time;
    m_ctx.key_exchanges = ptls_openssl_key_exchanges;
    init_cipher_suites_();
    m_ctx.cipher_suites = m_cipher_suites;
    m_ctx.server_cipher_preference = 1;
    if (!init_alpn_(params.alpn)) return false;
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
    if (!m_mx || !m_thread) return;
    if (m_mx->running()) {
      if (invoked())
	final_();
      else
	ZmBlock<>{}([this](auto wake) mutable {
	  invoke([this, wake = ZuMv(wake)]() mutable {
	    final_();
	    wake();
	  });
	});
    } else
      final_();
  }

  ZiMultiplex *mx() const { return m_mx; }

  template <typename ...Args>
  void run(Args &&...args) {
    m_mx->run(m_thread, ZuFwd<Args>(args)...);
  }
  template <typename ...Args>
  void invoke(Args &&...args) {
    m_mx->invoke(m_thread, ZuFwd<Args>(args)...);
  }
  bool invoked() { return m_mx->invoked(m_thread); }

private:
  bool asyncConfigured_() const { return m_asyncThread != 0; }

  void error_(ZeException e) {
    if (m_errorFn)
      m_errorFn(ZuMv(e));
    else
      ZiLogEvent(ZuMv(e));
  }

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

  void final_() {
    stopAsyncLoop_();
    m_errorFn = ErrorFn{};
  }

  template <typename SendFn, typename RecvFn, typename ArmedFn>
  bool asyncAddHandle_(
      Zi::Handle handle, SendFn send, RecvFn recv, ArmedFn armed) {
    if (!m_eventLoopStarted) return false;
    return ZmBlock<bool>{}([
      this, handle, send = ZuMv(send), recv = ZuMv(recv),
      armed = ZuMv(armed)
    ](auto wake) mutable {
      m_eventLoop.run([
	this, handle, send = ZuMv(send), recv = ZuMv(recv),
	armed = ZuMv(armed), wake = ZuMv(wake)
      ]() mutable {
	bool ok = m_eventLoop.addHandle(handle, ZuMv(send), ZuMv(recv));
	if (ok) armed();
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
  // TLS thread
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
      ZiLOG(Error, "Ztls", "cert_store_new() failed");
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
	ZiLOG(Error, "Ztls", ([e = ZeLastError](auto &s) {
	  s << "CertOpenSystemStore(nullptr, \"ROOT\") failed: " << e;
	}));
	return false;
      }

      PCCERT_CONTEXT context = nullptr;
      while (context = CertEnumCertificatesInStore(store, context)) {
	if (!Backend::cert_store_add_der(
	      m_cacert, context->pbCertEncoded, context->cbCertEncoded)) {
	  ZiLOG(Error, "Ztls", "cert_store_add_der() failed");
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
	ZiLOG(Error, "Ztls", ([function, path](auto &s) {
	  s << function << "(\"" << path << "\") failed";
	}));
	return false;
      }
    }
    if (m_verify) Backend::verify_cert_free(m_verify);
    m_verify = Backend::verify_cert_new(m_cacert);
    if (!m_verify) {
      ZiLOG(Error, "Ztls", "verify_cert_new() failed");
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
    m_alpnData.length(bytes);
    m_alpn.ensure(alpn.length());
    unsigned offset = 0;
    for (auto &s : alpn) {
      memcpy(m_alpnData.data() + offset, s.data(), s.length());
      m_alpn.push(ptls_iovec_t{
	m_alpnData.data() + offset,
	s.length()});
      offset += s.length();
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
	.hash = &ptls_openssl_sha384,
	.name = PTLS_CIPHER_SUITE_NAME_AES_256_GCM_SHA384
      };
      static ptls_cipher_suite_t fusion_aes128gcmsha256 = {
	.id = PTLS_CIPHER_SUITE_AES_128_GCM_SHA256,
	.aead = &ptls_fusion_aes128gcm,
	.hash = &ptls_openssl_sha256,
	.name = PTLS_CIPHER_SUITE_NAME_AES_128_GCM_SHA256
      };
      m_cipher_suites[n++] = &fusion_aes256gcmsha384;
      m_cipher_suites[n++] = &fusion_aes128gcmsha256;
#endif
    }
    for (auto p = ptls_openssl_cipher_suites; *p; ++p) {
      if (use_fusion &&
	  ((*p)->id == PTLS_CIPHER_SUITE_AES_256_GCM_SHA384 ||
	   (*p)->id == PTLS_CIPHER_SUITE_AES_128_GCM_SHA256))
	continue;
      m_cipher_suites[n++] = *p;
    }
    m_cipher_suites[n] = nullptr;
  }

  ZiMultiplex			*m_mx = nullptr;
  unsigned			m_thread = 0;
  unsigned			m_asyncThread = 0;
  ZiEventLoop			m_eventLoop;
  bool				m_eventLoopInit = false;
  bool				m_eventLoopStarted = false;
  ErrorFn			m_errorFn;

  ptls_context_t		m_ctx{};
  ptls_cipher_suite_t		*m_cipher_suites[16]{};
  ALPNData			m_alpnData;
  ALPN				m_alpn;
  Backend::CertStore		*m_cacert = nullptr;
  Backend::VerifyCert		*m_verify = nullptr;
};

// CRTP - implementation must conform to the following interface:
#if 0
  struct App : public Client<App> {
    using BufAlloc = Ztls::BufAlloc<BufSize>;

    void exception(ZmRef<ZeEvent>); // optional

    struct Link : public CliLink<App, Link, BufAlloc> {
      // TLS thread - handshake completed
      void connected(const char *alpn, int tlsver);

      void disconnected(); // TLS thread
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
  Backend::PKey			*m_key = nullptr;
  Backend::SignCert		*m_sign = nullptr;
};

template <typename App>
bool Client<App>::init(ClientParams params)
{
  using Link = typename App::Link;

  if (bool(params.certPath_) != bool(params.keyPath_)) {
    ZiLOG(Error, "Ztls", "client certPath and keyPath must be configured together");
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
    if (!this->loadCA(ZuCSpan{params.caPath_})) return false;

    if (params.certPath_ && params.keyPath_) {
      if (!Backend::load_certificates(ctx, params.certPath_.data()))
	return false;
      m_key = Backend::pkey_load_pem(params.keyPath_.data());
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
    using BufAlloc = Ztls::BufAlloc<BufSize>;

    void exception(ZmRef<ZeEvent>); // optional

    struct Link : public SrvLink<App, Link, BufAlloc> {
      // TLS thread - handshake completed
      void connected(const char *alpn);

      void disconnected(); // TLS thread
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
    this->mx()->listen(
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
    this->mx()->del(&m_rebindTimer);
    if (m_listening)
      this->mx()->stopListening(app()->localIP(), app()->localPort());
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
      app()->run([this]() { listen(); },
	  Zm::now(rebindFreq), ZmScheduler::Update, &m_rebindTimer);
    else
      ZiLOG(Error, "Ztls", ([transient](auto &s) {
	s << "listen() failed " << (transient ? "(transient)" : "");
      }));
  }

private:
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

  if (!params.certPath_) {
    ZiLOG(Error, "Ztls", "server certPath is required");
    return false;
  }
  if (!params.keyPath_) {
    ZiLOG(Error, "Ztls", "server keyPath is required");
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
    ctx->require_client_authentication = params.mTLS_ ? 1 : 0;
    ctx->max_early_data_size = 0;
    ctx->ticket_lifetime =
      params.cacheTimeout_ < 0 ? 86400 : params.cacheTimeout_;
    if (!this->loadCA(ZuCSpan{params.caPath_})) return false;

    if (!Backend::load_certificates(ctx, params.certPath_.data()))
      return false;
    m_key = Backend::pkey_load_pem(params.keyPath_.data());
    if (!m_key) return false;
    m_sign = Backend::sign_cert_new(m_key);
    if (!m_sign) {
      Backend::pkey_free(m_key);
      m_key = nullptr;
      return false;
    }
    if (params.asyncThread_ && !Backend::sign_cert_async(m_sign, true)) {
      ZiLOG(Error, "Ztls", "async server certificate signing is unsupported");
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
