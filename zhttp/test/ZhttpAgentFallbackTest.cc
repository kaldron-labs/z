//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Zhttp Agent HTTP/3-to-TLS fallback test

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

#include <zlib/ZhttpAgent.hh>

#include "ZhttpTestUtil.hh"

using namespace ZuTestUtil;

namespace ZhttpAgentFallbackTest_ {

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

struct Request {
  Zhttp::URL	url;
};

struct Agent :
  public Zhttp::Agent<
    Agent, Request, ZuTypeList<>, ZhttpHeaders("alt-svc"), 1024> {
  using Base =
    Zhttp::Agent<
      Agent, Request, ZuTypeList<>, ZhttpHeaders("alt-svc"), 1024>;

  void completed(Request &, const Zhttp::Result &result_) {
    result = result_;
    results.push(result_);
    done.post();
  }
  void responseStatus(Request &, unsigned status_) { status = status_; }
  void responseBody(Request &, ZuBSpan value) {
    bodyBytes += value.length();
  }
  template <typename Key>
  void responseHeader(Request &, ZuBSpan value) {
    if constexpr (Key{}() == "alt-svc")
      altSvc = ZuCSpan{value};
  }
  void observed(Request *, const Zhttp::AgentEvent &event) {
    events.push(event);
  }
  unsigned eventCount(int8_t type) const {
    unsigned count = 0;
    for (unsigned i = 0; i < events.length(); ++i)
      count += events[i].type == type;
    return count;
  }

  Zhttp::Result	result;
  ZtArray<Zhttp::Result,
    ZtArrayHeapID<"Zhttp.Test.Fallback.Results">> results;
  ZtArray<Zhttp::AgentEvent,
    ZtArrayHeapID<"Zhttp.Test.Fallback">> events;
  ZmSemaphore	done;
  uint64_t	bodyBytes = 0;
  unsigned	status = 0;
  ZtString<>	altSvc;
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

#ifndef _WIN32
pid_t startServer(
  ZuCSpan root, uint16_t port, ZuCSpan cert, ZuCSpan key)
{
  pid_t pid = ::fork();
  if (pid) return pid;

  ZtString<> port_;
  port_ << port;
  const char *server = ::access("./zhttpd", X_OK) == 0 ?
    "./zhttpd" : "zhttp/test/zhttpd";
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
  Zhttp::EngineConfig engine{&mx, "3", "4"};

  Resolver resolver;
  Agent agent;
  agent.discoveryResolver(&resolver.ops);
  Zhttp::AgentConfig agentConfig;
  agentConfig
    .concurrency(1).maxPending(1).requestTimeout(10)
    .protocol(Zhttp::ProtocolPolicy::PreferH3).blindH3(true)
    .tcp(true).tls(true).quic(true);
  bool agentInited = agent.init(
    engine, agentConfig, Zhttp::TCPConfig{},
    Zhttp::TLSConfig{}.caPath(cert),
    Zhttp::QUICConfig{}.caPath(untrustedCert).maxIdleTimeout(500));
  ZuCHECK(agentInited, "initialize prefer-mode agent");
  bool agentUp = agentInited && agent.start();
  ZuCHECK(agentUp, "start prefer-mode agent");

  if (agentUp) {
    Request request;
    ZtString<> url;
    url << "https://127.0.0.1:" << port << "/ok";
    bool parsed = Zhttp::URL::parse(request.url, url).ok();
    ZuCHECK(parsed, "parse fallback URL");
    if (parsed) {
      agent.submit(&request, 1);
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
	      agent.result.httpVersion == Zhttp::Version::H1 &&
	      agent.status == 200 && agent.bodyBytes == expected.length(),
	    "fallback terminal result identifies TLS/H1");
	  ZuCHECK(agent.altSvc, "TLS response contains Alt-Svc");
	  ZuCHECK(agent.eventCount(Zhttp::AgentEventType::Selected) == 2 &&
	      agent.eventCount(Zhttp::AgentEventType::AttemptFailed) == 1 &&
	      agent.eventCount(Zhttp::AgentEventType::Fallback) == 1 &&
	      agent.eventCount(Zhttp::AgentEventType::Completed) == 1,
	    "typed H3-to-H1 fallback transition sequence");
	  uint64_t requestID = 0;
	  uint64_t failedAttempt = 0;
	  uint64_t fallbackAttempt = 0;
	  for (unsigned i = 0; i < agent.events.length(); ++i) {
	    const auto &event = agent.events[i];
	    if (event.type == Zhttp::AgentEventType::AttemptFailed) {
	      requestID = event.request;
	      failedAttempt = event.attempt;
	    } else if (event.type == Zhttp::AgentEventType::Fallback) {
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
  Agent cachedAgent;
  cachedAgent.discoveryResolver(&cacheResolver.ops);
  Zhttp::AgentConfig cacheConfig;
  cacheConfig
    .concurrency(1).maxPending(1).requestTimeout(10)
    .protocol(Zhttp::ProtocolPolicy::PreferH3).blindH3(false)
    .tcp(true).tls(true).quic(true);
  bool cacheInited = cachedAgent.init(
    engine, cacheConfig, Zhttp::TCPConfig{},
    Zhttp::TLSConfig{}.caPath(cert),
    Zhttp::QUICConfig{}.caPath(cert).maxIdleTimeout(500));
  ZuCHECK(cacheInited, "initialize cached-routing agent");
  bool cacheUp = cacheInited && cachedAgent.start();
  ZuCHECK(cacheUp, "start cached-routing agent");
  if (cacheUp) {
    ZtArray<Request,
      ZtArrayHeapID<"Zhttp.Test.Fallback.Requests">> requests;
    requests.length(2);
    ZtString<> url;
    url << "https://127.0.0.1:" << port << "/ok";
    bool parsed =
      Zhttp::URL::parse(requests[0].url, url).ok() &&
      Zhttp::URL::parse(requests[1].url, url).ok();
    ZuCHECK(parsed, "parse cached-routing URLs");
    if (parsed) {
      cachedAgent.submit(requests.data(), requests.length());
      bool queried =
	cacheResolver.queried.timedwait(Zm::now(10)) == 0;
      ZuCHECK(queried, "first cached-routing request performs discovery");
      if (queried) {
	cacheResolver.noRecord();
	bool completed =
	  cachedAgent.done.timedwait(Zm::now(10)) == 0 &&
	  cachedAgent.done.timedwait(Zm::now(10)) == 0;
	ZuCHECK(completed, "TLS and cached H3 requests complete");
	if (completed) {
	  ZuCHECK(cachedAgent.results.length() == 2 &&
	      cachedAgent.results[0].ok() && cachedAgent.results[1].ok() &&
	      cachedAgent.results[0].transport == Zhttp::Transport::TLS &&
	      cachedAgent.results[1].transport == Zhttp::Transport::QUIC &&
	      cachedAgent.results[0].httpVersion == Zhttp::Version::H1 &&
	      cachedAgent.results[1].httpVersion == Zhttp::Version::H3 &&
	      cachedAgent.status == 200 &&
	      cachedAgent.bodyBytes == expected.length() * 2,
	    "Alt-Svc moves the second request from TLS/H1 to QUIC/H3");
	  bool cached = false;
	  for (unsigned i = 0; i < cachedAgent.events.length(); ++i) {
	    const auto &event = cachedAgent.events[i];
	    if (event.type == Zhttp::AgentEventType::Selected &&
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
  if (cacheInited) cachedAgent.stop();
  if (cacheInited) cachedAgent.final();
  mx.stop();
  ZuCHECK(stopServer(server), "stop TLS fallback service");
#endif
}

} // namespace ZhttpAgentFallbackTest_

int main(int argc, char **argv)
{
  using namespace ZhttpAgentFallbackTest_;

  (void)argc;
  (void)argv;
  ZuTestMain();
  ZuTestCall(fallback);
  return 0;
}
