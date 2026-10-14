//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_code_family.hh>
#include <zlib/zumd_saga_grant_delete.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/ZtlsSec.hh>
#include <zlib/ZmHeap.hh>

namespace Zum {

template <typename Heap = ZuVoid>
struct CodeFamilySaga_ : public Heap,
    public ZdbSaga<CodeFamilySaga_<Heap>, DB> {
  using Base = ZdbSaga<CodeFamilySaga_<Heap>, DB>;
  using Base::db;
  using Base::id;
  using Base::iteration;
  using Base::skip;
  using Base::insert;
  using Base::update;
  using Base::findUpd;
  using Base::findDel;
  using Base::result_;
  using Base::shard;
  using Type = ZuStringT<"codeFamily.v6">;
  enum { NPhases = 5 };

  Bytes		codeID;
  Bytes		codeDigest;
  Bytes		familyID;
  String	issuer;
  AppID		appID = 0;
  UserID	userID = 0;
  String	clientID;
  Bytes		credentialID;
  String	audience;
  IDVec		requestedRoleIDs;
  IDVec		roleIDs;
  ZtBitmap	actions;
  Bytes		digest;
  uint64_t	authVersion = 0;
  uint64_t	userVersion = 1;
  int64_t	authTime = 0;
  int64_t	created = 0;
  int64_t	expires = 0;
  String	scope;
  String	nonce;
  ProviderID	authorityProviderID = 0;
  uint64_t	policyVersion = 0;
  uint64_t	evidenceVersion = 0;
  UserSource::T authoritySource = UserSource::Local;

  Grant		beforeGrant;
  uint64_t	clientVersion = 0;
  uint64_t	assignmentVersion = 0;

  ZdbSagaPhase(0, zum.grant, Update) {
    db()->shardRun(0, [
      this, complete = ZuMv(complete)
    ]() mutable {
      this->template findUpd<0>(db()->grants, 0, ZuFwdTuple(codeID),
	ZuMv(complete), [this](ZdbRow<Grant> *row, auto &&complete) mutable {
	if (!row) { complete(false); return; }
	auto &code = row->data();
	if constexpr (Fwd) {
	if (beforeGrant.id != codeID || beforeGrant.state != State::Active ||
	    beforeGrant.owner || code.kind != GrantKind::Code ||
	    !Ztls::ctEqual(code.digest, codeDigest) || code.issuer != issuer ||
	    code.userID != userID || code.clientID != clientID ||
	    code.credentialID != credentialID || code.audience != audience ||
	    code.scope != beforeGrant.scope ||
	    code.requestedRoleIDs != beforeGrant.requestedRoleIDs ||
	    code.actions != beforeGrant.actions || code.nonce != nonce ||
	    code.appID != appID ||
	    code.authorityProviderID != authorityProviderID ||
	    code.policyVersion != policyVersion ||
	    code.evidenceVersion != evidenceVersion ||
	    code.authoritySource != authoritySource || code.roleIDs != roleIDs ||
	    code.userVersion != userVersion || code.expires <= created ||
	    code.state != State::Active || code.owner) {
	  complete(false);
	  return;
	}
	}
	code.state = Fwd ? State::Consumed : beforeGrant.state;
	code.owner = Fwd ? id() : beforeGrant.owner;
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaPhase(1, zum.refresh, Insert) {
    db()->shardRun(0, [
      this, complete = ZuMv(complete)
    ]() mutable {
      if constexpr (Fwd) {
        db()->refresh->template find<0>(0, ZuFwdTuple(familyID), [
	  this, complete = ZuMv(complete)
	](ZdbRowRef<Refresh> existing) mutable {
        ZdbRowRef<Refresh> row =
          new ZdbRow<Refresh>{db()->refresh, ZdbShard{0}};
        insert(db()->refresh, ZuMv(row), ZuMv(complete),
          [this, duplicate = bool(existing)](ZdbRow<Refresh> *row, auto &&complete) mutable {
          new (row->ptr()) Refresh{
	    .id = familyID,
	    .owner = id(),
            .authVersion = authVersion,
            .userVersion = userVersion,
            .clientVersion = clientVersion,
            .assignmentVersion = assignmentVersion,
	    .policyVersion = policyVersion,
	    .evidenceVersion = evidenceVersion,
	    .appID = appID,
	    .userID = userID,
	    .created = created,
	    .expires = expires,
            .state = State::Pending,
	    .issuer = issuer,
	    .clientID = clientID,
	    .audience = audience,
	    .authoritySource = authoritySource,
	    .authTime = authTime,
	    .requestedRoleIDs = requestedRoleIDs,
	    .roleIDs = roleIDs,
	    .actions = actions,
	    .credentialID = credentialID,
	    .digest = digest,
	    .scope = scope,
	    .nonce = nonce,
	    .authorityProviderID = authorityProviderID,
	    .updated = created
	  };
	  if (duplicate) { complete(false); return; }
	  complete(row->commit());
	});
	});
      } else {
        this->template findDel<0>(db()->refresh, 0, ZuFwdTuple(familyID),
	  ZuMv(complete), [](ZdbRow<Refresh> *row, auto &&complete) mutable {
	  complete(!row || row->commit());
	});
      }
    });
    return {};
  }

  ZdbSagaPhase(2, zum.refresh, Update) {
    db()->shardRun(0, [
      this, complete = ZuMv(complete)
    ]() mutable {
      db()->apps->template find<0>(0, ZuFwdTuple(appID), [
	this, complete = ZuMv(complete)
      ](ZdbRowRef<App> app) mutable {
	if constexpr (Fwd)
	  if (!appID || !app || app->data().state != State::Active ||
	      app->data().owner || app->data().authVersion != authVersion) {
	    complete(false);
	    return;
	  }
	db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
	  db()->users->template find<0>(0, ZuFwdTuple(userID), [
	    this, complete = ZuMv(complete)
	  ](ZdbRowRef<User> user) mutable {
	    if constexpr (Fwd)
	      if (!user || user->data().state != State::Active ||
		  user->data().authVersion != userVersion) {
		complete(false);
		return;
	      }
	    db()->shardRun(0, [
	      this, complete = ZuMv(complete)
	    ]() mutable {
	      this->template findUpd<0>(db()->refresh, 0, ZuFwdTuple(familyID),
		ZuMv(complete), [this](
		  ZdbRow<Refresh> *row, auto &&complete) mutable {
		if (!row) { complete(false); return; }
		auto &family = row->data();
		if (Fwd && (family.owner != id() || family.state != State::Pending)) {
		  complete(false); return;
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

  ZdbSagaPhase(3, zum.refresh, Update) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(db()->refresh, 0, ZuFwdTuple(familyID),
	ZuMv(complete), [this](
	  ZdbRow<Refresh> *row, auto &&complete) mutable {
	if (!row) { complete(false); return; }
	if constexpr (Fwd) {
	  if (row->data().state != State::Active || row->data().owner != id()) {
	    complete(false); return;
	  }
	}
	row->data().owner = Fwd ? uint128_t{0} : id();
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaPhase(4, zum.grant, Delete) {
    grantDelete<Fwd>(this, codeID, ZuMv(complete)); return {};
  }

  CodeFamilySaga_() = default;
  CodeFamilySaga_(
      Bytes codeID_, Bytes codeDigest_,
      Bytes familyID_, String issuer_,
      AppID appID_, UserID userID_,
      String clientID_, Bytes credentialID_,
      String audience_, IDVec requestedRoleIDs_,
      IDVec roleIDs_, ZtBitmap actions_,
      Bytes digest_, uint64_t authVersion_,
      uint64_t userVersion_, int64_t authTime_,
      int64_t created_, int64_t expires_,
      String scope_, String nonce_,
      ProviderID authorityProviderID_, uint64_t policyVersion_,
      uint64_t evidenceVersion_, UserSource::T authoritySource_,
      Grant beforeGrant_, uint64_t clientVersion_,
      uint64_t assignmentVersion_) :
    codeID{ZuMv(codeID_)},
    codeDigest{ZuMv(codeDigest_)},
    familyID{ZuMv(familyID_)},
    issuer{ZuMv(issuer_)},
    appID{ZuMv(appID_)},
    userID{ZuMv(userID_)},
    clientID{ZuMv(clientID_)},
    credentialID{ZuMv(credentialID_)},
    audience{ZuMv(audience_)},
    requestedRoleIDs{ZuMv(requestedRoleIDs_)},
    roleIDs{ZuMv(roleIDs_)},
    actions{ZuMv(actions_)},
    digest{ZuMv(digest_)},
    authVersion{ZuMv(authVersion_)},
    userVersion{ZuMv(userVersion_)},
    authTime{ZuMv(authTime_)},
    created{ZuMv(created_)},
    expires{ZuMv(expires_)},
    scope{ZuMv(scope_)},
    nonce{ZuMv(nonce_)},
    authorityProviderID{ZuMv(authorityProviderID_)},
    policyVersion{ZuMv(policyVersion_)},
    evidenceVersion{ZuMv(evidenceVersion_)},
    authoritySource{ZuMv(authoritySource_)},
    beforeGrant{ZuMv(beforeGrant_)},
    clientVersion{ZuMv(clientVersion_)},
    assignmentVersion{ZuMv(assignmentVersion_)} { }
  CodeFamilySaga_(CodeFamilyArgs args) :
    CodeFamilySaga_{
      ZuMv(args.codeID), ZuMv(args.codeDigest), ZuMv(args.familyID),
      ZuMv(args.issuer), ZuMv(args.appID), ZuMv(args.userID),
      ZuMv(args.clientID), ZuMv(args.credentialID), ZuMv(args.audience),
      ZuMv(args.requestedRoleIDs), ZuMv(args.roleIDs), ZuMv(args.actions),
      ZuMv(args.digest), ZuMv(args.authVersion), ZuMv(args.userVersion),
      ZuMv(args.authTime), ZuMv(args.created), ZuMv(args.expires),
      ZuMv(args.scope), ZuMv(args.nonce), ZuMv(args.authorityProviderID),
      ZuMv(args.policyVersion), ZuMv(args.evidenceVersion), ZuMv(args.authoritySource),
      ZuMv(args.beforeGrant), ZuMv(args.clientVersion), ZuMv(args.assignmentVersion)} { }
  ~CodeFamilySaga_() override;
};

using CodeFamilySaga = CodeFamilySaga_<
  ZmHeap<"Zum.CodeFamilySaga", CodeFamilySaga_<>>>;

ZfStruct(, (CodeFamilySaga, FB),
  (codeID,		(Ctor<0>),				Bytes),
  (codeDigest,		(Ctor<1>),				Bytes),
  (familyID,		(Ctor<2>),				Bytes),
  (issuer,		(Ctor<3>),				String),
  (appID,		(Ctor<4>),				UInt64),
  (userID,		(Ctor<5>),				UInt64),
  (clientID,		(Ctor<6>),				String),
  (credentialID,	(Ctor<7>),				Bytes),
  (audience,		(Ctor<8>),				String),
  (requestedRoleIDs,	(Ctor<9>),				UInt64Vec),
  (roleIDs,		(Ctor<10>),				UInt64Vec),
  (actions,		(Ctor<11>),				UDT),
  (digest,		(Ctor<12>),				Bytes),
  (authVersion,		(Ctor<13>),				UInt64),
  (userVersion,		(Ctor<14>, Deflt<1>),			UInt64),
  (authTime,		(Ctor<15>),				Int64),
  (created,		(Ctor<16>),				Int64),
  (expires,		(Ctor<17>),				Int64),
  (scope,		(Ctor<18>),				String),
  (nonce,		(Ctor<19>),				String),
  (authorityProviderID,	(Ctor<20>),				UInt64),
  (policyVersion,	(Ctor<21>),				UInt64),
  (evidenceVersion,	(Ctor<22>),				UInt64),
  (authoritySource,	(Ctor<23>, Enum<UserSource::Map>),	Int8),
  (beforeGrant,		(Ctor<24>),				UDT),
  (clientVersion,	(Ctor<25>),				UInt64),
  (assignmentVersion,	(Ctor<26>),				UInt64));
ZiFB::AsTable<"Zum.fbs.CodeFamily"> ZiFB_Fmt(CodeFamilySaga *);

ZuAssert((Zdb_::SagaDefValid<CodeFamilySaga>{}));

template <typename Heap>
CodeFamilySaga_<Heap>::~CodeFamilySaga_() = default;

SagaRef codeFamily(ZuBSpan data) {
  return CodeFamilySaga::load(data);
}

Zdb_::SagaDef codeFamilyDef() {
  return CodeFamilySaga::def();
}
SagaRef codeFamily(CodeFamilyArgs args) {
  return new CodeFamilySaga{ZuMv(args)};
}

bool codeFamilyPrepare(
    Ztls::Random &rng, const Grant &code, ZuBSpan codeDigest,
    String scope, IDVec requestedRoleIDs, ZtBitmap actions, uint64_t authVersion,
    int64_t now, int64_t expires, CodeFamilyArgs &family, String &refreshToken)
{
  OpaqueToken refresh;
  if (!opaqueIssue(rng, refresh)) return false;
  CodeFamilyArgs next;
  next.codeID = code.id;
  next.codeDigest = codeDigest;
  next.familyID = ZuMv(refresh.id);
  next.issuer = code.issuer;
  next.appID = code.appID;
  next.authorityProviderID = code.authorityProviderID;
  next.userID = code.userID;
  next.clientID = code.clientID;
  next.credentialID = code.credentialID;
  next.audience = code.audience;
  next.scope = ZuMv(scope);
  next.nonce = code.nonce;
  next.requestedRoleIDs = ZuMv(requestedRoleIDs);
  next.roleIDs = code.roleIDs;
  next.actions = ZuMv(actions);
  next.digest = ZuMv(refresh.digest);
  next.authVersion = authVersion;
  next.userVersion = code.userVersion;
  next.clientVersion = code.clientVersion;
  next.assignmentVersion = code.assignmentVersion;
  next.policyVersion = code.policyVersion;
  next.evidenceVersion = code.evidenceVersion;
  next.authoritySource = code.authoritySource;
  next.authTime = code.authTime;
  next.created = now;
  next.expires = expires;
  next.beforeGrant = code;
  family = ZuMv(next);
  refreshToken = ZuMv(refresh.token);
  return true;
}

} // namespace Zum
