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

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(discovery);
  return 0;
}
