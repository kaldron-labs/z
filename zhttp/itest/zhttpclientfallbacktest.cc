//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Zhttp Client HTTP/3-to-TLS fallback test

#ifndef _WIN32
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#endif

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmSemaphore.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/ZhttpClient.hh>

#include "ZhttpTestUtil.hh"

using namespace ZuTestUtil;

namespace ZhttpClientFallbackTest_ {

struct Resolver {
  Resolver() : ops{
    .context = this,
    .query = query_,
    .resolve = resolve_,
    .cancel = cancel_
  } { }

  static ZmRef<ZiResolver_::Query> query_(
    void *context, ZiResolver_::Host, uint16_t, uint16_t,
    ZiResolver_::QueryFn fn) {
    auto self = static_cast<Resolver *>(context);
    self->queryFn = ZuMv(fn);
    self->queried.post();
    return {};
  }
  static ZmRef<ZiResolver_::Query> resolve_(
    void *, ZiResolver_::Host, ZiResolver_::ResolveFn fn) {
    if (fn(ZiResolver_::ResolveResult{ZiIP{"127.0.0.1"}}))
      fn(ZiResolver_::ResolveResult{});
    return {};
  }
  static void cancel_(void *context, ZmRef<ZiResolver_::Query>) {
    static_cast<Resolver *>(context)->queryFn =
      ZiResolver_::QueryFn{};
  }

  void noRecord() {
    ZiResolver_::DNSMsg msg;
    for (unsigned i = 0; i < 12; ++i) msg.buf.push(0);
    auto fn = ZuMv(queryFn);
    fn(ZiResolver_::QueryResult{ZuMv(msg)});
  }

  Zhttp::DiscoveryResolver ops;
  ZiResolver_::QueryFn	queryFn;
  ZmSemaphore		queried;
};

struct ClientApp;
struct Pool;
struct Request_;
struct ResParser;

struct Request_ : public ZmObject {
  using Headers = ZuTypeList<>;
  constexpr Zhttp::BodyPolicy::T bodyPolicy() const {
    return Zhttp::BodyPolicy::None;
  }

  void reset() { }
  template <typename L>
  void operation(L &&l) const {
    l(Zhttp::Method::GET, [this](auto &&emit) {
      emit([this](auto &tx) { tx << target; });
    });
  }
  template <typename L> void protocol(L &&) const { }
  template <typename Key, typename L> void header(L &&) const { }
  template <typename L> void header(L &&) const { }

  bool replayable() const { return true; }
  bool reproducible() const { return true; }
  void connected(const Zhttp::ConnectedInfo &) { }
  void disconnected(bool) { }
  void connectFailed(bool) { }
  void selected(const Zhttp::Endpoint &) { }
  void redirected(const Zhttp::URL &url_) {
    target.length(0);
    url_.writeTarget(target);
  }
  void observed(const Zhttp::ClientEvent &);
  void completed(const Zhttp::Result &);

  uint64_t key() const { return key_; }
  uint64_t length() const { return 1; }

  ClientApp		*app = nullptr;
  Zhttp::URLString	target{"/"};
  uint64_t		key_ = 0;
};

struct ResParser {
  using Headers = ZhttpHeaders("alt-svc");

  bool enable1xx() const { return false; }

  uint64_t	*bodyBytes = nullptr;
  unsigned	*status_ = nullptr;
  ZtString<>	*altSvc = nullptr;

  void init(const Request_ &);
  void status(unsigned value) { *status_ = value; }
  void bodyInfo(Zhttp::BodyType::T, uint64_t) { }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuBSpan value) {
    if constexpr (Key{}() == "alt-svc") *altSvc = value;
  }
  template <typename Rx>
  bool body(Rx &rx) {
    return Zhttp::bodyEach(rx,
      [this](ZuBSpan value) { *bodyBytes += value.length(); });
  }
  template <typename LinkRef>
  void complete(LinkRef &&, bool) { }
};

ZuDerive(RequestQ, (ZmPQueue<Request_,
  ZmPQueueOverlap<false,
    ZmPQueueNode<Request_,
      ZmPQueueHeapID<"Zhttp.Test.Fallback.Request">>>>));
using Request = RequestQ::Node;
using TxQ = ZmPQTx<Pool, RequestQ, ZmPQTxOrdered<false>>;

struct Pool : public Zhttp::Pool<ClientApp, TxQ, ResParser> {
  using Base = Zhttp::Pool<ClientApp, TxQ, ResParser>;

  Pool(ClientApp *client) : Base{client} { }

  RequestQ *txQueue() { return &m_requests; }
  void archive_(Request *) { }
  ZmRef<Request> retrieve_(RequestQ::Key, RequestQ::Key) { return {}; }

private:
  RequestQ	m_requests;
};

struct ClientApp : public Zhttp::Client<ClientApp, Pool> {
  using Base = Zhttp::Client<ClientApp, Pool>;

  void idle() { }

  ZmRef<Request> request() {
    ZmRef<Request> request = new Request;
    request->app = this;
    request->key_ = m_key++;
    return request;
  }

  void requestCompleted(const Zhttp::Result &result_) {
    result = result_;
    results.push(result_);
    done.post();
  }
  void requestObserved(const Zhttp::ClientEvent &event) {
    events.push(event);
  }
  unsigned eventCount(Zhttp::ClientEventType::T type) const {
    unsigned count = 0;
    for (unsigned i = 0; i < events.length(); ++i)
      count += events[i].type == type;
    return count;
  }

  Zhttp::Result	result;
  ZtArray<Zhttp::Result,
    ZtArrayHeapID<"Zhttp.Test.Fallback.Results">> results;
  ZtArray<Zhttp::ClientEvent,
    ZtArrayHeapID<"Zhttp.Test.Fallback">> events;
  ZmSemaphore	done;
  uint64_t	bodyBytes = 0;
  unsigned	status = 0;
  ZtString<>	altSvc;

private:
  uint64_t	m_key = 0;
};

void ResParser::init(const Request_ &req) {
  bodyBytes = &req.app->bodyBytes;
  status_ = &req.app->status;
  altSvc = &req.app->altSvc;
}

void Request_::observed(const Zhttp::ClientEvent &event) {
  app->requestObserved(event);
}
void Request_::completed(const Zhttp::Result &result) {
  app->requestCompleted(result);
}

ZiMxParams mxParams()
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

#ifndef _WIN32
struct ServerProcess {
  pid_t	pid = -1;
  int	eventFD = -1;
};

ServerProcess startServer(
  ZuCSpan root, uint16_t port, ZuCSpan cert, ZuCSpan key)
{
  int ready[2];
  if (::pipe(ready) < 0) return {};
#ifdef __linux__
  pid_t parent = ::getpid();
#endif
  pid_t pid = ::fork();
  if (pid) {
    ::close(ready[1]);
    if (pid < 0) {
      ::close(ready[0]);
      return {};
    }
    return {pid, ready[0]};
  }

  ::close(ready[0]);
#ifdef __linux__
  if (::prctl(PR_SET_PDEATHSIG, SIGKILL) < 0 || ::getppid() != parent)
    _exit(127);
#endif

  ZtString<> port_, eventFD;
  port_ << port;
  eventFD << ready[1];
  const char *server = "../util/zhttpd";
  ::execl(
    server, server, root.data(),
    "--https", "--http3", "--addr", "127.0.0.1", "--port", port_.data(),
    "--cert", cert.data(), "--key", key.data(), "--timeout", "5",
    "--event-fd", eventFD.data(),
    static_cast<char *>(nullptr));
  _exit(127);
}

bool serverResponses(int fd, unsigned count)
{
  while (count) {
    pollfd event{fd, POLLIN, 0};
    if (::poll(&event, 1, 10000) <= 0 || !(event.revents & POLLIN))
      return false;
    uint8_t value;
    if (::read(fd, &value, 1) != 1) return false;
    if (value != uint8_t(0x80 | Zhttp::ResponseOutcome::Success))
      return false;
    --count;
  }
  return true;
}

bool serverReady(int fd)
{
  bool tls = false, quic = false;
  while (!tls || !quic) {
    pollfd event{fd, POLLIN, 0};
    if (::poll(&event, 1, 10000) <= 0 || !(event.revents & POLLIN))
      return false;
    uint8_t value;
    if (::read(fd, &value, 1) != 1) return false;
    tls |= value == Zhttp::Transport::TLS;
    quic |= value == Zhttp::Transport::QUIC;
  }
  return true;
}

bool stopServer(ServerProcess &server)
{
  if (server.pid <= 0) return false;
  ::kill(server.pid, SIGTERM);
  int status = 0;
  bool stopped = ::waitpid(server.pid, &status, 0) == server.pid &&
    (WIFEXITED(status) || WIFSIGNALED(status));
  ::close(server.eventFD);
  server = {};
  return stopped;
}
#endif

void fallback()
{
  ZuTestScope(fallback);

#ifdef _WIN32
  ZuCHECK(true, "fallback process fixture is POSIX-only");
#else
  Zhttp::Test::TempDir temp;
  ZuCHECK(temp.init("ZhttpFallback"), "create temporary directory");
  ZtString<> cert, key;
  ZuCHECK(
    Zhttp::Test::writeLocalhostCert(temp, cert, key),
    "create localhost certificate");
  Zhttp::Test::TempDir untrusted;
  ZuCHECK(untrusted.init("ZhttpFallbackUntrusted"),
    "create untrusted certificate directory");
  ZtString<> untrustedCert, untrustedKey;
  ZuCHECK(
    Zhttp::Test::writeLocalhostCert(
      untrusted, untrustedCert, untrustedKey),
    "create untrusted QUIC certificate");

  auto root = temp.pathOf("root");
  ZuCHECK(ZiFile::mkdir(root) == Zi::OK, "create fallback document root");
  auto body = temp.pathOf("root/ok");
  ZiFile file;
  ZuCSpan expected{"ok\n"};
  ZuCHECK(file.open(body, ZiFile::Write, 0666) == Zi::OK &&
      file.write(expected.data(), expected.length()) == Zi::OK,
    "create fallback response body");
  file.close();

  uint16_t port = Zhttp::Test::loopbackPort();
  ZuCHECK(port, "allocate loopback port");
  if (!port) return;
  ServerProcess server = startServer(root, port, cert, key);
  ZuCHECK(server.pid > 0, "start TLS fallback server");
  if (server.pid <= 0) return;
  bool ready = serverReady(server.eventFD);
  ZuCHECK(ready, "fallback server listens for TLS");
  if (!ready) {
    (void)stopServer(server);
    return;
  }

  ZiMultiplex mx{mxParams()};
  bool mxUp = mx.start();
  ZuCHECK(mxUp, "start multiplexer");
  if (!mxUp) {
    (void)stopServer(server);
    return;
  }
  Zhttp::HubConfig hub{&mx, "3", "4"};

  Resolver resolver;
  ClientApp agent;
  auto agentConfig = Zhttp::Config()
    .concurrency(1).requestTimeout(10)
    .protocol(Zhttp::ProtocolPolicy::PreferH3).blindH3(true)
    .tcp(true).tls(true).quic(true);
  bool agentInited = agent.init(
    hub, 1, agentConfig, Zhttp::TCPConfig{},
    Zhttp::H2Config{}.caPath(cert),
    Zhttp::QUICConfig{}.caPath(untrustedCert).maxIdleTimeout(500));
  agentInited = agentInited && agent.pool(
    0, Zhttp::Destination{ZuBSpan{"127.0.0.1"}, port});
  agent.discoveryResolver(&resolver.ops);
  ZuCHECK(agentInited, "initialize prefer-mode agent");
  bool agentUp = agentInited && agent.start();
  ZuCHECK(agentUp, "start prefer-mode agent");

  if (agentUp) {
    auto request = agent.request();
    request->target = "/ok";
    {
      agent.send(0, request);
      agent.seal(0);
      bool queried =
	resolver.queried.timedwait(Zm::now(10)) == 0;
      ZuCHECK(queried, "agent requests HTTPS discovery");
      if (queried) {
	resolver.noRecord();
	bool completed = agent.done.timedwait(Zm::now(10)) == 0;
	ZuCHECK(completed, "H3 failure falls back to TLS");
	if (completed) {
	  ZuCHECK(agent.result.ok() &&
	      agent.result.transport == Zhttp::Transport::TLS &&
	      agent.result.httpVersion == Zhttp::Version::H2 &&
	      agent.status == 200 && agent.bodyBytes == expected.length(),
	    "fallback terminal result identifies TLS/H2");
	  ZuCHECK(agent.altSvc, "TLS response contains Alt-Svc");
	  ZuCHECK(agent.eventCount(Zhttp::ClientEventType::Selected) == 2 &&
	      agent.eventCount(Zhttp::ClientEventType::AttemptFailed) == 1 &&
	      agent.eventCount(Zhttp::ClientEventType::Fallback) == 1 &&
	      agent.eventCount(Zhttp::ClientEventType::Completed) == 1,
	    "typed H3-to-H2 fallback transition sequence");
	  uint64_t requestID = 0;
	  uint64_t failedAttempt = 0;
	  uint64_t fallbackAttempt = 0;
	  for (unsigned i = 0; i < agent.events.length(); ++i) {
	    const auto &event = agent.events[i];
	    if (event.type == Zhttp::ClientEventType::AttemptFailed) {
	      requestID = event.request;
	      failedAttempt = event.attempt;
	    } else if (event.type == Zhttp::ClientEventType::Fallback) {
	      fallbackAttempt = event.attempt;
	      ZuCHECK(event.request == requestID &&
		  event.previousAttempt == failedAttempt,
		"fallback retains request ID and links distinct attempts");
	    }
	  }
	  ZuCHECK(failedAttempt && fallbackAttempt &&
	      failedAttempt != fallbackAttempt &&
	      agent.result.request == requestID &&
	      agent.result.attempt == fallbackAttempt,
	    "fallback terminal identity uses the TLS attempt");
	}
      }
    }
  }

  ZuCHECK(!agentUp || serverResponses(server.eventFD, 1),
    "fallback response completes before client teardown");

  if (agentInited) agent.stop();
  if (agentInited) agent.final();

  Resolver cacheResolver;
  ClientApp cachedClient;
  auto cacheConfig = Zhttp::Config()
    .concurrency(1).requestTimeout(10)
    .protocol(Zhttp::ProtocolPolicy::PreferH3).blindH3(false)
    .tcp(true).tls(true).quic(true);
  bool cacheInited = cachedClient.init(
    hub, 1, cacheConfig, Zhttp::TCPConfig{},
    Zhttp::H2Config{}.caPath(cert),
    Zhttp::QUICConfig{}.caPath(cert).maxIdleTimeout(500));
  cacheInited = cacheInited && cachedClient.pool(
    0, Zhttp::Destination{ZuBSpan{"127.0.0.1"}, port});
  cachedClient.discoveryResolver(&cacheResolver.ops);
  ZuCHECK(cacheInited, "initialize cached-routing agent");
  bool cacheUp = cacheInited && cachedClient.start();
  ZuCHECK(cacheUp, "start cached-routing agent");
  if (cacheUp) {
    auto request0 = cachedClient.request();
    auto request1 = cachedClient.request();
    request0->target = "/ok";
    request1->target = "/ok";
    {
      cachedClient.send(0, request0);
      cachedClient.send(0, request1);
      cachedClient.seal(0);
      bool queried =
	cacheResolver.queried.timedwait(Zm::now(10)) == 0;
      ZuCHECK(queried, "first cached-routing request performs discovery");
      if (queried) {
	cacheResolver.noRecord();
	bool completed =
	  cachedClient.done.timedwait(Zm::now(10)) == 0 &&
	  cachedClient.done.timedwait(Zm::now(10)) == 0;
	ZuCHECK(completed, "TLS and cached H3 requests complete");
	if (completed) {
	  ZuCHECK(cachedClient.results.length() == 2 &&
	      cachedClient.results[0].ok() && cachedClient.results[1].ok() &&
	      cachedClient.results[0].transport == Zhttp::Transport::TLS &&
	      cachedClient.results[1].transport == Zhttp::Transport::QUIC &&
	      cachedClient.results[0].httpVersion == Zhttp::Version::H2 &&
	      cachedClient.results[1].httpVersion == Zhttp::Version::H3 &&
	      cachedClient.status == 200 &&
	      cachedClient.bodyBytes == expected.length() * 2,
	    "Alt-Svc moves the second request from TLS/H2 to QUIC/H3");
	  bool cached = false;
	  for (unsigned i = 0; i < cachedClient.events.length(); ++i) {
	    const auto &event = cachedClient.events[i];
	    if (event.type == Zhttp::ClientEventType::Selected &&
		event.transport == Zhttp::Transport::QUIC &&
		event.endpointSource == Zhttp::EndpointSource::AltSvc)
	      cached = true;
	  }
	  ZuCHECK(cached,
	    "second request selects H3 from library-managed Alt-Svc cache");
	}
      }
    }
  }
  ZuCHECK(!cacheUp || serverResponses(server.eventFD, 2),
    "Alt-Svc response wave completes before client teardown");
  if (cacheInited) cachedClient.stop();
  if (cacheInited) cachedClient.final();

  ClientApp forcedClient;
  auto forcedDefaults = Zhttp::Config()
    .concurrency(1).requestTimeout(10)
    .protocol(Zhttp::ProtocolPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);
  bool forcedInited = forcedClient.init(
    hub, 1, forcedDefaults, Zhttp::TCPConfig{}, Zhttp::H2Config{},
    Zhttp::QUICConfig{}.caPath(cert).maxIdleTimeout(500));
  forcedInited = forcedInited && forcedClient.pool(
    0, Zhttp::Destination{ZuBSpan{"127.0.0.1"}, port},
    Zhttp::Config{}
      .protocol(Zhttp::ProtocolPolicy::ForceH3).blindH3(true)
      .tls(false).quic(true));
  ZuCHECK(forcedInited, "initialize forced-H3 pool override");
  bool forcedUp = forcedInited && forcedClient.start();
  ZuCHECK(forcedUp, "start forced-H3 agent");
  if (forcedUp) {
    auto request = forcedClient.request();
    request->target = "/ok";
    forcedClient.send(0, request);
    forcedClient.seal(0);
    bool completed = forcedClient.done.timedwait(Zm::now(10)) == 0;
    ZuCHECK(completed && forcedClient.result.ok() &&
	forcedClient.result.transport == Zhttp::Transport::QUIC &&
	forcedClient.result.httpVersion == Zhttp::Version::H3 &&
	forcedClient.eventCount(Zhttp::ClientEventType::Fallback) == 0,
      "forced-H3 pool completes over QUIC without TLS fallback");
  }
  ZuCHECK(!forcedUp || serverResponses(server.eventFD, 1),
    "forced-H3 response completes before client teardown");
  if (forcedInited) forcedClient.stop();
  if (forcedInited) forcedClient.final();

  ClientApp disabledClient;
  auto disabledDefaults = Zhttp::Config()
    .concurrency(1).requestTimeout(10)
    .protocol(Zhttp::ProtocolPolicy::ForceH3)
    .h2Policy(Zhttp::H2Policy::Disable)
    .tcp(true).tls(false).quic(true);
  bool disabledInited = disabledClient.init(
    hub, 1, disabledDefaults, Zhttp::TCPConfig{},
    Zhttp::H2Config{}.caPath(cert), Zhttp::QUICConfig{});
  disabledInited = disabledInited && disabledClient.pool(
    0, Zhttp::Destination{ZuBSpan{"127.0.0.1"}, port},
    Zhttp::Config{}
      .protocol(Zhttp::ProtocolPolicy::DisableH3)
      .h2Policy(Zhttp::H2Policy::Force)
      .tls(true).quic(false));
  ZuCHECK(disabledInited, "initialize H3-disabled pool override");
  bool disabledUp = disabledInited && disabledClient.start();
  ZuCHECK(disabledUp, "start H3-disabled agent");
  if (disabledUp) {
    auto request = disabledClient.request();
    request->target = "/ok";
    disabledClient.send(0, request);
    disabledClient.seal(0);
    bool completed = disabledClient.done.timedwait(Zm::now(10)) == 0;
    bool selectedQUIC = false;
    for (const auto &event: disabledClient.events)
      selectedQUIC |= event.type == Zhttp::ClientEventType::Selected &&
	event.transport == Zhttp::Transport::QUIC;
    ZuCHECK(completed && disabledClient.result.ok() &&
	disabledClient.result.transport == Zhttp::Transport::TLS &&
	disabledClient.result.httpVersion == Zhttp::Version::H2 &&
	!selectedQUIC,
      "H3-disabled pool remains on TLS/H2 despite advertised Alt-Svc");
  }
  ZuCHECK(!disabledUp || serverResponses(server.eventFD, 1),
    "H3-disabled response completes before client teardown");
  if (disabledInited) disabledClient.stop();
  if (disabledInited) disabledClient.final();

  mx.stop();
  ZuCHECK(stopServer(server), "stop TLS fallback server");
#endif
}

} // namespace ZhttpClientFallbackTest_

int main(int argc, char **argv)
{
  using namespace ZhttpClientFallbackTest_;

  (void)argc;
  (void)argv;
  ZuTestMain();
  ZuTestCall(fallback);
  return 0;
}
