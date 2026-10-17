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

void zumtest::prepareOIDC(ZmRef<RuntimeDB> db,
    ZmRef<Zum::DB> context, Ztls::Random &rng, OIDCSetup &state)
{
  ZuTestScope(prepareOIDC);
  Zum::ActionID readAction = 0;
  Zum::Client browser;
  browser.id = "browser";
  browser.appID = 9;
  browser.redirects.push("https://app/cb");
  browser.identityScopes.push("openid");
  browser.identityScopes.push("profile");
  browser.identityScopes.push("email");
  browser.profile = Zum::ClientProfile::Browser;
  browser.grants = Zum::ClientGrant::AuthCode() |
    Zum::ClientGrant::Refresh();
  browser.refreshAllowed = true;
  browser.state = Zum::State::Active;
  ZuCheck(insertRecord(context->clients, Zum::Client{browser}));

  ZuCheck(insertRecord(context->apps, Zum::App{
    .id = 9, .name = "oidc-app", .state = Zum::State::Active,
    .nextActionID = 1, .authVersion = 12, .audience = "orders"}));
  ZuCheck(insertRecord(context->actions, Zum::Action{
    .appID = 9, .id = readAction, .name = "orders.read",
    .state = Zum::State::Active}));
  ZtBitmap oidcRoleActions{1U};
  oidcRoleActions.set(readAction);
  ZuCheck(insertRecord(context->roles, Zum::Role{
    .appID = 9, .id = 7, .name = "read",
    .actions = ZuMv(oidcRoleActions), .state = Zum::State::Active}));
  Zum::IDVec appRoles;
  appRoles.push(7);
  ZuCheck(insertRecord(context->assignments, Zum::Assignment{
    .appID = 9, .userID = 42, .roleIDs = Zum::IDVec{appRoles},
    .state = Zum::State::Active}));
  Zum::Client oidcBrowser{browser};
  oidcBrowser.id = "oidc-browser";
  oidcBrowser.appID = 9;
  ZuCheck(insertRecord(context->clients, Zum::Client{oidcBrowser}));
  for (unsigned i = 0; i < 2; ++i)
    ZuCheck(insertRecord(context->clientAccess, Zum::ClientAccess{
      .clientID = i ? "oidc-browser" : "browser", .appID = 9, .roleIDs = Zum::IDVec{7},
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
  Zum::String multiJWKS{"{\"keys\":["};
  for (unsigned i = 0; i < 7; ++i)
    multiJWKS << "{\"kid\":\"unused-" << i << "\",\"kty\":\"EC\","
      "\"crv\":\"P-256\",\"x\":\"" << x << "\",\"y\":\"" << y << "\"},";
  multiJWKS << "{\"kid\":\"upstream\",\"kty\":\"EC\",\"crv\":\"P-256\","
    "\"x\":\"" << x << "\",\"y\":\"" << y << "\"}]}";
  upstreamJWKS = ZuMv(multiJWKS);
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
  Zum::String savedJWKS = ZuMv(upstreamJWKS);
  Zum::String jwk{"{\"kid\":\"upstream\",\"kty\":\"EC\",\"crv\":\"P-256\",\"x\":\""};
  jwk << x << "\",\"y\":\"" << y << "\"}";
  upstreamJWKS = Zum::String{"{\"keys\":[{\"kid\":\"unused\",\"kty\":\"EC\","
    "\"crv\":\"P-256\",\"x\":\""} << x << "\",\"y\":\"" << y << "\"}," <<
    jwk << "]}";
  Zum::String twoLocation;
  ZuCheck(ZmBlock<bool>{}([&oidc, &oidcConfig, &twoLocation](auto wake) mutable {
    if (!oidc.begin(Zum::Bytes{ZuBSpan{"oidc-two-key-id"}}, oidcConfig,
      [&twoLocation, wake = ZuMv(wake)](bool ok, Zum::String location) mutable {
        twoLocation = ZuMv(location);
        wake(ok);
      })) wake(false);
  }));
  Zum::String twoState, twoNonce;
  ZuCheck(oidcParams(twoLocation, twoState, twoNonce));
  Zum::String twoClaims{
    "{\"iss\":\"https://upstream.example/oauth2/default\","
    "\"sub\":\"00u44\",\"aud\":\"zum\",\"nonce\":\""};
  twoClaims << twoNonce <<
    "\",\"iat\":195,\"auth_time\":191,\"exp\":260,\"groups\":[\"Zum-Users\"]}";
  ZuCheck(signJWT(rng, upstreamKey,
    "{\"alg\":\"ES256\",\"kid\":\"upstream\",\"typ\":\"JWT\"}",
    twoClaims, upstreamToken));
  Zum::String twoCallback{"code=two-key-code&state="};
  twoCallback << twoState;
  ZuCheck(ZmBlock<bool>{}([&oidc, callback = ZuMv(twoCallback)](
      auto wake) mutable {
    if (!oidc.finish(9, ZuMv(callback), [wake = ZuMv(wake)](bool ok,
        Zum::Bytes, Zum::User, Zum::IDVec, Zum::Evidence, int64_t) mutable {
      wake(ok);
    })) wake(false);
  }));
  ZuCheck(tokenReqs == 3 && jwksReqs == 3 && userinfoReqs == 1);
  upstreamJWKS = ZuMv(savedJWKS);
  // A formerly trusted key removed by the upstream must not remain usable.
  savedJWKS = ZuMv(upstreamJWKS);
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
  ZuCheck(tokenReqs == 6 && jwksReqs == 6 && userinfoReqs == 1);
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
  constexpr auto oidcCodePrefix = "https://app/cb?code="_z;
  constexpr auto oidcCodeSuffix = "&state=oidc-return"_z;
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
  ZuCheck(providerServer.init(db, db->requests,
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
  constexpr auto consentMarker = "name=id value=\""_z;
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
  constexpr auto csrfMarker = "name=csrf value=\""_z;
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
  state.browser = ZuMv(browser);
  state.appRoles = ZuMv(appRoles);
  state.code = ZuMv(oidcCode);
}
