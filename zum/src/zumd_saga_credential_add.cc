//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_credential_add.hh>
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
struct CredentialAddSaga_ : public Heap,
    public ZdbSaga<CredentialAddSaga_<Heap>, DB> {
  using Base = ZdbSaga<CredentialAddSaga_<Heap>, DB>;
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
  using Type = ZuStringT<"credentialAdd.v2">;
  enum { NPhases = 5 };

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
  Grant		beforeGrant;

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
		grant.purpose != GrantPurpose::AddCredential ||
		grant.state != State::Active || grant.owner ||
		grant.issuer != issuer || grant.userID != userID ||
		grant.userVersion != userVersion || grant.expires <= created ||
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
	  if (!user || user->data().owner || user->data().state != State::Active ||
	      user->data().handle != userHandle || user->data().authVersion != userVersion) {
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
		  .userVersion = userVersion
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

  ZdbSagaPhase(2, zum.cred, Update) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(db()->creds, 0, ZuFwdTuple(credentialID),
	ZuMv(complete), [this](
	  ZdbRow<Cred> *row, auto &&complete) mutable {
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

  ZdbSagaPhase(3, zum.cred, Update) {
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
  ZdbSagaPhase(4, zum.grant, Delete) {
    grantDelete<Fwd>(this, ceremonyID, ZuMv(complete)); return {};
  }

  CredentialAddSaga_() = default;
  CredentialAddSaga_(
      Bytes ceremonyID_, String issuer_,
      UserID userID_, Bytes userHandle_,
      Bytes credentialID_, Bytes publicKey_,
      uint32_t signCount_, int64_t created_,
      bool backupEligible_, bool backedUp_,
      String label_, uint64_t userVersion_,
      Grant beforeGrant_) :
    ceremonyID{ZuMv(ceremonyID_)},
    issuer{ZuMv(issuer_)},
    userID{ZuMv(userID_)},
    userHandle{ZuMv(userHandle_)},
    credentialID{ZuMv(credentialID_)},
    publicKey{ZuMv(publicKey_)},
    signCount{ZuMv(signCount_)},
    created{ZuMv(created_)},
    backupEligible{ZuMv(backupEligible_)},
    backedUp{ZuMv(backedUp_)},
    label{ZuMv(label_)},
    userVersion{ZuMv(userVersion_)},
    beforeGrant{ZuMv(beforeGrant_)} { }
  CredentialAddSaga_(CredentialAddArgs args) :
    CredentialAddSaga_{
      ZuMv(args.ceremonyID), ZuMv(args.issuer), ZuMv(args.userID),
      ZuMv(args.userHandle), ZuMv(args.credentialID), ZuMv(args.publicKey),
      ZuMv(args.signCount), ZuMv(args.created), ZuMv(args.backupEligible),
      ZuMv(args.backedUp), ZuMv(args.label), ZuMv(args.userVersion),
      ZuMv(args.beforeGrant)} { }
  ~CredentialAddSaga_() override;
};

using CredentialAddSaga = CredentialAddSaga_<
  ZmHeap<"Zum.CredentialAddSaga", CredentialAddSaga_<>>>;

ZfStruct(, (CredentialAddSaga, FB),
  (ceremonyID,		(Ctor<0>),		Bytes),
  (issuer,		(Ctor<1>),		String),
  (userID,		(Ctor<2>),		UInt64),
  (userHandle,		(Ctor<3>),		Bytes),
  (credentialID,	(Ctor<4>),		Bytes),
  (publicKey,		(Ctor<5>),		Bytes),
  (signCount,		(Ctor<6>),		UInt32),
  (created,		(Ctor<7>),		Int64),
  (backupEligible,	(Ctor<8>),		Bool),
  (backedUp,		(Ctor<9>),		Bool),
  (label,		(Ctor<10>),		String),
  (userVersion,		(Ctor<11>, Deflt<1>),	UInt64),
  (beforeGrant,		(Ctor<12>),		UDT));
ZiFB::AsTable<"Zum.fbs.CredentialAdd"> ZiFB_Fmt(CredentialAddSaga *);

ZuAssert((Zdb_::SagaDefValid<CredentialAddSaga>{}));

template <typename Heap>
CredentialAddSaga_<Heap>::~CredentialAddSaga_() = default;

SagaRef credentialAdd(ZuBSpan data) {
  return CredentialAddSaga::load(data);
}

Zdb_::SagaDef credentialAddDef() {
  return CredentialAddSaga::def();
}
SagaRef credentialAdd(CredentialAddArgs args) {
  return new CredentialAddSaga{ZuMv(args)};
}

int credentialPrepare(
    const Grant &ceremony, ZuBSpan bindingDigest, RegistrationInput &input,
    ZuCSpan origin, ZuCSpan rpID,
    unsigned credentialIDMax,
    int64_t now, CredentialAddArgs &add)
{
  if (ceremony.kind != GrantKind::Ceremony ||
      ceremony.purpose != GrantPurpose::AddCredential ||
      ceremony.state != State::Active || ceremony.owner ||
      !ceremony.challenge || ceremony.expires <= now ||
      !Ztls::ctEqual(ceremony.bindingDigest, bindingDigest))
    return WebAuthnError::Ceremony;
  if (!ceremony.issuer || !ceremony.userID || !ceremony.userHandle || now <= 0)
    return WebAuthnError::Fields;

  RegistrationResult result;
  int error = verifyRegistration(input,
    RegistrationState{ceremony.challenge, origin, rpID},
    credentialIDMax, result);
  if (error) return error;

  CredentialAddArgs next{
    .ceremonyID = ceremony.id,
    .issuer = ceremony.issuer,
    .userID = ceremony.userID,
    .userHandle = ceremony.userHandle,
    .credentialID = ZuMv(result.credentialID),
    .publicKey = ZuMv(result.publicKey),
    .signCount = result.signCount,
    .created = now,
    .backupEligible = result.backupEligible,
    .backedUp = result.backedUp,
    .label = ceremony.label,
    .userVersion = ceremony.userVersion,
    .beforeGrant = ceremony
  };
  add = ZuMv(next);
  return WebAuthnError::OK;
}

} // namespace Zum
