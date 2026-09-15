//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Ping resource server: no database, local credential store or OIDC provider.

#include <iostream>
#include <stdlib.h>

#include <zlib/ZuCmp.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTrap.hh>
#include <zlib/ZfCf.hh>
#include <zlib/ZfCLI.hh>
#include <zlib/ZfJSON.hh>
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
  String managementIssuerURL;
  String managementURL;
  String clientID;
};
ZfStruct(, (ZumConfig, Cf),
  (((issuerURL), (Required)), (String)),
  (((managementIssuerURL), (Required)), (String)),
  (((managementURL), (Required)), (String)),
  (((clientID), (Required)), (String)));

struct Config {
  ZumConfig zum;
  String caPath;
  String audience;
  String addr{"127.0.0.1"};
  uint32_t port = 8080;
};
ZfStruct(, (Config, Cf),
  (((zum), (Required)), (UDT)),
  (((caPath)), (String)),
  (((audience), (Required)), (String)),
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
  return config.zum.issuerURL && config.zum.managementIssuerURL &&
    config.zum.managementURL &&
    config.zum.clientID && config.audience &&
    (config.addr == "127.0.0.1" || config.addr == "::1");
}

static Zum::ServiceManifest manifest()
{
  return {Zum::CatalogData{
    .actions = {{.name = "ping"}},
    .roles = {{.actions = {"ping"}, .name = "ping"}}
  }, 1};
}

struct RawData : public ZumObject {
  Zum::String data;
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
using Responses = ZuTypeList<Response<200>, Response<202>, Response<400>, Response<401>,
  Response<403>, Response<404>, Response<503>>;

class App;
template <typename Impl>
struct Request : public Zrest::ReqParser<Impl, RawData> {
  using Base = Zrest::ReqParser<Impl, RawData>;
  enum { Body = Zrest::BodyPolicy::Raw };
  using Base::header;
  static constexpr uint64_t QueryLimit = 16U<<10;
  static constexpr uint64_t BodyLimit = BodyMax;
  using Headers = ZhttpHeaders("authorization", "content-type");
  using Responses = ::Responses;
  App *app = nullptr;
  String authorization;
  String contentType;
  void init() { Base::init(); authorization.null(); contentType.null(); }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "authorization") authorization = value;
    else if constexpr (Key{}() == "content-type") contentType = value;
  }
  template <typename Link> void complete(Link *, bool);
};
struct Ping : public Request<Ping> {
  enum { Exact = 1 };
  using Path = ZuStringT<"/ping">;
};
struct Health : public Request<Health> {
  enum { Exact = 1 };
  using Path = ZuStringT<"/health/ready">;
};
struct NotFoundGet : public Request<NotFoundGet> { };
struct NotFoundPost : public Request<NotFoundPost> {
  enum { Method = Zhttp::Method::POST };
};
struct SSF : public Request<SSF> {
  enum { Exact = 1, Method = Zhttp::Method::POST };
  using Path = ZuStringT<"/ssf">;
};
using Requests = ZuTypeList<Ping, Health, SSF, NotFoundGet, NotFoundPost>;
ZrestCatalogDerive(Catalog, Requests);
ZrestCatalogImpl(Catalog)
struct Parser : public Zrest::MReqParser<Catalog> {
  using Base = Zrest::MReqParser<Catalog>;
  void init(App &app_) { app = &app_; }
  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    if (!Base::operation(method, target)) return false;
    u.dispatch([this](auto, auto &request) { request.app = app; });
    return true;
  }
  App *app = nullptr;
};
struct Builder : public ZmObject, public Zrest::MResBuilder<Catalog> { };
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
      PING_RESPONSE(202);
      PING_RESPONSE(400);
      PING_RESPONSE(401);
      PING_RESPONSE(403);
      PING_RESPONSE(404);
      default: response->template init<Response<503>, Req>(object.ptr()); break;
#undef PING_RESPONSE
    }
    link->send(ZuMv(response));
  }

  template <typename Req, typename Link>
  void request(Link *link, const Req &request, bool ok) {
    if (!ok) return reply<Req>(ZmRef<Link>{link}, 400,
      json({.error = "invalid_request"}));
    if constexpr (ZuIsSame<Req, NotFoundGet>{} ||
	ZuIsSame<Req, NotFoundPost>{}) {
      reply<Req>(ZmRef<Link>{link}, 404, json({.error = "not_found"}));
    } else if constexpr (ZuIsSame<Req, Health>{}) {
      reply<Req>(ZmRef<Link>{link}, 200, json({.status = "ready"}));
    } else if constexpr (ZuIsSame<Req, SSF>{}) {
      service.receiveSET(Zum::ServiceSETRequest{
        .authorization = request.authorization,
        .contentType = request.contentType,
        .body = ZuMv(request.object->data)},
        [hold = ZmRef<Link>{link}](int error) mutable {
          unsigned status;
          switch (error) {
            case Zum::ServiceError::OK: status = 202; break;
            case Zum::ServiceError::Unauthorized: status = 401; break;
            case Zum::ServiceError::Unavailable: status = 503; break;
            default: status = 400; break;
          }
          reply<Req>(ZuMv(hold), status, {});
        });
    } else {
      ZuCSpan bearer{request.authorization};
      if (bearer.length() <= 7 || bearer.prefix("Bearer ") != 7)
        return reply<Req>(ZmRef<Link>{link}, 401,
          json({.error = "invalid_token"}));
      service.verify(bearer.offset(7), [hold = ZmRef<Link>{link}](
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
  auto callbackAuth = getenv("ZUM_SSF_CALLBACK_AUTH");
  if (!secret || !*secret || !callbackAuth || !*callbackAuth) {
    std::cerr << "zumpingd: ZUM_CLIENT_SECRET and ZUM_SSF_CALLBACK_AUTH are required\n";
    return 1;
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
  bool ok = transport.init(&mx, config.zum.issuerURL,
    config.zum.managementIssuerURL, config.zum.managementURL,
    config.caPath);
  bool initialized = ok && app.service.init(Zum::ServiceConfig{
    .scheduler = &mx, .sid = ServiceSID, .issuerURL = config.zum.issuerURL,
    .managementIssuerURL = config.zum.managementIssuerURL,
    .managementURL = config.zum.managementURL,
    .clientID = config.zum.clientID,
    .clientSecret = Zum::Bytes{ZuCSpan{secret}},
    .audience = config.audience,
    .ssf = Zum::ServiceSSFConfig{
      .enabled = true, .receiverID = config.zum.clientID,
      .callbackPath = "/ssf",
      .callbackAuth = callbackAuth,
      .transmitterIssuer = config.zum.issuerURL,
      .audience = config.audience}}, transport.fn());
  ok = initialized;
  if (ok) {
    // Ping has no long-lived sessions; the callback still installs the
    // resource-server revocation hook used by stateful consumers.
    app.service.setRefreshRevocationFn([](Zum::RefreshID, int64_t) {
      // The example has no renewal state of its own; expose receipt of the
      // shared callback as a lifecycle diagnostic for integration fixtures.
      std::cout << "zumpingd refresh family revoked\n" << std::flush;
    });
    ZmSemaphore ready;
    app.service.start([&ready, &ok](int error) {
      ok = error == Zum::ServiceError::OK; ready.post();
    });
    ready.wait();
    if (ok) {
      app.service.publish(manifest(), [&ready, &ok](
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
