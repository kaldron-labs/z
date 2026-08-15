//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuArray.hh>
#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuCmp.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZfJSON.hh>

#include <zlib/Ztls.hh>
#include <zlib/ZtlsHMAC.hh>

#include "zrestjwt.hh"

ZtEnumImplStruct(TokenType);

struct JWTAlg {
  using T = int8_t;
  enum { Invalid = -1, HS256, N };
  ZtEnumNames(, JWTAlg, HS256);
  struct Map : public Map_ { };
};
struct JWTTyp {
  using T = int8_t;
  enum { Invalid = -1, JWT, N };
  ZtEnumNames(, JWTTyp, JWT);
  struct Map : public Map_ { };
};

ZtEnumImplStruct(JWTAlg);
ZtEnumImplStruct(JWTTyp);

struct JWTHeader {
  JWTAlg::T alg = JWTAlg::Invalid;
  JWTTyp::T typ = JWTTyp::Invalid;
};

struct JWTClaims {
  CredString sub;
  uint128_t jti = 0;
  int64_t iat = 0;
  int64_t nbf = 0;
  int64_t exp = 0;
  TokenType::T tokenType = TokenType::Invalid;
};

using JTIFormat = ZuFmt::Hex<false, ZuFmt::Right<32>>;

ZfStruct((JWTHeader, JSON),
  (((alg), (Enum<JWTAlg::Map>, Required)), (Int8)),
  (((typ), (Enum<JWTTyp::Map>, Required)), (Int8)));
ZfStruct((JWTClaims, JSON),
  (((sub), (Required)), (String)),
  (((iat), (Required)), (Int64)),
  (((nbf), (Required)), (Int64)),
  (((exp), (Required)), (Int64)),
  (((jti),
    (JSON::String<JTIFormat>, Required)), (UInt128)),
  (((tokenType),
    (JSON::ID<"token_type">, Enum<TokenType::Map>, Required)), (Int8)));

ZuDerive(JWTBuf, (ZtArray<uint8_t, ZtArrayHeapID<"zrest.JWTBuf">>));
ZuDerive(JWTJSON, (ZtString<ZtStringHeapID<"zrest.JWT.JSON">>));

static constexpr ZuString JWTHeaderEncoded{
  "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9"};

static bool encodePart(TokenString &out, ZuBSpan raw)
{
  uint64_t old = out.length();
  uint64_t n = ZuBase64URL::enclen(raw.length());
  if (old + n > JWTMax) return false;
  out.length(old + n);
  return ZuBase64URL::encode(
    ZuSpan<uint8_t>{reinterpret_cast<uint8_t *>(out.data() + old), n}, raw) == n;
}

template <typename Object>
static bool encodeJSON(TokenString &out, const Object &object)
{
  auto json = ZtScratch(JWTJSON, JWTScratchSize);
  try {
    ZfJSON::save(json, object);
  } catch (...) {
    return false;
  }
  if (json.length() > JWTPartMax) return false;
  return encodePart(out, ZuBSpan{json});
}

static bool issueToken(
    Ztls::Random &rng, ZuCSpan secret, ZuCSpan subject, int64_t now,
    uint64_t lifetime, TokenType::T type, TokenString &token)
{
  if (!secret || !subject || now <= 0 ||
      lifetime > uint64_t(INT64_MAX - now)) return false;
  JWTClaims claims;
  claims.sub = subject;
  claims.iat = claims.nbf = now;
  claims.exp = now + int64_t(lifetime);
  claims.tokenType = type;
  do {
    if (!rng.random(ZuSpan<uint8_t>{
	reinterpret_cast<uint8_t *>(&claims.jti), sizeof(claims.jti)})) return false;
  } while (!claims.jti);

  token.null();
  token << JWTHeaderEncoded << '.';
  if (!encodeJSON(token, claims)) return false;
  if (token.length() + 1 + ZuBase64URL::enclen(Ztls::HMAC<>::Size) > JWTMax)
    return false;

  ZuBArray<Ztls::HMAC<>::Size> signature(Ztls::HMAC<>::Size, false);
  Ztls::HMAC<> hmac;
  hmac.start(ZuBSpan{secret});
  hmac.update(ZuBSpan{token});
  hmac.finish(signature);
  token << '.';
  return encodePart(token, signature);
}

bool jwtIssuePair(
    Ztls::Random &rng, ZuCSpan secret, ZuCSpan subject, int64_t now,
    uint64_t accessSecs, uint64_t refreshSecs, TokenResponse &response)
{
  if (!accessSecs || refreshSecs <= accessSecs) return false;
  TokenResponse next;
  if (!issueToken(rng, secret, subject, now, accessSecs,
	TokenType::access, next.accessToken) ||
      !issueToken(rng, secret, subject, now, refreshSecs,
	TokenType::refresh, next.refreshToken)) return false;
  next.expiresIn = accessSecs;
  response.accessToken = ZuMv(next.accessToken);
  response.refreshToken = ZuMv(next.refreshToken);
  response.expiresIn = next.expiresIn;
  return true;
}

template <typename Buffer>
static bool decodePart(ZuCSpan encoded, Buffer &decoded)
{
  unsigned length = encoded.length();
  if (!length || length > JWTPartMax || (length & 3) == 1) return false;
  for (unsigned i = 0; i < length; ++i)
    if (!ZuBase64URL::is(encoded[i])) return false;
  uint8_t tail = ZuBase64URL::lookup(encoded[length - 1]);
  switch (length & 3) {
    case 2: if (tail & 15) return false; break;
    case 3: if (tail & 3) return false; break;
  }
  unsigned n = ZuBase64URL::declen(length);
  decoded.length(n);
  return ZuBase64URL::decode(decoded, ZuBSpan{encoded}) == n;
}

template <typename Object>
static bool decodeJSON(ZuCSpan encoded, Object &object)
{
  auto decoded = ZtScratch(JWTBuf, JWTScratchSize);
  if (!decodePart(encoded, decoded)) return false;
  try {
    auto scan = ZfJSON::scan(decoded.span());
    if (scan.p<0>() != int(decoded.length()) || !scan.p<1>() ||
	scan.p<1>()->data<ZfJSON::NodeArray>().length() != 1) return false;
    ZfJSON::handler<Object>((*scan.p<1>())[0]).load(object);
  } catch (...) {
    return false;
  }
  return true;
}

bool jwtValidate(
    ZuCSpan secret, ZuCSpan token, TokenType::T requiredType,
    int64_t now, CredString &subject)
{
  if (!secret || !token || token.length() > JWTMax || now <= 0) return false;
  int first = -1, second = -1;
  for (unsigned i = 0, n = token.length(); i < n; ++i) {
    if (token[i] != '.') continue;
    if (first < 0) first = i;
    else if (second < 0) second = i;
    else return false;
  }
  if (first <= 0 || second <= first + 1 || second + 1 >= int(token.length()))
    return false;
  ZuCSpan headerPart{token.data(), unsigned(first)};
  ZuCSpan claimsPart{token.data() + first + 1, unsigned(second - first - 1)};
  ZuCSpan signaturePart{
    token.data() + second + 1, token.length() - unsigned(second + 1)};

  JWTHeader header;
  JWTClaims claims;
  auto signature = ZtScratch(JWTBuf, JWTScratchSize);
  if (!decodeJSON(headerPart, header) || !decodeJSON(claimsPart, claims) ||
      !decodePart(signaturePart, signature) ||
      signature.length() != Ztls::HMAC<>::Size ||
      header.alg != JWTAlg::HS256 || header.typ != JWTTyp::JWT ||
      !claims.sub || !claims.jti || ZuCmp<uint128_t>::null(claims.jti) ||
      claims.tokenType != requiredType ||
      claims.iat <= 0 || claims.nbf <= 0 || claims.exp <= 0 ||
      claims.iat >= claims.exp || claims.nbf >= claims.exp ||
      claims.iat > now || claims.nbf > now || now >= claims.exp) return false;

  ZuBArray<Ztls::HMAC<>::Size> expected(Ztls::HMAC<>::Size, false);
  Ztls::HMAC<> hmac;
  hmac.start(ZuBSpan{secret});
  hmac.update(ZuBSpan{token.data(), unsigned(second)});
  hmac.finish(expected);
  if (Ztls_memcmp(expected.data(), signature.data(), Ztls::HMAC<>::Size))
    return false;
  CredString validSubject = ZuMv(claims.sub);
  subject = ZuMv(validSubject);
  return true;
}
