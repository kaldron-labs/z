//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumJWTVerify.hh>

#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuBox.hh>

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

static bool decodePart(ZuCSpan encoded, unsigned limit, JWTBytes &decoded)
{
  if (!encoded || encoded.length() > ZuBase64URL::enclen(limit)) return false;
  auto n = ZuBase64URL::declen(encoded.length());
  decoded.length(n, false);
  return ZuBase64URL::decode(decoded, ZuBSpan{encoded}) == n;
}

static ZfJSON::AnyNode *jsonObject(ZfJSON::AnyNode *tree)
{
  if (!tree || !tree->has<ZfJSON::AnyNode::Array>()) return nullptr;
  auto &roots = tree->data<ZfJSON::AnyNode::Array>();
  if (!roots || !roots[0]->has<ZfJSON::AnyNode::Object>()) return nullptr;
  return roots[0];
}

static bool loadHeader(JWTBytes &json, JWTHeader &header)
{
  auto parsed = ZfJSON::scan(ZuSpan<char>{json});
  if (parsed.p<0>() < 0) return false;
  auto node = jsonObject(parsed.p<1>());
  if (!node) return false;
  auto &fields = node->data<ZfJSON::AnyNode::Object>();
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
      continue;
    }
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
  uint8_t der[Ztls::COSE::ES256::DERMax];
  unsigned derLength;
  if (!Ztls::COSE::ES256::rawToDER(signature, der, derLength) ||
      !Ztls::COSE::ES256::verify(publicKey,
        ZuBSpan{token.data(),
          unsigned(signaturePart.data() - token.data() - 1)},
        ZuBSpan{der, derLength})) return false;

  JWTBytes claimsJSON;
  if (!decodePart(claimsPart, limits.json, claimsJSON)) return false;
  String nextClaims{ZuBSpan{claimsJSON}};
  nextClaims.length(nextClaims.length());
  header = ZuMv(nextHeader);
  claims = ZuMv(nextClaims);
  return true;
}

static bool loadClaims(
    JWTBytes &json, ZuCSpan issuer, ZuCSpan audience, int64_t now,
    const JWTLimits &limits, Principal &principal)
{
  auto parsed = ZfJSON::scan(ZuSpan<char>{json});
  if (parsed.p<0>() < 0) return false;
  auto node = jsonObject(parsed.p<1>());
  if (!node) return false;
  ClaimsJSON claims;
  for (auto &field: node->data<ZfJSON::AnyNode::Object>()) {
    auto name = field.p<0>();
    auto value = field.p<1>().ptr();
    if (name == "iss" || name == "sub" || name == "aud" ||
	name == "client_id" || name == "jti" || name == "scope") {
      if (!value->has<ZfJSON::AnyNode::String>()) return false;
      auto &string = value->data<ZfJSON::AnyNode::String>();
      if (name == "iss") claims.iss = string;
      else if (name == "sub") claims.sub = string;
      else if (name == "aud") claims.aud = string;
      else if (name == "client_id") claims.clientID = string;
      else if (name == "jti") claims.jti = string;
      else claims.scope = string;
    } else if (name == "zum_app_id") {
      if (!value->has<ZfJSON::AnyNode::String>()) return false;
      claims.appID = ZuBox<uint64_t>{
	value->data<ZfJSON::AnyNode::String>()};
    } else if (name == "iat" || name == "nbf" || name == "exp" ||
	name == "auth_time") {
      if (!value->has<ZfJSON::AnyNode::Number>()) return false;
      auto number = ZfJSON::eov_Decimal(
	value->data<ZfJSON::AnyNode::Number>());
      if (number.p<0>() < 0) return false;
      auto integer = int64_t(number.p<1>().floor());
      if (name == "iat") claims.iat = integer;
      else if (name == "nbf") claims.nbf = integer;
      else if (name == "exp") claims.exp = integer;
      else claims.authTime = integer;
    } else if (name == "actions" || name == "amr") {
      if (!value->has<ZfJSON::AnyNode::Array>()) return false;
      auto &array = value->data<ZfJSON::AnyNode::Array>();
      if (array.length() > limits.actions) return false;
      StringVec strings;
      for (auto &item: array) {
	if (!item->has<ZfJSON::AnyNode::String>()) return false;
	strings.push(item->data<ZfJSON::AnyNode::String>());
      }
      if (name == "actions") claims.actions = ZuMv(strings);
      else claims.amr = ZuMv(strings);
    }
  }
  bool interactive = claims.authTime || claims.amr;
  if (claims.iss != issuer || (audience && claims.aud != audience) ||
      !claims.aud || !claims.sub || !claims.clientID || !claims.appID ||
      !claims.jti || !claims.scope ||
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
  next.appID = claims.appID;
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

bool jwtVerifyIssuer(
    ZuCSpan token, ZuCSpan kid, ZuCSpan issuer,
    ZuBSpan publicKey, int64_t now, const JWTLimits &limits,
    Principal &principal)
{
  if (!kid || !issuer || now <= 0) return false;
  JWTHeader header;
  String claimsJSON;
  if (!jwtES256(token, publicKey, limits, header, claimsJSON) ||
      header.type != "at+jwt" || header.keyID != kid) return false;
  JWTBytes claims{ZuBSpan{claimsJSON}};
  return loadClaims(claims, issuer, {}, now, limits, principal);
}

} // namespace Zum
