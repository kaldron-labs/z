//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// mbedtls C++ wrapper - main TLS/SSL component

#ifndef Ztls_HH
#define Ztls_HH

#include <zlib/ZtlsLib.hh>

#include <mbedtls/ssl.h>
#include <mbedtls/ssl_cache.h>
#include <mbedtls/ssl_ticket.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/platform.h>
#ifdef ZDEBUG
#include <mbedtls/debug.h>
#endif

#if defined(MBEDTLS_PLATFORM_MEMORY) && \
  (!defined(MBEDTLS_PLATFORM_FREE_MACRO) || \
   !defined(MBEDTLS_PLATFORM_CALLOC_MACRO))
#define Ztls_VHEAP
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuSpan.hh>

#include <zlib/ZmList.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmVHeap.hh>

#include <zlib/ZeLog.hh>
#include <zlib/ZeAssert.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiIOBuf.hh>

#include <zlib/ZtlsRandom.hh>

namespace Ztls {

constexpr unsigned RxBufSize = MBEDTLS_SSL_IN_CONTENT_LEN;

ZuDerive(VHeap, ZmVHeap<"mbedtls">);

ZuDerive(LogMsg, ZtString<ZtStringHeapID<"Ztls.Log">>);
ZuDerive(Host, ZtString<ZtStringHeapID<"Ztls.Host">>);

// mbedtls runs within a single dedicated thread, without lock contention

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

  Link(App *app) : m_app(app) {
    mbedtls_ssl_init(&m_ssl);
    mbedtls_ssl_setup(&m_ssl, app->conf());
    mbedtls_ssl_set_bio(&m_ssl, this, txOut_, rxIn_, nullptr);
  }
  ~Link() {
    mbedtls_ssl_free(&m_ssl);
  }

  App *app() const { return m_app; }
  Cxn *cxn() const { return m_cxn; }

protected:
  mbedtls_ssl_context *ssl() { return &m_ssl; }

private:
  auto &txOutQueue() { return m_txOutQueue; }

  void connected_(Cxn *cxn, ZiIOContext &io) {
    m_rxBuf = new IOBufAlloc{impl()};
    io.init(ZiIOFn{ZmMkRef(this), [](Link *link, ZiIOContext &io) {
      link->rx(io);
      return true;
    }}, m_rxBuf->data(), m_rxBuf->size, 0);
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
    if (m_rxInQueue.count_() && mbedtls_ssl_is_handshake_over(&m_ssl))
      while (recv());
    mbedtls_ssl_session_reset(&m_ssl);
    if (m_cxn == cxn) m_cxn = nullptr;
    impl()->disconnected();
  }

  void rx(ZiIOContext &io) { // I/O Rx thread
    io.offset += io.length;
    m_rxBuf->length = io.offset;
    if (ZuLikely(!m_disconnecting.load_()))
      app()->run([buf = ZuMv(m_rxBuf)]() {
	auto link = static_cast<Impl *>(buf->owner);
	link->recv_(ZuMv(buf));
      });
    m_rxBuf = new IOBufAlloc{impl()};
    io.ptr = m_rxBuf->data();
    io.length = m_rxBuf->size;
    io.offset = 0;
  }

  void recv_(ZmRef<ZiIOBuf> buf) { // TLS thread
    m_rxInQueue.pushNode(ZmRef<IOQueue::Node>{ZuMv(buf)});
    while (!mbedtls_ssl_is_handshake_over(&m_ssl))
      if (!handshake()) return;
    while (recv());
  }

  // ssl bio f_recv function - TLS thread
  static int rxIn_(void *link_, uint8_t *ptr, size_t len) {
    return static_cast<Link *>(link_)->rxIn(ptr, len);
  }
  int rxIn(void *ptr, size_t len) { // TLS thread
    if (ZuUnlikely(!m_rxInQueue.count_())) {
      if (ZuUnlikely(!m_cxn)) return MBEDTLS_ERR_SSL_CONN_EOF;
      return MBEDTLS_ERR_SSL_WANT_READ;
    }
    ZmRef<ZiIOBuf> buf = m_rxInQueue.shift();
    unsigned n = buf->length;
    if (len > n) len = n;
    memcpy(ptr, buf->data(), len);
    buf->advance(len);
    if (buf->length) m_rxInQueue.unshiftNode(ZmRef<IOQueue::Node>{ZuMv(buf)});
    return len;
  }

protected:
  bool handshake() { // TLS thread
    int n = mbedtls_ssl_handshake(&m_ssl);

    if (n) {
      switch (n) {
	case MBEDTLS_ERR_SSL_WANT_READ:
#ifdef MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS
	case MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS:
#endif
	  break;
	case MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY:
	case MBEDTLS_ERR_SSL_CONN_EOF:
	case MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE: // remote verification failure
	  break;
	case MBEDTLS_ERR_X509_CERT_VERIFY_FAILED: {
	  ZeLOG(Error, ([hostname = LogMsg{impl()->server()}](auto &s) {
	    s << "server \"" << hostname << "\": unable to verify X.509 cert";
	  }));
	  disconnect_(false);
	} break;
	default:
	  ZeLOG(Error, ([n](auto &s) {
	    s << "mbedtls_ssl_handshake(): " << strerror_(n);
	  }));
	  disconnect_(false);
	  break;
      }
      return false;
    }

    impl()->verify_(); // client only - verify server cert
    impl()->save_(); // client only - save session for subsequent reconnect

    static auto tlsver = [](int i) {
      switch (i) {
	default: return 0;
	case MBEDTLS_SSL_VERSION_TLS1_2: return 12;
	case MBEDTLS_SSL_VERSION_TLS1_3: return 13;
      }
    };
    impl()->connected(
      mbedtls_ssl_get_alpn_protocol(&m_ssl),
      tlsver(mbedtls_ssl_get_version_number(&m_ssl)));

    return recv();
  }

private:
  bool recv() { // TLS thread
    int n = mbedtls_ssl_read(&m_ssl, nullptr, 0);

    if (n < 0) {
      switch (n) {
	case MBEDTLS_ERR_SSL_ASYNC_IN_PROGRESS:
	case MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS:
	case MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET:
	  return true;
	case MBEDTLS_ERR_SSL_WANT_READ:
	  if (!m_rxInQueue.count_()) return false;
	  break;
	case MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY:
	  disconnect_(true);
	  return true;
	case MBEDTLS_ERR_SSL_CONN_EOF:
	  disconnect_(false);
	  return false;
	default:
	  ZeLOG(Error, ([n](auto &s) {
	    s << "mbedtls_ssl_read(): " << strerror_(n);
	  }));
	  disconnect_(false);
	  return false;
      }
    }

    // Note: reads directly from the rx plaintext buffer to avoid
    // unnecessary buffer copying; mutates mbedtls internals accordingly
    // (in_offt, in_msglen, keep_current_message)

    auto ptr = m_ssl.private_in_offt;
    n = m_ssl.private_in_msglen;

    if (!ptr || !n) return false;

    auto o = n;
    n = impl()->process(ZuSpan<uint8_t>(ptr, o));
    if (ZuUnlikely(n < 0)) {
      m_ssl.private_in_offt = nullptr;
      m_ssl.private_keep_current_message = 0;
      disconnect_();
      return false;
    }
    // defensive sanity check on process() return, n > o wreaks havoc
    ZeAssert(n <= o, (o, n), "o=" << o << " n=" << n, n = o);
    if (ZuLikely(n)) {
      ZmAssert(n <= m_ssl.private_in_msglen);
      if (!(m_ssl.private_in_msglen -= n)) {
	m_ssl.private_in_offt = nullptr;
	m_ssl.private_keep_current_message = 0;
      } else
	m_ssl.private_in_offt += n;
    }

    return true;
  }

public:
  void send(const uint8_t *data, unsigned len) { // App thread(s)
    if (ZuUnlikely(!len)) return;
    unsigned offset = 0;
    do {
      unsigned n = len - offset;
      ZmRef<ZiIOBuf> buf = new IOBufAlloc{impl()};
      if (ZuUnlikely(n > buf->size)) n = buf->size;
      buf->length = n;
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
    send_(buf->data(), buf->length);
  }
  void send_(const uint8_t *data, unsigned len) { // TLS thread
    if (ZuUnlikely(!len)) return;
    if (ZuUnlikely(m_disconnecting.load_())) return;
    unsigned offset = 0;
    do {
      // Note: mbedtls encrypts the plaintext in-place and furthermore needs
      // to re-frame the ciphertext - the buffer copy can't (and shouldn't)
      // be elided

      int n = mbedtls_ssl_write(&m_ssl, data + offset, len - offset);
      if (n <= 0) {
	if (ZuUnlikely(!n)) {
	  ZeLOG(Error, "mbedtls_ssl_write(): unknown error");
	  return;
	}
	switch (n) {
	  case MBEDTLS_ERR_SSL_WANT_READ:
	  case MBEDTLS_ERR_SSL_WANT_WRITE:
	    continue;
	}
	ZeLOG(Error, ([n](auto &s) {
	  s << "mbedtls_ssl_write(): " << strerror_(n);
	}));
	disconnect_(false);
	return;
      }
      offset += n;
    } while (offset < len);
  }

private:
  // ssl bio f_send function - TLS thread
  static int txOut_(void *link_, const uint8_t *data, size_t len) {
    return static_cast<Link *>(link_)->txOut(data, len);
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
		  io.size = buf->size;
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
      int n = mbedtls_ssl_close_notify(&m_ssl);
      if (n) ZeLOG(Warning, ([n](auto &s) {
	s << "mbedtls_ssl_close_notify(): " << strerror_(n);
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

private:
  App			*m_app = nullptr;
  ZmScheduler::Timer	m_reconnTimer;

  // I/O Rx thread
  ZmRef<ZiIOBuf>	m_rxBuf;

  // I/O Tx thread
  IOQueue		m_txOutQueue;

  // TLS thread
  mbedtls_ssl_context	m_ssl;
  CxnRef		m_cxn = nullptr;
  IOQueue		m_rxInQueue;

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

friend Base;

  CliLink(App *app) : Base{app} {
    mbedtls_ssl_session_init(&m_session);
  }
  CliLink(App *app, Host server, uint16_t port) :
      Base{app}, m_server{ZuMv(server)}, m_port{port} {
    mbedtls_ssl_session_init(&m_session);
  }
  ~CliLink() {
    mbedtls_ssl_session_free(&m_session);
  }

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
      ZeLOG(Error, ([server = LogMsg{m_server}](auto &s) {
	s << '"' << server << "\": hostname lookup failure";
      }));
      impl()->connectFailed(true);
      return;
    }
    {
      int n;
      n = mbedtls_ssl_set_hostname(this->ssl(), m_server);
      if (n) {
	ZeLOG(Error, ([server = LogMsg{m_server}, n](auto &s) {
	  s << "mbedtls_ssl_set_hostname(\"" << server << "\"): " <<
	    strerror_(n);
	}));
	impl()->connectFailed(true);
	return;
      }
    }

    this->load_(); // load any session saved from a previous connection

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
  void save_() {
    int n = mbedtls_ssl_get_session(this->ssl(), &m_session);
    if (n) {
      ZeLOG(Error, ([n](auto &s) {
	s << "mbedtls_ssl_get_session(): " << strerror_(n);
      }));
      return;
    }
    m_saved = true;
  }

  void load_() {
    if (!m_saved) return;
    int n = mbedtls_ssl_set_session(this->ssl(), &m_session);
    if (n) {
      ZeLOG(Warning, ([n](auto &s) {
	s << "mbedtls_ssl_set_session(): " << strerror_(n);
      }));
      return;
    }
  }

  void connected__() {
    while (this->handshake());
  }

  void verify_() {
    uint32_t flags;
    if (flags = mbedtls_ssl_get_verify_result(this->ssl())) {
      ZeLOG(Error,
	  ([hostname = m_server, flags](auto &s) {
	s << "server \"" << hostname <<
	  "\": X.509 cert verification failure: ";
	static const char *errors[] = {
	  "validity has expired",
	  "revoked (is on a CRL)",
	  "CN does not match with the expected CN",
	  "not correctly signed by the trusted CA",
	  "CRL is not correctly signed by the trusted CA",
	  "CRL is expired",
	  "certificate missing",
	  "certificate verification skipped",
	  "unspecified/other",
	  "validity starts in the future",
	  "CRL is from the future",
	  "usage does not match the keyUsage extension",
	  "usage does not match the extendedKeyUsage extension",
	  "usage does not match the nsCertType extension",
	  "signed with an bad hash",
	  "signed with an bad PK alg (e.g. RSA vs ECDSA)",
	  "signed with bad key (e.g. bad curve, RSA too short)",
	  "CRL signed with an bad hash",
	  "CRL signed with bad PK alg (e.g. RSA vs ECDSA)",
	  "CRL signed with bad key (e.g. bad curve, RSA too short)"
	};
	{
	  bool comma = false;
	  constexpr unsigned n = sizeof(errors) / sizeof(errors[0]);
	  for (unsigned i = 0; i < n; i++)
	    if (flags & (1U<<i)) {
	      if (comma) s << ", ";
	      comma = true;
	      s << errors[i];
	    }
	}
      }));
      this->disconnect_(false);
    }
  }

public:
  void connectFailed(bool transient) {
    unsigned reconnFreq = app()->reconnFreq();
    if (transient && reconnFreq > 0)
      app()->run(
	  ZmFn<>{this, [](CliLink *link) { link->connect_(); }},
	  Zm::now(reconnFreq), ZmScheduler::Update, &m_reconnTimer);
    else
      ZeLOG(Error, "connect failed");
  }

private:
  mbedtls_ssl_session	m_session;
  ZmScheduler::Timer	m_reconnTimer;
  bool			m_saved = false;
  Host			m_server;
  uint16_t		m_port;
};

template <typename App, typename Impl, typename IOBufAlloc = IOBufAlloc<>>
class SrvLink : public SrvLink_<App, Impl, IOBufAlloc, SrvCxn<Impl>> {
public:
  using Cxn = SrvCxn<Impl>;
  using Base = SrvLink_<App, Impl, IOBufAlloc, Cxn>;

  using Base::impl;

friend Base;

  SrvLink(App *app) : Base(app) { }

private:
  const char *server() { return nullptr; } // unused

  void save_() { } // unused
  void load_() { } // unused
  void connected__() { } // unused
  void verify_() { } // unused
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
    mbedtls_platform_setup(&m_platform);
    psa_crypto_init();
    mbedtls_x509_crt_init(&m_cacert);
    mbedtls_ssl_config_init(&m_conf);
  }
  ~Engine() {
    mbedtls_ssl_config_free(&m_conf);
    mbedtls_x509_crt_free(&m_cacert);
    mbedtls_platform_teardown(&m_platform);
  }

  template <typename L>
  bool init(ZiMultiplex *mx, ZuCSpan thread, L l) {
    m_mx = mx;
    if (!(m_thread = m_mx->sid(thread))) {
      ZeLOG(Error, ([thread = LogMsg{thread}](auto &s) {
	s << "invalid Rx thread ID \"" << thread << '"';
      }));
      return false;
    }
    if (!m_mx->running()) {
      ZeLOG(Error, "multiplexer not running");
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
#ifdef ZDEBUG
    if (auto debug = ::getenv("ZTLS_DEBUG"))
      mbedtls_debug_set_threshold(ZuBox<int>{debug});
#endif
#ifdef Ztls_VHEAP
    mbedtls_platform_set_calloc_free(
      [](size_t n, size_t size) -> void * {
	n *= size;
	auto ptr = VHeap::valloc(n);
	memset(ptr, 0, n); // very tempting to remove this
	return ptr;
      },
      [](void *ptr) { VHeap::vfree(ptr); });
#endif
    mbedtls_ssl_conf_dbg(&m_conf,
      [](void *, int level, const char *file, int line, const char *msg_) {
	int sev;
	switch (level) {
	  case 0: sev = Ze::Error;
	  case 1: sev = Ze::Warning;
	  case 2: sev = Ze::Info;
	  case 3: sev = Ze::Info;
	  default: sev = Ze::Debug;
	}
#ifndef ZDEBUG
	if (sev > Ze::Debug)
#endif
	{
	  // this code path should not be latency-sensitive; furthermore
	  // the caller allocates message_ on the stack, so it needs to be
	  // copied on to the heap regardless
	  LogMsg msg{msg_};
	  msg.chomp();
	  ZeLogEvent(ZeEvent(sev, file, line, "", [msg = ZuMv(msg)](auto &s) {
	    s << msg;
	  }));
	}
      }, nullptr);
    if (!Random::init()) {
      ZeLOG(Error, "mbedtls_ctr_drbg_seed() failed");
      return false;
    }
    mbedtls_ssl_conf_rng(&m_conf, mbedtls_ctr_drbg_random, ctr_drbg());
    if (!l()) return false;
    mbedtls_ssl_conf_renegotiation(&m_conf, MBEDTLS_SSL_RENEGOTIATION_ENABLED);
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
  mbedtls_ssl_config *conf() { return &m_conf; }

  // Arch/Ubuntu/Debian/SLES - /etc/ssl/certs
  // Fedora/CentOS/RHEL - /etc/pki/tls/certs
  // Android - /system/etc/security/cacerts
  // FreeBSD - /usr/local/share/certs
  // NetBSD - /etc/openssl/certs
  // AIX - /var/ssl/certs
  // Windows - ROOT certificate store (using Cert* API)

  bool loadCA(const char *path) {
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
	ZeLOG(Error, ([e = ZeLastError](auto &s) {
	  s << "CertOpenSystemStore(nullptr, \"ROOT\") failed: " << e;
	}));
	return false;
      }

      PCCERT_CONTEXT context = nullptr;
      while (context = CertEnumCertificatesInStore(store, context)) {
	int n = mbedtls_x509_crt_parse(
	  m_cacert, context->pbCertEncoded, context->cbCertEncoded);
	if (n < 0) {
	  ZeLOG(Error, ([n](auto &s) {
	    s << "mbedtls_x509_crt_parse(): " << strerror_(n);
	  }));
	  CertFreeCertificateContext(context);
	  return false;
	}
      }

      CertCloseStore(store, 0);
      return true;
#endif
    }
    int n;
    const char *function;
    if (ZiFile::isdir(path)) {
      function = "mbedtls_x509_crt_parse_path";
      n = mbedtls_x509_crt_parse_path(&m_cacert, path);
    } else {
      function = "mbedtls_x509_crt_parse_file";
      n = mbedtls_x509_crt_parse_file(&m_cacert, path);
    }
    if (n < 0) {
      ZeLOG(Error, ([function, n](auto &s) {
	s << function << "(): " << strerror_(n);
      }));
      return false;
    }
    mbedtls_ssl_conf_ca_chain(&m_conf, &m_cacert, nullptr);
    return true;
  }

private:
  ZiMultiplex			*m_mx = nullptr;
  unsigned			m_thread = 0;

  mbedtls_platform_context	m_platform;
  mbedtls_x509_crt		m_cacert;
  mbedtls_ssl_config		m_conf;
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
      // +ve - the number of bytes processed
      // 0   - more data needed - continue appending to Rx output buffer
      // -ve - disconnect, abandon any remaining Rx dats
      int process(ZuSpan<uint8_t>); // process received data

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

  Client() {
    mbedtls_x509_crt_init(&m_cert);
    mbedtls_pk_init(&m_key);
  }
  ~Client() {
    mbedtls_pk_free(&m_key);
    mbedtls_x509_crt_free(&m_cert);
  }

  // specify certPath and keyPath for mTLS
  bool init(ZiMultiplex *mx, ZuCSpan thread, const char **alpn,
      const char *caPath = nullptr,
      const char *certPath = nullptr,
      const char *keyPath = nullptr) {
    return Base::init(mx, thread, [
      this, caPath, alpn, certPath, keyPath
    ]() -> bool {
      mbedtls_ssl_config_defaults(this->conf(),
	  MBEDTLS_SSL_IS_CLIENT,
	  MBEDTLS_SSL_TRANSPORT_STREAM,
	  MBEDTLS_SSL_PRESET_DEFAULT);

      mbedtls_ssl_conf_session_tickets(this->conf(),
	  MBEDTLS_SSL_SESSION_TICKETS_ENABLED);

      mbedtls_ssl_conf_authmode(this->conf(), MBEDTLS_SSL_VERIFY_REQUIRED);
      if (!this->loadCA(caPath)) return false;
      if (alpn) mbedtls_ssl_conf_alpn_protocols(this->conf(), alpn);

      if (certPath && keyPath) {
	if (mbedtls_x509_crt_parse_file(&m_cert, certPath)) return false;
	if (mbedtls_pk_parse_keyfile(&m_key, keyPath, "",
	    mbedtls_ctr_drbg_random, this->ctr_drbg())) return false;
	if (mbedtls_ssl_conf_own_cert(this->conf(), &m_cert, &m_key))
	  return false;
      }
      return true;
    });
  }

  void final() { Base::final(); }

protected:
  unsigned reconnFreq() const { return 0; } // default

private:
  mbedtls_x509_crt		m_cert;
  mbedtls_pk_context		m_key;
};

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
      // +ve - the number of bytes processed
      // 0   - more data needed - continue appending to Rx output buffer
      // -ve - disconnect, abandon any remaining Rx dats
      int process(ZuSpan<uint8_t>); // process received data
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

  Server() {
    mbedtls_x509_crt_init(&m_cert);
    mbedtls_pk_init(&m_key);
    mbedtls_ssl_cache_init(&m_cache);
    mbedtls_ssl_ticket_init(&m_ticket_ctx);
  }
  ~Server() {
    mbedtls_ssl_ticket_free(&m_ticket_ctx);
    mbedtls_ssl_cache_free(&m_cache);
    mbedtls_pk_free(&m_key);
    mbedtls_x509_crt_free(&m_cert);
  }

  bool init(ZiMultiplex *mx, ZuCSpan thread, const char **alpn,
      const char *caPath, const char *certPath, const char *keyPath,
      bool mTLS = false, int cacheMax = -1, int cacheTimeout = -1) {
    return Base::init(mx, thread, [
      this, alpn, caPath, certPath, keyPath, mTLS, cacheMax, cacheTimeout
    ]() -> bool {
      mbedtls_ssl_config_defaults(this->conf(),
	  MBEDTLS_SSL_IS_SERVER,
	  MBEDTLS_SSL_TRANSPORT_STREAM,
	  MBEDTLS_SSL_PRESET_DEFAULT);

      if (cacheMax >= 0) // library default is 50
	mbedtls_ssl_cache_set_max_entries(&m_cache, cacheMax);
      if (cacheTimeout >= 0) // library default is 86400, i.e. one day
	mbedtls_ssl_cache_set_timeout(&m_cache, cacheTimeout);

      mbedtls_ssl_conf_session_cache(this->conf(), &m_cache,
	  mbedtls_ssl_cache_get, mbedtls_ssl_cache_set);

      if (mbedtls_ssl_ticket_setup(&m_ticket_ctx,
	    mbedtls_ctr_drbg_random, this->ctr_drbg(),
	    MBEDTLS_CIPHER_AES_256_GCM,
	    cacheTimeout < 0 ? 86400 : cacheTimeout)) return false;
      mbedtls_ssl_conf_session_tickets_cb(this->conf(),
	  mbedtls_ssl_ticket_write,
	  mbedtls_ssl_ticket_parse,
	  &m_ticket_ctx);

      mbedtls_ssl_conf_authmode(this->conf(),
	  mTLS ? MBEDTLS_SSL_VERIFY_REQUIRED : MBEDTLS_SSL_VERIFY_NONE);
      if (!this->loadCA(caPath)) return false;
      if (alpn) mbedtls_ssl_conf_alpn_protocols(this->conf(), alpn);

      if (mbedtls_x509_crt_parse_file(&m_cert, certPath)) return false;
      if (mbedtls_pk_parse_keyfile(&m_key, keyPath, "",
	    mbedtls_ctr_drbg_random, this->ctr_drbg())) return false;
      if (mbedtls_ssl_conf_own_cert(this->conf(), &m_cert, &m_key))
	return false;
      return true;
    });
  }

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
    ZeLOG(Info, ([info](auto &s) {
      s << "listening(" << info.ip << ':' << info.port << ')';
    }));
  }
  void listenFailed(bool transient) { // default
    unsigned rebindFreq = app()->rebindFreq();
    if (transient && rebindFreq > 0)
      app()->run([this]() { listen(); },
	  Zm::now(rebindFreq), ZmScheduler::Update, &m_rebindTimer);
    else
      ZeLOG(Error, ([transient](auto &s) {
	s << "listen() failed " << (transient ? "(transient)" : "");
      }));
  }

private:
  mbedtls_x509_crt		m_cert;
  mbedtls_pk_context		m_key;
  mbedtls_ssl_cache_context	m_cache;
  mbedtls_ssl_ticket_context	m_ticket_ctx;
  ZmScheduler::Timer		m_rebindTimer;
  bool				m_listening = false;
};

}

#endif /* Ztls_HH */
