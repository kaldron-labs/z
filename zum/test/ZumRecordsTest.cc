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
static void records()
{
  ZuTestScope(records);
  Zum::Role role;
  role.id = 42;
  role.name = "operator";
  role.actions.length(128);
  role.actions.set(7);

  ZiFB::IOBuilder<ZiFB::BufferAlign<Zum::Role>::N> fbb{new ZiIOBufAlloc<>()};
  fbb.finish(ZiFB::save(fbb, role));
  auto buf = fbb.buf();
  ZuBSpan bytes{buf->data(), buf->length};
  ZuCheck(ZiFB::verify<Zum::Role>(bytes));
  auto loaded = ZiFB::load<Zum::Role>(bytes);
  ZuCheck(loaded.id == role.id);
  ZuCheck(loaded.name == role.name);
  ZuCheck(loaded.actions[7]);
  ZuCheck(loaded.state == Zum::State::Active);
}

template <typename T>
static T roundTrip(const T &value)
{
  ZiFB::IOBuilder<ZiFB::BufferAlign<T>::N> fbb{new ZiIOBufAlloc<>()};
  fbb.finish(ZiFB::save(fbb, value));
  auto buf = fbb.buf();
  return ZiFB::load<T>({buf->data(), buf->length});
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
    .created = 10, .updated = 11, .audience = "https://issuer/admin"});
  ZuCheck(app.id == 1 && app.name == "zum" && app.nextActionID == 70 &&
    app.catalogRevision == 3 && app.catalogDigest == ZuBSpan{"digest"} &&
    app.audience == "https://issuer/admin");

  Zum::Assignment assignment{
    .appID = 1, .userID = 2, .state = Zum::State::Active,
    .authVersion = 4};
  assignment.roleIDs.push(3);
  assignment = roundTrip(assignment);
  ZuCheck(assignment.appID == 1 && assignment.userID == 2 &&
    assignment.roleIDs.length() == 1 && assignment.roleIDs[0] == 3);

  Zum::ClientAccess clientAccess{
    .clientID = "service", .appID = 1, .state = Zum::State::Active,
    .authVersion = 5};
  clientAccess.roleIDs.push(6);
  clientAccess.roleIDs.push(7);
  clientAccess = roundTrip(clientAccess);
  ZuCheck(clientAccess.clientID == "service" &&
    clientAccess.roleIDs[0] == 6 &&
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
    .userID = 2, .clientID = "native", .appID = 1,
    .state = Zum::State::Active};
  consent.roleIDs.push(6);
  consent = roundTrip(consent);
  ZuCheck(consent.clientID == "native" && consent.roleIDs[0] == 6);

  Zum::IdemRequest request{
    .actorKind = Zum::ActorKind::User, .actorID = "2",
    .operation = Zum::MgmtOp::appEnroll, .idempotence = "once",
    .requestDigest = Zum::Bytes{ZuBSpan{"request"}}, .sagaID = 12,
    .status = Zum::RequestStatus::Complete, .expires = 500};
  request.resultIDs.push("1");
  request = roundTrip(request);
  ZuCheck(request.actorID == "2" &&
    request.operation == Zum::MgmtOp::appEnroll && request.sagaID == 12 &&
    request.resultIDs[0] == "1");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(loginNames);
  ZuTestCall(records);
  ZuTestCall(appRecords);
  return 0;
}
