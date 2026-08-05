//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Zhttp Client HTTP/3-to-TLS fallback test

#ifndef _WIN32
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
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
struct Request_;
struct ResParser;

struct Request_ : public ZmObject {
  using Headers = ZuTypeList<>;
  using BodyPolicy = Zhttp::Body::None;

  void reset() { }
  template <typename L>
  void operation(L &&l) const {
    auto url_ = url.url();
    l(Zhttp::Method::GET, url_.pathQuery());
  }
  template <typename L>
  void host(L &&l) const { l(url.url().authority()); }
  template <typename L> void protocol(L &&) const { }
  template <typename Key, typename L> void header(L &&) const { }
  template <typename L> void header(L &&) const { }

  bool replayable() const { return true; }
  bool reproducible() const { return true; }
  void connected(const Zhttp::ConnectedInfo &) { }
  void disconnected(bool) { }
  void connectFailed(bool) { }
  void selected(const Zhttp::Endpoint &) { }
  void redirected(const Zhttp::URL &url_) { url.assign(url_.raw); }
  void observed(const Zhttp::ClientEvent &);
  void completed(const Zhttp::Result &);

  uint64_t key() const { return key_; }
  uint64_t length() const { return 1; }

  ClientApp		*app = nullptr;
  Zhttp::URLStorage	url;
  uint64_t		key_ = 0;
};

struct ResParser {
  using Headers = ZhttpHeaders("alt-svc");
  static constexpr uint64_t BodyMax = 1024;

  uint64_t	*bodyBytes = nullptr;
  unsigned	*status_ = nullptr;
  ZtString<>	*altSvc = nullptr;

  void init(const Request_ &);
  void status(unsigned value) { *status_ = value; }
  void contentLength(uint64_t) { }
  void chunked() { }
  void version(ZuBSpan) { }
  template <typename Key>
  void header(ZuBSpan value) {
    if constexpr (Key{}() == "alt-svc") *altSvc = ZuCSpan{value};
  }
  template <typename Rx>
  void body(Rx &rx) {
    Zhttp::bodyEach(rx,
      [this](ZuBSpan value) { *bodyBytes += value.length(); });
  }
  template <typename State> void complete(State) { }
};

ZuDerive(RequestQ, (ZmPQueue<Request_,
  ZmPQueueOverlap<false,
    ZmPQueueNode<Request_,
      ZmPQueueHeapID<"Zhttp.Test.Fallback.Request">>>>));
using Request = RequestQ::Node;
using TxQ = ZmPQTx<ClientApp, RequestQ, ZmPQTxOrdered<false>>;

struct ClientApp : public Zhttp::Client<TxQ, ResParser> {
  using Base = Zhttp::Client<TxQ, ResParser>;

  RequestQ *txQueue() { return &m_requests; }
  void archive_(Request *) { }
  ZmRef<Request> retrieve_(RequestQ::Key, RequestQ::Key) { return {}; }
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
  RequestQ	m_requests;
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
pid_t startServer(
  ZuCSpan root, uint16_t port, ZuCSpan cert, ZuCSpan key)
{
  pid_t pid = ::fork();
  if (pid) return pid;

  ZtString<> port_;
  port_ << port;
  const char *server = "../util/zhttpd";
  ::execl(
    server, server, root.data(),
    "--https", "--http3", "--addr", "127.0.0.1", "--port", port_.data(),
    "--cert", cert.data(), "--key", key.data(), "--timeout", "5",
    static_cast<char *>(nullptr));
  _exit(127);
}

bool serverReady(uint16_t port)
{
  for (unsigned i = 0; i < 100; ++i) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd >= 0) {
      sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      addr.sin_port = htons(port);
      if (!::connect(
	  fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr))) {
	::close(fd);
	return true;
      }
      ::close(fd);
    }
    Zhttp::Test::sleepMS(20);
  }
  return false;
}

bool stopServer(pid_t pid)
{
  if (pid <= 0) return false;
  ::kill(pid, SIGTERM);
  int status = 0;
  return ::waitpid(pid, &status, 0) == pid &&
    (WIFEXITED(status) || WIFSIGNALED(status));
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
  pid_t server = startServer(root, port, cert, key);
  ZuCHECK(server > 0, "start TLS fallback service");
  if (server <= 0) return;
  bool ready = serverReady(port);
  ZuCHECK(ready, "fallback service listens for TLS");
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
  agent.discoveryResolver(&resolver.ops);
  auto agentConfig = Zhttp::ClientConfig()
    .concurrency(1).requestTimeout(10)
    .protocol(Zhttp::ProtocolPolicy::PreferH3).blindH3(true)
    .tcp(true).tls(true).quic(true);
  bool agentInited = agent.init(
    hub, agentConfig, Zhttp::TCPConfig{},
    Zhttp::H2Config{}.caPath(cert),
    Zhttp::QUICConfig{}.caPath(untrustedCert).maxIdleTimeout(500));
  ZuCHECK(agentInited, "initialize prefer-mode agent");
  bool agentUp = agentInited && agent.start();
  ZuCHECK(agentUp, "start prefer-mode agent");

  if (agentUp) {
    auto request = agent.request();
    ZtString<> url;
    url << "https://127.0.0.1:" << port << "/ok";
    bool parsed = request->url.assign(url).ok();
    ZuCHECK(parsed, "parse fallback URL");
    if (parsed) {
      agent.enqueue(request);
      agent.seal();
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

  if (agentInited) agent.stop();
  if (agentInited) agent.final();

  Resolver cacheResolver;
  ClientApp cachedClient;
  cachedClient.discoveryResolver(&cacheResolver.ops);
  auto cacheConfig = Zhttp::ClientConfig()
    .concurrency(1).requestTimeout(10)
    .protocol(Zhttp::ProtocolPolicy::PreferH3).blindH3(false)
    .tcp(true).tls(true).quic(true);
  bool cacheInited = cachedClient.init(
    hub, cacheConfig, Zhttp::TCPConfig{},
    Zhttp::H2Config{}.caPath(cert),
    Zhttp::QUICConfig{}.caPath(cert).maxIdleTimeout(500));
  ZuCHECK(cacheInited, "initialize cached-routing agent");
  bool cacheUp = cacheInited && cachedClient.start();
  ZuCHECK(cacheUp, "start cached-routing agent");
  if (cacheUp) {
    auto request0 = cachedClient.request();
    auto request1 = cachedClient.request();
    ZtString<> url;
    url << "https://127.0.0.1:" << port << "/ok";
    bool parsed =
      request0->url.assign(url).ok() &&
      request1->url.assign(url).ok();
    ZuCHECK(parsed, "parse cached-routing URLs");
    if (parsed) {
      cachedClient.enqueue(request0);
      cachedClient.enqueue(request1);
      cachedClient.seal();
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
  if (cacheInited) cachedClient.stop();
  if (cacheInited) cachedClient.final();
  mx.stop();
  ZuCHECK(stopServer(server), "stop TLS fallback service");
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
