//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// typed Zum Zdb and recoverable enrollment

#ifndef ZumDB_HH
#define ZumDB_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZuDerive.hh>

#include <zlib/ZmFn.hh>

#include <zlib/Zum.hh>

#include <zlib/Zdb.hh>

#include <zlib/ZumOAuth.hh>
#include <zlib/ZumWebAuthn.hh>

#include <zlib/ZtlsSec.hh>

#include <zlib/zum_saga_fbs.h>

namespace Zum {

struct MSaga;
struct DB;

ZdbTableDerive(IssuerTable, Issuer);
ZdbTableDerive(UserTable, User);
ZdbTableDerive(CredTable, Cred);
ZdbTableDerive(ActionTable, Action);
ZdbTableDerive(RoleTable, Role);
ZdbTableDerive(ScopeTable, Scope);
ZdbTableDerive(ClientTable, Client);
ZdbTableDerive(GrantTable, Grant);
ZdbTableDerive(SignKeyTable, SignKey);
ZdbTableDerive(AuditTable, Audit);

struct DBContext : public ZumPolymorph {
  ZdbTable<Issuer>	*issuers = nullptr;
  ZdbTable<User>	*users = nullptr;
  ZdbTable<Cred>	*creds = nullptr;
  ZdbTable<Action>	*actions = nullptr;
  ZdbTable<Role>	*roles = nullptr;
  ZdbTable<Scope>	*scopes = nullptr;
  ZdbTable<Client>	*clients = nullptr;
  ZdbTable<Grant>	*grants = nullptr;
  ZdbTable<SignKey>	*signKeys = nullptr;
  ZdbTable<Audit>	*audits = nullptr;
};

using ScopeVec = ZtArray<Scope, VecHeap>;
using RoleVec = ZtArray<Role, VecHeap>;
using ActionVec = ZtArray<Action, VecHeap>;

struct AuthorityData {
  Issuer	issuer;
  Grant		grant;
  User		user;
  Cred		cred;
  Client	client;
  ScopeVec	scopes;
  RoleVec	roles;
  ActionVec	actionRecords;
  ScopeSelection selection;
  IDVec		principalRoleIDs;
  ZtBitmap	actions;
};

ZuDerive(AuthorityFn, (ZmFn<void(int, AuthorityData),
  ZmFnHeapID<"Zum.AuthorityFn">>));
ZuDerive(SagaFn, (ZmFn<void(bool), ZmFnHeapID<"Zum.SagaFn">>));

ZumExtern void loadGrantAuth(
  DBContext *, Grant, Client, bool requestedPresent, String requested,
  AuthorityFn);
ZumExtern void loadClientAuth(
  DBContext *, String issuer, Client, bool requestedPresent,
  String requested, AuthorityFn);
ZumExtern void loadUserAuth(
  DBContext *, Grant, User, Cred, IDVec principalRoleIDs, AuthorityFn);

ZumExtern bool sagaSubmit(
  DB *, ZdbSagaID, ZmRef<MSaga>, SagaFn, SagaFn);

template <typename Complete>
void actionCreate(
    DBContext *context, String issuerID, String name, Complete &&complete)
{
  auto issuers = context->issuers;
  auto actions = context->actions;
  actions->run(0, [
    issuers, actions, issuerID = ZuMv(issuerID), name = ZuMv(name),
    complete = ZuFwd<Complete>(complete)
  ]() mutable {
    if (!name) {
      complete(false, ActionID{});
      return;
    }
    String lookupName = name;
    actions->find<1>(0, ZuFwdTuple(ZuMv(lookupName)), [
      issuers, actions, issuerID = ZuMv(issuerID), name = ZuMv(name),
      complete = ZuMv(complete)
    ](ZdbRowRef<Action> existing) mutable {
      if (existing) {
	complete(false, ActionID{});
	return;
      }
      String lookupIssuer = issuerID;
      issuers->findUpd<0>(0, ZuFwdTuple(ZuMv(lookupIssuer)), [
	actions, name = ZuMv(name), complete = ZuMv(complete)
      ](ZdbRow<Issuer> *issuer) mutable {
	if (!issuer || issuer->data().nextActionID == UINT32_MAX) {
	  complete(false, ActionID{});
	  return;
	}
	ActionID id = issuer->data().nextActionID++;
	++issuer->data().authVersion;
	if (!issuer->commit()) {
	  complete(false, ActionID{});
	  return;
	}
	ZdbRowRef<Action> row =
	  new ZdbRow<Action>{actions, ZdbShard{0}};
	actions->insert(ZuMv(row), [
	  id, name = ZuMv(name), complete = ZuMv(complete)
	](ZdbRow<Action> *row) mutable {
	  if (!row) {
	    complete(false, ActionID{});
	    return;
	  }
	  new (row->ptr()) Action{
	    .id = id, .name = ZuMv(name), .state = State::Active};
	  complete(row->commit(), id);
	});
      });
    });
  });
}

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
    int64_t authTime, int64_t codeExpires,
    Complete &&complete)
{
  auto issuers = context->issuers;
  auto users = context->users;
  auto grants = context->grants;
  grants->run(0, [
    issuers, users, grants, rng = &rng, id = ZuMv(id),
    bindingDigest = ZuMv(bindingDigest), userID,
    credentialID = ZuMv(credentialID), roleIDs = ZuMv(roleIDs),
    actions = ZuMv(actions),
    authVersion, userVersion, authTime, codeExpires,
    complete = ZuFwd<Complete>(complete)
  ]() mutable {
    grants->find<0>(0, ZuFwdTuple(id), [
      issuers, users, grants, rng, id = ZuMv(id),
      bindingDigest = ZuMv(bindingDigest), userID,
      credentialID = ZuMv(credentialID), roleIDs = ZuMv(roleIDs),
      actions = ZuMv(actions),
      authVersion, userVersion, authTime, codeExpires,
      complete = ZuMv(complete)
    ](ZdbRowRef<Grant> grant) mutable {
      if (!grant) {
	complete(false, String{});
	return;
      }
      String issuerID = grant->data().issuer;
      issuers->find<0>(0, ZuFwdTuple(ZuMv(issuerID)), [
	users, grants, rng, id = ZuMv(id),
	bindingDigest = ZuMv(bindingDigest), userID,
	credentialID = ZuMv(credentialID), roleIDs = ZuMv(roleIDs),
	actions = ZuMv(actions),
	authVersion, userVersion, authTime, codeExpires,
	complete = ZuMv(complete)
      ](ZdbRowRef<Issuer> issuer) mutable {
	if (!issuer || issuer->data().authVersion != authVersion) {
	  complete(false, String{});
	  return;
	}
	users->find<0>(0, ZuFwdTuple(userID), [
	  grants, rng, id = ZuMv(id), bindingDigest = ZuMv(bindingDigest),
	  userID, credentialID = ZuMv(credentialID), roleIDs = ZuMv(roleIDs),
	  actions = ZuMv(actions),
	  authVersion, userVersion, authTime, codeExpires,
	  complete = ZuMv(complete)
	](ZdbRowRef<User> user) mutable {
	  if (!user || user->data().state != State::Active ||
	      user->data().authVersion != userVersion) {
	    complete(false, String{});
	    return;
	  }
	  grants->findUpd<0, ZuSeq<1>>(0, ZuFwdTuple(id), [
	    rng, bindingDigest = ZuMv(bindingDigest), userID,
	    credentialID = ZuMv(credentialID), roleIDs = ZuMv(roleIDs),
	    actions = ZuMv(actions),
	    authVersion, userVersion, authTime, codeExpires,
	    complete = ZuMv(complete)
	  ](ZdbRow<Grant> *row) mutable {
	    String code;
	    if (!row || !authorizationFinish(
		*rng, row->data(), bindingDigest, userID, ZuMv(credentialID),
		ZuMv(roleIDs), ZuMv(actions), authVersion, userVersion,
		authTime, codeExpires, code)) {
	      complete(false, String{});
	      return;
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
    Bytes presentedDigest, String issuer, IDVec scopeIDs,
    ZtBitmap actions, uint64_t authVersion, UserID userID,
    uint64_t userVersion, int64_t now,
    unsigned generationLimit, unsigned spentLimit, Complete &&complete)
{
  auto issuers = context->issuers;
  auto users = context->users;
  auto grants = context->grants;
  issuers->run(0, [
    issuers, users, grants, rng = &rng, familyID = ZuMv(familyID),
    presentedDigest = ZuMv(presentedDigest), issuer = ZuMv(issuer),
    scopeIDs = ZuMv(scopeIDs), actions = ZuMv(actions), authVersion,
    userID, userVersion, now,
    generationLimit, spentLimit,
    complete = ZuFwd<Complete>(complete)
  ]() mutable {
    issuers->find<0>(0, ZuFwdTuple(issuer), [
      users, grants, rng, familyID = ZuMv(familyID),
      presentedDigest = ZuMv(presentedDigest), issuer = ZuMv(issuer),
      scopeIDs = ZuMv(scopeIDs), actions = ZuMv(actions), authVersion,
      userID, userVersion, now,
      generationLimit, spentLimit, complete = ZuMv(complete)
    ](ZdbRowRef<Issuer> issuerRow) mutable {
      if (!issuerRow || issuerRow->data().authVersion != authVersion) {
	complete(RefreshRotate::Invalid, String{});
	return;
      }
      users->find<0>(0, ZuFwdTuple(userID), [
	grants, rng, familyID = ZuMv(familyID),
	presentedDigest = ZuMv(presentedDigest), issuer = ZuMv(issuer),
	scopeIDs = ZuMv(scopeIDs), actions = ZuMv(actions), authVersion,
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
	  scopeIDs = ZuMv(scopeIDs), actions = ZuMv(actions), authVersion,
	  userID, userVersion, now, generationLimit, spentLimit,
	  complete = ZuMv(complete)
	](ZdbRow<Grant> *row) mutable {
	  if (!row || row->data().issuer != issuer) {
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
    DBContext *context, String issuer, Bytes familyID,
    uint64_t authVersion, int64_t now, TokenResponse response,
    Complete &&complete)
{
  auto issuers = context->issuers;
  auto users = context->users;
  auto grants = context->grants;
  issuers->run(0, [
    issuers, users, grants, issuer = ZuMv(issuer), familyID = ZuMv(familyID),
    authVersion, now, response = ZuMv(response),
    complete = ZuFwd<Complete>(complete)
  ]() mutable {
    issuers->find<0>(0, ZuFwdTuple(issuer), [
      users, grants, issuer = ZuMv(issuer), familyID = ZuMv(familyID),
      authVersion, now, response = ZuMv(response), complete = ZuMv(complete)
    ](ZdbRowRef<Issuer> row) mutable {
      if (!row || row->data().authVersion != authVersion) {
	tokenClear(response);
	complete(false, TokenResponse{});
	return;
      }
      if (!familyID) {
	complete(true, ZuMv(response));
	return;
      }
      grants->find<0>(0, ZuFwdTuple(familyID), [
	users, issuer = ZuMv(issuer), authVersion, now,
	response = ZuMv(response),
	complete = ZuMv(complete)
      ](ZdbRowRef<Grant> family) mutable {
	if (!family || family->data().kind != GrantKind::Refresh ||
	    family->data().state != State::Active ||
	    family->data().owner ||
	    family->data().expires <= now ||
	    family->data().issuer != issuer ||
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

struct Enrollment : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"enrollment.v1">;
  enum { NSteps = 7 };

  Bytes		ceremonyID;
  UserID	userID = 0;
  String	name;
  Bytes		handle;
  IDVec		roleIDs;
  Bytes		credentialID;
  Bytes		publicKey;
  uint32_t	signCount = 0;
  int64_t	created = 0;
  bool		backupEligible = false;
  bool		backedUp = false;
  String	label;

  ZdbSagaStep(0, zum.grant, Update) {
    if constexpr (!Fwd) {
      // A failed enrollment still spends its one-use capability.
      complete(true);
      return {};
    }
    context->grants->run(0, [
      this, complete = ZuMv(complete)
    ]() mutable {
      saga->findUpd<0>(context->grants, 0, ZuFwdTuple(ceremonyID),
	ZuMv(complete), [this](ZdbRow<Grant> *row, auto &&complete) mutable {
	auto &grant = row->data();
	if (grant.kind != GrantKind::Ceremony ||
	    (grant.purpose != GrantPurpose::Enrollment &&
	     grant.purpose != GrantPurpose::Bootstrap) ||
	    grant.state != State::Active || grant.owner) {
	  complete(false);
	  return;
	}
	grant.state = State::Consumed;
	grant.owner = saga->id();
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(1, zum.user, Insert) {
    context->users->run(0, [
      this, complete = ZuMv(complete)
    ]() mutable {
      if constexpr (Fwd) {
	ZdbRowRef<User> row =
	  new ZdbRow<User>{context->users, ZdbShard{0}};
	saga->insert(context->users, ZuMv(row), ZuMv(complete),
	  [this](ZdbRow<User> *row, auto &&complete) mutable {
	  new (row->ptr()) User{
	    .id = userID,
	    .name = name,
	    .handle = handle,
	    .roleIDs = roleIDs,
	    .created = created,
	    .updated = created,
	    .state = State::Pending,
	    .owner = saga->id()
	  };
	  complete(row->commit());
	});
      } else {
	saga->findDel<0>(context->users, 0, ZuFwdTuple(userID),
	  ZuMv(complete), [this](ZdbRow<User> *row, auto &&complete) mutable {
	  auto &user = row->data();
	  if (user.owner != saga->id() || user.state != State::Pending) {
	    complete(true);
	    return;
	  }
	  complete(row->commit());
	});
      }
    });
    return {};
  }

  ZdbSagaStep(2, zum.cred, Insert) {
    context->creds->run(0, [
      this, complete = ZuMv(complete)
    ]() mutable {
      if constexpr (Fwd) {
	ZdbRowRef<Cred> row =
	  new ZdbRow<Cred>{context->creds, ZdbShard{0}};
	saga->insert(context->creds, ZuMv(row), ZuMv(complete),
	  [this](ZdbRow<Cred> *row, auto &&complete) mutable {
	  new (row->ptr()) Cred{
	    .id = credentialID,
	    .userID = userID,
	    .publicKey = publicKey,
	    .signCount = signCount,
	    .created = created,
	    .updated = created,
	    .state = State::Pending,
	    .backupEligible = backupEligible,
	    .backedUp = backedUp,
	    .label = label,
	    .owner = saga->id()
	  };
	  complete(row->commit());
	});
      } else {
	saga->findDel<0>(context->creds, 0, ZuFwdTuple(credentialID),
	  ZuMv(complete), [this](ZdbRow<Cred> *row, auto &&complete) mutable {
	  auto &cred = row->data();
	  if (cred.owner != saga->id() || cred.state != State::Pending) {
	    complete(true);
	    return;
	  }
	  complete(row->commit());
	});
      }
    });
    return {};
  }

  ZdbSagaStep(3, zum.cred, Update) {
    context->creds->run(0, [
      this, complete = ZuMv(complete)
    ]() mutable {
      saga->findUpd<0>(context->creds, 0, ZuFwdTuple(credentialID),
	ZuMv(complete), [this](ZdbRow<Cred> *row, auto &&complete) mutable {
	auto &cred = row->data();
	if (cred.owner != saga->id() ||
	    cred.state != (Fwd ? State::Pending : State::Active)) {
	  complete(!Fwd);
	  return;
	}
	cred.state = Fwd ? State::Active : State::Pending;
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(4, zum.user, Update) {
    context->users->run(0, [
      this, complete = ZuMv(complete)
    ]() mutable {
      saga->findUpd<0>(context->users, 0, ZuFwdTuple(userID),
	ZuMv(complete), [this](ZdbRow<User> *row, auto &&complete) mutable {
	auto &user = row->data();
	if (user.owner != saga->id() ||
	    user.state != (Fwd ? State::Pending : State::Active)) {
	  complete(!Fwd);
	  return;
	}
	user.state = Fwd ? State::Active : State::Pending;
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(5, zum.cred, Update) {
    context->creds->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->creds, 0, ZuFwdTuple(credentialID),
	ZuMv(complete), [this](
	  ZdbRow<Cred> *row, auto &&complete) mutable {
	if (!row || row->data().state != State::Active ||
	    row->data().owner != (Fwd ? saga->id() : uint128_t{0})) {
	  complete(!Fwd);
	  return;
	}
	row->data().owner = Fwd ? uint128_t{0} : saga->id();
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(6, zum.user, Update) {
    context->users->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->users, 0, ZuFwdTuple(userID),
	ZuMv(complete), [this](
	  ZdbRow<User> *row, auto &&complete) mutable {
	if (!row || row->data().state != State::Active ||
	    row->data().owner != (Fwd ? saga->id() : uint128_t{0})) {
	  complete(!Fwd);
	  return;
	}
	row->data().owner = Fwd ? uint128_t{0} : saga->id();
	complete(row->commit());
      });
    });
    return {};
  }
};

ZfbStruct(ZumAPI, Enrollment,
  (((ceremonyID),	(Ctor<0>)),	(Bytes)),
  (((userID),		(Ctor<1>)),	(UInt64)),
  (((name),		(Ctor<2>)),	(String)),
  (((handle),		(Ctor<3>)),	(Bytes)),
  (((roleIDs),		(Ctor<4>)),	(UInt64Vec)),
  (((credentialID),	(Ctor<5>)),	(Bytes)),
  (((publicKey),	(Ctor<6>)),	(Bytes)),
  (((signCount),	(Ctor<7>)),	(UInt32)),
  (((created),		(Ctor<8>)),	(Int64)),
  (((backupEligible),	(Ctor<9>)),	(Bool)),
  (((backedUp),	(Ctor<10>)),	(Bool)),
  (((label),		(Ctor<11>)),	(String)));

ZumExtern int enrollmentPrepare(
  const Grant &, ZuBSpan bindingDigest, RegistrationInput &,
  ZuCSpan origin, ZuCSpan rpID,
  unsigned credentialIDMax,
  int64_t now, Enrollment &);

struct CredentialAdd : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"credentialAdd.v1">;
  enum { NSteps = 4 };

  Bytes		ceremonyID;
  String	issuer;
  UserID	userID = 0;
  Bytes		userHandle;
  Bytes		credentialID;
  Bytes		publicKey;
  uint32_t	signCount = 0;
  int64_t	created = 0;
  bool		backupEligible = false;
  bool		backedUp = false;
  String	label;
  uint64_t	userVersion = 1;

  ZdbSagaStep(0, zum.grant, Update) {
    if constexpr (!Fwd) {
      // A failed registration still spends its one-use ceremony.
      complete(true);
      return {};
    }
    context->users->run(0, [this, complete = ZuMv(complete)]() mutable {
      context->users->find<0>(0, ZuFwdTuple(userID), [
	this, complete = ZuMv(complete)
      ](ZdbRowRef<User> user) mutable {
	if (!user || user->data().state != State::Active ||
	    user->data().handle != userHandle ||
	    user->data().authVersion != userVersion) {
	  complete(false);
	  return;
	}
	context->grants->run(0, [
	  this, complete = ZuMv(complete)
	]() mutable {
	  saga->findUpd<0>(context->grants, 0, ZuFwdTuple(ceremonyID),
	    ZuMv(complete), [this](
	      ZdbRow<Grant> *row, auto &&complete) mutable {
	    auto &grant = row->data();
	    if (grant.kind != GrantKind::Ceremony ||
		grant.purpose != GrantPurpose::AddCredential ||
		grant.state != State::Active || grant.owner ||
		grant.issuer != issuer || grant.userID != userID ||
		grant.userVersion != userVersion || grant.expires <= created) {
	      complete(false);
	      return;
	    }
	    grant.state = State::Consumed;
	    grant.owner = saga->id();
	    complete(row->commit());
	  });
	});
      });
    });
    return {};
  }

  ZdbSagaStep(1, zum.cred, Insert) {
    context->creds->run(0, [this, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	ZdbRowRef<Cred> row =
	  new ZdbRow<Cred>{context->creds, ZdbShard{0}};
	saga->insert(context->creds, ZuMv(row), ZuMv(complete),
	  [this](ZdbRow<Cred> *row, auto &&complete) mutable {
	  new (row->ptr()) Cred{
	    .id = credentialID,
	    .userID = userID,
	    .publicKey = publicKey,
	    .signCount = signCount,
	    .created = created,
	    .updated = created,
	    .state = State::Pending,
	    .backupEligible = backupEligible,
	    .backedUp = backedUp,
	    .label = label,
	    .owner = saga->id(),
	    .userVersion = userVersion
	  };
	  complete(row->commit());
	});
      } else {
	saga->findDel<0>(context->creds, 0, ZuFwdTuple(credentialID),
	  ZuMv(complete), [this](
	    ZdbRow<Cred> *row, auto &&complete) mutable {
	  auto &cred = row->data();
	  if (cred.owner != saga->id() || cred.state != State::Pending) {
	    complete(true);
	    return;
	  }
	  complete(row->commit());
	});
      }
    });
    return {};
  }

  ZdbSagaStep(2, zum.cred, Update) {
    context->creds->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->creds, 0, ZuFwdTuple(credentialID),
	ZuMv(complete), [this](
	  ZdbRow<Cred> *row, auto &&complete) mutable {
	auto &cred = row->data();
	if (cred.owner != saga->id() ||
	    cred.state != (Fwd ? State::Pending : State::Active)) {
	  complete(!Fwd);
	  return;
	}
	cred.state = Fwd ? State::Active : State::Pending;
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(3, zum.cred, Update) {
    context->creds->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->creds, 0, ZuFwdTuple(credentialID),
	ZuMv(complete), [this](
	  ZdbRow<Cred> *row, auto &&complete) mutable {
	if (!row || row->data().state != State::Active ||
	    row->data().owner != (Fwd ? saga->id() : uint128_t{0})) {
	  complete(!Fwd);
	  return;
	}
	row->data().owner = Fwd ? uint128_t{0} : saga->id();
	complete(row->commit());
      });
    });
    return {};
  }
};

ZfbStruct(ZumAPI, CredentialAdd,
  (((ceremonyID),	(Ctor<0>)),	(Bytes)),
  (((issuer),		(Ctor<1>)),	(String)),
  (((userID),		(Ctor<2>)),	(UInt64)),
  (((userHandle),	(Ctor<3>)),	(Bytes)),
  (((credentialID),	(Ctor<4>)),	(Bytes)),
  (((publicKey),	(Ctor<5>)),	(Bytes)),
  (((signCount),	(Ctor<6>)),	(UInt32)),
  (((created),		(Ctor<7>)),	(Int64)),
  (((backupEligible),	(Ctor<8>)),	(Bool)),
  (((backedUp),		(Ctor<9>)),	(Bool)),
  (((label),		(Ctor<10>)),	(String)),
  (((userVersion),	(Ctor<11>)),	(UInt64, 1)));

ZumExtern int credentialPrepare(
  const Grant &, ZuBSpan bindingDigest, RegistrationInput &,
  ZuCSpan origin, ZuCSpan rpID,
  unsigned credentialIDMax,
  int64_t now, CredentialAdd &);

struct RecoveryStart : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"recoveryStart.v1">;
  enum { NSteps = 4 };

  Bytes		capabilityID;
  Bytes		digest;
  String	issuer;
  UserID	userID = 0;
  uint64_t	userVersion = 0;
  int64_t	created = 0;
  int64_t	expires = 0;
  String	actor;

  ZdbSagaStep(0, zum.user, Update) {
    context->users->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->users, 0, ZuFwdTuple(userID),
	ZuMv(complete), [this](
	  ZdbRow<User> *row, auto &&complete) mutable {
	if constexpr (Fwd) {
	  if (!row || row->data().owner ||
	      (row->data().state != State::Active &&
	       row->data().state != State::Suspended) ||
	      row->data().authVersion + 1 != userVersion) {
	    complete(false);
	    return;
	  }
	  row->data().state = State::Suspended;
	  row->data().authVersion = userVersion;
	  row->data().updated = created;
	  row->data().owner = saga->id();
	} else {
	  if (!row || row->data().state != State::Suspended ||
	      row->data().authVersion != userVersion ||
	      row->data().owner != saga->id()) {
	    complete(true);
	    return;
	  }
	  row->data().owner = 0;
	}
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(1, zum.grant, Insert) {
    context->grants->run(0, [this, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	ZdbRowRef<Grant> row =
	  new ZdbRow<Grant>{context->grants, ZdbShard{0}};
	saga->insert(context->grants, ZuMv(row), ZuMv(complete),
	  [this](ZdbRow<Grant> *row, auto &&complete) mutable {
	  new (row->ptr()) Grant{
	    .id = capabilityID,
	    .owner = saga->id(),
	    .userVersion = userVersion,
	    .userID = userID,
	    .created = created,
	    .expires = expires,
	    .kind = GrantKind::Capability,
	    .purpose = GrantPurpose::Recovery,
	    .state = State::Pending,
	    .issuer = issuer,
	    .digest = digest,
	    .actor = actor
	  };
	  complete(row->commit());
	});
      } else {
	saga->findDel<0>(context->grants, 0, ZuFwdTuple(capabilityID),
	  ZuMv(complete), [this](
	    ZdbRow<Grant> *row, auto &&complete) mutable {
	  if (row->data().owner != saga->id() ||
	      row->data().state != State::Pending) {
	    complete(true);
	    return;
	  }
	  complete(row->commit());
	});
      }
    });
    return {};
  }

  ZdbSagaStep(2, zum.user, Update) {
    context->users->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->users, 0, ZuFwdTuple(userID),
	ZuMv(complete), [this](
	  ZdbRow<User> *row, auto &&complete) mutable {
	if (!row || row->data().state != State::Suspended ||
	    row->data().authVersion != userVersion ||
	    row->data().owner != (Fwd ? saga->id() : uint128_t{0})) {
	  complete(!Fwd);
	  return;
	}
	row->data().owner = Fwd ? uint128_t{0} : saga->id();
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(3, zum.grant, Update) {
    context->grants->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->grants, 0, ZuFwdTuple(capabilityID),
	ZuMv(complete), [this](
	  ZdbRow<Grant> *row, auto &&complete) mutable {
	if (!row || row->data().owner !=
	      (Fwd ? saga->id() : uint128_t{0}) ||
	    row->data().state !=
	      (Fwd ? State::Pending : State::Active)) {
	  complete(!Fwd);
	  return;
	}
	row->data().state = Fwd ? State::Active : State::Pending;
	row->data().owner = Fwd ? uint128_t{0} : saga->id();
	complete(row->commit());
      });
    });
    return {};
  }
};

ZfbStruct(ZumAPI, RecoveryStart,
  (((capabilityID),	(Ctor<0>)),	(Bytes)),
  (((digest),		(Ctor<1>)),	(Bytes)),
  (((issuer),		(Ctor<2>)),	(String)),
  (((userID),		(Ctor<3>)),	(UInt64)),
  (((userVersion),	(Ctor<4>)),	(UInt64)),
  (((created),		(Ctor<5>)),	(Int64)),
  (((expires),		(Ctor<6>)),	(Int64)),
  (((actor),		(Ctor<7>)),	(String)));

struct RecoveryEnroll : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"recoveryEnroll.v1">;
  enum { NSteps = 6 };

  Bytes		ceremonyID;
  String	issuer;
  String	actor;
  UserID	userID = 0;
  uint64_t	userVersion = 0;
  Bytes		oldHandle;
  Bytes		newHandle;
  Bytes		credentialID;
  Bytes		publicKey;
  uint32_t	signCount = 0;
  int64_t	created = 0;
  bool		backupEligible = false;
  bool		backedUp = false;
  String	label;

  ZdbSagaStep(0, zum.grant, Update) {
    if constexpr (!Fwd) {
      // A failed recovery still spends its one-use capability.
      complete(true);
      return {};
    }
    context->users->run(0, [this, complete = ZuMv(complete)]() mutable {
      context->users->find<0>(0, ZuFwdTuple(userID), [
	this, complete = ZuMv(complete)
      ](ZdbRowRef<User> user) mutable {
	if (!user || user->data().state != State::Suspended ||
	    user->data().owner || user->data().authVersion != userVersion ||
	    user->data().handle != oldHandle) {
	  complete(false);
	  return;
	}
	context->grants->run(0, [
	  this, complete = ZuMv(complete)
	]() mutable {
	  saga->findUpd<0>(context->grants, 0, ZuFwdTuple(ceremonyID),
	    ZuMv(complete), [this](
	      ZdbRow<Grant> *row, auto &&complete) mutable {
	    auto &grant = row->data();
	    if (grant.kind != GrantKind::Ceremony ||
		grant.purpose != GrantPurpose::Recovery ||
		grant.state != State::Active || grant.owner ||
		grant.issuer != issuer || grant.userID != userID ||
		grant.userVersion != userVersion ||
		grant.userHandle != newHandle || grant.expires <= created) {
	      complete(false);
	      return;
	    }
	    grant.state = State::Consumed;
	    grant.owner = saga->id();
	    complete(row->commit());
	  });
	});
      });
    });
    return {};
  }

  ZdbSagaStep(1, zum.cred, Insert) {
    context->creds->run(0, [this, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	ZdbRowRef<Cred> row =
	  new ZdbRow<Cred>{context->creds, ZdbShard{0}};
	saga->insert(context->creds, ZuMv(row), ZuMv(complete),
	  [this](ZdbRow<Cred> *row, auto &&complete) mutable {
	  new (row->ptr()) Cred{
	    .id = credentialID,
	    .userID = userID,
	    .publicKey = publicKey,
	    .signCount = signCount,
	    .created = created,
	    .updated = created,
	    .state = State::Pending,
	    .backupEligible = backupEligible,
	    .backedUp = backedUp,
	    .label = label,
	    .owner = saga->id(),
	    .userVersion = userVersion
	  };
	  complete(row->commit());
	});
      } else {
	saga->findDel<0>(context->creds, 0, ZuFwdTuple(credentialID),
	  ZuMv(complete), [this](
	    ZdbRow<Cred> *row, auto &&complete) mutable {
	  if (row->data().owner != saga->id() ||
	      row->data().state != State::Pending) {
	    complete(true);
	    return;
	  }
	  complete(row->commit());
	});
      }
    });
    return {};
  }

  ZdbSagaStep(2, zum.cred, Update) {
    context->creds->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->creds, 0, ZuFwdTuple(credentialID),
	ZuMv(complete), [this](
	  ZdbRow<Cred> *row, auto &&complete) mutable {
	if (!row || row->data().owner != saga->id() ||
	    row->data().state != (Fwd ? State::Pending : State::Active)) {
	  complete(!Fwd);
	  return;
	}
	row->data().state = Fwd ? State::Active : State::Pending;
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(3, zum.user, Update) {
    context->users->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->users, 0, ZuFwdTuple(userID),
	ZuMv(complete), [this](
	  ZdbRow<User> *row, auto &&complete) mutable {
	if constexpr (Fwd) {
	  if (!row || row->data().state != State::Suspended ||
	      row->data().owner || row->data().authVersion != userVersion ||
	      row->data().handle != oldHandle) {
	    complete(false);
	    return;
	  }
	  row->data().handle = newHandle;
	  row->data().state = State::Active;
	  row->data().owner = saga->id();
	  row->data().updated = created;
	} else {
	  if (!row || row->data().state != State::Active ||
	      row->data().owner != saga->id() ||
	      row->data().authVersion != userVersion ||
	      row->data().handle != newHandle) {
	    complete(true);
	    return;
	  }
	  row->data().handle = oldHandle;
	  row->data().state = State::Suspended;
	  row->data().owner = 0;
	}
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(4, zum.cred, Update) {
    context->creds->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->creds, 0, ZuFwdTuple(credentialID),
	ZuMv(complete), [this](
	  ZdbRow<Cred> *row, auto &&complete) mutable {
	if (!row || row->data().state != State::Active ||
	    row->data().owner != (Fwd ? saga->id() : uint128_t{0})) {
	  complete(!Fwd);
	  return;
	}
	row->data().owner = Fwd ? uint128_t{0} : saga->id();
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(5, zum.user, Update) {
    context->users->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->users, 0, ZuFwdTuple(userID),
	ZuMv(complete), [this](
	  ZdbRow<User> *row, auto &&complete) mutable {
	if (!row || row->data().state != State::Active ||
	    row->data().owner != (Fwd ? saga->id() : uint128_t{0})) {
	  complete(!Fwd);
	  return;
	}
	row->data().owner = Fwd ? uint128_t{0} : saga->id();
	complete(row->commit());
      });
    });
    return {};
  }
};

ZfbStruct(ZumAPI, RecoveryEnroll,
  (((ceremonyID),	(Ctor<0>)),	(Bytes)),
  (((issuer),		(Ctor<1>)),	(String)),
  (((actor),		(Ctor<2>)),	(String)),
  (((userID),		(Ctor<3>)),	(UInt64)),
  (((userVersion),	(Ctor<4>)),	(UInt64)),
  (((oldHandle),	(Ctor<5>)),	(Bytes)),
  (((newHandle),	(Ctor<6>)),	(Bytes)),
  (((credentialID),	(Ctor<7>)),	(Bytes)),
  (((publicKey),	(Ctor<8>)),	(Bytes)),
  (((signCount),	(Ctor<9>)),	(UInt32)),
  (((created),		(Ctor<10>)),	(Int64)),
  (((backupEligible),	(Ctor<11>)),	(Bool)),
  (((backedUp),		(Ctor<12>)),	(Bool)),
  (((label),		(Ctor<13>)),	(String)));

ZumExtern int recoveryPrepare(
  const Grant &, const User &, ZuBSpan bindingDigest, RegistrationInput &,
  ZuCSpan origin, ZuCSpan rpID,
  unsigned credentialIDMax,
  int64_t now, RecoveryEnroll &);

struct CodeFamily : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"codeFamily.v1">;
  enum { NSteps = 4 };

  Bytes		codeID;
  Bytes		codeDigest;
  Bytes		familyID;
  String	issuer;
  UserID	userID = 0;
  String	clientID;
  Bytes		credentialID;
  String	audience;
  IDVec		scopeIDs;
  IDVec		roleIDs;
  ZtBitmap	actions;
  Bytes		digest;
  uint64_t	authVersion = 0;
  uint64_t	userVersion = 1;
  int64_t	authTime = 0;
  int64_t	created = 0;
  int64_t	expires = 0;

  ZdbSagaStep(0, zum.grant, Update) {
    context->grants->run(0, [
      this, complete = ZuMv(complete)
    ]() mutable {
      saga->findUpd<0>(context->grants, 0, ZuFwdTuple(codeID),
	ZuMv(complete), [this](ZdbRow<Grant> *row, auto &&complete) mutable {
	auto &code = row->data();
	bool valid = code.kind == GrantKind::Code &&
	  Ztls::ctEqual(code.digest, codeDigest) && code.issuer == issuer &&
	  code.userID == userID && code.clientID == clientID &&
	  code.credentialID == credentialID && code.audience == audience &&
	  code.roleIDs == roleIDs &&
	  code.userVersion == userVersion && code.expires > created;
	if constexpr (Fwd)
	  valid &= code.state == State::Active && !code.owner;
	else
	  valid &= code.state == State::Consumed && code.owner == saga->id();
	if (!valid) {
	  complete(!Fwd);
	  return;
	}
	code.state = Fwd ? State::Consumed : State::Active;
	code.owner = Fwd ? saga->id() : uint128_t{0};
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(1, zum.grant, Insert) {
    context->grants->run(0, [
      this, complete = ZuMv(complete)
    ]() mutable {
      if constexpr (Fwd) {
	ZdbRowRef<Grant> row =
	  new ZdbRow<Grant>{context->grants, ZdbShard{0}};
	saga->insert(context->grants, ZuMv(row), ZuMv(complete),
	  [this](ZdbRow<Grant> *row, auto &&complete) mutable {
	  new (row->ptr()) Grant{
	    .id = familyID,
	    .owner = saga->id(),
	    .authVersion = authVersion,
	    .userVersion = userVersion,
	    .userID = userID,
	    .created = created,
	    .expires = expires,
	    .kind = GrantKind::Refresh,
	    .state = State::Pending,
	    .issuer = issuer,
	    .clientID = clientID,
	    .audience = audience,
	    .authTime = authTime,
	    .scopeIDs = scopeIDs,
	    .roleIDs = roleIDs,
	    .actions = actions,
	    .credentialID = credentialID,
	    .digest = digest
	  };
	  complete(row->commit());
	});
      } else {
	saga->findDel<0>(context->grants, 0, ZuFwdTuple(familyID),
	  ZuMv(complete), [this](ZdbRow<Grant> *row, auto &&complete) mutable {
	  auto &family = row->data();
	  if (family.owner != saga->id() || family.state != State::Pending) {
	    complete(true);
	    return;
	  }
	  complete(row->commit());
	});
      }
    });
    return {};
  }

  ZdbSagaStep(2, zum.grant, Update) {
    context->issuers->run(0, [
      this, complete = ZuMv(complete)
    ]() mutable {
      context->issuers->find<0>(0, ZuFwdTuple(issuer), [
	this, complete = ZuMv(complete)
      ](ZdbRowRef<Issuer> issuerRow) mutable {
	if constexpr (Fwd)
	  if (!issuerRow || issuerRow->data().authVersion != authVersion) {
	    complete(false);
	    return;
	  }
	context->users->run(0, [this, complete = ZuMv(complete)]() mutable {
	  context->users->find<0>(0, ZuFwdTuple(userID), [
	    this, complete = ZuMv(complete)
	  ](ZdbRowRef<User> user) mutable {
	    if constexpr (Fwd)
	      if (!user || user->data().state != State::Active ||
		  user->data().authVersion != userVersion) {
		complete(false);
		return;
	      }
	    context->grants->run(0, [
	      this, complete = ZuMv(complete)
	    ]() mutable {
	      saga->findUpd<0>(context->grants, 0, ZuFwdTuple(familyID),
		ZuMv(complete), [this](
		  ZdbRow<Grant> *row, auto &&complete) mutable {
		auto &family = row->data();
		if (family.owner != saga->id() ||
		    family.state != (Fwd ? State::Pending : State::Active)) {
		  complete(!Fwd);
		  return;
		}
		family.state = Fwd ? State::Active : State::Pending;
		complete(row->commit());
	      });
	    });
	  });
	});
      });
    });
    return {};
  }

  ZdbSagaStep(3, zum.grant, Update) {
    context->grants->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->grants, 0, ZuFwdTuple(familyID),
	ZuMv(complete), [this](
	  ZdbRow<Grant> *row, auto &&complete) mutable {
	if (!row || row->data().state != State::Active ||
	    row->data().owner != (Fwd ? saga->id() : uint128_t{0})) {
	  complete(!Fwd);
	  return;
	}
	row->data().owner = Fwd ? uint128_t{0} : saga->id();
	complete(row->commit());
      });
    });
    return {};
  }
};

ZfbStruct(ZumAPI, CodeFamily,
  (((codeID),		(Ctor<0>)),	(Bytes)),
  (((codeDigest),	(Ctor<1>)),	(Bytes)),
  (((familyID),	(Ctor<2>)),	(Bytes)),
  (((issuer),		(Ctor<3>)),	(String)),
  (((userID),		(Ctor<4>)),	(UInt64)),
  (((clientID),	(Ctor<5>)),	(String)),
  (((credentialID),	(Ctor<6>)),	(Bytes)),
  (((audience),	(Ctor<7>)),	(String)),
  (((scopeIDs),	(Ctor<8>)),	(UInt64Vec)),
  (((roleIDs),		(Ctor<9>)),	(UInt64Vec)),
  (((actions),		(Ctor<10>)),	(UDT)),
  (((digest),		(Ctor<11>)),	(Bytes)),
  (((authVersion),	(Ctor<12>)),	(UInt64)),
  (((userVersion),	(Ctor<13>)),	(UInt64, 1)),
  (((authTime),	(Ctor<14>)),	(Int64)),
  (((created),		(Ctor<15>)),	(Int64)),
  (((expires),		(Ctor<16>)),	(Int64)));

ZumExtern bool codeFamilyPrepare(
  Ztls::Random &, const Grant &, ZuBSpan codeDigest,
  IDVec scopeIDs, ZtBitmap actions, uint64_t authVersion,
  int64_t now, int64_t expires, CodeFamily &, String &refreshToken);

struct UserChange : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"userChange.v1">;
  enum { NSteps = 3 };

  String	issuer;
  UserID	userID = 0;
  IDVec		oldRoleIDs;
  IDVec		newRoleIDs;
  int64_t	oldUpdated = 0;
  int64_t	updated = 0;
  uint64_t	authVersion = 0;
  State::T	oldState = State::Pending;
  State::T	newState = State::Pending;

  ZdbSagaStep(0, zum.user, Update) {
    context->users->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->users, 0, ZuFwdTuple(userID),
	ZuMv(complete), [this](
	  ZdbRow<User> *row, auto &&complete) mutable {
	if constexpr (Fwd) {
	  if (!row || row->data().owner ||
	      row->data().roleIDs != oldRoleIDs ||
	      row->data().updated != oldUpdated ||
	      row->data().state != oldState) {
	    complete(false);
	    return;
	  }
	  row->data().roleIDs = newRoleIDs;
	  row->data().updated = updated;
	  row->data().state = newState;
	  row->data().owner = saga->id();
	} else {
	  if (!row || row->data().owner != saga->id() ||
	      row->data().roleIDs != newRoleIDs ||
	      row->data().updated != updated ||
	      row->data().state != newState) {
	    complete(true);
	    return;
	  }
	  row->data().roleIDs = oldRoleIDs;
	  row->data().updated = oldUpdated;
	  row->data().state = oldState;
	  row->data().owner = 0;
	}
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(1, zum.issuer, Update) {
    context->issuers->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->issuers, 0, ZuFwdTuple(issuer),
	ZuMv(complete), [this](
	  ZdbRow<Issuer> *row, auto &&complete) mutable {
	if constexpr (Fwd) {
	  if (!row || row->data().authVersion != authVersion) {
	    complete(false);
	    return;
	  }
	  ++row->data().authVersion;
	} else {
	  if (!row || row->data().authVersion != authVersion + 1) {
	    complete(true);
	    return;
	  }
	  row->data().authVersion = authVersion;
	}
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(2, zum.user, Update) {
    context->users->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->users, 0, ZuFwdTuple(userID),
	ZuMv(complete), [this](
	  ZdbRow<User> *row, auto &&complete) mutable {
	if (!row || row->data().roleIDs != newRoleIDs ||
	    row->data().updated != updated || row->data().state != newState ||
	    row->data().owner != (Fwd ? saga->id() : uint128_t{0})) {
	  complete(!Fwd);
	  return;
	}
	row->data().owner = Fwd ? uint128_t{0} : saga->id();
	complete(row->commit());
      });
    });
    return {};
  }
};

ZfbStruct(ZumAPI, UserChange,
  (((issuer),		(Ctor<0>)),	(String)),
  (((userID),		(Ctor<1>)),	(UInt64)),
  (((oldRoleIDs),	(Ctor<2>)),	(UInt64Vec)),
  (((newRoleIDs),	(Ctor<3>)),	(UInt64Vec)),
  (((oldUpdated),	(Ctor<4>)),	(Int64)),
  (((updated),		(Ctor<5>)),	(Int64)),
  (((authVersion),	(Ctor<6>)),	(UInt64)),
  (((oldState),		(Ctor<7>, Enum<State::Map>)), (Int8)),
  (((newState),		(Ctor<8>, Enum<State::Map>)), (Int8)));

struct RoleChange : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"roleChange.v1">;
  enum { NSteps = 3 };

  String	issuer;
  RoleID	roleID = 0;
  String	name;
  ZtBitmap	oldActions;
  ZtBitmap	newActions;
  uint64_t	authVersion = 0;
  State::T	oldState = State::Pending;
  State::T	newState = State::Pending;

  ZdbSagaStep(0, zum.role, Update) {
    context->roles->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->roles, 0, ZuFwdTuple(roleID),
	ZuMv(complete), [this](
	  ZdbRow<Role> *row, auto &&complete) mutable {
	if constexpr (Fwd) {
	  if (!row || row->data().owner || row->data().name != name ||
	      row->data().actions != oldActions ||
	      row->data().state != oldState) {
	    complete(false);
	    return;
	  }
	  row->data().actions = newActions;
	  row->data().state = newState;
	  row->data().owner = saga->id();
	} else {
	  if (!row || row->data().owner != saga->id() ||
	      row->data().name != name || row->data().actions != newActions ||
	      row->data().state != newState) {
	    complete(true);
	    return;
	  }
	  row->data().actions = oldActions;
	  row->data().state = oldState;
	  row->data().owner = 0;
	}
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(1, zum.issuer, Update) {
    context->issuers->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->issuers, 0, ZuFwdTuple(issuer),
	ZuMv(complete), [this](
	  ZdbRow<Issuer> *row, auto &&complete) mutable {
	if constexpr (Fwd) {
	  if (!row || row->data().authVersion != authVersion) {
	    complete(false);
	    return;
	  }
	  ++row->data().authVersion;
	} else {
	  if (!row || row->data().authVersion != authVersion + 1) {
	    complete(true);
	    return;
	  }
	  row->data().authVersion = authVersion;
	}
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(2, zum.role, Update) {
    context->roles->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->roles, 0, ZuFwdTuple(roleID),
	ZuMv(complete), [this](
	  ZdbRow<Role> *row, auto &&complete) mutable {
	if (!row || row->data().name != name ||
	    row->data().actions != newActions || row->data().state != newState ||
	    row->data().owner != (Fwd ? saga->id() : uint128_t{0})) {
	  complete(!Fwd);
	  return;
	}
	row->data().owner = Fwd ? uint128_t{0} : saga->id();
	complete(row->commit());
      });
    });
    return {};
  }
};

ZfbStruct(ZumAPI, RoleChange,
  (((issuer),		(Ctor<0>)),	(String)),
  (((roleID),		(Ctor<1>)),	(UInt64)),
  (((name),		(Ctor<2>)),	(String)),
  (((oldActions),	(Ctor<3>)),	(UDT)),
  (((newActions),	(Ctor<4>)),	(UDT)),
  (((authVersion),	(Ctor<5>)),	(UInt64)),
  (((oldState),		(Ctor<6>, Enum<State::Map>)), (Int8)),
  (((newState),		(Ctor<7>, Enum<State::Map>)), (Int8)));

struct CredChange : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"credChange.v1">;
  enum { NSteps = 3 };

  String	issuer;
  Bytes		credentialID;
  int64_t	oldUpdated = 0;
  int64_t	updated = 0;
  uint64_t	authVersion = 0;
  State::T	oldState = State::Pending;
  State::T	newState = State::Pending;

  ZdbSagaStep(0, zum.cred, Update) {
    context->creds->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->creds, 0, ZuFwdTuple(credentialID),
	ZuMv(complete), [this](
	  ZdbRow<Cred> *row, auto &&complete) mutable {
	if constexpr (Fwd) {
	  if (!row || row->data().owner ||
	      row->data().updated != oldUpdated ||
	      row->data().state != oldState) {
	    complete(false);
	    return;
	  }
	  row->data().updated = updated;
	  row->data().state = newState;
	  row->data().owner = saga->id();
	} else {
	  if (!row || row->data().owner != saga->id() ||
	      row->data().updated != updated || row->data().state != newState) {
	    complete(true);
	    return;
	  }
	  row->data().updated = oldUpdated;
	  row->data().state = oldState;
	  row->data().owner = 0;
	}
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(1, zum.issuer, Update) {
    context->issuers->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->issuers, 0, ZuFwdTuple(issuer),
	ZuMv(complete), [this](
	  ZdbRow<Issuer> *row, auto &&complete) mutable {
	if constexpr (Fwd) {
	  if (!row || row->data().authVersion != authVersion) {
	    complete(false);
	    return;
	  }
	  ++row->data().authVersion;
	} else {
	  if (!row || row->data().authVersion != authVersion + 1) {
	    complete(true);
	    return;
	  }
	  row->data().authVersion = authVersion;
	}
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(2, zum.cred, Update) {
    context->creds->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->creds, 0, ZuFwdTuple(credentialID),
	ZuMv(complete), [this](
	  ZdbRow<Cred> *row, auto &&complete) mutable {
	if (!row || row->data().updated != updated ||
	    row->data().state != newState ||
	    row->data().owner != (Fwd ? saga->id() : uint128_t{0})) {
	  complete(!Fwd);
	  return;
	}
	row->data().owner = Fwd ? uint128_t{0} : saga->id();
	complete(row->commit());
      });
    });
    return {};
  }
};

ZfbStruct(ZumAPI, CredChange,
  (((issuer),		(Ctor<0>)),	(String)),
  (((credentialID),	(Ctor<1>)),	(Bytes)),
  (((oldUpdated),	(Ctor<2>)),	(Int64)),
  (((updated),		(Ctor<3>)),	(Int64)),
  (((authVersion),	(Ctor<4>)),	(UInt64)),
  (((oldState),		(Ctor<5>, Enum<State::Map>)), (Int8)),
  (((newState),		(Ctor<6>, Enum<State::Map>)), (Int8)));

struct ScopeChange : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"scopeChange.v1">;
  enum { NSteps = 3 };

  String	issuer;
  ScopeID	scopeID = 0;
  String	audience;
  String	name;
  IDVec		oldRoleIDs;
  IDVec		newRoleIDs;
  uint64_t	authVersion = 0;
  State::T	oldState = State::Pending;
  State::T	newState = State::Pending;

  ZdbSagaStep(0, zum.scope, Update) {
    context->scopes->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->scopes, 0, ZuFwdTuple(scopeID),
	ZuMv(complete), [this](
	  ZdbRow<Scope> *row, auto &&complete) mutable {
	if constexpr (Fwd) {
	  if (!row || row->data().owner || row->data().audience != audience ||
	      row->data().name != name || row->data().roleIDs != oldRoleIDs ||
	      row->data().state != oldState) {
	    complete(false);
	    return;
	  }
	  row->data().roleIDs = newRoleIDs;
	  row->data().state = newState;
	  row->data().owner = saga->id();
	} else {
	  if (!row || row->data().owner != saga->id() ||
	      row->data().audience != audience || row->data().name != name ||
	      row->data().roleIDs != newRoleIDs || row->data().state != newState) {
	    complete(true);
	    return;
	  }
	  row->data().roleIDs = oldRoleIDs;
	  row->data().state = oldState;
	  row->data().owner = 0;
	}
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(1, zum.issuer, Update) {
    context->issuers->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->issuers, 0, ZuFwdTuple(issuer),
	ZuMv(complete), [this](
	  ZdbRow<Issuer> *row, auto &&complete) mutable {
	if constexpr (Fwd) {
	  if (!row || row->data().authVersion != authVersion) {
	    complete(false);
	    return;
	  }
	  ++row->data().authVersion;
	} else {
	  if (!row || row->data().authVersion != authVersion + 1) {
	    complete(true);
	    return;
	  }
	  row->data().authVersion = authVersion;
	}
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(2, zum.scope, Update) {
    context->scopes->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->scopes, 0, ZuFwdTuple(scopeID),
	ZuMv(complete), [this](
	  ZdbRow<Scope> *row, auto &&complete) mutable {
	if (!row || row->data().audience != audience || row->data().name != name ||
	    row->data().roleIDs != newRoleIDs || row->data().state != newState ||
	    row->data().owner != (Fwd ? saga->id() : uint128_t{0})) {
	  complete(!Fwd);
	  return;
	}
	row->data().owner = Fwd ? uint128_t{0} : saga->id();
	complete(row->commit());
      });
    });
    return {};
  }
};

ZfbStruct(ZumAPI, ScopeChange,
  (((issuer),		(Ctor<0>)),	(String)),
  (((scopeID),		(Ctor<1>)),	(UInt64)),
  (((audience),		(Ctor<2>)),	(String)),
  (((name),		(Ctor<3>)),	(String)),
  (((oldRoleIDs),	(Ctor<4>)),	(UInt64Vec)),
  (((newRoleIDs),	(Ctor<5>)),	(UInt64Vec)),
  (((authVersion),	(Ctor<6>)),	(UInt64)),
  (((oldState),		(Ctor<7>, Enum<State::Map>)), (Int8)),
  (((newState),		(Ctor<8>, Enum<State::Map>)), (Int8)));

struct ClientChange : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"clientChange.v1">;
  enum { NSteps = 3 };

  String	issuer;
  String	clientID;
  Bytes		oldSecretDigest;
  Bytes		newSecretDigest;
  IDVec		oldRoleIDs;
  IDVec		newRoleIDs;
  int64_t	oldUpdated = 0;
  int64_t	updated = 0;
  uint64_t	authVersion = 0;
  State::T	oldState = State::Pending;
  State::T	newState = State::Pending;

  ZdbSagaStep(0, zum.client, Update) {
    context->clients->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->clients, 0, ZuFwdTuple(clientID),
	ZuMv(complete), [this](
	  ZdbRow<Client> *row, auto &&complete) mutable {
	if constexpr (Fwd) {
	  if (!row || row->data().owner ||
	      row->data().secretDigest != oldSecretDigest ||
	      row->data().roleIDs != oldRoleIDs ||
	      row->data().updated != oldUpdated || row->data().state != oldState) {
	    complete(false);
	    return;
	  }
	  row->data().secretDigest = newSecretDigest;
	  row->data().roleIDs = newRoleIDs;
	  row->data().updated = updated;
	  row->data().state = newState;
	  row->data().owner = saga->id();
	} else {
	  if (!row || row->data().owner != saga->id() ||
	      row->data().secretDigest != newSecretDigest ||
	      row->data().roleIDs != newRoleIDs ||
	      row->data().updated != updated || row->data().state != newState) {
	    complete(true);
	    return;
	  }
	  row->data().secretDigest = oldSecretDigest;
	  row->data().roleIDs = oldRoleIDs;
	  row->data().updated = oldUpdated;
	  row->data().state = oldState;
	  row->data().owner = 0;
	}
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(1, zum.issuer, Update) {
    context->issuers->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->issuers, 0, ZuFwdTuple(issuer),
	ZuMv(complete), [this](
	  ZdbRow<Issuer> *row, auto &&complete) mutable {
	if constexpr (Fwd) {
	  if (!row || row->data().authVersion != authVersion) {
	    complete(false);
	    return;
	  }
	  ++row->data().authVersion;
	} else {
	  if (!row || row->data().authVersion != authVersion + 1) {
	    complete(true);
	    return;
	  }
	  row->data().authVersion = authVersion;
	}
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(2, zum.client, Update) {
    context->clients->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->clients, 0, ZuFwdTuple(clientID),
	ZuMv(complete), [this](
	  ZdbRow<Client> *row, auto &&complete) mutable {
	if (!row || row->data().secretDigest != newSecretDigest ||
	    row->data().roleIDs != newRoleIDs || row->data().updated != updated ||
	    row->data().state != newState ||
	    row->data().owner != (Fwd ? saga->id() : uint128_t{0})) {
	  complete(!Fwd);
	  return;
	}
	row->data().owner = Fwd ? uint128_t{0} : saga->id();
	complete(row->commit());
      });
    });
    return {};
  }
};

ZfbStruct(ZumAPI, ClientChange,
  (((issuer),		(Ctor<0>)),	(String)),
  (((clientID),		(Ctor<1>)),	(String)),
  (((oldSecretDigest),	(Ctor<2>)),	(Bytes)),
  (((newSecretDigest),	(Ctor<3>)),	(Bytes)),
  (((oldRoleIDs),	(Ctor<4>)),	(UInt64Vec)),
  (((newRoleIDs),	(Ctor<5>)),	(UInt64Vec)),
  (((oldUpdated),	(Ctor<6>)),	(Int64)),
  (((updated),		(Ctor<7>)),	(Int64)),
  (((authVersion),	(Ctor<8>)),	(UInt64)),
  (((oldState),		(Ctor<9>, Enum<State::Map>)), (Int8)),
  (((newState),		(Ctor<10>, Enum<State::Map>)), (Int8)));

struct ActionChange : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"actionChange.v1">;
  enum { NSteps = 3 };

  String	issuer;
  ActionID	actionID = 0;
  String	name;
  uint64_t	authVersion = 0;
  State::T	oldState = State::Pending;
  State::T	newState = State::Pending;

  ZdbSagaStep(0, zum.action, Update) {
    context->actions->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->actions, 0, ZuFwdTuple(actionID),
	ZuMv(complete), [this](
	  ZdbRow<Action> *row, auto &&complete) mutable {
	if constexpr (Fwd) {
	  if (!row || row->data().owner || row->data().name != name ||
	      row->data().state != oldState) {
	    complete(false);
	    return;
	  }
	  row->data().state = newState;
	  row->data().owner = saga->id();
	} else {
	  if (!row || row->data().owner != saga->id() ||
	      row->data().name != name || row->data().state != newState) {
	    complete(true);
	    return;
	  }
	  row->data().state = oldState;
	  row->data().owner = 0;
	}
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(1, zum.issuer, Update) {
    context->issuers->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->issuers, 0, ZuFwdTuple(issuer),
	ZuMv(complete), [this](
	  ZdbRow<Issuer> *row, auto &&complete) mutable {
	if constexpr (Fwd) {
	  if (!row || row->data().authVersion != authVersion) {
	    complete(false);
	    return;
	  }
	  ++row->data().authVersion;
	} else {
	  if (!row || row->data().authVersion != authVersion + 1) {
	    complete(true);
	    return;
	  }
	  row->data().authVersion = authVersion;
	}
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(2, zum.action, Update) {
    context->actions->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->actions, 0, ZuFwdTuple(actionID),
	ZuMv(complete), [this](
	  ZdbRow<Action> *row, auto &&complete) mutable {
	if (!row || row->data().name != name || row->data().state != newState ||
	    row->data().owner != (Fwd ? saga->id() : uint128_t{0})) {
	  complete(!Fwd);
	  return;
	}
	row->data().owner = Fwd ? uint128_t{0} : saga->id();
	complete(row->commit());
      });
    });
    return {};
  }
};

ZfbStruct(ZumAPI, ActionChange,
  (((issuer),		(Ctor<0>)),	(String)),
  (((actionID),		(Ctor<1>)),	(UInt32)),
  (((name),		(Ctor<2>)),	(String)),
  (((authVersion),	(Ctor<3>)),	(UInt64)),
  (((oldState),		(Ctor<4>, Enum<State::Map>)), (Int8)),
  (((newState),		(Ctor<5>, Enum<State::Map>)), (Int8)));

struct SagaCatalog {
  using List = ZuTypeList<
    Enrollment, CredentialAdd, RecoveryStart, RecoveryEnroll, CodeFamily,
    UserChange, RoleChange, CredChange, ScopeChange, ClientChange, ActionChange>;
};
struct MSaga;
using MSagaBase = ZdbMSaga<SagaCatalog, MSaga>;
struct MSaga : public MSagaBase { ZuDerive_(MSaga, MSagaBase) };
ZuDerive(DB, (ZdbSagaDB<DBContext, SagaCatalog, SagaFn, MSaga>));

ZumExtern ZmRef<DBContext> registerSchema(DB *);

} // namespace Zum

#endif /* ZumDB_HH */
