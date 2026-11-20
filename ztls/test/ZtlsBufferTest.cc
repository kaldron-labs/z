//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <arpa/inet.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <zlib/ZuLib.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmSingleton.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/Ztls.hh>
#include <zlib/ZtlsBuf.hh>
#include <zlib/ZtlsCredentials.hh>
#include <zlib/ZtlsEngineOpenSSL.hh>
#include <zlib/ZtlsPK.hh>

#include "ZiTestResidue.hh"
#include "ZtlsTestHandshake.hh"
#include "ZtlsTestPorts.hh"
#include <iostream>

using namespace ZuTestUtil;

#define Ztls_CheckRT_1(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))

namespace {

constexpr unsigned MaxSize = (1u << 20);
constexpr unsigned JumboPayloadSize = 64u * 1024u;
constexpr unsigned SmallPayloadSize = 4u * 1024u;
constexpr unsigned TimeoutSeconds = 15;

struct TempDir {
  Zi::Path	path;
  ZtString<>	certPath;
  ZtString<>	certDERPath;
  ZtString<>	keyPath;
  ZtString<>	extPath;

  bool init()
  {
    path = ZiTestResidue::dir("fixtures");

    certPath << path << "/cert.pem";
    certDERPath << path << "/cert.der";
    keyPath << path << "/key.pem";
    extPath << path << "/cert.ext";

    ZtString<> cmd;
    cmd << "openssl req -x509 -newkey rsa:2048 -nodes -days 1 "
	   "-subj /CN=localhost "
	   "-addext basicConstraints=critical,CA:TRUE "
	   "-addext keyUsage=critical,digitalSignature,keyEncipherment,keyCertSign "
	   "-addext subjectAltName=DNS:localhost,IP:127.0.0.1,IP:::1 "
	   "-keyout "
	<< keyPath << ' ' << "-out " << certPath << " >/dev/null 2>&1";
    if (!systemOK(system(cmd.data()))) return false;
    cmd = {};
    cmd << "openssl x509 -in " << certPath << " -outform DER -out " << certDERPath
	<< " >/dev/null 2>&1";
    return systemOK(system(cmd.data()));
  }

  ZtString<> pathOf(ZuCSpan name) const
  {
    ZtString<> s;
    s << path << '/' << name;
    return s;
  }

  static bool systemOK(int status)
  { return status != -1 && WIFEXITED(status) && !WEXITSTATUS(status); }
};

struct LogCapture {
  ZmAtomic<unsigned>	errors{0};

  void reset()
  {
    ZiLog::stop(); // drain errors from the preceding negative case
    errors.store_(0);
    ZiLog::start();
  }

  static bool contains(ZuCSpan haystack, ZuCSpan needle)
  {
    if (!needle.length()) return true;
    if (needle.length() > haystack.length()) return false;
    for (unsigned i = 0, n = haystack.length() - needle.length(); i <= n; ++i)
      if (!memcmp(haystack.data() + i, needle.data(), needle.length())) return true;
    return false;
  }

  void onLog(ZeLogBuf &buf, const ZeEventInfo &info)
  {
    if (info.severity >= Ze::Error) {
      errors.xchAdd(1);
      log("TLS error: ", ZuCSpan{buf});
    }
  }
};

struct TestState {
  ZmSemaphore		done;
  ZmSemaphore		listening;
  unsigned		target = 2;
  ZiIP			ip;
  unsigned		port = 0;
  ZtArray<uint8_t>	clientPayload;
  ZtArray<uint8_t>	serverPayload;
  bool			checkALPN = false;
  bool			clientKeyUpdate = false;
  bool			serverKeyUpdate = false;
  bool			serverDisconnectAfterSend = false;
  bool			multi = false;
  bool			expectConnectFailure = false;
  unsigned		txHeadroom = 0;
  unsigned		txTailroom = 0;
  ZmAtomic<unsigned>	done_count{0};
  ZmAtomic<unsigned>	errors{0};
  ZmAtomic<unsigned>	client_rx_bytes{0};
  ZmAtomic<unsigned>	server_rx_bytes{0};
  ZmAtomic<unsigned>	client_connected{0};
  ZmAtomic<unsigned>	server_connected{0};
  ZmAtomic<unsigned>	client_tlsver{0};
  ZmAtomic<unsigned>	server_tlsver{0};
  ZmAtomic<unsigned>	client_cipher{0};
  ZmAtomic<unsigned>	server_cipher{0};
  ZmAtomic<unsigned>	client_closed{0};
  ZmAtomic<unsigned>	server_replied{0};
  ZmAtomic<unsigned>	client_tx_protected{0};
  ZmAtomic<unsigned>	server_tx_protected{0};
  ZmAtomic<unsigned>	client_peer_closed{0};
  ZmAtomic<unsigned>	connect_failed{0};
  ZmAtomic<unsigned>	connect_failed_on_rx{0};
  ZmAtomic<unsigned>	connect_failed_transient{0};
  ZmAtomic<uint64_t>	client_wire_count{0};
  ZmAtomic<uint64_t>	client_wire_bytes{0};
  ZmAtomic<uint64_t>	server_wire_count{0};
  ZmAtomic<uint64_t>	server_wire_bytes{0};
  Ztc::QueueTelemetry	client_rx;
  Ztc::QueueTelemetry	client_tx;
  Ztc::QueueTelemetry	server_rx;
  Ztc::QueueTelemetry	server_tx;
  const char		*error_msg = nullptr;

  void done_one()
  {
    if (done_count.xchAdd(1) < target) done.post();
  }
  void fail(const char *msg)
  {
    if (!errors.xch(1)) error_msg = msg;
    for (unsigned i = 0; i < target; ++i) done.post();
  }
};

template <typename Link>
void queue_telemetry(Link &link, Ztc::QueueTelemetry &rx, Ztc::QueueTelemetry &tx)
{
  auto	   linkKey = link.telKey();
  unsigned count = 0;
  unsigned allQueues = link.allQueues([&linkKey, &rx, &tx, &count](Ztc::Queue *queue)
  {
    auto key = queue->telKey();
    Ztls_CheckRT_1(
      key.template p<0>() == linkKey.template p<0>(), "telemetry queue owner ID mismatch");
    Ztls_CheckRT_1(key.template p<1>() == linkKey.template p<1>(), "telemetry queue ID mismatch");
    Ztls_CheckRT_1(
      key.template p<2>() == (count ? Ztc::QueueType::Tx : Ztc::QueueType::Rx),
      "telemetry queue iteration order mismatch");

    Ztc::QueueTelemetry data;
    data.ownerID = ZuID{} << "dirtyOwner";
    data.id = ZuID{} << "dirty";
    data.inBytes = data.outBytes = data.inCount = data.outCount = 1;
    data.count = data.size = data.full = 1;
    data.type = Ztc::QueueType::Thread;
    queue->telemetry(data);
    Ztls_CheckRT_1(data.ownerID == key.template p<0>(), "queue telemetry owner ID mismatch");
    Ztls_CheckRT_1(data.id == key.template p<1>(), "queue telemetry ID mismatch");
    Ztls_CheckRT_1(data.type == key.template p<2>(), "queue telemetry type mismatch");
    Ztls_CheckRT_1(!data.size && !data.full, "queue telemetry fields were not overwritten");
    if (data.type == Ztc::QueueType::Rx) {
      Ztls_CheckRT_1(
	!data.outBytes && !data.outCount, "Rx telemetry output fields were not overwritten");
      rx = data;
    } else {
      tx = data;
    }
    ++count;
  });
  Ztls_CheckRT_1(allQueues == 2, "allQueues returned incorrect count");
  Ztls_CheckRT_1(count == allQueues, "allQueues did not enumerate returned count");
  unsigned repeat = 0;
  link.allQueues([&repeat](Ztc::Queue *queue)
  {
    auto key = queue->telKey();
    Ztls_CheckRT_1(
      key.template p<2>() == (repeat++ ? Ztc::QueueType::Tx : Ztc::QueueType::Rx),
      "allQueues queue order is not stable");
  });
  Ztls_CheckRT_1(
    rx.ownerID == linkKey.template p<0>() && tx.ownerID == linkKey.template p<0>(),
    "queue owner ID mismatch");
  Ztls_CheckRT_1(
    rx.id == linkKey.template p<1>() && tx.id == linkKey.template p<1>(), "queue ID mismatch");
  Ztls_CheckRT_1(rx.type != tx.type, "Rx and Tx queue types do not discriminate the keys");
}

template <typename Link> void check_queues(Link &link)
{
  Ztc::QueueTelemetry rx;
  Ztc::QueueTelemetry tx;
  queue_telemetry(link, rx, tx);
}

void fill_payload(ZtArray<uint8_t> &payload, unsigned len, uint8_t seed)
{
  payload.length(len);
  for (unsigned i = 0; i < len; ++i) payload[i] = uint8_t(seed + (i * 31u) + (i >> 3));
}

bool write_bytes(const char *path, const ZtArray<uint8_t> &data)
{
  ZiFile file{path, ZiFile::Write | ZiFile::GC};
  if (!file) return false;
  return !data.length() || file.write(data.data(), data.length()) == Zi::OK;
}

template <typename A> bool read_bytes(const char *path, A &data)
{
  ZiFile file{path, ZiFile::ReadOnly | ZiFile::GC};
  if (!file) return false;
  auto len = file.size();
  if (len < 0 || uint64_t(len) > INT_MAX) return false;
  data.length(unsigned(len));
  return !len || file.read(data.data(), unsigned(len)) == int(len);
}

struct CertIssue {
  ZuCSpan	csr;
  ZuCSpan	cert;
  ZuCSpan	ca;
  ZuCSpan	caKey;
  ZuCSpan	validity;
  ZuCSpan	profile;
  unsigned	serial = 1;
  bool		der = true;
};

bool issue_cert(const TempDir &temp, const CertIssue &p)
{
  ZtString<> cmd;
  cmd << "openssl x509 -req -inform DER -in " << p.csr << " -CA " << p.ca << " -CAkey " << p.caKey
      << " -set_serial " << p.serial << ' ' << p.validity;
  if (p.profile) cmd << " -extfile " << temp.extPath << " -extensions " << p.profile;
  if (p.der) cmd << " -outform DER";
  cmd << " -out " << p.cert << " >/dev/null 2>&1";
  return TempDir::systemOK(system(cmd.data()));
}

bool gen_csr(ZuCSpan key, ZuCSpan csr, ZuCSpan subject)
{
  ZtString<> cmd;
  cmd << "openssl req -new -newkey rsa:2048 -nodes -subj /CN=" << subject << " -keyout " << key
      << " -outform DER -out " << csr << " >/dev/null 2>&1";
  return TempDir::systemOK(system(cmd.data()));
}

bool cert_to_der(ZuCSpan cert, ZuCSpan der)
{
  ZtString<> cmd;
  cmd << "openssl x509 -in " << cert << " -outform DER -out " << der << " >/dev/null 2>&1";
  return TempDir::systemOK(system(cmd.data()));
}

bool consume_payload(
  TestState &state, ZmAtomic<unsigned> &offset, const ZtArray<uint8_t> &expected,
  ZuSpan<uint8_t> span, const char *msg)
{
  unsigned len = unsigned(span.length());
  unsigned off = offset.xchAdd(len);
  if (off + len > expected.length()) {
    state.fail(msg);
    return false;
  }
  if (len && memcmp(span.data(), expected.data() + off, len)) {
    state.fail(msg);
    return false;
  }
  return off + len >= expected.length();
}

int consume_payload_frame(
  TestState &state, ZmAtomic<unsigned> &offset, const ZtArray<uint8_t> &expected,
  Ztls::RxStream &rx, const char *msg, bool &complete)
{
  unsigned off = offset.load_();
  if (ZuUnlikely(off >= expected.length())) {
    state.fail(msg);
    return -1;
  }

  uint64_t need = expected.length() - off;
  uint64_t seen = 0;
  int64_t  consumed = rx.consume(
    [&seen, need](ZuSpan<uint8_t> span) -> int64_t
  {
    seen += span.length();
    if (seen < need) return 0;
    return span.length() - (seen - need);
  }, [&state, &offset, &expected, msg, &complete](ZuSpan<uint8_t> span)
  { complete = consume_payload(state, offset, expected, span, msg); });

  if (ZuUnlikely(consumed < 0)) return -1;
  return consumed ? 1 : 0;
}

bool wait_for(ZmSemaphore &sem) { return sem.timedwait(Zm::now(TimeoutSeconds)) == 0; }

bool wait_done(TestState &state)
{
  for (unsigned i = 0; i < state.target; ++i)
    if (!wait_for(state.done)) return false;
  return true;
}

uint16_t reserve_loopback_port(ZiIP ip = ZiIP{"127.0.0.1"})
{
  static uint16_t nextPort = ZtlsTestPort::BufferTest;
  uint16_t	  port = nextPort++;
  int		  s = ::socket(ip.v6() ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s < 0) return 0;
  int one = 1;
  ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  ZiSockAddr addr{ip, port};
  if (::bind(s, addr.sa(), addr.len())) port = 0;
  ::close(s);
  return port;
}

ZiMxParams mx_params()
{
  return ZiMxParams()
    .scheduler(
      [](auto &s)
  {
    s.nThreads(4).thread(1, [](auto &t) {
      t.isolated(1);
    }).thread(2, [](auto &t) {
      t.isolated(1);
    }).thread(3, [](auto &t) {
      t.isolated(1);
    }).thread(4, [](auto &t) { t.isolated(1); });
  })
    .rxThread(1)
    .txThread(2);
}

template <typename Lower> struct ReserveLayer : public ZiTxLayer<ReserveLayer<Lower>, Lower> {
  using Base = ZiTxLayer<ReserveLayer<Lower>, Lower>;

  ReserveLayer(Lower &lower, unsigned headRoom, unsigned tailRoom)
  :
    Base{lower, headRoom, tailRoom}
  { }

  ~ReserveLayer() { this->flush(); }

  bool prepareBuf_(ZiIOBuf *, bool) { return true; }
};

template <typename Link> void send_payload(Link *link, const ZtArray<uint8_t> &payload)
{
  if (!payload.length()) return;
  auto	       tx = link->txStream();
  auto	      &state = link->app()->state;
  unsigned     head1 = state.txHeadroom >> 1;
  unsigned     tail1 = state.txTailroom >> 1;
  ReserveLayer first{tx, head1, tail1};
  ReserveLayer second{first, state.txHeadroom - head1, state.txTailroom - tail1};
  second.append(payload.data(), unsigned(payload.length()));
  second << Zi::flush();
}

template <typename State> struct BaseClient : public Ztls::Client<BaseClient<State>> {
  using RxBufAlloc = Ztls::RxBufAlloc<Ztls_::RxRecordCapacity, MaxSize>;
  using TxBufAlloc = Ztls::TxBufAlloc<>;
  using Base = Ztls::Client<BaseClient<State>>;

  Zi::IOBufAllocator	*rxBufAllocator = nullptr;
  Zi::IOBufAllocator	*txBufAllocator = nullptr;

  template <typename Params> Params buffers(Params params)
  {
    if (!params.multiplex()) return params;
    if (!params.rxAllocator()) {
      rxBufAllocator = Link::rxAllocator(params.multiplex());
      params.rxAllocator(rxBufAllocator);
    }
    if (!params.txAllocator()) {
      txBufAllocator = Link::txAllocator(params.multiplex());
      params.txAllocator(txBufAllocator);
    }
    if (!params.txPacketAllocator()) {
      params.txPacketAllocator(params.multiplex()
	  ->template allocator<
	    Zi::IOBufType::TxTCP, Ztls::TxPacket, 1514, ZiIOBuf_DefltAlign,
	    ZuStringT<"Ztls.Test.Packet">>(1024));
    }
    return params;
  }

  template <typename Params> bool init(Params params)
  { return Base::init(buffers(ZuMv(params))); }

  struct Link : ZmNHeap<Link>, Ztls::CliLink<BaseClient, Link, RxBufAlloc, TxBufAlloc> {
  friend inline ZuStringT<"Ztls.BufferTest.CliLink"> ZmNHeapID(Link *);
    using BaseLink = Ztls::CliLink<BaseClient, Link, RxBufAlloc, TxBufAlloc>;
    Link(BaseClient *app) : BaseLink{app, ZuID{"client"}} { }
    ZmAtomic<unsigned>	rx_offset{0};

    void connected(Ztls::Connected info)
    {
      auto &state = this->app()->state;
      if (state.checkALPN && info.alpn != "h2") state.fail("server ALPN preference lost");
      check_queues(*this);
      state.client_connected = 1;
      state.client_tlsver = unsigned(info.version);
      state.client_cipher = this->tlsInfo().cipherID;
      if (state.clientKeyUpdate && !this->updateKey_0(true)) {
	state.fail("client key update failed");
	return;
      }
      send_payload(this, state.clientPayload);
    }
    void disconnected(bool peer)
    {
      auto &state = this->app()->state;
      if (!this->txQueue.empty_())
	state.fail("client disconnect completed before I/O Tx drained");
      state.client_peer_closed = peer;
      state.done_one();
    }
    void connectFailed(bool transient)
    {
      auto  app = this->app();
      auto &state = app->state;
      if (!state.expectConnectFailure) {
	state.fail("connect failed");
	return;
      }
      state.connect_failed_on_rx = app->rxInvoked();
      state.connect_failed_transient = transient;
      state.connect_failed.xchAdd(1);
      state.done.post();
    }
    void txProtected()
    {
      auto app = this->app();
      auto tid = Zm::getTID();
      if (
	!app->mx()->invoked_(tid, app->txThread()) || app->mx()->invoked_(tid, app->rxThread())) {
	app->state.fail("client record protection ran outside TLS Tx thread");
	return;
      }
      app->state.client_tx_protected.xchAdd(1);
    }
    void sent(ZmRef<ZiIOBuf> buf, bool ok)
    {
      auto &state = this->app()->state;
      BaseLink::sent(buf, ok);
      if (buf->allocator() != this->txPacketAllocator() ||
	buf->allocator() == this->txAllocator() || buf->skip)
	state.fail("TLS wire output lost its separate kernel packet backing");
      if (!ok) {
	state.fail("client TLS wire send failed");
	return;
      }
      state.client_wire_count.xchAdd(1);
      state.client_wire_bytes.xchAdd(buf->length);
    }
    int process(Ztls::RxStream &rx)
    {
      auto &state = this->app()->state;
      while (!rx.empty()) {
	bool complete = false;
	int  consumed = consume_payload_frame(
	  state, state.multi ? rx_offset : state.client_rx_bytes, state.serverPayload, rx,
	  "client received unexpected payload", complete);
	if (ZuUnlikely(consumed < 0)) return -1;
	if (!consumed) return 0;
	if (complete && (state.multi || !state.client_closed.xch(1))) {
	  if (!state.multi) { queue_telemetry(*this, state.client_rx, state.client_tx); }
	  if (!state.serverDisconnectAfterSend) this->disconnect_();
	}
      }
      return 1;
    }
  };

  BaseClient(State &state_) : state(state_) { }

  void override_cipher_suites(Ztls_::SuiteSpan suites) { this->ctx()->cipherSuites = suites; }

  State			&state;
};

template <typename State> struct BaseServer : public Ztls::Server<BaseServer<State>> {
  using RxBufAlloc = Ztls::RxBufAlloc<Ztls_::RxRecordCapacity, MaxSize>;
  using TxBufAlloc = Ztls::TxBufAlloc<>;
  using Base = Ztls::Server<BaseServer<State>>;

  Zi::IOBufAllocator	*rxBufAllocator = nullptr;
  Zi::IOBufAllocator	*txBufAllocator = nullptr;

  template <typename Params> Params buffers(Params params)
  {
    if (!params.multiplex()) return params;
    if (!params.rxAllocator()) {
      rxBufAllocator = Link::rxAllocator(params.multiplex());
      params.rxAllocator(rxBufAllocator);
    }
    if (!params.txAllocator()) {
      txBufAllocator = Link::txAllocator(params.multiplex());
      params.txAllocator(txBufAllocator);
    }
    if (!params.txPacketAllocator()) {
      params.txPacketAllocator(params.multiplex()
	  ->template allocator<
	    Zi::IOBufType::TxTCP, Ztls::TxPacket, 1514, ZiIOBuf_DefltAlign,
	    ZuStringT<"Ztls.Test.Packet">>(1024));
    }
    return params;
  }

  template <typename Params> bool init(Params params)
  { return Base::init(buffers(ZuMv(params))); }

  struct Link : ZmNHeap<Link>, Ztls::SrvLink<BaseServer, Link, RxBufAlloc, TxBufAlloc> {
  friend inline ZuStringT<"Ztls.BufferTest.SrvLink"> ZmNHeapID(Link *);
    using BaseLink = Ztls::SrvLink<BaseServer, Link, RxBufAlloc, TxBufAlloc>;
    Link(BaseServer *app) : BaseLink{app} { }
    ZmAtomic<unsigned>	rx_offset{0};

    void connected(Ztls::Connected info)
    {
      auto &state = this->app()->state;
      if (state.checkALPN && info.alpn != "h2") state.fail("server ALPN preference lost");
      check_queues(*this);
      state.server_connected = 1;
      state.server_tlsver = unsigned(info.version);
      state.server_cipher = this->tlsInfo().cipherID;
    }
    void disconnected(bool)
    {
      auto &state = this->app()->state;
      if (!this->txQueue.empty_())
	state.fail("server disconnect completed before I/O Tx drained");
      if (!state.multi && state.server_connected.load_())
	queue_telemetry(*this, state.server_rx, state.server_tx);
      state.done_one();
    }
    void txProtected()
    {
      auto app = this->app();
      auto tid = Zm::getTID();
      if (
	!app->mx()->invoked_(tid, app->txThread()) || app->mx()->invoked_(tid, app->rxThread())) {
	app->state.fail("server record protection ran outside TLS Tx thread");
	return;
      }
      app->state.server_tx_protected.xchAdd(1);
    }
    void sent(ZmRef<ZiIOBuf> buf, bool ok)
    {
      auto &state = this->app()->state;
      BaseLink::sent(buf, ok);
      if (buf->allocator() != this->txPacketAllocator() ||
	buf->allocator() == this->txAllocator() || buf->skip)
	state.fail("TLS wire output lost its separate kernel packet backing");
      if (!ok) {
	state.fail("server TLS wire send failed");
	return;
      }
      state.server_wire_count.xchAdd(1);
      state.server_wire_bytes.xchAdd(buf->length);
    }
    int process(Ztls::RxStream &rx)
    {
      auto &state = this->app()->state;
      while (!rx.empty()) {
	bool complete = false;
	int  consumed = consume_payload_frame(
	  state, state.multi ? rx_offset : state.server_rx_bytes, state.clientPayload, rx,
	  "server received unexpected payload", complete);
	if (ZuUnlikely(consumed < 0)) return -1;
	if (!consumed) return 0;
	if (complete && (state.multi || !state.server_replied.xch(1))) {
	  if (state.serverKeyUpdate && !this->updateKey_0(true)) {
	    state.fail("server key update failed");
	    return -1;
	  }
	  send_payload(this, state.serverPayload);
	  if (state.serverDisconnectAfterSend) this->disconnect_();
	}
      }
      return 1;
    }
  };

  using Cxn = Link::Cxn;
  Cxn *accepted(const ZiCxnInfo &ci) { return new Cxn(new Link(this), ci); }

  BaseServer(State &state_, ZiIP ip_) : state(state_), ip(ip_) { }

  ZiIP localIP() const { return ip; }
  unsigned localPort() const { return state.port; }

  void listening(const ZiListenInfo &info)
  {
    (void)info;
    state.listening.post();
  }
  void listenFailed(bool) { state.fail("listen failed"); }

  void override_cipher_suites(Ztls_::SuiteSpan suites) { this->ctx()->cipherSuites = suites; }

  State			&state;
  ZiIP			ip;
};

void run_in_process(
  TempDir &temp, LogCapture &capture, unsigned clientLen, unsigned serverLen,
  bool clientKeyUpdate, bool serverKeyUpdate = false, bool serverDisconnectAfterSend = false,
  ZiIP ip = ZiIP{"127.0.0.1"}, const char *connectIP = "127.0.0.1",
  Ztls_::SuiteSpan cipherSuites = {}, unsigned expectedCipher = 0, unsigned txHeadroom = 0,
  unsigned txTailroom = 0, ZmRef<Ztls::PK::AnyPK> clientKey = {}, Ztls::Certs clientCerts = {},
  Ztls::Certs caCerts = {})
{
  capture.reset();

  TestState state;
  state.target = 2;
  state.ip = ip;
  state.port = reserve_loopback_port(ip);
  state.clientKeyUpdate = clientKeyUpdate;
  state.serverKeyUpdate = serverKeyUpdate;
  state.serverDisconnectAfterSend = serverDisconnectAfterSend;
  state.txHeadroom = txHeadroom;
  state.txTailroom = txTailroom;
  fill_payload(state.clientPayload, clientLen, 0x11);
  fill_payload(state.serverPayload, serverLen, 0x63);
  if (ip.v6() && !state.port) {
    std::cout << "# IPv6 loopback unavailable; skipping TLS loopback\n";
    return;
  }
  Ztls_CheckRT_1(state.port, "failed to reserve loopback port");
  if (!state.port) return;

  BaseServer<TestState> server(state, state.ip);
  BaseClient<TestState> client(state);
  ZiMultiplex		mx(mx_params());
  Ztls::ServerParams	serverParams{&mx, "3", "4"};
  serverParams.certPath(temp.certPath.data()).keyPath(temp.keyPath.data());
  ZuCSpan protocols[] = {"h2", "http/1.1", "h2"};
  serverParams.alpn(protocols);
  state.checkALPN = true;
  if (clientKey) serverParams.mTLS(true).caPath(temp.certPath.data());
  bool serverOK = server.init(ZuMv(serverParams));
  Ztls_CheckRT_1(serverOK, "TLS server init failed");
  if (!serverOK) {
    mx.stop();
    return;
  }

  Ztls::ClientParams clientParams{&mx, "3", "4"};
  ZuCSpan	     offers[ZtlsALPNBuiltin + 1];
  unsigned	     n = ZtlsALPNBuiltin + unsigned(clientKeyUpdate);
  for (unsigned i = 0; i < n - 1; ++i) offers[i] = "http/1.1";
  offers[n - 1] = "h2";
  clientParams.alpn({offers, n});
  if (clientKey) {
    clientParams.key(ZuMv(clientKey)).certs(ZuMv(clientCerts)).caCerts(ZuMv(caCerts));
  } else {
    clientParams.caPath(temp.certPath.data());
  }
  bool clientOK = client.init(ZuMv(clientParams));
  Ztls_CheckRT_1(clientOK, "TLS client init failed");
  if (!clientOK) {
    mx.stop();
    return;
  }

  if (cipherSuites.length()) {
    server.override_cipher_suites(cipherSuites);
    client.override_cipher_suites(cipherSuites);
  }

  bool mxStarted = mx.start();
  Ztls_CheckRT_1(mxStarted, "ZiMultiplex start failed");
  if (!mxStarted) return;

  bool serverStarted = server.start();
  bool clientStarted = client.start();
  Ztls_CheckRT_1(serverStarted && clientStarted, "TLS hubs failed to start");
  if (!serverStarted || !clientStarted) {
    if (clientStarted) client.stop();
    if (serverStarted) server.stop();
    client.final();
    server.final();
    mx.stop();
    return;
  }

  server.listen();
  bool listening = wait_for(state.listening);
  Ztls_CheckRT_1(listening, "listen timed out");
  if (!listening) {
    mx.stop();
    return;
  }

  ZmRef<BaseClient<TestState>::Link> link = new BaseClient<TestState>::Link(&client);
  check_queues(*link);
  link->connect(connectIP, state.port);

  bool done = wait_done(state);
  Ztls_CheckRT_1(done, "TLS disconnect wait timed out");

  link = nullptr;
  mx.stopListening(state.ip, state.port);
  bool clientStopped = client.stop();
  bool serverStopped = server.stop();
  Ztls_CheckRT_1(clientStopped && serverStopped, "TLS hubs failed to stop");
  client.final();
  server.final();
  mx.stop();

  Ztls_CheckRT_1(!capture.errors.load_(), "unexpected error logs");
  Ztls_CheckRT_1(!state.errors.load_(), state.error_msg ? state.error_msg : "state error");
  Ztls_CheckRT_1(state.client_connected.load_(), "client did not connect");
  Ztls_CheckRT_1(state.server_connected.load_(), "server did not connect");
  Ztls_CheckRT_1(state.client_tlsver.load_() == 13, "client did not use TLS 1.3");
  Ztls_CheckRT_1(state.server_tlsver.load_() == 13, "server did not use TLS 1.3");
  Ztls_CheckRT_1(state.client_rx_bytes.load_() == serverLen, "client payload length mismatch");
  Ztls_CheckRT_1(state.server_rx_bytes.load_() == clientLen, "server payload length mismatch");
  Ztls_CheckRT_1(state.client_rx.inBytes == serverLen, "client plaintext Rx telemetry mismatch");
  Ztls_CheckRT_1(state.server_rx.inBytes == clientLen, "server plaintext Rx telemetry mismatch");
  Ztls_CheckRT_1(state.client_rx.inCount, "client plaintext Rx ingress count is zero");
  Ztls_CheckRT_1(state.server_rx.inCount, "server plaintext Rx ingress count is zero");
  Ztls_CheckRT_1(
    state.client_tx.inCount && state.client_tx.inBytes, "client serialized Tx ingress is zero");
  Ztls_CheckRT_1(
    state.server_tx.inCount && state.server_tx.inBytes, "server serialized Tx ingress is zero");
  Ztls_CheckRT_1(state.client_tx.inBytes == clientLen, "client plaintext Tx telemetry mismatch");
  Ztls_CheckRT_1(state.server_tx.inBytes == serverLen, "server plaintext Tx telemetry mismatch");
  Ztls_CheckRT_1(
    state.client_tx_protected.load_(), "client application records were not protected on TLS Tx");
  Ztls_CheckRT_1(
    state.server_tx_protected.load_(), "server application records were not protected on TLS Tx");
  Ztls_CheckRT_1(
    state.client_wire_count.load_() && state.client_wire_bytes.load_(),
    "client TLS wire egress is zero");
  Ztls_CheckRT_1(
    state.server_wire_count.load_() && state.server_wire_bytes.load_(),
    "server TLS wire egress is zero");
  Ztls_CheckRT_1(
    state.client_rx.count <= state.client_rx.inCount &&
      state.client_tx.count <= state.client_tx.inCount,
    "client TLS queue occupancy exceeds ingress");
  Ztls_CheckRT_1(
    state.server_rx.count <= state.server_rx.inCount &&
      state.server_tx.count <= state.server_tx.inCount,
    "server TLS queue occupancy exceeds ingress");
  if (clientKeyUpdate || serverKeyUpdate) {
    Ztls_CheckRT_1(
      state.client_rx.inCount == 1,
      "zero-output control record incremented client plaintext Rx count");
    Ztls_CheckRT_1(
      state.server_rx.inCount == 1,
      "zero-output control record incremented server plaintext Rx count");
  }
  if (serverDisconnectAfterSend)
    Ztls_CheckRT_1(
      state.client_peer_closed.load_(), "client did not receive graceful server close");
  if (expectedCipher) {
    Ztls_CheckRT_1(
      state.client_cipher.load_() == expectedCipher, "client selected unexpected cipher");
    Ztls_CheckRT_1(
      state.server_cipher.load_() == expectedCipher, "server selected unexpected cipher");
  }
}

static void ticketResumption(TempDir &temp)
{
  ZuTestScopeRT(ticketResumption);
  Ztls::PK::Key key;
  bool		loaded = Ztls::PK::loadPEM(key, temp.keyPath.data());
  ZuCheckRT(loaded);
  if (!loaded) return;
  auto		 sign = Ztls::sign_cert_new(key);
  auto		 ticketKey = Ztls::ticket_key_new();
  Ztls_::Context client{}, server{};
  ZuGuard	 cleanup{[sign, ticketKey]()
  {
    if (ticketKey) Ztls::ticket_key_free(ticketKey);
    if (sign) Ztls::sign_cert_free(sign);
  }};
  bool configured = sign && ticketKey && Ztls::load_certificates(&server, temp.certPath.data());
  ZuCheckRT(configured);
  if (!configured) return;
  client.random = server.random = &Ztls_::opensslRandom;
  client.getTime = server.getTime = &Ztls_::getTime;
  client.keyExchanges = server.keyExchanges = Ztls_::openssl_key_exchanges;
  client.cipherSuites = server.cipherSuites = Ztls_::openssl_cipher_suites;
  server.signCertificate = Ztls::sign_cert_cb(sign);
  server.encryptTicket = Ztls::ticket_encrypt_cb(ticketKey);
  server.ticketLifetime = 60;

  ZtBArray<> ticket;
  struct SaveSessionTicket : Ztls_::SaveTicket {
    int call(Ztls::Cxn *connection, Ztls_::Bytes input) override
    {
      auto &saved = *static_cast<ZtBArray<> *>(*Ztls_::get_data_ptr(connection));
      if (!saved) saved = ZuBSpan{input.data(), unsigned(input.length())};
      return 0;
    }
  } save;
  client.saveTicket = &save;
  for (bool resume : {false, true}) {
    Ztls::Cxn txConnection{&client, false}, rxConnection{&server, true};
    auto      tx = &txConnection, rx = &rxConnection;
    ZuCheckRT(tx && rx);
    if (!tx || !rx) return;
    *Ztls_::get_data_ptr(tx) = &ticket;
    Ztls_::HandshakeProps properties{};
    if (resume)
      properties.client.sessionTicket = Ztls_::wire_bytes(ticket.data(), ticket.length());
    bool complete = ZtlsTest::handshake(tx, rx, &properties);
    ZuCheckRT(complete && ticket.length());
    if (!complete || !ticket) return;
    ZuCheckRT(bool(Ztls_::isPskHandshake(tx)) == resume);
    ZuCheckRT(bool(Ztls_::isPskHandshake(rx)) == resume);
  }
}

template <typename T> void run_in_memory(TempDir &temp, LogCapture &capture, ZuCSpan type)
{
  static constexpr char CertExts[] =
    "[client]\n"
    "subjectKeyIdentifier=hash\n"
    "authorityKeyIdentifier=keyid:always\n"
    "basicConstraints=critical,CA:FALSE\n"
    "keyUsage=critical,digitalSignature\n"
    "extendedKeyUsage=clientAuth\n"
    "subjectAltName=critical,DNS:client\n"
    "[server]\n"
    "subjectKeyIdentifier=hash\n"
    "authorityKeyIdentifier=keyid:always\n"
    "basicConstraints=critical,CA:FALSE\n"
    "keyUsage=critical,digitalSignature\n"
    "extendedKeyUsage=serverAuth\n"
    "subjectAltName=critical,DNS:client\n"
    "[badku]\n"
    "subjectKeyIdentifier=hash\n"
    "authorityKeyIdentifier=keyid:always\n"
    "basicConstraints=critical,CA:FALSE\n"
    "keyUsage=critical,keyEncipherment\n"
    "extendedKeyUsage=clientAuth\n"
    "subjectAltName=critical,DNS:client\n"
    "[ca]\n"
    "subjectKeyIdentifier=hash\n"
    "authorityKeyIdentifier=keyid:always\n"
    "basicConstraints=critical,CA:TRUE\n"
    "keyUsage=critical,keyCertSign,cRLSign\n"
    "[ca0]\n"
    "subjectKeyIdentifier=hash\n"
    "authorityKeyIdentifier=keyid:always\n"
    "basicConstraints=critical,CA:TRUE,pathlen:0\n"
    "keyUsage=critical,keyCertSign,cRLSign\n"
    "[nonca]\n"
    "subjectKeyIdentifier=hash\n"
    "authorityKeyIdentifier=keyid:always\n"
    "basicConstraints=critical,CA:FALSE\n"
    "keyUsage=critical,digitalSignature\n";
  ZtBArray extData;
  extData << ZuBSpan{CertExts};
  Ztls_CheckRT_1(
    write_bytes(temp.extPath.data(), extData), type, " extension configuration write failed");

  Ztls::Random rng;
  Ztls_CheckRT_1(rng.init(), type, " random initialization failed");
  ZmRef<T> key;
  if constexpr (ZuIsSame<T, Ztls::PK::SK_RSA>{})
    key = new T{rng, 2048};
  else if constexpr (ZuIsSame<T, Ztls::PK::SK_EC>{})
    key = new T{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  else
    key = new T{rng};

  ZtBArray csr;
  auto	   result = key->saveCSR(csr);
  Ztls_CheckRT_1(!result.template is<ZeException>(), type, " CSR generation failed");
  if (result.template is<ZeException>()) return;

  ZtString<> name;
  name << type << ".csr.der";
  ZtString<> csrPath = temp.pathOf(name.data());
  name = {};
  name << type << ".cert.der";
  ZtString<> certPath = temp.pathOf(name.data());
  Ztls_CheckRT_1(write_bytes(csrPath.data(), csr), type, " CSR write failed");
  Ztls_CheckRT_1(
    issue_cert(
      temp,
      CertIssue{.csr = csrPath,
	.cert = certPath,
	.ca = temp.certPath,
	.caKey = temp.keyPath,
	.validity = "-days 1",
	.profile = "client",
	.serial = 100}),
    type, " certificate issuance failed");

  Ztls::Cert leaf;
  Ztls::Cert root;
  Ztls_CheckRT_1(read_bytes(certPath.data(), leaf), type, " issued certificate read failed");
  Ztls_CheckRT_1(read_bytes(temp.certDERPath.data(), root), type, " CA certificate read failed");
  if (!leaf || !root) return;

  ZuBSpan	 chain[] = {leaf, root};
  Ztls_::Context ctx{};
  Ztls_CheckRT_1(
    Ztls::load_certificates_der(&ctx, chain, key->key), type,
    " in-memory chain validation failed");
  auto	  installed = ctx.certificates.data();
  auto	  installedCount = ctx.certificates.length();
  ZuBSpan malformed[] = {ZuBSpan{"bad"}};
  Ztls_CheckRT_1(
    !Ztls::load_certificates_der(&ctx, malformed, key->key), type,
    " malformed certificate accepted");
  Ztls_CheckRT_1(
    ctx.certificates.data() == installed && ctx.certificates.length() == installedCount, type,
    " failed chain replaced installed certificates");
  ctx.certificates.null();

  ZuBSpan reversed[] = {root, leaf};
  Ztls_CheckRT_1(
    !Ztls::load_certificates_der(&ctx, reversed, key->key), type, " reversed chain accepted");
  Ztls_CheckRT_1(!Ztls::load_certificates_der(&ctx, {}, key->key), type, " empty chain accepted");

  ZmRef<T> other;
  if constexpr (ZuIsSame<T, Ztls::PK::SK_RSA>{})
    other = new T{rng, 2048};
  else if constexpr (ZuIsSame<T, Ztls::PK::SK_EC>{})
    other = new T{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  else
    other = new T{rng};
  Ztls_CheckRT_1(
    !Ztls::load_certificates_der(&ctx, chain, other->key), type, " mismatched key accepted");

  auto publicResult = key->mkPK();
  Ztls_CheckRT_1(!publicResult.template is<ZeException>(), type, " public key creation failed");
  if (!publicResult.template is<ZeException>()) {
    auto publicKey = ZuMv(publicResult).template p<0>();
    Ztls_CheckRT_1(
      !Ztls::load_certificates_der(&ctx, chain, publicKey->key), type,
      " public-only key accepted");
  }

  TestState		 invalidState;
  BaseClient<TestState>	 invalidClient{invalidState};
  Ztls::Certs		 noCerts;
  ZmRef<Ztls::PK::AnyPK> mixedKey{ZuMv(other)};
  auto			 mixedParams =
    Ztls::ClientParams(nullptr, "3", "4")
      .hub([&temp](auto &p) { p.certPath(temp.certPath.data()).keyPath(temp.keyPath.data()); })
      .key(ZuMv(mixedKey))
      .certs(ZuMv(noCerts));
  Ztls_CheckRT_1(
    !invalidClient.init(ZuMv(mixedParams)), type, " mixed path/in-memory credentials accepted");

  if constexpr (ZuIsSame<T, Ztls::PK::SK_RSA>{}) {
    Ztls::Cert broken{leaf};
    broken[broken.length() - 1] ^= 1;
    ZuBSpan brokenChain[] = {broken, root};
    Ztls_CheckRT_1(
      !Ztls::load_certificates_der(&ctx, brokenChain, key->key), type, " broken chain accepted");

    auto rejectIssued = [&](ZuCSpan name, ZuCSpan validity, ZuCSpan profile, unsigned serial)
    {
      ZtString<> path = temp.pathOf(name);
      bool	 issued = issue_cert(
	temp,
	CertIssue{.csr = csrPath,
	  .cert = path,
	  .ca = temp.certPath,
	  .caKey = temp.keyPath,
	  .validity = validity,
	  .profile = profile,
	  .serial = serial});
      Ztls_CheckRT_1(issued, type, " ", name, " issuance failed");
      Ztls::Cert cert;
      bool	 read = issued && read_bytes(path.data(), cert);
      Ztls_CheckRT_1(read, type, " ", name, " read failed");
      if (!read) return;
      ZuBSpan invalid[] = {cert, root};
      Ztls_CheckRT_1(
	!Ztls::load_certificates_der(&ctx, invalid, key->key), type, " ", name, " accepted");
    };
    rejectIssued(
      "expired.der", "-not_before 20000101000000Z -not_after 20010101000000Z", "client", 101);
    rejectIssued(
      "future.der", "-not_before 21000101000000Z -not_after 21010101000000Z", "client", 102);
    rejectIssued("server-eku.der", "-days 1", "server", 103);
    rejectIssued("bad-ku.der", "-days 1", "badku", 104);

    ZtString<> int0Key = temp.pathOf("int0.key");
    ZtString<> int0CSR = temp.pathOf("int0.csr.der");
    ZtString<> int0PEM = temp.pathOf("int0.pem");
    ZtString<> int0DER = temp.pathOf("int0.der");
    ZtString<> int1Key = temp.pathOf("int1.key");
    ZtString<> int1CSR = temp.pathOf("int1.csr.der");
    ZtString<> int1PEM = temp.pathOf("int1.pem");
    ZtString<> int1DER = temp.pathOf("int1.der");
    ZtString<> constrainedPath = temp.pathOf("constrained.der");
    bool       constrained = gen_csr(int0Key, int0CSR, "int0") &&
      issue_cert(
	temp,
	CertIssue{.csr = int0CSR,
	  .cert = int0PEM,
	  .ca = temp.certPath,
	  .caKey = temp.keyPath,
	  .validity = "-days 1",
	  .profile = "ca0",
	  .serial = 105,
	  .der = false}) &&
      cert_to_der(int0PEM, int0DER) && gen_csr(int1Key, int1CSR, "int1") &&
      issue_cert(
	temp,
	CertIssue{.csr = int1CSR,
	  .cert = int1PEM,
	  .ca = int0PEM,
	  .caKey = int0Key,
	  .validity = "-days 1",
	  .profile = "ca",
	  .serial = 106,
	  .der = false}) &&
      cert_to_der(int1PEM, int1DER) &&
      issue_cert(
	temp,
	CertIssue{.csr = csrPath,
	  .cert = constrainedPath,
	  .ca = int1PEM,
	  .caKey = int1Key,
	  .validity = "-days 1",
	  .profile = "client",
	  .serial = 107});
    Ztls_CheckRT_1(constrained, type, " constrained chain issuance failed");
    Ztls::Cert constrainedLeaf;
    Ztls::Cert int0;
    Ztls::Cert int1;
    constrained = constrained && read_bytes(constrainedPath.data(), constrainedLeaf) &&
      read_bytes(int0DER.data(), int0) && read_bytes(int1DER.data(), int1);
    Ztls_CheckRT_1(constrained, type, " constrained chain read failed");
    if (constrained) {
      ZuBSpan constrainedChain[] = {constrainedLeaf, int1, int0, root};
      Ztls_CheckRT_1(
	!Ztls::load_certificates_der(&ctx, constrainedChain, key->key), type,
	" path-length constraint violation accepted");

      ZuBSpan unrelatedChain[] = {leaf, int0};
      Ztls_CheckRT_1(
	!Ztls::load_certificates_der(&ctx, unrelatedChain, key->key), type,
	" unrelated chain anchor accepted");
    }

    ZtString<> nonCAKey = temp.pathOf("nonca.key");
    ZtString<> nonCACSR = temp.pathOf("nonca.csr.der");
    ZtString<> nonCAPEM = temp.pathOf("nonca.pem");
    ZtString<> nonCADER = temp.pathOf("nonca.der");
    ZtString<> nonCALeafPath = temp.pathOf("nonca-leaf.der");
    bool       nonCA = gen_csr(nonCAKey, nonCACSR, "nonca") &&
      issue_cert(
	temp,
	CertIssue{.csr = nonCACSR,
	  .cert = nonCAPEM,
	  .ca = temp.certPath,
	  .caKey = temp.keyPath,
	  .validity = "-days 1",
	  .profile = "nonca",
	  .serial = 108,
	  .der = false}) &&
      cert_to_der(nonCAPEM, nonCADER) &&
      issue_cert(
	temp,
	CertIssue{.csr = csrPath,
	  .cert = nonCALeafPath,
	  .ca = nonCAPEM,
	  .caKey = nonCAKey,
	  .validity = "-days 1",
	  .profile = "client",
	  .serial = 109});
    Ztls_CheckRT_1(nonCA, type, " non-CA chain issuance failed");
    Ztls::Cert nonCACert;
    Ztls::Cert nonCALeaf;
    nonCA = nonCA && read_bytes(nonCADER.data(), nonCACert) &&
      read_bytes(nonCALeafPath.data(), nonCALeaf);
    Ztls_CheckRT_1(nonCA, type, " non-CA chain read failed");
    if (nonCA) {
      ZuBSpan nonCAChain[] = {nonCALeaf, nonCACert, root};
      Ztls_CheckRT_1(
	!Ztls::load_certificates_der(&ctx, nonCAChain, key->key), type,
	" non-CA intermediate accepted");
    }
  }

  auto store = Ztls::cert_store_new();
  Ztls_CheckRT_1(
    store && Ztls::cert_store_add_ca(store, root), type, " supplemental CA rejected");
  Ztls_CheckRT_1(
    store && Ztls::cert_store_add_ca(store, root), type, " duplicate supplemental CA rejected");
  Ztls_CheckRT_1(
    store && !Ztls::cert_store_add_ca(store, leaf), type,
    " non-CA supplemental certificate accepted");
  Ztls_CheckRT_1(
    store && !Ztls::cert_store_add_ca(store, ZuBSpan{"bad"}), type,
    " malformed supplemental CA accepted");
  if constexpr (ZuIsSame<T, Ztls::PK::SK_RSA>{}) {
    ZtString<> expiredCAPath = temp.pathOf("expired-ca.der");
    bool       expiredCA = issue_cert(
      temp,
      CertIssue{.csr = csrPath,
	.cert = expiredCAPath,
	.ca = temp.certPath,
	.caKey = temp.keyPath,
	.validity = "-not_before 20000101000000Z -not_after 20010101000000Z",
	.profile = "ca",
	.serial = 110});
    Ztls_CheckRT_1(expiredCA, type, " expired CA issuance failed");
    Ztls::Cert cert;
    expiredCA = expiredCA && read_bytes(expiredCAPath.data(), cert);
    Ztls_CheckRT_1(expiredCA, type, " expired CA read failed");
    if (expiredCA)
      Ztls_CheckRT_1(
	!Ztls::cert_store_add_ca(store, cert), type, " expired supplemental CA accepted");
  }
  Ztls::cert_store_free(store);

  Ztls::Cert  leaf2{leaf};
  Ztls::Cert  root2{root};
  Ztls::Certs clientCerts;
  clientCerts.push(ZuMv(leaf2));
  clientCerts.push(ZuMv(root2));
  Ztls::Certs caCerts;
  caCerts.push(ZuMv(root));
  ZmRef<Ztls::PK::AnyPK> clientKey{ZuMv(key)};
  run_in_process(
    temp, capture, SmallPayloadSize, SmallPayloadSize, false, false, false, ZiIP{"127.0.0.1"},
    "127.0.0.1", {}, 0, 0, 0, ZuMv(clientKey), ZuMv(clientCerts), ZuMv(caCerts));
}

void testConnectFailureShard(TempDir &temp, LogCapture &capture)
{
  ZuTestScopeRT(testConnectFailureShard);
  capture.reset();

  TestState state;
  state.expectConnectFailure = true;
  uint16_t port = reserve_loopback_port();
  Ztls_CheckRT_1(port, "failed to reserve refused-connect port");
  if (!port) return;

  BaseClient<TestState> client{state};
  ZiMultiplex		mx(mx_params());
  bool			clientOK = client.init(
    Ztls::ClientParams(&mx, "3", "4").hub([&temp](auto &p) { p.caPath(temp.certPath.data()); }));
  Ztls_CheckRT_1(clientOK, "TLS failure client init failed");
  if (!clientOK) {
    mx.stop();
    return;
  }

  bool mxStarted = mx.start();
  Ztls_CheckRT_1(mxStarted, "ZiMultiplex start failed");
  if (!mxStarted) return;

  bool clientStarted = client.start();
  Ztls_CheckRT_1(clientStarted, "TLS failure client failed to start");
  if (!clientStarted) {
    client.final();
    mx.stop();
    return;
  }

  ZmRef<BaseClient<TestState>::Link> link = new BaseClient<TestState>::Link{&client};
  link->connect("localhost", port, ZiIP{"127.0.0.1"});
  bool completed = wait_for(state.done);
  Ztls_CheckRT_1(completed, "TLS connect failure timed out");

  link = nullptr;
  bool clientStopped = client.stop();
  Ztls_CheckRT_1(clientStopped, "TLS failure client failed to stop");
  client.final();
  mx.stop();

  Ztls_CheckRT_1(
    !state.client_connected.load_(), "refused TLS connection unexpectedly connected");
  Ztls_CheckRT_1(
    state.connect_failed.load_() == 1, "TLS connect failure callback count mismatch");
  Ztls_CheckRT_1(
    state.connect_failed_on_rx.load_(),
    "TLS connect failure callback ran outside configured Rx thread");
  Ztls_CheckRT_1(
    state.connect_failed_transient.load_(), "TLS refused-connect failure lost transient status");
  capture.reset();
}

void testWrongHostname(TempDir &temp, LogCapture &capture)
{
  ZuTestScopeRT(testWrongHostname);
  capture.reset();

  TestState state;
  state.expectConnectFailure = true;
  state.ip = ZiIP{"127.0.0.1"};
  state.port = reserve_loopback_port(state.ip);
  Ztls_CheckRT_1(state.port, "failed to reserve wrong-hostname port");
  if (!state.port) return;

  BaseServer<TestState> server{state, state.ip};
  BaseClient<TestState> client{state};
  ZiMultiplex		mx(mx_params());
  bool			serverOK = server.init(
    Ztls::ServerParams(&mx, "3", "4")
      .certPath(temp.certPath.data())
      .keyPath(temp.keyPath.data()));
  Ztls_CheckRT_1(serverOK, "wrong-hostname server init failed");
  bool clientOK = client.init(
    Ztls::ClientParams(&mx, "3", "4").hub([&temp](auto &p) { p.caPath(temp.certPath.data()); }));
  Ztls_CheckRT_1(clientOK, "wrong-hostname client init failed");
  if (!serverOK || !clientOK) {
    mx.stop();
    return;
  }

  bool mxStarted = mx.start();
  Ztls_CheckRT_1(mxStarted, "wrong-hostname multiplexer start failed");
  if (!mxStarted) return;

  bool serverStarted = server.start();
  bool clientStarted = client.start();
  Ztls_CheckRT_1(serverStarted && clientStarted, "wrong-hostname hubs failed to start");
  if (!serverStarted || !clientStarted) {
    if (clientStarted) client.stop();
    if (serverStarted) server.stop();
    client.final();
    server.final();
    mx.stop();
    return;
  }

  server.listen();
  bool listening = wait_for(state.listening);
  Ztls_CheckRT_1(listening, "wrong-hostname listen timed out");
  if (!listening) {
    mx.stop();
    return;
  }

  ZmRef<BaseClient<TestState>::Link> link = new BaseClient<TestState>::Link{&client};
  link->connect("wrong.invalid", state.port, state.ip);
  bool completed = wait_done(state);
  Ztls_CheckRT_1(completed, "wrong-hostname failure timed out");

  link = nullptr;
  mx.stopListening(state.ip, state.port);
  bool clientStopped = client.stop();
  bool serverStopped = server.stop();
  Ztls_CheckRT_1(clientStopped && serverStopped, "wrong-hostname hubs failed to stop");
  client.final();
  server.final();
  mx.stop();

  ZiLog::stop();
  Ztls_CheckRT_1(!state.client_connected.load_(), "wrong-hostname TLS connection succeeded");
  Ztls_CheckRT_1(capture.errors.load_(), "wrong-hostname failure did not report a TLS error");
  capture.reset();
  ZiLog::start();
}

void run_tls12_handshake_rejected(TempDir &temp, LogCapture &capture)
{
  capture.reset();

  TestState state;
  state.target = 1;
  state.ip = ZiIP("127.0.0.1");
  state.port = reserve_loopback_port();
  state.serverDisconnectAfterSend = true;
  fill_payload(state.clientPayload, SmallPayloadSize, 0x27);
  fill_payload(state.serverPayload, SmallPayloadSize, 0x91);
  Ztls_CheckRT_1(state.port, "failed to reserve loopback port");
  if (!state.port) return;

  BaseServer<TestState> server(state, state.ip);
  ZiMultiplex		mx(mx_params());
  bool			serverOK = server.init(
    Ztls::ServerParams(&mx, "3", "4")
      .certPath(temp.certPath.data())
      .keyPath(temp.keyPath.data()));
  Ztls_CheckRT_1(serverOK, "TLS server init failed");
  if (!serverOK) {
    mx.stop();
    return;
  }
  bool mxStarted = mx.start();
  Ztls_CheckRT_1(mxStarted, "ZiMultiplex start failed");
  if (!mxStarted) return;

  bool serverStarted = server.start();
  Ztls_CheckRT_1(serverStarted, "TLS server failed to start");
  if (!serverStarted) {
    server.final();
    mx.stop();
    return;
  }

  auto inPath = temp.pathOf("tls12-in.bin");
  auto outPath = temp.pathOf("tls12-out.bin");
  auto errPath = temp.pathOf("tls12-err.log");
  bool wrote = write_bytes(inPath.data(), state.clientPayload);
  Ztls_CheckRT_1(wrote, "failed to write openssl input");
  if (!wrote) {
    mx.stop();
    return;
  }

  server.listen();
  bool listening = wait_for(state.listening);
  Ztls_CheckRT_1(listening, "listen timed out");
  if (!listening) {
    mx.stop();
    return;
  }

  ZtString<> cmd;
  cmd << "timeout " << TimeoutSeconds
      << "s openssl s_client -quiet -ign_eof -verify_return_error "
	 "-tls1_2 -cipher ECDHE-RSA-AES128-GCM-SHA256"
      << " -servername localhost -connect 127.0.0.1:" << state.port << " -CAfile "
      << temp.certPath << " < " << inPath << " > " << outPath << " 2> " << errPath;
  bool cmdOK = TempDir::systemOK(system(cmd.data()));
  if (cmdOK) state.fail("unexpected TLS 1.2 handshake success");

  bool done = wait_done(state);
  Ztls_CheckRT_1(done, "TLS server disconnect wait timed out");

  mx.stopListening(state.ip, state.port);
  bool serverStopped = server.stop();
  Ztls_CheckRT_1(serverStopped, "TLS server failed to stop");
  server.final();
  mx.stop();
  ZtArray<uint8_t> err;
  bool		   readErr = read_bytes(errPath.data(), err);
  bool		   protocolVersion = readErr && LogCapture::contains(err, "protocol version");
  Ztls_CheckRT_1(!cmdOK, "TLS 1.2 handshake unexpectedly succeeded");
  Ztls_CheckRT_1(capture.errors.load_(), "TLS 1.2 rejection did not log an error");
  Ztls_CheckRT_1(protocolVersion, "TLS 1.2 rejection did not report protocol version");
  Ztls_CheckRT_1(!state.errors.load_(), state.error_msg ? state.error_msg : "state error");
  Ztls_CheckRT_1(
    !state.server_connected.load_(), "TLS 1.2 rejection reached application connected()");
}

void testTLS13JumboBuffers(TempDir &temp, LogCapture &capture)
{
  ZuTestScopeRT(testTLS13JumboBuffers);
  run_in_process(temp, capture, JumboPayloadSize, JumboPayloadSize, false);
}

void testTLS13ConcurrentRecords(TempDir &temp, LogCapture &capture)
{
  ZuTestScopeRT(testTLS13ConcurrentRecords);
  constexpr unsigned Clients = 64;
  constexpr unsigned Payload = 4096;
  capture.reset();

  TestState state;
  state.multi = true;
  state.target = Clients * 2;
  state.ip = ZiIP{"127.0.0.1"};
  state.port = reserve_loopback_port();
  state.serverDisconnectAfterSend = true;
  fill_payload(state.clientPayload, Payload, 0x11);
  fill_payload(state.serverPayload, Payload, 0x63);
  Ztls_CheckRT_1(state.port, "failed to reserve loopback port");
  if (!state.port) return;

  BaseServer<TestState> server(state, state.ip);
  BaseClient<TestState> client(state);
  ZiMultiplex		mx(mx_params());
  bool			serverOK = server.init(
    Ztls::ServerParams(&mx, "3", "4")
      .certPath(temp.certPath.data())
      .keyPath(temp.keyPath.data()));
  Ztls::ClientParams clientParams{&mx, "3", "4"};
  clientParams.caPath(temp.certPath.data());
  bool clientOK = client.init(ZuMv(clientParams));
  Ztls_CheckRT_1(serverOK && clientOK, "TLS stress init failed");
  if (!serverOK || !clientOK) {
    mx.stop();
    return;
  }
  bool mxStarted = mx.start();
  Ztls_CheckRT_1(mxStarted, "ZiMultiplex start failed");
  if (!mxStarted) return;
  bool serverStarted = server.start();
  bool clientStarted = client.start();
  Ztls_CheckRT_1(serverStarted && clientStarted, "TLS stress start failed");
  if (!serverStarted || !clientStarted) {
    mx.stop();
    return;
  }
  server.listen();
  if (!wait_for(state.listening)) {
    Ztls_CheckRT_1(false, "TLS stress listen timed out");
  } else {
    ZtArray<ZmRef<BaseClient<TestState>::Link>> links;
    links.length(Clients);
    for (unsigned i = 0; i < Clients; ++i) {
      links[i] = new BaseClient<TestState>::Link(&client);
      links[i]->connect("127.0.0.1", state.port);
    }
    Ztls_CheckRT_1(wait_done(state), "TLS stress disconnect wait timed out");
  }
  mx.stopListening(state.ip, state.port);
  client.stop();
  server.stop();
  client.final();
  server.final();
  mx.stop();
  Ztls_CheckRT_1(!capture.errors.load_(), "TLS stress emitted error logs");
  Ztls_CheckRT_1(!state.errors.load_(), state.error_msg ? state.error_msg : "TLS stress failed");
}

void testTLS13IPv6Loopback(TempDir &temp, LogCapture &capture)
{
  ZuTestScopeRT(testTLS13IPv6Loopback);
  run_in_process(
    temp, capture, SmallPayloadSize, SmallPayloadSize, false, false, false, ZiIP{"::1"}, "::1");
}

void testTLS13KeyUpdate(TempDir &temp, LogCapture &capture)
{
  ZuTestScopeRT(testTLS13KeyUpdate);
  auto suite = Ztls_::find_cipher_suite(Ztls_::openssl_cipher_suites, Ztls_SuiteAes128gcmSHA256);
  Ztls_CheckRT_1(suite, "TLS 1.3 AES-GCM suite unavailable");
  if (!suite) return;
  run_in_process(
    temp, capture, SmallPayloadSize, SmallPayloadSize, true, false, false, ZiIP{"127.0.0.1"},
    "127.0.0.1", {&suite, 1}, Ztls_SuiteAes128gcmSHA256);
}

void testTLS13ServerKeyUpdate(TempDir &temp, LogCapture &capture)
{
  ZuTestScopeRT(testTLS13ServerKeyUpdate);
  run_in_process(temp, capture, SmallPayloadSize, SmallPayloadSize, false, true);
}

void testTLS13QueuedClose(TempDir &temp, LogCapture &capture)
{
  ZuTestScopeRT(testTLS13QueuedClose);
  run_in_process(temp, capture, SmallPayloadSize, SmallPayloadSize, false, false, true);
}

void testTLS13ComposedHeadroom(TempDir &temp, LogCapture &capture)
{
  ZuTestScopeRT(testTLS13ComposedHeadroom);

  constexpr unsigned ExtraHeadroom = 31;
  constexpr unsigned ExtraTailroom = 17;
  constexpr unsigned RecordPayload = 16 * 1024 - 325 - ExtraHeadroom - ExtraTailroom;
  constexpr unsigned Payload = RecordPayload * 2 + 1;

  for (auto suite : Ztls_::openssl_cipher_suites) {
    run_in_process(
      temp, capture, Payload, Payload, false, false, false, ZiIP{"127.0.0.1"}, "127.0.0.1",
      {&suite, 1}, suite->id, ExtraHeadroom, ExtraTailroom);
  }
}

void testTLS12HandshakeRejected(TempDir &temp, LogCapture &capture)
{
  ZuTestScopeRT(testTLS12HandshakeRejected);
  run_tls12_handshake_rejected(temp, capture);
}

void testInMemoryRSA(TempDir &temp, LogCapture &capture)
{
  ZuTestScopeRT(testInMemoryRSA);
  run_in_memory<Ztls::PK::SK_RSA>(temp, capture, "rsa");
}

void testInMemoryEC(TempDir &temp, LogCapture &capture)
{
  ZuTestScopeRT(testInMemoryEC);
  run_in_memory<Ztls::PK::SK_EC>(temp, capture, "ec");
}

void testInMemoryED25519(TempDir &temp, LogCapture &capture)
{
  ZuTestScopeRT(testInMemoryED25519);
  run_in_memory<Ztls::PK::SK_ED25519>(temp, capture, "ed25519");
}

} // namespace

int main(int argc, char **argv)
{
  ZuTestUtil::parse(argc, argv);

  ZiTestResidue::init("ZtlsBufferTest");

  ZiLog::init("ZtlsBufferTest");
  ZiLog::level(0);

  LogCapture capture;
  ZiLog::sink(ZiLog::lambdaSink([&capture](ZeLogBuf &buf, const ZeEventInfo &info) {
    capture.onLog(buf, info);
  }));
  ZiLog::start();

  ZuTestMain();
  TempDir temp;
  bool	  tempOK = temp.init();
  ZuCHECK(tempOK, "failed to generate cert/key");
  if (tempOK) {
    ZuTestCall(testConnectFailureShard, temp, capture);
    ZuTestCall(testWrongHostname, temp, capture);
    ZuTestCall(testTLS13JumboBuffers, temp, capture);
    ZuTestCall(testTLS13ConcurrentRecords, temp, capture);
    ZuTestCall(testTLS13IPv6Loopback, temp, capture);
    ZuTestCall(testTLS13KeyUpdate, temp, capture);
    ZuTestCall(testTLS13ServerKeyUpdate, temp, capture);
    ZuTestCall(testTLS13QueuedClose, temp, capture);
    ZuTestCall(testTLS13ComposedHeadroom, temp, capture);
    ZuTestCall(testTLS12HandshakeRejected, temp, capture);
    ZuTestCall(ticketResumption, temp);
    ZuTestCall(testInMemoryRSA, temp, capture);
    ZuTestCall(testInMemoryEC, temp, capture);
    ZuTestCall(testInMemoryED25519, temp, capture);
  }

  ZiLog::stop();
}
