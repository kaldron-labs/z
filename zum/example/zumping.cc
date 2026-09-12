//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Minimal native OAuth client using a browser/passkey flow and Zrest

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

ZuDerive(String, ZtString<ZtStringHeapID<"zumping.String">>);
ZuDerive(Bytes, (ZtArray<uint8_t, ZtArrayHeapID<"zumping.Bytes">>));

using ObjectHeap = ZmVHeap<
  "zumping.Object", 0, ZmVHeap_DefltMax, alignof(max_align_t)>;
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
  CallbackPort = 8081
};

struct Options {
  String	config;
  bool		noBrowser = false;
  bool		help = false;
};

ZfStruct(, (Options, CLI),
  (((config), (CLI::Long<"config">)), (String)),
  (((noBrowser), (CLI::Long<"no-browser">)), (Bool)),
  (((help), (CLI::Flag<'h'>, CLI::Long<"help">)), (Bool)));

static void usage(int code = 1)
{
  std::cerr <<
    "Usage: zumping --config FILE [--no-browser]\n";
  ::exit(code);
}

struct Config {
  String serviceURL;
  String clientID;
  String scope{"ping"};
  uint32_t callbackPort = CallbackPort;
  uint32_t loginTimeout = 180;
};
ZfStruct(, (Config, Cf),
  (((serviceURL), (Required)), (String)),
  (((clientID), (Required)), (String)),
  (((scope)), (String, "ping")),
  (((callbackPort), ((Range<1, 65535>))), (UInt32, CallbackPort)),
  (((loginTimeout), ((Range<1, 3600>))), (UInt32, 180)));

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
  return config.serviceURL && config.clientID;
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

template <unsigned Status_>
struct HTTPData : public Object {
  enum { Status = Status_ };
  String data;
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

struct TokenBuilder : public Zrest::ReqBuilder<TokenBuilder, Call> {
  using Base = Zrest::ReqBuilder<TokenBuilder, Call>;
  using Base::header;
  enum { Method = Zhttp::Method::POST, Exact = 1,
    Body = Zrest::BodyPolicy::Raw };
  using Path = ZuStringT<"/token">;
  using Headers = ZhttpHeaders(
    ("content-type", "application/x-www-form-urlencoded"), "content-length");
  using Responses = ZuTypeList<TokenOK, BadRequest, TokenUnauthorized,
    ServerError>;
  const String &bodyObject(const Call *call) const { return call->body; }
};

struct ResourceBuilder : public Zrest::ReqBuilder<ResourceBuilder, Call> {
  using Base = Zrest::ReqBuilder<ResourceBuilder, Call>;
  using Base::header;
  enum { Exact = 1 };
  using Path = ZuStringT<"/ping">;
  using Headers = ZhttpHeaders("authorization");
  struct OK : public Zrest::ResParser<OK, OKData> {
    enum { Status = 200, Body = Zrest::BodyPolicy::Raw };
  };
  struct Unauthorized : public Zrest::ResParser<Unauthorized,
      UnauthorizedData> {
    enum { Status = 401, Body = Zrest::BodyPolicy::Raw };
  };
  struct Forbidden : public Zrest::ResParser<Forbidden, ForbiddenData> {
    enum { Status = 403, Body = Zrest::BodyPolicy::Raw };
  };
  using Responses = ZuTypeList<OK, Unauthorized, Forbidden, ServerError>;
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "authorization") l(this->object->authorization);
    else Base::template header<Key>(ZuFwd<L>(l));
  }
};

using ClientRequests = ZuTypeList<TokenBuilder, ResourceBuilder>;

struct ReqBuilder_ : public ZmObject,
    public Zrest::MReqBuilder<ClientRequests> {
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

struct ResParser : public Zrest::MResParser<ReqBuilder_> { };

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

class Client : public Zhttp::Client<Client, Pool> {
public:
  template <typename Builder>
  void perform(Result &result, String body = {},
      String authorization = {}) {
    ZmRef<Call> call = new Call{};
    call->client = this;
    call->result = &result;
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
  uint64_t m_id = 0;
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

struct CallbackParser : public Zrest::MReqParser<CallbackRequests> {
  using Base = Zrest::MReqParser<CallbackRequests>;
  void init(CallbackApp &app_) { app = &app_; }
  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    if (!Base::operation(method, target)) return false;
    u.dispatch([this](auto, auto &request) { request.app = app; });
    return true;
  }
  CallbackApp *app = nullptr;
};

using CallbackBuilder = Zrest::MResBuilder<CallbackParser>;
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
  if (!wire.accessToken ||
      !ZuICmp<ZuCSpan>::equals(wire.tokenType, "Bearer")) return false;
  Tokens next{ZuMv(wire.accessToken), ZuMv(wire.refreshToken),
    ZuMv(wire.scope)};
  clearTokens(tokens);
  tokens = ZuMv(next);
  return true;
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

int main(int argc, char **argv)
{
  Options options;
  try { argc = ZfCLI::load(options, argc, argv); }
  catch (const ZeException &e) { std::cerr << e << '\n'; usage(); }
  if (options.help) usage(0);
  if (argc != 1 || !options.config) usage();
  Config config;
  if (!loadConfig(options.config, config)) {
    std::cerr << "zumping: invalid configuration\n"; return 1;
  }

  Zhttp::URL urlStorage;
  auto urlError = urlStorage.assign(config.serviceURL);
  if (!urlError.ok()) { std::cerr << "zumping: invalid server URL\n"; return 1; }
  Zhttp::URLView url = urlStorage.url();
  if (!url.host || (url.path && url.path != "/") ||
      url.hasQuery || url.hasFragment) {
    std::cerr << "zumping: server URL must be an HTTP(S) origin\n";
    return 1;
  }

  ZiLog::init("zumping");
  ZiLog::level(Ze::Warning);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZmTrap::sigintFn(interrupted);
  ZmTrap::trap();

  ZiMultiplex mx{mxParams()};
  if (!mx.start()) return 1;

  Client client;
  bool secure = url.scheme == Zhttp::Scheme::https;
  auto clientConfig = Zhttp::Config().links(1).concurrency(1).linkMax(1)
    .requestTimeout(ClientTimeout).retainedBodyMax(BodyMax)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .secure(secure).tcp(true).tls(secure).quic(false);
  bool clientInited = client.init(Zhttp::HubConfig{&mx, "rx", "tx"}, 1,
    clientConfig, Zhttp::TCPConfig{}, Zhttp::H2Config{}, Zhttp::QUICConfig{});
  bool poolInited = clientInited && client.pool(0,
    Zhttp::Destination{url.host, url.port, url.ipv6Literal});
  bool clientUp = poolInited && client.start();
  if (!clientUp) {
    std::cerr << "zumping: HTTP client start failed\n";
    if (clientInited) client.stop();
    client.final(); mx.stop(); ZiLog::stop(); return 1;
  }

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
    Zhttp::HubConfig{&mx, "cb-rx", "cb-tx"}, ZuMv(callbackConfig), &callback);
  bool callbackUp = callbackInited && callbackServer.start();
  if (!callbackUp) {
    std::cerr << "zumping: callback listener start failed\n";
    if (callbackInited) (void)callbackServer.stop();
    callbackServer.final(); client.stop(); client.final();
    mx.stop(); ZiLog::stop(); return 1;
  }

  String redirect;
  redirect << "http://127.0.0.1:" << config.callbackPort << "/callback";
  String origin{config.serviceURL};
  if (origin && origin[origin.length() - 1] == '/') origin.length(origin.length() - 1);
  String authorize;
  authorize << origin << "/authorize?response_type=code&" <<
    formField("client_id", config.clientID) << '&' <<
    formField("redirect_uri", redirect) << '&' << formField("scope", config.scope) <<
    "&state=" << state <<
    "&code_challenge=" << challenge << "&code_challenge_method=S256";
  if (ok) openBrowser(authorize, options.noBrowser);
  if (!ok) std::cerr << "zumping: random source failed\n";

  if (ok) ok = !callbackDone.timedwait(
    Zm::now() + ZuTime{int64_t(config.loginTimeout)});
  ok = ok && callback.code && callback.state == state && !callback.error;
  if (!ok) std::cerr << "zumping: authorization failed or timed out\n";

  Tokens tokens;
  if (ok) {
    String form;
    form << "grant_type=authorization_code&" << formField("code", callback.code) <<
      '&' << formField("client_id", config.clientID) << '&' <<
      formField("redirect_uri", redirect) << '&' << formField("code_verifier", verifier);
    Result result;
    client.perform<TokenBuilder>(result, ZuMv(form));
    ok = result.status == 200 && tokenJSON(result.body, tokens);
    if (!ok) std::cerr << "zumping: code redemption failed\n";
  }

  if (ok) {
    Result result;
    String bearer{"Bearer "};
    bearer << tokens.accessToken;
    client.perform<ResourceBuilder>(result, {}, ZuMv(bearer));
    ok = result.status == 200;
    if (ok) std::cout << result.body << '\n';
  }

  if (ok && tokens.refreshToken) {
    String form{"grant_type=refresh_token&"};
    form << formField("refresh_token", tokens.refreshToken) << '&' <<
      formField("client_id", config.clientID);
    Result result;
    client.perform<TokenBuilder>(result, ZuMv(form));
    Tokens rotated;
    ok = result.status == 200 && tokenJSON(result.body, rotated);
    if (ok) {
      clearTokens(tokens);
      tokens = ZuMv(rotated);
      std::cout << "refresh token rotated\n";
    } else {
      std::cerr << "zumping: refresh failed\n";
    }
  }

  if (ok) {
    Result result;
    String bearer{"Bearer "};
    bearer << tokens.accessToken;
    client.perform<ResourceBuilder>(result, {}, ZuMv(bearer));
    ok = result.status == 200;
    if (ok) std::cout << result.body << '\n';
  }
  clearTokens(tokens);

  (void)callbackServer.stop();
  callbackServer.final();
  client.stop();
  client.final();
  mx.stop();
  ZiLog::stop();
  return ok ? 0 : 1;
}
