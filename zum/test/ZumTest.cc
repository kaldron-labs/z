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
#include <zlib/ZiLog.hh>

#include <zlib/ZvMxParams.hh>

#include <zlib/ZdbMemStore.hh>

#include <zlib/zumd_admin.hh>
#include <zlib/zumd_authorize.hh>
#include <zlib/zumd_db.hh>
#include <zlib/zumd_db_ops.hh>
#include <zlib/zumd_key_db.hh>
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

using namespace ZuTestUtil;

ZuAssert((Zdb_::SagaBasesValid_<
  Zum::DBContext, Zum::SagaCatalog::List>{}));

static Zum::String testJwk(ZuCSpan id)
{
  // Public P-256 generator point; no test private material is published.
  Zum::String json{"{\"kty\":\"EC\",\"crv\":\"P-256\",\"kid\":\""};
  json << id << "\",\"x\":\"axfR8uEsQkf4vOblY6RA8ncDfYEt6zOg9KE5RdiYwpY\","
    "\"y\":\"T-NC4v4af5uO5-tKfA-eFivOM1drMV7Oy7ZAaDe_UfU\"}";
  return json;
}

static void loginNames()
{
  ZuTestScope(loginNames);
  Zum::String name{" \tAdmin@KALDRON.IO\t "};
  ZuCheck(Zum::loginNormalize(name) && name == "admin@kaldron.io");
  name = Zum::String{ZuCSpan{"\xc3\xa9@example.COM", 14}};
  ZuCSpan normalized{"\xc3\xa9@example.com", 14};
  ZuCheck(Zum::loginNormalize(name) && name == normalized);
  name = Zum::String{ZuCSpan{"bad\xc0\xaf", 5}};
  ZuCheck(!Zum::loginNormalize(name));
  name = Zum::String{ZuCSpan{"bad\xe2\x28\xa1", 6}};
  ZuCheck(!Zum::loginNormalize(name));
  name = Zum::String{ZuCSpan{"bad\x01", 4}};
  ZuCheck(!Zum::loginNormalize(name));
}

static void managementCatalog()
{
  ZuTestScope(managementCatalog);
  ZuCheck(Zum::MgmtOp::N == 70);
  ZuCheck(!Zum::managementAction(-1));
  ZuCheck(!Zum::managementAction(Zum::MgmtOp::N));
  ZuCheck(Zum::managementAction(Zum::MgmtOp::userRecover) ==
    "Zum.userRecover");
  ZuCheck(Zum::CoreAction::N == Zum::MgmtOp::N &&
    !Zum::coreAction(Zum::CoreAction::N));
  ZuCheck(Zum::managementNeedsIdempotency(Zum::MgmtOp::appEnroll) &&
    Zum::managementNeedsIdempotency(Zum::MgmtOp::catalogPublish) &&
    !Zum::managementNeedsIdempotency(Zum::MgmtOp::appState) &&
    !Zum::managementNeedsIdempotency(Zum::MgmtOp::appQuery));
  for (unsigned i = 0; i < Zum::MgmtOp::N; ++i) {
    const auto *route = Zum::managementRoute(i);
    if (!Zum::managementAction(i)) {
      ZuCheck(!route && !Zum::managementAudited(i));
      continue;
    }
    ZuCheck(route && Zum::managementAudited(i) ==
      (route->method != Zhttp::Method::GET));
  }
  ZuCheck(!Zum::managementAudited(-1) &&
    !Zum::managementAudited(Zum::MgmtOp::N));
  auto audit = Zum::managementAuditRecord("issuer", Zum::MgmtOp::roleActions,
    "administrator", 42, "/admin/apps/42/roles/7/actions", "request-1",
    412, 123);
  ZuCheck(audit.time == 123 && audit.issuer == "issuer" &&
    audit.appID == 42 && audit.operationID == Zum::MgmtOp::roleActions &&
    audit.actor == "administrator" &&
    audit.target == "/admin/apps/42/roles/7/actions" &&
    audit.event == Zum::AuditEvent::Administration &&
    audit.outcome == Zum::AuditOutcome::Failure &&
    audit.correlationID == "request-1" && audit.detail == "roleActions");
  ZuCheck(Zum::MgmtOp::lookup("credentialAdd") < 0);
  ZuCheck(Zum::MgmtOp::lookup("auditUpdate") < 0);
  ZuCheck(Zum::MgmtOp::lookup("auditQuery") < 0 &&
    Zum::MgmtOp::lookup("auditCleanup") < 0);
  ZuCheck(!Zum::managementAction(Zum::MgmtOp::retired67) &&
    !Zum::managementAction(Zum::MgmtOp::retired68));
  ZuCheck(Zum::managementOperation(Zhttp::Method::GET, "/admin/audit") < 0 &&
    Zum::managementOperation(Zhttp::Method::POST, "/admin/audit/cleanup") < 0);
  ZuCheck(Zum::MgmtOp::lookup("grantCreate") < 0);
  bool unique = true;
  for (unsigned i = 0; i < Zum::MgmtOp::N; ++i) {
    if (!Zum::managementAction(i)) continue;
    unique &= Zum::MgmtOp::lookup(Zum::MgmtOp::name(i)) == i;
    auto route = Zum::managementRoute(i);
    unique &= route && route->op == int(i) && route->path &&
      Zum::managementOperation(route->method, route->path) == int(i);
    for (unsigned j = 0; j < i; ++j)
      unique &= Zum::managementAction(i) != Zum::managementAction(j) &&
        (!route || !Zum::managementRoute(j) ||
         route->method != Zum::managementRoute(j)->method ||
         ZuCSpan{route->path} != Zum::managementRoute(j)->path);
  }
  unique &= !Zum::managementRoute(-1) &&
    !Zum::managementRoute(Zum::MgmtOp::N) &&
    Zum::managementOperation(Zhttp::Method::GET, "/admin/unknown") < 0;
  ZuCheck(unique);
  ZuCheck(Zum::managementAllow("/admin/apps") == "GET, POST");
  ZuCheck(Zum::managementAllow("/admin/apps/42?ignored=true") == "PATCH");
  ZuCheck(Zum::managementOperation(
    Zhttp::Method::PATCH, "/admin/apps/state") == Zum::MgmtOp::appUpdate);
  ZuCheck(Zum::managementOperation(
    Zhttp::Method::PUT, "/admin/apps/42/state") == Zum::MgmtOp::appState);
  ZuCheck(Zum::managementOperation(
    Zhttp::Method::PUT, "/apps/42/state") == Zum::MgmtOp::appState);
  ZuCheck(Zum::managementAllow("/apps") == "GET, POST");
  ZuCheck(Zum::managementOperation(
    Zhttp::Method::GET, "/admin/apps/") < 0);
  ZuCheck(Zum::managementOperation(
    Zhttp::Method::GET, "/admin//apps") < 0);
  ZuCheck(Zum::managementOperation(
    Zhttp::Method::PUT, "/admin/apps/42/state/extra") < 0);
  ZuCheck(!Zum::managementAllow("/admin/unknown"));
}

static void jsonContract()
{
  ZuTestScope(jsonContract);
  Zum::Cred credential{.id = Zum::Bytes{ZuBSpan{"\xfb\xff"}}};
  ZtString<> credentialJSON;
  ZfJSON::AsObject::Handler<Zum::Cred, ZuFacet::JSON>::
    template save<Zum::PublicField>(credentialJSON, credential);
  ZuCheck(credentialJSON.find<"\"id\":\"-_8\"">() >= 0);
  auto encode = []<typename T>(const T &value) {
    Zum::String json;
    ZfJSON::AsObject::Handler<T, ZuFacet::JSON>::
      template save<Zum::PublicField>(json, value);
    return json;
  };
  ZuCheck(encode(Zum::App{.catalogDigest = credential.id}).
    find<"\"catalogDigest\":\"-_8\"">() >= 0);
  ZuCheck(encode(Zum::User{.handle = credential.id}).
    find<"\"handle\":\"-_8\"">() >= 0);
  auto grantJSON = encode(Zum::Grant{
    .id = credential.id, .credentialID = credential.id});
  ZuCheck(grantJSON.find<"\"id\":\"-_8\"">() >= 0 &&
    grantJSON.find<"\"credentialID\":\"-_8\"">() >= 0);
  Zum::Role role{
    .appID = UINT64_MAX - 1,
    .id = 9007199254740993ULL,
    .name = "reader",
    .catalogRevision = 9007199254740995ULL,
    .version = 9007199254740996ULL
  };
  ZtString<> json;
  ZfJSON::AsObject::Handler<Zum::Role, ZuFacet::JSON>::
    template save<Zum::PublicField>(json, role);
  ZuCheck(json.find<"\"appID\":\"18446744073709551614\"">() >= 0 &&
    json.find<"\"id\":\"9007199254740993\"">() >= 0 &&
    json.find<"\"name\":\"reader\"">() >= 0 &&
    json.find<"\"catalogRevision\":\"9007199254740995\"">() >= 0);
}

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
}

struct HTTPTestApp {
  template <typename ...Args> void authorize(Args &&...) { }
  template <typename ...Args> void token(Args &&...) { }
  template <typename ...Args> void passkeyBegin(Args &&...) { }
  template <typename ...Args> void passkeyFinish(Args &&...) { }
  template <typename ...Args> void metadata(Args &&...) { }
  template <typename ...Args> void openidMetadata(Args &&...) { }
  template <typename ...Args> void jwks(Args &&...) { }
  template <typename ...Args> void userInfo(Args &&...) { }
  template <typename ...Args> void revoke(Args &&...) { }
  template <typename ...Args> void oidcCallback(Args &&...) { }
  template <typename ...Args> void login(Args &&...) { }
  template <typename ...Args> void consent(Args &&...) { }
  template <typename ...Args> void logout(Args &&...) { }
};

using HTTPTestRequests = Zum::HTTPRequests<HTTPTestApp>;
ZrestCatalogDerive(HTTPTestCatalog, HTTPTestRequests);
ZrestCatalogImpl(HTTPTestCatalog)
using HTTPTestChild = Zum::HTTPParser<HTTPTestApp, HTTPTestCatalog>;
using HTTPTestRoots = ZuTypeList<
  Zrest::ReqRoot<ZuStringT<"oauth2">, HTTPTestChild>>;
ZrestRootCatalogDerive(HTTPTestRootCatalog, HTTPTestRoots);
ZrestRootCatalogImpl(HTTPTestRootCatalog)
using HTTPTestParser = Zrest::MReqParser<HTTPTestRootCatalog,
  Zrest::MReqRootPolicy<HTTPTestRootCatalog, HTTPTestApp>>;

static void httpRoutes()
{
  ZuTestScope(httpRoutes);
  HTTPTestApp app;
  HTTPTestParser parser;
  parser.init(app);
  auto match = [&parser](Zhttp::Method::T method, ZuCSpan path) {
    Zum::String data;
    data << path;
    Zhttp::Target target;
    target.path = data;
    bool ok = parser.operation(method, target);
    parser.reset();
    return ok;
  };
  ZuCheck(match(Zhttp::Method::GET,
    "/oauth2/42/v1/authorize?client_id=x"));
  ZuCheck(!match(Zhttp::Method::GET,
    "/oauth2/42/v1/authorize/extra"));
  ZuCheck(match(Zhttp::Method::POST, "/oauth2/42/v1/token"));
  ZuCheck(!match(Zhttp::Method::GET, "/oauth2/42/v1/token"));
  ZuCheck(match(Zhttp::Method::POST, "/oauth2/42/v1/passkey/begin"));
  ZuCheck(match(Zhttp::Method::POST, "/oauth2/42/v1/passkey/finish"));
  ZuCheck(match(Zhttp::Method::GET,
    "/oauth2/42/.well-known/openid-configuration"));
  ZuCheck(match(Zhttp::Method::GET, "/oauth2/42/v1/keys"));
  ZuCheck(match(Zhttp::Method::GET, "/oauth2/42/v1/userinfo"));
  ZuCheck(match(Zhttp::Method::POST, "/oauth2/42/v1/userinfo"));
  ZuCheck(match(Zhttp::Method::POST, "/oauth2/42/v1/revoke"));
  ZuCheck(!match(Zhttp::Method::GET, "/oauth2/42/v1/revoke"));
  ZuCheck(match(Zhttp::Method::GET,
    "/oauth2/42/v1/oidc/callback?code=x&state=y"));
  ZuCheck(match(Zhttp::Method::GET, "/oauth2/42/v1/login"));
  ZuCheck(match(Zhttp::Method::POST, "/oauth2/42/v1/login"));
  ZuCheck(match(Zhttp::Method::POST, "/oauth2/42/v1/consent"));
  ZuCheck(!match(Zhttp::Method::GET, "/oauth2/42/v1/consent"));
  ZuCheck(match(Zhttp::Method::POST, "/oauth2/42/v1/logout"));
  ZuCheck(!match(Zhttp::Method::GET, "/oauth2/42/v1/logout"));
  ZuCheck(!match(Zhttp::Method::POST, "/oauth2//v1/token"));
  ZuCheck(!match(Zhttp::Method::POST, "/oauth2//42/v1/token"));
  ZuCheck(!match(Zhttp::Method::POST, "/oauth2/42tail/v1/token"));
  ZuCheck(!match(Zhttp::Method::POST,
    "/oauth2/18446744073709551615/v1/token"));
  ZuCheck(!match(Zhttp::Method::POST, "/oauth2/42/v1/unknown"));

  Zum::String query;
  query << "/oauth2/42/v1/authorize?client_id=x";
  Zhttp::Target target;
  target.path = query;
  ZuCheck(parser.operation(Zhttp::Method::GET, target));
  auto &child = parser.u.template p<HTTPTestChild>();
  ZuCheck(child.u.cdispatch([](auto, const auto &request) {
    if constexpr (ZuIsSame<ZuDecay<decltype(request)>,
        Zum::AuthorizeReq<HTTPTestApp>>{})
      return request.object->appID == 42 &&
        request.object->data == "client_id=x";
    else
      return false;
  }));
  parser.reset();

  // Parser input spans are callback-scoped. A later body append must not
  // invalidate the query or authentication headers used at completion.
  Zum::HTTPQuery ownedQuery;
  Zum::String queryInput{ZuCSpan{"?client_id=test"}};
  ownedQuery = queryInput;
  memset(queryInput.data(), 0, queryInput.length());
  ZuCheck(ownedQuery.data == "client_id=test");
  Zum::HTTPData ownedBody;
  Zum::String bodyInput{ZuCSpan{"{\"type\":\"public-key\"}"}};
  ownedBody = bodyInput;
  memset(bodyInput.data(), 0, bodyInput.length());
  ZuCheck(ownedBody.data == "{\"type\":\"public-key\"}");
  Zum::PasskeyFinishReq<HTTPTestApp> passkey;
  passkey.init();
  Zum::String cookieInput{ZuCSpan{"zum_tx=test"}};
  passkey.template header<ZuStringT<"cookie">>(
    Zhttp::FieldSection::Final, cookieInput);
  memset(cookieInput.data(), 0, cookieInput.length());
  ZuCheck(passkey.cookie == "zum_tx=test");
  Zum::TokenReq<HTTPTestApp> tokenRequest;
  tokenRequest.init();
  Zum::String authInput{ZuCSpan{"Basic test"}};
  tokenRequest.template header<ZuStringT<"authorization">>(
    Zhttp::FieldSection::Final, authInput);
  memset(authInput.data(), 0, authInput.length());
  ZuCheck(tokenRequest.authorization == "Basic test");

  ZmRef<Zum::HTTPResponse> response = new Zum::HTTPResponse{};
  response->body = "{}";
  {
    Zum::HTTPBuilder<HTTPTestCatalog> builder;
    builder.template init<Zum::TokenOK, Zum::TokenReq<HTTPTestApp>>(
      response.ptr());
    ZuCheck(builder.status() == 200 &&
      builder.bodyPolicy() == Zhttp::BodyPolicy::Fixed);
  }

  Zum::ServerReply finish{.location = "http://127.0.0.1:49152/cb?code=x&state=y",
    .setCookie = "zum_tx=session; Secure; HttpOnly",
    .type = Zum::ReplyType::Redirect};
  Zum::passkeyReply(finish);
  ZuCheck(finish.type == Zum::ReplyType::OK && !finish.location &&
    finish.body == "{\"redirectURI\":\"http://127.0.0.1:49152/cb?code=x&state=y\"}" &&
    finish.setCookie == "zum_tx=session; Secure; HttpOnly");
  response->body = finish.body;
  response->setCookie = finish.setCookie;
  Zum::HTTPBuilder<HTTPTestCatalog> builder;
  builder.template init<Zum::PasskeyOK, Zum::PasskeyFinishReq<HTTPTestApp>>(
    response.ptr());
  ZuCheck(builder.status() == 200 &&
    builder.bodyPolicy() == Zhttp::BodyPolicy::Fixed);
  finish = Zum::ServerReply{.location = "https://app/cb?state=\"quoted\"",
    .type = Zum::ReplyType::Redirect};
  Zum::passkeyReply(finish);
  ZuCheck(finish.body ==
    "{\"redirectURI\":\"https://app/cb?state=\\\"quoted\\\"\"}");
  finish = Zum::ServerReply{.body = "<form action=/consent></form>",
    .type = Zum::ReplyType::Page};
  Zum::passkeyReply(finish);
  ZuCheck(finish.type == Zum::ReplyType::Page &&
    finish.body == "<form action=/consent></form>");
  finish = Zum::ServerReply{.body = "{\"error\":\"access_denied\"}",
    .type = Zum::ReplyType::OAuthError};
  Zum::passkeyReply(finish);
  ZuCheck(finish.type == Zum::ReplyType::OAuthError &&
    finish.body == "{\"error\":\"access_denied\"}");
}

static void limits()
{
  ZuTestScope(limits);
  Zum::ServerLimits server;
  ZuCheck(server.valid());
  server.form = Zum::ServerLimitMax::Form - 1;
  ZuCheck(server.valid());
  server.form = Zum::ServerLimitMax::Form;
  ZuCheck(server.valid());
  server.form = Zum::ServerLimitMax::Form + 1;
  ZuCheck(!server.valid());

  server = {};
  server.ceremonyQuery = Zum::ServerLimitMax::CeremonyQuery;
  ZuCheck(server.valid());
  server.ceremonyQuery = Zum::ServerLimitMax::CeremonyQuery + 1;
  ZuCheck(!server.valid());
  server = {};
  server.json = Zum::ServerLimitMax::JSON;
  ZuCheck(server.valid());
  server.json = Zum::ServerLimitMax::JSON + 1;
  ZuCheck(!server.valid());

  server = {};
  server.oidc.response = Zum::OIDCLimitMax::Response;
  server.oidc.callback = Zum::OIDCLimitMax::Callback;
  ZuCheck(server.valid());
  server.oidc.response = Zum::OIDCLimitMax::Response + 1;
  ZuCheck(!server.valid());
  server = {};
  server.oidc.callback = Zum::OIDCLimitMax::Callback + 1;
  ZuCheck(!server.valid());

  Zum::AuthorizeReq<HTTPTestApp> query;
  query.init();
  Zum::String suffix;
  suffix.length(Zum::ServerLimitMax::Query);
  memset(suffix.data(), 'x', suffix.length());
  Zum::String path{"/v1/authorize"};
  path << suffix;
  Zhttp::Target target;
  target.path = path;
  ZuCheck(query.operation(Zhttp::Method::GET, target));
  query.init();
  suffix.length(Zum::ServerLimitMax::Query + 1);
  memset(suffix.data(), 'x', suffix.length());
  path = "/v1/authorize";
  path << suffix;
  target.path = path;
  ZuCheck(!query.operation(Zhttp::Method::GET, target));

  Zum::TokenReq<HTTPTestApp> form;
  form.init();
  ZuCheck(form.bodyInfo(Zhttp::BodyType::Fixed, Zum::ServerLimitMax::Form));
  ZuCheck(!form.bodyInfo(
    Zhttp::BodyType::Fixed, Zum::ServerLimitMax::Form + 1));
  Zum::PasskeyBeginReq<HTTPTestApp> json;
  json.init();
  ZuCheck(json.bodyInfo(Zhttp::BodyType::Fixed, Zum::ServerLimitMax::JSON));
  ZuCheck(!json.bodyInfo(
    Zhttp::BodyType::Fixed, Zum::ServerLimitMax::JSON + 1));

  ZuCheck(Zum::AuthorityScanLimit::RoleMappingsScan ==
      Zum::AuthorityScanLimit::RoleMappings + 1 &&
    Zum::AuthorityScanLimit::ClientAccessScan ==
      Zum::AuthorityScanLimit::ClientAccess + 1 &&
    Zum::AdminQueryLimit::Scan == Zum::AdminQueryLimit::Results + 1);
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
  client.state = Zum::State::Active;
  client.type = Zum::ClientType::Confidential;
  client.redirects.clear();
  client.redirects.push("https://app.example/cb");
  params.redirectURI = "https://app.example/cb";
  ZuCheck(Zum::authorizeClient(client, params));
  params.redirectURI = "https://app.example:443/cb";
  ZuCheck(!Zum::authorizeClient(client, params));
  params.redirectURI = "https://app.example/cb";
  client.grants = Zum::ClientGrant::ClientCredentials;
  ZuCheck(!Zum::authorizeClient(client, params));
}

static bool jwtPart(Zum::String &token, ZuBSpan value)
{
  auto offset = token.length();
  auto length = ZuBase64URL::enclen(value.length());
  token.length(offset + length);
  return ZuBase64URL::encode(token.span().offset(offset), value) == length;
}

static Zum::String base64URL(ZuBSpan value)
{
  Zum::String encoded;
  encoded.length(ZuBase64URL::enclen(value.length()));
  encoded.length(ZuBase64URL::encode(encoded.span(), value));
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
  if (!jwtPart(next, raw))
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
  selection.appID = 27;
  selection.audienceID = 3;
  selection.audience = "orders";
  selection.scope = "openid read";
  Zum::Grant ceremony;
  ZuCheck(Zum::authorizationBegin(rng, ceremony, "https://issuer", params,
    selection, true, ZuBSpan{"browser binding"}, 9, 100, 160));
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
  Zum::CodeFamily family;
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
  ZtBitmap wide{130U};
  wide.set(1).set(129);
  ZtBitmap shortLimit{2U};
  shortLimit.set(1);
  auto shortResult = Zum::intersectActions(ZtBitmap{wide}, shortLimit);
  ZuCheck(shortResult[1] && !shortResult[129]);
  auto emptyResult = Zum::intersectActions(ZuMv(wide), ZtBitmap{});
  ZuCheck(!emptyResult[1] && !emptyResult[129] && !emptyResult.length());
  ZuCheck(narrowed[2]);
  ZuCheck(!narrowed[65]);

  Zum::IDVec userRoles;
  userRoles.push(1);
  userRoles.push(2);
  Zum::IDVec delegatedRoles;
  delegatedRoles.push(2);
  delegatedRoles.push(3);
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
  auto resolved = Zum::effectiveActions(8, userRoles, delegatedRoles,
    roles, actionRecords);
  ZuCheck(!resolved[1] && resolved[2] && !resolved[3]);

  Zum::Client client{.id = "browser", .appID = 1};
  client.identityScopes.push("openid");
  client.identityScopes.push("profile");
  Zum::StringVec redirects;
  redirects.push("https://app.example/callback");
  ZuCheck(!Zum::clientConfigValid(Zum::ClientType::Native,
    Zum::ClientGrant::AuthorizationCode, true, redirects));
  ZuCheck(Zum::clientConfigValid(Zum::ClientType::Native,
    Zum::ClientGrant::AuthorizationCode | Zum::ClientGrant::RefreshToken,
    true, redirects));
  Zum::ClientAccess access{.clientID = "browser", .appID = 1,
    .audienceIDs = {1, 2}, .roleIDs = {1, 2}};
  Zum::ScopeAuth scopes[3] = {
    {.role = {.appID = 1, .id = 1, .name = "read"}, .audienceID = 1,
      .audience = "orders"},
    {.role = {.appID = 1, .id = 2, .name = "write"}, .audienceID = 1,
      .audience = "orders"},
    {.role = {.appID = 1, .id = 3, .name = "charge"}, .audienceID = 2,
      .audience = "billing"}
  };
  Zum::ScopeSelection selection;
  ZuCheck(Zum::selectScopes(client, access,
    "read write read", scopes, selection) ==
    Zum::ScopeError::OK);
  ZuCheck(selection.audience == "orders" && selection.scope == "read write" &&
    selection.roleIDs.length() == 2 && selection.roleIDs[0] == 1 &&
    selection.roleIDs[1] == 2);
  access.roleIDs.push(3);
  ZuCheck(Zum::selectScopes(client, access,
    "read charge", scopes, selection) ==
    Zum::ScopeError::Audience);

  Zum::IDVec granted;
  granted.push(1);
  ZuCheck(Zum::selectGrantedScopes(
    client, access, granted, false, {}, scopes, selection) == Zum::ScopeError::OK);
  ZuCheck(selection.scope == "read" && selection.roleIDs.length() == 1 &&
    selection.roleIDs[0] == 1);
  ZuCheck(Zum::selectGrantedScopes(
    client, access, granted, true, "write", scopes, selection) ==
    Zum::ScopeError::Unavailable);
  ZuCheck(Zum::selectScopes(client, access,
    "openid profile read", scopes, selection) ==
    Zum::ScopeError::OK);
  ZuCheck(selection.identity && selection.audience == "orders" &&
    selection.scope == "openid profile read" && selection.roleIDs.length() == 1);
  ZuCheck(Zum::selectGrantedScopes(client, access,
    granted, "openid read", false, {},
    scopes, selection) == Zum::ScopeError::OK);
  ZuCheck(selection.identity && selection.scope == "openid read");
  ZuCheck(Zum::selectGrantedScopes(client, access,
    granted, "openid read", true,
    "profile", scopes, selection) == Zum::ScopeError::Unavailable);

  client.type = Zum::ClientType::Browser;
  client.grants = Zum::ClientGrant::AuthorizationCode |
    Zum::ClientGrant::RefreshToken;
  client.refreshAllowed = true;
  client.state = Zum::State::Active;
  ZuCheck(Zum::selectScopes(client, access,
    "openid offline_access read", scopes, selection) == Zum::ScopeError::OK);
  ZuCheck(selection.identity &&
    selection.scope == "openid offline_access read" &&
    selection.roleIDs.length() == 1);
  client.refreshAllowed = false;
  ZuCheck(Zum::selectScopes(client, access,
    "openid offline_access read", scopes, selection) ==
    Zum::ScopeError::Unavailable);
  client.refreshAllowed = true;
  Zum::User user{
    .id = 42,
    .handle = Zum::Bytes{ZuBSpan{"handle"}},
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
  grant.scope = "read write";
  grant.requestedRoleIDs.push(1);
  grant.requestedRoleIDs.push(2);
  grant.roleIDs = userRoles;
  grant.actions.length(8);
  grant.actions.set(1);
  ZtBitmap authority;
  client.type = Zum::ClientType::Confidential;
  client.grants = Zum::ClientGrant::ClientCredentials |
    Zum::ClientGrant::RefreshToken;
  ZuCheck(Zum::clientAuthority(client, access, "offline_access", 8,
    scopes, roles, actionRecords, selection, authority) ==
    Zum::ScopeError::Unavailable);
  client.type = Zum::ClientType::Browser;
  client.grants = Zum::ClientGrant::AuthorizationCode |
    Zum::ClientGrant::RefreshToken;
  ZuCheck(Zum::interactiveAuthority(grant, user, cred, client,
    false, {}, 8, access, scopes, roles, actionRecords, selection, authority) ==
    Zum::ScopeError::OK);
  ZuCheck(selection.scope == "read write" && authority[1] &&
    !authority[2] && !authority[3]);
  client.type = Zum::ClientType::Confidential;
  ZuCheck(Zum::interactivePrincipal(grant, user, cred, client));
  client.grants = Zum::ClientGrant::ClientCredentials;
  ZuCheck(!Zum::interactivePrincipal(grant, user, cred, client));
  client.grants = Zum::ClientGrant::AuthorizationCode |
    Zum::ClientGrant::RefreshToken;
  client.type = Zum::ClientType::Browser;
  ZuCheck(Zum::interactiveAuthority(grant, user, cred, client,
    true, "write", 8, access, scopes, roles, actionRecords,
    selection, authority) == Zum::ScopeError::OK && !authority);
  grant.userID = 43;
  ZuCheck(Zum::interactiveAuthority(grant, user, cred, client,
    false, {}, 8, access, scopes, roles, actionRecords, selection, authority) ==
    Zum::AuthorityError::Invalid);
  grant.userID = 42;
  cred.state = Zum::State::Disabled;
  ZuCheck(Zum::interactiveAuthority(grant, user, cred, client,
    false, {}, 8, access, scopes, roles, actionRecords, selection, authority) ==
    Zum::AuthorityError::Invalid);
  cred.state = Zum::State::Active;
  ++user.authVersion;
  ZuCheck(Zum::interactiveAuthority(grant, user, cred, client,
    false, {}, 8, access, scopes, roles, actionRecords, selection, authority) ==
    Zum::AuthorityError::Invalid);

  client.type = Zum::ClientType::Confidential;
  client.grants = Zum::ClientGrant::ClientCredentials;
  access.roleIDs = userRoles;
  ZuCheck(Zum::clientAuthority(client, access, "write", 8,
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

template <typename T>
static T roundTrip(const T &value)
{
  Zfb::IOBuilder fbb{new ZiIOBufAlloc<>()};
  fbb.Finish(ZfbStruct::save(fbb, value));
  auto buf = fbb.buf();
  return ZfbStruct::ctor<T>(ZfbStruct::root<T>(buf->data()));
}

static void appRecords()
{
  ZuTestScope(appRecords);
  Zum::Issuer issuer{
    .id = "issuer", .schemaVersion = Zum::SchemaVersion,
    .keyCheck = Zum::Bytes{ZuBSpan{"current-check"}},
    .pendingKeyCheck = Zum::Bytes{ZuBSpan{"pending-check"}}};
  auto restoredIssuer = roundTrip(issuer);
  ZuCheck(restoredIssuer.schemaVersion == issuer.schemaVersion &&
    restoredIssuer.keyCheck == issuer.keyCheck &&
    restoredIssuer.pendingKeyCheck == issuer.pendingKeyCheck);
  issuer.pendingKeyCheck.null();
  ZuCheck(!roundTrip(issuer).pendingKeyCheck);
  Zum::Cred credential{.id = Zum::Bytes{ZuBSpan{"snapshot-credential"}}, .userID = 42,
    .publicKey = Zum::Bytes{ZuBSpan{"public-key"}}, .signCount = 17,
    .created = 100, .updated = 123, .state = Zum::State::Active,
    .backupEligible = true, .backedUp = false, .label = "security key",
    .owner = 19, .userVersion = 3, .version = 7};
  auto restoredCred = roundTrip(credential);
  ZuCheck(restoredCred.id == credential.id && restoredCred.userID == credential.userID &&
    restoredCred.publicKey == credential.publicKey && restoredCred.signCount == 17 &&
    restoredCred.created == 100 && restoredCred.updated == 123 &&
    restoredCred.state == Zum::State::Active && restoredCred.backupEligible &&
    !restoredCred.backedUp && restoredCred.label == "security key" &&
    restoredCred.owner == 19 && restoredCred.userVersion == 3 && restoredCred.version == 7);
  auto app = roundTrip(Zum::App{
    .id = 1, .name = "zum", .label = "Zum", .state = Zum::State::Active,
    .nextActionID = 70, .authVersion = 2, .catalogRevision = 3,
    .catalogDigest = Zum::Bytes{ZuBSpan{"digest"}},
    .created = 10, .updated = 11});
  ZuCheck(app.id == 1 && app.name == "zum" && app.nextActionID == 70 &&
    app.catalogRevision == 3 && app.catalogDigest == ZuBSpan{"digest"});

  Zum::Membership membership{
    .appID = 1, .userID = 2, .state = Zum::State::Active,
    .authVersion = 4};
  membership.roleIDs.push(3);
  membership = roundTrip(membership);
  ZuCheck(membership.appID == 1 && membership.userID == 2 &&
    membership.roleIDs.length() == 1 && membership.roleIDs[0] == 3);

  auto audience = roundTrip(Zum::Audience{
    .id = 4, .appID = 1, .name = "admin",
    .uri = "https://issuer/admin", .state = Zum::State::Active});
  ZuCheck(audience.id == 4 && audience.appID == 1 &&
    audience.uri == "https://issuer/admin");

  Zum::ClientAccess clientAccess{
    .clientID = "service", .appID = 1, .state = Zum::State::Active,
    .authVersion = 5};
  clientAccess.audienceIDs.push(4);
  clientAccess.roleIDs.push(6);
  clientAccess.roleIDs.push(7);
  clientAccess = roundTrip(clientAccess);
  ZuCheck(clientAccess.clientID == "service" &&
    clientAccess.audienceIDs[0] == 4 && clientAccess.roleIDs[0] == 6 &&
    clientAccess.roleIDs[1] == 7);

  Zum::AdminAccess adminAccess{
    .actorKind = Zum::ActorKind::Client, .actorID = "service", .appID = 1,
    .state = Zum::State::Active};
  adminAccess.operationIDs.push(Zum::MgmtOp::catalogPublish);
  adminAccess.roleIDs.push(7);
  adminAccess = roundTrip(adminAccess);
  ZuCheck(adminAccess.actorKind == Zum::ActorKind::Client &&
    adminAccess.operationIDs[0] == Zum::MgmtOp::catalogPublish);

  Zum::Provider provider{
    .id = 8, .name = "upstream", .issuer = "https://idp.example",
    .clientID = "zum", .clientSecret = Zum::Bytes{ZuBSpan{"ciphertext"}},
    .roleClaim = "roles", .claimSource = Zum::ClaimSource::UserInfo,
    .state = Zum::State::Active};
  provider.scopes.push("openid");
  provider = roundTrip(provider);
  ZuCheck(provider.id == 8 && provider.scopes[0] == "openid" &&
    provider.clientSecret == ZuBSpan{"ciphertext"});

  Zum::AuthPolicy policy{
    .appID = 1, .providerID = 8,
    .eligibilityMode = Zum::EligibilityMode::MappedRole,
    .eligibilityClaim = "roles", .assignmentMaxAge = 300,
    .sessionIdle = 1800, .sessionAbsolute = 43200, .tokenLifetime = 300,
    .consentPolicy = Zum::ConsentPolicy::Preauthorized,
    .state = Zum::State::Active};
  policy.eligibilityValues.push("member");
  policy = roundTrip(policy);
  ZuCheck(policy.appID == 1 && policy.providerID == 8 && policy.localFirst &&
    policy.assignmentMaxAge == 300 && policy.eligibilityValues[0] == "member");

  auto identity = roundTrip(Zum::ExtIdentity{
    .providerID = 8, .issuer = "https://idp.example", .subject = "abc",
    .userID = 2});
  ZuCheck(identity.providerID == 8 && identity.subject == "abc" &&
    identity.userID == 2);

  auto roleMap = roundTrip(Zum::RoleMap{
    .appID = 1, .providerID = 8, .value = "operators", .roleID = 7,
    .state = Zum::State::Active});
  ZuCheck(roleMap.appID == 1 && roleMap.value == "operators" &&
    roleMap.roleID == 7);

  Zum::Evidence evidence{
    .appID = 1, .userID = 2, .providerID = 8, .eligible = true,
    .observed = 100, .deadline = 400,
    .source = Zum::ClaimSource::IDToken, .policyVersion = 9,
    .protectedRefreshToken = Zum::Bytes{ZuBSpan{"protected"}}};
  evidence.roleValues.push("operators");
  evidence = roundTrip(evidence);
  ZuCheck(evidence.eligible && evidence.deadline == 400 &&
    evidence.roleValues[0] == "operators");

  auto session = roundTrip(Zum::Session{
    .digest = Zum::Bytes{ZuBSpan{"session"}}, .userID = 2,
    .providerID = 8, .issuer = "https://idp.example", .subject = "abc",
    .authTime = 100, .idleDeadline = 200, .absoluteDeadline = 300,
    .state = Zum::State::Active});
  ZuCheck(session.digest == ZuBSpan{"session"} && session.userID == 2 &&
    session.absoluteDeadline == 300);

  Zum::Consent consent{
    .userID = 2, .clientID = "native", .appID = 1, .audienceID = 4,
    .state = Zum::State::Active};
  consent.roleIDs.push(6);
  consent = roundTrip(consent);
  ZuCheck(consent.clientID == "native" && consent.roleIDs[0] == 6);

  Zum::IdemRequest request{
    .actorKind = Zum::ActorKind::User, .actorID = "2",
    .operation = Zum::MgmtOp::appEnroll, .idempotencyKey = "once",
    .requestDigest = Zum::Bytes{ZuBSpan{"request"}}, .sagaID = 12,
    .status = Zum::RequestStatus::Complete, .expires = 500};
  request.resultIDs.push("1");
  request = roundTrip(request);
  ZuCheck(request.actorID == "2" &&
    request.operation == Zum::MgmtOp::appEnroll && request.sagaID == 12 &&
    request.resultIDs[0] == "1");
}

static void appAuthority()
{
  ZuTestScope(appAuthority);
  Zum::App app{
    .id = 1, .name = "one", .state = Zum::State::Active,
    .nextActionID = 2};
  Zum::Membership membership{
    .appID = 1, .userID = 9, .state = Zum::State::Active};
  membership.roleIDs.push(7);
  Zum::Audience audience{
    .id = 3, .appID = 1, .uri = "https://one.example/api",
    .state = Zum::State::Active};
  Zum::ScopeAuth scope{
    .role = Zum::Role{.appID = 1, .id = 7, .name = "operator",
      .state = Zum::State::Active},
    .audienceID = 3, .audience = audience.uri};
  scope.role.actions.length(2);
  scope.role.actions.set(0);

  Zum::Role one{
    .appID = 1, .id = 7, .name = "operator", .state = Zum::State::Active};
  one.actions.length(2);
  one.actions.set(0);
  Zum::Role two{
    .appID = 2, .id = 7, .name = "operator", .state = Zum::State::Active};
  two.actions.length(2);
  two.actions.set(1);
  Zum::RoleVec roles;
  roles.push(ZuMv(one));
  roles.push(ZuMv(two));

  Zum::ActionVec actions;
  actions.push(Zum::Action{
    .appID = 1, .id = 0, .name = "read", .state = Zum::State::Active});
  actions.push(Zum::Action{
    .appID = 2, .id = 0, .name = "read", .state = Zum::State::Active});
  auto effective = Zum::appEffectiveActions(
    app, membership, scope, audience, roles, actions);
  ZuCheck(effective[0] && !effective[1]);
  ZuCheck(ZuStructKey<0>(actions[0]) != ZuStructKey<0>(actions[1]));
  ZuCheck(ZuStructKey<1>(actions[0]) != ZuStructKey<1>(actions[1]));
  ZuCheck(ZuStructKey<0>(roles[0]) != ZuStructKey<0>(roles[1]));
  ZuCheck(ZuStructKey<1>(roles[0]) != ZuStructKey<1>(roles[1]));
  auto otherScope = scope;
  otherScope.role.appID = 2;
  ZuCheck(otherScope.role.appID != scope.role.appID);

  Zum::Session firstSession{.digest = Zum::Bytes{ZuBSpan{"first"}}, .userID = 9};
  auto secondSession = firstSession;
  secondSession.digest = Zum::Bytes{ZuBSpan{"second"}};
  ZuCheck(ZuStructKey<1>(firstSession) != ZuStructKey<1>(secondSession));
  Zum::Consent firstConsent{.userID = 9, .clientID = "client", .appID = 1,
    .audienceID = 3};
  auto secondConsent = firstConsent;
  secondConsent.audienceID = 4;
  ZuCheck(ZuStructKey<1>(firstConsent) != ZuStructKey<1>(secondConsent));
  Zum::Grant firstGrant{.id = Zum::Bytes{ZuBSpan{"first"}}, .appID = 1, .userID = 9};
  auto secondGrant = firstGrant;
  secondGrant.id = Zum::Bytes{ZuBSpan{"second"}};
  ZuCheck(ZuStructKey<2>(firstGrant) != ZuStructKey<2>(secondGrant));
  ZuCheck(ZuStructKey<3>(firstGrant) != ZuStructKey<3>(secondGrant));

  auto wrongAudience = audience;
  wrongAudience.appID = 2;
  ZuCheck(!Zum::scopeValid(app, scope, wrongAudience, roles));
  auto wrongScope = scope;
  wrongScope.role.id = 8;
  ZuCheck(!Zum::scopeValid(app, wrongScope, audience, roles));
  auto wrongMembership = membership;
  wrongMembership.appID = 2;
  ZuCheck(!Zum::membershipValid(app, wrongMembership, roles));

  Zum::ActionID id = 0;
  ZuCheck(Zum::actionAlloc(app, id) && id == 2 && app.nextActionID == 3 &&
    app.authVersion == 2 && app.version == 2);
  app.nextActionID = UINT32_MAX;
  ZuCheck(!Zum::actionAlloc(app, id));
}

static void discovery()
{
  ZuTestScope(discovery);
  Zum::String path;
  ZuCheck(Zum::appIssuerPath(42, path) && path == "/oauth2/42");
  Zum::AppIssuerPath issuerPath;
  ZuCheck(ZfURI::loadPath(issuerPath, path) &&
    issuerPath.oauth2 == "oauth2" && issuerPath.appID == 42);
  ZuCheck(Zum::appEndpointPath(42, "v1", "authorize", path) &&
    path == "/oauth2/42/v1/authorize");
  Zum::AppEndpointPath endpointPath;
  ZuCheck(ZfURI::loadPath(endpointPath, path) &&
    endpointPath.oauth2 == "oauth2" && endpointPath.appID == 42 &&
    endpointPath.group == "v1" && endpointPath.endpoint == "authorize");
  ZuCheck(Zum::appNestedEndpointPath(
    42, "v1", "passkey", "begin", path) &&
    path == "/oauth2/42/v1/passkey/begin");
  Zum::AppNestedEndpointPath nestedPath;
  ZuCheck(ZfURI::loadPath(nestedPath, path) &&
    nestedPath.appID == 42 && nestedPath.group == "v1" &&
    nestedPath.section == "passkey" && nestedPath.endpoint == "begin");
  ZuCheck(Zum::appOIDCMetadataPath(42, path) &&
    path == "/oauth2/42/.well-known/openid-configuration");
  Zum::AppOIDCMetadataPath oidcPath;
  ZuCheck(ZfURI::loadPath(oidcPath, path) && oidcPath.appID == 42 &&
    oidcPath.wellKnown == ".well-known" &&
    oidcPath.endpoint == "openid-configuration");
  ZuCheck(Zum::appOAuthMetadataPath(42, path) &&
    path == "/.well-known/oauth-authorization-server/oauth2/42");
  Zum::AppOAuthMetadataPath oauthPath;
  ZuCheck(ZfURI::loadPath(oauthPath, path) && oauthPath.appID == 42 &&
    oauthPath.wellKnown == ".well-known" &&
    oauthPath.endpoint == "oauth-authorization-server" &&
    oauthPath.oauth2 == "oauth2");
  Zum::String missing;
  Zum::String extra;
  missing.copy("/oauth2/v1/token");
  extra.copy("/oauth2/42/v1/token/extra");
  ZuCheck(!ZfURI::loadPath(endpointPath, missing));
  ZuCheck(!ZfURI::loadPath(endpointPath, extra));
  Zum::String issuer;
  ZuCheck(Zum::appIssuer("https://auth.example/", 42, issuer) &&
    issuer == "https://auth.example/oauth2/42");
  ZuCheck(!Zum::appIssuer("https://auth.example/?bad=1", 42, issuer));
  auto metadata = Zum::metadataJSON("https://auth.example/", 42, {});
  ZuCheck(metadata.find<
    "\"issuer\":\"https://auth.example/oauth2/42\"">() >= 0);
  ZuCheck(metadata.find<
    "\"authorization_endpoint\":"
    "\"https://auth.example/oauth2/42/v1/authorize\"">() >= 0);
  ZuCheck(metadata.find<"\"code_challenge_methods_supported\":[\"S256\"]">() >= 0);
  ZuCheck(metadata.find<"\"offline_access\"">() >= 0);
  ZuCheck(metadata.find<"\"userinfo_endpoint\":"
    "\"https://auth.example/oauth2/42/v1/userinfo\"">() >= 0);
  ZuCheck(metadata.find<"\"id_token_signing_alg_values_supported\":"
    "[\"ES256\"]">() >= 0);

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
  client.appID = 27;
  client.type = Zum::ClientType::Browser;
  client.grants = Zum::ClientGrant::AuthorizationCode;
  client.state = Zum::State::Active;
  Zum::ScopeSelection selection;
  selection.appID = 27;
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
    claims.appID == 27 &&
    claims.actions.length() == 1 && claims.actions[0] == "orders.read");
  client.type = Zum::ClientType::Confidential;
  ZuCheck(Zum::interactiveClaims(rng, "https://issuer", user, client,
    selection, authority, actions, "passkey", 100, 100, 160, claims));
  client.grants = Zum::ClientGrant::ClientCredentials;
  ZuCheck(!Zum::interactiveClaims(rng, "https://issuer", user, client,
    selection, authority, actions, "passkey", 100, 100, 160, claims));
  client.grants = Zum::ClientGrant::AuthorizationCode;
  client.type = Zum::ClientType::Browser;
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
    principal.tokenID.issuer == claims.issuer &&
    principal.tokenID.jti == claims.jti &&
    principal.clientID == "browser" && principal.scope == "orders.read" &&
    principal.appID == 27 &&
    principal.actions.length() == 1 &&
    principal.actions[0] == "orders.read" &&
    principal.authMethod == "passkey" &&
    principal.authTime == 100 && principal.expires == 160);
  ZuCheck(!Zum::jwtVerify(prepared.token, "key-1", "https://issuer", "other",
    publicKey, 120, limits, principal));
  ZuCheck(!Zum::jwtVerify(prepared.token, "key-1", "https://issuer", "orders",
    publicKey, 160, limits, principal));

  Zum::SignKey verifyKey{.id = "key-1", .issuer = "https://issuer",
    .algorithm = "ES256", .notBefore = 100, .retireAfter = 150,
    .state = Zum::State::Active};
  verifyKey.publicJwk << "{\"kty\":\"EC\",\"crv\":\"P-256\",\"kid\":\"key-1\",\"x\":\"" <<
    base64URL({publicKey + 1, Ztls::COSE::ES256::CoordinateSize}) <<
    "\",\"y\":\"" << base64URL({publicKey + 1 + Ztls::COSE::ES256::CoordinateSize,
      Ztls::COSE::ES256::CoordinateSize}) << "\"}";
  auto verifyRecord = [&verifyKey, &prepared, &limits](
      int64_t now, ZuCSpan issuer, ZuCSpan audience) {
    Zum::Principal principal;
    return Zum::signKeyVerify(verifyKey, prepared.token, issuer, audience,
      now, limits, principal);
  };
  ZuCheck(verifyRecord(120, "https://issuer", "orders"));
  ZuCheck(verifyRecord(120, "https://issuer", {}));
  ZuCheck(!verifyRecord(120, "https://other-issuer", "orders"));
  ZuCheck(!verifyRecord(120, "https://issuer", "other"));
  verifyKey.state = Zum::State::Suspended;
  ZuCheck(verifyRecord(149, "https://issuer", "orders"));
  ZuCheck(!verifyRecord(150, "https://issuer", "orders"));
  verifyKey.state = Zum::State::Revoked;
  ZuCheck(!verifyRecord(120, "https://issuer", "orders"));
  verifyKey.state = Zum::State::Pending;
  ZuCheck(!verifyRecord(120, "https://issuer", "orders"));
  verifyKey.state = Zum::State::Disabled;
  ZuCheck(!verifyRecord(120, "https://issuer", "orders"));
  verifyKey.state = Zum::State::Active;
  verifyKey.notBefore = 121;
  ZuCheck(!verifyRecord(120, "https://issuer", "orders"));
  verifyKey.notBefore = 100;
  verifyKey.algorithm = "RS256";
  ZuCheck(!verifyRecord(120, "https://issuer", "orders"));
  verifyKey.algorithm = "ES256";
  verifyKey.id = "other-key";
  ZuCheck(!verifyRecord(120, "https://issuer", "orders"));

  Zum::String changed = prepared.token;
  changed[changed.length() / 2] ^= 1;
  ZuCheck(!Zum::jwtVerify(changed, "key-1", "https://issuer", "orders",
    publicKey, 120, limits, principal));

  Zum::ScopeSelection oidcSelection = selection;
  oidcSelection.scope = "openid profile email orders.read";
  user.name = "alice";
  user.profile = "Alice Example";
  user.email = "alice@example.com";
  Zum::IDClaims idClaims;
  ZuCheck(Zum::idClaims("https://issuer", user, client, oidcSelection,
    "nonce-1", "passkey", 100, 120, 180, idClaims));
  Zum::PreparedJWT idPrepared;
  ZuCheck(Zum::idTokenPrepare(idClaims, "key-1", limits, idPrepared));
  signatureLength = 0;
  signed_ = sk.sign(rng, idPrepared.digest, [
    &signature, &signatureLength
  ](ZuBSpan der) {
    signatureLength = der.length();
    memcpy(signature, der.data(), signatureLength);
  });
  ZuCheck(!signed_.template is<ZeException>() && signatureLength &&
    Zum::jwtFinish(idPrepared, ZuBSpan{signature, signatureLength}, limits));
  Zum::JWTHeader idHeader;
  Zum::String idJSON;
  ZuCheck(Zum::jwtES256(idPrepared.token, publicKey, limits,
    idHeader, idJSON) && idHeader.type == "JWT" &&
    idJSON.find<"\"aud\":\"browser\"">() >= 0 &&
    idJSON.find<"\"nonce\":\"nonce-1\"">() >= 0 &&
    idJSON.find<"\"email\":\"alice@example.com\"">() >= 0);
  principal.scope = oidcSelection.scope;
  principal.subject = idClaims.subject;
  Zum::String userInfo;
  ZuCheck(Zum::userInfoJSON(user, principal, userInfo) &&
    userInfo == "{\"sub\":\"b3BhcXVlIHVzZXIgaGFuZGxl\","
      "\"name\":\"Alice Example\",\"preferred_username\":\"alice\","
      "\"email\":\"alice@example.com\"}");
  ZuCheck(Zum::jwtVerifyIssuer(prepared.token, "key-1", "https://issuer",
    publicKey, 120, limits, principal));

  client.id = "workload";
  client.type = Zum::ClientType::Confidential;
  client.grants = Zum::ClientGrant::ClientCredentials;
  claims = {};
  ZuCheck(Zum::clientClaims(rng, "https://issuer", client, selection,
    authority, actions, 27, 200, 260, claims));
  ZuCheck(claims.subject == "workload" && claims.clientID == "workload" &&
    claims.appID == 27 && !claims.authTime && !claims.amr &&
    claims.actions.length() == 1);
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
    principal.clientID == "workload" && principal.appID == 27 &&
    !principal.authMethod && !principal.authTime && principal.expires == 260);
}

static void ssfSET()
{
  ZuTestScope(ssfSET);
  Ztls::Random rng;
  ZuCheck(rng.init());
  Ztls::PK::SK_EC sk{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  uint8_t publicKey[Ztls::COSE::ES256::PublicKeySize];
  ZuCheck(Ztls::Backend::pkey_ec_export_public(sk.key, publicKey));
  Zum::SignKey key{.id = "key-1", .issuer = "https://issuer"};
  Zum::String set;
  Zum::makeSSF(key, "https://receiver", Zum::RefreshID{
    .issuer = "https://issuer/oauth2/9", .familyID = "family-1"}, 180, 120,
    [&sk, &rng](const Zum::SignKey &, ZuBSpan digest,
        Zum::SignatureFn complete) {
      sk.sign(rng, digest, [complete = ZuMv(complete)](ZuBSpan der) mutable {
        complete(Zum::Bytes{der});
      });
    }, [&set](Zum::String value) { set = ZuMv(value); });
  ZuCheck(set);
  Zum::JWTHeader header;
  Zum::String claims;
  Zum::JWTLimits limits;
  ZuCheck(Zum::jwtES256(set, publicKey, limits, header, claims) &&
    header.type == "secevent+jwt" && header.algorithm == "ES256" &&
    claims.find<"urn:zum:events:refresh-token-revoked">() >= 0 &&
    claims.find<"family-1">() >= 0);
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
  encoded.length(ZuBase64URL::encode(encoded.span(), challenge));
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
  encoded.length(ZuBase64URL::encode(encoded.span(), challenge));
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
  using M = Zum::MSaga;
  {
    Zum::KeyBinding binding{
      .issuer = "issuer",
      .beforeCheck = Zum::Bytes{ZuBSpan{"old-check"}},
      .afterCheck = Zum::Bytes{ZuBSpan{"old-check"}},
      .afterPending = Zum::Bytes{ZuBSpan{"new-check"}}};
    ZmRef<M> saga = new M{};
    saga->init(ZuMv(binding));
    Zdb_::SagaPayload payload;
    M::save(saga, payload);
    auto loaded = M::load(Zum::KeyBinding::Type{}(), payload);
    ZuCheck(bool(loaded));
    ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
      if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::KeyBinding>{})
	return change.issuer == "issuer" &&
	  change.beforeCheck == ZuBSpan{"old-check"} &&
	  change.afterCheck == change.beforeCheck && !change.beforePending &&
	  change.afterPending == ZuBSpan{"new-check"};
      else return false;
    }));
  }
  for (unsigned field = Zum::SecretRekey::ProviderField;
      field <= Zum::SecretRekey::SignKeyField; ++field) {
    Zum::SecretRekey rekey{
      .field = field, .providerID = 7, .appID = 8, .userID = 9,
      .keyID = "signer", .before = Zum::Bytes{ZuBSpan{"old-envelope"}},
      .after = Zum::Bytes{ZuBSpan{"new-envelope"}}};
    ZmRef<M> saga = new M{};
    saga->init(ZuMv(rekey));
    Zdb_::SagaPayload payload;
    M::save(saga, payload);
    auto loaded = M::load(Zum::SecretRekey::Type{}(), payload);
    ZuCheck(bool(loaded));
    ZuCheck(loaded->u.cdispatch([field](auto, const auto &change) {
      if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::SecretRekey>{})
	return change.field == field && change.providerID == 7 &&
	  change.appID == 8 && change.userID == 9 && change.keyID == "signer" &&
	  change.before == ZuBSpan{"old-envelope"} &&
	  change.after == ZuBSpan{"new-envelope"};
      else return false;
    }));
  }
  Zum::Enrollment enrollment;
  enrollment.ceremonyID = Zum::Bytes{ZuBSpan{"ceremony"}};
  enrollment.userID = 42;
  enrollment.name = "first user";
  enrollment.handle = Zum::Bytes{ZuBSpan{"handle"}};
  enrollment.credentialID = Zum::Bytes{ZuBSpan{"credential"}};
  enrollment.publicKey = Zum::Bytes{ZuBSpan{"cose-key"}};
  enrollment.created = 123;
  enrollment.beforeGrant = Zum::Grant{.id = enrollment.ceremonyID,
    .kind = Zum::GrantKind::Ceremony, .state = Zum::State::Active};

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
	enrollment.publicKey == ZuBSpan{"cose-key"} &&
	enrollment.beforeGrant.id == enrollment.ceremonyID;
    else
      return false;
  }));

  Zdb_::SagaTypeStep step;
  ZuCheck(M::catalog(0, 0, step));
  ZuCheck(step.table == "zum.grant" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(0, 1, step));
  ZuCheck(step.table == "zum.user" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(0, 2, step));
  ZuCheck(step.table == "zum.user" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(0, 7, step));
  ZuCheck(step.table == "zum.user" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(0, 8, step));
  ZuCheck(step.table == "zum.grant" && step.op == ZdbSagaOp::Delete);
  ZuCheck(!M::catalog(0, 9, step));

  Zum::CredentialAdd add;
  add.ceremonyID = Zum::Bytes{ZuBSpan{"credential ceremony"}};
  add.issuer = "issuer";
  add.userID = 42;
  add.userHandle = Zum::Bytes{ZuBSpan{"handle"}};
  add.credentialID = Zum::Bytes{ZuBSpan{"credential-2"}};
  add.publicKey = Zum::Bytes{ZuBSpan{"cose-key-2"}};
  add.created = 124;
  add.beforeGrant = Zum::Grant{.id = add.ceremonyID,
    .created = 120, .expires = 200, .kind = Zum::GrantKind::Ceremony,
    .purpose = Zum::GrantPurpose::AddCredential, .state = Zum::State::Active};
  saga = new M{};
  saga->init(ZuMv(add));
  M::save(saga, payload);
  loaded = M::load(Zum::CredentialAdd::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &add) {
    if constexpr (ZuIsSame<ZuDecay<decltype(add)>, Zum::CredentialAdd>{})
      return add.issuer == "issuer" && add.userID == 42 &&
	add.credentialID == ZuBSpan{"credential-2"} &&
	add.beforeGrant.id == add.ceremonyID && add.beforeGrant.expires == 200;
    else
      return false;
  }));
  ZuCheck(M::catalog(1, 0, step));
  ZuCheck(step.table == "zum.grant" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(1, 3, step));
  ZuCheck(step.table == "zum.cred" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(1, 4, step));
  ZuCheck(step.table == "zum.grant" && step.op == ZdbSagaOp::Delete);

  Zum::RecoveryStart recovery;
  recovery.capabilityID = Zum::Bytes{ZuBSpan{"recovery capability"}};
  recovery.digest = Zum::Bytes{ZuBSpan{"recovery digest"}};
  recovery.issuer = "issuer";
  recovery.userID = 42;
  recovery.userVersion = 2;
  recovery.created = 125;
  recovery.expires = 180;
  recovery.actor = "administrator";
  recovery.version = 7;
  recovery.oldState = Zum::State::Suspended;
  recovery.oldUpdated = 120;
  recovery.request = Zum::IdemRequest{.actorID = "admin",
    .operation = Zum::MgmtOp::userRecover, .idempotencyKey = "recover"};
  saga = new M{};
  saga->init(ZuMv(recovery));
  M::save(saga, payload);
  loaded = M::load(Zum::RecoveryStart::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &recovery) {
    if constexpr (ZuIsSame<ZuDecay<decltype(recovery)>, Zum::RecoveryStart>{})
      return recovery.issuer == "issuer" && recovery.userID == 42 &&
	recovery.userVersion == 2 && recovery.actor == "administrator" &&
	recovery.version == 7 && recovery.oldState == Zum::State::Suspended &&
	recovery.oldUpdated == 120 && recovery.request.idempotencyKey == "recover";
    else
      return false;
  }));
  ZuCheck(M::catalog(2, 0, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(2, 1, step));
  ZuCheck(step.table == "zum.user" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(2, 4, step));
  ZuCheck(step.table == "zum.grant" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(2, 5, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Update);

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
  recoveryEnroll.beforeGrant = Zum::Grant{.id = recoveryEnroll.ceremonyID,
    .created = 120, .expires = 200, .state = Zum::State::Active};
  recoveryEnroll.beforeUser = Zum::User{.id = 42, .handle = recoveryEnroll.oldHandle,
    .updated = 122, .state = Zum::State::Suspended, .authVersion = 2, .version = 7};
  saga = new M{};
  saga->init(ZuMv(recoveryEnroll));
  M::save(saga, payload);
  loaded = M::load(Zum::RecoveryEnroll::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &recovery) {
    if constexpr (ZuIsSame<
	ZuDecay<decltype(recovery)>, Zum::RecoveryEnroll>{})
      return recovery.actor == "administrator" && recovery.userID == 42 &&
	recovery.userVersion == 2 && recovery.newHandle == ZuBSpan{"new handle"} &&
	recovery.beforeGrant.expires == 200 && recovery.beforeUser.version == 7 &&
	recovery.beforeUser.updated == 122;
    else
      return false;
  }));
  ZuCheck(M::catalog(3, 0, step));
  ZuCheck(step.table == "zum.grant" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(3, 5, step));
  ZuCheck(step.table == "zum.user" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(3, 6, step));
  ZuCheck(step.table == "zum.grant" && step.op == ZdbSagaOp::Delete);

  Zum::CodeFamily family;
  family.codeID = Zum::Bytes{ZuBSpan{"code"}};
  family.codeDigest = Zum::Bytes{ZuBSpan{"code digest"}};
  family.familyID = Zum::Bytes{ZuBSpan{"family"}};
  family.issuer = "issuer";
  family.userID = 42;
  family.clientID = "browser";
  family.credentialID = Zum::Bytes{ZuBSpan{"credential"}};
  family.audience = "orders";
  family.requestedRoleIDs.push(3);
  family.actions.length(8);
  family.actions.set(2);
  family.digest = Zum::Bytes{ZuBSpan{"digest"}};
  family.authVersion = 7;
  family.authTime = family.created = 100;
  family.expires = 1000;
  family.beforeGrant = Zum::Grant{.id = family.codeID, .expires = 200,
    .kind = Zum::GrantKind::Code, .state = Zum::State::Active};
  saga = new M{};
  saga->init(ZuMv(family));
  M::save(saga, payload);
  loaded = M::load(Zum::CodeFamily::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &family) {
    if constexpr (ZuIsSame<ZuDecay<decltype(family)>, Zum::CodeFamily>{})
      return family.userID == 42 && family.authVersion == 7 &&
        family.actions[2] &&
	family.beforeGrant.id == family.codeID && family.beforeGrant.expires == 200;
    else
      return false;
  }));
  ZuCheck(M::catalog(4, 0, step));
  ZuCheck(step.table == "zum.grant" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(4, 2, step));
  ZuCheck(step.table == "zum.grant" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(4, 3, step));
  ZuCheck(step.table == "zum.grant" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(4, 4, step));
  ZuCheck(step.table == "zum.grant" && step.op == ZdbSagaOp::Delete);

  Zum::AppEnrollment appEnrollment{.coreAppID = 1, .appID = 9,
    .appName = "orders", .appLabel = "Orders", .audienceID = 10,
    .audienceURI = "https://orders.example",
    .signKey = Zum::SignKey{.id = "app_9_1",
      .issuer = "https://auth.example/oauth2/9", .algorithm = "ES256",
      .publicJwk = "{}", .privateMaterial = Zum::Bytes{ZuBSpan{"key"}},
      .notBefore = 123, .state = Zum::State::Active,
      .created = 123, .updated = 123},
    .clientID = "svc_orders",
    .secretDigest = Zum::Bytes{ZuBSpan{"verifier"}},
    .clientType = Zum::ClientType::Confidential, .catalogClient = true,
    .created = 123, .catalogPublishOp = Zum::MgmtOp::catalogPublish,
    .operationQueryOp = Zum::MgmtOp::operationQuery,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::appEnroll, .idempotencyKey = "enroll-orders",
      .requestDigest = Zum::Bytes{ZuBSpan{"request digest"}},
      .expires = 86400, .created = 120, .updated = 120}};
  appEnrollment.redirects.push("https://orders.example/callback");
  saga = new M{};
  saga->init(ZuMv(appEnrollment));
  M::save(saga, payload);
  loaded = M::load(Zum::AppEnrollment::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &enrollment) {
    if constexpr (ZuIsSame<ZuDecay<decltype(enrollment)>,
        Zum::AppEnrollment>{})
      return enrollment.coreAppID == 1 && enrollment.appID == 9 &&
	 enrollment.appName == "orders" && enrollment.clientID == "svc_orders" &&
	 enrollment.signKey.id == "app_9_1" &&
	 enrollment.signKey.issuer == "https://auth.example/oauth2/9" &&
	 enrollment.secretDigest == ZuBSpan{"verifier"} &&
	 enrollment.catalogClient &&
         enrollment.request.idempotencyKey == "enroll-orders" &&
         enrollment.request.operation == Zum::MgmtOp::appEnroll &&
         enrollment.request.requestDigest == ZuBSpan{"request digest"};
    else
      return false;
  }));
  ZuCheck(M::catalog(5, 0, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(5, 1, step));
  ZuCheck(step.table == "zum.app" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(5, 3, step));
  ZuCheck(step.table == "zum.sign_key" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(5, 7, step));
  ZuCheck(step.table == "zum.auth_policy" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(5, 14, step));
  ZuCheck(step.table == "zum.app" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(5, 15, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Update);

  Zum::ExternalProjection projection{.providerID = 8,
    .issuer = "https://upstream.example", .subject = "00u1", .userID = 44,
    .name = "oidc:8:44", .handle = Zum::Bytes{ZuBSpan{"handle"}},
    .created = 123};
  saga = new M{};
  saga->init(ZuMv(projection));
  M::save(saga, payload);
  loaded = M::load(Zum::ExternalProjection::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &projection) {
    if constexpr (ZuIsSame<ZuDecay<decltype(projection)>,
        Zum::ExternalProjection>{})
      return projection.providerID == 8 && projection.subject == "00u1" &&
        projection.userID == 44 && projection.handle == ZuBSpan{"handle"};
    else
      return false;
  }));
  ZuCheck(M::catalog(6, 0, step));
  ZuCheck(step.table == "zum.ext_identity" &&
    step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(6, 1, step));
  ZuCheck(step.table == "zum.user" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(6, 4, step));
  ZuCheck(step.table == "zum.ext_identity" &&
    step.op == ZdbSagaOp::Update);

  Zum::AppActionAdd appAction{.appID = 9, .actionID = 3,
    .name = "orders.ship", .label = "Ship orders", .created = 124,
    .oldAppVersion = 7, .oldAuthVersion = 11, .oldUpdated = 123,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::actionAdd, .idempotencyKey = "create-ship",
      .requestDigest = Zum::Bytes{ZuBSpan{"digest"}}, .expires = 86400,
      .version = 1, .created = 123, .updated = 123}};
  saga = new M{};
  saga->init(ZuMv(appAction));
  M::save(saga, payload);
  loaded = M::load(Zum::AppActionAdd::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &action) {
    if constexpr (ZuIsSame<ZuDecay<decltype(action)>, Zum::AppActionAdd>{})
      return action.appID == 9 && action.actionID == 3 &&
	 action.name == "orders.ship" && action.label == "Ship orders" &&
	 action.created == 124 && action.oldAppVersion == 7 &&
	 action.oldAuthVersion == 11 && action.oldUpdated == 123 &&
         action.request.actorID == "admin" &&
         action.request.operation == Zum::MgmtOp::actionAdd &&
         action.request.idempotencyKey == "create-ship" &&
         action.request.requestDigest == ZuBSpan{"digest"};
    else
      return false;
  }));
  ZuCheck(M::catalog(7, 0, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(7, 1, step));
  ZuCheck(step.table == "zum.app" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(7, 2, step));
  ZuCheck(step.table == "zum.action" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(7, 3, step));
  ZuCheck(step.table == "zum.action" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(7, 4, step));
  ZuCheck(step.table == "zum.app" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(7, 5, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Update);

  Zum::MembershipAdd membership{.appID = 9, .userID = 42, .created = 125,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::membershipAdd, .idempotencyKey = "enroll-user",
      .requestDigest = Zum::Bytes{ZuBSpan{"membership"}}}, .error = 409};
  saga = new M{};
  saga->init(ZuMv(membership));
  M::save(saga, payload);
  loaded = M::load(Zum::MembershipAdd::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &add) {
    if constexpr (ZuIsSame<ZuDecay<decltype(add)>, Zum::MembershipAdd>{})
      return add.appID == 9 && add.userID == 42 && add.created == 125 &&
	add.request.actorID == "admin" &&
	add.request.operation == Zum::MgmtOp::membershipAdd &&
	add.request.idempotencyKey == "enroll-user" &&
	add.request.requestDigest == ZuBSpan{"membership"} && add.error == 503;
    else return false;
  }));
  ZuCheck(M::catalog(11, 0, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(11, 1, step));
  ZuCheck(step.table == "zum.membership" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(11, 2, step));
  ZuCheck(step.table == "zum.membership" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(11, 3, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Update);

  Zum::MembershipChange memberChange{.appID = 9, .userID = 42,
    .oldRoles = {1}, .newRoles = {2}, .version = 3, .authVersion = 4,
    .updated = 125, .appVersion = 5, .appAuthVersion = 6,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::membershipRoles, .idempotencyKey = "assign-role"},
    .assignRoles = true, .ifMatch = "\"v3\"", .error = 400};
  saga = new M{};
  saga->init(ZuMv(memberChange));
  M::save(saga, payload);
  loaded = M::load(Zum::MembershipChange::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::MembershipChange>{})
      return change.appID == 9 && change.userID == 42 &&
	change.oldRoles == Zum::IDVec{1} && change.newRoles == Zum::IDVec{2} &&
	!change.unchanged() && change.version == 3 && change.authVersion == 4 &&
	change.appVersion == 5 && change.appAuthVersion == 6 &&
	change.request.idempotencyKey == "assign-role" &&
	change.assignRoles && change.ifMatch == "\"v3\"" && !change.error;
    else return false;
  }));
  ZuCheck(M::catalog(8, 0, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(8, 4, step));
  ZuCheck(step.table == "zum.app" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(8, 5, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Update);
  Zum::RoleEdit roleActions{
    .app = Zum::App{.id = 9, .nextActionID = 2},
    .before = Zum::Role{.appID = 9, .id = 4, .name = "operator"},
    .actionIDs = {0, 1}, .ifMatch = "\"v1\"", .updated = 126,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::roleActions, .idempotencyKey = "role-actions"},
    .kind = Zum::RoleEdit::Label, .label = "Role label",
    .state = Zum::State::Suspended, .error = 400};
  saga = new M{};
  saga->init(ZuMv(roleActions));
  M::save(saga, payload);
  loaded = M::load(Zum::RoleEdit::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::RoleEdit>{})
      return change.app.id == 9 && change.app.nextActionID == 2 &&
	change.before.id == 4 && change.before.name == "operator" &&
	change.actionIDs == Zum::ActionIDVec{0, 1} && change.ifMatch == "\"v1\"" &&
	change.updated == 126 && change.request.idempotencyKey == "role-actions" &&
	change.kind == Zum::RoleEdit::Label && change.label == "Role label" &&
	change.state == Zum::State::Suspended &&
	!change.error && !change.actions.length();
    else return false;
  }));
  ZuCheck(M::catalog(12, 0, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(12, 2, step));
  ZuCheck(step.table == "zum.role" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(12, 5, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Update);
  Zum::ActionEdit actionEdit{
    .app = Zum::App{.id = 9},
    .before = Zum::Action{.appID = 9, .id = 0, .name = "ping"},
    .ifMatch = "\"v1\"", .updated = 128,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::actionState, .idempotencyKey = "action-state"},
    .state = Zum::State::Disabled, .error = 409};
  saga = new M{};
  saga->init(ZuMv(actionEdit));
  M::save(saga, payload);
  loaded = M::load(Zum::ActionEdit::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::ActionEdit>{})
      return change.app.id == 9 && change.before.appID == 9 && !change.before.id &&
	change.before.name == "ping" && change.ifMatch == "\"v1\"" &&
	change.request.idempotencyKey == "action-state" &&
	change.state == Zum::State::Disabled && !change.error;
    else return false;
  }));
  ZuCheck(M::catalog(13, 0, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(13, 2, step));
  ZuCheck(step.table == "zum.action" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(13, 5, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Update);
  Zum::AppChange appChange{.before = Zum::App{.id = 9, .name = "orders",
      .label = "Original", .state = Zum::State::Active, .authVersion = 4, .version = 3},
    .ifMatch = "\"v3\"", .label = "Updated", .updated = 129,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::appState, .idempotencyKey = "app-state"},
    .stateOnly = true, .state = Zum::State::Disabled, .error = 409};
  saga = new M{};
  saga->init(ZuMv(appChange));
  M::save(saga, payload);
  loaded = M::load(Zum::AppChange::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::AppChange>{})
      return change.before.id == 9 && change.before.name == "orders" &&
	change.before.label == "Original" && change.before.authVersion == 4 &&
	change.before.version == 3 && change.label == "Updated" &&
	change.ifMatch == "\"v3\"" && change.stateOnly &&
	change.state == Zum::State::Disabled && !change.unchanged() && !change.error &&
	change.request.idempotencyKey == "app-state";
    else return false;
  }));
  ZuCheck(M::catalog(14, 0, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(14, 1, step));
  ZuCheck(step.table == "zum.app" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(14, 3, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Update);
  Zum::UserEdit userEdit{.before = Zum::User{.id = 42, .name = "local",
      .profile = "Original", .email = "original@example.test"},
    .ifMatch = "\"v1\"", .profile = "Updated", .updated = 130,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::userUpdate, .idempotencyKey = "user-profile"},
    .email = "updated@example.test", .fields = 3, .error = 409};
  saga = new M{};
  saga->init(ZuMv(userEdit));
  M::save(saga, payload);
  loaded = M::load(Zum::UserEdit::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::UserEdit>{})
      return change.before.id == 42 && change.before.profile == "Original" &&
	change.before.email == "original@example.test" && change.profile == "Updated" &&
	change.email == "updated@example.test" && change.fields == 3 &&
	!change.stateOnly && !change.error &&
	change.request.idempotencyKey == "user-profile";
    else return false;
  }));
  ZuCheck(M::catalog(15, 0, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(15, 1, step));
  ZuCheck(step.table == "zum.user" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(15, 3, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Update);
  Zum::CredEdit credEdit{.before = Zum::Cred{
      .id = Zum::Bytes{ZuBSpan{"credential"}}, .userID = 42, .signCount = 7,
      .state = Zum::State::Active, .backedUp = true, .label = "Original"},
    .ifMatch = "\"v1\"", .label = "Updated", .updated = 131,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::credentialUpdate, .idempotencyKey = "credential-label"},
    .error = 409};
  saga = new M{};
  saga->init(ZuMv(credEdit));
  M::save(saga, payload);
  loaded = M::load(Zum::CredEdit::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::CredEdit>{})
      return change.before.id == ZuBSpan{"credential"} && change.before.userID == 42 &&
	change.before.signCount == 7 && change.before.backedUp &&
	change.before.label == "Original" && change.label == "Updated" &&
	!change.stateOnly && !change.error &&
	change.request.idempotencyKey == "credential-label";
    else return false;
  }));
  ZuCheck(M::catalog(16, 0, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(16, 1, step));
  ZuCheck(step.table == "zum.cred" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(16, 3, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Update);
  Zum::AudienceEdit audienceEdit{.app = Zum::App{.id = 42,
      .state = Zum::State::Active},
    .before = Zum::Audience{.id = 43, .appID = 42, .name = "Original",
      .uri = "https://ping.example/", .state = Zum::State::Active},
    .ifMatch = "\"v1\"", .updated = 132,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::audienceUpdate, .idempotencyKey = "audience-name"},
    .name = "Updated", .error = 409};
  ZuCheck(!audienceEdit.appError(audienceEdit.app));
  ZuCheck(!audienceEdit.audienceError(audienceEdit.before));
  audienceEdit.ifMatch = "\"v2\"";
  ZuCheck(audienceEdit.audienceError(audienceEdit.before) == 412);
  audienceEdit.ifMatch = "\"v1\"";
  audienceEdit.stateOnly = true;
  audienceEdit.state = Zum::State::Active;
  ZuCheck(audienceEdit.unchanged());
  audienceEdit.state = Zum::State::Disabled;
  ZuCheck(!audienceEdit.unchanged());
  audienceEdit.before.state = Zum::State::Revoked;
  ZuCheck(audienceEdit.audienceError(audienceEdit.before) == 409);
  audienceEdit.before.state = Zum::State::Active;
  audienceEdit.stateOnly = false;
  saga = new M{};
  saga->init(ZuMv(audienceEdit));
  M::save(saga, payload);
  loaded = M::load(Zum::AudienceEdit::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::AudienceEdit>{})
      return change.app.id == 42 && change.before.id == 43 &&
	change.before.appID == 42 && change.before.name == "Original" &&
	change.before.uri == "https://ping.example/" && change.name == "Updated" &&
	!change.stateOnly && !change.error &&
	change.request.idempotencyKey == "audience-name";
    else return false;
  }));
  ZuCheck(M::catalog(17, 0, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(17, 1, step));
  ZuCheck(step.table == "zum.app" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(17, 2, step));
  ZuCheck(step.table == "zum.audience" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(17, 5, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Update);
  Zum::ProviderEdit providerEdit{.before = Zum::Provider{.id = 44,
      .name = "upstream", .issuer = "https://id.example/", .clientID = "client",
      .clientSecret = Zum::Bytes{ZuBSpan{"ciphertext"}}, .roleClaim = "roles",
      .state = Zum::State::Active},
    .ifMatch = "\"v1\"", .values = Zum::Provider{.roleClaim = "groups"},
    .fields = 16, .updated = 133,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::providerUpdate, .idempotencyKey = "provider-claim"},
    .error = 409};
  ZuCheck(!providerEdit.recordError(providerEdit.before));
  auto editedProvider = providerEdit.before;
  providerEdit.edit(editedProvider);
  ZuCheck(editedProvider.roleClaim == "groups" &&
    editedProvider.issuer == providerEdit.before.issuer &&
    editedProvider.clientSecret == providerEdit.before.clientSecret);
  providerEdit.values.roleClaim.null();
  ZuCheck(providerEdit.recordError(providerEdit.before) == 409);
  providerEdit.values.roleClaim = "groups";
  providerEdit.ifMatch = "\"v2\"";
  ZuCheck(providerEdit.recordError(providerEdit.before) == 412);
  providerEdit.ifMatch = "\"v1\"";
  providerEdit.stateOnly = true;
  providerEdit.state = Zum::State::Active;
  ZuCheck(providerEdit.unchanged());
  providerEdit.before.state = Zum::State::Revoked;
  ZuCheck(providerEdit.recordError(providerEdit.before) == 409);
  providerEdit.before.state = Zum::State::Active;
  providerEdit.stateOnly = false;
  saga = new M{};
  saga->init(ZuMv(providerEdit));
  M::save(saga, payload);
  loaded = M::load(Zum::ProviderEdit::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::ProviderEdit>{})
      return change.before.id == 44 && change.before.roleClaim == "roles" &&
	change.before.clientSecret == ZuBSpan{"ciphertext"} &&
	change.values.roleClaim == "groups" && change.fields == 16 &&
	change.updated == 133 &&
	!change.stateOnly && !change.error &&
	change.request.idempotencyKey == "provider-claim";
    else return false;
  }));
  ZuCheck(M::catalog(18, 0, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(18, 1, step));
  ZuCheck(step.table == "zum.provider" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(18, 3, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Update);
  Zum::ClientEdit clientEdit{.before = Zum::Client{.id = "native-cli", .appID = 42,
      .label = "Original", .secretDigest = Zum::Bytes{ZuBSpan{"digest"}},
      .redirects = {"http://127.0.0.1/callback"}, .type = Zum::ClientType::Native,
      .grants = Zum::ClientGrant::AuthorizationCode, .state = Zum::State::Active},
    .ifMatch = "\"v1\"", .values = Zum::Client{.label = "Updated"},
    .fields = 1, .updated = 134,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::clientUpdate, .idempotencyKey = "client-label"},
    .error = 409};
  ZuCheck(!clientEdit.recordError(clientEdit.before));
  auto editedClient = clientEdit.before;
  clientEdit.edit(editedClient);
  ZuCheck(editedClient.label == "Updated" && editedClient.appID == 42 &&
    editedClient.secretDigest == clientEdit.before.secretDigest &&
    editedClient.redirects == clientEdit.before.redirects);
  clientEdit.fields = 2;
  ZuCheck(clientEdit.recordError(clientEdit.before) == 409);
  clientEdit.fields = 4;
  clientEdit.values.grants = Zum::ClientGrant::ClientCredentials;
  ZuCheck(clientEdit.recordError(clientEdit.before) == 409);
  clientEdit.fields = 1;
  clientEdit.ifMatch = "\"v2\"";
  ZuCheck(clientEdit.recordError(clientEdit.before) == 412);
  clientEdit.ifMatch = "\"v1\"";
  clientEdit.stateOnly = true;
  clientEdit.state = Zum::State::Active;
  ZuCheck(clientEdit.unchanged());
  clientEdit.before.state = Zum::State::Revoked;
  ZuCheck(clientEdit.recordError(clientEdit.before) == 409);
  clientEdit.before.state = Zum::State::Active;
  clientEdit.stateOnly = false;
  saga = new M{};
  saga->init(ZuMv(clientEdit));
  M::save(saga, payload);
  loaded = M::load(Zum::ClientEdit::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::ClientEdit>{})
      return change.before.id == "native-cli" && change.before.appID == 42 &&
	change.before.secretDigest == ZuBSpan{"digest"} &&
	change.values.label == "Updated" && change.fields == 1 &&
	change.updated == 134 && !change.overlapSeconds &&
	!change.stateOnly && !change.error &&
	change.request.idempotencyKey == "client-label";
    else return false;
  }));
  ZuCheck(M::catalog(19, 0, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(19, 1, step));
  ZuCheck(step.table == "zum.client" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(19, 3, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Update);
  Zum::ClientEdit rotation{.before = Zum::Client{.id = "service",
      .secretDigest = Zum::Bytes{ZuBSpan{"old-hash"}}, .secretVersion = 7,
      .type = Zum::ClientType::Confidential,
      .authMethod = Zum::ClientAuthMethod::ClientSecretBasic, .state = Zum::State::Active},
    .ifMatch = "\"v1\"", .fields = Zum::ClientEdit::Secret, .updated = 200,
    .overlapSeconds = 60};
  rotation.values.secretDigest.length(Ztls::SecretHash::Size, false);
  memset(rotation.values.secretDigest.data(), 1, rotation.values.secretDigest.length());
  ZuCheck(!rotation.recordError(rotation.before));
  auto rotatedClient = rotation.result();
  ZuCheck(rotatedClient.secretVersion == 8 && rotatedClient.version == 2 &&
    rotatedClient.previousSecretDigest == rotation.before.secretDigest &&
    rotatedClient.previousSecretExpires == 260 &&
    rotatedClient.secretDigest == rotation.values.secretDigest);
  rotation.overlapSeconds = 0;
  rotatedClient = rotation.result();
  ZuCheck(!rotatedClient.previousSecretDigest && !rotatedClient.previousSecretExpires);
  rotation.before.secretVersion = UINT64_MAX;
  ZuCheck(rotation.recordError(rotation.before) == 409);
  rotation.before.secretVersion = 7;
  rotation.overlapSeconds = 60;
  saga = new M{};
  saga->init(ZuMv(rotation));
  M::save(saga, payload);
  loaded = M::load(Zum::ClientEdit::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::ClientEdit>{})
      return change.fields == Zum::ClientEdit::Secret && change.overlapSeconds == 60 &&
	change.before.secretVersion == 7 && change.values.secretDigest.length() == Ztls::SecretHash::Size;
    else return false;
  }));
  Zum::KeyRetire keyRetire{.before = Zum::SignKey{.id = "key-1",
      .issuer = "https://id.example/", .algorithm = "ES256",
      .privateMaterial = Zum::Bytes{ZuBSpan{"ciphertext"}}, .state = Zum::State::Active},
    .ifMatch = "\"v1\"", .retireAfter = 200, .updated = 135,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::signKeyRetire, .idempotencyKey = "retire-key"},
    .error = 409};
  ZuCheck(!keyRetire.recordError(keyRetire.before));
  keyRetire.retireAfter = 135;
  ZuCheck(keyRetire.recordError(keyRetire.before) == 400);
  keyRetire.retireAfter = 200;
  keyRetire.ifMatch = "\"v2\"";
  ZuCheck(keyRetire.recordError(keyRetire.before) == 412);
  keyRetire.ifMatch = "\"v1\"";
  keyRetire.before.state = Zum::State::Consumed;
  ZuCheck(keyRetire.recordError(keyRetire.before) == 409);
  keyRetire.before.state = Zum::State::Revoked;
  ZuCheck(keyRetire.recordError(keyRetire.before) == 409);
  keyRetire.before.state = Zum::State::Active;
  saga = new M{};
  saga->init(ZuMv(keyRetire));
  M::save(saga, payload);
  loaded = M::load(Zum::KeyRetire::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::KeyRetire>{})
      return change.before.id == "key-1" && change.before.algorithm == "ES256" &&
	change.before.privateMaterial == ZuBSpan{"ciphertext"} &&
	change.retireAfter == 200 && change.updated == 135 && !change.error &&
	change.request.idempotencyKey == "retire-key";
    else return false;
  }));
  ZuCheck(M::catalog(20, 0, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(20, 1, step));
  ZuCheck(step.table == "zum.sign_key" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(20, 2, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Update);
  auto accessState = []<typename Edit>(Edit change, unsigned index, ZuCSpan table) {
    ZuCheck(!change.recordError(change.before));
    auto edited = change.before;
    change.edit(edited);
    ZuCheck(edited.state == Zum::State::Disabled &&
      edited.appID == change.before.appID && edited.roleIDs == change.before.roleIDs);
    if constexpr (ZuIsSame<Edit, Zum::ClientAccessState>{})
      ZuCheck(edited.authVersion == change.before.authVersion + 1 &&
        edited.clientID == change.before.clientID &&
        edited.audienceIDs == change.before.audienceIDs);
    else
      ZuCheck(edited.actorKind == change.before.actorKind &&
        edited.actorID == change.before.actorID &&
        edited.operationIDs == change.before.operationIDs);
    change.ifMatch = "\"v2\"";
    ZuCheck(change.recordError(change.before) == 412);
    change.ifMatch = "\"v1\"";
    change.state = Zum::State::Active;
    ZuCheck(change.unchanged());
    change.before.state = Zum::State::Revoked;
    ZuCheck(change.recordError(change.before) == 409);
    change.before.state = Zum::State::Active;
    change.state = Zum::State::Pending;
    ZuCheck(change.recordError(change.before) == 400);
    change.state = Zum::State::Disabled;
    change.error = 409;
    ZmRef<M> saved = new M{};
    saved->init(ZuMv(change));
    Zdb_::SagaPayload bytes;
    M::save(saved, bytes);
    auto restored = M::load(typename Edit::Type{}(), bytes);
    ZuCheck(bool(restored));
    ZuCheck(restored->u.cdispatch([](auto, const auto &value) {
      if constexpr (ZuIsSame<ZuDecay<decltype(value)>, Edit>{})
        return value.before.appID == 42 && value.state == Zum::State::Disabled &&
          !value.error && value.request.idempotencyKey == "access-state";
      else return false;
    }));
    Zdb_::SagaTypeStep info;
    ZuCheck(M::catalog(index, 1, info));
    ZuCheck(info.table == table && info.op == ZdbSagaOp::Update);
  };
  accessState(Zum::ClientAccessState{
    .before = Zum::ClientAccess{.clientID = "client", .appID = 42,
      .audienceIDs = {43}, .roleIDs = {44}, .state = Zum::State::Active},
    .ifMatch = "\"v1\"", .updated = 136,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::clientAccessState, .idempotencyKey = "access-state"},
    .state = Zum::State::Disabled}, 21, "zum.client_access");
  accessState(Zum::AdminAccessState{
    .before = Zum::AdminAccess{.actorKind = Zum::ActorKind::User,
      .actorID = "admin", .appID = 42, .operationIDs = {Zum::MgmtOp::roleQuery},
      .roleIDs = {44}, .state = Zum::State::Active},
    .ifMatch = "\"v1\"", .updated = 136,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::adminAccessState, .idempotencyKey = "access-state"},
    .state = Zum::State::Disabled}, 22, "zum.admin_access");
  Zum::RoleMapDelete mapDelete{.app = Zum::App{.id = 42, .state = Zum::State::Active},
    .before = Zum::RoleMap{.appID = 42, .providerID = 43, .value = "upstream-role",
      .roleID = 44, .state = Zum::State::Active},
    .ifMatch = "\"v1\"", .updated = 137,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::roleMapDelete, .idempotencyKey = "delete-map"},
    .error = 409};
  ZuCheck(!mapDelete.appError(mapDelete.app));
  ZuCheck(!mapDelete.recordError(mapDelete.before));
  mapDelete.ifMatch = "\"v2\"";
  ZuCheck(mapDelete.recordError(mapDelete.before) == 412);
  mapDelete.ifMatch = "\"v1\"";
  mapDelete.before.owner = 1;
  ZuCheck(mapDelete.recordError(mapDelete.before) == 409);
  mapDelete.before.owner = 0;
  saga = new M{};
  saga->init(ZuMv(mapDelete));
  M::save(saga, payload);
  loaded = M::load(Zum::RoleMapDelete::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::RoleMapDelete>{})
      return change.app.id == 42 && change.before.providerID == 43 &&
	change.before.value == "upstream-role" && change.before.roleID == 44 &&
	change.request.idempotencyKey == "delete-map" && !change.error;
    else return false;
  }));
  ZuCheck(M::catalog(23, 2, step));
  ZuCheck(step.table == "zum.role_map" && step.op == ZdbSagaOp::Delete);
  ZuCheck(M::catalog(23, 4, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Update);
  Zum::RoleMapPut mapPut{.app = Zum::App{.id = 42, .state = Zum::State::Active},
    .before = Zum::RoleMap{.appID = 42, .providerID = 43, .value = "upstream-role",
      .roleID = 44, .state = Zum::State::Active}, .roleID = 45,
    .ifMatch = "\"v1\"", .updated = 138,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::roleMapSet, .idempotencyKey = "replace-map"},
    .error = 409};
  ZuCheck(!mapPut.appError(mapPut.app));
  ZuCheck(!mapPut.recordError(mapPut.before));
  mapPut.ifMatch.null();
  ZuCheck(mapPut.recordError(mapPut.before) == 428);
  mapPut.ifNoneMatch = "*";
  ZuCheck(mapPut.recordError(mapPut.before) == 412);
  mapPut.ifNoneMatch.null();
  mapPut.ifMatch = "\"v1\"";
  saga = new M{};
  saga->init(ZuMv(mapPut));
  M::save(saga, payload);
  loaded = M::load(Zum::RoleMapPut::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::RoleMapPut>{})
      return change.app.id == 42 && change.before.providerID == 43 &&
	change.before.value == "upstream-role" && change.before.roleID == 44 &&
	change.roleID == 45 && change.request.idempotencyKey == "replace-map" && !change.error;
    else return false;
  }));
  ZuCheck(M::catalog(24, 2, step));
  ZuCheck(step.table == "zum.role_map" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(24, 3, step));
  ZuCheck(step.table == "zum.role_map" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(24, 6, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Update);
  Zum::PolicyPut policyPut{.app = Zum::App{.id = 42, .state = Zum::State::Active},
    .before = Zum::AuthPolicy{.appID = 42, .created = 100},
    .values = Zum::AuthPolicy{.appID = 42, .assignmentMaxAge = 60,
      .sessionIdle = 300, .sessionAbsolute = 3600, .tokenLifetime = 60,
      .state = Zum::State::Active}, .ifMatch = "\"v1\"", .updated = 139,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::authPolicySet, .idempotencyKey = "policy"},
    .error = 409};
  ZuCheck(!policyPut.appError(policyPut.app));
  ZuCheck(!policyPut.recordError(policyPut.before));
  auto policyResult = policyPut.result();
  ZuCheck(policyResult.created == 100 && policyResult.updated == 139 &&
    policyResult.version == 2 && policyResult.localFirst && !policyResult.providerID &&
    policyResult.tokenLifetime == 60);
  policyPut.ifNoneMatch = "*";
  ZuCheck(policyPut.recordError(policyPut.before) == 412);
  policyPut.ifNoneMatch.null();
  saga = new M{};
  saga->init(ZuMv(policyPut));
  M::save(saga, payload);
  loaded = M::load(Zum::PolicyPut::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::PolicyPut>{})
      return change.app.id == 42 && change.before.created == 100 &&
	change.values.sessionIdle == 300 && change.values.sessionAbsolute == 3600 &&
	change.request.idempotencyKey == "policy" && !change.error;
    else return false;
  }));
  ZuCheck(M::catalog(25, 2, step));
  ZuCheck(step.table == "zum.auth_policy" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(25, 3, step));
  ZuCheck(step.table == "zum.auth_policy" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(25, 6, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Update);
  auto accessPut = []<typename Edit>(Edit change, unsigned index, ZuCSpan table) {
    ZuCheck(!change.appError(change.app));
    ZuCheck(!change.recordError(change.before));
    auto result = change.result();
    ZuCheck(result.appID == 42 && result.roleIDs == change.values.roleIDs &&
      result.state == Zum::State::Active && result.version == 2 &&
      result.created == 100 && result.updated == 140 && !result.owner);
    if constexpr (ZuIsSame<Edit, Zum::ClientAccessPut>{})
      ZuCheck(result.authVersion == 8 && result.audienceIDs == change.values.audienceIDs);
    else ZuCheck(result.operationIDs == change.values.operationIDs);
    change.ifNoneMatch = "*";
    ZuCheck(change.recordError(change.before) == 412);
    change.ifNoneMatch.null();
    change.error = 409;
    ZmRef<M> saved = new M{};
    saved->init(ZuMv(change));
    Zdb_::SagaPayload bytes;
    M::save(saved, bytes);
    auto restored = M::load(typename Edit::Type{}(), bytes);
    ZuCheck(bool(restored));
    ZuCheck(restored->u.cdispatch([](auto, const auto &value) {
      if constexpr (ZuIsSame<ZuDecay<decltype(value)>, Edit>{})
        return value.app.id == 42 && value.before.created == 100 &&
          value.values.roleIDs.length() == 1 && value.values.roleIDs[0] == 45 &&
          !value.error && value.request.idempotencyKey == "access-put";
      else return false;
    }));
    Zdb_::SagaTypeStep info;
    ZuCheck(M::catalog(index, 2, info));
    ZuCheck(info.table == table && info.op == ZdbSagaOp::Insert);
    ZuCheck(M::catalog(index, 3, info));
    ZuCheck(info.table == table && info.op == ZdbSagaOp::Update);
    ZuCheck(M::catalog(index, 6, info));
    ZuCheck(info.table == "zum.request" && info.op == ZdbSagaOp::Update);
  };
  accessPut(Zum::ClientAccessPut{
    .app = Zum::App{.id = 42, .state = Zum::State::Active},
    .before = Zum::ClientAccess{.clientID = "client", .appID = 42,
      .authVersion = 7, .created = 100},
    .values = Zum::ClientAccess{.clientID = "client", .appID = 42,
      .audienceIDs = {43}, .roleIDs = {45}}, .ifMatch = "\"v1\"", .updated = 140,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::clientAccessSet, .idempotencyKey = "access-put"}},
    26, "zum.client_access");
  accessPut(Zum::AdminAccessPut{
    .app = Zum::App{.id = 42, .state = Zum::State::Active},
    .before = Zum::AdminAccess{.actorKind = Zum::ActorKind::User, .actorID = "admin",
      .appID = 42, .created = 100},
    .values = Zum::AdminAccess{.actorKind = Zum::ActorKind::User, .actorID = "admin",
      .appID = 42, .operationIDs = {Zum::MgmtOp::roleQuery}, .roleIDs = {45}},
    .ifMatch = "\"v1\"", .updated = 140,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::adminAccessSet, .idempotencyKey = "access-put"}},
    27, "zum.admin_access");
  Zum::ProviderAdd providerAdd{.before = Zum::Provider{.id = 46, .version = 0},
    .values = Zum::Provider{.id = 46, .name = "upstream", .issuer = "https://id.example/",
      .clientID = "client", .clientSecret = Zum::Bytes{ZuBSpan{"ciphertext"}},
      .roleClaim = "roles"}, .updated = 141,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::providerAdd, .idempotencyKey = "provider-add"}};
  providerAdd.validate([](bool ok) { ZuCheck(ok); });
  auto providerResult = providerAdd.result();
  ZuCheck(providerResult.id == 46 && providerResult.version == 1 &&
    providerResult.state == Zum::State::Active && providerResult.created == 141 &&
    providerResult.updated == 141 && !providerResult.owner &&
    providerResult.clientSecret == ZuBSpan{"ciphertext"});
  providerAdd.before.version = 1;
  providerAdd.validate([](bool ok) { ZuCheck(!ok); });
  ZuCheck(providerAdd.error == 409);
  providerAdd.before.version = 0;
  providerAdd.values.name.null();
  providerAdd.validate([](bool ok) { ZuCheck(!ok); });
  ZuCheck(providerAdd.error == 400);
  providerAdd.values.name = "upstream";
  saga = new M{};
  saga->init(ZuMv(providerAdd));
  M::save(saga, payload);
  loaded = M::load(Zum::ProviderAdd::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::ProviderAdd>{})
      return change.values.id == 46 && !change.before.version &&
	change.values.clientSecret == ZuBSpan{"ciphertext"} && !change.error &&
	change.request.idempotencyKey == "provider-add";
    else return false;
  }));
  ZuCheck(M::catalog(28, 1, step));
  ZuCheck(step.table == "zum.provider" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(28, 3, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Update);
  Zum::AudienceAdd audienceAdd{.app = Zum::App{.id = 42, .state = Zum::State::Active},
    .before = Zum::Audience{.id = 47, .appID = 42, .version = 0},
    .values = Zum::Audience{.id = 47, .appID = 42, .name = "ping",
      .uri = "https://ping.example/"}, .updated = 142,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::audienceAdd, .idempotencyKey = "audience-add"}};
  ZuCheck(!audienceAdd.appError(audienceAdd.app));
  audienceAdd.validate([](bool ok) { ZuCheck(ok); });
  auto audienceResult = audienceAdd.result();
  ZuCheck(audienceResult.id == 47 && audienceResult.appID == 42 &&
    audienceResult.state == Zum::State::Active && audienceResult.version == 1 &&
    audienceResult.created == 142 && audienceResult.updated == 142 && !audienceResult.owner);
  audienceAdd.values.appID = 43;
  audienceAdd.validate([](bool ok) { ZuCheck(!ok); });
  ZuCheck(audienceAdd.error == 400);
  audienceAdd.values.appID = 42;
  saga = new M{};
  saga->init(ZuMv(audienceAdd));
  M::save(saga, payload);
  loaded = M::load(Zum::AudienceAdd::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::AudienceAdd>{})
      return change.app.id == 42 && change.values.id == 47 && !change.before.version &&
	change.values.uri == "https://ping.example/" && !change.error &&
	change.request.idempotencyKey == "audience-add";
    else return false;
  }));
  ZuCheck(M::catalog(29, 2, step));
  ZuCheck(step.table == "zum.audience" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(29, 5, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Update);
  auto catalogAdd = []<typename Add>(Add change, unsigned index, ZuCSpan table) {
    ZuCheck(!change.appError(change.app));
    auto result = change.result();
    ZuCheck(result.appID == 42 && result.id == 48 && result.name == "custom" &&
      result.origin == Zum::Origin::Custom && result.version == 1 &&
      result.created == 143 && !result.owner && !result.catalogRevision);
    if constexpr (ZuIsSame<Add, Zum::RoleAdd>{}) {
      ZuCheck(!result.actions);
      change.validate([](bool ok) { ZuCheck(ok); });
    } else ZuCheck(result.audienceID == 47);
    change.values.appID = 43;
    change.validate([](bool ok) { ZuCheck(!ok); });
    ZuCheck(change.error == 400);
    change.values.appID = 42;
    ZmRef<M> saved = new M{};
    saved->init(ZuMv(change));
    Zdb_::SagaPayload bytes;
    M::save(saved, bytes);
    auto restored = M::load(typename Add::Type{}(), bytes);
    ZuCheck(bool(restored));
    ZuCheck(restored->u.cdispatch([](auto, const auto &value) {
      if constexpr (ZuIsSame<ZuDecay<decltype(value)>, Add>{})
        return value.app.id == 42 && value.values.id == 48 && !value.before.version &&
          !value.error && value.request.idempotencyKey == "catalog-add";
      else return false;
    }));
    Zdb_::SagaTypeStep info;
    ZuCheck(M::catalog(index, 2, info));
    ZuCheck(info.table == table && info.op == ZdbSagaOp::Insert);
    ZuCheck(M::catalog(index, 5, info));
    ZuCheck(info.table == "zum.request" && info.op == ZdbSagaOp::Update);
  };
  catalogAdd(Zum::RoleAdd{
    .app = Zum::App{.id = 42, .state = Zum::State::Active},
    .before = Zum::Role{.appID = 42, .id = 48, .version = 0},
    .values = Zum::Role{.appID = 42, .id = 48, .name = "custom"}, .updated = 143,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::roleAdd, .idempotencyKey = "catalog-add"}}, 30, "zum.role");
  Zum::UserInvite invitation{.before = Zum::User{.id = 49, .version = 0},
    .values = Zum::User{.id = 49, .name = "new-user", .email = "user@example.test"},
    .grant = Zum::Grant{.id = Zum::Bytes{ZuBSpan{"grant-id"}}, .userID = 49,
      .created = 144, .expires = 200, .kind = Zum::GrantKind::Capability,
      .purpose = Zum::GrantPurpose::Enrollment, .state = Zum::State::Active,
      .issuer = "https://id.example/", .digest = Zum::Bytes{ZuBSpan{"digest"}},
      .userName = "new-user", .label = "Zum passkey", .actor = "precreated"},
    .updated = 144, .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::userInvite, .idempotencyKey = "invite"},
    .external = Zum::User{.id = 50, .source = Zum::UserSource::External,
      .name = "new-user", .authVersion = 7, .version = 9}};
  invitation.validate([](bool ok) { ZuCheck(ok); });
  auto invitedUser = invitation.result();
  ZuCheck(invitedUser.id == 49 && invitedUser.source == Zum::UserSource::Local &&
    invitedUser.state == Zum::State::Pending && !invitedUser.handle &&
    invitedUser.version == 1 && invitedUser.created == 144 && !invitedUser.owner);
  invitation.grant.userID = 50;
  invitation.validate([](bool ok) { ZuCheck(!ok); });
  ZuCheck(invitation.error == 400);
  invitation.grant.userID = 49;
  saga = new M{};
  saga->init(ZuMv(invitation));
  M::save(saga, payload);
  loaded = M::load(Zum::UserInvite::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::UserInvite>{})
      return change.values.id == 49 && !change.before.version && !change.error &&
	change.updated == 144 && change.grant.expires == 200 &&
	change.grant.digest == ZuBSpan{"digest"} && change.grant.userID == 49 &&
	change.request.idempotencyKey == "invite" && change.external.id == 50 &&
	change.external.authVersion == 7 && change.external.version == 9;
    else return false;
  }));
  ZuCheck(M::catalog(31, 1, step));
  ZuCheck(step.table == "zum.user" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(31, 2, step));
  ZuCheck(step.table == "zum.grant" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(31, 7, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Update);
  Zum::ClientAdd clientAdd{.app = Zum::App{.id = 42, .state = Zum::State::Active},
    .before = Zum::Client{.id = "cli-native", .appID = 42, .version = 0},
    .values = Zum::Client{.id = "cli-native", .appID = 42,
      .redirects = {"http://127.0.0.1/callback"}, .type = Zum::ClientType::Native,
      .grants = Zum::ClientGrant::AuthorizationCode}, .updated = 145,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::clientAdd, .idempotencyKey = "client-add"}};
  clientAdd.validate([](bool ok) { ZuCheck(ok); });
  auto newClient = clientAdd.result();
  ZuCheck(newClient.id == "cli-native" && newClient.appID == 42 &&
    !newClient.secretDigest && newClient.authMethod == Zum::ClientAuthMethod::None &&
    newClient.state == Zum::State::Active && newClient.created == 145 && newClient.version == 1);
  clientAdd.values.type = Zum::ClientType::Confidential;
  clientAdd.validate([](bool ok) { ZuCheck(!ok); });
  ZuCheck(clientAdd.error == 400);
  clientAdd.values.type = Zum::ClientType::Native;
  clientAdd.values.grants = Zum::ClientGrant::ClientCredentials;
  clientAdd.validate([](bool ok) { ZuCheck(!ok); });
  ZuCheck(clientAdd.error == 400);
  clientAdd.values.grants = Zum::ClientGrant::AuthorizationCode;
  saga = new M{};
  saga->init(ZuMv(clientAdd));
  M::save(saga, payload);
  loaded = M::load(Zum::ClientAdd::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::ClientAdd>{})
      return change.app.id == 42 && change.values.id == "cli-native" &&
	!change.before.version && !change.error && !change.values.secretDigest &&
	change.request.idempotencyKey == "client-add";
    else return false;
  }));
  ZuCheck(M::catalog(32, 2, step));
  ZuCheck(step.table == "zum.client" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(32, 5, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Update);
  Zum::KeyAdd keyAdd{.before = Zum::SignKey{.id = "key-new", .version = 0},
    .values = Zum::SignKey{.id = "key-new", .issuer = "issuer", .algorithm = "ES256",
      .providerRef = "test-key-provider", .publicJwk = testJwk("key-new"), .notBefore = 146},
    .updated = 146, .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::signKeyAdd, .idempotencyKey = "key-add"}};
  ZuCheck(!keyAdd.validate());
  Zum::Bytes decodedKey;
  auto publicJwk = keyAdd.values.publicJwk;
  ZuCheck(Zum::signKeyPublic(keyAdd.values, decodedKey) &&
    decodedKey.length() == Ztls::COSE::ES256::PublicKeySize &&
    Ztls::COSE::ES256::validPK(decodedKey) &&
    keyAdd.values.publicJwk == publicJwk);
  keyAdd.values.publicJwk = testJwk("wrong-key");
  ZuCheck(keyAdd.validate() == 400);
  ZuCheck(!Zum::signKeyPublic(keyAdd.values, decodedKey) && !decodedKey);
  keyAdd.values.publicJwk = testJwk("key-new");
  keyAdd.values.publicJwk.length(keyAdd.values.publicJwk.length() - 1);
  keyAdd.values.publicJwk << ",\"d\":\"private-material\"}";
  ZuCheck(keyAdd.validate() == 400);
  ZuCheck(!Zum::signKeyPublic(keyAdd.values, decodedKey) && !decodedKey);
  keyAdd.values.publicJwk = "not-json";
  ZuCheck(keyAdd.validate() == 400);
  keyAdd.values.publicJwk = testJwk("key-new");
  keyAdd.values.privateMaterial = Zum::Bytes{ZuBSpan{"ciphertext"}};
  {
    Ztls::Random keyRng;
    ZuCheck(keyRng.init());
    // Scalar one derives the public generator point used by testJwk.
    uint8_t scalar[Ztls::COSE::ES256::CoordinateSize]{};
    scalar[sizeof(scalar) - 1] = 1;
    ZuCheck(Zum::signKeyMatch(keyRng, keyAdd.values, scalar));
    scalar[sizeof(scalar) - 1] = 2;
    ZuCheck(!Zum::signKeyMatch(keyRng, keyAdd.values, scalar));
    ZuClear(scalar, sizeof(scalar));
  }
  ZuCheck(keyAdd.validate() == 400);
  keyAdd.values.providerRef.null();
  ZuCheck(!keyAdd.validate());
  auto newKey = keyAdd.result();
  ZuCheck(newKey.id == "key-new" && newKey.privateMaterial == ZuBSpan{"ciphertext"} &&
    newKey.state == Zum::State::Active && newKey.version == 1 && newKey.created == 146);
  keyAdd.before.version = 1;
  ZuCheck(keyAdd.validate() == 409);
  keyAdd.before.version = 0;
  saga = new M{};
  saga->init(ZuMv(keyAdd));
  M::save(saga, payload);
  loaded = M::load(Zum::KeyAdd::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::KeyAdd>{})
      return change.values.id == "key-new" && !change.before.version &&
	change.values.privateMaterial == ZuBSpan{"ciphertext"} &&
	change.request.idempotencyKey == "key-add";
    else return false;
  }));
  ZuCheck(M::catalog(33, 1, step));
  ZuCheck(step.table == "zum.sign_key" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(33, 2, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Update);

  Zum::Revoke revoke{.updated = 147,
    .request = Zum::IdemRequest{.actorKind = Zum::ActorKind::User,
      .actorID = "admin", .operation = Zum::MgmtOp::grantRevoke,
      .idempotencyKey = "revoke"}};
  Zum::Grant grant;
  grant.id = Zum::Bytes{ZuBSpan{"revoke-grant"}};
  grant.state = Zum::State::Active;
  revoke.grants.push(Zum::SagaImage::save(grant));
  grant.state = Zum::State::Consumed;
  revoke.grants.push(Zum::SagaImage::save(grant));
  grant.state = Zum::State::Revoked;
  revoke.grants.push(Zum::SagaImage::save(grant));
  grant.state = Zum::State::Active;
  grant.owner = 1;
  revoke.grants.push(Zum::SagaImage::save(grant));
  ZuCheck(revoke.count() == 1);
  saga = new M{};
  saga->init(ZuMv(revoke));
  M::save(saga, payload);
  loaded = M::load(Zum::Revoke::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::Revoke>{})
      return change.grants.length() == 4 && change.count() == 1 &&
	change.updated == 147 && change.request.idempotencyKey == "revoke";
    else return false;
  }));
  ZuCheck(M::catalog(34, 3, step));
  ZuCheck(step.table == "zum.grant" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(34, 7, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Update);

  grant.owner = 0;
  grant.expires = 146;
  Zum::GrantCleanup cleanup{.updated = 147};
  cleanup.grants.push(Zum::SagaImage::save(grant));
  saga = new M{};
  saga->init(ZuMv(cleanup));
  M::save(saga, payload);
  loaded = M::load(Zum::GrantCleanup::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::GrantCleanup>{}) {
      Zum::Grant before;
      return change.updated == 147 && change.grants.length() == 1 &&
	Zum::SagaImage::load(change.grants[0], before) && before.expires == 146;
    } else return false;
  }));
  ZuCheck(M::catalog(35, 1, step));
  ZuCheck(step.table == "zum.grant" && step.op == ZdbSagaOp::Delete);
  ZuCheck(M::catalog(35, 2, step));
  ZuCheck(step.table == "zum.request" && step.op == ZdbSagaOp::Update);

  Zum::ConsentCode consentCode{
    .beforeGrant = Zum::Grant{.id = Zum::Bytes{ZuBSpan{"consent-code-001"}}},
    .beforeConsent = Zum::Consent{.version = 0},
    .roleIDs = {7},
    .now = 148};
  consentCode.afterGrant = consentCode.beforeGrant;
  consentCode.afterGrant.kind = Zum::GrantKind::Code;
  consentCode.afterGrant.digest = Zum::Bytes{ZuBSpan{"code-digest"}};
  auto consentLogic = consentCode;
  consentLogic.beforeConsent = consentLogic.result();
  consentLogic.beforeConsent.roleIDs.push(8);
  consentLogic.roleIDs = {8, 9};
  auto mergedConsent = consentLogic.result();
  ZuCheck((mergedConsent.roleIDs == Zum::IDVec{7, 8, 9} && mergedConsent.version == 2));
  consentLogic.beforeConsent.state = Zum::State::Revoked;
  mergedConsent = consentLogic.result();
  ZuCheck((mergedConsent.roleIDs == Zum::IDVec{8, 9} &&
    mergedConsent.state == Zum::State::Active && mergedConsent.created == 148));
  saga = new M{};
  saga->init(ZuMv(consentCode));
  M::save(saga, payload);
  loaded = M::load(Zum::ConsentCode::Type{}(), payload);
  ZuCheck(bool(loaded));
  ZuCheck(loaded->u.cdispatch([](auto, const auto &change) {
    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Zum::ConsentCode>{})
      return change.now == 148 && !change.beforeConsent.version &&
	change.roleIDs.length() == 1 && change.roleIDs[0] == 7 &&
	change.result().created == 148 && change.afterGrant.kind == Zum::GrantKind::Code &&
	change.afterGrant.digest == ZuBSpan{"code-digest"};
    else return false;
  }));
  ZuCheck(M::catalog(36, 0, step));
  ZuCheck(step.table == "zum.grant" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(36, 1, step));
  ZuCheck(step.table == "zum.consent" && step.op == ZdbSagaOp::Insert);
  ZuCheck(M::catalog(36, 2, step));
  ZuCheck(step.table == "zum.consent" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(36, 3, step));
  ZuCheck(step.table == "zum.grant" && step.op == ZdbSagaOp::Update);
  ZuCheck(M::catalog(36, 4, step));
  ZuCheck(step.table == "zum.consent" && step.op == ZdbSagaOp::Update);
  ZuCheck(!M::catalog(36, 5, step));
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
      Zum::String{"issuer"}, Zum::AppID{8},
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
      Zum::String{"issuer"}, Zum::AppID{8},
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
    Zum::String &code, uint64_t authVersion = 10)
{
  return ZmBlock<bool>{}([
    context, &rng, id = ZuMv(id),
    bindingDigest = ZuMv(bindingDigest), userID, credentialID, &code, authVersion
  ](auto wake) mutable {
    ZtBitmap actions{8U};
    actions.set(3);
    Zum::IDVec roleIDs;
    roleIDs.push(7);
    Zum::authorizationFinish(context, rng, ZuMv(id), ZuMv(bindingDigest),
      userID, Zum::Bytes{credentialID}, ZuMv(roleIDs), ZuMv(actions),
      authVersion, 1, 120, 180, Zum::Evidence{}, [
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
	Zum::IDVec requestedRoleIDs = row->data().requestedRoleIDs;
	ZtBitmap actions = row->data().actions;
	if (!Zum::codeMatches(row->data(), codeDigest,
	    "browser", "https://app/cb",
	    "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk", 125)) {
	  wake(false);
	  return;
	}
	wake(Zum::codeFamilyPrepare(rng, row->data(), codeDigest,
	  row->data().scope, ZuMv(requestedRoleIDs), ZuMv(actions), 10, 125, 1000,
	  family, refreshToken));
      });
    });
  });
}

static Zum::RefreshRotate::T rotateRefresh(
    Zum::DBContext *context, Ztls::Random &rng, ZuCSpan refreshToken,
    Zum::String &nextToken, Zum::AppID appID = 8)
{
  Zum::Bytes familyID, digest;
  if (!Zum::opaqueParse(refreshToken, familyID, digest))
    return Zum::RefreshRotate::Invalid;
  return ZmBlock<Zum::RefreshRotate::T>{}([
    context, &rng, familyID = ZuMv(familyID), digest = ZuMv(digest),
    &nextToken, appID
  ](auto wake) mutable {
    Zum::IDVec requestedRoleIDs;
    requestedRoleIDs.push(7);
    ZtBitmap actions{8U};
    actions.set(3);
    Zum::refreshFinish(context, rng, ZuMv(familyID), ZuMv(digest),
      Zum::String{"issuer"}, appID, Zum::String{"read"},
      ZuMv(requestedRoleIDs), ZuMv(actions),
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
	  row->data().state == state && row->data().requestedRoleIDs.length() == 1 &&
	  row->data().requestedRoleIDs[0] == 7 && row->data().actions[3]);
      });
    });
  });
}

static bool insertIssuer(
    Zum::DBContext *context, ZuCSpan id)
{
  return ZmBlock<bool>{}([context, id](auto wake) mutable {
    context->issuers->run(0, [context, id, wake = ZuMv(wake)]() mutable {
      ZdbRowRef<Zum::Issuer> row =
	new ZdbRow<Zum::Issuer>{context->issuers, ZdbShard{0}};
      context->issuers->insert(row, [id, wake = ZuMv(wake)](
	  ZdbRow<Zum::Issuer> *row) mutable {
	if (!row) { wake(false); return; }
	new (row->ptr()) Zum::Issuer{.id = Zum::String{id}};
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
    Zum::DBContext *context, Zum::Grant grant, Zum::Client client,
    int64_t now = 200)
{
  return ZmBlock<AuthorityResult>{}([
    context, grant = ZuMv(grant), client = ZuMv(client), now
  ](auto wake) mutable {
    Zum::loadGrantAuth(
      context, ZuMv(grant), ZuMv(client), false, {}, now, [
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
    Zum::String query, int64_t now = 200,
    Zum::String issuer = "issuer")
{
  Zum::AuthorizeConfig config{
    .issuer = ZuMv(issuer), .appID = 9, .rpID = "login.example",
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
    Zum::Requests *requests, Zum::DB *db, Zum::DBContext *context,
    Ztls::Random &rng,
    Zum::Bytes ceremonyID, Zum::AssertionInput input,
    Zum::ActionID action, int64_t now = 210,
    Zum::String issuer = "issuer/oauth2/9", Zum::AppID appID = 9,
    bool approveConsent = false)
{
  Zum::AuthorizeFinishConfig config{
    .issuer = ZuMv(issuer), .appID = appID,
    .origin = "https://example.com", .rpID = "example.com",
    .now = now, .codeExpires = now + 290, .consent = approveConsent};
  Zum::PolicyFn policy{[action](
      const Zum::User &, const Zum::Client &,
      const Zum::ScopeSelection &, const ZtBitmap &allowed,
      Zum::PolicyDoneFn complete) mutable {
    ZtBitmap actions{allowed.length()};
    if (allowed[action]) actions.set(action);
    complete(true, ZuMv(actions));
  }};
  return ZmBlock<FinishedAuthorization>{}([
    requests, db, context, &rng, ceremonyID = ZuMv(ceremonyID),
    input = ZuMv(input), config = ZuMv(config), policy = ZuMv(policy),
    approveConsent
  ](auto wake) mutable {
    Zum::Bytes consentID = ceremonyID;
    Zum::Bytes consentBinding{ZuBSpan{"browser binding"}};
    auto consentConfig = config;
    auto consentPolicy = policy;
    if (!Zum::authorizeFinish(requests, Zm::now() + ZuTime{10},
      context, rng, ZuMv(ceremonyID),
      Zum::Bytes{ZuBSpan{"browser binding"}}, ZuMv(input), ZuMv(config),
      ZuMv(policy), [requests, db, context, &rng, wake = ZuMv(wake),
          approveConsent, consentID = ZuMv(consentID),
          consentBinding = ZuMv(consentBinding),
          consentConfig = ZuMv(consentConfig),
          consentPolicy = ZuMv(consentPolicy)](
	  int error, Zum::String location) mutable {
	if (error == Zum::AuthorizeIssue::Consent && approveConsent) {
	  auto consentComplete = [wake = ZuMv(wake)](
	      int error, Zum::String location) mutable {
	    wake(FinishedAuthorization{error, ZuMv(location)});
	  };
	  auto fallback = consentComplete;
	  if (!Zum::authorizeConsentFinish(requests, Zm::now() + ZuTime{10},
      db, context, rng, ZuMv(consentID), ZuMv(consentBinding), true,
	      ZuMv(consentConfig), ZuMv(consentPolicy), ZuMv(consentComplete)))
	    fallback(Zum::OAuthError::TemporarilyUnavailable, {});
	  return;
	}
	wake(FinishedAuthorization{error, ZuMv(location)});
      }))
      wake(FinishedAuthorization{});
  });
}

static FinishedAuthorization finishOIDCAuthorization(
    Zum::Requests *requests, Zum::DBContext *context, Ztls::Random &rng,
    Zum::Bytes ceremonyID, Zum::User user, Zum::IDVec roleIDs,
    Zum::Evidence evidence, Zum::ActionID action, int64_t authTime,
    int64_t now = 200, Zum::String issuer = "issuer/oauth2/9",
    Zum::AppID appID = 9)
{
  Zum::AuthorizeFinishConfig config{.issuer = ZuMv(issuer), .appID = appID,
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
    user = ZuMv(user), roleIDs = ZuMv(roleIDs), evidence = ZuMv(evidence),
    authTime,
    config = ZuMv(config), policy = ZuMv(policy)
  ](auto wake) mutable {
    if (!Zum::authorizeOIDCFinish(requests, Zm::now() + ZuTime{10},
      context, rng, ZuMv(ceremonyID),
      Zum::Bytes{ZuBSpan{"oidc binding"}}, ZuMv(user), ZuMv(roleIDs),
      ZuMv(evidence), authTime, ZuMv(config), ZuMv(policy), [wake](
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
      const Zum::SignKey &record, ZuBSpan digest, Zum::SignatureFn complete) mutable {
    Zum::Bytes signature;
    auto result = key.sign(rng, digest, [&signature](ZuBSpan der) {
      signature = Zum::Bytes{der};
    });
    if (record.providerRef != "test-key" || result.template is<ZeException>())
      signature = {};
    complete(ZuMv(signature));
  }};
}

static IssuedToken issueTokenRequest(
    Zum::Requests *requests, TestDB *db, Zum::DBContext *context,
    Ztls::Random &rng,
    Zum::String form, Zum::String authorization,
    Ztls::PK::SK_EC &key, int64_t now,
    Zum::String issuer = "issuer")
{
  Zum::TokenConfig config{
    .issuer = ZuMv(issuer),
    .appID = 9,
    .maxKeys = 64,
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

static Zum::IdemRequest loadRequest(
    Zum::DBContext *context, Zum::ActorKind::T actorKind,
    ZuCSpan actorID, Zum::ActionID operation, ZuCSpan key)
{
  return ZmBlock<Zum::IdemRequest>{}([
    context, actorKind, actorID, operation, key
  ](auto wake) mutable {
    context->requests->run(0, [
      context, actorKind, actorID, operation, key, wake = ZuMv(wake)
    ]() mutable {
      context->requests->find<0>(0,
	ZuFwdTuple(actorKind, actorID, operation, key), [wake = ZuMv(wake)](
	    ZdbRowRef<Zum::IdemRequest> row) mutable {
	  wake(row ? Zum::IdemRequest{row->data()} : Zum::IdemRequest{});
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
      context->roles->find<0>(0, ZuFwdTuple(Zum::AppID{0}, id), [
	wake = ZuMv(wake)
      ](ZdbRowRef<Zum::Role> row) mutable {
	wake(row ? Zum::Role{row->data()} : Zum::Role{});
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
    Zum::DBContext *context, Zum::ActionID id, Zum::AppID appID = 0)
{
  return ZmBlock<Zum::Action>{}([context, id, appID](auto wake) mutable {
    context->actions->run(0, [context, id, appID, wake = ZuMv(wake)]() mutable {
      context->actions->find<0>(0, ZuFwdTuple(appID, id), [
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

// Only the logger thread accesses these rows while logging is running.
static Zum::StringVec logRows;

template <typename ...Args>
static bool logged(Args &&...args)
{
  Zum::StringVec needles;
  (needles.push(Zum::String{ZuFwd<Args>(args)}), ...);
  return ZmBlock<bool>{}([needles = ZuMv(needles)](auto wake) mutable {
    // The marker executes after previously enqueued events, without sleeps.
    ZiLOG(Info, "ZumTest", ([needles = ZuMv(needles),
        wake = ZuMv(wake)](auto &) mutable {
      bool found = false;
      for (const auto &row : logRows) {
        bool match = true;
        for (const auto &needle : needles)
          if (!needle || row.find(needle) < 0) { match = false; break; }
        if (match) { found = true; break; }
      }
      wake(found);
    }));
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
	  .digest = Zum::Bytes{ZuBSpan{"code digest"}},
	  .scope = "read"
	};
	wake(row->commit());
      });
    });
  });
}

template <typename T>
static bool runSaga(TestDB *db, T data, ZdbSagaID id)
{
  using M = Zum::MSaga;
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
	bool ok = complete ? !code : code &&
	  code->data().kind == Zum::GrantKind::Code &&
	  code->data().state == Zum::State::Active && !code->data().owner;
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
    bool expected, int64_t now = 130, Zum::AppID appID = 8)
{
  return ZmBlock<bool>{}([
    context, familyID, authVersion, expected, now, appID
  ](auto wake) mutable {
    Zum::TokenResponse response{
      .accessToken = "signed.jwt", .refreshToken = "refresh",
      .scope = "read", .expiresIn = 300};
    Zum::tokenRelease(context, Zum::String{"issuer"}, appID,
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
  ZiLog::init("ZumTest");
  ZiLog::level(0);
  logRows.null();
  ZiLog::sink(ZiLog::lambdaSink([](
      ZeLogBuf &buf, const ZeEventInfo &info) {
    if (info.component == "Zum")
      logRows.push(Zum::String{" "} << ZuCSpan{buf.data(), buf.length()} << " ");
  }));
  ZiLog::start();
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
  ZuCheck(insertRecord(context->apps, Zum::App{
    .id = 9003, .name = "action-saga", .state = Zum::State::Active,
    .nextActionID = 0, .authVersion = 7, .version = 3,
    .created = 90, .updated = 100}));
  auto actionAddState = [context = context.ptr()](unsigned count, uint64_t version,
      uint64_t authVersion, int64_t updated) {
    return ZmBlock<bool>{}([context, count, version, authVersion, updated](
        auto wake) mutable {
      context->apps->run(0, [context, count, version, authVersion, updated,
          wake = ZuMv(wake)]() mutable {
        context->apps->find<0>(0, ZuFwdTuple(Zum::AppID{9003}),
          [count, version, authVersion, updated, wake = ZuMv(wake)](
              ZdbRowRef<Zum::App> row) mutable {
            wake(row && !row->data().owner &&
              row->data().nextActionID == count &&
              row->data().version == version &&
              row->data().authVersion == authVersion &&
              row->data().updated == updated);
          });
      });
    });
  };
  ZuCheck(runSaga(db, Zum::AppActionAdd{.appID = 9003, .actionID = 0,
    .name = "first", .label = "First action", .created = 101,
    .oldAppVersion = 3, .oldAuthVersion = 7, .oldUpdated = 100},
    ZdbSagaID{9003}));
  ZuCheck(actionAddState(1, 4, 8, 101));
  auto firstAction = loadAction(context, 0, 9003);
  ZuCheck(firstAction.appID == 9003 && firstAction.name == "first" &&
    firstAction.label == "First action" && !firstAction.owner &&
    firstAction.state == Zum::State::Active &&
    firstAction.origin == Zum::Origin::Custom);
  // A stale app snapshot cannot allocate another position.
  ZuCheck(!runSaga(db, Zum::AppActionAdd{.appID = 9003, .actionID = 0,
    .name = "stale", .created = 102, .oldAppVersion = 3,
    .oldAuthVersion = 7, .oldUpdated = 100}, ZdbSagaID{9004}));
  ZuCheck(actionAddState(1, 4, 8, 101));
  // Duplicate names fail after reserving the position; compensation must
  // restore every app field, preserving the previously published action.
  ZuCheck(!runSaga(db, Zum::AppActionAdd{.appID = 9003, .actionID = 1,
    .name = "first", .created = 102, .oldAppVersion = 4,
    .oldAuthVersion = 8, .oldUpdated = 101}, ZdbSagaID{9005}));
  ZuCheck(actionAddState(1, 4, 8, 101));
  ZuCheck(!loadAction(context, 1, 9003).appID);
  firstAction = loadAction(context, 0, 9003);
  ZuCheck(firstAction.name == "first" && !firstAction.owner &&
    firstAction.label == "First action");
  ZuCheck(runSaga(db, Zum::AppActionAdd{.appID = 9003, .actionID = 1,
    .name = "second", .created = 103, .oldAppVersion = 4,
    .oldAuthVersion = 8, .oldUpdated = 101}, ZdbSagaID{9006}));
  ZuCheck(actionAddState(2, 5, 9, 103));
  // A missing application is a business rejection inside the saga. Its pending
  // request must disappear on rollback, without deactivating the database.
  ZuCheck(!runSaga(db, Zum::AppActionAdd{.appID = 9999,
    .actionID = UINT32_MAX, .name = "missing-app", .created = 104,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::actionAdd, .idempotencyKey = "missing-app",
      .requestDigest = Zum::Bytes{ZuBSpan{"request"}}, .expires = 86400,
      .created = 104, .updated = 104}}, ZdbSagaID{9007}));
  ZuCheck(ZmBlock<bool>{}([context = context.ptr()](auto wake) mutable {
    auto requests = context->requests;
    requests->run(0, [requests, wake = ZuMv(wake)]() mutable {
      requests->find<0>(0, ZuFwdTuple(Zum::ActorKind::User,
        Zum::String{"admin"}, Zum::ActionID(Zum::MgmtOp::actionAdd),
        Zum::String{"missing-app"}), [wake = ZuMv(wake)](
          ZdbRowRef<Zum::IdemRequest> row) mutable { wake(!row); });
    });
  }));
  ZuCheck(actionAddState(2, 5, 9, 103));
  auto secondAction = loadAction(context, 1, 9003);
  // An unchanged catalog still completes its request while skipping business
  // writes; no application version or authority change is warranted.
  Zum::App unchanged{.id = 9003, .state = Zum::State::Active,
    .nextActionID = 2, .authVersion = 9, .version = 5, .updated = 103};
  auto invalidAdminActor = [&db, &unchanged](Zum::ActorKind::T kind,
      Zum::String actorID, ZdbSagaID id) {
    return !runSaga(db, Zum::AdminAccessPut{.app = unchanged,
      .before = Zum::AdminAccess{.actorKind = kind, .actorID = actorID,
	.appID = 9003, .version = 0},
      .values = Zum::AdminAccess{.actorKind = kind, .actorID = ZuMv(actorID),
	.appID = 9003, .operationIDs = {Zum::MgmtOp::roleQuery}},
      .ifNoneMatch = "*", .updated = 104}, id);
  };
  ZuCheck(invalidAdminActor(Zum::ActorKind::User, "99999999", ZdbSagaID{12016}));
  ZuCheck(invalidAdminActor(Zum::ActorKind::User, "41junk", ZdbSagaID{12017}));
  ZuCheck(invalidAdminActor(Zum::ActorKind::Client, "missing-admin-client", ZdbSagaID{12018}));
  ZuCheck(actionAddState(2, 5, 9, 103));
  ZuCheck(runSaga(db, Zum::CatalogPublish{.before = unchanged, .after = unchanged,
    .request = Zum::IdemRequest{.actorKind = Zum::ActorKind::Client,
      .actorID = "publisher", .operation = Zum::MgmtOp::catalogPublish,
      .idempotencyKey = "unchanged", .requestDigest = Zum::Bytes{ZuBSpan{"manifest"}},
      .expires = 86400, .created = 105, .updated = 105}}, ZdbSagaID{9008}));
  ZuCheck(actionAddState(2, 5, 9, 103));
  ZuCheck(ZmBlock<bool>{}([context = context.ptr()](auto wake) mutable {
    auto requests = context->requests;
    requests->run(0, [requests, wake = ZuMv(wake)]() mutable {
      requests->find<0>(0, ZuFwdTuple(Zum::ActorKind::Client,
        Zum::String{"publisher"}, Zum::ActionID(Zum::MgmtOp::catalogPublish),
        Zum::String{"unchanged"}), [wake = ZuMv(wake)](
          ZdbRowRef<Zum::IdemRequest> row) mutable {
          wake(row && row->data().status == Zum::RequestStatus::Complete &&
            !row->data().owner && row->data().updated == 105 &&
            row->data().resultIDs.length() == 1 && row->data().resultIDs[0] == "9003");
        });
    });
  }));
  ZuCheck(secondAction.name == "second" && !secondAction.owner &&
    secondAction.state == Zum::State::Active);
  // Membership validation and its request outcome belong to the same saga.
  ZuCheck(insertRecord(context->users, Zum::User{
    .id = 9100, .name = "membership local",
    .handle = Zum::Bytes{ZuBSpan{"membership local"}}}));
  ZuCheck(insertRecord(context->users, Zum::User{
    .id = 9101, .source = Zum::UserSource::External, .name = "membership external",
    .handle = Zum::Bytes{ZuBSpan{"membership external"}},
    .state = Zum::State::Active}));
  auto addMembership = [&db](Zum::AppID appID, Zum::UserID userID,
      ZuCSpan key, unsigned sagaID) {
    return runSaga(db, Zum::MembershipAdd{.appID = appID,
      .userID = userID, .created = 106,
      .request = Zum::IdemRequest{.actorID = "admin",
        .operation = Zum::MgmtOp::membershipAdd, .idempotencyKey = key,
        .requestDigest = Zum::Bytes{ZuBSpan{"membership"}}, .expires = 86400,
        .version = 1, .created = 106, .updated = 106}}, ZdbSagaID{sagaID});
  };
  auto membershipReq = [context = context.ptr()](ZuCSpan key, bool exists) {
    return ZmBlock<bool>{}([context, key, exists](auto wake) mutable {
      context->requests->run(0, [context, key, exists, wake = ZuMv(wake)]() mutable {
        context->requests->find<0>(0, ZuFwdTuple(Zum::ActorKind::User,
          Zum::String{"admin"}, Zum::ActionID(Zum::MgmtOp::membershipAdd), key),
          [exists, wake = ZuMv(wake)](ZdbRowRef<Zum::IdemRequest> row) mutable {
            wake(exists ? row && row->data().status == Zum::RequestStatus::Complete &&
              !row->data().owner && row->data().version == 2 &&
              row->data().resultIDs.length() == 1 &&
              row->data().resultIDs[0] == "9003:9100" : !row);
          });
      });
    });
  };
  auto membershipState = [context = context.ptr()](Zum::AppID appID,
      Zum::UserID userID, bool exists) {
    return ZmBlock<bool>{}([context, appID, userID, exists](auto wake) mutable {
      context->memberships->run(0, [context, appID, userID, exists,
          wake = ZuMv(wake)]() mutable {
        context->memberships->find<0>(0, ZuFwdTuple(appID, userID),
          [exists, wake = ZuMv(wake)](ZdbRowRef<Zum::Membership> row) mutable {
            wake(exists ? row && row->data().state == Zum::State::Active &&
              !row->data().owner && !row->data().roleIDs &&
              row->data().version == 1 && row->data().created == 106 &&
              row->data().updated == 106 : !row);
          });
      });
    });
  };
  ZuCheck(addMembership(9003, 9100, "membership-created", 9100));
  ZuCheck(membershipReq("membership-created", true));
  ZuCheck(membershipState(9003, 9100, true));
  ZuCheck(!addMembership(9003, 9100, "membership-duplicate", 9101));
  ZuCheck(membershipReq("membership-duplicate", false));
  ZuCheck(membershipState(9003, 9100, true));
  ZuCheck(!addMembership(9999, 9100, "membership-no-app", 9102));
  ZuCheck(membershipReq("membership-no-app", false));
  ZuCheck(membershipState(9999, 9100, false));
  ZuCheck(!addMembership(9003, 9999, "membership-no-user", 9103));
  ZuCheck(membershipReq("membership-no-user", false));
  ZuCheck(membershipState(9003, 9999, false));
  ZuCheck(!addMembership(9003, 9101, "membership-external", 9104));
  ZuCheck(membershipReq("membership-external", false));
  ZuCheck(membershipState(9003, 9101, false));
  // Replacing an assignment with itself persists its request result without
  // advancing either membership or application authority/version.
  ZuCheck(runSaga(db, Zum::MembershipChange{.appID = 9003, .userID = 9100,
    .oldState = Zum::State::Active, .newState = Zum::State::Active,
    .version = 1, .authVersion = 1, .oldUpdated = 106, .updated = 107,
    .appVersion = 5, .appAuthVersion = 9, .appUpdated = 103,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::membershipRoles, .idempotencyKey = "same-roles",
      .expires = 86400, .version = 1, .created = 107, .updated = 107},
    .assignRoles = true, .ifMatch = "\"v1\""},
    ZdbSagaID{9105}));
  ZuCheck(membershipState(9003, 9100, true));
  ZuCheck(actionAddState(2, 5, 9, 103));
  ZuCheck(ZmBlock<bool>{}([context = context.ptr()](auto wake) mutable {
    context->requests->run(0, [context, wake = ZuMv(wake)]() mutable {
      context->requests->find<0>(0, ZuFwdTuple(Zum::ActorKind::User,
        Zum::String{"admin"}, Zum::ActionID(Zum::MgmtOp::membershipRoles),
        Zum::String{"same-roles"}), [wake = ZuMv(wake)](
          ZdbRowRef<Zum::IdemRequest> row) mutable {
          wake(row && row->data().status == Zum::RequestStatus::Complete &&
            !row->data().owner && row->data().version == 2 && !row->data().resultIDs);
        });
    });
  }));
  ZuCheck(insertRecord(context->roles, Zum::Role{
    .appID = 9003, .id = 500, .name = "assignable", .state = Zum::State::Active}));
  // An unchanged request still obeys its ETag and application preconditions.
  for (unsigned test = 0; test < 4; ++test) {
    Zum::String key;
    key << "bad-member-state-" << test;
    ZuCheck(!runSaga(db, Zum::MembershipChange{
      .appID = Zum::AppID(test == 3 ? 9999 : 9003), .userID = 9100,
      .oldState = Zum::State::Active,
      .newState = Zum::State::T(test == 1 ? Zum::State::Suspended : Zum::State::Active),
      .version = 1, .authVersion = 1, .oldUpdated = 106, .updated = 108,
      .appVersion = uint64_t(test == 2 ? 4 : 5), .appAuthVersion = 9, .appUpdated = 103,
      .request = Zum::IdemRequest{.actorID = "admin",
        .operation = Zum::MgmtOp::membershipState, .idempotencyKey = key,
        .expires = 86400, .version = 1, .created = 108, .updated = 108},
      .ifMatch = test < 2 ? "\"v99\"" : "\"v1\""}, ZdbSagaID{9300 + test}));
    ZuCheck(membershipState(9003, 9100, true));
    ZuCheck(actionAddState(2, 5, 9, 103));
    ZuCheck(ZmBlock<bool>{}([context = context.ptr(), key](auto wake) mutable {
      context->requests->run(0, [context, key, wake = ZuMv(wake)]() mutable {
        context->requests->find<0>(0, ZuFwdTuple(Zum::ActorKind::User,
          Zum::String{"admin"}, Zum::ActionID(Zum::MgmtOp::membershipState), key),
          [wake = ZuMv(wake)](ZdbRowRef<Zum::IdemRequest> row) mutable {
            wake(!row);
          });
      });
    }));
  }
  ZuCheck(insertRecord(context->roles, Zum::Role{
    .appID = 9003, .id = 501, .name = "disabled", .state = Zum::State::Disabled}));
  ZuCheck(insertRecord(context->roles, Zum::Role{
    .appID = 9003, .id = 502, .name = "deleted", .state = Zum::State::Active,
    .tombstone = true}));
  ZuCheck(insertRecord(context->roles, Zum::Role{
    .appID = 9002, .id = 503, .name = "other-app", .state = Zum::State::Active}));
  // Existing projections/orphan records must not make an external or absent
  // user eligible for local role assignments.
  for (auto userID : Zum::IDVec{9101, 9999})
    ZuCheck(insertRecord(context->memberships, Zum::Membership{
      .appID = 9003, .userID = userID, .state = Zum::State::Active,
      .version = 1, .created = 106, .updated = 106}));
  for (unsigned test = 0; test < 7; ++test) {
    Zum::IDVec roles;
    switch (test) {
      case 0: roles = {500, 500}; break;
      case 1: roles = {501}; break;
      case 2: roles = {502}; break;
      case 3: roles = {503}; break;
      case 4: roles = {0}; break;
      default: roles = {500}; break;
    }
    Zum::UserID userID = test < 5 ? 9100 : test == 5 ? 9101 : 9999;
    Zum::String key;
    key << "bad-role-" << test;
    ZuCheck(!runSaga(db, Zum::MembershipChange{.appID = 9003, .userID = userID,
      .newRoles = ZuMv(roles), .oldState = Zum::State::Active,
      .newState = Zum::State::Active, .version = 1, .authVersion = 1,
      .oldUpdated = 106, .updated = 108, .appVersion = 5,
      .appAuthVersion = 9, .appUpdated = 103,
      .request = Zum::IdemRequest{.actorID = "admin",
        .operation = Zum::MgmtOp::membershipRoles, .idempotencyKey = key,
        .expires = 86400, .version = 1, .created = 108, .updated = 108},
      .assignRoles = true}, ZdbSagaID{9200 + test}));
    ZuCheck(membershipState(9003, userID, true));
    ZuCheck(actionAddState(2, 5, 9, 103));
    ZuCheck(ZmBlock<bool>{}([context = context.ptr(), key](auto wake) mutable {
      context->requests->run(0, [context, key, wake = ZuMv(wake)]() mutable {
        context->requests->find<0>(0, ZuFwdTuple(Zum::ActorKind::User,
          Zum::String{"admin"}, Zum::ActionID(Zum::MgmtOp::membershipRoles), key),
          [wake = ZuMv(wake)](ZdbRowRef<Zum::IdemRequest> row) mutable {
            wake(!row);
          });
      });
    }));
  }
  ZuCheck(insertRecord(context->actions, Zum::Action{
    .appID = 9001, .id = 5, .name = "shared"}));
  for (unsigned test = 0; test < 4; ++test) {
    Zum::String key;
    key << "role-actions-" << test;
    Zum::RoleEdit change{
      .app = Zum::App{.id = 9003, .state = Zum::State::Active,
        .nextActionID = 2, .authVersion = 9, .version = 5, .updated = 103},
      .before = Zum::Role{.appID = 9003, .id = 500, .name = "assignable"},
      .actionIDs = test == 0 ? Zum::ActionIDVec{0, 0} : Zum::ActionIDVec{0, 1},
      .ifMatch = test == 1 ? "\"v99\"" : "\"v1\"", .updated = 109,
      .request = Zum::IdemRequest{.actorID = "admin",
        .operation = Zum::MgmtOp::roleActions, .idempotencyKey = key,
        .expires = 86400, .version = 1, .created = 109, .updated = 109}};
    if (test == 3) {
      change.app.version = 6;
      change.app.authVersion = 10;
      change.app.updated = 109;
      change.before.version = 2;
      change.before.updated = 109;
      change.before.actions.length(2);
      change.before.actions.set(0).set(1);
      change.ifMatch = "\"v2\"";
      change.kind = Zum::RoleEdit::Label;
      change.label = "Edited label";
      change.request.operation = Zum::MgmtOp::roleUpdate;
    }
    ZuCheck(runSaga(db, ZuMv(change), ZdbSagaID{9400 + test}) == (test >= 2));
    ZuCheck(actionAddState(2, test >= 2 ? 4 + test : 5, test >= 2 ? 10 : 9,
      test >= 2 ? 109 : 103));
    ZuCheck(ZmBlock<bool>{}([context = context.ptr(), test](auto wake) mutable {
      context->roles->run(0, [context, test, wake = ZuMv(wake)]() mutable {
        context->roles->find<0>(0, ZuFwdTuple(Zum::AppID{9003}, Zum::RoleID{500}),
          [test, wake = ZuMv(wake)](ZdbRowRef<Zum::Role> row) mutable {
            bool valid = row && !row->data().owner && row->data().name == "assignable" &&
              row->data().version == (test >= 2 ? test : 1);
            if (valid && test >= 2)
              valid = row->data().actions[0] && row->data().actions[1];
            else if (valid) valid = !row->data().actions.length();
            if (valid && test == 3) valid = row->data().label == "Edited label";
            wake(valid);
          });
      });
    }));
    ZuCheck(ZmBlock<bool>{}([context = context.ptr(), key, test](auto wake) mutable {
      context->requests->run(0, [context, key, test, wake = ZuMv(wake)]() mutable {
        context->requests->find<0>(0, ZuFwdTuple(Zum::ActorKind::User,
          Zum::String{"admin"}, Zum::ActionID(test == 3 ?
            Zum::MgmtOp::roleUpdate : Zum::MgmtOp::roleActions), key),
          [test, wake = ZuMv(wake)](ZdbRowRef<Zum::IdemRequest> row) mutable {
            wake(test >= 2 ? row && !row->data().owner &&
              row->data().status == Zum::RequestStatus::Complete &&
              row->data().version == 2 && !row->data().resultIDs : !row);
          });
      });
    }));
  }
  ZuCheck(insertRecord(context->actions, Zum::Action{
    .appID = 9002, .id = 5, .name = "shared"}));
  Zum::App stateApp{.id = 9003, .state = Zum::State::Active, .nextActionID = 2,
    .authVersion = 10, .version = 7, .updated = 109};
  Zum::Role stateRole{.appID = 9003, .id = 500, .name = "assignable",
    .label = "Edited label", .version = 3, .updated = 109};
  stateRole.actions.length(2);
  stateRole.actions.set(0).set(1);
  for (unsigned test = 0; test < 6; ++test) {
    bool valid = test != 0 && test != 4;
    Zum::State::T state = test < 2 || test >= 4 ?
      Zum::State::Active : Zum::State::Disabled;
    Zum::String key, etag;
    key << "role-state-" << test;
    etag << "\"v" << (valid ? stateRole.version : 99) << '"';
    Zum::RoleEdit change{.app = stateApp, .before = stateRole,
      .ifMatch = ZuMv(etag), .updated = 110,
      .request = Zum::IdemRequest{.actorID = "admin",
        .operation = Zum::MgmtOp::roleState, .idempotencyKey = key,
        .expires = 86400, .version = 1, .created = 110, .updated = 110},
      .kind = Zum::RoleEdit::Status, .state = state};
    ZuCheck(runSaga(db, ZuMv(change), ZdbSagaID{9500 + test}) == valid);
    if (valid && state != stateRole.state) {
      stateRole.state = state;
      ++stateRole.version;
      stateRole.updated = 110;
      ++stateApp.version;
      ++stateApp.authVersion;
      stateApp.updated = 110;
    }
    ZuCheck(actionAddState(2, stateApp.version, stateApp.authVersion, stateApp.updated));
    auto role = ZmBlock<Zum::Role>{}([context = context.ptr()](auto wake) mutable {
      context->roles->run(0, [context, wake = ZuMv(wake)]() mutable {
        context->roles->find<0>(0, ZuFwdTuple(Zum::AppID{9003}, Zum::RoleID{500}),
          [wake = ZuMv(wake)](ZdbRowRef<Zum::Role> row) mutable {
            wake(row ? Zum::Role{row->data()} : Zum::Role{});
          });
      });
    });
    ZuCheck(role.id == 500 && !role.owner && role.version == stateRole.version &&
      role.state == stateRole.state && role.updated == stateRole.updated &&
      role.label == stateRole.label && role.actions == stateRole.actions);
    ZuCheck(ZmBlock<bool>{}([context = context.ptr(), key, valid](auto wake) mutable {
      context->requests->run(0, [context, key, valid, wake = ZuMv(wake)]() mutable {
        context->requests->find<0>(0, ZuFwdTuple(Zum::ActorKind::User,
          Zum::String{"admin"}, Zum::ActionID(Zum::MgmtOp::roleState), key),
          [valid, wake = ZuMv(wake)](ZdbRowRef<Zum::IdemRequest> row) mutable {
            wake(valid ? row && row->data().status == Zum::RequestStatus::Complete &&
              !row->data().owner && !row->data().resultIDs : !row);
          });
      });
    }));
  }
  ZuCheck(insertRecord(context->roles, Zum::Role{
    .appID = 9001, .id = 5, .name = "shared"}));
  ZuCheck(insertRecord(context->roles, Zum::Role{
    .appID = 9002, .id = 5, .name = "shared"}));
  ZuCheck(missingAssertion(context) == Zum::WebAuthnError::Ceremony);

  auto stateAction = loadAction(context, 0, 9003);
  for (unsigned test = 0; test < 6; ++test) {
    bool valid = test != 0 && test != 4;
    Zum::State::T state = test < 2 || test >= 4 ?
      Zum::State::Active : Zum::State::Disabled;
    Zum::String key, etag;
    key << "action-state-" << test;
    etag << "\"v" << (valid ? stateAction.version : 99) << '"';
    Zum::ActionEdit change{.app = stateApp, .before = stateAction,
      .ifMatch = ZuMv(etag), .updated = 113,
      .request = Zum::IdemRequest{.actorID = "admin",
        .operation = Zum::MgmtOp::actionState, .idempotencyKey = key,
        .expires = 86400, .version = 1, .created = 113, .updated = 113},
      .state = state};
    ZuCheck(runSaga(db, ZuMv(change), ZdbSagaID{9700 + test}) == valid);
    if (valid && state != stateAction.state) {
      stateAction.state = state;
      ++stateAction.version;
      stateAction.updated = 113;
      ++stateApp.version;
      ++stateApp.authVersion;
      stateApp.updated = 113;
    }
    ZuCheck(actionAddState(2, stateApp.version, stateApp.authVersion, stateApp.updated));
    auto action = loadAction(context, 0, 9003);
    ZuCheck(action.appID == 9003 && !action.id && !action.owner &&
      action.version == stateAction.version && action.state == stateAction.state &&
      action.updated == stateAction.updated && action.created == stateAction.created &&
      action.name == stateAction.name && action.label == stateAction.label &&
      action.origin == stateAction.origin && !action.tombstone);
    ZuCheck(ZmBlock<bool>{}([context = context.ptr(), key, valid](auto wake) mutable {
      context->requests->run(0, [context, key, valid, wake = ZuMv(wake)]() mutable {
        context->requests->find<0>(0, ZuFwdTuple(Zum::ActorKind::User,
          Zum::String{"admin"}, Zum::ActionID(Zum::MgmtOp::actionState), key),
          [valid, wake = ZuMv(wake)](ZdbRowRef<Zum::IdemRequest> row) mutable {
            wake(valid ? row && row->data().status == Zum::RequestStatus::Complete &&
              !row->data().owner && row->data().version == 2 &&
              !row->data().resultIDs : !row);
          });
      });
    }));
  }

  auto appSnapshot = [context = context.ptr()]() {
    return ZmBlock<Zum::App>{}([context](auto wake) mutable {
      context->apps->run(0, [context, wake = ZuMv(wake)]() mutable {
        context->apps->find<0>(0, ZuFwdTuple(Zum::AppID{9003}),
          [wake = ZuMv(wake)](ZdbRowRef<Zum::App> row) mutable {
            wake(row ? Zum::App{row->data()} : Zum::App{});
          });
      });
    });
  };
  auto expectedApp = appSnapshot();
  ZuCheck(expectedApp.id == 9003);
  for (unsigned test = 0; test < 7; ++test) {
    bool valid = test != 0 && test != 5;
    bool stateOnly = test != 2;
    Zum::State::T state = test == 3 || test == 4 ?
      Zum::State::Disabled : Zum::State::Active;
    Zum::ActionID op = stateOnly ? Zum::MgmtOp::appState : Zum::MgmtOp::appUpdate;
    Zum::String key, etag;
    key << "app-change-" << test;
    etag << "\"v" << (valid ? expectedApp.version : 99) << '"';
    Zum::AppChange change{.before = expectedApp, .ifMatch = ZuMv(etag),
      .label = "Updated application", .updated = 114,
      .request = Zum::IdemRequest{.actorID = "admin", .operation = op,
        .idempotencyKey = key, .expires = 86400, .version = 1,
        .created = 114, .updated = 114},
      .stateOnly = stateOnly, .state = state};
    ZuCheck(runSaga(db, ZuMv(change), ZdbSagaID{9800 + test}) == valid);
    if (valid && (!stateOnly || expectedApp.state != state)) {
      if (stateOnly) {
        expectedApp.state = state;
        ++expectedApp.authVersion;
      } else expectedApp.label = "Updated application";
      ++expectedApp.version;
      expectedApp.updated = 114;
    }
    auto app = appSnapshot();
    ZuCheck(app.id == expectedApp.id && !app.owner && app.name == expectedApp.name &&
      app.label == expectedApp.label && app.state == expectedApp.state &&
      app.version == expectedApp.version && app.authVersion == expectedApp.authVersion &&
      app.created == expectedApp.created && app.updated == expectedApp.updated &&
      app.nextActionID == expectedApp.nextActionID &&
      app.catalogRevision == expectedApp.catalogRevision &&
      app.catalogDigest == expectedApp.catalogDigest);
    ZuCheck(ZmBlock<bool>{}([context = context.ptr(), key, op, valid](auto wake) mutable {
      context->requests->run(0, [context, key, op, valid, wake = ZuMv(wake)]() mutable {
        context->requests->find<0>(0, ZuFwdTuple(Zum::ActorKind::User,
          Zum::String{"admin"}, op, key),
          [valid, wake = ZuMv(wake)](ZdbRowRef<Zum::IdemRequest> row) mutable {
            wake(valid ? row && row->data().status == Zum::RequestStatus::Complete &&
              !row->data().owner && row->data().version == 2 &&
              !row->data().resultIDs : !row);
          });
      });
    }));
  }

  auto expectedUser = loadUser(context, 9100);
  ZuCheck(expectedUser.id == 9100 && expectedUser.state == Zum::State::Pending);
  for (unsigned test = 0; test < 7; ++test) {
    bool stateOnly = test >= 4;
    bool valid = test != 3 && test != 4;
    uint8_t fields = test == 0 ? 3 : test == 1 ? 2 : 1;
    Zum::State::T state = test == 4 ? Zum::State::Active : Zum::State::Suspended;
    Zum::ActionID op = stateOnly ? Zum::MgmtOp::userState : Zum::MgmtOp::userUpdate;
    Zum::String key, etag;
    key << "user-edit-" << test;
    etag << "\"v" << (test == 3 ? 99 : expectedUser.version) << '"';
    Zum::UserEdit change{.before = expectedUser, .ifMatch = ZuMv(etag),
      .profile = test == 2 ? "" : "Updated profile", .updated = 115,
      .request = Zum::IdemRequest{.actorID = "admin", .operation = op,
        .idempotencyKey = key, .expires = 86400, .version = 1,
        .created = 115, .updated = 115},
      .stateOnly = stateOnly, .state = state, .email = "updated@example.test",
      .fields = fields};
    ZuCheck(runSaga(db, ZuMv(change), ZdbSagaID{9900 + test}) == valid);
    if (valid && (!stateOnly || expectedUser.state != state)) {
      if (stateOnly) {
        expectedUser.state = state;
        ++expectedUser.authVersion;
      } else {
        if (fields & 1U) expectedUser.profile = test == 2 ? "" : "Updated profile";
        if (fields & 2U) expectedUser.email = "updated@example.test";
      }
      ++expectedUser.version;
      expectedUser.updated = 115;
    }
    auto user = loadUser(context, 9100);
    ZuCheck(user.id == 9100 && !user.owner && user.name == expectedUser.name &&
      user.profile == expectedUser.profile && user.email == expectedUser.email &&
      user.state == expectedUser.state && user.version == expectedUser.version &&
      user.authVersion == expectedUser.authVersion && user.updated == expectedUser.updated &&
      user.source == expectedUser.source && user.handle == expectedUser.handle &&
      user.version == expectedUser.version);
    ZuCheck(ZmBlock<bool>{}([context = context.ptr(), key, op, valid](auto wake) mutable {
      context->requests->run(0, [context, key, op, valid, wake = ZuMv(wake)]() mutable {
        context->requests->find<0>(0, ZuFwdTuple(Zum::ActorKind::User,
          Zum::String{"admin"}, op, key),
          [valid, wake = ZuMv(wake)](ZdbRowRef<Zum::IdemRequest> row) mutable {
            wake(valid ? row && row->data().status == Zum::RequestStatus::Complete &&
              !row->data().owner && row->data().version == 2 &&
              !row->data().resultIDs : !row);
          });
      });
    }));
  }
  auto externalUser = loadUser(context, 9101);
  Zum::String externalETag;
  externalETag << "\"v" << externalUser.version << '"';
  ZuCheck(!runSaga(db, Zum::UserEdit{.before = externalUser,
    .ifMatch = ZuMv(externalETag), .updated = 116,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::userState, .idempotencyKey = "external-state",
      .expires = 86400, .version = 1, .created = 116, .updated = 116},
    .stateOnly = true, .state = Zum::State::Disabled}, ZdbSagaID{9910}));
  auto stillExternal = loadUser(context, 9101);
  ZuCheck(stillExternal.state == externalUser.state &&
    stillExternal.version == externalUser.version &&
    stillExternal.authVersion == externalUser.authVersion && !stillExternal.owner);
  Zum::User pendingUser{.id = 9911, .state = Zum::State::Suspended};
  Zum::UserEdit activation{.before = pendingUser, .ifMatch = "\"v1\"",
    .stateOnly = true, .state = Zum::State::Active};
  ZuCheck(activation.userError(pendingUser) == 409);

  Zum::Cred expectedCred{.id = Zum::Bytes{ZuBSpan{"admin-edit"}},
    .userID = 9100, .publicKey = Zum::Bytes{ZuBSpan{"public-key"}},
    .signCount = 8, .created = 115, .updated = 115,
    .state = Zum::State::Active, .backupEligible = true, .label = "Original"};
  ZuCheck(insertRecord(context->creds, expectedCred));
  for (unsigned test = 0; test < 7; ++test) {
    bool valid = test != 1 && test != 4 && test != 6;
    bool stateOnly = test >= 2 && test <= 5;
    Zum::State::T state = test == 2 || test == 3 ?
      Zum::State::Disabled : Zum::State::Active;
    Zum::ActionID op = stateOnly ?
      Zum::MgmtOp::credentialState : Zum::MgmtOp::credentialUpdate;
    Zum::String key, etag;
    key << "credential-edit-" << test;
    etag << "\"v" << (test == 1 || test == 4 ? 99 : expectedCred.version) << '"';
    Zum::CredEdit change{.before = expectedCred, .ifMatch = ZuMv(etag),
      .label = "Renamed credential", .updated = 116,
      .request = Zum::IdemRequest{.actorID = "admin", .operation = op,
        .idempotencyKey = key, .expires = 86400, .version = 1,
        .created = 116, .updated = 116},
      .stateOnly = stateOnly, .state = state};
    // Authentication counters may have advanced within the same timestamp.
    // Administrative edits must not replace those unrelated fields.
    change.before.signCount = 7;
    change.before.backedUp = true;
    if (test == 6) --change.before.updated;
    ZuCheck(runSaga(db, ZuMv(change), ZdbSagaID{9920 + test}) == valid);
    if (valid && (!stateOnly || expectedCred.state != state)) {
      if (stateOnly) expectedCred.state = state;
      else expectedCred.label = "Renamed credential";
      ++expectedCred.version;
      expectedCred.updated = 116;
    }
    auto cred = loadCred(context, expectedCred.id);
    ZuCheck(cred.id == expectedCred.id && !cred.owner &&
      cred.userID == expectedCred.userID && cred.publicKey == expectedCred.publicKey &&
      cred.label == expectedCred.label && cred.state == expectedCred.state &&
      cred.version == expectedCred.version && cred.updated == expectedCred.updated &&
      cred.created == expectedCred.created && cred.signCount == 8 &&
      cred.backupEligible && !cred.backedUp && cred.userVersion == expectedCred.userVersion);
    ZuCheck(ZmBlock<bool>{}([context = context.ptr(), key, op, valid](auto wake) mutable {
      context->requests->run(0, [context, key, op, valid, wake = ZuMv(wake)]() mutable {
        context->requests->find<0>(0, ZuFwdTuple(Zum::ActorKind::User,
          Zum::String{"admin"}, op, key),
          [valid, wake = ZuMv(wake)](ZdbRowRef<Zum::IdemRequest> row) mutable {
            wake(valid ? row && row->data().status == Zum::RequestStatus::Complete &&
              !row->data().owner && row->data().version == 2 &&
              !row->data().resultIDs : !row);
          });
      });
    }));
  }

  Ztls::Random rng;
  ZuCheck(rng.init());
  Zum::Grant authorization;
  authorization.id = Zum::Bytes{ZuBSpan{"authorization-id"}};
  authorization.issuer = "issuer";
  authorization.appID = 8;
  authorization.clientID = "browser";
  authorization.audience = "orders";
  authorization.redirectURI = "https://app/cb";
  authorization.scope = "read";
  authorization.requestedRoleIDs.push(7);
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
  ZuCheck(insertIssuer(context, "issuer"));
  ZuCheck(insertRecord(context->apps, Zum::App{
    .id = 8, .name = "code-app", .state = Zum::State::Active,
    .authVersion = 10}));
  ZuCheck(insertRecord(context->apps, Zum::App{
    .id = 20, .name = "independent-app", .state = Zum::State::Active,
    .authVersion = 17}));
  ZuCheck(insertRecord(context->apps, Zum::App{
    .id = 21, .name = "disabled-app", .state = Zum::State::Disabled,
    .authVersion = 10}));
  ZuCheck(insertRecord(context->apps, Zum::App{
    .id = 22, .name = "owned-app", .state = Zum::State::Active,
    .authVersion = 10, .owner = 1}));
  ZuCheck(insertRecord(context->apps, Zum::App{
    .id = 24, .name = "other-app", .state = Zum::State::Active,
    .authVersion = 10}));
  ZuCheck(insertRecord(context->signKeys, Zum::SignKey{
    .id = "token-key",
    .issuer = "issuer",
    .algorithm = "ES256",
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
      context, "issuer", 100, 4, [wake = ZuMv(wake)](
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
  Zum::Grant collisionCeremony{.id = Zum::Bytes{ZuBSpan{"collision-ceremony"}},
    .created = 100, .expires = 200, .kind = Zum::GrantKind::Ceremony,
    .purpose = Zum::GrantPurpose::Enrollment, .state = Zum::State::Active,
    .issuer = "issuer"};
  ZuCheck(insertRecord(context->grants, collisionCeremony));
  ZuCheck(!runSaga(db, Zum::Enrollment{.ceremonyID = collisionCeremony.id,
    .userID = 41, .name = "duplicate code user", .created = 101,
    .beforeGrant = collisionCeremony}, ZdbSagaID{12010}));
  auto restoredCeremony = loadGrant(context, collisionCeremony.id);
  ZuCheck(restoredCeremony.state == Zum::State::Active && !restoredCeremony.owner &&
    restoredCeremony.expires == collisionCeremony.expires);
  Zum::User invitedBefore{.id = 12011, .name = "rollback invite",
    .created = 90, .updated = 95, .state = Zum::State::Pending,
    .version = 3};
  ZuCheck(insertRecord(context->users, invitedBefore));
  Zum::Bytes duplicateCred{ZuBSpan{"enrollment-collision-cred"}};
  ZuCheck(insertRecord(context->creds, Zum::Cred{.id = duplicateCred,
    .userID = 41, .state = Zum::State::Active}));
  ZuCheck(!runSaga(db, Zum::Enrollment{.ceremonyID = collisionCeremony.id,
    .userID = invitedBefore.id, .name = invitedBefore.name,
    .handle = Zum::Bytes{ZuBSpan{"rollback invite handle"}},
    .credentialID = duplicateCred, .created = 101, .precreated = true,
    .beforeGrant = collisionCeremony, .beforeUser = invitedBefore}, ZdbSagaID{12011}));
  auto invitedRestored = loadUser(context, invitedBefore.id);
  ZuCheck(invitedRestored.id == invitedBefore.id && !invitedRestored.handle &&
    !invitedRestored.owner && invitedRestored.version == invitedBefore.version &&
    invitedRestored.updated == invitedBefore.updated &&
    invitedRestored.state == Zum::State::Pending);
  ZuCheck(loadCred(context, duplicateCred).userID == 41);
  collisionCeremony.purpose = Zum::GrantPurpose::AddCredential;
  collisionCeremony.id = Zum::Bytes{ZuBSpan{"credential-collision-ceremony"}};
  collisionCeremony.userID = 41;
  ZuCheck(insertRecord(context->grants, collisionCeremony));
  Zum::CredentialAdd duplicateAdd;
  duplicateAdd.ceremonyID = collisionCeremony.id;
  duplicateAdd.issuer = "issuer";
  duplicateAdd.userID = 41;
  duplicateAdd.userHandle = Zum::Bytes{ZuBSpan{"code handle"}};
  duplicateAdd.credentialID = duplicateCred;
  duplicateAdd.created = 101;
  duplicateAdd.beforeGrant = collisionCeremony;
  ZuCheck(!runSaga(db, ZuMv(duplicateAdd), ZdbSagaID{12012}));
  restoredCeremony = loadGrant(context, collisionCeremony.id);
  ZuCheck(restoredCeremony.state == Zum::State::Active && !restoredCeremony.owner);
  Zum::Revoke revokeRollback{.updated = 101};
  auto revocable = loadGrant(context, ZuBSpan{"revocable"});
  revokeRollback.grants.push(Zum::SagaImage::save(revocable));
  revocable.id = Zum::Bytes{ZuBSpan{"missing-revoke-grant"}};
  revokeRollback.grants.push(Zum::SagaImage::save(revocable));
  ZuCheck(!runSaga(db, ZuMv(revokeRollback), ZdbSagaID{12007}));
  revocable = loadGrant(context, ZuBSpan{"revocable"});
  ZuCheck(revocable.state == Zum::State::Active && !revocable.owner);
  Zum::Revoke revokeGrant{.updated = 101};
  revokeGrant.grants.push(Zum::SagaImage::save(revocable));
  ZuCheck(runSaga(db, ZuMv(revokeGrant), ZdbSagaID{12008}));
  ZuCheck(loadGrant(context, ZuBSpan{"revocable"}).state ==
    Zum::State::Revoked);
  Zum::Revoke revokeAgain{.updated = 102};
  revokeAgain.grants.push(Zum::SagaImage::save(loadGrant(context, ZuBSpan{"revocable"})));
  ZuCheck(!revokeAgain.count());
  ZuCheck(runSaga(db, ZuMv(revokeAgain), ZdbSagaID{12009}));
  Zum::GrantCleanup cleanup{.updated = 1};
  Zum::GrantCleanup rollback{.updated = 1};
  auto expired = loadGrant(context, ZuBSpan{"revocable"});
  rollback.grants.push(Zum::SagaImage::save(expired));
  expired.id = Zum::Bytes{ZuBSpan{"changed-cleanup-grant"}};
  auto changedGrant = expired;
  changedGrant.expires = 2;
  ZuCheck(insertRecord(context->grants, changedGrant));
  rollback.grants.push(Zum::SagaImage::save(expired));
  ZuCheck(!runSaga(db, ZuMv(rollback), ZdbSagaID{12006}));
  ZuCheck(Zum::SagaImage::save(loadGrant(context, changedGrant.id)) ==
    Zum::SagaImage::save(changedGrant));
  expired = loadGrant(context, ZuBSpan{"revocable"});
  ZuCheck(expired.id && expired.state == Zum::State::Revoked && !expired.owner);
  cleanup.grants.push(Zum::SagaImage::save(loadGrant(context, ZuBSpan{"revocable"})));
  // Native deletion of an absent record succeeds without an application guard.
  expired.id = Zum::Bytes{ZuBSpan{"missing-cleanup-grant"}};
  cleanup.grants.push(Zum::SagaImage::save(expired));
  ZuCheck(runSaga(db, ZuMv(cleanup), ZdbSagaID{12005}));
  ZuCheck(!loadGrant(context, ZuBSpan{"revocable"}).id);
  Zum::SSFDelivery ssfDelivery{
    .eventID = "event-1", .receiverID = "receiver-1",
    .familyIssuer = "issuer/oauth2/9", .familyID = "family-1",
    .familyExpires = 1000, .set = Zum::Bytes{ZuBSpan{"signed-set"}},
    .nextDelivery = 101};
  Zum::SSFDeliveryAdd ssfAdd;
  ssfAdd.delivery = ssfDelivery;
  ZuCheck(runSaga(db, ZuMv(ssfAdd), ZdbSagaID{12023}));
  ZuCheck(ZmBlock<bool>{}([context](auto wake) mutable {
    auto table = context->ssfDeliveries;
    table->run(0, [table, wake = ZuMv(wake)]() mutable {
      table->find<0>(0, ZuFwdTuple(Zum::String{"event-1"},
          Zum::String{"receiver-1"}),
        [wake = ZuMv(wake)](ZdbRowRef<Zum::SSFDelivery> row) mutable {
          wake(row && !row->data().owner &&
            row->data().set == ZuBSpan{"signed-set"});
        });
    });
  }));
  // Issuer version is 10; app 20 independently has version 17.
  for (unsigned i = 0; i < 6; ++i) {
    Zum::Grant candidate{authorization};
    candidate.id[candidate.id.length() - 1] = uint8_t('0' + i);
    switch (i) {
      case 0: candidate.appID = 20; candidate.authVersion = 17; break;
      case 1: candidate.appID = 21; break; // disabled
      case 2: candidate.appID = 22; break; // saga-owned
      case 3: candidate.appID = 23; break; // missing
      case 4: candidate.appID = 0; break;
      default: candidate.appID = 20; break; // stale app version
    }
    Zum::Bytes id{candidate.id};
    uint64_t version = candidate.authVersion;
    ZuCheck(storeAuthorization(context, ZuMv(candidate)));
    Zum::String code;
    bool ok = finishAuthorization(context, rng, Zum::Bytes{id},
      Zum::Bytes{ZuBSpan{"browser binding"}}, 41,
      ZuBSpan{"code credential"}, code, version);
    ZuCheck(ok == (i == 0) && bool(code) == (i == 0));
    if (!i) ZuCheck(authorizationState(
      context, id, code, 41, ZuBSpan{"code credential"}));
    if (!i) ZuCheck(ZmBlock<bool>{}([context, id](auto wake) mutable {
      auto grants = context->grants;
      grants->run(0, [grants, id = ZuMv(id), wake = ZuMv(wake)]() mutable {
	Zum::GrantTable::Key<2> key{id, Zum::UserID{41}, Zum::AppID{20}};
	grants->find<2>(0, ZuMv(key),
	  [id = ZuMv(id), wake = ZuMv(wake)](ZdbRowRef<Zum::Grant> row) mutable {
	  wake(row && row->data().id == id);
	});
      });
    }));
  }
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
  auto collisionFamily = family;
  collisionFamily.familyID = Zum::Bytes{ZuBSpan{"family-collision"}};
  ZuCheck(insertRecord(context->grants, Zum::Grant{.id = collisionFamily.familyID,
    .expires = 1000, .kind = Zum::GrantKind::Refresh, .state = Zum::State::Active}));
  ZuCheck(!runSaga(db, ZuMv(collisionFamily), ZdbSagaID{12013}));
  auto rolledBackCode = loadGrant(context, family.codeID);
  ZuCheck(rolledBackCode.state == Zum::State::Active && !rolledBackCode.owner &&
    rolledBackCode.digest == family.codeDigest && rolledBackCode.scope == family.beforeGrant.scope);
  ZuCheck(loadGrant(context, ZuBSpan{"family-collision"}).state == Zum::State::Active);
  Zum::Bytes familyID = family.familyID;
  Zum::Bytes refreshID, refreshDigest;
  ZuCheck(Zum::opaqueParse(refreshToken, refreshID, refreshDigest) &&
    refreshID == family.familyID && refreshDigest == family.digest);
  ZuCheck(runSaga(db, ZuMv(family), ZdbSagaID{2}));
  ZuCheck(codeFamilyState(context,
    ZuBSpan{"authorization-id"}, familyID, true));
  ZuCheck(releaseToken(context, familyID, 10, true));
  ZuCheck(releaseToken(context, {}, 10, true));
  ZuCheck(releaseToken(context, {}, 17, true, 130, 20));
  ZuCheck(releaseToken(context, {}, 10, false, 130, 20));
  ZuCheck(releaseToken(context, {}, 10, false, 130, 21));
  ZuCheck(releaseToken(context, {}, 10, false, 130, 22));
  ZuCheck(releaseToken(context, {}, 10, false, 130, 23));
  ZuCheck(releaseToken(context, {}, 10, false, 130, 0));
  ZuCheck(releaseToken(context, familyID, 17, false, 130, 20));
  ZuCheck(releaseToken(context, familyID, 10, false, 130, 24));
  ZuCheck(releaseToken(context, familyID, 9, false));
  ZuCheck(releaseToken(context, familyID, 10, false, 1000));
  ZuCheck(releaseToken(context, ZuBSpan{"missing family"}, 10, false));
  Zum::String wrongRefresh = refreshToken;
  wrongRefresh[wrongRefresh.length() - 1] ^= 1;
  Zum::String nextRefresh;
  ZuCheck(rotateRefresh(context, rng, refreshToken, nextRefresh, 24) ==
    Zum::RefreshRotate::Invalid && !nextRefresh);
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
    .issuer = "issuer", .appID = 9, .now = 90, .expires = 1000};
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
    .issuer = "issuer", .appID = 9, .now = 91, .expires = 1000};
  ZuCheck(!ZmBlock<bool>{}([
    &db, &context, &rng, &duplicateBootstrap
  ](auto wake) mutable {
    Zum::bootstrapIssue(db->requests, Zm::now() + ZuTime{10},
      context, rng, ZuMv(duplicateBootstrap), [
	wake = ZuMv(wake)
    ](bool ok, Zum::String) mutable { wake(ok); });
  }));

  Zum::EnrollmentBeginResult enrollmentBegin;
  ZuCheck(insertRecord(context->users, Zum::User{
    .id = 42, .source = Zum::UserSource::Local, .name = "first user",
    .created = 90, .updated = 90, .state = Zum::State::Pending}));
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
    enrollmentGrant.purpose == Zum::GrantPurpose::Bootstrap);
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
  ZuCheck(logged(
    Zum::String{} << " event=" << int(Zum::AuditEvent::PrincipalChange) << " ",
    Zum::String{} << " outcome=" << int(Zum::AuditOutcome::Success) << " ",
    Zum::String{} << " subject=" << Zum::auditID(passkeyHandle) << " ",
    Zum::String{} << " detail=" << "enrollment" << " "));
  ZuCheck(logged(
    Zum::String{} << " event=" << int(Zum::AuditEvent::CredentialChange) << " ",
    Zum::String{} << " outcome=" << int(Zum::AuditOutcome::Success) << " ",
    Zum::String{} << " subject=" << Zum::auditID(passkeyHandle) << " ",
    Zum::String{} << " target=" << Zum::auditID(ZuBSpan{"credential"}) << " ",
    Zum::String{} << " detail=" << "enrollment" << " "));

  auto sessionUser = loadUser(context, 42);
  Zum::Session providerSession;
  Zum::String sessionToken;
  ZuCheck(ZmBlock<int>{}([&db, &context, &rng, &sessionUser,
      &providerSession, &sessionToken](auto wake) mutable {
    Zum::sessionIssue(db->requests, Zm::now() + ZuTime{10}, context, rng,
      Zum::SessionConfig{.issuer = "issuer/oauth2/9",
	.subject = Zum::auditID(sessionUser.handle), .userID = 42,
	.authTime = 123, .now = 130, .idleLifetime = 10,
	.absoluteLifetime = 30, .authVersion = sessionUser.authVersion},
      [&providerSession, &sessionToken, wake = ZuMv(wake)](
	  int error, Zum::Session session, Zum::String token) mutable {
	providerSession = ZuMv(session);
	sessionToken = ZuMv(token);
	wake(error);
      });
  }) == Zum::SessionError::OK && sessionToken &&
    providerSession.userID == 42 && providerSession.authTime == 123 &&
    providerSession.idleDeadline == 140 &&
    providerSession.absoluteDeadline == 160 &&
    providerSession.state == Zum::State::Active);
  Zum::String reusableSession = sessionToken;
  ZuCheck(ZmBlock<int>{}([&db, &context, &reusableSession,
      &providerSession](auto wake) mutable {
    Zum::sessionUse(db->requests, Zm::now() + ZuTime{10}, context,
      ZuMv(reusableSession), Zum::String{"issuer/oauth2/9"}, 135, 10,
      [&providerSession, wake = ZuMv(wake)](int error,
	  Zum::Session session, Zum::String) mutable {
	providerSession = ZuMv(session);
	wake(error);
      });
  }) == Zum::SessionError::OK && providerSession.idleDeadline == 145 &&
    providerSession.absoluteDeadline == 160 && providerSession.version == 2);
  ZuCheck(ZmBlock<bool>{}([context, &providerSession](auto wake) mutable {
    auto sessions = context->sessions;
    sessions->run(0, [sessions, &providerSession, wake = ZuMv(wake)]() mutable {
      sessions->find<2>(0, ZuFwdTuple(int64_t{145}), [
          &providerSession, wake = ZuMv(wake)](ZdbRowRef<Zum::Session> row) mutable {
        wake(row && row->data().digest == providerSession.digest &&
          row->data().idleDeadline == 145);
      });
    });
  }));
  auto sessionOwner = [context, digest = providerSession.digest](uint128_t owner) {
    return ZmBlock<Zum::Session>{}([context, &digest, owner](auto wake) mutable {
      auto table = context->sessions;
      table->run(0, [table, digest, owner, wake = ZuMv(wake)]() mutable {
	table->findUpd<0>(0, ZuFwdTuple(ZuMv(digest)), [owner,
	    wake = ZuMv(wake)](ZdbRow<Zum::Session> *row) mutable {
	  if (!row) { wake(Zum::Session{}); return; }
	  Zum::Session before = row->data();
	  row->data().owner = owner;
	  wake(row->commit() ? ZuMv(before) : Zum::Session{});
	});
      });
    });
  };
  ZuCheck(sessionOwner(12023).digest == providerSession.digest);
  reusableSession = sessionToken;
  ZuCheck(ZmBlock<int>{}([&db, &context, &reusableSession](auto wake) mutable {
    Zum::sessionUse(db->requests, Zm::now() + ZuTime{10}, context,
      ZuMv(reusableSession), Zum::String{"issuer/oauth2/9"}, 136, 10,
      [wake = ZuMv(wake)](int error, Zum::Session, Zum::String) mutable {
	wake(error);
      });
  }) == Zum::SessionError::Expired);
  reusableSession = sessionToken;
  ZuCheck(ZmBlock<int>{}([&db, &context, &reusableSession](auto wake) mutable {
    Zum::sessionRevoke(db->requests, Zm::now() + ZuTime{10}, context,
      ZuMv(reusableSession), Zum::String{"issuer/oauth2/9"}, 136,
      [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::SessionError::Storage);
  auto reservedSession = sessionOwner(0);
  ZuCheck(reservedSession.owner == 12023 &&
    reservedSession.state == providerSession.state &&
    reservedSession.version == providerSession.version &&
    reservedSession.idleDeadline == providerSession.idleDeadline &&
    reservedSession.updated == providerSession.updated);
  Zum::String revokedSession = sessionToken;
  ZuCheck(ZmBlock<int>{}([&db, &context, &revokedSession](auto wake) mutable {
    Zum::sessionRevoke(db->requests, Zm::now() + ZuTime{10}, context,
      ZuMv(revokedSession), Zum::String{"issuer/oauth2/9"}, 136,
      [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::SessionError::OK);
  reusableSession = sessionToken;
  ZuCheck(ZmBlock<int>{}([&db, &context, &reusableSession](auto wake) mutable {
    Zum::sessionUse(db->requests, Zm::now() + ZuTime{10}, context,
      ZuMv(reusableSession), Zum::String{"issuer/oauth2/9"}, 137, 10,
      [wake = ZuMv(wake)](int error, Zum::Session, Zum::String) mutable {
	wake(error);
      });
  }) == Zum::SessionError::Expired);

  Zum::OpaqueToken inviteToken;
  ZuCheck(Zum::opaqueIssue(rng, inviteToken));
  auto inviteID = inviteToken.id;
  auto inviteDigest = inviteToken.digest;
  Zum::String invitation = ZuMv(inviteToken.token);
  Zum::User inviteExternal{.id = 18001, .source = Zum::UserSource::External,
    .name = "invited", .created = 100, .updated = 100,
    .state = Zum::State::Active, .authVersion = 3, .version = 5};
  ZuCheck(insertRecord(context->users, Zum::User{inviteExternal}));
  ZuCheck(runSaga(db, Zum::UserInvite{
    .before = Zum::User{.id = 45, .version = 0},
    .values = Zum::User{.id = 45, .name = "invited"},
    .grant = Zum::Grant{.id = ZuMv(inviteToken.id), .userID = 45,
      .created = 123, .expires = 300, .kind = Zum::GrantKind::Capability,
      .purpose = Zum::GrantPurpose::Enrollment, .state = Zum::State::Active,
      .issuer = "issuer", .digest = ZuMv(inviteToken.digest), .userName = "invited",
      .label = "invited passkey", .actor = "precreated"}, .updated = 123,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::userInvite, .idempotencyKey = "invite-runtime"},
    .external = inviteExternal},
    ZdbSagaID{12000}));
  auto invalidatedExternal = loadUser(context, inviteExternal.id);
  ZuCheck(invalidatedExternal.authVersion == 4 && invalidatedExternal.version == 6 &&
    invalidatedExternal.updated == 123 && !invalidatedExternal.owner &&
    invalidatedExternal.name == inviteExternal.name &&
    invalidatedExternal.state == Zum::State::Active);
  Zum::User staleExternal{.id = 18002, .source = Zum::UserSource::External,
    .name = "stale-invited", .created = 100, .updated = 100,
    .state = Zum::State::Active, .authVersion = 3, .version = 5};
  ZuCheck(insertRecord(context->users, Zum::User{staleExternal}));
  --staleExternal.version;
  Zum::OpaqueToken staleToken;
  ZuCheck(Zum::opaqueIssue(rng, staleToken));
  auto staleGrantID = staleToken.id;
  ZuCheck(!runSaga(db, Zum::UserInvite{
    .before = Zum::User{.id = 18003, .version = 0},
    .values = Zum::User{.id = 18003, .name = "stale-invited"},
    .grant = Zum::Grant{.id = ZuMv(staleToken.id), .userID = 18003,
      .created = 123, .expires = 300, .kind = Zum::GrantKind::Capability,
      .purpose = Zum::GrantPurpose::Enrollment, .state = Zum::State::Active,
      .issuer = "issuer", .digest = ZuMv(staleToken.digest), .userName = "stale-invited",
      .actor = "precreated"}, .updated = 123,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::userInvite, .idempotencyKey = "invite-stale-external"},
    .external = staleExternal}, ZdbSagaID{18003}));
  ZuCheck(!loadUser(context, 18003).id && !loadGrant(context, staleGrantID).id);
  auto retainedExternal = loadUser(context, staleExternal.id);
  ZuCheck(retainedExternal.version == 5 && retainedExternal.authVersion == 3 &&
    retainedExternal.updated == 100 && !retainedExternal.owner);
  auto pendingInvite = loadUser(context, 45);
  auto inviteCapability = loadGrant(context, inviteID);
  ZuCheck(pendingInvite.id == 45 && pendingInvite.name == "invited" &&
    pendingInvite.source == Zum::UserSource::Local && !pendingInvite.owner &&
    pendingInvite.state == Zum::State::Pending && !pendingInvite.handle);
  ZuCheck(inviteCapability.userID == 45 && !inviteCapability.owner &&
    inviteCapability.kind == Zum::GrantKind::Capability &&
    inviteCapability.purpose == Zum::GrantPurpose::Enrollment &&
    inviteCapability.digest == inviteDigest);
  // A grant collision occurs after user insertion; compensation removes only
  // this invitation's user and leaves the existing invitation intact.
  ZuCheck(!runSaga(db, Zum::UserInvite{
    .before = Zum::User{.id = 12001, .version = 0},
    .values = Zum::User{.id = 12001, .name = "failed-invitation"},
    .grant = Zum::Grant{.id = inviteID, .userID = 12001,
      .created = 123, .expires = 300, .kind = Zum::GrantKind::Capability,
      .purpose = Zum::GrantPurpose::Enrollment, .state = Zum::State::Active,
      .issuer = "issuer", .digest = inviteDigest, .userName = "failed-invitation",
      .label = "invited passkey", .actor = "precreated"}, .updated = 123,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::userInvite, .idempotencyKey = "invite-rollback"}},
    ZdbSagaID{12001}));
  ZuCheck(!loadUser(context, 12001).id);
  auto retainedInvite = loadGrant(context, inviteID);
  ZuCheck(retainedInvite.userID == 45 && retainedInvite.digest == inviteDigest &&
    !retainedInvite.owner && retainedInvite.state == Zum::State::Active);
  Zum::EnrollmentBeginResult invitedBegin;
  ZuCheck(ZmBlock<int>{}([&db, &context, &rng, &invitation,
      &invitedBegin](auto wake) mutable {
    Zum::bootstrapBegin(db->requests, Zm::now() + ZuTime{10}, context,
      rng, ZuMv(invitation), Zum::Bytes{ZuBSpan{"invited binding"}},
      Zum::EnrollmentBeginConfig{.issuer = "issuer", .rpID = "example.com",
	.rpName = "Example", .now = 124, .expires = 200,
	.timeout = 60000}, [&invitedBegin, wake = ZuMv(wake)](
	  int error, Zum::EnrollmentBeginResult result) mutable {
	invitedBegin = ZuMv(result);
	wake(error);
      });
  }) == Zum::WebAuthnError::OK);
  auto invitedGrant = loadGrant(context, invitedBegin.ceremonyID);
  ZuCheck(invitedGrant.kind == Zum::GrantKind::Ceremony &&
    invitedGrant.purpose == Zum::GrantPurpose::Enrollment &&
    invitedGrant.userID == 45 && invitedGrant.userName == "invited" &&
    invitedGrant.actor == "precreated" &&
    invitedGrant.bindingDigest == ZuBSpan{"invited binding"});

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
  ZuCheck(logged(
    Zum::String{} << " event=" << int(Zum::AuditEvent::CredentialChange) << " ",
    Zum::String{} << " outcome=" << int(Zum::AuditOutcome::Success) << " ",
    Zum::String{} << " actor=" << Zum::auditID(passkeyHandle) << " ",
    Zum::String{} << " subject=" << Zum::auditID(passkeyHandle) << " ",
    Zum::String{} << " target=" << Zum::auditID(ZuBSpan{"credential-2"}) << " ",
    Zum::String{} << " detail=" << "add" << " "));

  Zum::Grant passkeyAuthorization;
  passkeyAuthorization.id =
    Zum::Bytes{ZuBSpan{"passkey-auth-id0"}};
  passkeyAuthorization.issuer = "issuer";
  passkeyAuthorization.appID = 8;
  passkeyAuthorization.clientID = "browser";
  passkeyAuthorization.audience = "orders";
  passkeyAuthorization.redirectURI = "https://app/cb";
  passkeyAuthorization.scope = "read";
  passkeyAuthorization.requestedRoleIDs.push(7);
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
  family.beforeGrant = loadGrant(context, family.codeID);
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
  family.beforeGrant = loadGrant(context, family.codeID);
  ZuCheck(!runSaga(db, ZuMv(family), ZdbSagaID{4}));
  ZuCheck(codeFamilyState(
    context, ZuBSpan{"wrong code"}, ZuBSpan{"wrong family"}, false));

  Zum::ActionID readAction = 0, writeAction = 1;
  Zum::Client browser;
  browser.id = "browser";
  browser.appID = 9;
  browser.redirects.push("https://app/cb");
  browser.identityScopes.push("openid");
  browser.identityScopes.push("profile");
  browser.identityScopes.push("email");
  browser.type = Zum::ClientType::Browser;
  browser.grants = Zum::ClientGrant::AuthorizationCode |
    Zum::ClientGrant::RefreshToken;
  browser.refreshAllowed = true;
  browser.state = Zum::State::Active;
  ZuCheck(insertRecord(context->clients, Zum::Client{browser}));

  ZuCheck(insertRecord(context->apps, Zum::App{
    .id = 9, .name = "oidc-app", .state = Zum::State::Active,
    .nextActionID = 1, .authVersion = 12}));
  ZuCheck(insertRecord(context->actions, Zum::Action{
    .appID = 9, .id = readAction, .name = "orders.read",
    .state = Zum::State::Active}));
  ZtBitmap oidcRoleActions{1U};
  oidcRoleActions.set(readAction);
  ZuCheck(insertRecord(context->roles, Zum::Role{
    .appID = 9, .id = 7, .name = "read",
    .actions = ZuMv(oidcRoleActions), .state = Zum::State::Active}));
  ZuCheck(insertRecord(context->audiences, Zum::Audience{
    .id = 7, .appID = 9, .name = "orders", .uri = "orders",
    .state = Zum::State::Active}));
  Zum::IDVec appRoles;
  appRoles.push(7);
  ZuCheck(insertRecord(context->memberships, Zum::Membership{
    .appID = 9, .userID = 42, .roleIDs = Zum::IDVec{appRoles},
    .state = Zum::State::Active}));
  Zum::Client oidcBrowser{browser};
  oidcBrowser.id = "oidc-browser";
  oidcBrowser.appID = 9;
  ZuCheck(insertRecord(context->clients, Zum::Client{oidcBrowser}));
  for (unsigned i = 0; i < 2; ++i)
    ZuCheck(insertRecord(context->clientAccess, Zum::ClientAccess{
      .clientID = i ? "oidc-browser" : "browser", .appID = 9,
      .audienceIDs = Zum::IDVec{7}, .roleIDs = Zum::IDVec{7},
      .state = Zum::State::Active}));
  ZuCheck(insertRecord(context->providers, Zum::Provider{
    .id = 8, .name = "upstream", .issuer =
      "https://upstream.example/oauth2/default",
    .claimSource = Zum::ClaimSource::IDToken,
    .state = Zum::State::Active}));
  ZuCheck(insertRecord(context->providers, Zum::Provider{
    .id = 10, .name = "userinfo-upstream", .issuer =
      "https://upstream.example/oauth2/default",
    .claimSource = Zum::ClaimSource::UserInfo,
    .state = Zum::State::Active}));
  ZuCheck(insertRecord(context->authPolicies, Zum::AuthPolicy{
    .appID = 9, .providerID = 8, .assignmentMaxAge = 300,
    .sessionIdle = 1800, .sessionAbsolute = 43200, .tokenLifetime = 300,
    .state = Zum::State::Active, .version = 3}));
  ZuCheck(insertRecord(context->roleMaps, Zum::RoleMap{
    .appID = 9, .providerID = 8, .value = "Zum-Users", .roleID = 7,
    .state = Zum::State::Active}));

  ZuCheck(insertRecord(context->users, Zum::User{
    .id = 44,
    .source = Zum::UserSource::External,
    .name = "oidc user",
    .handle = Zum::Bytes{ZuBSpan{"oidc handle"}},
    .state = Zum::State::Active
  }));
  ZuCheck(insertRecord(context->extIdentities, Zum::ExtIdentity{
    .providerID = 8, .issuer = "https://upstream.example/oauth2/default",
    .subject = "00u44", .userID = 44, .created = 100, .updated = 100
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
  unsigned discoveryReqs = 0, tokenReqs = 0, jwksReqs = 0,
    userinfoReqs = 0;
  Zum::OIDCConfig oidcConfig;
  oidcConfig.appID = 9;
  oidcConfig.providerID = 8;
  oidcConfig.policyVersion = 3;
  oidcConfig.assignmentMaxAge = 300;
  oidcConfig.issuer = "https://upstream.example/oauth2/default";
  oidcConfig.authorizeEndpoint = "https://upstream.example/authorize";
  oidcConfig.tokenEndpoint = "https://upstream.example/token";
  oidcConfig.jwksEndpoint = "https://upstream.example/jwks";
  oidcConfig.userinfoEndpoint = "https://upstream.example/userinfo";
  oidcConfig.clientID = "zum";
  oidcConfig.clientSecret = "secret:+ &%";
  oidcConfig.redirectURI = "https://zum.example/oidc/callback";
  oidcConfig.oidcScopes.push("openid");
  oidcConfig.oidcScopes.push("groups");
  oidcConfig.roles = Zum::OIDCRoles::Mapped;
  oidcConfig.loginHint = "person+test@example.com";
  oidcConfig.roleClaim = "groups";
  oidcConfig.roleMap.push(Zum::OIDCRoleMap{"Zum-Users", 7});
  Zum::String upstreamDiscovery{
    "{\"issuer\":\"https://upstream.example/oauth2/default\","
    "\"authorization_endpoint\":\"https://upstream.example/authorize\","
    "\"token_endpoint\":\"https://upstream.example/token\","
    "\"jwks_uri\":\"https://upstream.example/jwks\","
    "\"userinfo_endpoint\":\"https://upstream.example/userinfo\","
    "\"response_types_supported\":[\"code\"],"
    "\"id_token_signing_alg_values_supported\":[\"ES256\"],"
    "\"token_endpoint_auth_methods_supported\":[\"client_secret_basic\"]}"};
  Zum::OIDCConfig discoveredConfig{oidcConfig};
  discoveredConfig.authorizeEndpoint.null();
  discoveredConfig.tokenEndpoint.null();
  discoveredConfig.jwksEndpoint.null();
  Zum::Grant oidcCeremony;
  oidcCeremony.id = Zum::Bytes{ZuBSpan{"oidc-grant-id-00"}};
  oidcCeremony.issuer = "issuer/oauth2/9";
  oidcCeremony.clientID = "oidc-browser";
  oidcCeremony.appID = 9;
  oidcCeremony.audience = "orders";
  oidcCeremony.redirectURI = "https://app/cb";
  oidcCeremony.scope = "openid read";
  oidcCeremony.nonce = "local-nonce";
  oidcCeremony.requestedRoleIDs.push(7);
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
    Zum::OIDCLimits{}, 4, 30, []() { return int64_t{200}; }, [
      &upstreamToken, &upstreamJWKS, &upstreamDiscovery,
      &discoveryReqs, &tokenReqs, &jwksReqs, &userinfoReqs
    ](Zum::OIDCHTTPRequest request, Zum::OIDCHTTPDoneFn done) mutable {
      if (request.url == "https://upstream.example/oauth2/default/"
          ".well-known/openid-configuration") {
        ++discoveryReqs;
        done(200, upstreamDiscovery);
      } else if (request.url == "https://upstream.example/token") {
        ++tokenReqs;
        if (request.authorization != "Basic enVtOnNlY3JldCUzQSUyQiUyMCUyNiUyNQ==") {
          done(401, Zum::String{});
          return;
        }
        Zum::String body{"{\"id_token\":\""};
        body << upstreamToken << "\",\"access_token\":\"upstream-access\"}";
        done(200, ZuMv(body));
      } else if (request.url == "https://upstream.example/jwks") {
        ++jwksReqs;
        done(200, upstreamJWKS);
      } else if (request.url == "https://upstream.example/userinfo") {
        ++userinfoReqs;
        done(request.method == Zum::OIDCHTTPMethod::GET &&
          request.authorization == "Bearer upstream-access" ? 200 : 401,
          Zum::String{"{\"sub\":\"00u44\","
            "\"groups\":[\"Zum-Users\"]}"});
      } else {
        done(500, Zum::String{});
      }
    }));
  Zum::String oidcLocation;
  ZuCheck(ZmBlock<bool>{}([
      &oidc, &oidcLocation, &discoveredConfig](auto wake) mutable {
    if (!oidc.begin(Zum::Bytes{ZuBSpan{"oidc-grant-id-00"}},
      discoveredConfig, [
        &oidcLocation, wake = ZuMv(wake)
      ](bool ok, Zum::String location) mutable {
        oidcLocation = ZuMv(location);
        wake(ok);
      })) wake(false);
  }));
  Zum::String oidcState, oidcNonce;
  ZuCheck(oidcParams(oidcLocation, oidcState, oidcNonce));
  Zum::String upstreamClaims{
    "{\"iss\":\"https://upstream.example/oauth2/default\","
    "\"sub\":\"00u44\","
    "\"aud\":\"zum\",\"nonce\":\""};
  upstreamClaims << oidcNonce <<
    "\",\"iat\":195,\"auth_time\":190,\"exp\":260,\"groups\":[\"Zum-Users\"]}";
  ZuCheck(signJWT(rng, upstreamKey,
    "{\"alg\":\"ES256\",\"kid\":\"upstream\",\"typ\":\"JWT\"}",
    upstreamClaims, upstreamToken));
  Zum::Bytes oidcGrant;
  Zum::User oidcUser;
  Zum::IDVec oidcRoles;
  Zum::Evidence oidcLoginEvidence;
  int64_t oidcAuthTime = 0;
  Zum::String callback{"code=upstream-code&state="};
  callback << oidcState;
  ZuCheck(!ZmBlock<bool>{}([&oidc, callback = Zum::String{callback}](
      auto wake) mutable {
    if (!oidc.finish(10, ZuMv(callback), [wake = ZuMv(wake)](bool ok,
	Zum::Bytes, Zum::User, Zum::IDVec, Zum::Evidence, int64_t) mutable {
      wake(ok);
    })) wake(true);
  }));
  ZuCheck(tokenReqs == 0);
  ZuCheck(ZmBlock<bool>{}([
    &oidc, &callback, &oidcGrant, &oidcUser, &oidcRoles,
    &oidcLoginEvidence, &oidcAuthTime
  ](auto wake) mutable {
    if (!oidc.finish(9, ZuMv(callback), [
        &oidcGrant, &oidcUser, &oidcRoles, &oidcLoginEvidence, &oidcAuthTime,
        wake = ZuMv(wake)
      ](bool ok, Zum::Bytes grantID, Zum::User user,
          Zum::IDVec roles, Zum::Evidence evidence, int64_t authTime) mutable {
        oidcGrant = ZuMv(grantID);
        oidcUser = ZuMv(user);
        oidcRoles = ZuMv(roles);
        oidcLoginEvidence = ZuMv(evidence);
        oidcAuthTime = authTime;
        wake(ok);
      })) wake(false);
  }));
  ZuCheck(oidcGrant == ZuBSpan{"oidc-grant-id-00"} &&
    oidcUser.id == 44 && oidcRoles.length() == 1 && oidcRoles[0] == 7 &&
    oidcAuthTime == 190 && discoveryReqs == 1 && tokenReqs == 1 &&
    jwksReqs == 1);
  auto oidcEvidence = ZmBlock<Zum::Evidence>{}([
    context
  ](auto wake) mutable {
    context->evidence->run(0, [context, wake = ZuMv(wake)]() mutable {
      context->evidence->find<0>(0, ZuFwdTuple(
          Zum::AppID{9}, Zum::UserID{44}, Zum::ProviderID{8}), [
          wake = ZuMv(wake)](ZdbRowRef<Zum::Evidence> row) mutable {
        wake(row ? Zum::Evidence{row->data()} : Zum::Evidence{});
      });
    });
  });
  ZuCheck(oidcEvidence.eligible && oidcEvidence.observed == 200 &&
    oidcEvidence.deadline == 500 && oidcEvidence.policyVersion == 3 &&
    oidcEvidence.roleValues.length() == 1 &&
    oidcEvidence.roleValues[0] == "Zum-Users");
  Zum::OIDCConfig userinfoConfig{oidcConfig};
  userinfoConfig.providerID = 10;
  userinfoConfig.claimSource = Zum::ClaimSource::UserInfo;
  Zum::String userinfoLocation;
  ZuCheck(ZmBlock<bool>{}([
      &oidc, &userinfoLocation, &userinfoConfig](auto wake) mutable {
    if (!oidc.begin(Zum::Bytes{ZuBSpan{"oidc-userinfo-id"}},
      userinfoConfig, [
        &userinfoLocation, wake = ZuMv(wake)
      ](bool ok, Zum::String location) mutable {
        userinfoLocation = ZuMv(location);
        wake(ok);
      })) wake(false);
  }));
  Zum::String userinfoState, userinfoNonce;
  ZuCheck(oidcParams(userinfoLocation, userinfoState, userinfoNonce));
  ZuCheck(userinfoLocation.find<"login_hint=person%2Btest%40example.com">() >= 0);
  Zum::String userinfoClaims{
    "{\"iss\":\"https://upstream.example/oauth2/default\","
    "\"sub\":\"00u44\",\"aud\":\"zum\",\"nonce\":\""};
  userinfoClaims << userinfoNonce << "\",\"iat\":195,\"auth_time\":191,\"exp\":260}";
  ZuCheck(signJWT(rng, upstreamKey,
    "{\"alg\":\"ES256\",\"kid\":\"upstream\",\"typ\":\"JWT\"}",
    userinfoClaims, upstreamToken));
  Zum::Bytes userinfoGrant;
  Zum::User userinfoUser;
  Zum::IDVec userinfoRoles;
  Zum::Evidence userinfoEvidence;
  int64_t userinfoAuthTime = 0;
  Zum::String userinfoCallback{"code=userinfo-code&state="};
  userinfoCallback << userinfoState;
  ZuCheck(ZmBlock<bool>{}([
    &oidc, &userinfoCallback, &userinfoGrant, &userinfoUser,
    &userinfoRoles, &userinfoEvidence, &userinfoAuthTime
  ](auto wake) mutable {
    if (!oidc.finish(9, ZuMv(userinfoCallback), [
        &userinfoGrant, &userinfoUser, &userinfoRoles, &userinfoEvidence,
        &userinfoAuthTime,
        wake = ZuMv(wake)
      ](bool ok, Zum::Bytes grantID, Zum::User user,
          Zum::IDVec roles, Zum::Evidence evidence, int64_t authTime) mutable {
        userinfoGrant = ZuMv(grantID);
        userinfoUser = ZuMv(user);
        userinfoRoles = ZuMv(roles);
        userinfoEvidence = ZuMv(evidence);
        userinfoAuthTime = authTime;
        wake(ok);
      })) wake(false);
  }));
  ZuCheck(userinfoGrant == ZuBSpan{"oidc-userinfo-id"} &&
    userinfoUser.id && userinfoUser.source == Zum::UserSource::External &&
    userinfoRoles.length() == 1 &&
    userinfoRoles[0] == 7 && userinfoAuthTime == 191 &&
    tokenReqs == 2 && jwksReqs == 2 && userinfoReqs == 1);
  // A formerly trusted key removed by the upstream must not remain usable.
  Zum::String savedJWKS = ZuMv(upstreamJWKS);
  Zum::String jwk{"{\"kid\":\"upstream\",\"kty\":\"EC\",\"crv\":\"P-256\",\"x\":\""};
  jwk << x << "\",\"y\":\"" << y << "\"}";
  Zum::StringVec invalidKeySets;
  invalidKeySets.push("{\"keys\":[]}");
  invalidKeySets.push(Zum::String{"{\"keys\":["} << jwk << ',' << jwk << "]}");
  invalidKeySets.push(Zum::String{"{\"keys\":[],\"keys\":["} << jwk << "]}");
  for (auto &invalidKeys: invalidKeySets) {
    upstreamJWKS = invalidKeys;
    Zum::String removedLocation;
    ZuCheck(ZmBlock<bool>{}([&oidc, &oidcConfig, &removedLocation](
	auto wake) mutable {
      if (!oidc.begin(Zum::Bytes{ZuBSpan{"removed-key-login"}}, oidcConfig,
	[&removedLocation, wake = ZuMv(wake)](bool ok, Zum::String location) mutable {
	  removedLocation = ZuMv(location);
	  wake(ok);
	})) wake(false);
    }));
    Zum::String removedState, removedNonce;
    ZuCheck(oidcParams(removedLocation, removedState, removedNonce));
    Zum::String removedClaims{
      "{\"iss\":\"https://upstream.example/oauth2/default\","
      "\"sub\":\"00u44\",\"aud\":\"zum\",\"nonce\":\""};
    removedClaims << removedNonce <<
      "\",\"iat\":195,\"auth_time\":191,\"exp\":260,\"groups\":[\"Zum-Users\"]}";
    ZuCheck(signJWT(rng, upstreamKey,
      "{\"alg\":\"ES256\",\"kid\":\"upstream\",\"typ\":\"JWT\"}",
      removedClaims, upstreamToken));
    Zum::String removedCallback{"code=removed-key-code&state="};
    removedCallback << removedState;
    ZuCheck(!ZmBlock<bool>{}([&oidc, callback = ZuMv(removedCallback)](
	auto wake) mutable {
      if (!oidc.finish(9, ZuMv(callback), [wake = ZuMv(wake)](bool ok,
	  Zum::Bytes, Zum::User, Zum::IDVec, Zum::Evidence, int64_t) mutable {
	wake(ok);
      })) wake(false);
    }));
  }
  ZuCheck(tokenReqs == 5 && jwksReqs == 5 && userinfoReqs == 1);
  upstreamJWKS = ZuMv(savedJWKS);
  Zum::User projectedUser;
  Zum::IDVec projectedRoles;
  Zum::StringVec projectedValues;
  projectedValues.push("Zum-Users");
  ZuCheck(ZmBlock<bool>{}([
    context, &oidcConfig, &projectedUser, &projectedRoles,
    values = ZuMv(projectedValues)
  ](auto wake) mutable {
    Zum::oidcLoadUser(context, "00u-new", oidcConfig, ZuMv(values), {}, 201,
      [&projectedUser, &projectedRoles, wake = ZuMv(wake)](
          bool ok, Zum::User user, Zum::IDVec roles,
          Zum::Evidence) mutable {
        projectedUser = ZuMv(user);
        projectedRoles = ZuMv(roles);
        wake(ok);
      });
  }));
  ZuCheck(projectedUser.id && projectedUser.id != 44 &&
    projectedUser.source == Zum::UserSource::External &&
    projectedUser.state == Zum::State::Active && projectedUser.handle &&
    projectedUser.name.prefix("oidc:8:") == "oidc:8:" &&
    projectedRoles.length() == 1 && projectedRoles[0] == 7);
  ZuCheck(ZmBlock<bool>{}([context, &oidcConfig, &projectedUser](auto wake) mutable {
    Zum::StringVec values;
    values.push("Zum-Users");
    Zum::oidcLoadUser(context, "00u-new", oidcConfig, ZuMv(values), {}, 202,
      [&projectedUser, wake = ZuMv(wake)](bool ok, Zum::User user,
          Zum::IDVec, Zum::Evidence evidence) mutable {
        wake(ok && user.id == projectedUser.id && evidence.observed == 202 &&
          evidence.deadline == 502 && evidence.version == 2);
      });
  }));
  ZuCheck(ZmBlock<bool>{}([context, &projectedUser](auto wake) mutable {
    auto evidence = context->evidence;
    evidence->run(0, [evidence, &projectedUser, wake = ZuMv(wake)]() mutable {
      evidence->find<1>(0, ZuFwdTuple(int64_t{502}), [
          &projectedUser, wake = ZuMv(wake)](ZdbRowRef<Zum::Evidence> row) mutable {
        wake(row && row->data().userID == projectedUser.id &&
          row->data().deadline == 502);
      });
    });
  }));
  ZuCheck(ZmBlock<bool>{}([context, &projectedUser](auto wake) mutable {
    context->extIdentities->run(0, [context, &projectedUser,
        wake = ZuMv(wake)]() mutable {
      context->extIdentities->find<0>(0, ZuFwdTuple(
          Zum::ProviderID{8},
          ZuCSpan{"https://upstream.example/oauth2/default"},
          ZuCSpan{"00u-new"}), [&projectedUser, wake = ZuMv(wake)](
            ZdbRowRef<Zum::ExtIdentity> row) mutable {
        wake(row && !row->data().owner &&
          row->data().userID == projectedUser.id);
      });
    });
  }));
  auto wrongOIDCIssuer = finishOIDCAuthorization(
    db->requests, context, rng, Zum::Bytes{oidcGrant}, Zum::User{oidcUser},
    Zum::IDVec{oidcRoles}, Zum::Evidence{oidcLoginEvidence}, readAction,
    oidcAuthTime, 200, "issuer/oauth2/10", 9);
  ZuCheck(wrongOIDCIssuer.error == Zum::OAuthError::AccessDenied &&
    !wrongOIDCIssuer.location);
  auto wrongOIDCApp = finishOIDCAuthorization(
    db->requests, context, rng, Zum::Bytes{oidcGrant}, Zum::User{oidcUser},
    Zum::IDVec{oidcRoles}, Zum::Evidence{oidcLoginEvidence}, readAction,
    oidcAuthTime, 200, "issuer/oauth2/9", 10);
  ZuCheck(wrongOIDCApp.error == Zum::OAuthError::AccessDenied &&
    !wrongOIDCApp.location);
  auto oidcAuthorized = finishOIDCAuthorization(
    db->requests, context, rng, Zum::Bytes{oidcGrant}, Zum::User{oidcUser},
    Zum::IDVec{oidcRoles}, Zum::Evidence{oidcLoginEvidence}, readAction,
    oidcAuthTime);
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
  auto delegatedCode = loadGrant(context, ZuBSpan{oidcGrant});
  ZuCheck(delegatedCode.authoritySource == Zum::UserSource::External &&
    delegatedCode.authorityProviderID == 8 &&
    delegatedCode.policyVersion == 3 && delegatedCode.evidenceVersion == 1);
  auto delegatedAuthority = loadInteractiveAuthority(
    context, Zum::Grant{delegatedCode}, Zum::Client{oidcBrowser});
  ZuCheck(delegatedAuthority.error == Zum::ScopeError::OK &&
    delegatedAuthority.data.actions[readAction]);
  ZuCheck(ZmBlock<bool>{}([context](auto wake) mutable {
    context->roleMaps->run(0, [context, wake = ZuMv(wake)]() mutable {
      context->roleMaps->findUpd<0>(0, ZuFwdTuple(Zum::AppID{9},
          Zum::ProviderID{8}, ZuCSpan{"Zum-Users"}), [wake = ZuMv(wake)](
          ZdbRow<Zum::RoleMap> *row) mutable {
        if (!row) { wake(false); return; }
        row->data().state = Zum::State::Disabled;
        wake(row->commit());
      });
    });
  }));
  delegatedAuthority = loadInteractiveAuthority(
    context, Zum::Grant{delegatedCode}, Zum::Client{oidcBrowser});
  ZuCheck(delegatedAuthority.error == Zum::ScopeError::OK &&
    !delegatedAuthority.data.actions);
  ZuCheck(ZmBlock<bool>{}([context](auto wake) mutable {
    context->roleMaps->run(0, [context, wake = ZuMv(wake)]() mutable {
      context->roleMaps->findUpd<0>(0, ZuFwdTuple(Zum::AppID{9},
          Zum::ProviderID{8}, ZuCSpan{"Zum-Users"}), [wake = ZuMv(wake)](
          ZdbRow<Zum::RoleMap> *row) mutable {
        if (!row) { wake(false); return; }
        row->data().state = Zum::State::Active;
        wake(row->commit());
      });
    });
  }));
  auto oidcScheduler = db->requests->scheduler();
  unsigned oidcSID = db->requests->sid();
  oidc.final();
  ZmSemaphore oidcDown;
  oidcScheduler->run([&oidcDown]() { oidcDown.post(); }, oidcSID);
  oidcDown.wait();

  ZuCheck(insertRecord(context->signKeys, Zum::SignKey{
    .id = "app-token-key",
    .issuer = "issuer/oauth2/9",
    .algorithm = "ES256",
    .providerRef = "test-key",
    .publicJwk = "{\"kty\":\"EC\",\"kid\":\"app-token-key\"}",
    .notBefore = 100,
    .retireAfter = 1000,
    .state = Zum::State::Active
  }));
  auto ssoUser = loadUser(context, 42);
  Zum::String ssoToken;
  ZuCheck(ZmBlock<int>{}([&db, &context, &rng, &ssoUser,
      &ssoToken](auto wake) mutable {
    Zum::sessionIssue(db->requests, Zm::now() + ZuTime{10}, context, rng,
      Zum::SessionConfig{.issuer = "issuer/oauth2/9",
	.subject = Zum::auditID(ssoUser.handle), .userID = 42,
	.authTime = 190, .now = 200, .idleLifetime = 1800,
	.absoluteLifetime = 43200, .authVersion = ssoUser.authVersion},
      [&ssoToken, wake = ZuMv(wake)](int error, Zum::Session,
	  Zum::String token) mutable {
	ssoToken = ZuMv(token);
	wake(error);
      });
  }) == Zum::SessionError::OK && ssoToken);
  int64_t serverNow = 210;
  Zum::Server providerServer;
  ZuCheck(providerServer.init(db, context, db->requests,
    Zum::ServerConfig{.issuer = "issuer", .rpID = "example.com",
      .rpName = "Example",
      .authMethod = Zum::AuthMethod::Passkey},
    [&serverNow]() { return serverNow; },
    [](Zum::AppID, Zum::Bytes id, Zum::String) { return base64URL(id); },
    [](const Zum::User &, const Zum::Client &,
	const Zum::ScopeSelection &, const ZtBitmap &allowed,
	Zum::PolicyDoneFn complete) { complete(true, ZtBitmap{allowed}); },
    [](Zum::PasskeyStart, Zum::AdmitDoneFn complete) {
      complete(Zum::PasskeyAdmission{});
    }, [](const Zum::SignKey &, ZuBSpan, Zum::SignatureFn complete) {
      complete(Zum::Bytes{});
    }));
  ZuCheck(ZmBlock<bool>{}([&db](auto wake) mutable {
    db->requests->deactivate([wake = ZuMv(wake)]() mutable { wake(true); });
  }));
  auto inactiveInfo = ZmBlock<Zum::ServerReply>{}([&providerServer](auto wake) mutable {
    providerServer.userInfo(9, "Bearer invalid",
      [wake = ZuMv(wake)](Zum::ServerReply reply) mutable { wake(ZuMv(reply)); });
  });
  ZuCheck(inactiveInfo.type == Zum::ReplyType::ServerError);
  auto inactiveReady = ZmBlock<Zum::ServerReply>{}([&providerServer](auto wake) mutable {
    providerServer.ready([wake = ZuMv(wake)](Zum::ServerReply reply) mutable {
      wake(ZuMv(reply));
    });
  });
  ZuCheck(inactiveReady.type == Zum::ReplyType::ServerError);
  db->requests->activate();
  auto invalidInfo = ZmBlock<Zum::ServerReply>{}([&providerServer](auto wake) mutable {
    providerServer.userInfo(9, "Bearer invalid",
      [wake = ZuMv(wake)](Zum::ServerReply reply) mutable { wake(ZuMv(reply)); });
  });
  ZuCheck(invalidInfo.type == Zum::ReplyType::BearerError);
  Zum::String loginQuery{
    "response_type=code&client_id=browser&redirect_uri="
    "https%3A%2F%2Fapp%2Fcb&scope=read&state=login-post&code_challenge="
    "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM&"
    "code_challenge_method=S256"};
  auto loginStart = ZmBlock<Zum::ServerReply>{}([
      &providerServer, &loginQuery](auto wake) mutable {
    providerServer.authorize(9, ZuMv(loginQuery),
      [wake = ZuMv(wake)](Zum::ServerReply reply) mutable {
	wake(ZuMv(reply));
      });
  });
  ZuCheck(loginStart.type == Zum::ReplyType::Page && loginStart.body &&
    loginStart.setCookie.find<"zum_tx=">() == 0);
  Zum::String loginForm{"id="};
  loginForm << loginStart.body << "&login=alice";
  auto loginSelected = ZmBlock<Zum::ServerReply>{}([
      &providerServer, &loginForm, &loginStart](auto wake) mutable {
    providerServer.login(9, ZuMv(loginForm), Zum::String{loginStart.setCookie},
      [wake = ZuMv(wake)](Zum::ServerReply reply) mutable {
	wake(ZuMv(reply));
      });
  });
  ZuCheck(loginSelected.type == Zum::ReplyType::Page &&
    loginSelected.body == loginStart.body && !loginSelected.setCookie);
  Zum::String ssoQuery{
    "response_type=code&client_id=browser&redirect_uri="
    "https%3A%2F%2Fapp%2Fcb&scope=read&state=sso&code_challenge="
    "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM&"
    "code_challenge_method=S256&max_age=100"};
  Zum::String ssoCookie{"zum_tx="};
  ssoCookie << ssoToken;
  auto ssoReply = ZmBlock<Zum::ServerReply>{}([
      &providerServer, &ssoQuery, &ssoCookie](auto wake) mutable {
    providerServer.authorize(9, ZuMv(ssoQuery), ZuMv(ssoCookie),
      [wake = ZuMv(wake)](Zum::ServerReply reply) mutable {
	wake(ZuMv(reply));
      });
  });
  constexpr ZuCSpan consentMarker{"name=id value=\""};
  int consentOffset = ssoReply.body.find<"name=id value=\"">();
  Zum::String consentID;
  if (consentOffset >= 0) {
    unsigned begin = unsigned(consentOffset) + consentMarker.length();
    unsigned end = begin;
    while (end < ssoReply.body.length() && ssoReply.body[end] != '"') ++end;
    if (end < ssoReply.body.length())
      consentID = ZuCSpan{ssoReply.body.data() + begin, end - begin};
  }
  ZuCheck(ssoReply.type == Zum::ReplyType::Page && consentID &&
    ssoReply.setCookie.find<"zum_tx=">() == 0);
  auto consentReply = ZmBlock<Zum::ServerReply>{}([
      &providerServer, &consentID, &ssoReply](auto wake) mutable {
    Zum::String form{"id="};
    form << consentID << "&decision=approve";
    providerServer.consent(9, ZuMv(form), Zum::String{ssoReply.setCookie},
      [wake = ZuMv(wake)](Zum::ServerReply reply) mutable {
	wake(ZuMv(reply));
      });
  });
  ZuCheck(consentReply.type == Zum::ReplyType::Redirect &&
    consentReply.location.find<"https://app/cb?code=">() == 0 &&
    consentReply.location.find<"&state=sso">() > 0 &&
    consentReply.setCookie.find<"zum_tx=">() == 0);

  ZuCheck(insertRecord(context->consents, Zum::Consent{
    .userID = oidcUser.id, .clientID = "oidc-browser", .appID = 9,
    .audienceID = 7,
    .roleIDs = Zum::IDVec{Zum::RoleID{7}},
    .state = Zum::State::Active, .created = 210, .updated = 210}));

  Zum::String upstreamSession;
  ZuCheck(ZmBlock<int>{}([&db, &context, &rng, &oidcUser,
      &upstreamSession](auto wake) mutable {
    Zum::sessionIssue(db->requests, Zm::now() + ZuTime{10}, context, rng,
      Zum::SessionConfig{.issuer = "issuer/oauth2/9",
	.subject = Zum::auditID(oidcUser.handle), .userID = oidcUser.id,
	.providerID = 8, .authTime = 190, .now = 200,
	.idleLifetime = 1800, .absoluteLifetime = 43200,
	.authVersion = oidcUser.authVersion},
      [&upstreamSession, wake = ZuMv(wake)](int error, Zum::Session,
	  Zum::String token) mutable {
	upstreamSession = ZuMv(token);
	wake(error);
      });
  }) == Zum::SessionError::OK && upstreamSession);
  auto upstreamAuthorize = [&providerServer, &upstreamSession](
      ZuCSpan state) {
    Zum::String query{
      "response_type=code&client_id=oidc-browser&redirect_uri="
      "https%3A%2F%2Fapp%2Fcb&scope=read&state="};
    query << state << "&code_challenge="
      "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM&"
      "code_challenge_method=S256&prompt=none";
    Zum::String cookie{"zum_tx="};
    cookie << upstreamSession;
    return ZmBlock<Zum::ServerReply>{}([&providerServer,
	query = ZuMv(query), cookie = ZuMv(cookie)](auto wake) mutable {
      providerServer.authorize(9, ZuMv(query), ZuMv(cookie),
	[wake = ZuMv(wake)](Zum::ServerReply reply) mutable {
	  wake(ZuMv(reply));
	});
    });
  };
  auto upstreamSSO = upstreamAuthorize("upstream-sso");
  ZuCheck(upstreamSSO.type == Zum::ReplyType::Redirect &&
    upstreamSSO.location.find<"https://app/cb?code=">() == 0 &&
    upstreamSSO.location.find<"&state=upstream-sso">() > 0);
  ZuCheck(ZmBlock<bool>{}([context](auto wake) mutable {
    context->roleMaps->run(0, [context, wake = ZuMv(wake)]() mutable {
      context->roleMaps->findUpd<0>(0, ZuFwdTuple(Zum::AppID{9},
	  Zum::ProviderID{8}, ZuCSpan{"Zum-Users"}), [wake = ZuMv(wake)](
	    ZdbRow<Zum::RoleMap> *row) mutable {
	if (!row) { wake(false); return; }
	row->data().state = Zum::State::Disabled;
	wake(row->commit());
      });
    });
  }));
  auto unmappedSSO = upstreamAuthorize("unmapped-sso");
  ZuCheck(unmappedSSO.type == Zum::ReplyType::Redirect &&
    unmappedSSO.location.find<"error=login_required">() > 0 &&
    unmappedSSO.location.find<"state=unmapped-sso">() > 0);
  ZuCheck(ZmBlock<bool>{}([context](auto wake) mutable {
    context->roleMaps->run(0, [context, wake = ZuMv(wake)]() mutable {
      context->roleMaps->findUpd<0>(0, ZuFwdTuple(Zum::AppID{9},
	  Zum::ProviderID{8}, ZuCSpan{"Zum-Users"}), [wake = ZuMv(wake)](
	    ZdbRow<Zum::RoleMap> *row) mutable {
	if (!row) { wake(false); return; }
	row->data().state = Zum::State::Active;
	wake(row->commit());
      });
    });
  }));
  serverNow = 501;
  auto staleSSO = upstreamAuthorize("stale-sso");
  ZuCheck(staleSSO.type == Zum::ReplyType::Redirect &&
    staleSSO.location.find<"error=login_required">() > 0 &&
    staleSSO.location.find<"state=stale-sso">() > 0);
  serverNow = 210;
  Zum::String logoutCookie{"zum_tx="};
  logoutCookie << ssoToken;
  auto loginPage = ZmBlock<Zum::ServerReply>{}([
      &providerServer, &logoutCookie](auto wake) mutable {
    providerServer.login(9, Zum::String{logoutCookie}, [wake = ZuMv(wake)](
	Zum::ServerReply reply) mutable { wake(ZuMv(reply)); });
  });
  constexpr ZuCSpan csrfMarker{"name=csrf value=\""};
  int csrfOffset = loginPage.body.find<"name=csrf value=\"">();
  Zum::String logoutCSRF;
  if (csrfOffset >= 0) {
    unsigned begin = unsigned(csrfOffset) + csrfMarker.length();
    unsigned end = begin;
    while (end < loginPage.body.length() && loginPage.body[end] != '"') ++end;
    if (end < loginPage.body.length())
      logoutCSRF = ZuCSpan{loginPage.body.data() + begin, end - begin};
  }
  ZuCheck(loginPage.type == Zum::ReplyType::Page && logoutCSRF);
  auto badLogout = ZmBlock<Zum::ServerReply>{}([
      &providerServer, &logoutCookie](auto wake) mutable {
    providerServer.logout(9, "csrf=wrong", Zum::String{logoutCookie},
      [wake = ZuMv(wake)](Zum::ServerReply reply) mutable {
	wake(ZuMv(reply));
      });
  });
  ZuCheck(badLogout.type == Zum::ReplyType::OAuthError &&
    !badLogout.setCookie);
  auto goodLogout = ZmBlock<Zum::ServerReply>{}([
      &providerServer, &logoutCookie, &logoutCSRF](auto wake) mutable {
    Zum::String form{"csrf="};
    form << logoutCSRF;
    providerServer.logout(9, ZuMv(form), Zum::String{logoutCookie},
      [wake = ZuMv(wake)](Zum::ServerReply reply) mutable {
	wake(ZuMv(reply));
      });
  });
  ZuCheck(goodLogout.type == Zum::ReplyType::Page &&
    goodLogout.body.find<"Signed out of Zum">() > 0 &&
    goodLogout.setCookie.find<"Max-Age=0">() > 0);
  ZuCheck(ZmBlock<int>{}([&db, &context, &ssoToken](auto wake) mutable {
    Zum::sessionUse(db->requests, Zm::now() + ZuTime{10}, context,
      Zum::String{ssoToken}, "issuer", 211, 1800,
      [wake = ZuMv(wake)](int error, Zum::Session,
	  Zum::String) mutable { wake(error); });
  }) == Zum::SessionError::Expired);
  providerServer.final();

  Zum::String authorizeQuery{
    "response_type=code&client_id=browser&"};
  authorizeQuery <<
    "redirect_uri=https%3A%2F%2Fapp%2Fcb&scope=read%20offline_access&state=return&" <<
    "code_challenge=" <<
    "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM&" <<
    "code_challenge_method=S256&prompt=consent&max_age=100";
  auto authorized = beginAuthorization(
    db->requests, context, rng, ZuMv(authorizeQuery), 200,
    "issuer/oauth2/9");
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
    requestGrant.scope == "read offline_access" &&
    requestGrant.requestedRoleIDs.length() == 1 && requestGrant.requestedRoleIDs[0] == 7 &&
    requestGrant.bindingDigest == ZuBSpan{"browser binding"} &&
    requestGrant.oauthState == "return" && requestGrant.oauthStatePresent &&
    requestGrant.prompt == "consent" && requestGrant.promptPresent &&
    requestGrant.maxAge == 100 && requestGrant.maxAgePresent &&
    requestGrant.authVersion == 12 &&
    requestGrant.expires == 260);

  Zum::Grant authorityGrant;
  authorityGrant.issuer = "issuer/oauth2/9";
  authorityGrant.appID = 9;
  authorityGrant.userID = 42;
  authorityGrant.clientID = "browser";
  authorityGrant.credentialID = Zum::Bytes{ZuBSpan{"credential"}};
  authorityGrant.audience = "orders";
  authorityGrant.scope = "read offline_access";
  authorityGrant.requestedRoleIDs.push(7);
  authorityGrant.roleIDs.push(7);
  authorityGrant.actions.length(2);
  authorityGrant.actions.set(readAction);
  auto authority = loadInteractiveAuthority(
    context, Zum::Grant{authorityGrant}, Zum::Client{browser});
  ZuCheck(authority.error == Zum::ScopeError::OK &&
    authority.data.app.authVersion == 12 &&
    authority.data.selection.scope == "read offline_access" &&
    authority.data.actions[readAction] &&
    !authority.data.actions[writeAction] &&
    authority.data.actionRecords.length() == 1);

  auto changeMembership = [context](Zum::State::T state,
      uint128_t owner, bool roles) {
    return ZmBlock<bool>{}([context, state, owner, roles](auto wake) mutable {
      context->memberships->run(0, [context, state, owner, roles,
	  wake = ZuMv(wake)]() mutable {
	context->memberships->findUpd<0>(0,
	  ZuFwdTuple(Zum::AppID{9}, Zum::UserID{42}),
	  [state, owner, roles, wake = ZuMv(wake)](
	      ZdbRow<Zum::Membership> *row) mutable {
	    if (!row) { wake(false); return; }
	    row->data().state = state;
	    row->data().owner = owner;
	    row->data().roleIDs.null();
	    if (roles) row->data().roleIDs.push(7);
	    ++row->data().authVersion;
	    wake(row->commit());
	  });
      });
    });
  };
  ZuCheck(changeMembership(Zum::State::Suspended, 0, true));
  authority = loadInteractiveAuthority(
    context, Zum::Grant{authorityGrant}, Zum::Client{browser});
  ZuCheck(authority.error == Zum::AuthorityError::Invalid);
  ZuCheck(changeMembership(Zum::State::Active, 1, true));
  authority = loadInteractiveAuthority(
    context, Zum::Grant{authorityGrant}, Zum::Client{browser});
  ZuCheck(authority.error == Zum::AuthorityError::Invalid);
  ZuCheck(changeMembership(Zum::State::Active, 0, false));
  authority = loadInteractiveAuthority(
    context, Zum::Grant{authorityGrant}, Zum::Client{browser});
  ZuCheck(authority.error == Zum::ScopeError::OK &&
    !authority.data.actions[readAction]);
  ZuCheck(changeMembership(Zum::State::Active, 0, true));
  authority = loadInteractiveAuthority(
    context, Zum::Grant{authorityGrant}, Zum::Client{browser});
  ZuCheck(authority.error == Zum::ScopeError::OK &&
    authority.data.actions[readAction]);
  Zum::Grant limitedGrant{authorityGrant};
  limitedGrant.actions = ZtBitmap{};
  authority = loadInteractiveAuthority(
    context, ZuMv(limitedGrant), Zum::Client{browser});
  ZuCheck(authority.error == Zum::ScopeError::OK &&
    !authority.data.actions[readAction]);

  Zum::Client workload;
  workload.id = "workload";
  workload.appID = 9;
  workload.type = Zum::ClientType::Confidential;
  workload.grants = Zum::ClientGrant::ClientCredentials;
  workload.state = Zum::State::Active;
  workload.secretDigest.length(Ztls::SecretHash::Size, false);
  ZuCheck(Ztls::secretHash(rng, ZuBSpan{"secret"},
    workload.secretDigest));
  ZuCheck(insertRecord(context->clients, Zum::Client{workload}));
  ZuCheck(insertRecord(context->clientAccess, Zum::ClientAccess{
    .clientID = "workload", .appID = 9,
    .audienceIDs = Zum::IDVec{appRoles},
    .roleIDs = Zum::IDVec{appRoles},
    .state = Zum::State::Active}));
  authority = loadClientAuthority(context, Zum::Client{workload});
  ZuCheck(authority.error == Zum::ScopeError::OK &&
    authority.data.clientAppID == 9 &&
    authority.data.selection.scope == "read" &&
    authority.data.actions[readAction] &&
    !authority.data.actions[writeAction]);

  ZtBitmap serviceActions{1U};
  serviceActions.set(0);
  Zum::IDVec serviceRoles;
  serviceRoles.push(901);
  ZuCheck(insertRecord(context->apps, Zum::App{
    .id = 900, .name = "authority-target", .state = Zum::State::Active,
    .nextActionID = 1, .authVersion = 4}));
  ZuCheck(insertRecord(context->apps, Zum::App{
    .id = 902, .name = "authority-owner", .state = Zum::State::Active}));
  ZuCheck(insertRecord(context->actions, Zum::Action{
    .appID = 900, .id = 0, .name = "Zum.operationQuery",
    .state = Zum::State::Active}));
  ZuCheck(insertRecord(context->roles, Zum::Role{
    .appID = 900, .id = 901, .name = "read",
    .actions = ZuMv(serviceActions), .state = Zum::State::Active}));
  ZuCheck(insertRecord(context->audiences, Zum::Audience{
    .id = 904, .appID = 900, .name = "management", .uri = "management",
    .state = Zum::State::Active}));
  Zum::Client service;
  service.id = "enrolled-service";
  service.appID = 902;
  service.type = Zum::ClientType::Confidential;
  service.grants = Zum::ClientGrant::ClientCredentials;
  service.state = Zum::State::Active;
  ZuCheck(insertRecord(context->clients, Zum::Client{service}));
  Zum::ClientAccess serviceAccess{
    .clientID = "enrolled-service", .appID = 900,
    .audienceIDs = Zum::IDVec{904},
    .roleIDs = Zum::IDVec{901},
    .state = Zum::State::Active};
  ZuCheck(insertRecord(context->clientAccess,
    Zum::ClientAccess{serviceAccess}));
  authority = loadClientAuthority(context, Zum::Client{service});
  ZuCheck(authority.error == Zum::ScopeError::OK &&
    authority.data.clientAppID == 902 &&
    authority.data.app.id == 900 &&
    authority.data.selection.scope == "read" &&
    authority.data.actions[0] &&
    authority.data.actionRecords.length() == 1 &&
    authority.data.actionRecords[0].appID == 900);

  // Missing rows, foreign ownership, disabled rows, and unapproved IDs
  // must not authorize a workload through a different audience URI.
  for (unsigned i = 0; i < 4; ++i) {
    Zum::AudienceID audienceID = 920 + i;
    Zum::String uri;
    uri << "denied-audience-" << i;
    if (i) ZuCheck(insertRecord(context->audiences, Zum::Audience{
      .id = audienceID, .appID = i == 1 ? 902U : 900U,
      .name = uri, .uri = uri,
      .state = Zum::State::T(
	i == 2 ? Zum::State::Disabled : Zum::State::Active)}));
    Zum::Client denied{service};
    denied.id = uri;
    ZuCheck(insertRecord(context->clients, Zum::Client{denied}));
    ZuCheck(insertRecord(context->clientAccess, Zum::ClientAccess{
      .clientID = denied.id, .appID = 900,
      .audienceIDs = Zum::IDVec{i == 3 ? Zum::AudienceID{904} : audienceID},
      .roleIDs = Zum::IDVec{901},
      .state = Zum::State::Active}));
    authority = loadClientAuthority(context, ZuMv(denied));
    ZuCheck(authority.error == (i == 3 ? Zum::ScopeError::OK :
      Zum::ScopeError::Unavailable));
  }

  Zum::Client noAccess{service};
  noAccess.id = "service-without-access";
  ZuCheck(insertRecord(context->clients, Zum::Client{noAccess}));
  authority = loadClientAuthority(context, ZuMv(noAccess));
  ZuCheck(authority.error == Zum::ScopeError::Unavailable);

  Zum::Client disabledOwner{service};
  disabledOwner.id = "service-disabled-owner";
  ZuCheck(insertRecord(context->clients, Zum::Client{disabledOwner}));
  ZuCheck(insertRecord(context->clientAccess, Zum::ClientAccess{
    .clientID = "service-disabled-owner", .appID = 900,
    .audienceIDs = Zum::IDVec{904},
    .roleIDs = Zum::IDVec{901},
    .state = Zum::State::Active}));
  ZuCheck(ZmBlock<bool>{}([context](auto wake) mutable {
    context->apps->run(0, [context, wake = ZuMv(wake)]() mutable {
      context->apps->findUpd<0>(0, ZuFwdTuple(Zum::AppID{902}), [
	wake = ZuMv(wake)](ZdbRow<Zum::App> *row) mutable {
	if (!row) { wake(false); return; }
	row->data().state = Zum::State::Disabled;
	wake(row->commit());
      });
    });
  }));
  authority = loadClientAuthority(context, ZuMv(disabledOwner));
  ZuCheck(authority.error == Zum::AuthorityError::Invalid);
  ZuCheck(ZmBlock<bool>{}([context](auto wake) mutable {
    context->apps->run(0, [context, wake = ZuMv(wake)]() mutable {
      context->apps->findUpd<0>(0, ZuFwdTuple(Zum::AppID{902}), [
	wake = ZuMv(wake)](ZdbRow<Zum::App> *row) mutable {
	if (!row) { wake(false); return; }
	row->data().state = Zum::State::Active;
	wake(row->commit());
      });
    });
  }));

  Ztls::PK::SK_EC tokenKey{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  uint8_t tokenPublic[Ztls::COSE::ES256::PublicKeySize];
  ZuCheck(Ztls::Backend::pkey_ec_export_public(tokenKey.key, tokenPublic));
  ZuCheck(setKeyRetirement(context, "app-token-key", 250));
  auto issued = issueTokenRequest(db->requests, db, context, rng,
    Zum::String{"grant_type=client_credentials"},
    Zum::String{"Basic d29ya2xvYWQ6c2VjcmV0"}, tokenKey, 200,
    "issuer/oauth2/9");
  ZuCheck(issued.error == Zum::OAuthError::ServerError &&
    !issued.response.accessToken);
  ZuCheck(setKeyRetirement(context, "app-token-key", 1000));
  ZuCheck(ZmBlock<bool>{}([context](auto wake) mutable {
    context->evidence->run(0, [context, wake = ZuMv(wake)]() mutable {
      context->evidence->findUpd<0>(0, ZuFwdTuple(Zum::AppID{9},
          Zum::UserID{44}, Zum::ProviderID{8}), [wake = ZuMv(wake)](
          ZdbRow<Zum::Evidence> *row) mutable {
        if (!row) { wake(false); return; }
        row->data().deadline = 275;
        wake(row->commit());
      });
    });
  }));
  Zum::String oidcCodeForm{"grant_type=authorization_code&code="};
  oidcCodeForm << oidcCode <<
    "&client_id=oidc-browser&redirect_uri=https%3A%2F%2Fapp%2Fcb" <<
    "&code_verifier=" <<
    "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
  issued = issueTokenRequest(db->requests, db, context, rng,
    ZuMv(oidcCodeForm), {}, tokenKey, 250, "issuer/oauth2/9");
  Zum::Principal principal;
  Zum::JWTHeader idHeader;
  Zum::String idJSON;
  ZuCheck(issued.error == Zum::TokenIssue::OK &&
    !issued.response.refreshToken && issued.response.idToken &&
    issued.response.scope == "openid read" &&
    issued.response.expiresIn == 25 &&
    Zum::jwtVerify(issued.response.accessToken, "app-token-key", "issuer/oauth2/9",
      "orders", tokenPublic, 270, Zum::JWTLimits{}, principal));
  ZuCheck(Zum::jwtES256(issued.response.idToken, tokenPublic,
    Zum::JWTLimits{}, idHeader, idJSON) && idHeader.type == "JWT" &&
    idJSON.find<"\"iss\":\"issuer/oauth2/9\"">() >= 0 &&
    idJSON.find<"\"aud\":\"oidc-browser\"">() >= 0 &&
    idJSON.find<"\"nonce\":\"local-nonce\"">() >= 0 &&
    idJSON.find<"\"sub\":\"b2lkYyBoYW5kbGU\"">() >= 0);
  ZuCheck(principal.subject == base64URL(ZuBSpan{"oidc handle"}) &&
    principal.authMethod == "oidc" &&
    principal.authTime == 190 && principal.actions.length() == 1 &&
    principal.actions[0] == "orders.read");
  issued = issueTokenRequest(db->requests, db, context, rng,
    Zum::String{"grant_type=client_credentials"},
    Zum::String{"Basic d29ya2xvYWQ6c2VjcmV0"}, tokenKey, 200,
    "issuer/oauth2/9");
  ZuCheck(issued.error == Zum::TokenIssue::OK &&
    issued.response.scope == "read" && !issued.response.refreshToken &&
    issued.response.expiresIn == 60 && Zum::jwtVerify(
      issued.response.accessToken, "app-token-key", "issuer/oauth2/9", "orders",
      tokenPublic, 220, Zum::JWTLimits{}, principal));
  ZuCheck(principal.subject == "workload" &&
    principal.clientID == "workload" && !principal.authMethod &&
    principal.appID == 9 && principal.actions.length() == 1 &&
    principal.actions[0] == "orders.read");

  Zum::AssertionInput requestAssertion;
  ZuCheck(makeAssertion(rng, passkey, 7, requestGrant.challenge,
    "https://example.com", "example.com", requestAssertion));
  requestAssertion.userHandle = passkeyHandle;
  auto wrongFinishIssuer = finishAuthorization(
    db->requests, db, context, rng, Zum::Bytes{authorized.result.ceremonyID},
    Zum::AssertionInput{requestAssertion}, readAction, 210,
    "issuer/oauth2/10", 9);
  ZuCheck(wrongFinishIssuer.error == Zum::OAuthError::AccessDenied &&
    !wrongFinishIssuer.location);
  auto wrongFinishApp = finishAuthorization(
    db->requests, db, context, rng, Zum::Bytes{authorized.result.ceremonyID},
    Zum::AssertionInput{requestAssertion}, readAction, 210,
    "issuer/oauth2/9", 10);
  ZuCheck(wrongFinishApp.error == Zum::OAuthError::AccessDenied &&
    !wrongFinishApp.location);
  auto finished = finishAuthorization(db->requests, db, context, rng,
    Zum::Bytes{authorized.result.ceremonyID}, ZuMv(requestAssertion),
    readAction, 210, "issuer/oauth2/9", 9, true);
  constexpr ZuCSpan codePrefix{"https://app/cb?code="};
  constexpr ZuCSpan codeSuffix{"&state=return"};
  ZuCheck(finished.error == Zum::AuthorizeIssue::OK &&
    finished.location.length() > codePrefix.length() + codeSuffix.length() &&
    (ZuCSpan{finished.location.data(), codePrefix.length()} == codePrefix) &&
    (ZuCSpan{finished.location.data() + finished.location.length() -
      codeSuffix.length(), codeSuffix.length()} == codeSuffix));
  ZuCheck(logged(
    Zum::String{} << " event=" << int(Zum::AuditEvent::Authentication) << " ",
    Zum::String{} << " outcome=" << int(Zum::AuditOutcome::Success) << " ",
    Zum::String{} << " actor=" << "browser" << " ",
    Zum::String{} << " subject=" << Zum::auditID(passkeyHandle) << " ",
    Zum::String{} << " target=" << Zum::auditID(ZuBSpan{"credential"}) << " "));

  Zum::String repeatedAuthorizeQuery{
    "response_type=code&client_id=browser&"};
  repeatedAuthorizeQuery <<
    "redirect_uri=https%3A%2F%2Fapp%2Fcb&scope=read&state=return&" <<
    "code_challenge=" <<
    "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM&" <<
    "code_challenge_method=S256";
  auto repeated = beginAuthorization(
    db->requests, context, rng, ZuMv(repeatedAuthorizeQuery), 200,
    "issuer/oauth2/9");
  auto repeatedGrant = loadGrant(context, repeated.result.ceremonyID);
  Zum::AssertionInput repeatedAssertion;
  ZuCheck(repeated.error == Zum::AuthorizeIssue::OK &&
    makeAssertion(rng, passkey, 7, repeatedGrant.challenge,
      "https://example.com", "example.com", repeatedAssertion));
  repeatedAssertion.userHandle = passkeyHandle;
  auto rejected = finishAuthorization(db->requests, db, context, rng,
    Zum::Bytes{repeated.result.ceremonyID}, ZuMv(repeatedAssertion),
    readAction);
  ZuCheck(rejected.error == Zum::OAuthError::AccessDenied);
  ZuCheck(logged(
    Zum::String{} << " event=" << int(Zum::AuditEvent::Authentication) << " ",
    Zum::String{} << " outcome=" << int(Zum::AuditOutcome::Failure) << " ",
    Zum::String{} << " detail=" << "counter regression" << " ",
    Zum::String{} << " actor=" << "browser" << " ",
    Zum::String{} << " subject=" << Zum::auditID(passkeyHandle) << " ",
    Zum::String{} << " target=" << Zum::auditID(ZuBSpan{"credential"}) << " "));
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
    db->requests, db, context, rng, ZuMv(codeForm), {}, tokenKey, 250,
    "issuer/oauth2/9");
  ZuCheck(issued.error == Zum::TokenIssue::OK &&
    issued.response.refreshToken &&
    issued.response.scope == "read offline_access" &&
    issued.response.expiresIn == 60 && Zum::jwtVerify(
      issued.response.accessToken, "app-token-key", "issuer/oauth2/9", "orders",
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
    codeFamily.scope == issued.response.scope &&
    codeFamily.authVersion == 12 && codeFamily.requestedRoleIDs.length() == 1 &&
    codeFamily.requestedRoleIDs[0] == 7 && codeFamily.actions[readAction] &&
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
    .appID = 9,
    .userID = 42,
    .created = 200,
    .expires = 1000,
    .kind = Zum::GrantKind::Refresh,
    .state = Zum::State::Active,
    .issuer = "issuer/oauth2/9",
    .clientID = "browser",
    .audience = "orders",
    .authTime = 123,
    .requestedRoleIDs = ZuMv(refreshScopes),
    .roleIDs = ZuMv(refreshRoles),
    .actions = ZuMv(refreshActions),
    .credentialID = Zum::Bytes{ZuBSpan{"credential"}},
    .digest = refresh.digest,
    .scope = "read"
  };
  ZuCheck(insertRecord(context->grants, Zum::Grant{refreshFamily}));
  Zum::String refreshForm{"grant_type=refresh_token&refresh_token="};
  refreshForm << refresh.token << "&client_id=browser";
  issued = issueTokenRequest(
    db->requests, db, context, rng, ZuMv(refreshForm), {}, tokenKey, 300,
    "issuer/oauth2/9");
  ZuCheck(issued.error == Zum::TokenIssue::OK &&
    issued.response.refreshToken && issued.response.scope == "read" &&
    issued.response.expiresIn == 60 && Zum::jwtVerify(
      issued.response.accessToken, "app-token-key", "issuer/oauth2/9", "orders",
      tokenPublic, 320, Zum::JWTLimits{}, principal));
  ZuCheck(principal.authMethod == "passkey" && principal.authTime == 123 &&
    principal.appID == 9 && principal.actions.length() == 1 &&
    principal.actions[0] == "orders.read");
  Zum::Bytes rotatedID, rotatedDigest;
  ZuCheck(Zum::opaqueParse(
    issued.response.refreshToken, rotatedID, rotatedDigest) &&
    rotatedID == refresh.id);
  auto rotated = loadGrant(context, refresh.id);
  ZuCheck(rotated.generation == 1 && rotated.digest == rotatedDigest &&
    rotated.scope == issued.response.scope &&
    rotated.spent.length() == 1 && rotated.spent[0] == refresh.digest &&
    rotated.requestedRoleIDs.length() == 1 && rotated.requestedRoleIDs[0] == 7 &&
    rotated.actions[readAction] && !rotated.actions[writeAction]);
  Zum::String reuseForm{"grant_type=refresh_token&refresh_token="};
  reuseForm << refresh.token << "&client_id=browser";
  issued = issueTokenRequest(
    db->requests, db, context, rng, ZuMv(reuseForm), {}, tokenKey, 330,
    "issuer/oauth2/9");
  ZuCheck(issued.error == Zum::OAuthError::InvalidGrant &&
    loadGrant(context, refresh.id).state == Zum::State::Revoked);
  ZuCheck(logged(
    Zum::String{} << " event=" << int(Zum::AuditEvent::RefreshReuse) << " ",
    Zum::String{} << " outcome=" << int(Zum::AuditOutcome::Failure) << " ",
    Zum::String{} << " actor=" << "browser" << " ",
    Zum::String{} << " target=" << Zum::auditID(refresh.id) << " "));

  issued = issueTokenRequest(db->requests, db, context, rng,
    Zum::String{"grant_type=client_credentials"},
    Zum::String{"Basic d29ya2xvYWQ6d3Jvbmc="}, tokenKey, 340,
    "issuer/oauth2/9");
  ZuCheck(issued.error == Zum::OAuthError::InvalidClient);
  ZuCheck(logged(
    Zum::String{} << " event=" << int(Zum::AuditEvent::ClientAuthentication) << " ",
    Zum::String{} << " outcome=" << int(Zum::AuditOutcome::Failure) << " ",
    Zum::String{} << " actor=" << "workload" << " "));

  Zum::String recoveryCapability;
  auto beforeRecovery = loadUser(context, 42);
  ZuCheck(insertRecord(context->grants, Zum::Grant{
    .id = Zum::Bytes{ZuBSpan{"recovery-collision"}}, .userID = 45,
    .created = 349, .expires = 410, .state = Zum::State::Active, .issuer = "issuer"}));
  ZuCheck(!runSaga(db, Zum::RecoveryStart{
    .capabilityID = Zum::Bytes{ZuBSpan{"recovery-collision"}},
    .digest = Zum::Bytes{ZuBSpan{"digest"}}, .issuer = "issuer", .userID = 42,
    .userVersion = beforeRecovery.authVersion + 1, .created = 350, .expires = 410,
    .actor = "administrator", .version = beforeRecovery.version,
    .oldState = beforeRecovery.state, .oldUpdated = beforeRecovery.updated,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::userRecover, .idempotencyKey = "recovery-rollback"}},
    ZdbSagaID{12002}));
  auto afterRecovery = loadUser(context, 42);
  ZuCheck(afterRecovery.state == beforeRecovery.state &&
    afterRecovery.version == beforeRecovery.version &&
    afterRecovery.authVersion == beforeRecovery.authVersion &&
    afterRecovery.updated == beforeRecovery.updated && !afterRecovery.owner);
  ZuCheck(loadGrant(context, ZuBSpan{"recovery-collision"}).userID == 45);
  auto recoveryVersion = loadUser(context, 42).version;
  ZuCheck(!runSaga(db, Zum::RecoveryStart{
    .capabilityID = Zum::Bytes{ZuBSpan{"stale-recovery"}},
    .digest = Zum::Bytes{ZuBSpan{"digest"}},
    .issuer = "issuer", .userID = 42, .userVersion = beforeRecovery.authVersion + 1,
    .created = 350, .expires = 410, .actor = "administrator",
    .version = recoveryVersion + 1, .oldState = beforeRecovery.state,
    .oldUpdated = beforeRecovery.updated}, ZdbSagaID{9007}));
  ZuCheck(ZmBlock<bool>{}([
    &db, &context, &rng, recoveryVersion
  ](auto wake) mutable {
    Zum::recoveryIssue(db->requests, Zm::now() + ZuTime{10},
      db, context, rng, Zum::RecoveryIssueConfig{
        .issuer = "issuer", .actor = "administrator", .userID = 42,
        .now = 350, .expires = 410, .version = recoveryVersion + 1},
      [wake = ZuMv(wake)](bool ok, Zum::String capability) mutable {
        wake(!ok && !capability);
      });
  }));
  ZuCheck(loadUser(context, 42).version == recoveryVersion &&
    loadUser(context, 42).state == Zum::State::Active);
  ZuCheck(ZmBlock<bool>{}([
    &db, &context, &rng, &recoveryCapability, recoveryVersion
  ](auto wake) mutable {
    Zum::recoveryIssue(db->requests, Zm::now() + ZuTime{10},
      db, context, rng, Zum::RecoveryIssueConfig{
      .issuer = "issuer",
      .actor = "administrator",
      .userID = 42,
      .now = 350,
      .expires = 410,
      .version = recoveryVersion
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
    recoveredUser.authVersion == 2 && !recoveredUser.owner &&
    recoveredUser.version == recoveryVersion + 1);
  ZuCheck(recoveryGrant.kind == Zum::GrantKind::Capability &&
    recoveryGrant.purpose == Zum::GrantPurpose::Recovery &&
    recoveryGrant.state == Zum::State::Active && !recoveryGrant.owner &&
    recoveryGrant.userID == 42 && recoveryGrant.userVersion == 2 &&
    Ztls::ctEqual(recoveryGrant.digest, recoveryDigest));
  ZuCheck(releaseToken(context, codeFamilyID, 12, false, 350, 9));
  ZuCheck(logged(
    Zum::String{} << " event=" << int(Zum::AuditEvent::PrincipalChange) << " ",
    Zum::String{} << " outcome=" << int(Zum::AuditOutcome::Success) << " ",
    Zum::String{} << " actor=" << "administrator" << " ",
    Zum::String{} << " subject=" << Zum::auditID(passkeyHandle) << " ",
    Zum::String{} << " detail=" << "recovery suspended" << " "));

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
  auto existingRecoveryCred = loadCred(context, ZuBSpan{"credential"});
  ZuCheck(bool(existingRecoveryCred.id));
  Zum::RecoveryEnroll rollbackRecovery{
    .ceremonyID = recoveryGrant.id, .issuer = recoveryGrant.issuer,
    .actor = recoveryGrant.actor, .userID = 42, .userVersion = 2,
    .oldHandle = recoveredUser.handle, .newHandle = recoveryHandle,
    .credentialID = Zum::Bytes{ZuBSpan{"credential"}}, .created = 352,
    .beforeGrant = recoveryGrant, .beforeUser = recoveredUser};
  ZuCheck(!runSaga(db, rollbackRecovery, ZdbSagaID{12014}));
  ZuCheck(Zum::SagaImage::save(loadCred(context, existingRecoveryCred.id)) ==
    Zum::SagaImage::save(existingRecoveryCred));
  ZuCheck(loadGrant(context, recoveryGrant.id).state == Zum::State::Active &&
    !loadGrant(context, recoveryGrant.id).owner);
  rollbackRecovery.credentialID = Zum::Bytes{ZuBSpan{"stale-recovery-credential"}};
  --rollbackRecovery.beforeUser.version;
  ZuCheck(!runSaga(db, ZuMv(rollbackRecovery), ZdbSagaID{12015}));
  ZuCheck(!loadCred(context, ZuBSpan{"stale-recovery-credential"}).id);
  auto rollbackUser = loadUser(context, 42);
  ZuCheck(rollbackUser.state == Zum::State::Suspended && !rollbackUser.owner &&
    rollbackUser.version == recoveredUser.version &&
    rollbackUser.updated == recoveredUser.updated && rollbackUser.handle == recoveredUser.handle);
  ZuCheck(loadGrant(context, recoveryGrant.id).state == Zum::State::Active &&
    !loadGrant(context, recoveryGrant.id).owner);
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
  ZuCheck(logged(
    Zum::String{} << " event=" << int(Zum::AuditEvent::PrincipalChange) << " ",
    Zum::String{} << " actor=" << "administrator" << " ",
    Zum::String{} << " subject=" << Zum::auditID(recoveryHandle) << " ",
    Zum::String{} << " detail=" << "recovery completed" << " "));
  ZuCheck(logged(
    Zum::String{} << " event=" << int(Zum::AuditEvent::CredentialChange) << " ",
    Zum::String{} << " actor=" << "administrator" << " ",
    Zum::String{} << " subject=" << Zum::auditID(recoveryHandle) << " ",
    Zum::String{} << " target=" << Zum::auditID(ZuBSpan{"recovery-credential"}) << " ",
    Zum::String{} << " detail=" << "recovery" << " "));

  Zum::String enrollmentQuery{
    "response_type=code&client_id=browser&"};
  enrollmentQuery <<
    "redirect_uri=https%3A%2F%2Fapp%2Fcb&scope=read&state=enroll&" <<
    "code_challenge=" <<
    "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM&" <<
    "code_challenge_method=S256";
  auto enrollmentAuthorization = beginAuthorization(
    db->requests, context, rng, ZuMv(enrollmentQuery), 420,
    "issuer/oauth2/9");
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
  ZuCheck(insertRecord(context->memberships, Zum::Membership{
    .appID = 9, .userID = 43, .roleIDs = Zum::IDVec{appRoles},
    .state = Zum::State::Active}));
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
    db->requests, db, context, rng,
    ZuMv(enrollmentAuthorization.result.ceremonyID),
    ZuMv(enrollmentAssertion), readAction, 423);
  ZuCheck(enrollmentFinished.error == Zum::AuthorizeIssue::OK &&
    enrollmentFinished.location.find<"state=enroll">() >= 0);

  Zum::KeyAdd nextKey{.before = Zum::SignKey{.id = "next-key", .version = 0},
    .values = Zum::SignKey{.id = "next-key", .issuer = "issuer", .algorithm = "ES256",
      .providerRef = "test-key", .publicJwk = testJwk("next-key"),
      .notBefore = 430}, .updated = 430,
    .request = Zum::IdemRequest{.actorID = "operator",
      .operation = Zum::MgmtOp::signKeyAdd, .idempotencyKey = "next-key"}};
  auto nextKeyRecord = nextKey.result();
  ZuCheck(runSaga(db, ZuMv(nextKey), ZdbSagaID{12003}));
  auto selectedKey = [context](Zum::String issuer, int64_t now,
      unsigned limit) {
    return ZmBlock<Zum::SignKey>{}([context, issuer = ZuMv(issuer), now,
      limit](auto wake) mutable {
      Zum::signKeyLoad(context, ZuMv(issuer), now, now + 60, limit,
	[wake = ZuMv(wake)](Zum::SignKey key) mutable { wake(ZuMv(key)); });
    });
  };
  ZuCheck(selectedKey("issuer", 430, 4).id == "next-key");
  ZuCheck(!selectedKey("other-issuer", 430, 4).id);
  ZuCheck(!selectedKey("issuer", 99, 4).id);
  ZuCheck(!selectedKey("issuer", 430, 1).id);
  ZuCheck(!selectedKey("issuer", 430, 0).id);
  ZuCheck(!selectedKey("issuer", 430, UINT_MAX).id);
  auto issuedKey = [db, context, &rng, &tokenKey, &tokenPublic](
      int64_t now, ZuCSpan id) {
    auto issued = issueTokenRequest(db->requests, db, context, rng,
      Zum::String{"grant_type=client_credentials"},
      Zum::String{"Basic d29ya2xvYWQ6c2VjcmV0"}, tokenKey, now);
    Zum::Principal principal;
    return issued.error == Zum::TokenIssue::OK && Zum::jwtVerify(
      issued.response.accessToken, id, "issuer", "orders", tokenPublic,
      now, Zum::JWTLimits{}, principal);
  };
  ZuCheck(issuedKey(429, "token-key"));
  ZuCheck(issuedKey(430, "next-key"));
  ZuCheck(runSaga(db, Zum::KeyRetire{.before = nextKeyRecord, .ifMatch = "\"v1\"",
    .retireAfter = 500, .updated = 431,
    .request = Zum::IdemRequest{.actorID = "operator",
      .operation = Zum::MgmtOp::signKeyRetire, .idempotencyKey = "retire-next-key"}},
    ZdbSagaID{12004}));
  ZuCheck(issuedKey(431, "token-key"));
  ZuCheck(selectedKey("issuer", 431, 4).id == "token-key");
  auto overlapJWKS = ZmBlock<Zum::String>{}([db, context](auto wake) mutable {
    Zum::jwksLoad(db->requests, Zm::now() + ZuTime{10}, context,
      "issuer", 499, 4,
      [wake = ZuMv(wake)](bool ok, Zum::String json) mutable {
        wake(ok ? ZuMv(json) : Zum::String{});
      });
  });
  ZuCheck(overlapJWKS.find<"next-key">() >= 0);
  ZuCheck(ZmBlock<bool>{}([context](auto wake) mutable {
    auto keys = context->signKeys;
    keys->run(0, [keys, wake = ZuMv(wake)]() mutable {
      keys->find<1>(0, ZuFwdTuple(int64_t{500}),
	[keys, wake = ZuMv(wake)](ZdbRowRef<Zum::SignKey> row) mutable {
	  if (!row || row->data().id != "next-key") { wake(false); return; }
	  keys->find<2>(0, ZuFwdTuple(Zum::String{"issuer"}, int64_t{500}),
	    [wake = ZuMv(wake)](ZdbRowRef<Zum::SignKey> row) mutable {
	      wake(row && row->data().id == "next-key" &&
	        row->data().state == Zum::State::Suspended && row->data().version == 2);
	    });
	});
    });
  }));
  auto retiredJWKS = ZmBlock<Zum::String>{}([
    &db, &context
  ](auto wake) mutable {
    Zum::jwksLoad(db->requests, Zm::now() + ZuTime{10},
      context, "issuer", 501, 4, [wake = ZuMv(wake)](
	bool ok, Zum::String json) mutable {
      wake(ok ? ZuMv(json) : Zum::String{});
    });
  });
  ZuCheck(retiredJWKS.find<"next-key">() < 0 &&
    retiredJWKS.find<"token-key">() >= 0);

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
	.issuer = "issuer", .appID = 9, .now = 452
      }, [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::RevokeIssue::OK);
  ZuCheck(loadGrant(context, revokeToken.id).state == Zum::State::Revoked);
  ZuCheck(logged(
    Zum::String{} << " event=" << int(Zum::AuditEvent::Revocation) << " ",
    Zum::String{} << " outcome=" << int(Zum::AuditOutcome::Success) << " ",
    Zum::String{} << " actor=" << "browser" << " ",
    Zum::String{} << " subject=" << Zum::auditID(revokeToken.id) << " "));
  Zum::OpaqueToken unknownToken;
  ZuCheck(Zum::opaqueIssue(rng, unknownToken));
  Zum::String unknownForm{"token="};
  unknownForm << unknownToken.token << "&client_id=browser";
  ZuCheck(ZmBlock<int>{}([
    &db, &context, &unknownForm
  ](auto wake) mutable {
    Zum::revokeRequest(db->requests, Zm::now() + ZuTime{10}, context,
      ZuMv(unknownForm), {}, Zum::RevokeConfig{
	.issuer = "issuer", .appID = 9, .now = 453
      }, [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::RevokeIssue::OK);

  // Enrollment/recovery changes must update the secondary handle index,
  // not only the cached row found through its primary user ID.
  ZuCheck(ZmBlock<bool>{}([context](auto wake) mutable {
    auto users = context->users;
    users->run(0, [users, wake = ZuMv(wake)]() mutable {
      users->findUpd<0>(0, ZuFwdTuple(Zum::UserID{45}),
        [users, wake = ZuMv(wake)](ZdbRow<Zum::User> *row) mutable {
        if (!row) { wake(false); return; }
        row->data().handle = Zum::Bytes{ZuBSpan{"indexed-user-handle"}};
        if (!row->commit()) { wake(false); return; }
        users->find<1>(0, ZuFwdTuple(ZuBSpan{"indexed-user-handle"}),
          [wake = ZuMv(wake)](ZdbRowRef<Zum::User> user) mutable {
          wake(user && user->data().id == 45 &&
            user->data().handle == ZuBSpan{"indexed-user-handle"});
        });
      });
    });
  }));

  // Explicit consent and code issuance are one native operation.
  ZuCheck(insertRecord(context->apps, Zum::App{
    .id = 40000, .name = "consent-saga", .state = Zum::State::Active,
    .authVersion = 1}));
  ZuCheck(insertRecord(context->users, Zum::User{
    .id = 40000, .name = "consent-saga-user",
    .handle = Zum::Bytes{ZuBSpan{"consent-saga-user"}}, .state = Zum::State::Active}));
  Zum::Client consentClient;
  consentClient.id = "consent-saga-client";
  consentClient.appID = 40000;
  consentClient.state = Zum::State::Active;
  ZuCheck(insertRecord(context->clients, consentClient));
  Zum::ConsentCode consentChange;
  auto &ceremony = consentChange.beforeGrant;
  ceremony.id = Zum::Bytes{ZuBSpan{"consent-code-001"}};
  ceremony.issuer = "issuer";
  ceremony.appID = 40000;
  ceremony.userID = 40000;
  ceremony.clientID = consentClient.id;
  ceremony.audienceID = 7;
  ceremony.requestedRoleIDs = {7};
  ceremony.bindingDigest = Zum::Bytes{ZuBSpan{"consent binding"}};
  ceremony.authVersion = 1;
  ceremony.userVersion = 1;
  ceremony.authTime = 100;
  ceremony.created = 100;
  ceremony.expires = 200;
  ceremony.state = Zum::State::Pending;
  consentChange.afterGrant = ceremony;
  consentChange.afterGrant.kind = Zum::GrantKind::Code;
  consentChange.afterGrant.state = Zum::State::Active;
  consentChange.afterGrant.digest = Zum::Bytes{ZuBSpan{"consent code digest"}};
  consentChange.afterGrant.bindingDigest.null();
  consentChange.beforeConsent.version = 0;
  consentChange.roleIDs = {7};
  consentChange.now = 101;
  auto readConsent = [context]() {
    return ZmBlock<Zum::Consent>{}([context](auto wake) mutable {
      context->consents->run(0, [context, wake = ZuMv(wake)]() mutable {
	context->consents->find<0>(0, ZuFwdTuple(Zum::UserID{40000},
	  Zum::String{"consent-saga-client"}, Zum::AppID{40000}, Zum::AudienceID{7}),
	  [wake = ZuMv(wake)](ZdbRowRef<Zum::Consent> row) mutable {
	    wake(row ? Zum::Consent{row->data()} : Zum::Consent{});
	  });
      });
    });
  };
  ZuCheck(insertRecord(context->grants, ceremony));
  ZuCheck(runSaga(db, consentChange, ZdbSagaID{12019}));
  auto consent = readConsent();
  ZuCheck(consent.userID == 40000 && !consent.owner && consent.version == 1 &&
    consent.roleIDs == Zum::IDVec{7} && consent.created == 101 && consent.updated == 101);
  auto consentCode = loadGrant(context, ceremony.id);
  ZuCheck(consentCode.kind == Zum::GrantKind::Code && !consentCode.owner &&
    consentCode.digest == consentChange.afterGrant.digest && !consentCode.bindingDigest);

  ceremony.id = Zum::Bytes{ZuBSpan{"consent-code-002"}};
  consentChange.afterGrant.id = ceremony.id;
  consentChange.beforeConsent = consent;
  consentChange.beforeConsent.version = 2; // stale, row is still version one
  consentChange.roleIDs.push(8);
  consentChange.now = 102;
  ZuCheck(insertRecord(context->grants, ceremony));
  ZuCheck(!runSaga(db, consentChange, ZdbSagaID{12020}));
  ZuCheck(Zum::SagaImage::save(loadGrant(context, ceremony.id)) ==
    Zum::SagaImage::save(ceremony));
  ZuCheck(Zum::SagaImage::save(readConsent()) == Zum::SagaImage::save(consent));

  consentChange.beforeConsent = consent;
  ZuCheck(runSaga(db, consentChange, ZdbSagaID{12021}));
  consent = readConsent();
  ZuCheck((!consent.owner && consent.version == 2 && consent.roleIDs == Zum::IDVec{7, 8} &&
    consent.created == 101 && consent.updated == 102));
  consentCode = loadGrant(context, ceremony.id);
  ZuCheck(!consentCode.owner && consentCode.kind == Zum::GrantKind::Code);

  ceremony.id = Zum::Bytes{ZuBSpan{"consent-code-003"}};
  consentChange.afterGrant.id = ceremony.id;
  consentChange.beforeConsent = {};
  consentChange.beforeConsent.version = 0; // race: consent was inserted meanwhile
  ZuCheck(insertRecord(context->grants, ceremony));
  ZuCheck(!runSaga(db, consentChange, ZdbSagaID{12022}));
  ZuCheck(Zum::SagaImage::save(loadGrant(context, ceremony.id)) ==
    Zum::SagaImage::save(ceremony));
  ZuCheck(Zum::SagaImage::save(readConsent()) == Zum::SagaImage::save(consent));

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
  ZuCheck(!logged("next workload secret"));
  ZuCheck(recoveryCapability && !logged(recoveryCapability));
  ZuCheck(browserCode && !logged(browserCode));
  ZiLog::stop();
  ZiLog::sink(ZiLog::debugSink());
  logRows.null();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(loginNames);
  ZuTestCall(managementCatalog);
  ZuTestCall(jsonContract);
  ZuTestCall(requests);
  ZuTestCall(oauthForms);
  ZuTestCall(oidcRoles);
  ZuTestCall(httpRoutes);
  ZuTestCall(limits);
  ZuTestCall(redirects);
  ZuTestCall(pkce);
  ZuTestCall(opaque);
  ZuTestCall(actions);
  ZuTestCall(records);
  ZuTestCall(appRecords);
  ZuTestCall(appAuthority);
  ZuTestCall(discovery);
  ZuTestCall(refresh);
  ZuTestCall(jwt);
  ZuTestCall(ssfSET);
  ZuTestCall(webAuthn);
  ZuTestCall(webAuthnOptions);
  ZuTestCall(webAuthnInput);
  ZuTestCall(enrollmentSaga);
  ZuTestCall(enrollmentRuntime);
  return 0;
}
