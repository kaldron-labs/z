//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_recovery_start.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/ZtlsSec.hh>
#include <zlib/ZmHeap.hh>

namespace Zum {

template <typename Heap = ZuVoid>
struct RecoveryStartSaga_ : public Heap,
    public ZdbSaga<RecoveryStartSaga_<Heap>, DB> {
  using Base = ZdbSaga<RecoveryStartSaga_<Heap>, DB>;
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
  using Type = ZuStringT<"recoveryStart.v3">;
  enum { NPhases = 6 };

  Bytes		capabilityID;
  Bytes		digest;
  String	issuer;
  UserID	userID = 0;
  uint64_t	userVersion = 0;
  int64_t	created = 0;
  int64_t	expires = 0;
  String	actor;
  uint64_t	version = 0;
  State::T oldState = State::Active;
  int64_t oldUpdated = 0;
  IdemRequest request;

  void validate(Zdb_::SagaCompleteFn);

  ZdbSagaPhase(0, zum.request, Insert) {
    if constexpr (Fwd) {
      validate([this, complete = ZuMv(complete)](bool ok) mutable {
	if (!ok) { complete(false); return; }
	requestInsert<Fwd>(this, ZuMv(complete));
      });
    } else requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(1, zum.user, Update) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(db()->users, 0, ZuFwdTuple(userID),
	ZuMv(complete), [this](
	  ZdbRow<User> *row, auto &&complete) mutable {
	if constexpr (Fwd) {
	  if (!row || row->data().owner ||
	      (row->data().state != State::Active &&
	       row->data().state != State::Suspended) ||
	      !userVersion || row->data().authVersion == UINT64_MAX ||
	      row->data().authVersion + 1 != userVersion ||
	      row->data().state != oldState || row->data().updated != oldUpdated ||
	      row->data().source != UserSource::Local ||
	      !version || version == UINT64_MAX ||
	      row->data().version != version) {
	    complete(false);
	    return;
	  }
	  row->data().state = State::Suspended;
	  row->data().authVersion = userVersion;
	  row->data().version = version + 1;
	  row->data().updated = created;
	  row->data().owner = id();
	} else {
	  row->data().state = oldState;
	  row->data().authVersion = userVersion - 1;
	  row->data().version = version;
	  row->data().updated = oldUpdated;
	  row->data().owner = 0;
	}
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaPhase(2, zum.grant, Insert) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	db()->grants->template find<0>(0, ZuFwdTuple(capabilityID),
	  [this, complete = ZuMv(complete)](ZdbRowRef<Grant> existing) mutable {
	    db()->shardRun(0, [this, existing = ZuMv(existing),
		complete = ZuMv(complete)]() mutable {
	      db()->refresh->template find<0>(0, ZuFwdTuple(capabilityID),
		[this, existing = ZuMv(existing), complete = ZuMv(complete)](
		    ZdbRowRef<Refresh> refresh) mutable {
		if (refresh) { complete(false); return; }
	    ZdbRowRef<Grant> row =
	      new ZdbRow<Grant>{db()->grants, ZdbShard{0}};
	    insert(db()->grants, ZuMv(row), ZuMv(complete),
	      [this, duplicate = bool(existing)](ZdbRow<Grant> *row, auto &&complete) mutable {
		new (row->ptr()) Grant{
		  .id = capabilityID,
		  .owner = id(),
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
		if (duplicate) { complete(false); return; }
		complete(row->commit());
	      });
	      });
	    });
	  });
      } else {
	this->template findDel<0>(db()->grants, 0, ZuFwdTuple(capabilityID),
	  ZuMv(complete), [](
	    ZdbRow<Grant> *row, auto &&complete) mutable {
	  complete(!row || row->commit());
	});
      }
    });
    return {};
  }

  ZdbSagaPhase(3, zum.user, Update) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(db()->users, 0, ZuFwdTuple(userID),
	ZuMv(complete), [this](
	  ZdbRow<User> *row, auto &&complete) mutable {
	if (!row) { complete(!Fwd); return; }
	row->data().owner = Fwd ? uint128_t{0} : id();
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaPhase(4, zum.grant, Update) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(db()->grants, 0, ZuFwdTuple(capabilityID),
	ZuMv(complete), [this](
	  ZdbRow<Grant> *row, auto &&complete) mutable {
	if (!row) { complete(!Fwd); return; }
	row->data().state = Fwd ? State::Active : State::Pending;
	row->data().owner = Fwd ? uint128_t{0} : id();
	complete(row->commit());
      });
    });
    return {};
  }
  ZdbSagaPhase(5, zum.request, Update) {
    requestComplete(this, {}, created, ZuMv(complete));
    return {};
  }

  RecoveryStartSaga_() = default;
  RecoveryStartSaga_(
      Bytes capabilityID_, Bytes digest_,
      String issuer_, UserID userID_,
      uint64_t userVersion_, int64_t created_,
      int64_t expires_, String actor_,
      uint64_t version_, State::T oldState_,
      int64_t oldUpdated_, IdemRequest request_) :
    capabilityID{ZuMv(capabilityID_)},
    digest{ZuMv(digest_)},
    issuer{ZuMv(issuer_)},
    userID{ZuMv(userID_)},
    userVersion{ZuMv(userVersion_)},
    created{ZuMv(created_)},
    expires{ZuMv(expires_)},
    actor{ZuMv(actor_)},
    version{ZuMv(version_)},
    oldState{ZuMv(oldState_)},
    oldUpdated{ZuMv(oldUpdated_)},
    request{ZuMv(request_)} { }
  RecoveryStartSaga_(RecoveryStartArgs args) :
    RecoveryStartSaga_{
      ZuMv(args.capabilityID), ZuMv(args.digest), ZuMv(args.issuer),
      ZuMv(args.userID), ZuMv(args.userVersion), ZuMv(args.created),
      ZuMv(args.expires), ZuMv(args.actor), ZuMv(args.version),
      ZuMv(args.oldState), ZuMv(args.oldUpdated), ZuMv(args.request)} { }
  ~RecoveryStartSaga_() override;
};

using RecoveryStartSaga = RecoveryStartSaga_<
  ZmHeap<"Zum.RecoveryStartSaga", RecoveryStartSaga_<>>>;

ZfStruct(, (RecoveryStartSaga, FB),
  (capabilityID,	(Ctor<0>),			Bytes),
  (digest,		(Ctor<1>),			Bytes),
  (issuer,		(Ctor<2>),			String),
  (userID,		(Ctor<3>),			UInt64),
  (userVersion,		(Ctor<4>),			UInt64),
  (created,		(Ctor<5>),			Int64),
  (expires,		(Ctor<6>),			Int64),
  (actor,		(Ctor<7>),			String),
  (version,		(Ctor<8>),			UInt64),
  (oldState,		(Ctor<9>, Enum<State::Map>),	Int8),
  (oldUpdated,		(Ctor<10>),			Int64),
  (request,		(Ctor<11>),			UDT));
Zfb::AsTable<"Zum.fbs.RecoveryStart"> Zfb_Fmt(RecoveryStartSaga *);

ZuAssert((Zdb_::SagaDefValid<RecoveryStartSaga>{}));

template <typename Heap>
RecoveryStartSaga_<Heap>::~RecoveryStartSaga_() = default;

SagaRef recoveryStart(ZuBSpan data) {
  return RecoveryStartSaga::load(data);
}

Zdb_::SagaDef recoveryStartDef() {
  return RecoveryStartSaga::def();
}
SagaRef recoveryStart(RecoveryStartArgs args) {
  return new RecoveryStartSaga{ZuMv(args)};
}

template <typename Heap>
void RecoveryStartSaga_<Heap>::validate(Zdb_::SagaCompleteFn complete)
{
  if (!issuer || !actor || !userID || !capabilityID || !digest || created <= 0 ||
      expires <= created || !version || version == UINT64_MAX || !userVersion) {
    complete(false); return;
  }
  complete(true);
}

} // namespace Zum
