//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "ZumSagaTestSupport.hh"
#include <zlib/zumd_saga_revoke.hh>
#include <zlib/zumd_saga_grant_cleanup.hh>
#include <zlib/zumd_saga_session_cleanup.hh>
#include <zlib/zumd_saga_refresh_cleanup.hh>
#include <zlib/zumd_saga_app_cleanup.hh>
#include <zlib/zumd_saga_consent_code.hh>
#include <zlib/zumd_saga_image.hh>

static void session()
{
  ZuTestScope(session);
  roundTrip(Zum::revokeDef(), Zum::revoke(Zum::RevokeArgs{}));
  roundTrip(Zum::grantCleanupDef(), Zum::grantCleanup(Zum::GrantCleanupArgs{}));
  roundTrip(Zum::sessionCleanupDef(), Zum::sessionCleanup(Zum::SessionCleanupArgs{}));
  roundTrip(Zum::refreshCleanupDef(), Zum::refreshCleanup(Zum::RefreshCleanupArgs{}));
  roundTrip(Zum::appCleanupDef(), Zum::appCleanup(Zum::AppCleanupArgs{}));
  roundTrip(Zum::consentCodeDef(), Zum::consentCode(Zum::ConsentCodeArgs{}));

  Zum::RevokeArgs revoke;
  revoke.updated = 147;
  Zum::Grant grant{.id = Zum::Bytes{ZuBSpan{"revoke-grant"}},
    .state = Zum::State::Active};
  revoke.grants.push(Zum::SagaImage::save(grant));
  grant.state = Zum::State::Consumed;
  revoke.grants.push(Zum::SagaImage::save(grant));
  grant.state = Zum::State::Revoked;
  revoke.grants.push(Zum::SagaImage::save(grant));
  grant.state = Zum::State::Active;
  grant.owner = 1;
  revoke.grants.push(Zum::SagaImage::save(grant));
  ZuCheck(revoke.count() == 1);
  auto revokeSaga = Zum::revoke(ZuMv(revoke));
  ZuCheck(revokeSaga->nSteps() == 10);
  distinct(Zum::revokeDef(), ZuMv(revokeSaga),
    Zum::revoke(Zum::RevokeArgs{}));
  phase(Zum::revokeDef(), 3, "zum.grant", ZdbSagaOp::Update);
  phase(Zum::revokeDef(), 7, "zum.request", ZdbSagaOp::Update);

  Zum::GrantCleanupArgs grants;
  grants.updated = 147;
  grants.grants.push(Zum::GrantDelete{
    .id = Zum::Bytes{ZuBSpan{"revoke-grant"}}, .expires = 146,
    .state = Zum::State::Active});
  auto cleanup = Zum::grantCleanup(ZuMv(grants));
  ZuCheck(cleanup->nSteps() == 3);
  distinct(Zum::grantCleanupDef(), ZuMv(cleanup),
    Zum::grantCleanup(Zum::GrantCleanupArgs{}));
  phase(Zum::grantCleanupDef(), 1, "zum.grant", ZdbSagaOp::Delete);

  Zum::SessionCleanupArgs sessions;
  sessions.updated = 147;
  sessions.sessions.push(Zum::SessionDelete{
    .digest = Zum::Bytes{ZuBSpan{"cleanup-session"}}, .version = 3});
  auto sessionSaga = Zum::sessionCleanup(ZuMv(sessions));
  ZuCheck(sessionSaga->nSteps() == 2);
  distinct(Zum::sessionCleanupDef(), ZuMv(sessionSaga),
    Zum::sessionCleanup(Zum::SessionCleanupArgs{}));
  phase(Zum::sessionCleanupDef(), 0, "zum.session", ZdbSagaOp::Delete);
  // The last phase terminates the repeat and is not reversed by recovery.
  phase(Zum::sessionCleanupDef(), 1, "zum.session", ZdbSagaOp::Update);

  Zum::ConsentCodeArgs code;
  code.beforeGrant.id = Zum::Bytes{ZuBSpan{"consent-code"}};
  code.afterGrant = code.beforeGrant;
  code.afterGrant.kind = Zum::GrantKind::Code;
  code.roleIDs.push(7);
  code.now = 148;
  distinct(Zum::consentCodeDef(), Zum::consentCode(ZuMv(code)),
    Zum::consentCode(Zum::ConsentCodeArgs{}));
  phase(Zum::consentCodeDef(), 4, "zum.consent", ZdbSagaOp::Update);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(session);
  return 0;
}
