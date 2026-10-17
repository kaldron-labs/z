//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTest.hh>
#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuMatcher.hh>

#include <string.h>

#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZfCf.hh>

#include "../../zdb/test/ZdbTestCf.hh"
#include <zlib/ZiLog.hh>

#include <zlib/ZvMxParams.hh>


#include <zlib/zumd_admin.hh>
#include <zlib/zumd_authorize.hh>
#include <zlib/zumd_db.hh>
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

using namespace zumtest;
using TestDB = zumtest::RuntimeDB;

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
Zum::StringVec zumtest::logRows;

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

void zumtest::prepareIdentity(ZmRef<RuntimeDB> db,
    ZmRef<Zum::DB> context, Ztls::Random &rng, Zum::Bytes &passkeyHandle,
    ZmRef<Ztls::PK::SK_EC> &passkey)
{
  ZuTestScope(prepareIdentity);
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
    .authVersion = 10, .audience = "orders"}));
  ZuCheck(insertRecord(context->apps, Zum::App{
    .id = 20, .name = "independent-app", .state = Zum::State::Active,
    .authVersion = 17, .audience = "https://independent-app.example"}));
  ZuCheck(insertRecord(context->apps, Zum::App{
    .id = 21, .name = "disabled-app", .state = Zum::State::Disabled,
    .authVersion = 10, .audience = "https://disabled-app.example"}));
  ZuCheck(insertRecord(context->apps, Zum::App{
    .id = 22, .name = "owned-app", .state = Zum::State::Active,
    .authVersion = 10, .owner = 1, .audience = "https://owned-app.example"}));
  ZuCheck(insertRecord(context->apps, Zum::App{
    .id = 24, .name = "other-app", .state = Zum::State::Active,
    .authVersion = 10, .audience = "https://other-app.example"}));
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
  Zum::Grant collisionCeremony{.id = Zum::Bytes{ZuBSpan{"collision-ceremony"}},
    .created = 100, .expires = 200, .kind = Zum::GrantKind::Ceremony,
    .purpose = Zum::GrantPurpose::Enrollment, .state = Zum::State::Active,
    .issuer = "issuer"};
  ZuCheck(insertRecord(context->grants, collisionCeremony));
  ZuCheck(!runSaga(db, Zum::enrollment(Zum::EnrollmentArgs{.ceremonyID = collisionCeremony.id,
    .userID = 41, .name = "duplicate code user", .created = 101,
    .beforeGrant = collisionCeremony}), ZdbSagaID{12010}));
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
  ZuCheck(!runSaga(db, Zum::enrollment(Zum::EnrollmentArgs{.ceremonyID = collisionCeremony.id,
    .userID = invitedBefore.id, .name = invitedBefore.name,
    .handle = Zum::Bytes{ZuBSpan{"rollback invite handle"}},
    .credentialID = duplicateCred, .created = 101, .precreated = true,
    .beforeGrant = collisionCeremony, .beforeUser = invitedBefore}), ZdbSagaID{12011}));
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
  Zum::CredentialAddArgs duplicateAdd;
  duplicateAdd.ceremonyID = collisionCeremony.id;
  duplicateAdd.issuer = "issuer";
  duplicateAdd.userID = 41;
  duplicateAdd.userHandle = Zum::Bytes{ZuBSpan{"code handle"}};
  duplicateAdd.credentialID = duplicateCred;
  duplicateAdd.created = 101;
  duplicateAdd.beforeGrant = collisionCeremony;
  ZuCheck(!runSaga(db, Zum::credentialAdd(ZuMv(duplicateAdd)), ZdbSagaID{12012}));
  restoredCeremony = loadGrant(context, collisionCeremony.id);
  ZuCheck(restoredCeremony.state == Zum::State::Active && !restoredCeremony.owner);
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

  Zum::CodeFamilyArgs family;
  Zum::String refreshToken;
  ZuCheck(prepareCodeFamily(
    context, rng, authorizationCode, family, refreshToken));
  auto collisionFamily = family;
  collisionFamily.familyID = Zum::Bytes{ZuBSpan{"family-collision"}};
  ZuCheck(insertRecord(context->refresh, Zum::Refresh{.id = collisionFamily.familyID,
    .expires = 1000, .state = Zum::State::Active}));
  ZuCheck(!runSaga(db, Zum::codeFamily(ZuMv(collisionFamily)), ZdbSagaID{12013}));
  auto rolledBackCode = loadGrant(context, family.codeID);
  ZuCheck(rolledBackCode.state == Zum::State::Active && !rolledBackCode.owner &&
    rolledBackCode.digest == family.codeDigest && rolledBackCode.scope == family.beforeGrant.scope);
  ZuCheck(loadRefresh(context, ZuBSpan{"family-collision"}).state == Zum::State::Active);
  Zum::Bytes familyID = family.familyID;
  Zum::Bytes refreshID, refreshDigest;
  ZuCheck(Zum::opaqueParse(refreshToken, refreshID, refreshDigest) &&
    refreshID == family.familyID && refreshDigest == family.digest);
  ZuCheck(runSaga(db, Zum::codeFamily(ZuMv(family)), ZdbSagaID{2}));
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
  passkeyHandle = enrollmentGrant.userHandle;
  passkey = new Ztls::PK::SK_EC{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  uint8_t passkeyPublic[Ztls::COSE::ES256::PublicKeySize];
  ZuCheck(Ztls::Backend::pkey_ec_export_public(passkey->key, passkeyPublic));
  Zum::RegistrationInput registrationInput;
  ZuCheck(makeRegistration(passkeyPublic, 5, "credential",
    enrollmentGrant.challenge,
    "https://example.com", "example.com", registrationInput));
  Zum::Bytes enrollmentID = enrollmentBegin.ceremonyID;
  ZuCheck(ZmBlock<int>{}([
    &db, &enrollmentBegin, &registrationInput
  ](auto wake) mutable {
    Zum::enrollmentFinish(db->requests, Zm::now() + ZuTime{10},
      db, ZuMv(enrollmentBegin.ceremonyID),
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
  ZuCheck(runSaga(db, Zum::userInvite(Zum::UserInviteArgs{
    .before = Zum::User{.id = 45, .version = 0},
    .values = Zum::User{.id = 45, .name = "invited"},
    .grant = Zum::Grant{.id = ZuMv(inviteToken.id), .userID = 45,
      .created = 123, .expires = 300, .kind = Zum::GrantKind::Capability,
      .purpose = Zum::GrantPurpose::Enrollment, .state = Zum::State::Active,
      .issuer = "issuer", .digest = ZuMv(inviteToken.digest), .userName = "invited",
      .label = "invited passkey", .actor = "precreated"}, .updated = 123,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::userInvite, .idempotence = "invite-runtime"},
    .external = inviteExternal}),
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
  ZuCheck(!runSaga(db, Zum::userInvite(Zum::UserInviteArgs{
    .before = Zum::User{.id = 18003, .version = 0},
    .values = Zum::User{.id = 18003, .name = "stale-invited"},
    .grant = Zum::Grant{.id = ZuMv(staleToken.id), .userID = 18003,
      .created = 123, .expires = 300, .kind = Zum::GrantKind::Capability,
      .purpose = Zum::GrantPurpose::Enrollment, .state = Zum::State::Active,
      .issuer = "issuer", .digest = ZuMv(staleToken.digest), .userName = "stale-invited",
      .actor = "precreated"}, .updated = 123,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::userInvite, .idempotence = "invite-stale-external"},
    .external = staleExternal}), ZdbSagaID{18003}));
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
  ZuCheck(!runSaga(db, Zum::userInvite(Zum::UserInviteArgs{
    .before = Zum::User{.id = 12001, .version = 0},
    .values = Zum::User{.id = 12001, .name = "failed-invitation"},
    .grant = Zum::Grant{.id = inviteID, .userID = 12001,
      .created = 123, .expires = 300, .kind = Zum::GrantKind::Capability,
      .purpose = Zum::GrantPurpose::Enrollment, .state = Zum::State::Active,
      .issuer = "issuer", .digest = inviteDigest, .userName = "failed-invitation",
      .label = "invited passkey", .actor = "precreated"}, .updated = 123,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::userInvite, .idempotence = "invite-rollback"}}),
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
    &db, &addBegin, &registrationInput
  ](auto wake) mutable {
    Zum::credentialFinish(db->requests, Zm::now() + ZuTime{10},
      db, ZuMv(addBegin.ceremonyID),
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
  ZuCheck(makeAssertion(rng, *passkey, 6, passkeyInput));
  passkeyInput.userHandle = passkeyHandle;
  auto authenticated = storedAssertion(context, ZuMv(passkeyInput));
  ZuCheck(authenticated.error == Zum::WebAuthnError::OK &&
    authenticated.user.id == 42 && authenticated.grant.id ==
      ZuBSpan{"passkey-auth-id0"} &&
    authenticated.result.counter == Zum::CounterState::Advanced &&
    authenticated.result.signCount == 6 &&
    credentialCount(context, ZuBSpan{"credential"}, 6));

  ZuCheck(makeAssertion(rng, *passkey, 6, passkeyInput));
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
  ZuCheck(!runSaga(db, Zum::codeFamily(ZuMv(family)), ZdbSagaID{3}));
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
  ZuCheck(!runSaga(db, Zum::codeFamily(ZuMv(family)), ZdbSagaID{4}));
  ZuCheck(codeFamilyState(
    context, ZuBSpan{"wrong code"}, ZuBSpan{"wrong family"}, false));
}
