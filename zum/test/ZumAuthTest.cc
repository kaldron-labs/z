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
#include <zlib/zumd_saga_admin_ops.hh>
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

  Zum::Client client{.id = "browser", .appID = 1};
  client.identityScopes.push("openid");
  client.identityScopes.push("profile");
  ZuCheck(Zum::clientType(Zum::ClientProfile::Browser) ==
    Zum::ClientType::Public);
  ZuCheck(Zum::clientType(Zum::ClientProfile::Native) ==
    Zum::ClientType::Public);
  ZuCheck(Zum::clientType(Zum::ClientProfile::Server) ==
    Zum::ClientType::Confidential);
  ZuCheck(Zum::clientType(Zum::ClientProfile::T(-1)) < 0);
  Zum::StringVec redirects;
  redirects.push("https://app.example/callback");
  ZuCheck(!Zum::clientConfigValid(Zum::ClientProfile::Native,
    Zum::ClientGrant::AuthCode(), true, redirects));
  ZuCheck(Zum::clientConfigValid(Zum::ClientProfile::Native,
    Zum::ClientGrant::AuthCode() | Zum::ClientGrant::Refresh(),
    true, redirects));
  // A native client stays public even when it persists tokens in a vault.
  ZuCheck(!Zum::clientConfigValid(Zum::ClientProfile::Native,
    Zum::ClientGrant::ClientCredentials(), false, redirects));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(actions);
  return 0;
}
