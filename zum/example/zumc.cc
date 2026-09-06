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
#include <zlib/ZuLib.hh>

#include <zlib/ZmList.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmPQueue.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZfCLI.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/ZfURI.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpServer.hh>
#include <zlib/ZrestClient.hh>
#include <zlib/ZrestServer.hh>

#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsRandom.hh>

ZuDerive(String, ZtString<ZtStringHeapID<"zumc.String">>);
ZuDerive(Bytes, (ZtArray<uint8_t, ZtArrayHeapID<"zumc.Bytes">>));

struct Tokens {
  String	accessToken;
  String	refreshToken;
  String	scope;
};

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
  String	url{"http://localhost:8080/"};
  uint32_t	callbackPort = CallbackPort;
  bool		noBrowser = false;
  bool		help = false;
};

ZfStruct((Options, CLI),
  (((callbackPort), (CLI::Long<"callback-port">)), (UInt32, CallbackPort)),
  (((noBrowser), (CLI::Long<"no-browser">)), (Bool)),
  (((url), (CLI::Arg<1>)), (String, "http://localhost:8080/")),
  (((help), (CLI::Flag<'h'>, CLI::Long<"help">)), (Bool)));

static void usage(int code = 1)
{
  std::cerr <<
    "Usage: zumc [OPTION]... [AUTHORIZATION-SERVER]\n\n"
    "  --callback-port=N  loopback callback port, default 8081\n"
    "  --no-browser       print the authorization URL without opening it\n"
    "  -h, --help         show help\n\n"
    "The authorization server defaults to http://localhost:8080/.\n";
  ::exit(code);
}

static String encode(ZuBSpan data)
{
  String s;
  s.length(ZuBase64URL::enclen(data.length()));
  s.length(ZuBase64URL::encode({
    reinterpret_cast<uint8_t *>(s.data()), s.length()}, data));
  return s;
}

template <unsigned Status_>
struct HTTPData : public ZmObject {
  enum { Status = Status_ };
  String data;
  HTTPData &operator =(ZuSpan<uint8_t> data_) {
    data = ZuBSpan{data_};
    return *this;
  }
};

struct Result {
  ZmSemaphore	done;
  String	body;
  unsigned	status = 0;
};

class Client;

struct Call : public ZmObject {
  Client	*client = nullptr;
  Result	*result = nullptr;
  String	body;
  String	authorization;

  template <typename Link, typename Response>
  void process(Link *, const Response *response) const {
    result->status = Response::Status;
    result->body = response->data;
    result->done.post();
  }
  template <typename Link> void failed(Link *) const {
    result->done.post();
  }
};

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
  using Path = ZuStringT<"/resource">;
  using Headers = ZhttpHeaders("authorization");
  struct OK : public Zrest::ResParser<OK, OKData> {
    enum { Status = 200, Body = Zrest::BodyPolicy::Raw };
  };
  struct Unauthorized : public Zrest::ResParser<Unauthorized,
      UnauthorizedData> {
    enum { Status = 401, Body = Zrest::BodyPolicy::Raw };
  };
  using Responses = ZuTypeList<OK, Unauthorized>;
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
};

struct ResParser : public Zrest::MResParser<ReqBuilder_> { };

class Pool;
template <typename Heap = ZuVoid> class Pool_;
ZuDerive(ReqBuilderQ, (ZmPQueue<ReqBuilder_,
  ZmPQueueOverlap<false, ZmPQueueNode<ReqBuilder_,
    ZmPQueueHeapID<"zum.ReqBuilder">>>>));
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
      this->send(0, ZuMv(request));
    });
    result.done.wait();
  }
private:
  uint64_t m_id = 0;
};

struct CallbackData : public ZmObject {
  ZuSpan<uint8_t> data;
  CallbackData &operator =(ZuSpan<uint8_t> data_) { data = data_; return *this; }
};

struct CallbackBody : public ZmObject {
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
ZuDerive(CallbackBuilderQ, (ZmList<CallbackBuilder_,
  ZmListNode<CallbackBuilder_, ZmListHeapID<"zum.CallbackBuilder">>>));
using CallbackBuilderNode = CallbackBuilderQ::Node;

class CallbackApp {
public:
  using Parser = CallbackParser;
  using ResBuilderQ = CallbackBuilderQ;

  template <typename Link>
  void callback(Link *link, const CallbackReq &request, bool ok)
  {
    auto span = request.object->data;
    if (span && span[0] == '?') span.offset(1);
    String query{ZuBSpan{span}};
    if (!query.mutable_()) query.length(query.length());
    unsigned seen = 0;
    if (ok) ok = ZfURI::scanForm({query.data(), query.length()},
      ZfURI::FormLimits{3, 16, 4096}, [this, &seen](
          ZuSpan<char> name, ZuSpan<char> value) {
        if (name == "code" && !(seen & 1U)) {
          code = value; seen |= 1U; return true;
        }
        if (name == "state" && !(seen & 2U)) {
          state = value; seen |= 2U; return true;
        }
        if (name == "error" && !(seen & 4U)) {
          error = value; seen |= 4U; return true;
        }
        return false;
      }) == ZfURI::FormResult::OK;
    ZmRef<CallbackBody> object = new CallbackBody{};
    object->data = ok && code ?
      String{"<!doctype html><h1>Authorized</h1><p>You may close this window.</p>"} :
      String{"<!doctype html><h1>Authorization failed</h1>"};
    ZmRef<CallbackBuilderNode> response = new CallbackBuilderNode{};
    response->template init<CallbackOK, CallbackReq>(object.ptr());
    link->send(ZuMv(response));
    callbackDone.post();
  }

  void listening(int, unsigned) { }
  void listenFailed(int, bool) { callbackDone.post(); }
  void connected(int) { }
  void disconnected(int) { }

  String	code;
  String	state;
  String	error;
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
  if (!json.mutable_()) json.length(json.length());
  auto parsed = ZfJSON::scanStrict({json.data(), json.length()},
    ZfJSON::ScanLimits{BodyMax, 3, 32, BodyMax});
  if (!parsed || !parsed.root->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.root->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 || !roots[0]->has<ZfJSON::AnyNode::Object>())
    return false;
  unsigned seen = 0;
  for (auto &field: roots[0]->data<ZfJSON::AnyNode::Object>()) {
    auto value = field.p<1>().ptr();
    if (field.p<0>() == "access_token" && value->has<ZfJSON::AnyNode::String>()) {
      tokens.accessToken = value->data<ZfJSON::AnyNode::String>(); seen |= 1U;
    } else if (field.p<0>() == "refresh_token" &&
        value->has<ZfJSON::AnyNode::String>()) {
      tokens.refreshToken = value->data<ZfJSON::AnyNode::String>(); seen |= 2U;
    } else if (field.p<0>() == "scope" && value->has<ZfJSON::AnyNode::String>()) {
      tokens.scope = value->data<ZfJSON::AnyNode::String>();
    }
  }
  return (seen & 3U) == 3U;
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
  if (argc < 1 || argc > 2 || !options.callbackPort ||
      options.callbackPort > 65535) usage();

  Zhttp::URL urlStorage;
  auto urlError = urlStorage.assign(options.url);
  if (!urlError.ok()) { std::cerr << "zumc: invalid server URL\n"; return 1; }
  Zhttp::URLView url = urlStorage.url();
  if (!url.host || (url.path && url.path != "/") ||
      url.hasQuery || url.hasFragment) {
    std::cerr << "zumc: server URL must be an HTTP(S) origin\n";
    return 1;
  }

  ZiLog::init("zumc");
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
    .secure(secure).tcp(true).tls(secure).quic(false);
  bool clientInited = client.init(Zhttp::HubConfig{&mx, "rx", "tx"}, 1,
    clientConfig, Zhttp::TCPConfig{}, Zhttp::H2Config{}, Zhttp::QUICConfig{});
  bool poolInited = clientInited && client.pool(0,
    Zhttp::Destination{url.host, url.port, url.ipv6Literal});
  bool clientUp = poolInited && client.start();
  if (!clientUp) {
    std::cerr << "zumc: HTTP client start failed\n";
    if (clientInited) client.stop();
    client.final(); mx.stop(); ZiLog::stop(); return 1;
  }

  CallbackApp callback;
  Zhttp::Server<CallbackApp> callbackServer;
  auto callbackConfig = Zhttp::ServerConfig().localIP(ZiIP("127.0.0.1"))
    .port(options.callbackPort).idleTimeout(ClientTimeout)
    .retainedBodyMax(BodyMax).tcp();
  bool callbackInited = callbackServer.init(
    Zhttp::HubConfig{&mx, "cb-rx", "cb-tx"}, ZuMv(callbackConfig), &callback);
  bool callbackUp = callbackInited && callbackServer.start();
  if (!callbackUp) {
    std::cerr << "zumc: callback listener start failed\n";
    if (callbackInited) (void)callbackServer.stop();
    callbackServer.final(); client.stop(); client.final();
    mx.stop(); ZiLog::stop(); return 1;
  }

  Ztls::Random rng;
  Bytes random;
  random.length(32, false);
  bool ok = rng.init() && rng.random(random);
  String verifier = ok ? encode(random) : String{};
  uint8_t digest[Ztls::MD<>::Size];
  if (ok) {
    Ztls::MD<> md;
    md.update(ZuBSpan{verifier});
    md.finish(digest);
  }
  String challenge = ok ? encode(digest) : String{};
  random.length(16, false);
  ok = ok && rng.random(random);
  String state = ok ? encode(random) : String{};
  String redirect;
  redirect << "http://127.0.0.1:" << options.callbackPort << "/callback";
  String origin{options.url};
  if (origin && origin[origin.length() - 1] == '/') origin.length(origin.length() - 1);
  String authorize;
  authorize << origin << "/authorize?response_type=code&client_id=zum&redirect_uri="
    "http%3A%2F%2F127.0.0.1%3A" << options.callbackPort <<
    "%2Fcallback&scope=example.use&state=" << state <<
    "&code_challenge=" << challenge << "&code_challenge_method=S256";
  if (ok) openBrowser(authorize, options.noBrowser);
  if (!ok) std::cerr << "zumc: random source failed\n";

  if (ok) callbackDone.wait();
  ok = ok && callback.code && callback.state == state && !callback.error;
  if (!ok && callback.error)
    std::cerr << "zumc: authorization failed: " << callback.error << '\n';

  Tokens tokens;
  if (ok) {
    String form;
    form << "grant_type=authorization_code&code=" << callback.code <<
      "&client_id=zum&redirect_uri=http%3A%2F%2F127.0.0.1%3A" <<
      options.callbackPort << "%2Fcallback&code_verifier=" << verifier;
    Result result;
    client.perform<TokenBuilder>(result, ZuMv(form));
    ok = result.status == 200 && tokenJSON(result.body, tokens);
    if (!ok) std::cerr << "zumc: code redemption failed: " << result.body << '\n';
  }

  if (ok) {
    Result result;
    String bearer{"Bearer "};
    bearer << tokens.accessToken;
    client.perform<ResourceBuilder>(result, {}, ZuMv(bearer));
    ok = result.status == 200;
    std::cout << "access token: " << result.body << '\n';
  }

  if (ok) {
    String form{"grant_type=refresh_token&refresh_token="};
    form << tokens.refreshToken << "&client_id=zum";
    Result result;
    client.perform<TokenBuilder>(result, ZuMv(form));
    Tokens rotated;
    ok = result.status == 200 && tokenJSON(result.body, rotated);
    if (ok) {
      clearTokens(tokens);
      tokens = ZuMv(rotated);
      std::cout << "refresh token rotated\n";
    } else {
      std::cerr << "zumc: refresh failed: " << result.body << '\n';
    }
  }

  if (ok) {
    Result result;
    String bearer{"Bearer "};
    bearer << tokens.accessToken;
    client.perform<ResourceBuilder>(result, {}, ZuMv(bearer));
    ok = result.status == 200;
    std::cout << "refreshed access token: " << result.body << '\n';
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
