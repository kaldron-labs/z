//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_jwt.hh>

#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuArray.hh>

#include <zlib/ZfJSON.hh>

#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsCOSE.hh>
#include <zlib/ZtlsPK.hh>
#include <zlib/zumd_secret.hh>

namespace Zum {

struct PublicJWK {
  String kid;
  String kty;
  String crv;
  String use;
  String alg;
  String x;
  String y;
};
ZfStruct(, (PublicJWK, JSON),
  (kid,		(Required),		String),
  (kty,		(Required),		String),
  (crv,		(Required),		String),
  (use,		(Required),		String),
  (alg,		(Required),		String),
  (x,		(Required),		String),
  (y,		(Required),		String));

static bool keyCoordinate(String &out, ZuBSpan value)
{
  auto length = ZuBase64URL::enclen(value.length());
  out.length(length);
  return ZuBase64URL::encode(out.span(), value) == length;
}

bool signKeyCreate(Ztls::Random &rng, ZuBSpan dbKey,
    ZuCSpan issuerURL, ZuCSpan id, int64_t now, SignKey &signer)
{
  if (!dbKey || !issuerURL || !id || now <= 0) return false;
  Ztls::PK::SK_EC key{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  Secret privateKey;
  Bytes publicKey;
  privateKey.length(Ztls::Backend::pkey_ec_key_size(key.key), false);
  publicKey.length(Ztls::Backend::pkey_ec_public_size(key.key), false);
  if (publicKey.length() != Ztls::COSE::ES256::PublicKeySize ||
      !Ztls::Backend::pkey_ec_export_private(key.key, privateKey) ||
      !Ztls::Backend::pkey_ec_export_public(key.key, publicKey)) return false;
  String x, y;
  if (!keyCoordinate(x, {publicKey.data() + 1,
        Ztls::COSE::ES256::CoordinateSize}) ||
      !keyCoordinate(y, {publicKey.data() + 1 +
        Ztls::COSE::ES256::CoordinateSize,
        Ztls::COSE::ES256::CoordinateSize})) {
    return false;
  }
  Bytes encrypted;
  serverSecretEncrypt(rng, dbKey, issuerURL, "zum.sign_key", id,
    "privateMaterial", privateKey, encrypted);
  privateKey.null();
  if (!encrypted) return false;
  String jwk;
  ZfJSON::save(jwk, PublicJWK{
    id, "EC", "P-256", "sig", "ES256", ZuMv(x), ZuMv(y)});
  signer = SignKey{
    .id = id, .issuer = issuerURL, .algorithm = "ES256",
    .publicJwk = ZuMv(jwk), .privateMaterial = ZuMv(encrypted),
    .notBefore = now, .state = State::Active,
    .version = 1, .created = now, .updated = now};
  return true;
}

struct JWTHeaderJSON {
  String typ;
  String alg;
  String kid;
};
ZfStruct(, (JWTHeaderJSON, JSON),
  (typ,		(Required),		String),
  (alg,		(Required),		String),
  (kid,		(Required),		String));

ZfStruct(, (AccessClaims, JSON),
  (issuerURL,	(JSON::ID<"iss">, Required),					String),
  (subject,		(JSON::ID<"sub">, Required),				String),
  (audience,		(JSON::ID<"aud">, Required),				String),
  (clientID,		(JSON::ID<"client_id">, Required),			String),
  (appID,		(JSON::ID<"zum_app_id">, JSON::String<>, Required),	UInt64),
  (iat,		(Required),							Int64),
  (nbf,		(Required),							Int64),
  (exp,		(Required),							Int64),
  (jti,		(Required),							String),
  (scope,		(Required),						String),
  (actions,		(Required),						StringVec),
  (authTime,		(JSON::ID<"auth_time">, JSON::Opt),			Int64),
  (amr,		(JSON::Opt),							StringVec));

ZfStruct(, (IDClaims, JSON),
  (issuerURL,	(JSON::ID<"iss">, Required),				String),
  (subject,		(JSON::ID<"sub">, Required),			String),
  (audience,		(JSON::ID<"aud">, Required),			String),
  (iat,		(Required),						Int64),
  (exp,		(Required),						Int64),
  (authTime,		(JSON::ID<"auth_time">, Required),		Int64),
  (amr,		(Required),						StringVec),
  (nonce,		(JSON::Opt),					String),
  (name,		(JSON::Opt),					String),
  (preferredUserName, (JSON::ID<"preferred_username">, JSON::Opt),	String),
  (email,		(JSON::Opt),					String));

struct UserInfo {
  String subject;
  String name;
  String preferredUserName;
  String email;
};
ZfStruct(, (UserInfo, JSON),
  (subject,		(JSON::ID<"sub">, Required),			String),
  (name,		(JSON::Opt),					String),
  (preferredUserName, (JSON::ID<"preferred_username">, JSON::Opt),	String),
  (email,		(JSON::Opt),					String));

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
    Ztls::Random &rng, ZuCSpan issuerURL, const App &app, ZuCSpan subject,
    const Client &client, const ScopeSelection &selection,
    const ZtBitmap &authority, ZuSpan<const Action> actions, AppID clientAppID,
    int64_t now, int64_t expires, AccessClaims &claims)
{
  ZuBArray<JWTIDSize> random(JWTIDSize, false);
  if (!rng.random(random)) return false;
  AccessClaims next;
  next.issuerURL = issuerURL;
  next.subject = subject;
  next.audience = app.audience;
  next.clientID = client.id;
  next.appID = clientAppID;
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
    Ztls::Random &rng, ZuCSpan issuerURL, const App &app, const User &user,
    const Client &client, const ScopeSelection &selection,
    const ZtBitmap &authority, ZuSpan<const Action> actions,
    ZuCSpan authMethod, int64_t authTime, int64_t now, int64_t expires,
    AccessClaims &claims)
{
  if (user.state != State::Active || !user.handle ||
      client.state != State::Active ||
      (client.profile != ClientProfile::Browser &&
       client.profile != ClientProfile::Native &&
       client.profile != ClientProfile::Server) ||
      !(client.grants & ClientGrant::AuthCode())) return false;
  String subject;
  if (!encode(subject, user.handle)) return false;
  AccessClaims next;
  if (!accessClaims(rng, issuerURL, app, subject, client, selection,
      authority, actions, client.appID, now, expires, next)) return false;
  if (authMethod != "passkey" && authMethod != "oidc") return false;
  next.authTime = authTime;
  next.amr.push(authMethod);
  claims = ZuMv(next);
  return true;
}

bool clientClaims(
    Ztls::Random &rng, ZuCSpan issuerURL, const App &app, const Client &client,
    const ScopeSelection &selection, const ZtBitmap &authority,
    ZuSpan<const Action> actions, AppID clientAppID,
    int64_t now, int64_t expires,
    AccessClaims &claims)
{
  if (client.state != State::Active ||
      clientType(client.profile) != ClientType::Confidential ||
      !(client.grants & ClientGrant::ClientCredentials())) return false;
  return accessClaims(rng, issuerURL, app, client.id, client, selection,
    authority, actions, clientAppID, now, expires, claims);
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
  if (!claims.issuerURL || !claims.subject || !claims.audience ||
      !claims.clientID || !claims.appID || !claims.jti || !claims.scope ||
      claims.actions.length() > limits.actions || claims.iat <= 0 ||
      claims.nbf <= 0 || claims.exp <= claims.iat || claims.nbf >= claims.exp ||
      (interactive != bool(claims.amr))) return false;
  if (interactive) {
    if (claims.amr.length() != 1) return false;
    auto authMethod = claims.amr[0];
    if (authMethod != "passkey" && authMethod != "oidc") return false;
  }

  ZfJSON::save(json, claims);
  return json.length() <= limits.json;
}

bool jwtPrepare(
    const AccessClaims &claims, ZuCSpan kid, const JWTLimits &limits,
    PreparedJWT &prepared)
{
  if (!kid || !limits.token || !limits.json || !limits.actions) return false;
  String header;
  ZfJSON::save(header, JWTHeaderJSON{"at+jwt", "ES256", kid});
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
    ZuCSpan issuerURL, const User &user, const Client &client,
    const ScopeSelection &selection,
    ZuCSpan nonce, ZuCSpan authMethod, int64_t authTime,
    int64_t now, int64_t expires, IDClaims &claims)
{
  if (!issuerURL || user.state != State::Active || user.owner || !user.handle ||
      client.state != State::Active || client.owner || !client.id ||
      !scopeContains(selection.scope, "openid") ||
      (authMethod != "passkey" && authMethod != "oidc") ||
      authTime <= 0 || now <= 0 || expires <= now) return false;
  IDClaims next;
  next.issuerURL = issuerURL;
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
  if (!kid || !claims.issuerURL || !claims.subject || !claims.audience ||
      claims.amr.length() != 1 || claims.iat <= 0 ||
      claims.exp <= claims.iat || claims.authTime <= 0 ||
      !limits.token || !limits.json) return false;
  auto authMethod = claims.amr[0];
  if (authMethod != "passkey" && authMethod != "oidc") return false;
  String header;
  ZfJSON::save(header, JWTHeaderJSON{"JWT", "ES256", kid});
  String json;
  ZfJSON::save(json, claims);
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
  UserInfo info{.subject = ZuMv(subject)};
  if (scopeContains(principal.scope, "profile")) {
    if (user.profile || user.name) info.name = user.profile ? user.profile : user.name;
    info.preferredUserName = user.name;
  }
  if (scopeContains(principal.scope, "email")) info.email = user.email;
  json.null();
  ZfJSON::save(json, info);
  return true;
}

} // namespace Zum
