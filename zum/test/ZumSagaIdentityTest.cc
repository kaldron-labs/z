//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "ZumSagaTestSupport.hh"
#include <zlib/ZumMgmt.hh>
#include <zlib/zumd_saga_enrollment.hh>
#include <zlib/zumd_saga_credential_add.hh>
#include <zlib/zumd_saga_recovery_start.hh>
#include <zlib/zumd_saga_recovery_enroll.hh>
#include <zlib/zumd_saga_code_family.hh>
#include <zlib/zumd_saga_app_enrollment.hh>
#include <zlib/zumd_saga_external_projection.hh>

static void identity()
{
  ZuTestScope(identity);
  roundTrip(Zum::enrollmentDef(), Zum::enrollment(Zum::EnrollmentArgs{}));
  roundTrip(Zum::credentialAddDef(), Zum::credentialAdd(Zum::CredentialAddArgs{}));
  roundTrip(Zum::recoveryStartDef(), Zum::recoveryStart(Zum::RecoveryStartArgs{}));
  roundTrip(Zum::recoveryEnrollDef(), Zum::recoveryEnroll(Zum::RecoveryEnrollArgs{}));
  roundTrip(Zum::codeFamilyDef(), Zum::codeFamily(Zum::CodeFamilyArgs{}));
  roundTrip(Zum::appEnrollmentDef(), Zum::appEnrollment(Zum::AppEnrollmentArgs{}));
  roundTrip(Zum::externalProjectionDef(), Zum::externalProjection(Zum::ExternalProjectionArgs{}));

  Zum::EnrollmentArgs enrollment;
  enrollment.ceremonyID = Zum::Bytes{ZuBSpan{"ceremony"}};
  enrollment.userID = 42;
  enrollment.name = "first user";
  enrollment.publicKey = Zum::Bytes{ZuBSpan{"cose-key"}};
  enrollment.beforeGrant.id = enrollment.ceremonyID;
  distinct(Zum::enrollmentDef(), Zum::enrollment(ZuMv(enrollment)),
    Zum::enrollment(Zum::EnrollmentArgs{}));
  phase(Zum::enrollmentDef(), 0, "zum.grant", ZdbSagaOp::Update);
  phase(Zum::enrollmentDef(), 8, "zum.grant", ZdbSagaOp::Delete);

  Zum::CodeFamilyArgs family;
  family.codeID = Zum::Bytes{ZuBSpan{"code"}};
  family.userID = 42;
  family.authVersion = 7;
  family.actions.length(8);
  family.actions.set(2);
  family.beforeGrant.id = family.codeID;
  distinct(Zum::codeFamilyDef(), Zum::codeFamily(ZuMv(family)),
    Zum::codeFamily(Zum::CodeFamilyArgs{}));
  phase(Zum::codeFamilyDef(), 2, "zum.refresh", ZdbSagaOp::Update);

  Zum::AppEnrollmentArgs app{
    .appID = 9, .appName = "orders", .appLabel = "Orders",
    .audience = "https://orders.example",
    .signKey = Zum::SignKey{.id = "app_9_1", .issuer = "auth"},
    .clientID = "svc_orders",
    .secretDigest = Zum::Bytes{ZuBSpan{"verifier"}},
    .created = 123, .catalogPublishOp = Zum::MgmtOp::catalogPublish,
    .operationQueryOp = Zum::MgmtOp::operationQuery};
  distinct(Zum::appEnrollmentDef(), Zum::appEnrollment(ZuMv(app)),
    Zum::appEnrollment(Zum::AppEnrollmentArgs{}));
  ZuCheck(Zum::appEnrollmentDef().nPhases == 12);
  phase(Zum::appEnrollmentDef(), 0, "zum.request", ZdbSagaOp::Insert);
  phase(Zum::appEnrollmentDef(), 1, "zum.app", ZdbSagaOp::Insert);
  phase(Zum::appEnrollmentDef(), 2, "zum.sign_key", ZdbSagaOp::Insert);
  phase(Zum::appEnrollmentDef(), 3, "zum.client", ZdbSagaOp::Insert);
  phase(Zum::appEnrollmentDef(), 4, "zum.admin_access", ZdbSagaOp::Insert);
  phase(Zum::appEnrollmentDef(), 5, "zum.auth_policy", ZdbSagaOp::Insert);
  phase(Zum::appEnrollmentDef(), 6, "zum.sign_key", ZdbSagaOp::Update);
  phase(Zum::appEnrollmentDef(), 7, "zum.client", ZdbSagaOp::Update);
  phase(Zum::appEnrollmentDef(), 8, "zum.admin_access", ZdbSagaOp::Update);
  phase(Zum::appEnrollmentDef(), 9, "zum.auth_policy", ZdbSagaOp::Update);
  phase(Zum::appEnrollmentDef(), 10, "zum.app", ZdbSagaOp::Update);
  phase(Zum::appEnrollmentDef(), 11, "zum.request", ZdbSagaOp::Update);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(identity);
  return 0;
}
