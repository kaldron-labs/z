//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumService.hh>
#include <zlib/ZumURI.hh>

#include <zlib/ZuBase64.hh>
#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuLib.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuPtr.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmLHash.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZfJSON.hh>
#include <zlib/ZfURI.hh>

#include <zlib/ZhttpURL.hh>

#include <zlib/ZtlsCOSE.hh>

namespace Zum {

// This translation unit is the installed service-side client. Server and Zdb
// implementation are owned by the zumd program.

ZuDerive(ServiceReadyFn, (ZmFn<void(bool),
  ZmFnHeapID<"Zum.Service.Ready">>));
ZuDerive(ServiceHTTPResultFn, (ZmFn<void(ServiceHTTPResponse),
  ZmFnHeapID<"Zum.Service.HTTPResult">>));
ZuDerive(ServiceReadyVec, (ZtArray<ServiceReadyFn,
  ZtArrayHeapID<"Zum.Service.ReadyVec">>));

static bool userAuthMethod(ZuCSpan method)
{
  switch (method.length()) {
    case 7: return method == "passkey";
    case 4: return method == "oidc";
    default: return false;
  }
}

template <typename Vec, typename Value>
static void completeAll(Vec &pending, Value value)
{
  auto waiters = ZuMv(pending);
  pending.null();
  // ZmFn has POD array traits; consume each capture before discarding the array.
  for (auto &waiter: waiters) {
    auto fn = ZuMv(waiter);
    fn(value);
  }
}

struct ServiceKey {
  String id;
  Bytes publicKey;
};
static ZuCSpan serviceKeyID(const ServiceKey &key) { return key.id; }
static const char *serviceKeysID() { return "Zum.Service.Keys"; }
ZuDerive(ServiceKeysHash, (ZmLHash<ServiceKey,
  ZmLHashKey<serviceKeyID, ZmLHashID<serviceKeysID, ZmLHashLocal<>>>>));
struct ServiceKeys : public ZmNHeap<ServiceKeys>, public ServiceKeysHash {
friend inline ZuStringT<"Zum.Service.Keys"> ZmNHeapID(ServiceKeys *);

  using ServiceKeysHash::ServiceKeysHash;
};

using ServiceKeySet = ZuPtr<ServiceKeys>;

static ServiceKeySet serviceKeys(unsigned count)
{
  ZmHashParams params{count};
  params.init(serviceKeysID());
  return new ServiceKeys{params};
}

struct SETSeen {
  String id;
  int64_t expires = 0;
};

static const String &setSeenID(const SETSeen &item)
{
  return item.id;
}
ZmHashDerive(SETSeenHash, SETSeen,
  (ZmHashNode<SETSeen,
    ZmHashKey<setSeenID,
    ZmHashLock<ZmNoLock, ZmHashHeapID<"Zum.Service.SETSeen">>>>));

struct SETSeenExpiry {
  int64_t expires = 0;
  String id;
  int cmp(const SETSeenExpiry &v) const {
    if (expires != v.expires) return (expires > v.expires) - (expires < v.expires);
    return id.cmp(v.id);
  }
  friend bool operator ==(const SETSeenExpiry &l, const SETSeenExpiry &r) {
    return !l.cmp(r);
  }
};
using SETSeenExpiries = ZmRBTree<SETSeenExpiry,
  ZmRBTreeUnique<true,
    ZmRBTreeLock<ZmNoLock,
      ZmRBTreeHeapID<"Zum.Service.SETSeenExp">>>>;

struct SETSubject {
  String format;
  String issuerURL;
  String familyID;
  int64_t expires = 0;
};
ZfStruct(, (SETSubject, JSON),
  (format,		(Required),				String),
  (issuerURL,	(JSON::ID<"iss">, Required),			String),
  (familyID,		(JSON::ID<"family_id">, Required),	String),
  (expires,		(JSON::ID<"exp">, Required),		Int64));

struct SETEvent {
  SETSubject subject;
};
ZfStruct(, (SETEvent, JSON),
  (subject,		(Required),	UDT));

struct SETEvents {
  SETEvent revoked;
};
ZfStruct(, (SETEvents, JSON),
  (revoked,
    (JSON::ID<"urn:zum:events:refresh-token-revoked">, Required),	UDT));

struct SETClaims {
  String issuerURL;
  String audience;
  String id;
  int64_t issued = 0;
  SETEvents events;
};
ZfStruct(, (SETClaims, JSON),
  (issuerURL,	(JSON::ID<"iss">, Required),		String),
  (audience,		(JSON::ID<"aud">, Required),	String),
  (id,		(JSON::ID<"jti">, Required),		String),
  (issued,		(JSON::ID<"iat">, Required),	Int64),
  (events,		(Required),			UDT));

struct Introspection {
  bool active = false;
  String issuerURL;
  String subject;
  String clientID;
  String audience;
  String jti;
  String scope;
  StringVec actions;
  AppID appID = 0;
  int64_t expires = 0;
  StringVec amr;
  String grantType;
};
ZfStruct(, (Introspection, JSON),
  (active,		(Required),						Bool),
  (issuerURL,	(JSON::ID<"iss">, JSON::Opt),					String),
  (subject,		(JSON::ID<"sub">, JSON::Opt),				String),
  (clientID,		(JSON::ID<"client_id">, JSON::Opt),			String),
  (audience,		(JSON::ID<"aud">, JSON::Opt),				String),
  (jti,		(JSON::Opt),							String),
  (scope,		(JSON::Opt),						String),
  (actions,		(JSON::Opt),						StringVec),
  (appID,		(JSON::ID<"zum_app_id">, JSON::String<>, JSON::Opt),	UInt64),
  (expires,		(JSON::ID<"exp">, JSON::Opt),				Int64),
  (amr,		(JSON::Opt),							StringVec),
  (grantType,		(JSON::ID<"grant_type">, JSON::Opt),			String));

struct Token {
  String accessToken;
  uint64_t expiresIn = 0;
};
ZfStruct(, (Token, JSON),
  (accessToken,	(JSON::ID<"access_token">, Required),		String),
  (expiresIn,	(JSON::ID<"expires_in">, Required),		UInt64));

struct Discovery {
  String issuerURL;
  String tokenEndpoint;
  String jwksURI;
};
ZfStruct(, (Discovery, JSON),
  (issuerURL,	(JSON::ID<"issuer">, Required),				String),
  (tokenEndpoint,	(JSON::ID<"token_endpoint">, Required),		String),
  (jwksURI,		(JSON::ID<"jwks_uri">, Required),		String));

struct JWK {
  String alg;
  String crv;
  String kid;
  String kty;
  String use;
  String x;
  String y;
};
ZfStruct(, (JWK, JSON),
  (alg,		(JSON::Opt),		String),
  (crv,		(Required),		String),
  (kid,		(Required),		String),
  (kty,		(Required),		String),
  (use,		(JSON::Opt),		String),
  (x,		(Required),		String),
  (y,		(Required),		String));
ZuDerive(JWKWireArray,
  (ZtArray<JWK, ZtArrayHeapID<"Zum.Service.JWKs">>));
struct JWKWireVec : public JWKWireArray {
  ZuDerive_(JWKWireVec, JWKWireArray);
  friend ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(JWKWireVec *);
};
struct JWKSResponse { JWKWireVec keys; };
ZfStruct(, (JWKSResponse, JSON),
  (keys,		(Required),	UDT));

template <typename T>
static bool jsonLoad(String &json, unsigned limit, T &value)
{
  if (!json || json.length() > limit) return false;
  if (!json.mutable_()) json.length(json.length());
  auto parsed = ZfJSON::scan(json);
  if (parsed.p<0>() != int(json.length()) || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1) return false;
  auto handler = ZfJSON::handler<T>(roots[0]);
  if (!handler.valid) return false;
  value = handler.ctor();
  return true;
}

static bool contentType(ZuCSpan value)
{
  return value == "application/secevent+jwt" ||
    (value.length() > 24 &&
      ZuCmp<ZuCSpan>::equals(ZuCSpan{value.data(), 24},
        "application/secevent+jwt") && value[24] == ';');
}

static bool tokenResponse(String json, unsigned limit,
    String &token, uint64_t &expires)
{
  ZuGuard clear{[&json]() {
    if (json.mutable_()) ZuClear(json);
  }};
  Token wire;
  ZuGuard clearWire{[&wire]() {
    if (wire.accessToken.mutable_())
      ZuClear(wire.accessToken);
  }};
  if (!jsonLoad(json, limit, wire) || !wire.accessToken || !wire.expiresIn)
    return false;
  token = ZuMv(wire.accessToken);
  expires = wire.expiresIn;
  return true;
}

static bool discoveryResponse(String json, unsigned limit,
    ZuCSpan expectedIssuer, String &token, String &jwks)
{
  Discovery wire;
  if (!jsonLoad(json, limit, wire) || wire.issuerURL != expectedIssuer ||
      !wire.tokenEndpoint || !wire.jwksURI) return false;
  token = ZuMv(wire.tokenEndpoint);
  jwks = ZuMv(wire.jwksURI);
  return true;
}

static ServiceKeySet jwksResponse(
    String json, unsigned limit, unsigned keyMax)
{
  JWKSResponse wire;
  if (!jsonLoad(json, limit, wire) || !wire.keys ||
      wire.keys.length() > keyMax) return {};
  auto next = serviceKeys(wire.keys.length());
  for (auto &item: wire.keys) {
    if (!item.kid || item.kty != "EC" || item.crv != "P-256" ||
        (item.use && item.use != "sig") ||
        (item.alg && item.alg != "ES256")) continue;
    ServiceKey key{.id = ZuMv(item.kid)};
    key.publicKey.length(Ztls::COSE::ES256::PublicKeySize, false);
    key.publicKey[0] = 4;
    if (ZuBase64URL::decode({key.publicKey.data() + 1,
          Ztls::COSE::ES256::CoordinateSize}, ZuBSpan{item.x}) !=
          Ztls::COSE::ES256::CoordinateSize ||
        ZuBase64URL::decode({key.publicKey.data() + 1 +
          Ztls::COSE::ES256::CoordinateSize,
          Ztls::COSE::ES256::CoordinateSize}, ZuBSpan{item.y}) !=
          Ztls::COSE::ES256::CoordinateSize ||
        !Ztls::COSE::ES256::validPK(key.publicKey)) continue;
    if (next->find(key.id)) return {};
    next->add(ZuMv(key));
  }
  if (!next->count_()) return {};
  return next;
}

static String endpoint(ZuCSpan issuerURL, ZuCSpan path)
{
  String url{issuerURL};
  if (url && url[url.length() - 1] == '/') url.length(url.length() - 1);
  url << path;
  return url;
}

static String basicAuth(ZuCSpan clientID, ZuBSpan secret)
{
  SecretString plain;
  ZfURI::PathQuote::quote(plain, clientID);
  plain << ':';
  bool safe = true;
  for (auto c: secret)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_' ||
          c == '.' || c == '~')) { safe = false; break; }
  if (safe) plain << ZuCSpan{secret};
  else ZfURI::PathQuote::quote(plain, secret);
  SecretString encoded;
  encoded.length(ZuBase64::enclen(plain.length()));
  encoded.length(ZuBase64::encode(encoded.span(), ZuBSpan{plain}));
  plain.null();
  String authorization{"Basic "};
  authorization << encoded;
  return authorization;
}

static int serviceStatus(unsigned status)
{
  if (status >= 200 && status < 300) return ServiceError::OK;
  if (status == 401) return ServiceError::Unauthorized;
  if (status == 403) return ServiceError::Forbidden;
  if (status >= 500 || !status) return ServiceError::Unavailable;
  return ServiceError::Invalid;
}

static bool serviceURL(ZuCSpan value, bool originOnly)
{
  Zhttp::URL parsed{value};
  auto url = parsed.url();
  if (!parsed.ok() || !url.host || url.hasFragment ||
      (originOnly && (url.hasQuery || (url.path && url.path != "/"))))
    return false;
  if (url.scheme != Zhttp::Scheme::https &&
      !(url.scheme == Zhttp::Scheme::http &&
        (url.host == "localhost" || url.host == "127.0.0.1" ||
          url.host == "::1"))) return false;
  return true;
}

static bool serviceIssuer(
    ZuCSpan issuerURL, String *metadata = nullptr, AppID *appID = nullptr)
{
  if (!serviceURL(issuerURL, false)) return false;
  Zhttp::URL parsed{issuerURL};
  auto url = parsed.url();
  if (url.hasQuery || !url.path) return false;
  // URLView is const; typed URI loading percent-decodes its mutable input.
  String source{url.path};
  AppIssuerPath path;
  if (!ZfURI::loadPath(path, source) || path.oauth2 != "oauth2" ||
      !path.appID) return false;
  if (appID) *appID = path.appID;
  if (metadata) {
    *metadata << url.origin();
    ZfURI::savePath(*metadata, AppOAuthMetadataPath{
      .wellKnown = ".well-known",
      .endpoint = "oauth-authorization-server",
      .oauth2 = "oauth2", .appID = path.appID});
  }
  return true;
}

// One bounded cleanup turn handles at most 64 container entries. This is
// a scheduler work budget, independent of configured key/dedup capacities.
namespace ServiceWork { enum { Batch = 64 }; }
namespace ServiceIngress { enum { Closed = 1LL<<62, Count = Closed - 1 }; }

template <typename Data, typename Done>
struct ServiceAction : public ZmNHeap<ServiceAction<Data, Done>>, public ZmObject {
friend inline ZuStringT<"Zum.Service.Action"> ZmNHeapID(ServiceAction *);

  Data data;
  Done done;
  ServiceAction(Data data_, Done done_) :
    data{ZuMv(data_)}, done{ZuMv(done_)} { }
};

struct ServiceState : public ZmNHeap<ServiceState> {
friend inline ZuStringT<"Zum.ZumService.ServiceState"> ZmNHeapID(ServiceState *);

  Service *owner;
  ServiceConfig config;
  ServiceHTTPFn http;
  AppID appID = 0;
  AppID managementAppID = 0;

  // Shared admission word; owner-shard protocol state has its own cache line.
  alignas(Zm::CacheLineSize)
  ZmAtomic<uint64_t> ingress = ServiceIngress::Closed;

  alignas(Zm::CacheLineSize)
  ServiceKeySet keys;
  ServiceKeySet managementKeys;
  ServiceKeySet ssfKeys;
  SETSeenHash seen;
  SETSeenExpiries seenExpiries;
  ServiceRefreshFn refreshFn;
  ServiceReadyVec tokenWaiters;
  ServiceReadyVec keyWaiters;
  String jwksURL;
  String managementJWKSURL;
  String ssfJWKSURL;
  String tokenURL;
  String resourceTokenURL;
  String accessToken;
  int64_t accessExpires = 0;
  int64_t keysLoaded = 0;
  unsigned inflight = 0;
  int startupError = ServiceError::OK;
  int startResult = -1;
  bool workloadManagement = false;
  bool settlePosted = false;
  bool renewing = false;
  bool keysRefreshing = false;
  ZmScheduler::Timer registerTimer;
  bool registerArmed = false;
  ZmScheduler::Timer expiryTimer;
  bool expiryArmed = false;

  ServiceState(Service *owner_, ServiceConfig config, ServiceHTTPFn http,
      AppID appID_, AppID managementAppID_) :
    owner{owner_}, config{ZuMv(config)}, http{ZuMv(http)},
    appID{appID_}, managementAppID{managementAppID_} { }

  ~ServiceState() {
    ZmAssert_(quiescent());
    if (config.clientSecret.mutable_()) ZuClear(config.clientSecret);
    if (config.introspectionSecret.mutable_()) ZuClear(config.introspectionSecret);
    if (config.ssf.callbackAuth.mutable_()) ZuClear(config.ssf.callbackAuth);
  }

  bool active_() const {
    if (startResult == 0) return false;
    switch (owner->state()) {
      case ZmEngineState::Starting:
      case ZmEngineState::StopPending:
      case ZmEngineState::Running: return true;
    }
    return false;
  }
  bool quiescent() const {
    return !(ingress.load_() & ServiceIngress::Count) && !inflight && !settlePosted &&
      !registerArmed && !expiryArmed && !renewing && !keysRefreshing;
  }

  bool admit_() {
    uint64_t count;
    do {
      count = ingress.load_();
      if (count & ServiceIngress::Closed) return false;
    } while (ingress.cmpXch(count + 1, count) != count);
    return true;
  }
  void seal_() { ingress |= ServiceIngress::Closed; }

  template <typename Data, typename Done, typename L>
  void submit_(Data data, Done done, L &&fn) {
    using Action = ServiceAction<Data, Done>;
    ZmRef<Action> action = new Action{ZuMv(data), ZuMv(done)};
    post([this, action = ZuMv(action), fn = ZuFwd<L>(fn)]() mutable {
      fn(ZuMv(action->data), ZuMv(action->done));
      action = nullptr;
      --ingress;
      settle_();
    });
  }

  template <typename L> void post(L &&fn) {
    config.scheduler->run(ZuFwd<L>(fn), config.sid);
  }

  void send_(ServiceHTTPRequest request, ServiceHTTPResultFn complete)
  {
    if (!active_()) {
      complete(ServiceHTTPResponse{});
      return;
    }
    ++inflight;
    request.timeout = config.requestTimeout;
    http(ZuMv(request), [this, complete = ZuMv(complete)](
        ServiceHTTPResponse response) mutable {
      using Action = ServiceAction<ServiceHTTPResponse, ServiceHTTPResultFn>;
      ZmRef<Action> action = new Action{ZuMv(response), ZuMv(complete)};
      post([this, action = ZuMv(action)]() mutable {
        if (action->data.body.length() > config.responseMax) action->data = {};
        action->done(ZuMv(action->data));
        action = nullptr; // release callback captures before lifecycle completion
        --inflight;
        settle_();
      });
    });
  }

  void start_0()
  {
    ZmAssert_(quiescent());
    startupError = ServiceError::OK;
    startResult = -1;
    discovery_0([this](bool ok) {
      if (!ok) { start_2(ServiceError::Invalid); return; }
      authenticate([this](bool ok) {
	if (!ok) { start_2(ServiceError::Unavailable); return; }
	keys_(workloadManagement, [this](bool ok) {
	  if (!ok || !verifyWorkload()) {
	    start_2(ServiceError::Unauthorized);
	    return;
	  }
	  if (config.ssf.enabled &&
	      config.ssf.transmitterIssuer == config.managementIssuerURL) {
	    ServiceKeySet next;
	    if (managementKeys) {
	      next = serviceKeys(managementKeys->count_());
	      auto i = managementKeys->citer();
	      while (auto key = i()) next->add(*key);
	    }
	    ssfKeys = ZuMv(next);
	  }
	  keys_(false, [this](bool ok) {
	    if (!ok) { start_2(ServiceError::Unauthorized); return; }
	    if (!config.ssf.enabled ||
		config.ssf.transmitterIssuer == config.managementIssuerURL) {
	      start_1();
	      return;
	    }
	    ssfKeys_([this](bool ok) {
	      if (!ok) { start_2(ServiceError::Unauthorized); return; }
	      start_1();
	    });
	  });
	});
      });
    });
  }

  void start_1()
  {
    if (!config.ssf.enabled) { start_2(ServiceError::OK); return; }
    register_([self = this](bool ok) mutable {
      self->start_2(ok ? ServiceError::OK : ServiceError::Unavailable);
    });
  }

  void register_(ServiceReadyFn complete)
  {
    token_0([self = this, complete = ZuMv(complete)](
        bool ok) mutable {
      if (!ok || !self->active_()) { complete(false); return; }
      String url = endpoint(self->config.managementURL, "/admin/apps/");
      url << self->appID << "/ssf";
      String body;
      ZfJSON::save(body, SSFRegistration{
        .receiverID = self->config.ssf.receiverID,
        .deliveryURL = self->config.ssf.deliveryURL,
        .callbackAuth = self->config.ssf.callbackAuth,
        .expiresIn = self->config.ssf.lease});
      self->send_(ServiceHTTPRequest{.method = ServiceMethod::POST,
        .url = ZuMv(url),
        .authorization = String{"Bearer "} << self->accessToken,
        .contentType = "application/json", .body = ZuMv(body)},
        [self, complete = ZuMv(complete)](
            ServiceHTTPResponse response) mutable {
          SSFLease lease;
          bool ok = self->active_() && response.status == 200 &&
            jsonLoad(response.body, self->config.responseMax, lease) &&
            lease.expiresIn >= 2 && lease.expires > Zm::now().sec();
          if (ok) self->registerArm(lease.expiresIn>>1);
          complete(ok);
        });
    });
  }

  void registerArm(uint32_t delay)
  {
    if (!active_()) return;
    if (registerArmed) config.scheduler->del(&registerTimer);
    registerArmed = true;
    config.scheduler->add(&registerTimer, Zm::now() + ZuTime{double(delay)},
      ZmScheduler::Update, [this](auto &&arm) {
        return arm([this]() {
          registerArmed = false;
          if (!active_()) return;
          register_([self = this](bool ok) mutable {
            if (!ok) self->registerArm(1);
          });
        });
      }, config.sid);
  }

  void authenticate(ServiceReadyFn complete)
  {
    authenticateURL(tokenURL, true, ZuMv(complete));
  }

  void authenticateURL(String url, bool management, ServiceReadyFn complete)
  {
    String form{"grant_type=client_credentials&scope=zum.catalog"};
    send_(ServiceHTTPRequest{.method = ServiceMethod::POST,
      .url = ZuMv(url),
      .authorization = basicAuth(config.clientID, config.clientSecret),
      .contentType = "application/x-www-form-urlencoded",
      .body = ZuMv(form)}, [self = this, management,
        complete = ZuMv(complete)](ServiceHTTPResponse response) mutable {
      String token;
      uint64_t expires = 0;
      bool ok = response.status == 200 && tokenResponse(
        ZuMv(response.body), self->config.responseMax, token, expires) &&
        expires <= uint64_t(INT64_MAX - Zm::now().sec());
      if (ok) {
        if (self->accessToken.mutable_())
          ZuClear(self->accessToken);
        self->accessToken = ZuMv(token);
        self->accessExpires = Zm::now().sec() + int64_t(expires);
	self->workloadManagement = management;
      }
      if (!ok && management && self->resourceTokenURL &&
	  self->resourceTokenURL != self->tokenURL) {
	self->authenticateURL(self->resourceTokenURL, false, ZuMv(complete));
	return;
      }
      complete(ok);
    });
  }

  void discovery_0(ServiceReadyFn complete)
  {
    String url, managementURL;
    if (!serviceIssuer(config.issuerURL, &url) ||
        !serviceIssuer(config.managementIssuerURL, &managementURL)) {
      complete(false);
      return;
    }
    send_(ServiceHTTPRequest{.url = ZuMv(url)}, [self = this,
        managementURL = ZuMv(managementURL),
        complete = ZuMv(complete)](ServiceHTTPResponse response) mutable {
      String token, jwks;
      bool ok = response.status == 200 && discoveryResponse(
        ZuMv(response.body), self->config.responseMax,
        self->config.issuerURL, token, jwks);
      ok = ok && serviceURL(token, false) && serviceURL(jwks, false);
      if (!ok) { complete(false); return; }
      self->resourceTokenURL = ZuMv(token);
      self->jwksURL = ZuMv(jwks);
      self->discovery_1(ZuMv(managementURL), [self,
          complete = ZuMv(complete)](bool ok) mutable {
        if (!ok) { complete(false); return; }
        self->discovery_2(ZuMv(complete));
      });
    });
  }

  void discovery_1(String url, ServiceReadyFn complete)
  {
    send_(ServiceHTTPRequest{.url = ZuMv(url)}, [self = this,
        complete = ZuMv(complete)](ServiceHTTPResponse response) mutable {
      String token, jwks;
      bool ok = response.status == 200 && discoveryResponse(
        ZuMv(response.body), self->config.responseMax,
        self->config.managementIssuerURL, token, jwks);
      ok = ok && serviceURL(token, false) && serviceURL(jwks, false);
      if (ok) {
	self->tokenURL = ZuMv(token);
	self->managementJWKSURL = ZuMv(jwks);
      }
      complete(ok);
    });
  }

  void discovery_2(ServiceReadyFn complete)
  {
    if (!config.ssf.enabled) { complete(true); return; }
    if (config.ssf.transmitterIssuer == config.managementIssuerURL) {
      ssfJWKSURL = managementJWKSURL;
      complete(bool(ssfJWKSURL));
      return;
    }
    String url;
    if (!serviceIssuer(config.ssf.transmitterIssuer, &url)) {
      complete(false);
      return;
    }
    send_(ServiceHTTPRequest{.url = ZuMv(url)}, [self = this,
        complete = ZuMv(complete)](ServiceHTTPResponse response) mutable {
      String token, jwks;
      bool ok = response.status == 200 && discoveryResponse(
        ZuMv(response.body), self->config.responseMax,
        self->config.ssf.transmitterIssuer, token, jwks);
      ok = ok && serviceURL(token, false) && serviceURL(jwks, false);
      if (ok) self->ssfJWKSURL = ZuMv(jwks);
      complete(ok);
    });
  }

  void keys_(bool management, ServiceReadyFn complete)
  {
    String url = management ? managementJWKSURL : jwksURL;
    send_(ServiceHTTPRequest{.url = ZuMv(url)}, [self = this,
        management, complete = ZuMv(complete)](
        ServiceHTTPResponse response) mutable {
      auto keys = response.status == 200 ? jwksResponse(ZuMv(response.body),
	self->config.responseMax, self->config.keyMax) : ServiceKeySet{};
      bool ok = !!keys;
      if (ok) {
        if (management)
	  self->managementKeys = ZuMv(keys);
        else {
	  self->keys = ZuMv(keys);
          self->keysLoaded = Zm::now().sec();
        }
      }
      complete(ok);
    });
  }

  void ssfKeys_(ServiceReadyFn complete)
  {
    send_(ServiceHTTPRequest{.url = ZuMv(ssfJWKSURL)},
      [self = this, complete = ZuMv(complete)](
          ServiceHTTPResponse response) mutable {
	auto next = response.status == 200 ? jwksResponse(
          ZuMv(response.body), self->config.responseMax,
	  self->config.keyMax) : ServiceKeySet{};
	bool ok = !!next;
	if (ok) self->ssfKeys = ZuMv(next);
        complete(ok);
      });
  }

  static const ServiceKey *key_(const ServiceKeySet &keys, ZuCSpan id)
  {
    return keys ? keys->find(id) : nullptr;
  }

  bool verify_(ZuCSpan token, Principal &principal)
  {
    JWTHeader header;
    if (!jwtHeader(token, config.jwtLimits, header)) return false;
    auto key = key_(keys, header.keyID);
    return key && jwtVerify(token, header.keyID, config.issuerURL,
      config.audience, key->publicKey, Zm::now().sec(), config.jwtLimits,
      principal) && principal.appID == appID;
  }

  void expire_()
  {
    int64_t now = Zm::now().sec();
    while (seenExpiries.count_()) {
      auto key = seenExpiries.minimumKey();
      if (key.expires > now) break;
      seen.delVal(key.id);
      seenExpiries.del(key);
    }
  }

  void scheduleExpiry()
  {
    if (expiryArmed) {
      config.scheduler->del(&expiryTimer);
      expiryArmed = false;
    }
    int64_t deadline = 0;
    if (seenExpiries.count_() && (!deadline ||
        seenExpiries.minimumKey().expires < deadline))
      deadline = seenExpiries.minimumKey().expires;
    if (!deadline) return;
    int64_t now = Zm::now().sec();
    if (deadline <= now) { expire_(); return; }
    expiryArmed = true;
    config.scheduler->add(&expiryTimer, ZuTime{deadline}, ZmScheduler::Update,
      [this](auto &&arm) {
        return arm([this]() {
          expiryArmed = false;
          if (!active_()) return;
          expire_();
          scheduleExpiry();
        });
      }, config.sid);
  }

  void refreshFn_(ServiceRefreshFn fn)
  {
    refreshFn = ZuMv(fn);
  }

  void receiveSET_(ServiceSETRequest request, ServiceSETDoneFn complete)
  {
    if (!active_() || !config.ssf.enabled || !complete ||
        request.body.length() > config.ssf.maxBytes ||
        !contentType(request.contentType) ||
        request.authorization != config.ssf.callbackAuth) {
      if (complete) complete(ServiceError::Invalid);
      return;
    }
    expire_();

    JWTHeader header;
    String claims;
    if (!jwtHeader(request.body, config.jwtLimits, header) ||
        header.type != "secevent+jwt") {
      complete(ServiceError::Unauthorized);
      return;
    }
    auto key = key_(ssfKeys, header.keyID);
    if (!key || !jwtES256(request.body, key->publicKey, config.jwtLimits,
        header, claims)) {
      complete(ServiceError::Unauthorized);
      return;
    }

    SETClaims value;
    if (!jsonLoad(claims, config.jwtLimits.json, value)) {
      complete(ServiceError::Invalid);
      return;
    }
    int64_t now = Zm::now().sec();
    if (value.issuerURL != config.ssf.transmitterIssuer ||
        value.audience != config.ssf.audience || !value.id ||
        value.issued <= 0 ||
        value.issued > now + int64_t(config.ssf.clockSkew) ||
        value.issued < now - int64_t(config.ssf.clockSkew)) {
      complete(ServiceError::Unauthorized);
      return;
    }
    if (seen.find(value.id)) {
      complete(ServiceError::OK);
      return;
    }
    if (seen.count_() >= config.ssf.dedupMax) {
      complete(ServiceError::Unavailable);
      return;
    }

    auto &subject = value.events.revoked.subject;
    if (subject.format != "opaque" || !subject.issuerURL ||
        !subject.familyID || value.id == subject.familyID ||
        subject.issuerURL != config.issuerURL || subject.expires <= now) {
      complete(ServiceError::Invalid);
      return;
    }

    seenExpiries.add(SETSeenExpiry{subject.expires, value.id});
    seen.add(SETSeen{
      .id = ZuMv(value.id), .expires = subject.expires});
    if (refreshFn) refreshFn(RefreshID{
      .issuerURL = ZuMv(subject.issuerURL), .familyID = ZuMv(subject.familyID)},
      subject.expires);
    scheduleExpiry();
    complete(ServiceError::OK);
  }


  void refreshKeys(ServiceReadyFn complete)
  {
    keyWaiters.push(ZuMv(complete));
    if (keysRefreshing) return;
    int64_t now = Zm::now().sec();
    if (keysLoaded && now - keysLoaded < config.jwksRefresh) {
      completeAll(keyWaiters, false);
      return;
    }
    keysRefreshing = true;
    keys_(false, [self = this](bool ok) mutable {
      self->keysRefreshing = false;
      completeAll(self->keyWaiters, ok);
    });
  }

  bool verifyWorkload()
  {
    JWTHeader header;
    if (!jwtHeader(accessToken, config.jwtLimits, header)) return false;
    const auto &keys = workloadManagement ? managementKeys : this->keys;
    auto key = key_(keys, header.keyID);
    Principal principal;
    String issuerURL = workloadManagement ? config.managementIssuerURL :
      config.issuerURL;
    String audience = workloadManagement ?
      endpoint(config.managementURL, "/admin") : config.audience;
    if (!key || !jwtVerify(accessToken, header.keyID,
        issuerURL,
        audience, key->publicKey, Zm::now().sec(), config.jwtLimits,
        principal) || principal.authMethod ||
        principal.clientID != config.clientID ||
        principal.appID != (workloadManagement ? managementAppID : appID))
      return false;
    if (accessExpires > principal.expires) accessExpires = principal.expires;
    return true;
  }

  void token_1(bool ok)
  {
    renewing = false;
    completeAll(tokenWaiters, ok);
  }

  void token_0(ServiceReadyFn complete)
  {
    if (!active_()) { complete(false); return; }
    int64_t now = Zm::now().sec();
    if (accessToken && accessExpires > now + config.renewSkew) {
      complete(true);
      return;
    }
    tokenWaiters.push(ZuMv(complete));
    if (renewing) return;
    renewing = true;
    authenticate([self = this](bool ok) mutable {
      if (!ok || !self->active_()) { self->token_1(false); return; }
      self->keys_(true, [self](bool ok) mutable {
        self->token_1(ok && self->active_() &&
          self->verifyWorkload());
      });
    });
  }

  void introspect(String token, ServiceVerifyFn complete)
  {
    if (!config.introspectionURL || !config.introspectionClientID ||
        !config.introspectionSecret) {
      if (token.mutable_()) ZuClear(token);
      complete(ServiceError::Unauthorized, ServicePrincipal{});
      return;
    }
    String form{"token="};
    ZfURI::PathQuote::quote(form, token);
    send_(ServiceHTTPRequest{.method = ServiceMethod::POST,
      .url = config.introspectionURL,
      .authorization = basicAuth(config.introspectionClientID,
        config.introspectionSecret),
      .contentType = "application/x-www-form-urlencoded",
      .body = ZuMv(form)}, [self = this,
        token = ZuMv(token), complete = ZuMv(complete)](
        ServiceHTTPResponse response) mutable {
      Introspection value;
      bool parsed = response.status == 200 &&
        jsonLoad(response.body, self->config.responseMax, value);
      int64_t now = Zm::now().sec();
      bool valid = parsed && value.active && value.issuerURL == self->config.issuerURL &&
        value.audience == self->config.audience && value.jti &&
        value.expires > now && (!value.appID || value.appID == self->appID) &&
        value.subject;
      if (!valid) {
        if (token.mutable_()) ZuClear(token);
        int error = parsed ? ServiceError::Unauthorized :
          serviceStatus(response.status);
        if (error == ServiceError::OK) error = ServiceError::Unauthorized;
        complete(error, ServicePrincipal{});
        return;
      }
      TokenID tokenID{.issuerURL = ZuMv(value.issuerURL), .jti = ZuMv(value.jti)};
      String authMethod;
      if (value.amr.length() == 1 && userAuthMethod(value.amr[0]))
        authMethod = ZuMv(value.amr[0]);
      else if (!value.amr && value.grantType == "client_credentials")
        authMethod = "client_credentials";
      if (token.mutable_()) ZuClear(token);
      complete(ServiceError::OK, ServicePrincipal{
        .tokenID = ZuMv(tokenID), .subject = ZuMv(value.subject),
        .appID = self->appID, .audience = self->config.audience,
        .actions = ZuMv(value.actions), .expires = value.expires,
        .authMethod = ZuMv(authMethod)});
    });
  }

  void verify_(String token, ServiceVerifyFn complete)
  {
    if (!active_() || !token) {
      if (token.mutable_()) ZuClear(token);
      complete(active_() ? ServiceError::Invalid : ServiceError::Stopped,
        ServicePrincipal{});
      return;
    }
    Principal principal;
    if (verify_(token, principal)) {
      if (token.mutable_()) ZuClear(token);
      complete(ServiceError::OK, ServicePrincipal{
        .tokenID = ZuMv(principal.tokenID),
        .subject = ZuMv(principal.subject), .appID = principal.appID,
        .audience = config.audience, .actions = ZuMv(principal.actions),
        .expires = principal.expires,
        .authMethod = principal.authMethod ? ZuMv(principal.authMethod) :
          String{"client_credentials"}});
      return;
    }
    JWTHeader header;
    if (!jwtHeader(token, config.jwtLimits, header) ||
        key_(keys, header.keyID)) {
      if (token.mutable_()) ZuClear(token);
      complete(ServiceError::Unauthorized, ServicePrincipal{});
      return;
    }
    refreshKeys([self = this, token = ZuMv(token),
        complete = ZuMv(complete)](bool ok) mutable {
      Principal principal;
      if (!self->active_()) {
        if (token.mutable_()) ZuClear(token);
        complete(ServiceError::Stopped, ServicePrincipal{});
        return;
      }
      if (ok && self->verify_(token, principal)) {
        if (token.mutable_()) ZuClear(token);
        complete(ServiceError::OK, ServicePrincipal{
          .tokenID = ZuMv(principal.tokenID),
          .subject = ZuMv(principal.subject), .appID = principal.appID,
          .audience = self->config.audience,
          .actions = ZuMv(principal.actions), .expires = principal.expires,
          .authMethod = principal.authMethod ? ZuMv(principal.authMethod) :
            String{"client_credentials"}});
        return;
      }
      JWTHeader refreshed;
      if (!jwtHeader(token, self->config.jwtLimits, refreshed) ||
          self->key_(self->keys, refreshed.keyID)) {
        if (token.mutable_()) ZuClear(token);
        complete(ServiceError::Unauthorized, ServicePrincipal{});
        return;
      }
      self->introspect(ZuMv(token), ZuMv(complete));
    });
  }

  void publish_(ServiceManifest manifest, ServiceProtocolFn complete)
  {
    token_0([self = this, manifest = ZuMv(manifest),
        complete = ZuMv(complete)](bool ok) mutable {
      if (!ok || !manifest.revision) {
        complete(ServiceProtocolResult{.error = ok ? ServiceError::Invalid :
          self->active_() ? ServiceError::Unavailable :
            ServiceError::Stopped});
        return;
      }
      String url = endpoint(self->config.managementURL, "/admin/apps/");
      url << self->appID << "/catalog";
      String etag{"\"catalog-"};
      etag << (manifest.revision - 1) << '"';
      String idem{self->config.clientID};
      idem << ':' << manifest.revision;
      self->send_(ServiceHTTPRequest{.method = ServiceMethod::PUT,
        .url = ZuMv(url),
        .authorization = String{"Bearer "} << self->accessToken,
        .contentType = "application/json", .ifMatch = ZuMv(etag),
        .idempotence = ZuMv(idem), .manifest = ZuMv(manifest)},
        [complete = ZuMv(complete)](ServiceHTTPResponse response) mutable {
          complete(ServiceProtocolResult{.body = ZuMv(response.body),
            .status = response.status, .error = serviceStatus(response.status)});
        });
    });
  }

  void start_2(int error)
  {
    startupError = error;
    startResult = error == ServiceError::OK;
    if (!startResult) cancelTimers_();
    settle_();
  }

  void stop_()
  {
    cancelTimers_();
    // Drain the timer owner's already-posted callbacks before publication.
    settle_();
  }

  void cancelTimers_()
  {
    config.scheduler->del(&registerTimer);
    registerArmed = false;
    config.scheduler->del(&expiryTimer);
    expiryArmed = false;
  }

  static void clearKeys_(ServiceKeySet &set, unsigned &left)
  {
    if (!set) return;
    {
      auto i = set->iter();
      while (left && i()) { --left; i.del(); }
    }
    if (!set->count_()) set = nullptr;
  }

  bool clearRuntime()
  {
    unsigned left = ServiceWork::Batch;
    clearKeys_(keys, left);
    clearKeys_(managementKeys, left);
    clearKeys_(ssfKeys, left);
    while (left && seenExpiries.count_()) {
      --left;
      auto key = seenExpiries.minimumKey();
      seen.delVal(key.id);
      seenExpiries.del(key);
    }
    if (keys || managementKeys || ssfKeys || seenExpiries.count_()) return false;
    ZmAssert_(!seen.count_() && !tokenWaiters.length() && !keyWaiters.length());
    if (accessToken.mutable_()) ZuClear(accessToken);
    accessToken.null();
    accessExpires = keysLoaded = 0;
    workloadManagement = false;
    jwksURL.null();
    managementJWKSURL.null();
    ssfJWKSURL.null();
    tokenURL.null();
    resourceTokenURL.null();
    return true;
  }

  void settle_()
  {
    bool stopping = owner->stopping();
    if (settlePosted || (!stopping && startResult < 0)) return;
    settlePosted = true;
    post([this]() {
      settlePosted = false;
      if (inflight || (ingress.load_() & ServiceIngress::Count)) return;
      if (startResult == 1) {
        startResult = -1;
        ingress = 0;
        owner->started(true);
        return;
      }
      if (!clearRuntime()) { settle_(); return; }
      bool failedStart = startResult == 0;
      startResult = -1;
      if (failedStart) owner->started(false);
      else owner->stopped(true);
    });
  }

};

Service::Service() = default;
Service::~Service() { final(); }

bool Service::init(ServiceConfig config, ServiceHTTPFn http)
{
  AppID appID = 0, managementAppID = 0;
  if (m_state || !config.scheduler || !config.sid ||
      config.sid > config.scheduler->params().nThreads() ||
      !config.issuerURL || !config.managementIssuerURL ||
      !config.managementURL || !config.clientID ||
      !config.clientSecret || !config.audience || !config.requestTimeout ||
      !config.responseMax || !config.keyMax || !http ||
      !serviceIssuer(config.issuerURL, nullptr, &appID) ||
      !serviceIssuer(config.managementIssuerURL, nullptr, &managementAppID) ||
      !serviceURL(config.managementURL, true) ||
      (config.introspectionURL && (!serviceURL(config.introspectionURL, false) ||
        !config.introspectionClientID || !config.introspectionSecret)) ||
      (config.ssf.enabled && (!config.ssf.receiverID ||
        !serviceURL(config.ssf.deliveryURL, false) ||
        !config.ssf.callbackAuth || config.ssf.lease < 2 ||
        !config.ssf.transmitterIssuer ||
        !serviceIssuer(config.ssf.transmitterIssuer) ||
        !config.ssf.audience ||
        !config.ssf.maxBytes || !config.ssf.dedupMax))) return false;
  m_state = new ServiceState{
    this, ZuMv(config), ZuMv(http), appID, managementAppID};
  return true;
}

void Service::start_()
{
  if (!m_state) { Engine::started(false); return; }
  auto state = m_state.ptr();
  state->post([state]() { state->start_0(); });
}

void Service::stop_()
{
  auto state = m_state.ptr();
  state->seal_();
  state->post([state]() { state->stop_(); });
}

int Service::startError() const
{
  return m_state ? m_state->startupError : ServiceError::Invalid;
}

void Service::verify(String accessToken, ServiceVerifyFn complete)
{
  if (!complete) return;
  if (!m_state || Engine::state() != ZmEngineState::Running || !m_state->admit_()) {
    if (accessToken.mutable_()) ZuClear(accessToken);
    complete(ServiceError::Stopped, ServicePrincipal{});
    return;
  }
  auto state = m_state.ptr();
  state->submit_(ZuMv(accessToken), ZuMv(complete),
    [state](String token, ServiceVerifyFn complete) {
      state->verify_(ZuMv(token), ZuMv(complete));
    });
}

void Service::refreshFn(ServiceRefreshFn fn)
{
  if (!m_state) return;
  auto state = m_state.ptr();
  if (Engine::state() == ZmEngineState::Stopped) {
    state->refreshFn_(ZuMv(fn));
    return;
  }
  if (Engine::state() != ZmEngineState::Running || !state->admit_()) return;
  state->submit_(ZuMv(fn), ZuVoid{},
    [state](ServiceRefreshFn fn, ZuVoid) { state->refreshFn_(ZuMv(fn)); });
}

void Service::receiveSET(ServiceSETRequest request, ServiceSETDoneFn complete)
{
  if (!complete) return;
  if (!m_state || Engine::state() != ZmEngineState::Running || !m_state->admit_()) {
    complete(ServiceError::Stopped);
    return;
  }
  auto state = m_state.ptr();
  state->submit_(ZuMv(request), ZuMv(complete),
    [state](ServiceSETRequest request, ServiceSETDoneFn complete) {
      state->receiveSET_(ZuMv(request), ZuMv(complete));
    });
}

void Service::publish(ServiceManifest manifest, ServiceProtocolFn complete)
{
  if (!complete) return;
  if (!m_state || Engine::state() != ZmEngineState::Running || !m_state->admit_()) {
    complete(ServiceProtocolResult{.error = ServiceError::Stopped});
    return;
  }
  auto state = m_state.ptr();
  state->submit_(ZuMv(manifest), ZuMv(complete),
    [state](ServiceManifest manifest, ServiceProtocolFn complete) {
      state->publish_(ZuMv(manifest), ZuMv(complete));
    });
}

void Service::final()
{
  ZmAssert_(Engine::state() == ZmEngineState::Stopped);
  if (m_state) ZmAssert_(m_state->quiescent());
  m_state = nullptr;
}

} // namespace Zum
