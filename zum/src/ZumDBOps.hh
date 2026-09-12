//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// asynchronous authority and grant database operations

#ifndef ZumDBOps_HH
#define ZumDBOps_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZumIdentityDB.hh>
#include <zlib/ZumAppDB.hh>
#include <zlib/ZumOAuth.hh>
#include <zlib/ZumWebAuthn.hh>
#include <zlib/ZtlsSec.hh>

namespace Zum {

ZuDerive(ScopeVec, (ZtArray<ScopeAuth, VecHeap>));
ZuDerive(RoleVec, (ZtArray<Role, VecHeap>));
ZuDerive(ActionVec, (ZtArray<Action, VecHeap>));

struct AuthorityData {
  Issuer	issuer;
  App		app;
  Grant		grant;
  User		user;
  Cred		cred;
  Client	client;
  ClientAccess	access;
  ScopeVec	scopes;
  RoleVec	roles;
  ActionVec	actionRecords;
  ScopeSelection selection;
  IDVec		principalRoleIDs;
  ZtBitmap	actions;
  AppID		clientAppID = 0;
  int64_t	authorityDeadline = 0;
};

ZuDerive(AuthorityFn, (ZmFn<void(int, AuthorityData),
  ZmFnHeapID<"Zum.AuthorityFn">>));
ZuDerive(ClientScopesFn,
  (ZmFn<void(int, App, Client, ClientAccess, ScopeVec),
  ZmFnHeapID<"Zum.ClientScopesFn">>));
ZumExtern void clientScopes(DBContext *, Client, ClientScopesFn);

ZumExtern void loadGrantAuth(
  DBContext *, Grant, Client, bool requestedPresent, String requested,
  int64_t now, AuthorityFn);
ZumExtern void loadClientAuth(
  DBContext *, String issuer, Client, bool requestedPresent,
  String requested, AuthorityFn);
ZumExtern void loadUserAuth(
  DBContext *, Grant, User, Cred, IDVec principalRoleIDs, AuthorityFn);


template <typename Complete>
void assertionVerify(
    DBContext *context, Bytes ceremonyID, Bytes bindingDigest,
    AssertionInput input, String origin, String rpID, int64_t now,
    Complete &&complete)
{
  auto grants = context->grants;
  auto creds = context->creds;
  auto users = context->users;
  grants->run(0, [
    grants, creds, users, ceremonyID = ZuMv(ceremonyID),
    bindingDigest = ZuMv(bindingDigest), input = ZuMv(input),
    origin = ZuMv(origin), rpID = ZuMv(rpID), now,
    complete = ZuFwd<Complete>(complete)
  ]() mutable {
    grants->find<0>(0, ZuFwdTuple(ceremonyID), [
      creds, users, bindingDigest = ZuMv(bindingDigest), input = ZuMv(input),
      origin = ZuMv(origin), rpID = ZuMv(rpID), now,
      complete = ZuMv(complete)
    ](ZdbRowRef<Grant> grant) mutable {
      if (!grant || grant->data().kind != GrantKind::Ceremony ||
	  grant->data().purpose != GrantPurpose::Authorization ||
	  grant->data().state != State::Active || grant->data().owner ||
	  !grant->data().challenge || grant->data().expires <= now ||
	  !Ztls::ctEqual(grant->data().bindingDigest, bindingDigest)) {
	complete(WebAuthnError::Ceremony,
	  User{}, Cred{}, grant ? Grant{
	    .issuer = grant->data().issuer,
	    .clientID = grant->data().clientID
	  } : Grant{},
	  AssertionResult{});
	return;
      }
      Bytes credentialID = input.credentialID;
      creds->find<0>(0, ZuFwdTuple(credentialID), [
	creds, users, grant = ZuMv(grant), input = ZuMv(input),
	origin = ZuMv(origin), rpID = ZuMv(rpID), now,
	complete = ZuMv(complete)
      ](ZdbRowRef<Cred> cred) mutable {
	if (!cred || cred->data().state != State::Active || cred->data().owner ||
	    !cred->data().userID || !cred->data().publicKey) {
	  complete(WebAuthnError::Credential,
	    User{}, Cred{}, Grant{
	      .issuer = grant->data().issuer,
	      .clientID = grant->data().clientID
	    }, AssertionResult{});
	  return;
	}
	UserID userID = cred->data().userID;
	users->find<0>(0, ZuFwdTuple(userID), [
	  creds, grant = ZuMv(grant), cred = ZuMv(cred),
	  input = ZuMv(input), origin = ZuMv(origin), rpID = ZuMv(rpID),
	  now, complete = ZuMv(complete)
	](ZdbRowRef<User> user) mutable {
	  if (!user || user->data().state != State::Active ||
	      user->data().owner || !user->data().handle ||
	      cred->data().userVersion != user->data().authVersion) {
	    complete(WebAuthnError::Credential,
	      User{}, Cred{.id = cred->data().id}, Grant{
		.issuer = grant->data().issuer,
		.clientID = grant->data().clientID
	      },
	      AssertionResult{});
	    return;
	  }
	  AssertionState state{
	    .challenge = grant->data().challenge,
	    .origin = origin,
	    .rpID = rpID,
	    .publicKey = cred->data().publicKey,
	    .userHandle = user->data().handle,
	    .signCount = cred->data().signCount,
	    .backupEligible = cred->data().backupEligible
	  };
	  AssertionResult result;
	  int error = verifyAssertion(input, state, result);
	  if (error) {
	    complete(error, User{.handle = user->data().handle},
	      Cred{.id = cred->data().id}, Grant{
		.issuer = grant->data().issuer,
		.clientID = grant->data().clientID
	      }, AssertionResult{});
	    return;
	  }
	  Bytes credentialID = cred->data().id;
	  creds->findUpd<0>(0, ZuFwdTuple(credentialID), [
	    grant = ZuMv(grant), cred = ZuMv(cred), user = ZuMv(user),
	    result, now, complete = ZuMv(complete)
	  ](ZdbRow<Cred> *row) mutable {
	    if (!row || row->data().state != State::Active || row->data().owner ||
		row->data().signCount != cred->data().signCount) {
	      complete(WebAuthnError::Credential,
		User{.handle = user->data().handle},
		Cred{.id = cred->data().id}, Grant{
		  .issuer = grant->data().issuer,
		  .clientID = grant->data().clientID
		}, AssertionResult{});
	      return;
	    }
	    bool changed = false;
	    if (result.counter == CounterState::Advanced) {
	      row->data().signCount = result.signCount;
	      changed = true;
	    }
	    if (row->data().backedUp != result.backedUp) {
	      row->data().backedUp = result.backedUp;
	      changed = true;
	    }
	    if (changed) {
	      row->data().updated = now;
	      if (!row->commit()) {
		complete(WebAuthnError::Storage,
		  User{.handle = user->data().handle},
		  Cred{.id = cred->data().id}, Grant{
		    .issuer = grant->data().issuer,
		    .clientID = grant->data().clientID
		  }, AssertionResult{});
		return;
	      }
	    }
	    complete(WebAuthnError::OK, User{user->data()},
	      Cred{row->data()}, Grant{grant->data()}, result);
	  });
	});
      });
    });
  });
}

template <typename Complete>
void authorizationInsert(
    DBContext *context, Grant grant, Complete &&complete)
{
  auto grants = context->grants;
  grants->run(0, [
    grants, grant = ZuMv(grant), complete = ZuFwd<Complete>(complete)
  ]() mutable {
    ZdbRowRef<Grant> row = new ZdbRow<Grant>{grants, ZdbShard{0}};
    grants->insert(ZuMv(row), [
      grant = ZuMv(grant), complete = ZuMv(complete)
    ](ZdbRow<Grant> *row) mutable {
      if (!row) {
	complete(false);
	return;
      }
      new (row->ptr()) Grant{ZuMv(grant)};
      complete(row->commit());
    });
  });
}

template <typename Complete>
void authorizationFinish(
    DBContext *context, Ztls::Random &rng, Bytes id, Bytes bindingDigest,
    UserID userID, Bytes credentialID, IDVec roleIDs, ZtBitmap actions,
    uint64_t authVersion, uint64_t userVersion,
    int64_t authTime, int64_t codeExpires, Evidence evidence,
    Complete &&complete)
{
  auto apps = context->apps;
  auto users = context->users;
  auto grants = context->grants;
  grants->run(0, [
    apps, users, grants, rng = &rng, id = ZuMv(id),
    bindingDigest = ZuMv(bindingDigest), userID,
    credentialID = ZuMv(credentialID), roleIDs = ZuMv(roleIDs),
    actions = ZuMv(actions),
    authVersion, userVersion, authTime, codeExpires, evidence = ZuMv(evidence),
    complete = ZuFwd<Complete>(complete)
  ]() mutable {
    grants->find<0>(0, ZuFwdTuple(id), [
      apps, users, grants, rng, id = ZuMv(id),
      bindingDigest = ZuMv(bindingDigest), userID,
      credentialID = ZuMv(credentialID), roleIDs = ZuMv(roleIDs),
      actions = ZuMv(actions),
      authVersion, userVersion, authTime, codeExpires,
      evidence = ZuMv(evidence),
      complete = ZuMv(complete)
    ](ZdbRowRef<Grant> grant) mutable {
      if (!grant || !grant->data().appID) {
	complete(false, String{});
	return;
      }
      AppID appID = grant->data().appID;
      apps->find<0>(0, ZuFwdTuple(appID), [
	users, grants, rng, id = ZuMv(id),
	bindingDigest = ZuMv(bindingDigest), userID,
	credentialID = ZuMv(credentialID), roleIDs = ZuMv(roleIDs),
	actions = ZuMv(actions),
	appID, authVersion, userVersion, authTime, codeExpires,
	evidence = ZuMv(evidence),
	complete = ZuMv(complete)
      ](ZdbRowRef<App> app) mutable {
	if (!app || app->data().state != State::Active || app->data().owner ||
	    app->data().authVersion != authVersion) {
	  complete(false, String{});
	  return;
	}
	users->find<0>(0, ZuFwdTuple(userID), [
	  grants, rng, id = ZuMv(id), bindingDigest = ZuMv(bindingDigest),
	  userID, credentialID = ZuMv(credentialID), roleIDs = ZuMv(roleIDs),
	  actions = ZuMv(actions),
	  appID, authVersion, userVersion, authTime, codeExpires,
	  evidence = ZuMv(evidence),
	  complete = ZuMv(complete)
	](ZdbRowRef<User> user) mutable {
	  if (!user || user->data().state != State::Active ||
	      user->data().authVersion != userVersion) {
	    complete(false, String{});
	    return;
	  }
	  grants->findUpd<0, ZuSeq<1, 2>>(0, ZuFwdTuple(id), [
	    rng, bindingDigest = ZuMv(bindingDigest), userID,
	    credentialID = ZuMv(credentialID), roleIDs = ZuMv(roleIDs),
	    actions = ZuMv(actions),
	    appID, authVersion, userVersion, authTime, codeExpires,
	    evidence = ZuMv(evidence),
	    complete = ZuMv(complete)
	  ](ZdbRow<Grant> *row) mutable {
	    String code;
	    if (!row || row->data().appID != appID || !authorizationFinish(
		*rng, row->data(), bindingDigest, userID, ZuMv(credentialID),
		ZuMv(roleIDs), ZuMv(actions), authVersion, userVersion,
		authTime, codeExpires, code)) {
	      complete(false, String{});
	      return;
	    }
	    if (evidence.appID) {
	      row->data().authoritySource = UserSource::External;
	      row->data().authorityProviderID = evidence.providerID;
	      row->data().policyVersion = evidence.policyVersion;
	      row->data().evidenceVersion = evidence.version;
	    }
	    if (!row->commit()) {
	      ZuClear(code.data(), code.length());
	      complete(false, String{});
	      return;
	    }
	    complete(true, ZuMv(code));
	  });
	});
      });
    });
  });
}

template <typename Complete>
void refreshFinish(
    DBContext *context, Ztls::Random &rng, Bytes familyID,
    Bytes presentedDigest, String issuer, AppID appID, String scope, IDVec scopeIDs,
    ZtBitmap actions, uint64_t authVersion, UserID userID,
    uint64_t userVersion, int64_t now,
    unsigned generationLimit, unsigned spentLimit, Complete &&complete)
{
  auto apps = context->apps;
  auto users = context->users;
  auto grants = context->grants;
  apps->run(0, [
    apps, users, grants, rng = &rng, familyID = ZuMv(familyID),
    presentedDigest = ZuMv(presentedDigest), issuer = ZuMv(issuer),
    scope = ZuMv(scope), scopeIDs = ZuMv(scopeIDs),
    actions = ZuMv(actions), appID, authVersion,
    userID, userVersion, now,
    generationLimit, spentLimit,
    complete = ZuFwd<Complete>(complete)
  ]() mutable {
    apps->find<0>(0, ZuFwdTuple(appID), [
      users, grants, rng, familyID = ZuMv(familyID),
      presentedDigest = ZuMv(presentedDigest), issuer = ZuMv(issuer),
      scope = ZuMv(scope), scopeIDs = ZuMv(scopeIDs),
      actions = ZuMv(actions), appID, authVersion,
      userID, userVersion, now,
      generationLimit, spentLimit, complete = ZuMv(complete)
    ](ZdbRowRef<App> app) mutable {
      if (!appID || !app || app->data().state != State::Active ||
	  app->data().owner || app->data().authVersion != authVersion) {
	complete(RefreshRotate::Invalid, String{});
	return;
      }
      users->find<0>(0, ZuFwdTuple(userID), [
	grants, rng, familyID = ZuMv(familyID),
	presentedDigest = ZuMv(presentedDigest), issuer = ZuMv(issuer),
	scope = ZuMv(scope), scopeIDs = ZuMv(scopeIDs),
	actions = ZuMv(actions), appID, authVersion,
	userID, userVersion, now, generationLimit, spentLimit,
	complete = ZuMv(complete)
      ](ZdbRowRef<User> user) mutable {
	if (!user || user->data().state != State::Active ||
	    user->data().authVersion != userVersion) {
	  complete(RefreshRotate::Invalid, String{});
	  return;
	}
	grants->findUpd<0>(0, ZuFwdTuple(familyID), [
	  rng, familyID = ZuMv(familyID),
	  presentedDigest = ZuMv(presentedDigest), issuer = ZuMv(issuer),
	  scope = ZuMv(scope), scopeIDs = ZuMv(scopeIDs),
	  actions = ZuMv(actions), appID, authVersion,
	  userID, userVersion, now, generationLimit, spentLimit,
	  complete = ZuMv(complete)
	](ZdbRow<Grant> *row) mutable {
	  if (!row || row->data().issuer != issuer ||
	      row->data().appID != appID) {
	    complete(RefreshRotate::Invalid, String{});
	    return;
	  }
	  if (row->data().userID != userID ||
	      row->data().userVersion != userVersion) {
	    complete(RefreshRotate::Invalid, String{});
	    return;
	  }
	  auto match = refreshMatch(row->data(), presentedDigest);
	  OpaqueToken next;
	  if (match == RefreshMatch::Current &&
	      !opaqueIssue(*rng, familyID, next)) {
	    complete(RefreshRotate::Invalid, String{});
	    return;
	  }
	  auto result = refreshRotate(row->data(), match, presentedDigest,
	    ZuMv(next.digest), now, generationLimit, spentLimit);
	  if (result == RefreshRotate::Rotated) {
	    row->data().scope = ZuMv(scope);
	    row->data().scopeIDs = ZuMv(scopeIDs);
	    row->data().actions = ZuMv(actions);
	    row->data().authVersion = authVersion;
	  }
	  if (result != RefreshRotate::Rotated &&
	      result != RefreshRotate::Reused) {
	    if (next.token)
	      ZuClear(next.token.data(), next.token.length());
	    complete(result, String{});
	    return;
	  }
	  if (!row->commit()) {
	    if (next.token)
	      ZuClear(next.token.data(), next.token.length());
	    complete(RefreshRotate::Invalid, String{});
	    return;
	  }
	  if (result == RefreshRotate::Reused) {
	    complete(result, String{});
	    return;
	  }
	  complete(result, ZuMv(next.token));
	});
      });
	});
  });
}

template <typename Complete>
void tokenRelease(
    DBContext *context, String issuer, AppID appID, Bytes familyID,
    uint64_t authVersion, int64_t now, TokenResponse response,
    Complete &&complete)
{
  auto apps = context->apps;
  auto users = context->users;
  auto grants = context->grants;
  apps->run(0, [
    apps, users, grants, issuer = ZuMv(issuer), familyID = ZuMv(familyID),
    appID, authVersion, now, response = ZuMv(response),
    complete = ZuFwd<Complete>(complete)
  ]() mutable {
    apps->find<0>(0, ZuFwdTuple(appID), [
      users, grants, issuer = ZuMv(issuer), familyID = ZuMv(familyID),
      appID, authVersion, now, response = ZuMv(response), complete = ZuMv(complete)
    ](ZdbRowRef<App> row) mutable {
      if (!appID || !row || row->data().state != State::Active ||
	  row->data().owner || row->data().authVersion != authVersion) {
	tokenClear(response);
	complete(false, TokenResponse{});
	return;
      }
      if (!familyID) {
	complete(true, ZuMv(response));
	return;
      }
      grants->find<0>(0, ZuFwdTuple(familyID), [
	users, issuer = ZuMv(issuer), appID, authVersion, now,
	response = ZuMv(response),
	complete = ZuMv(complete)
      ](ZdbRowRef<Grant> family) mutable {
	if (!family || family->data().kind != GrantKind::Refresh ||
	    family->data().state != State::Active ||
	    family->data().owner ||
	    family->data().expires <= now ||
	    family->data().issuer != issuer ||
	    family->data().appID != appID ||
	    family->data().authVersion != authVersion) {
	  tokenClear(response);
	  complete(false, TokenResponse{});
	  return;
	}
	auto userID = family->data().userID;
	auto userVersion = family->data().userVersion;
	users->find<0>(0, ZuFwdTuple(userID), [
	  userVersion, response = ZuMv(response), complete = ZuMv(complete)
	](ZdbRowRef<User> user) mutable {
	  if (!user || user->data().state != State::Active ||
	      user->data().authVersion != userVersion) {
	    tokenClear(response);
	    complete(false, TokenResponse{});
	    return;
	  }
	  complete(true, ZuMv(response));
	});
      });
    });
  });
}

} // namespace Zum

#endif /* ZumDBOps_HH */
