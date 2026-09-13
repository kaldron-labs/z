//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// multi-protocol client cancellation and shutdown tests

#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>

#include <zlib/ZtcHeap.hh>

#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiPlatform.hh>
#include <zlib/ZiResolver.hh>

#include <zlib/ZhttpClient.hh>

#include "ZhttpTestPorts.hh"

using namespace ZuTestUtil;

namespace ZhttpClientCancelTest_ {

#ifdef ZHTTP_CLIENT_POOL_TEST
enum { TestPortBegin = ZhttpTestPort::ClientPool };
enum { TestPortEnd = ZhttpTestPort::ClientPoolEnd };
#else
enum { TestPortBegin = ZhttpTestPort::ClientCancel };
enum { TestPortEnd = ZhttpTestPort::ClientCancelEnd };
#endif

uint64_t zhttpClientAllocated()
{
  uint64_t allocated = 0;
  Ztc::HeapMgr::all(Ztc::HeapMgr::AllFn{
    [&allocated](Ztc::Heap *heap) {
      Ztc::HeapTelemetry data;
      heap->telemetry(data);
      ZuCSpan id{data.id};
      if (id.length() >= 6 && !memcmp(id.data(), "Zhttp.", 6) &&
	  (id.length() < 11 || memcmp(id.data(), "Zhttp.Test.", 11)))
	allocated += data.allocated();
    }});
  return allocated;
}

struct ResolveCounter {
  ResolveCounter() : ops{
    .context = this,
    .query = query_,
    .resolve = resolve_,
    .cancel = cancel_
  } { }

  static ZmRef<ZiResolver_::Query> query_(
      void *context, ZiResolver_::Host, uint16_t, uint16_t,
      ZiResolver_::QueryFn fn) {
    ++static_cast<ResolveCounter *>(context)->queries;
    fn(ZiResolver_::QueryResult{ZiResolver_::Event{}});
    return {};
  }
  static ZmRef<ZiResolver_::Query> resolve_(
      void *context, ZiResolver_::Host, ZiResolver_::ResolveFn fn) {
    ++static_cast<ResolveCounter *>(context)->resolves;
    if (fn(ZiResolver_::ResolveResult{ZiIP{"127.0.0.1"}}))
      fn(ZiResolver_::ResolveResult{});
    return new ZiResolver_::Query;
  }
  static void cancel_(void *context, ZmRef<ZiResolver_::Query> query) {
    if (query) ++static_cast<ResolveCounter *>(context)->cancels;
  }

  Zhttp::DiscoveryResolver ops;
  unsigned	queries = 0;
  unsigned	resolves = 0;
  unsigned	cancels = 0;
};

struct HoldingResolver {
  HoldingResolver() : ops{
    .context = this,
    .query = query_,
    .resolve = resolve_,
    .cancel = cancel_
  } { }

  static ZmRef<ZiResolver_::Query> query_(
      void *, ZiResolver_::Host, uint16_t, uint16_t,
      ZiResolver_::QueryFn) { return {}; }
  static ZmRef<ZiResolver_::Query> resolve_(
      void *context, ZiResolver_::Host, ZiResolver_::ResolveFn fn) {
    auto self = static_cast<HoldingResolver *>(context);
    self->fn = ZuMv(fn);
    self->entered.post();
    return {};
  }
  static void cancel_(void *context, ZmRef<ZiResolver_::Query>) {
    auto self = static_cast<HoldingResolver *>(context);
    self->fn = {};
    ++self->cancelled;
  }

  Zhttp::DiscoveryResolver ops;
  ZiResolver_::ResolveFn fn;
  ZmSemaphore	entered;
  unsigned	cancelled = 0;
};

struct App;
struct Pool;
template <typename Heap = ZuVoid> struct Pool_;
struct ReqBuilder_;
struct ResParser;

using ReqHeaderList =
  ZuTypeList<ZuStringT<"content-length">, ZuTypeList<>>;
ZhttpHdrCatalogDerive(ReqHeaders, ReqHeaderList);
ZhttpHdrCatalogImpl(ReqHeaders)
using ResHeaderList =
  ZuTypeList<ZuStringT<"location">, ZuTypeList<>>;
ZhttpHdrCatalogDerive(ResHeaders, ResHeaderList);
ZhttpHdrCatalogImpl(ResHeaders)

struct ReqBuilder_ : public ZmObject, public Zhttp::ReqBuilder {
  using ContentLength = ZuStringT<"content-length">;
  using HdrCatalog = ReqHeaders;
  Zhttp::BodyPolicy::T bodyPolicy() const {
    return bodyData ?
      Zhttp::BodyPolicy::OptionalFixed : Zhttp::BodyPolicy::None;
  }

  template <typename L>
  void operation(L &&l) const {
    l(method, [this](auto &&emit) {
      emit([this](auto &tx) { tx << target; });
    });
  }
  template <typename L> void protocol(L &&) const { }
  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (ZuIsSame<Key, ContentLength>{})
      if (bodyData) l(Zhttp::contentLengthPad());
  }
  template <typename L> void header(L &&) const { }
  template <typename Emit>
  void body(Emit &&emit) {
    if (!bodyData) return;
    emit([this](auto &body) {
      body << bodyData;
      body.flush();
      bodyLength = body.produced();
      return Zhttp::WriteOutcome::End;
    });
  }
  template <typename L>
  void bodyHdrs(L &&l) const {
    Zhttp::contentLengthSet(l, bodyLength);
  }

  void connected(const Zhttp::ConnectedInfo &) { }
  void disconnected(bool) { }
  void connectFailed(bool) { }
  void selected(
    const Zhttp::Endpoint *, uint64_t, uint64_t,
    unsigned, unsigned, Zhttp::Transport::T, Zhttp::Version::T);
  void attemptFailed(
    uint64_t, uint64_t, unsigned, unsigned, unsigned,
    Zhttp::Transport::T, Zhttp::Version::T, bool, bool);
  void redirected(
    const Zhttp::URLView &, uint64_t, uint64_t, uint64_t,
    unsigned, uint16_t);
  void retried(uint64_t, uint64_t, uint64_t, uint16_t);
  void fallback(
    uint64_t, uint64_t, uint64_t,
    Zhttp::Transport::T, Zhttp::Version::T);
  void cancelled(const Zhttp::Result &);
  void completed(const Zhttp::Result &);

  uint64_t key() const { return key_; }
  uint64_t length() const { return 1; }
  bool idempotent(Zhttp::Method::T method_) const {
    return idempotent_ || Zhttp::idempotentMethod(method_);
  }

  App			*app = nullptr;
  Zhttp::URLString	target{"/"};
  ZtString<>		bodyData;
  uint64_t		key_ = 0;
  unsigned		bodyLength = 0;
  unsigned		completions = 0;
  mutable unsigned	inits = 0;
  Zhttp::ResultCode::T resultCode = Zhttp::ResultCode::OK;
  Zhttp::Method::T	method = Zhttp::Method::GET;
  bool			idempotent_ = false;
};

int listenerAt(uint16_t);

struct ResParser : public Zhttp::Parser {
  using HdrCatalog = ResHeaders;

  bool enable1xx() const { return false; }

  void init(const ReqBuilder_ &req) { ++req.inits; }
  void status(unsigned status__) { status_ = status__; }
  bool bodyInfo(Zhttp::BodyType::T, uint64_t) { return true; }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t>) { }
  template <typename Rx>
  bool body(Rx &rx) {
    return !rejectBody && Zhttp::bodyDrain(rx);
  }
  template <typename Link>
  void complete(Link *, bool ok_) {
    completed = true;
    ok = ok_;
  }
  void reset() {
    status_ = 0;
    completed = false;
    ok = false;
    rejectBody = false;
  }

  unsigned	status_ = 0;
  bool		completed = false;
  bool		ok = false;
  bool		rejectBody = false;
};

struct MockBodyRx {
  explicit operator bool() const { return pending; }
  uint64_t length() const { return pending; }
  template <typename Scan, typename Consume>
  int64_t consume(Scan &&, Consume &&) { return 0; }
  uint64_t pending = 3;
};

ZmPQueueDerive(ReqBuilderQ, ReqBuilder_,
  ZmPQueueOverlap<false, ZmPQueueNode<ReqBuilder_,
    ZmPQueueHeapID<"Zhttp.Test.ReqBuilder">>>);
using ReqBuilder = ReqBuilderQ::Node;
using TxQ = ZmPQTx<Pool, ReqBuilderQ, ZmPQTxOrdered<false>>;

template <typename Heap>
struct Pool_ : public Heap, public Zhttp::Pool<App, TxQ, ResParser> {
  using Base = Zhttp::Pool<App, TxQ, ResParser>;

  Pool_(App *);

  ReqBuilderQ *txQueue() { return &m_requests; }
  void archive_(ReqBuilder *request);
  ZmRef<ReqBuilder> retrieve_(ReqBuilderQ::Key, ReqBuilderQ::Key) { return {}; }

private:
  ReqBuilderQ	m_requests;
};
using PoolHeap = ZmHeap<"Zhttp.Test.Pool", Pool_<>>;
struct Pool : public Pool_<PoolHeap> {
  Pool(App *);
};

ZuAssert((ZuIsSame<
  decltype(ZuDeclVal<const Pool &>().authority()), ZuBSpan>{}));

struct App : public Zhttp::Client<App, Pool> {
  using Base = Zhttp::Client<App, Pool>;

  void idle() {
    ++idleCount;
    idleDone.post();
  }

  void archived(ReqBuilder *request) {
    archives.push(request->key());
    archiveDone.post();
  }

  ZmRef<ReqBuilder> request() {
    ZmRef<ReqBuilder> request = new ReqBuilder;
    request->app = this;
    request->key_ = m_key++;
    return request;
  }

  void requestCompleted(const Zhttp::Result &result) {
    results.push(result);
    if (results.length() == expected) done.post();
  }

  ZtArray<Zhttp::Result, ZtArrayHeapID<"Zhttp.Test.Results">> results;
  ZtArray<uint64_t, ZtArrayHeapID<"Zhttp.Test.Archives">> archives;
  ZmSemaphore	done;
  ZmSemaphore	archiveDone;
  ZmSemaphore	serverReady;
  ZmSemaphore	idleDone;
  unsigned	expected = 0;
  unsigned	idleCount = 0;
  unsigned	selectedEvents = 0;
  unsigned	attemptFailedEvents = 0;
  unsigned	redirectedEvents = 0;
  unsigned	retriedEvents = 0;
  unsigned	fallbackEvents = 0;
  unsigned	cancelledEvents = 0;
  unsigned	completedEvents = 0;
  uint64_t	firstSelectedRequest = 0;
  uint64_t	firstSelectedAttempt = 0;
  uint64_t	failedRequest = 0;
  uint64_t	failedAttempt = 0;
  uint64_t	redirectedRequest = 0;
  uint64_t	redirectedAttempt = 0;
  uint64_t	redirectedPreviousAttempt = 0;
  uint64_t	retriedRequest = 0;
  uint64_t	retriedAttempt = 0;
  uint64_t	retriedPreviousAttempt = 0;
  bool		failureTransient = false;
  uint16_t	retryPort = 0;
  int		retryFD = -1;
  Pool		*poolImpl = nullptr;

private:
  uint64_t	m_key = 0;
};

template <typename Heap>
Pool_<Heap>::Pool_(App *app) : Base{app} { }
Pool::Pool(App *app) : Pool_<PoolHeap>{app} { app->poolImpl = this; }

template <typename Heap>
void Pool_<Heap>::archive_(ReqBuilder *request) { client()->archived(request); }

void ReqBuilder_::selected(
    const Zhttp::Endpoint *,
    uint64_t request, uint64_t attempt,
    unsigned, unsigned, Zhttp::Transport::T, Zhttp::Version::T) {
  if (!app->selectedEvents) {
    app->firstSelectedRequest = request;
    app->firstSelectedAttempt = attempt;
  }
  ++app->selectedEvents;
}
void ReqBuilder_::attemptFailed(
    uint64_t request, uint64_t attempt,
    unsigned, unsigned, unsigned,
    Zhttp::Transport::T, Zhttp::Version::T,
    bool transient, bool) {
  ++app->attemptFailedEvents;
  app->failedRequest = request;
  app->failedAttempt = attempt;
  app->failureTransient = transient;
  if (app->retryPort && app->retryFD < 0) {
    app->retryFD = listenerAt(app->retryPort);
    app->serverReady.post();
  }
}
void ReqBuilder_::redirected(
    const Zhttp::URLView &url_,
    uint64_t request, uint64_t attempt, uint64_t previousAttempt,
    unsigned, uint16_t) {
  target.length(0);
  url_.writeTarget(target);
  ++app->redirectedEvents;
  app->redirectedRequest = request;
  app->redirectedAttempt = attempt;
  app->redirectedPreviousAttempt = previousAttempt;
}
void ReqBuilder_::retried(
    uint64_t request, uint64_t attempt,
    uint64_t previousAttempt, uint16_t) {
  ++app->retriedEvents;
  app->retriedRequest = request;
  app->retriedAttempt = attempt;
  app->retriedPreviousAttempt = previousAttempt;
}
void ReqBuilder_::fallback(
    uint64_t, uint64_t, uint64_t,
    Zhttp::Transport::T, Zhttp::Version::T) {
  ++app->fallbackEvents;
}
void ReqBuilder_::cancelled(const Zhttp::Result &) {
  ++app->cancelledEvents;
}
void ReqBuilder_::completed(const Zhttp::Result &result) {
  ++app->completedEvents;
  ++completions;
  resultCode = result.code;
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

bool startApp(
  App &app, ZiMultiplex &mx, uint16_t port, Zhttp::Config config)
{
  config.secure(false).tls(false).quic(false);
  bool inited = app.init(
      Zhttp::HubConfig{&mx, "3", "4"}, 1, config,
      Zhttp::TCPConfig{}, Zhttp::H2Config{}, Zhttp::QUICConfig{});
  bool pooled = inited && app.pool(
    0, Zhttp::Destination{ZuBSpan{"127.0.0.1"}, port});
  bool started = pooled && app.start();
  return started;
}

void attemptState()
{
  ZuTestScope(attemptState);

  struct Link {
    void ref() const { }
    bool deref() const { return false; }
    void responseHeadersParsed(Pool::LiveReq *) { ++headers; }
    void responseBodyBytes(Pool::LiveReq *) { ++bodyUpdates; }
    void complete(bool ok_) { completed = true; ok = ok_; }

    unsigned	headers = 0;
    unsigned	bodyUpdates = 0;
    bool	completed = false;
    bool	ok = false;
  };
  struct ParserState {
    using T = int;
    enum { Complete, Failed };
  };

  App app;
  Pool pool{&app};
  Pool::LiveReq attempt;
  ZmRef<ReqBuilder> request = new ReqBuilder;
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
  pool.poolSend(link, attempt, Zhttp::Version::H1);
  ZuCHECK(attempt.phase == Zhttp::AttemptPhase::Sending,
    "request transmission enters sending phase");
  pool.status(link, attempt, parser, 200, false);
  ZuCHECK(attempt.phase == Zhttp::AttemptPhase::ReceivingHeaders &&
      attempt.protocol.status == 200 && parser.status_ == 200,
    "response status enters receiving-headers phase");
  pool.complete<ParserState>(
    link, attempt, parser, ParserState::Complete);
  ZuCHECK(attempt.phase == Zhttp::AttemptPhase::Closing &&
      link.headers == 1 && link.completed && link.ok &&
      !parser.status_ && !parser.completed && !parser.ok,
    "response completion enters closing phase and resets the parser");

  MockBodyRx body;
  Pool::LiveReq rejected;
  rejected.request = request;
  rejected.phase = Zhttp::AttemptPhase::ReceivingBody;
  ResParser rejectingParser;
  rejectingParser.rejectBody = true;
  Link rejectingLink;
  ZuCHECK(!pool.body(rejectingLink, rejected, rejectingParser, body) &&
      rejected.failure.kind == Zhttp::FailureKind::Body &&
      rejected.responseBody.received == 3 &&
      !rejected.responseBody.consumed &&
      rejected.responseBody.pending == 3 && rejectingLink.bodyUpdates == 1,
    "response body rejection was classified and accounted as a body failure");
  pool.complete<ParserState>(
    rejectingLink, rejected, rejectingParser, ParserState::Failed);
  ZuCHECK(rejected.failure.kind == Zhttp::FailureKind::Body &&
      rejected.responseBody.reset == 3 &&
      rejected.responseBody.discarded == 3 &&
      !rejected.responseBody.pending && !rejectingParser.completed &&
      !rejectingParser.ok && !rejectingParser.rejectBody &&
      rejectingLink.completed && !rejectingLink.ok,
    "body rejection preserves accounting and resets the parser");

  Pool::LiveReq failed;
  failed.request = request;
  Zhttp::BodyCommit commit{
    .produced = 9, .committed = 7, .reset = 2, .discarded = 2,
    .headers = true, .final = false};
  pool.poolTxFailed(link, failed, commit);
  ZuCHECK(failed.failure.kind == Zhttp::FailureKind::Tx &&
      failed.requestBody.produced == 9 &&
      failed.requestBody.committed == 7 &&
      failed.requestBody.headers && !failed.requestBody.final,
    "Tx failure retains the complete request-body commit snapshot");

  failed.failure = {};
  failed.protocol.http10 = true;
  ZuCHECK(!pool.poolReusable(failed),
    "HTTP/1.0 defaults to a non-persistent connection");
  failed.protocol.persistence = Zhttp::Persistence::KeepAlive;
  ZuCHECK(pool.poolReusable(failed),
    "HTTP/1.0 keep-alive is represented by one persistence state");
  failed.protocol.closeDelimited = true;
  ZuCHECK(!pool.poolReusable(failed),
    "close-delimited framing remains orthogonal to persistence");
}

int listener(uint16_t &port)
{
  static uint16_t nextPort = TestPortBegin;
  if (nextPort > TestPortEnd) { port = 0; return -1; }
  port = nextPort++;
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  int one = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0 ||
      ::listen(fd, 32) < 0) {
    ::close(fd);
    return -1;
  }
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

class ServerSockets {
public:
  using FDs = ZtArray<int, ZtArrayHeapID<"Zhttp.Test.ServerFDs">>;

  ServerSockets(int listener = -1) : m_listener{listener} { }
  ~ServerSockets() { stop(); }

  bool reset(int listener) {
    ZmGuard<ZmLock> guard(m_lock);
    if (m_stopping || m_listener >= 0) {
      closeFD_(listener);
      return false;
    }
    m_listener = listener;
    return listener >= 0;
  }

  int accept() {
    int listener;
    {
      ZmGuard<ZmLock> guard(m_lock);
      if (m_stopping || m_listener < 0) return -1;
      listener = m_listener;
    }
    int fd = ::accept(listener, nullptr, nullptr);
    if (fd < 0) return -1;
    {
      ZmGuard<ZmLock> guard(m_lock);
      if (!m_stopping) {
	m_fds.push(fd);
	return fd;
      }
    }
    closeFD_(fd);
    return -1;
  }

  void closeListener() {
    int fd = -1;
    {
      ZmGuard<ZmLock> guard(m_lock);
      fd = m_listener;
      m_listener = -1;
    }
    closeFD_(fd);
  }

  void close(int fd) {
    if (fd < 0) return;
    bool owned = false;
    {
      ZmGuard<ZmLock> guard(m_lock);
      for (unsigned i = 0, n = m_fds.length(); i < n; ++i)
	if (m_fds[i] == fd) {
	  m_fds.splice(i, 1);
	  owned = true;
	  break;
	}
    }
    if (owned) closeFD_(fd);
  }

  void stop() {
    int listener = -1;
    FDs fds;
    {
      ZmGuard<ZmLock> guard(m_lock);
      if (m_stopping) return;
      m_stopping = true;
      listener = m_listener;
      m_listener = -1;
      fds = ZuMv(m_fds);
      m_fds.init_();
    }
    closeFD_(listener);
    for (int fd: fds) closeFD_(fd);
  }

private:
  static void closeFD_(int fd) {
    if (fd < 0) return;
    ::shutdown(fd, SHUT_RDWR);
    ::close(fd);
  }

  ZmLock	m_lock;
  FDs		m_fds;
  int		m_listener = -1;
  bool		m_stopping = false;
};

enum : unsigned {
  // Bound malformed test input while allowing the 100-request pipeline case.
  RequestReadMax = 64U * 1024U
};
using RequestReadBuf =
  ZtArray<uint8_t, ZtArrayHeapID<"Zhttp.Test.RequestRead">>;

bool readRequest(int fd)
{
  RequestReadBuf buf;
  buf.size(RequestReadMax);
  unsigned l = buf.size();
  unsigned length = 0;
  while (length < l) {
    ssize_t n = ::recv(fd, buf.data() + length, l - length, 0);
    if (n <= 0) return false;
    length += unsigned(n);
    if (length >= 4 && ::memmem(buf.data(), length, "\r\n\r\n", 4))
      return true;
  }
  return false;
}

bool readRequest(int fd, ZuCSpan target, ZuCSpan authority)
{
  RequestReadBuf buf;
  buf.size(RequestReadMax);
  unsigned l = buf.size();
  unsigned length = 0;
  ZtString<> requestLine, authorityLine;
  requestLine << "GET " << target << " HTTP/1.1\r\n";
  authorityLine << authority << "\r\n";
  while (length < l) {
    ssize_t n = ::recv(fd, buf.data() + length, l - length, 0);
    if (n <= 0) return false;
    length += unsigned(n);
    if (length >= 4 && ::memmem(buf.data(), length, "\r\n\r\n", 4))
      return ::memmem(
	buf.data(), length, requestLine.data(), requestLine.length()) &&
	::memmem(
	  buf.data(), length, authorityLine.data(), authorityLine.length());
  }
  return false;
}

bool readRequests(int fd, unsigned count)
{
  RequestReadBuf buf;
  buf.size(RequestReadMax);
  unsigned l = buf.size();
  unsigned length = 0;
  while (length < l) {
    ssize_t n = ::recv(fd, buf.data() + length, l - length, 0);
    if (n <= 0) return false;
    length += unsigned(n);
    unsigned found = 0;
    const uint8_t *ptr = buf.data();
    unsigned left = length;
    while (left >= 4) {
      auto end = static_cast<const uint8_t *>(
	::memmem(ptr, left, "\r\n\r\n", 4));
      if (!end) break;
      ++found;
      unsigned used = unsigned(end + 4 - ptr);
      ptr += used;
      left -= used;
    }
    if (found >= count) return true;
  }
  return false;
}

bool readRequestBody(int fd, ZuCSpan body)
{
  RequestReadBuf buf;
  buf.size(RequestReadMax);
  unsigned l = buf.size();
  unsigned length = 0;
  while (length < l) {
    ssize_t n = ::recv(fd, buf.data() + length, l - length, 0);
    if (n <= 0) return false;
    length += unsigned(n);
    auto end = static_cast<const uint8_t *>(
      ::memmem(buf.data(), length, "\r\n\r\n", 4));
    if (!end) continue;
    unsigned offset = unsigned(end + 4 - buf.data());
    if (length < offset + body.length()) continue;
    return !memcmp(buf.data() + offset, body.data(), body.length());
  }
  return false;
}

bool sendResponse(int fd, ZuCSpan response)
{
  unsigned offset = 0, l = response.length();
  while (offset < l) {
    ssize_t n = ::send(
      fd, response.data() + offset, l - offset, 0);
    if (n <= 0) return false;
    offset += unsigned(n);
  }
  return true;
}

bool readRequestKey(int fd, unsigned &key)
{
  RequestReadBuf buf;
  buf.size(RequestReadMax);
  unsigned l = buf.size();
  unsigned length = 0;
  while (length < l) {
    ssize_t n = ::recv(fd, buf.data() + length, l - length, 0);
    if (n <= 0) return false;
    length += unsigned(n);
    if (length >= 4 && ::memmem(buf.data(), length, "\r\n\r\n", 4)) {
      auto target = static_cast<const uint8_t *>(
	::memmem(buf.data(), length, "GET /", 5));
      if (!target || target + 6 >= buf.data() + length ||
	target[5] < '0' || target[5] > '9' || target[6] != ' ')
	return false;
      key = target[5] - '0';
      return true;
    }
  }
  return false;
}

bool serveOutOfOrder(ServerSockets &sockets, App &app)
{
  int a = sockets.accept();
  int b = sockets.accept();
  sockets.closeListener();
  if (a < 0 || b < 0) {
    sockets.close(a);
    sockets.close(b);
    return false;
  }
  unsigned aKey, bKey;
  if (!readRequestKey(a, aKey) || !readRequestKey(b, bKey) || aKey == bKey) {
    sockets.close(a);
    sockets.close(b);
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
  sockets.close(fd1);
  if (ok) ok = app.archiveDone.timedwait(Zm::now(10)) == 0;
  if (ok) ok = sendResponse(fd0, response);
  sockets.close(fd0);
  return ok;
}

bool serve(ServerSockets &sockets)
{
  int fd = sockets.accept();
  sockets.closeListener();
  if (fd < 0) return false;
  static constexpr ZuCSpan response =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 2\r\n"
    "Connection: close\r\n"
    "\r\n"
    "ok";
  bool ok = readRequest(fd) && sendResponse(fd, response);
  sockets.close(fd);
  return ok;
}

bool serve(ServerSockets &sockets, ZuCSpan target, ZuCSpan authority)
{
  int fd = sockets.accept();
  sockets.closeListener();
  if (fd < 0) return false;
  static constexpr ZuCSpan response =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 2\r\n"
    "Connection: close\r\n"
    "\r\n"
    "ok";
  bool ok = readRequest(fd, target, authority) &&
    sendResponse(fd, response);
  sockets.close(fd);
  return ok;
}

bool serveBody(ServerSockets &sockets)
{
  int fd = sockets.accept();
  sockets.closeListener();
  if (fd < 0) return false;
  static constexpr ZuCSpan response =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 5\r\n"
    "Connection: close\r\n"
    "\r\n"
    "abcde";
  bool ok = readRequest(fd) && sendResponse(fd, response);
  sockets.close(fd);
  return ok;
}

bool serveRequestBody(ServerSockets &sockets, ZuCSpan body)
{
  int fd = sockets.accept();
  sockets.closeListener();
  if (fd < 0) return false;
  static constexpr ZuCSpan response =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 0\r\n"
    "Connection: close\r\n"
    "\r\n";
  bool ok = readRequestBody(fd, body) && sendResponse(fd, response);
  sockets.close(fd);
  return ok;
}

bool serveTwo(ServerSockets &sockets)
{
  static constexpr ZuCSpan response =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 2\r\n"
    "Connection: close\r\n"
    "\r\n"
    "ok";
  bool ok = true;
  for (unsigned i = 0; i < 2; ++i) {
    int fd = sockets.accept();
    if (fd < 0) {
      ok = false;
      break;
    }
    ok = readRequest(fd) && sendResponse(fd, response);
    sockets.close(fd);
    if (!ok) break;
  }
  sockets.closeListener();
  return ok;
}

bool serveRedirect(ServerSockets &sockets, bool follow)
{
  int fd = sockets.accept();
  sockets.closeListener();
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
  sockets.close(fd);
  return ok;
}

bool serveCrossOriginRedirect(ServerSockets &sockets)
{
  int fd = sockets.accept();
  sockets.closeListener();
  if (fd < 0) return false;
  static constexpr ZuCSpan redirect =
    "HTTP/1.1 302 Found\r\n"
    "Location: http://example.com/next\r\n"
    "Content-Length: 0\r\n"
    "Connection: close\r\n"
    "\r\n";
  bool ok = readRequest(fd) && sendResponse(fd, redirect);
  sockets.close(fd);
  return ok;
}

bool servePipeline(ServerSockets &sockets)
{
  int fd = sockets.accept();
  sockets.closeListener();
  if (fd < 0) return false;
  static constexpr ZuCSpan responses =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 2\r\n"
    "\r\n"
    "ok"
    "HTTP/1.1 201 Created\r\n"
    "Content-Length: 2\r\n"
    "Connection: close\r\n"
    "\r\n"
    "ok";
  bool ok = readRequests(fd, 2) && sendResponse(fd, responses);
  sockets.close(fd);
  return ok;
}

bool servePipelineStall(
    ServerSockets &sockets, ZmSemaphore &ready, unsigned count)
{
  int fd = sockets.accept();
  sockets.closeListener();
  if (fd < 0) return false;
  bool ok = readRequests(fd, count);
  ready.post();
  uint8_t byte;
  if (ok) ok = ::recv(fd, &byte, 1, 0) <= 0;
  sockets.close(fd);
  return ok;
}

bool serveWaves(ServerSockets &sockets)
{
  int fd = sockets.accept();
  sockets.closeListener();
  if (fd < 0) return false;
  static constexpr ZuCSpan response =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 2\r\n"
    "\r\n"
    "ok";
  static constexpr ZuCSpan finalResponse =
    "HTTP/1.1 201 Created\r\n"
    "Content-Length: 2\r\n"
    "Connection: close\r\n"
    "\r\n"
    "ok";
  bool ok = readRequest(fd) && sendResponse(fd, response) &&
    readRequest(fd) && sendResponse(fd, finalResponse);
  sockets.close(fd);
  return ok;
}

bool serveIdleStop(ServerSockets &sockets, ZmSemaphore &idle)
{
  int fd = sockets.accept();
  sockets.closeListener();
  if (fd < 0) return false;
  static constexpr ZuCSpan response =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 2\r\n"
    "\r\n"
    "ok";
  bool ok = readRequest(fd) && sendResponse(fd, response);
  idle.post();
  uint8_t byte;
  if (ok) ok = ::recv(fd, &byte, 1, 0) <= 0;
  sockets.close(fd);
  return ok;
}

bool serveParallel(
    ServerSockets &sockets, unsigned links, unsigned perLink)
{
  static constexpr ZuCSpan response =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 2\r\n"
    "\r\n"
    "ok";
  static constexpr ZuCSpan finalResponse =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 2\r\n"
    "Connection: close\r\n"
    "\r\n"
    "ok";
  bool ok = true;
  for (unsigned i = 0; i < links; ++i) {
    int fd = sockets.accept();
    if (fd < 0) {
      ok = false;
      break;
    }
    ok = readRequests(fd, perLink);
    for (unsigned j = 0; ok && j < perLink; ++j)
      ok = sendResponse(fd, j + 1 == perLink ? finalResponse : response);
    sockets.close(fd);
    if (!ok) break;
  }
  sockets.closeListener();
  return ok;
}

bool serveLimitedCombinations(
    ServerSockets &sockets, ZmSemaphore &ready,
    ZmSemaphore &release0, ZmSemaphore &release1)
{
  int a = sockets.accept();
  int b = sockets.accept();
  if (a < 0 || b < 0) {
    sockets.close(a);
    sockets.close(b);
    return false;
  }
  unsigned aKey, bKey;
  if (!readRequestKey(a, aKey) || !readRequestKey(b, bKey) ||
      aKey > 1 || bKey > 1 || aKey == bKey) {
    sockets.close(a);
    sockets.close(b);
    return false;
  }
  int fd0 = aKey ? b : a;
  int fd1 = aKey ? a : b;
  static constexpr ZuCSpan response =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 0\r\n"
    "\r\n";
  static constexpr ZuCSpan closeResponse =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 0\r\n"
    "Connection: close\r\n"
    "\r\n";
  ready.post();
  bool ok = release0.timedwait(Zm::now(10)) == 0 &&
    sendResponse(fd0, closeResponse);
  sockets.close(fd0);
  if (ok)
    ok = release1.timedwait(Zm::now(10)) == 0 &&
      sendResponse(fd1, response);
  unsigned key = 0;
  if (ok) ok = readRequestKey(fd1, key) && key == 2 &&
    sendResponse(fd1, closeResponse);
  sockets.close(fd1);
  int fd3 = ok ? sockets.accept() : -1;
  if (fd3 < 0)
    ok = false;
  else {
    ok = readRequestKey(fd3, key) && key == 3 &&
      sendResponse(fd3, closeResponse);
    sockets.close(fd3);
  }
  sockets.closeListener();
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
  auto config = Zhttp::Config()
    .concurrency(1)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);
  ZuCHECK(startApp(app, mx, port, config), "initialize/start agent");

  auto request0 = app.request();
  auto request1 = app.request();
  request0->target = "/";
  request1->target = "/";

  app.send(0, request0);
  app.send(0, request1);
  app.seal(0);
  app.cancel(0, request1->key());
  app.cancel(0, request0->key());
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "queued and active cancellation completes");
  app.stop();

  ZuCHECK(app.Base::completed() == 2 && app.results.length() == 2,
    "queued and active requests complete exactly once");
  ZuCHECK(app.results[0].code == Zhttp::ResultCode::Cancelled &&
      app.results[1].code == Zhttp::ResultCode::Cancelled,
    "cancellation result classification");
  ZuCHECK(app.cancelledEvents == 2 &&
      app.completedEvents == 2,
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
  ServerSockets sockets{fd};
  ZmThread server{[&sockets, &app, &serverOK]() {
    serverOK.store_(serveOutOfOrder(sockets, app));
  }};
  auto config = Zhttp::Config()
    .links(2).concurrency(2)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);
  ZuCHECK(startApp(app, mx, port, config), "initialize/start agent");

  auto request0 = app.request();
  auto request1 = app.request();
  request0->target = "/0";
  request1->target = "/1";

  app.send(0, request0);
  app.send(0, request1);
  app.seal(0);
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "out-of-order requests complete");
  app.stop();
  sockets.stop();
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

void pipeline()
{
  ZuTestScope(pipeline);

  uint16_t port;
  int fd = listener(port);
  ZuCHECK(fd >= 0 && port, "create pipeline loopback listener");
  if (fd < 0) return;

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start multiplexer");

  App app;
  app.expected = 2;
  ZmAtomic<unsigned> serverOK = 0;
  ServerSockets sockets{fd};
  ZmThread server{[&sockets, &serverOK]() {
    serverOK.store_(servePipeline(sockets));
  }};
  auto config = Zhttp::Config()
    .links(1).concurrency(2).linkMax(2)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(false).quic(false);
  ZuCHECK(startApp(app, mx, port, config), "initialize/start agent");

  auto request0 = app.request();
  auto request1 = app.request();
  request0->target = "/0";
  request1->target = "/1";
  app.send(0, request0);
  app.send(0, request1);
  app.seal(0);

  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "overlapping H1 operations complete");
  app.stop();
  sockets.stop();
  server.join();
  ZuCHECK(serverOK.load_(),
    "one TCP connection receives both pipelined requests");
  ZuCHECK(app.results.length() == 2 &&
      app.results[0].status == 200 && app.results[1].status == 201,
    "pipelined responses retain request FIFO order");
  ZuCHECK(app.completed() == 2 && !app.failed(),
    "pipelined operations complete exactly once");

  app.final();
  mx.stop();
}

void pipelineTxError()
{
  ZuTestScope(pipelineTxError);

  uint16_t port;
  int fd = listener(port);
  ZuCHECK(fd >= 0 && port, "create pipeline Tx-error listener");
  if (fd < 0) return;

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start pipeline Tx-error multiplexer");

  App app;
  app.expected = 2;
  ZmAtomic<unsigned> serverOK = 0;
  ZmSemaphore ready;
  ServerSockets sockets{fd};
  ZmThread server{[&sockets, &ready, &serverOK]() {
    serverOK.store_(servePipelineStall(sockets, ready, 2));
  }};
  auto config = Zhttp::Config()
    .links(1).concurrency(2).linkMax(2)
    .maxRetries(0)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(false).quic(false);
  bool started = startApp(app, mx, port, config);
  ZuCHECK(started, "initialize/start pipeline Tx-error pool");

  if (started) {
    app.send(0, app.request());
    app.send(0, app.request());
    app.seal(0);
  }
  bool emitted = started && ready.timedwait(Zm::now(10)) == 0;
  ZuCHECK(emitted, "both H1 operations are emitted before Tx failure");
  bool handled = false;
  if (emitted && app.poolImpl) {
    ZeException e;
    handled = app.poolImpl->poolH1TxError(0, 1, e);
  }
  bool completed = emitted && app.done.timedwait(Zm::now(10)) == 0;
  ZuCHECK(handled && completed,
    "generation-scoped H1 Tx failure drains the pipeline");
  app.stop();
  sockets.stop();
  server.join();

  ZuCHECK(serverOK.load_(), "Tx failure closes the pipelined connection");
  ZuCHECK(app.results.length() == 2 && app.completed() == 2 &&
      app.failed() == 2 &&
      app.attemptFailedEvents == 2 &&
      app.completedEvents == 2,
    "Tx failure completes each pipelined operation exactly once");

  app.final();
  mx.stop();
}

void pipelineCancellation(unsigned cancelAt)
{
  ZuTestScope(pipelineCancellation);

  enum { Requests = 3 };
  uint16_t port;
  int fd = listener(port);
  ZuCHECK(fd >= 0 && port,
    "create FIFO-cancellation listener for position ", cancelAt);
  if (fd < 0) return;

  ZiMultiplex mx{mxParams()};
  bool mxUp = mx.start();
  ZuCHECK(mxUp,
    "start FIFO-cancellation multiplexer for position ", cancelAt);
  if (!mxUp) {
    ::close(fd);
    return;
  }

  App app;
  app.expected = Requests;
  ZmAtomic<unsigned> serverOK = 0;
  ZmSemaphore ready;
  ServerSockets sockets{fd};
  ZmThread server{[&sockets, &ready, &serverOK]() {
    serverOK.store_(servePipelineStall(sockets, ready, Requests));
  }};
  auto config = Zhttp::Config()
    .links(1).concurrency(Requests).linkMax(Requests)
    .maxRetries(0)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(false).quic(false);
  bool started = startApp(app, mx, port, config);
  ZuCHECK(started,
    "start FIFO-cancellation pool for position ", cancelAt);

  ZtArray<ZmRef<ReqBuilder>,
    ZtArrayHeapID<"Zhttp.Test.CancelFIFO">> requests;
  if (started)
    for (unsigned i = 0; i < Requests; ++i) {
      requests.push(app.request());
      app.send(0, requests[i]);
    }
  if (started) app.seal(0);
  bool emitted = started && ready.timedwait(Zm::now(10)) == 0;
  ZuCHECK(emitted,
    "emit complete H1 FIFO before cancelling position ", cancelAt);
  bool cancelled = emitted && app.cancel(0, requests[cancelAt]->key());
  bool completed = cancelled && app.done.timedwait(Zm::now(10)) == 0;
  ZuCHECK(cancelled && completed,
    "cancel emitted H1 FIFO position ", cancelAt);

  app.stop();
  sockets.stop();
  server.join();
  ZuCHECK(serverOK.load_(),
    "FIFO cancellation closes its native connection at position ",
    cancelAt);
  bool resultsOK = app.results.length() == Requests &&
    app.completed() == Requests && requests.length() == Requests;
  for (unsigned i = 0, n = requests.length(); i < n; ++i)
    resultsOK &= requests[i]->completions == 1 &&
      requests[i]->resultCode == (i == cancelAt ?
	Zhttp::ResultCode::Cancelled : Zhttp::ResultCode::Failed);
  ZuCHECK(resultsOK &&
      app.cancelledEvents == 1 &&
      app.completedEvents == Requests,
    "FIFO cancellation completes each position exactly once at ",
    cancelAt);

  app.final();
  mx.stop();
}

void limited()
{
  ZuTestScope(limited);

  uint16_t port;
  int fd = listener(port);
  ZuCHECK(fd >= 0 && port, "create rate-limit loopback listener");
  if (fd < 0) return;

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start multiplexer");

  App app;
  app.expected = 2;
  ZmAtomic<unsigned> serverOK = 0;
  ServerSockets sockets{fd};
  ZmThread server{[&sockets, &serverOK]() {
    serverOK.store_(serveTwo(sockets));
  }};
  auto config = Zhttp::Config()
    .links(2).concurrency(2).linkMax(1)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(false).quic(false);
  ZuCHECK(startApp(app, mx, port, config), "initialize/start agent");

  ZuCHECK(app.limited(0, 0, true) && app.limited(0, 0, true) &&
      app.limited(0, 1, true) && app.limited(0, 1, true),
    "idempotently limit every stable link slot");
  ZmSemaphore rxBarrier;
  mx.run([&rxBarrier]() { rxBarrier.post(); }, mx.sid("3"));
  ZuCHECK(rxBarrier.timedwait(Zm::now(10)) == 0,
    "rate limits reach the pool Rx owner");
  auto request0 = app.request();
  request0->target = "/0";
  app.send(0, request0);
  ZmSemaphore txBarrier;
  app.txRun(0, [&txBarrier]() { txBarrier.post(); });
  ZuCHECK(txBarrier.timedwait(Zm::now(10)) == 0,
    "queued request reaches the pool Tx owner");
  mx.run([&rxBarrier]() { rxBarrier.post(); }, mx.sid("3"));
  ZuCHECK(rxBarrier.timedwait(Zm::now(10)) == 0 &&
      app.archiveDone.trywait() != 0,
    "all-limited links leave the request queued without admission");

  ZuCHECK(app.limited(0, 1, false) && app.limited(0, 1, false),
    "idempotently restore stable link slot 1");
  ZuCHECK(app.archiveDone.timedwait(Zm::now(10)) == 0,
    "request completes through the remaining link");

  ZuCHECK(app.limited(0, 0, false) && app.limited(0, 0, false),
    "idempotently restore stable link slot 0");
  auto request1 = app.request();
  request1->target = "/1";
  app.send(0, request1);
  app.seal(0);
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "request completes through restored link");
  ZmSemaphore stopped;
  bool stopOK = false;
  app.stop([&stopped, &stopOK](bool ok) {
    stopOK = ok;
    stopped.post();
  });
  ZuCHECK(app.limited(0, 0, true) && app.limited(0, 0, false),
    "limit changes remain safe during orderly shutdown");
  ZuCHECK(stopped.timedwait(Zm::now(10)) == 0 && stopOK,
    "orderly shutdown completes after rate-limit changes");
  sockets.stop();
  server.join();

  ZuCHECK(serverOK.load_(), "rate-limit test servers receive both requests");
  ZuCHECK(app.results.length() == 2 &&
      app.results[0].link == 1 && app.results[1].link == 0,
    "round-robin skips and then restores the limited link");

  app.final();
  mx.stop();
}

void limitedSaturated()
{
  ZuTestScope(limitedSaturated);

  uint16_t port;
  int fd = listener(port);
  ZuCHECK(fd >= 0 && port, "create combined capacity-gate listener");
  if (fd < 0) return;

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start combined capacity-gate multiplexer");

  App app;
  app.expected = 4;
  ZmAtomic<unsigned> serverOK = 0;
  ZmSemaphore ready, release0, release1;
  ServerSockets sockets{fd};
  ZmThread server{[
    &sockets, &ready, &release0, &release1, &serverOK
  ]() {
    serverOK.store_(serveLimitedCombinations(
      sockets, ready, release0, release1));
  }};
  auto config = Zhttp::Config()
    .links(2).concurrency(4).linkMax(1)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(false).quic(false);
  ZuCHECK(startApp(app, mx, port, config),
    "initialize/start combined capacity-gate agent");

  auto request0 = app.request();
  auto request1 = app.request();
  request0->target = "/0";
  request1->target = "/1";
  app.send(0, ZuMv(request0));
  app.send(0, ZuMv(request1));
  ZuCHECK(ready.timedwait(Zm::now(10)) == 0,
    "both links reach configured H1 saturation");
  ZuCHECK(app.limited(0, 0, true),
    "application limiting composes with protocol saturation");
  ZmSemaphore rxBarrier;
  mx.run([&rxBarrier]() { rxBarrier.post(); }, mx.sid("3"));
  ZuCHECK(rxBarrier.timedwait(Zm::now(10)) == 0,
    "the limited gate reaches the saturated link owner");

  release0.post();
  ZuCHECK(app.archiveDone.timedwait(Zm::now(10)) == 0,
    "the saturated+limited link drains its active operation");
  auto request2 = app.request();
  request2->target = "/2";
  app.send(0, ZuMv(request2));
  ZmSemaphore txBarrier;
  app.txRun(0, [&txBarrier]() { txBarrier.post(); });
  ZuCHECK(txBarrier.timedwait(Zm::now(10)) == 0,
    "replacement work reaches the pool Tx owner");
  mx.run([&rxBarrier]() { rxBarrier.post(); }, mx.sid("3"));
  ZuCHECK(rxBarrier.timedwait(Zm::now(10)) == 0 &&
      app.archiveDone.trywait() != 0,
    "limited-only and saturated-only links leave replacement work queued");

  release1.post();
  ZuCHECK(app.archiveDone.timedwait(Zm::now(10)) == 0 &&
      app.archiveDone.timedwait(Zm::now(10)) == 0,
    "capacity restoration admits work through the un-limited link");
  ZuCHECK(app.limited(0, 0, false),
    "clearing the surviving limited gate restores the disconnected link");
  auto request3 = app.request();
  request3->target = "/3";
  app.send(0, ZuMv(request3));
  app.seal(0);
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "the restored persistent link reconnects and completes");
  app.stop();
  sockets.stop();
  server.join();

  ZuCHECK(serverOK.load_(),
    "the server observes queued admission and persistent-link reconnect");
  ZuCHECK(app.results.length() == 4 &&
      app.results[0].link == 0 && app.results[1].link == 1 &&
      app.results[2].link == 1 && app.results[3].link == 0,
    "limited and saturated gates remain independent across reconnect");

  app.final();
  mx.stop();
}

void parallelism()
{
  ZuTestScope(parallelism);

  enum { Links = 10, PerLink = 10, Requests = Links * PerLink };
  uint16_t port;
  int fd = listener(port);
  ZuCHECK(fd >= 0 && port, "create parallelism loopback listener");
  if (fd < 0) return;

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start parallelism multiplexer");

  App app;
  app.expected = Requests;
  ZmAtomic<unsigned> serverOK = 0;
  ServerSockets sockets{fd};
  ZmThread server{[&sockets, &serverOK]() {
    serverOK.store_(serveParallel(sockets, Links, PerLink));
  }};
  auto config = Zhttp::Config()
    .links(Links).concurrency(Requests).linkMax(PerLink)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(false).quic(false);
  ZuCHECK(startApp(app, mx, port, config), "initialize/start parallel pool");
  ResolveCounter resolver;
  app.discoveryResolver(&resolver.ops);

  for (unsigned i = 0; i < Requests; ++i) {
    auto request = app.request();
    request->target << i;
    app.send(0, ZuMv(request));
  }
  app.seal(0);
  ZuCHECK(app.done.timedwait(Zm::now(20)) == 0,
    "100 overlapping operations complete over 10 links");
  app.stop();
  sockets.stop();
  server.join();

  ZtArray<unsigned, ZtArrayHeapID<"Zhttp.Test.Distribution">>
    distribution;
  distribution.length(Links);
  for (unsigned i = 0; i < Links; ++i) distribution[i] = 0;
  bool valid = app.results.length() == Requests;
  for (unsigned i = 0, n = app.results.length(); i < n; ++i) {
    const auto &result = app.results[i];
    valid &= result.ok() && result.link < Links;
    if (result.link < Links) ++distribution[result.link];
  }
  for (unsigned i = 0; i < Links; ++i)
    valid &= distribution[i] == PerLink;
  ZuCHECK(serverOK.load_(),
    "server observes no more than the configured 10 connections");
  ZuCHECK(valid,
    "round-robin assigns exactly 10 overlapping operations per link");
  ZuCHECK(app.completed() == Requests && !app.failed(),
    "pool concurrency is independent of physical link count");
  ZuCHECK(!resolver.queries && resolver.resolves == 1 &&
      resolver.cancels == 1,
    "one pool resolution is shared and its synchronous stale handle is "
    "cancelled");

  app.final();
  mx.stop();
}

void waves()
{
  ZuTestScope(waves);

  uint16_t port;
  int fd = listener(port);
  ZuCHECK(fd >= 0 && port, "create successive-wave listener");
  if (fd < 0) return;

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start successive-wave multiplexer");
  uint64_t heapBaseline = zhttpClientAllocated();
  App app;
  app.expected = 2;
  ZmAtomic<unsigned> serverOK = 0;
  ServerSockets sockets{fd};
  ZmThread server{[&sockets, &serverOK]() {
    serverOK.store_(serveWaves(sockets));
  }};
  auto config = Zhttp::Config()
    .links(1).concurrency(1).linkMax(1)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(false).quic(false);
  ZuCHECK(startApp(app, mx, port, config),
    "initialize/start successive-wave pool");

  app.send(0, app.request());
  ZuCHECK(app.archiveDone.timedwait(Zm::now(10)) == 0,
    "first wave completes before the second is submitted");
  app.send(0, app.request());
  app.seal(0);
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "second wave completes on the reusable pool link");
  app.stop();
  sockets.stop();
  server.join();

  ZuCHECK(serverOK.load_(),
    "both waves use one persistent native H1 connection");
  ZuCHECK(app.results.length() == 2 && app.results[0].ok() &&
      app.results[1].ok() && app.results[0].link == 0 &&
      app.results[1].link == 0 && app.results[0].status == 200 &&
      app.results[1].status == 201,
    "successive waves retain stable pool/link identity");

  app.final();
  uint64_t heapFinal = zhttpClientAllocated();
  ZuCHECK(heapFinal == heapBaseline,
    "client final returns identified Zhttp heaps to baseline (before=",
    heapBaseline, ", after=", heapFinal, ')');
  mx.stop();
}

void stopQueuedLimited()
{
  ZuTestScope(stopQueuedLimited);

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start queued-stop multiplexer");
  uint64_t heapBaseline = zhttpClientAllocated();
  App app;
  app.expected = 1;
  auto config = Zhttp::Config()
    .links(1).concurrency(1).linkMax(1)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(false).quic(false);
  bool started = startApp(app, mx, 1, config);
  ZuCHECK(started, "start queued-stop pool");
  bool limited = started && app.limited(0, 0, true);
  ZmSemaphore rxBarrier;
  mx.run([&rxBarrier]() { rxBarrier.post(); }, mx.sid("3"));
  limited &= rxBarrier.timedwait(Zm::now(10)) == 0;
  ZuCHECK(limited, "publish application limit before queueing");

  ZmRef<ReqBuilder> request = app.request();
  if (started) {
    app.send(0, request);
    app.seal(0);
  }
  ZmSemaphore txBarrier;
  app.txRun(0, [&txBarrier]() { txBarrier.post(); });
  bool queued = txBarrier.timedwait(Zm::now(10)) == 0;
  mx.run([&rxBarrier]() { rxBarrier.post(); }, mx.sid("3"));
  queued &= rxBarrier.timedwait(Zm::now(10)) == 0 &&
    app.done.trywait() != 0;
  ZuCHECK(queued, "limited link retains queued request without connecting");

  ZmSemaphore stopped;
  ZmAtomic<unsigned> stopCalls = 0;
  bool stopOK = false;
  app.stop([&stopped, &stopCalls, &stopOK](bool ok) {
    stopOK = ok;
    ++stopCalls;
    stopped.post();
  });
  bool drained = stopped.timedwait(Zm::now(10)) == 0 &&
    app.done.timedwait(Zm::now(10)) == 0;
  ZuCHECK(drained && stopOK && stopCalls.load_() == 1 &&
      request->completions == 1 &&
      request->resultCode == Zhttp::ResultCode::Cancelled,
    "stop drains queued limited work exactly once");

  request = nullptr;
  app.final();
  uint64_t heapFinal = zhttpClientAllocated();
  ZuCHECK(heapFinal == heapBaseline,
    "queued-stop final returns production heaps to baseline (before=",
    heapBaseline, ", after=", heapFinal, ')');
  mx.stop();
}

void stopActivePipeline()
{
  ZuTestScope(stopActivePipeline);

  enum { Requests = 2 };
  uint16_t port;
  int fd = listener(port);
  ZuCHECK(fd >= 0 && port, "create active-stop listener");
  if (fd < 0) return;
  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start active-stop multiplexer");

  App app;
  app.expected = Requests;
  ZmAtomic<unsigned> serverOK = 0;
  ZmSemaphore ready;
  ServerSockets sockets{fd};
  ZmThread server{[&sockets, &ready, &serverOK]() {
    serverOK.store_(servePipelineStall(sockets, ready, Requests));
  }};
  auto config = Zhttp::Config()
    .links(1).concurrency(Requests).linkMax(Requests)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(false).quic(false);
  bool started = startApp(app, mx, port, config);
  ZuCHECK(started, "start active-stop pool");
  ZtArray<ZmRef<ReqBuilder>,
    ZtArrayHeapID<"Zhttp.Test.StopActive">> requests;
  if (started)
    for (unsigned i = 0; i < Requests; ++i) {
      requests.push(app.request());
      app.send(0, requests[i]);
    }
  bool emitted = started && ready.timedwait(Zm::now(10)) == 0;
  ZuCHECK(emitted, "emit pipelined requests before stop");

  ZmSemaphore stopped;
  ZmAtomic<unsigned> stopCalls = 0;
  bool stopOK = false;
  app.stop([&stopped, &stopCalls, &stopOK](bool ok) {
    stopOK = ok;
    ++stopCalls;
    stopped.post();
  });
  bool drained = stopped.timedwait(Zm::now(10)) == 0 &&
    app.done.timedwait(Zm::now(10)) == 0;
  sockets.stop();
  server.join();
  bool resultsOK = requests.length() == Requests &&
    app.results.length() == Requests && app.completed() == Requests;
  for (auto &request: requests)
    resultsOK &= request->completions == 1 &&
      request->resultCode == Zhttp::ResultCode::Cancelled;
  ZuCHECK(drained && stopOK && stopCalls.load_() == 1 && resultsOK,
    "stop drains every active pipelined operation exactly once");
  ZuCHECK(serverOK.load_(), "active stop closes the native H1 connection");

  app.final();
  mx.stop();
}

void stopReusableIdle()
{
  ZuTestScope(stopReusableIdle);

  uint16_t port;
  int fd = listener(port);
  ZuCHECK(fd >= 0 && port, "create reusable-idle stop listener");
  if (fd < 0) return;
  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start reusable-idle stop multiplexer");
  uint64_t heapBaseline = zhttpClientAllocated();

  App app;
  app.expected = 1;
  ZmAtomic<unsigned> serverOK = 0;
  ZmSemaphore idle;
  ServerSockets sockets{fd};
  ZmThread server{[&sockets, &idle, &serverOK]() {
    serverOK.store_(serveIdleStop(sockets, idle));
  }};
  auto config = Zhttp::Config()
    .links(1).concurrency(1).linkMax(1)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(false).quic(false);
  bool started = startApp(app, mx, port, config);
  ZuCHECK(started, "start reusable-idle pool");
  ZmRef<ReqBuilder> request = app.request();
  if (started) {
    app.send(0, request);
    app.seal(0);
  }
  bool completed = app.done.timedwait(Zm::now(10)) == 0 &&
    idle.timedwait(Zm::now(10)) == 0;
  ZuCHECK(completed && request->completions == 1 &&
      request->resultCode == Zhttp::ResultCode::OK,
    "request completes while its H1 connection remains reusable");

  ZmSemaphore stopped;
  ZmAtomic<unsigned> stopCalls = 0;
  bool stopOK = false;
  app.stop([&stopped, &stopCalls, &stopOK](bool ok) {
    stopOK = ok;
    ++stopCalls;
    stopped.post();
  });
  bool drained = stopped.timedwait(Zm::now(10)) == 0;
  sockets.stop();
  server.join();
  ZuCHECK(drained && stopOK && stopCalls.load_() == 1 && serverOK.load_(),
    "stop closes a reusable-idle connection exactly once");

  request = nullptr;
  app.final();
  uint64_t heapFinal = zhttpClientAllocated();
  ZuCHECK(heapFinal == heapBaseline,
    "idle-stop final returns production heaps to baseline (before=",
    heapBaseline, ", after=", heapFinal, ')');
  mx.stop();
}

void boundedLinkScan()
{
  ZuTestScope(boundedLinkScan);

  enum {
    Limited = Zhttp::ClientWorkBatch,
    Links = Limited + 6
  };
  uint16_t port;
  int fd = listener(port);
  ZuCHECK(fd >= 0 && port, "create bounded-scan loopback listener");
  if (fd < 0) return;

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start bounded-scan multiplexer");

  App app;
  app.expected = 1;
  ZmAtomic<unsigned> serverOK = 0;
  ServerSockets sockets{fd};
  ZmThread server{[&sockets, &serverOK]() {
    serverOK.store_(serveParallel(sockets, 1, 1));
  }};
  auto config = Zhttp::Config()
    .links(Links).concurrency(1).linkMax(1)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(false).quic(false);
  ZuCHECK(startApp(app, mx, port, config),
    "initialize/start a pool larger than one scheduler batch");
  bool limitsOK = true;
  for (unsigned i = 0; i < Limited; ++i)
    limitsOK &= app.limited(0, i, true);
  ZuCHECK(limitsOK, "limit the early stable-link scan batch");
  ZmSemaphore limited;
  mx.run([&limited]() { limited.post(); }, mx.sid("3"));
  ZuCHECK(limited.timedwait(Zm::now(10)) == 0,
    "publish every application link limit before admission");

  app.send(0, app.request());
  app.seal(0);
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "bounded admission continues on a later scheduler turn");
  app.stop();
  sockets.stop();
  server.join();

  ZuCHECK(serverOK.load_(), "the later eligible link carries the request");
  ZuCHECK(app.results.length() == 1 && app.results[0].ok() &&
      app.results[0].link == Limited,
    "round-robin resumes at the first link beyond the scan batch");

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
  auto config = Zhttp::Config()
    .concurrency(1).requestTimeout(1)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);
  ZuCHECK(startApp(app, mx, port, config), "initialize/start agent");

  auto request = app.request();
  request->target = "/";

  app.send(0, request);
  app.seal(0);
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0, "request timeout completes");
  ZuCHECK(app.Base::completed() == 1 && app.results.length() == 1 &&
      app.results[0].code == Zhttp::ResultCode::TimedOut,
    "timeout result classification");
  app.stop();
  ZuCHECK(app.completedEvents == 1,
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
  ServerSockets sockets;
  ZmThread server{[&app, &sockets, &serverOK]() {
    if (app.serverReady.timedwait(Zm::now(10)) == 0 &&
	sockets.reset(app.retryFD))
      serverOK.store_(serve(sockets));
  }};

  auto config = Zhttp::Config()
    .concurrency(1).maxRetries(1)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);
  ZuCHECK(startApp(app, mx, port, config),
    "initialize/start retry agent");

  auto request = app.request();
  request->target = "/";
  request->method = Zhttp::Method::POST;
  app.send(0, request);
  app.seal(0);
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "transient failure retry completes");
  app.stop();
  sockets.stop();
  server.join();

  ZuCHECK(serverOK.load_(), "retry server receives request");
  ZuCHECK(app.results.length() == 1 && app.results[0].ok() &&
      app.results[0].retries == 1,
    "retry produces one successful terminal result");
  ZuCHECK(request->inits == 1,
    "connect retry initializes the response parser once");
  ZuCHECK(app.selectedEvents == 2 &&
      app.attemptFailedEvents == 1 &&
      app.retriedEvents == 1 &&
      app.completedEvents == 1,
    "typed retry transition sequence");
  ZuCHECK(app.failureTransient, "retry failure is classified transient");
  ZuCHECK(app.retriedRequest == app.failedRequest &&
      app.retriedPreviousAttempt == app.failedAttempt,
    "retry retains request ID and links distinct attempts");
  ZuCHECK(app.failedAttempt && app.retriedAttempt &&
      app.failedAttempt != app.retriedAttempt,
    "retry attempt IDs are distinct");

  app.final();
  mx.stop();
}

void redirectUnsafe()
{
  ZuTestScope(redirectUnsafe);

  uint16_t port;
  int fd = listener(port);
  ZuCHECK(fd >= 0 && port, "create non-idempotent redirect listener");
  if (fd < 0) return;
  ZmAtomic<unsigned> serverOK = 0;
  ServerSockets sockets{fd};
  ZmThread server{[&sockets, &serverOK]() {
    serverOK.store_(serveRedirect(sockets, false));
  }};

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start non-idempotent redirect multiplexer");
  App app;
  app.expected = 1;
  auto config = Zhttp::Config()
    .concurrency(1).maxRedirects(2)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);
  ZuCHECK(startApp(app, mx, port, config),
    "initialize/start non-idempotent redirect agent");

  auto request = app.request();
  request->method = Zhttp::Method::POST;
  request->target = "/start";
  app.send(0, request);
  app.seal(0);
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "non-idempotent redirect completes without replay");
  app.stop();
  sockets.stop();
  server.join();

  ZuCHECK(serverOK.load_(),
    "non-idempotent redirect server receives one request");
  ZuCHECK(app.results.length() == 1 &&
      app.results[0].code == Zhttp::ResultCode::ReplayUnsafe &&
      app.results[0].redirects == 0,
    "non-idempotent redirect is not replayed");
  ZuCHECK(app.selectedEvents == 1 && !app.redirectedEvents &&
      app.completedEvents == 1,
    "non-idempotent redirect has one terminal attempt");

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
  ServerSockets sockets{fd};
  ZmThread server{[&sockets, &serverOK]() {
    serverOK.store_(serveRedirect(sockets, true));
  }};

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start multiplexer");
  App app;
  app.expected = 1;
  auto config = Zhttp::Config()
    .concurrency(1).maxRedirects(2)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);
  ZuCHECK(startApp(app, mx, port, config),
    "initialize/start redirect agent");

  auto request = app.request();
  request->method = Zhttp::Method::POST;
  request->idempotent_ = true;
  request->target = "/start";
  app.send(0, request);
  app.seal(0);
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0, "redirect completes");
  app.stop();
  sockets.stop();
  server.join();

  ZuCHECK(serverOK.load_(), "redirect server receives both requests");
  ZuCHECK(app.results.length() == 1 && app.results[0].ok() &&
      app.results[0].redirects == 1,
    "redirect produces one successful terminal result");
  ZuCHECK(request->inits == 2,
    "redirect reinitializes the response parser per wire response");
  ZuCHECK(app.selectedEvents == 2 &&
      app.redirectedEvents == 1 &&
      app.completedEvents == 1,
    "typed redirect transition sequence");
  ZuCHECK(app.redirectedRequest == app.firstSelectedRequest &&
      app.redirectedPreviousAttempt == app.firstSelectedAttempt,
    "redirect retains request ID and links distinct attempts");
  ZuCHECK(app.firstSelectedAttempt && app.redirectedAttempt &&
      app.firstSelectedAttempt != app.redirectedAttempt &&
      app.results[0].request == app.firstSelectedRequest &&
      app.results[0].attempt == app.redirectedAttempt,
    "redirect terminal identity uses the new attempt");

  app.final();
  mx.stop();
}

void crossOriginRedirect()
{
  ZuTestScope(crossOriginRedirect);

  uint16_t port;
  int fd = listener(port);
  ZuCHECK(fd >= 0 && port, "create cross-origin redirect listener");
  if (fd < 0) return;
  ZmAtomic<unsigned> serverOK = 0;
  ServerSockets sockets{fd};
  ZmThread server{[&sockets, &serverOK]() {
    serverOK.store_(serveCrossOriginRedirect(sockets));
  }};

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start multiplexer");
  App app;
  app.expected = 1;
  auto config = Zhttp::Config()
    .concurrency(1).maxRedirects(2)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);
  ZuCHECK(startApp(app, mx, port, config),
    "initialize/start cross-origin redirect agent");

  auto request = app.request();
  request->target = "/start";
  app.send(0, request);
  app.seal(0);
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "cross-origin redirect refusal completes");
  app.stop();
  sockets.stop();
  server.join();

  ZuCHECK(serverOK.load_(),
    "cross-origin redirect server receives one request");
  ZuCHECK(app.results.length() == 1 &&
      app.results[0].code == Zhttp::ResultCode::InvalidRedirect &&
      app.results[0].redirects == 0,
    "cross-origin redirect is rejected without replay");
  ZuCHECK(app.selectedEvents == 1 &&
      app.redirectedEvents == 0 &&
      app.completedEvents == 1,
    "cross-origin redirect has no new attempt");

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
  auto config = Zhttp::Config()
    .concurrency(1).maxRetries(2)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);
  ZuCHECK(startApp(app, mx, port, config),
    "initialize/start retry-limit agent");

  auto request = app.request();
  request->target = "/";
  app.send(0, request);
  app.seal(0);
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "retry budget exhaustion completes");
  app.stop();

  ZuCHECK(app.results.length() == 1 &&
      app.results[0].code == Zhttp::ResultCode::Failed &&
      app.results[0].retries == 2,
    "retry budget produces one failed result");
  ZuCHECK(app.selectedEvents == 3 &&
      app.attemptFailedEvents == 3 &&
      app.retriedEvents == 2 &&
      app.completedEvents == 1,
    "retry budget bounds attempts");

  app.final();
  mx.stop();
}

void configuration()
{
  ZuTestScope(configuration);

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start configuration multiplexer");

  auto defaults = Zhttp::Config()
    .links(2).concurrency(4).linkMax(2).secure(false)
    .requestTimeout(17).maxRetries(3).maxRedirects(5)
    .maxOrigins(41).maxAltSvc(7)
    .retainedBodyMax(700).retainedMessageMax(900)
    .discoveryLimits({3, 4, 5, 6})
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .h2Policy(Zhttp::H2Policy::Disable)
    .blindH3(true).altSvcCrossHost(true)
    .tcp(true).tls(false).quic(false);
  App app;
  ZuCHECK(app.init(
      Zhttp::HubConfig{&mx, "3", "4"}, 2, defaults,
      Zhttp::TCPConfig{}, Zhttp::H2Config{}, Zhttp::QUICConfig{}),
    "initialize two-pool coordinator");
  ZuCHECK(!app.pool(
      2, Zhttp::Destination{ZuBSpan{"127.0.0.1"}, 1}),
    "reject out-of-range pool slot");
  ZuCHECK(!app.pool(
      1, Zhttp::Destination{ZuBSpan{"127.0.0.1"}, 0}),
    "reject a zero destination port");
  ZuCHECK(!app.pool(
      1, Zhttp::Destination{ZuBSpan{"bad host"}, 1}),
    "reject an invalid destination host");
  ZuCHECK(app.pool(
      0, Zhttp::Destination{ZuBSpan{"127.0.0.1"}, 1}),
    "configure pool slot 0 from client defaults");
  ZuCHECK(!app.pool(
      0, Zhttp::Destination{ZuBSpan{"127.0.0.1"}, 2}),
    "reject duplicate pool slot");
  ZuCHECK(!app.start(), "reject start with an unconfigured pool slot");
  ZuCHECK(!app.pool(
      1, Zhttp::Destination{ZuBSpan{"localhost"}, 2},
      Zhttp::Config{}.links(0)),
    "reject an invalid effective pool capacity");
  ZuCHECK(!app.pool(
      1, Zhttp::Destination{ZuBSpan{"localhost"}, 2},
      Zhttp::Config{}.secure(false).tcp(false)),
    "reject an effective plain policy without TCP");
  ZuCHECK(!app.pool(
      1, Zhttp::Destination{ZuBSpan{"localhost"}, 2},
      Zhttp::Config{}
	.secure(true).protocol(Zhttp::ProtoPolicy::ForceH3).quic(false)),
    "reject a forced-H3 policy without QUIC");
  auto overrides = Zhttp::Config()
    .links(1).concurrency(2).linkMax(1).requestTimeout(0)
    .maxRetries(1).maxRedirects(2).maxOrigins(19).maxAltSvc(3)
    .retainedBodyMax(70).retainedMessageMax(90)
    .discoveryLimits({8, 9, 10, 11})
    .protocol(Zhttp::ProtoPolicy::PreferH3)
    .h2Policy(Zhttp::H2Policy::Prefer)
    .blindH3(false).altSvcCrossHost(false)
    .secure(true).tcp(true).tls(true).quic(true);
  ZuCHECK(app.pool(
      1, Zhttp::Destination{ZuBSpan{"localhost"}, 2}, overrides),
    "configure pool slot 1 with common-type overrides");
  ZuCHECK(app.start(), "start all configured pools");
  ZuCHECK(!app.pool(
      1, Zhttp::Destination{ZuBSpan{"localhost"}, 3}),
    "reject pool registration after start");
  const auto *config0 = app.config(0);
  const auto *config1 = app.config(1);
  ZuCHECK(config0 && config0->links() == 2 &&
      config0->concurrency() == 4 && config0->linkMax() == 2 &&
      config0->requestTimeout() == 17 && config0->maxRetries() == 3 &&
      config0->maxRedirects() == 5 && config0->retainedBodyMax() == 700 &&
      config0->retainedMessageMax() == 900 &&
      config0->maxOrigins() == 41 && config0->maxAltSvc() == 7 &&
      config0->discoveryLimits().maxRecords == 3 &&
      config0->discoveryLimits().maxHints == 4 &&
      config0->discoveryLimits().maxEndpoints == 5 &&
      config0->discoveryLimits().maxAliasDepth == 6 &&
      config0->protocol() == Zhttp::ProtoPolicy::DisableH3 &&
      config0->h2Policy() == Zhttp::H2Policy::Disable &&
      config0->blindH3() && config0->altSvcCrossHost() &&
      !config0->secure() && config0->tcp() &&
      !config0->tls() && !config0->quic(),
    "pool without overrides inherits the complete client policy");
  ZuCHECK(config1 && config1->links() == 1 &&
      config1->concurrency() == 2 && config1->linkMax() == 1 &&
      config1->requestTimeout() == 0 && config1->maxRetries() == 1 &&
      config1->maxRedirects() == 2 && config1->retainedBodyMax() == 70 &&
      config1->retainedMessageMax() == 90 &&
      config1->maxOrigins() == 19 && config1->maxAltSvc() == 3 &&
      config1->discoveryLimits().maxRecords == 8 &&
      config1->discoveryLimits().maxHints == 9 &&
      config1->discoveryLimits().maxEndpoints == 10 &&
      config1->discoveryLimits().maxAliasDepth == 11 &&
      config1->protocol() == Zhttp::ProtoPolicy::PreferH3 &&
      config1->h2Policy() == Zhttp::H2Policy::Prefer &&
      !config1->blindH3() && !config1->altSvcCrossHost() &&
      config1->secure() && config1->tcp() &&
      config1->tls() && config1->quic(),
    "pool overrides are isolated and explicit zero remains a value");
  ZuCHECK(app.limited(0, 1, true) && app.limited(0, 1, false) &&
      !app.limited(0, 2, true) && !app.limited(2, 0, true),
    "validate pool/link slots for application limiting");
  app.stop();
  app.final();
  mx.stop();
}

void destination()
{
  ZuTestScope(destination);

  Zhttp::Destination name{ZuBSpan{"Example.COM"}, 8443};
  Zhttp::Destination ipv6{ZuBSpan{"2001:0db8::1"}, 443, true};
  Zhttp::Destination zero{ZuBSpan{"example.com"}, 0};
  Zhttp::Destination bad{ZuBSpan{"bad host"}, 443};
  ZuCHECK(name.valid() && name.host == "example.com" &&
      name.authority == "example.com:8443",
    "destination owns and normalizes a DNS authority");
  ZuCHECK(ipv6.valid() && ipv6.host == "2001:db8::1" &&
      ipv6.authority == "[2001:db8::1]:443",
    "destination canonicalizes and brackets IPv6 exactly once");
  ZuCHECK(!zero.valid() && !bad.valid(),
    "destination rejects zero ports and malformed hosts");
}

void stopResolving()
{
  ZuTestScope(stopResolving);

  enum : unsigned {
    // Cross two 64-item stop batches while one request owns resolution.
    Requests = 130
  };

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start resolving-stop multiplexer");
  auto config = Zhttp::Config()
    .links(1).concurrency(1).linkMax(1).secure(false)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(false).quic(false);
  App app;
  app.expected = Requests;
  bool inited = app.init(
    Zhttp::HubConfig{&mx, "3", "4"}, 1, config,
    Zhttp::TCPConfig{}, Zhttp::H2Config{}, Zhttp::QUICConfig{});
  inited = inited && app.pool(
    0, Zhttp::Destination{ZuBSpan{"pending.invalid"}, 80});
  HoldingResolver resolver;
  app.discoveryResolver(&resolver.ops);
  ZuCHECK(inited && app.start(), "start pool with an injected resolver");

  for (unsigned i = 0; i < Requests; ++i)
    app.send(0, app.request());
  app.seal(0);
  ZuCHECK(resolver.entered.timedwait(Zm::now(10)) == 0,
    "request enters pool-scoped resolution");
  app.stop();
  bool cancelled = app.results.length() == Requests;
  for (const auto &result: app.results)
    cancelled &= result.code == Zhttp::ResultCode::Cancelled;
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0 &&
      cancelled,
    "stop batches queued work and completes every request exactly once");
  ZuCHECK(resolver.cancelled == 1 && !app.active() &&
      app.completed() == Requests && app.failed() == Requests,
    "stop cancels shared resolution and drains pool counters");

  app.final();
  mx.stop();
}

void multiplePools()
{
  ZuTestScope(multiplePools);

  uint16_t port0, port1;
  int fd0 = listener(port0);
  int fd1 = listener(port1);
  ZuCHECK(fd0 >= 0 && fd1 >= 0 && port0 && port1,
    "create independent pool listeners");
  if (fd0 < 0 || fd1 < 0) {
    if (fd0 >= 0) ::close(fd0);
    if (fd1 >= 0) ::close(fd1);
    return;
  }
  Zhttp::Destination dest0{ZuBSpan{"127.0.0.1"}, port0};
  Zhttp::Destination dest1{ZuBSpan{"127.0.0.1"}, port1};
  ZmAtomic<unsigned> server0OK = 0;
  ZmAtomic<unsigned> server1OK = 0;
  ServerSockets sockets0{fd0};
  ServerSockets sockets1{fd1};
  ZmThread server0{[&sockets0, &dest0, &server0OK]() {
    server0OK.store_(serve(sockets0, "/pool0", dest0.authority));
  }};
  ZmThread server1{[&sockets1, &dest1, &server1OK]() {
    server1OK.store_(serve(sockets1, "/pool1", dest1.authority));
  }};

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start multi-pool multiplexer");
  auto config = Zhttp::Config()
    .links(1).concurrency(1).linkMax(1).secure(false)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(false).quic(false);
  App app;
  app.expected = 2;
  bool inited = app.init(
    Zhttp::HubConfig{&mx, "3", "4"}, 2, config,
    Zhttp::TCPConfig{}, Zhttp::H2Config{}, Zhttp::QUICConfig{});
  inited = inited && app.pool(0, dest0) && app.pool(1, dest1);
  ZuCHECK(inited && app.start(), "initialize/start two destination pools");

  auto request0 = app.request();
  auto request1 = app.request();
  request0->target = "/pool0";
  request1->target = "/pool1";
  request1->key_ = request0->key_;
  app.send(0, request0);
  app.send(1, request1);
  app.seal(0);
  app.seal(1);
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "requests with equal keys complete in separate pools");
  ZuCHECK(app.idleDone.timedwait(Zm::now(10)) == 0,
    "aggregate idle fires after both sealed pools drain");
  app.stop();
  sockets0.stop();
  sockets1.stop();
  server0.join();
  server1.join();

  ZuCHECK(server0OK.load_() && server1OK.load_(),
    "each pool supplies its own target and Host authority");
  bool pool0 = false;
  bool pool1 = false;
  for (unsigned i = 0, n = app.results.length(); i < n; ++i) {
    pool0 |= app.results[i].ok() && app.results[i].pool == 0;
    pool1 |= app.results[i].ok() && app.results[i].pool == 1;
  }
  ZuCHECK(app.results.length() == 2 && pool0 && pool1,
    "terminal results retain their independent pool slots");
  ZuCHECK(app.completed() == 2 && !app.failed() && !app.active() &&
      app.completed(0) == 1 && app.completed(1) == 1 &&
      !app.failed(0) && !app.failed(1) &&
      !app.active(0) && !app.active(1),
    "aggregate and per-pool counters retire independently");
  ZuCHECK(app.idleCount == 1,
    "the client emits one aggregate idle notification");

  app.final();
  mx.stop();
}

void sameOriginPools()
{
  ZuTestScope(sameOriginPools);

  uint16_t port;
  int fd = listener(port);
  ZuCHECK(fd >= 0 && port, "create shared-origin listener");
  if (fd < 0) return;
  ZmAtomic<unsigned> serverOK = 0;
  ServerSockets sockets{fd};
  ZmThread server{[&sockets, &serverOK]() {
    serverOK.store_(serveTwo(sockets));
  }};

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start same-origin multiplexer");
  auto defaults = Zhttp::Config()
    .links(2).concurrency(2).linkMax(2).secure(false)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(false).quic(false);
  App app;
  app.expected = 2;
  bool inited = app.init(
    Zhttp::HubConfig{&mx, "3", "4"}, 2, defaults,
    Zhttp::TCPConfig{}, Zhttp::H2Config{}, Zhttp::QUICConfig{});
  Zhttp::Destination dest{ZuBSpan{"127.0.0.1"}, port};
  auto override = Zhttp::Config().links(1).concurrency(1)
    .linkMax(1);
  inited = inited && app.pool(0, dest) && app.pool(1, dest, override);
  ZuCHECK(inited && app.start(),
    "configure independent policies for two same-origin pools");

  auto request0 = app.request();
  auto request1 = app.request();
  request1->key_ = request0->key_;
  app.send(0, request0);
  app.send(1, request1);
  app.seal(0);
  app.seal(1);
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "same-origin requests with equal keys complete independently");
  app.stop();
  sockets.stop();
  server.join();

  ZuCHECK(serverOK.load_(),
    "same-origin pools own two independent native connections");
  ZuCHECK(app.config(0)->links() == 2 && app.config(1)->links() == 1,
    "same-origin pool override does not mutate client defaults");

  app.final();
  mx.stop();
}

void poolTimeoutOverride()
{
  ZuTestScope(poolTimeoutOverride);

  uint16_t stalledPort, responsivePort;
  int stalledFD = listener(stalledPort);
  int responsiveFD = listener(responsivePort);
  ZuCHECK(stalledFD >= 0 && responsiveFD >= 0 &&
      stalledPort && responsivePort,
    "create stalled and responsive pool listeners");
  if (stalledFD < 0 || responsiveFD < 0) {
    if (stalledFD >= 0) ::close(stalledFD);
    if (responsiveFD >= 0) ::close(responsiveFD);
    return;
  }

  ZmAtomic<unsigned> serverOK = 0;
  ServerSockets responsive{responsiveFD};
  ZmThread server{[&responsive, &serverOK]() {
    serverOK.store_(serve(responsive));
  }};

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start policy-override multiplexer");
  auto defaults = Zhttp::Config()
    .links(1).concurrency(1).linkMax(1).requestTimeout(1)
    .secure(false).protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(false).quic(false);
  App app;
  app.expected = 2;
  bool inited = app.init(
    Zhttp::HubConfig{&mx, "3", "4"}, 2, defaults,
    Zhttp::TCPConfig{}, Zhttp::H2Config{}, Zhttp::QUICConfig{});
  inited = inited && app.pool(
    0, Zhttp::Destination{ZuBSpan{"127.0.0.1"}, stalledPort});
  inited = inited && app.pool(
    1, Zhttp::Destination{ZuBSpan{"127.0.0.1"}, responsivePort},
    Zhttp::Config{}.requestTimeout(0));
  ZuCHECK(inited && app.start(),
    "start pools with isolated timeout policies");

  app.send(0, app.request());
  app.send(1, app.request());
  app.seal(0);
  app.seal(1);
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "default timeout and zero-timeout override both terminate");
  app.stop();
  responsive.stop();
  server.join();
  ::close(stalledFD);

  const Zhttp::Result *stalled = nullptr;
  const Zhttp::Result *completed = nullptr;
  for (const auto &result: app.results) {
    if (result.pool == 0) stalled = &result;
    if (result.pool == 1) completed = &result;
  }
  ZuCHECK(serverOK.load_(),
    "the zero-timeout override pool reaches its destination");
  ZuCHECK(stalled && stalled->code == Zhttp::ResultCode::TimedOut &&
      completed && completed->ok(),
    "the timeout override changes behavior only in its pool");
  ZuCHECK(app.completed(0) == 1 && app.failed(0) == 1 &&
      app.completed(1) == 1 && !app.failed(1),
    "per-pool counters preserve the isolated policy outcome");

  app.final();
  mx.stop();
}

void poolRetryOverride()
{
  ZuTestScope(poolRetryOverride);

  uint16_t defaultPort, overridePort;
  int defaultFD = listener(defaultPort);
  int overrideFD = listener(overridePort);
  ZuCHECK(defaultFD >= 0 && overrideFD >= 0 && defaultPort && overridePort,
    "reserve two closed retry destinations");
  if (defaultFD < 0 || overrideFD < 0) {
    if (defaultFD >= 0) ::close(defaultFD);
    if (overrideFD >= 0) ::close(overrideFD);
    return;
  }
  ::close(defaultFD);
  ::close(overrideFD);

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start retry-override multiplexer");
  auto defaults = Zhttp::Config()
    .links(1).concurrency(1).linkMax(1).maxRetries(0)
    .secure(false).protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(false).quic(false);
  App app;
  app.expected = 2;
  bool inited = app.init(
    Zhttp::HubConfig{&mx, "3", "4"}, 2, defaults,
    Zhttp::TCPConfig{}, Zhttp::H2Config{}, Zhttp::QUICConfig{});
  inited = inited && app.pool(
    0, Zhttp::Destination{ZuBSpan{"127.0.0.1"}, defaultPort});
  inited = inited && app.pool(
    1, Zhttp::Destination{ZuBSpan{"127.0.0.1"}, overridePort},
    Zhttp::Config{}.maxRetries(2));
  ZuCHECK(inited && app.start(),
    "start pools with isolated retry budgets");

  app.send(0, app.request());
  app.send(1, app.request());
  app.seal(0);
  app.seal(1);
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "both retry policies reach terminal results");
  app.stop();

  const Zhttp::Result *defaulted = nullptr;
  const Zhttp::Result *overridden = nullptr;
  for (const auto &result: app.results) {
    if (result.pool == 0) defaulted = &result;
    if (result.pool == 1) overridden = &result;
  }
  ZuCHECK(defaulted && defaulted->code == Zhttp::ResultCode::Failed &&
      !defaulted->retries &&
      overridden && overridden->code == Zhttp::ResultCode::Failed &&
      overridden->retries == 2,
    "the retry override changes attempts only in its pool");
  ZuCHECK(app.retriedEvents == 2,
    "only the overridden pool emits retry transitions");

  app.final();
  mx.stop();
}

void poolRedirectOverride()
{
  ZuTestScope(poolRedirectOverride);

  uint16_t defaultPort, overridePort;
  int defaultFD = listener(defaultPort);
  int overrideFD = listener(overridePort);
  ZuCHECK(defaultFD >= 0 && overrideFD >= 0 && defaultPort && overridePort,
    "create two redirect policy listeners");
  if (defaultFD < 0 || overrideFD < 0) {
    if (defaultFD >= 0) ::close(defaultFD);
    if (overrideFD >= 0) ::close(overrideFD);
    return;
  }

  ZmAtomic<unsigned> defaultOK = 0;
  ZmAtomic<unsigned> overrideOK = 0;
  ServerSockets defaultSockets{defaultFD};
  ServerSockets overrideSockets{overrideFD};
  ZmThread defaultServer{[&defaultSockets, &defaultOK]() {
    defaultOK.store_(serveRedirect(defaultSockets, false));
  }};
  ZmThread overrideServer{[&overrideSockets, &overrideOK]() {
    overrideOK.store_(serveRedirect(overrideSockets, true));
  }};

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start redirect-override multiplexer");
  auto defaults = Zhttp::Config()
    .links(1).concurrency(1).linkMax(1).maxRedirects(0)
    .secure(false).protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(false).quic(false);
  App app;
  app.expected = 2;
  bool inited = app.init(
    Zhttp::HubConfig{&mx, "3", "4"}, 2, defaults,
    Zhttp::TCPConfig{}, Zhttp::H2Config{}, Zhttp::QUICConfig{});
  inited = inited && app.pool(
    0, Zhttp::Destination{ZuBSpan{"127.0.0.1"}, defaultPort});
  inited = inited && app.pool(
    1, Zhttp::Destination{ZuBSpan{"127.0.0.1"}, overridePort},
    Zhttp::Config{}.maxRedirects(1));
  ZuCHECK(inited && app.start(),
    "start pools with isolated redirect budgets");

  auto defaultRequest = app.request();
  auto overrideRequest = app.request();
  defaultRequest->target = "/start";
  overrideRequest->target = "/start";
  app.send(0, ZuMv(defaultRequest));
  app.send(1, ZuMv(overrideRequest));
  app.seal(0);
  app.seal(1);
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "both redirect policies reach terminal results");
  app.stop();
  defaultSockets.stop();
  overrideSockets.stop();
  defaultServer.join();
  overrideServer.join();

  const Zhttp::Result *defaulted = nullptr;
  const Zhttp::Result *overridden = nullptr;
  for (const auto &result: app.results) {
    if (result.pool == 0) defaulted = &result;
    if (result.pool == 1) overridden = &result;
  }
  ZuCHECK(defaultOK.load_() && overrideOK.load_(),
    "both redirect servers observe their configured exchange");
  ZuCHECK(defaulted &&
      defaulted->code == Zhttp::ResultCode::RedirectLimit &&
      !defaulted->redirects && overridden && overridden->ok() &&
      overridden->redirects == 1,
    "the redirect override changes behavior only in its pool");
  ZuCHECK(app.redirectedEvents == 1,
    "only the overridden pool emits a redirect transition");

  app.final();
  mx.stop();
}

void poolRetainedBodyOverride()
{
  ZuTestScope(poolRetainedBodyOverride);

  uint16_t defaultPort, overridePort;
  int defaultFD = listener(defaultPort);
  int overrideFD = listener(overridePort);
  ZuCHECK(defaultFD >= 0 && overrideFD >= 0 && defaultPort && overridePort,
    "create two response-body limit listeners");
  if (defaultFD < 0 || overrideFD < 0) {
    if (defaultFD >= 0) ::close(defaultFD);
    if (overrideFD >= 0) ::close(overrideFD);
    return;
  }

  ZmAtomic<unsigned> defaultOK = 0;
  ZmAtomic<unsigned> overrideOK = 0;
  ServerSockets defaultSockets{defaultFD};
  ServerSockets overrideSockets{overrideFD};
  ZmThread defaultServer{[&defaultSockets, &defaultOK]() {
    defaultOK.store_(serveBody(defaultSockets));
  }};
  ZmThread overrideServer{[&overrideSockets, &overrideOK]() {
    overrideOK.store_(serveBody(overrideSockets));
  }};

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start retained-body override multiplexer");
  auto defaults = Zhttp::Config()
    .links(1).concurrency(1).linkMax(1).retainedBodyMax(4)
    .secure(false).protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(false).quic(false);
  App app;
  app.expected = 2;
  bool inited = app.init(
    Zhttp::HubConfig{&mx, "3", "4"}, 2, defaults,
    Zhttp::TCPConfig{}, Zhttp::H2Config{}, Zhttp::QUICConfig{});
  inited = inited && app.pool(
    0, Zhttp::Destination{ZuBSpan{"127.0.0.1"}, defaultPort});
  inited = inited && app.pool(
    1, Zhttp::Destination{ZuBSpan{"127.0.0.1"}, overridePort},
    Zhttp::Config{}.retainedBodyMax(5));
  ZuCHECK(inited && app.start(),
    "start pools with isolated retained response limits");

  app.send(0, app.request());
  app.send(1, app.request());
  app.seal(0);
  app.seal(1);
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "both retained-body policies reach terminal results");
  app.stop();
  defaultSockets.stop();
  overrideSockets.stop();
  defaultServer.join();
  overrideServer.join();

  const Zhttp::Result *defaulted = nullptr;
  const Zhttp::Result *overridden = nullptr;
  for (const auto &result: app.results) {
    if (result.pool == 0) defaulted = &result;
    if (result.pool == 1) overridden = &result;
  }
  ZuCHECK(defaultOK.load_() && overrideOK.load_(),
    "both response-body servers send the same five-byte body");
  ZuCHECK(defaulted && !defaulted->ok() &&
      overridden && overridden->ok() &&
      overridden->responseBodyReceived == 5,
    "the retained-body override changes behavior only in its pool");

  app.final();
  mx.stop();
}

void poolRetainedMessageOverride()
{
  ZuTestScope(poolRetainedMessageOverride);

  uint16_t defaultPort, overridePort;
  int defaultFD = listener(defaultPort);
  int overrideFD = listener(overridePort);
  ZuCHECK(defaultFD >= 0 && overrideFD >= 0 && defaultPort && overridePort,
    "create two retained-message limit listeners");
  if (defaultFD < 0 || overrideFD < 0) {
    if (defaultFD >= 0) ::close(defaultFD);
    if (overrideFD >= 0) ::close(overrideFD);
    return;
  }

  static constexpr ZuCSpan Body = "abcde";
  ZmAtomic<unsigned> overrideOK = 0;
  ServerSockets overrideSockets{overrideFD};
  ZmThread overrideServer{[&overrideSockets, &overrideOK]() {
    overrideOK.store_(serveRequestBody(overrideSockets, Body));
  }};

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start retained-message override multiplexer");
  auto defaults = Zhttp::Config()
    .links(1).concurrency(1).linkMax(1)
    .retainedBodyMax(Body.length()).retainedMessageMax(Body.length() - 1)
    .secure(false).protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(false).quic(false);
  App app;
  app.expected = 2;
  bool inited = app.init(
    Zhttp::HubConfig{&mx, "3", "4"}, 2, defaults,
    Zhttp::TCPConfig{}, Zhttp::H2Config{}, Zhttp::QUICConfig{});
  inited = inited && app.pool(
    0, Zhttp::Destination{ZuBSpan{"127.0.0.1"}, defaultPort});
  inited = inited && app.pool(
    1, Zhttp::Destination{ZuBSpan{"127.0.0.1"}, overridePort},
    Zhttp::Config{}.retainedMessageMax(1024));
  ZuCHECK(inited && app.start(),
    "start pools with isolated retained request limits");

  auto defaultRequest = app.request();
  auto overrideRequest = app.request();
  defaultRequest->bodyData = Body;
  overrideRequest->bodyData = Body;
  app.send(0, ZuMv(defaultRequest));
  app.send(1, ZuMv(overrideRequest));
  app.seal(0);
  app.seal(1);
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "both retained-message policies reach terminal results");
  app.stop();
  overrideSockets.stop();
  overrideServer.join();
  ::close(defaultFD);

  const Zhttp::Result *defaulted = nullptr;
  const Zhttp::Result *overridden = nullptr;
  for (const auto &result: app.results) {
    if (result.pool == 0) defaulted = &result;
    if (result.pool == 1) overridden = &result;
  }
  ZuCHECK(overrideOK.load_(),
    "the larger retained-message pool sends its fixed request body");
  ZuCHECK(defaulted && !defaulted->ok() &&
      overridden && overridden->ok() &&
      overridden->requestBodyProduced == Body.length(),
    "the retained-message override changes behavior only in its pool");

  app.final();
  mx.stop();
}

void poolLinkMaxOverride()
{
  ZuTestScope(poolLinkMaxOverride);

  enum { Requests = 3 };
  uint16_t defaultPort, overridePort;
  int defaultFD = listener(defaultPort);
  int overrideFD = listener(overridePort);
  ZuCHECK(defaultFD >= 0 && overrideFD >= 0 && defaultPort && overridePort,
    "create two per-link concurrency listeners");
  if (defaultFD < 0 || overrideFD < 0) {
    if (defaultFD >= 0) ::close(defaultFD);
    if (overrideFD >= 0) ::close(overrideFD);
    return;
  }

  ZmAtomic<unsigned> serverOK = 0;
  ServerSockets overrideSockets{overrideFD};
  ZmThread server{[&overrideSockets, &serverOK]() {
    serverOK.store_(serveParallel(overrideSockets, 1, Requests));
  }};

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "start per-link override multiplexer");
  auto defaults = Zhttp::Config()
    .links(1).concurrency(Requests).linkMax(1)
    .secure(false).protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(false).quic(false);
  App app;
  app.expected = Requests;
  bool inited = app.init(
    Zhttp::HubConfig{&mx, "3", "4"}, 2, defaults,
    Zhttp::TCPConfig{}, Zhttp::H2Config{}, Zhttp::QUICConfig{});
  inited = inited && app.pool(
    0, Zhttp::Destination{ZuBSpan{"127.0.0.1"}, defaultPort});
  inited = inited && app.pool(
    1, Zhttp::Destination{ZuBSpan{"127.0.0.1"}, overridePort},
    Zhttp::Config{}.linkMax(Requests));
  ZuCHECK(inited && app.start(),
    "start pools with distinct per-link concurrency");

  app.seal(0);
  for (unsigned i = 0; i < Requests; ++i)
    app.send(1, app.request());
  app.seal(1);
  ZuCHECK(app.done.timedwait(Zm::now(10)) == 0,
    "the overridden link emits all requests before its first response");
  app.stop();
  overrideSockets.stop();
  server.join();
  ::close(defaultFD);

  bool resultsOK = app.results.length() == Requests;
  for (const auto &result: app.results)
    resultsOK &= result.ok() && result.pool == 1 && result.link == 0;
  ZuCHECK(serverOK.load_(),
    "one connection carries the overridden three-operation pipeline");
  ZuCHECK(resultsOK,
    "the per-link override changes H1 admission only in its pool");

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

  Zhttp::Config config;
  config
    .concurrency(1)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .tcp(true).tls(true).quic(false);

  {
    App app;
    bool inited = startApp(app, mx, 1, config);
    ZuCHECK(inited, "start resolver-owning agent");
    ZuCHECK(ZiResolver::instance()->running(),
      "client starts its owned resolver");
    if (inited) app.stop();
    if (inited) app.final();
    ZuCHECK(!ZiResolver::instance()->initialized(),
      "agent finalizes its resolver");
  }

  ZiResolver::start();
  ZuCHECK(ZiResolver::instance()->running(), "start external resolver");
  {
    App app;
    bool inited = startApp(app, mx, 1, config);
    ZuCHECK(inited, "start resolver-borrowing agent");
    if (inited) {
      ZmSemaphore finalized;
      bool stopped = false;
      app.stop([&app, &mx, &finalized, &stopped](bool ok) {
	// Post behind the stop callback before releasing its owner. Calling a
	// second blocking stop from final() here would deadlock the Rx shard.
	mx.run([&app, &finalized, &stopped, ok]() {
	  stopped = ok;
	  if (ok) app.final();
	  finalized.post();
	}, mx.rxThread());
      });
      finalized.wait();
      ZuCHECK(stopped, "finalize drained client on Rx without blocking");
    }
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
  ZiLog::init(
#ifdef ZHTTP_CLIENT_POOL_TEST
      "ZhttpClientPoolTest"
#else
      "ZhttpClientCancelTest"
#endif
      );
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuTestMain();
#ifdef ZHTTP_CLIENT_POOL_TEST
  ZuTestCall(destination);
  ZuTestCall(configuration);
  ZuTestCall(stopResolving);
  ZuTestCall(multiplePools);
  ZuTestCall(sameOriginPools);
  ZuTestCall(poolTimeoutOverride);
  ZuTestCall(poolRetryOverride);
  ZuTestCall(poolRedirectOverride);
  ZuTestCall(poolRetainedBodyOverride);
  ZuTestCall(poolRetainedMessageOverride);
  ZuTestCall(poolLinkMaxOverride);
  ZuTestCall(pipeline);
  ZuTestCall(pipelineTxError);
  ZuTestCall(pipelineCancellation, 0U);
  ZuTestCall(pipelineCancellation, 1U);
  ZuTestCall(pipelineCancellation, 2U);
  ZuTestCall(parallelism);
  ZuTestCall(waves);
  ZuTestCall(stopQueuedLimited);
  ZuTestCall(stopActivePipeline);
  ZuTestCall(stopReusableIdle);
  ZuTestCall(boundedLinkScan);
  ZuTestCall(limited);
  ZuTestCall(limitedSaturated);
#else
  ZuTestCall(attemptState);
  ZuTestCall(cancel);
  ZuTestCall(outOfOrder);
  ZuTestCall(timeout);
  ZuTestCall(retry);
  ZuTestCall(redirect);
  ZuTestCall(redirectUnsafe);
  ZuTestCall(crossOriginRedirect);
  ZuTestCall(retryLimit);
  ZuTestCall(resolverLifecycle);
#endif
  ZiLog::stop();
  return 0;
}
