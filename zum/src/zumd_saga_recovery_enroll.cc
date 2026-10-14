//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_recovery_enroll.hh>
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
struct RecoveryEnrollSaga_ : public Heap,
    public ZdbSaga<RecoveryEnrollSaga_<Heap>, DB> {
  using Base = ZdbSaga<RecoveryEnrollSaga_<Heap>, DB>;
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
  using Type = ZuStringT<"recoveryEnroll.v2">;
  enum { NPhases = 7 };

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
  Grant		beforeGrant;
  User		beforeUser;

  template <bool Fwd, typename Complete>
  void consume(Complete complete)
  {
    auto table = db()->grants;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(table, 0, ZuFwdTuple(ceremonyID), ZuMv(complete),
	[this](ZdbRow<Grant> *row, auto &&complete) mutable {
	  if (!row) { complete(false); return; }
	  auto &grant = row->data();
	  if constexpr (Fwd) {
	    if (beforeGrant.id != ceremonyID || beforeGrant.state != State::Active ||
		beforeGrant.owner || grant.kind != GrantKind::Ceremony ||
		grant.purpose != GrantPurpose::Recovery ||
		grant.state != State::Active || grant.owner ||
		grant.issuer != issuer || grant.userID != userID ||
		grant.userVersion != userVersion || grant.userHandle != newHandle ||
		grant.expires <= created ||
		grant.challenge != beforeGrant.challenge ||
		grant.bindingDigest != beforeGrant.bindingDigest) {
	      complete(false); return;
	    }
	  }
	  grant.state = Fwd ? State::Consumed : beforeGrant.state;
	  grant.owner = Fwd ? id() : beforeGrant.owner;
	  complete(row->commit());
	});
    });
  }

  ZdbSagaPhase(0, zum.grant, Update) {
    if constexpr (!Fwd) {
      consume<Fwd>(ZuMv(complete));
    } else {
      db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
	db()->users->template find<0>(0, ZuFwdTuple(userID), [
	  this, complete = ZuMv(complete)](ZdbRowRef<User> user) mutable {
	  if (!user || user->data().owner || user->data().state != State::Suspended ||
	      user->data().handle != oldHandle || user->data().authVersion != userVersion) {
	    complete(false); return;
	  }
	  consume<Fwd>(ZuMv(complete));
	});
      });
    }
    return {};
  }

  ZdbSagaPhase(1, zum.cred, Insert) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	ZdbRowRef<Cred> row =
	  new ZdbRow<Cred>{db()->creds, ZdbShard{0}};
	db()->creds->template find<0>(0, ZuFwdTuple(credentialID), [
	  this, row = ZuMv(row), complete = ZuMv(complete)
	](ZdbRowRef<Cred> existing) mutable {
	insert(db()->creds, ZuMv(row), ZuMv(complete),
	  [this, duplicate = bool(existing)](ZdbRow<Cred> *row, auto &&complete) mutable {
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
	    .owner = id(),
	    .userVersion = userVersion
	  };
	  if (duplicate) { complete(false); return; }
	  complete(row->commit());
	});
	});
      } else {
	this->template findDel<0>(db()->creds, 0, ZuFwdTuple(credentialID),
	  ZuMv(complete), [this](
	    ZdbRow<Cred> *row, auto &&complete) mutable {
	  if (!row) { complete(true); return; }
	  if (row->data().owner != id() ||
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

  ZdbSagaPhase(2, zum.cred, Update) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(db()->creds, 0, ZuFwdTuple(credentialID),
	ZuMv(complete), [this](
	  ZdbRow<Cred> *row, auto &&complete) mutable {
	if (!row) { complete(false); return; }
	if (Fwd && (row->data().owner != id() ||
	    row->data().state != State::Pending)) {
	  complete(false); return;
	}
	row->data().state = Fwd ? State::Active : State::Pending;
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaPhase(3, zum.user, Update) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0, ZuSeq<1>>(db()->users, 0, ZuFwdTuple(userID),
	ZuMv(complete), [this](
	  ZdbRow<User> *row, auto &&complete) mutable {
	if (!row) { complete(false); return; }
	if constexpr (Fwd) {
	  if (beforeUser.id != userID || beforeUser.version == UINT64_MAX ||
	      row->data().version != beforeUser.version ||
	      row->data().updated != beforeUser.updated ||
	      row->data().state != State::Suspended ||
	      row->data().owner || row->data().authVersion != userVersion ||
	      row->data().handle != oldHandle) {
	    complete(false);
	    return;
	  }
	  row->data().handle = newHandle;
	  row->data().state = State::Active;
	  row->data().owner = id();
	  row->data().updated = created;
	  row->data().version = beforeUser.version + 1;
	} else {
	  row->data() = beforeUser;
	}
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaPhase(4, zum.cred, Update) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(db()->creds, 0, ZuFwdTuple(credentialID),
	ZuMv(complete), [this](
	  ZdbRow<Cred> *row, auto &&complete) mutable {
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

  ZdbSagaPhase(5, zum.user, Update) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(db()->users, 0, ZuFwdTuple(userID),
	ZuMv(complete), [this](
	  ZdbRow<User> *row, auto &&complete) mutable {
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
  ZdbSagaPhase(6, zum.grant, Delete) {
    grantDelete<Fwd>(this, ceremonyID, ZuMv(complete)); return {};
  }

  RecoveryEnrollSaga_() = default;
  RecoveryEnrollSaga_(
      Bytes ceremonyID_, String issuer_,
      String actor_, UserID userID_,
      uint64_t userVersion_, Bytes oldHandle_,
      Bytes newHandle_, Bytes credentialID_,
      Bytes publicKey_, uint32_t signCount_,
      int64_t created_, bool backupEligible_,
      bool backedUp_, String label_,
      Grant beforeGrant_, User beforeUser_) :
    ceremonyID{ZuMv(ceremonyID_)},
    issuer{ZuMv(issuer_)},
    actor{ZuMv(actor_)},
    userID{ZuMv(userID_)},
    userVersion{ZuMv(userVersion_)},
    oldHandle{ZuMv(oldHandle_)},
    newHandle{ZuMv(newHandle_)},
    credentialID{ZuMv(credentialID_)},
    publicKey{ZuMv(publicKey_)},
    signCount{ZuMv(signCount_)},
    created{ZuMv(created_)},
    backupEligible{ZuMv(backupEligible_)},
    backedUp{ZuMv(backedUp_)},
    label{ZuMv(label_)},
    beforeGrant{ZuMv(beforeGrant_)},
    beforeUser{ZuMv(beforeUser_)} { }
  RecoveryEnrollSaga_(RecoveryEnrollArgs args) :
    RecoveryEnrollSaga_{
      ZuMv(args.ceremonyID), ZuMv(args.issuer), ZuMv(args.actor),
      ZuMv(args.userID), ZuMv(args.userVersion), ZuMv(args.oldHandle),
      ZuMv(args.newHandle), ZuMv(args.credentialID), ZuMv(args.publicKey),
      ZuMv(args.signCount), ZuMv(args.created), ZuMv(args.backupEligible),
      ZuMv(args.backedUp), ZuMv(args.label), ZuMv(args.beforeGrant),
      ZuMv(args.beforeUser)} { }
  ~RecoveryEnrollSaga_() override;
};

using RecoveryEnrollSaga = RecoveryEnrollSaga_<
  ZmHeap<"Zum.RecoveryEnrollSaga", RecoveryEnrollSaga_<>>>;

ZfStruct(, (RecoveryEnrollSaga, FB),
  (ceremonyID,		(Ctor<0>),	Bytes),
  (issuer,		(Ctor<1>),	String),
  (actor,		(Ctor<2>),	String),
  (userID,		(Ctor<3>),	UInt64),
  (userVersion,		(Ctor<4>),	UInt64),
  (oldHandle,		(Ctor<5>),	Bytes),
  (newHandle,		(Ctor<6>),	Bytes),
  (credentialID,	(Ctor<7>),	Bytes),
  (publicKey,		(Ctor<8>),	Bytes),
  (signCount,		(Ctor<9>),	UInt32),
  (created,		(Ctor<10>),	Int64),
  (backupEligible,	(Ctor<11>),	Bool),
  (backedUp,		(Ctor<12>),	Bool),
  (label,		(Ctor<13>),	String),
  (beforeGrant,		(Ctor<14>),	UDT),
  (beforeUser,		(Ctor<15>),	UDT));
ZiFB::AsTable<"Zum.fbs.RecoveryEnroll"> ZiFB_Fmt(RecoveryEnrollSaga *);

ZuAssert((Zdb_::SagaDefValid<RecoveryEnrollSaga>{}));

template <typename Heap>
RecoveryEnrollSaga_<Heap>::~RecoveryEnrollSaga_() = default;

SagaRef recoveryEnroll(ZuBSpan data) {
  return RecoveryEnrollSaga::load(data);
}

Zdb_::SagaDef recoveryEnrollDef() {
  return RecoveryEnrollSaga::def();
}
SagaRef recoveryEnroll(RecoveryEnrollArgs args) {
  return new RecoveryEnrollSaga{ZuMv(args)};
}

int recoveryPrepare(
    const Grant &ceremony, const User &user, ZuBSpan bindingDigest,
    RegistrationInput &input, ZuCSpan origin, ZuCSpan rpID,
    unsigned credentialIDMax,
    int64_t now, RecoveryEnrollArgs &recovery)
{
  if (ceremony.kind != GrantKind::Ceremony ||
      ceremony.purpose != GrantPurpose::Recovery ||
      ceremony.state != State::Active || ceremony.owner ||
      !ceremony.challenge || ceremony.expires <= now ||
      !Ztls::ctEqual(ceremony.bindingDigest, bindingDigest))
    return WebAuthnError::Ceremony;
  if (!ceremony.issuer || !ceremony.actor || !ceremony.userID ||
      !ceremony.userHandle || now <= 0 ||
      user.id != ceremony.userID || user.state != State::Suspended ||
      user.owner || user.authVersion != ceremony.userVersion || !user.handle)
    return WebAuthnError::Fields;

  RegistrationResult result;
  int error = verifyRegistration(input,
    RegistrationState{ceremony.challenge, origin, rpID},
    credentialIDMax, result);
  if (error) return error;

  recovery = RecoveryEnrollArgs{
    .ceremonyID = ceremony.id,
    .issuer = ceremony.issuer,
    .actor = ceremony.actor,
    .userID = ceremony.userID,
    .userVersion = ceremony.userVersion,
    .oldHandle = user.handle,
    .newHandle = ceremony.userHandle,
    .credentialID = ZuMv(result.credentialID),
    .publicKey = ZuMv(result.publicKey),
    .signCount = result.signCount,
    .created = now,
    .backupEligible = result.backupEligible,
    .backedUp = result.backedUp,
    .label = ceremony.label,
    .beforeGrant = ceremony,
    .beforeUser = user
  };
  return WebAuthnError::OK;
}

} // namespace Zum
