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
#include <zlib/ZeAssert.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiIOBuf.hh>

#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsBackend.hh>

#include <stdlib.h>
#include <string.h>

namespace Ztls {

ZuDerive(LogMsg, ZtString<ZtStringHeapID<"Ztls.Log">>);
ZuDerive(Host, ZtString<ZtStringHeapID<"Ztls.Host">>);

// picotls runs within a single dedicated thread, without lock contention

// API functions: listen, connect, disconnect/disconnect_, send/send_ (Tx)
// API callbacks: accepted, connected, disconnected, process (Rx)

// Function Category | I/O Threads |        TLS thread          | App threads
// ------------------|-------------|----------------------------|------------
// Server            | accepted()  | connected() disconnected() | listen()
// Client            |             | connect_() connectFailed() | connect()
// Disconnect        |             | disconnect_()              | disconnect()
// Transmission (Tx) |             | send_()                    | send()
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

  void connected(ZiIOContext &io) { m_link->connected_(this, io); }
  void disconnected() {
    if (Link *link = m_link) link->disconnected_(this, ZuMv(m_link));
  }

private:
  LinkRef	m_link = nullptr;
};

// client links are persistent, own the (transient) connection
template <typename Link> using CliCxn = Cxn<Link, Link *>;
// server links are transient, are owned by the connection
template <typename Link> using SrvCxn = Cxn<Link, ZmRef<Link>>;

ZuDerive(IOQueue,
  (ZmList<ZiIOBuf, ZmListNode<ZiIOBuf, ZmListHeapID<"">>>));

struct RxCursor {
  IOQueue &queue;
  ZmRef<ZiIOBuf> cur;

  RxCursor(IOQueue &queue_) : queue(queue_) {
    if (queue.count_()) cur = queue.headNode();
  }

  ZuSpan<uint8_t> span() {
    refresh_();
    return cur ? cur->span() : ZuSpan<uint8_t>{};
  }

  bool advance(size_t n) {
    refresh_();
    if (!cur) return false;
    if (n > cur->length) n = cur->length;
    cur->advance(n);
    if (!cur->length) pop_();
    return n != 0;
  }

  bool next() {
    refresh_();
    if (!cur) return false;
    pop_();
    return cur;
  }

  bool empty() {
    refresh_();
    return !cur;
  }

private:
  void pop_() {
    if (!cur) return;
    queue.shift();
    cur = queue.count_() ? queue.headNode() : nullptr;
  }

  void refresh_() {
    if (!cur && queue.count_()) cur = queue.headNode();
    while (cur && !cur->length) pop_();
  }
};

template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = ZiIOBuf_HeapID{}()>
using IOBufAlloc =
  Zi::IOBufAlloc<IOQueue::Node, Size, MaxSize, ZuStringT<HeapID>>;

template <
  typename App, typename Impl, typename IOBufAlloc_,
  typename Cxn_, typename CxnRef_>
class Link : public ZmPolymorph {
  ZuAssert((ZuIs_<IOBufAlloc_, IOQueue::Node>{}));

public:
  using IOBufAlloc = IOBufAlloc_;
  using Cxn = Cxn_;
  using CxnRef = CxnRef_;
  using ImplRef = typename Cxn::LinkRef;

friend Cxn;

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  Link(App *app, bool isServer) : m_app(app), m_isServer(isServer) {
    reset_tls_();
  }
  ~Link() {
    if (m_tls) ptls_free(m_tls);
  }

  App *app() const { return m_app; }
  Cxn *cxn() const { return m_cxn; }

protected:
  ptls_t *tls() { return m_tls; }
  ptls_handshake_properties_t *handshake_props() { return &m_props; }
  void reset_handshake_props_() { memset(&m_props, 0, sizeof(m_props)); }

private:
  auto &txOutQueue() { return m_txOutQueue; }

  void connected_(Cxn *cxn, ZiIOContext &io) {
    m_rxBuf = new IOBufAlloc{impl()};
    m_rxBuf->clear();
    m_rx_need = m_rec_hdr_len;
    m_rx_total = 0;
    m_rx_have_hdr = false;
    io.init(ZiIOFn{ZmMkRef(this), [](Link *link, ZiIOContext &io) {
      link->rx(io);
      return true;
    }}, m_rxBuf->data(), m_rx_need, 0);
    app()->run([impl = ZmMkRef(this->impl()), cxn = ZmMkRef(cxn)]() {
      impl->connected_2(ZuMv(cxn));
    });
  }
  void connected_2(ZmRef<Cxn> cxn) {
    if (ZuUnlikely(m_cxn == cxn)) return;
    if (ZuUnlikely(m_cxn)) { auto cxn_ = ZuMv(m_cxn); cxn_->close(); }
    m_cxn = ZuMv(cxn);
    impl()->connected__();
  }

  template <typename ImplRef_>
  void disconnected_(Cxn *cxn, ImplRef_ impl_) {
    ZmRef<Impl> impl{ZuMv(impl_)};
    app()->run([impl = ZuMv(impl), cxn = ZmMkRef(cxn)]() {
      impl->disconnected_2(cxn);
      auto mx = cxn->mx();
      // drain Tx while keeping cxn referenced
      mx->txRun([cxn = ZuMv(cxn)]() { });
    });
  }
  void disconnected_2(Cxn *cxn) { // TLS thread
    if (m_rxInQueue.count_() && m_tls && ptls_handshake_is_complete(m_tls))
      while (recv());
    if (m_cxn == cxn) m_cxn = nullptr;
    reset_tls_();
    impl()->disconnected();
  }

  void rx(ZiIOContext &io) { // I/O Rx thread
    io.offset += io.length;
    m_rxBuf->length = io.offset;

    if (!m_rx_have_hdr) {
      if (io.offset < m_rec_hdr_len) {
	io.ptr = m_rxBuf->data();
	io.size = m_rec_hdr_len;
	return;
      }
      auto hdr = m_rxBuf->data();
      unsigned rec_len = (unsigned(hdr[3]) << 8) | unsigned(hdr[4]);
      m_rx_total = unsigned(m_rec_hdr_len) + rec_len;
      if (ZuUnlikely(m_rx_total > IOBufAlloc::MaxSize)) {
	ZiLOG(Error, "Ztls", "TLS record too big / corrupt");
	io.disconnect();
	return;
      }
      m_rx_need = m_rx_total - io.offset;
      m_rx_have_hdr = true;
      if (ZuUnlikely(m_rx_total > m_rxBuf->size)) {
	if (ZuUnlikely(!m_rxBuf->ensure(m_rx_total))) {
	  io.disconnect();
	  return;
	}
      }
      io.ptr = m_rxBuf->data();
      io.size = m_rx_total;
    }

    if (io.offset < m_rx_total) {
      m_rx_need = m_rx_total - io.offset;
      io.ptr = m_rxBuf->data();
      io.size = m_rx_total;
      return;
    }

    // full record
    m_rxBuf->length = m_rx_total;
    if (ZuLikely(!m_disconnecting.load_()))
      app()->run([buf = ZuMv(m_rxBuf)]() {
	auto link = static_cast<Impl *>(buf->owner);
	link->recv_(ZuMv(buf));
      });
    m_rxBuf = new IOBufAlloc{impl()};
    m_rxBuf->clear();
    m_rx_need = m_rec_hdr_len;
    m_rx_total = 0;
    m_rx_have_hdr = false;
    io.ptr = m_rxBuf->data();
    io.size = m_rx_need;
    io.offset = 0;
  }

  void recv_(ZmRef<ZiIOBuf> buf) { // TLS thread
    m_rxInQueue.pushNode(ZmRef<IOQueue::Node>{ZuMv(buf)});
    while (m_tls && !ptls_handshake_is_complete(m_tls))
      if (!handshake()) return;
    while (recv());
  }

protected:
  bool handshake() { // TLS thread
    if (ZuUnlikely(!m_tls)) return false;
    if (ZuUnlikely(!m_handshakeStarted && !m_isServer)) {
      m_handshakeStarted = true;
      size_t inlen = 0;
      int n = handshake_send_(nullptr, &inlen);
      if (!n) return handshake_done_();
      if (n == PTLS_ERROR_IN_PROGRESS || n == PTLS_ERROR_ASYNC_OPERATION)
	return false;
      if (PTLS_ERROR_GET_CLASS(n) == PTLS_ERROR_CLASS_PEER_ALERT &&
	  PTLS_ERROR_TO_ALERT(n) == PTLS_ALERT_CLOSE_NOTIFY) {
	disconnect_(true);
	return false;
      }
      ZiLOG(Error, "Ztls", ([n](auto &s) {
	s << "ptls_handshake(): " << strerror_(n);
      }));
      disconnect_(false);
      return false;
    }

    while (m_rxInQueue.count_()) {
      ZmRef<ZiIOBuf> buf = m_rxInQueue.shift();
      size_t inlen = buf->length;
      int n = handshake_send_(buf->data(), &inlen);
      if (ZuUnlikely(inlen != buf->length)) {
	ZiLOG(Error, "Ztls", "ptls_handshake() partial record");
	disconnect_(false);
	return false;
      }
      if (!n) return handshake_done_();
      if (n == PTLS_ERROR_IN_PROGRESS || n == PTLS_ERROR_ASYNC_OPERATION)
	return false;
      if (PTLS_ERROR_GET_CLASS(n) == PTLS_ERROR_CLASS_PEER_ALERT &&
	  PTLS_ERROR_TO_ALERT(n) == PTLS_ALERT_CLOSE_NOTIFY) {
	disconnect_(true);
	return false;
      }
      ZiLOG(Error, "Ztls", ([n](auto &s) {
	s << "ptls_handshake(): " << strerror_(n);
      }));
      disconnect_(false);
      return false;
    }
    return false;
  }

private:
  bool recv() { // TLS thread
    if (ZuUnlikely(!m_tls || !m_rxInQueue.count_())) return false;
    ZmRef<ZiIOBuf> buf = m_rxInQueue.shift();
    size_t inlen = buf->length;
    if (ZuUnlikely(!m_cipher)) return false;
    if (ZuUnlikely(buf->skip)) {
      ZiLOG(Error, "Ztls", "TLS RX buffer has non-zero skip");
      disconnect_(false);
      return false;
    }

    unsigned align_bits = m_cipher->aead->align_bits;

    ptls_buffer_t plain;
    auto base = buf->data() - buf->skip;
    ptls_buffer_init(&plain, base, buf->size);
    plain.origin = buf.ptr();
    plain.align_bits = align_bits;
    int n = ptls_receive(m_tls, &plain, buf->data(), &inlen);
    if (ZuUnlikely(inlen != buf->length)) {
      ZiLOG(Error, "Ztls", "ptls_receive() partial record");
      disconnect_(false);
      return false;
    }
    base = buf->data() - buf->skip;
    bool origin_match = plain.origin == buf.ptr();
    if (!n) {
      if (origin_match) {
	if (ZuUnlikely(plain.base != base)) {
	  ZiLOG(Error, "Ztls", "TLS RX buffer origin mismatch");
	  disconnect_(false);
	  return false;
	}
	if (plain.off) {
	  buf->skip = 0;
	  buf->length = plain.off;
	  m_rxPlainQueue.pushNode(ZmRef<IOQueue::Node>{ZuMv(buf)});
	}
      } else if (plain.base != base) {
	if (ZuUnlikely(!m_rx_realloc_warned)) {
	  m_rx_realloc_warned = true;
	  ZiLOG(Warning, "Ztls", "ptls_receive() reallocated, falling back to copy");
	}
	if (plain.off) {
	  ZmRef<ZiIOBuf> out = new IOBufAlloc{impl()};
	  if (ZuUnlikely(plain.off > out->size))
	    if (ZuUnlikely(!out->ensure(plain.off))) return false;
	  out->length = plain.off;
	  memcpy(out->data(), plain.base, plain.off);
	  m_rxPlainQueue.pushNode(ZmRef<IOQueue::Node>{ZuMv(out)});
	}
	ptls_buffer_dispose(&plain);
      } else if (plain.off) {
	buf->skip = 0;
	buf->length = plain.off;
	m_rxPlainQueue.pushNode(ZmRef<IOQueue::Node>{ZuMv(buf)});
      }
      int p = process_plaintext_();
      if (p < 0) return false;
      if (!p) return m_rxInQueue.count_();
      return m_rxInQueue.count_();
    }
    if (!origin_match && plain.base != base) ptls_buffer_dispose(&plain);
    if (PTLS_ERROR_GET_CLASS(n) == PTLS_ERROR_CLASS_PEER_ALERT &&
	PTLS_ERROR_TO_ALERT(n) == PTLS_ALERT_CLOSE_NOTIFY) {
      disconnect_(true);
      return true;
    }
    ZiLOG(Error, "Ztls", ([n](auto &s) {
      s << "ptls_receive(): " << strerror_(n);
    }));
    disconnect_(false);
    return false;
  }

public:
  // TX zero-copy requirements for send(ZmRef<ZiIOBuf>):
  // - use alloc_txbuf() (skip == m_tx_headroom)
  // - do not alter skip; headroom must be intact
  // - ensure avail() >= m_tx_tailroom
  // - base must remain cache-line aligned (align_bits satisfied)
  ZmRef<ZiIOBuf> alloc_txbuf(size_t plaintext_len) { // App/TLS threads
    ZmRef<ZiIOBuf> buf = new IOBufAlloc{impl()};
    unsigned total = unsigned(plaintext_len + m_tx_headroom + m_tx_tailroom);
    if (ZuUnlikely(total > buf->size)) {
      if (ZuUnlikely(!buf->ensure(total))) return nullptr;
    }
    buf->skip = m_tx_headroom;
    buf->length = plaintext_len;
    return buf;
  }

  void send(const uint8_t *data, unsigned len) { // App thread(s)
    if (ZuUnlikely(!len)) return;
    unsigned offset = 0;
    do {
      unsigned n = len - offset;
      if (ZuUnlikely(n > m_max_plaintext)) n = m_max_plaintext;
      ZmRef<ZiIOBuf> buf = alloc_txbuf(n);
      if (ZuUnlikely(!buf)) return;
      memcpy(buf->data(), data + offset, n);
      app()->invoke([buf = ZuMv(buf)]() mutable {
	auto link = static_cast<Impl *>(buf->owner);
	link->send_(ZuMv(buf));
      });
      offset += n;
    } while (offset < len);
  }
  void send(ZmRef<ZiIOBuf> buf) {
    if (ZuUnlikely(!buf->length)) return;
    if (ZuUnlikely(m_disconnecting.load_())) return;
    buf->owner = this;
    app()->invoke([buf = ZuMv(buf)]() mutable {
      auto link = static_cast<Link *>(buf->owner);
      link->send_(ZuMv(buf));
    });
  }

  void send_(ZmRef<ZiIOBuf> buf) { // TLS thread
    if (ZuUnlikely(!buf || !buf->length)) return;
    if (ZuUnlikely(m_disconnecting.load_())) return;
    if (ZuUnlikely(!m_tls || !ptls_handshake_is_complete(m_tls))) return;

    if (ZuUnlikely(buf->length > m_max_plaintext)) {
      send_(buf->data(), buf->length);
      return;
    }

    if (ZuUnlikely(buf->skip != m_tx_headroom)) {
      ZiLOG(Error, "Ztls", "TLS TX buffer missing headroom");
      disconnect_(false);
      return;
    }
    if (ZuUnlikely(buf->avail() < m_tx_tailroom)) {
      unsigned need = unsigned(m_tx_headroom + buf->length + m_tx_tailroom);
      if (ZuUnlikely(need > buf->size))
	if (ZuUnlikely(!buf->ensure(need))) {
	  ZiLOG(Error, "Ztls", "TLS TX buffer growth failed");
	  disconnect_(false);
	  return;
	}
      if (ZuUnlikely(buf->avail() < m_tx_tailroom)) {
	ZiLOG(Error, "Ztls", "TLS TX buffer missing tail room");
	disconnect_(false);
	return;
      }
    }

    bool preflushed = preflush_key_update_();
    if (ZuUnlikely(m_disconnecting.load_())) return;
    send_record_(ZuMv(buf), buf->data(), buf->length, preflushed);
  }
  void send_(const uint8_t *data, unsigned len) { // TLS thread
    if (ZuUnlikely(!len)) return;
    if (ZuUnlikely(m_disconnecting.load_())) return;
    if (ZuUnlikely(!m_tls || !ptls_handshake_is_complete(m_tls))) return;
    unsigned offset = 0;
    do {
      unsigned n = len - offset;
      if (ZuUnlikely(n > m_max_plaintext)) n = m_max_plaintext;
      ZmRef<ZiIOBuf> buf = alloc_txbuf(n);
      if (ZuUnlikely(!buf)) return;
      memcpy(buf->data(), data + offset, n);
      send_(ZuMv(buf));
      offset += n;
    } while (offset < len);
  }

private:
  int handshake_send_(const uint8_t *input, size_t *inlen) { // TLS thread
    ZmRef<ZiIOBuf> out = new IOBufAlloc{impl()};
    auto base = out->data() - out->skip;
    ptls_buffer_t sendbuf;
    ptls_buffer_init(&sendbuf, base, out->size);
    sendbuf.origin = out.ptr();
    int n = ptls_handshake(m_tls, &sendbuf, input, inlen, &m_props);
    flush_sendbuf_(sendbuf, ZuMv(out));
    return n;
  }

  int send_alert_(uint8_t level, uint8_t desc) { // TLS thread
    ZmRef<ZiIOBuf> out = new IOBufAlloc{impl()};
    if (ZuUnlikely(out->size < 256))
      if (ZuUnlikely(!out->ensure(256))) return PTLS_ERROR_NO_MEMORY;
    ptls_buffer_t sendbuf;
    ptls_buffer_init(&sendbuf, out->data() - out->skip, out->size);
    sendbuf.origin = out.ptr();
    int n = ptls_send_alert(m_tls, &sendbuf, level, desc);
    flush_sendbuf_(sendbuf, ZuMv(out));
    return n;
  }

  void flush_sendbuf_(ptls_buffer_t &sendbuf, ZmRef<ZiIOBuf> out) {
    auto base = out->data() - out->skip;
    bool origin_match = sendbuf.origin == out.ptr();
    if (!sendbuf.off) {
      if (!origin_match && sendbuf.base != base) ptls_buffer_dispose(&sendbuf);
      return;
    }
    if (sendbuf.base != base) {
      if (origin_match) {
	ZiLOG(Error, "Ztls", "TLS handshake buffer origin mismatch");
	disconnect_(false);
	return;
      }
      if (ZuUnlikely(!m_tx_realloc_warned)) {
	m_tx_realloc_warned = true;
	ZiLOG(Warning, "Ztls", "ptls_handshake() reallocated, falling back to copy");
      }
      txOut(sendbuf.base, sendbuf.off);
      ptls_buffer_dispose(&sendbuf);
      return;
    }
    out->skip = 0;
    out->length = sendbuf.off;
    txOut(ZuMv(out));
  }

  bool preflush_key_update_() { // TLS thread
    if (ZuUnlikely(!m_tls || !m_cipher)) return false;
    if (m_tlsver != PTLS_PROTOCOL_VERSION_TLS13) return false;
    constexpr uint64_t Threshold = (1ull<<24);
    if (!m_tx_need_key_update && m_tx_seq_est < Threshold - 1) return false;
    m_tx_need_key_update = false;
    int n = ptls_update_key(m_tls, 0);
    if (n) {
      ZiLOG(Error, "Ztls", ([n](auto &s) {
	s << "ptls_update_key(): " << strerror_(n);
      }));
      disconnect_(false);
      return false;
    }
    ZmRef<ZiIOBuf> buf = alloc_txbuf(0);
    if (ZuUnlikely(!buf)) return false;
    send_record_(ZuMv(buf), nullptr, 0, true);
    m_tx_seq_est = 0;
    return true;
  }

  void send_record_(ZmRef<ZiIOBuf> buf, const uint8_t *data, unsigned len,
      bool preflushed) { // TLS thread
    if (ZuUnlikely(!m_cipher)) return;

    unsigned need = unsigned(m_tx_headroom + len + m_tx_tailroom);
    if (ZuUnlikely(need > buf->size))
      if (ZuUnlikely(!buf->ensure(need))) {
	ZiLOG(Error, "Ztls", "TLS TX buffer growth failed");
	disconnect_(false);
	return;
      }

    const uint8_t *payload = len ? buf->data() : data;
    auto base = buf->data() - m_tx_headroom;
    size_t cap = buf->size;

    unsigned align_bits = m_cipher->aead->align_bits;
    if (align_bits) {
      uintptr_t mask = (uintptr_t(1) << align_bits) - 1;
      if (ZuUnlikely(reinterpret_cast<uintptr_t>(base) & mask)) {
	ZiLOG(Error, "Ztls", "TLS TX buffer misaligned");
	disconnect_(false);
	return;
      }
    }

    ptls_buffer_t out;
    ptls_buffer_init(&out, base, cap);
    out.origin = buf.ptr();
    out.align_bits = align_bits;
    int n = ptls_send(m_tls, &out, payload, len);

    unsigned recs = count_records_(out.base, out.off);
    if (recs) m_tx_seq_est += recs;

    auto current_base = buf->data() - m_tx_headroom;
    bool origin_match = out.origin == buf.ptr();
    if (out.base != current_base) {
      if (origin_match) {
	ZiLOG(Error, "Ztls", "TLS TX buffer origin mismatch");
	disconnect_(false);
	return;
      }
      if (ZuUnlikely(!m_tx_realloc_warned)) {
	m_tx_realloc_warned = true;
	ZiLOG(Warning, "Ztls", "ptls_send() reallocated, falling back to copy");
      }
      if (recs > 1 && !preflushed)
	txOutSplit_(out.base, out.off);
      else
	txOut(out.base, out.off);
      ptls_buffer_dispose(&out);
      if (n) {
	ZiLOG(Error, "Ztls", ([n](auto &s) {
	  s << "ptls_send(): " << strerror_(n);
	}));
	disconnect_(false);
      }
      return;
    }

    if (out.off) {
      buf->skip = 0;
      buf->length = out.off;
      if (recs > 1 && !preflushed)
	txOutSplit_(buf->data(), buf->length);
      else
	txOut(ZuMv(buf));
    }

    if (n) {
      ZiLOG(Error, "Ztls", ([n](auto &s) {
	s << "ptls_send(): " << strerror_(n);
      }));
      disconnect_(false);
    }
  }

  unsigned count_records_(const uint8_t *data, size_t len) const {
    unsigned count = 0;
    size_t o = 0;
    while (len - o >= m_rec_hdr_len) {
      auto hdr = data + o;
      unsigned rec_len = (unsigned(hdr[3]) << 8) | unsigned(hdr[4]);
      size_t total = m_rec_hdr_len + rec_len;
      if (ZuUnlikely(total > len - o)) break;
      ++count;
      o += total;
    }
    return count;
  }

  void txOutSplit_(const uint8_t *data, size_t len) { // TLS thread
    size_t o = 0;
    while (len - o >= m_rec_hdr_len) {
      auto hdr = data + o;
      unsigned rec_len = (unsigned(hdr[3]) << 8) | unsigned(hdr[4]);
      size_t total = m_rec_hdr_len + rec_len;
      if (ZuUnlikely(total > len - o)) break;
      ZmRef<ZiIOBuf> buf = new IOBufAlloc{this};
      if (ZuUnlikely(total > buf->size))
	if (ZuUnlikely(!buf->ensure(total))) return;
      buf->length = total;
      memcpy(buf->data(), data + o, total);
      txOut(ZuMv(buf));
      o += total;
    }
    if (o < len) txOut(data + o, len - o);
  }

  int txOut(ZmRef<ZiIOBuf> buf) { // TLS thread
    if (ZuUnlikely(!buf || !buf->length)) return 0;
    if (ZuUnlikely(!m_cxn)) return buf->length; // discard late Tx
    m_cxn->send(ZiIOFn::mvFn(ZuMv(buf),
	[](ZmRef<ZiIOBuf> buf, ZiIOContext &io) {
	  auto self = static_cast<Link *>(buf->owner);
	  auto &queue = self->txOutQueue();
	  if (queue.count_()) {
	    queue.pushNode(ZuMv(buf));
	    return true;
	  }
	  auto buf_ = buf.ptr();
	  queue.pushNode(ZuMv(buf));
	  io.init(ZiIOFn{queue.headNode(),
	    [](ZiIOBuf *buf, ZiIOContext &io) {
	      auto self = static_cast<Link *>(buf->owner);
	      auto &queue = self->txOutQueue();
	      if (ZuUnlikely(io.length < 0)) {
		queue.clean();
		io.complete();
		return true;
	      }
	      if (ZuUnlikely((io.offset += io.length) < io.size))
		return true;
	      queue.shift();
	      if (queue.count_()) {
		{
		  ZmRef<ZiIOBuf> buf_{queue.headNode()};
		  buf = buf_.ptr();
		  io.fn.object(ZuMv(buf_));
		}
		io.ptr = buf->data();
		io.size = buf->length;
		io.offset = 0;
	      } else
		io.complete();
	      return true;
	    }}, buf_->data(), buf_->length, 0);
	  return true;
	}));
    return buf->length;
  }

  int txOut(const uint8_t *data, size_t len) { // TLS thread
    if (ZuUnlikely(!len)) return 0;
    if (ZuUnlikely(!m_cxn)) return len; // discard late Tx
    unsigned offset = 0;
    do {
      unsigned n = len - offset;
      ZmRef<ZiIOBuf> buf = new IOBufAlloc{this};
      if (ZuUnlikely(n > buf->size)) n = buf->size;
      buf->length = n;
      memcpy(buf->data(), data + offset, n);
      m_cxn->send(ZiIOFn::mvFn(ZuMv(buf),
	  [](ZmRef<ZiIOBuf> buf, ZiIOContext &io) {
	    auto self = static_cast<Link *>(buf->owner);
	    auto &queue = self->txOutQueue();
	    if (queue.count_()) {
	      queue.pushNode(ZuMv(buf));
	      return true;
	    }
	    auto buf_ = buf.ptr();
	    queue.pushNode(ZuMv(buf));
	    io.init(ZiIOFn{queue.headNode(),
	      [](ZiIOBuf *buf, ZiIOContext &io) {
		auto self = static_cast<Link *>(buf->owner);
		auto &queue = self->txOutQueue();
		if (ZuUnlikely(io.length < 0)) {
		  queue.clean();
		  io.complete();
		  return true;
		}
		if (ZuUnlikely((io.offset += io.length) < io.size))
		  return true;
		queue.shift();
		if (queue.count_()) {
		  {
		    ZmRef<ZiIOBuf> buf_{queue.headNode()};
		    buf = buf_.ptr();
		    io.fn.object(ZuMv(buf_));
		  }
		  io.ptr = buf->data();
		  io.size = buf->length;
		  io.offset = 0;
		} else
		  io.complete();
		return true;
	      }}, buf_->data(), buf_->length, 0);
	    return true;
	  }));
      offset += n;
    } while (offset < len);
    return len;
  }

public:
  void disconnect() { // App thread(s)
    m_disconnecting = 1;
    app()->invoke([this]() { disconnect_(); });
  }
  void disconnect_(bool notify = true) { // TLS thread
    m_disconnecting = 1; // disconnect() might be bypassed
    app()->mx()->del(&m_reconnTimer);
    if (notify) {
      int n = m_tls ?
	send_alert_(PTLS_ALERT_LEVEL_WARNING, PTLS_ALERT_CLOSE_NOTIFY) :
	0;
      if (n) ZiLOG(Warning, "Ztls", ([n](auto &s) {
	s << "ptls_send_alert(): " << strerror_(n);
      }));
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
  bool handshake_done_() {
    m_tlsver = ptls_get_protocol_version(m_tls);
    m_cipher = ptls_get_cipher(m_tls);
    if (ZuUnlikely(!m_cipher)) {
      ZiLOG(Error, "Ztls", "ptls_get_cipher() failed");
      disconnect_(false);
      return false;
    }
    m_rec_hdr_len = 5;
    m_rec_tag_len = m_cipher->aead->tag_size;
    m_rec_iv_len =
      (m_tlsver == PTLS_PROTOCOL_VERSION_TLS12) ?
	m_cipher->aead->tls12.record_iv_size : 0;
    m_rec_overhead = ptls_get_record_overhead(m_tls);
    m_tx_headroom = m_rec_hdr_len + m_rec_iv_len;
    m_tx_tailroom =
      (m_rec_overhead > m_tx_headroom) ?
	(m_rec_overhead - m_tx_headroom) : 0;
    m_max_plaintext = 16 * 1024;
    m_tx_seq_est = 0;
    m_tx_need_key_update = false;
    m_tx_realloc_warned = false;
    m_rx_realloc_warned = false;
    impl()->connected(
      ptls_get_negotiated_protocol(m_tls),
      tlsver_(ptls_get_protocol_version(m_tls)));
    return recv();
  }
  int process_plaintext_() {
    RxCursor rx{m_rxPlainQueue};
    while (!rx.empty()) {
      int n = impl()->process(rx);
      if (ZuUnlikely(n < 0)) {
	disconnect_();
	return -1;
      }
      if (!n) return 0;
    }
    return 1;
  }
  void reset_tls_() {
    if (m_tls) ptls_free(m_tls);
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
    m_tx_headroom = 0;
    m_tx_tailroom = 0;
    m_max_plaintext = 16 * 1024;
    m_tx_seq_est = 0;
    m_tx_need_key_update = false;
    m_tx_realloc_warned = false;
    m_rx_realloc_warned = false;
    m_rxInQueue.clean();
    m_rxPlainQueue.clean();
    reset_handshake_props_();
    m_handshakeStarted = false;
  }

private:
  App			*m_app = nullptr;
  ZmScheduler::Timer	m_reconnTimer;

  // I/O Rx thread
  ZmRef<ZiIOBuf>	m_rxBuf;
  unsigned		m_rx_need = 5;
  unsigned		m_rx_total = 0;
  bool			m_rx_have_hdr = false;

  // I/O Tx thread
  IOQueue		m_txOutQueue;

  // TLS thread
  ptls_t		*m_tls = nullptr;
  uint16_t		m_tlsver = 0;
  ptls_cipher_suite_t	*m_cipher = nullptr;
  size_t		m_rec_hdr_len = 5;
  size_t		m_rec_iv_len = 0;
  size_t		m_rec_tag_len = 0;
  size_t		m_rec_overhead = 0;
  size_t		m_tx_headroom = 0;
  size_t		m_tx_tailroom = 0;
  size_t		m_max_plaintext = 16 * 1024;
  uint64_t		m_tx_seq_est = 0;
  bool			m_tx_need_key_update = false;
  bool			m_tx_realloc_warned = false;
  bool			m_rx_realloc_warned = false;
  ptls_handshake_properties_t m_props{};
  bool			m_handshakeStarted = false;
  CxnRef		m_cxn = nullptr;
  IOQueue		m_rxInQueue;
  IOQueue		m_rxPlainQueue;

  // Contended
  ZmAtomic<unsigned>	m_disconnecting = 0;
};

// client links are persistent, own the (transient) connection
template <typename App, typename Impl, typename IOBufAlloc, typename Cxn>
using CliLink_ = Link<App, Impl, IOBufAlloc, Cxn, ZmRef<Cxn>>;
// server links are transient, are owned by the connection
template <typename App, typename Impl, typename IOBufAlloc, typename Cxn>
using SrvLink_ = Link<App, Impl, IOBufAlloc, Cxn, Cxn *>;

template <typename App, typename Impl, typename IOBufAlloc = IOBufAlloc<>>
class CliLink : public CliLink_<App, Impl, IOBufAlloc, CliCxn<Impl>> {
public:
  using Cxn = CliCxn<Impl>;
  using Base = CliLink_<App, Impl, IOBufAlloc, Cxn>;

  using Base::impl;
  using Base::app;
  using Base::handshake_props;
  using Base::reset_handshake_props_;
  using Base::reset_tls_;
  using Base::tls;

friend Base;

  CliLink(App *app) : Base{app, false} { }
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

  void connected__() {
    prepare_handshake_();
    while (this->handshake());
  }

  void prepare_handshake_() {
    reset_handshake_props_();
    auto props = handshake_props();
    auto list = app()->alpn_list();
    size_t count = app()->alpn_count();
    if (list && count) {
      props->client.negotiated_protocols.list = list;
      props->client.negotiated_protocols.count = count;
    }
    if (m_ticket.length())
      props->client.session_ticket =
	ptls_iovec_init(m_ticket.data(), m_ticket.length());
    m_maxEarlyData = 0;
    props->client.max_early_data_size = &m_maxEarlyData;
    props->client.early_data_acceptance = PTLS_EARLY_DATA_UNKNOWN;
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
  ZtArray<uint8_t>	m_ticket;
  size_t		m_maxEarlyData = 0;
  Host			m_server;
  uint16_t		m_port;
};

template <typename App, typename Impl, typename IOBufAlloc = IOBufAlloc<>>
class SrvLink : public SrvLink_<App, Impl, IOBufAlloc, SrvCxn<Impl>> {
public:
  using Cxn = SrvCxn<Impl>;
  using Base = SrvLink_<App, Impl, IOBufAlloc, Cxn>;

  using Base::impl;
  using Base::app;

friend Base;

  SrvLink(App *app) : Base(app, true) { }

private:
  void connected__() { } // unused

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
    for (size_t i = 0; i < count; ++i) {
      for (size_t j = 0; j < params->negotiated_protocols.count; ++j) {
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
template <typename, typename, typename, typename, typename> friend class Link;
template <typename, typename, typename> friend class CliLink;
template <typename, typename, typename> friend class SrvLink;

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

  template <typename L>
  bool init(ZiMultiplex *mx, ZuCSpan thread, L l) {
    m_mx = mx;
    if (!(m_thread = m_mx->sid(thread))) {
      ZiLOG(Error, "Ztls", ([thread = LogMsg{thread}](auto &s) {
	s << "invalid Rx thread ID \"" << thread << '"';
      }));
      return false;
    }
    if (!m_mx->running()) {
      ZiLOG(Error, "Ztls", "multiplexer not running");
      return false;
    }
    return ZmBlock<bool>{}([this, l = ZuMv(l)](auto wake) mutable {
      invoke([this, l = ZuMv(l), wake = ZuMv(wake)]() mutable {
	wake(init_(ZuMv(l)));
      });
    });
  }
private:
  template <typename L>
  bool init_(L l) {
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
    if (!l()) return false;
    return true;
  }

public:
  void final() { }

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

protected:
  // TLS thread
  ptls_context_t *ctx() { return &m_ctx; }
  const ptls_context_t *ctx() const { return &m_ctx; }

  const ptls_iovec_t *alpn_list() const {
    return m_alpn.length() ? m_alpn.data() : nullptr;
  }
  size_t alpn_count() const { return m_alpn.length(); }

  // Arch/Ubuntu/Debian/SLES - /etc/ssl/certs
  // Fedora/CentOS/RHEL - /etc/pki/tls/certs
  // Android - /system/etc/security/cacerts
  // FreeBSD - /usr/local/share/certs
  // NetBSD - /etc/openssl/certs
  // AIX - /var/ssl/certs
  // Windows - ROOT certificate store (using Cert* API)

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
  bool init_alpn_(const char **alpn) {
    m_alpn.clear();
    if (!alpn) return true;
    size_t count = 0;
    while (alpn[count]) ++count;
    if (!count) return true;
    m_alpn.length(count);
    for (size_t i = 0; i < count; ++i) {
      const char *p = alpn[i];
      m_alpn[i] = ptls_iovec_init(p, strlen(p));
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

  ptls_context_t		m_ctx{};
  ptls_cipher_suite_t		*m_cipher_suites[16]{};
  ZtArray<ptls_iovec_t>	m_alpn;
  Backend::CertStore		*m_cacert = nullptr;
  Backend::VerifyCert		*m_verify = nullptr;
};

// CRTP - implementation must conform to the following interface:
#if 0
  struct App : public Client<App> {
    using IOBufAlloc = Ztls::IOBufAlloc<BufSize>;

    void exception(ZmRef<ZeEvent>); // optional

    struct Link : public CliLink<App, Link, IOBufAlloc> {
      // TLS thread - handshake completed
      void connected(const char *alpn, int tlsver);

      void disconnected(); // TLS thread
      void connectFailed(bool transient); // I/O Tx thread

      // process() should return:
      // +ve - consumed some data and can continue
      // 0   - more data needed - leave buffers queued
      // -ve - disconnect, abandon any remaining Rx data
      int process(RxCursor &); // process received data

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
  bool init(ZiMultiplex *mx, ZuCSpan thread, const char **alpn,
      const char *caPath = nullptr,
      const char *certPath = nullptr,
      const char *keyPath = nullptr);

  void final() { Base::final(); }

protected:
  unsigned reconnFreq() const { return 0; } // default

private:
  Backend::PKey			*m_key = nullptr;
  Backend::SignCert		*m_sign = nullptr;
};

template <typename App>
bool Client<App>::init(
  ZiMultiplex *mx, ZuCSpan thread, const char **alpn,
  const char *caPath, const char *certPath, const char *keyPath)
{
  using Link = typename App::Link;

  return Base::init(mx, thread, [
    this, caPath, alpn, certPath, keyPath
  ]() -> bool {
    if (!this->init_alpn_(alpn)) return false;
    auto ctx = this->ctx();
    ctx->on_client_hello = nullptr;
    ctx->save_ticket = {
      .cb = [](ptls_save_ticket_t *, ptls_t *tls, ptls_iovec_t input) => int {
	auto link = static_cast<Link *>(*ptls_get_data_ptr(tls));
	if (link) link->save_ticket(input);
	return 0;
      }
    };
    ctx->sign_certificate = nullptr;
    ctx->encrypt_ticket = nullptr;
    ctx->require_client_authentication = 0;
    if (!this->loadCA(caPath)) return false;

    if (certPath && keyPath) {
      if (!Backend::load_certificates(ctx, certPath)) return false;
      m_key = Backend::pkey_load_pem(keyPath);
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
    using IOBufAlloc = Ztls::IOBufAlloc<BufSize>;

    void exception(ZmRef<ZeEvent>); // optional

    struct Link : public SrvLink<App, Link, IOBufAlloc> {
      // TLS thread - handshake completed
      void connected(const char *alpn);

      void disconnected(); // TLS thread
      void connectFailed(bool transient); // I/O Tx thread
      
      // process() should return:
      // +ve - consumed some data and can continue
      // 0   - more data needed - leave buffers queued
      // -ve - disconnect, abandon any remaining Rx data
      int process(RxCursor &); // process received data
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

  bool init(
    ZiMultiplex *mx, ZuCSpan thread, const char **alpn,
    const char *caPath, const char *certPath, const char *keyPath,
    bool mTLS = false, int cacheMax = -1, int cacheTimeout = -1);

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
bool Server<App>::init(
  ZiMultiplex *mx, ZuCSpan thread, const char **alpn,
  const char *caPath, const char *certPath, const char *keyPath,
  bool mTLS = false, int cacheMax = -1, int cacheTimeout = -1)
{
  using Link = typename App::Link;

  return Base::init(mx, thread, [
    this, alpn, caPath, certPath, keyPath, mTLS, cacheMax, cacheTimeout
  ]() -> bool {
    (void)cacheMax;
    if (!this->init_alpn_(alpn)) return false;
    auto ctx = this->ctx();
    ctx->on_client_hello = {
      .cb = [](ptls_on_client_hello_t *, ptls_t *tls, ptls_on_client_hello_parameters_t *params) {
	auto link = static_cast<Link *>(*ptls_get_data_ptr(tls));
	return link ? link->on_client_hello(tls, params) : 0;
      }
    };
    ctx->sign_certificate = nullptr;
    ctx->encrypt_ticket = nullptr;
    ctx->save_ticket = nullptr;
    ctx->require_client_authentication = mTLS ? 1 : 0;
    ctx->max_early_data_size = 0;
    ctx->ticket_lifetime = cacheTimeout < 0 ? 86400 : cacheTimeout;
    if (!this->loadCA(caPath)) return false;

    if (!Backend::load_certificates(ctx, certPath)) return false;
    m_key = Backend::pkey_load_pem(keyPath);
    if (!m_key) return false;
    m_sign = Backend::sign_cert_new(m_key);
    if (!m_sign) {
      Backend::pkey_free(m_key);
      m_key = nullptr;
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
