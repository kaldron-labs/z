//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_app_enrollment.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/ZtlsSec.hh>
#include <zlib/ZmHeap.hh>

namespace Zum {

struct AppEnrollmentSaga : public ZmNHeap<AppEnrollmentSaga>,
    public ZdbSaga<AppEnrollmentSaga, DB> {
friend inline ZuStringT<"Zum.AppEnrollmentSaga"> ZmNHeapID(AppEnrollmentSaga *);

  using Base = ZdbSaga<AppEnrollmentSaga, DB>;
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
  using Type = ZuStringT<"appEnrollment.v6">;
  enum { NPhases = 12 };

  AppID		appID = 0;
  String	appName;
  String	appLabel;
  String	audience;
  SignKey	signKey;
  String	clientID;
  Bytes		secretDigest;
  int64_t	created = 0;
  ActionID	catalogPublishOp = 0;
  ActionID	operationQueryOp = 0;
  IdemRequest	request;

  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }

  ZdbSagaPhase(1, zum.app, Insert) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	ZdbRowRef<App> row = new ZdbRow<App>{db()->apps, ZdbShard{0}};
	insert(db()->apps, ZuMv(row), ZuMv(complete),
	  [this](ZdbRow<App> *row, auto &&complete) mutable {
	    new (row->ptr()) App{.id = appID, .name = appName,
	      .label = appLabel, .state = State::Pending, .authVersion = 1,
	      .version = 1, .created = created, .updated = created,
	      .owner = id(), .audience = audience};
	    complete(row->commit());
	  });
      } else {
	this->template findDel<0>(db()->apps, 0, ZuFwdTuple(appID),
	  ZuMv(complete), [this](ZdbRow<App> *row, auto &&complete) mutable {
	    if (!row || row->data().owner != id() ||
		row->data().state != State::Pending) { complete(true); return; }
	    complete(row->commit());
	  });
      }
    });
    return {};
  }

  ZdbSagaPhase(2, zum.sign_key, Insert) {
    if (!signKey.id) { skip(ZuMv(complete)); return {}; }
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	ZdbRowRef<SignKey> row =
	  new ZdbRow<SignKey>{db()->signKeys, ZdbShard{0}};
	insert(db()->signKeys, ZuMv(row), ZuMv(complete),
	  [this](ZdbRow<SignKey> *row, auto &&complete) mutable {
	    new (row->ptr()) SignKey{signKey};
	    row->data().owner = id();
	    complete(row->commit());
	  });
      } else {
	this->template findDel<0>(db()->signKeys, 0, ZuFwdTuple(signKey.id),
	  ZuMv(complete), [this](ZdbRow<SignKey> *row,
	      auto &&complete) mutable {
	    if (!row || row->data().owner != id()) {
	      complete(true); return;
	    }
	    complete(row->commit());
	  });
      }
    });
    return {};
  }

  ZdbSagaPhase(3, zum.client, Insert) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	ZdbRowRef<Client> row =
	  new ZdbRow<Client>{db()->clients, ZdbShard{0}};
	insert(db()->clients, ZuMv(row), ZuMv(complete),
	  [this](ZdbRow<Client> *row, auto &&complete) mutable {
	    Client client{.id = clientID,
	      .appID = appID,
	      .label = "default service client",
	      .secretDigest = secretDigest, .secretVersion = 1,
	      .created = created, .updated = created,
	      .profile = ClientProfile::Server,
	      .grants = uint8_t(ClientGrant::ClientCredentials()),
	      .refreshAllowed = false, .state = State::Active,
	      .version = 1, .owner = id()};
	    new (row->ptr()) Client{ZuMv(client)};
	    complete(row->commit());
	  });
      } else {
	this->template findDel<0>(db()->clients, 0, ZuFwdTuple(clientID),
	  ZuMv(complete), [this](ZdbRow<Client> *row,
	      auto &&complete) mutable {
	    if (!row || row->data().owner != id()) {
	      complete(true); return;
	    }
	    complete(row->commit());
	  });
      }
    });
    return {};
  }

  ZdbSagaPhase(4, zum.admin_access, Insert) {
    if (!catalogPublishOp || !operationQueryOp ||
	catalogPublishOp == operationQueryOp) {
      complete(false);
      return {};
    }
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	ActionIDVec operations;
	operations.push(catalogPublishOp);
	operations.push(operationQueryOp);
	ZdbRowRef<AdminAccess> row =
	  new ZdbRow<AdminAccess>{db()->adminAccess, ZdbShard{0}};
	insert(db()->adminAccess, ZuMv(row), ZuMv(complete),
	  [this, operations = ZuMv(operations)](
	      ZdbRow<AdminAccess> *row, auto &&complete) mutable {
	    new (row->ptr()) AdminAccess{.actorKind = ActorKind::Client,
	      .actorID = clientID, .appID = appID,
	      .operationIDs = ZuMv(operations), .state = State::Active,
	      .version = 1, .created = created, .updated = created,
	      .owner = id()};
	    complete(row->commit());
	  });
      } else {
	this->template findDel<0>(db()->adminAccess, 0,
	  ZuFwdTuple(ActorKind::Client, clientID, appID), ZuMv(complete),
	  [this](ZdbRow<AdminAccess> *row, auto &&complete) mutable {
	    if (!row || row->data().owner != id()) {
	      complete(true); return;
	    }
	    complete(row->commit());
	  });
      }
    });
    return {};
  }

  ZdbSagaPhase(5, zum.auth_policy, Insert) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	ZdbRowRef<AuthPolicy> row =
	  new ZdbRow<AuthPolicy>{db()->authPolicies, ZdbShard{0}};
	insert(db()->authPolicies, ZuMv(row), ZuMv(complete),
	  [this](ZdbRow<AuthPolicy> *row, auto &&complete) mutable {
	    new (row->ptr()) AuthPolicy{.appID = appID, .localFirst = true,
	      .assignmentMaxAge = 300, .sessionIdle = 1800,
	      .sessionAbsolute = 43200, .tokenLifetime = 300,
	      .consentPolicy = ConsentPolicy::Explicit, .state = State::Active,
	      .version = 1, .created = created, .updated = created,
	      .owner = id()};
	    complete(row->commit());
	  });
      } else {
	this->template findDel<0>(db()->authPolicies, 0, ZuFwdTuple(appID),
	  ZuMv(complete), [this](ZdbRow<AuthPolicy> *row,
	      auto &&complete) mutable {
	    if (!row || row->data().owner != id()) {
	      complete(true); return;
	    }
	    complete(row->commit());
	  });
      }
    });
    return {};
  }

#define ZUM_APP_ENROLL_RELEASE(N, member, tableName, keyExpr, optionalExpr) \
  ZdbSagaPhase(N, zum.member, Update) { \
    if (optionalExpr) { skip(ZuMv(complete)); return {}; } \
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable { \
      this->template findUpd<0>(db()->tableName, 0, keyExpr, ZuMv(complete), \
	[this](auto *row, auto &&complete) mutable { \
	  if (!row || row->data().owner != \
	      (Fwd ? id() : uint128_t{0})) { \
	    complete(!Fwd); return; \
	  } \
	  row->data().owner = Fwd ? uint128_t{0} : id(); \
	  complete(row->commit()); \
	}); \
    }); \
    return {}; \
  }

  ZUM_APP_ENROLL_RELEASE(6, sign_key, signKeys,
    ZuFwdTuple(signKey.id), !signKey.id)
  ZUM_APP_ENROLL_RELEASE(7, client, clients,
    ZuFwdTuple(clientID), false)
  ZUM_APP_ENROLL_RELEASE(8, admin_access, adminAccess,
    ZuFwdTuple(ActorKind::Client, clientID, appID), false)
  ZUM_APP_ENROLL_RELEASE(9, auth_policy, authPolicies,
    ZuFwdTuple(appID), false)

#undef ZUM_APP_ENROLL_RELEASE

  ZdbSagaPhase(10, zum.app, Update) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(db()->apps, 0, ZuFwdTuple(appID),
	ZuMv(complete), [this](ZdbRow<App> *row,
	    auto &&complete) mutable {
	if (!row || row->data().state !=
	    (Fwd ? State::Pending : State::Active) ||
	    row->data().owner != (Fwd ? id() : uint128_t{0})) {
	  complete(!Fwd); return;
	}
	row->data().state = Fwd ? State::Active : State::Pending;
	row->data().version = Fwd ? 2 : 1;
	row->data().owner = Fwd ? uint128_t{0} : id();
	complete(row->commit());
      });
    });
    return {};
  }
  ZdbSagaPhase(11, zum.request, Update) {
    StringVec ids;
    ids.push(String{} << appID);
    ids.push(clientID);
    requestComplete(this, ZuMv(ids), created, ZuMv(complete));
    return {};
  }

  AppEnrollmentSaga() = default;
  AppEnrollmentSaga(AppID appID_, String appName_, String appLabel_,
      String audience_, SignKey signKey_, String clientID_, Bytes secretDigest_,
      int64_t created_, ActionID catalogPublishOp_, ActionID operationQueryOp_,
      IdemRequest request_) :
    appID{ZuMv(appID_)},
    appName{ZuMv(appName_)},
    appLabel{ZuMv(appLabel_)},
    audience{ZuMv(audience_)},
    signKey{ZuMv(signKey_)},
    clientID{ZuMv(clientID_)},
    secretDigest{ZuMv(secretDigest_)},
    created{ZuMv(created_)},
    catalogPublishOp{ZuMv(catalogPublishOp_)},
    operationQueryOp{ZuMv(operationQueryOp_)},
    request{ZuMv(request_)} { }
  AppEnrollmentSaga(AppEnrollmentArgs args) :
    AppEnrollmentSaga{ZuMv(args.appID), ZuMv(args.appName),
      ZuMv(args.appLabel), ZuMv(args.audience), ZuMv(args.signKey),
      ZuMv(args.clientID), ZuMv(args.secretDigest), ZuMv(args.created),
      ZuMv(args.catalogPublishOp), ZuMv(args.operationQueryOp),
      ZuMv(args.request)} { }
  ~AppEnrollmentSaga() override;
};

ZfStruct(, (AppEnrollmentSaga, FB),
  (appID,		(Ctor<0>),	UInt64),
  (appName,		(Ctor<1>),	String),
  (appLabel,		(Ctor<2>),	String),
  (audience,		(Ctor<3>),	String),
  (signKey,		(Ctor<4>),	UDT),
  (clientID,		(Ctor<5>),	String),
  (secretDigest,	(Ctor<6>),	Bytes),
  (created,		(Ctor<7>),	Int64),
  (catalogPublishOp,	(Ctor<8>),	UInt32),
  (operationQueryOp,	(Ctor<9>),	UInt32),
  (request,		(Ctor<10>),	UDT));
Zfb::AsTable<"Zum.fbs.AppEnrollment"> Zfb_Fmt(AppEnrollmentSaga *);

ZuAssert((Zdb_::SagaDefValid<AppEnrollmentSaga>{}));

AppEnrollmentSaga::~AppEnrollmentSaga() = default;

SagaRef appEnrollment(ZuBSpan data) {
  return AppEnrollmentSaga::load(data);
}

Zdb_::SagaDef appEnrollmentDef() {
  return AppEnrollmentSaga::def();
}
SagaRef appEnrollment(AppEnrollmentArgs args) {
  return new AppEnrollmentSaga{ZuMv(args)};
}

} // namespace Zum
