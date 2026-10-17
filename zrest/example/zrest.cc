//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// OAuth native client and REST ping workload

#include <iostream>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <time.h>
#include <unistd.h>

#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZuPercent.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmPQueue.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtPlatform.hh>

#include <zlib/ZfCLI.hh>

#include <zlib/ZiHashTune.hh>
#include <zlib/ZiHeapTune.hh>
#include <zlib/ZiLog.hh>

#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpServer.hh>
#include <zlib/ZrestClient.hh>
#include <zlib/ZrestServer.hh>

#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsRandom.hh>

#include "zrest.hh"

ZtEnumNS(, Http3Mode, int8_t, force, prefer, disable);
ZtEnumNS(, Http2Mode, int8_t, force, prefer, disable);
ZtEnumImplNS(Http3Mode);
ZtEnumImplNS(Http2Mode);

enum {
  ClientTimeout = 15,
  CallbackTimeout = 30,
  H3StallTimeout = 15,
  H3QuietTimeout = 2,
  MaxRedirects = 8,
  RespBodyMax = 1U<<20
};

struct CallbackFieldIDs {
  using Keys = ZuStringTL<"code", "state", "iss", "error">;
};

struct Options {
  ZuCSpan	ca;
  ZuCSpan	issuerURL{"https://localhost:8443/oauth2/ping"};
  ZuCSpan	clientID{"zrest-native"};
  ZuCSpan	scope{"ping"};
  ZuCSpan	resource{"https://localhost:8443/api/ping"};
  ZuCSpan	browser{"xdg-open"};
  unsigned	callbackTimeout = CallbackTimeout;
  unsigned	requests = 1;
  unsigned	concurrency = 1;
  unsigned	links = 1;
  unsigned	linkMax = 1;
  unsigned	maxPending = Zrest::Default::MaxPending;
  unsigned	retries = 0;
  unsigned	timeout = ClientTimeout;
  unsigned	stallTimeout = H3StallTimeout;
  unsigned	quietTimeout = H3QuietTimeout;
  double	interval = 0;
  ZuCSpan	keyLog;
  Http3Mode::T	http3 = Http3Mode::prefer;
  Http2Mode::T	http2 = Http2Mode::prefer;
  uint32_t	quicHeartbeat = 0;
  bool		devHTTP = false;
  bool		verbose = false;
#ifdef ZiMultiplex_DEBUG
  bool		debug = false;
  bool		frag = false;
  bool		yield = false;
#endif
#ifdef ZiMultiplex_FILTER
  ZuCSpan	quicRxDrop;
  ZuCSpan	quicTxDrop;
#endif
#ifdef Zquic_DEBUG
  uint32_t	quicDiag = 0;
#endif
  uint32_t	memDiag = 0;
  bool		help = false;
};

ZfStruct(, (Options, CLI),
  (ca,        (CLI::Opt<'c'>,  CLI::Long<"ca">),		String),
  (issuerURL, (CLI::Long<"issuer">),				String),
  (clientID,  (CLI::Long<"client-id">),				String),
  (scope,     (CLI::Long<"scope">),				String),
  (resource,  (CLI::Long<"resource">),				String),
  (browser,   (CLI::Long<"browser">),				String),
  (callbackTimeout,
    (CLI::Long<"callback-timeout">),				UInt32),
  (requests,  (CLI::Opt<'n'>,  CLI::Long<"requests">),		UInt32),
  (concurrency, (CLI::Opt<'j'>, CLI::Long<"jobs">),		UInt32),
  (links,      (CLI::Long<"links">),				UInt32),
  (linkMax,   (CLI::Long<"link-max">),				UInt32),
  (maxPending, (CLI::Long<"max-pending">),			UInt32),
  (retries,   (CLI::Long<"retries">),				UInt32),
  (timeout,   (CLI::Long<"timeout">),				UInt32),
  (stallTimeout, (CLI::Long<"stall-timeout">),			UInt32),
  (quietTimeout, (CLI::Long<"quiet-timeout">),			UInt32),
  (interval,  (CLI::Opt<'i'>, CLI::Long<"interval">),		Float),
  (keyLog,    (CLI::Long<"key-log">),				String),
  (http3,     (Enum<Http3Mode::Map>,
		  CLI::Opt<'3'>, CLI::Long<"http3">),		Int8),
  (http2,     (Enum<Http2Mode::Map>,
		  CLI::Opt<'2'>, CLI::Long<"http2">),		Int8),
  (quicHeartbeat,
    (CLI::Long<"quic-heartbeat">),				UInt32),
  (devHTTP,   (CLI::Long<"dev-http">),				Bool),
  (verbose,   (CLI::Flag<'v'>, CLI::Long<"verbose">),		Bool),
#ifdef ZiMultiplex_DEBUG
  (debug,     (CLI::Long<"debug">),				Bool),
  (frag,      (CLI::Long<"frag">),				Bool),
  (yield,     (CLI::Long<"yield">),				Bool),
#endif
#ifdef ZiMultiplex_FILTER
  (quicRxDrop, (CLI::Long<"quic-rx-drop">),			String),
  (quicTxDrop, (CLI::Long<"quic-tx-drop">),			String),
#endif
#ifdef Zquic_DEBUG
  (quicDiag,   (CLI::Long<"quic-diag">),			UInt32),
#endif
  (memDiag,    (CLI::Long<"mem-diag">),				UInt32),
  (help,       (CLI::Flag<'h'>, CLI::Long<"help">),		Bool));

struct Result : public ZmNHeap<Result>, public ZmObject {
friend inline ZuStringT<"zrest.Result"> ZmNHeapID(Result *);

  ZmSemaphore	done;
  OAuthSecret	body;
  OAuthString	contentType;
  OAuthString	cacheControl;
  OAuthString	pragma;
  unsigned	status = 0;
  unsigned	contentTypeCount = 0;
  unsigned	cacheControlCount = 0;
  unsigned	pragmaCount = 0;
};

struct Call : public ZmNHeap<Call>, public ZmObject {
friend inline ZuStringT<"zrest.Call"> ZmNHeapID(Call *);

  ZmRef<Result>	result;
  OAuthString	target;
  OAuthSecret	body;
  OAuthSecret	authorization;

  template <typename Response>
  void process(Zrest::Reply<Response> reply) const {
    const auto &response = reply.bodyObject();
    result->status = Response::Status;
    result->body = response.data;
    result->contentType = response.contentType;
    result->cacheControl = response.cacheControl;
    result->pragma = response.pragma;
    result->contentTypeCount = response.contentTypeCount;
    result->cacheControlCount = response.cacheControlCount;
    result->pragmaCount = response.pragmaCount;
    result->done.post();
  }
  void failed() const {
    result->done.post();
  }
};

struct ResponseData : public ZmNHeap<ResponseData>, public ZmObject {
friend inline ZuStringT<"zrest.ResponseData"> ZmNHeapID(ResponseData *);

  OAuthString data;
  OAuthString contentType;
  OAuthString cacheControl;
  OAuthString pragma;
  unsigned contentTypeCount = 0;
  unsigned cacheControlCount = 0;
  unsigned pragmaCount = 0;

  ResponseData &operator =(ZuSpan<uint8_t> data_) {
    data = data_;
    return *this;
  }
};

template <typename Impl, unsigned Status_>
struct ResponseParser : public Zrest::ResParser<Impl, ResponseData> {
  using Base = Zrest::ResParser<Impl, ResponseData>;
  using Base::header;
  using Headers = ZhttpHeaders("content-type", "cache-control", "pragma");
  enum { Status = Status_, Body = Zrest::BodyPolicy::Raw };

  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "content-type") {
      ++this->object->contentTypeCount;
      if (value.length() <= 128) this->object->contentType = value;
    } else if constexpr (Key{}() == "cache-control") {
      ++this->object->cacheControlCount;
      if (value.length() <= 128) this->object->cacheControl = value;
    } else if constexpr (Key{}() == "pragma") {
      ++this->object->pragmaCount;
      if (value.length() <= 128) this->object->pragma = value;
    }
  }
};

struct Response200 : public ResponseParser<Response200, 200> { };
struct Response400 : public ResponseParser<Response400, 400> { };
struct Response401 : public ResponseParser<Response401, 401> { };
struct Response403 : public ResponseParser<Response403, 403> { };
struct Response404 : public ResponseParser<Response404, 404> { };
struct Response500 : public ResponseParser<Response500, 500> { };
struct Response503 : public ResponseParser<Response503, 503> { };
using Responses = ZuTypeList<Response200, Response400, Response401,
  Response403, Response404, Response500, Response503>;

template <typename Impl, Zhttp::Method::T Method_>
struct DynamicBuilder : public Zrest::ReqBuilder<Impl, Call> {
  using Path = ZuStringT<"/">;
  using Responses = ::Responses;
  enum { Method = Method_, Exact = 1 };

  template <typename Emit> void operation(Emit &&emit) const {
    emit(Method, [this](auto &&emitTarget) {
      emitTarget([this](auto &s) { s << this->object->target; });
    });
  }
};

struct MetadataBuilder :
    public DynamicBuilder<MetadataBuilder, Zhttp::Method::GET> { };

struct FormBuilder :
    public DynamicBuilder<FormBuilder, Zhttp::Method::POST> {
  using Base = DynamicBuilder<FormBuilder, Zhttp::Method::POST>;
  using Base::header;
  using Headers = ZhttpHeaders(
    ("content-type", "application/x-www-form-urlencoded"), "content-length");
  enum { Body = Zrest::BodyPolicy::Raw };
  const OAuthSecret &bodyObject(const Call *call) const { return call->body; }
};

struct PingBuilder : public DynamicBuilder<PingBuilder, Zhttp::Method::GET> {
  using Base = DynamicBuilder<PingBuilder, Zhttp::Method::GET>;
  using Base::header;
  using Path = PingPath;
  using Headers = ZhttpHeaders("authorization");

  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "authorization") l(this->object->authorization);
    else Base::template header<Key>(ZuFwd<L>(l));
  }
};

using Requests = ZuTypeList<MetadataBuilder, FormBuilder, PingBuilder>;
ZrestCatalogDerive(Catalog, Requests);
ZrestCatalogImpl(Catalog)

struct ClientApp { };
class Client : public Zrest::Client<ClientApp, Catalog, Zhttp::Profiles<Zhttp::H1TCP,
  Zhttp::H1TLS, Zhttp::H2TLS, Zhttp::H3QUIC>> {
  using Base = Zrest::Client<ClientApp, Catalog, Zhttp::Profiles<Zhttp::H1TCP, Zhttp::H1TLS,
    Zhttp::H2TLS, Zhttp::H3QUIC>>;
public:
  template <typename Builder>
  ZmRef<Result> submit(unsigned pool, OAuthString target,
      OAuthSecret body = {}, OAuthSecret authorization = {}) {
    ZmRef<Result> result = new Result{};
    ZmRef<Call> call = new Call{};
    call->result = result;
    call->target = ZuMv(target);
    call->body = ZuMv(body);
    call->authorization = ZuMv(authorization);
    if (!Base::template call<Builder>(pool, call, call)) call->failed();
    return result;
  }

  template <typename Builder>
  ZmRef<Result> perform(unsigned pool, OAuthString target,
      OAuthSecret body = {}, OAuthSecret authorization = {}) {
    auto result = submit<Builder>(pool, ZuMv(target), ZuMv(body),
      ZuMv(authorization));
    result->done.wait();
    return result;
  }

};

template <typename Fn>
static bool formEach(ZuCSpan form, Fn &&fn)
{
  while (form) {
    int amp = form.find([](char c) { return c == '&'; });
    ZuCSpan item = amp >= 0 ?
      ZuCSpan{form.data(), unsigned(amp)} : form;
    int eq = item.find([](char c) { return c == '='; });
    if (eq < 0) return false;
    OAuthString name{item.data(), unsigned(eq)};
    OAuthString value{item.data() + unsigned(eq) + 1,
      item.length() - unsigned(eq) - 1};
    using Form = ZuPercent::Codec<ZfURI::PercentQuote<true>>;
    auto key = Form::decode(name.span());
    auto data = Form::decode(value.span());
    if (!key || !data) return false;
    name.length(key.out);
    value.length(data.out);
    if (!fn(name, value)) return false;
    if (amp < 0) break;
    form.offset(unsigned(amp) + 1);
  }
  return true;
}

struct CallbackData : public ZmNHeap<CallbackData>, public ZmObject {
friend inline ZuStringT<"zrest.CallbackData"> ZmNHeapID(CallbackData *);

  OAuthString data;
  CallbackData &operator =(ZuSpan<uint8_t> data_) { data = data_; return *this; }
};

struct CallbackBody : public ZmNHeap<CallbackBody>, public ZmObject {
friend inline ZuStringT<"zrest.CallbackBody"> ZmNHeapID(CallbackBody *);
 OAuthString data; };

template <typename Impl, unsigned Status_>
struct CallbackResponse : public Zrest::ResBuilder<Impl, CallbackBody> {
  using Headers = ZhttpHeaders(
    ("content-type", "text/html; charset=utf-8"), "content-length");
  enum { Status = Status_, Body = Zrest::BodyPolicy::Raw };
  const OAuthString &bodyObject(const CallbackBody *body) const {
    return body->data;
  }
};
struct CallbackOK : public CallbackResponse<CallbackOK, 200> { };
struct CallbackBad : public CallbackResponse<CallbackBad, 400> { };
struct CallbackGone : public CallbackResponse<CallbackGone, 410> { };

class CallbackApp;
struct CallbackReq : public Zrest::ReqParser<CallbackReq, CallbackData> {
  using Path = ZuStringT<"/callback">;
  using Responses = ZuTypeList<CallbackOK, CallbackBad, CallbackGone>;
  enum { Exact = 1, Query = Zrest::QueryPolicy::Raw };
  static constexpr uint64_t QueryLimit = OAuthURLMax;
};

using CallbackRequests = ZuTypeList<CallbackReq>;
ZrestCatalogDerive(CallbackCatalog, CallbackRequests);
ZrestCatalogImpl(CallbackCatalog)
using CallbackDecoder = Zrest::MReqParser<CallbackCatalog>;
using CallbackServer = Zrest::Server<CallbackApp, CallbackCatalog,
  CallbackDecoder, Zhttp::Profiles<Zhttp::H1TCP>>;

class CallbackApp {
public:
  using Token = Zrest::Completion<CallbackReq, CallbackServer>;

  void request(CallbackReq *, const CallbackReq &request,
      const Zrest::RequestContext &context, ZmRef<Token> token) {
    bool ok = context.ok();
    OAuthSecret code_;
    OAuthString state_, issuerURL_, error_;
    unsigned seen = 0;
    bool duplicate = false;
    if (ok && request.object && !received) {
      auto query = ZuCSpan{request.object->data};
      if (query && query[0] == '?') query.offset(1);
      ok = formEach(query, [&seen, &duplicate, &code_, &state_,
	  &issuerURL_, &error_](ZuCSpan name, ZuCSpan value) {
        static constexpr auto matcher = ZuMatcher<CallbackFieldIDs>();
        unsigned bit;
        switch (matcher.exact(name)) {
          case 0: bit = 1; code_ = value; break;
          case 1: bit = 2; state_ = value; break;
          case 2: bit = 4; issuerURL_ = value; break;
          case 3: bit = 8; error_ = value; break;
          default: return false;
        }
        duplicate |= bool(seen & bit);
        seen |= bit;
        return true;
      });
      ok = ok && !duplicate &&
        ((seen == 7 && code_) || (seen == 14 && error_));
    } else ok = false;
    bool gone = received;
    if (ok) {
      code = ZuMv(code_);
      state = ZuMv(state_);
      issuerURL = ZuMv(issuerURL_);
      error = ZuMv(error_);
      received = true;
    }
    ZmRef<CallbackBody> body = new CallbackBody{};
    body->data = ok && code ?
      "<!doctype html><h1>Authorized</h1><p>You may close this window.</p>" :
      "<!doctype html><h1>Authorization failed</h1>";
    if (gone)
      token->complete(Zrest::Reply<CallbackGone>{body});
    else if (ok)
      token->complete(Zrest::Reply<CallbackOK>{body});
    else
      token->complete(Zrest::Reply<CallbackBad>{body});
    if (ok) done.post();
  }

  void listening(int, unsigned port_) { port = port_; ready.post(); }
  void listenFailed(int, bool) { ready.post(); done.post(); }
  void connected(int) { }
  void disconnected(int) { }

  ZmSemaphore	ready;
  ZmSemaphore	done;
  OAuthSecret	code;
  OAuthString	state;
  OAuthString	issuerURL;
  OAuthString	error;
  unsigned	port = 0;
  bool		received = false;
};

struct Tokens {
  OAuthSecret access;
  OAuthSecret refresh;
  int64_t deadline = 0;

  void clear() { access.null(); refresh.null(); deadline = 0; }
};

static bool jsonHeaders(const Result &result, bool noStore)
{
  auto type = ZuCSpan{result.contentType};
  int semi = type.find([](char c) { return c == ';'; });
  if (semi >= 0) type.trunc(unsigned(semi));
  return result.contentTypeCount == 1 && type == "application/json" &&
    (!noStore || (result.cacheControlCount == 1 &&
      result.cacheControl == "no-store"));
}

template <typename Object>
static bool loadJSON(Result &result, Object &object, bool noStore = true)
{
  if (!jsonHeaders(result, noStore) || !result.body ||
      result.body.length() > RespBodyMax) return false;
  auto scan = ZfJSON::scan(result.body.span());
  if (scan.p<0>() != int(result.body.length()) || !scan.p<1>() ||
      !scan.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = scan.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1) return false;
  auto handler = ZfJSON::handler<Object>(roots[0]);
  if (!handler.valid) return false;
  try { handler.load(object); }
  catch (...) { return false; }
  return true;
}

static bool contains(const OAuthStringVec &values, ZuCSpan expected)
{
  for (const auto &value: values) if (value == expected) return true;
  return false;
}

static bool grantTypesSupported(const OAuthStringVec &values)
{
  uint8_t lookup[2]{};
  for (const auto &value: values) {
    if (value.length() == 18) {
      if (value == "authorization_code") lookup[0] = 1;
    } else if (value.length() == 13) {
      if (value == "refresh_token") lookup[1] = 1;
    }
    if (lookup[0] && lookup[1]) return true;
  }
  return false;
}

static bool loopback(const Zhttp::URLView &url)
{
  return url.host == "127.0.0.1" || url.host == "::1";
}

static bool validURL(const Zhttp::URLView &url, bool devHTTP)
{
  return url.ok() && url.host && !url.hasQuery && !url.hasFragment &&
    (url.scheme == Zhttp::Scheme::https ||
      (devHTTP && url.scheme == Zhttp::Scheme::http && loopback(url)));
}

static bool sameOrigin(const Zhttp::URLView &l, const Zhttp::URLView &r)
{
  return l.scheme == r.scheme && l.host == r.host && l.port == r.port &&
    l.ipv6Literal == r.ipv6Literal;
}

static bool endpointTarget(ZuCSpan value, const Zhttp::URLView &issuerURL,
    ZuCSpan expected, ZuCSpan appID, OAuthString &target)
{
  Zhttp::URL storage;
  if (!storage.assign(value).ok()) return false;
  auto url = storage.url();
  if (!validURL(url, issuerURL.scheme == Zhttp::Scheme::http) ||
      !sameOrigin(url, issuerURL) || !url.path) return false;
  target = url.path;
  OAuthEndpointPath endpoint;
  if (!ZfURI::loadPath(endpoint, target) ||
      endpoint.oauth2 != "oauth2" ||
      endpoint.app != appID || endpoint.version != "v1" ||
      endpoint.endpoint != expected) return false;
  return true;
}

template <typename S = OAuthString>
static S encode(ZuBSpan data)
{
  S value;
  value.length(ZuBase64URL::enclen(data.length()));
  value.length(ZuBase64URL::encode(value.span(), data));
  return value;
}

static uint64_t nowNS()
{
  auto now = Zm::now();
  return uint64_t(now.sec()) * 1000000000ULL + now.nsec();
}

static pid_t openBrowser(ZuCSpan browser, ZuCSpan ca, ZuCSpan url)
{
  pid_t pid = ::fork();
  if (pid) return pid;
  OAuthString browser_{browser}, ca_{ca}, url_{url};
  if (ca_) ::setenv("ZREST_CA", ca_.data(), 1);
  if (browser_.find([](char c) { return c == '/'; }) >= 0)
    ::execl(browser_.data(), browser_.data(), url_.data(),
      static_cast<char *>(nullptr));
  else
    ::execlp(browser_.data(), browser_.data(), url_.data(),
      static_cast<char *>(nullptr));
  ::_exit(127);
}

static bool tokenResponse(Result &result, ZuCSpan scope,
    Tokens &tokens, bool refresh)
{
  OAuthTokenRes response;
  if (result.status != 200 || !loadJSON(result, response) ||
      !response.accessToken || response.accessToken.length() > OAuthTokenMax ||
      response.tokenType != "Bearer" || !response.expiresIn ||
      response.scope != scope ||
      (!refresh && !response.refreshToken) ||
      response.refreshToken.length() > OAuthTokenMax) return false;
  int64_t now = Zm::now().sec();
  if (now <= 0 || response.expiresIn > uint64_t(INT64_MAX - now)) return false;
  OAuthSecret nextRefresh = response.refreshToken ?
    ZuMv(response.refreshToken) : OAuthSecret{tokens.refresh};
  tokens.access = ZuMv(response.accessToken);
  tokens.refresh = ZuMv(nextRefresh);
  tokens.deadline = now + int64_t(response.expiresIn);
  return true;
}

static bool pong(Result &result)
{
  Pong response;
  return result.status == 200 && loadJSON(result, response, false) &&
    response.pong;
}

static OAuthSecret bearer(const Tokens &tokens)
{
  OAuthSecret value{"Bearer "};
  value << tokens.access;
  return value;
}

static bool refresh(Client &client, ZuCSpan target, ZuCSpan clientID,
    ZuCSpan scope, Tokens &tokens, bool verbose)
{
  OAuthRefreshTokenReq request;
  request.grantType = "refresh_token";
  request.refreshToken = tokens.refresh;
  request.clientID = clientID;
  OAuthSecret form;
  ZfURI::saveBody(form, request);
  auto result = client.perform<FormBuilder>(0, OAuthString{target}, ZuMv(form));
  if (!tokenResponse(*result, scope, tokens, true)) return false;
  if (verbose) ZiLOG(Info, "zrest", ([ns = nowNS()](auto &s) {
    s << "event=refresh time_ns=" << ns;
  }));
  return true;
}

static bool sleepFor(double seconds)
{
  if (seconds <= 0) return true;
  double whole;
  double fraction = ::modf(seconds, &whole);
  timespec delay{time_t(whole), long(fraction * 1000000000.0)};
  while (::nanosleep(&delay, &delay))
    if (errno != EINTR) return false;
  return true;
}

static unsigned callbackPort()
{
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return 0;
  sockaddr_in address{
    .sin_family = AF_INET,
    .sin_port = 0,
    .sin_addr = {.s_addr = htonl(INADDR_LOOPBACK)}
  };
  unsigned port = 0;
  if (!::bind(fd, reinterpret_cast<const sockaddr *>(&address),
      sizeof(address))) {
    socklen_t length = sizeof(address);
    if (!::getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length))
      port = ntohs(address.sin_port);
  }
  ::close(fd);
  return port;
}

static ZiMxParams mxParams(const Options &options)
{
  auto params = ZiMxParams().scheduler([](auto &s) {
    s.nThreads(4)
      .thread(1, [](auto &t) { t.name("rx"); t.isolated(1); })
      .thread(2, [](auto &t) { t.name("tx"); t.isolated(1); })
      .thread(3, [](auto &t) { t.name("cb-rx"); t.isolated(1); })
      .thread(4, [](auto &t) { t.name("cb-tx"); t.isolated(1); });
  }).rxThread(1).txThread(2);
#ifdef ZiMultiplex_DEBUG
  if (options.debug) params.debug(true);
  if (options.frag) params.frag(true);
  if (options.yield) params.yield(true);
#else
  (void)options;
#endif
  return params;
}

static void usage(int code = 1)
{
  std::cerr <<
    "Usage: zrest [OPTION]...\n\n"
    "  --issuer=URL        exact OAuth issuer URL\n"
    "  --client-id=ID      public client ID, default zrest-native\n"
    "  --scope=SCOPE       requested scope, default ping\n"
    "  --resource=URL      exact protected resource URL\n"
    "  --browser=COMMAND   external user-agent launcher\n"
    "  --callback-timeout=N  callback timeout in seconds\n"
    "  -c, --ca=PATH       CA path for HTTPS\n"
    "  --dev-http          permit HTTP loopback development URLs\n"
    "  -n, --requests=N    submit N ping requests\n"
    "  -j, --jobs=N        concurrent requests per wave\n"
    "  -i, --interval=N    seconds between waves, fractions accepted\n"
    "  --links=N           persistent links per destination\n"
    "  --link-max=N        per-link pipeline/stream limit\n"
  "  --max-pending=N     maximum admitted REST calls\n"
    "  --retries=N         transient connection retries\n"
    "  --timeout=N         HTTP completion timeout\n"
    "  -3, --http3=MODE    force, prefer, disable\n"
    "  -2, --http2=MODE    force, prefer, disable\n"
    "  -v, --verbose       log OAuth and ping events\n"
    "  -h, --help          show help\n" << std::flush;
  ::exit(code);
}

static volatile sig_atomic_t interrupted;
static void interrupt() { interrupted = 1; }

int main(int argc, char **argv)
{
  ZiHeapTune::load();
  ZiHashTune::load();

  Options options;
  try { argc = ZfCLI::load(options, argc, argv); }
  catch (const ZeException &e) { std::cerr << e << '\n'; usage(); }
  if (options.help) usage(0);
  if (argc != 1 || !options.issuerURL || !options.clientID || !options.scope ||
      !options.resource || !options.browser || !options.callbackTimeout ||
      !options.requests || !options.concurrency || !options.links ||
      !options.linkMax || !options.maxPending || !options.timeout ||
      !::isfinite(options.interval) ||
      options.interval < 0 || options.http3 < 0 ||
      options.http3 >= Http3Mode::N || options.http2 < 0 ||
      options.http2 >= Http2Mode::N) usage();

  Zhttp::URL issuerStorage, resourceStorage;
  if (!issuerStorage.assign(options.issuerURL).ok() ||
      !resourceStorage.assign(options.resource).ok()) usage();
  auto issuerURL = issuerStorage.url();
  auto resourceURL = resourceStorage.url();
  if (!validURL(issuerURL, options.devHTTP) ||
      !validURL(resourceURL, options.devHTTP) || !issuerURL.path ||
      !resourceURL.path || issuerURL.scheme != resourceURL.scheme) usage();

  OAuthString issuerPath{issuerURL.path};
  OAuthIssuerPath issuer;
  if (!ZfURI::loadPath(issuer, issuerPath) ||
      issuer.oauth2 != "oauth2" ||
      !issuer.app || issuer.app.length() > OAuthAppIDMax) usage();

  OAuthMetadataPath metadataPath;
  metadataPath.app = issuer.app;
  OAuthString metadataTarget;
  ZfURI::savePath(metadataTarget, metadataPath);

  OAuthString resourceTarget{resourceURL.path};
  if (resourceURL.hasQuery) resourceTarget << '?' << resourceURL.query;
  Ping pingRequest;
  pingRequest.ping = true;
  ZfURI::save(resourceTarget, pingRequest);

  ZiLog::init("zrest");
  ZiLog::level(options.verbose ? Ze::Info : Ze::Warning);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZmTrap::sigintFn(interrupt);
  ZmTrap::trap();

  ZiMultiplex mx{mxParams(options)};
  if (!mx.start()) return 1;

  Zhttp::ProtoPolicy::T policy;
  switch (options.http3) {
    case Http3Mode::force: policy = Zhttp::ProtoPolicy::ForceH3; break;
    case Http3Mode::disable: policy = Zhttp::ProtoPolicy::DisableH3; break;
    default: policy = Zhttp::ProtoPolicy::PreferH3; break;
  }
  Zhttp::H2Policy::T h2Policy;
  switch (options.http2) {
    case Http2Mode::force: h2Policy = Zhttp::H2Policy::Force; break;
    case Http2Mode::disable: h2Policy = Zhttp::H2Policy::Disable; break;
    default: h2Policy = Zhttp::H2Policy::Prefer; break;
  }
  bool secure = issuerURL.scheme == Zhttp::Scheme::https;
  Zrest::ClientConfig config;
  config.links(options.links)
    .concurrency(options.concurrency).linkMax(options.linkMax)
    .requestTimeout(options.timeout).maxRedirects(MaxRedirects)
    .maxRetries(options.retries).bodyMax(RespBodyMax).retainedBodyMax(RespBodyMax)
    .protocol(policy).h2Policy(h2Policy)
    .blindH3(policy == Zhttp::ProtoPolicy::ForceH3)
    .secure(secure).tcp(true)
    .tls(secure && policy != Zhttp::ProtoPolicy::ForceH3)
    .quic(secure && policy != Zhttp::ProtoPolicy::DisableH3);
  Zrest::Limits limits;
  limits.maxPending = options.maxPending;
  config.limits(limits);
  auto h2 = Zhttp::H2Config{}.caPath(options.ca).policy(h2Policy);
  auto quic = Zhttp::QUICConfig{}.caPath(options.ca)
    .keyLogPath(options.keyLog).heartbeat(options.quicHeartbeat ?
      ZuTime{options.quicHeartbeat} : ZuTime{});

  Client client;
  ClientApp clientApp;
  bool clientInited = client.init(
    Zhttp::HubConfig{&mx, "rx", "tx"}, 2, ZuMv(config), &clientApp,
    Zhttp::TCPConfig{}, h2, quic);
  bool poolsInited = clientInited && client.pool(0,
    Zhttp::Destination{issuerURL.host, issuerURL.port, issuerURL.ipv6Literal}) &&
    client.pool(1, Zhttp::Destination{
      resourceURL.host, resourceURL.port, resourceURL.ipv6Literal});
  client.txErrorFn(ZiTxErrorFn{[](ZeException &e) {
    ZiLOG(Error, "zrest", ([e](auto &s) { s << "transmit error: " << e; }));
    return false;
  }});
  bool clientUp = poolsInited && client.start();
  if (!clientUp) {
    if (clientInited) client.stop();
    client.final(); mx.stop(); ZiLog::stop(); return 1;
  }

  bool ok = true;
  OAuthString authorizeTarget, tokenTarget, revokeTarget, keysTarget;
  auto metadataResult =
    client.perform<MetadataBuilder>(0, OAuthString{metadataTarget});
  OAuthMetadata metadata;
  ok = metadataResult->status == 200 && loadJSON(*metadataResult, metadata) &&
    metadata.issuerURL == options.issuerURL && contains(metadata.responseTypes, "code") &&
    grantTypesSupported(metadata.grantTypes) &&
    contains(metadata.codeChallengeMethods, "S256") &&
    contains(metadata.scopes, options.scope) &&
    contains(metadata.tokenAuthMethods, "none") &&
    contains(metadata.revokeAuthMethods, "none") &&
    endpointTarget(metadata.authorizationEndpoint, issuerURL, "authorize",
      issuer.app, authorizeTarget) &&
    endpointTarget(metadata.tokenEndpoint, issuerURL, "token",
      issuer.app, tokenTarget) &&
    endpointTarget(metadata.revocationEndpoint, issuerURL, "revoke",
      issuer.app, revokeTarget) &&
    endpointTarget(metadata.jwksURI, issuerURL, "keys", issuer.app, keysTarget);

  Ztls::Random rng;
  ZuBArray<32> random(32, false);
  OAuthSecret verifier, state;
  OAuthString challenge, redirect;
  if (ok) ok = rng.init() && rng.random(random);
  if (ok) verifier = encode<OAuthSecret>(random);
  ZuBArray<Ztls::MD<>::Size> digest(Ztls::MD<>::Size, false);
  if (ok) {
    Ztls::MD<> md;
    md.update(ZuBSpan{verifier});
    md.finish(digest);
    challenge = encode(digest);
    ok = rng.random(random);
  }
  if (ok) state = encode<OAuthSecret>(random);

  CallbackApp callback;
  CallbackServer callbackServer;
  bool callbackInited = false, callbackUp = false;
  pid_t browserPID = -1;
  if (ok) {
    unsigned port = callbackPort();
    Zrest::ServerConfig callbackConfig;
    callbackConfig.localIP(ZiIP("127.0.0.1")).port(port)
      .idleTimeout(options.callbackTimeout).bodyMax(OAuthURLMax)
      .retainedBodyMax(OAuthURLMax).tcp();
    callbackConfig.limits(limits);
    callbackInited = port && callbackServer.init(
      Zhttp::HubConfig{&mx, "cb-rx", "cb-tx"},
      ZuMv(callbackConfig), &callback);
    callbackUp = callbackInited && callbackServer.start();
    ok = callbackUp && !callback.ready.timedwait(Zm::now(5)) && callback.port;
  }
  if (ok) {
    redirect << "http://127.0.0.1:" << callback.port << "/callback";
    OAuthAuthorizeReq request;
    request.responseType = "code";
    request.clientID = options.clientID;
    request.redirectURI = redirect;
    request.scope = options.scope;
    request.state = state;
    request.codeChallenge = challenge;
    request.codeChallengeMethod = "S256";
    OAuthString authorizeURL{metadata.authorizationEndpoint};
    ZfURI::save(authorizeURL, request);
    browserPID = openBrowser(options.browser, options.ca, authorizeURL);
    ok = browserPID > 0 && !callback.done.timedwait(
      Zm::now(options.callbackTimeout));
  }
  if (callbackUp) (void)callbackServer.stop();
  if (callbackInited) callbackServer.final();
  if (browserPID > 0) {
    int status;
    while (::waitpid(browserPID, &status, 0) < 0 && errno == EINTR) { }
  }
  ok = ok && callback.received && callback.state == state &&
    callback.issuerURL == options.issuerURL && !callback.error && callback.code;

  Tokens tokens;
  if (ok) {
    OAuthCodeTokenReq request;
    request.grantType = "authorization_code";
    request.code = callback.code;
    request.redirectURI = redirect;
    request.clientID = options.clientID;
    request.codeVerifier = verifier;
    OAuthSecret form;
    ZfURI::saveBody(form, request);
    callback.code.null();
    verifier.null();
    auto result = client.perform<FormBuilder>(0,
      OAuthString{tokenTarget}, ZuMv(form));
    ok = tokenResponse(*result, options.scope, tokens, false);
    if (ok && options.verbose)
      ZiLOG(Info, "zrest", ([ns = nowNS()](auto &s) {
        s << "event=authorize time_ns=" << ns;
      }));
  }

  unsigned generated = 0, completed = 0, tick = 0;
  while (ok && !interrupted && generated < options.requests) {
    if (tokens.deadline <= Zm::now().sec() + 1)
      ok = refresh(client, tokenTarget, options.clientID,
        options.scope, tokens, options.verbose);
    unsigned count = options.requests - generated;
    if (count > options.concurrency) count = options.concurrency;
    ZtArray<ZmRef<Result>> results;
    for (unsigned i = 0; ok && i < count; ++i)
      results.push(client.submit<PingBuilder>(1, OAuthString{resourceTarget},
        {}, bearer(tokens)));
    ZtArray<unsigned> replay;
    for (unsigned i = 0; ok && i < results.length(); ++i) {
      results[i]->done.wait();
      if (results[i]->status == 401) replay.push(generated + i);
      else if (!pong(*results[i])) ok = false;
      else {
        ++completed;
        if (options.verbose)
          ZiLOG(Info, "zrest", ([id = generated + i](auto &s) {
            s << "event=pong id=" << id;
          }));
      }
    }
    if (ok && replay) {
      ok = refresh(client, tokenTarget, options.clientID,
        options.scope, tokens, options.verbose);
      for (unsigned id: replay) {
        if (!ok) break;
        auto result = client.perform<PingBuilder>(1,
          OAuthString{resourceTarget}, {}, bearer(tokens));
        ok = pong(*result);
        if (ok) {
          ++completed;
          if (options.verbose)
            ZiLOG(Info, "zrest", ([id](auto &s) {
              s << "event=pong id=" << id;
            }));
        }
      }
    }
    generated += count;
    if (options.interval > 0) {
      ++tick;
      if (options.verbose)
        ZiLOG(Info, "zrest", ([tick, count, ns = nowNS()](auto &s) {
          s << "event=ping-wave tick=" << tick << " count=" << count <<
            " time_ns=" << ns;
        }));
      if (generated < options.requests) ok = sleepFor(options.interval);
    }
  }

  if (tokens.refresh) {
    OAuthRevokeReq request;
    request.token = tokens.refresh;
    request.tokenTypeHint = "refresh_token";
    request.clientID = options.clientID;
    OAuthSecret form;
    ZfURI::saveBody(form, request);
    auto result = client.perform<FormBuilder>(0,
      OAuthString{revokeTarget}, ZuMv(form));
    ok = ok && (result->status == 200 || !result->status);
  }
  tokens.clear();
  state.null();

  client.stop();
  client.final();
  mx.stop();
  if (!ok || interrupted || completed != options.requests)
    ZiLOG(Error, "zrest", ([completed, expected = options.requests](auto &s) {
      s << "incomplete run (completed=" << completed <<
        ", expected=" << expected << ')';
    }));
  ZiLog::stop();
  return ok && !interrupted && completed == options.requests ? 0 : 1;
}
