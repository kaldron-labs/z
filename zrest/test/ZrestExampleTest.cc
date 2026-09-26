//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZfJSON.hh>
#include <zlib/ZfURI.hh>

#include <zlib/ZtlsMD.hh>

#include "zrest.hh"

using namespace ZuTestUtil;

template <typename T>
static void loadURI(T &value, ZuCSpan uri)
{
  ZuTestScope(loadURI);

  ZtString<> data{uri};
  auto scan = ZfURI::scan(data.span());
  auto handler = ZfURI::handler<T>(scan.template p<1>());
  ZuCHECK(handler.valid);
  handler.load(value);
}

template <typename T>
static void loadJSON(T &value, ZuCSpan json)
{
  ZuTestScope(loadJSON);

  ZtString<> data{json};
  auto scan = ZfJSON::scan(data.span());
  auto handler = ZfJSON::handler<T>((*scan.template p<1>())[0]);
  ZuCHECK(handler.valid);
  handler.load(value);
}

static void pathTest()
{
  ZuTestScope(paths);

  OAuthIssuerPath issuer{.app = "ping"};
  OAuthEndpointPath endpoint{.app = "ping", .endpoint = "token"};
  OAuthMetadataPath metadata{.app = "ping"};
  ZtString<> uri;
  ZfURI::save(uri, issuer);
  ZuCheck(uri == "/oauth2/ping");
  uri.null();
  ZfURI::save(uri, endpoint);
  ZuCheck(uri == "/oauth2/ping/v1/token");
  uri.null();
  ZfURI::save(uri, metadata);
  ZuCheck(uri ==
    "/.well-known/oauth-authorization-server/oauth2/ping");

  OAuthEndpointPath loaded;
  ZuTestCall(loadURI, loaded, "/oauth2/ping/v1/authorize");
  ZuCheck(loaded.oauth2 == "oauth2" && loaded.app == "ping" &&
    loaded.version == "v1" && loaded.endpoint == "authorize");
}

static void authorizeTest()
{
  ZuTestScope(authorize);

  OAuthAuthorizeReq authorize;
  authorize.responseType = "code";
  authorize.clientID = "zrest-native";
  authorize.redirectURI = "http://127.0.0.1:49152/callback";
  authorize.scope = "ping";
  authorize.state = "state";
  authorize.codeChallenge = "challenge";
  authorize.codeChallengeMethod = "S256";
  ZtString<> uri;
  ZfURI::save(uri, authorize);
  OAuthAuthorizeReq loaded;
  ZuTestCall(loadURI, loaded, uri);
  ZuCheck(loaded.responseType == "code" &&
    loaded.clientID == "zrest-native" && loaded.scope == "ping" &&
    loaded.state == "state" && loaded.codeChallenge == "challenge" &&
    loaded.codeChallengeMethod == "S256" &&
    loaded.redirectURI == "http://127.0.0.1:49152/callback");

  OAuthAuthorizeCodeRes response{
    .code = "code", .state = "state",
    .issuerURL = "https://localhost:8443/oauth2/ping"};
  uri.null();
  ZfURI::save(uri, response);
  OAuthAuthorizeCodeRes callback;
  ZuTestCall(loadURI, callback, uri);
  ZuCheck(callback.code == "code");
  ZuCheck(callback.state == "state");
  ZuCheck(callback.issuerURL == "https://localhost:8443/oauth2/ping");
}

static void formTest()
{
  ZuTestScope(forms);

  OAuthCodeTokenReq token;
  token.grantType = "authorization_code";
  token.code = "code";
  token.redirectURI = "http://127.0.0.1:49152/callback";
  token.clientID = "zrest-native";
  token.codeVerifier = "verifier";
  ZtString<> uri;
  ZfURI::save(uri, token);
  OAuthCodeTokenReq loaded;
  ZuTestCall(loadURI, loaded, uri);
  ZuCheck(loaded.grantType == "authorization_code");
  ZuCheck(loaded.code == "code");
  ZuCheck(loaded.clientID == "zrest-native");
  ZuCheck(loaded.codeVerifier == "verifier");

  OAuthRefreshTokenReq refresh;
  refresh.grantType = "refresh_token";
  refresh.refreshToken = "refresh";
  refresh.scope = "ping";
  refresh.clientID = "zrest-native";
  uri.null();
  ZfURI::save(uri, refresh);
  OAuthRefreshTokenReq loadedRefresh;
  ZuTestCall(loadURI, loadedRefresh, uri);
  ZuCheck(loadedRefresh.grantType == "refresh_token" &&
    loadedRefresh.clientID == "zrest-native" &&
    loadedRefresh.refreshToken == "refresh" &&
    loadedRefresh.scope == "ping");

  OAuthRevokeReq revoke;
  revoke.token = "refresh";
  revoke.tokenTypeHint = "refresh_token";
  revoke.clientID = "zrest-native";
  uri.null();
  ZfURI::save(uri, revoke);
  OAuthRevokeReq loadedRevoke;
  ZuTestCall(loadURI, loadedRevoke, uri);
  ZuCheck(loadedRevoke.token == "refresh" &&
    loadedRevoke.tokenTypeHint == "refresh_token" &&
    loadedRevoke.clientID == "zrest-native");
}

static void jsonTest()
{
  ZuTestScope(json);

  OAuthTokenRes tokens;
  tokens.accessToken = "access";
  tokens.tokenType = "Bearer";
  tokens.expiresIn = 300;
  tokens.refreshToken = "refresh";
  tokens.scope = "ping";
  ZtString<> json;
  ZfJSON::save(json, tokens);
  ZuCheck(json ==
    "{\"access_token\":\"access\",\"token_type\":\"Bearer\","
    "\"expires_in\":300,\"refresh_token\":\"refresh\","
    "\"scope\":\"ping\"}");
  OAuthTokenRes loaded;
  ZuTestCall(loadJSON, loaded, json);
  ZuCheck(loaded.accessToken == "access" &&
    loaded.tokenType == "Bearer" && loaded.expiresIn == 300 &&
    loaded.refreshToken == "refresh" && loaded.scope == "ping");

  OAuthError error;
  error.error = "invalid_grant";
  error.errorDescription = "authorization code is invalid";
  json.null();
  ZfJSON::save(json, error);
  ZuCheck(json ==
    "{\"error\":\"invalid_grant\",\"error_description\":"
    "\"authorization code is invalid\"}");

  OAuthAccessClaims claims{
    .issuerURL = "https://localhost:8443/oauth2/ping",
    .audience = "https://localhost:8443/api/ping",
    .subject = "test", .clientID = "zrest-native", .scope = "ping",
    .issuedAt = 1700000000, .notBefore = 1700000000,
    .expiry = 1700000300, .tokenID = "token-id"};
  json.null();
  ZfJSON::save(json, claims);
  OAuthAccessClaims loadedClaims;
  ZuTestCall(loadJSON, loadedClaims, json);
  ZuCheck(loadedClaims.issuerURL == claims.issuerURL &&
    loadedClaims.audience == claims.audience &&
    loadedClaims.subject == claims.subject &&
    loadedClaims.clientID == claims.clientID &&
    loadedClaims.scope == "ping" &&
    loadedClaims.issuedAt == 1700000000 &&
    loadedClaims.notBefore == 1700000000 &&
    loadedClaims.expiry == 1700000300 &&
    loadedClaims.tokenID == "token-id");
}

static void pkceTest()
{
  ZuTestScope(pkce);

  static constexpr auto verifier =
    "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"_Zu;
  static constexpr auto expected =
    "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"_Zu;
  ZuBArray<Ztls::MD<>::Size> digest(Ztls::MD<>::Size, false);
  Ztls::MD<> md;
  md.update(ZuBSpan{verifier});
  md.finish(digest);
  ZtString<> challenge;
  challenge.length(ZuBase64URL::enclen(digest.length()));
  challenge.length(ZuBase64URL::encode(challenge.span(), digest));
  ZuCheck(challenge == expected);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(pathTest);
  ZuTestCall(authorizeTest);
  ZuTestCall(formTest);
  ZuTestCall(jsonTest);
  ZuTestCall(pkceTest);
  return 0;
}
