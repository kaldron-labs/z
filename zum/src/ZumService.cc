//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumService.hh>
#include <zlib/ZumURI.hh>

#include <zlib/ZuBase64.hh>
#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuLib.hh>
#include <zlib/ZuPtr.hh>

#include <zlib/ZmHash.hh>
#include <zlib/ZmRBTree.hh>

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
ZuDerive(ServiceDoneVec, (ZtArray<ServiceDoneFn,
  ZtArrayHeapID<"Zum.Service.DoneVec">>));

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
ZuDerive(ServiceKeyVec, (ZtArray<ServiceKey,
  ZtArrayHeapID<"Zum.Service.KeyVec">>));

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
  String issuer;
  String familyID;
  int64_t expires = 0;
};
ZfStruct(, (SETSubject, JSON),
  (((format),		(Required)),	(String)),
  (((issuer),		(JSON::ID<"iss">, Required)),	(String)),
  (((familyID),		(JSON::ID<"family_id">, Required)),	(String)),
  (((expires),		(JSON::ID<"exp">, Required)),	(Int64)));

struct SETEvent {
  SETSubject subject;
};
ZfStruct(, (SETEvent, JSON),
  (((subject),		(Required)),	(UDT)));

struct SETEvents {
  SETEvent revoked;
};
ZfStruct(, (SETEvents, JSON),
  (((revoked),
    (JSON::ID<"urn:zum:events:refresh-token-revoked">, Required)),
    (UDT)));

struct SETClaims {
  String issuer;
  String audience;
  String id;
  int64_t issued = 0;
  SETEvents events;
};
ZfStruct(, (SETClaims, JSON),
  (((issuer),		(JSON::ID<"iss">, Required)),	(String)),
  (((audience),		(JSON::ID<"aud">, Required)),	(String)),
  (((id),		(JSON::ID<"jti">, Required)),	(String)),
  (((issued),		(JSON::ID<"iat">, Required)),	(Int64)),
  (((events),		(Required)),	(UDT)));

struct Introspection {
  bool active = false;
  String issuer;
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
  (((active),		(Required)),				(Bool)),
  (((issuer),		(JSON::ID<"iss">, JSON::Opt)),	(String)),
  (((subject),		(JSON::ID<"sub">, JSON::Opt)),	(String)),
  (((clientID),		(JSON::ID<"client_id">, JSON::Opt)), (String)),
  (((audience),		(JSON::ID<"aud">, JSON::Opt)),	(String)),
  (((jti),		(JSON::Opt)),				(String)),
  (((scope),		(JSON::Opt)),				(String)),
  (((actions),		(JSON::Opt)),				(StringVec)),
  (((appID),		(JSON::ID<"zum_app_id">, JSON::String<>, JSON::Opt)), (UInt64)),
  (((expires),		(JSON::ID<"exp">, JSON::Opt)),	(Int64)),
  (((amr),		(JSON::Opt)),			(StringVec)),
  (((grantType),		(JSON::ID<"grant_type">, JSON::Opt)), (String)));

struct Token {
  String accessToken;
  uint64_t expiresIn = 0;
};
ZfStruct(, (Token, JSON),
  (((accessToken),	(JSON::ID<"access_token">, Required)),	(String)),
  (((expiresIn),	(JSON::ID<"expires_in">, Required)),	(UInt64)));

struct Discovery {
  String issuer;
  String tokenEndpoint;
  String jwksURI;
};
ZfStruct(, (Discovery, JSON),
  (((issuer),		(Required)),	(String)),
  (((tokenEndpoint),	(JSON::ID<"token_endpoint">, Required)),	(String)),
  (((jwksURI),		(JSON::ID<"jwks_uri">, Required)),	(String)));

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
  (((alg),		(JSON::Opt)),	(String)),
  (((crv),		(Required)),	(String)),
  (((kid),		(Required)),	(String)),
  (((kty),		(Required)),	(String)),
  (((use),		(JSON::Opt)),	(String)),
  (((x),		(Required)),	(String)),
  (((y),		(Required)),	(String)));
ZuDerive(JWKWireArray,
  (ZtArray<JWK, ZtArrayHeapID<"Zum.Service.JWKs">>));
struct JWKWireVec : public JWKWireArray {
  ZuDerive_(JWKWireVec, JWKWireArray);
  friend ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(JWKWireVec *);
};
struct JWKSResponse { JWKWireVec keys; };
ZfStruct(, (JWKSResponse, JSON),
  (((keys),		(Required)),	(UDT)));

template <typename T>
static bool jsonLoad(String &json, unsigned limit, T &value)
{
  if (!json || json.length() > limit) return false;
  if (!json.mutable_()) json.length(json.length());
  auto parsed = ZfJSON::scan({json.data(), json.length()});
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
    if (json.mutable_()) ZuClear(json.data(), json.length());
  }};
  Token wire;
  ZuGuard clearWire{[&wire]() {
    if (wire.accessToken.mutable_())
      ZuClear(wire.accessToken.data(), wire.accessToken.length());
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
  if (!jsonLoad(json, limit, wire) || wire.issuer != expectedIssuer ||
      !wire.tokenEndpoint || !wire.jwksURI) return false;
  token = ZuMv(wire.tokenEndpoint);
  jwks = ZuMv(wire.jwksURI);
  return true;
}

static bool jwksResponse(
    String json, unsigned limit, unsigned keyMax, ServiceKeyVec &keys)
{
  JWKSResponse wire;
  if (!jsonLoad(json, limit, wire) || !wire.keys ||
      wire.keys.length() > keyMax) return false;
  ServiceKeyVec next;
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
    for (auto &existing: next)
      if (existing.id == key.id) return false;
    next.push(ZuMv(key));
  }
  if (!next) return false;
  keys = ZuMv(next);
  return true;
}

static String endpoint(ZuCSpan issuer, ZuCSpan path)
{
  String url{issuer};
  if (url && url[url.length() - 1] == '/') url.length(url.length() - 1);
  url << path;
  return url;
}

static String basicAuth(ZuCSpan clientID, ZuBSpan secret)
{
  String plain;
  ZfURI::PathQuote::quote(plain, clientID);
  plain << ':';
  bool safe = true;
  for (auto c: secret)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_' ||
          c == '.' || c == '~')) { safe = false; break; }
  if (safe) plain << ZuCSpan{secret};
  else ZfURI::PathQuote::quote(plain, secret);
  String encoded;
  encoded.length(ZuBase64::enclen(plain.length()));
  encoded.length(ZuBase64::encode(encoded.span(), ZuBSpan{plain}));
  if (plain.mutable_()) ZuClear(plain.data(), plain.length());
  String authorization{"Basic "};
  authorization << encoded;
  if (encoded.mutable_()) ZuClear(encoded.data(), encoded.length());
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
    ZuCSpan issuer, String *metadata = nullptr, AppID *appID = nullptr)
{
  if (!serviceURL(issuer, false)) return false;
  Zhttp::URL parsed{issuer};
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

template <typename Heap = ZuVoid>
struct ServiceState_ : public Heap, public ZmObject  {
  enum { Initial, Starting, Started, Stopping, Stopped };

  ServiceState_(ServiceConfig config, ServiceHTTPFn http,
      AppID appID_, AppID managementAppID_) :
    config{ZuMv(config)}, http{ZuMv(http)}, appID{appID_},
    managementAppID{managementAppID_} { }

  ~ServiceState_() { clear_(); }

  template <typename L> void post(L &&fn) {
    config.scheduler->run(ZuFwd<L>(fn), config.sid);
  }

  void send_(ServiceHTTPRequest request, ServiceHTTPResultFn complete)
  {
    if (state == Stopping || state == Stopped) {
      complete(ServiceHTTPResponse{});
      return;
    }
    ++inflight;
    request.timeout = config.requestTimeout;
    http(ZuMv(request), [self = ZmRef<ServiceState_>{this},
        complete = ZuMv(complete)](ServiceHTTPResponse response) mutable {
      self->post([self = ZuMv(self), response = ZuMv(response),
          complete = ZuMv(complete)]() mutable {
        if (self->inflight) --self->inflight;
        if (response.body.length() > self->config.responseMax) response = {};
        complete(ZuMv(response));
        self->stopped_();
      });
    });
  }

  void start_(ServiceDoneFn complete)
  {
    if (state != Initial) { complete(ServiceError::Invalid); return; }
    state = Starting;
    discovery_([self = ZmRef<ServiceState_>{this},
        complete = ZuMv(complete)](bool ok) mutable {
      if (self->state != Starting) { complete(ServiceError::Stopped); return; }
      if (!ok) { self->state = Initial; complete(ServiceError::Invalid); return; }
      self->authenticate_([self = ZuMv(self),
          complete = ZuMv(complete)](bool ok) mutable {
        if (self->state != Starting) { complete(ServiceError::Stopped); return; }
        if (!ok) { self->state = Initial; complete(ServiceError::Unavailable); return; }
	self->keys_(self->workloadManagement,
	 [self = ZuMv(self), complete = ZuMv(complete)](
            bool ok) mutable {
          if (self->state != Starting) { complete(ServiceError::Stopped); return; }
          if (!ok || !self->verifyWorkload_()) {
            self->state = Initial;
            complete(ServiceError::Unauthorized);
            return;
          }
          if (self->config.ssf.enabled &&
              self->config.ssf.transmitterIssuer ==
                self->config.managementIssuerURL)
            self->ssfKeys = self->managementKeys;
          self->keys_(false, [self = ZuMv(self),
              complete = ZuMv(complete)](bool ok) mutable {
            if (self->state != Starting) {
              complete(ServiceError::Stopped);
              return;
            }
            if (!ok) {
              self->state = Initial;
              complete(ServiceError::Unauthorized);
              return;
            }
            if (!self->config.ssf.enabled ||
                self->config.ssf.transmitterIssuer ==
                  self->config.managementIssuerURL) {
              self->state = Started;
              complete(ServiceError::OK);
              return;
            }
            self->ssfKeys_([self = ZuMv(self), complete = ZuMv(complete)](
                bool ok) mutable {
              if (self->state != Starting) {
                complete(ServiceError::Stopped);
                return;
              }
              if (!ok) {
                self->state = Initial;
                complete(ServiceError::Unauthorized);
                return;
              }
              self->state = Started;
              complete(ServiceError::OK);
            });
          });
        });
      });
    });
  }

  void authenticate_(ServiceReadyFn complete)
  {
    authenticateURL_(tokenURL, true, ZuMv(complete));
  }

  void authenticateURL_(String url, bool management, ServiceReadyFn complete)
  {
    String form{"grant_type=client_credentials&scope=zum.catalog"};
    send_(ServiceHTTPRequest{.method = ServiceMethod::POST,
      .url = ZuMv(url),
      .authorization = basicAuth(config.clientID, config.clientSecret),
      .contentType = "application/x-www-form-urlencoded",
      .body = ZuMv(form)}, [self = ZmRef<ServiceState_>{this}, management,
        complete = ZuMv(complete)](ServiceHTTPResponse response) mutable {
      String token;
      uint64_t expires = 0;
      bool ok = response.status == 200 && tokenResponse(
        ZuMv(response.body), self->config.responseMax, token, expires) &&
        expires <= uint64_t(INT64_MAX - Zm::now().sec());
      if (ok) {
        if (self->accessToken.mutable_())
          ZuClear(self->accessToken.data(), self->accessToken.length());
        self->accessToken = ZuMv(token);
        self->accessExpires = Zm::now().sec() + int64_t(expires);
	self->workloadManagement = management;
      }
      if (!ok && management && self->resourceTokenURL &&
	  self->resourceTokenURL != self->tokenURL) {
	self->authenticateURL_(self->resourceTokenURL, false, ZuMv(complete));
	return;
      }
      complete(ok);
    });
  }

  void discovery_(ServiceReadyFn complete)
  {
    String url, managementURL;
    if (!serviceIssuer(config.issuerURL, &url) ||
        !serviceIssuer(config.managementIssuerURL, &managementURL)) {
      complete(false);
      return;
    }
    send_(ServiceHTTPRequest{.url = ZuMv(url)}, [self = ZmRef<ServiceState_>{this},
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
      self->managementDiscovery_(ZuMv(managementURL), [self = ZuMv(self),
          complete = ZuMv(complete)](bool ok) mutable {
        if (!ok) { complete(false); return; }
        self->transmitterDiscovery_(ZuMv(complete));
      });
    });
  }

  void managementDiscovery_(String url, ServiceReadyFn complete)
  {
    send_(ServiceHTTPRequest{.url = ZuMv(url)}, [self = ZmRef<ServiceState_>{this},
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

  void transmitterDiscovery_(ServiceReadyFn complete)
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
    send_(ServiceHTTPRequest{.url = ZuMv(url)}, [self = ZmRef<ServiceState_>{this},
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
    send_(ServiceHTTPRequest{.url = ZuMv(url)}, [self = ZmRef<ServiceState_>{this},
        management, complete = ZuMv(complete)](
        ServiceHTTPResponse response) mutable {
      ServiceKeyVec keys;
      bool ok = response.status == 200 && jwksResponse(ZuMv(response.body),
        self->config.responseMax, self->config.keyMax, keys);
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
      [self = ZmRef<ServiceState_>{this}, complete = ZuMv(complete)](
          ServiceHTTPResponse response) mutable {
        ServiceKeyVec next;
        bool ok = response.status == 200 && jwksResponse(
          ZuMv(response.body), self->config.responseMax,
          self->config.keyMax, next);
        if (ok) self->ssfKeys = ZuMv(next);
        complete(ok);
      });
  }

  static const ServiceKey *key_(const ServiceKeyVec &keys, ZuCSpan id)
  {
    for (auto &key: keys) if (key.id == id) return &key;
    return nullptr;
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

  void scheduleExpiry_()
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
    config.scheduler->add(&expiryTimer,
      Zm::now() + ZuTime{double(deadline - now)}, ZmScheduler::Update,
      [this](auto &&arm) {
        return arm([this]() {
          expiryArmed = false;
          expire_();
          scheduleExpiry_();
        });
      });
  }

  void setRefreshRevocationFn_(ServiceRefreshFn fn)
  {
    refreshFn = ZuMv(fn);
  }

  void receiveSET_(ServiceSETRequest request, ServiceSETDoneFn complete)
  {
    if (state != Started || !config.ssf.enabled || !complete ||
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
    if (value.issuer != config.ssf.transmitterIssuer ||
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
    if (subject.format != "opaque" || !subject.issuer ||
        !subject.familyID || value.id == subject.familyID ||
        subject.issuer != config.issuerURL || subject.expires <= now) {
      complete(ServiceError::Invalid);
      return;
    }

    seen.add(SETSeen{
      .id = ZuMv(value.id), .expires = subject.expires});
    seenExpiries.add(SETSeenExpiry{subject.expires, value.id});
    if (refreshFn) refreshFn(RefreshID{
      .issuer = ZuMv(subject.issuer), .familyID = ZuMv(subject.familyID)},
      subject.expires);
    scheduleExpiry_();
    complete(ServiceError::OK);
  }


  void refreshKeys_(ServiceReadyFn complete)
  {
    keyWaiters.push(ZuMv(complete));
    if (keysRefreshing) return;
    int64_t now = Zm::now().sec();
    if (keysLoaded && now - keysLoaded < config.jwksRefresh) {
      completeAll(keyWaiters, false);
      return;
    }
    keysRefreshing = true;
    keys_(false, [self = ZmRef<ServiceState_>{this}](bool ok) mutable {
      self->keysRefreshing = false;
      completeAll(self->keyWaiters, ok);
    });
  }

  bool verifyWorkload_()
  {
    JWTHeader header;
    if (!jwtHeader(accessToken, config.jwtLimits, header)) return false;
    const auto &keys = workloadManagement ? managementKeys : this->keys;
    auto key = key_(keys, header.keyID);
    Principal principal;
    String issuer = workloadManagement ? config.managementIssuerURL :
      config.issuerURL;
    String audience = workloadManagement ?
      endpoint(config.managementURL, "/admin") : config.audience;
    if (!key || !jwtVerify(accessToken, header.keyID,
        issuer,
        audience, key->publicKey, Zm::now().sec(), config.jwtLimits,
        principal) || principal.authMethod ||
        principal.clientID != config.clientID ||
        principal.appID != (workloadManagement ? managementAppID : appID))
      return false;
    if (accessExpires > principal.expires) accessExpires = principal.expires;
    return true;
  }

  void renewed_(bool ok)
  {
    renewing = false;
    completeAll(tokenWaiters, ok);
  }

  void token_(ServiceReadyFn complete)
  {
    if (state != Started) { complete(false); return; }
    int64_t now = Zm::now().sec();
    if (accessToken && accessExpires > now + config.renewSkew) {
      complete(true);
      return;
    }
    tokenWaiters.push(ZuMv(complete));
    if (renewing) return;
    renewing = true;
    authenticate_([self = ZmRef<ServiceState_>{this}](bool ok) mutable {
      if (!ok || self->state != Started) { self->renewed_(false); return; }
      self->keys_(true, [self = ZuMv(self)](bool ok) mutable {
        self->renewed_(ok && self->state == Started &&
          self->verifyWorkload_());
      });
    });
  }

  void introspect_(String token, ServiceVerifyFn complete)
  {
    if (!config.introspectionURL || !config.introspectionClientID ||
        !config.introspectionSecret) {
      if (token.mutable_()) ZuClear(token.data(), token.length());
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
      .body = ZuMv(form)}, [self = ZmRef<ServiceState_>{this},
        token = ZuMv(token), complete = ZuMv(complete)](
        ServiceHTTPResponse response) mutable {
      Introspection value;
      bool parsed = response.status == 200 &&
        jsonLoad(response.body, self->config.responseMax, value);
      int64_t now = Zm::now().sec();
      bool valid = parsed && value.active && value.issuer == self->config.issuerURL &&
        value.audience == self->config.audience && value.jti &&
        value.expires > now && (!value.appID || value.appID == self->appID) &&
        value.subject;
      if (!valid) {
        if (token.mutable_()) ZuClear(token.data(), token.length());
        int error = parsed ? ServiceError::Unauthorized :
          serviceStatus(response.status);
        if (error == ServiceError::OK) error = ServiceError::Unauthorized;
        complete(error, ServicePrincipal{});
        return;
      }
      TokenID tokenID{.issuer = ZuMv(value.issuer), .jti = ZuMv(value.jti)};
      String authMethod;
      if (value.amr.length() == 1 &&
          (value.amr[0] == "passkey" || value.amr[0] == "oidc"))
        authMethod = ZuMv(value.amr[0]);
      else if (!value.amr && value.grantType == "client_credentials")
        authMethod = "client_credentials";
      if (token.mutable_()) ZuClear(token.data(), token.length());
      complete(ServiceError::OK, ServicePrincipal{
        .tokenID = ZuMv(tokenID), .subject = ZuMv(value.subject),
        .appID = self->appID, .audience = self->config.audience,
        .actions = ZuMv(value.actions), .expires = value.expires,
        .authMethod = ZuMv(authMethod)});
    });
  }

  void verify_(String token, ServiceVerifyFn complete)
  {
    if (state != Started || !token) {
      if (token.mutable_()) ZuClear(token.data(), token.length());
      complete(state == Started ? ServiceError::Invalid : ServiceError::Stopped,
        ServicePrincipal{});
      return;
    }
    Principal principal;
    if (verify_(token, principal)) {
      if (token.mutable_()) ZuClear(token.data(), token.length());
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
      if (token.mutable_()) ZuClear(token.data(), token.length());
      complete(ServiceError::Unauthorized, ServicePrincipal{});
      return;
    }
    refreshKeys_([self = ZmRef<ServiceState_>{this}, token = ZuMv(token),
        complete = ZuMv(complete)](bool ok) mutable {
      Principal principal;
      if (self->state != Started) {
        if (token.mutable_()) ZuClear(token.data(), token.length());
        complete(ServiceError::Stopped, ServicePrincipal{});
        return;
      }
      if (ok && self->verify_(token, principal)) {
        if (token.mutable_()) ZuClear(token.data(), token.length());
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
        if (token.mutable_()) ZuClear(token.data(), token.length());
        complete(ServiceError::Unauthorized, ServicePrincipal{});
        return;
      }
      self->introspect_(ZuMv(token), ZuMv(complete));
    });
  }

  void publish_(ServiceManifest manifest, ServiceProtocolFn complete)
  {
    token_([self = ZmRef<ServiceState_>{this}, manifest = ZuMv(manifest),
        complete = ZuMv(complete)](bool ok) mutable {
      if (!ok || !manifest.revision) {
        complete(ServiceProtocolResult{.error = ok ? ServiceError::Invalid :
          self->state == Started ? ServiceError::Unavailable :
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
        .idempotencyKey = ZuMv(idem), .manifest = ZuMv(manifest)},
        [complete = ZuMv(complete)](ServiceHTTPResponse response) mutable {
          complete(ServiceProtocolResult{.body = ZuMv(response.body),
            .status = response.status, .error = serviceStatus(response.status)});
        });
    });
  }

  void stop_(ServiceDoneFn complete)
  {
    if (state == Stopped) { complete(ServiceError::OK); return; }
    if (state == Stopping) { stopWaiters.push(ZuMv(complete)); return; }
    state = Stopping;
    stopWaiters.push(ZuMv(complete));
    stopped_();
  }

  void stopped_()
  {
    if (state != Stopping || inflight) return;
    state = Stopped;
    clear_();
    completeAll(stopWaiters, ServiceError::OK);
  }

  void clear_()
  {
    if (expiryArmed) {
      config.scheduler->del(&expiryTimer);
      expiryArmed = false;
    }
    if (accessToken.mutable_())
      ZuClear(accessToken.data(), accessToken.length());
    accessToken.null();
    if (config.clientSecret.mutable_())
      ZuClear(config.clientSecret.data(), config.clientSecret.length());
    if (config.introspectionSecret.mutable_())
      ZuClear(config.introspectionSecret.data(), config.introspectionSecret.length());
    if (config.ssf.callbackAuth.mutable_())
      ZuClear(config.ssf.callbackAuth.data(), config.ssf.callbackAuth.length());
  }

  ServiceConfig config;
  ServiceHTTPFn http;
  ServiceKeyVec keys;
  ServiceKeyVec managementKeys;
  ServiceKeyVec ssfKeys;
  SETSeenHash seen;
  SETSeenExpiries seenExpiries;
  ServiceRefreshFn refreshFn;
  ServiceReadyVec tokenWaiters;
  ServiceReadyVec keyWaiters;
  ServiceDoneVec stopWaiters;
  String jwksURL;
  String managementJWKSURL;
  String ssfJWKSURL;
  String tokenURL;
  String resourceTokenURL;
  String accessToken;
  bool workloadManagement = false;
  AppID appID = 0;
  AppID managementAppID = 0;
  int64_t accessExpires = 0;
  int64_t keysLoaded = 0;
  unsigned inflight = 0;
  int8_t state = Initial;
  bool renewing = false;
  bool keysRefreshing = false;
  ZmScheduler::Timer expiryTimer;
  bool expiryArmed = false;
};
using ServiceStateHeap = ZmHeap<"Zum.ZumService.ServiceState", ServiceState_<>>;
ZuDerive(ServiceState, (ServiceState_<ServiceStateHeap>));
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
        !config.ssf.callbackPath || !config.ssf.callbackAuth ||
        !config.ssf.transmitterIssuer ||
        !serviceIssuer(config.ssf.transmitterIssuer) ||
        !config.ssf.audience ||
        !config.ssf.maxBytes || !config.ssf.dedupMax))) return false;
  m_state = new ServiceState{
    ZuMv(config), ZuMv(http), appID, managementAppID};
  return true;
}

void Service::start(ServiceDoneFn complete)
{
  if (!m_state || !complete) return;
  auto state = m_state;
  state->post([state = ZuMv(state), complete = ZuMv(complete)]() mutable {
    state->start_(ZuMv(complete));
  });
}

void Service::verify(String accessToken, ServiceVerifyFn complete)
{
  if (!m_state || !complete) return;
  auto state = m_state;
  state->post([state = ZuMv(state), accessToken = ZuMv(accessToken),
      complete = ZuMv(complete)]() mutable {
    state->verify_(ZuMv(accessToken), ZuMv(complete));
  });
}

void Service::setRefreshRevocationFn(ServiceRefreshFn fn)
{
  if (!m_state) return;
  auto state = m_state;
  state->post([state = ZuMv(state), fn = ZuMv(fn)]() mutable {
    state->setRefreshRevocationFn_(ZuMv(fn));
  });
}

void Service::receiveSET(ServiceSETRequest request, ServiceSETDoneFn complete)
{
  if (!m_state || !complete) return;
  auto state = m_state;
  state->post([state = ZuMv(state), request = ZuMv(request),
      complete = ZuMv(complete)]() mutable {
    state->receiveSET_(ZuMv(request), ZuMv(complete));
  });
}

void Service::publish(ServiceManifest manifest, ServiceProtocolFn complete)
{
  if (!m_state || !complete) return;
  auto state = m_state;
  state->post([state = ZuMv(state), manifest = ZuMv(manifest),
      complete = ZuMv(complete)]() mutable {
    state->publish_(ZuMv(manifest), ZuMv(complete));
  });
}

void Service::stop(ServiceDoneFn complete)
{
  if (!m_state || !complete) return;
  auto state = m_state;
  state->post([state = ZuMv(state), complete = ZuMv(complete)]() mutable {
    state->stop_(ZuMv(complete));
  });
}

void Service::final()
{
  if (!m_state) return;
  m_state = nullptr;
}

} // namespace Zum
