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
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/zumd_db_ops.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_saga_enrollment.hh>
#include <zlib/zumd_saga_credential_add.hh>
#include <zlib/zumd_saga_recovery_start.hh>
#include <zlib/zumd_saga_recovery_enroll.hh>
#include <zlib/zumd_saga_code_family.hh>
#include <zlib/zumd_saga_revoke.hh>
#include <zlib/zumd_saga_grant_cleanup.hh>
#include <zlib/zumd_saga_ssf_delivery_add.hh>
#include <zlib/zumd_saga_consent_code.hh>
#include <zlib/zumd_saga_app_action_add.hh>
#include <zlib/zumd_saga_assignment_add.hh>
#include <zlib/zumd_saga_assignment_change.hh>
#include <zlib/zumd_saga_admin_access_put.hh>
#include <zlib/zumd_saga_app_change.hh>
#include <zlib/zumd_saga_user_edit.hh>
#include <zlib/zumd_saga_cred_edit.hh>
#include <zlib/zumd_saga_user_invite.hh>
#include <zlib/zumd_saga_key_add.hh>
#include <zlib/zumd_saga_key_retire.hh>
#include <zlib/zumd_saga_catalog_publish.hh>
#include <zlib/zumd_saga_role_add.hh>
#include <zlib/zumd_saga_role_edit.hh>
#include <zlib/zumd_saga_action_edit.hh>
#include <zlib/zumd_saga_catalog_publish.hh>
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
using TestDB = RuntimeDB;

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
    Zum::DB *context, Zum::Grant grant)
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

static int missingAssertion(Zum::DB *context)
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
    Zum::DB *context, Zum::AssertionInput input)
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
    Zum::DB *context, ZuBSpan credentialID, uint32_t signCount)
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
    Zum::DB *context, Ztls::Random &rng, Zum::Bytes id,
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
    Zum::DB *context, ZuBSpan id, ZuCSpan code,
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
    Zum::DB *context, Ztls::Random &rng, ZuCSpan code,
    Zum::CodeFamilyArgs &family, Zum::String &refreshToken)
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
    Zum::DB *context, Ztls::Random &rng, ZuCSpan refreshToken,
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
    Zum::DB *context, ZuBSpan familyID, ZuBSpan digest,
    unsigned generation, Zum::State::T state)
{
  return ZmBlock<bool>{}([
    context, familyID, digest, generation, state
  ](auto wake) mutable {
    context->refresh->run(0, [
      context, familyID, digest, generation, state, wake = ZuMv(wake)
    ]() mutable {
      context->refresh->find<0>(0, ZuFwdTuple(familyID), [
	digest, generation, state, wake = ZuMv(wake)
	](ZdbRowRef<Zum::Refresh> row) mutable {
	wake(row && row->data().digest == digest &&
	  row->data().generation == generation &&
	  row->data().state == state && row->data().requestedRoleIDs.length() == 1 &&
	  row->data().requestedRoleIDs[0] == 7 && row->data().actions[3]);
      });
    });
  });
}

static bool insertIssuer(
    Zum::DB *context, ZuCSpan id)
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
    Zum::DB *context, Zum::Grant grant, Zum::Client client,
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
    Zum::DB *context, Zum::Client client)
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
    Zum::Requests *requests, Zum::DB *context, Ztls::Random &rng,
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
    Zum::Requests *requests, Zum::DB *db,
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
    requests, db, &rng, ceremonyID = ZuMv(ceremonyID),
    input = ZuMv(input), config = ZuMv(config), policy = ZuMv(policy),
    approveConsent
  ](auto wake) mutable {
    Zum::Bytes consentID = ceremonyID;
    Zum::Bytes consentBinding{ZuBSpan{"browser binding"}};
    auto consentConfig = config;
    auto consentPolicy = policy;
    if (!Zum::authorizeFinish(requests, Zm::now() + ZuTime{10},
      db, rng, ZuMv(ceremonyID),
      Zum::Bytes{ZuBSpan{"browser binding"}}, ZuMv(input), ZuMv(config),
      ZuMv(policy), [requests, db, &rng, wake = ZuMv(wake),
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
      db, rng, ZuMv(consentID), ZuMv(consentBinding), true,
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
    Zum::Requests *requests, Zum::DB *context, Ztls::Random &rng,
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
    Zum::Requests *requests, TestDB *db,
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
    requests, db, &rng, form = ZuMv(form),
    authorization = ZuMv(authorization), config = ZuMv(config),
    sign = ZuMv(sign)
  ](auto wake) mutable {
    if (!Zum::tokenRequest(requests, Zm::now() + ZuTime{10},
      db, rng, ZuMv(form), ZuMv(authorization),
      ZuMv(config), ZuMv(sign), [wake](
	  int error, Zum::TokenResponse response) mutable {
	wake(IssuedToken{error, ZuMv(response)});
      }))
      wake(IssuedToken{});
  });
}

static Zum::Grant loadGrant(Zum::DB *context, ZuBSpan id)
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

static Zum::Refresh loadRefresh(Zum::DB *context, ZuBSpan id)
{
  return ZmBlock<Zum::Refresh>{}([context, id](auto wake) mutable {
    context->refresh->run(0, [context, id, wake = ZuMv(wake)]() mutable {
      context->refresh->find<0>(0, ZuFwdTuple(id), [
	wake = ZuMv(wake)
      ](ZdbRowRef<Zum::Refresh> row) mutable {
	wake(row ? Zum::Refresh{row->data()} : Zum::Refresh{});
      });
    });
  });
}

static Zum::IdemRequest loadRequest(
    Zum::DB *context, Zum::ActorKind::T actorKind,
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

static Zum::User loadUser(Zum::DB *context, Zum::UserID id)
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

static Zum::Role loadRole(Zum::DB *context, Zum::RoleID id)
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

static Zum::Client loadClient(Zum::DB *context, ZuCSpan id)
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
    Zum::DB *context, Zum::ActionID id, Zum::AppID appID = 0)
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

static Zum::Cred loadCred(Zum::DB *context, ZuBSpan id)
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

static bool insertCode(Zum::DB *context, ZuBSpan id)
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

static bool runSaga(TestDB *db, Zum::SagaRef saga, ZdbSagaID id)
{
  ZmSemaphore completed;
  bool terminal = false;
  bool submitted = ZmBlock<bool>{}([
      db, id, saga = ZuMv(saga), &completed, &terminal
  ](auto wake) mutable {
    if (!Zum::sagaSubmit(db, id, ZuMv(saga),
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
    Zum::DB *context, ZuBSpan codeID, ZuBSpan familyID, bool complete)
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
        context->refresh->find<0>(0, ZuFwdTuple(familyID), [
	  complete, ok, wake = ZuMv(wake)
      ](ZdbRowRef<Zum::Refresh> family) mutable {
	  wake(ok && (complete ?
	    family && family->data().state == Zum::State::Active &&
	      !family->data().owner : !family));
	});
      });
    });
  });
}

static bool releaseToken(
    Zum::DB *context, ZuBSpan familyID, uint64_t authVersion,
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
    Zum::DB *context, ZuBSpan ceremonyID, Zum::UserID userID,
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
    Zum::DB *context, ZuCSpan id, int64_t retireAfter)
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

static void runtimeRecovery()
{
  ZuTestScope(runtimeRecovery);
  ZiLog::init("ZumRuntimeRecoveryTest");
  ZiLog::level(0);
  logRows.null();
  ZiLog::sink(ZiLog::lambdaSink([](
      ZeLogBuf &buf, const ZeEventInfo &info) {
    if (info.component == "Zum")
      logRows.push(Zum::String{" "} << ZuCSpan{buf.data(), buf.length()} << " ");
  }));
  ZiLog::start();
  auto config = dbConfig();
  auto configFields = ZfCf::handler<zdbtest::ConfigFields>(config).ctor();
  ZiMultiplex mx{ZvMxParams("mx", configFields.mx.p<const ZfCf::AnyNode *>())};
  ZuCheck(mx.start());
  ZmRef<ZdbMem::Store> store = new ZdbMem::Store{};
  ZmRef<TestDB> db = new TestDB{};
  db->requests = new Zum::Requests{};
  ZuCheck(db->requests->init(&mx, 5, 64));
  db->init(ZdbCf{configFields.zdb.p<const ZfCf::AnyNode *>()}, &mx,
    ZdbHandler{.upFn = dbUp, .downFn = dbDown}, store);
  Zum::registerSchema(db);
  ZmRef<Zum::DB> context = db;
  ZuCheck(db->start());
  db->active.wait();
  Ztls::Random rng;
  ZuCheck(rng.init());
  Zum::Bytes passkeyHandle;
  ZmRef<Ztls::PK::SK_EC> passkey;
  prepareIdentity(db, context, rng, passkeyHandle, passkey);
  Zum::RegistrationInput registrationInput;

  OIDCSetup oidcSetup;
  prepareOIDC(db, context, rng, oidcSetup);
  Ztls::PK::SK_EC tokenKey{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  uint8_t tokenPublic[Ztls::COSE::ES256::PublicKeySize];
  TokenSetup tokenSetup;
  prepareToken(db, context, rng, oidcSetup, passkeyHandle,
    *passkey, tokenKey, tokenPublic, tokenSetup);
  Zum::ActionID readAction = 0;
  Zum::IDVec appRoles{ZuMv(oidcSetup.appRoles)};
  Zum::Bytes codeFamilyID{ZuMv(tokenSetup.codeFamilyID)};
  Zum::String browserCode{ZuMv(tokenSetup.browserCode)};

  Zum::String recoveryCapability;
  auto beforeRecovery = loadUser(context, 42);
  ZuCheck(insertRecord(context->refresh, Zum::Refresh{
    .id = Zum::Bytes{ZuBSpan{"recovery-collision"}}, .userID = 45,
    .created = 349, .expires = 410, .state = Zum::State::Active, .issuer = "issuer"}));
  ZuCheck(!runSaga(db, Zum::recoveryStart(Zum::RecoveryStartArgs{
    .capabilityID = Zum::Bytes{ZuBSpan{"recovery-collision"}},
    .digest = Zum::Bytes{ZuBSpan{"digest"}}, .issuer = "issuer", .userID = 42,
    .userVersion = beforeRecovery.authVersion + 1, .created = 350, .expires = 410,
    .actor = "administrator", .version = beforeRecovery.version,
    .oldState = beforeRecovery.state, .oldUpdated = beforeRecovery.updated,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::userRecover, .idempotence = "recovery-rollback"}}),
    ZdbSagaID{12002}));
  auto afterRecovery = loadUser(context, 42);
  ZuCheck(afterRecovery.state == beforeRecovery.state &&
    afterRecovery.version == beforeRecovery.version &&
    afterRecovery.authVersion == beforeRecovery.authVersion &&
    afterRecovery.updated == beforeRecovery.updated && !afterRecovery.owner);
  ZuCheck(loadRefresh(context, ZuBSpan{"recovery-collision"}).userID == 45);
  auto recoveryVersion = loadUser(context, 42).version;
  ZuCheck(!runSaga(db, Zum::recoveryStart(Zum::RecoveryStartArgs{
    .capabilityID = Zum::Bytes{ZuBSpan{"stale-recovery"}},
    .digest = Zum::Bytes{ZuBSpan{"digest"}},
    .issuer = "issuer", .userID = 42, .userVersion = beforeRecovery.authVersion + 1,
    .created = 350, .expires = 410, .actor = "administrator",
    .version = recoveryVersion + 1, .oldState = beforeRecovery.state,
    .oldUpdated = beforeRecovery.updated}), ZdbSagaID{9007}));
  ZuCheck(ZmBlock<bool>{}([
    &db, &context, &rng, recoveryVersion
  ](auto wake) mutable {
    Zum::recoveryIssue(db->requests, Zm::now() + ZuTime{10},
      db, rng, Zum::RecoveryIssueConfig{
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
      db, rng, Zum::RecoveryIssueConfig{
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

  Zum::SecretString recoveryCapabilityLog{recoveryCapability};
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
  Zum::RecoveryEnrollArgs rollbackRecovery{
    .ceremonyID = recoveryGrant.id, .issuer = recoveryGrant.issuer,
    .actor = recoveryGrant.actor, .userID = 42, .userVersion = 2,
    .oldHandle = recoveredUser.handle, .newHandle = recoveryHandle,
    .credentialID = Zum::Bytes{ZuBSpan{"credential"}}, .created = 352,
    .beforeGrant = recoveryGrant, .beforeUser = recoveredUser};
  ZuCheck(!runSaga(db, Zum::recoveryEnroll(rollbackRecovery), ZdbSagaID{12014}));
  ZuCheck(Zum::SagaImage::save(loadCred(context, existingRecoveryCred.id)) ==
    Zum::SagaImage::save(existingRecoveryCred));
  ZuCheck(loadGrant(context, recoveryGrant.id).state == Zum::State::Active &&
    !loadGrant(context, recoveryGrant.id).owner);
  rollbackRecovery.credentialID = Zum::Bytes{ZuBSpan{"stale-recovery-credential"}};
  --rollbackRecovery.beforeUser.version;
  ZuCheck(!runSaga(db, Zum::recoveryEnroll(ZuMv(rollbackRecovery)), ZdbSagaID{12015}));
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
      db, ZuMv(recoveryBegin.ceremonyID),
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
    Zum::enrollmentFinish(db->requests, Zm::now() + ZuTime{10}, db,
      ZuMv(browserEnrollmentBegin.ceremonyID),
      Zum::Bytes{ZuBSpan{"browser binding"}}, ZuMv(registrationInput),
      Zum::EnrollmentFinishConfig{
	.origin = "https://example.com",
	.rpID = "example.com",
	.credentialIDMax = 128,
	.now = 422
      }, [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::WebAuthnError::OK);
  ZuCheck(insertRecord(context->assignments, Zum::Assignment{
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
    db->requests, db, rng,
    ZuMv(enrollmentAuthorization.result.ceremonyID),
    ZuMv(enrollmentAssertion), readAction, 423);
  ZuCheck(enrollmentFinished.error == Zum::AuthorizeIssue::OK &&
    enrollmentFinished.location.find<"state=enroll">() >= 0);

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
  ZuCheck(recoveryCapabilityLog && !logged(recoveryCapabilityLog));
  recoveryCapabilityLog.null();
  ZuCheck(browserCode && !logged(browserCode));
  ZiLog::stop();
  ZiLog::sink(ZiLog::debugSink());
  logRows.null();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(runtimeRecovery);
  return 0;
}
