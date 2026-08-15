//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <errno.h>
#include <arpa/inet.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/Ztls.hh>
#include <zlib/ZtlsPico.hh>

using namespace ZuTestUtil;

#define ZTLS_CHECK_RT(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))

namespace {

constexpr unsigned BufSize = 128;
constexpr unsigned MaxSize = (1u << 20);
constexpr unsigned JumboPayloadSize = 64u * 1024u;
constexpr unsigned SmallPayloadSize = 4u * 1024u;
constexpr unsigned TimeoutSeconds = 15;

struct TempDir {
  char		path[PATH_MAX]{};
  ZtString<>	certPath;
  ZtString<>	keyPath;

  ~TempDir() { cleanup(); }

  bool init()
  {
    strcpy(path, "/tmp/ZtlsBufHookTest.XXXXXX");
    if (!mkdtemp(path)) return false;

    certPath << static_cast<const char *>(path) << "/cert.pem";
    keyPath << static_cast<const char *>(path) << "/key.pem";

    ZtString<> cmd;
    cmd <<
      "openssl req -x509 -newkey rsa:2048 -nodes -days 1 "
      "-subj /CN=localhost "
      "-addext basicConstraints=critical,CA:TRUE "
      "-addext keyUsage=critical,digitalSignature,keyEncipherment,keyCertSign "
      "-addext subjectAltName=DNS:localhost,IP:127.0.0.1,IP:::1 "
      "-keyout " << keyPath << ' ' <<
      "-out " << certPath << " >/dev/null 2>&1";
    return systemOK(system(cmd.data()));
  }

  ZtString<> pathOf(const char *name) const
  {
    ZtString<> s;
    s << static_cast<const char *>(path) << '/' << name;
    return s;
  }

  static bool systemOK(int status)
  {
    return status != -1 && WIFEXITED(status) && !WEXITSTATUS(status);
  }

  void cleanup()
  {
    if (!path[0]) return;
    const char *names[] = {
      "cert.pem", "key.pem", "tls12-in.bin", "tls12-out.bin",
      "tls12-err.log", nullptr
    };
    for (auto name = names; *name; ++name) {
      auto p = pathOf(*name);
      unlink(p.data());
    }
    rmdir(path);
    path[0] = 0;
  }
};

struct LogCapture {
  ZmAtomic<unsigned> errors{0};

  void reset() {
    errors.store_(0);
  }

  static bool contains(ZuCSpan haystack, ZuCSpan needle) {
    if (!needle.length()) return true;
    if (needle.length() > haystack.length()) return false;
    for (unsigned i = 0, n = haystack.length() - needle.length(); i <= n; ++i)
      if (!memcmp(haystack.data() + i, needle.data(), needle.length()))
	return true;
    return false;
  }

  void onLog(ZeLogBuf &buf, const ZeEventInfo &info) {
    (void)buf;
    if (info.severity >= Ze::Error) errors.xchAdd(1);
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
  bool			clientKeyUpdate = false;
  bool			serverKeyUpdate = false;
  bool			serverDisconnectAfterSend = false;
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
  ZmAtomic<Ztc::Link *>	server_link{nullptr};
  Ztc::QueueTelemetry	client_rx;
  Ztc::QueueTelemetry	client_tx;
  Ztc::QueueTelemetry	server_rx;
  Ztc::QueueTelemetry	server_tx;
  const char		*error_msg = nullptr;

  void done_one() {
    if (done_count.xchAdd(1) < target) done.post();
  }
  void fail(const char *msg) {
    if (!errors.xch(1)) error_msg = msg;
    for (unsigned i = 0; i < target; ++i) done.post();
  }
};

template <typename Link>
void queue_telemetry(
  Link &link, Ztc::QueueTelemetry &rx, Ztc::QueueTelemetry &tx)
{
  auto linkKey = link.telKey();
  unsigned count = 0;
  unsigned allQueues = link.allQueues(
      [&linkKey, &rx, &tx, &count](Ztc::Queue *queue) {
    auto key = queue->telKey();
    ZTLS_CHECK_RT(key.template p<0>() == linkKey.template p<0>(),
      "telemetry queue owner ID mismatch");
    ZTLS_CHECK_RT(key.template p<1>() == linkKey.template p<1>(),
      "telemetry queue ID mismatch");
    ZTLS_CHECK_RT(key.template p<2>() ==
	(count ? Ztc::QueueType::Tx : Ztc::QueueType::Rx),
      "telemetry queue iteration order mismatch");

    Ztc::QueueTelemetry data;
    data.ownerID = ZuID{} << "dirtyOwner";
    data.id = ZuID{} << "dirty";
    data.inBytes = data.outBytes = data.inCount = data.outCount = 1;
    data.count = data.size = data.full = 1;
    data.type = Ztc::QueueType::Thread;
    queue->telemetry(data);
    ZTLS_CHECK_RT(data.ownerID == key.template p<0>(),
      "queue telemetry owner ID mismatch");
    ZTLS_CHECK_RT(data.id == key.template p<1>(),
      "queue telemetry ID mismatch");
    ZTLS_CHECK_RT(data.type == key.template p<2>(),
      "queue telemetry type mismatch");
    ZTLS_CHECK_RT(!data.size && !data.full,
      "queue telemetry fields were not overwritten");
    if (data.type == Ztc::QueueType::Rx) {
      ZTLS_CHECK_RT(!data.outBytes && !data.outCount,
	"Rx telemetry output fields were not overwritten");
      rx = data;
    } else {
      tx = data;
    }
    ++count;
  });
  ZTLS_CHECK_RT(allQueues == 2, "allQueues returned incorrect count");
  ZTLS_CHECK_RT(count == allQueues,
    "allQueues did not enumerate returned count");
  unsigned repeat = 0;
  link.allQueues([&repeat](Ztc::Queue *queue) {
    auto key = queue->telKey();
    ZTLS_CHECK_RT(key.template p<2>() ==
	(repeat++ ? Ztc::QueueType::Tx : Ztc::QueueType::Rx),
      "allQueues queue order is not stable");
  });
  ZTLS_CHECK_RT(rx.ownerID == linkKey.template p<0>() &&
      tx.ownerID == linkKey.template p<0>(),
    "queue owner ID mismatch");
  ZTLS_CHECK_RT(rx.id == linkKey.template p<1>() &&
      tx.id == linkKey.template p<1>(),
    "queue ID mismatch");
  ZTLS_CHECK_RT(rx.type != tx.type,
    "Rx and Tx queue types do not discriminate the keys");
}

template <typename Link>
void check_queues(Link &link)
{
  Ztc::QueueTelemetry rx;
  Ztc::QueueTelemetry tx;
  queue_telemetry(link, rx, tx);
}

void fill_payload(ZtArray<uint8_t> &payload, unsigned len, uint8_t seed)
{
  payload.length(len);
  for (unsigned i = 0; i < len; ++i)
    payload[i] = uint8_t(seed + (i * 31u) + (i >> 3));
}

bool write_bytes(const char *path, const ZtArray<uint8_t> &data)
{
  FILE *file = fopen(path, "wb");
  if (!file) return false;
  bool ok = !data.length() ||
    fwrite(data.data(), 1, data.length(), file) == data.length();
  if (fclose(file)) ok = false;
  return ok;
}

bool read_bytes(const char *path, ZtArray<uint8_t> &data)
{
  FILE *file = fopen(path, "rb");
  if (!file) return false;
  if (fseek(file, 0, SEEK_END)) { fclose(file); return false; }
  long len = ftell(file);
  if (len < 0) { fclose(file); return false; }
  if (fseek(file, 0, SEEK_SET)) { fclose(file); return false; }
  data.length(unsigned(len));
  bool ok = !len || fread(data.data(), 1, unsigned(len), file) == unsigned(len);
  if (fclose(file)) ok = false;
  return ok;
}

bool consume_payload(
    TestState &state,
    ZmAtomic<unsigned> &offset,
    const ZtArray<uint8_t> &expected,
    ZuSpan<uint8_t> span,
    const char *msg)
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
    TestState &state,
    ZmAtomic<unsigned> &offset,
    const ZtArray<uint8_t> &expected,
    Ztls::RxStream &rx,
    const char *msg,
    bool &complete)
{
  unsigned off = offset.load_();
  if (ZuUnlikely(off >= expected.length())) {
    state.fail(msg);
    return -1;
  }

  uint64_t need = expected.length() - off;
  uint64_t seen = 0;
  int64_t consumed = rx.consume(
    [&seen, need](ZuSpan<uint8_t> span) -> int64_t {
      seen += span.length();
      if (seen < need) return 0;
      return span.length() - (seen - need);
    },
    [&state, &offset, &expected, msg, &complete](ZuSpan<uint8_t> span) {
      complete = consume_payload(state, offset, expected, span, msg);
    });

  if (ZuUnlikely(consumed < 0)) return -1;
  return consumed ? 1 : 0;
}

bool wait_for(ZmSemaphore &sem)
{
  return sem.timedwait(Zm::now(TimeoutSeconds)) == 0;
}

bool wait_done(TestState &state)
{
  for (unsigned i = 0; i < state.target; ++i)
    if (!wait_for(state.done)) return false;
  return true;
}

uint16_t reserve_loopback_port(ZiIP ip = ZiIP{"127.0.0.1"})
{
  int s = ::socket(ip.v6() ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s < 0) return 0;
  ZiSockAddr addr{ip, 0};
  uint16_t port = 0;
  if (!::bind(s, addr.sa(), addr.len())) {
    socklen_t len = addr.len();
    if (!::getsockname(s, addr.sa(), &len))
      port = addr.port();
  }
  ::close(s);
  return port;
}

ZiMxParams mx_params()
{
  return ZiMxParams()
    .scheduler([](auto &s) {
      s.nThreads(4)
	.thread(1, [](auto &t) { t.isolated(1); })
	.thread(2, [](auto &t) { t.isolated(1); })
	.thread(3, [](auto &t) { t.isolated(1); })
	.thread(4, [](auto &t) { t.isolated(1); });
    })
    .rxThread(1).txThread(2);
}

template <typename Lower>
struct ReserveLayer : public ZiTxLayer<ReserveLayer<Lower>, Lower> {
  using Base = ZiTxLayer<ReserveLayer<Lower>, Lower>;

  ReserveLayer(Lower &lower, unsigned headRoom, unsigned tailRoom) :
    Base{lower, headRoom, tailRoom} { }

  void prepareBuf_(ZiIOBuf *, bool) { }
};

template <typename Link>
void send_payload(Link *link, const ZtArray<uint8_t> &payload)
{
  if (!payload.length()) return;
  auto tx = link->txStream();
  auto &state = link->app()->state;
  unsigned head1 = state.txHeadroom >> 1;
  unsigned tail1 = state.txTailroom >> 1;
  ReserveLayer first{tx, head1, tail1};
  ReserveLayer second{
    first, state.txHeadroom - head1, state.txTailroom - tail1};
  second.append(payload.data(), unsigned(payload.length()));
  second << Zi::flush();
}

template <typename State>
struct BaseClient : public Ztls::Client<BaseClient<State>> {
  using RxBufAlloc = Ztls::RxBufAlloc<BufSize, MaxSize>;
  using TxBufAlloc = Ztls::TxBufAlloc<BufSize, MaxSize>;
  using Base = Ztls::Client<BaseClient<State>>;

  struct Link : public Ztls::CliLink<BaseClient, Link, RxBufAlloc, TxBufAlloc> {
    using BaseLink = Ztls::CliLink<BaseClient, Link, RxBufAlloc, TxBufAlloc>;
    Link(BaseClient *app) : BaseLink{app, ZuID{"client"}} { }

    void connected(Ztls::Connected info) {
      auto &state = this->app()->state;
      check_queues(*this);
      state.client_connected = 1;
      state.client_tlsver = unsigned(info.version);
      state.client_cipher = this->tlsInfo().cipherID;
      if (state.clientKeyUpdate && !this->updateKey_(true)) {
	state.fail("client key update failed");
	return;
      }
      send_payload(this, state.clientPayload);
    }
    void disconnected(bool peer) {
      auto &state = this->app()->state;
      state.client_peer_closed = peer;
      state.done_one();
    }
    void connectFailed(bool transient) {
      auto app = this->app();
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
    void txProtected_() {
      auto app = this->app();
      if (!app->txInvoked() || app->rxInvoked()) {
	app->state.fail("client record protection ran outside TLS Tx thread");
	return;
      }
      app->state.client_tx_protected.xchAdd(1);
    }
    void sent(ZmRef<ZiTxBuf> buf, bool ok) {
      auto &state = this->app()->state;
      if (!ok) {
	state.fail("client TLS wire send failed");
	return;
      }
      state.client_wire_count.xchAdd(1);
      state.client_wire_bytes.xchAdd(buf->length);
    }
    int process(Ztls::RxStream &rx) {
      auto &state = this->app()->state;
      while (!rx.empty()) {
	bool complete = false;
	int consumed = consume_payload_frame(
	  state, state.client_rx_bytes, state.serverPayload, rx,
	  "client received unexpected payload", complete);
	if (ZuUnlikely(consumed < 0)) return -1;
	if (!consumed) return 0;
	if (complete && !state.client_closed.xch(1)) {
	  queue_telemetry(*this, state.client_rx, state.client_tx);
	  if (auto serverLink = state.server_link.load_()) {
	    queue_telemetry(
	      *serverLink, state.server_rx, state.server_tx);
	  }
	  if (!state.serverDisconnectAfterSend) this->disconnect_();
	}
      }
      return 1;
    }
  };

  BaseClient(State &state_) : state(state_) { }

  void override_cipher_suites(ptls_cipher_suite_t **suites) {
    this->ctx()->cipher_suites = suites;
  }

  State	&state;
};

template <typename State>
struct BaseServer : public Ztls::Server<BaseServer<State>> {
  using RxBufAlloc = Ztls::RxBufAlloc<BufSize, MaxSize>;
  using TxBufAlloc = Ztls::TxBufAlloc<BufSize, MaxSize>;
  using Base = Ztls::Server<BaseServer<State>>;

  struct Link : public Ztls::SrvLink<BaseServer, Link, RxBufAlloc, TxBufAlloc> {
    using BaseLink = Ztls::SrvLink<BaseServer, Link, RxBufAlloc, TxBufAlloc>;
    Link(BaseServer *app) : BaseLink{app} { }

    void connected(Ztls::Connected info) {
      auto &state = this->app()->state;
      check_queues(*this);
      state.server_link = this;
      state.server_connected = 1;
      state.server_tlsver = unsigned(info.version);
      state.server_cipher = this->tlsInfo().cipherID;
    }
    void disconnected(bool) { this->app()->state.done_one(); }
    void txProtected_() {
      auto app = this->app();
      if (!app->txInvoked() || app->rxInvoked()) {
	app->state.fail("server record protection ran outside TLS Tx thread");
	return;
      }
      app->state.server_tx_protected.xchAdd(1);
    }
    void sent(ZmRef<ZiTxBuf> buf, bool ok) {
      auto &state = this->app()->state;
      if (!ok) {
	state.fail("server TLS wire send failed");
	return;
      }
      state.server_wire_count.xchAdd(1);
      state.server_wire_bytes.xchAdd(buf->length);
    }
    int process(Ztls::RxStream &rx) {
      auto &state = this->app()->state;
      while (!rx.empty()) {
	bool complete = false;
	int consumed = consume_payload_frame(
	  state, state.server_rx_bytes, state.clientPayload, rx,
	  "server received unexpected payload", complete);
	if (ZuUnlikely(consumed < 0)) return -1;
	if (!consumed) return 0;
	if (complete && !state.server_replied.xch(1)) {
	  if (state.serverKeyUpdate && !this->updateKey_(true)) {
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

  using Cxn = typename Link::Cxn;
  Cxn *accepted(const ZiCxnInfo &ci) {
    return new Cxn(new Link(this), ci);
  }

  BaseServer(State &state_, ZiIP ip_) : state(state_), ip(ip_) { }

  ZiIP localIP() const { return ip; }
  unsigned localPort() const { return state.port; }

  void listening(const ZiListenInfo &info) {
    (void)info;
    state.listening.post();
  }
  void listenFailed(bool) { state.fail("listen failed"); }

  void override_cipher_suites(ptls_cipher_suite_t **suites) {
    this->ctx()->cipher_suites = suites;
  }
  void override_tls12_cipher_suites(ptls_cipher_suite_t **suites) {
    this->ctx()->tls12_cipher_suites = suites;
  }

  State	&state;
  ZiIP	ip;
};

struct SuiteList {
  ptls_cipher_suite_t *list[2]{};
  void set(ptls_cipher_suite_t *suite) {
    list[0] = suite;
    list[1] = nullptr;
  }
};

void run_in_process(
    TempDir &temp,
    LogCapture &capture,
    unsigned clientLen,
    unsigned serverLen,
    bool clientKeyUpdate,
    bool serverKeyUpdate = false,
    bool serverDisconnectAfterSend = false,
    ZiIP ip = ZiIP{"127.0.0.1"},
    const char *connectIP = "127.0.0.1",
    ptls_cipher_suite_t **cipherSuites = nullptr,
    unsigned expectedCipher = 0,
    unsigned txHeadroom = 0,
    unsigned txTailroom = 0)
{
  Ztls::Pico::reset_stats();
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
  ZTLS_CHECK_RT(state.port, "failed to reserve loopback port");
  if (!state.port) return;

  BaseServer<TestState> server(state, state.ip);
  BaseClient<TestState> client(state);
  ZiMultiplex mx(mx_params());
  bool mxStarted = mx.start();
  ZTLS_CHECK_RT(mxStarted, "ZiMultiplex start failed");
  if (!mxStarted) return;

  bool serverOK = server.init(
    Ztls::ServerParams(&mx, "3", "4")
      .certPath(temp.certPath.data())
      .keyPath(temp.keyPath.data()));
  ZTLS_CHECK_RT(serverOK, "TLS server init failed");
  if (!serverOK) { mx.stop(); return; }

  bool clientOK = client.init(
    Ztls::ClientParams(&mx, "3", "4").caPath(temp.certPath.data()));
  ZTLS_CHECK_RT(clientOK, "TLS client init failed");
  if (!clientOK) { mx.stop(); return; }

  if (cipherSuites) {
    server.override_cipher_suites(cipherSuites);
    client.override_cipher_suites(cipherSuites);
  }

  bool serverStarted = server.start();
  bool clientStarted = client.start();
  ZTLS_CHECK_RT(serverStarted && clientStarted, "TLS hubs failed to start");
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
  ZTLS_CHECK_RT(listening, "listen timed out");
  if (!listening) { mx.stop(); return; }

  ZmRef<typename BaseClient<TestState>::Link> link =
    new typename BaseClient<TestState>::Link(&client);
  check_queues(*link);
  link->connect(connectIP, state.port);

  bool done = wait_done(state);
  ZTLS_CHECK_RT(done, "TLS disconnect wait timed out");

  link = nullptr;
  mx.stopListening(state.ip, state.port);
  bool clientStopped = client.stop();
  bool serverStopped = server.stop();
  ZTLS_CHECK_RT(clientStopped && serverStopped, "TLS hubs failed to stop");
  client.final();
  server.final();
  mx.stop();

  ZTLS_CHECK_RT(!capture.errors.load_(), "unexpected error logs");
  ZTLS_CHECK_RT(!state.errors.load_(),
    state.error_msg ? state.error_msg : "state error");
  ZTLS_CHECK_RT(state.client_connected.load_(), "client did not connect");
  ZTLS_CHECK_RT(state.server_connected.load_(), "server did not connect");
  ZTLS_CHECK_RT(state.client_tlsver.load_() == 13, "client did not use TLS 1.3");
  ZTLS_CHECK_RT(state.server_tlsver.load_() == 13, "server did not use TLS 1.3");
  ZTLS_CHECK_RT(state.client_rx_bytes.load_() == serverLen,
    "client payload length mismatch");
  ZTLS_CHECK_RT(state.server_rx_bytes.load_() == clientLen,
    "server payload length mismatch");
  ZTLS_CHECK_RT(state.client_rx.inBytes == serverLen,
    "client plaintext Rx telemetry mismatch");
  ZTLS_CHECK_RT(state.server_rx.inBytes == clientLen,
    "server plaintext Rx telemetry mismatch");
  ZTLS_CHECK_RT(state.client_rx.inCount,
    "client plaintext Rx ingress count is zero");
  ZTLS_CHECK_RT(state.server_rx.inCount,
    "server plaintext Rx ingress count is zero");
  ZTLS_CHECK_RT(state.client_tx.inCount && state.client_tx.inBytes,
    "client serialized Tx ingress is zero");
  ZTLS_CHECK_RT(state.server_tx.inCount && state.server_tx.inBytes,
    "server serialized Tx ingress is zero");
  ZTLS_CHECK_RT(state.client_tx.inBytes >= clientLen,
    "client serialized Tx bytes omit application payload");
  ZTLS_CHECK_RT(state.server_tx.inBytes >= serverLen,
    "server serialized Tx bytes omit application payload");
  ZTLS_CHECK_RT(state.client_tx_protected.load_(),
    "client application records were not protected on TLS Tx");
  ZTLS_CHECK_RT(state.server_tx_protected.load_(),
    "server application records were not protected on TLS Tx");
  ZTLS_CHECK_RT(state.client_wire_count.load_() &&
      state.client_wire_bytes.load_(),
    "client TLS wire egress is zero");
  ZTLS_CHECK_RT(state.server_wire_count.load_() &&
      state.server_wire_bytes.load_(),
    "server TLS wire egress is zero");
  ZTLS_CHECK_RT(state.client_rx.count <= state.client_rx.inCount &&
      state.client_tx.count <= state.client_tx.inCount,
    "client TLS queue occupancy exceeds ingress");
  ZTLS_CHECK_RT(state.server_rx.count <= state.server_rx.inCount &&
      state.server_tx.count <= state.server_tx.inCount,
    "server TLS queue occupancy exceeds ingress");
  if (clientKeyUpdate || serverKeyUpdate) {
    ZTLS_CHECK_RT(state.client_rx.inCount == 1,
      "zero-output control record incremented client plaintext Rx count");
    ZTLS_CHECK_RT(state.server_rx.inCount == 1,
      "zero-output control record incremented server plaintext Rx count");
  }
  if (serverDisconnectAfterSend)
    ZTLS_CHECK_RT(state.client_peer_closed.load_(),
      "client did not receive graceful server close");
  if (expectedCipher) {
    ZTLS_CHECK_RT(state.client_cipher.load_() == expectedCipher,
      "client selected unexpected cipher");
    ZTLS_CHECK_RT(state.server_cipher.load_() == expectedCipher,
      "server selected unexpected cipher");
  }
}

void testConnectFailureShard(TempDir &temp, LogCapture &capture)
{
  ZuTestScopeRT(testConnectFailureShard);
  capture.reset();

  TestState state;
  state.expectConnectFailure = true;
  uint16_t port = reserve_loopback_port();
  ZTLS_CHECK_RT(port, "failed to reserve refused-connect port");
  if (!port) return;

  BaseClient<TestState> client{state};
  ZiMultiplex mx(mx_params());
  bool mxStarted = mx.start();
  ZTLS_CHECK_RT(mxStarted, "ZiMultiplex start failed");
  if (!mxStarted) return;

  bool clientOK = client.init(
    Ztls::ClientParams(&mx, "3", "4").caPath(temp.certPath.data()));
  ZTLS_CHECK_RT(clientOK, "TLS failure client init failed");
  if (!clientOK) { mx.stop(); return; }

  bool clientStarted = client.start();
  ZTLS_CHECK_RT(clientStarted, "TLS failure client failed to start");
  if (!clientStarted) { client.final(); mx.stop(); return; }

  ZmRef<BaseClient<TestState>::Link> link =
    new BaseClient<TestState>::Link{&client};
  link->connect("localhost", port, ZiIP{"127.0.0.1"});
  bool completed = wait_for(state.done);
  ZTLS_CHECK_RT(completed, "TLS connect failure timed out");

  link = nullptr;
  bool clientStopped = client.stop();
  ZTLS_CHECK_RT(clientStopped, "TLS failure client failed to stop");
  client.final();
  mx.stop();

  ZTLS_CHECK_RT(!state.client_connected.load_(),
    "refused TLS connection unexpectedly connected");
  ZTLS_CHECK_RT(state.connect_failed.load_() == 1,
    "TLS connect failure callback count mismatch");
  ZTLS_CHECK_RT(state.connect_failed_on_rx.load_(),
    "TLS connect failure callback ran outside configured Rx thread");
  ZTLS_CHECK_RT(state.connect_failed_transient.load_(),
    "TLS refused-connect failure lost transient status");
  capture.reset();
}

struct ImportedTLS {
  SuiteList	suites;
  ptls_context_t	ctx{};
  ptls_t	*client = nullptr;
  ptls_t	*server = nullptr;
  ~ImportedTLS() {
    if (client) ptls_free(client);
    if (server) ptls_free(server);
  }
};

bool import_tls12_one(
    ptls_context_t *ctx,
    ptls_cipher_suite_t *suite,
    bool isServer,
    ImportedTLS &pair)
{
  uint8_t master[PTLS_TLS12_MASTER_SECRET_SIZE];
  uint8_t randoms[PTLS_HELLO_RANDOM_SIZE * 2];
  for (unsigned i = 0; i < sizeof(master); ++i) master[i] = uint8_t(0x40 + i);
  for (unsigned i = 0; i < sizeof(randoms); ++i) randoms[i] = uint8_t(0x80 + i);

  uint8_t paramsStorage[512];
  ptls_buffer_t params;
  ptls_buffer_init_tx(&params, paramsStorage, sizeof(paramsStorage));
  int n = ptls_build_tls12_export_params(
    ctx, &params, isServer ? 1 : 0, 0, suite, master, randoms,
    1, "localhost", ptls_iovec_init(nullptr, 0));
  if (n) {
    ptls_buffer_dispose(&params);
    return false;
  }
  ptls_t *tls = nullptr;
  n = ptls_import(ctx, &tls, ptls_iovec_init(params.base, params.off));
  ptls_buffer_dispose(&params);
  if (n) return false;
  if (isServer)
    pair.server = tls;
  else
    pair.client = tls;
  return true;
}

bool import_tls12_pair(ptls_cipher_suite_t *suite, ImportedTLS &pair)
{
  pair.suites.set(suite);
  pair.ctx.random_bytes = Ztls::Backend::random_bytes_cb();
  pair.ctx.get_time = &ptls_get_time;
  pair.ctx.cipher_suites = Ztls::Backend::cipher_suites();
  pair.ctx.tls12_cipher_suites = pair.suites.list;
  return
    import_tls12_one(&pair.ctx, suite, false, pair) &&
    import_tls12_one(&pair.ctx, suite, true, pair);
}

bool tls12_roundtrip(
    ptls_t *sender,
    ptls_t *receiver,
    ptls_cipher_suite_t *suite,
    const ZtArray<uint8_t> &payload)
{
  constexpr unsigned TxRecordCapacity = 16 * 1024;
  unsigned headroom = 5 + suite->aead->tls12.record_iv_size;
  using TestBuf = Ztls::TxBufAlloc<BufSize, MaxSize>;

  ZmRef<ZiIOBuf> buf = new TestBuf{nullptr};
  if (!buf || !buf->ensure(TxRecordCapacity)) return false;
  memcpy(buf->data_() + headroom, payload.data(), payload.length());

  ptls_buffer_t tx;
  ptls_buffer_init_tx(&tx, buf->data_(), TxRecordCapacity);
  tx.origin = buf.ptr();
  tx.align_bits = suite->aead->align_bits;
  int n = ptls_send(sender, &tx, buf->data_() + headroom, payload.length());
  if (n) return false;
  if (tx.origin != buf.ptr() || tx.base != buf->data_() || !tx.off)
    return false;

  buf->skip = 0;
  buf->length = uint32_t(tx.off);

  ptls_buffer_t rx;
  ptls_buffer_init_rx(&rx, buf->data_() + headroom, buf->size - headroom);
  rx.origin = buf.ptr();
  rx.align_bits = suite->aead->align_bits;
  size_t inlen = buf->length;
  n = ptls_receive(receiver, &rx, buf->data_(), &inlen);
  if (n || inlen != buf->length) return false;
  if (rx.origin != buf.ptr() || rx.base != buf->data_() + headroom)
    return false;
  if (rx.off != payload.length()) return false;
  return !payload.length() || !memcmp(rx.base, payload.data(), payload.length());
}

void run_tls12_records(ptls_cipher_suite_t *suite)
{
  Ztls::Pico::install();
  Ztls::Pico::reset_stats();

  ImportedTLS pair;
  bool imported = import_tls12_pair(suite, pair);
  ZTLS_CHECK_RT(imported, "failed to import TLS 1.2 record state");
  if (!imported) return;

  ZTLS_CHECK_RT(ptls_get_protocol_version(pair.client) == PTLS_PROTOCOL_VERSION_TLS12,
    "client import did not create TLS 1.2 state");
  ZTLS_CHECK_RT(ptls_get_protocol_version(pair.server) == PTLS_PROTOCOL_VERSION_TLS12,
    "server import did not create TLS 1.2 state");

  auto overhead = 5 + suite->aead->tls12.record_iv_size + suite->aead->tag_size;
  ZTLS_CHECK_RT(ptls_get_record_overhead(pair.client) == overhead,
    "client TLS 1.2 record overhead mismatch");
  ZTLS_CHECK_RT(ptls_get_record_overhead(pair.server) == overhead,
    "server TLS 1.2 record overhead mismatch");

  ZtArray<uint8_t> clientPayload;
  ZtArray<uint8_t> serverPayload;
  fill_payload(clientPayload, SmallPayloadSize, 0x27);
  fill_payload(serverPayload, SmallPayloadSize, 0x91);

  ZTLS_CHECK_RT(tls12_roundtrip(pair.client, pair.server, suite, clientPayload),
    "client-to-server TLS 1.2 record round trip failed");
  ZTLS_CHECK_RT(tls12_roundtrip(pair.server, pair.client, suite, serverPayload),
    "server-to-client TLS 1.2 record round trip failed");
}

void run_tls12_handshake_rejected(
    TempDir &temp,
    LogCapture &capture)
{
  Ztls::Pico::reset_stats();
  capture.reset();

  SuiteList suites;
  auto suite = Ztls::Backend::tls12_ecdhe_rsa_aes128gcmsha256();
  ZTLS_CHECK_RT(suite, "TLS 1.2 AES-GCM suite unavailable");
  if (!suite) return;
  suites.set(suite);

  TestState state;
  state.target = 1;
  state.ip = ZiIP("127.0.0.1");
  state.port = reserve_loopback_port();
  state.serverDisconnectAfterSend = true;
  fill_payload(state.clientPayload, SmallPayloadSize, 0x27);
  fill_payload(state.serverPayload, SmallPayloadSize, 0x91);
  ZTLS_CHECK_RT(state.port, "failed to reserve loopback port");
  if (!state.port) return;

  BaseServer<TestState> server(state, state.ip);
  ZiMultiplex mx(mx_params());
  bool mxStarted = mx.start();
  ZTLS_CHECK_RT(mxStarted, "ZiMultiplex start failed");
  if (!mxStarted) return;

  bool serverOK = server.init(
    Ztls::ServerParams(&mx, "3", "4")
      .certPath(temp.certPath.data())
      .keyPath(temp.keyPath.data()));
  ZTLS_CHECK_RT(serverOK, "TLS server init failed");
  if (!serverOK) { mx.stop(); return; }
  server.override_tls12_cipher_suites(suites.list);
  bool serverStarted = server.start();
  ZTLS_CHECK_RT(serverStarted, "TLS server failed to start");
  if (!serverStarted) { server.final(); mx.stop(); return; }

  auto inPath = temp.pathOf("tls12-in.bin");
  auto outPath = temp.pathOf("tls12-out.bin");
  auto errPath = temp.pathOf("tls12-err.log");
  bool wrote = write_bytes(inPath.data(), state.clientPayload);
  ZTLS_CHECK_RT(wrote, "failed to write openssl input");
  if (!wrote) { mx.stop(); return; }

  server.listen();
  bool listening = wait_for(state.listening);
  ZTLS_CHECK_RT(listening, "listen timed out");
  if (!listening) { mx.stop(); return; }

  ZtString<> cmd;
  cmd << "timeout " << TimeoutSeconds <<
    "s openssl s_client -quiet -ign_eof -verify_return_error "
    "-tls1_2 -cipher ECDHE-RSA-AES128-GCM-SHA256" <<
    " -servername localhost -connect 127.0.0.1:" << state.port <<
    " -CAfile " << temp.certPath <<
    " < " << inPath <<
    " > " << outPath <<
    " 2> " << errPath;
  bool cmdOK = TempDir::systemOK(system(cmd.data()));
  if (cmdOK) state.fail("unexpected TLS 1.2 handshake success");

  bool done = wait_done(state);
  ZTLS_CHECK_RT(done, "TLS server disconnect wait timed out");

  mx.stopListening(state.ip, state.port);
  bool serverStopped = server.stop();
  ZTLS_CHECK_RT(serverStopped, "TLS server failed to stop");
  server.final();
  mx.stop();
  ZtArray<uint8_t> err;
  bool readErr = read_bytes(errPath.data(), err);
  bool protocolVersion =
    readErr && LogCapture::contains(err, "protocol version");
  ZTLS_CHECK_RT(!cmdOK, "TLS 1.2 handshake unexpectedly succeeded");
  ZTLS_CHECK_RT(capture.errors.load_(),
    "TLS 1.2 rejection did not log an error");
  ZTLS_CHECK_RT(protocolVersion,
    "TLS 1.2 rejection did not report protocol version");
  ZTLS_CHECK_RT(!state.errors.load_(),
    state.error_msg ? state.error_msg : "state error");
  ZTLS_CHECK_RT(!state.server_connected.load_(),
    "TLS 1.2 rejection reached application connected()");
}

void testTLS13JumboBuffers(TempDir &temp, LogCapture &capture)
{
  ZuTestScopeRT(testTLS13JumboBuffers);
  run_in_process(temp, capture, JumboPayloadSize, JumboPayloadSize, false);
}

void testTLS13IPv6Loopback(TempDir &temp, LogCapture &capture)
{
  ZuTestScopeRT(testTLS13IPv6Loopback);
  run_in_process(
    temp, capture, SmallPayloadSize, SmallPayloadSize, false, false, false,
    ZiIP{"::1"}, "::1");
}

void testTLS13KeyUpdate(TempDir &temp, LogCapture &capture)
{
  ZuTestScopeRT(testTLS13KeyUpdate);
  SuiteList suites;
  auto suite = Ztls::Backend::cipher_suite(PTLS_CIPHER_SUITE_AES_128_GCM_SHA256);
  ZTLS_CHECK_RT(suite, "TLS 1.3 AES-GCM suite unavailable");
  if (!suite) return;
  suites.set(suite);
  run_in_process(
    temp, capture, SmallPayloadSize, SmallPayloadSize, true, false, false,
    ZiIP{"127.0.0.1"}, "127.0.0.1",
    suites.list, PTLS_CIPHER_SUITE_AES_128_GCM_SHA256);
}

void testTLS13ServerKeyUpdate(TempDir &temp, LogCapture &capture)
{
  ZuTestScopeRT(testTLS13ServerKeyUpdate);
  run_in_process(
    temp, capture, SmallPayloadSize, SmallPayloadSize, false, true);
}

void testTLS13QueuedClose(TempDir &temp, LogCapture &capture)
{
  ZuTestScopeRT(testTLS13QueuedClose);
  run_in_process(
    temp, capture, SmallPayloadSize, SmallPayloadSize, false, false, true);
}

void testTLS13ComposedHeadroom(TempDir &temp, LogCapture &capture)
{
  ZuTestScopeRT(testTLS13ComposedHeadroom);

  constexpr unsigned ExtraHeadroom = 31;
  constexpr unsigned ExtraTailroom = 17;
  constexpr unsigned RecordPayload =
    16 * 1024 - 325 - ExtraHeadroom - ExtraTailroom;
  constexpr unsigned Payload = RecordPayload * 2 + 1;

  for (auto suites = Ztls::Backend::cipher_suites(); *suites; ++suites) {
    SuiteList selected;
    selected.set(*suites);
    run_in_process(
      temp, capture, Payload, Payload, false, false, false,
      ZiIP{"127.0.0.1"}, "127.0.0.1",
      selected.list, (*suites)->id, ExtraHeadroom, ExtraTailroom);
  }
}

void testTLS12ExplicitIV(TempDir &temp, LogCapture &capture)
{
  (void)temp;
  (void)capture;
  ZuTestScopeRT(testTLS12ExplicitIV);
  auto suite = Ztls::Backend::tls12_ecdhe_rsa_aes128gcmsha256();
  ZTLS_CHECK_RT(suite, "TLS 1.2 AES-GCM suite unavailable");
  if (!suite) return;
  ZTLS_CHECK_RT(suite->aead->tls12.record_iv_size > 0,
    "TLS 1.2 AES-GCM should use an explicit record IV");
  run_tls12_records(suite);
}

void testTLS12NoExplicitIV(TempDir &temp, LogCapture &capture)
{
  (void)temp;
  (void)capture;
  ZuTestScopeRT(testTLS12NoExplicitIV);
  auto suite = Ztls::Backend::tls12_ecdhe_rsa_chacha20poly1305sha256();
  if (!suite) {
    std::cout << "# TLS 1.2 ChaCha20-Poly1305 unavailable in this backend\n";
    return;
  }
  ZTLS_CHECK_RT(!suite->aead->tls12.record_iv_size,
    "TLS 1.2 ChaCha20-Poly1305 should not use an explicit record IV");
  run_tls12_records(suite);
}

void testTLS12HandshakeRejected(TempDir &temp, LogCapture &capture)
{
  ZuTestScopeRT(testTLS12HandshakeRejected);
  run_tls12_handshake_rejected(temp, capture);
}

} // namespace

int main(int argc, char **argv)
{
  ZuTestUtil::parse(argc, argv);

  ZiLog::init("ZtlsBufHookTest");
  ZiLog::level(0);

  LogCapture capture;
  ZiLog::sink(ZiLog::lambdaSink([&capture](ZeLogBuf &buf, const ZeEventInfo &info) {
    capture.onLog(buf, info);
  }));
  ZiLog::start();

  ZuTestMain();
  TempDir temp;
  bool tempOK = temp.init();
  ZuCHECK(tempOK, "failed to generate cert/key");
  if (tempOK) {
    ZuTestCall(testConnectFailureShard, temp, capture);
    ZuTestCall(testTLS13JumboBuffers, temp, capture);
    ZuTestCall(testTLS13IPv6Loopback, temp, capture);
    ZuTestCall(testTLS13KeyUpdate, temp, capture);
    ZuTestCall(testTLS13ServerKeyUpdate, temp, capture);
    ZuTestCall(testTLS13QueuedClose, temp, capture);
    ZuTestCall(testTLS13ComposedHeadroom, temp, capture);
    ZuTestCall(testTLS12ExplicitIV, temp, capture);
    ZuTestCall(testTLS12NoExplicitIV, temp, capture);
    ZuTestCall(testTLS12HandshakeRejected, temp, capture);
  }

  ZiLog::stop();
}
