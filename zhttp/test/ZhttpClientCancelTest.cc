//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// multi-protocol client cancellation and shutdown tests

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>

#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiPlatform.hh>
#include <zlib/ZiResolver.hh>

#include <zlib/ZhttpClient.hh>

using namespace ZuTestUtil;

namespace ZhttpClientCancelTest_ {

struct App;
struct Request_;
struct ResParser;

struct Request_ : public ZmObject {
  using Headers = ZuTypeList<>;
  constexpr Zhttp::BodyPolicy::T bodyPolicy() const {
    return Zhttp::BodyPolicy::None;
  }

  void reset() { ++resets; }
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

  bool replayable() const;
  bool reproducible() const;
  void connected(const Zhttp::ConnectedInfo &) { }
  void disconnected(bool) { }
  void connectFailed(bool) { }
  void selected(const Zhttp::Endpoint &) { }
  void redirected(const Zhttp::URL &);
  void observed(const Zhttp::ClientEvent &);
  void completed(const Zhttp::Result &);

  uint64_t key() const { return key_; }
  uint64_t length() const { return 1; }

  App			*app = nullptr;
  Zhttp::URLStorage	url;
  uint64_t		key_ = 0;
  unsigned		resets = 0;
  mutable unsigned	inits = 0;
};

int listenerAt(uint16_t);

struct ResParser {
  using Headers = ZuTypeList<ZuStringT<"location">, void>;
  static constexpr uint64_t BodyMax = 1024;

  void init(const Request_ &req) { ++req.inits; }
  void status(unsigned status__) { status_ = status__; }
  void contentLength(uint64_t) { }
  void chunked() { }
  void version(ZuBSpan) { }
  template <typename Key> void header(ZuBSpan) { }
  template <typename Rx> void body(Rx &rx) { Zhttp::bodyDrain(rx); }
  template <typename State> void complete(State ok_) {
    completed = true;
    ok = ok_;
  }

  unsigned	status_ = 0;
  bool		completed = false;
  bool		ok = false;
};

ZuDerive(RequestQ, (ZmPQueue<Request_,
  ZmPQueueOverlap<false,
    ZmPQueueNode<Request_,
      ZmPQueueHeapID<"Zhttp.Test.Request">>>>));
using Request = RequestQ::Node;
using TxQ = ZmPQTx<App, RequestQ, ZmPQTxOrdered<false>>;

struct App : public Zhttp::Client<TxQ, ResParser> {
  using Base = Zhttp::Client<TxQ, ResParser>;

  RequestQ *txQueue() { return &m_requests; }
  void archive_(Request *request) {
    archives.push(request->key());
    archiveDone.post();
  }
  ZmRef<Request> retrieve_(RequestQ::Key, RequestQ::Key) { return {}; }

  ZmRef<Request> request() {
    ZmRef<Request> request = new Request;
    request->app = this;
    request->key_ = m_key++;
    return request;
  }

  void requestCompleted(const Zhttp::Result &result) {
    results.push(result);
    if (results.length() == expected) done.post();
  }

  void requestObserved(const Zhttp::ClientEvent &event) {
    events.push(event);
    if (event.type == Zhttp::ClientEventType::AttemptFailed &&
	retryPort && retryFD < 0) {
      retryFD = listenerAt(retryPort);
      serverReady.post();
    }
  }

  unsigned eventCount(Zhttp::ClientEventType::T type) const {
    unsigned count = 0;
    for (unsigned i = 0; i < events.length(); ++i)
      count += events[i].type == type;
    return count;
  }

  ZtArray<Zhttp::Result, ZtArrayHeapID<"Zhttp.Test.Results">> results;
  ZtArray<Zhttp::ClientEvent, ZtArrayHeapID<"Zhttp.Test.Events">> events;
  ZtArray<uint64_t, ZtArrayHeapID<"Zhttp.Test.Archives">> archives;
  ZmSemaphore	done;
  ZmSemaphore	archiveDone;
  ZmSemaphore	serverReady;
  unsigned	expected = 0;
  uint16_t	retryPort = 0;
  int		retryFD = -1;
  bool		replayable_ = true;

private:
  RequestQ	m_requests;
  uint64_t	m_key = 0;
};

bool Request_::replayable() const { return app->replayable_; }
bool Request_::reproducible() const { return app->replayable_; }
void Request_::redirected(const Zhttp::URL &url_) { url.assign(url_.raw); }
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

void attemptState()
{
  ZuTestScope(attemptState);

  struct Link {
    void responseHeadersParsed(App::LiveReq *) { ++headers; }
    void complete(bool ok_) { completed = true; ok = ok_; }

    unsigned	headers = 0;
    bool	completed = false;
    bool	ok = false;
  };
  struct ParserState {
    using T = int;
    enum { Complete, Failed };
  };

  App app;
  App::LiveReq attempt;
  ZmRef<Request> request = new Request;
  attempt.request = request;
  attempt.events = Zhttp::AttemptEvent{}.SelectionObserved() |
    Zhttp::AttemptEvent{}.FailureObserved();

  ZuCHECK(attempt.request == request,
    "live request retains the intrusive request");
  ZuCHECK(attempt.phase == Zhttp::AttemptPhase::Idle &&
      attempt.failure.kind == Zhttp::FailureKind::None &&
      attempt.route.redirectState == Zhttp::RedirectState::None &&
      attempt.protocol.persistence == Zhttp::Persistence::Default,
    "attempt state groups have idle defaults");
  ZuCHECK(attempt.events & Zhttp::AttemptEvent{}.SelectionObserved() &&
      attempt.events & Zhttp::AttemptEvent{}.FailureObserved(),
    "independent attempt observation flags compose");
  ZuCHECK(Zhttp::AttemptPhase{}.name(Zhttp::AttemptPhase::ReceivingBody) ==
      "ReceivingBody" &&
      Zhttp::FailureKind{}.name(Zhttp::FailureKind::Protocol) == "Protocol" &&
      Zhttp::RedirectState{}.name(Zhttp::RedirectState::Invalid) == "Invalid" &&
      Zhttp::Persistence{}.name(Zhttp::Persistence::KeepAlive) == "KeepAlive",
    "attempt state enums expose stable names");

  Link link;
  ResParser parser;
  app.poolSend(link, attempt, Zhttp::Version::H1);
  ZuCHECK(attempt.phase == Zhttp::AttemptPhase::Sending,
    "request transmission enters sending phase");
  app.status(link, attempt, parser, 200);
  ZuCHECK(attempt.phase == Zhttp::AttemptPhase::ReceivingHeaders &&
      attempt.protocol.status == 200 && parser.status_ == 200,
    "response status enters receiving-headers phase");
  app.complete<ParserState>(
    link, attempt, parser, ParserState::Complete);
  ZuCHECK(attempt.phase == Zhttp::AttemptPhase::Closing &&
      link.headers == 1 && link.completed && link.ok &&
      parser.completed && parser.ok,
    "response completion enters closing phase exactly once");

  App::LiveReq failed;
  failed.request = request;
  Zhttp::BodyCommit commit{
    .produced = 9, .committed = 7, .reset = 2, .discarded = 2,
    .headers = true, .final = false};
  app.poolTxFailed(link, failed, commit);
  ZuCHECK(failed.failure.kind == Zhttp::FailureKind::Tx &&
      failed.requestBody.produced == 9 &&
      failed.requestBody.committed == 7 &&
      failed.requestBody.headers && !failed.requestBody.final,
    "Tx failure retains the complete request-body commit snapshot");

  failed.failure = {};
  failed.protocol.http10 = true;
  ZuCHECK(!app.poolReusable(failed),
    "HTTP/1.0 defaults to a non-persistent connection");
  failed.protocol.persistence = Zhttp::Persistence::KeepAlive;
  ZuCHECK(app.poolReusable(failed),
    "HTTP/1.0 keep-alive is represented by one persistence state");
  failed.protocol.closeDelimited = true;
  ZuCHECK(!app.poolReusable(failed),
    "close-delimited framing remains orthogonal to persistence");
}

int listener(uint16_t &port)
{
  port = 0;
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0 ||
      ::listen(fd, 2) < 0) {
    ::close(fd);
    return -1;
  }
  socklen_t len = sizeof(addr);
  if (::getsockname(
      fd, reinterpret_cast<sockaddr *>(&addr), &len) < 0) {
    ::close(fd);
    return -1;
  }
  port = ntohs(addr.sin_port);
  return fd;
}

int listenerAt(uint16_t port)
{
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  int one = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0 ||
      ::listen(fd, 1) < 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

bool readRequest(int fd)
{
  uint8_t buf[4096];
  unsigned length = 0;
  while (length < sizeof(buf)) {
    ssize_t n = ::recv(fd, buf + length, sizeof(buf) - length, 0);
    if (n <= 0) return false;
    length += unsigned(n);
    if (length >= 4 && ::memmem(buf, length, "\r\n\r\n", 4))
      return true;
  }
  return false;
}

bool sendResponse(int fd, ZuCSpan response)
{
  unsigned offset = 0;
  while (offset < response.length()) {
    ssize_t n = ::send(
      fd, response.data() + offset, response.length() - offset, 0);
    if (n <= 0) return false;
    offset += unsigned(n);
  }
  return true;
}

bool readRequestKey(int fd, unsigned &key)
{
  uint8_t buf[4096];
  unsigned length = 0;
  while (length < sizeof(buf)) {
    ssize_t n = ::recv(fd, buf + length, sizeof(buf) - length, 0);
    if (n <= 0) return false;
    length += unsigned(n);
    if (length >= 4 && ::memmem(buf, length, "\r\n\r\n", 4)) {
      if (::memmem(buf, length, "GET /0 ", 7)) key = 0;
      else if (::memmem(buf, length, "GET /1 ", 7)) key = 1;
      else return false;
      return true;
    }
  }
  return false;
}

bool serveOutOfOrder(int listener, App &app)
{
  int a = ::accept(listener, nullptr, nullptr);
  int b = ::accept(listener, nullptr, nullptr);
  ::close(listener);
  if (a < 0 || b < 0) {
    if (a >= 0) ::close(a);
    if (b >= 0) ::close(b);
    return false;
  }
  unsigned aKey, bKey;
  if (!readRequestKey(a, aKey) || !readRequestKey(b, bKey) || aKey == bKey) {
    ::close(a);
    ::close(b);
    return false;
  }
  int fd0 = aKey ? b : a;
  int fd1 = aKey ? a : b;
  static constexpr ZuCSpan response =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 2\r\n"
    "Connection: close\r\n"
    "\r\n"
    "ok";
  bool ok = sendResponse(fd1, response);
  ::shutdown(fd1, SHUT_RDWR);
  ::close(fd1);
  if (ok) ok = app.archiveDone.timedwait(Zm::now(10)) == 0;
  if (ok) ok = sendResponse(fd0, response);
  ::shutdown(fd0, SHUT_RDWR);
  ::close(fd0);
  return ok;
}

bool serve(int listener)
{
  int fd = ::accept(listener, nullptr, nullptr);
  ::close(listener);
  if (fd < 0) return false;
  static constexpr ZuCSpan response =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 2\r\n"
    "Connection: close\r\n"
    "\r\n"
    "ok";
  bool ok = readRequest(fd) && sendResponse(fd, response);
  ::shutdown(fd, SHUT_RDWR);
  ::close(fd);
  return ok;
}

bool serveRedirect(int listener, bool follow)
{
  int fd = ::accept(listener, nullptr, nullptr);
  ::close(listener);
  if (fd < 0) return false;
  static constexpr ZuCSpan redirect =
    "HTTP/1.1 302 Found\r\n"
    "Location: /next\r\n"
    "Content-Length: 0\r\n"
    "\r\n";
  static constexpr ZuCSpan response =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 2\r\n"
    "Connection: close\r\n"
    "\r\n"
    "ok";
  bool ok = readRequest(fd) && sendResponse(fd, redirect);
  if (ok && follow)
    ok = readRequest(fd) && sendResponse(fd, response);
  ::shutdown(fd, SHUT_RDWR);
  ::close(fd);
  return ok;
}

void cancel()
{
  ZuTestScope(cancel);

  uint16_t port;
  int fd = listener(port);
  ZuCHECK(fd >= 0 && port, "create stalled loopback listener");
  if (fd < 0) return;

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start multiplexer");

  App app;
  app.expected = 2;
  auto config = Zhttp::ClientConfig()
    .concurrency(1)
    .protocol(Zhttp::ProtocolPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);
  ZuCHECK(app.init(
      Zhttp::HubConfig{&mx, "3", "4"}, config,
      Zhttp::TCPConfig{}, Zhttp::H2Config{}, Zhttp::QUICConfig{}),
    "initialize agent");
  ZuCHECK(app.start(), "start agent");

  auto request0 = app.request();
  auto request1 = app.request();
  ZtString<> url;
  url << "http://127.0.0.1:" << port << '/';
  ZuCHECK(request0->url.assign(url).ok(), "parse URL");
  request1->url = request0->url;

  app.enqueue(request0);
  app.enqueue(request1);
  app.seal();
  app.cancel(request1->key());
  app.cancel(request0->key());
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "queued and active cancellation completes");
  app.stop();

  ZuCHECK(app.Base::completed() == 2 && app.results.length() == 2,
    "queued and active requests complete exactly once");
  ZuCHECK(app.results[0].code == Zhttp::ResultCode::Cancelled &&
      app.results[1].code == Zhttp::ResultCode::Cancelled,
    "cancellation result classification");
  ZuCHECK(app.eventCount(Zhttp::ClientEventType::Cancelled) == 2 &&
      app.eventCount(Zhttp::ClientEventType::Completed) == 2,
    "typed cancellation and completion events");

  app.final();
  mx.stop();
  ::close(fd);
}

void outOfOrder()
{
  ZuTestScope(outOfOrder);

  uint16_t port;
  int fd = listener(port);
  ZuCHECK(fd >= 0 && port, "create out-of-order loopback listener");
  if (fd < 0) return;

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start multiplexer");

  App app;
  app.expected = 2;
  ZmAtomic<unsigned> serverOK = 0;
  ZmThread server{[fd, &app, &serverOK]() {
    serverOK.store_(serveOutOfOrder(fd, app));
  }};
  auto config = Zhttp::ClientConfig()
    .concurrency(2)
    .protocol(Zhttp::ProtocolPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);
  ZuCHECK(app.init(
      Zhttp::HubConfig{&mx, "3", "4"}, config,
      Zhttp::TCPConfig{}, Zhttp::H2Config{}, Zhttp::QUICConfig{}),
    "initialize agent");
  ZuCHECK(app.start(), "start agent");

  auto request0 = app.request();
  auto request1 = app.request();
  ZtString<> url;
  url << "http://127.0.0.1:" << port;
  ZtString<> url0{url};
  ZtString<> url1{url};
  url0 << "/0";
  url1 << "/1";
  ZuCHECK(request0->url.assign(url0).ok() &&
      request1->url.assign(url1).ok(),
    "parse out-of-order URLs");

  app.enqueue(request0);
  app.enqueue(request1);
  app.seal();
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "out-of-order requests complete");
  app.stop();
  server.join();

  ZuCHECK(serverOK.load_(), "server completes key 1 before key 0");
  ZuCHECK(app.archives.length() == 2 &&
      app.archives[0] == request1->key() &&
      app.archives[1] == request0->key(),
    "unordered acknowledgements retire exact nodes independently");
  ZuCHECK(app.Base::completed() == 2 && app.results.length() == 2,
    "out-of-order requests complete exactly once");

  app.final();
  mx.stop();
}

void timeout()
{
  ZuTestScope(timeout);

  uint16_t port;
  int fd = listener(port);
  ZuCHECK(fd >= 0 && port, "create stalled loopback listener");
  if (fd < 0) return;

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start multiplexer");

  App app;
  app.expected = 1;
  auto config = Zhttp::ClientConfig()
    .concurrency(1).requestTimeout(1)
    .protocol(Zhttp::ProtocolPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);
  ZuCHECK(app.init(
      Zhttp::HubConfig{&mx, "3", "4"}, config,
      Zhttp::TCPConfig{}, Zhttp::H2Config{}, Zhttp::QUICConfig{}),
    "initialize agent");
  ZuCHECK(app.start(), "start agent");

  auto request = app.request();
  ZtString<> url;
  url << "http://127.0.0.1:" << port << '/';
  ZuCHECK(request->url.assign(url).ok(), "parse URL");

  app.enqueue(request);
  app.seal();
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0, "request timeout completes");
  ZuCHECK(app.Base::completed() == 1 && app.results.length() == 1 &&
      app.results[0].code == Zhttp::ResultCode::TimedOut,
    "timeout result classification");
  app.stop();
  ZuCHECK(app.eventCount(Zhttp::ClientEventType::Completed) == 1 &&
      app.events[app.events.length() - 1].type ==
	Zhttp::ClientEventType::Completed,
    "typed timeout completion event");
  app.final();
  mx.stop();
  ::close(fd);
}

void retry()
{
  ZuTestScope(retry);

  uint16_t port;
  int reserve = listener(port);
  ZuCHECK(reserve >= 0 && port, "reserve retry port");
  if (reserve < 0) return;
  ::close(reserve);

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start multiplexer");

  App app;
  app.expected = 1;
  app.retryPort = port;
  ZmAtomic<unsigned> serverOK = 0;
  ZmThread server{[&app, &serverOK]() {
    if (app.serverReady.timedwait(Zm::now(10)) == 0)
      serverOK.store_(serve(app.retryFD));
  }};

  auto config = Zhttp::ClientConfig()
    .concurrency(1).maxRetries(1)
    .protocol(Zhttp::ProtocolPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);
  ZuCHECK(app.init(
      Zhttp::HubConfig{&mx, "3", "4"}, config,
      Zhttp::TCPConfig{}, Zhttp::H2Config{}, Zhttp::QUICConfig{}),
    "initialize retry agent");
  ZuCHECK(app.start(), "start retry agent");

  auto request = app.request();
  ZtString<> url;
  url << "http://127.0.0.1:" << port << '/';
  ZuCHECK(request->url.assign(url).ok(), "parse retry URL");
  app.enqueue(request);
  app.seal();
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "transient failure retry completes");
  app.stop();
  server.join();

  ZuCHECK(serverOK.load_(), "retry server receives request");
  ZuCHECK(app.results.length() == 1 && app.results[0].ok() &&
      app.results[0].retries == 1,
    "retry produces one successful terminal result");
  ZuCHECK(request->resets == 1 && request->inits == 1,
    "connect retry initializes request builder and response parser once");
  ZuCHECK(app.eventCount(Zhttp::ClientEventType::Selected) == 2 &&
      app.eventCount(Zhttp::ClientEventType::AttemptFailed) == 1 &&
      app.eventCount(Zhttp::ClientEventType::Retried) == 1 &&
      app.eventCount(Zhttp::ClientEventType::Completed) == 1,
    "typed retry transition sequence");
  uint64_t requestID = 0;
  uint64_t firstAttempt = 0;
  uint64_t secondAttempt = 0;
  for (unsigned i = 0; i < app.events.length(); ++i) {
    const auto &event = app.events[i];
    if (event.type == Zhttp::ClientEventType::AttemptFailed) {
      requestID = event.request;
      firstAttempt = event.attempt;
      ZuCHECK(event.transient, "retry failure is classified transient");
    } else if (event.type == Zhttp::ClientEventType::Retried) {
      secondAttempt = event.attempt;
      ZuCHECK(event.request == requestID &&
	  event.previousAttempt == firstAttempt,
	"retry retains request ID and links distinct attempts");
    }
  }
  ZuCHECK(firstAttempt && secondAttempt && firstAttempt != secondAttempt,
    "retry attempt IDs are distinct");

  app.final();
  mx.stop();
}

void redirect()
{
  ZuTestScope(redirect);

  uint16_t port;
  int fd = listener(port);
  ZuCHECK(fd >= 0 && port, "create redirect listener");
  if (fd < 0) return;
  ZmAtomic<unsigned> serverOK = 0;
  ZmThread server{[fd, &serverOK]() {
    serverOK.store_(serveRedirect(fd, true));
  }};

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start multiplexer");
  App app;
  app.expected = 1;
  auto config = Zhttp::ClientConfig()
    .concurrency(1).maxRedirects(2)
    .protocol(Zhttp::ProtocolPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);
  ZuCHECK(app.init(
      Zhttp::HubConfig{&mx, "3", "4"}, config,
      Zhttp::TCPConfig{}, Zhttp::H2Config{}, Zhttp::QUICConfig{}),
    "initialize redirect agent");
  ZuCHECK(app.start(), "start redirect agent");

  auto request = app.request();
  ZtString<> url;
  url << "http://127.0.0.1:" << port << "/start";
  ZuCHECK(request->url.assign(url).ok(), "parse redirect URL");
  app.enqueue(request);
  app.seal();
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0, "redirect completes");
  app.stop();
  server.join();

  ZuCHECK(serverOK.load_(), "redirect server receives both requests");
  ZuCHECK(app.results.length() == 1 && app.results[0].ok() &&
      app.results[0].redirects == 1,
    "redirect produces one successful terminal result");
  ZuCHECK(request->resets == 2 && request->inits == 2,
    "redirect reinitializes request builder and response parser per message");
  ZuCHECK(app.eventCount(Zhttp::ClientEventType::Selected) == 2 &&
      app.eventCount(Zhttp::ClientEventType::Redirected) == 1 &&
      app.eventCount(Zhttp::ClientEventType::Completed) == 1,
    "typed redirect transition sequence");
  uint64_t firstAttempt = 0;
  uint64_t secondAttempt = 0;
  uint64_t requestID = 0;
  for (unsigned i = 0; i < app.events.length(); ++i) {
    const auto &event = app.events[i];
    if (event.type == Zhttp::ClientEventType::Selected && !firstAttempt) {
      requestID = event.request;
      firstAttempt = event.attempt;
    } else if (event.type == Zhttp::ClientEventType::Redirected) {
      secondAttempt = event.attempt;
      ZuCHECK(event.request == requestID &&
	  event.previousAttempt == firstAttempt,
	"redirect retains request ID and links distinct attempts");
    }
  }
  ZuCHECK(firstAttempt && secondAttempt && firstAttempt != secondAttempt &&
      app.results[0].request == requestID &&
      app.results[0].attempt == secondAttempt,
    "redirect terminal identity uses the new attempt");

  app.final();
  mx.stop();
}

void unsafeRedirect()
{
  ZuTestScope(unsafeRedirect);

  uint16_t port;
  int fd = listener(port);
  ZuCHECK(fd >= 0 && port, "create unsafe redirect listener");
  if (fd < 0) return;
  ZmAtomic<unsigned> serverOK = 0;
  ZmThread server{[fd, &serverOK]() {
    serverOK.store_(serveRedirect(fd, false));
  }};

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start multiplexer");
  App app;
  app.expected = 1;
  app.replayable_ = false;
  auto config = Zhttp::ClientConfig()
    .concurrency(1).maxRedirects(2)
    .protocol(Zhttp::ProtocolPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);
  ZuCHECK(app.init(
      Zhttp::HubConfig{&mx, "3", "4"}, config,
      Zhttp::TCPConfig{}, Zhttp::H2Config{}, Zhttp::QUICConfig{}),
    "initialize unsafe redirect agent");
  ZuCHECK(app.start(), "start unsafe redirect agent");

  auto request = app.request();
  ZtString<> url;
  url << "http://127.0.0.1:" << port << "/start";
  ZuCHECK(request->url.assign(url).ok(),
    "parse unsafe redirect URL");
  app.enqueue(request);
  app.seal();
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "unsafe redirect refusal completes");
  app.stop();
  server.join();

  ZuCHECK(serverOK.load_(), "unsafe redirect server receives one request");
  ZuCHECK(app.results.length() == 1 &&
      app.results[0].code == Zhttp::ResultCode::ReplayUnsafe &&
      app.results[0].redirects == 0,
    "unsafe redirect is not replayed");
  ZuCHECK(app.eventCount(Zhttp::ClientEventType::Selected) == 1 &&
      app.eventCount(Zhttp::ClientEventType::Redirected) == 0 &&
      app.eventCount(Zhttp::ClientEventType::Completed) == 1,
    "unsafe redirect has no new generation");

  app.final();
  mx.stop();
}

void retryLimit()
{
  ZuTestScope(retryLimit);

  uint16_t port;
  int reserve = listener(port);
  ZuCHECK(reserve >= 0 && port, "reserve closed retry port");
  if (reserve < 0) return;
  ::close(reserve);

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start multiplexer");
  App app;
  app.expected = 1;
  auto config = Zhttp::ClientConfig()
    .concurrency(1).maxRetries(2)
    .protocol(Zhttp::ProtocolPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);
  ZuCHECK(app.init(
      Zhttp::HubConfig{&mx, "3", "4"}, config,
      Zhttp::TCPConfig{}, Zhttp::H2Config{}, Zhttp::QUICConfig{}),
    "initialize retry-limit agent");
  ZuCHECK(app.start(), "start retry-limit agent");

  auto request = app.request();
  ZtString<> url;
  url << "http://127.0.0.1:" << port << '/';
  ZuCHECK(request->url.assign(url).ok(),
    "parse retry-limit URL");
  app.enqueue(request);
  app.seal();
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "retry budget exhaustion completes");
  app.stop();

  ZuCHECK(app.results.length() == 1 &&
      app.results[0].code == Zhttp::ResultCode::Failed &&
      app.results[0].retries == 2,
    "retry budget produces one failed result");
  ZuCHECK(app.eventCount(Zhttp::ClientEventType::Selected) == 3 &&
      app.eventCount(Zhttp::ClientEventType::AttemptFailed) == 3 &&
      app.eventCount(Zhttp::ClientEventType::Retried) == 2 &&
      app.eventCount(Zhttp::ClientEventType::Completed) == 1,
    "retry budget bounds attempts");

  app.final();
  mx.stop();
}

void unsafeRetry()
{
  ZuTestScope(unsafeRetry);

  uint16_t port;
  int reserve = listener(port);
  ZuCHECK(reserve >= 0 && port, "reserve unsafe retry port");
  if (reserve < 0) return;
  ::close(reserve);

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start multiplexer");
  App app;
  app.expected = 1;
  app.replayable_ = false;
  auto config = Zhttp::ClientConfig()
    .concurrency(1).maxRetries(2)
    .protocol(Zhttp::ProtocolPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);
  ZuCHECK(app.init(
      Zhttp::HubConfig{&mx, "3", "4"}, config,
      Zhttp::TCPConfig{}, Zhttp::H2Config{}, Zhttp::QUICConfig{}),
    "initialize unsafe retry agent");
  ZuCHECK(app.start(), "start unsafe retry agent");

  auto request = app.request();
  ZtString<> url;
  url << "http://127.0.0.1:" << port << '/';
  ZuCHECK(request->url.assign(url).ok(),
    "parse unsafe retry URL");
  app.enqueue(request);
  app.seal();
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "unsafe retry refusal completes");
  app.stop();

  ZuCHECK(app.results.length() == 1 &&
      app.results[0].code == Zhttp::ResultCode::Failed &&
      app.results[0].retries == 0,
    "unsafe request is not retried");
  ZuCHECK(app.eventCount(Zhttp::ClientEventType::Selected) == 1 &&
      app.eventCount(Zhttp::ClientEventType::AttemptFailed) == 1 &&
      app.eventCount(Zhttp::ClientEventType::Retried) == 0 &&
      app.eventCount(Zhttp::ClientEventType::Completed) == 1,
    "unsafe retry has no new attempt");

  app.final();
  mx.stop();
}

void resolverLifecycle()
{
  ZuTestScope(resolverLifecycle);

  ZiResolver::final();
  ZiMultiplex mx{mxParams()};
  bool mxUp = mx.start();
  ZuCHECK(mxUp, "start resolver lifecycle multiplexer");
  if (!mxUp) return;

  Zhttp::ClientConfig config;
  config
    .concurrency(1)
    .protocol(Zhttp::ProtocolPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);

  {
    App app;
    bool inited = app.init(
      Zhttp::HubConfig{&mx, "3", "4"}, config,
      Zhttp::TCPConfig{}, Zhttp::H2Config{}, Zhttp::QUICConfig{});
    ZuCHECK(inited && app.start(), "start resolver-owning agent");
    ZiResolver::start();
    ZuCHECK(ZiResolver::instance()->running(), "start agent-owned resolver");
    if (inited) app.stop();
    if (inited) app.final();
    ZuCHECK(!ZiResolver::instance()->initialized(),
      "agent finalizes its resolver");
  }

  ZiResolver::start();
  ZuCHECK(ZiResolver::instance()->running(), "start external resolver");
  {
    App app;
    bool inited = app.init(
      Zhttp::HubConfig{&mx, "3", "4"}, config,
      Zhttp::TCPConfig{}, Zhttp::H2Config{}, Zhttp::QUICConfig{});
    ZuCHECK(inited && app.start(), "start resolver-borrowing agent");
    if (inited) app.stop();
    if (inited) app.final();
    ZuCHECK(ZiResolver::instance()->running(),
      "agent preserves externally owned resolver");
  }
  ZiResolver::stop();
  ZiResolver::final();
  mx.stop();
}

} // namespace ZhttpClientCancelTest_

int main(int argc, char **argv)
{
  using namespace ZhttpClientCancelTest_;

  (void)argc;
  (void)argv;
  ZuTestMain();
  ZuTestCall(attemptState);
  ZuTestCall(cancel);
  ZuTestCall(outOfOrder);
  ZuTestCall(timeout);
  ZuTestCall(retry);
  ZuTestCall(redirect);
  ZuTestCall(unsafeRedirect);
  ZuTestCall(retryLimit);
  ZuTestCall(unsafeRetry);
  ZuTestCall(resolverLifecycle);
  return 0;
}
