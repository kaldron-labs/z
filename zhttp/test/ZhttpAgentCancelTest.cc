//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// multi-protocol agent cancellation and shutdown tests

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>

#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiPlatform.hh>
#include <zlib/ZiResolver.hh>

#include <zlib/Zhttp.hh>
#include <zlib/ZhttpAgent.hh>

using namespace ZuTestUtil;

namespace ZhttpAgentCancelTest_ {

struct Request {
  Zhttp::URL	url;
};

int listenerAt(uint16_t);

struct App :
  public Zhttp::Agent<
    App, Request, ZuTypeList<>,
    ZuTypeList<ZuStringT<"location">, void>, 1024> {
  using Base =
    Zhttp::Agent<
      App, Request, ZuTypeList<>,
      ZuTypeList<ZuStringT<"location">, void>, 1024>;

  bool requestReplayable(const Request &) const { return replayable; }

  void completed(Request &, const Zhttp::Result &result) {
    results.push(result);
    if (results.length() == expected) done.post();
  }

  void observed(Request *, const Zhttp::AgentEvent &event) {
    events.push(event);
    if (event.type == Zhttp::AgentEventType::AttemptFailed &&
	retryPort && retryFD < 0) {
      retryFD = listenerAt(retryPort);
      serverReady.post();
    }
  }

  unsigned eventCount(int8_t type) const {
    unsigned count = 0;
    for (unsigned i = 0; i < events.length(); ++i)
      count += events[i].type == type;
    return count;
  }

  ZtArray<Zhttp::Result, ZtArrayHeapID<"Zhttp.Test.Results">> results;
  ZtArray<Zhttp::AgentEvent, ZtArrayHeapID<"Zhttp.Test.Events">> events;
  ZmSemaphore	done;
  ZmSemaphore	serverReady;
  unsigned	expected = 0;
  uint16_t	retryPort = 0;
  int		retryFD = -1;
  bool		replayable = true;
};

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
  Zhttp::AgentConfig config;
  config
    .concurrency(1).maxPending(2)
    .protocol(Zhttp::ProtocolPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);
  ZuCHECK(app.init(
      Zhttp::EngineConfig{&mx, "3", "4"}, config,
      Zhttp::TCPConfig{}, Zhttp::TLSConfig{}, Zhttp::QUICConfig{}),
    "initialize agent");
  ZuCHECK(app.start(), "start agent");

  Request requests[2];
  ZtString<> url;
  url << "http://127.0.0.1:" << port << '/';
  ZuCHECK(Zhttp::URL::parse(requests[0].url, url).ok(), "parse URL");
  requests[1].url = requests[0].url;

  app.submit(requests, 2);
  app.cancel(requests[1]);
  app.cancel(requests[0]);
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "queued and active cancellation completes");
  app.stop();

  ZuCHECK(app.Base::completed() == 2 && app.results.length() == 2,
    "queued and active requests complete exactly once");
  ZuCHECK(app.results[0].code == Zhttp::ResultCode::Cancelled &&
      app.results[1].code == Zhttp::ResultCode::Cancelled,
    "cancellation result classification");
  ZuCHECK(app.eventCount(Zhttp::AgentEventType::Cancelled) == 2 &&
      app.eventCount(Zhttp::AgentEventType::Completed) == 2 &&
      app.eventCount(Zhttp::AgentEventType::Stopping) == 1,
    "typed cancellation, completion, and shutdown events");

  app.final();
  mx.stop();
  ::close(fd);
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
  Zhttp::AgentConfig config;
  config
    .concurrency(1).maxPending(1).requestTimeout(1)
    .protocol(Zhttp::ProtocolPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);
  ZuCHECK(app.init(
      Zhttp::EngineConfig{&mx, "3", "4"}, config,
      Zhttp::TCPConfig{}, Zhttp::TLSConfig{}, Zhttp::QUICConfig{}),
    "initialize agent");
  ZuCHECK(app.start(), "start agent");

  Request request;
  ZtString<> url;
  url << "http://127.0.0.1:" << port << '/';
  ZuCHECK(Zhttp::URL::parse(request.url, url).ok(), "parse URL");

  app.submit(&request, 1);
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0, "request timeout completes");
  ZuCHECK(app.Base::completed() == 1 && app.results.length() == 1 &&
      app.results[0].code == Zhttp::ResultCode::TimedOut,
    "timeout result classification");
  app.stop();
  ZuCHECK(app.eventCount(Zhttp::AgentEventType::Completed) == 1 &&
      app.events[app.events.length() - 1].type ==
	Zhttp::AgentEventType::Stopping,
    "typed timeout completion and shutdown events");
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

  Zhttp::AgentConfig config;
  config
    .concurrency(1).maxPending(1).maxRetries(1)
    .protocol(Zhttp::ProtocolPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);
  ZuCHECK(app.init(
      Zhttp::EngineConfig{&mx, "3", "4"}, config,
      Zhttp::TCPConfig{}, Zhttp::TLSConfig{}, Zhttp::QUICConfig{}),
    "initialize retry agent");
  ZuCHECK(app.start(), "start retry agent");

  Request request;
  ZtString<> url;
  url << "http://127.0.0.1:" << port << '/';
  ZuCHECK(Zhttp::URL::parse(request.url, url).ok(), "parse retry URL");
  app.submit(&request, 1);
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "transient failure retry completes");
  app.stop();
  server.join();

  ZuCHECK(serverOK.load_(), "retry server receives request");
  ZuCHECK(app.results.length() == 1 && app.results[0].ok() &&
      app.results[0].retries == 1,
    "retry produces one successful terminal result");
  ZuCHECK(app.eventCount(Zhttp::AgentEventType::Selected) == 2 &&
      app.eventCount(Zhttp::AgentEventType::AttemptFailed) == 1 &&
      app.eventCount(Zhttp::AgentEventType::Retried) == 1 &&
      app.eventCount(Zhttp::AgentEventType::Completed) == 1,
    "typed retry transition sequence");
  uint64_t requestID = 0;
  uint64_t firstAttempt = 0;
  uint64_t secondAttempt = 0;
  for (unsigned i = 0; i < app.events.length(); ++i) {
    const auto &event = app.events[i];
    if (event.type == Zhttp::AgentEventType::AttemptFailed) {
      requestID = event.request;
      firstAttempt = event.attempt;
      ZuCHECK(event.transient, "retry failure is classified transient");
    } else if (event.type == Zhttp::AgentEventType::Retried) {
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
  Zhttp::AgentConfig config;
  config
    .concurrency(1).maxPending(1).maxRedirects(2)
    .protocol(Zhttp::ProtocolPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);
  ZuCHECK(app.init(
      Zhttp::EngineConfig{&mx, "3", "4"}, config,
      Zhttp::TCPConfig{}, Zhttp::TLSConfig{}, Zhttp::QUICConfig{}),
    "initialize redirect agent");
  ZuCHECK(app.start(), "start redirect agent");

  Request request;
  ZtString<> url;
  url << "http://127.0.0.1:" << port << "/start";
  ZuCHECK(Zhttp::URL::parse(request.url, url).ok(), "parse redirect URL");
  app.submit(&request, 1);
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0, "redirect completes");
  app.stop();
  server.join();

  ZuCHECK(serverOK.load_(), "redirect server receives both requests");
  ZuCHECK(app.results.length() == 1 && app.results[0].ok() &&
      app.results[0].redirects == 1,
    "redirect produces one successful terminal result");
  ZuCHECK(app.eventCount(Zhttp::AgentEventType::Selected) == 2 &&
      app.eventCount(Zhttp::AgentEventType::Redirected) == 1 &&
      app.eventCount(Zhttp::AgentEventType::Completed) == 1,
    "typed redirect transition sequence");
  uint64_t firstAttempt = 0;
  uint64_t secondAttempt = 0;
  uint64_t requestID = 0;
  for (unsigned i = 0; i < app.events.length(); ++i) {
    const auto &event = app.events[i];
    if (event.type == Zhttp::AgentEventType::Selected && !firstAttempt) {
      requestID = event.request;
      firstAttempt = event.attempt;
    } else if (event.type == Zhttp::AgentEventType::Redirected) {
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
  app.replayable = false;
  Zhttp::AgentConfig config;
  config
    .concurrency(1).maxPending(1).maxRedirects(2)
    .protocol(Zhttp::ProtocolPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);
  ZuCHECK(app.init(
      Zhttp::EngineConfig{&mx, "3", "4"}, config,
      Zhttp::TCPConfig{}, Zhttp::TLSConfig{}, Zhttp::QUICConfig{}),
    "initialize unsafe redirect agent");
  ZuCHECK(app.start(), "start unsafe redirect agent");

  Request request;
  ZtString<> url;
  url << "http://127.0.0.1:" << port << "/start";
  ZuCHECK(Zhttp::URL::parse(request.url, url).ok(),
    "parse unsafe redirect URL");
  app.submit(&request, 1);
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "unsafe redirect refusal completes");
  app.stop();
  server.join();

  ZuCHECK(serverOK.load_(), "unsafe redirect server receives one request");
  ZuCHECK(app.results.length() == 1 &&
      app.results[0].code == Zhttp::ResultCode::ReplayUnsafe &&
      app.results[0].redirects == 0,
    "unsafe redirect is not replayed");
  ZuCHECK(app.eventCount(Zhttp::AgentEventType::Selected) == 1 &&
      app.eventCount(Zhttp::AgentEventType::Redirected) == 0 &&
      app.eventCount(Zhttp::AgentEventType::Completed) == 1,
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
  Zhttp::AgentConfig config;
  config
    .concurrency(1).maxPending(1).maxRetries(2)
    .protocol(Zhttp::ProtocolPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);
  ZuCHECK(app.init(
      Zhttp::EngineConfig{&mx, "3", "4"}, config,
      Zhttp::TCPConfig{}, Zhttp::TLSConfig{}, Zhttp::QUICConfig{}),
    "initialize retry-limit agent");
  ZuCHECK(app.start(), "start retry-limit agent");

  Request request;
  ZtString<> url;
  url << "http://127.0.0.1:" << port << '/';
  ZuCHECK(Zhttp::URL::parse(request.url, url).ok(),
    "parse retry-limit URL");
  app.submit(&request, 1);
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "retry budget exhaustion completes");
  app.stop();

  ZuCHECK(app.results.length() == 1 &&
      app.results[0].code == Zhttp::ResultCode::Failed &&
      app.results[0].retries == 2,
    "retry budget produces one failed result");
  ZuCHECK(app.eventCount(Zhttp::AgentEventType::Selected) == 3 &&
      app.eventCount(Zhttp::AgentEventType::AttemptFailed) == 3 &&
      app.eventCount(Zhttp::AgentEventType::Retried) == 2 &&
      app.eventCount(Zhttp::AgentEventType::Completed) == 1,
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
  app.replayable = false;
  Zhttp::AgentConfig config;
  config
    .concurrency(1).maxPending(1).maxRetries(2)
    .protocol(Zhttp::ProtocolPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);
  ZuCHECK(app.init(
      Zhttp::EngineConfig{&mx, "3", "4"}, config,
      Zhttp::TCPConfig{}, Zhttp::TLSConfig{}, Zhttp::QUICConfig{}),
    "initialize unsafe retry agent");
  ZuCHECK(app.start(), "start unsafe retry agent");

  Request request;
  ZtString<> url;
  url << "http://127.0.0.1:" << port << '/';
  ZuCHECK(Zhttp::URL::parse(request.url, url).ok(),
    "parse unsafe retry URL");
  app.submit(&request, 1);
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "unsafe retry refusal completes");
  app.stop();

  ZuCHECK(app.results.length() == 1 &&
      app.results[0].code == Zhttp::ResultCode::Failed &&
      app.results[0].retries == 0,
    "unsafe request is not retried");
  ZuCHECK(app.eventCount(Zhttp::AgentEventType::Selected) == 1 &&
      app.eventCount(Zhttp::AgentEventType::AttemptFailed) == 1 &&
      app.eventCount(Zhttp::AgentEventType::Retried) == 0 &&
      app.eventCount(Zhttp::AgentEventType::Completed) == 1,
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

  Zhttp::AgentConfig config;
  config
    .concurrency(1).maxPending(1)
    .protocol(Zhttp::ProtocolPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);

  {
    App app;
    bool inited = app.init(
      Zhttp::EngineConfig{&mx, "3", "4"}, config,
      Zhttp::TCPConfig{}, Zhttp::TLSConfig{}, Zhttp::QUICConfig{});
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
      Zhttp::EngineConfig{&mx, "3", "4"}, config,
      Zhttp::TCPConfig{}, Zhttp::TLSConfig{}, Zhttp::QUICConfig{});
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

} // namespace ZhttpAgentCancelTest_

int main(int argc, char **argv)
{
  using namespace ZhttpAgentCancelTest_;

  (void)argc;
  (void)argv;
  ZuTestMain();
  ZuTestCall(cancel);
  ZuTestCall(timeout);
  ZuTestCall(retry);
  ZuTestCall(redirect);
  ZuTestCall(unsafeRedirect);
  ZuTestCall(retryLimit);
  ZuTestCall(unsafeRetry);
  ZuTestCall(resolverLifecycle);
  return 0;
}
