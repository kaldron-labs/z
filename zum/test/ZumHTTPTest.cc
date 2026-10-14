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
  ZuCheck(child.u.cdispatch([](auto I, const auto &request) {
    if constexpr (ZuIsSame<typename decltype(child.u)::template Type<I>,
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
    finish.body == "{\"redirect_uri\":\"http://127.0.0.1:49152/cb?code=x&state=y\"}" &&
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
    "{\"redirect_uri\":\"https://app/cb?state=\\\"quoted\\\"\"}");
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
  form.bodyMax(Zum::ServerLimitMax::Form);
  ZuCheck(form.bodyInfo(Zhttp::BodyType::Fixed, Zum::ServerLimitMax::Form));
  ZuCheck(!form.bodyInfo(
    Zhttp::BodyType::Fixed, Zum::ServerLimitMax::Form + 1));
  Zum::PasskeyBeginReq<HTTPTestApp> json;
  json.init();
  json.bodyMax(Zum::ServerLimitMax::JSON);
  ZuCheck(json.bodyInfo(Zhttp::BodyType::Fixed, Zum::ServerLimitMax::JSON));
  ZuCheck(!json.bodyInfo(
    Zhttp::BodyType::Fixed, Zum::ServerLimitMax::JSON + 1));

  ZuCheck(Zum::AuthorityScanLimit::RoleMappingsScan ==
      Zum::AuthorityScanLimit::RoleMappings + 1 &&
    Zum::AuthorityScanLimit::ClientAccessScan ==
      Zum::AuthorityScanLimit::ClientAccess + 1 &&
    Zum::AdminQueryLimit::Scan == Zum::AdminQueryLimit::Results + 1);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(httpRoutes);
  ZuTestCall(limits);
  return 0;
}
