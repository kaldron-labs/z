//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumJWT.hh>

#include <zlib/ZuBase64URL.hh>

#include <zlib/ZfJSON.hh>

#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsCOSE.hh>

namespace Zum {

bool scopeContains(ZuCSpan scopes, ZuCSpan name)
{
  unsigned offset = 0;
  while (offset < scopes.length()) {
    while (offset < scopes.length() && scopes[offset] == ' ') ++offset;
    unsigned end = offset;
    while (end < scopes.length() && scopes[end] != ' ') ++end;
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
  uint8_t random[JWTIDSize];
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

  json.null();
  json << "{\"iss\":";
  ZfJSON::quote(json, claims.issuer);
  json << ",\"sub\":";
  ZfJSON::quote(json, claims.subject);
  json << ",\"aud\":";
  ZfJSON::quote(json, claims.audience);
  json << ",\"client_id\":";
  ZfJSON::quote(json, claims.clientID);
  json << ",\"zum_app_id\":\"" << claims.appID << '"';
  json << ",\"iat\":" << ZuBoxed(claims.iat) <<
    ",\"nbf\":" << ZuBoxed(claims.nbf) <<
    ",\"exp\":" << ZuBoxed(claims.exp) << ",\"jti\":";
  ZfJSON::quote(json, claims.jti);
  json << ",\"scope\":";
  ZfJSON::quote(json, claims.scope);
  json << ",\"actions\":[";
  for (unsigned i = 0, n = claims.actions.length(); i < n; ++i) {
    if (i) json << ',';
    ZfJSON::quote(json, claims.actions[i]);
  }
  json << ']';
  if (interactive) {
    json << ",\"auth_time\":" << ZuBoxed(claims.authTime) <<
      ",\"amr\":[";
    ZfJSON::quote(json, claims.amr[0]);
    json << ']';
  }
  json << '}';
  return json.length() <= limits.json;
}

bool jwtPrepare(
    const AccessClaims &claims, ZuCSpan kid, const JWTLimits &limits,
    PreparedJWT &prepared)
{
  if (!kid || !limits.token || !limits.json || !limits.actions) return false;
  String header;
  header << "{\"typ\":\"at+jwt\",\"alg\":\"ES256\",\"kid\":";
  ZfJSON::quote(header, kid);
  header << '}';
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
  String header{"{\"typ\":\"JWT\",\"alg\":\"ES256\",\"kid\":"};
  ZfJSON::quote(header, kid);
  header << '}';
  String json{"{\"iss\":"};
  ZfJSON::quote(json, claims.issuer);
  json << ",\"sub\":";
  ZfJSON::quote(json, claims.subject);
  json << ",\"aud\":";
  ZfJSON::quote(json, claims.audience);
  json << ",\"iat\":" << ZuBoxed(claims.iat) <<
    ",\"exp\":" << ZuBoxed(claims.exp) <<
    ",\"auth_time\":" << ZuBoxed(claims.authTime) << ",\"amr\":[";
  ZfJSON::quote(json, claims.amr[0]);
  json << ']';
  if (claims.nonce) {
    json << ",\"nonce\":";
    ZfJSON::quote(json, claims.nonce);
  }
  if (claims.name) {
    json << ",\"name\":";
    ZfJSON::quote(json, claims.name);
  }
  if (claims.preferredUserName) {
    json << ",\"preferred_username\":";
    ZfJSON::quote(json, claims.preferredUserName);
  }
  if (claims.email) {
    json << ",\"email\":";
    ZfJSON::quote(json, claims.email);
  }
  json << '}';
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
  uint8_t raw[Ztls::COSE::ES256::SignatureSize];
  if (!Ztls::COSE::ES256::derToRaw(derSignature, raw) ||
      prepared.token.length() + 1 + ZuBase64URL::enclen(sizeof(raw)) >
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
  String next{"{\"sub\":"};
  ZfJSON::quote(next, subject);
  if (scopeContains(principal.scope, "profile")) {
    if (user.profile || user.name) {
      next << ",\"name\":";
      ZfJSON::quote(next, user.profile ? user.profile : user.name);
    }
    if (user.name) {
      next << ",\"preferred_username\":";
      ZfJSON::quote(next, user.name);
    }
  }
  if (scopeContains(principal.scope, "email") && user.email) {
    next << ",\"email\":";
    ZfJSON::quote(next, user.email);
  }
  next << '}';
  json = ZuMv(next);
  return true;
}

} // namespace Zum
