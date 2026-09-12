//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Ping service: no database, local credential store or upstream OIDC integration.

#include <iostream>
#include <stdlib.h>

#include <zlib/ZuPercent.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTrap.hh>
#include <zlib/ZfCf.hh>
#include <zlib/ZfCLI.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/ZfURI.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZhttpServer.hh>
#include <zlib/ZrestServer.hh>
#include <zlib/ZumService.hh>

#include "pinghttp.hh"

using Zum::String;
enum { BodyMax = 64U<<10, ServiceSID = 5 };
static ZmSemaphore done;
static void interrupted() { done.post(); }

struct Message {
  String error;
  String reply;
  String status;
};
ZfStruct(, (Message, JSON),
  (((error),		(JSON::Opt)),	(String)),
  (((reply),		(JSON::Opt)),	(String)),
  (((status),		(JSON::Opt)),	(String)));

static String json(Message message)
{
  String body;
  ZfJSON::save(body, message);
  return body;
}

struct Options { String config; bool help = false; };
ZfStruct(, (Options, CLI),
  (((config), (CLI::Long<"config">)), (String)),
  (((help), (CLI::Flag<'h'>, CLI::Long<"help">)), (Bool)));

struct ZumConfig {
  String issuerURL;
  String clientID;
};
ZfStruct(, (ZumConfig, Cf),
  (((issuerURL), (Required)), (String)),
  (((clientID), (Required)), (String)));

struct Config {
  ZumConfig zum;
  String caPath;
  String audience;
  uint64_t audienceID = 0;
  String addr{"127.0.0.1"};
  uint32_t port = 8080;
};
ZfStruct(, (Config, Cf),
  (((zum), (Required)), (UDT)),
  (((caPath)), (String)),
  (((audience), (Required)), (String)),
  (((audienceID), (Required)), (UInt64)),
  (((addr)), (String, "127.0.0.1")),
  (((port), ((Range<1, 65535>))), (UInt32, 8080)));

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
  // TLS terminates at a deployment reverse proxy; this listener is loopback only.
  return config.zum.issuerURL && config.zum.clientID && config.audience &&
    config.audienceID && (config.addr == "127.0.0.1" || config.addr == "::1");
}

static Zum::ServiceManifest manifest(uint64_t audienceID)
{
  return {Zum::ServiceCatalog{
    .actions = {{.name = "ping"}},
    .roles = {{.actions = {"ping"}, .name = "ping"}},
    .scopes = {{.audienceID = audienceID, .name = "ping", .roles = {"ping"}}}
  }, 1};
}

static bool decode(String &out, ZuCSpan value)
{
  out = value;
  using Form = ZuPercent::Codec<ZfURI::PercentQuote<true>>;
  auto result = Form::decode(out.span());
  if (!result) return false;
  out.length(result.out);
  return true;
}

struct AuthorizeFields {
  using Keys = ZuStringTL<"client_id", "redirect_uri", "response_type",
    "scope", "resource", "state", "code_challenge",
    "code_challenge_method", "nonce", "prompt", "max_age">;
};

static bool authorizeInput(ZuCSpan query, Zum::ServiceAuthorizeRequest &input)
{
  constexpr auto matcher = ZuMatcher<AuthorizeFields>();
  if (query && query[0] == '?') query.offset(1);
  unsigned seen = 0;
  while (query) {
    auto amp = query.find("&");
    ZuCSpan part{query.data(), amp >= 0 ? unsigned(amp) : query.length()};
    auto equal = part.find("=");
    if (equal < 0) return false;
    String key, value;
    if (!decode(key, {part.data(), unsigned(equal)}) ||
        !decode(value, part.offset(unsigned(equal) + 1))) return false;
    int field = matcher.exact(key);
    if (field < 0) {
      if (amp < 0) break;
      query.offset(unsigned(amp) + 1);
      continue;
    }
    unsigned bit = 1U<<field;
    if (seen & bit) return false;
    seen |= bit;
    switch (field) {
      case 0: input.clientID = ZuMv(value); break;
      case 1: input.redirectURI = ZuMv(value); break;
      case 2: input.responseType = ZuMv(value); break;
      case 3: input.scope = ZuMv(value); break;
      case 4: input.resource = ZuMv(value); break;
      case 5: input.state = ZuMv(value); input.statePresent = true; break;
      case 6: input.codeChallenge = ZuMv(value); break;
      case 7: input.codeChallengeMethod = ZuMv(value); break;
      case 8: input.nonce = ZuMv(value); input.noncePresent = true; break;
      case 9: input.prompt = ZuMv(value); input.promptPresent = true; break;
      case 10: {
	ZuBox<uint32_t> n;
	if (n.scan(value) != int(value.length()) ||
	    ZuCmp<uint32_t>::null(n)) return false;
	input.maxAge = n;
	input.maxAgePresent = true;
      } break;
    }
    if (amp < 0) break;
    query.offset(unsigned(amp) + 1);
  }
  return (seen & (1U | 2U | 4U | 64U | 128U)) == (1U | 2U | 4U | 64U | 128U);
}

struct RawData : public ZumObject {
  ZuSpan<uint8_t> data;
  RawData &operator =(ZuSpan<uint8_t> value) { data = value; return *this; }
};
struct Reply : public ZumObject { String body; String location; };

template <unsigned Status_>
struct Response : public Zrest::ResBuilder<Response<Status_>, Reply> {
  using Base = Zrest::ResBuilder<Response<Status_>, Reply>;
  using Base::header;
  enum { Status = Status_, Body = Zrest::BodyPolicy::Raw };
  using Headers = ZhttpHeaders(("content-type", "application/json"),
    ("cache-control", "no-store"), "content-length", "location", "www-authenticate");
  const String &bodyObject(const Reply *reply) const { return reply->body; }
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "location") {
      if (this->object->location) l(this->object->location);
    } else if constexpr (Key{}() == "www-authenticate") {
      if constexpr (Status_ == 401) l("Bearer");
    } else Base::template header<Key>(ZuFwd<L>(l));
  }
};
using Responses = ZuTypeList<Response<200>, Response<302>, Response<400>,
  Response<401>, Response<403>, Response<404>, Response<405>, Response<409>,
  Response<415>, Response<422>, Response<429>, Response<500>, Response<502>,
  Response<503>, Response<504>>;

class App;
template <typename Impl>
struct Request : public Zrest::ReqParser<Impl, RawData> {
  using Base = Zrest::ReqParser<Impl, RawData>;
  using Base::header;
  static constexpr uint64_t QueryLimit = 16U<<10;
  static constexpr uint64_t BodyLimit = BodyMax;
  using Headers = ZhttpHeaders("authorization");
  using Responses = ::Responses;
  App *app = nullptr;
  String authorization;
  void init() { Base::init(); authorization.null(); }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "authorization") authorization = value;
  }
  template <typename Link> void complete(Link *, bool);
};
struct Authorize : public Request<Authorize> {
  enum { Exact = 1, Query = Zrest::QueryPolicy::Raw };
  using Path = ZuStringT<"/authorize">;
};
struct Token : public Request<Token> {
  enum { Exact = 1, Method = Zhttp::Method::POST, Body = Zrest::BodyPolicy::Raw };
  using Path = ZuStringT<"/token">;
  using Headers = ZhttpHeaders(
    ("content-type", "application/x-www-form-urlencoded"), "content-length");
};
struct Revoke : public Request<Revoke> {
  enum { Exact = 1, Method = Zhttp::Method::POST, Body = Zrest::BodyPolicy::Raw };
  using Path = ZuStringT<"/revoke">;
  using Headers = ZhttpHeaders(
    ("content-type", "application/x-www-form-urlencoded"), "content-length");
};
struct Ping : public Request<Ping> {
  enum { Exact = 1 };
  using Path = ZuStringT<"/ping">;
};
struct Health : public Request<Health> {
  enum { Exact = 1 };
  using Path = ZuStringT<"/health/ready">;
};
using Requests = ZuTypeList<Authorize, Token, Revoke, Ping, Health>;
struct Parser : public Zrest::MReqParser<Requests> {
  using Base = Zrest::MReqParser<Requests>;
  void init(App &app_) { app = &app_; }
  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    if (!Base::operation(method, target)) return false;
    u.dispatch([this](auto, auto &request) { request.app = app; });
    return true;
  }
  App *app = nullptr;
};
struct Builder : public ZmObject, public Zrest::MResBuilder<Parser> { };
ZmListDerive(BuilderQ, Builder,
  ZmListNode<Builder, ZmListHeapID<"zumpingd.Reply">>);

class App {
public:
  using Parser = ::Parser;
  using ResBuilderQ = BuilderQ;
  Zum::Service service;

  template <typename Req, typename Link>
  static void reply(ZmRef<Link> link, unsigned status, String body, String location = {}) {
    ZmRef<Reply> object = new Reply{ };
    object->body = ZuMv(body);
    object->location = ZuMv(location);
    ZmRef<BuilderQ::Node> response = new BuilderQ::Node{};
    switch (status) {
#define PING_RESPONSE(N) case N: response->template init<Response<N>, Req>(object.ptr()); break
      PING_RESPONSE(200);
      PING_RESPONSE(302);
      PING_RESPONSE(400);
      PING_RESPONSE(401);
      PING_RESPONSE(403);
      PING_RESPONSE(404);
      PING_RESPONSE(405);
      PING_RESPONSE(409);
      PING_RESPONSE(415);
      PING_RESPONSE(422);
      PING_RESPONSE(429);
      PING_RESPONSE(500);
      PING_RESPONSE(502);
      PING_RESPONSE(504);
      default: response->template init<Response<503>, Req>(object.ptr()); break;
#undef PING_RESPONSE
    }
    link->send(ZuMv(response));
  }

  template <typename Req, typename Link>
  void request(Link *link, const Req &request, bool ok) {
    if (!ok) return reply<Req>(ZmRef<Link>{link}, 400,
      json({.error = "invalid_request"}));
    if constexpr (ZuIsSame<Req, Health>{}) {
      reply<Req>(ZmRef<Link>{link}, 200, json({.status = "ready"}));
    } else if constexpr (ZuIsSame<Req, Authorize>{}) {
      Zum::ServiceAuthorizeRequest input;
      if (!authorizeInput(request.object->data, input))
        return reply<Req>(ZmRef<Link>{link}, 400,
          json({.error = "invalid_request"}));
      service.authorize(ZuMv(input), [hold = ZmRef<Link>{link}](
          Zum::ServiceAuthorizeResult result) mutable {
        if (result.error == Zum::ServiceError::OK)
          reply<Req>(ZuMv(hold), 302, {}, ZuMv(result.authorizationURL));
        else reply<Req>(ZuMv(hold),
          result.error == Zum::ServiceError::Unavailable ? 503 : 400,
          json({.error = "authorization_failed"}));
      });
    } else if constexpr (ZuIsSame<Req, Token>{} || ZuIsSame<Req, Revoke>{}) {
      auto complete = [hold = ZmRef<Link>{link}](Zum::ServiceProtocolResult result) mutable {
        reply<Req>(ZuMv(hold), result.status, ZuMv(result.body));
      };
      String form{request.object->data};
      if constexpr (ZuIsSame<Req, Token>{}) service.token(ZuMv(form), ZuMv(complete));
      else service.revoke(ZuMv(form), ZuMv(complete));
    } else {
      ZuCSpan bearer{request.authorization};
      if (bearer.length() <= 7 || bearer.prefix("Bearer ") != 7)
        return reply<Req>(ZmRef<Link>{link}, 401,
          json({.error = "invalid_token"}));
      service.verify(String{bearer.offset(7)}, [hold = ZmRef<Link>{link}](
          int error, Zum::ServicePrincipal principal) mutable {
        if (error != Zum::ServiceError::OK)
          return reply<Req>(ZuMv(hold), error == Zum::ServiceError::Unavailable ? 503 : 401,
            json({.error = "invalid_token"}));
        for (const auto &action: principal.actions)
          if (action == "ping")
            return reply<Req>(ZuMv(hold), 200, json({.reply = "pong"}));
        reply<Req>(ZuMv(hold), 403, json({.error = "insufficient_scope"}));
      });
    }
  }
  void listening(int, unsigned port) {
    std::cout << "zumpingd listening on port " << port << '\n' << std::flush;
  }
  void listenFailed(int, bool) { done.post(); }
  void connected(int) { }
  void disconnected(int) { }
};

template <typename Impl>
template <typename Link>
void Request<Impl>::complete(Link *link, bool ok)
{
  app->request(link, static_cast<const Impl &>(*this), ok);
}

int main(int argc, char **argv)
{
  Options options;
  Config config;
  try {
    argc = ZfCLI::load(options, argc, argv);
    if (options.help) { std::cout << "Usage: zumpingd --config FILE\n"; return 0; }
    if (argc != 1 || !options.config || !loadConfig(options.config, config)) {
      std::cerr << "zumpingd: invalid configuration\n"; return 1;
    }
  } catch (const ZeException &e) { std::cerr << e << '\n'; return 1; }
  auto secret = getenv("ZUM_CLIENT_SECRET");
  if (!secret || !*secret) {
    std::cerr << "zumpingd: ZUM_CLIENT_SECRET is required\n"; return 1;
  }
  ZiLog::init("zumpingd");
  ZiLog::level(Ze::Warning);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZmTrap::sigintFn(interrupted);
  ZmTrap::trap();
  ZiMultiplex mx{ZiMxParams().scheduler([](auto &s) {
    s.nThreads(ServiceSID)
      .thread(1, [](auto &t) { t.name("rx"); t.isolated(1); })
      .thread(2, [](auto &t) { t.name("tx"); t.isolated(1); })
      .thread(3, [](auto &t) { t.name("srv-rx"); t.isolated(1); })
      .thread(4, [](auto &t) { t.name("srv-tx"); t.isolated(1); })
      .thread(ServiceSID, [](auto &t) { t.name("auth"); t.isolated(1); });
  }).rxThread(1).txThread(2)};
  if (!mx.start()) { ZiLog::stop(); return 1; }
  Zum::PingHTTP transport;
  App app;
  bool ok = transport.init(&mx, config.zum.issuerURL, config.caPath);
  bool initialized = ok && app.service.init(Zum::ServiceConfig{
    .scheduler = &mx, .sid = ServiceSID, .issuerURL = config.zum.issuerURL,
    .clientID = config.zum.clientID,
    .clientSecret = Zum::Bytes{ZuCSpan{secret}},
    .audience = config.audience}, transport.fn());
  ok = initialized;
  if (ok) {
    ZmSemaphore ready;
    app.service.start([&ready, &ok](int error) {
      ok = error == Zum::ServiceError::OK; ready.post();
    });
    ready.wait();
    if (ok) {
      app.service.publish(manifest(config.audienceID), [&ready, &ok](
          Zum::ServiceProtocolResult result) {
        ok = result.error == Zum::ServiceError::OK; ready.post();
      });
      ready.wait();
    }
  }
  Zhttp::Server<App> server;
  bool listener = false;
  if (ok) {
    auto settings = Zhttp::ServerConfig().localIP(ZiIP(config.addr))
      .port(config.port).idleTimeout(30).retainedBodyMax(BodyMax).tcp();
    listener = server.init(Zhttp::HubConfig{&mx, "srv-rx", "srv-tx"}, ZuMv(settings), &app);
    ok = listener && server.start();
  }
  if (ok) done.wait();
  else std::cerr << "zumpingd: service authentication, publication or listener failed\n";
  if (listener) { (void)server.stop(); server.final(); }
  if (initialized) {
    ZmSemaphore drained;
    app.service.stop([&drained](int) { drained.post(); });
    drained.wait();
    app.service.final();
  }
  transport.final();
  mx.stop();
  ZiLog::stop();
  return ok ? 0 : 1;
}
