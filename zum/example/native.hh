//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Shared native example OAuth flow; independent of libZum

#ifndef ZumNativeExample_HH
#define ZumNativeExample_HH

#include <iostream>

#ifndef _WIN32
#include <sys/types.h>
#include <unistd.h>
#endif

#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuLib.hh>
#include <zlib/ZuICmp.hh>
#include <zlib/ZuPercent.hh>

#include <zlib/ZmList.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmPQueue.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTrap.hh>
#include <zlib/ZmVHeap.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZfCLI.hh>
#include <zlib/ZfCf.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/ZfURI.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiFile.hh>

#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpServer.hh>
#include <zlib/ZrestClient.hh>
#include <zlib/ZrestServer.hh>

#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsRandom.hh>

#include <zlib/ZumURI.hh>

namespace ZumNative {

ZuDerive(String, ZtString<ZtStringHeapID<"Zum.Native.String">>);
ZuDerive(Bytes, (ZtArray<uint8_t, ZtArrayHeapID<"Zum.Native.Bytes">>));

using ObjectHeap = ZmVHeap<
  "Zum.Native.Object", 0, ZmVHeap_DefltMax, alignof(max_align_t)>;
class ObjectAlloc {
public:
  static void *operator new(size_t size) { return ObjectHeap::valloc(size); }
  static void operator delete(void *ptr) { ObjectHeap::vfree(ptr); }
  static void operator delete(void *ptr, size_t) { ObjectHeap::vfree(ptr); }
};
class Object : public ObjectAlloc, public ZmObject { };

struct Tokens {
  String	accessToken;
  String	refreshToken;
  String	scope;
};

struct TokenWire {
  String accessToken;
  String refreshToken;
  String scope;
  String tokenType;
};
ZfStruct(, (TokenWire, JSON),
  (((accessToken),	(JSON::ID<"access_token">, Required)),	(String)),
  (((refreshToken),	(JSON::ID<"refresh_token">, JSON::Opt)),	(String)),
  (((scope),		(JSON::Opt)),	(String)),
  (((tokenType),	(JSON::ID<"token_type">, Required)),	(String)));

static void clearTokens(Tokens &tokens)
{
  if (tokens.accessToken && tokens.accessToken.mutable_())
    ZuClear(tokens.accessToken.data(), tokens.accessToken.length());
  if (tokens.refreshToken && tokens.refreshToken.mutable_())
    ZuClear(tokens.refreshToken.data(), tokens.refreshToken.length());
  tokens = {};
}

enum {
  ClientTimeout = 30,
  BodyMax = 64U<<10,
  DestinationMax = 8,
  CallbackPort = 8081
};

struct Config {
  String issuerURL;
  String serviceURL;
  String clientID;
  String caPath;
  String scope{"ping"};
  uint32_t callbackPort = CallbackPort;
  uint32_t loginTimeout = 180;
  bool loopbackTest = false;
};
ZfStruct(, (Config, Cf),
  (((issuerURL), (Required)), (String)),
  (((serviceURL)), (String)),
  (((clientID), (Required)), (String)),
  (((caPath)), (String)),
  (((scope)), (String, "ping")),
  (((callbackPort), ((Range<1, 65535>))), (UInt32, CallbackPort)),
  (((loginTimeout), ((Range<1, 3600>))), (UInt32, 180)),
  (((loopbackTest)), (Bool, false)));

static bool loadConfig(ZuCSpan path, Config &config)
{
  ZiFile file;
  if (file.open(Zi::Path{path}, ZiFile::ReadOnly | ZiFile::NoFollow |
	ZiFile::GC) != Zi::OK) return false;
  auto size = file.size();
  if (size <= 0 || uint64_t(size) > BodyMax) return false;
  String source;
  source.length(unsigned(size));
  if (file.read(source.data(), unsigned(size)) != int(size)) return false;
  auto parsed = ZfCf::scan(source.span());
  if (parsed.p<0>() < 0 || !parsed.p<1>()) return false;
  config = ZfCf::handler<Config>(parsed.p<1>()).ctor();
  return config.issuerURL && config.clientID;
}

ZuDerive(StringVec, (ZtArray<String,
  ZtArrayHeapID<"Zum.Native.StringVec">>));
struct MetadataWire {
  String issuer;
  String authorizationEndpoint;
  String tokenEndpoint;
  String jwksURI;
  String revocationEndpoint;
  StringVec responseTypesSupported;
  StringVec grantTypesSupported;
  StringVec codeChallengeMethods;
};
ZfStruct(, (MetadataWire, JSON),
  (((issuer),		(JSON::Opt)),	(String)),
  (((authorizationEndpoint),
    (JSON::ID<"authorization_endpoint">, JSON::Opt)),	(String)),
  (((tokenEndpoint),
    (JSON::ID<"token_endpoint">, JSON::Opt)),	(String)),
  (((jwksURI),		(JSON::ID<"jwks_uri">, JSON::Opt)),	(String)),
  (((revocationEndpoint),
    (JSON::ID<"revocation_endpoint">, JSON::Opt)),	(String)),
  (((responseTypesSupported),
    (JSON::ID<"response_types_supported">, JSON::Opt)),	(StringVec)),
  (((grantTypesSupported),
    (JSON::ID<"grant_types_supported">, JSON::Opt)),	(StringVec)),
  (((codeChallengeMethods),
    (JSON::ID<"code_challenge_methods_supported">, JSON::Opt)),
    (StringVec)));

template <typename Fn>
static bool formEach(ZuCSpan form, Fn &&fn)
{
  while (form) {
    auto amp = form.find("&");
    ZuCSpan item = amp >= 0 ?
      ZuCSpan{form.data(), unsigned(amp)} : form;
    auto eq = item.find("=");
    if (eq < 0) return false;
    String name{item.data(), unsigned(eq)};
    String value{item.offset(unsigned(eq) + 1)};
    using Form = ZuPercent::Codec<ZfURI::PercentQuote<true>>;
    auto key = Form::decode(name.span());
    auto data = Form::decode(value.span());
    if (!key || !data) return false;
    name.length(key.out);
    value.length(data.out);
    fn(name, value);
    if (amp < 0) break;
    form.offset(unsigned(amp) + 1);
  }
  return true;
}

static String formField(ZuCSpan name, ZuCSpan value)
{
  String form{name};
  form << '=';
  ZfURI::PathQuote::quote(form, value);
  return form;
}

static String encode(ZuBSpan data)
{
  String s;
  s.length(ZuBase64URL::enclen(data.length()));
  s.length(ZuBase64URL::encode(s.span(), data));
  return s;
}

template <unsigned Status_>
struct HTTPData : public Object {
  enum { Status = Status_ };
  String data;
  ~HTTPData() {
    if (data.mutable_()) ZuClear(data.data(), data.length());
  }
  HTTPData &operator =(ZuSpan<uint8_t> data_) {
    data = data_;
    return *this;
  }
};

struct Result {
  ZmSemaphore	done;
  String	body;
  unsigned	status = 0;
  ~Result() {
    if (body.mutable_()) ZuClear(body.data(), body.length());
  }
};

class Client;

struct Call : public Object {
  Client	*client = nullptr;
  Result	*result = nullptr;
  String	target;
  String	body;
  String	authorization;
  mutable ZmAtomic<unsigned> done = 0;

  ~Call() {
    if (body.mutable_()) ZuClear(body.data(), body.length());
    if (authorization.mutable_()) ZuClear(authorization.data(), authorization.length());
  }
  void finish(unsigned status, ZuCSpan body = {}) const {
    if (done.cmpXch(1, 0)) return;
    result->status = status;
    result->body = body;
    result->done.post();
  }

  template <typename Link, typename Response>
  void process(Link *, const Response *response) const {
    finish(Response::Status, response->data);
  }
  template <typename Link> void failed(Link *) const {
    finish(0);
  }
};

using OKData = HTTPData<200>;
using BadRequestData = HTTPData<400>;
using UnauthorizedData = HTTPData<401>;
using ForbiddenData = HTTPData<403>;
using ServerErrorData = HTTPData<500>;

struct RequestOK : public Zrest::ResParser<RequestOK, OKData> {
  enum { Status = 200, Body = Zrest::BodyPolicy::Raw };
};
struct BadRequest : public Zrest::ResParser<BadRequest, BadRequestData> {
  enum { Status = 400, Body = Zrest::BodyPolicy::Raw };
};
struct TokenUnauthorized : public Zrest::ResParser<TokenUnauthorized,
    UnauthorizedData> {
  enum { Status = 401, Body = Zrest::BodyPolicy::Raw };
};
struct Forbidden : public Zrest::ResParser<Forbidden, ForbiddenData> {
  enum { Status = 403, Body = Zrest::BodyPolicy::Raw };
};
struct ServerError : public Zrest::ResParser<ServerError, ServerErrorData> {
  enum { Status = 500, Body = Zrest::BodyPolicy::Raw };
};

template <typename Impl, Zhttp::Method::T Method_, unsigned Body_,
    typename Path_>
struct Request : public Zrest::ReqBuilder<Impl, Call> {
  using Base = Zrest::ReqBuilder<Impl, Call>;
  using Base::header;
  enum { Method = Method_, Exact = 1, Query = Zrest::QueryPolicy::Raw,
    Body = Body_ };
  using Path = Path_;
  using Headers = ZhttpHeaders(
    "content-type", "authorization", "content-length");
  using Responses = ZuTypeList<RequestOK, BadRequest, TokenUnauthorized,
    Forbidden, ServerError>;
  const String &queryObject(const Call *call) const { return call->target; }
  const String &bodyObject(const Call *call) const { return call->body; }
  template <typename Emit> void operation(Emit &&emit) const {
    emit(Method_, [this](auto &&emit) {
      emit([this](auto &s) {
	s << ZuStringT<"/">{}() << this->queryObject(this->object.ptr());
      });
    });
  }
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "content-type") {
      if constexpr (Body_ != Zrest::BodyPolicy::None)
	l("application/x-www-form-urlencoded");
    } else if constexpr (Key{}() == "authorization")
      l(this->object->authorization);
    else Base::template header<Key>(ZuFwd<L>(l));
  }
};

struct MetadataBuilder : public Request<MetadataBuilder,
    Zhttp::Method::GET, Zrest::BodyPolicy::None,
    ZuStringT<"/metadata">> { };
struct TokenBuilder : public Request<TokenBuilder,
    Zhttp::Method::POST, Zrest::BodyPolicy::Raw,
    ZuStringT<"/token">> { };
struct RevokeBuilder : public Request<RevokeBuilder,
    Zhttp::Method::POST, Zrest::BodyPolicy::Raw,
    ZuStringT<"/revoke">> { };
struct ResourceBuilder : public Request<ResourceBuilder,
    Zhttp::Method::GET, Zrest::BodyPolicy::None,
    ZuStringT<"/resource">> { };

using ClientRequests = ZuTypeList<MetadataBuilder, TokenBuilder,
  RevokeBuilder, ResourceBuilder>;
ZrestCatalogDerive(ClientCatalog, ClientRequests);
ZrestCatalogImpl(ClientCatalog)

struct ReqBuilder_ : public ZmObject,
    public Zrest::MReqBuilder<ClientCatalog> {
  using Reqs = ClientRequests;
  uint64_t id = 0;
  uint64_t key() const { return id; }
  uint64_t length() const { return 1; }
  void completed(const Zhttp::Result &result) {
    u.cdispatch([&result](auto, const auto &request) {
      request.object->finish(result.status);
    });
  }
};

struct ResParser : public Zrest::MResParser<ClientCatalog, ReqBuilder_> { };

class Pool;
template <typename Heap = ZuVoid> class Pool_;
ZmPQueueDerive(ReqBuilderQ, ReqBuilder_,
  ZmPQueueOverlap<false, ZmPQueueNode<ReqBuilder_,
    ZmPQueueHeapID<"zum.ReqBuilder">>>);
using ReqBuilder = ReqBuilderQ::Node;
ZuDerive(TxQ, (ZmPQTx<Pool, ReqBuilderQ, ZmPQTxOrdered<false>>));

template <typename Heap>
class Pool_ : public Heap, public Zhttp::Pool<Client, TxQ, ResParser> {
  using Base = Zhttp::Pool<Client, TxQ, ResParser>;
public:
  Pool_(Client *client) : Base{client} { }
  ReqBuilderQ *txQueue() { return &m_requests; }
  void archive_(ReqBuilder *) { }
  ZmRef<ReqBuilder> retrieve_(ReqBuilderQ::Key, ReqBuilderQ::Key) { return {}; }
private:
  ReqBuilderQ m_requests;
};

using PoolHeap = ZmHeap<"zum.Pool", Pool_<>>;
class Pool : public Pool_<PoolHeap> {
public:
  using Pool_<PoolHeap>::Pool_;
};

class Client : public Object, public Zhttp::Client<Client, Pool> {
  using Base = Zhttp::Client<Client, Pool>;
public:
  bool init(ZiMultiplex *mx, const Zhttp::URLView &url, ZuCSpan caPath) {
    m_origin = Zhttp::Origin{url.origin()};
    bool secure = url.scheme == Zhttp::Scheme::https;
    auto config = Zhttp::Config().links(1).concurrency(1).linkMax(1)
      .requestTimeout(ClientTimeout).retainedBodyMax(BodyMax)
      .protocol(Zhttp::ProtoPolicy::DisableH3)
      .secure(secure).tcp(true).tls(secure).quic(false);
    if (!Base::init(Zhttp::HubConfig{mx, "rx", "tx"}, 1,
        config, Zhttp::TCPConfig{}, Zhttp::H2Config{}.caPath(caPath),
        Zhttp::QUICConfig{})) return false;
    if (Base::pool(0, Zhttp::Destination{url.origin()}) && Base::start())
      return true;
    Base::stop();
    Base::final();
    return false;
  }

  bool origin(const Zhttp::URLView &url) const {
    return m_origin == Zhttp::Origin{url.origin()};
  }

  template <typename Builder>
  void perform(Result &result, const Zhttp::URLView &url, String body = {},
      String authorization = {}) {
    ZmRef<Call> call = new Call{};
    call->client = this;
    call->result = &result;
    if (url.path) {
      auto path = url.path;
      if (path[0] == '/') path.offset(1);
      call->target = path;
    }
    if (url.hasQuery) call->target << '?' << url.query;
    call->body = ZuMv(body);
    call->authorization = ZuMv(authorization);
    txRun(0, [this, call = ZuMv(call)]() mutable {
      ZmRef<ReqBuilder> request = new ReqBuilder{};
      request->id = m_id++;
      request->template init<Builder>(call.ptr());
      if (!this->send(0, ZuMv(request))) call->finish(0);
    });
    result.done.wait();
  }
private:
  Zhttp::Origin m_origin;
  uint64_t m_id = 0;
};

class Clients {
public:
  bool add(ZiMultiplex *mx, const Zhttp::URLView &url, ZuCSpan caPath) {
    for (auto &client: m_clients)
      if (client->origin(url)) return true;
    if (m_clients.length() >= DestinationMax) return false;
    ZmRef<Client> client = new Client{};
    if (!client->init(mx, url, caPath)) return false;
    m_clients.push(ZuMv(client));
    return true;
  }

  template <typename Builder>
  bool perform(Result &result, ZuCSpan endpoint, String body = {},
      String authorization = {}) {
    ZuGuard clear{[&body, &authorization]() {
      if (body.mutable_()) ZuClear(body.data(), body.length());
      if (authorization.mutable_())
	ZuClear(authorization.data(), authorization.length());
    }};
    Zhttp::URL parsed{endpoint};
    auto url = parsed.url();
    if (!parsed.ok()) return false;
    for (auto &client: m_clients) {
      if (!client->origin(url)) continue;
      client->template perform<Builder>(result, url,
	ZuMv(body), ZuMv(authorization));
      return true;
    }
    return false;
  }

  void final() {
    for (auto &client: m_clients) {
      client->stop();
      client->final();
    }
    m_clients.null();
  }

private:
  ZtArray<ZmRef<Client>> m_clients;
};

struct CallbackData : public Object {
  ZuSpan<uint8_t> data;
  CallbackData &operator =(ZuSpan<uint8_t> data_) { data = data_; return *this; }
};

struct CallbackBody : public Object {
  String data;
};

struct CallbackOK : public Zrest::ResBuilder<CallbackOK, CallbackBody> {
  enum { Body = Zrest::BodyPolicy::Raw };
  using Headers = ZhttpHeaders(
    ("content-type", "text/html; charset=utf-8"), "content-length");
  const String &bodyObject(const CallbackBody *data) const { return data->data; }
};

class CallbackApp;
static ZmSemaphore callbackDone;

struct CallbackReq : public Zrest::ReqParser<CallbackReq, CallbackData> {
  enum { Exact = 1, Query = Zrest::QueryPolicy::Raw };
  static constexpr uint64_t QueryLimit = 16U<<10;
  using Path = ZuStringT<"/callback">;
  using Responses = ZuTypeList<CallbackOK>;
  CallbackApp *app = nullptr;
  template <typename Link> void complete(Link *, bool);
};

using CallbackRequests = ZuTypeList<CallbackReq>;
ZrestCatalogDerive(CallbackCatalog, CallbackRequests);
ZrestCatalogImpl(CallbackCatalog)

struct CallbackParser : public Zrest::MReqParser<CallbackCatalog> {
  using Base = Zrest::MReqParser<CallbackCatalog>;
  void init(CallbackApp &app_) { app = &app_; }
  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    if (!Base::operation(method, target)) return false;
    u.dispatch([this](auto, auto &request) { request.app = app; });
    return true;
  }
  CallbackApp *app = nullptr;
};

using CallbackBuilder = Zrest::MResBuilder<CallbackCatalog>;
struct CallbackBuilder_ : public ZmObject, public CallbackBuilder { };
ZmListDerive(CallbackBuilderQ, CallbackBuilder_,
  ZmListNode<CallbackBuilder_, ZmListHeapID<"zum.CallbackBuilder">>);
using CallbackBuilderNode = CallbackBuilderQ::Node;

class CallbackApp {
public:
  using Parser = CallbackParser;
  using ResBuilderQ = CallbackBuilderQ;

  template <typename Link>
  void callback(Link *link, const CallbackReq &request, bool ok)
  {
    String query;
    if (ok && request.object) {
      auto span = request.object->data;
      if (span && span[0] == '?') span.offset(1);
      query = span;
    } else ok = false;
    if (!query.mutable_()) query.length(query.length());
    unsigned seen = 0;
    bool duplicate = false;
    String code_, state_, error_;
    if (ok) ok = formEach({query.data(), query.length()}, [&seen, &duplicate,
	&code_, &state_, &error_](
          ZuCSpan name, ZuCSpan value) {
        if (name == "code") {
	  duplicate |= bool(seen & 1U); code_ = value; seen |= 1U; return;
        }
        if (name == "state") {
	  duplicate |= bool(seen & 2U); state_ = value; seen |= 2U; return;
        }
        if (name == "error") {
	  duplicate |= bool(seen & 4U); error_ = value; seen |= 4U; return;
        }
      });
    ok = ok && !duplicate && !received && state_ == expectedState &&
      ((seen == 3U && code_) || (seen == 6U && error_));
    if (ok) {
      code = ZuMv(code_); state = ZuMv(state_); error = ZuMv(error_);
      received = true;
    }
    ZmRef<CallbackBody> object = new CallbackBody{};
    object->data = ok && code ?
      String{"<!doctype html><h1>Authorized</h1><p>You may close this window.</p>"} :
      String{"<!doctype html><h1>Authorization failed</h1>"};
    ZmRef<CallbackBuilderNode> response = new CallbackBuilderNode{};
    response->template init<CallbackOK, CallbackReq>(object.ptr());
    link->send(ZuMv(response));
    if (ok) callbackDone.post();
  }

  void listening(int, unsigned) { }
  void listenFailed(int, bool) { callbackDone.post(); }
  void connected(int) { }
  void disconnected(int) { }

  String	code;
  String	state;
  String	error;
  String	expectedState;
  bool		received = false;
};

template <typename Link>
void CallbackReq::complete(Link *link, bool ok) { app->callback(link, *this, ok); }

static void interrupted() { callbackDone.post(); }

static ZiMxParams mxParams()
{
  return ZiMxParams().scheduler([](auto &s) {
    s.nThreads(4)
      .thread(1, [](auto &t) { t.name("rx"); t.isolated(1); })
      .thread(2, [](auto &t) { t.name("tx"); t.isolated(1); })
      .thread(3, [](auto &t) { t.name("cb-rx"); t.isolated(1); })
      .thread(4, [](auto &t) { t.name("cb-tx"); t.isolated(1); });
  }).rxThread(1).txThread(2);
}

static bool tokenJSON(String &json, Tokens &tokens)
{
  if (json.length() > BodyMax) return false;
  if (!json.mutable_()) json.length(json.length());
  auto parsed = ZfJSON::scan({json.data(), json.length()});
  if (parsed.p<0>() != int(json.length()) || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 || !ZfJSON::unique(roots[0])) return false;
  TokenWire wire = ZfJSON::handler<TokenWire>(roots[0]).ctor();
  ZuGuard clear{[&wire]() {
    if (wire.accessToken.mutable_())
      ZuClear(wire.accessToken.data(), wire.accessToken.length());
    if (wire.refreshToken.mutable_())
      ZuClear(wire.refreshToken.data(), wire.refreshToken.length());
  }};
  if (!wire.accessToken ||
      !ZuICmp<ZuCSpan>::equals(wire.tokenType, "Bearer")) return false;
  Tokens next{ZuMv(wire.accessToken), ZuMv(wire.refreshToken),
    ZuMv(wire.scope)};
  clearTokens(tokens);
  tokens = ZuMv(next);
  return true;
}

static bool endpointURL(ZuCSpan value, bool loopbackTest,
    Zhttp::URL &parsed)
{
  auto error = parsed.assign(value);
  if (!error.ok()) return false;
  auto url = parsed.url();
  if (!url.host || url.hasFragment) return false;
  if (url.scheme == Zhttp::Scheme::https) return true;
  return loopbackTest && url.scheme == Zhttp::Scheme::http &&
    (url.host == "localhost" || url.host == "127.0.0.1" ||
      url.host == "::1");
}

static bool metadataURL(ZuCSpan issuer, bool loopbackTest,
    String &value, Zhttp::URL &parsed)
{
  if (!endpointURL(issuer, loopbackTest, parsed)) return false;
  auto url = parsed.url();
  if (url.hasQuery || url.hasFragment || !url.path) return false;
  // URLView is const; typed URI loading percent-decodes its mutable input.
  String source{url.path};
  Zum::AppIssuerPath path;
  if (!ZfURI::loadPath(path, source) || path.oauth2 != "oauth2" ||
      !path.appID) return false;
  value << url.origin();
  ZfURI::savePath(value, Zum::AppOAuthMetadataPath{
    .wellKnown = ".well-known", .endpoint = "oauth-authorization-server",
    .oauth2 = "oauth2", .appID = path.appID});
  return true;
}

static bool contains(const StringVec &values, ZuCSpan value)
{
  for (const auto &item: values) if (item == value) return true;
  return false;
}

static bool metadataJSON(String &json, ZuCSpan issuer, MetadataWire &metadata)
{
  if (json.length() > BodyMax) return false;
  if (!json.mutable_()) json.length(json.length());
  auto parsed = ZfJSON::scan({json.data(), json.length()});
  if (parsed.p<0>() != int(json.length()) || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 || !ZfJSON::unique(roots[0])) return false;
  metadata = ZfJSON::handler<MetadataWire>(roots[0]).ctor();
  return metadata.issuer == issuer && metadata.authorizationEndpoint &&
    metadata.tokenEndpoint && metadata.jwksURI &&
    metadata.revocationEndpoint &&
    contains(metadata.responseTypesSupported, "code") &&
    contains(metadata.grantTypesSupported, "authorization_code") &&
    contains(metadata.grantTypesSupported, "refresh_token") &&
    contains(metadata.codeChallengeMethods, "S256");
}

static void openBrowser(ZuCSpan url, bool noBrowser)
{
  std::cout << "Open this URL in a browser:\n" << url << "\n\n" << std::flush;
  if (noBrowser) return;
#ifndef _WIN32
  pid_t pid = ::fork();
  if (!pid) {
    String path{url};
    ::execlp("xdg-open", "xdg-open", path.data(), static_cast<char *>(nullptr));
    ::_exit(127);
  }
#endif
}

template <typename L>
bool run(const Config &config, bool noBrowser, L &&useToken)
{
  String discoveryURL;
  Zhttp::URL issuer;
  if (!metadataURL(config.issuerURL, config.loopbackTest,
      discoveryURL, issuer)) {
    std::cerr << "OAuth: invalid application issuer URL\n";
    return false;
  }
  ZiLog::init("native-oauth");
  ZiLog::level(Ze::Warning);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZmTrap::sigintFn(interrupted);
  ZmTrap::trap();

  ZiMultiplex mx{mxParams()};
  if (!mx.start()) return false;

  Clients clients;
  if (!clients.add(&mx, issuer.url(), config.caPath)) {
    std::cerr << "OAuth: issuer HTTP client start failed\n";
    clients.final(); mx.stop(); ZiLog::stop(); return false;
  }
  Result discovery;
  bool ok = clients.perform<MetadataBuilder>(discovery, discoveryURL) &&
    discovery.status == 200;
  MetadataWire metadata;
  ok = ok && metadataJSON(discovery.body, config.issuerURL, metadata);
  Zhttp::URL authorization, token, jwks, revocation;
  ok = ok && endpointURL(metadata.authorizationEndpoint,
    config.loopbackTest, authorization) &&
    endpointURL(metadata.tokenEndpoint, config.loopbackTest, token) &&
    endpointURL(metadata.jwksURI, config.loopbackTest, jwks) &&
    endpointURL(metadata.revocationEndpoint, config.loopbackTest, revocation);
  ok = ok && clients.add(&mx, token.url(), config.caPath) &&
    clients.add(&mx, revocation.url(), config.caPath);
  if (!ok) {
    std::cerr << "OAuth: OAuth discovery failed\n";
    clients.final(); mx.stop(); ZiLog::stop(); return false;
  }

  Ztls::Random rng;
  Bytes random;
  random.length(32, false);
  ok = rng.init() && rng.random(random);
  String verifier = ok ? encode(random) : String{};
  ZuGuard verifierGuard{[&verifier]() {
    if (verifier.mutable_()) ZuClear(verifier.data(), verifier.length());
  }};
  ZuBArray<Ztls::MD<>::Size> digest(Ztls::MD<>::Size, false);
  if (ok) {
    Ztls::MD<> md;
    md.update(ZuBSpan{verifier});
    md.finish(digest);
  }
  String challenge = ok ? encode(digest) : String{};
  random.length(16, false);
  ok = ok && rng.random(random);
  String state = ok ? encode(random) : String{};
  CallbackApp callback;
  ZuGuard codeGuard{[&callback]() {
    if (callback.code.mutable_())
      ZuClear(callback.code.data(), callback.code.length());
  }};
  callback.expectedState = state;
  Zhttp::Server<CallbackApp> callbackServer;
  auto callbackConfig = Zhttp::ServerConfig().localIP(ZiIP("127.0.0.1"))
    .port(config.callbackPort).idleTimeout(ClientTimeout)
    .retainedBodyMax(BodyMax).tcp();
  bool callbackInited = callbackServer.init(
    Zhttp::HubConfig{&mx, "cb-rx", "cb-tx"}, ZuMv(callbackConfig), &callback);
  bool callbackUp = callbackInited && callbackServer.start();
  if (!callbackUp) {
    std::cerr << "OAuth: callback listener start failed\n";
    if (callbackInited) (void)callbackServer.stop();
    callbackServer.final(); clients.final();
    mx.stop(); ZiLog::stop(); return false;
  }

  String redirect;
  redirect << "http://127.0.0.1:" << config.callbackPort << "/callback";
  String authorize{metadata.authorizationEndpoint};
  authorize << (authorization.url().hasQuery ? '&' : '?') <<
    "response_type=code&" <<
    formField("client_id", config.clientID) << '&' <<
    formField("redirect_uri", redirect) << '&' << formField("scope", config.scope) <<
    "&state=" << state <<
    "&code_challenge=" << challenge << "&code_challenge_method=S256";
  if (ok) openBrowser(authorize, noBrowser);
  if (!ok) std::cerr << "OAuth: random source failed\n";

  if (ok) ok = !callbackDone.timedwait(
    Zm::now() + ZuTime{int64_t(config.loginTimeout)});
  ok = ok && callback.code && callback.state == state && !callback.error;
  if (!ok) std::cerr << "OAuth: authorization failed or timed out\n";

  Tokens tokens;
  if (ok) {
    String form;
    form << "grant_type=authorization_code&" << formField("code", callback.code) <<
      '&' << formField("client_id", config.clientID) << '&' <<
      formField("redirect_uri", redirect) << '&' << formField("code_verifier", verifier);
    Result result;
    ok = clients.perform<TokenBuilder>(result, metadata.tokenEndpoint,
      ZuMv(form)) && result.status == 200 && tokenJSON(result.body, tokens);
    if (!ok) std::cerr << "OAuth: code redemption failed\n";
  }

  if (ok) ok = useToken(mx, clients, ZuCSpan{tokens.accessToken});

  if (ok && tokens.refreshToken) {
    String form{"grant_type=refresh_token&"};
    form << formField("refresh_token", tokens.refreshToken) << '&' <<
      formField("client_id", config.clientID);
    Result result;
    ok = clients.perform<TokenBuilder>(result, metadata.tokenEndpoint,
      ZuMv(form));
    Tokens rotated;
    ok = ok && result.status == 200 && tokenJSON(result.body, rotated) &&
      rotated.refreshToken;
    if (ok) {
      clearTokens(tokens);
      tokens = ZuMv(rotated);
      std::cout << "refresh token rotated\n";
    } else {
      clearTokens(rotated);
      std::cerr << "OAuth: refresh failed\n";
    }
  }

  if (ok) ok = useToken(mx, clients, ZuCSpan{tokens.accessToken});

  if (tokens.refreshToken) {
    String form;
    form << formField("token", tokens.refreshToken) << '&' <<
      formField("token_type_hint", "refresh_token") << '&' <<
      formField("client_id", config.clientID);
    Result result;
    bool revoked = clients.perform<RevokeBuilder>(result,
      metadata.revocationEndpoint, ZuMv(form)) && result.status == 200;
    if (!revoked) {
      std::cerr << "OAuth: refresh token revocation failed\n";
      ok = false;
    }
  }
  clearTokens(tokens);

  (void)callbackServer.stop();
  callbackServer.final();
  clients.final();
  mx.stop();
  ZiLog::stop();
  return ok;
}

} // ZumNative

#endif /* ZumNativeExample_HH */
