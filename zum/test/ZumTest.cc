//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuBase64URL.hh>

#include <string.h>

#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZfCf.hh>

#include <zlib/ZvMxParams.hh>

#include <zlib/ZdbMemStore.hh>

#include <zlib/ZumAdmin.hh>
#include <zlib/ZumAuthorize.hh>
#include <zlib/ZumDB.hh>
#include <zlib/ZumDiscovery.hh>
#include <zlib/ZumHTTP.hh>
#include <zlib/ZumJWT.hh>
#include <zlib/ZumOAuth.hh>
#include <zlib/ZumOIDC.hh>
#include <zlib/ZumPasskey.hh>
#include <zlib/ZumRequest.hh>
#include <zlib/ZumToken.hh>
#include <zlib/ZumWebAuthn.hh>

#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsCOSE.hh>
#include <zlib/ZtlsPK.hh>
#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsSec.hh>

using namespace ZuTestUtil;

static_assert(Zdb_::SagaBasesValid_<Zum::DBContext, Zum::Sagas>{});

static void requests()
{
  ZuTestScope(requests);
  ZmSchedParams params;
  params.nThreads(1);
  params.thread(1).isolated(true);
  ZmScheduler scheduler{ZuMv(params)};
  ZuCheck(scheduler.start());

  ZmRef<Zum::Requests> requests = new Zum::Requests{};
  ZuCheck(requests->init(&scheduler, 1, 1));
  requests->activate();

  ZmRef<Zum::Request> pending;
  ZmSemaphore started;
  ZmSemaphore finished;
  ZmAtomic<unsigned> completed = 0;
  ZuCheck(requests->run(Zm::now() + ZuTime{60}, [
    &pending, &started
  ](ZmRef<Zum::Request> request) mutable {
    pending = ZuMv(request);
    started.post();
  }, [&completed]() { ++completed; }));
  started.wait();
  ZuCheck(requests->count() == 1);
  ZuCheck(!requests->run(Zm::now() + ZuTime{60}, [](
      ZmRef<Zum::Request>) { }, [&completed]() { ++completed; }));
  pending->complete([&completed, &finished]() {
    ++completed;
    finished.post();
  });
  finished.wait();
  ZuCheck(completed == 1);

  ZmSemaphore timedOut;
  ZuCheck(requests->run(Zm::now() + ZuTime{0.01}, [](
      ZmRef<Zum::Request>) { }, [&completed, &timedOut]() {
    ++completed;
    timedOut.post();
  }));
  timedOut.wait();
  ZuCheck(completed == 2 && !requests->count());

  ZmRef<Zum::Request> late;
  started.reset();
  ZuCheck(requests->run(Zm::now() + ZuTime{60}, [
    &late, &started
  ](ZmRef<Zum::Request> request) mutable {
    late = ZuMv(request);
    started.post();
  }, [&completed]() { ++completed; }));
  started.wait();
  ZmSemaphore deactivated;
  requests->deactivate([&deactivated]() { deactivated.post(); });
  deactivated.wait();
  ZuCheck(!requests->active() && !requests->count() && completed == 3);
  late->complete([&completed]() { ++completed; });
  ZmSemaphore drained;
  scheduler.run([&drained]() { drained.post(); }, 1);
  drained.wait();
  ZuCheck(completed == 3);
  ZuCheck(!requests->run(Zm::now() + ZuTime{60}, [](
      ZmRef<Zum::Request>) { }, [&completed]() { ++completed; }));

  late = {};
  pending = {};
  requests = {};
  ZuCheck(scheduler.stop());
}

static void oauthForms()
{
  ZuTestScope(oauthForms);
  char authorize[] =
    "response_type=code&client_id=browser&redirect_uri=https%3A%2F%2Fapp%2Fcb"
    "&scope=read&state=&code_challenge="
    "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"
    "&code_challenge_method=S256";
  Zum::AuthorizeParams params;
  Zum::parseAuthorize(authorize, params);
  ZuCheck(params.responseType == "code");
  ZuCheck(params.redirectURI == "https://app/cb");
  ZuCheck(params.has(Zum::AuthorizeParams::State) && !params.state);
  ZuCheck(Zum::validateAuthorize(params) == Zum::ProfileError::OK);

  char duplicate[] = "client_id=a&client_id=b";
  Zum::parseAuthorize(duplicate, params);
  ZuCheck(params.clientID == "b");
  char unknown[] = "username=user";
  Zum::parseAuthorize(unknown, params);
  ZuCheck(!params.seen);

  char token[] =
    "grant_type=refresh_token&client_id=browser&refresh_token=opaque&scope=";
  Zum::TokenParams tokenParams;
  Zum::parseToken(token, tokenParams);
  ZuCheck(tokenParams.grantType == "refresh_token");
  ZuCheck(tokenParams.has(Zum::TokenParams::Scope) && !tokenParams.scope);
  int grant;
  ZuCheck(Zum::validateToken(tokenParams, grant) == Zum::ProfileError::OK &&
    grant == Zum::TokenGrant::RefreshToken);

  char confidential[] =
    "grant_type=authorization_code&code=opaque&redirect_uri=https%3A%2F%2Fapp"
    "&code_verifier=dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
  Zum::parseToken(confidential, tokenParams);
  ZuCheck(Zum::validateToken(tokenParams, grant) == Zum::ProfileError::OK &&
    grant == Zum::TokenGrant::AuthorizationCode &&
    !tokenParams.has(Zum::TokenParams::ClientID));

  char workload[] = "grant_type=client_credentials&code=bad";
  Zum::parseToken(workload, tokenParams);
  ZuCheck(Zum::validateToken(tokenParams, grant) == Zum::ProfileError::OK);

  char revoke[] = "token=opaque&token_type_hint=refresh_token";
  Zum::RevokeParams revokeParams;
  Zum::parseRevoke(revoke, revokeParams);
  ZuCheck(Zum::validateRevoke(revokeParams) == Zum::ProfileError::OK &&
    revokeParams.token == "opaque" &&
    revokeParams.tokenTypeHint == "refresh_token");
  char badRevoke[] = "token=opaque&token=again";
  Zum::parseRevoke(badRevoke, revokeParams);
  ZuCheck(revokeParams.token == "again");

  char basic[] = "bAsIc Y2xpZW50OnNlY3JldA==";
  Zum::BasicAuth auth;
  ZuCheck(Zum::parseBasic(basic, auth));
  ZuCheck(auth.clientID == "client" && auth.secret == "secret");
  char badBasic[] = "Basic Y2xpZW50OnNlY3JldA=!";
  ZuCheck(!Zum::parseBasic(badBasic, auth));

  Zum::TokenResponse response{
    .accessToken = "jwt", .refreshToken = "opaque", .scope = "read write",
    .expiresIn = 300};
  ZuCheck(Zum::tokenResponseJSON(response) ==
    "{\"access_token\":\"jwt\",\"token_type\":\"Bearer\","
    "\"expires_in\":300,\"scope\":\"read write\","
    "\"refresh_token\":\"opaque\"}");
  ZuCheck(Zum::oauthErrorJSON(Zum::OAuthError::InvalidGrant) ==
    "{\"error\":\"invalid_grant\"}");
  ZuCheck(Zum::codeRedirect(
    "https://app/cb?fixed=1", "a.b", "return here", true) ==
    "https://app/cb?fixed=1&code=a.b&state=return%20here");
  ZuCheck(Zum::errorRedirect(
    "https://app/cb", Zum::OAuthError::InvalidScope, {}, false) ==
    "https://app/cb?error=invalid_scope");

  Ztls::Random rng;
  ZuCheck(rng.init());
  Zum::Client client;
  client.id = "client";
  client.type = Zum::ClientType::Confidential;
  client.state = Zum::State::Active;
  client.grants = Zum::ClientGrant::ClientCredentials;
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
}

struct HTTPTestApp {
  template <typename ...Args> void authorize(Args &&...) { }
  template <typename ...Args> void token(Args &&...) { }
  template <typename ...Args> void passkeyBegin(Args &&...) { }
  template <typename ...Args> void passkeyFinish(Args &&...) { }
  template <typename ...Args> void metadata(Args &&...) { }
  template <typename ...Args> void jwks(Args &&...) { }
  template <typename ...Args> void revoke(Args &&...) { }
  template <typename ...Args> void oidcCallback(Args &&...) { }
};

static void httpRoutes()
{
  ZuTestScope(httpRoutes);
  HTTPTestApp app;
  Zum::HTTPParser<HTTPTestApp> parser;
  parser.init(app);
  auto match = [&parser](Zhttp::Method::T method, ZuCSpan path) {
    Zum::String data{path};
    Zhttp::Target target;
    target.path = {
      reinterpret_cast<uint8_t *>(data.data()), data.length()};
    bool ok = parser.operation(method, target);
    parser.reset();
    return ok;
  };
  ZuCheck(match(Zhttp::Method::GET, "/authorize?client_id=x"));
  ZuCheck(!match(Zhttp::Method::GET, "/authorize/extra"));
  ZuCheck(match(Zhttp::Method::POST, "/token"));
  ZuCheck(!match(Zhttp::Method::GET, "/token"));
  ZuCheck(match(Zhttp::Method::POST, "/passkey/begin"));
  ZuCheck(match(Zhttp::Method::POST, "/passkey/finish"));
  ZuCheck(match(Zhttp::Method::GET,
    "/.well-known/oauth-authorization-server"));
  ZuCheck(match(Zhttp::Method::GET, "/jwks"));
  ZuCheck(match(Zhttp::Method::POST, "/revoke"));
  ZuCheck(!match(Zhttp::Method::GET, "/revoke"));
  ZuCheck(match(Zhttp::Method::GET, "/oidc/callback?code=x&state=y"));

  Zum::String query{"/authorize?client_id=x"};
  Zhttp::Target target;
  target.path = {
    reinterpret_cast<uint8_t *>(query.data()), query.length()};
  ZuCheck(parser.operation(Zhttp::Method::GET, target));
  ZuCheck(parser.u.cdispatch([](auto, const auto &request) {
    if constexpr (ZuIsSame<ZuDecay<decltype(request)>,
        Zum::AuthorizeReq<HTTPTestApp>>{})
      return request.object->data == ZuBSpan{"client_id=x"};
    else
      return false;
  }));
  parser.reset();

  ZmRef<Zum::HTTPResponse> response = new Zum::HTTPResponse{};
  response->body = "{}";
  Zum::HTTPBuilder<HTTPTestApp> builder;
  builder.template init<Zum::TokenOK, Zum::TokenReq<HTTPTestApp>>(
    response.ptr());
  ZuCheck(builder.status() == 200 &&
    builder.bodyPolicy() == Zhttp::BodyPolicy::Fixed);
}

static void redirects()
{
  ZuTestScope(redirects);
  ZuCheck(Zum::redirectMatches(Zum::ClientType::Browser,
    ZuBSpan{"https://app/cb"}, ZuBSpan{"https://app/cb"}));
  ZuCheck(!Zum::redirectMatches(Zum::ClientType::Browser,
    ZuBSpan{"https://app/cb"}, ZuBSpan{"https://app:443/cb"}));
  ZuCheck(Zum::redirectMatches(Zum::ClientType::Native,
    ZuBSpan{"http://127.0.0.1/cb?x=1"},
    ZuBSpan{"http://127.0.0.1:49152/cb?x=1"}));
  ZuCheck(Zum::redirectMatches(Zum::ClientType::Native,
    ZuBSpan{"http://[::1]/cb"}, ZuBSpan{"http://[::1]:49152/cb"}));
  ZuCheck(!Zum::redirectMatches(Zum::ClientType::Native,
    ZuBSpan{"http://127.0.0.1/cb"},
    ZuBSpan{"http://127.0.0.1:49152/other"}));
  ZuCheck(!Zum::redirectMatches(Zum::ClientType::Native,
    ZuBSpan{"http://127.0.0.1/cb"},
    ZuBSpan{"http://192.0.2.1:49152/cb"}));

  Zum::Client client;
  client.id = "native";
  client.type = Zum::ClientType::Native;
  client.grants = Zum::ClientGrant::AuthorizationCode;
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
}

static bool jwtPart(Zum::String &token, ZuCSpan value)
{
  auto offset = token.length();
  auto length = ZuBase64URL::enclen(value.length());
  token.length(offset + length);
  return ZuBase64URL::encode({
    reinterpret_cast<uint8_t *>(token.data() + offset), unsigned(length)},
    ZuBSpan{value}) == length;
}

static Zum::String base64URL(ZuBSpan value)
{
  Zum::String encoded;
  encoded.length(ZuBase64URL::enclen(value.length()));
  encoded.length(ZuBase64URL::encode({
    reinterpret_cast<uint8_t *>(encoded.data()), encoded.length()}, value));
  return encoded;
}

static bool oidcParams(
    Zum::String location, Zum::String &state, Zum::String &nonce)
{
  int offset = location.find<"?">();
  if (offset < 0) return false;
  location.splice(0, unsigned(offset + 1));
  unsigned seen = 0;
  Zum::formEach({location.data(), location.length()}, [
      &state, &nonce, &seen
    ](ZuCSpan name, ZuCSpan value) {
      unsigned bit;
      Zum::String *target;
      if (name == "state") { bit = 1U; target = &state; }
      else if (name == "nonce") { bit = 2U; target = &nonce; }
      else return;
      seen |= bit;
      *target = value;
    });
  return seen == 3U && state && nonce;
}

static bool signJWT(
    Ztls::Random &rng, Ztls::PK::SK_EC &key,
    ZuCSpan header, ZuCSpan claims, Zum::String &token)
{
  Zum::String next;
  if (!jwtPart(next, header)) return false;
  next << '.';
  if (!jwtPart(next, claims)) return false;
  uint8_t digest[Ztls::MD<>::Size];
  Ztls::MD<> md;
  md.update(ZuBSpan{next});
  md.finish(digest);
  Zum::Bytes der;
  auto result = key.sign(rng, digest, [&der](ZuBSpan value) {
    der = Zum::Bytes{value};
  });
  uint8_t raw[Ztls::COSE::ES256::SignatureSize];
  if (result.template is<ZeException>() ||
      !Ztls::COSE::ES256::derToRaw(der, raw)) return false;
  next << '.';
  if (!jwtPart(next, ZuCSpan{
      reinterpret_cast<const char *>(raw), unsigned(sizeof(raw))}))
    return false;
  token = ZuMv(next);
  return true;
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
  config.roleMap.push(Zum::RoleMap{"MISP-Admins", 1});
  config.roleMap.push(Zum::RoleMap{"MISP-Users", 3});
  config.roleMap.push(Zum::RoleMap{
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

  config.roleMap.push(Zum::RoleMap{"MISP-Admins", 2});
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
    "\"iat\":100,\"exp\":200,\"groups\":[\"MISP-Users\"]}", token));
  Zum::OIDCClaims claims;
  ZuCheck(Zum::oidcVerifyIDToken(token, publicKey, "n-1", config,
    120, Zum::OIDCLimits{}, claims));
  ZuCheck(claims.subject == "00u123" &&
    claims.roleValues.length() == 1 && claims.roleValues[0] == "MISP-Users");
  ZuCheck(!Zum::oidcVerifyIDToken(token, publicKey, "wrong", config,
    120, Zum::OIDCLimits{}, claims));
  ZuCheck(!Zum::oidcVerifyIDToken(token, publicKey, "n-1", config,
    200, Zum::OIDCLimits{.clockSkew = 0}, claims));

  Zum::String multiAudience;
  ZuCheck(signJWT(rng, key, "{\"alg\":\"ES256\",\"kid\":\"upstream\"}",
    "{\"iss\":\"https://example.okta.com/oauth2/default\","
    "\"sub\":\"00u123\",\"aud\":[\"misp\",\"api\"],\"nonce\":\"n-1\","
    "\"iat\":100,\"exp\":200,\"groups\":[\"MISP-Users\"]}",
    multiAudience));
  ZuCheck(!Zum::oidcVerifyIDToken(multiAudience, publicKey, "n-1", config,
    120, Zum::OIDCLimits{}, claims));
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
  params.seen = 1U<<Zum::AuthorizeParams::State;
  Zum::ScopeSelection selection;
  selection.audience = "orders";
  selection.scopeIDs.push(7);
  Zum::Grant ceremony;
  ZuCheck(Zum::authorizationBegin(rng, ceremony, "https://issuer", params,
    selection, true, ZuBSpan{"browser binding"}, 9, 100, 160));
  ZuCheck(ceremony.id.length() == 16 && ceremony.challenge.length() == 32 &&
    ceremony.issuer == "https://issuer" && ceremony.clientID == "browser" &&
    ceremony.audience == "orders" && ceremony.redirectURI ==
      "https://app/cb" && ceremony.scopeIDs.length() == 1 &&
    ceremony.scopeIDs[0] == 7 && ceremony.bindingDigest ==
      ZuBSpan{"browser binding"} && ceremony.pkceChallenge ==
      ZuBSpan{"E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"} &&
    !ceremony.oauthState && ceremony.oauthStatePresent &&
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
  Zum::CodeFamily family;
  Zum::String refreshToken;
  ZuCheck(Zum::codeFamilyPrepare(rng, ceremony, codeDigest,
    ZuMv(narrowedScopes), ZuMv(narrowedActions), 11, 150, 1000,
    family, refreshToken));
  ZuCheck(family.codeID == ceremony.id && family.codeDigest == codeDigest &&
    family.issuer == ceremony.issuer && family.userID == ceremony.userID &&
    family.clientID == ceremony.clientID &&
    family.credentialID == ceremony.credentialID &&
    family.audience == ceremony.audience && family.scopeIDs.length() == 1 &&
    family.scopeIDs[0] == 7 && family.actions[3] &&
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

static void actions()
{
  ZuTestScope(actions);
  ZtBitmap durable{128U};
  durable.set(1);
  durable.set(2);
  durable.set(65);
  ZtBitmap delegated{128U};
  delegated.set(2);
  delegated.set(65);
  delegated.set(66);

  auto effective = Zum::intersectActions(ZuMv(durable), delegated);
  ZuCheck(!effective[1]);
  ZuCheck(effective[2]);
  ZuCheck(effective[65]);
  ZuCheck(!effective[66]);

  ZtBitmap ceiling{128U};
  ceiling.set(2);
  auto narrowed = Zum::intersectActions(ZuMv(effective), ceiling);
  ZuCheck(narrowed[2]);
  ZuCheck(!narrowed[65]);

  Zum::IDVec userRoles;
  userRoles.push(1);
  userRoles.push(2);
  Zum::IDVec scopeRoles;
  scopeRoles.push(2);
  scopeRoles.push(3);
  Zum::Role roles[] = {
    {.id = 1, .name = "reader", .actions = ZtBitmap{8U}},
    {.id = 2, .name = "writer", .actions = ZtBitmap{8U}},
    {.id = 3, .name = "delegated", .actions = ZtBitmap{8U}}
  };
  roles[0].actions.set(1);
  roles[1].actions.set(2);
  roles[1].actions.set(3);
  roles[2].actions.set(3);
  Zum::Action actionRecords[] = {
    {.id = 1, .name = "read"},
    {.id = 2, .name = "write"},
    {.id = 3, .name = "disabled", .state = Zum::State::Disabled}
  };
  auto resolved = Zum::effectiveActions(8, userRoles, scopeRoles,
    roles, actionRecords);
  ZuCheck(!resolved[1] && resolved[2] && !resolved[3]);

  Zum::Client client;
  client.scopeIDs.push(10);
  client.scopeIDs.push(11);
  client.audiences.push("orders");
  client.audiences.push("billing");
  Zum::Scope scopes[] = {
    {.id = 10, .audience = "orders", .name = "read"},
    {.id = 11, .audience = "orders", .name = "write"},
    {.id = 12, .audience = "billing", .name = "charge"}
  };
  scopes[0].roleIDs.push(1);
  scopes[1].roleIDs.push(2);
  Zum::ScopeSelection selection;
  ZuCheck(Zum::selectScopes(client, "read write read", scopes, selection) ==
    Zum::ScopeError::OK);
  ZuCheck(selection.audience == "orders" && selection.scope == "read write" &&
    selection.scopeIDs.length() == 2 && selection.roleIDs.length() == 2);
  client.scopeIDs.push(12);
  ZuCheck(Zum::selectScopes(client, "read charge", scopes, selection) ==
    Zum::ScopeError::Audience);

  Zum::IDVec granted;
  granted.push(10);
  ZuCheck(Zum::selectGrantedScopes(
    client, granted, false, {}, scopes, selection) == Zum::ScopeError::OK);
  ZuCheck(selection.scope == "read" && selection.scopeIDs.length() == 1 &&
    selection.scopeIDs[0] == 10);
  ZuCheck(Zum::selectGrantedScopes(
    client, granted, true, "write", scopes, selection) ==
    Zum::ScopeError::Unavailable);

  client.id = "browser";
  client.type = Zum::ClientType::Browser;
  client.grants = Zum::ClientGrant::AuthorizationCode |
    Zum::ClientGrant::RefreshToken;
  client.state = Zum::State::Active;
  Zum::User user{
    .id = 42,
    .handle = Zum::Bytes{ZuBSpan{"handle"}},
    .roleIDs = userRoles,
    .state = Zum::State::Active
  };
  Zum::Cred cred{
    .id = Zum::Bytes{ZuBSpan{"credential"}},
    .userID = 42,
    .publicKey = Zum::Bytes{ZuBSpan{"public key"}},
    .state = Zum::State::Active
  };
  Zum::Grant grant;
  grant.userID = 42;
  grant.clientID = "browser";
  grant.credentialID = cred.id;
  grant.audience = "orders";
  grant.scopeIDs.push(10);
  grant.scopeIDs.push(11);
  grant.actions.length(8);
  grant.actions.set(1);
  ZtBitmap authority;
  ZuCheck(Zum::interactiveAuthority(grant, user, cred, client,
    false, {}, 8, scopes, roles, actionRecords, selection, authority) ==
    Zum::ScopeError::OK);
  ZuCheck(selection.scope == "read write" && authority[1] &&
    !authority[2] && !authority[3]);
  ZuCheck(Zum::interactiveAuthority(grant, user, cred, client,
    true, "write", 8, scopes, roles, actionRecords,
    selection, authority) == Zum::ScopeError::OK && !authority);
  grant.userID = 43;
  ZuCheck(Zum::interactiveAuthority(grant, user, cred, client,
    false, {}, 8, scopes, roles, actionRecords, selection, authority) ==
    Zum::AuthorityError::Invalid);
  grant.userID = 42;
  cred.state = Zum::State::Disabled;
  ZuCheck(Zum::interactiveAuthority(grant, user, cred, client,
    false, {}, 8, scopes, roles, actionRecords, selection, authority) ==
    Zum::AuthorityError::Invalid);
  cred.state = Zum::State::Active;
  ++user.authVersion;
  ZuCheck(Zum::interactiveAuthority(grant, user, cred, client,
    false, {}, 8, scopes, roles, actionRecords, selection, authority) ==
    Zum::AuthorityError::Invalid);

  client.type = Zum::ClientType::Confidential;
  client.grants = Zum::ClientGrant::ClientCredentials;
  client.roleIDs = userRoles;
  ZuCheck(Zum::clientAuthority(client, "write", 8,
    scopes, roles, actionRecords, selection, authority) ==
    Zum::ScopeError::OK);
  ZuCheck(selection.scope == "write" && authority[2] && !authority[3]);
}

static void records()
{
  ZuTestScope(records);
  Zum::Role role;
  role.id = 42;
  role.name = "operator";
  role.actions.length(128);
  role.actions.set(7);

  Zfb::IOBuilder fbb{new ZiIOBufAlloc<>()};
  fbb.Finish(ZfbStruct::save(fbb, role));
  auto buf = fbb.buf();
  flatbuffers::Verifier verifier{buf->data(), buf->length};
  ZuCheck(Zum::fbs::VerifyRoleBuffer(verifier));
  auto saved = ZfbStruct::root<Zum::Role>(buf->data());
  auto loaded = ZfbStruct::ctor<Zum::Role>(saved);
  ZuCheck(loaded.id == role.id);
  ZuCheck(loaded.name == role.name);
  ZuCheck(loaded.actions[7]);
  ZuCheck(loaded.state == Zum::State::Active);
}

static void discovery()
{
  ZuTestScope(discovery);
  auto metadata = Zum::metadataJSON("https://auth.example/");
  ZuCheck(metadata.find<"\"issuer\":\"https://auth.example/\"">() >= 0);
  ZuCheck(metadata.find<
    "\"authorization_endpoint\":\"https://auth.example/authorize\"">() >= 0);
  ZuCheck(metadata.find<"\"code_challenge_methods_supported\":[\"S256\"]">() >= 0);

  Zum::StringVec keys;
  keys.push(Zum::String{"{\"kty\":\"EC\",\"kid\":\"one\"}"});
  keys.push(Zum::String{"{\"kty\":\"EC\",\"kid\":\"two\"}"});
  ZuCheck(Zum::jwksJSON(keys) ==
    "{\"keys\":[{\"kty\":\"EC\",\"kid\":\"one\"},"
    "{\"kty\":\"EC\",\"kid\":\"two\"}]}");
}

static void refresh()
{
  ZuTestScope(refresh);
  Zum::Grant grant;
  grant.digest = Zum::Bytes{ZuBSpan{"current"}};
  grant.spent.push(Zum::Bytes{ZuBSpan{"spent-0"}});
  grant.spent.push(Zum::Bytes{ZuBSpan{"spent-1"}});

  ZuCheck(Zum::refreshMatch(grant, ZuBSpan{"current"}) ==
    Zum::RefreshMatch::Current);
  ZuCheck(Zum::refreshMatch(grant, ZuBSpan{"spent-1"}) ==
    Zum::RefreshMatch::Spent);
  ZuCheck(Zum::refreshMatch(grant, ZuBSpan{"unknown"}) ==
    Zum::RefreshMatch::Unknown);

  grant.kind = Zum::GrantKind::Refresh;
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

static void jwt()
{
  ZuTestScope(jwt);
  Ztls::Random rng;
  ZuCheck(rng.init());
  Ztls::PK::SK_EC sk{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  uint8_t publicKey[Ztls::COSE::ES256::PublicKeySize];
  ZuCheck(Ztls::Backend::pkey_ec_export_public(sk.key, publicKey));

  Zum::User user;
  user.handle = Zum::Bytes{ZuBSpan{"opaque user handle"}};
  user.state = Zum::State::Active;
  Zum::Client client;
  client.id = "browser";
  client.type = Zum::ClientType::Browser;
  client.grants = Zum::ClientGrant::AuthorizationCode;
  client.state = Zum::State::Active;
  Zum::ScopeSelection selection;
  selection.audience = "orders";
  selection.scope = "orders.read";
  ZtBitmap authority{8U};
  authority.set(3);
  Zum::Action actions[] = {
    {.id = 3, .name = "orders.read"},
    {.id = 4, .name = "orders.disabled", .state = Zum::State::Disabled}
  };
  Zum::AccessClaims claims;
  ZuCheck(Zum::interactiveClaims(rng, "https://issuer", user, client,
    selection, authority, actions, "passkey", 100, 100, 160, claims));
  ZuCheck(claims.subject != "user-42" && claims.jti &&
    claims.actions.length() == 1 && claims.actions[0] == "orders.read");
  Zum::String subject = claims.subject;
  Zum::JWTLimits limits;
  Zum::PreparedJWT prepared;
  ZuCheck(Zum::jwtPrepare(claims, "key-1", limits, prepared));
  uint8_t signature[Ztls::COSE::ES256::DERMax];
  unsigned signatureLength = 0;
  auto signed_ = sk.sign(rng, prepared.digest, [
    &signature, &signatureLength
  ](ZuBSpan der) {
    signatureLength = der.length();
    memcpy(signature, der.data(), signatureLength);
  });
  ZuCheck(!signed_.template is<ZeException>() && signatureLength);
  ZuCheck(Zum::jwtFinish(
    prepared, ZuBSpan{signature, signatureLength}, limits));

  Zum::Principal principal;
  ZuCheck(Zum::jwtVerify(prepared.token, "key-1", "https://issuer", "orders",
    publicKey, 120, limits, principal));
  ZuCheck(principal.subject == subject &&
    principal.clientID == "browser" && principal.scope == "orders.read" &&
    principal.actions.length() == 1 &&
    principal.actions[0] == "orders.read" &&
    principal.authMethod == "passkey" &&
    principal.authTime == 100 && principal.expires == 160);
  ZuCheck(!Zum::jwtVerify(prepared.token, "key-1", "https://issuer", "other",
    publicKey, 120, limits, principal));
  ZuCheck(!Zum::jwtVerify(prepared.token, "key-1", "https://issuer", "orders",
    publicKey, 160, limits, principal));

  Zum::String changed = prepared.token;
  changed[changed.length() / 2] ^= 1;
  ZuCheck(!Zum::jwtVerify(changed, "key-1", "https://issuer", "orders",
    publicKey, 120, limits, principal));

  client.id = "workload";
  client.type = Zum::ClientType::Confidential;
  client.grants = Zum::ClientGrant::ClientCredentials;
  claims = {};
  ZuCheck(Zum::clientClaims(rng, "https://issuer", client, selection,
    authority, actions, 200, 260, claims));
  ZuCheck(claims.subject == "workload" && claims.clientID == "workload" &&
    !claims.authTime && !claims.amr && claims.actions.length() == 1);
  ZuCheck(Zum::jwtPrepare(claims, "key-1", limits, prepared));
  signatureLength = 0;
  signed_ = sk.sign(rng, prepared.digest, [
    &signature, &signatureLength
  ](ZuBSpan der) {
    signatureLength = der.length();
    memcpy(signature, der.data(), signatureLength);
  });
  ZuCheck(!signed_.template is<ZeException>() && signatureLength);
  ZuCheck(Zum::jwtFinish(
    prepared, ZuBSpan{signature, signatureLength}, limits));
  ZuCheck(Zum::jwtVerify(prepared.token, "key-1", "https://issuer", "orders",
    publicKey, 220, limits, principal));
  ZuCheck(principal.subject == "workload" &&
    principal.clientID == "workload" && !principal.authMethod &&
    !principal.authTime && principal.expires == 260);
}

static constexpr char assertionClientData_[] =
  "{\"type\":\"webauthn.get\",\"challenge\":\"Y2hhbGxlbmdl\","
  "\"origin\":\"https://example.com\","
  "\"tokenBinding\":{\"status\":\"supported\"}}";

// Fixed WebAuthn assertion layout: SHA-256 RP hash, flags, and counter.
enum { TestAuthDataSize = Ztls::MD<>::Size + 1 + 4 };

static bool makeAssertion(
    Ztls::Random &rng, Ztls::PK::SK_EC &sk, uint32_t signCount,
    Zum::AssertionInput &input, bool backupEligible = false)
{
  uint8_t authData[TestAuthDataSize];
  {
    Ztls::MD<> md;
    md.update(ZuBSpan{"example.com"});
    md.finish(authData);
  }
  authData[32] = 5 | (backupEligible ? 8 : 0);
  authData[33] = signCount>>24;
  authData[34] = signCount>>16;
  authData[35] = signCount>>8;
  authData[36] = signCount;
  uint8_t clientHash[Ztls::MD<>::Size];
  {
    Ztls::MD<> md;
    md.update(ZuBSpan{
      assertionClientData_, sizeof(assertionClientData_) - 1});
    md.finish(clientHash);
  }
  uint8_t signedHash[Ztls::MD<>::Size];
  {
    Ztls::MD<> md;
    md.update(authData);
    md.update(clientHash);
    md.finish(signedHash);
  }
  Zum::Bytes signature;
  auto signed_ = sk.sign(rng, signedHash, [&signature](ZuBSpan der) {
    signature = Zum::Bytes{der};
  });
  if (signed_.template is<ZeException>() || !signature) return false;
  input = Zum::AssertionInput{
    .credentialID = Zum::Bytes{ZuBSpan{"credential"}},
    .clientDataJSON = Zum::Bytes{ZuBSpan{
      assertionClientData_, sizeof(assertionClientData_) - 1}},
    .authenticatorData = Zum::Bytes{ZuBSpan{authData}},
    .signature = ZuMv(signature),
    .userHandle = Zum::Bytes{ZuBSpan{"handle"}}
  };
  return true;
}

static bool makeAssertion(
    Ztls::Random &rng, Ztls::PK::SK_EC &sk, uint32_t signCount,
    ZuBSpan challenge, ZuCSpan origin, ZuCSpan rpID,
    Zum::AssertionInput &input)
{
  Zum::String encoded;
  encoded.length(ZuBase64URL::enclen(challenge.length()));
  encoded.length(ZuBase64URL::encode({
    reinterpret_cast<uint8_t *>(encoded.data()), encoded.length()},
    challenge));
  Zum::String clientData{"{\"type\":\"webauthn.get\",\"challenge\":"};
  ZfJSON::quote(clientData, encoded);
  clientData << ",\"origin\":";
  ZfJSON::quote(clientData, origin);
  clientData << '}';

  uint8_t authData[TestAuthDataSize];
  {
    Ztls::MD<> md;
    md.update(ZuBSpan{rpID});
    md.finish(authData);
  }
  authData[32] = 5;
  authData[33] = signCount>>24;
  authData[34] = signCount>>16;
  authData[35] = signCount>>8;
  authData[36] = signCount;
  uint8_t clientHash[Ztls::MD<>::Size];
  {
    Ztls::MD<> md;
    md.update(clientData);
    md.finish(clientHash);
  }
  uint8_t signedHash[Ztls::MD<>::Size];
  {
    Ztls::MD<> md;
    md.update(authData);
    md.update(clientHash);
    md.finish(signedHash);
  }
  Zum::Bytes signature;
  auto signed_ = sk.sign(rng, signedHash, [&signature](ZuBSpan der) {
    signature = Zum::Bytes{der};
  });
  if (signed_.template is<ZeException>() || !signature) return false;
  input = Zum::AssertionInput{
    .credentialID = Zum::Bytes{ZuBSpan{"credential"}},
    .clientDataJSON = Zum::Bytes{ZuBSpan{clientData}},
    .authenticatorData = Zum::Bytes{ZuBSpan{authData}},
    .signature = ZuMv(signature),
    .userHandle = Zum::Bytes{ZuBSpan{"handle"}}
  };
  return true;
}

static bool makeRegistration(
    ZuBSpan publicKey, uint32_t signCount, ZuCSpan credentialID,
    ZuBSpan challenge,
    ZuCSpan origin, ZuCSpan rpID, Zum::RegistrationInput &input)
{
  if (publicKey.length() != Ztls::COSE::ES256::PublicKeySize) return false;
  Zum::String encoded;
  encoded.length(ZuBase64URL::enclen(challenge.length()));
  encoded.length(ZuBase64URL::encode({
    reinterpret_cast<uint8_t *>(encoded.data()), encoded.length()},
    challenge));
  Zum::String clientData{"{\"type\":\"webauthn.create\",\"challenge\":"};
  ZfJSON::quote(clientData, encoded);
  clientData << ",\"origin\":";
  ZfJSON::quote(clientData, origin);
  clientData << '}';

  Zum::Bytes authData;
  authData.length(55 + credentialID.length() + 77, false);
  memset(authData.data(), 0, authData.length());
  {
    Ztls::MD<> md;
    md.update(ZuBSpan{rpID});
    md.finish(authData);
  }
  authData[32] = 0x45; // user present, verified, attested data
  authData[33] = signCount>>24;
  authData[34] = signCount>>16;
  authData[35] = signCount>>8;
  authData[36] = signCount;
  authData[53] = credentialID.length()>>8;
  authData[54] = credentialID.length();
  memcpy(authData.data() + 55, credentialID.data(), credentialID.length());
  unsigned o = 55 + credentialID.length();
  authData[o++] = 0xa5;
  authData[o++] = 0x01;
  authData[o++] = 0x02;
  authData[o++] = 0x03;
  authData[o++] = 0x26;
  authData[o++] = 0x20;
  authData[o++] = 0x01;
  authData[o++] = 0x21;
  authData[o++] = 0x58;
  authData[o++] = 0x20;
  memcpy(authData.data() + o, publicKey.data() + 1, 32);
  o += 32;
  authData[o++] = 0x22;
  authData[o++] = 0x58;
  authData[o++] = 0x20;
  memcpy(authData.data() + o, publicKey.data() + 33, 32);
  o += 32;
  if (o != authData.length()) return false;

  Zum::Bytes attestation;
  attestation.length(authData.length() + 30, false);
  o = 0;
  attestation[o++] = 0xa3;
  memcpy(attestation.data() + o, "\x63" "fmt\x64" "none", 9);
  o += 9;
  memcpy(attestation.data() + o, "\x68" "authData", 9);
  o += 9;
  attestation[o++] = 0x58;
  attestation[o++] = authData.length();
  memcpy(attestation.data() + o, authData.data(), authData.length());
  o += authData.length();
  memcpy(attestation.data() + o, "\x67" "attStmt\xa0", 9);
  o += 9;
  if (o != attestation.length()) return false;

  input = Zum::RegistrationInput{
    .credentialID = Zum::Bytes{ZuBSpan{credentialID}},
    .clientDataJSON = Zum::Bytes{ZuBSpan{clientData}},
    .attestationObject = ZuMv(attestation)
  };
  return true;
}

static void webAuthn()
{
  ZuTestScope(webAuthn);
  Ztls::Random rng;
  ZuCheck(rng.init());
  Ztls::PK::SK_EC sk{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  uint8_t publicKey[Ztls::COSE::ES256::PublicKeySize];
  ZuCheck(Ztls::Backend::pkey_ec_export_public(sk.key, publicKey));

  Zum::AssertionInput input;
  ZuCheck(makeAssertion(rng, sk, 6, input));

  Zum::AssertionState state{
    .challenge = ZuBSpan{"challenge"},
    .origin = ZuCSpan{"https://example.com"},
    .rpID = ZuCSpan{"example.com"},
    .publicKey = publicKey,
    .userHandle = ZuBSpan{"handle"},
    .signCount = 5
  };
  Zum::AssertionResult result;
  ZuCheck(Zum::verifyAssertion(input, state, result) ==
      Zum::WebAuthnError::OK);
  ZuCheck(result.signCount == 6 &&
    result.counter == Zum::CounterState::Advanced && !result.backedUp);

  input.clientDataJSON =
    Zum::Bytes{ZuBSpan{
      assertionClientData_, sizeof(assertionClientData_) - 1}};
  state.origin = "https://other.example";
  ZuCheck(Zum::verifyAssertion(input, state, result) ==
      Zum::WebAuthnError::Origin);
  input.clientDataJSON =
    Zum::Bytes{ZuBSpan{
      assertionClientData_, sizeof(assertionClientData_) - 1}};
  state.origin = "https://example.com";
  state.signCount = 6;
  ZuCheck(Zum::verifyAssertion(input, state, result) ==
      Zum::WebAuthnError::Counter);
  ZuCheck(makeAssertion(rng, sk, 6, input, true));
  state.backupEligible = true;
  ZuCheck(Zum::verifyAssertion(input, state, result) ==
      Zum::WebAuthnError::OK);
  ZuCheck(result.counter == Zum::CounterState::Regression);

  Zum::RegistrationState registrationState{
    .challenge = ZuBSpan{"challenge"},
    .origin = ZuCSpan{"https://example.com"},
    .rpID = ZuCSpan{"example.com"}
  };
  Zum::RegistrationInput registrationInput;
  ZuCheck(makeRegistration(publicKey, 0, "credential",
    ZuBSpan{"challenge"},
    "https://example.com", "example.com", registrationInput));
  Zum::RegistrationResult registration;
  ZuCheck(Zum::verifyRegistration(registrationInput,
    registrationState, 128,
    registration) == Zum::WebAuthnError::OK);
  ZuCheck(registration.credentialID == ZuBSpan{"credential"} &&
    registration.publicKey == ZuBSpan{publicKey} &&
    !registration.signCount && !registration.backupEligible &&
    !registration.backedUp);
}

static void webAuthnOptions()
{
  ZuTestScope(webAuthnOptions);
  Ztls::Random rng;
  ZuCheck(rng.init());
  Zum::Bytes challenge;
  ZuCheck(Zum::webAuthnChallenge(rng, challenge));
  ZuCheck(challenge.length() == Zum::WebAuthnChallengeSize);

  uint8_t fixed[] = {0, 1, 2, 3};
  ZuCheck(Zum::assertionOptions(fixed, "login.example", 60000) ==
    "{\"publicKey\":{\"challenge\":\"AAECAw\","
    "\"rpId\":\"login.example\",\"timeout\":60000,"
    "\"userVerification\":\"required\"}}");
  uint8_t handle[] = {0xfe, 0xff};
  ZuCheck(Zum::registrationOptions(fixed, "login.example", "Example",
      handle, "a\\\"b", "A B", 60000) ==
    "{\"publicKey\":{\"challenge\":\"AAECAw\","
    "\"rp\":{\"id\":\"login.example\",\"name\":\"Example\"},"
    "\"user\":{\"id\":\"_v8\",\"name\":\"a\\\\\\\"b\","
    "\"displayName\":\"A B\"},\"pubKeyCredParams\":[{"
    "\"type\":\"public-key\",\"alg\":-7}],\"timeout\":60000,"
    "\"authenticatorSelection\":{\"residentKey\":\"required\","
    "\"requireResidentKey\":true,\"userVerification\":\"required\"},"
    "\"attestation\":\"none\"}}");
}

static void webAuthnInput()
{
  ZuTestScope(webAuthnInput);
  Zum::WebAuthnInputLimits limits;
  char assertion[] =
    "{\"type\":\"public-key\",\"rawId\":\"Y3JlZA\",\"response\":{"
    "\"clientDataJSON\":\"e30\",\"authenticatorData\":\"AA\","
    "\"signature\":\"AQ\",\"userHandle\":\"aGFuZGxl\"}}";
  Zum::AssertionInput assertionInput;
  ZuCheck(Zum::parseAssertion(
    {assertion, sizeof(assertion) - 1}, limits,
    assertionInput) == Zum::WebAuthnError::OK);
  ZuCheck(assertionInput.credentialID == ZuBSpan{"cred"} &&
    assertionInput.clientDataJSON == ZuBSpan{"{}"} &&
    assertionInput.authenticatorData == ZuBSpan{uint8_t{0}} &&
    assertionInput.signature == ZuBSpan{uint8_t{1}} &&
    assertionInput.userHandle == ZuBSpan{"handle"});

  char registration[] =
    "{\"type\":\"public-key\",\"rawId\":\"Y3JlZA\",\"response\":{"
    "\"clientDataJSON\":\"e30\",\"attestationObject\":\"Ag\"}}";
  Zum::RegistrationInput registrationInput;
  ZuCheck(Zum::parseRegistration(
    {registration, sizeof(registration) - 1}, limits,
    registrationInput) == Zum::WebAuthnError::OK);
  ZuCheck(registrationInput.credentialID == ZuBSpan{"cred"} &&
    registrationInput.clientDataJSON == ZuBSpan{"{}"} &&
    registrationInput.attestationObject == ZuBSpan{uint8_t{2}});

  char nonCanonical[] =
    "{\"type\":\"public-key\",\"rawId\":\"Y3JlZB\",\"response\":{"
    "\"clientDataJSON\":\"e30\",\"attestationObject\":\"Ag\"}}";
  ZuCheck(Zum::parseRegistration(
    {nonCanonical, sizeof(nonCanonical) - 1}, limits,
    registrationInput) == Zum::WebAuthnError::OK);
}

static void enrollmentSaga()
{
  ZuTestScope(enrollmentSaga);
  using M = ZdbMSaga<Zum::Sagas>;
  Zum::Enrollment enrollment;
  enrollment.ceremonyID = Zum::Bytes{ZuBSpan{"ceremony"}};
  enrollment.userID = 42;
  enrollment.name = "first user";
  enrollment.handle = Zum::Bytes{ZuBSpan{"handle"}};
  enrollment.roleIDs.push(7);
  enrollment.credentialID = Zum::Bytes{ZuBSpan{"credential"}};
  enrollment.publicKey = Zum::Bytes{ZuBSpan{"cose-key"}};
  enrollment.created = 123;

  ZmRef<M> saga = new M{};
  saga->init(ZuMv(enrollment));
  Zdb_::SagaPayload payload;
  M::save(saga, payload);
  auto loaded = M::load(Zum::Enrollment::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &enrollment) {
    if constexpr (ZuIsSame<
        ZuDecay<decltype(enrollment)>, Zum::Enrollment>{})
      return enrollment.userID == 42 && enrollment.name == "first user" &&
	enrollment.roleIDs.length() == 1 && enrollment.roleIDs[0] == 7 &&
	enrollment.publicKey == ZuBSpan{"cose-key"};
    else
      return false;
  }));

  Zdb_::SagaTypeStep step;
  ZuCheck(M::catalog(0, 0, step));
  ZuCheck(step.table == "zum.grant" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(0, 6, step));
  ZuCheck(step.table == "zum.user" && step.op == ZdbSagaOp::Update);
  ZuCheck(!M::catalog(0, 7, step));

  Zum::CredentialAdd add;
  add.ceremonyID = Zum::Bytes{ZuBSpan{"credential ceremony"}};
  add.issuer = "issuer";
  add.userID = 42;
  add.userHandle = Zum::Bytes{ZuBSpan{"handle"}};
  add.credentialID = Zum::Bytes{ZuBSpan{"credential-2"}};
  add.publicKey = Zum::Bytes{ZuBSpan{"cose-key-2"}};
  add.created = 124;
  saga = new M{};
  saga->init(ZuMv(add));
  M::save(saga, payload);
  loaded = M::load(Zum::CredentialAdd::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &add) {
    if constexpr (ZuIsSame<ZuDecay<decltype(add)>, Zum::CredentialAdd>{})
      return add.issuer == "issuer" && add.userID == 42 &&
	add.credentialID == ZuBSpan{"credential-2"};
    else
      return false;
  }));
  ZuCheck(M::catalog(1, 0, step));
  ZuCheck(step.table == "zum.grant" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(1, 3, step));
  ZuCheck(step.table == "zum.cred" && step.op == ZdbSagaOp::Update);

  Zum::RecoveryStart recovery;
  recovery.capabilityID = Zum::Bytes{ZuBSpan{"recovery capability"}};
  recovery.digest = Zum::Bytes{ZuBSpan{"recovery digest"}};
  recovery.issuer = "issuer";
  recovery.userID = 42;
  recovery.userVersion = 2;
  recovery.created = 125;
  recovery.expires = 180;
  recovery.actor = "administrator";
  saga = new M{};
  saga->init(ZuMv(recovery));
  M::save(saga, payload);
  loaded = M::load(Zum::RecoveryStart::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &recovery) {
    if constexpr (ZuIsSame<ZuDecay<decltype(recovery)>, Zum::RecoveryStart>{})
      return recovery.issuer == "issuer" && recovery.userID == 42 &&
	recovery.userVersion == 2 && recovery.actor == "administrator";
    else
      return false;
  }));
  ZuCheck(M::catalog(2, 0, step));
  ZuCheck(step.table == "zum.user" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(2, 3, step));
  ZuCheck(step.table == "zum.grant" && step.op == ZdbSagaOp::Update);

  Zum::RecoveryEnroll recoveryEnroll;
  recoveryEnroll.ceremonyID = Zum::Bytes{ZuBSpan{"recovery ceremony"}};
  recoveryEnroll.issuer = "issuer";
  recoveryEnroll.actor = "administrator";
  recoveryEnroll.userID = 42;
  recoveryEnroll.userVersion = 2;
  recoveryEnroll.oldHandle = Zum::Bytes{ZuBSpan{"old handle"}};
  recoveryEnroll.newHandle = Zum::Bytes{ZuBSpan{"new handle"}};
  recoveryEnroll.credentialID = Zum::Bytes{ZuBSpan{"replacement"}};
  recoveryEnroll.publicKey = Zum::Bytes{ZuBSpan{"replacement key"}};
  recoveryEnroll.created = 126;
  saga = new M{};
  saga->init(ZuMv(recoveryEnroll));
  M::save(saga, payload);
  loaded = M::load(Zum::RecoveryEnroll::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &recovery) {
    if constexpr (ZuIsSame<
	ZuDecay<decltype(recovery)>, Zum::RecoveryEnroll>{})
      return recovery.actor == "administrator" && recovery.userID == 42 &&
	recovery.userVersion == 2 && recovery.newHandle == ZuBSpan{"new handle"};
    else
      return false;
  }));
  ZuCheck(M::catalog(3, 0, step));
  ZuCheck(step.table == "zum.grant" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(3, 5, step));
  ZuCheck(step.table == "zum.user" && step.op == ZdbSagaOp::Update);

  Zum::CodeFamily family;
  family.codeID = Zum::Bytes{ZuBSpan{"code"}};
  family.codeDigest = Zum::Bytes{ZuBSpan{"code digest"}};
  family.familyID = Zum::Bytes{ZuBSpan{"family"}};
  family.issuer = "issuer";
  family.userID = 42;
  family.clientID = "browser";
  family.credentialID = Zum::Bytes{ZuBSpan{"credential"}};
  family.audience = "orders";
  family.scopeIDs.push(3);
  family.actions.length(8);
  family.actions.set(2);
  family.digest = Zum::Bytes{ZuBSpan{"digest"}};
  family.authVersion = 7;
  family.authTime = family.created = 100;
  family.expires = 1000;
  saga = new M{};
  saga->init(ZuMv(family));
  M::save(saga, payload);
  loaded = M::load(Zum::CodeFamily::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &family) {
    if constexpr (ZuIsSame<ZuDecay<decltype(family)>, Zum::CodeFamily>{})
      return family.userID == 42 && family.authVersion == 7 &&
	family.actions[2];
    else
      return false;
  }));
  ZuCheck(M::catalog(4, 0, step));
  ZuCheck(step.table == "zum.grant" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(4, 2, step));
  ZuCheck(step.table == "zum.grant" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(4, 3, step));
  ZuCheck(step.table == "zum.grant" && step.op == ZdbSagaOp::Update);

  Zum::UserChange change;
  change.issuer = "issuer";
  change.userID = 42;
  change.oldRoleIDs.push(1);
  change.newRoleIDs.push(2);
  change.oldUpdated = 100;
  change.updated = 101;
  change.authVersion = 7;
  change.oldState = Zum::State::Active;
  change.newState = Zum::State::Disabled;
  saga = new M{};
  saga->init(ZuMv(change));
  M::save(saga, payload);
  loaded = M::load(Zum::UserChange::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::UserChange>{})
      return change.userID == 42 && change.authVersion == 7 &&
	change.oldRoleIDs.length() == 1 && change.oldRoleIDs[0] == 1 &&
	change.newRoleIDs.length() == 1 && change.newRoleIDs[0] == 2;
    else
      return false;
  }));
  ZuCheck(M::catalog(5, 0, step));
  ZuCheck(step.table == "zum.user" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(5, 1, step));
  ZuCheck(step.table == "zum.issuer" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(5, 2, step));
  ZuCheck(step.table == "zum.user" && step.op == ZdbSagaOp::Update);

  Zum::RoleChange roleChange;
  roleChange.issuer = "issuer";
  roleChange.roleID = 7;
  roleChange.name = "reader";
  roleChange.oldActions.length(8);
  roleChange.oldActions.set(1);
  roleChange.newActions.length(8);
  roleChange.newActions.set(2);
  roleChange.authVersion = 8;
  roleChange.oldState = Zum::State::Active;
  roleChange.newState = Zum::State::Disabled;
  saga = new M{};
  saga->init(ZuMv(roleChange));
  M::save(saga, payload);
  loaded = M::load(Zum::RoleChange::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::RoleChange>{})
      return change.roleID == 7 && change.name == "reader" &&
	change.authVersion == 8 && change.oldActions[1] &&
	change.newActions[2];
    else
      return false;
  }));
  ZuCheck(M::catalog(6, 0, step));
  ZuCheck(step.table == "zum.role" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(6, 1, step));
  ZuCheck(step.table == "zum.issuer" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(6, 2, step));
  ZuCheck(step.table == "zum.role" && step.op == ZdbSagaOp::Update);

  Zum::CredChange credChange;
  credChange.issuer = "issuer";
  credChange.credentialID = Zum::Bytes{ZuBSpan{"credential"}};
  credChange.oldUpdated = 100;
  credChange.updated = 101;
  credChange.authVersion = 9;
  credChange.oldState = Zum::State::Active;
  credChange.newState = Zum::State::Revoked;
  saga = new M{};
  saga->init(ZuMv(credChange));
  M::save(saga, payload);
  loaded = M::load(Zum::CredChange::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::CredChange>{})
      return change.credentialID == ZuBSpan{"credential"} &&
	change.authVersion == 9 && change.newState == Zum::State::Revoked;
    else
      return false;
  }));
  ZuCheck(M::catalog(7, 0, step));
  ZuCheck(step.table == "zum.cred" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(7, 1, step));
  ZuCheck(step.table == "zum.issuer" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(7, 2, step));
  ZuCheck(step.table == "zum.cred" && step.op == ZdbSagaOp::Update);

  Zum::ScopeChange scopeChange;
  scopeChange.issuer = "issuer";
  scopeChange.scopeID = 7;
  scopeChange.audience = "orders";
  scopeChange.name = "read";
  scopeChange.oldRoleIDs.push(7);
  scopeChange.authVersion = 10;
  scopeChange.oldState = Zum::State::Active;
  scopeChange.newState = Zum::State::Disabled;
  saga = new M{};
  saga->init(ZuMv(scopeChange));
  M::save(saga, payload);
  loaded = M::load(Zum::ScopeChange::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::ScopeChange>{})
      return change.scopeID == 7 && change.audience == "orders" &&
	change.name == "read" && change.authVersion == 10 &&
	change.oldRoleIDs.length() == 1 && change.oldRoleIDs[0] == 7;
    else
      return false;
  }));
  ZuCheck(M::catalog(8, 0, step));
  ZuCheck(step.table == "zum.scope" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(8, 1, step));
  ZuCheck(step.table == "zum.issuer" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(8, 2, step));
  ZuCheck(step.table == "zum.scope" && step.op == ZdbSagaOp::Update);

  Zum::ClientChange clientChange;
  clientChange.issuer = "issuer";
  clientChange.clientID = "workload";
  clientChange.oldSecretDigest = Zum::Bytes{ZuBSpan{"old digest"}};
  clientChange.newSecretDigest = Zum::Bytes{ZuBSpan{"new digest"}};
  clientChange.oldRoleIDs.push(7);
  clientChange.oldUpdated = 100;
  clientChange.updated = 101;
  clientChange.authVersion = 11;
  clientChange.oldState = Zum::State::Active;
  clientChange.newState = Zum::State::Disabled;
  saga = new M{};
  saga->init(ZuMv(clientChange));
  M::save(saga, payload);
  loaded = M::load(Zum::ClientChange::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::ClientChange>{})
      return change.clientID == "workload" && change.authVersion == 11 &&
	change.oldSecretDigest == ZuBSpan{"old digest"} &&
	change.newSecretDigest == ZuBSpan{"new digest"};
    else
      return false;
  }));
  ZuCheck(M::catalog(9, 0, step));
  ZuCheck(step.table == "zum.client" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(9, 1, step));
  ZuCheck(step.table == "zum.issuer" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(9, 2, step));
  ZuCheck(step.table == "zum.client" && step.op == ZdbSagaOp::Update);

  Zum::ActionChange actionChange;
  actionChange.issuer = "issuer";
  actionChange.actionID = 0;
  actionChange.name = "orders.read";
  actionChange.authVersion = 12;
  actionChange.oldState = Zum::State::Active;
  actionChange.newState = Zum::State::Disabled;
  saga = new M{};
  saga->init(ZuMv(actionChange));
  M::save(saga, payload);
  loaded = M::load(Zum::ActionChange::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::ActionChange>{})
      return !change.actionID && change.name == "orders.read" &&
	change.authVersion == 12 && change.newState == Zum::State::Disabled;
    else
      return false;
  }));
  ZuCheck(M::catalog(10, 0, step));
  ZuCheck(step.table == "zum.action" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(10, 1, step));
  ZuCheck(step.table == "zum.issuer" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(10, 2, step));
  ZuCheck(step.table == "zum.action" && step.op == ZdbSagaOp::Update);
}

struct TestDB : public Zum::DB {
  ZmSemaphore	active;
  ZmRef<Zum::Requests> requests;
};

static ZuPtr<const ZfCf::AnyNode> dbConfig()
{
  auto scan = ZfCf::scan(
    "zdb: {\n"
    "  thread: zdb, shards: 1, threads: [shard],\n"
    "  store: {thread: store},\n"
    "  hostID: self, hosts: {self: {standalone: true}}, tables: {}\n"
    "},\n"
    "mx: {\n"
    "  nThreads: 5, rxThread: rx, txThread: tx, threads: {\n"
    "    1: {name: rx, isolated: true},\n"
    "    2: {name: tx, isolated: true},\n"
    "    3: {name: zdb, isolated: true},\n"
    "    4: {name: store, isolated: true},\n"
    "    5: {name: shard, isolated: true}\n"
    "  }\n"
    "}\n");
  return ZuMv(scan.p<1>());
}

static void dbUp(Zdb *db, ZdbHost *)
{
  auto test = static_cast<TestDB *>(db);
  test->requests->activate();
  test->active.post();
}

static void dbDown(Zdb *db, bool)
{
  static_cast<TestDB *>(db)->requests->deactivate();
}

static bool storeAuthorization(
    Zum::DBContext *context, Zum::Grant grant)
{
  return ZmBlock<bool>{}([
    context, grant = ZuMv(grant)
  ](auto wake) mutable {
    Zum::authorizationInsert(
      context, ZuMv(grant), [wake = ZuMv(wake)](bool ok) mutable {
	wake(ok);
      });
  });
}

static int missingAssertion(Zum::DBContext *context)
{
  return ZmBlock<int>{}([context](auto wake) mutable {
    Zum::assertionVerify(context,
      Zum::Bytes{ZuBSpan{"missing ceremony"}},
      Zum::Bytes{ZuBSpan{"browser binding"}}, Zum::AssertionInput{},
      Zum::String{"https://example.com"}, Zum::String{"example.com"}, 100,
      [wake = ZuMv(wake)](int error, Zum::User, Zum::Cred, Zum::Grant,
          Zum::AssertionResult) mutable { wake(error); });
  });
}

struct StoredAssertion {
  int			error = Zum::WebAuthnError::Storage;
  Zum::User		user;
  Zum::Grant		grant;
  Zum::AssertionResult	result;
};

static StoredAssertion storedAssertion(
    Zum::DBContext *context, Zum::AssertionInput input)
{
  return ZmBlock<StoredAssertion>{}([
    context, input = ZuMv(input)
  ](auto wake) mutable {
    Zum::assertionVerify(context,
      Zum::Bytes{ZuBSpan{"passkey-auth-id0"}},
      Zum::Bytes{ZuBSpan{"passkey binding"}}, ZuMv(input),
      Zum::String{"https://example.com"}, Zum::String{"example.com"}, 130,
      [wake = ZuMv(wake)](int error, Zum::User user, Zum::Cred,
          Zum::Grant grant,
          Zum::AssertionResult result) mutable {
        wake(StoredAssertion{
          error, ZuMv(user), ZuMv(grant), ZuMv(result)});
      });
  });
}

static bool credentialCount(
    Zum::DBContext *context, ZuBSpan credentialID, uint32_t signCount)
{
  return ZmBlock<bool>{}([
    context, credentialID, signCount
  ](auto wake) mutable {
    context->creds->run(0, [
      context, credentialID, signCount, wake = ZuMv(wake)
    ]() mutable {
      context->creds->find<0>(0, ZuFwdTuple(credentialID), [
        signCount, wake = ZuMv(wake)
      ](ZdbRowRef<Zum::Cred> row) mutable {
        wake(row && row->data().state == Zum::State::Active &&
	  row->data().signCount == signCount);
      });
    });
  });
}

static bool finishAuthorization(
    Zum::DBContext *context, Ztls::Random &rng, Zum::Bytes id,
    Zum::Bytes bindingDigest, Zum::UserID userID, ZuBSpan credentialID,
    Zum::String &code)
{
  return ZmBlock<bool>{}([
    context, &rng, id = ZuMv(id),
    bindingDigest = ZuMv(bindingDigest), userID, credentialID, &code
  ](auto wake) mutable {
    ZtBitmap actions{8U};
    actions.set(3);
    Zum::IDVec roleIDs;
    roleIDs.push(7);
    Zum::authorizationFinish(context, rng, ZuMv(id), ZuMv(bindingDigest),
      userID, Zum::Bytes{credentialID}, ZuMv(roleIDs), ZuMv(actions),
      10, 1, 120, 180, [
	&code, wake = ZuMv(wake)
      ](bool ok, Zum::String next) mutable {
	if (ok) code = ZuMv(next);
	wake(ok);
      });
  });
}

static bool authorizationState(
    Zum::DBContext *context, ZuBSpan id, ZuCSpan code,
    Zum::UserID userID, ZuBSpan credentialID)
{
  Zum::Bytes codeID, codeDigest;
  if (!Zum::opaqueParse(code, codeID, codeDigest) || codeID != id) return false;
  return ZmBlock<bool>{}([
    context, id, codeDigest = ZuMv(codeDigest), userID, credentialID
  ](auto wake) mutable {
    context->grants->run(0, [
      context, id, codeDigest = ZuMv(codeDigest), userID, credentialID,
      wake = ZuMv(wake)
    ]() mutable {
      context->grants->find<0>(0, ZuFwdTuple(id), [
	codeDigest = ZuMv(codeDigest), userID, credentialID,
	wake = ZuMv(wake)
      ](ZdbRowRef<Zum::Grant> row) mutable {
	wake(row && row->data().userID == userID &&
	  row->data().credentialID == credentialID &&
	  row->data().actions[3] && !row->data().challenge &&
	  !row->data().bindingDigest && Zum::codeMatches(
	    row->data(), codeDigest,
	    "browser", "https://app/cb",
	    "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk", 121));
      });
    });
  });
}

static bool prepareCodeFamily(
    Zum::DBContext *context, Ztls::Random &rng, ZuCSpan code,
    Zum::CodeFamily &family, Zum::String &refreshToken)
{
  Zum::Bytes codeID, codeDigest;
  if (!Zum::opaqueParse(code, codeID, codeDigest)) return false;
  return ZmBlock<bool>{}([
    context, &rng, codeID = ZuMv(codeID),
    codeDigest = ZuMv(codeDigest), &family, &refreshToken
  ](auto wake) mutable {
    context->grants->run(0, [
      context, &rng, codeID = ZuMv(codeID),
      codeDigest = ZuMv(codeDigest), &family, &refreshToken,
      wake = ZuMv(wake)
    ]() mutable {
      context->grants->find<0>(0, ZuFwdTuple(codeID), [
	&rng, codeDigest = ZuMv(codeDigest), &family, &refreshToken,
	wake = ZuMv(wake)
      ](ZdbRowRef<Zum::Grant> row) mutable {
	if (!row) {
	  wake(false);
	  return;
	}
	Zum::IDVec scopeIDs = row->data().scopeIDs;
	ZtBitmap actions = row->data().actions;
	if (!Zum::codeMatches(row->data(), codeDigest,
	    "browser", "https://app/cb",
	    "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk", 125)) {
	  wake(false);
	  return;
	}
	wake(Zum::codeFamilyPrepare(rng, row->data(), codeDigest,
	  ZuMv(scopeIDs), ZuMv(actions), 10, 125, 1000,
	  family, refreshToken));
      });
    });
  });
}

static Zum::RefreshRotate::T rotateRefresh(
    Zum::DBContext *context, Ztls::Random &rng, ZuCSpan refreshToken,
    Zum::String &nextToken)
{
  Zum::Bytes familyID, digest;
  if (!Zum::opaqueParse(refreshToken, familyID, digest))
    return Zum::RefreshRotate::Invalid;
  return ZmBlock<Zum::RefreshRotate::T>{}([
    context, &rng, familyID = ZuMv(familyID), digest = ZuMv(digest),
    &nextToken
  ](auto wake) mutable {
    Zum::IDVec scopeIDs;
    scopeIDs.push(7);
    ZtBitmap actions{8U};
    actions.set(3);
    Zum::refreshFinish(context, rng, ZuMv(familyID), ZuMv(digest),
      Zum::String{"issuer"}, ZuMv(scopeIDs), ZuMv(actions),
      10, 41, 1, 130, 8, 8,
      [&nextToken, wake = ZuMv(wake)](
	  Zum::RefreshRotate::T result, Zum::String token) mutable {
	if (result == Zum::RefreshRotate::Rotated)
	  nextToken = ZuMv(token);
	wake(result);
      });
  });
}

static bool refreshState(
    Zum::DBContext *context, ZuBSpan familyID, ZuBSpan digest,
    unsigned generation, Zum::State::T state)
{
  return ZmBlock<bool>{}([
    context, familyID, digest, generation, state
  ](auto wake) mutable {
    context->grants->run(0, [
      context, familyID, digest, generation, state, wake = ZuMv(wake)
    ]() mutable {
      context->grants->find<0>(0, ZuFwdTuple(familyID), [
	digest, generation, state, wake = ZuMv(wake)
      ](ZdbRowRef<Zum::Grant> row) mutable {
	wake(row && row->data().digest == digest &&
	  row->data().generation == generation &&
	  row->data().state == state && row->data().scopeIDs.length() == 1 &&
	  row->data().scopeIDs[0] == 7 && row->data().actions[3]);
      });
    });
  });
}

static bool insertIssuer(
    Zum::DBContext *context, ZuCSpan id, uint64_t authVersion)
{
  return ZmBlock<bool>{}([context, id, authVersion](auto wake) mutable {
    context->issuers->run(0, [
	context, id, authVersion, wake = ZuMv(wake)
    ]() mutable {
      ZdbRowRef<Zum::Issuer> row =
	new ZdbRow<Zum::Issuer>{context->issuers, ZdbShard{0}};
      context->issuers->insert(row, [
	id, authVersion, wake = ZuMv(wake)
      ](ZdbRow<Zum::Issuer> *row) mutable {
	if (!row) { wake(false); return; }
	new (row->ptr()) Zum::Issuer{
	  .id = Zum::String{id}, .authVersion = authVersion};
	wake(row->commit());
      });
    });
  });
}

template <typename T>
static bool insertRecord(ZdbTable<T> *table, T data)
{
  return ZmBlock<bool>{}([
    table, data = ZuMv(data)
  ](auto wake) mutable {
    table->run(0, [table, data = ZuMv(data), wake = ZuMv(wake)]() mutable {
      ZdbRowRef<T> row = new ZdbRow<T>{table, ZdbShard{0}};
      table->insert(row, [
	data = ZuMv(data), wake = ZuMv(wake)
      ](ZdbRow<T> *row) mutable {
	if (!row) { wake(false); return; }
	new (row->ptr()) T{ZuMv(data)};
	wake(row->commit());
      });
    });
  });
}

struct AuthorityResult {
  int			error = Zum::AuthorityError::Invalid;
  Zum::AuthorityData	data;
};

static AuthorityResult loadInteractiveAuthority(
    Zum::DBContext *context, Zum::Grant grant, Zum::Client client)
{
  return ZmBlock<AuthorityResult>{}([
    context, grant = ZuMv(grant), client = ZuMv(client)
  ](auto wake) mutable {
    Zum::loadGrantAuth(
      context, ZuMv(grant), ZuMv(client), false, {}, [
	wake = ZuMv(wake)
    ](int error, Zum::AuthorityData data) mutable {
      wake(AuthorityResult{error, ZuMv(data)});
    });
  });
}

static AuthorityResult loadClientAuthority(
    Zum::DBContext *context, Zum::Client client)
{
  return ZmBlock<AuthorityResult>{}([
    context, client = ZuMv(client)
  ](auto wake) mutable {
    Zum::loadClientAuth(context, Zum::String{"issuer"},
      ZuMv(client), true, Zum::String{"read"}, [
	wake = ZuMv(wake)
      ](int error, Zum::AuthorityData data) mutable {
	wake(AuthorityResult{error, ZuMv(data)});
      });
  });
}

struct IssuedToken {
  int			error = Zum::OAuthError::ServerError;
  Zum::TokenResponse	response;
};

struct Authorized {
  int			 error = Zum::OAuthError::ServerError;
  Zum::AuthorizeResult result;
};

struct FinishedAuthorization {
  int		 error = Zum::OAuthError::ServerError;
  Zum::String	 location;
};

static Authorized beginAuthorization(
    Zum::Requests *requests, Zum::DBContext *context, Ztls::Random &rng,
    Zum::String query, int64_t now = 200)
{
  Zum::AuthorizeConfig config{
    .issuer = "issuer", .rpID = "login.example",
    .now = now, .expires = now + 60, .timeout = 60000, .passkey = true};
  return ZmBlock<Authorized>{}([
    requests, context, &rng, query = ZuMv(query), config = ZuMv(config)
  ](auto wake) mutable {
    if (!Zum::authorizeRequest(requests, Zm::now() + ZuTime{10},
      context, rng, ZuMv(query),
      Zum::Bytes{ZuBSpan{"browser binding"}}, ZuMv(config), [
	wake
      ](int error, Zum::AuthorizeResult result) mutable {
	wake(Authorized{error, ZuMv(result)});
      }))
      wake(Authorized{});
  });
}

static FinishedAuthorization finishAuthorization(
    Zum::Requests *requests, Zum::DBContext *context, Ztls::Random &rng,
    Zum::Bytes ceremonyID, Zum::AssertionInput input,
    Zum::ActionID action, int64_t now = 210)
{
  Zum::AuthorizeFinishConfig config{
    .origin = "https://example.com", .rpID = "example.com",
    .now = now, .codeExpires = now + 290};
  Zum::PolicyFn policy{[action](
      const Zum::User &, const Zum::Client &,
      const Zum::ScopeSelection &, const ZtBitmap &allowed,
      Zum::PolicyDoneFn complete) mutable {
    ZtBitmap actions{allowed.length()};
    if (allowed[action]) actions.set(action);
    complete(true, ZuMv(actions));
  }};
  return ZmBlock<FinishedAuthorization>{}([
    requests, context, &rng, ceremonyID = ZuMv(ceremonyID),
    input = ZuMv(input), config = ZuMv(config), policy = ZuMv(policy)
  ](auto wake) mutable {
    if (!Zum::authorizeFinish(requests, Zm::now() + ZuTime{10},
      context, rng, ZuMv(ceremonyID),
      Zum::Bytes{ZuBSpan{"browser binding"}}, ZuMv(input), ZuMv(config),
      ZuMv(policy), [wake](
	  int error, Zum::String location) mutable {
	wake(FinishedAuthorization{error, ZuMv(location)});
      }))
      wake(FinishedAuthorization{});
  });
}

static FinishedAuthorization finishOIDCAuthorization(
    Zum::Requests *requests, Zum::DBContext *context, Ztls::Random &rng,
    Zum::Bytes ceremonyID, Zum::User user, Zum::IDVec roleIDs,
    Zum::ActionID action, int64_t authTime, int64_t now = 200)
{
  Zum::AuthorizeFinishConfig config{.now = now, .codeExpires = now + 290};
  Zum::PolicyFn policy{[action](
      const Zum::User &, const Zum::Client &,
      const Zum::ScopeSelection &, const ZtBitmap &allowed,
      Zum::PolicyDoneFn complete) mutable {
    ZtBitmap actions{allowed.length()};
    if (allowed[action]) actions.set(action);
    complete(true, ZuMv(actions));
  }};
  return ZmBlock<FinishedAuthorization>{}([
    requests, context, &rng, ceremonyID = ZuMv(ceremonyID),
    user = ZuMv(user), roleIDs = ZuMv(roleIDs), authTime,
    config = ZuMv(config), policy = ZuMv(policy)
  ](auto wake) mutable {
    if (!Zum::authorizeOIDCFinish(requests, Zm::now() + ZuTime{10},
      context, rng, ZuMv(ceremonyID),
      Zum::Bytes{ZuBSpan{"oidc binding"}}, ZuMv(user), ZuMv(roleIDs),
      authTime, ZuMv(config), ZuMv(policy), [wake](
	  int error, Zum::String location) mutable {
	wake(FinishedAuthorization{error, ZuMv(location)});
      }))
      wake(FinishedAuthorization{});
  });
}

static Zum::SignFn testSigner(
    Ztls::Random &rng, Ztls::PK::SK_EC &key)
{
  return Zum::SignFn{[&rng, &key](
      ZuCSpan providerRef, ZuBSpan digest, Zum::SignatureFn complete) mutable {
    Zum::Bytes signature;
    auto result = key.sign(rng, digest, [&signature](ZuBSpan der) {
      signature = Zum::Bytes{der};
    });
    if (providerRef != "test-key" || result.template is<ZeException>())
      signature = {};
    complete(ZuMv(signature));
  }};
}

static IssuedToken issueTokenRequest(
    Zum::Requests *requests, TestDB *db, Zum::DBContext *context,
    Ztls::Random &rng,
    Zum::String form, Zum::String authorization,
    Ztls::PK::SK_EC &key, int64_t now)
{
  Zum::TokenConfig config{
    .issuer = "issuer",
    .keyID = "token-key",
    .now = now,
    .accessExpires = now + 60,
    .refreshExpires = 1000,
    .generationLimit = 8,
    .spentLimit = 8
  };
  auto sign = testSigner(rng, key);
  return ZmBlock<IssuedToken>{}([
    requests, db, context, &rng, form = ZuMv(form),
    authorization = ZuMv(authorization), config = ZuMv(config),
    sign = ZuMv(sign)
  ](auto wake) mutable {
    if (!Zum::tokenRequest(requests, Zm::now() + ZuTime{10},
      db, context, rng, ZuMv(form), ZuMv(authorization),
      ZuMv(config), ZuMv(sign), [wake](
	  int error, Zum::TokenResponse response) mutable {
	wake(IssuedToken{error, ZuMv(response)});
      }))
      wake(IssuedToken{});
  });
}

static Zum::Grant loadGrant(Zum::DBContext *context, ZuBSpan id)
{
  return ZmBlock<Zum::Grant>{}([context, id](auto wake) mutable {
    context->grants->run(0, [context, id, wake = ZuMv(wake)]() mutable {
      context->grants->find<0>(0, ZuFwdTuple(id), [
	wake = ZuMv(wake)
      ](ZdbRowRef<Zum::Grant> row) mutable {
	wake(row ? Zum::Grant{row->data()} : Zum::Grant{});
      });
    });
  });
}

static Zum::User loadUser(Zum::DBContext *context, Zum::UserID id)
{
  return ZmBlock<Zum::User>{}([context, id](auto wake) mutable {
    context->users->run(0, [context, id, wake = ZuMv(wake)]() mutable {
      context->users->find<0>(0, ZuFwdTuple(id), [
	wake = ZuMv(wake)
      ](ZdbRowRef<Zum::User> row) mutable {
	wake(row ? Zum::User{row->data()} : Zum::User{});
      });
    });
  });
}

static Zum::Role loadRole(Zum::DBContext *context, Zum::RoleID id)
{
  return ZmBlock<Zum::Role>{}([context, id](auto wake) mutable {
    context->roles->run(0, [context, id, wake = ZuMv(wake)]() mutable {
      context->roles->find<0>(0, ZuFwdTuple(id), [
	wake = ZuMv(wake)
      ](ZdbRowRef<Zum::Role> row) mutable {
	wake(row ? Zum::Role{row->data()} : Zum::Role{});
      });
    });
  });
}

static Zum::Scope loadScope(Zum::DBContext *context, Zum::ScopeID id)
{
  return ZmBlock<Zum::Scope>{}([context, id](auto wake) mutable {
    context->scopes->run(0, [context, id, wake = ZuMv(wake)]() mutable {
      context->scopes->find<0>(0, ZuFwdTuple(id), [
	wake = ZuMv(wake)
      ](ZdbRowRef<Zum::Scope> row) mutable {
	wake(row ? Zum::Scope{row->data()} : Zum::Scope{});
      });
    });
  });
}

static Zum::Client loadClient(Zum::DBContext *context, ZuCSpan id)
{
  return ZmBlock<Zum::Client>{}([context, id](auto wake) mutable {
    context->clients->run(0, [context, id, wake = ZuMv(wake)]() mutable {
      context->clients->find<0>(0, ZuFwdTuple(id), [
	wake = ZuMv(wake)
      ](ZdbRowRef<Zum::Client> row) mutable {
	wake(row ? Zum::Client{row->data()} : Zum::Client{});
      });
    });
  });
}

static Zum::Action loadAction(
    Zum::DBContext *context, Zum::ActionID id)
{
  return ZmBlock<Zum::Action>{}([context, id](auto wake) mutable {
    context->actions->run(0, [context, id, wake = ZuMv(wake)]() mutable {
      context->actions->find<0>(0, ZuFwdTuple(id), [
	wake = ZuMv(wake)
      ](ZdbRowRef<Zum::Action> row) mutable {
	wake(row ? Zum::Action{row->data()} : Zum::Action{});
      });
    });
  });
}

static Zum::Cred loadCred(Zum::DBContext *context, ZuBSpan id)
{
  return ZmBlock<Zum::Cred>{}([context, id](auto wake) mutable {
    context->creds->run(0, [context, id, wake = ZuMv(wake)]() mutable {
      context->creds->find<0>(0, ZuFwdTuple(id), [
	wake = ZuMv(wake)
      ](ZdbRowRef<Zum::Cred> row) mutable {
	wake(row ? Zum::Cred{row->data()} : Zum::Cred{});
      });
    });
  });
}

static Zum::Audit loadAudit(
    Zum::DBContext *context, uint64_t id, ZuCSpan issuer)
{
  return ZmBlock<Zum::Audit>{}([context, id, issuer](auto wake) mutable {
    context->audits->run(0, [
      context, id, issuer, wake = ZuMv(wake)
    ]() mutable {
      context->audits->find<0>(0, ZuFwdTuple(id, issuer), [
	wake = ZuMv(wake)
      ](ZdbRowRef<Zum::Audit> row) mutable {
	wake(row ? Zum::Audit{row->data()} : Zum::Audit{});
      });
    });
  });
}

static bool createAction(
    Zum::DBContext *context, ZuCSpan name, Zum::ActionID &id)
{
  return ZmBlock<bool>{}([context, name, &id](auto wake) mutable {
    Zum::actionCreate(context, Zum::String{"issuer"}, Zum::String{name}, [
      &id, wake = ZuMv(wake)
    ](bool ok, Zum::ActionID next) mutable {
      if (ok) id = next;
      wake(ok);
    });
  });
}

static bool actionState(
    Zum::DBContext *context, Zum::ActionID id, ZuCSpan name,
    Zum::ActionID nextActionID, uint64_t authVersion)
{
  return ZmBlock<bool>{}([
    context, id, name, nextActionID, authVersion
  ](auto wake) mutable {
    context->actions->run(0, [
      context, id, name, nextActionID, authVersion, wake = ZuMv(wake)
    ]() mutable {
      context->actions->find<0>(0, ZuFwdTuple(id), [
        context, name, nextActionID, authVersion, wake = ZuMv(wake)
      ](ZdbRowRef<Zum::Action> action) mutable {
        bool ok = action && action->data().name == name &&
          action->data().state == Zum::State::Active;
        context->issuers->find<0>(0, ZuFwdTuple(ZuCSpan{"issuer"}), [
          ok, nextActionID, authVersion, wake = ZuMv(wake)
        ](ZdbRowRef<Zum::Issuer> issuer) mutable {
          wake(ok && issuer &&
            issuer->data().nextActionID == nextActionID &&
            issuer->data().authVersion == authVersion);
        });
      });
    });
  });
}

static bool insertCode(Zum::DBContext *context, ZuBSpan id)
{
  return ZmBlock<bool>{}([context, id](auto wake) mutable {
    context->grants->run(0, [context, id, wake = ZuMv(wake)]() mutable {
      ZdbRowRef<Zum::Grant> row =
	new ZdbRow<Zum::Grant>{context->grants, ZdbShard{0}};
      context->grants->insert(row, [
	id, wake = ZuMv(wake)](ZdbRow<Zum::Grant> *row) mutable {
	if (!row) { wake(false); return; }
	new (row->ptr()) Zum::Grant{
	  .id = Zum::Bytes{id},
	  .userID = 42,
	  .expires = 1000,
	  .kind = Zum::GrantKind::Code,
	  .state = Zum::State::Active,
	  .issuer = "issuer",
	  .clientID = "browser",
	  .audience = "orders",
	  .authTime = 100,
	  .credentialID = Zum::Bytes{ZuBSpan{"credential"}},
	  .digest = Zum::Bytes{ZuBSpan{"code digest"}}
	};
	wake(row->commit());
      });
    });
  });
}

template <typename T>
static bool runSaga(TestDB *db, T data, ZdbSagaID id)
{
  using M = ZdbMSaga<Zum::Sagas>;
  ZmRef<M> saga = new M{};
  saga->init(ZuMv(data));
  ZmSemaphore completed;
  bool terminal = false;
  bool submitted = ZmBlock<bool>{}([
      db, id, saga = ZuMv(saga), &completed, &terminal
  ](auto wake) mutable {
    if (!db->saga(0, id, ZuMv(saga),
	[wake = ZuMv(wake)](bool ok) mutable { wake(ok); },
	[&completed, &terminal](bool ok) {
	  terminal = ok;
	  completed.post();
	})) wake(false);
  });
  if (!submitted) return false;
  completed.wait();
  return terminal;
}

static bool codeFamilyState(
    Zum::DBContext *context, ZuBSpan codeID, ZuBSpan familyID, bool complete)
{
  return ZmBlock<bool>{}([
      context, codeID, familyID, complete
  ](auto wake) mutable {
    context->grants->run(0, [
	context, codeID, familyID, complete, wake = ZuMv(wake)
    ]() mutable {
      context->grants->find<0>(0, ZuFwdTuple(codeID), [
	context, familyID, complete, wake = ZuMv(wake)
      ](ZdbRowRef<Zum::Grant> code) mutable {
	bool ok = code && code->data().kind == Zum::GrantKind::Code &&
	  code->data().state ==
	    (complete ? Zum::State::Consumed : Zum::State::Active) &&
	  (complete ? bool(code->data().owner) : !code->data().owner);
	context->grants->find<0>(0, ZuFwdTuple(familyID), [
	  complete, ok, wake = ZuMv(wake)
	](ZdbRowRef<Zum::Grant> family) mutable {
	  wake(ok && (complete ?
	    family && family->data().kind == Zum::GrantKind::Refresh &&
	      family->data().state == Zum::State::Active &&
	      !family->data().owner : !family));
	});
      });
    });
  });
}

static bool releaseToken(
    Zum::DBContext *context, ZuBSpan familyID, uint64_t authVersion,
    bool expected, int64_t now = 130)
{
  return ZmBlock<bool>{}([
    context, familyID, authVersion, expected, now
  ](auto wake) mutable {
    Zum::TokenResponse response{
      .accessToken = "signed.jwt", .refreshToken = "refresh",
      .scope = "read", .expiresIn = 300};
    Zum::tokenRelease(context, Zum::String{"issuer"},
      Zum::Bytes{familyID}, authVersion, now, ZuMv(response), [
	expected, wake = ZuMv(wake)
      ](bool ok, Zum::TokenResponse response) mutable {
	if (expected)
	  wake(ok && response.accessToken == "signed.jwt" &&
	    response.refreshToken == "refresh" && response.scope == "read" &&
	    response.expiresIn == 300);
	else
	  wake(!ok && !response.accessToken && !response.refreshToken &&
	    !response.scope && !response.expiresIn);
      });
  });
}

static bool enrollmentState(
    Zum::DBContext *context, ZuBSpan ceremonyID, Zum::UserID userID,
    ZuBSpan credentialID)
{
  return ZmBlock<bool>{}([
      context, ceremonyID, userID, credentialID
  ](auto wake) mutable {
    context->users->run(0, [
	context, ceremonyID, userID, credentialID,
	wake = ZuMv(wake)
    ]() mutable {
      context->users->find<0>(0, ZuFwdTuple(userID), [
	context, ceremonyID, credentialID,
	wake = ZuMv(wake)
      ](ZdbRowRef<Zum::User> user) mutable {
	bool ok = user && user->data().state == Zum::State::Active &&
	  !user->data().owner;
	context->creds->find<0>(0, ZuFwdTuple(credentialID), [
	  context, ceremonyID, ok,
	  wake = ZuMv(wake)
	](ZdbRowRef<Zum::Cred> cred) mutable {
	  ok &= cred && cred->data().state == Zum::State::Active &&
	    !cred->data().owner;
	  context->grants->find<0>(0, ZuFwdTuple(ceremonyID), [
	    ok, wake = ZuMv(wake)
	  ](ZdbRowRef<Zum::Grant> grant) mutable {
	    wake(ok && !grant);
	  });
	});
      });
    });
  });
}

static bool setKeyRetirement(
    Zum::DBContext *context, ZuCSpan id, int64_t retireAfter)
{
  return ZmBlock<bool>{}([context, id, retireAfter](auto wake) mutable {
    context->signKeys->run(0, [
      context, id, retireAfter, wake = ZuMv(wake)
    ]() mutable {
      context->signKeys->findUpd<0>(0, ZuFwdTuple(id), [
	wake = ZuMv(wake), retireAfter
      ](ZdbRow<Zum::SignKey> *row) mutable {
	if (!row) { wake(false); return; }
	row->data().retireAfter = retireAfter;
	wake(row->commit());
      });
    });
  });
}

static void enrollmentRuntime()
{
  ZuTestScope(enrollmentRuntime);
  auto config = dbConfig();
  ZiMultiplex mx{ZvMxParams{"mx", config->resolve("mx")}};
  ZuCheck(mx.start());
  ZmRef<ZdbMem::Store> store = new ZdbMem::Store{};
  ZmRef<TestDB> db = new TestDB{};
  db->requests = new Zum::Requests{};
  ZuCheck(db->requests->init(&mx, 5, 64));
  db->init(ZdbCf{config->resolve("zdb")}, &mx,
    ZdbHandler{.upFn = dbUp, .downFn = dbDown}, store);
  ZmRef<Zum::DBContext> context = Zum::registerSchema(db);
  ZuCheck(db->start());
  db->active.wait();
  ZuCheck(missingAssertion(context) == Zum::WebAuthnError::Ceremony);

  Ztls::Random rng;
  ZuCheck(rng.init());
  Zum::Grant authorization;
  authorization.id = Zum::Bytes{ZuBSpan{"authorization-id"}};
  authorization.issuer = "issuer";
  authorization.clientID = "browser";
  authorization.audience = "orders";
  authorization.redirectURI = "https://app/cb";
  authorization.scopeIDs.push(7);
  authorization.challenge = Zum::Bytes{ZuBSpan{"challenge"}};
  authorization.bindingDigest = Zum::Bytes{ZuBSpan{"browser binding"}};
  authorization.pkceChallenge = Zum::Bytes{ZuBSpan{
    "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"}};
  authorization.authVersion = 10;
  authorization.created = 100;
  authorization.expires = 160;
  authorization.kind = Zum::GrantKind::Ceremony;
  authorization.purpose = Zum::GrantPurpose::Authorization;
  authorization.state = Zum::State::Active;
  ZuCheck(insertIssuer(context, "issuer", 10));
  ZuCheck(insertRecord(context->signKeys, Zum::SignKey{
    .id = "token-key",
    .providerRef = "test-key",
    .publicJwk = "{\"kty\":\"EC\",\"kid\":\"token-key\"}",
    .notBefore = 100,
    .retireAfter = 1000,
    .state = Zum::State::Active
  }));
  auto persistedJWKS = ZmBlock<Zum::String>{}([
    &db, &context
  ](auto wake) mutable {
    Zum::jwksLoad(db->requests, Zm::now() + ZuTime{10},
      context, 100, 4, [wake = ZuMv(wake)](
	bool ok, Zum::String json) mutable {
      wake(ok ? ZuMv(json) : Zum::String{});
    });
  });
  ZuCheck(persistedJWKS ==
    "{\"keys\":[{\"kty\":\"EC\",\"kid\":\"token-key\"}]}");
  ZuCheck(insertRecord(context->users, Zum::User{
    .id = 41,
    .name = "code user",
    .handle = Zum::Bytes{ZuBSpan{"code handle"}},
    .state = Zum::State::Active}));
  ZuCheck(insertRecord(context->grants, Zum::Grant{
    .id = Zum::Bytes{ZuBSpan{"revocable"}},
    .kind = Zum::GrantKind::Refresh,
    .state = Zum::State::Active,
    .issuer = "issuer"}));
  ZuCheck(ZmBlock<int>{}([&db, &context](auto wake) mutable {
    Zum::grantRevoke(db->requests, Zm::now() + ZuTime{10},
      context, Zum::String{"issuer"},
      Zum::Bytes{ZuBSpan{"revocable"}}, Zum::String{"operator"}, 101,
      [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::AdminError::OK);
  ZuCheck(loadGrant(context, ZuBSpan{"revocable"}).state ==
    Zum::State::Revoked);
  ZuCheck(ZmBlock<int>{}([&db, &context](auto wake) mutable {
    Zum::grantRevoke(db->requests, Zm::now() + ZuTime{10},
      context, Zum::String{"issuer"},
      Zum::Bytes{ZuBSpan{"revocable"}}, Zum::String{"operator"}, 102,
      [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::AdminError::OK);
  ZuCheck(ZmBlock<bool>{}([&context](auto wake) mutable {
    context->audits->run(0, [context, wake = ZuMv(wake)]() mutable {
      context->audits->find<0>(0,
	ZuFwdTuple(uint64_t{0}, ZuCSpan{"issuer"}), [
	  context, wake = ZuMv(wake)
      ](ZdbRowRef<Zum::Audit> audit) mutable {
	context->issuers->find<0>(0, ZuFwdTuple(ZuCSpan{"issuer"}), [
	  audit = ZuMv(audit), wake = ZuMv(wake)
	](ZdbRowRef<Zum::Issuer> issuer) mutable {
	  wake(audit && audit->data().event == Zum::AuditEvent::Revocation &&
	    audit->data().actor == "operator" &&
	    audit->data().outcome == Zum::AuditOutcome::Success &&
	    issuer && issuer->data().nextAuditID == 1);
	});
      });
    });
  }));
  ZuCheck(ZmBlock<unsigned>{}([&db, &context](auto wake) mutable {
    Zum::grantCleanup(db->requests, Zm::now() + ZuTime{10},
      context, 1, 1, [wake = ZuMv(wake)](
	int error, unsigned removed) mutable {
      wake(error == Zum::AdminError::OK ? removed : 0);
    });
  }) == 1);
  ZuCheck(!loadGrant(context, ZuBSpan{"revocable"}).id);
  ZuCheck(storeAuthorization(context, ZuMv(authorization)));
  Zum::String authorizationCode;
  ZuCheck(!finishAuthorization(context, rng,
    Zum::Bytes{ZuBSpan{"authorization-id"}},
    Zum::Bytes{ZuBSpan{"wrong binding"}}, 41,
    ZuBSpan{"code credential"}, authorizationCode));
  ZuCheck(!authorizationCode);
  ZuCheck(finishAuthorization(context, rng,
    Zum::Bytes{ZuBSpan{"authorization-id"}},
    Zum::Bytes{ZuBSpan{"browser binding"}}, 41,
    ZuBSpan{"code credential"}, authorizationCode));
  ZuCheck(authorizationState(
    context, ZuBSpan{"authorization-id"}, authorizationCode,
    41, ZuBSpan{"code credential"}));
  Zum::String replayCode;
  ZuCheck(!finishAuthorization(context, rng,
    Zum::Bytes{ZuBSpan{"authorization-id"}},
    Zum::Bytes{ZuBSpan{"browser binding"}}, 41,
    ZuBSpan{"code credential"}, replayCode));
  ZuCheck(!replayCode);

  Zum::CodeFamily family;
  Zum::String refreshToken;
  ZuCheck(prepareCodeFamily(
    context, rng, authorizationCode, family, refreshToken));
  Zum::Bytes familyID = family.familyID;
  Zum::Bytes refreshID, refreshDigest;
  ZuCheck(Zum::opaqueParse(refreshToken, refreshID, refreshDigest) &&
    refreshID == family.familyID && refreshDigest == family.digest);
  ZuCheck(runSaga(db, ZuMv(family), ZdbSagaID{2}));
  ZuCheck(codeFamilyState(context,
    ZuBSpan{"authorization-id"}, familyID, true));
  ZuCheck(releaseToken(context, familyID, 10, true));
  ZuCheck(releaseToken(context, {}, 10, true));
  ZuCheck(releaseToken(context, familyID, 9, false));
  ZuCheck(releaseToken(context, familyID, 10, false, 1000));
  ZuCheck(releaseToken(context, ZuBSpan{"missing family"}, 10, false));
  Zum::String wrongRefresh = refreshToken;
  wrongRefresh[wrongRefresh.length() - 1] ^= 1;
  Zum::String nextRefresh;
  ZuCheck(rotateRefresh(context, rng, wrongRefresh, nextRefresh) ==
    Zum::RefreshRotate::Unknown && !nextRefresh);
  ZuCheck(refreshState(context, familyID, refreshDigest, 0,
    Zum::State::Active));
  ZuCheck(rotateRefresh(context, rng, refreshToken, nextRefresh) ==
    Zum::RefreshRotate::Rotated && nextRefresh);
  Zum::Bytes nextID, nextDigest;
  ZuCheck(Zum::opaqueParse(nextRefresh, nextID, nextDigest) &&
    nextID == familyID);
  ZuCheck(refreshState(context, familyID, nextDigest, 1,
    Zum::State::Active));
  Zum::String replayRefresh;
  ZuCheck(rotateRefresh(context, rng, refreshToken, replayRefresh) ==
    Zum::RefreshRotate::Reused && !replayRefresh);
  ZuCheck(refreshState(context, familyID, nextDigest, 1,
    Zum::State::Revoked));
  ZuCheck(releaseToken(context, familyID, 10, false));

  Zum::String bootstrap;
  Zum::BootstrapConfig bootstrapConfig{
    .issuer = "issuer", .now = 90, .expires = 1000};
  bootstrapConfig.roleIDs.push(7);
  ZuCheck(ZmBlock<bool>{}([
    &db, &context, &rng, &bootstrapConfig, &bootstrap
  ](auto wake) mutable {
    Zum::bootstrapIssue(db->requests, Zm::now() + ZuTime{10},
      context, rng, ZuMv(bootstrapConfig), [
	&bootstrap, wake = ZuMv(wake)
    ](bool ok, Zum::String capability) mutable {
      bootstrap = ZuMv(capability);
      wake(ok);
    });
  }) && bootstrap);
  Zum::BootstrapConfig duplicateBootstrap{
    .issuer = "issuer", .now = 91, .expires = 1000};
  ZuCheck(!ZmBlock<bool>{}([
    &db, &context, &rng, &duplicateBootstrap
  ](auto wake) mutable {
    Zum::bootstrapIssue(db->requests, Zm::now() + ZuTime{10},
      context, rng, ZuMv(duplicateBootstrap), [
	wake = ZuMv(wake)
    ](bool ok, Zum::String) mutable { wake(ok); });
  }));

  Zum::EnrollmentBeginResult enrollmentBegin;
  Zum::EnrollmentBeginConfig enrollmentConfig{
    .issuer = "issuer",
    .rpID = "example.com",
    .rpName = "Example",
    .name = "first user",
    .displayName = "First User",
    .label = "first passkey",
    .userID = 42,
    .now = 100,
    .expires = 1000,
    .timeout = 60000
  };
  enrollmentConfig.roleIDs.push(99);
  Zum::String replayCapability = bootstrap;
  auto replayConfig = enrollmentConfig;
  ZuCheck(ZmBlock<int>{}([
    &db, &context, &rng, &bootstrap, &enrollmentConfig, &enrollmentBegin
  ](auto wake) mutable {
    Zum::bootstrapBegin(db->requests, Zm::now() + ZuTime{10},
      context, rng, ZuMv(bootstrap),
      Zum::Bytes{ZuBSpan{"enrollment binding"}}, ZuMv(enrollmentConfig), [
	&enrollmentBegin, wake = ZuMv(wake)
      ](int error, Zum::EnrollmentBeginResult result) mutable {
	enrollmentBegin = ZuMv(result);
	wake(error);
      });
  }) == Zum::WebAuthnError::OK);
  ZuCheck(enrollmentBegin.ceremonyID.length() == 16 &&
    enrollmentBegin.options);
  auto enrollmentGrant = loadGrant(context, enrollmentBegin.ceremonyID);
  ZuCheck(enrollmentGrant.challenge && enrollmentGrant.userHandle &&
    enrollmentGrant.purpose == Zum::GrantPurpose::Bootstrap &&
    enrollmentGrant.roleIDs.length() == 1 && enrollmentGrant.roleIDs[0] == 7);
  ZuCheck(ZmBlock<int>{}([
    &db, &context, &rng, &replayCapability, &replayConfig
  ](auto wake) mutable {
    Zum::bootstrapBegin(db->requests, Zm::now() + ZuTime{10},
      context, rng, ZuMv(replayCapability),
      Zum::Bytes{ZuBSpan{"enrollment binding"}}, ZuMv(replayConfig), [
	wake = ZuMv(wake)
      ](int error, Zum::EnrollmentBeginResult) mutable { wake(error); });
  }) == Zum::WebAuthnError::Ceremony);
  Zum::Bytes passkeyHandle = enrollmentGrant.userHandle;
  Ztls::PK::SK_EC passkey{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  uint8_t passkeyPublic[Ztls::COSE::ES256::PublicKeySize];
  ZuCheck(Ztls::Backend::pkey_ec_export_public(passkey.key, passkeyPublic));
  Zum::RegistrationInput registrationInput;
  ZuCheck(makeRegistration(passkeyPublic, 5, "credential",
    enrollmentGrant.challenge,
    "https://example.com", "example.com", registrationInput));
  Zum::Bytes enrollmentID = enrollmentBegin.ceremonyID;
  ZuCheck(ZmBlock<int>{}([
    &db, &context, &enrollmentBegin, &registrationInput
  ](auto wake) mutable {
    Zum::enrollmentFinish(db->requests, Zm::now() + ZuTime{10},
      db, context, ZuMv(enrollmentBegin.ceremonyID),
      Zum::Bytes{ZuBSpan{"enrollment binding"}}, ZuMv(registrationInput),
      Zum::EnrollmentFinishConfig{
	.origin = "https://example.com",
	.rpID = "example.com",
	.credentialIDMax = 128,
	.now = 123
      }, [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::WebAuthnError::OK);
  ZuCheck(enrollmentState(
    context, enrollmentID, 42, ZuBSpan{"credential"}));
  auto principalAudit = loadAudit(context, 1, "issuer");
  auto credentialAudit = loadAudit(context, 2, "issuer");
  ZuCheck(principalAudit.event == Zum::AuditEvent::PrincipalChange &&
    principalAudit.outcome == Zum::AuditOutcome::Success &&
    principalAudit.subject == Zum::auditID(passkeyHandle) &&
    principalAudit.detail == "enrollment");
  ZuCheck(credentialAudit.event == Zum::AuditEvent::CredentialChange &&
    credentialAudit.outcome == Zum::AuditOutcome::Success &&
    credentialAudit.subject == Zum::auditID(passkeyHandle) &&
    credentialAudit.target == Zum::auditID(ZuBSpan{"credential"}) &&
    credentialAudit.detail == "enrollment");

  Zum::EnrollmentBeginResult addBegin;
  ZuCheck(ZmBlock<int>{}([
    &db, &context, &rng, &addBegin
  ](auto wake) mutable {
    Zum::credentialBegin(db->requests, Zm::now() + ZuTime{10},
      context, rng,
      Zum::Bytes{ZuBSpan{"credential binding"}},
      Zum::CredentialBeginConfig{
	.issuer = "issuer",
	.rpID = "example.com",
	.rpName = "Example",
	.displayName = "First User",
	.label = "second passkey",
	.userID = 42,
	.now = 124,
	.expires = 180,
	.timeout = 60000
      }, [&addBegin, wake = ZuMv(wake)](
	  int error, Zum::EnrollmentBeginResult result) mutable {
	addBegin = ZuMv(result);
	wake(error);
      });
  }) == Zum::WebAuthnError::OK);
  auto addGrant = loadGrant(context, addBegin.ceremonyID);
  ZuCheck(addGrant.purpose == Zum::GrantPurpose::AddCredential &&
    addGrant.userID == 42 && addGrant.userHandle == passkeyHandle);
  Ztls::PK::SK_EC secondPasskey{
    rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  uint8_t secondPublic[Ztls::COSE::ES256::PublicKeySize];
  ZuCheck(Ztls::Backend::pkey_ec_export_public(
    secondPasskey.key, secondPublic));
  ZuCheck(makeRegistration(secondPublic, 0, "credential-2",
    addGrant.challenge, "https://example.com", "example.com",
    registrationInput));
  Zum::Bytes addID = addBegin.ceremonyID;
  ZuCheck(ZmBlock<int>{}([
    &db, &context, &addBegin, &registrationInput
  ](auto wake) mutable {
    Zum::credentialFinish(db->requests, Zm::now() + ZuTime{10},
      db, context, ZuMv(addBegin.ceremonyID),
      Zum::Bytes{ZuBSpan{"credential binding"}},
      ZuMv(registrationInput), Zum::EnrollmentFinishConfig{
	.origin = "https://example.com",
	.rpID = "example.com",
	.credentialIDMax = 128,
	.now = 125
      }, [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::WebAuthnError::OK);
  ZuCheck(credentialCount(context, ZuBSpan{"credential-2"}, 0));
  ZuCheck(!loadGrant(context, addID).id);
  auto addAudit = loadAudit(context, 3, "issuer");
  ZuCheck(addAudit.event == Zum::AuditEvent::CredentialChange &&
    addAudit.outcome == Zum::AuditOutcome::Success &&
    addAudit.actor == Zum::auditID(passkeyHandle) &&
    addAudit.subject == Zum::auditID(passkeyHandle) &&
    addAudit.target == Zum::auditID(ZuBSpan{"credential-2"}) &&
    addAudit.detail == "add");

  Zum::Grant passkeyAuthorization;
  passkeyAuthorization.id =
    Zum::Bytes{ZuBSpan{"passkey-auth-id0"}};
  passkeyAuthorization.issuer = "issuer";
  passkeyAuthorization.clientID = "browser";
  passkeyAuthorization.audience = "orders";
  passkeyAuthorization.redirectURI = "https://app/cb";
  passkeyAuthorization.scopeIDs.push(7);
  passkeyAuthorization.challenge = Zum::Bytes{ZuBSpan{"challenge"}};
  passkeyAuthorization.bindingDigest =
    Zum::Bytes{ZuBSpan{"passkey binding"}};
  passkeyAuthorization.pkceChallenge = Zum::Bytes{ZuBSpan{
    "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"}};
  passkeyAuthorization.authVersion = 10;
  passkeyAuthorization.created = 125;
  passkeyAuthorization.expires = 160;
  passkeyAuthorization.kind = Zum::GrantKind::Ceremony;
  passkeyAuthorization.purpose = Zum::GrantPurpose::Authorization;
  passkeyAuthorization.state = Zum::State::Active;
  ZuCheck(storeAuthorization(context, ZuMv(passkeyAuthorization)));

  Zum::AssertionInput passkeyInput;
  ZuCheck(makeAssertion(rng, passkey, 6, passkeyInput));
  passkeyInput.userHandle = passkeyHandle;
  auto authenticated = storedAssertion(context, ZuMv(passkeyInput));
  ZuCheck(authenticated.error == Zum::WebAuthnError::OK &&
    authenticated.user.id == 42 && authenticated.grant.id ==
      ZuBSpan{"passkey-auth-id0"} &&
    authenticated.result.counter == Zum::CounterState::Advanced &&
    authenticated.result.signCount == 6 &&
    credentialCount(context, ZuBSpan{"credential"}, 6));

  ZuCheck(makeAssertion(rng, passkey, 6, passkeyInput));
  passkeyInput.userHandle = passkeyHandle;
  authenticated = storedAssertion(context, ZuMv(passkeyInput));
  ZuCheck(authenticated.error == Zum::WebAuthnError::Counter &&
    credentialCount(context, ZuBSpan{"credential"}, 6));

  Zum::String passkeyCode;
  ZuCheck(finishAuthorization(context, rng,
    Zum::Bytes{ZuBSpan{"passkey-auth-id0"}},
    Zum::Bytes{ZuBSpan{"passkey binding"}}, 42,
    ZuBSpan{"credential"}, passkeyCode));
  ZuCheck(authorizationState(
    context, ZuBSpan{"passkey-auth-id0"}, passkeyCode,
    42, ZuBSpan{"credential"}));

  ZuCheck(insertCode(context, ZuBSpan{"stale code"}));
  family = {};
  family.codeID = Zum::Bytes{ZuBSpan{"stale code"}};
  family.codeDigest = Zum::Bytes{ZuBSpan{"code digest"}};
  family.familyID = Zum::Bytes{ZuBSpan{"stale family"}};
  family.issuer = "issuer";
  family.userID = 42;
  family.clientID = "browser";
  family.credentialID = Zum::Bytes{ZuBSpan{"credential"}};
  family.audience = "orders";
  family.digest = Zum::Bytes{ZuBSpan{"refresh digest"}};
  family.authVersion = 9;
  family.authTime = family.created = 126;
  family.expires = 1000;
  ZuCheck(!runSaga(db, ZuMv(family), ZdbSagaID{3}));
  ZuCheck(codeFamilyState(
    context, ZuBSpan{"stale code"}, ZuBSpan{"stale family"}, false));

  ZuCheck(insertCode(context, ZuBSpan{"wrong code"}));
  family = {};
  family.authVersion = 10;
  family.codeID = Zum::Bytes{ZuBSpan{"wrong code"}};
  family.codeDigest = Zum::Bytes{ZuBSpan{"wrong digest"}};
  family.familyID = Zum::Bytes{ZuBSpan{"wrong family"}};
  family.issuer = "issuer";
  family.userID = 42;
  family.clientID = "browser";
  family.credentialID = Zum::Bytes{ZuBSpan{"credential"}};
  family.audience = "orders";
  family.digest = Zum::Bytes{ZuBSpan{"refresh digest"}};
  family.authTime = family.created = 127;
  family.expires = 1000;
  ZuCheck(!runSaga(db, ZuMv(family), ZdbSagaID{4}));
  ZuCheck(codeFamilyState(
    context, ZuBSpan{"wrong code"}, ZuBSpan{"wrong family"}, false));

  Zum::ActionID readAction = UINT32_MAX, writeAction = UINT32_MAX;
  ZuCheck(createAction(context, "orders.read", readAction) &&
    readAction == 0);
  ZuCheck(createAction(context, "orders.write", writeAction) &&
    writeAction == 1);
  Zum::ActionID duplicateAction = UINT32_MAX;
  ZuCheck(!createAction(context, "orders.read", duplicateAction) &&
    duplicateAction == UINT32_MAX);
  ZuCheck(actionState(context, readAction, "orders.read", 2, 12) &&
    actionState(context, writeAction, "orders.write", 2, 12));

  ZtBitmap roleActions{2U};
  roleActions.set(readAction).set(writeAction);
  ZuCheck(insertRecord(context->roles, Zum::Role{
    .id = 7, .name = "reader", .actions = ZuMv(roleActions),
    .state = Zum::State::Active}));
  Zum::IDVec scopeRoles;
  scopeRoles.push(7);
  ZuCheck(insertRecord(context->scopes, Zum::Scope{
    .id = 7, .audience = "orders", .name = "read",
    .roleIDs = ZuMv(scopeRoles), .state = Zum::State::Active}));
  Zum::Client browser;
  browser.id = "browser";
  browser.redirects.push("https://app/cb");
  browser.audiences.push("orders");
  browser.scopeIDs.push(7);
  browser.type = Zum::ClientType::Browser;
  browser.grants = Zum::ClientGrant::AuthorizationCode |
    Zum::ClientGrant::RefreshToken;
  browser.state = Zum::State::Active;
  ZuCheck(insertRecord(context->clients, Zum::Client{browser}));

  ZuCheck(insertRecord(context->users, Zum::User{
    .id = 44,
    .name = "oidc user",
    .handle = Zum::Bytes{ZuBSpan{"oidc handle"}},
    .state = Zum::State::Active,
    .oidcSub = "00u44"
  }));
  Ztls::PK::SK_EC upstreamKey{
    rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  uint8_t upstreamPublic[Ztls::COSE::ES256::PublicKeySize];
  ZuCheck(Ztls::Backend::pkey_ec_export_public(
    upstreamKey.key, upstreamPublic));
  auto x = base64URL(ZuBSpan{
    upstreamPublic + 1, Ztls::COSE::ES256::CoordinateSize});
  auto y = base64URL(ZuBSpan{upstreamPublic + 1 +
    Ztls::COSE::ES256::CoordinateSize, Ztls::COSE::ES256::CoordinateSize});
  Zum::String upstreamJWKS{
    "{\"keys\":[{\"kid\":\"upstream\",\"kty\":\"EC\","
    "\"crv\":\"P-256\",\"use\":\"sig\",\"alg\":\"ES256\",\"x\":\""};
  upstreamJWKS << x << "\",\"y\":\"" << y << "\"}]}";
  Zum::String upstreamToken;
  unsigned tokenReqs = 0, jwksReqs = 0;
  Zum::OIDCConfig oidcConfig;
  oidcConfig.issuer = "https://upstream.example";
  oidcConfig.authorizeEndpoint = "https://upstream.example/authorize";
  oidcConfig.tokenEndpoint = "https://upstream.example/token";
  oidcConfig.jwksEndpoint = "https://upstream.example/jwks";
  oidcConfig.clientID = "zum";
  oidcConfig.clientSecret = "secret";
  oidcConfig.redirectURI = "https://zum.example/oidc/callback";
  oidcConfig.oidcScopes.push("openid");
  oidcConfig.oidcScopes.push("groups");
  oidcConfig.roles = Zum::OIDCRoles::Mapped;
  oidcConfig.roleClaim = "groups";
  oidcConfig.roleMap.push(Zum::RoleMap{"Zum-Users", 7});
  Zum::Grant oidcCeremony;
  oidcCeremony.id = Zum::Bytes{ZuBSpan{"oidc-grant-id-00"}};
  oidcCeremony.issuer = "issuer";
  oidcCeremony.clientID = "browser";
  oidcCeremony.audience = "orders";
  oidcCeremony.redirectURI = "https://app/cb";
  oidcCeremony.scopeIDs.push(7);
  oidcCeremony.bindingDigest = Zum::Bytes{ZuBSpan{"oidc binding"}};
  oidcCeremony.pkceChallenge = Zum::Bytes{ZuBSpan{
    "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"}};
  oidcCeremony.oauthState = "oidc-return";
  oidcCeremony.oauthStatePresent = true;
  oidcCeremony.authVersion = 12;
  oidcCeremony.created = 190;
  oidcCeremony.expires = 260;
  oidcCeremony.kind = Zum::GrantKind::Ceremony;
  oidcCeremony.purpose = Zum::GrantPurpose::Authorization;
  oidcCeremony.state = Zum::State::Active;
  ZuCheck(storeAuthorization(context, ZuMv(oidcCeremony)));
  Zum::OIDC oidc;
  ZuCheck(oidc.init(db->requests->scheduler(), db->requests->sid(), context,
    oidcConfig, Zum::OIDCLimits{}, 4, 30, []() { return int64_t{200}; }, [
      &upstreamToken, &upstreamJWKS, &tokenReqs, &jwksReqs
    ](Zum::OIDCHTTPRequest request, Zum::OIDCHTTPDoneFn done) mutable {
      if (request.url == "https://upstream.example/token") {
        ++tokenReqs;
        Zum::String body{"{\"id_token\":\""};
        body << upstreamToken << "\"}";
        done(200, ZuMv(body));
      } else if (request.url == "https://upstream.example/jwks") {
        ++jwksReqs;
        done(200, upstreamJWKS);
      } else {
        done(500, Zum::String{});
      }
    }));
  Zum::String oidcLocation;
  ZuCheck(ZmBlock<bool>{}([&oidc, &oidcLocation](auto wake) mutable {
    if (!oidc.begin(Zum::Bytes{ZuBSpan{"oidc-grant-id-00"}}, [
        &oidcLocation, wake = ZuMv(wake)
      ](bool ok, Zum::String location) mutable {
        oidcLocation = ZuMv(location);
        wake(ok);
      })) wake(false);
  }));
  Zum::String oidcState, oidcNonce;
  ZuCheck(oidcParams(oidcLocation, oidcState, oidcNonce));
  Zum::String upstreamClaims{
    "{\"iss\":\"https://upstream.example\",\"sub\":\"00u44\","
    "\"aud\":\"zum\",\"nonce\":\""};
  upstreamClaims << oidcNonce <<
    "\",\"iat\":190,\"exp\":260,\"groups\":[\"Zum-Users\"]}";
  ZuCheck(signJWT(rng, upstreamKey,
    "{\"alg\":\"ES256\",\"kid\":\"upstream\",\"typ\":\"JWT\"}",
    upstreamClaims, upstreamToken));
  Zum::Bytes oidcGrant;
  Zum::User oidcUser;
  Zum::IDVec oidcRoles;
  int64_t oidcAuthTime = 0;
  Zum::String callback{"code=upstream-code&state="};
  callback << oidcState;
  ZuCheck(ZmBlock<bool>{}([
    &oidc, &callback, &oidcGrant, &oidcUser, &oidcRoles, &oidcAuthTime
  ](auto wake) mutable {
    if (!oidc.finish(ZuMv(callback), [
        &oidcGrant, &oidcUser, &oidcRoles, &oidcAuthTime,
        wake = ZuMv(wake)
      ](bool ok, Zum::Bytes grantID, Zum::User user,
          Zum::IDVec roles, int64_t authTime) mutable {
        oidcGrant = ZuMv(grantID);
        oidcUser = ZuMv(user);
        oidcRoles = ZuMv(roles);
        oidcAuthTime = authTime;
        wake(ok);
      })) wake(false);
  }));
  ZuCheck(oidcGrant == ZuBSpan{"oidc-grant-id-00"} &&
    oidcUser.id == 44 && oidcRoles.length() == 1 && oidcRoles[0] == 7 &&
    oidcAuthTime == 190 && tokenReqs == 1 && jwksReqs == 1);
  auto oidcAuthorized = finishOIDCAuthorization(
    db->requests, context, rng, Zum::Bytes{oidcGrant}, Zum::User{oidcUser},
    Zum::IDVec{oidcRoles}, readAction, oidcAuthTime);
  constexpr ZuCSpan oidcCodePrefix{"https://app/cb?code="};
  constexpr ZuCSpan oidcCodeSuffix{"&state=oidc-return"};
  ZuCheck(oidcAuthorized.error == Zum::AuthorizeIssue::OK &&
    oidcAuthorized.location.length() >
      oidcCodePrefix.length() + oidcCodeSuffix.length() &&
    (ZuCSpan{oidcAuthorized.location.data(), oidcCodePrefix.length()} ==
      oidcCodePrefix) &&
    (ZuCSpan{oidcAuthorized.location.data() +
      oidcAuthorized.location.length() - oidcCodeSuffix.length(),
      oidcCodeSuffix.length()} == oidcCodeSuffix));
  Zum::String oidcCode{ZuCSpan{
    oidcAuthorized.location.data() + oidcCodePrefix.length(),
    oidcAuthorized.location.length() - oidcCodePrefix.length() -
      oidcCodeSuffix.length()}};
  auto oidcScheduler = db->requests->scheduler();
  unsigned oidcSID = db->requests->sid();
  oidc.final();
  ZmSemaphore oidcDown;
  oidcScheduler->run([&oidcDown]() { oidcDown.post(); }, oidcSID);
  oidcDown.wait();

  Zum::String authorizeQuery{
    "response_type=code&client_id=browser&"};
  authorizeQuery <<
    "redirect_uri=https%3A%2F%2Fapp%2Fcb&scope=read&state=return&" <<
    "code_challenge=" <<
    "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM&" <<
    "code_challenge_method=S256";
  auto authorized = beginAuthorization(
    db->requests, context, rng, ZuMv(authorizeQuery));
  ZuCheck(authorized.error == Zum::AuthorizeIssue::OK &&
    authorized.result.ceremonyID && authorized.result.options &&
    !authorized.result.redirect);
  auto requestGrant = loadGrant(context, authorized.result.ceremonyID);
  ZuCheck(requestGrant.kind == Zum::GrantKind::Ceremony &&
    requestGrant.purpose == Zum::GrantPurpose::Authorization &&
    requestGrant.state == Zum::State::Active &&
    requestGrant.clientID == "browser" &&
    requestGrant.redirectURI == "https://app/cb" &&
    requestGrant.audience == "orders" &&
    requestGrant.scopeIDs.length() == 1 && requestGrant.scopeIDs[0] == 7 &&
    requestGrant.bindingDigest == ZuBSpan{"browser binding"} &&
    requestGrant.oauthState == "return" && requestGrant.oauthStatePresent &&
    requestGrant.authVersion == 12 &&
    requestGrant.expires == 260);

  Zum::Grant authorityGrant;
  authorityGrant.issuer = "issuer";
  authorityGrant.userID = 42;
  authorityGrant.clientID = "browser";
  authorityGrant.credentialID = Zum::Bytes{ZuBSpan{"credential"}};
  authorityGrant.audience = "orders";
  authorityGrant.scopeIDs.push(7);
  authorityGrant.roleIDs.push(7);
  authorityGrant.actions.length(2);
  authorityGrant.actions.set(readAction);
  auto authority = loadInteractiveAuthority(
    context, ZuMv(authorityGrant), Zum::Client{browser});
  ZuCheck(authority.error == Zum::ScopeError::OK &&
    authority.data.issuer.authVersion == 12 &&
    authority.data.selection.scope == "read" &&
    authority.data.actions[readAction] &&
    !authority.data.actions[writeAction] &&
    authority.data.actionRecords.length() == 1);

  Zum::Client workload;
  workload.id = "workload";
  workload.audiences.push("orders");
  workload.scopeIDs.push(7);
  workload.roleIDs.push(7);
  workload.type = Zum::ClientType::Confidential;
  workload.grants = Zum::ClientGrant::ClientCredentials;
  workload.state = Zum::State::Active;
  workload.secretDigest.length(Ztls::SecretHash::Size, false);
  ZuCheck(Ztls::secretHash(rng, ZuBSpan{"secret"},
    workload.secretDigest));
  ZuCheck(insertRecord(context->clients, Zum::Client{workload}));
  authority = loadClientAuthority(context, Zum::Client{workload});
  ZuCheck(authority.error == Zum::ScopeError::OK &&
    authority.data.selection.scope == "read" &&
    authority.data.actions[readAction] &&
    authority.data.actions[writeAction]);

  Ztls::PK::SK_EC tokenKey{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  uint8_t tokenPublic[Ztls::COSE::ES256::PublicKeySize];
  ZuCheck(Ztls::Backend::pkey_ec_export_public(tokenKey.key, tokenPublic));
  ZuCheck(setKeyRetirement(context, "token-key", 250));
  auto issued = issueTokenRequest(db->requests, db, context, rng,
    Zum::String{"grant_type=client_credentials"},
    Zum::String{"Basic d29ya2xvYWQ6c2VjcmV0"}, tokenKey, 200);
  ZuCheck(issued.error == Zum::OAuthError::ServerError &&
    !issued.response.accessToken);
  ZuCheck(setKeyRetirement(context, "token-key", 1000));
  Zum::String oidcCodeForm{"grant_type=authorization_code&code="};
  oidcCodeForm << oidcCode <<
    "&client_id=browser&redirect_uri=https%3A%2F%2Fapp%2Fcb" <<
    "&code_verifier=" <<
    "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
  issued = issueTokenRequest(db->requests, db, context, rng,
    ZuMv(oidcCodeForm), {}, tokenKey, 250);
  Zum::Principal principal;
  ZuCheck(issued.error == Zum::TokenIssue::OK &&
    issued.response.refreshToken && issued.response.scope == "read" &&
    Zum::jwtVerify(issued.response.accessToken, "token-key", "issuer",
      "orders", tokenPublic, 270, Zum::JWTLimits{}, principal));
  ZuCheck(principal.subject == base64URL(ZuBSpan{"oidc handle"}) &&
    principal.authMethod == "oidc" &&
    principal.authTime == 190 && principal.actions.length() == 1 &&
    principal.actions[0] == "orders.read");
  issued = issueTokenRequest(db->requests, db, context, rng,
    Zum::String{"grant_type=client_credentials"},
    Zum::String{"Basic d29ya2xvYWQ6c2VjcmV0"}, tokenKey, 200);
  ZuCheck(issued.error == Zum::TokenIssue::OK &&
    issued.response.scope == "read" && !issued.response.refreshToken &&
    issued.response.expiresIn == 60 && Zum::jwtVerify(
      issued.response.accessToken, "token-key", "issuer", "orders",
      tokenPublic, 220, Zum::JWTLimits{}, principal));
  ZuCheck(principal.subject == "workload" &&
    principal.clientID == "workload" && !principal.authMethod &&
    principal.actions.length() == 2);

  Zum::AssertionInput requestAssertion;
  ZuCheck(makeAssertion(rng, passkey, 7, requestGrant.challenge,
    "https://example.com", "example.com", requestAssertion));
  requestAssertion.userHandle = passkeyHandle;
  auto finished = finishAuthorization(db->requests, context, rng,
    Zum::Bytes{authorized.result.ceremonyID}, ZuMv(requestAssertion),
    readAction);
  constexpr ZuCSpan codePrefix{"https://app/cb?code="};
  constexpr ZuCSpan codeSuffix{"&state=return"};
  ZuCheck(finished.error == Zum::AuthorizeIssue::OK &&
    finished.location.length() > codePrefix.length() + codeSuffix.length() &&
    (ZuCSpan{finished.location.data(), codePrefix.length()} == codePrefix) &&
    (ZuCSpan{finished.location.data() + finished.location.length() -
      codeSuffix.length(), codeSuffix.length()} == codeSuffix));
  auto authAudit = loadAudit(context, 4, "issuer");
  ZuCheck(authAudit.event == Zum::AuditEvent::Authentication &&
    authAudit.outcome == Zum::AuditOutcome::Success &&
    authAudit.actor == "browser" &&
    authAudit.subject == Zum::auditID(passkeyHandle) &&
    authAudit.target == Zum::auditID(ZuBSpan{"credential"}));

  Zum::String repeatedAuthorizeQuery{
    "response_type=code&client_id=browser&"};
  repeatedAuthorizeQuery <<
    "redirect_uri=https%3A%2F%2Fapp%2Fcb&scope=read&state=return&" <<
    "code_challenge=" <<
    "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM&" <<
    "code_challenge_method=S256";
  auto repeated = beginAuthorization(
    db->requests, context, rng, ZuMv(repeatedAuthorizeQuery));
  auto repeatedGrant = loadGrant(context, repeated.result.ceremonyID);
  Zum::AssertionInput repeatedAssertion;
  ZuCheck(repeated.error == Zum::AuthorizeIssue::OK &&
    makeAssertion(rng, passkey, 7, repeatedGrant.challenge,
      "https://example.com", "example.com", repeatedAssertion));
  repeatedAssertion.userHandle = passkeyHandle;
  auto rejected = finishAuthorization(db->requests, context, rng,
    Zum::Bytes{repeated.result.ceremonyID}, ZuMv(repeatedAssertion),
    readAction);
  ZuCheck(rejected.error == Zum::OAuthError::AccessDenied);
  auto counterAudit = loadAudit(context, 5, "issuer");
  ZuCheck(counterAudit.event == Zum::AuditEvent::Authentication &&
    counterAudit.outcome == Zum::AuditOutcome::Failure &&
    counterAudit.detail == "counter regression" &&
    counterAudit.actor == "browser" &&
    counterAudit.subject == Zum::auditID(passkeyHandle) &&
    counterAudit.target == Zum::auditID(ZuBSpan{"credential"}));
  Zum::String browserCode{ZuCSpan{
    finished.location.data() + codePrefix.length(),
    finished.location.length() - codePrefix.length() - codeSuffix.length()}};
  Zum::Bytes codeID, codeDigest;
  ZuCheck(Zum::opaqueParse(browserCode, codeID, codeDigest));
  Zum::String codeForm{"grant_type=authorization_code&code="};
  codeForm << browserCode <<
    "&client_id=browser&redirect_uri=https%3A%2F%2Fapp%2Fcb" <<
    "&code_verifier=" <<
    "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
  issued = issueTokenRequest(
    db->requests, db, context, rng, ZuMv(codeForm), {}, tokenKey, 250);
  ZuCheck(issued.error == Zum::TokenIssue::OK &&
    issued.response.refreshToken && issued.response.scope == "read" &&
    issued.response.expiresIn == 60 && Zum::jwtVerify(
      issued.response.accessToken, "token-key", "issuer", "orders",
      tokenPublic, 270, Zum::JWTLimits{}, principal));
  ZuCheck(principal.authMethod == "passkey" && principal.authTime == 210 &&
    principal.actions.length() == 1 && principal.actions[0] == "orders.read");
  Zum::Bytes codeFamilyID, codeRefreshDigest;
  ZuCheck(Zum::opaqueParse(issued.response.refreshToken,
    codeFamilyID, codeRefreshDigest));
  auto codeFamily = loadGrant(context, codeFamilyID);
  ZuCheck(codeFamily.kind == Zum::GrantKind::Refresh &&
    codeFamily.state == Zum::State::Active &&
    codeFamily.digest == codeRefreshDigest &&
    codeFamily.authVersion == 12 && codeFamily.scopeIDs.length() == 1 &&
    codeFamily.scopeIDs[0] == 7 && codeFamily.actions[readAction] &&
    !codeFamily.actions[writeAction]);
  ZuCheck(!loadGrant(context, codeID).id);

  Zum::OpaqueToken refresh;
  ZuCheck(Zum::opaqueIssue(rng, refresh));
  ZtBitmap refreshActions{2U};
  refreshActions.set(readAction).set(writeAction);
  Zum::IDVec refreshScopes;
  refreshScopes.push(7);
  Zum::IDVec refreshRoles;
  refreshRoles.push(7);
  Zum::Grant refreshFamily{
    .id = refresh.id,
    .authVersion = 12,
    .userID = 42,
    .created = 200,
    .expires = 1000,
    .kind = Zum::GrantKind::Refresh,
    .state = Zum::State::Active,
    .issuer = "issuer",
    .clientID = "browser",
    .audience = "orders",
    .authTime = 123,
    .scopeIDs = ZuMv(refreshScopes),
    .roleIDs = ZuMv(refreshRoles),
    .actions = ZuMv(refreshActions),
    .credentialID = Zum::Bytes{ZuBSpan{"credential"}},
    .digest = refresh.digest
  };
  ZuCheck(insertRecord(context->grants, Zum::Grant{refreshFamily}));
  Zum::String refreshForm{"grant_type=refresh_token&refresh_token="};
  refreshForm << refresh.token << "&client_id=browser";
  issued = issueTokenRequest(
    db->requests, db, context, rng, ZuMv(refreshForm), {}, tokenKey, 300);
  ZuCheck(issued.error == Zum::TokenIssue::OK &&
    issued.response.refreshToken && issued.response.scope == "read" &&
    issued.response.expiresIn == 60 && Zum::jwtVerify(
      issued.response.accessToken, "token-key", "issuer", "orders",
      tokenPublic, 320, Zum::JWTLimits{}, principal));
  ZuCheck(principal.authMethod == "passkey" && principal.authTime == 123 &&
    principal.actions.length() == 2);
  Zum::Bytes rotatedID, rotatedDigest;
  ZuCheck(Zum::opaqueParse(
    issued.response.refreshToken, rotatedID, rotatedDigest) &&
    rotatedID == refresh.id);
  auto rotated = loadGrant(context, refresh.id);
  ZuCheck(rotated.generation == 1 && rotated.digest == rotatedDigest &&
    rotated.spent.length() == 1 && rotated.spent[0] == refresh.digest &&
    rotated.scopeIDs.length() == 1 && rotated.scopeIDs[0] == 7 &&
    rotated.actions[readAction] && rotated.actions[writeAction]);
  Zum::String reuseForm{"grant_type=refresh_token&refresh_token="};
  reuseForm << refresh.token << "&client_id=browser";
  issued = issueTokenRequest(
    db->requests, db, context, rng, ZuMv(reuseForm), {}, tokenKey, 330);
  ZuCheck(issued.error == Zum::OAuthError::InvalidGrant &&
    loadGrant(context, refresh.id).state == Zum::State::Revoked);
  auto reuseAudit = loadAudit(context, 6, "issuer");
  ZuCheck(reuseAudit.event == Zum::AuditEvent::RefreshReuse &&
    reuseAudit.outcome == Zum::AuditOutcome::Failure &&
    reuseAudit.actor == "browser" &&
    reuseAudit.target == Zum::auditID(refresh.id));

  issued = issueTokenRequest(db->requests, db, context, rng,
    Zum::String{"grant_type=client_credentials"},
    Zum::String{"Basic d29ya2xvYWQ6d3Jvbmc="}, tokenKey, 340);
  ZuCheck(issued.error == Zum::OAuthError::InvalidClient);
  auto clientAudit = loadAudit(context, 7, "issuer");
  ZuCheck(clientAudit.event == Zum::AuditEvent::ClientAuthentication &&
    clientAudit.outcome == Zum::AuditOutcome::Failure &&
    clientAudit.actor == "workload");

  Zum::String recoveryCapability;
  ZuCheck(ZmBlock<bool>{}([
    &db, &context, &rng, &recoveryCapability
  ](auto wake) mutable {
    Zum::recoveryIssue(db->requests, Zm::now() + ZuTime{10},
      db, context, rng, Zum::RecoveryIssueConfig{
      .issuer = "issuer",
      .actor = "administrator",
      .userID = 42,
      .now = 350,
      .expires = 410
    }, [&recoveryCapability, wake = ZuMv(wake)](
	bool ok, Zum::String capability) mutable {
      recoveryCapability = ZuMv(capability);
      wake(ok);
    });
  }));
  Zum::Bytes recoveryID, recoveryDigest;
  ZuCheck(Zum::opaqueParse(
    recoveryCapability, recoveryID, recoveryDigest));
  auto recoveredUser = loadUser(context, 42);
  auto recoveryGrant = loadGrant(context, recoveryID);
  ZuCheck(recoveredUser.state == Zum::State::Suspended &&
    recoveredUser.authVersion == 2 && !recoveredUser.owner);
  ZuCheck(recoveryGrant.kind == Zum::GrantKind::Capability &&
    recoveryGrant.purpose == Zum::GrantPurpose::Recovery &&
    recoveryGrant.state == Zum::State::Active && !recoveryGrant.owner &&
    recoveryGrant.userID == 42 && recoveryGrant.userVersion == 2 &&
    Ztls::ctEqual(recoveryGrant.digest, recoveryDigest));
  ZuCheck(releaseToken(context, codeFamilyID, 12, false, 350));
  auto recoveryAudit = loadAudit(context, 8, "issuer");
  ZuCheck(recoveryAudit.event == Zum::AuditEvent::PrincipalChange &&
    recoveryAudit.outcome == Zum::AuditOutcome::Success &&
    recoveryAudit.actor == "administrator" &&
    recoveryAudit.subject == Zum::auditID(passkeyHandle) &&
    recoveryAudit.detail == "recovery suspended");

  Zum::EnrollmentBeginResult recoveryBegin;
  ZuCheck(ZmBlock<int>{}([
    &db, &context, &rng, &recoveryCapability, &recoveryBegin
  ](auto wake) mutable {
    Zum::recoveryBegin(db->requests, Zm::now() + ZuTime{10},
      context, rng, ZuMv(recoveryCapability),
      Zum::Bytes{ZuBSpan{"recovery binding"}}, Zum::RecoveryBeginConfig{
	.issuer = "issuer",
	.rpID = "example.com",
	.rpName = "Example",
	.displayName = "First User",
	.label = "recovery passkey",
	.now = 351,
	.expires = 400,
	.timeout = 60000
      }, [&recoveryBegin, wake = ZuMv(wake)](
	  int error, Zum::EnrollmentBeginResult result) mutable {
	recoveryBegin = ZuMv(result);
	wake(error);
      });
  }) == Zum::WebAuthnError::OK);
  recoveryGrant = loadGrant(context, recoveryBegin.ceremonyID);
  ZuCheck(recoveryGrant.kind == Zum::GrantKind::Ceremony &&
    recoveryGrant.purpose == Zum::GrantPurpose::Recovery &&
    recoveryGrant.state == Zum::State::Active &&
    recoveryGrant.userID == 42 && recoveryGrant.userVersion == 2 &&
    recoveryGrant.userHandle && recoveryGrant.userHandle != passkeyHandle &&
    recoveryGrant.actor == "administrator" && !recoveryGrant.digest);
  Zum::Bytes recoveryHandle = recoveryGrant.userHandle;
  Ztls::PK::SK_EC recoveryPasskey{
    rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  uint8_t recoveryPublic[Ztls::COSE::ES256::PublicKeySize];
  ZuCheck(Ztls::Backend::pkey_ec_export_public(
    recoveryPasskey.key, recoveryPublic));
  ZuCheck(makeRegistration(recoveryPublic, 1, "recovery-credential",
    recoveryGrant.challenge, "https://example.com", "example.com",
    registrationInput));
  ZuCheck(ZmBlock<int>{}([
    &db, &context, &recoveryBegin, &registrationInput
  ](auto wake) mutable {
    Zum::recoveryFinish(db->requests, Zm::now() + ZuTime{10},
      db, context, ZuMv(recoveryBegin.ceremonyID),
      Zum::Bytes{ZuBSpan{"recovery binding"}}, ZuMv(registrationInput),
      Zum::EnrollmentFinishConfig{
	.origin = "https://example.com",
	.rpID = "example.com",
	.credentialIDMax = 128,
	.now = 352
      }, [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::WebAuthnError::OK);
  recoveredUser = loadUser(context, 42);
  ZuCheck(recoveredUser.state == Zum::State::Active &&
    recoveredUser.authVersion == 2 && recoveredUser.handle == recoveryHandle &&
    !recoveredUser.owner);
  auto recoveryCred = loadCred(context, ZuBSpan{"recovery-credential"});
  ZuCheck(recoveryCred.state == Zum::State::Active &&
    recoveryCred.userID == 42 && recoveryCred.userVersion == 2 &&
    recoveryCred.signCount == 1 && !recoveryCred.owner);
  ZuCheck(!loadGrant(context, recoveryID).id);
  auto recoveryCompleteAudit = loadAudit(context, 9, "issuer");
  auto recoveryCredentialAudit = loadAudit(context, 10, "issuer");
  ZuCheck(recoveryCompleteAudit.event == Zum::AuditEvent::PrincipalChange &&
    recoveryCompleteAudit.actor == "administrator" &&
    recoveryCompleteAudit.subject == Zum::auditID(recoveryHandle) &&
    recoveryCompleteAudit.detail == "recovery completed");
  ZuCheck(recoveryCredentialAudit.event ==
      Zum::AuditEvent::CredentialChange &&
    recoveryCredentialAudit.actor == "administrator" &&
    recoveryCredentialAudit.subject == Zum::auditID(recoveryHandle) &&
    recoveryCredentialAudit.target ==
      Zum::auditID(ZuBSpan{"recovery-credential"}) &&
    recoveryCredentialAudit.detail == "recovery");
  ZuCheck(ZmBlock<unsigned>{}([&db, &context](auto wake) mutable {
    Zum::auditCleanup(db->requests, Zm::now() + ZuTime{10},
      context, 101, 1, [wake = ZuMv(wake)](
	int error, unsigned removed) mutable {
      wake(error == Zum::AdminError::OK ? removed : 0);
    });
  }) == 1);
  ZuCheck(!loadAudit(context, 0, "issuer").issuer);

  Zum::String enrollmentQuery{
    "response_type=code&client_id=browser&"};
  enrollmentQuery <<
    "redirect_uri=https%3A%2F%2Fapp%2Fcb&scope=read&state=enroll&" <<
    "code_challenge=" <<
    "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM&" <<
    "code_challenge_method=S256";
  auto enrollmentAuthorization = beginAuthorization(
    db->requests, context, rng, ZuMv(enrollmentQuery), 420);
  auto enrollmentAuthorizationGrant = loadGrant(
    context, enrollmentAuthorization.result.ceremonyID);
  ZuCheck(enrollmentAuthorization.error == Zum::AuthorizeIssue::OK &&
    enrollmentAuthorizationGrant.kind == Zum::GrantKind::Ceremony &&
    enrollmentAuthorizationGrant.purpose == Zum::GrantPurpose::Authorization);

  Zum::EnrollmentBeginConfig browserEnrollment{
    .issuer = "issuer",
    .rpID = "example.com",
    .rpName = "Example",
    .name = "new user",
    .displayName = "New User",
    .label = "first passkey",
    .userID = 43,
    .now = 421,
    .expires = 480,
    .timeout = 60000
  };
  browserEnrollment.roleIDs.push(7);
  Zum::EnrollmentBeginResult browserEnrollmentBegin;
  ZuCheck(ZmBlock<int>{}([
    &db, &context, &rng, &browserEnrollment, &browserEnrollmentBegin
  ](auto wake) mutable {
    Zum::enrollmentBegin(db->requests, Zm::now() + ZuTime{10},
      context, rng,
      Zum::Bytes{ZuBSpan{"browser binding"}}, ZuMv(browserEnrollment), [
	&browserEnrollmentBegin, wake = ZuMv(wake)
      ](int error, Zum::EnrollmentBeginResult result) mutable {
	browserEnrollmentBegin = ZuMv(result);
	wake(error);
      });
  }) == Zum::WebAuthnError::OK);
  auto browserEnrollmentGrant = loadGrant(
    context, browserEnrollmentBegin.ceremonyID);
  Ztls::PK::SK_EC browserPasskey{
    rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  uint8_t browserPublic[Ztls::COSE::ES256::PublicKeySize];
  ZuCheck(Ztls::Backend::pkey_ec_export_public(
    browserPasskey.key, browserPublic));
  ZuCheck(makeRegistration(browserPublic, 0, "browser-credential",
    browserEnrollmentGrant.challenge, "https://example.com", "example.com",
    registrationInput));
  ZuCheck(ZmBlock<int>{}([
    &db, &context, &browserEnrollmentBegin, &registrationInput
  ](auto wake) mutable {
    Zum::enrollmentFinish(db->requests, Zm::now() + ZuTime{10}, db, context,
      ZuMv(browserEnrollmentBegin.ceremonyID),
      Zum::Bytes{ZuBSpan{"browser binding"}}, ZuMv(registrationInput),
      Zum::EnrollmentFinishConfig{
	.origin = "https://example.com",
	.rpID = "example.com",
	.credentialIDMax = 128,
	.now = 422
      }, [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::WebAuthnError::OK);
  ZuCheck(loadGrant(context,
    enrollmentAuthorization.result.ceremonyID).kind ==
      Zum::GrantKind::Ceremony);
  Zum::AssertionInput enrollmentAssertion;
  ZuCheck(makeAssertion(rng, browserPasskey, 1,
    enrollmentAuthorizationGrant.challenge,
    "https://example.com", "example.com", enrollmentAssertion));
  enrollmentAssertion.credentialID =
    Zum::Bytes{ZuBSpan{"browser-credential"}};
  enrollmentAssertion.userHandle = browserEnrollmentGrant.userHandle;
  auto enrollmentFinished = finishAuthorization(
    db->requests, context, rng,
    ZuMv(enrollmentAuthorization.result.ceremonyID),
    ZuMv(enrollmentAssertion), readAction, 423);
  ZuCheck(enrollmentFinished.error == Zum::AuthorizeIssue::OK &&
    enrollmentFinished.location.find<"state=enroll">() >= 0);

  ZuCheck(ZmBlock<int>{}([&db, &context](auto wake) mutable {
    Zum::signKeyAdd(db->requests, Zm::now() + ZuTime{10},
      context, Zum::String{"issuer"},
      Zum::String{"operator"}, Zum::SignKey{
	.id = "next-key",
	.providerRef = "next-provider-key",
	.publicJwk = "{\"kty\":\"EC\",\"kid\":\"next-key\"}",
	.notBefore = 430,
	.state = Zum::State::Active
      }, 430, [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::AdminError::OK);
  ZuCheck(ZmBlock<int>{}([&db, &context](auto wake) mutable {
    Zum::signKeyRetire(db->requests, Zm::now() + ZuTime{10},
      context, Zum::String{"issuer"},
      Zum::String{"operator"}, Zum::String{"next-key"}, 500, 431,
      [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::AdminError::OK);
  auto addedKeyAudit = loadAudit(context, 14, "issuer");
  auto retiredKeyAudit = loadAudit(context, 15, "issuer");
  ZuCheck(addedKeyAudit.event == Zum::AuditEvent::KeyRotation &&
    addedKeyAudit.actor == "operator" &&
    addedKeyAudit.target == "next-key" && addedKeyAudit.detail == "added");
  ZuCheck(retiredKeyAudit.event == Zum::AuditEvent::KeyRotation &&
    retiredKeyAudit.actor == "operator" &&
    retiredKeyAudit.target == "next-key" &&
    retiredKeyAudit.detail == "retiring");
  auto retiredJWKS = ZmBlock<Zum::String>{}([
    &db, &context
  ](auto wake) mutable {
    Zum::jwksLoad(db->requests, Zm::now() + ZuTime{10},
      context, 501, 4, [wake = ZuMv(wake)](
	bool ok, Zum::String json) mutable {
      wake(ok ? ZuMv(json) : Zum::String{});
    });
  });
  ZuCheck(retiredJWKS.find<"next-key">() < 0 &&
    retiredJWKS.find<"token-key">() >= 0);

  Zum::IDVec missingRole;
  missingRole.push(999);
  ZuCheck(ZmBlock<int>{}([
    &db, &context, &rng, &missingRole
  ](auto wake) mutable {
    Zum::userRoles(db->requests, Zm::now() + ZuTime{10},
      db, context, rng, Zum::String{"issuer"},
      Zum::String{"operator"}, 43, ZuMv(missingRole), 439,
      [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::AdminError::Invalid);
  auto unchangedUser = loadUser(context, 43);
  ZuCheck(unchangedUser.roleIDs.length() == 1 &&
    unchangedUser.roleIDs[0] == 7 && unchangedUser.updated == 422);
  ZuCheck(actionState(context, readAction, "orders.read", 2, 12));

  Zum::IDVec noRoles;
  ZuCheck(ZmBlock<int>{}([
    &db, &context, &rng, &noRoles
  ](auto wake) mutable {
    Zum::userRoles(db->requests, Zm::now() + ZuTime{10},
      db, context, rng, Zum::String{"issuer"},
      Zum::String{"operator"}, 43, ZuMv(noRoles), 440,
      [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::AdminError::OK);
  auto changedUser = loadUser(context, 43);
  ZuCheck(!changedUser.roleIDs && changedUser.state == Zum::State::Active &&
    changedUser.updated == 440 && !changedUser.owner);
  ZuCheck(actionState(context, readAction, "orders.read", 2, 13));
  auto rolesAudit = loadAudit(context, 16, "issuer");
  ZuCheck(rolesAudit.event == Zum::AuditEvent::RBACChange &&
    rolesAudit.actor == "operator" && rolesAudit.detail == "roles");

  ZuCheck(ZmBlock<int>{}([&db, &context, &rng](auto wake) mutable {
    Zum::userState(db->requests, Zm::now() + ZuTime{10},
      db, context, rng, Zum::String{"issuer"},
      Zum::String{"operator"}, 43, Zum::State::Disabled, 441,
      [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::AdminError::OK);
  changedUser = loadUser(context, 43);
  ZuCheck(changedUser.state == Zum::State::Disabled &&
    changedUser.updated == 441 && !changedUser.owner);
  ZuCheck(actionState(context, readAction, "orders.read", 2, 14));
  auto stateAudit = loadAudit(context, 17, "issuer");
  ZuCheck(stateAudit.event == Zum::AuditEvent::PrincipalChange &&
    stateAudit.actor == "operator" && stateAudit.detail == "state");

  Zum::ActionID deleteAction = UINT32_MAX;
  ZuCheck(ZmBlock<int>{}([
    &db, &context, &deleteAction
  ](auto wake) mutable {
    Zum::actionAdd(db->requests, Zm::now() + ZuTime{10},
      context, Zum::String{"issuer"},
      Zum::String{"operator"}, Zum::String{"orders.delete"}, 442, [
	&deleteAction, wake = ZuMv(wake)
      ](int error, Zum::ActionID id) mutable {
	deleteAction = id;
	wake(error);
      });
  }) == Zum::AdminError::OK && deleteAction == 2);
  ZuCheck(actionState(context, deleteAction, "orders.delete", 3, 15));
  auto actionAudit = loadAudit(context, 18, "issuer");
  ZuCheck(actionAudit.event == Zum::AuditEvent::RBACChange &&
    actionAudit.actor == "operator" && actionAudit.target == "orders.delete" &&
    actionAudit.detail == "action added");

  ZtBitmap invalidRoleActions{64U};
  invalidRoleActions.set(50);
  ZuCheck(ZmBlock<int>{}([
    &db, &context, &rng, &invalidRoleActions
  ](auto wake) mutable {
    Zum::roleActions(db->requests, Zm::now() + ZuTime{10},
      db, context, rng, Zum::String{"issuer"},
      Zum::String{"operator"}, 7, ZuMv(invalidRoleActions), 443,
      [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::AdminError::Invalid);
  auto changedRole = loadRole(context, 7);
  ZuCheck(changedRole.actions[readAction] &&
    changedRole.actions[writeAction] && !changedRole.owner);
  ZuCheck(actionState(context, deleteAction, "orders.delete", 3, 15));

  ZtBitmap changedRoleActions{3U};
  changedRoleActions.set(readAction).set(deleteAction);
  ZuCheck(ZmBlock<int>{}([
    &db, &context, &rng, &changedRoleActions
  ](auto wake) mutable {
    Zum::roleActions(db->requests, Zm::now() + ZuTime{10},
      db, context, rng, Zum::String{"issuer"},
      Zum::String{"operator"}, 7, ZuMv(changedRoleActions), 444,
      [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::AdminError::OK);
  changedRole = loadRole(context, 7);
  ZuCheck(changedRole.actions[readAction] &&
    !changedRole.actions[writeAction] && changedRole.actions[deleteAction] &&
    changedRole.state == Zum::State::Active && !changedRole.owner);
  ZuCheck(actionState(context, deleteAction, "orders.delete", 3, 16));
  auto roleActionsAudit = loadAudit(context, 19, "issuer");
  ZuCheck(roleActionsAudit.event == Zum::AuditEvent::RBACChange &&
    roleActionsAudit.actor == "operator" &&
    roleActionsAudit.target == "reader" &&
    roleActionsAudit.detail == "role actions");

  ZuCheck(ZmBlock<int>{}([&db, &context, &rng](auto wake) mutable {
    Zum::roleState(db->requests, Zm::now() + ZuTime{10},
      db, context, rng, Zum::String{"issuer"},
      Zum::String{"operator"}, 7, Zum::State::Disabled, 445,
      [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::AdminError::OK);
  changedRole = loadRole(context, 7);
  ZuCheck(changedRole.state == Zum::State::Disabled && !changedRole.owner);
  ZuCheck(actionState(context, deleteAction, "orders.delete", 3, 17));
  auto roleStateAudit = loadAudit(context, 20, "issuer");
  ZuCheck(roleStateAudit.event == Zum::AuditEvent::RBACChange &&
    roleStateAudit.actor == "operator" && roleStateAudit.target == "reader" &&
    roleStateAudit.detail == "role state");

  ZuCheck(ZmBlock<int>{}([&db, &context, &rng](auto wake) mutable {
    Zum::credentialState(db->requests, Zm::now() + ZuTime{10},
      db, context, rng, Zum::String{"issuer"},
      Zum::String{"operator"},
      Zum::Bytes{ZuBSpan{"browser-credential"}}, Zum::State::Revoked, 446,
      [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::AdminError::OK);
  auto revokedCred = loadCred(context, ZuBSpan{"browser-credential"});
  ZuCheck(revokedCred.state == Zum::State::Revoked &&
    revokedCred.updated == 446 && !revokedCred.owner);
  ZuCheck(actionState(context, deleteAction, "orders.delete", 3, 18));
  auto credStateAudit = loadAudit(context, 21, "issuer");
  ZuCheck(credStateAudit.event == Zum::AuditEvent::CredentialChange &&
    credStateAudit.actor == "operator" &&
    credStateAudit.target ==
      Zum::auditID(ZuBSpan{"browser-credential"}) &&
    credStateAudit.detail == "state");
  ZuCheck(ZmBlock<int>{}([&db, &context, &rng](auto wake) mutable {
    Zum::credentialState(db->requests, Zm::now() + ZuTime{10},
      db, context, rng, Zum::String{"issuer"},
      Zum::String{"operator"},
      Zum::Bytes{ZuBSpan{"browser-credential"}}, Zum::State::Active, 447,
      [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::AdminError::Invalid);

  ZuCheck(ZmBlock<int>{}([&db, &context, &rng](auto wake) mutable {
    Zum::scopeState(db->requests, Zm::now() + ZuTime{10},
      db, context, rng, Zum::String{"issuer"},
      Zum::String{"operator"}, 7, Zum::State::Disabled, 448,
      [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::AdminError::OK);
  auto disabledScope = loadScope(context, 7);
  ZuCheck(disabledScope.state == Zum::State::Disabled &&
    !disabledScope.owner);
  ZuCheck(actionState(context, deleteAction, "orders.delete", 3, 19));
  auto scopeStateAudit = loadAudit(context, 22, "issuer");
  ZuCheck(scopeStateAudit.event == Zum::AuditEvent::RBACChange &&
    scopeStateAudit.actor == "operator" && scopeStateAudit.target == "read" &&
    scopeStateAudit.detail == "scope state");

  Zum::Bytes nextSecretDigest;
  nextSecretDigest.length(Ztls::SecretHash::Size, false);
  ZuCheck(Ztls::secretHash(
    rng, ZuBSpan{"next workload secret"}, nextSecretDigest));
  ZuCheck(ZmBlock<int>{}([
    &db, &context, &rng, &nextSecretDigest
  ](auto wake) mutable {
    Zum::clientSecretDigest(db->requests, Zm::now() + ZuTime{10},
      db, context, rng, Zum::String{"issuer"},
      Zum::String{"operator"}, Zum::String{"workload"},
      ZuMv(nextSecretDigest), 449,
      [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::AdminError::OK);
  auto changedClient = loadClient(context, "workload");
  ZuCheck(changedClient.updated == 449 && !changedClient.owner &&
    Ztls::secretVerify(
      changedClient.secretDigest, ZuBSpan{"next workload secret"}) &&
    !Ztls::secretVerify(changedClient.secretDigest, ZuBSpan{"secret"}));
  ZuCheck(actionState(context, deleteAction, "orders.delete", 3, 20));
  auto clientSecretAudit = loadAudit(context, 23, "issuer");
  ZuCheck(clientSecretAudit.event == Zum::AuditEvent::PrincipalChange &&
    clientSecretAudit.actor == "operator" &&
    clientSecretAudit.target == "workload" &&
    clientSecretAudit.detail == "client secret");

  ZuCheck(ZmBlock<int>{}([&db, &context, &rng](auto wake) mutable {
    Zum::clientState(db->requests, Zm::now() + ZuTime{10},
      db, context, rng, Zum::String{"issuer"},
      Zum::String{"operator"}, Zum::String{"workload"},
      Zum::State::Revoked, 450,
      [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::AdminError::OK);
  changedClient = loadClient(context, "workload");
  ZuCheck(changedClient.state == Zum::State::Revoked &&
    changedClient.updated == 450 && !changedClient.owner);
  ZuCheck(actionState(context, deleteAction, "orders.delete", 3, 21));
  auto clientStateAudit = loadAudit(context, 24, "issuer");
  ZuCheck(clientStateAudit.event == Zum::AuditEvent::PrincipalChange &&
    clientStateAudit.actor == "operator" &&
    clientStateAudit.target == "workload" &&
    clientStateAudit.detail == "client state");

  ZuCheck(ZmBlock<int>{}([
    &db, &context, &rng, &readAction
  ](auto wake) mutable {
    Zum::actionState(db->requests, Zm::now() + ZuTime{10},
      db, context, rng, Zum::String{"issuer"},
      Zum::String{"operator"}, readAction, Zum::State::Disabled, 451,
      [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::AdminError::OK);
  auto disabledAction = loadAction(context, readAction);
  ZuCheck(disabledAction.state == Zum::State::Disabled &&
    disabledAction.name == "orders.read" && !disabledAction.owner);
  auto issuerAfterAction = ZmBlock<Zum::Issuer>{}([
    context
  ](auto wake) mutable {
    context->issuers->run(0, [context, wake = ZuMv(wake)]() mutable {
      context->issuers->find<0>(0, ZuFwdTuple(ZuCSpan{"issuer"}), [
	wake = ZuMv(wake)
      ](ZdbRowRef<Zum::Issuer> row) mutable {
	wake(row ? Zum::Issuer{row->data()} : Zum::Issuer{});
      });
    });
  });
  ZuCheck(issuerAfterAction.authVersion == 22);
  auto actionStateAudit = loadAudit(context, 25, "issuer");
  ZuCheck(actionStateAudit.event == Zum::AuditEvent::RBACChange &&
    actionStateAudit.actor == "operator" &&
    actionStateAudit.target == "orders.read" &&
    actionStateAudit.detail == "action state");

  Zum::OpaqueToken revokeToken;
  ZuCheck(Zum::opaqueIssue(rng, revokeToken));
  ZuCheck(insertRecord(context->grants, Zum::Grant{
    .id = revokeToken.id,
    .expires = 1000,
    .kind = Zum::GrantKind::Refresh,
    .state = Zum::State::Active,
    .issuer = "issuer",
    .clientID = "browser",
    .digest = revokeToken.digest
  }));
  Zum::String revokeForm{"token="};
  revokeForm << revokeToken.token << "&client_id=browser";
  ZuCheck(ZmBlock<int>{}([
    &db, &context, &revokeForm
  ](auto wake) mutable {
    Zum::revokeRequest(db->requests, Zm::now() + ZuTime{10}, context,
      ZuMv(revokeForm), {}, Zum::RevokeConfig{
	.issuer = "issuer", .now = 452
      }, [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::RevokeIssue::OK);
  ZuCheck(loadGrant(context, revokeToken.id).state == Zum::State::Revoked);
  auto revokeAudit = loadAudit(context, 26, "issuer");
  ZuCheck(revokeAudit.event == Zum::AuditEvent::Revocation &&
    revokeAudit.outcome == Zum::AuditOutcome::Success &&
    revokeAudit.actor == "browser" &&
    revokeAudit.subject == Zum::auditID(revokeToken.id));
  Zum::OpaqueToken unknownToken;
  ZuCheck(Zum::opaqueIssue(rng, unknownToken));
  Zum::String unknownForm{"token="};
  unknownForm << unknownToken.token << "&client_id=browser";
  ZuCheck(ZmBlock<int>{}([
    &db, &context, &unknownForm
  ](auto wake) mutable {
    Zum::revokeRequest(db->requests, Zm::now() + ZuTime{10}, context,
      ZuMv(unknownForm), {}, Zum::RevokeConfig{
	.issuer = "issuer", .now = 453
      }, [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::RevokeIssue::OK);

  ZmSemaphore requestsDown;
  db->requests->deactivate([&requestsDown]() { requestsDown.post(); });
  requestsDown.wait();
  ZuCheck(db->stop());
  context = {};
  db->final();
  db->requests = {};
  db = {};
  store = {};
  ZuCheck(mx.stop());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(requests);
  ZuTestCall(oauthForms);
  ZuTestCall(oidcRoles);
  ZuTestCall(httpRoutes);
  ZuTestCall(redirects);
  ZuTestCall(pkce);
  ZuTestCall(opaque);
  ZuTestCall(actions);
  ZuTestCall(records);
  ZuTestCall(discovery);
  ZuTestCall(refresh);
  ZuTestCall(jwt);
  ZuTestCall(webAuthn);
  ZuTestCall(webAuthnOptions);
  ZuTestCall(webAuthnInput);
  ZuTestCall(enrollmentSaga);
  ZuTestCall(enrollmentRuntime);
  return 0;
}
