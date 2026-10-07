//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Zum administrative REST client

#include <iostream>

#ifndef _WIN32
#include <sys/types.h>
#include <unistd.h>
#endif

#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuLib.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuICmp.hh>
#include <zlib/ZuPercent.hh>
#include <zlib/ZuPtr.hh>

#include <zlib/ZmList.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmPQueue.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZfCLI.hh>
#include <zlib/ZfCf.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/ZfURI.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>

#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpServer.hh>
#include <zlib/ZrestClient.hh>
#include <zlib/ZrestServer.hh>

#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsVault.hh>

#include <zlib/ZumMgmt.hh>
#include <zlib/ZumURI.hh>

#include <zlib/ZumVaultClient.hh>
#include <zlib/zum_cli.hh>
#include <zlib/ZtPlatform.hh>

ZuDerive(String, ZtString<ZtStringHeapID<"zumc.String">>);
ZuDerive(Bytes, (ZtArray<uint8_t, ZtArrayHeapID<"zumc.Bytes">>));

struct Token {
  ZumVaultClient::SecretText accessToken;
  ZumVaultClient::SecretText refreshToken;
  ZumVaultClient::Text scope;
  String tokenType;
};
ZfStruct(, (Token, JSON),
  (accessToken,	(JSON::ID<"access_token">, Required),			String),
  (refreshToken,	(JSON::ID<"refresh_token">, JSON::Opt),		String),
  (scope,		(JSON::Opt),					String),
  (tokenType,	(JSON::ID<"token_type">, Required),			String));

ZuDerive(StringVec, (ZtArray<String,
  ZtArrayHeapID<"zumc.StringVec">>));
struct Metadata {
  String issuerURL;
  String authorizationEndpoint;
  String tokenEndpoint;
  String jwksURI;
  String revocationEndpoint;
  StringVec responseTypesSupported;
  StringVec grantTypesSupported;
  StringVec codeChallengeMethods;
};
ZfStruct(, (Metadata, JSON),
  (issuerURL,	(JSON::ID<"issuer">, JSON::Opt),		String),
  (authorizationEndpoint,
    (JSON::ID<"authorization_endpoint">, JSON::Opt),		String),
  (tokenEndpoint,
    (JSON::ID<"token_endpoint">, JSON::Opt),			String),
  (jwksURI,		(JSON::ID<"jwks_uri">, JSON::Opt),	String),
  (revocationEndpoint,
    (JSON::ID<"revocation_endpoint">, JSON::Opt),		String),
  (responseTypesSupported,
    (JSON::ID<"response_types_supported">, JSON::Opt),		StringVec),
  (grantTypesSupported,
    (JSON::ID<"grant_types_supported">, JSON::Opt),		StringVec),
  (codeChallengeMethods,
    (JSON::ID<"code_challenge_methods_supported">, JSON::Opt),	StringVec));

static void clearTokens(ZumVaultClient::Credential &tokens)
{
  tokens.accessToken.null();
  tokens.refreshToken.null();
  tokens.scope = ZumVaultClient::Text{};
}

enum {
  ClientTimeout = 30,
  LoginTimeout = 180,
  BodyMax = 1U<<20,
  CallbackPort = 8081
};

using Options = ZumCLI::Options;

struct Config {
  String	issuerURL;
  String	managementURL;
  String	caPath;
  String	clientID{"zum-admin"};
  String	scope{"zum.admin"};
  uint32_t	callbackPort = CallbackPort;
  uint32_t	loginTimeout = LoginTimeout;
  bool		loopbackTest = false;
};
ZfStruct(, (Config, Cf),
  (issuerURL, (Mutable, Required),			String),
  (managementURL, (Mutable, Required),			String),
  (caPath, (Mutable),					String),
  (clientID, (Mutable),					String),
  (scope, (Mutable),					String),
  (callbackPort, (Mutable, (Range<1, 65535>)),		UInt32),
  (loginTimeout, (Mutable, (Range<1, 3600>)),		UInt32),
  (loopbackTest, (Mutable),				Bool));

static void usage(int code = 1, const ZfCLI::Parser<Options> *parser = nullptr)
{
  std::cerr <<
    "Usage: zum [--config FILE] login [--no-browser]\n"
    "       zum [--config FILE] app add NAME AUDIENCE [OPTIONS]\n"
    "       zum [--config FILE] RESOURCE [SUBRESOURCE] VERB ARGS... [OPTIONS]\n\n"
    "Required fields are positional; optional fields use --kebab-case names.\n"
    "Lists are comma-separated; use an empty argument for an empty list.\n"
    "JSON responses go to stdout. Idempotency keys are generated automatically.\n"
    "Controls: --if-match ETAG, --if-none-match '*', --idempotence KEY.\n"
    "Configuration: --config FILE, ZUM_CONFIG, or $ZUM_HOME/zum.cf\n"
    "($HOME/.zum/zum.cf when ZUM_HOME is unset).\n\n";
  for (int op = 0; op < Zum::MgmtOp::N; ++op) {
    if (parser && !ZumCLI::matches(
	*parser, ZumCLI::command(op), parser->argc - 1)) continue;
    auto args = ZumCLI::spec(op);
    std::cerr << "  zum " << ZumCLI::command(op);
    auto argument = [](ZuCSpan entry) {
      auto key = ZumCLI::option(ZumCLI::name(entry));
      for (unsigned i = 0, n = key.length(); i < n; ++i) {
	char c = key[i];
	std::cerr << char(c == '-' ? '_' :
	  c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
      }
    };
    for (auto fields = args.required; fields;) {
      std::cerr << ' ';
      argument(ZumCLI::word(fields));
    }
    for (auto fields = args.positional; fields;) {
      std::cerr << " [";
      argument(ZumCLI::word(fields));
      std::cerr << ']';
    }
    for (auto fields = args.optional; fields;) {
      auto entry = ZumCLI::word(fields);
      std::cerr << " [--" << ZumCLI::option(ZumCLI::name(entry)) << ' ';
      argument(entry);
      std::cerr << ']';
    }
    std::cerr << '\n';
  }
  ::exit(code);
}

static bool readFile(ZuCSpan path, String &data, unsigned limit = BodyMax)
{
  ZiFile file;
  if (file.open(Zi::Path{path}, ZiFile::ReadOnly | ZiFile::NoFollow |
	ZiFile::GC) != Zi::OK) return false;
  auto size = file.size();
  if (size < 0 || uint64_t(size) > limit) return false;
  data.length(unsigned(size));
  return !size || file.read(data.data(), unsigned(size)) == int(size);
}

static bool loadConfig(ZuCSpan path, Config &config)
{
  String source;
  if (!readFile(path, source)) return false;
  if (!source.mutable_()) source.length(source.length());
  auto scan = ZfCf::scan({source.data(), source.length()});
  if (scan.p<0>() < 0 || !scan.p<1>()) return false;
  ZfCf::handler<Config>(scan.p<1>()).update(config);
  return true;
}


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

static bool endpointURL(ZuCSpan value, bool loopbackTest, Zhttp::URL &parsed)
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

static bool metadataURL(ZuCSpan issuerURL, bool loopbackTest,
    String &value, Zhttp::URL &parsed)
{
  if (!endpointURL(issuerURL, loopbackTest, parsed)) return false;
  auto url = parsed.url();
  if (url.hasQuery || !url.path) return false;
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

template <unsigned Status_, typename Heap = ZuVoid>
struct HTTPData_ : public Heap, public ZmObject  {
  enum { Status = Status_ };
  String data;
  HTTPData_ &operator =(ZuSpan<uint8_t> data_) {
    data = data_;
    return *this;
  }
};
template <unsigned Status_>
ZuDerive(HTTPDataHeap, (ZmHeap<"Zum.HTTPData", HTTPData_<Status_>>));
template <unsigned Status_>
ZuDerive(HTTPData, (HTTPData_<Status_, HTTPDataHeap<Status_>>));

struct Result {
  ZmSemaphore	done;
  String	body;
  unsigned	status = 0;
  ~Result() {
    if (body.mutable_()) ZuClear(body);
  }
};

class Client;

template <typename Heap = ZuVoid>
struct Call_ : public Heap, public ZmObject  {
  Client	*client = nullptr;
  Result	*result = nullptr;
  String	query;
  String	body;
  String	authorization;
  mutable ZmAtomic<unsigned> done = 0;

  ~Call_() {
    if (body.mutable_()) ZuClear(body);
    if (authorization.mutable_()) ZuClear(authorization);
  }
  void finish(unsigned status, ZuCSpan body = {}) const {
    if (done.cmpXch(1, 0)) return;
    result->status = status;
    result->body = body;
    result->done.post();
  }
  String	ifMatch;
  String	ifNoneMatch;
  String	idempotence;

  template <typename Link, typename Response>
  void process(Link *, const Response *response) const {
    finish(Response::Status, response->data);
  }
  template <typename Link> void failed(Link *) const {
    finish(0);
  }
};
ZuDerive(CallHeap, (ZmHeap<"Zum.zum.Call", Call_<>>));
ZuDerive(Call, (Call_<CallHeap>));

using OKData = HTTPData<200>;
using BadRequestData = HTTPData<400>;
using UnauthorizedData = HTTPData<401>;
using ServerErrorData = HTTPData<500>;

struct TokenOK : public Zrest::ResParser<TokenOK, OKData> {
  enum { Status = 200, Body = Zrest::BodyPolicy::Raw };
};
struct BadRequest : public Zrest::ResParser<BadRequest, BadRequestData> {
  enum { Status = 400, Body = Zrest::BodyPolicy::Raw };
};
struct TokenUnauthorized : public Zrest::ResParser<TokenUnauthorized,
    UnauthorizedData> {
  enum { Status = 401, Body = Zrest::BodyPolicy::Raw };
};
struct ServerError : public Zrest::ResParser<ServerError, ServerErrorData> {
  enum { Status = 500, Body = Zrest::BodyPolicy::Raw };
};

#define ZUM_RESPONSE(NAME, STATUS) \
  using NAME##Data = HTTPData<STATUS>; \
  struct NAME : public Zrest::ResParser<NAME, NAME##Data> { \
    enum { Status = STATUS, Body = Zrest::BodyPolicy::Raw }; \
  }
ZUM_RESPONSE(Created, 201);
ZUM_RESPONSE(Accepted, 202);
ZUM_RESPONSE(NoContent, 204);
ZUM_RESPONSE(Forbidden, 403);
ZUM_RESPONSE(NotFound, 404);
ZUM_RESPONSE(MethodNotAllowed, 405);
ZUM_RESPONSE(Conflict, 409);
ZUM_RESPONSE(PreconditionFailed, 412);
ZUM_RESPONSE(Unprocessable, 422);
ZUM_RESPONSE(PreconditionRequired, 428);
ZUM_RESPONSE(Limited, 429);
ZUM_RESPONSE(Unavailable, 503);
ZUM_RESPONSE(NotImplemented, 501);
#undef ZUM_RESPONSE

using AdminResponses = ZuTypeList<TokenOK, Created, Accepted, NoContent,
  BadRequest, TokenUnauthorized, Forbidden, NotFound, MethodNotAllowed,
  Conflict, PreconditionFailed, Unprocessable, PreconditionRequired, Limited,
  ServerError, NotImplemented, Unavailable>;

template <typename Impl, Zhttp::Method::T Method_, unsigned Body_>
struct OAuthBuilder : public Zrest::ReqBuilder<Impl, Call> {
  using Base = Zrest::ReqBuilder<Impl, Call>;
  using Base::header;
  enum { Method = Method_, Exact = 1, Query = Zrest::QueryPolicy::Raw,
    Body = Body_ };
  using Path = ZuStringT<"/">;
  using Headers = ZhttpHeaders("content-type", "content-length");
  using Responses = ZuTypeList<TokenOK, BadRequest, TokenUnauthorized,
    ServerError>;
  const String &queryObject(const Call *call) const { return call->query; }
  const String &bodyObject(const Call *call) const { return call->body; }
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "content-type") {
      if constexpr (Body_ != Zrest::BodyPolicy::None)
	l("application/x-www-form-urlencoded");
    }
    else Base::template header<Key>(ZuFwd<L>(l));
  }
};

struct MetadataBuilder : public OAuthBuilder<MetadataBuilder,
    Zhttp::Method::GET, Zrest::BodyPolicy::None> { };
struct TokenBuilder : public OAuthBuilder<TokenBuilder,
    Zhttp::Method::POST, Zrest::BodyPolicy::Raw> { };

template <typename Impl, Zhttp::Method::T Method_, bool Body_>
struct AdminBuilder : public Zrest::ReqBuilder<Impl, Call> {
  using Base = Zrest::ReqBuilder<Impl, Call>;
  using Base::header;
  enum { Method = Method_, Exact = 1, Query = Zrest::QueryPolicy::Raw,
    Body = Body_ ? Zrest::BodyPolicy::Raw : Zrest::BodyPolicy::None };
  using Path = ZuStringT<"/admin">;
  using Headers = ZhttpHeaders("authorization", "if-match", "if-none-match",
    "idempotency-key", "content-type", "content-length");
  using Responses = AdminResponses;
  const String &queryObject(const Call *call) const { return call->query; }
  const String &bodyObject(const Call *call) const { return call->body; }
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "authorization") l(this->object->authorization);
    else if constexpr (Key{}() == "if-match") l(this->object->ifMatch);
    else if constexpr (Key{}() == "if-none-match") l(this->object->ifNoneMatch);
    else if constexpr (Key{}() == "idempotency-key")
      l(this->object->idempotence);
    else if constexpr (Key{}() == "content-type") l("application/json");
    else Base::template header<Key>(ZuFwd<L>(l));
  }
};

struct AdminGET : AdminBuilder<AdminGET, Zhttp::Method::GET, false> { };
struct AdminPOST : AdminBuilder<AdminPOST, Zhttp::Method::POST, true> { };
struct AdminPUT : AdminBuilder<AdminPUT, Zhttp::Method::PUT, true> { };
struct AdminPATCH : AdminBuilder<AdminPATCH, Zhttp::Method::PATCH, true> { };
struct AdminDELETE : AdminBuilder<AdminDELETE, Zhttp::Method::DELETE, false> { };

using ClientRequests = ZuTypeList<MetadataBuilder, TokenBuilder,
  AdminGET, AdminPOST, AdminPUT, AdminPATCH, AdminDELETE>;
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

struct Pool;
template <typename Heap> class Pool_;
ZmPQueueDerive(ReqBuilderQ, ReqBuilder_,
  ZmPQueueOverlap<false, ZmPQueueNode<ReqBuilder_,
    ZmPQueueHeapID<"zum.ReqBuilder">>>);
using ReqBuilder = ReqBuilderQ::Node;
ZuDerive(TxQ, (ZmPQTx<Pool, ReqBuilderQ, ZmPQTxOrdered<false>>));

template <typename Heap = ZuVoid>
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

ZuDerive(PoolHeap, (ZmHeap<"zum.Pool", Pool_<>>));
ZuDerive(Pool, (Pool_<PoolHeap>));

class Client : public Zhttp::Client<Client, Pool> {
public:
  template <typename Builder>
  void perform(Result &result, ZmRef<Call> input) {
    // Each attempt owns its completion guard; refresh/retry may reuse input.
    ZmRef<Call> call = new Call{};
    call->query = input->query;
    call->body = input->body;
    call->authorization = input->authorization;
    call->ifMatch = input->ifMatch;
    call->ifNoneMatch = input->ifNoneMatch;
    call->idempotence = input->idempotence;
    call->client = this;
    call->result = &result;
    send_<Builder>(ZuMv(call));
    // Zhttp owns the finite request timeout. Wait for completion before this
    // stack-owned Result can go away, including transport failure/cancellation.
    result.done.wait();
  }
  template <typename Builder>
  void perform(Result &result, String body = {},
      String authorization = {}) {
    ZmRef<Call> call = new Call{};
    call->client = this;
    call->result = &result;
    call->body = ZuMv(body);
    call->authorization = ZuMv(authorization);
    send_<Builder>(ZuMv(call));
    result.done.wait();
  }
  template <typename Builder>
  void performAt(Result &result, const Zhttp::URLView &url,
      String body = {}, String authorization = {}) {
    ZmRef<Call> call = new Call{};
    call->client = this;
    call->result = &result;
    if (url.path) {
      auto path = url.path;
      if (path[0] == '/') path.offset(1);
      call->query = path;
    }
    if (url.hasQuery) call->query << '?' << url.query;
    call->body = ZuMv(body);
    call->authorization = ZuMv(authorization);
    send_<Builder>(ZuMv(call));
    result.done.wait();
  }
private:
  template <typename Builder>
  void send_(ZmRef<Call> call) {
    txRun(0, [this, call = ZuMv(call)]() mutable {
      ZmRef<ReqBuilder> request = new ReqBuilder{};
      request->id = m_id++;
      request->template init<Builder>(call.ptr());
      if (!this->send(0, ZuMv(request))) call->finish(0);
    });
  }
  uint64_t m_id = 0;
};

static bool initClient(Client &client, ZiMultiplex &mx,
    const Zhttp::URLView &url, ZuCSpan caPath)
{
  bool secure = url.scheme == Zhttp::Scheme::https;
  auto config = Zhttp::Config().links(1).concurrency(1).linkMax(1)
    .requestTimeout(ClientTimeout).retainedBodyMax(BodyMax)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .secure(secure).tcp(true).tls(secure).quic(false);
  if (!client.init(Zhttp::HubConfig{&mx, "rx", "tx"}, 1,
      config, Zhttp::TCPConfig{}, Zhttp::H2Config{}.caPath(caPath),
      Zhttp::QUICConfig{})) return false;
  if (client.pool(0, Zhttp::Destination{url.origin()}) && client.start())
    return true;
  client.stop();
  client.final();
  return false;
}

template <typename Heap = ZuVoid>
struct CallbackData_ : public Heap, public ZmObject  {
  String data;
  CallbackData_ &operator =(ZuSpan<uint8_t> data_) { data = data_; return *this; }
};
ZuDerive(CallbackDataHeap, (ZmHeap<"Zum.zum.CallbackData", CallbackData_<>>));
ZuDerive(CallbackData, (CallbackData_<CallbackDataHeap>));

template <typename Heap = ZuVoid>
struct CallbackBody_ : public Heap, public ZmObject  {
  String data;
};
ZuDerive(CallbackBodyHeap, (ZmHeap<"Zum.zum.CallbackBody", CallbackBody_<>>));
ZuDerive(CallbackBody, (CallbackBody_<CallbackBodyHeap>));

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
      auto span = request.object->data.span();
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

static bool tokenJSON(String &json, ZumVaultClient::Credential &tokens)
{
  if (json.length() > BodyMax) return false;
  if (!json.mutable_()) json.length(json.length());
  auto parsed = ZfJSON::scan({json.data(), json.length()});
  if (parsed.p<0>() != int(json.length()) || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 || !roots[0]->has<ZfJSON::AnyNode::Object>())
    return false;
  auto handler = ZfJSON::handler<Token>(roots[0]);
  if (!handler.valid) return false;
  auto wire = handler.ctor();
  if (!wire.accessToken ||
      !ZuICmp<ZuCSpan>::equals(wire.tokenType, "Bearer")) return false;
  clearTokens(tokens);
  tokens.accessToken = ZuMv(wire.accessToken);
  tokens.refreshToken = ZuMv(wire.refreshToken);
  tokens.scope = ZuMv(wire.scope);
  return true;
}

static bool metadataJSON(String &json, ZuCSpan issuerURL, Metadata &metadata)
{
  if (json.length() > BodyMax) return false;
  if (!json.mutable_()) json.length(json.length());
  auto parsed = ZfJSON::scan(json);
  if (parsed.p<0>() != int(json.length()) || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1) return false;
  auto handler = ZfJSON::handler<Metadata>(roots[0]);
  if (!handler.valid) return false;
  metadata = handler.ctor();
  return metadata.issuerURL == issuerURL && metadata.authorizationEndpoint &&
    metadata.tokenEndpoint && metadata.jwksURI &&
    metadata.revocationEndpoint &&
    contains(metadata.responseTypesSupported, "code") &&
    contains(metadata.grantTypesSupported, "authorization_code") &&
    contains(metadata.grantTypesSupported, "refresh_token") &&
    contains(metadata.codeChallengeMethods, "S256");
}

static bool saveTokens(const ZumVaultClient::Credential &tokens)
{
  return !ZumVaultClient::save(tokens).is<ZeException>();
}

static ZuPtr<ZfJSON::AnyNode> jsonObject(String &json)
{
  if (!json || !json.mutable_()) json.length(json.length());
  auto parsed = ZfJSON::scan({json.data(), json.length()});
  if (parsed.p<0>() != int(json.length()) || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return {};
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 || !roots[0]->has<ZfJSON::AnyNode::Object>())
    return {};
  return ZuMv(roots[0]);
}

static ZfJSON::AnyNode *field(
    const ZfJSON::AnyNode::Object &object, ZuCSpan name)
{
  for (auto &item: object) if (item.p<0>() == name) return item.p<1>();
  return nullptr;
}

static bool scalar(const ZfJSON::AnyNode *node, String &value)
{
  if (!node) return false;
  if (node->has<ZfJSON::AnyNode::String>())
    value = node->data<ZfJSON::AnyNode::String>();
  else if (node->has<ZfJSON::AnyNode::Number>())
    value = node->data<ZfJSON::AnyNode::Number>();
  else if (node->has<ZfJSON::AnyNode::True>()) value = "true";
  else if (node->has<ZfJSON::AnyNode::False>()) value = "false";
  else return false;
  return true;
}

static bool loadTokens(ZumVaultClient::Credential &tokens)
{
  return !ZumVaultClient::load(tokens).is<ZeException>();
}

static bool pathParameter(ZuCSpan pattern, ZuCSpan name)
{
  String token{"{"};
  token << name << '}';
  return pattern.find(token) >= 0;
}

static bool prepareRequest(const Zum::MgmtRoute &route, String &source,
    ZmRef<Call> &call, const Options &options)
{
  auto root = jsonObject(source);
  if (!root) return false;
  auto object = &root->data<ZfJSON::AnyNode::Object>();
  String path{route.path};
  for (;;) {
    auto begin = path.find("{");
    if (begin < 0) break;
    auto rest = path.cspan().offset(unsigned(begin) + 1);
    auto end = rest.find("}");
    if (end < 0) return false;
    ZuCSpan name{rest.data(), unsigned(end)};
    String value, encoded;
    if (!scalar(field(*object, name), value)) return false;
    ZfURI::PathQuote::quote(encoded, value);
    String next{ZuCSpan{path.data(), unsigned(begin)}};
    next << encoded << rest.offset(unsigned(end) + 1);
    path = ZuMv(next);
  }
  call = new Call{};
  call->query = path.cspan().offset(sizeof("/admin") - 1);
  call->ifMatch = options.ifMatch;
  call->ifNoneMatch = options.ifNoneMatch;
  call->idempotence = options.idempotence;
  if (Zum::managementNeedsIdempotency(route.op) && !call->idempotence) {
    Ztls::Random rng;
    Bytes random;
    // 128 random bits per invocation; the same Call survives token refresh.
    random.length(16, false);
    if (!rng.init() || !rng.random(random)) return false;
    call->idempotence = encode(random);
  }

  if (route.method == Zhttp::Method::GET) {
    bool first = true;
    for (auto &item: *object) {
      if (pathParameter(route.path, item.p<0>())) continue;
      auto emit = [&call, &first, &item](const ZfJSON::AnyNode *node) {
	String value;
	if (!scalar(node, value))
	  ZfJSON::save(value, ZfJSON::Union<>{node});
	call->query << (first ? '?' : '&');
	first = false;
	ZfURI::PathQuote::quote(call->query, item.p<0>());
	call->query << '=';
	ZfURI::PathQuote::quote(call->query, value);
      };
      if (item.p<1>()->has<ZfJSON::AnyNode::Array>())
	for (auto &value: item.p<1>()->data<ZfJSON::AnyNode::Array>())
	  emit(value);
      else emit(item.p<1>());
    }
  } else if (route.method != Zhttp::Method::DELETE) {
    ZuPtr<ZfJSON::AnyNode> body =
      new ZfJSON::Node<ZfJSON::AnyNode::Object>{};
    auto &fields = body->data<ZfJSON::AnyNode::Object>();
    for (auto &item: *object) {
      if (pathParameter(route.path, item.p<0>())) continue;
      fields.push(ZfJSON::AnyNode::Field{
	item.p<0>(), ZuMv(item.p<1>())});
    }
    ZfJSON::save(call->body, ZfJSON::Union<>{
      static_cast<const ZfJSON::AnyNode *>(body.ptr())});
  }
  return true;
}

static void performAdmin(Client &client, const Zum::MgmtRoute &route,
    Result &result, ZmRef<Call> call)
{
  switch (route.method) {
    case Zhttp::Method::GET:
      client.perform<AdminGET>(result, ZuMv(call)); return;
    case Zhttp::Method::POST:
      client.perform<AdminPOST>(result, ZuMv(call)); return;
    case Zhttp::Method::PUT:
      client.perform<AdminPUT>(result, ZuMv(call)); return;
    case Zhttp::Method::PATCH:
      client.perform<AdminPATCH>(result, ZuMv(call)); return;
    case Zhttp::Method::DELETE:
      client.perform<AdminDELETE>(result, ZuMv(call)); return;
    default: return;
  }
}

static void openBrowser(ZuCSpan url, bool noBrowser)
{
  std::cerr << "Open this URL in a browser:\n" << url << "\n\n" << std::flush;
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

int main(int argc, char **argv)
{
  Options options;
  ZfCLI::Parser<Options> parser;
  try {
    ZfCLI::InArgv<> input(argc, argv);
    parser.scanArgv(input.argv);
    ZfCLI::handler<Options>(parser.root).load(options);
  } catch (const ZeException &e) {
    std::cerr << e << '\n'; usage();
  }
  if (options.help) usage(0, &parser);
  if (!options.command) usage();
  bool loggingIn = options.command == "login";
  int op = loggingIn ? -1 : ZumCLI::operation(parser);
  const auto *route = Zum::managementRoute(op);
  ZumCLI::String request, error;
  if (!loggingIn && (!route || !ZumCLI::request(parser, op, request, error))) {
    std::cerr << "zum: " << (route ? error.cspan() :
      ZuCSpan{"unknown command"}) << '\n';
    usage();
  }
  if (loggingIn && parser.argc != 2) usage();
  if (!options.config) options.config = Zt::getpath("ZUM_CONFIG");
  if (!options.config) {
    auto home = Zt::getpath("ZUM_HOME");
    if (home && *home) options.config = home;
    else if (auto base = Zt::getpath("HOME"))
      options.config << base << "/.zum";
    if (!options.config) usage();
    options.config << "/zum.cf";
  }

  Config config;
  try {
    if (!loadConfig(options.config, config))
      throw ZeEXCEPT(Fatal, "zum", "configuration load failed");
  } catch (const ZeException &e) {
    std::cerr << e << '\n';
    return 1;
  }

  String discoveryURL;
  Zhttp::URL issuerURL;
  if (!metadataURL(config.issuerURL, config.loopbackTest,
      discoveryURL, issuerURL)) {
    std::cerr << "zum: invalid application issuerURL\n";
    return 1;
  }
  Zhttp::URL managementURL;
  if (!endpointURL(config.managementURL, config.loopbackTest,
      managementURL) || managementURL.url().hasQuery ||
      (managementURL.url().path && managementURL.url().path != "/")) {
    std::cerr << "zum: managementURL must be an HTTP(S) origin\n";
    return 1;
  }

  ZiLog::init("zum");
  ZiLog::level(Ze::Warning);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZmTrap::sigintFn(interrupted);
  ZmTrap::trap();

  ZiMultiplex mx{mxParams()};
  if (!mx.start()) return 1;

  Client issuerClient;
  if (!initClient(issuerClient, mx, issuerURL.url(), config.caPath)) {
    std::cerr << "zum: issuer HTTP client start failed\n";
    mx.stop(); ZiLog::stop(); return 1;
  }

  Zhttp::URL discoveryParsed{discoveryURL};
  Result discovery;
  issuerClient.performAt<MetadataBuilder>(
    discovery, discoveryParsed.url());
  Metadata metadata;
  bool discovered = discovery.status == 200 &&
    metadataJSON(discovery.body, config.issuerURL, metadata);
  Zhttp::URL authorizationURL, tokenURL, jwksURL, revocationURL;
  discovered = discovered && endpointURL(metadata.authorizationEndpoint,
      config.loopbackTest, authorizationURL) &&
    endpointURL(metadata.tokenEndpoint, config.loopbackTest, tokenURL) &&
    endpointURL(metadata.jwksURI, config.loopbackTest, jwksURL) &&
    endpointURL(metadata.revocationEndpoint,
      config.loopbackTest, revocationURL);
  if (!discovered) {
    std::cerr << "zum: OAuth discovery failed\n";
    issuerClient.stop(); issuerClient.final();
    mx.stop(); ZiLog::stop(); return 1;
  }

  Client tokenClient;
  Client managementClient;
  if (!initClient(tokenClient, mx, tokenURL.url(), config.caPath)) {
    std::cerr << "zum: token HTTP client start failed\n";
    issuerClient.stop(); issuerClient.final();
    mx.stop(); ZiLog::stop(); return 1;
  }
  if (!initClient(managementClient, mx,
      managementURL.url(), config.caPath)) {
    std::cerr << "zum: management HTTP client start failed\n";
    tokenClient.stop(); tokenClient.final();
    issuerClient.stop(); issuerClient.final();
    mx.stop(); ZiLog::stop(); return 1;
  }

  ZumVaultClient::Credential tokens{config.issuerURL,
    config.managementURL, config.clientID};
  auto login = [&config, &mx, &options, &metadata, &authorizationURL,
      &tokenURL, &tokenClient, &tokens]() {
    Ztls::Random rng;
    Bytes random;
    random.length(32, false);
    bool ok = rng.init() && rng.random(random);
    String verifier = ok ? encode(random) : String{};
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
    callback.expectedState = state;
    Zhttp::Server<CallbackApp> callbackServer;
    auto callbackConfig = Zhttp::ServerConfig().localIP(ZiIP("127.0.0.1"))
      .port(config.callbackPort).idleTimeout(ClientTimeout)
      .retainedBodyMax(BodyMax).tcp();
    bool callbackInited = callbackServer.init(
      Zhttp::HubConfig{&mx, "cb-rx", "cb-tx"}, ZuMv(callbackConfig),
      &callback);
    bool callbackUp = callbackInited && callbackServer.start();
    if (!callbackUp) {
      std::cerr << "zum: callback listener start failed\n";
      if (callbackInited) (void)callbackServer.stop();
      callbackServer.final();
      return false;
    }
    String redirect{"http://127.0.0.1:"};
    redirect << config.callbackPort << "/callback";
    String authorize{metadata.authorizationEndpoint};
    authorize << (authorizationURL.url().hasQuery ? '&' : '?') <<
      "response_type=code&" << formField("client_id", config.clientID) <<
      '&' << formField("redirect_uri", redirect) << '&' <<
      formField("scope", config.scope);
    authorize << "&state=" << state << "&code_challenge=" << challenge <<
      "&code_challenge_method=S256";
    if (ok) openBrowser(authorize, options.noBrowser);
    if (ok) ok = !callbackDone.timedwait(
      Zm::now() + ZuTime{int64_t(config.loginTimeout)});
    ok = ok && callback.code && callback.state == state && !callback.error;
    if (!ok && callback.error)
      std::cerr << "zum: authorization failed\n";
    if (!ok && !callback.error)
      std::cerr << "zum: authorization callback timed out or failed\n";
    if (ok) {
      String form = formField("grant_type", "authorization_code");
      form << '&' << formField("code", callback.code) << '&' <<
	formField("client_id", config.clientID) << '&' <<
	formField("redirect_uri", redirect) << '&' <<
	formField("code_verifier", verifier);
      Result result;
      tokenClient.performAt<TokenBuilder>(
        result, tokenURL.url(), ZuMv(form));
      ok = result.status == 200 && tokenJSON(result.body, tokens);
      if (!ok)
	std::cerr << "zum: code redemption failed: " << result.body << '\n';
    }
    if (verifier && verifier.mutable_())
      ZuClear(verifier);
    (void)callbackServer.stop();
    callbackServer.final();
    return ok;
  };

  bool ok = true;
  if (loggingIn) {
    ok = login();
    if (ok && tokens.refreshToken)
      ok = saveTokens(tokens);
    if (ok) std::cerr << "authenticated\n";
  } else {
    String source{ZuMv(request)};
    ZmRef<Call> call;
    ok = prepareRequest(*route, source, call, options);
    if (!ok) std::cerr << "zum: could not prepare request\n";
    if (ok && !loadTokens(tokens)) ok = login();
    if (ok) {
      call->authorization = "Bearer ";
      call->authorization << tokens.accessToken;
      Result result;
      performAdmin(managementClient, *route, result, call);
      if (result.status == 401 && tokens.refreshToken) {
	String form = formField("grant_type", "refresh_token");
	form << '&' << formField("refresh_token", tokens.refreshToken) << '&' <<
	  formField("client_id", config.clientID);
	Result refreshed;
	tokenClient.performAt<TokenBuilder>(
	  refreshed, tokenURL.url(), ZuMv(form));
	ZumVaultClient::Credential next;
	if (refreshed.status == 200 && tokenJSON(refreshed.body, next)) {
	  clearTokens(tokens);
	  tokens.accessToken = ZuMv(next.accessToken);
	  tokens.refreshToken = ZuMv(next.refreshToken);
	  tokens.scope = ZuMv(next.scope);
	  call->authorization = "Bearer ";
	  call->authorization << tokens.accessToken;
	  result.body = String{};
	  result.status = 0;
	  performAdmin(managementClient, *route, result, call);
	}
      }
      bool saved = !tokens.refreshToken || saveTokens(tokens);
      ok = result.status >= 200 && result.status < 300;
      std::cout << result.body;
      if (!result.body || result.body[result.body.length() - 1] != '\n')
	std::cout << '\n';
      if (!ok) {
	std::cerr << "zum: HTTP " << result.status << '\n';
	if (call->idempotence)
	  std::cerr << "zum: retry key: --idempotence " <<
	    call->idempotence << '\n';
      }
      if (!saved) {
	std::cerr << "zum: could not persist credentials\n";
	ok = false;
      }
    }
  }
  clearTokens(tokens);

  managementClient.stop();
  managementClient.final();
  tokenClient.stop();
  tokenClient.final();
  issuerClient.stop();
  issuerClient.final();
  mx.stop();
  ZiLog::stop();
  return ok ? 0 : 1;
}
