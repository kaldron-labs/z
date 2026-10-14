//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuMatcher.hh>

#include <string.h>

#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZfCf.hh>

#include "../../zdb/test/ZdbTestCf.hh"
#include <zlib/ZiLog.hh>

#include <zlib/ZvMxParams.hh>

#include <zlib/ZdbMemStore.hh>

#include <zlib/zumd_admin.hh>
#include <zlib/zumd_authorize.hh>
#include <zlib/zumd_db.hh>
#include <zlib/zumd_db_ops.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_saga_code_family.hh>
#include <zlib/zumd_discovery.hh>
#include <zlib/zumd_http.hh>
#include <zlib/zumd_jwt.hh>
#include <zlib/zumd_oauth.hh>
#include <zlib/zumd_oidc.hh>
#include <zlib/zumd_passkey.hh>
#include <zlib/zumd_request.hh>
#include <zlib/zumd_session.hh>
#include <zlib/zumd_token.hh>
#include <zlib/zumd_revoke.hh>
#include <zlib/zumd_webauthn.hh>

#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsCOSE.hh>
#include <zlib/ZtlsPK.hh>
#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsSec.hh>

#include "ZumTestSupport.hh"

using namespace ZuTestUtil;
using namespace zumtest;


static void oauthForms()
{
  ZuTestScope(oauthForms);
  char authorize[] =
    "response_type=code&client_id=browser&redirect_uri=https%3A%2F%2Fapp%2Fcb"
    "&scope=read&state=&code_challenge="
    "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"
    "&code_challenge_method=S256&login_hint=admin%40example.com"
    "&resource=https%3A%2F%2Fapi.example&prompt=login&max_age=0";
  Zum::AuthorizeParams params;
  ZuCheck(Zum::parseAuthorize(authorize, params));
  ZuCheck(params.responseType == "code");
  ZuCheck(params.redirectURI == "https://app/cb");
  ZuCheck(params.has(Zum::AuthorizeParams::State) && !params.state);
  ZuCheck(params.loginHint == "admin@example.com");
  ZuCheck(params.resource == "https://api.example" &&
    params.prompt == "login" && params.maxAge == "0");
  ZuCheck(Zum::validateAuthorize(params) == Zum::ProfileError::OK);
  params.maxAge = "60";
  ZuCheck(Zum::validateAuthorize(params) == Zum::ProfileError::OK);
  params.maxAge = "60junk";
  ZuCheck(Zum::validateAuthorize(params) == Zum::ProfileError::Unsupported);
  params.maxAge = "18446744073709551615";
  ZuCheck(Zum::validateAuthorize(params) == Zum::ProfileError::Unsupported);
  params.maxAge = "";
  ZuCheck(Zum::validateAuthorize(params) == Zum::ProfileError::Empty);
  char invalidPrompt[] =
    "response_type=code&client_id=browser&redirect_uri=https%3A%2F%2Fapp%2Fcb"
    "&scope=read&code_challenge=x&code_challenge_method=S256&"
    "prompt=none%20login";
  ZuCheck(Zum::parseAuthorize(invalidPrompt, params));
  ZuCheck(Zum::validateAuthorize(params) == Zum::ProfileError::Unsupported);
  char consentPrompt[] =
    "response_type=code&client_id=browser&redirect_uri=https%3A%2F%2Fapp%2Fcb"
    "&scope=openid%20offline_access&code_challenge=x&"
    "code_challenge_method=S256&prompt=consent";
  ZuCheck(Zum::parseAuthorize(consentPrompt, params));
  ZuCheck(Zum::validateAuthorize(params) == Zum::ProfileError::OK &&
    params.prompt == "consent");
  char combinedPrompt[] =
    "response_type=code&client_id=browser&redirect_uri=https%3A%2F%2Fapp%2Fcb"
    "&scope=openid&code_challenge=x&code_challenge_method=S256&"
    "prompt=none%20consent";
  ZuCheck(Zum::parseAuthorize(combinedPrompt, params));
  ZuCheck(Zum::validateAuthorize(params) == Zum::ProfileError::Unsupported);

  char duplicate[] = "client_id=a&client_id=b";
  ZuCheck(!Zum::parseAuthorize(duplicate, params));
  char unknown[] = "username=user";
  ZuCheck(Zum::parseAuthorize(unknown, params));
  ZuCheck(!params.seen);
  char badFormEscape[] = "client_id=%GG";
  ZuCheck(!Zum::parseAuthorize(badFormEscape, params));

  char token[] =
    "grant_type=refresh_token&client_id=browser&refresh_token=opaque&scope=";
  Zum::TokenParams tokenParams;
  ZuCheck(Zum::parseToken(token, tokenParams));
  ZuCheck(tokenParams.grantType == "refresh_token");
  ZuCheck(tokenParams.has(Zum::TokenParams::Scope) && !tokenParams.scope);
  int grant;
  ZuCheck(Zum::validateToken(tokenParams, grant) == Zum::ProfileError::OK &&
    grant == Zum::TokenGrant::RefreshToken);

  char confidential[] =
    "grant_type=authorization_code&code=opaque&redirect_uri=https%3A%2F%2Fapp"
    "&code_verifier=dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
  ZuCheck(Zum::parseToken(confidential, tokenParams));
  ZuCheck(Zum::validateToken(tokenParams, grant) == Zum::ProfileError::OK &&
    grant == Zum::TokenGrant::AuthorizationCode &&
    !tokenParams.has(Zum::TokenParams::ClientID));

  char workload[] = "grant_type=client_credentials&code=bad";
  ZuCheck(Zum::parseToken(workload, tokenParams));
  ZuCheck(Zum::validateToken(tokenParams, grant) == Zum::ProfileError::OK);

  char revoke[] = "token=opaque&token_type_hint=refresh_token";
  Zum::RevokeParams revokeParams;
  ZuCheck(Zum::parseRevoke(revoke, revokeParams));
  ZuCheck(Zum::validateRevoke(revokeParams) == Zum::ProfileError::OK &&
    revokeParams.token == "opaque" &&
    revokeParams.tokenTypeHint == "refresh_token");
  char badRevoke[] = "token=opaque&token=again";
  ZuCheck(!Zum::parseRevoke(badRevoke, revokeParams));

  char basic[] = "bAsIc Y2xpZW50OnNlY3JldA==";
  Zum::BasicAuth auth;
  ZuCheck(Zum::parseBasic(basic, auth));
  ZuCheck(auth.clientID == "client" && auth.secret == "secret");
  char escapedBasic[] =
    "Basic Y2xpZW50JTNBaWQrJTJCOnNlY3JldCUzQSUyNiUzRCUyNSUyQitlbmQ=";
  ZuCheck(Zum::parseBasic(escapedBasic, auth));
  ZuCheck(auth.clientID == "client:id +" && auth.secret == "secret:&=%+ end");
  char badEscape[] = "Basic Y2xpZW50OmJhZCUy";
  ZuCheck(!Zum::parseBasic(badEscape, auth));
  char badBasic[] = "Basic Y2xpZW50OnNlY3JldA=!";
  ZuCheck(!Zum::parseBasic(badBasic, auth));

  Zum::TokenResponse response{
    .accessToken = "jwt", .idToken = "id.jwt", .refreshToken = "opaque",
    .scope = "read write",
    .expiresIn = 300};
  ZuCheck(Zum::tokenResponseJSON(response) ==
    "{\"access_token\":\"jwt\",\"token_type\":\"Bearer\","
    "\"expires_in\":300,\"scope\":\"read write\","
    "\"id_token\":\"id.jwt\","
    "\"refresh_token\":\"opaque\"}");
  ZuCheck(Zum::oauthErrorJSON(Zum::OAuthError::InvalidGrant) ==
    "{\"error\":\"invalid_grant\"}");
  ZuCheck(Zum::codeRedirect(
    "https://app/cb?fixed=1", "a.b", "return here", true) ==
    "https://app/cb?fixed=1&code=a.b&state=return%20here");
  ZuCheck(Zum::codeRedirect(
    "https://app/cb", "code+/%", "state+ &=%", true) ==
    "https://app/cb?code=code%2B%2F%25&state=state%2B%20%26%3D%25");
  ZuCheck(Zum::errorRedirect(
    "https://app/cb", Zum::OAuthError::InvalidScope, {}, false) ==
    "https://app/cb?error=invalid_scope");
  ZuCheck(Zum::errorRedirect(
    "https://app/cb", Zum::OAuthError::InvalidScope, "state+ &=%", true) ==
    "https://app/cb?error=invalid_scope&state=state%2B%20%26%3D%25");

  Ztls::Random rng;
  ZuCheck(rng.init());
  Zum::Client client;
  client.id = "client";
  client.profile = Zum::ClientProfile::Server;
  client.state = Zum::State::Active;
  client.grants = Zum::ClientGrant::ClientCredentials();
  client.secretDigest.length(Ztls::SecretHash::Size, false);
  ZuCheck(Ztls::secretHash(rng, ZuBSpan{"secret"}, client.secretDigest));
  tokenParams = {};
  tokenParams.grantType = "client_credentials";
  tokenParams.seen = 1U<<Zum::TokenParams::GrantType;
  ZuCheck(Zum::authenticateClient(client,
    Zum::TokenGrant::ClientCredentials, tokenParams, &auth) ==
    Zum::ClientAuth::InvalidClient);
  char basicAgain[] = "Basic Y2xpZW50OnNlY3JldA==";
  ZuCheck(Zum::parseBasic(basicAgain, auth));
  ZuCheck(Zum::authenticateClient(client,
    Zum::TokenGrant::ClientCredentials, tokenParams, &auth) ==
    Zum::ClientAuth::OK);
  client.previousSecretDigest = ZuMv(client.secretDigest);
  client.previousSecretExpires = 100;
  client.secretDigest.length(Ztls::SecretHash::Size, false);
  ZuCheck(Ztls::secretHash(rng, ZuBSpan{"replacement"},
    client.secretDigest));
  ZuCheck(Zum::authenticateClient(client,
    Zum::TokenGrant::ClientCredentials, tokenParams, &auth, 99) ==
    Zum::ClientAuth::OK);
  ZuCheck(Zum::authenticateClient(client,
    Zum::TokenGrant::ClientCredentials, tokenParams, &auth, 100) ==
    Zum::ClientAuth::InvalidClient);
  client.profile = Zum::ClientProfile::Native;
  ZuCheck(Zum::authenticateClient(client,
    Zum::TokenGrant::ClientCredentials, tokenParams, &auth) ==
    Zum::ClientAuth::UnauthorizedGrant);
  client.grants = Zum::ClientGrant::AuthCode();
  ZuCheck(Zum::authenticateClient(client,
    Zum::TokenGrant::AuthorizationCode, tokenParams, &auth) ==
    Zum::ClientAuth::InvalidClient);
  tokenParams.clientID = client.id;
  ZuCheck(Zum::authenticateClient(client,
    Zum::TokenGrant::AuthorizationCode, tokenParams, nullptr) ==
    Zum::ClientAuth::OK);
}

static void oidcRoles()
{
  ZuTestScope(oidcRoles);
  Zum::OIDCConfig config;
  config.issuer = "https://example.okta.com/oauth2/default";
  config.authorizeEndpoint = "https://example.okta.com/oauth2/v1/authorize";
  config.tokenEndpoint = "https://example.okta.com/oauth2/v1/token";
  config.jwksEndpoint = "https://example.okta.com/oauth2/v1/keys";
  config.clientID = "misp";
  config.clientSecret = "secret";
  config.redirectURI = "https://misp.example/oidc/callback";
  config.oidcScopes.push("openid");
  ZuCheck(Zum::oidcConfigValid(config));

  config.roles = Zum::OIDCRoles::Mapped;
  config.roleClaim = "groups";
  config.roleMap.push(Zum::OIDCRoleMap{"MISP-Admins", 1});
  config.roleMap.push(Zum::OIDCRoleMap{"MISP-Users", 3});
  config.roleMap.push(Zum::OIDCRoleMap{
    "Application - MISP Threat Intelligence - Users", 3});
  ZuCheck(Zum::oidcConfigValid(config));

  Zum::StringVec values;
  values.push("unknown");
  values.push("MISP-Users");
  values.push("MISP-Users");
  values.push("Application - MISP Threat Intelligence - Users");
  values.push("MISP-Admins");
  Zum::IDVec roles = Zum::oidcMapRoles(values, config.roleMap);
  ZuCheck(roles.length() == 2 && roles[0] == 3 && roles[1] == 1);

  config.roleMap.push(Zum::OIDCRoleMap{"MISP-Admins", 2});
  ZuCheck(Zum::oidcConfigValid(config));
  config.roleMap.length(config.roleMap.length() - 1);

  Ztls::Random rng;
  ZuCheck(rng.init());
  Ztls::PK::SK_EC key{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  uint8_t publicKey[Ztls::COSE::ES256::PublicKeySize];
  ZuCheck(Ztls::Backend::pkey_ec_export_public(key.key, publicKey));
  Zum::String token;
  ZuCheck(signJWT(rng, key, "{\"alg\":\"ES256\",\"kid\":\"upstream\","
    "\"typ\":\"JWT\"}",
    "{\"iss\":\"https://example.okta.com/oauth2/default\","
    "\"sub\":\"00u123\",\"aud\":\"misp\",\"nonce\":\"n-1\","
    "\"iat\":100,\"auth_time\":80,\"exp\":200,\"groups\":[\"MISP-Users\"]}", token));
  Zum::OIDCClaims claims;
  ZuCheck(Zum::oidcVerifyIDToken(token, publicKey, "n-1", config,
    120, Zum::OIDCLimits{}, claims));
  ZuCheck(claims.subject == "00u123" &&
    claims.authTime == 80 && claims.iat == 100 &&
    claims.roleValues.length() == 1 && claims.roleValues[0] == "MISP-Users");
  config.maxAgePresent = true;
  config.maxAge = 40;
  ZuCheck(Zum::oidcVerifyIDToken(token, publicKey, "n-1", config,
    120, Zum::OIDCLimits{.clockSkew = 0}, claims));
  config.maxAge = 39;
  ZuCheck(!Zum::oidcVerifyIDToken(token, publicKey, "n-1", config,
    120, Zum::OIDCLimits{.clockSkew = 0}, claims));
  config.maxAgePresent = false;
  ZuCheck(!Zum::oidcVerifyIDToken(token, publicKey, "wrong", config,
    120, Zum::OIDCLimits{}, claims));
  ZuCheck(!Zum::oidcVerifyIDToken(token, publicKey, "n-1", config,
    200, Zum::OIDCLimits{.clockSkew = 0}, claims));

  Zum::String multiAudience;
  ZuCheck(signJWT(rng, key, "{\"alg\":\"ES256\",\"kid\":\"upstream\"}",
    "{\"iss\":\"https://example.okta.com/oauth2/default\","
    "\"sub\":\"00u123\",\"aud\":[\"misp\",\"api\"],\"nonce\":\"n-1\","
    "\"iat\":100,\"auth_time\":80,\"exp\":200,\"groups\":[\"MISP-Users\"]}",
    multiAudience));
  ZuCheck(!Zum::oidcVerifyIDToken(multiAudience, publicKey, "n-1", config,
    120, Zum::OIDCLimits{}, claims));
}
static void redirects()
{
  ZuTestScope(redirects);
  ZuCheck(Zum::redirectMatches(Zum::ClientProfile::Browser,
    ZuBSpan{"https://app/cb"}, ZuBSpan{"https://app/cb"}));
  ZuCheck(!Zum::redirectMatches(Zum::ClientProfile::Browser,
    ZuBSpan{"https://app/cb"}, ZuBSpan{"https://app:443/cb"}));
  ZuCheck(Zum::redirectMatches(Zum::ClientProfile::Native,
    ZuBSpan{"http://127.0.0.1/cb?x=1"},
    ZuBSpan{"http://127.0.0.1:49152/cb?x=1"}));
  ZuCheck(Zum::redirectMatches(Zum::ClientProfile::Native,
    ZuBSpan{"http://[::1]/cb"}, ZuBSpan{"http://[::1]:49152/cb"}));
  ZuCheck(!Zum::redirectMatches(Zum::ClientProfile::Native,
    ZuBSpan{"http://127.0.0.1/cb"},
    ZuBSpan{"http://127.0.0.1:49152/other"}));
  ZuCheck(!Zum::redirectMatches(Zum::ClientProfile::Native,
    ZuBSpan{"http://127.0.0.1/cb"},
    ZuBSpan{"http://192.0.2.1:49152/cb"}));

  Zum::Client client;
  client.id = "native";
  client.profile = Zum::ClientProfile::Native;
  client.grants = Zum::ClientGrant::AuthCode();
  client.state = Zum::State::Active;
  client.redirects.push("http://127.0.0.1/cb");
  Zum::AuthorizeParams params;
  params.clientID = "native";
  params.redirectURI = "http://127.0.0.1:49152/cb";
  ZuCheck(Zum::authorizeClient(client, params));
  params.redirectURI = "http://127.0.0.1:49152/other";
  ZuCheck(!Zum::authorizeClient(client, params));
  client.state = Zum::State::Disabled;
  params.redirectURI = "http://127.0.0.1:49152/cb";
  ZuCheck(!Zum::authorizeClient(client, params));
  client.state = Zum::State::Active;
  client.profile = Zum::ClientProfile::Server;
  client.redirects.clear();
  client.redirects.push("https://app.example/cb");
  params.redirectURI = "https://app.example/cb";
  ZuCheck(Zum::authorizeClient(client, params));
  params.redirectURI = "https://app.example:443/cb";
  ZuCheck(!Zum::authorizeClient(client, params));
  params.redirectURI = "https://app.example/cb";
  client.grants = Zum::ClientGrant::ClientCredentials();
  ZuCheck(!Zum::authorizeClient(client, params));
}
static void pkce()
{
  ZuTestScope(pkce);
  ZuCheck(Zum::pkceVerify(
    "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
    "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"));
  ZuCheck(!Zum::pkceVerify(
    "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
    "dBjftJeZ4CVP-mB92K27uhbUJU1p1r/wW1gFWFOEjXk"));
}
static void opaque()
{
  ZuTestScope(opaque);
  Ztls::Random rng;
  ZuCheck(rng.init());

  Zum::AuthorizeParams params;
  params.clientID = "browser";
  params.redirectURI = "https://app/cb";
  params.codeChallenge =
    "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM";
  params.state = {};
  params.nonce = "oidc-nonce";
  params.prompt = "none";
  params.maxAge = "0";
  params.seen = (1U<<Zum::AuthorizeParams::State) |
    (1U<<Zum::AuthorizeParams::Nonce) |
    (1U<<Zum::AuthorizeParams::Prompt) |
    (1U<<Zum::AuthorizeParams::MaxAge);
  Zum::ScopeSelection selection;
  Zum::App app{.id = 27, .authVersion = 9, .audience = "orders"};
  selection.scope = "openid read";
  Zum::Grant ceremony;
  ZuCheck(Zum::authorizationBegin(rng, ceremony, "https://issuer", app, params,
    selection, true, ZuBSpan{"browser binding"}, 100, 160));
  ZuCheck(ceremony.id.length() == 16 && ceremony.challenge.length() == 32 &&
    ceremony.issuer == "https://issuer" && ceremony.appID == 27 &&
    ceremony.clientID == "browser" &&
    ceremony.audience == "orders" && ceremony.redirectURI ==
      "https://app/cb" && ceremony.bindingDigest ==
      ZuBSpan{"browser binding"} && ceremony.pkceChallenge ==
      ZuBSpan{"E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"} &&
    !ceremony.oauthState && ceremony.oauthStatePresent &&
    ceremony.scope == "openid read" && ceremony.nonce == "oidc-nonce" &&
    ceremony.prompt == "none" && ceremony.promptPresent &&
    !ceremony.maxAge && ceremony.maxAgePresent &&
    ceremony.authVersion == 9 && ceremony.created == 100 &&
    ceremony.expires == 160 && ceremony.kind == Zum::GrantKind::Ceremony &&
    ceremony.purpose == Zum::GrantPurpose::Authorization &&
    ceremony.state == Zum::State::Active);

  ZtBitmap ceremonyActions{8U};
  ceremonyActions.set(3);
  Zum::IDVec ceremonyRoles;
  ceremonyRoles.push(1);
  Zum::String ceremonyCode;
  ZuCheck(!Zum::authorizationFinish(rng, ceremony,
    ZuBSpan{"wrong binding"}, 42, Zum::Bytes{ZuBSpan{"credential"}},
    ceremonyRoles, ceremonyActions, 9, 1, 120, 180, ceremonyCode));
  ZuCheck(ceremony.kind == Zum::GrantKind::Ceremony && !ceremonyCode);
  Zum::Grant consentCeremony{ceremony};
  consentCeremony.state = Zum::State::Pending;
  Zum::String consentCode;
  ZuCheck(Zum::authorizationFinish(rng, consentCeremony,
    ZuBSpan{"browser binding"}, 42, Zum::Bytes{ZuBSpan{"credential"}},
    Zum::IDVec{ceremonyRoles}, ZtBitmap{ceremonyActions},
    9, 1, 120, 180, consentCode));
  ZuCheck(consentCeremony.kind == Zum::GrantKind::Code &&
    consentCeremony.state == Zum::State::Active && consentCode);
  ZuCheck(Zum::authorizationFinish(rng, ceremony,
    ZuBSpan{"browser binding"}, 42, Zum::Bytes{ZuBSpan{"credential"}},
    ZuMv(ceremonyRoles), ZuMv(ceremonyActions),
    9, 1, 120, 180, ceremonyCode));
  ZuCheck(ceremony.kind == Zum::GrantKind::Code &&
    ceremony.userID == 42 && ceremony.credentialID ==
      ZuBSpan{"credential"} && ceremony.actions[3] &&
    ceremony.authVersion == 9 && ceremony.authTime == 120 &&
    ceremony.expires == 180 && ceremony.digest && ceremonyCode &&
    ceremony.oauthStatePresent);
  ZuCheck(Zum::codeRedirect(ceremony, ceremonyCode).find<"&state=">() >= 0);
  Zum::String replayCode;
  ZuCheck(!Zum::authorizationFinish(rng, ceremony,
    ZuBSpan{"browser binding"}, 42, Zum::Bytes{ZuBSpan{"credential"}},
    Zum::IDVec{}, ZtBitmap{8U}, 9, 1, 121, 181, replayCode) && !replayCode);

  Zum::OpaqueToken token;
  ZuCheck(Zum::opaqueIssue(rng, token));
  Zum::Bytes id, digest;
  ZuCheck(Zum::opaqueParse(token.token, id, digest));
  ZuCheck(id == token.id && Ztls::ctEqual(token.digest, digest));
  token.token[token.token.length() - 1] ^= 1;
  ZuCheck(!Ztls::ctEqual(token.digest, Zum::opaqueDigest(token.token)));

  Zum::Bytes codeID, codeDigest;
  ZuCheck(Zum::opaqueParse(ceremonyCode, codeID, codeDigest));
  ZuCheck(codeID == ceremony.id);
  ZuCheck(Zum::codeMatches(ceremony, codeDigest,
    "browser", "https://app/cb",
    "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk", 150));
  ZuCheck(!Zum::codeMatches(ceremony, codeDigest,
    "browser", "https://app/cb",
    "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk", 180));

  Zum::IDVec narrowedScopes;
  narrowedScopes.push(7);
  ZtBitmap narrowedActions{8U};
  narrowedActions.set(3);
  Zum::CodeFamilyArgs family;
  Zum::String refreshToken;
  ZuCheck(Zum::codeFamilyPrepare(rng, ceremony, codeDigest,
    "openid read", ZuMv(narrowedScopes), ZuMv(narrowedActions), 11, 150, 1000,
    family, refreshToken));
  ZuCheck(family.codeID == ceremony.id && family.codeDigest == codeDigest &&
    family.issuer == ceremony.issuer && family.appID == ceremony.appID &&
    family.userID == ceremony.userID &&
    family.clientID == ceremony.clientID &&
    family.credentialID == ceremony.credentialID &&
    family.audience == ceremony.audience && family.scope == "openid read" &&
    family.requestedRoleIDs.length() == 1 &&
    family.requestedRoleIDs[0] == 7 && family.actions[3] &&
    family.authVersion == 11 && family.authTime == ceremony.authTime &&
    family.created == 150 && family.expires == 1000 && refreshToken);
  Zum::Bytes refreshID, refreshDigest;
  ZuCheck(Zum::opaqueParse(refreshToken, refreshID, refreshDigest) &&
    refreshID == family.familyID && refreshDigest == family.digest);

  ceremonyCode[ceremonyCode.length() - 1] ^= 1;
  ZuCheck(!Zum::codeMatches(ceremony, Zum::opaqueDigest(ceremonyCode),
    "browser", "https://app/cb",
    "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk", 120));
}
static void refresh()
{
  ZuTestScope(refresh);
  Zum::Refresh grant;
  grant.digest = Zum::Bytes{ZuBSpan{"current"}};
  grant.spent.push(Zum::Bytes{ZuBSpan{"spent-0"}});
  grant.spent.push(Zum::Bytes{ZuBSpan{"spent-1"}});

  ZuCheck(Zum::refreshMatch(grant, ZuBSpan{"current"}) ==
    Zum::RefreshMatch::Current);
  ZuCheck(Zum::refreshMatch(grant, ZuBSpan{"spent-1"}) ==
    Zum::RefreshMatch::Spent);
  ZuCheck(Zum::refreshMatch(grant, ZuBSpan{"unknown"}) ==
    Zum::RefreshMatch::Unknown);

  grant.state = Zum::State::Active;
  grant.expires = 1000;
  ZuCheck(Zum::refreshRotate(grant, ZuBSpan{"unknown"},
    Zum::Bytes{ZuBSpan{"next"}}, 100, 8, 8) == Zum::RefreshRotate::Unknown);
  ZuCheck(Zum::refreshRotate(grant, ZuBSpan{"current"},
    Zum::Bytes{ZuBSpan{"next"}}, 100, 8, 8) == Zum::RefreshRotate::Rotated);
  ZuCheck(grant.digest == ZuBSpan{"next"} && grant.generation == 1 &&
    grant.spent.length() == 3 && grant.spent[2] == ZuBSpan{"current"});
  ZuCheck(Zum::refreshRotate(grant, ZuBSpan{"next"},
    Zum::Bytes{ZuBSpan{"unused"}}, 100, 1, 8) ==
      Zum::RefreshRotate::Exhausted);
  ZuCheck(grant.digest == ZuBSpan{"next"} && grant.generation == 1 &&
    grant.state == Zum::State::Active);
  ZuCheck(Zum::refreshRotate(grant, ZuBSpan{"current"},
    Zum::Bytes{ZuBSpan{"unused"}}, 100, 8, 8) == Zum::RefreshRotate::Reused);
  ZuCheck(grant.state == Zum::State::Revoked);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(oauthForms);
  ZuTestCall(oidcRoles);
  ZuTestCall(redirects);
  ZuTestCall(pkce);
  ZuTestCall(opaque);
  ZuTestCall(refresh);
  return 0;
}
