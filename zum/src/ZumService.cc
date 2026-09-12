//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumService.hh>

#include <zlib/ZuBase64.hh>
#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuPtr.hh>

#include <zlib/ZfJSON.hh>
#include <zlib/ZfURI.hh>

#include <zlib/ZtlsCOSE.hh>

namespace Zum {

// This translation unit is the installed service-side client. Server and Zdb
// implementation remain in zumd's private convenience library.

ZuDerive(ServiceReadyFn, (ZmFn<void(bool),
  ZmFnHeapID<"Zum.Service.Ready">>));
ZuDerive(ServiceHTTPResultFn, (ZmFn<void(ServiceHTTPResponse),
  ZmFnHeapID<"Zum.Service.HTTPResult">>));
ZuDerive(ServiceReadyVec, (ZtArray<ServiceReadyFn,
  ZtArrayHeapID<"Zum.Service.ReadyVec">>));
ZuDerive(ServiceDoneVec, (ZtArray<ServiceDoneFn,
  ZtArrayHeapID<"Zum.Service.DoneVec">>));

namespace ServiceProtocolOp { enum { Token, Revoke }; }

struct ServiceKey {
  String id;
  Bytes publicKey;
};
ZuDerive(ServiceKeyVec, (ZtArray<ServiceKey,
  ZtArrayHeapID<"Zum.Service.KeyVec">>));

struct TokenWire {
  String accessToken;
  uint64_t expiresIn = 0;
};
ZfStruct(, (TokenWire, JSON),
  (((accessToken),	(JSON::ID<"access_token">, Required)),	(String)),
  (((expiresIn),	(JSON::ID<"expires_in">, Required)),	(UInt64)));

struct DiscoveryWire {
  String issuer;
  String jwksURI;
};
ZfStruct(, (DiscoveryWire, JSON),
  (((issuer),		(Required)),	(String)),
  (((jwksURI),		(JSON::ID<"jwks_uri">, Required)),	(String)));

struct JWKWire {
  String alg;
  String crv;
  String kid;
  String kty;
  String use;
  String x;
  String y;
};
ZfStruct(, (JWKWire, JSON),
  (((alg),		(JSON::Opt)),	(String)),
  (((crv),		(Required)),	(String)),
  (((kid),		(Required)),	(String)),
  (((kty),		(Required)),	(String)),
  (((use),		(JSON::Opt)),	(String)),
  (((x),		(Required)),	(String)),
  (((y),		(Required)),	(String)));
ZuDerive(JWKWireArray,
  (ZtArray<JWKWire, ZtArrayHeapID<"Zum.Service.JWKs">>));
struct JWKWireVec : public JWKWireArray {
  ZuDerive_(JWKWireVec, JWKWireArray);
  friend ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(JWKWireVec *);
};
struct JWKSResponse { JWKWireVec keys; };
ZfStruct(, (JWKSResponse, JSON),
  (((keys),		(Required)),	(UDT)));

struct AuthorizeWire {
  String authorizationURL;
  uint64_t expiresIn = 0;
};
ZfStruct(, (AuthorizeWire, JSON),
  (((authorizationURL),	(Required)),	(String)),
  (((expiresIn),		(Required)),	(UInt64)));

struct AuthorizeRequestWire {
  String clientID;
  String redirectURI;
  String responseType;
  String scope;
  String resource;
  String state;
  String codeChallenge;
  String codeChallengeMethod;
  String nonce;
  String prompt;
  uint32_t maxAge = ZuCmp<uint32_t>::null();
};
ZfStruct(, (AuthorizeRequestWire, JSON),
  (((clientID),		(Required)),	(String)),
  (((redirectURI),	(Required)),	(String)),
  (((responseType),	(Required)),	(String)),
  (((scope),		(Required)),	(String)),
  (((resource),		(JSON::Opt)),	(String)),
  (((state),		(JSON::Opt)),	(String)),
  (((codeChallenge),	(Required)),	(String)),
  (((codeChallengeMethod),(Required)),	(String)),
  (((nonce),		(JSON::Opt)),	(String)),
  (((prompt),		(JSON::Opt)),	(String)),
  (((maxAge),		(JSON::Opt)),	(UInt32)));

struct FormWire { String form; };
ZfStruct(, (FormWire, JSON),
  (((form),		(Required)),	(String)));

template <typename T>
static bool jsonLoad(String &json, unsigned limit, T &value)
{
  if (!json || json.length() > limit) return false;
  if (!json.mutable_()) json.length(json.length());
  auto parsed = ZfJSON::scan({json.data(), json.length()});
  if (parsed.p<0>() != int(json.length()) || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 || !ZfJSON::unique(roots[0])) return false;
  value = ZfJSON::handler<T>(roots[0]).ctor();
  return true;
}

static bool tokenResponse(String json, unsigned limit,
    String &token, uint64_t &expires)
{
  TokenWire wire;
  if (!jsonLoad(json, limit, wire) || !wire.accessToken || !wire.expiresIn)
    return false;
  token = ZuMv(wire.accessToken);
  expires = wire.expiresIn;
  return true;
}

static bool discoveryResponse(String json, unsigned limit,
    ZuCSpan expectedIssuer, String &jwks)
{
  DiscoveryWire wire;
  if (!jsonLoad(json, limit, wire) || wire.issuer != expectedIssuer ||
      !wire.jwksURI) return false;
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

static bool authorizeResponse(String json, unsigned limit,
    ServiceAuthorizeResult &result)
{
  AuthorizeWire wire;
  if (!jsonLoad(json, limit, wire) || !wire.authorizationURL ||
      !wire.expiresIn) return false;
  result.authorizationURL = ZuMv(wire.authorizationURL);
  result.expiresIn = wire.expiresIn;
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
  ZfURI::PathQuote::quote(plain, secret);
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

struct ServiceState : public ZumObject {
  enum State { Initial, Starting, Started, Stopping, Stopped };

  ServiceState(ServiceConfig config, ServiceHTTPFn http) :
    config{ZuMv(config)}, http{ZuMv(http)} { }

  ~ServiceState() { clear_(); }

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
    http(ZuMv(request), [self = ZmRef<ServiceState>{this},
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
    authenticate_([self = ZmRef<ServiceState>{this},
        complete = ZuMv(complete)](bool ok) mutable {
      if (self->state != Starting) { complete(ServiceError::Stopped); return; }
      if (!ok) { self->state = Initial; complete(ServiceError::Unavailable); return; }
      self->discovery_([self = ZuMv(self),
          complete = ZuMv(complete)](bool ok) mutable {
        if (self->state != Starting) { complete(ServiceError::Stopped); return; }
        if (!ok) { self->state = Initial; complete(ServiceError::Invalid); return; }
        self->keys_([self = ZuMv(self), complete = ZuMv(complete)](
            bool ok) mutable {
          if (self->state != Starting) { complete(ServiceError::Stopped); return; }
          if (!ok || !self->verifyWorkload_()) {
            self->state = Initial;
            complete(ServiceError::Unauthorized);
            return;
          }
          self->state = Started;
          complete(ServiceError::OK);
        });
      });
    });
  }

  void authenticate_(ServiceReadyFn complete)
  {
    String form{"grant_type=client_credentials&scope=zum.service"};
    send_(ServiceHTTPRequest{.method = ServiceMethod::POST,
      .url = endpoint(config.issuerURL, "/token"),
      .authorization = basicAuth(config.clientID, config.clientSecret),
      .contentType = "application/x-www-form-urlencoded",
      .body = ZuMv(form)}, [self = ZmRef<ServiceState>{this},
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
      }
      complete(ok);
    });
  }

  void discovery_(ServiceReadyFn complete)
  {
    send_(ServiceHTTPRequest{.url = endpoint(config.issuerURL,
      "/.well-known/openid-configuration")}, [self = ZmRef<ServiceState>{this},
        complete = ZuMv(complete)](ServiceHTTPResponse response) mutable {
      String jwks;
      bool ok = response.status == 200 && discoveryResponse(
        ZuMv(response.body), self->config.responseMax,
        self->config.issuerURL, jwks) &&
        jwks == endpoint(self->config.issuerURL, "/jwks");
      if (ok) self->jwksURL = ZuMv(jwks);
      complete(ok);
    });
  }

  void keys_(ServiceReadyFn complete)
  {
    send_(ServiceHTTPRequest{.url = jwksURL}, [self = ZmRef<ServiceState>{this},
        complete = ZuMv(complete)](ServiceHTTPResponse response) mutable {
      ServiceKeyVec keys;
      bool ok = response.status == 200 && jwksResponse(ZuMv(response.body),
        self->config.responseMax, self->config.keyMax, keys);
      if (ok) {
        self->keys = ZuMv(keys);
        self->keysLoaded = Zm::now().sec();
      }
      complete(ok);
    });
  }

  const ServiceKey *key_(ZuCSpan id) const
  {
    for (auto &key: keys) if (key.id == id) return &key;
    return nullptr;
  }

  bool verify_(ZuCSpan token, Principal &principal)
  {
    JWTHeader header;
    if (!jwtHeader(token, config.jwtLimits, header)) return false;
    auto key = key_(header.keyID);
    return key && jwtVerify(token, header.keyID, config.issuerURL,
      config.audience, key->publicKey, Zm::now().sec(), config.jwtLimits,
      principal) && principal.appID == appID;
  }

  void refreshKeys_(ServiceReadyFn complete)
  {
    keyWaiters.push(ZuMv(complete));
    if (keysRefreshing) return;
    int64_t now = Zm::now().sec();
    if (keysLoaded && now - keysLoaded < config.jwksRefresh) {
      auto waiters = ZuMv(keyWaiters);
      keyWaiters.null();
      for (auto &waiter: waiters) waiter(false);
      return;
    }
    keysRefreshing = true;
    keys_([self = ZmRef<ServiceState>{this}](bool ok) mutable {
      self->keysRefreshing = false;
      auto waiters = ZuMv(self->keyWaiters);
      self->keyWaiters.null();
      for (auto &waiter: waiters) waiter(ok);
    });
  }

  bool verifyWorkload_()
  {
    JWTHeader header;
    if (!jwtHeader(accessToken, config.jwtLimits, header)) return false;
    auto key = key_(header.keyID);
    Principal principal;
    String audience = endpoint(config.issuerURL, "/admin");
    if (!key || !jwtVerify(accessToken, header.keyID, config.issuerURL,
        audience, key->publicKey, Zm::now().sec(), config.jwtLimits,
        principal) || principal.authMethod ||
        principal.clientID != config.clientID || !principal.appID) return false;
    appID = principal.appID;
    if (accessExpires > principal.expires) accessExpires = principal.expires;
    return true;
  }

  void verifyWorkload_(ServiceReadyFn complete)
  {
    if (verifyWorkload_()) { complete(true); return; }
    JWTHeader header;
    if (!jwtHeader(accessToken, config.jwtLimits, header) || key_(header.keyID)) {
      complete(false);
      return;
    }
    refreshKeys_([self = ZmRef<ServiceState>{this},
        complete = ZuMv(complete)](bool ok) mutable {
      complete(ok && self->state == Started && self->verifyWorkload_());
    });
  }

  void renewed_(bool ok)
  {
    renewing = false;
    auto waiters = ZuMv(tokenWaiters);
    tokenWaiters.null();
    for (auto &waiter: waiters) waiter(ok);
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
    authenticate_([self = ZmRef<ServiceState>{this}](bool ok) mutable {
      if (!ok || self->state != Started) { self->renewed_(false); return; }
      self->verifyWorkload_([self = ZuMv(self)](bool ok) mutable {
        self->renewed_(ok);
      });
    });
  }

  void authorize_(ServiceAuthorizeRequest request, ServiceAuthorizeFn complete)
  {
    token_([self = ZmRef<ServiceState>{this}, request = ZuMv(request),
        complete = ZuMv(complete)](bool ok) mutable {
      if (!ok) { complete(ServiceAuthorizeResult{
        .error = self->state == Started ? ServiceError::Unavailable :
          ServiceError::Stopped}); return; }
      if (!request.clientID || !request.redirectURI || !request.responseType ||
          !request.scope || !request.codeChallenge ||
          !request.codeChallengeMethod) {
        complete(ServiceAuthorizeResult{.error = ServiceError::Invalid});
        return;
      }
      AuthorizeRequestWire wire{
        .clientID = ZuMv(request.clientID),
        .redirectURI = ZuMv(request.redirectURI),
        .responseType = ZuMv(request.responseType),
        .scope = ZuMv(request.scope), .resource = ZuMv(request.resource),
        .state = request.statePresent ? ZuMv(request.state) : String{},
        .codeChallenge = ZuMv(request.codeChallenge),
        .codeChallengeMethod = ZuMv(request.codeChallengeMethod),
        .nonce = request.noncePresent ? ZuMv(request.nonce) : String{},
        .prompt = request.promptPresent ? ZuMv(request.prompt) : String{},
        .maxAge = request.maxAgePresent ? request.maxAge :
          ZuCmp<uint32_t>::null()};
      String body;
      ZfJSON::save(body, wire);
      self->send_(ServiceHTTPRequest{.method = ServiceMethod::POST,
        .url = endpoint(self->config.issuerURL, "/service/authorize"),
        .authorization = String{"Bearer "} << self->accessToken,
        .contentType = "application/json", .body = ZuMv(body)},
        [self, complete = ZuMv(complete)](
            ServiceHTTPResponse response) mutable {
          ServiceAuthorizeResult result;
          if (response.status == 200 && authorizeResponse(
              ZuMv(response.body), self->config.responseMax, result))
            result.error = ServiceError::OK;
          else result.error = response.status == 200 ?
            ServiceError::Invalid : serviceStatus(response.status);
          complete(ZuMv(result));
        });
    });
  }

  void protocol_(int operation, String form, ServiceProtocolFn complete)
  {
    token_([self = ZmRef<ServiceState>{this}, operation,
        form = ZuMv(form), complete = ZuMv(complete)](bool ok) mutable {
      if (!ok || !form) {
        complete(ServiceProtocolResult{.error = !form ? ServiceError::Invalid :
          self->state == Started ? ServiceError::Unavailable :
            ServiceError::Stopped});
        return;
      }
      String body;
      ZfJSON::save(body, FormWire{ZuMv(form)});
      auto path = operation == ServiceProtocolOp::Token ?
        ZuCSpan{"/service/token"} : ZuCSpan{"/service/revoke"};
      self->send_(ServiceHTTPRequest{.method = ServiceMethod::POST,
        .url = endpoint(self->config.issuerURL, path),
        .authorization = String{"Bearer "} << self->accessToken,
        .contentType = "application/json", .body = ZuMv(body)},
        [complete = ZuMv(complete)](ServiceHTTPResponse response) mutable {
          complete(ServiceProtocolResult{.body = ZuMv(response.body),
            .status = response.status, .error = serviceStatus(response.status)});
        });
    });
  }

  void verify_(String token, ServiceVerifyFn complete)
  {
    if (state != Started || !token) {
      complete(state == Started ? ServiceError::Invalid : ServiceError::Stopped,
        ServicePrincipal{});
      return;
    }
    Principal principal;
    if (verify_(token, principal)) {
      complete(ServiceError::OK, ServicePrincipal{
        .subject = ZuMv(principal.subject), .appID = principal.appID,
        .audience = config.audience, .actions = ZuMv(principal.actions),
        .expires = principal.expires});
      return;
    }
    JWTHeader header;
    if (!jwtHeader(token, config.jwtLimits, header) || key_(header.keyID)) {
      complete(ServiceError::Unauthorized, ServicePrincipal{});
      return;
    }
    refreshKeys_([self = ZmRef<ServiceState>{this}, token = ZuMv(token),
        complete = ZuMv(complete)](bool ok) mutable {
      Principal principal;
      if (!ok || self->state != Started ||
          !self->verify_(token, principal)) {
        complete(self->state == Started ? ServiceError::Unauthorized :
          ServiceError::Stopped, ServicePrincipal{});
        return;
      }
      complete(ServiceError::OK, ServicePrincipal{
        .subject = ZuMv(principal.subject), .appID = principal.appID,
        .audience = self->config.audience,
        .actions = ZuMv(principal.actions), .expires = principal.expires});
    });
  }

  void publish_(ServiceManifest manifest, ServiceProtocolFn complete)
  {
    token_([self = ZmRef<ServiceState>{this}, manifest = ZuMv(manifest),
        complete = ZuMv(complete)](bool ok) mutable {
      if (!ok || !manifest.revision) {
        complete(ServiceProtocolResult{.error = ok ? ServiceError::Invalid :
          self->state == Started ? ServiceError::Unavailable :
            ServiceError::Stopped});
        return;
      }
      String url = endpoint(self->config.issuerURL, "/admin/apps/");
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
    auto waiters = ZuMv(stopWaiters);
    stopWaiters.null();
    for (auto &waiter: waiters) waiter(ServiceError::OK);
  }

  void clear_()
  {
    if (accessToken.mutable_())
      ZuClear(accessToken.data(), accessToken.length());
    accessToken.null();
    if (config.clientSecret.mutable_())
      ZuClear(config.clientSecret.data(), config.clientSecret.length());
  }

  ServiceConfig config;
  ServiceHTTPFn http;
  ServiceKeyVec keys;
  ServiceReadyVec tokenWaiters;
  ServiceReadyVec keyWaiters;
  ServiceDoneVec stopWaiters;
  String jwksURL;
  String accessToken;
  AppID appID = 0;
  int64_t accessExpires = 0;
  int64_t keysLoaded = 0;
  unsigned inflight = 0;
  State state = Initial;
  bool renewing = false;
  bool keysRefreshing = false;
};

Service::Service() = default;
Service::~Service() { final(); }

bool Service::init(ServiceConfig config, ServiceHTTPFn http)
{
  if (m_state || !config.scheduler || !config.sid ||
      config.sid > config.scheduler->params().nThreads() ||
      !config.issuerURL || !config.clientID ||
      !config.clientSecret || !config.audience || !config.requestTimeout ||
      !config.responseMax || !config.keyMax || !http) return false;
  m_state = new ServiceState{ZuMv(config), ZuMv(http)};
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

void Service::authorize(
    ServiceAuthorizeRequest request, ServiceAuthorizeFn complete)
{
  if (!m_state || !complete) return;
  auto state = m_state;
  state->post([state = ZuMv(state), request = ZuMv(request),
      complete = ZuMv(complete)]() mutable {
    state->authorize_(ZuMv(request), ZuMv(complete));
  });
}

void Service::token(String form, ServiceProtocolFn complete)
{
  if (!m_state || !complete) return;
  auto state = m_state;
  state->post([state = ZuMv(state), form = ZuMv(form),
      complete = ZuMv(complete)]() mutable {
    state->protocol_(ServiceProtocolOp::Token, ZuMv(form), ZuMv(complete));
  });
}

void Service::revoke(String form, ServiceProtocolFn complete)
{
  if (!m_state || !complete) return;
  auto state = m_state;
  state->post([state = ZuMv(state), form = ZuMv(form),
      complete = ZuMv(complete)]() mutable {
    state->protocol_(ServiceProtocolOp::Revoke, ZuMv(form), ZuMv(complete));
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
