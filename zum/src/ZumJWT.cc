//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumJWT.hh>

#include <zlib/ZuBase64URL.hh>

#include <zlib/ZfJSON.hh>

#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsSec.hh>

namespace Zum {

ZuDerive(JWTBytes, (ZtArray<uint8_t, ZtArrayHeapID<"Zum.JWT.Bytes">>));

struct ClaimsJSON {
  String	iss;
  String	sub;
  String	aud;
  String	clientID;
  String	jti;
  String	scope;
  StringVec	actions;
  int64_t	iat = 0;
  int64_t	nbf = 0;
  int64_t	exp = 0;
  int64_t	authTime = 0;
  StringVec	amr;
};

static bool encode(String &out, ZuBSpan data)
{
  auto n = ZuBase64URL::enclen(data.length());
  out.length(n);
  return ZuBase64URL::encode({
    reinterpret_cast<uint8_t *>(out.data()), unsigned(n)}, data) == n;
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
       client.type != ClientType::Native) ||
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
    ZuSpan<const Action> actions, int64_t now, int64_t expires,
    AccessClaims &claims)
{
  if (client.state != State::Active ||
      client.type != ClientType::Confidential ||
      !(client.grants & ClientGrant::ClientCredentials)) return false;
  return accessClaims(rng, issuer, client.id, client, selection,
    authority, actions, now, expires, claims);
}

ZfStruct((ClaimsJSON, JSON),
  (((iss),		(Required)),			(String)),
  (((sub),		(Required)),			(String)),
  (((aud),		(Required)),			(String)),
  (((clientID),	(JSON::ID<"client_id">, Required)),	(String)),
  (((jti),		(Required)),			(String)),
  (((scope),		(Required)),			(String)),
  (((actions),		(Required)),			(StringVec)),
  (((iat),		(Required)),			(Int64)),
  (((nbf),		(Required)),			(Int64)),
  (((exp),		(Required)),			(Int64)),
  (((authTime),	(JSON::ID<"auth_time">, JSON::Opt)),	(Int64)),
  (((amr),		(JSON::Opt)),			(StringVec)));

static bool encodePart(String &out, ZuBSpan data, unsigned limit)
{
  auto old = out.length();
  auto n = ZuBase64URL::enclen(data.length());
  if (old + n > limit) return false;
  out.length(old + n);
  return ZuBase64URL::encode({
    reinterpret_cast<uint8_t *>(out.data() + old), unsigned(n)}, data) == n;
}

static bool claimsJSON(
    String &json, const AccessClaims &claims, const JWTLimits &limits)
{
  bool interactive = claims.authTime > 0;
  if (!claims.issuer || !claims.subject || !claims.audience ||
      !claims.clientID || !claims.jti || !claims.scope || !claims.actions ||
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

bool jwtFinish(
    PreparedJWT &prepared, ZuBSpan derSignature, const JWTLimits &limits)
{
  uint8_t raw[Ztls::ES256::SignatureSize];
  if (!Ztls::es256DERToRaw(derSignature, raw) ||
      prepared.token.length() + 1 + ZuBase64URL::enclen(sizeof(raw)) >
        limits.token) return false;
  prepared.token << '.';
  return encodePart(prepared.token, raw, limits.token);
}

static bool decodePart(ZuCSpan encoded, unsigned limit, JWTBytes &decoded)
{
  if (!encoded || (encoded.length() & 3) == 1 ||
      encoded.length() > ZuBase64URL::enclen(limit)) return false;
  auto n = ZuBase64URL::declen(encoded.length());
  decoded.length(n, false);
  return ZuBase64URL::decode(decoded, ZuBSpan{encoded}) == n;
}

static ZfJSON::AnyNode *jsonObject(
    JWTBytes &json, const JWTLimits &limits, ZfJSON::ScanResult &result)
{
  result = ZfJSON::scanStrict({
    reinterpret_cast<char *>(json.data()), json.length()},
    {limits.json, limits.depth, limits.nodes, limits.string});
  if (!result || !result.root->has<ZfJSON::AnyNode::Array>()) return nullptr;
  auto &roots = result.root->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 ||
      !roots[0]->has<ZfJSON::AnyNode::Object>()) return nullptr;
  return roots[0];
}

static bool loadHeader(
    JWTBytes &json, const JWTLimits &limits, JWTHeader &header)
{
  ZfJSON::ScanResult result;
  auto node = jsonObject(json, limits, result);
  if (!node) return false;
  auto &fields = node->data<ZfJSON::AnyNode::Object>();
  if (fields.length() < 2 || fields.length() > 3) return false;
  unsigned seen = 0;
  JWTHeader next;
  for (auto &field: fields) {
    auto value = field.p<1>().ptr();
    if (!value->has<ZfJSON::AnyNode::String>()) return false;
    auto string = value->data<ZfJSON::AnyNode::String>();
    unsigned bit;
    if (field.p<0>() == "typ") {
      bit = 1U;
      next.type = string;
    } else if (field.p<0>() == "alg") {
      bit = 2U;
      next.algorithm = string;
    } else if (field.p<0>() == "kid") {
      bit = 4U;
      next.keyID = string;
    } else {
      return false;
    }
    if (seen & bit) return false;
    seen |= bit;
  }
  if ((seen & 6U) != 6U || !next.algorithm || !next.keyID) return false;
  header = ZuMv(next);
  return true;
}

static bool splitJWT(
    ZuCSpan token, ZuCSpan &header, ZuCSpan &claims, ZuCSpan &signature,
    unsigned limit)
{
  if (!token || token.length() > limit) return false;
  int first = -1, second = -1;
  for (unsigned i = 0, n = token.length(); i < n; ++i) {
    if (token[i] != '.') continue;
    if (first < 0) first = i;
    else if (second < 0) second = i;
    else return false;
  }
  if (first <= 0 || second <= first + 1 ||
      second + 1 >= int(token.length())) return false;
  header = {token.data(), unsigned(first)};
  claims = {
    token.data() + first + 1, unsigned(second - first - 1)};
  signature = {
    token.data() + second + 1, token.length() - unsigned(second + 1)};
  return true;
}

bool jwtHeader(ZuCSpan token, const JWTLimits &limits, JWTHeader &header)
{
  ZuCSpan headerPart, claimsPart, signaturePart;
  JWTBytes decoded;
  return splitJWT(token, headerPart, claimsPart, signaturePart, limits.token) &&
    decodePart(headerPart, limits.json, decoded) &&
    loadHeader(decoded, limits, header);
}

bool jwtES256(
    ZuCSpan token, ZuBSpan publicKey, const JWTLimits &limits,
    JWTHeader &header, String &claims)
{
  ZuCSpan headerPart, claimsPart, signaturePart;
  if (!publicKey || !splitJWT(
      token, headerPart, claimsPart, signaturePart, limits.token)) return false;

  JWTBytes headerJSON, signature;
  JWTHeader nextHeader;
  if (!decodePart(headerPart, limits.json, headerJSON) ||
      !loadHeader(headerJSON, limits, nextHeader) ||
      nextHeader.algorithm != "ES256" ||
      !decodePart(signaturePart, Ztls::ES256::SignatureSize, signature) ||
      signature.length() != Ztls::ES256::SignatureSize) return false;
  uint8_t der[Ztls::ES256::DERMax];
  unsigned derLength;
  if (!Ztls::es256RawToDER(signature, der, derLength) ||
      !Ztls::es256Verify(publicKey,
        ZuBSpan{reinterpret_cast<const uint8_t *>(token.data()),
          unsigned(signaturePart.data() - token.data() - 1)},
        ZuBSpan{der, derLength})) return false;

  JWTBytes claimsJSON;
  if (!decodePart(claimsPart, limits.json, claimsJSON)) return false;
  String nextClaims{ZuBSpan{claimsJSON}};
  header = ZuMv(nextHeader);
  claims = ZuMv(nextClaims);
  return true;
}

static bool claimsFields(
    ZfJSON::AnyNode *node, bool &interactive)
{
  auto &fields = node->data<ZfJSON::AnyNode::Object>();
  uint16_t seen = 0;
  for (auto &field: fields) {
    unsigned bit;
    auto id = field.p<0>();
    if (id == "iss") bit = 1U<<0;
    else if (id == "sub") bit = 1U<<1;
    else if (id == "aud") bit = 1U<<2;
    else if (id == "client_id") bit = 1U<<3;
    else if (id == "jti") bit = 1U<<4;
    else if (id == "scope") bit = 1U<<5;
    else if (id == "actions") bit = 1U<<6;
    else if (id == "iat") bit = 1U<<7;
    else if (id == "nbf") bit = 1U<<8;
    else if (id == "exp") bit = 1U<<9;
    else if (id == "auth_time") bit = 1U<<10;
    else if (id == "amr") bit = 1U<<11;
    else return false;
    seen |= bit;
  }
  constexpr uint16_t required = (1U<<10) - 1;
  interactive = (seen & (3U<<10)) == (3U<<10);
  return (seen & required) == required &&
    (!(seen & (3U<<10)) || interactive) &&
    fields.length() == 10U + (interactive ? 2U : 0U);
}

static bool loadClaims(
    JWTBytes &json, ZuCSpan issuer, ZuCSpan audience, int64_t now,
    const JWTLimits &limits, Principal &principal)
{
  ZfJSON::ScanResult result;
  auto node = jsonObject(json, limits, result);
  bool interactive;
  if (!node || !claimsFields(node, interactive)) return false;
  ClaimsJSON claims;
  try {
    ZfJSON::handler<ClaimsJSON>(node).load(claims);
  } catch (...) {
    return false;
  }
  if (claims.iss != issuer || claims.aud != audience || !claims.sub ||
      !claims.clientID || !claims.jti || !claims.scope || !claims.actions ||
      claims.actions.length() > limits.actions || claims.iat <= 0 ||
      claims.nbf <= 0 || claims.exp <= claims.iat || claims.nbf >= claims.exp ||
      claims.iat > now || claims.nbf > now || now >= claims.exp) return false;
  if (interactive && (claims.authTime <= 0 || claims.authTime > claims.iat ||
      claims.amr.length() != 1 ||
      (claims.amr[0] != "passkey" && claims.amr[0] != "oidc"))) return false;

  Principal next;
  next.subject = ZuMv(claims.sub);
  next.clientID = ZuMv(claims.clientID);
  next.scope = ZuMv(claims.scope);
  next.actions = ZuMv(claims.actions);
  next.expires = claims.exp;
  next.authTime = interactive ? claims.authTime : 0;
  if (interactive) next.authMethod = ZuMv(claims.amr[0]);
  principal = ZuMv(next);
  return true;
}

bool jwtVerify(
    ZuCSpan token, ZuCSpan kid, ZuCSpan issuer, ZuCSpan audience,
    ZuBSpan publicKey, int64_t now, const JWTLimits &limits,
    Principal &principal)
{
  if (!kid || !issuer || !audience || now <= 0) return false;
  JWTHeader header;
  String claimsJSON;
  if (!jwtES256(token, publicKey, limits, header, claimsJSON) ||
      header.type != "at+jwt" || header.keyID != kid) return false;
  JWTBytes claims{ZuBSpan{claimsJSON}};
  return loadClaims(claims, issuer, audience, now, limits, principal);
}

} // namespace Zum
