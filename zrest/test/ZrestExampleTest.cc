//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuBase64URL.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZfJSON.hh>
#include <zlib/ZfURI.hh>

#include <zlib/ZtlsHMAC.hh>

#include "zrestjwt.hh"
#include "zrestproto.hh"

using namespace ZuTestUtil;

struct TestClaims {
  uint128_t jti = 0;
};

using TestJTIFormat = ZuFmt::Hex<false, ZuFmt::Right<32>>;

ZfStruct((TestClaims, JSON),
  (((jti), (JSON::String<TestJTIFormat>, Required)), (UInt128)));

ZuDerive(TestJWTBuf, (ZtArray<uint8_t,
  ZtArrayHeapID<"zrest.Test.JWTBuf">>));

static uint128_t tokenJTI(ZuCSpan token)
{
  int first = token.find([](char c) { return c == '.'; });
  if (first < 0) return 0;
  int second = ZuCSpan{token.data() + first + 1,
    token.length() - unsigned(first + 1)}.find(
      [](char c) { return c == '.'; });
  if (second < 0) return 0;
  ZuCSpan encoded{token.data() + first + 1, unsigned(second)};
  TestJWTBuf decoded;
  decoded.length(ZuBase64URL::declen(encoded.length()));
  if (ZuBase64URL::decode(decoded, ZuBSpan{encoded}) != decoded.length())
    return 0;
  TestClaims claims;
  try {
    auto scan = ZfJSON::scan(decoded.span());
    if (scan.p<0>() != int(decoded.length()) || !scan.p<1>()) return 0;
    ZfJSON::handler<TestClaims>((*scan.p<1>())[0]).load(claims);
  } catch (...) {
    return 0;
  }
  return claims.jti;
}

static void durationTest()
{
  ZuTestScope(duration);
  uint64_t n = 17;
  ZuCheck(parseDuration("1s", false, n) && n == 1);
  ZuCheck(parseDuration("2m", false, n) && n == 120);
  ZuCheck(parseDuration("3h", false, n) && n == 10800);
  ZuCheck(!parseDuration("0s", false, n) && n == 10800);
  ZuCheck(parseDuration("0s", true, n) && n == 0);
  n = 19;
  ZuCheck(!parseDuration("1.5s", false, n) && n == 19);
  ZuCheck(!parseDuration("1h2m", false, n) && n == 19);
  ZuCheck(!parseDuration("1", false, n) && n == 19);
  ZuCheck(!parseDuration("18446744073709551616s", false, n) && n == 19);
  ZuCheck(!parseDuration("18446744073709551615h", false, n) && n == 19);
  ZuCheck(!parseDuration("", false, n) && n == 19);
  ZuCheck(!parseDuration("-1s", false, n) && n == 19);
  ZuCheck(!parseDuration("1S", false, n) && n == 19);
}

static void mappingTest()
{
  ZuTestScope(mapping);
  TokenResponse response;
  response.accessToken = "access";
  response.refreshToken = "refresh";
  response.expiresIn = 3;
  ZtString<> json;
  ZfJSON::save(json, response);
  ZuCheck(json ==
    "{\"access_token\":\"access\",\"refresh_token\":\"refresh\",\"expires_in\":3}");
  Ping ping;
  ping.ping = true;
  ZtString<> uri;
  ZfURI::save(uri, ping);
  ZuCheck(uri == "?ping=true");

  TokenResponse loaded;
  auto jsonScan = ZfJSON::scan(json.span());
  ZfJSON::handler<TokenResponse>((*jsonScan.p<1>())[0]).load(loaded);
  ZuCheck(loaded.accessToken == "access" &&
    loaded.refreshToken == "refresh" && loaded.expiresIn == 3);
  Ping loadedPing;
  auto uriScan = ZfURI::scan(uri.span());
  ZfURI::handler<Ping>(uriScan.p<1>()).load(loadedPing);
  ZuCheck(loadedPing.ping);
}

static TokenString testToken(ZuCSpan header, ZuCSpan claims, ZuCSpan secret)
{
  TokenString token;
  auto encode = [&token](ZuCSpan value) {
    unsigned old = token.length();
    unsigned n = ZuBase64URL::enclen(value.length());
    token.length(old + n);
    ZuBase64URL::encode(ZuSpan<uint8_t>{
      reinterpret_cast<uint8_t *>(token.data() + old), n}, ZuBSpan{value});
  };
  encode(header);
  token << '.';
  encode(claims);
  ZuBArray<Ztls::HMAC<>::Size> signature(Ztls::HMAC<>::Size, false);
  Ztls::HMAC<> hmac;
  hmac.start(ZuBSpan{secret});
  hmac.update(ZuBSpan{token});
  hmac.finish(signature);
  token << '.';
  unsigned old = token.length();
  unsigned n = ZuBase64URL::enclen(signature.length());
  token.length(old + n);
  ZuBase64URL::encode(ZuSpan<uint8_t>{
    reinterpret_cast<uint8_t *>(token.data() + old), n}, signature);
  return token;
}

static void jwtTest()
{
  ZuTestScope(jwt);
  static const char vector[] =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJzdWIiOiJ0ZXN0IiwiaWF0IjoxNzAwMDAwMDAwLCJuYmYiOjE3MDAwMDAwMDAs"
    "ImV4cCI6MTcwMDAwMDA2MCwianRpIjoiMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAw"
    "MDAwMDAwMDEiLCJ0b2tlbl90eXBlIjoiYWNjZXNzIn0."
    "kBGU2FYqAFIj-jS1Tr95K-bz3rxxzMVxfPowyX3MXdk";
  CredString subject;
  ZuCheck(jwtValidate("matrix-secret", vector, TokenType::access,
    1700000001, subject) && subject == "test");
  ZuCheck(!jwtValidate("matrix-secret", vector, TokenType::refresh,
    1700000001, subject));
  ZuCheck(!jwtValidate("matrix-secret", vector, TokenType::access,
    1700000060, subject));

  Ztls::Random rng;
  ZuCheck(rng.init());
  TokenResponse first, second;
  ZuCheck(jwtIssuePair(rng, "secret", "user", 1700000000, 1, 60, first));
  ZuCheck(jwtIssuePair(rng, "secret", "user", 1700000000, 1, 60, second));
  uint128_t firstJTI = tokenJTI(first.accessToken);
  uint128_t secondJTI = tokenJTI(second.accessToken);
  ZuCheck(firstJTI && secondJTI && firstJTI != secondJTI);
  ZuCheck(jwtValidate("secret", first.accessToken, TokenType::access,
    1700000000, subject) && subject == "user");
  ZuCheck(jwtValidate("secret", first.refreshToken, TokenType::refresh,
    1700000001, subject));
  ZuCheck(!jwtValidate("secret", first.refreshToken, TokenType::access,
    1700000001, subject));
  ZuCheck(!jwtValidate("secret", first.accessToken, TokenType::access,
    1700000001, subject));

  TokenString corrupt = first.refreshToken;
  corrupt[corrupt.length() - 1] ^= 1;
  ZuCheck(!jwtValidate("secret", corrupt, TokenType::refresh,
    1700000001, subject));
  ZuCheck(!jwtValidate("secret", "a.b", TokenType::access,
    1700000000, subject));
  ZuCheck(!jwtValidate("secret", "a.b.c.d", TokenType::access,
    1700000000, subject));
  ZuCheck(!jwtValidate("secret", "=.e30.AA", TokenType::access,
    1700000000, subject));
  TokenString nonCanonical;
  nonCanonical << vector;
  ZuCheck(nonCanonical[nonCanonical.length() - 1] == 'k');
  nonCanonical[nonCanonical.length() - 1] = 'l';
  ZuCheck(!jwtValidate("matrix-secret", nonCanonical, TokenType::access,
    1700000001, subject));

  ZuCSpan validClaims =
    "{\"sub\":\"user\",\"iat\":1700000000,\"nbf\":1700000000,"
    "\"exp\":1700000060,\"jti\":"
    "\"00000000000000000000000000000001\",\"token_type\":\"access\"}";
  auto wrongAlg = testToken(
    "{\"alg\":\"HS384\",\"typ\":\"JWT\"}", validClaims, "secret");
  auto wrongTyp = testToken(
    "{\"alg\":\"HS256\",\"typ\":\"JWS\"}", validClaims, "secret");
  auto trailing = testToken(
    "{\"alg\":\"HS256\",\"typ\":\"JWT\"}",
    "{\"sub\":\"user\"}x", "secret");
  auto futureIAT = testToken(
    "{\"alg\":\"HS256\",\"typ\":\"JWT\"}",
    "{\"sub\":\"user\",\"iat\":1700000002,\"nbf\":1700000000,"
    "\"exp\":1700000060,\"jti\":"
    "\"00000000000000000000000000000001\",\"token_type\":\"access\"}",
    "secret");
  auto futureNBF = testToken(
    "{\"alg\":\"HS256\",\"typ\":\"JWT\"}",
    "{\"sub\":\"user\",\"iat\":1700000000,\"nbf\":1700000002,"
    "\"exp\":1700000060,\"jti\":"
    "\"00000000000000000000000000000001\",\"token_type\":\"access\"}",
    "secret");
  auto missingJTI = testToken(
    "{\"alg\":\"HS256\",\"typ\":\"JWT\"}",
    "{\"sub\":\"user\",\"iat\":1700000000,\"nbf\":1700000000,"
    "\"exp\":1700000060,\"token_type\":\"access\"}", "secret");
  auto zeroJTI = testToken(
    "{\"alg\":\"HS256\",\"typ\":\"JWT\"}",
    "{\"sub\":\"user\",\"iat\":1700000000,\"nbf\":1700000000,"
    "\"exp\":1700000060,\"jti\":"
    "\"00000000000000000000000000000000\",\"token_type\":\"access\"}",
    "secret");
  ZuCheck(!jwtValidate("secret", wrongAlg, TokenType::access,
    1700000001, subject));
  ZuCheck(!jwtValidate("secret", wrongTyp, TokenType::access,
    1700000001, subject));
  ZuCheck(!jwtValidate("secret", trailing, TokenType::access,
    1700000001, subject));
  ZuCheck(!jwtValidate("secret", futureIAT, TokenType::access,
    1700000001, subject));
  ZuCheck(!jwtValidate("secret", futureNBF, TokenType::access,
    1700000001, subject));
  ZuCheck(!jwtValidate("secret", missingJTI, TokenType::access,
    1700000001, subject));
  ZuCheck(!jwtValidate("secret", zeroJTI, TokenType::access,
    1700000001, subject));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(durationTest);
  ZuTestCall(mappingTest);
  ZuTestCall(jwtTest);
  return 0;
}
