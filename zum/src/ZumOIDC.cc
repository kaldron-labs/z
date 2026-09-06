//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumOIDC.hh>

#include <zlib/ZuBase64.hh>
#include <zlib/ZuBase64URL.hh>

#include <zlib/ZfJSON.hh>
#include <zlib/ZfURI.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmScheduler.hh>

#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsRandom.hh>

namespace Zum {

static bool hasScope(const StringVec &scopes, ZuCSpan name)
{
  for (auto &scope: scopes) if (scope == name) return true;
  return false;
}

static bool reservedClaim(ZuCSpan name)
{
  return name == "iss" || name == "sub" || name == "aud" ||
    name == "azp" || name == "nonce" || name == "iat" || name == "exp";
}

static bool hasRole(const IDVec &roles, RoleID roleID)
{
  for (auto id: roles) if (id == roleID) return true;
  return false;
}

bool oidcConfigValid(const OIDCConfig &config)
{
  if (!config.issuer || !config.authorizeEndpoint ||
      !config.tokenEndpoint || !config.jwksEndpoint || !config.clientID ||
      !config.redirectURI || !hasScope(config.oidcScopes, "openid") ||
      (config.clientAuth != OIDCClientAuth::Basic &&
       config.clientAuth != OIDCClientAuth::Post &&
       config.clientAuth != OIDCClientAuth::None) ||
      (config.clientAuth != OIDCClientAuth::None && !config.clientSecret))
    return false;
  switch (config.roles) {
    case OIDCRoles::Local:
      return !config.roleClaim && !config.roleMap;
    case OIDCRoles::Mapped:
      return config.roleClaim && !reservedClaim(config.roleClaim);
    default:
      return false;
  }
}

static bool stringValue(ZfJSON::AnyNode *node, String &value)
{
  if (!node || !node->has<ZfJSON::AnyNode::String>()) return false;
  value = node->data<ZfJSON::AnyNode::String>();
  return true;
}

static bool integerValue(ZfJSON::AnyNode *node, int64_t &value)
{
  if (!node || !node->has<ZfJSON::AnyNode::Number>()) return false;
  auto parsed = ZfJSON::eov_Decimal(node->data<ZfJSON::AnyNode::Number>());
  if (parsed.p<0>() < 0) return false;
  value = int64_t(parsed.p<1>().floor());
  return true;
}

static bool stringsValue(
    ZfJSON::AnyNode *node, unsigned limit, StringVec &values)
{
  StringVec next;
  if (node && node->has<ZfJSON::AnyNode::String>()) {
    String value;
    if (!stringValue(node, value)) return false;
    next.push(ZuMv(value));
  } else if (node && node->has<ZfJSON::AnyNode::Array>()) {
    auto &array = node->data<ZfJSON::AnyNode::Array>();
    if (array.length() > limit) return false;
    for (auto &item: array) {
      String value;
      if (!stringValue(item, value)) return false;
      next.push(ZuMv(value));
    }
  } else {
    return false;
  }
  values = ZuMv(next);
  return true;
}

static bool hasAudience(const StringVec &audiences, ZuCSpan audience)
{
  for (auto &value: audiences) if (value == audience) return true;
  return false;
}

bool oidcVerifyIDToken(
    ZuCSpan token, ZuBSpan publicKey, ZuCSpan nonce,
    const OIDCConfig &config, int64_t now, const OIDCLimits &limits,
    OIDCClaims &claims)
{
  if (!nonce || !config.issuer || !config.clientID || now <= 0 ||
      !limits.audiences || !limits.roleValues || limits.clockSkew < 0)
    return false;
  JWTHeader header;
  String json;
  if (!jwtES256(token, publicKey, limits.jwt, header, json) ||
      (header.type && header.type != "JWT")) return false;
  auto parsed = ZfJSON::scan({json.data(), json.length()});
  if (parsed.p<0>() < 0 || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (!roots || !roots[0]->has<ZfJSON::AnyNode::Object>()) return false;

  OIDCClaims next;
  String authorizedParty;
  unsigned seen = 0;
  auto &fields = roots[0]->data<ZfJSON::AnyNode::Object>();
  for (auto &field: fields) {
    auto name = field.p<0>();
    auto value = field.p<1>().ptr();
    unsigned bit = 0;
    bool valid = true;
    if (name == "iss") {
      bit = 1U<<0; valid = stringValue(value, next.issuer);
    } else if (name == "sub") {
      bit = 1U<<1; valid = stringValue(value, next.subject);
    } else if (name == "aud") {
      bit = 1U<<2;
      valid = stringsValue(value, limits.audiences, next.audience);
    } else if (name == "azp") {
      bit = 1U<<3; valid = stringValue(value, authorizedParty);
    } else if (name == "nonce") {
      bit = 1U<<4; valid = stringValue(value, next.nonce);
    } else if (name == "iat") {
      bit = 1U<<5; valid = integerValue(value, next.iat);
    } else if (name == "exp") {
      bit = 1U<<6; valid = integerValue(value, next.expires);
    } else if (config.roles == OIDCRoles::Mapped &&
	name == config.roleClaim) {
      bit = 1U<<7;
      valid = stringsValue(value, limits.roleValues, next.roleValues);
    } else {
      continue;
    }
    if (!valid) return false;
    seen |= bit;
  }
  constexpr unsigned required = (1U<<3) - 1 | (7U<<4);
  if ((seen & required) != required || next.issuer != config.issuer ||
      !next.subject || next.nonce != nonce ||
      !hasAudience(next.audience, config.clientID) ||
      next.iat <= 0 || next.expires <= next.iat ||
      next.iat > now + limits.clockSkew ||
      now - limits.clockSkew >= next.expires) return false;
  if ((next.audience.length() > 1 || (seen & (1U<<3))) &&
      authorizedParty != config.clientID) return false;
  claims = ZuMv(next);
  return true;
}

IDVec oidcMapRoles(
    ZuSpan<const String> values, ZuSpan<const RoleMap> map)
{
  IDVec roles;
  for (auto &value: values) {
    if (!value) continue;
    for (auto &entry: map)
      if (entry.value == value) {
	if (entry.roleID && !hasRole(roles, entry.roleID))
	  roles.push(entry.roleID);
	break;
      }
  }
  return roles;
}

void oidcLoadUser(
    DBContext *context, String subject, const OIDCConfig &config,
    StringVec roleValues, OIDCUserFn complete)
{
  if (!context || !subject || !complete) {
    if (complete) complete(false, User{}, IDVec{});
    return;
  }
  auto mode = config.roles;
  IDVec mapped;
  if (mode == OIDCRoles::Mapped)
    mapped = oidcMapRoles(roleValues, config.roleMap);
  auto users = context->users;
  users->run(0, [
    users, subject = ZuMv(subject), mode, mapped = ZuMv(mapped),
    complete = ZuMv(complete)
  ]() mutable {
    users->find<3>(0, ZuFwdTuple(ZuMv(subject)), [
      mode, mapped = ZuMv(mapped),
      complete = ZuMv(complete)
    ](ZdbRowRef<User> row) mutable {
      if (!row || row->data().state != State::Active || row->data().owner) {
	complete(false, User{}, IDVec{});
	return;
      }
      User user = row->data();
      switch (mode) {
	case OIDCRoles::Local:
	  mapped = user.roleIDs;
	  break;
	case OIDCRoles::Mapped:
	  break;
	default:
	  complete(false, User{}, IDVec{});
	  return;
      }
      complete(true, ZuMv(user), ZuMv(mapped));
    });
  });
}

struct OIDCKey {
  String	id;
  Bytes		publicKey;
};

static const String &oidcKeyID(const OIDCKey &key) { return key.id; }
using OIDCKeyHash = ZmHash<OIDCKey,
  ZmHashKey<oidcKeyID,
    ZmHashLock<ZmNoLock, ZmHashHeapID<"Zum.OIDC.Keys">>>>;
using OIDCKeyVec = ZtArray<OIDCKey, ZtArrayHeapID<"Zum.OIDC.KeySet">>;

class OIDCState;

class OIDCReq : public ZumObject {
public:
  OIDCReq(OIDCState *state_) : state{state_} { }

  OIDCState		*state = nullptr;
  String		stateID;
  String		nonce;
  String		verifier;
  String		keyID;
  String		idToken;
  Bytes			grantID;
  ZmScheduler::Timer	timer;
  OIDCFinishFn		complete;
  bool			consumed = false;
  bool			waiting = false;
};

static const String &oidcReqID(const ZmRef<OIDCReq> &request)
{
  return request->stateID;
}

using OIDCReqHash = ZmHash<ZmRef<OIDCReq>,
  ZmHashKey<oidcReqID,
    ZmHashLock<ZmNoLock, ZmHashHeapID<"Zum.OIDC.Pending">>>>;
using OIDCWaitVec = ZtArray<ZmRef<OIDCReq>,
  ZtArrayHeapID<"Zum.OIDC.Wait">>;

static bool randomText(Ztls::Random &rng, String &value)
{
  uint8_t random[OIDCRandomSize];
  if (!rng.random(random)) return false;
  value.length(ZuBase64URL::enclen(sizeof(random)));
  return ZuBase64URL::encode(value.span(), random) ==
    value.length();
}

static void formField(String &out, bool &first, ZuCSpan name, ZuCSpan value)
{
  if (!first) out << '&';
  first = false;
  out << name << '=';
  ZfURI::URIQuote<true>::quote(out, value);
}

static bool tokenResponse(
    String &json, const OIDCLimits &limits, String &idToken)
{
  if (!json || json.length() > limits.response) return false;
  if (!json.mutable_()) json.length(json.length());
  auto parsed = ZfJSON::scan({json.data(), json.length()});
  if (parsed.p<0>() < 0 || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (!roots || !roots[0]->has<ZfJSON::AnyNode::Object>()) return false;
  String next;
  for (auto &field: roots[0]->data<ZfJSON::AnyNode::Object>()) {
    if (field.p<0>() != "id_token") continue;
    if (!stringValue(field.p<1>().ptr(), next)) return false;
  }
  if (!next || next.length() > limits.jwt.token) return false;
  idToken = ZuMv(next);
  return true;
}

static bool jwksResponse(
    String &json, const OIDCLimits &limits, OIDCKeyVec &keys)
{
  if (!json || json.length() > limits.response) return false;
  if (!json.mutable_()) json.length(json.length());
  auto parsed = ZfJSON::scan({json.data(), json.length()});
  if (parsed.p<0>() < 0 || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (!roots || !roots[0]->has<ZfJSON::AnyNode::Object>()) return false;
  ZfJSON::AnyNode *keysNode = nullptr;
  for (auto &field: roots[0]->data<ZfJSON::AnyNode::Object>())
    if (field.p<0>() == "keys") keysNode = field.p<1>().ptr();
  if (!keysNode || !keysNode->has<ZfJSON::AnyNode::Array>()) return false;
  auto &array = keysNode->data<ZfJSON::AnyNode::Array>();
  if (!array || array.length() > limits.keys) return false;
  OIDCKeyVec next;
  for (auto &node: array) {
    if (!node->has<ZfJSON::AnyNode::Object>()) continue;
    String kid, kty, crv, use, alg, x, y;
    unsigned seen = 0;
    bool valid = true;
    for (auto &field: node->data<ZfJSON::AnyNode::Object>()) {
      String *value = nullptr;
      unsigned bit = 0;
      if (field.p<0>() == "kid") { bit = 1U<<0; value = &kid; }
      else if (field.p<0>() == "kty") { bit = 1U<<1; value = &kty; }
      else if (field.p<0>() == "crv") { bit = 1U<<2; value = &crv; }
      else if (field.p<0>() == "use") { bit = 1U<<3; value = &use; }
      else if (field.p<0>() == "alg") { bit = 1U<<4; value = &alg; }
      else if (field.p<0>() == "x") { bit = 1U<<5; value = &x; }
      else if (field.p<0>() == "y") { bit = 1U<<6; value = &y; }
      else continue;
      if (!stringValue(field.p<1>().ptr(), *value)) {
	valid = false;
	break;
      }
      seen |= bit;
    }
    constexpr unsigned required =
      (1U<<0) | (1U<<1) | (1U<<2) | (1U<<5) | (1U<<6);
    if (!valid || (seen & required) != required || !kid || kty != "EC" ||
        crv != "P-256" || (use && use != "sig") ||
        (alg && alg != "ES256")) continue;
    OIDCKey key{.id = ZuMv(kid)};
    key.publicKey.length(Ztls::ES256::PublicKeySize, false);
    key.publicKey[0] = 4;
    if (ZuBase64URL::decode({key.publicKey.data() + 1,
          Ztls::ES256::CoordinateSize}, ZuBSpan{x}) !=
          Ztls::ES256::CoordinateSize ||
        ZuBase64URL::decode({key.publicKey.data() + 1 +
          Ztls::ES256::CoordinateSize, Ztls::ES256::CoordinateSize},
          ZuBSpan{y}) != Ztls::ES256::CoordinateSize)
      continue;
    bool duplicate = false;
    for (auto &existing: next)
      if (existing.id == key.id) { duplicate = true; break; }
    if (duplicate) continue;
    next.push(ZuMv(key));
  }
  if (!next) return false;
  keys = ZuMv(next);
  return true;
}

class OIDCState : public ZumObject {
public:
  bool init(
      ZmScheduler *scheduler, unsigned sid, DBContext *context,
      OIDCConfig config, OIDCLimits limits, unsigned pendingLimit,
      uint64_t timeout, OIDCClockFn clock, OIDCHTTPFn http)
  {
    if (!scheduler || !sid || sid > scheduler->params().nThreads() ||
        !context || !oidcConfigValid(config) || !limits.response ||
        !limits.keys || !pendingLimit || !timeout || !clock || !http ||
        !m_rng.init())
      return false;
    m_scheduler = scheduler;
    m_sid = sid;
    m_context = context;
    m_config = ZuMv(config);
    m_limits = limits;
    m_pendingLimit = pendingLimit;
    m_timeout = timeout;
    m_clock = ZuMv(clock);
    m_http = ZuMv(http);
    m_up = 1;
    return true;
  }

  void final()
  {
    m_up = 0;
    invoke_([self = ZmRef<OIDCState>{this}]() mutable {
      auto i = self->m_pending.iter();
      while (auto request = i.val()) {
        self->m_scheduler->del(&request->timer);
        auto complete = ZuMv(request->complete);
        request->state = nullptr;
        i.del();
        if (complete) complete(false, Bytes{}, User{}, IDVec{}, 0);
      }
      self->m_waiting.null();
      self->m_keys.clean();
      self->m_http = {};
      self->m_clock = {};
      if (self->m_config.clientSecret.mutable_())
        ZuClear(self->m_config.clientSecret.data(),
          self->m_config.clientSecret.length());
      self->m_config = {};
    });
  }

  bool begin(Bytes grantID, OIDCBeginFn complete)
  {
    if (!m_up || !grantID || !complete) return false;
    invoke_([self = ZmRef<OIDCState>{this}, grantID = ZuMv(grantID),
        complete = ZuMv(complete)]() mutable {
      self->begin_(ZuMv(grantID), ZuMv(complete));
    });
    return true;
  }

  bool finish(String query, OIDCFinishFn complete)
  {
    if (!m_up || !query || !complete) return false;
    invoke_([self = ZmRef<OIDCState>{this}, query = ZuMv(query),
        complete = ZuMv(complete)]() mutable {
      self->finish_(ZuMv(query), ZuMv(complete));
    });
    return true;
  }

private:
  void invoke_(ZmFn<void()> fn)
  {
    m_scheduler->run([fn = ZuMv(fn)]() mutable { fn(); }, m_sid);
  }

  void begin_(Bytes grantID, OIDCBeginFn complete)
  {
    if (!m_up || m_pending.count_() >= m_pendingLimit) {
      complete(false, String{});
      return;
    }
    ZmRef<OIDCReq> request = new OIDCReq{this};
    if (!randomText(m_rng, request->stateID) ||
        !randomText(m_rng, request->nonce) ||
        !randomText(m_rng, request->verifier)) {
      complete(false, String{});
      return;
    }
    request->grantID = ZuMv(grantID);
    uint8_t digest[Ztls::MD<>::Size];
    Ztls::MD<> md;
    md.update(ZuBSpan{request->verifier});
    md.finish(digest);
    String challenge;
    challenge.length(ZuBase64URL::enclen(sizeof(digest)));
    challenge.length(ZuBase64URL::encode(challenge.span(), digest));
    if (!challenge || m_pending.find(request->stateID)) {
      complete(false, String{});
      return;
    }
    String scope;
    for (auto &value: m_config.oidcScopes) {
      if (!value) continue;
      if (scope) scope << ' ';
      scope << value;
    }
    String location{m_config.authorizeEndpoint};
    location << (location.find<"?">() >= 0 ? '&' : '?');
    bool first = true;
    formField(location, first, "response_type", "code");
    formField(location, first, "client_id", m_config.clientID);
    formField(location, first, "redirect_uri", m_config.redirectURI);
    formField(location, first, "scope", scope);
    formField(location, first, "state", request->stateID);
    formField(location, first, "nonce", request->nonce);
    formField(location, first, "code_challenge", challenge);
    formField(location, first, "code_challenge_method", "S256");
    m_pending.add(request);
    m_scheduler->add(&request->timer, Zm::now() + ZuTime{double(m_timeout)},
      ZmScheduler::Update, [request](auto &&arm) {
        return arm([request]() { if (request->state) request->state->fail_(request); });
      }, m_sid);
    complete(true, ZuMv(location));
  }

  void finish_(String query, OIDCFinishFn complete)
  {
    if (!m_up || query.length() > m_limits.callback) {
      complete(false, Bytes{}, User{}, IDVec{}, 0);
      return;
    }
    if (!query.mutable_()) query.length(query.length());
    if (query[0] == '?') query.splice(0, 1);
    String code, stateID, error;
    unsigned seen = 0;
    formEach({query.data(), query.length()},
      [&code, &stateID, &error, &seen](ZuCSpan name, ZuCSpan value) {
        String *target;
        unsigned bit;
        if (name == "code") { target = &code; bit = 1U; }
        else if (name == "state") { target = &stateID; bit = 2U; }
        else if (name == "error") { target = &error; bit = 4U; }
        else return;
        seen |= bit;
        *target = value;
      });
    auto request = stateID ? m_pending.findVal(stateID) : ZmRef<OIDCReq>{};
    if (!request || request->consumed || !(seen & 2U) ||
        bool(code) == bool(error)) {
      complete(false, Bytes{}, User{}, IDVec{}, 0);
      return;
    }
    request->consumed = true;
    request->complete = ZuMv(complete);
    if (error) { fail_(request); return; }
    String body;
    bool first = true;
    formField(body, first, "grant_type", "authorization_code");
    formField(body, first, "code", code);
    formField(body, first, "redirect_uri", m_config.redirectURI);
    formField(body, first, "code_verifier", request->verifier);
    OIDCHTTPRequest http{
      .url = m_config.tokenEndpoint,
      .contentType = "application/x-www-form-urlencoded",
      .body = ZuMv(body), .method = OIDCHTTPMethod::POST};
    switch (m_config.clientAuth) {
      case OIDCClientAuth::Basic: {
        String plain{m_config.clientID};
        plain << ':' << m_config.clientSecret;
        String encoded;
        encoded.length(ZuBase64::enclen(plain.length()));
        encoded.length(ZuBase64::encode(encoded.span(), ZuBSpan{plain}));
        if (plain.mutable_()) ZuClear(plain.data(), plain.length());
        http.authorization << "Basic " << encoded;
      } break;
      case OIDCClientAuth::Post:
        formField(http.body, first, "client_id", m_config.clientID);
        formField(http.body, first, "client_secret", m_config.clientSecret);
        break;
      case OIDCClientAuth::None:
        formField(http.body, first, "client_id", m_config.clientID);
        break;
    }
    auto send = m_http;
    send(ZuMv(http), [self = ZmRef<OIDCState>{this}, request](
        unsigned status, String body) mutable {
      self->invoke_([self, request, status, body = ZuMv(body)]() mutable {
        self->token_(request, status, ZuMv(body));
      });
    });
  }

  void token_(ZmRef<OIDCReq> request, unsigned status, String body)
  {
    if (!active_(request) || status != 200 ||
        !tokenResponse(body, m_limits, request->idToken)) {
      fail_(request);
      return;
    }
    JWTHeader header;
    if (!jwtHeader(request->idToken, m_limits.jwt, header) ||
        header.algorithm != "ES256" || !header.keyID) {
      fail_(request);
      return;
    }
    request->keyID = ZuMv(header.keyID);
    auto key = m_keys.findVal(request->keyID);
    if (key.id) {
      verify_(request, key.publicKey);
      return;
    }
    request->waiting = true;
    m_waiting.push(request);
    if (m_refreshing) return;
    m_refreshing = true;
    auto send = m_http;
    send(OIDCHTTPRequest{.url = m_config.jwksEndpoint},
      [self = ZmRef<OIDCState>{this}](unsigned status, String body) mutable {
        self->invoke_([self, status, body = ZuMv(body)]() mutable {
          self->keys_(status, ZuMv(body));
        });
      });
  }

  void keys_(unsigned status, String body)
  {
    m_refreshing = false;
    OIDCKeyVec keys;
    bool ok = m_up && status == 200 && jwksResponse(body, m_limits, keys);
    if (ok) {
      m_keys.clean();
      for (auto &key: keys) m_keys.add(ZuMv(key));
    }
    auto waiting = ZuMv(m_waiting);
    m_waiting.null();
    for (auto &request: waiting) {
      request->waiting = false;
      if (!ok || !active_(request)) { fail_(request); continue; }
      auto key = m_keys.findVal(request->keyID);
      if (!key.id) { fail_(request); continue; }
      verify_(request, key.publicKey);
    }
  }

  void verify_(ZmRef<OIDCReq> request, ZuBSpan publicKey)
  {
    OIDCClaims claims;
    int64_t now = m_clock ? m_clock() : 0;
    if (!active_(request) || !oidcVerifyIDToken(request->idToken, publicKey,
        request->nonce, m_config, now, m_limits, claims)) {
      fail_(request);
      return;
    }
    auto subject = claims.subject;
    auto roleValues = claims.roleValues;
    int64_t authTime = claims.iat;
    oidcLoadUser(m_context, ZuMv(subject), m_config, ZuMv(roleValues),
      [self = ZmRef<OIDCState>{this}, request, authTime](
          bool ok, User user, IDVec roleIDs) mutable {
        self->invoke_([self, request, ok, user = ZuMv(user),
            roleIDs = ZuMv(roleIDs), authTime]() mutable {
          if (!ok || !self->active_(request)) { self->fail_(request); return; }
          auto grantID = request->grantID;
          self->complete_(request, true, ZuMv(grantID), ZuMv(user),
            ZuMv(roleIDs), authTime);
        });
      });
  }

  bool active_(const ZmRef<OIDCReq> &request)
  {
    return m_up && request && request->state == this &&
      m_pending.findVal(request->stateID) == request;
  }

  void fail_(ZmRef<OIDCReq> request)
  {
    if (!active_(request)) return;
    complete_(request, false, {}, {}, {}, 0);
  }

  void complete_(
      ZmRef<OIDCReq> request, bool ok, Bytes grantID, User user,
      IDVec roleIDs, int64_t authTime)
  {
    m_scheduler->del(&request->timer);
    m_pending.del(request->stateID);
    request->state = nullptr;
    if (request->verifier.mutable_())
      ZuClear(request->verifier.data(), request->verifier.length());
    request->verifier.null();
    if (request->idToken.mutable_())
      ZuClear(request->idToken.data(), request->idToken.length());
    request->idToken.null();
    auto complete = ZuMv(request->complete);
    if (complete)
      complete(ok, ZuMv(grantID), ZuMv(user), ZuMv(roleIDs), authTime);
  }

  ZmScheduler	*m_scheduler = nullptr;
  unsigned	m_sid = 0;
  DBContext	*m_context = nullptr;
  OIDCConfig	m_config;
  OIDCLimits	m_limits;
  unsigned	m_pendingLimit = 0;
  uint64_t	m_timeout = 0;
  OIDCHTTPFn	m_http;
  OIDCClockFn	m_clock;
  Ztls::Random	m_rng;
  OIDCReqHash	m_pending;
  OIDCKeyHash	m_keys;
  OIDCWaitVec	m_waiting;
  ZmAtomic<uint32_t> m_up = 0;
  bool		m_refreshing = false;
};

OIDC::OIDC() = default;
OIDC::~OIDC() { final(); }

bool OIDC::init(
    ZmScheduler *scheduler, unsigned sid, DBContext *context,
    OIDCConfig config, OIDCLimits limits, unsigned pendingLimit,
    uint64_t timeout, OIDCClockFn clock, OIDCHTTPFn http)
{
  if (m_state) return false;
  ZmRef<OIDCState> state = new OIDCState;
  if (!state->init(scheduler, sid, context, ZuMv(config), limits,
      pendingLimit, timeout, ZuMv(clock), ZuMv(http))) return false;
  m_state = ZuMv(state);
  return true;
}

void OIDC::final()
{
  if (!m_state) return;
  auto state = ZuMv(m_state);
  state->final();
}

bool OIDC::begin(Bytes grantID, OIDCBeginFn complete)
{
  return m_state && m_state->begin(ZuMv(grantID), ZuMv(complete));
}

bool OIDC::finish(String query, OIDCFinishFn complete)
{
  return m_state && m_state->finish(ZuMv(query), ZuMv(complete));
}

} // namespace Zum
