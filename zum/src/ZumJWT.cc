//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumJWT.hh>

#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuArray.hh>

#include <zlib/ZfJSON.hh>

#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsCOSE.hh>

namespace Zum {

struct JWTHeaderWire {
  String typ;
  String alg;
  String kid;
};
ZfStruct(, (JWTHeaderWire, JSON),
  (((typ),		(Required)),	(String)),
  (((alg),		(Required)),	(String)),
  (((kid),		(Required)),	(String)));

struct AccessClaimsWire {
  String issuer;
  String subject;
  String audience;
  String clientID;
  AppID appID = 0;
  int64_t iat = 0;
  int64_t nbf = 0;
  int64_t exp = 0;
  String jti;
  String scope;
  StringVec actions;
  int64_t authTime = 0;
  StringVec amr;
};
ZfStruct(, (AccessClaimsWire, JSON),
  (((issuer),		(JSON::ID<"iss">, Required)),	(String)),
  (((subject),		(JSON::ID<"sub">, Required)),	(String)),
  (((audience),		(JSON::ID<"aud">, Required)),	(String)),
  (((clientID),		(JSON::ID<"client_id">, Required)), (String)),
  (((appID),		(JSON::ID<"zum_app_id">, JSON::String<>, Required)), (UInt64)),
  (((iat),		(Required)),	(Int64)),
  (((nbf),		(Required)),	(Int64)),
  (((exp),		(Required)),	(Int64)),
  (((jti),		(Required)),	(String)),
  (((scope),		(Required)),	(String)),
  (((actions),		(Required)),	(StringVec)),
  (((authTime),		(JSON::ID<"auth_time">, JSON::Opt)), (Int64)),
  (((amr),		(JSON::Opt)),	(StringVec)));

struct IDClaimsWire {
  String issuer;
  String subject;
  String audience;
  int64_t iat = 0;
  int64_t exp = 0;
  int64_t authTime = 0;
  StringVec amr;
  String nonce;
  String name;
  String preferredUserName;
  String email;
};
ZfStruct(, (IDClaimsWire, JSON),
  (((issuer),		(JSON::ID<"iss">, Required)),	(String)),
  (((subject),		(JSON::ID<"sub">, Required)),	(String)),
  (((audience),		(JSON::ID<"aud">, Required)),	(String)),
  (((iat),		(Required)),	(Int64)),
  (((exp),		(Required)),	(Int64)),
  (((authTime),		(JSON::ID<"auth_time">, Required)), (Int64)),
  (((amr),		(Required)),	(StringVec)),
  (((nonce),		(JSON::Opt)),	(String)),
  (((name),		(JSON::Opt)),	(String)),
  (((preferredUserName), (JSON::ID<"preferred_username">, JSON::Opt)), (String)),
  (((email),		(JSON::Opt)),	(String)));

struct UserInfoWire {
  String subject;
  String name;
  String preferredUserName;
  String email;
};
ZfStruct(, (UserInfoWire, JSON),
  (((subject),		(JSON::ID<"sub">, Required)),	(String)),
  (((name),		(JSON::Opt)),	(String)),
  (((preferredUserName), (JSON::ID<"preferred_username">, JSON::Opt)), (String)),
  (((email),		(JSON::Opt)),	(String)));

bool scopeContains(ZuCSpan scopes, ZuCSpan name)
{
  unsigned offset = 0;
  unsigned n = scopes.length();
  while (offset < n) {
    while (offset < n && scopes[offset] == ' ') ++offset;
    unsigned end = offset;
    while (end < n && scopes[end] != ' ') ++end;
    if (ZuCSpan{scopes.data() + offset, end - offset} == name) return true;
    offset = end;
  }
  return false;
}

static bool encode(String &out, ZuBSpan data)
{
  auto n = ZuBase64URL::enclen(data.length());
  out.length(n);
  return ZuBase64URL::encode(out.span(), data) == n;
}

static bool accessClaims(
    Ztls::Random &rng, ZuCSpan issuer, ZuCSpan subject,
    const Client &client, const ScopeSelection &selection,
    const ZtBitmap &authority, ZuSpan<const Action> actions,
    int64_t now, int64_t expires, AccessClaims &claims)
{
  ZuBArray<JWTIDSize> random(JWTIDSize, false);
  if (!rng.random(random)) return false;
  AccessClaims next;
  next.issuer = issuer;
  next.subject = subject;
  next.audience = selection.audience;
  next.clientID = client.id;
  next.appID = client.appID;
  if (!encode(next.jti, random)) return false;
  next.scope = selection.scope;
  for (auto &action: actions)
    if (action.state == State::Active && action.id < authority.length() &&
	authority[action.id]) next.actions.push(action.name);
  next.iat = next.nbf = now;
  next.exp = expires;
  claims = ZuMv(next);
  return true;
}

bool interactiveClaims(
    Ztls::Random &rng, ZuCSpan issuer, const User &user,
    const Client &client, const ScopeSelection &selection,
    const ZtBitmap &authority, ZuSpan<const Action> actions,
    ZuCSpan authMethod, int64_t authTime, int64_t now, int64_t expires,
    AccessClaims &claims)
{
  if (user.state != State::Active || !user.handle ||
      client.state != State::Active ||
      (client.type != ClientType::Browser &&
       client.type != ClientType::Native &&
       client.type != ClientType::Confidential) ||
      !(client.grants & ClientGrant::AuthorizationCode)) return false;
  String subject;
  if (!encode(subject, user.handle)) return false;
  AccessClaims next;
  if (!accessClaims(rng, issuer, subject, client, selection,
      authority, actions, now, expires, next)) return false;
  if (authMethod != "passkey" && authMethod != "oidc") return false;
  next.authTime = authTime;
  next.amr.push(authMethod);
  claims = ZuMv(next);
  return true;
}

bool clientClaims(
    Ztls::Random &rng, ZuCSpan issuer, const Client &client,
    const ScopeSelection &selection, const ZtBitmap &authority,
    ZuSpan<const Action> actions, AppID clientAppID,
    int64_t now, int64_t expires,
    AccessClaims &claims)
{
  if (client.state != State::Active ||
      client.type != ClientType::Confidential ||
      !(client.grants & ClientGrant::ClientCredentials)) return false;
  Client identity{client};
  identity.appID = clientAppID;
  return accessClaims(rng, issuer, client.id, identity, selection,
    authority, actions, now, expires, claims);
}

static bool encodePart(String &out, ZuBSpan data, unsigned limit)
{
  auto old = out.length();
  auto n = ZuBase64URL::enclen(data.length());
  if (old + n > limit) return false;
  out.length(old + n);
  return ZuBase64URL::encode(out.span().offset(old), data) == n;
}

static bool claimsJSON(
    String &json, const AccessClaims &claims, const JWTLimits &limits)
{
  bool interactive = claims.authTime > 0;
  if (!claims.issuer || !claims.subject || !claims.audience ||
      !claims.clientID || !claims.appID || !claims.jti || !claims.scope ||
      claims.actions.length() > limits.actions || claims.iat <= 0 ||
      claims.nbf <= 0 || claims.exp <= claims.iat || claims.nbf >= claims.exp ||
      (interactive != bool(claims.amr))) return false;
  if (interactive && (claims.amr.length() != 1 ||
      (claims.amr[0] != "passkey" && claims.amr[0] != "oidc"))) return false;

  ZfJSON::save(json, AccessClaimsWire{
    .issuer = claims.issuer, .subject = claims.subject,
    .audience = claims.audience, .clientID = claims.clientID,
    .appID = claims.appID, .iat = claims.iat, .nbf = claims.nbf,
    .exp = claims.exp, .jti = claims.jti, .scope = claims.scope,
    .actions = claims.actions,
    .authTime = interactive ? claims.authTime : 0,
    .amr = interactive ? claims.amr : StringVec{}});
  return json.length() <= limits.json;
}

bool jwtPrepare(
    const AccessClaims &claims, ZuCSpan kid, const JWTLimits &limits,
    PreparedJWT &prepared)
{
  if (!kid || !limits.token || !limits.json || !limits.actions) return false;
  String header;
  ZfJSON::save(header, JWTHeaderWire{"at+jwt", "ES256", kid});
  if (header.length() > limits.json) return false;
  String claims_;
  if (!claimsJSON(claims_, claims, limits)) return false;

  PreparedJWT next;
  if (!encodePart(next.token, ZuBSpan{header}, limits.token)) return false;
  next.token << '.';
  if (!encodePart(next.token, ZuBSpan{claims_}, limits.token)) return false;
  Ztls::MD<> md;
  md.update(ZuBSpan{next.token});
  md.finish(next.digest);
  prepared = ZuMv(next);
  return true;
}

bool idClaims(
    ZuCSpan issuer, const User &user, const Client &client,
    const ScopeSelection &selection,
    ZuCSpan nonce, ZuCSpan authMethod, int64_t authTime,
    int64_t now, int64_t expires, IDClaims &claims)
{
  if (!issuer || user.state != State::Active || user.owner || !user.handle ||
      client.state != State::Active || client.owner || !client.id ||
      !scopeContains(selection.scope, "openid") ||
      (authMethod != "passkey" && authMethod != "oidc") ||
      authTime <= 0 || now <= 0 || expires <= now) return false;
  IDClaims next;
  next.issuer = issuer;
  if (!encode(next.subject, user.handle)) return false;
  next.audience = client.id;
  next.nonce = nonce;
  if (scopeContains(selection.scope, "profile")) {
    next.name = user.profile ? user.profile : user.name;
    next.preferredUserName = user.name;
  }
  if (scopeContains(selection.scope, "email")) next.email = user.email;
  next.amr.push(authMethod);
  next.iat = now;
  next.exp = expires;
  next.authTime = authTime;
  claims = ZuMv(next);
  return true;
}

bool idTokenPrepare(
    const IDClaims &claims, ZuCSpan kid, const JWTLimits &limits,
    PreparedJWT &prepared)
{
  if (!kid || !claims.issuer || !claims.subject || !claims.audience ||
      claims.amr.length() != 1 || claims.iat <= 0 ||
      claims.exp <= claims.iat || claims.authTime <= 0 ||
      (claims.amr[0] != "passkey" && claims.amr[0] != "oidc") ||
      !limits.token || !limits.json) return false;
  String header;
  ZfJSON::save(header, JWTHeaderWire{"JWT", "ES256", kid});
  String json;
  ZfJSON::save(json, IDClaimsWire{
    .issuer = claims.issuer, .subject = claims.subject,
    .audience = claims.audience, .iat = claims.iat, .exp = claims.exp,
    .authTime = claims.authTime, .amr = claims.amr,
    .nonce = claims.nonce, .name = claims.name,
    .preferredUserName = claims.preferredUserName, .email = claims.email});
  if (header.length() > limits.json || json.length() > limits.json)
    return false;
  PreparedJWT next;
  if (!encodePart(next.token, ZuBSpan{header}, limits.token)) return false;
  next.token << '.';
  if (!encodePart(next.token, ZuBSpan{json}, limits.token)) return false;
  Ztls::MD<> md;
  md.update(ZuBSpan{next.token});
  md.finish(next.digest);
  prepared = ZuMv(next);
  return true;
}

bool jwtFinish(
    PreparedJWT &prepared, ZuBSpan derSignature, const JWTLimits &limits)
{
  ZuBArray<Ztls::COSE::ES256::SignatureSize> raw(
    Ztls::COSE::ES256::SignatureSize, false);
  if (!Ztls::COSE::ES256::derToRaw(derSignature, raw) ||
      prepared.token.length() + 1 + ZuBase64URL::enclen(raw.length()) >
        limits.token) return false;
  prepared.token << '.';
  return encodePart(prepared.token, raw, limits.token);
}

bool userInfoJSON(const User &user, const Principal &principal, String &json)
{
  if (user.state != State::Active || user.owner || !user.handle ||
      !scopeContains(principal.scope, "openid")) return false;
  String subject;
  if (!encode(subject, user.handle) || subject != principal.subject)
    return false;
  UserInfoWire wire{.subject = ZuMv(subject)};
  if (scopeContains(principal.scope, "profile")) {
    if (user.profile || user.name) wire.name = user.profile ? user.profile : user.name;
    wire.preferredUserName = user.name;
  }
  if (scopeContains(principal.scope, "email")) wire.email = user.email;
  json.null();
  ZfJSON::save(json, wire);
  return true;
}

} // namespace Zum
