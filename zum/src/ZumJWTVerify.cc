//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumJWTVerify.hh>

#include <zlib/ZuArray.hh>
#include <zlib/ZuBase64URL.hh>
#include <zlib/ZfJSON.hh>

#include <zlib/ZtlsCOSE.hh>

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
  AppID		appID = 0;
};
ZfStruct(, (JWTHeader, JSON),
  (((type),		(JSON::ID<"typ">, JSON::Opt)),	String),
  (((algorithm),	(JSON::ID<"alg">, Required)),	String),
  (((keyID),		(JSON::ID<"kid">, Required)),	String));
ZfStruct(, (ClaimsJSON, JSON),
  (((iss),		(Required)),						String),
  (((sub),		(Required)),						String),
  (((aud),		(Required)),						String),
  (((clientID),		(JSON::ID<"client_id">, Required)),			String),
  (((jti),		(Required)),						String),
  (((scope),		(Required)),						String),
  (((actions),		(Required)),						StringVec),
  (((iat),		(Required)),						Int64),
  (((nbf),		(Required)),						Int64),
  (((exp),		(Required)),						Int64),
  (((authTime),		(JSON::ID<"auth_time">, JSON::Opt)),		Int64),
  (((amr),		(JSON::Opt)),						StringVec),
  (((appID),		(JSON::ID<"zum_app_id">, JSON::String<>, Required)),	UInt64));

static bool decodePart(ZuCSpan encoded, unsigned limit, JWTBytes &decoded)
{
  if (!encoded || encoded.length() > ZuBase64URL::enclen(limit)) return false;
  auto n = ZuBase64URL::declen(encoded.length());
  decoded.length(n, false);
  return ZuBase64URL::decode(decoded, ZuBSpan{encoded}) == n;
}

template <typename T>
static bool loadJSON(JWTBytes &json, T &value)
{
  auto parsed = ZfJSON::scan(ZuSpan<char>{json});
  if (parsed.p<0>() != int(json.length()) || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 || !roots[0]->has<ZfJSON::AnyNode::Object>())
    return false;
  auto handler = ZfJSON::handler<T>(roots[0]);
  if (!handler.valid) return false;
  value = handler.ctor();
  return true;
}

static bool loadHeader(JWTBytes &json, JWTHeader &header)
{
  JWTHeader next;
  if (!loadJSON(json, next) || !next.algorithm || !next.keyID) return false;
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
    loadHeader(decoded, header);
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
      !loadHeader(headerJSON, nextHeader) ||
      nextHeader.algorithm != "ES256" ||
      !decodePart(signaturePart, Ztls::COSE::ES256::SignatureSize, signature) ||
      signature.length() != Ztls::COSE::ES256::SignatureSize) return false;
  ZuBArray<Ztls::COSE::ES256::DERMax> der(
    Ztls::COSE::ES256::DERMax, false);
  unsigned derLength;
  if (!Ztls::COSE::ES256::rawToDER(signature, der, derLength) ||
      !Ztls::COSE::ES256::verify(publicKey,
        ZuBSpan{token.data(),
          unsigned(signaturePart.data() - token.data() - 1)},
        ZuBSpan{der.data(), derLength})) return false;

  JWTBytes claimsJSON;
  if (!decodePart(claimsPart, limits.json, claimsJSON)) return false;
  String nextClaims{claimsJSON};
  nextClaims.length(nextClaims.length());
  header = ZuMv(nextHeader);
  claims = ZuMv(nextClaims);
  return true;
}

static bool loadClaims(
    JWTBytes &json, ZuCSpan issuerURL, ZuCSpan audience, int64_t now,
    const JWTLimits &limits, Principal &principal)
{
  ClaimsJSON claims;
  if (!loadJSON(json, claims)) return false;
  bool interactive = claims.authTime || claims.amr;
  if (claims.iss != issuerURL || (audience && claims.aud != audience) ||
      !claims.aud || !claims.sub || !claims.clientID || !claims.appID ||
      !claims.jti || !claims.scope ||
      claims.actions.length() > limits.actions || claims.iat <= 0 ||
      claims.nbf <= 0 || claims.exp <= claims.iat || claims.nbf >= claims.exp ||
      claims.iat > now || claims.nbf > now || now >= claims.exp) return false;
  String authMethod;
  if (interactive) {
    if (claims.authTime <= 0 || claims.authTime > claims.iat ||
	claims.amr.length() != 1) return false;
    authMethod = ZuMv(claims.amr[0]);
    if (authMethod != "passkey" && authMethod != "oidc") return false;
  }

  Principal next;
  next.tokenID = TokenID{
    .issuerURL = ZuMv(claims.iss), .jti = ZuMv(claims.jti)};
  next.audience = ZuMv(claims.aud);
  next.subject = ZuMv(claims.sub);
  next.clientID = ZuMv(claims.clientID);
  next.scope = ZuMv(claims.scope);
  next.actions = ZuMv(claims.actions);
  next.expires = claims.exp;
  next.authTime = interactive ? claims.authTime : 0;
  if (interactive) next.authMethod = ZuMv(authMethod);
  next.appID = claims.appID;
  principal = ZuMv(next);
  return true;
}

bool jwtVerify(
    ZuCSpan token, ZuCSpan kid, ZuCSpan issuerURL, ZuCSpan audience,
    ZuBSpan publicKey, int64_t now, const JWTLimits &limits,
    Principal &principal)
{
  if (!kid || !issuerURL || !audience || now <= 0) return false;
  JWTHeader header;
  String claimsJSON;
  if (!jwtES256(token, publicKey, limits, header, claimsJSON) ||
      header.type != "at+jwt" || header.keyID != kid) return false;
  JWTBytes claims{claimsJSON};
  return loadClaims(claims, issuerURL, audience, now, limits, principal);
}

bool jwtVerifyIssuer(
    ZuCSpan token, ZuCSpan kid, ZuCSpan issuerURL,
    ZuBSpan publicKey, int64_t now, const JWTLimits &limits,
    Principal &principal)
{
  if (!kid || !issuerURL || now <= 0) return false;
  JWTHeader header;
  String claimsJSON;
  if (!jwtES256(token, publicKey, limits, header, claimsJSON) ||
      header.type != "at+jwt" || header.keyID != kid) return false;
  JWTBytes claims{claimsJSON};
  return loadClaims(claims, issuerURL, {}, now, limits, principal);
}

} // namespace Zum
