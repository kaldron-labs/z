//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_enrollment.hh>
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
struct EnrollmentSaga_ : public Heap,
    public ZdbSaga<EnrollmentSaga_<Heap>, DB> {
  using Base = ZdbSaga<EnrollmentSaga_<Heap>, DB>;
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
  using Type = ZuStringT<"enrollment.v2">;
  enum { NPhases = 9 };

  Bytes		ceremonyID;
  UserID	userID = 0;
  String	name;
  Bytes		handle;
  Bytes		credentialID;
  Bytes		publicKey;
  uint32_t	signCount = 0;
  int64_t	created = 0;
  bool		backupEligible = false;
  bool		backedUp = false;
  String	label;
  bool		precreated = false;
  Grant		beforeGrant;
  User		beforeUser;

  ZdbSagaPhase(0, zum.grant, Update) {
    db()->shardRun(0, [
      this, complete = ZuMv(complete)
    ]() mutable {
      this->template findUpd<0>(db()->grants, 0, ZuFwdTuple(ceremonyID),
	ZuMv(complete), [this](ZdbRow<Grant> *row, auto &&complete) mutable {
	if (!row) { complete(false); return; }
	auto &grant = row->data();
	if constexpr (Fwd) {
	  if (beforeGrant.id != ceremonyID || beforeGrant.state != State::Active ||
	      beforeGrant.owner || grant.kind != GrantKind::Ceremony ||
	      (grant.purpose != GrantPurpose::Enrollment &&
	       grant.purpose != GrantPurpose::Bootstrap) ||
	      grant.state != State::Active || grant.owner || grant.expires <= created ||
	      grant.digest != beforeGrant.digest || grant.challenge != beforeGrant.challenge ||
	      grant.bindingDigest != beforeGrant.bindingDigest) {
	    complete(false);
	    return;
	  }
	}
	grant.state = Fwd ? State::Consumed : beforeGrant.state;
	grant.owner = Fwd ? id() : beforeGrant.owner;
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaPhase(1, zum.user, Update) {
    if (!precreated) { skip(ZuMv(complete)); return {}; }
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
	this->template findUpd<0, ZuSeq<1>>(db()->users, 0, ZuFwdTuple(userID),
	  ZuMv(complete), [this](
	    ZdbRow<User> *row, auto &&complete) mutable {
	  if (!row) { complete(false); return; }
	  if constexpr (Fwd) {
	    if (beforeUser.id != userID || row->data().owner ||
		row->data().version != beforeUser.version ||
		row->data().authVersion != beforeUser.authVersion ||
		row->data().updated != beforeUser.updated ||
		beforeUser.version == UINT64_MAX ||
		row->data().state != State::Pending ||
		row->data().source != UserSource::Local ||
		row->data().name != name || row->data().handle) {
	      complete(false);
	      return;
	    }
	    row->data().handle = handle;
	    row->data().updated = created;
	    row->data().version = beforeUser.version + 1;
	    row->data().owner = id();
	  } else {
	    row->data() = beforeUser;
	  }
	  complete(row->commit());
	});
    });
    return {};
  }

  ZdbSagaPhase(2, zum.user, Insert) {
    if (precreated) { skip(ZuMv(complete)); return {}; }
    // Duplicate rejection is business validation in the mutation callback;
    // native saga replay can suppress that callback after an applied insert.
    db()->shardRun(0, [
      this, complete = ZuMv(complete)
    ]() mutable {
      if constexpr (Fwd) {
	db()->users->template find<0>(0, ZuFwdTuple(userID),
	  [this, complete = ZuMv(complete)](ZdbRowRef<User> existing) mutable {
	    ZdbRowRef<User> row =
	      new ZdbRow<User>{db()->users, ZdbShard{0}};
	    insert(db()->users, ZuMv(row), ZuMv(complete),
	      [this, duplicate = bool(existing)](
		  ZdbRow<User> *row, auto &&complete) mutable {
		new (row->ptr()) User{
		  .id = userID,
		  .name = name,
		  .handle = handle,
		  .created = created,
		  .updated = created,
		  .state = State::Pending,
		  .owner = id()
		};
		if (duplicate) { complete(false); return; }
		complete(row->commit());
	      });
	});
      } else {
	this->template findDel<0>(db()->users, 0, ZuFwdTuple(userID),
	  ZuMv(complete), [](ZdbRow<User> *row, auto &&complete) mutable {
	  complete(!row || row->commit());
	});
      }
    });
    return {};
  }

  ZdbSagaPhase(3, zum.cred, Insert) {
    db()->shardRun(0, [
      this, complete = ZuMv(complete)
    ]() mutable {
      if constexpr (Fwd) {
	db()->creds->template find<0>(0, ZuFwdTuple(credentialID),
	  [this, complete = ZuMv(complete)](ZdbRowRef<Cred> existing) mutable {
	    ZdbRowRef<Cred> row =
	      new ZdbRow<Cred>{db()->creds, ZdbShard{0}};
	    insert(db()->creds, ZuMv(row), ZuMv(complete),
	      [this, duplicate = bool(existing)](
		  ZdbRow<Cred> *row, auto &&complete) mutable {
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
		  .userVersion = precreated ? beforeUser.authVersion : 1
		};
		if (duplicate) { complete(false); return; }
		complete(row->commit());
	      });
	});
      } else {
	this->template findDel<0>(db()->creds, 0, ZuFwdTuple(credentialID),
	  ZuMv(complete), [](ZdbRow<Cred> *row, auto &&complete) mutable {
	  complete(!row || row->commit());
	});
      }
    });
    return {};
  }

  ZdbSagaPhase(4, zum.cred, Update) {
    db()->shardRun(0, [
      this, complete = ZuMv(complete)
    ]() mutable {
      this->template findUpd<0>(db()->creds, 0, ZuFwdTuple(credentialID),
	ZuMv(complete), [this](ZdbRow<Cred> *row, auto &&complete) mutable {
	if (!row) { complete(false); return; }
	auto &cred = row->data();
	if (Fwd && (cred.owner != id() || cred.state != State::Pending)) {
	  complete(false); return;
	}
	cred.state = Fwd ? State::Active : State::Pending;
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaPhase(5, zum.user, Update) {
    db()->shardRun(0, [
      this, complete = ZuMv(complete)
    ]() mutable {
      this->template findUpd<0>(db()->users, 0, ZuFwdTuple(userID),
	ZuMv(complete), [this](ZdbRow<User> *row, auto &&complete) mutable {
	if (!row) { complete(false); return; }
	auto &user = row->data();
	if (Fwd && (user.owner != id() || user.state != State::Pending)) {
	  complete(false); return;
	}
	user.state = Fwd ? State::Active : State::Pending;
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaPhase(6, zum.cred, Update) {
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

  ZdbSagaPhase(7, zum.user, Update) {
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

  ZdbSagaPhase(8, zum.grant, Delete) {
    grantDelete<Fwd>(this, ceremonyID, ZuMv(complete)); return {};
  }

  EnrollmentSaga_() = default;
  EnrollmentSaga_(
      Bytes ceremonyID_, UserID userID_,
      String name_, Bytes handle_,
      Bytes credentialID_, Bytes publicKey_,
      uint32_t signCount_, int64_t created_,
      bool backupEligible_, bool backedUp_,
      String label_, bool precreated_,
      Grant beforeGrant_, User beforeUser_) :
    ceremonyID{ZuMv(ceremonyID_)},
    userID{ZuMv(userID_)},
    name{ZuMv(name_)},
    handle{ZuMv(handle_)},
    credentialID{ZuMv(credentialID_)},
    publicKey{ZuMv(publicKey_)},
    signCount{ZuMv(signCount_)},
    created{ZuMv(created_)},
    backupEligible{ZuMv(backupEligible_)},
    backedUp{ZuMv(backedUp_)},
    label{ZuMv(label_)},
    precreated{ZuMv(precreated_)},
    beforeGrant{ZuMv(beforeGrant_)},
    beforeUser{ZuMv(beforeUser_)} { }
  EnrollmentSaga_(EnrollmentArgs args) :
    EnrollmentSaga_{
      ZuMv(args.ceremonyID), ZuMv(args.userID), ZuMv(args.name),
      ZuMv(args.handle), ZuMv(args.credentialID), ZuMv(args.publicKey),
      ZuMv(args.signCount), ZuMv(args.created), ZuMv(args.backupEligible),
      ZuMv(args.backedUp), ZuMv(args.label), ZuMv(args.precreated),
      ZuMv(args.beforeGrant), ZuMv(args.beforeUser)} { }
  ~EnrollmentSaga_() override;
};

using EnrollmentSaga = EnrollmentSaga_<
  ZmHeap<"Zum.EnrollmentSaga", EnrollmentSaga_<>>>;

ZfStruct(, (EnrollmentSaga, FB),
  (ceremonyID,		(Ctor<0>),			Bytes),
  (userID,		(Ctor<1>),			UInt64),
  (name,		(Ctor<2>),			String),
  (handle,		(Ctor<3>),			Bytes),
  (credentialID,	(Ctor<4>),			Bytes),
  (publicKey,		(Ctor<5>),			Bytes),
  (signCount,		(Ctor<6>),			UInt32),
  (created,		(Ctor<7>),			Int64),
  (backupEligible,	(Ctor<8>),			Bool),
  (backedUp,		(Ctor<9>),			Bool),
  (label,		(Ctor<10>),			String),
  (precreated,		(Ctor<11>, Deflt<false>),	Bool),
  (beforeGrant,		(Ctor<12>),			UDT),
  (beforeUser,		(Ctor<13>),			UDT));
ZiFB::AsTable<"Zum.fbs.Enrollment"> ZiFB_Fmt(EnrollmentSaga *);

ZuAssert((Zdb_::SagaDefValid<EnrollmentSaga>{}));

template <typename Heap>
EnrollmentSaga_<Heap>::~EnrollmentSaga_() = default;

SagaRef enrollment(ZuBSpan data) {
  return EnrollmentSaga::load(data);
}

Zdb_::SagaDef enrollmentDef() {
  return EnrollmentSaga::def();
}
SagaRef enrollment(EnrollmentArgs args) {
  return new EnrollmentSaga{ZuMv(args)};
}

int enrollmentPrepare(
    const Grant &ceremony, ZuBSpan bindingDigest, RegistrationInput &input,
    ZuCSpan origin, ZuCSpan rpID,
    unsigned credentialIDMax,
    int64_t now, EnrollmentArgs &enrollment)
{
  if (ceremony.kind != GrantKind::Ceremony ||
      (ceremony.purpose != GrantPurpose::Enrollment &&
       ceremony.purpose != GrantPurpose::Bootstrap) ||
      ceremony.state != State::Active || ceremony.owner ||
      !ceremony.challenge || ceremony.expires <= now ||
      !Ztls::ctEqual(ceremony.bindingDigest, bindingDigest))
    return WebAuthnError::Ceremony;
  if (!ceremony.userID || !ceremony.userName ||
      !ceremony.userHandle || now <= 0)
    return WebAuthnError::Fields;

  RegistrationResult result;
  int error = verifyRegistration(input,
    RegistrationState{ceremony.challenge, origin, rpID},
    credentialIDMax, result);
  if (error) return error;

  EnrollmentArgs next;
  next.ceremonyID = ceremony.id;
  next.userID = ceremony.userID;
  next.name = ceremony.userName;
  next.handle = ceremony.userHandle;
  next.credentialID = ZuMv(result.credentialID);
  next.publicKey = ZuMv(result.publicKey);
  next.signCount = result.signCount;
  next.created = now;
  next.backupEligible = result.backupEligible;
  next.backedUp = result.backedUp;
  next.label = ceremony.label;
  next.precreated = ceremony.purpose == GrantPurpose::Bootstrap ||
    ceremony.actor == "precreated";
  next.beforeGrant = ceremony;
  enrollment = ZuMv(next);
  return WebAuthnError::OK;
}

} // namespace Zum
