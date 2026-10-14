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

void zumtest::prepareToken(ZmRef<RuntimeDB> db,
    ZmRef<Zum::DB> context, Ztls::Random &rng, OIDCSetup &oidcSetup,
    const Zum::Bytes &passkeyHandle, Ztls::PK::SK_EC &passkey,
    Ztls::PK::SK_EC &tokenKey,
    uint8_t (&tokenPublic)[Ztls::COSE::ES256::PublicKeySize], TokenSetup &state)
{
  ZuTestScope(prepareToken);
  Zum::ActionID readAction = 0, writeAction = 1;
  Zum::Client browser{oidcSetup.browser};
  Zum::IDVec appRoles{oidcSetup.appRoles};
  Zum::String oidcCode{ZuMv(oidcSetup.code)};
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

  auto changeAssignment = [context](Zum::State::T state,
      uint128_t owner, bool roles) {
    return ZmBlock<bool>{}([context, state, owner, roles](auto wake) mutable {
      context->assignments->run(0, [context, state, owner, roles,
	  wake = ZuMv(wake)]() mutable {
	context->assignments->findUpd<0>(0,
	  ZuFwdTuple(Zum::AppID{9}, Zum::UserID{42}),
	  [state, owner, roles, wake = ZuMv(wake)](
	      ZdbRow<Zum::Assignment> *row) mutable {
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
  ZuCheck(changeAssignment(Zum::State::Suspended, 0, true));
  authority = loadInteractiveAuthority(
    context, Zum::Grant{authorityGrant}, Zum::Client{browser});
  ZuCheck(authority.error == Zum::AuthorityError::Invalid);
  ZuCheck(changeAssignment(Zum::State::Active, 1, true));
  authority = loadInteractiveAuthority(
    context, Zum::Grant{authorityGrant}, Zum::Client{browser});
  ZuCheck(authority.error == Zum::AuthorityError::Invalid);
  ZuCheck(changeAssignment(Zum::State::Active, 0, false));
  authority = loadInteractiveAuthority(
    context, Zum::Grant{authorityGrant}, Zum::Client{browser});
  ZuCheck(authority.error == Zum::ScopeError::OK &&
    !authority.data.actions[readAction]);
  ZuCheck(changeAssignment(Zum::State::Active, 0, true));
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
  workload.profile = Zum::ClientProfile::Server;
  workload.grants = Zum::ClientGrant::ClientCredentials();
  workload.state = Zum::State::Active;
  workload.secretDigest.length(Ztls::SecretHash::Size, false);
  ZuCheck(Ztls::secretHash(rng, ZuBSpan{"secret"},
    workload.secretDigest));
  ZuCheck(insertRecord(context->clients, Zum::Client{workload}));
  ZuCheck(insertRecord(context->clientAccess, Zum::ClientAccess{
    .clientID = "workload", .appID = 9,
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
    .nextActionID = 1, .authVersion = 4, .audience = "management"}));
  ZuCheck(insertRecord(context->apps, Zum::App{
    .id = 902, .name = "authority-owner", .state = Zum::State::Active,
    .audience = "https://authority-owner.example"}));
  ZuCheck(insertRecord(context->actions, Zum::Action{
    .appID = 900, .id = 0, .name = "Zum.operationQuery",
    .state = Zum::State::Active}));
  ZuCheck(insertRecord(context->roles, Zum::Role{
    .appID = 900, .id = 901, .name = "read",
    .actions = ZuMv(serviceActions), .state = Zum::State::Active}));
  Zum::Client service;
  service.id = "enrolled-service";
  service.appID = 902;
  service.profile = Zum::ClientProfile::Server;
  service.grants = Zum::ClientGrant::ClientCredentials();
  service.state = Zum::State::Active;
  ZuCheck(insertRecord(context->clients, Zum::Client{service}));
  Zum::ClientAccess serviceAccess{
    .clientID = "enrolled-service", .appID = 900,
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

  ZuCheck(Ztls::Backend::pkey_ec_export_public(tokenKey.key, tokenPublic));
  ZuCheck(setKeyRetirement(context, "app-token-key", 250));
  auto issued = issueTokenRequest(db->requests, db, rng,
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
  issued = issueTokenRequest(db->requests, db, rng,
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
  issued = issueTokenRequest(db->requests, db, rng,
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
    db->requests, db, rng, Zum::Bytes{authorized.result.ceremonyID},
    Zum::AssertionInput{requestAssertion}, readAction, 210,
    "issuer/oauth2/10", 9);
  ZuCheck(wrongFinishIssuer.error == Zum::OAuthError::AccessDenied &&
    !wrongFinishIssuer.location);
  auto wrongFinishApp = finishAuthorization(
    db->requests, db, rng, Zum::Bytes{authorized.result.ceremonyID},
    Zum::AssertionInput{requestAssertion}, readAction, 210,
    "issuer/oauth2/9", 10);
  ZuCheck(wrongFinishApp.error == Zum::OAuthError::AccessDenied &&
    !wrongFinishApp.location);
  auto finished = finishAuthorization(db->requests, db, rng,
    Zum::Bytes{authorized.result.ceremonyID}, ZuMv(requestAssertion),
    readAction, 210, "issuer/oauth2/9", 9, true);
  constexpr auto codePrefix = "https://app/cb?code="_z;
  constexpr auto codeSuffix = "&state=return"_z;
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
  auto rejected = finishAuthorization(db->requests, db, rng,
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
  state.browserCode = Zum::String{ZuCSpan{
    finished.location.data() + codePrefix.length(),
    finished.location.length() - codePrefix.length() - codeSuffix.length()}};
  auto &browserCode = state.browserCode;
  Zum::Bytes codeID, codeDigest;
  ZuCheck(Zum::opaqueParse(browserCode, codeID, codeDigest));
  Zum::String codeForm{"grant_type=authorization_code&code="};
  codeForm << browserCode <<
    "&client_id=browser&redirect_uri=https%3A%2F%2Fapp%2Fcb" <<
    "&code_verifier=" <<
    "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
  issued = issueTokenRequest(
    db->requests, db, rng, ZuMv(codeForm), {}, tokenKey, 250,
    "issuer/oauth2/9");
  ZuCheck(issued.error == Zum::TokenIssue::OK &&
    issued.response.refreshToken &&
    issued.response.scope == "read offline_access" &&
    issued.response.expiresIn == 60 && Zum::jwtVerify(
      issued.response.accessToken, "app-token-key", "issuer/oauth2/9", "orders",
      tokenPublic, 270, Zum::JWTLimits{}, principal));
  ZuCheck(principal.authMethod == "passkey" && principal.authTime == 210 &&
    principal.actions.length() == 1 && principal.actions[0] == "orders.read");
  auto &codeFamilyID = state.codeFamilyID;
  Zum::Bytes codeRefreshDigest;
  ZuCheck(Zum::opaqueParse(issued.response.refreshToken,
    codeFamilyID, codeRefreshDigest));
  auto codeFamily = loadRefresh(context, codeFamilyID);
  ZuCheck(
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
  Zum::Refresh refreshFamily{
    .id = refresh.id,
    .authVersion = 12,
    .appID = 9,
    .userID = 42,
    .created = 200,
    .expires = 1000,
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
  ZuCheck(insertRecord(context->refresh, Zum::Refresh{refreshFamily}));
  Zum::String refreshForm{"grant_type=refresh_token&refresh_token="};
  refreshForm << refresh.token << "&client_id=browser";
  issued = issueTokenRequest(
    db->requests, db, rng, ZuMv(refreshForm), {}, tokenKey, 300,
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
  auto rotated = loadRefresh(context, refresh.id);
  ZuCheck(rotated.generation == 1 && rotated.digest == rotatedDigest &&
    rotated.scope == issued.response.scope &&
    rotated.spent.length() == 1 && rotated.spent[0] == refresh.digest &&
    rotated.requestedRoleIDs.length() == 1 && rotated.requestedRoleIDs[0] == 7 &&
    rotated.actions[readAction] && !rotated.actions[writeAction]);
  Zum::String reuseForm{"grant_type=refresh_token&refresh_token="};
  reuseForm << refresh.token << "&client_id=browser";
  issued = issueTokenRequest(
    db->requests, db, rng, ZuMv(reuseForm), {}, tokenKey, 330,
    "issuer/oauth2/9");
  ZuCheck(issued.error == Zum::OAuthError::InvalidGrant &&
    loadRefresh(context, refresh.id).state == Zum::State::Revoked);
  ZuCheck(logged(
    Zum::String{} << " event=" << int(Zum::AuditEvent::RefreshReuse) << " ",
    Zum::String{} << " outcome=" << int(Zum::AuditOutcome::Failure) << " ",
    Zum::String{} << " actor=" << "browser" << " ",
    Zum::String{} << " target=" << Zum::auditID(refresh.id) << " "));

  issued = issueTokenRequest(db->requests, db, rng,
    Zum::String{"grant_type=client_credentials"},
    Zum::String{"Basic d29ya2xvYWQ6d3Jvbmc="}, tokenKey, 340,
    "issuer/oauth2/9");
  ZuCheck(issued.error == Zum::OAuthError::InvalidClient);
  ZuCheck(logged(
    Zum::String{} << " event=" << int(Zum::AuditEvent::ClientAuthentication) << " ",
    Zum::String{} << " outcome=" << int(Zum::AuditOutcome::Failure) << " ",
    Zum::String{} << " actor=" << "workload" << " "));
}
