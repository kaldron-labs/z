//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Minimal passkey OAuth service using Zum's standard HTTP adapter

#include <iostream>
#include <string.h>

#include <zlib/ZuBase64URL.hh>

#include <zlib/ZmBlock.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZfCf.hh>
#include <zlib/ZfCLI.hh>
#include <zlib/ZfJSON.hh>

#include <zlib/ZiLog.hh>

#include <zlib/Zdb.hh>

#include <zlib/ZhttpServer.hh>
#include <zlib/ZrestServer.hh>

#include <zlib/ZumAdmin.hh>
#include <zlib/ZumHTTP.hh>

#include <zlib/ZtlsPK.hh>
#include <zlib/ZtlsCOSE.hh>
#include <zlib/ZtlsRandom.hh>

enum { RequestLimit = 64, BodyMax = 64U<<10 };

static constexpr ZuCSpan ClientID = "zum";
static constexpr ZuCSpan RPID = "localhost";
static constexpr ZuCSpan Audience = "example";
static constexpr ZuCSpan Scope = "example.use";
static constexpr ZuCSpan KeyID = "example-key";
static constexpr ZuCSpan ProviderRef = "memory-key";
// A real service loads the private key from protected external storage.
static const char *SigningKey =
  "\x01\x01\x01\x01\x01\x01\x01\x01"
  "\x01\x01\x01\x01\x01\x01\x01\x01"
  "\x01\x01\x01\x01\x01\x01\x01\x01"
  "\x01\x01\x01\x01\x01\x01\x01\x01";

struct Options {
  Zum::String	addr{"127.0.0.1"};
  uint32_t	port = 8080;
  bool		help = false;
};

ZfStruct((Options, CLI),
  (((addr), (CLI::Long<"addr">)), (String, "127.0.0.1")),
  (((port), (CLI::Long<"port">)), (UInt32, 8080)),
  (((help), (CLI::Flag<'h'>, CLI::Long<"help">)), (Bool)));

static void usage(int code = 1)
{
  std::cerr <<
    "Usage: zumd [OPTION]...\n\n"
    "  --addr=IP    listen address, default 127.0.0.1\n"
    "  --port=N     listen port, default 8080\n"
    "  -h, --help   show help\n\n"
    "ZDB_MODULE selects the Zdb store module and ZDB_CONNECT selects its\n"
    "database connection. Both must be set.\n\n"
    "Use localhost:8080 for the default WebAuthn RP and issuer.\n";
  ::exit(code);
}

static Zum::String encode(ZuBSpan data)
{
  Zum::String value;
  value.length(ZuBase64URL::enclen(data.length()));
  value.length(ZuBase64URL::encode(value.span(), data));
  return value;
}

struct RawData : public ZmObject {
  ZuSpan<uint8_t> data;
  RawData &operator =(ZuSpan<uint8_t> value) { data = value; return *this; }
};

struct ResourceOK : public Zrest::ResBuilder<ResourceOK, Zum::HTTPResponse> {
  enum { Body = Zrest::BodyPolicy::Raw };
  using Headers = ZhttpHeaders(
    ("content-type", "application/json"), "content-length");
  const Zum::String &bodyObject(const Zum::HTTPResponse *response) const {
    return response->body;
  }
};

struct Unauthorized : public Zrest::ResBuilder<Unauthorized, Zum::HTTPResponse> {
  using Base = Zrest::ResBuilder<Unauthorized, Zum::HTTPResponse>;
  using Base::header;
  enum { Status = 401, Body = Zrest::BodyPolicy::Raw };
  using Headers = ZhttpHeaders(
    ("content-type", "application/json"),
    "www-authenticate", "content-length");
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "www-authenticate") l("Bearer");
    else Base::template header<Key>(ZuFwd<L>(l));
  }
  const Zum::String &bodyObject(const Zum::HTTPResponse *response) const {
    return response->body;
  }
};

class App;

struct ResourceReq : public Zrest::ReqParser<ResourceReq, RawData> {
  using Base = Zrest::ReqParser<ResourceReq, RawData>;
  using Base::header;
  enum { Exact = 1 };
  using Path = ZuStringT<"/resource">;
  using Headers = ZhttpHeaders("authorization");
  using Responses = ZuTypeList<ResourceOK, Unauthorized>;
  App *app = nullptr;
  Zum::String authorization;
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "authorization") authorization = value;
  }
  template <typename Link> void complete(Link *, bool);
};

using Requests = ZuTypeList<Zum::AuthorizeReq<App>, Zum::TokenReq<App>,
  Zum::PasskeyBeginReq<App>, Zum::PasskeyFinishReq<App>,
  Zum::MetadataReq<App>, Zum::JWKSReq<App>, Zum::RevokeReq<App>, ResourceReq>;

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

using Builder = Zrest::MResBuilder<Parser>;
struct ResBuilder_ : public ZmObject, public Builder { };
ZmListDerive(ResBuilderQ, ResBuilder_,
  ZmListNode<ResBuilder_, ZmListHeapID<"zumd.ResBuilder">>);

struct DB : public Zum::DB {
  ZmSemaphore active;
  ZmRef<Zum::Requests> requests;
};

static ZmSemaphore done;

class App : public Zum::HTTP<App> {
public:
  using Parser = ::Parser;
  using ResBuilderQ = ::ResBuilderQ;

  bool init(const Options *, ZiMultiplex *, ZuCSpan, ZuCSpan);
  void final();

  template <typename Link>
  void resource(Link *link, const ResourceReq &request, bool ok)
  {
    Zum::Principal principal;
    ZuCSpan bearer = request.authorization;
    bool bearerOK = bearer.length() > 7 &&
      !memcmp(bearer.data(), "Bearer ", 7);
    if (bearerOK) bearer.offset(7);
    if (ok && bearerOK && Zum::jwtVerify(bearer, KeyID, m_issuer, Audience,
        m_publicKey, now_(), Zum::JWTLimits{}, principal)) {
      Zum::String body{"{\"message\":\"hello\",\"subject\":"};
      ZfJSON::quote(body, principal.subject);
      body << ",\"scope\":";
      ZfJSON::quote(body, principal.scope);
      body << '}';
      send_<ResourceOK>(link, request, ZuMv(body));
    } else {
      send_<Unauthorized>(link, request,
        Zum::String{"{\"error\":\"unauthorized\"}"});
    }
  }

  void listening(int, unsigned port) {
    std::cout << "zumd listening on http://localhost:" << port << '\n' <<
      "Run: ./zum/example/zumc http://localhost:" << port << "/\n" <<
      std::flush;
  }
  void listenFailed(int, bool) { done.post(); }
  void connected(int) { }
  void disconnected(int) { }

private:
  template <typename Response, typename Link, typename Request>
  void send_(Link *link, const Request &, Zum::String body)
  {
    ZmRef<Zum::HTTPResponse> object = new Zum::HTTPResponse{};
    object->body = ZuMv(body);
    ZmRef<ResBuilderQ::Node> response = new ResBuilderQ::Node{};
    response->template init<Response, Request>(object.ptr());
    link->send(ZuMv(response));
  }

  static int64_t now_() { return Zm::now().sec(); }
  static Zum::String page_(ZuBSpan, ZuCSpan);
  bool seed_();

  ZmRef<DB>		m_db;
  ZmRef<Zum::DBContext>	m_context;
  ZmRef<Ztls::PK::SK_EC> m_key;
  Ztls::Random		m_rng;
  Zum::Server		m_zum;
  Zum::Bytes		m_publicKey;
  Zum::String		m_issuer;
};

template <typename Link>
void ResourceReq::complete(Link *link, bool ok) { app->resource(link, *this, ok); }

static void dbUp(Zdb *db, ZdbHost *)
{
  auto appDB = static_cast<DB *>(db);
  appDB->requests->activate();
  appDB->active.post();
}

static void dbDown(Zdb *db, bool)
{
  static_cast<DB *>(db)->requests->deactivate();
}

template <typename T>
static bool insertRecord(ZdbTable<T> *table, T data)
{

  return ZmBlock<bool>{}([table, data = ZuMv(data)](auto wake) mutable {
    table->run(0, [table, data = ZuMv(data), wake = ZuMv(wake)]() mutable {
      ZdbRowRef<T> row = new ZdbRow<T>{table, ZdbShard{0}};
      table->insert(row, [data = ZuMv(data), wake = ZuMv(wake)](
          ZdbRow<T> *row) mutable {
        if (!row) { wake(false); return; }
        new (row->ptr()) T{ZuMv(data)};
        wake(row->commit());
      });
    });
  });
}

Zum::String App::page_(ZuBSpan ceremonyID, ZuCSpan options)
{
  Zum::String page;
  page << "<!doctype html><meta charset=utf-8><title>Zum login</title>"
    "<style>body{font:16px sans-serif;max-width:38rem;margin:4rem auto}"
    "button{font:inherit;padding:.6rem 1rem}#register{display:none}"
    "pre{white-space:pre-wrap}</style><h1>Zum example</h1>"
    "<p>Use a passkey to authorize the native client.</p>"
    "<button id=login>Use passkey</button> "
    "<button id=register>Create first user</button><pre id=out></pre><script>"
    "const aid='" << encode(ceremonyID) << "',ao=" << options << ";"
    "const out=document.querySelector('#out'),reg=document.querySelector('#register');"
    "const dec=s=>Uint8Array.from(atob(s.replace(/-/g,'+').replace(/_/g,'/')+"
    "'==='.slice((s.length+3)%4)),c=>c.charCodeAt(0));"
    "const enc=b=>btoa(String.fromCharCode(...new Uint8Array(b)))"
    ".replace(/\\+/g,'-').replace(/\\//g,'_').replace(/=+$/,'');"
    "function opts(o){o=structuredClone(o);o.publicKey.challenge=dec(o.publicKey.challenge);"
    "if(o.publicKey.user)o.publicKey.user.id=dec(o.publicKey.user.id);return o}"
    "function wire(c){const r=c.response,x={id:c.id,rawId:enc(c.rawId),type:c.type,response:{"
    "clientDataJSON:enc(r.clientDataJSON)}};if(r.authenticatorData)x.response.authenticatorData=enc(r.authenticatorData);"
    "if(r.signature)x.response.signature=enc(r.signature);if(r.userHandle)x.response.userHandle=enc(r.userHandle);"
    "if(r.attestationObject)x.response.attestationObject=enc(r.attestationObject);return x}"
    "async function finish(id,c,auth){const r=await fetch('/passkey/finish?id='+id,{method:'POST',"
    "headers:{'content-type':'application/json'},body:JSON.stringify(wire(c)),redirect:'manual'});"
    "if(!r.ok)throw Error(await r.text());if(auth){const u=r.headers.get('location');if(!u)throw Error('missing redirect');location.href=u}}"
    "async function login(){try{out.textContent='Waiting for passkey...';const c=await navigator.credentials.get(opts(ao));"
    "await finish(aid,c,true)}catch(e){out.textContent=e;reg.style.display='inline-block'}}"
    "async function enroll(){try{out.textContent='Creating passkey...';const b=await fetch('/passkey/begin',{method:'POST',"
    "headers:{'content-type':'application/json'},body:'{\"purpose\":\"enrollment\"}'}),j=await b.json();"
    "if(!b.ok)throw Error(JSON.stringify(j));const c=await navigator.credentials.create(opts(j.options));"
    "await finish(j.ceremony,c,false);location.reload()}catch(e){out.textContent=e}}"
    "document.querySelector('#login').onclick=login;reg.onclick=enroll;login();</script>";
  return page;
}

bool App::seed_()
{
  uint8_t publicKey[Ztls::COSE::ES256::PublicKeySize];
  if (!Ztls::Backend::pkey_ec_export_public(
      m_key->key, {publicKey, sizeof(publicKey)})) return false;
  m_publicKey = Zum::Bytes{ZuBSpan{publicKey}};
  char jwk[Ztls::COSE::ES256::JWKSize];
  unsigned jwkLength = 0;
  if (!Ztls::COSE::ES256::jwk(
      m_publicKey, {jwk, sizeof(jwk)}, jwkLength)) return false;
  Zum::String publicJwk;
  publicJwk << ZuCSpan{jwk, jwkLength - 1} <<
    ",\"use\":\"sig\",\"alg\":\"ES256\",\"kid\":\"" << KeyID << "\"}";

  ZtBitmap actions{1}; actions.set(0);
  Zum::IDVec roleIDs; roleIDs.push(1);
  Zum::IDVec scopeIDs; scopeIDs.push(1);
  Zum::StringVec redirects; redirects.push("http://127.0.0.1/callback");
  Zum::StringVec audiences; audiences.push(Zum::String{Audience});
  int64_t now = now_();
  return insertRecord(m_context->issuers, Zum::Issuer{
      .id = m_issuer, .nextActionID = 1, .authVersion = 1}) &&
    insertRecord(m_context->actions, Zum::Action{
      .id = 0, .name = "example.use"}) &&
    insertRecord(m_context->roles, Zum::Role{
      .id = 1, .name = "example-user", .actions = ZuMv(actions)}) &&
    insertRecord(m_context->scopes, Zum::Scope{
      .id = 1, .audience = Zum::String{Audience}, .name = Zum::String{Scope},
      .roleIDs = ZuMv(roleIDs)}) &&
    insertRecord(m_context->clients, Zum::Client{
      .id = Zum::String{ClientID}, .redirects = ZuMv(redirects),
      .audiences = ZuMv(audiences), .scopeIDs = ZuMv(scopeIDs),
      .created = now, .updated = now, .type = Zum::ClientType::Native,
      .grants = Zum::ClientGrant::AuthorizationCode |
        Zum::ClientGrant::RefreshToken, .state = Zum::State::Active}) &&
    insertRecord(m_context->signKeys, Zum::SignKey{
      .id = Zum::String{KeyID}, .providerRef = Zum::String{ProviderRef},
      .publicJwk = ZuMv(publicJwk), .notBefore = now,
      .state = Zum::State::Active});
}

bool App::init(
    const Options *options, ZiMultiplex *mx, ZuCSpan module, ZuCSpan connect)
{
  m_issuer << "http://localhost:" << options->port;
  if (!m_rng.init()) return false;
  m_key = new Ztls::PK::SK_EC{m_rng,
    Ztls::PK::OIDs::EC_GRP_SECP256R1, ZuBSpan{ZuCSpan{SigningKey}}};
  ZmRef<ZfCf::Defines> defines = new ZfCf::Defines{};
  defines->add(ZfCf::DefKey{"MODULE"}, ZfCf::DefVal{module});
  defines->add(ZfCf::DefKey{"CONNECT"}, ZfCf::DefVal{connect});
  auto scan = ZfCf::scan(
    "thread: zdb, shards: 1, threads: [shard],"
    "store: {thread: store, module: ${MODULE},connection: ${CONNECT}},"
    "hostID: self, hosts: {self: {standalone: true}}, tables: {}",
    {}, ZuMv(defines));
  if (!scan.p<1>()) return false;
  m_db = new DB{};
  m_db->requests = new Zum::Requests{};
  if (!m_db->requests->init(mx, 5, RequestLimit)) return false;
  m_db->init(ZdbCf{scan.p<1>()}, mx,
    ZdbHandler{.upFn = dbUp, .downFn = dbDown});
  m_context = Zum::registerSchema(m_db);
  if (!m_db->start()) return false;
  m_db->active.wait();
  if (!seed_()) return false;

  Zum::ServerConfig config{
    .issuer = m_issuer, .rpID = Zum::String{RPID}, .rpName = "Zum example",
    .keyID = Zum::String{KeyID}};
  bool inited = m_zum.init(m_db, m_context, m_db->requests, ZuMv(config),
    []() { return now_(); },
    [](Zum::Bytes id, Zum::String options) { return page_(id, options); },
    [](const Zum::User &, const Zum::Client &,
        const Zum::ScopeSelection &, const ZtBitmap &allowed,
        Zum::PolicyDoneFn complete) {
      complete(true, ZtBitmap{allowed});
    },
    [](Zum::PasskeyStart start, Zum::AdmitDoneFn complete) {
      Zum::PasskeyAdmission admission;
      if (start.type == Zum::PasskeyStartType::Enrollment) {
        admission.allowed = true;
        admission.enrollment.name = "example-user";
        admission.enrollment.displayName = "Example User";
        admission.enrollment.roleIDs.push(1);
        admission.enrollment.label = "example passkey";
        admission.enrollment.userID = 1;
      }
      complete(ZuMv(admission));
    },
    [this](ZuCSpan provider, ZuBSpan digest, Zum::SignatureFn complete) {
      Zum::Bytes signature;
      auto result = m_key->sign(m_rng, digest, [&signature](ZuBSpan der) {
        signature = Zum::Bytes{der};
      });
      if (provider != ProviderRef || result.template is<ZeException>())
        signature = {};
      complete(ZuMv(signature));
    });
  if (!inited) return false;
  Zum::HTTP<App>::init(m_zum);
  return true;
}

void App::final()
{
  if (!m_db) return;
  ZmSemaphore drained;
  m_db->requests->deactivate([&drained]() { drained.post(); });
  drained.wait();
  m_zum.final();
  (void)m_db->stop();
  m_context = {};
  m_db->final();
  m_db = {};
  m_key = {};
}

static ZiMxParams mxParams()
{
  return ZiMxParams().scheduler([](auto &s) {
    s.nThreads(5)
      .thread(1, [](auto &t) { t.name("rx"); t.isolated(1); })
      .thread(2, [](auto &t) { t.name("tx"); t.isolated(1); })
      .thread(3, [](auto &t) { t.name("zdb"); t.isolated(1); })
      .thread(4, [](auto &t) { t.name("store"); t.isolated(1); })
      .thread(5, [](auto &t) { t.name("shard"); t.isolated(1); });
  }).rxThread(1).txThread(2);
}

static void interrupted() { done.post(); }

int main(int argc, char **argv)
{
  Options options;
  try { argc = ZfCLI::load(options, argc, argv); }
  catch (const ZeException &e) { std::cerr << e << '\n'; usage(); }
  if (options.help) usage(0);
  if (argc != 1 || !options.port || options.port > 65535) usage();
  ZuCSpan module = ::getenv("ZDB_MODULE");
  ZuCSpan connect = ::getenv("ZDB_CONNECT");
  if (!module || !connect) {
    std::cerr << "zumd: set ZDB_MODULE and ZDB_CONNECT\n";
    return 1;
  }

  ZiLog::init("zumd");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZmTrap::sigintFn(interrupted);
  ZmTrap::trap();

  ZiMultiplex mx{mxParams()};
  if (!mx.start()) return 1;
  App app;
  if (!app.init(&options, &mx, module, connect)) {
    std::cerr << "zumd: initialization failed\n";
    app.final();
    mx.stop();
    ZiLog::stop();
    return 1;
  }
  Zhttp::Server<App> server;
  auto config = Zhttp::ServerConfig().localIP(ZiIP(options.addr))
    .port(options.port).idleTimeout(30).retainedBodyMax(BodyMax).tcp();
  bool inited = server.init(Zhttp::HubConfig{&mx, "rx", "tx"},
    ZuMv(config), &app);
  if (!inited || !server.start()) {
    std::cerr << "zumd: HTTP server start failed\n";
    if (inited) (void)server.stop();
    server.final();
    app.final();
    mx.stop();
    ZiLog::stop();
    return 1;
  }
  done.wait();
  (void)server.stop();
  server.final();
  app.final();
  mx.stop();
  ZiLog::stop();
  return 0;
}
