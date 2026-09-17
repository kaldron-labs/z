//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// recoverable server mutation definitions

#ifndef zumd_sagas_HH
#define zumd_sagas_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/zumd_oauth.hh>
#include <zlib/zumd_webauthn.hh>
#include <zlib/ZtlsSec.hh>

#include <zlib/zum_saga_fbs.h>

namespace Zum {

template <bool Fwd, typename Def, typename Complete>
void grantDelete(Def *def, Bytes id, Complete complete)
{
  auto table = def->context->grants;
  table->run(0, [def, table, id = ZuMv(id), complete = ZuMv(complete)]() mutable {
    if constexpr (Fwd) {
      def->saga->template findDel<0>(table, 0, ZuFwdTuple(ZuMv(id)), ZuMv(complete),
	[def](ZdbRow<Grant> *row, auto &&complete) mutable {
	  if (!row || row->data().owner != def->saga->id() ||
	      row->data().state != State::Consumed) { complete(false); return; }
	  complete(row->commit());
	});
    } else {
      ZdbRowRef<Grant> row = new ZdbRow<Grant>{table, ZdbShard{0}};
      def->saga->insert(table, ZuMv(row), ZuMv(complete),
	[def](ZdbRow<Grant> *row, auto &&complete) mutable {
	  new (row->ptr()) Grant{def->beforeGrant};
	  row->data().state = State::Consumed;
	  row->data().owner = def->saga->id();
	  complete(row->commit());
	});
    }
  });
}

struct Enrollment : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"enrollment.v2">;
  enum { NSteps = 9 };

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

  ZdbSagaStep(0, zum.grant, Update) {
    context->grants->run(0, [
      this, complete = ZuMv(complete)
    ]() mutable {
      saga->findUpd<0>(context->grants, 0, ZuFwdTuple(ceremonyID),
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
	grant.owner = Fwd ? saga->id() : beforeGrant.owner;
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(1, zum.user, Update) {
    if (!precreated) { saga->skip(ZuMv(complete)); return {}; }
    context->users->run(0, [this, complete = ZuMv(complete)]() mutable {
	saga->findUpd<0, ZuSeq<1>>(context->users, 0, ZuFwdTuple(userID),
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
	    row->data().owner = saga->id();
	  } else {
	    row->data() = beforeUser;
	  }
	  complete(row->commit());
	});
    });
    return {};
  }

  ZdbSagaStep(2, zum.user, Insert) {
    if (precreated) { saga->skip(ZuMv(complete)); return {}; }
    // Duplicate rejection is business validation in the mutation callback;
    // native saga replay can suppress that callback after an applied insert.
    context->users->run(0, [
      this, complete = ZuMv(complete)
    ]() mutable {
      if constexpr (Fwd) {
	context->users->find<0>(0, ZuFwdTuple(userID),
	  [this, complete = ZuMv(complete)](ZdbRowRef<User> existing) mutable {
	    ZdbRowRef<User> row =
	      new ZdbRow<User>{context->users, ZdbShard{0}};
	    saga->insert(context->users, ZuMv(row), ZuMv(complete),
	      [this, duplicate = bool(existing)](
		  ZdbRow<User> *row, auto &&complete) mutable {
		new (row->ptr()) User{
		  .id = userID,
		  .name = name,
		  .handle = handle,
		  .created = created,
		  .updated = created,
		  .state = State::Pending,
		  .owner = saga->id()
		};
		if (duplicate) { complete(false); return; }
		complete(row->commit());
	      });
	});
      } else {
	saga->findDel<0>(context->users, 0, ZuFwdTuple(userID),
	  ZuMv(complete), [](ZdbRow<User> *row, auto &&complete) mutable {
	  complete(!row || row->commit());
	});
      }
    });
    return {};
  }

  ZdbSagaStep(3, zum.cred, Insert) {
    context->creds->run(0, [
      this, complete = ZuMv(complete)
    ]() mutable {
      if constexpr (Fwd) {
	context->creds->find<0>(0, ZuFwdTuple(credentialID),
	  [this, complete = ZuMv(complete)](ZdbRowRef<Cred> existing) mutable {
	    ZdbRowRef<Cred> row =
	      new ZdbRow<Cred>{context->creds, ZdbShard{0}};
	    saga->insert(context->creds, ZuMv(row), ZuMv(complete),
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
		  .owner = saga->id(),
		  .userVersion = precreated ? beforeUser.authVersion : 1
		};
		if (duplicate) { complete(false); return; }
		complete(row->commit());
	      });
	});
      } else {
	saga->findDel<0>(context->creds, 0, ZuFwdTuple(credentialID),
	  ZuMv(complete), [](ZdbRow<Cred> *row, auto &&complete) mutable {
	  complete(!row || row->commit());
	});
      }
    });
    return {};
  }

  ZdbSagaStep(4, zum.cred, Update) {
    context->creds->run(0, [
      this, complete = ZuMv(complete)
    ]() mutable {
      saga->findUpd<0>(context->creds, 0, ZuFwdTuple(credentialID),
	ZuMv(complete), [this](ZdbRow<Cred> *row, auto &&complete) mutable {
	if (!row) { complete(false); return; }
	auto &cred = row->data();
	if (Fwd && (cred.owner != saga->id() || cred.state != State::Pending)) {
	  complete(false); return;
	}
	cred.state = Fwd ? State::Active : State::Pending;
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(5, zum.user, Update) {
    context->users->run(0, [
      this, complete = ZuMv(complete)
    ]() mutable {
      saga->findUpd<0>(context->users, 0, ZuFwdTuple(userID),
	ZuMv(complete), [this](ZdbRow<User> *row, auto &&complete) mutable {
	if (!row) { complete(false); return; }
	auto &user = row->data();
	if (Fwd && (user.owner != saga->id() || user.state != State::Pending)) {
	  complete(false); return;
	}
	user.state = Fwd ? State::Active : State::Pending;
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(6, zum.cred, Update) {
    context->creds->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->creds, 0, ZuFwdTuple(credentialID),
	ZuMv(complete), [this](
	  ZdbRow<Cred> *row, auto &&complete) mutable {
	if (!row) { complete(false); return; }
	if constexpr (Fwd) {
	  if (row->data().state != State::Active || row->data().owner != saga->id()) {
	    complete(false); return;
	  }
	}
	row->data().owner = Fwd ? uint128_t{0} : saga->id();
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(7, zum.user, Update) {
    context->users->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->users, 0, ZuFwdTuple(userID),
	ZuMv(complete), [this](
	  ZdbRow<User> *row, auto &&complete) mutable {
	if (!row) { complete(false); return; }
	if constexpr (Fwd) {
	  if (row->data().state != State::Active || row->data().owner != saga->id()) {
	    complete(false); return;
	  }
	}
	row->data().owner = Fwd ? uint128_t{0} : saga->id();
	complete(row->commit());
      });
    });
    return {};
  }
  ZdbSagaStep(8, zum.grant, Delete) {
    grantDelete<Fwd>(this, ceremonyID, ZuMv(complete)); return {};
  }
};

ZfbStruct(ZumAPI, Enrollment,
  (((ceremonyID),	(Ctor<0>)),	(Bytes)),
  (((userID),		(Ctor<1>)),	(UInt64)),
  (((name),		(Ctor<2>)),	(String)),
  (((handle),		(Ctor<3>)),	(Bytes)),
  (((credentialID),	(Ctor<4>)),	(Bytes)),
  (((publicKey),	(Ctor<5>)),	(Bytes)),
  (((signCount),	(Ctor<6>)),	(UInt32)),
  (((created),		(Ctor<7>)),	(Int64)),
  (((backupEligible),	(Ctor<8>)),	(Bool)),
  (((backedUp),	(Ctor<9>)),	(Bool)),
  (((label),		(Ctor<10>)),	(String)),
  (((precreated),	(Ctor<11>)),	(Bool, false)),
  (((beforeGrant),	(Ctor<12>)),	(UDT)),
  (((beforeUser),	(Ctor<13>)),	(UDT)));

ZumExtern int enrollmentPrepare(
  const Grant &, ZuBSpan bindingDigest, RegistrationInput &,
  ZuCSpan origin, ZuCSpan rpID,
  unsigned credentialIDMax,
  int64_t now, Enrollment &);

struct CredentialAdd : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"credentialAdd.v2">;
  enum { NSteps = 5 };

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
    auto table = context->grants;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      saga->template findUpd<0>(table, 0, ZuFwdTuple(ceremonyID), ZuMv(complete),
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
	  grant.owner = Fwd ? saga->id() : beforeGrant.owner;
	  complete(row->commit());
	});
    });
  }

  ZdbSagaStep(0, zum.grant, Update) {
    if constexpr (!Fwd) {
      consume<Fwd>(ZuMv(complete));
    } else {
      context->users->run(0, [this, complete = ZuMv(complete)]() mutable {
	context->users->find<0>(0, ZuFwdTuple(userID), [
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

  ZdbSagaStep(1, zum.cred, Insert) {
    context->creds->run(0, [this, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	context->creds->find<0>(0, ZuFwdTuple(credentialID),
	  [this, complete = ZuMv(complete)](ZdbRowRef<Cred> existing) mutable {
	    ZdbRowRef<Cred> row =
	      new ZdbRow<Cred>{context->creds, ZdbShard{0}};
	    saga->insert(context->creds, ZuMv(row), ZuMv(complete),
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
		  .owner = saga->id(),
		  .userVersion = userVersion
		};
		if (duplicate) { complete(false); return; }
		complete(row->commit());
	      });
	  });
      } else {
	saga->findDel<0>(context->creds, 0, ZuFwdTuple(credentialID),
	  ZuMv(complete), [](ZdbRow<Cred> *row, auto &&complete) mutable {
	  complete(!row || row->commit());
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
	if (!row) { complete(false); return; }
	auto &cred = row->data();
	if (Fwd && (cred.owner != saga->id() || cred.state != State::Pending)) {
	  complete(false); return;
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
	if (!row) { complete(false); return; }
	if constexpr (Fwd) {
	  if (row->data().state != State::Active || row->data().owner != saga->id()) {
	    complete(false); return;
	  }
	}
	row->data().owner = Fwd ? uint128_t{0} : saga->id();
	complete(row->commit());
      });
    });
    return {};
  }
  ZdbSagaStep(4, zum.grant, Delete) {
    grantDelete<Fwd>(this, ceremonyID, ZuMv(complete)); return {};
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
  (((userVersion),	(Ctor<11>)),	(UInt64, 1)),
  (((beforeGrant),	(Ctor<12>)),	(UDT)));

ZumExtern int credentialPrepare(
  const Grant &, ZuBSpan bindingDigest, RegistrationInput &,
  ZuCSpan origin, ZuCSpan rpID,
  unsigned credentialIDMax,
  int64_t now, CredentialAdd &);

struct RecoveryStart : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"recoveryStart.v3">;
  enum { NSteps = 6 };

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

  ZumAPI void validate(Zdb_::SagaCompleteFn);

  ZdbSagaStep(0, zum.request, Insert) {
    if constexpr (Fwd) {
      validate([this, complete = ZuMv(complete)](bool ok) mutable {
	if (!ok) { complete(false); return; }
	requestInsert<Fwd>(this, ZuMv(complete));
      });
    } else requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(1, zum.user, Update) {
    context->users->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->users, 0, ZuFwdTuple(userID),
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
	  row->data().owner = saga->id();
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

  ZdbSagaStep(2, zum.grant, Insert) {
    context->grants->run(0, [this, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	context->grants->find<0>(0, ZuFwdTuple(capabilityID),
	  [this, complete = ZuMv(complete)](ZdbRowRef<Grant> existing) mutable {
	    context->refresh->run(0, [this, existing = ZuMv(existing),
		complete = ZuMv(complete)]() mutable {
	      context->refresh->find<0>(0, ZuFwdTuple(capabilityID),
		[this, existing = ZuMv(existing), complete = ZuMv(complete)](
		    ZdbRowRef<Refresh> refresh) mutable {
		if (refresh) { complete(false); return; }
	    ZdbRowRef<Grant> row =
	      new ZdbRow<Grant>{context->grants, ZdbShard{0}};
	    saga->insert(context->grants, ZuMv(row), ZuMv(complete),
	      [this, duplicate = bool(existing)](ZdbRow<Grant> *row, auto &&complete) mutable {
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
		if (duplicate) { complete(false); return; }
		complete(row->commit());
	      });
	      });
	    });
	  });
      } else {
	saga->findDel<0>(context->grants, 0, ZuFwdTuple(capabilityID),
	  ZuMv(complete), [](
	    ZdbRow<Grant> *row, auto &&complete) mutable {
	  complete(!row || row->commit());
	});
      }
    });
    return {};
  }

  ZdbSagaStep(3, zum.user, Update) {
    context->users->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->users, 0, ZuFwdTuple(userID),
	ZuMv(complete), [this](
	  ZdbRow<User> *row, auto &&complete) mutable {
	if (!row) { complete(!Fwd); return; }
	row->data().owner = Fwd ? uint128_t{0} : saga->id();
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(4, zum.grant, Update) {
    context->grants->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->grants, 0, ZuFwdTuple(capabilityID),
	ZuMv(complete), [this](
	  ZdbRow<Grant> *row, auto &&complete) mutable {
	if (!row) { complete(!Fwd); return; }
	row->data().state = Fwd ? State::Active : State::Pending;
	row->data().owner = Fwd ? uint128_t{0} : saga->id();
	complete(row->commit());
      });
    });
    return {};
  }
  ZdbSagaStep(5, zum.request, Update) {
    requestComplete(this, {}, created, ZuMv(complete));
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
  (((actor),		(Ctor<7>)),	(String)),
  (((version),		(Ctor<8>)),	(UInt64)),
  (((oldState), (Ctor<9>, Enum<State::Map>)), (Int8)),
  (((oldUpdated), (Ctor<10>)), (Int64)),
  (((request), (Ctor<11>)), (UDT)));

struct RecoveryEnroll : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"recoveryEnroll.v2">;
  enum { NSteps = 7 };

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
    auto table = context->grants;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      saga->template findUpd<0>(table, 0, ZuFwdTuple(ceremonyID), ZuMv(complete),
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
	  grant.owner = Fwd ? saga->id() : beforeGrant.owner;
	  complete(row->commit());
	});
    });
  }

  ZdbSagaStep(0, zum.grant, Update) {
    if constexpr (!Fwd) {
      consume<Fwd>(ZuMv(complete));
    } else {
      context->users->run(0, [this, complete = ZuMv(complete)]() mutable {
	context->users->find<0>(0, ZuFwdTuple(userID), [
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

  ZdbSagaStep(1, zum.cred, Insert) {
    context->creds->run(0, [this, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	ZdbRowRef<Cred> row =
	  new ZdbRow<Cred>{context->creds, ZdbShard{0}};
	context->creds->find<0>(0, ZuFwdTuple(credentialID), [
	  this, row = ZuMv(row), complete = ZuMv(complete)
	](ZdbRowRef<Cred> existing) mutable {
	saga->insert(context->creds, ZuMv(row), ZuMv(complete),
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
	    .owner = saga->id(),
	    .userVersion = userVersion
	  };
	  if (duplicate) { complete(false); return; }
	  complete(row->commit());
	});
	});
      } else {
	saga->findDel<0>(context->creds, 0, ZuFwdTuple(credentialID),
	  ZuMv(complete), [this](
	    ZdbRow<Cred> *row, auto &&complete) mutable {
	  if (!row) { complete(true); return; }
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
	if (!row) { complete(false); return; }
	if (Fwd && (row->data().owner != saga->id() ||
	    row->data().state != State::Pending)) {
	  complete(false); return;
	}
	row->data().state = Fwd ? State::Active : State::Pending;
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(3, zum.user, Update) {
    context->users->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0, ZuSeq<1>>(context->users, 0, ZuFwdTuple(userID),
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
	  row->data().owner = saga->id();
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

  ZdbSagaStep(4, zum.cred, Update) {
    context->creds->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->creds, 0, ZuFwdTuple(credentialID),
	ZuMv(complete), [this](
	  ZdbRow<Cred> *row, auto &&complete) mutable {
	if (!row) { complete(false); return; }
	if constexpr (Fwd) {
	  if (row->data().state != State::Active || row->data().owner != saga->id()) {
	    complete(false); return;
	  }
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
	if (!row) { complete(false); return; }
	if constexpr (Fwd) {
	  if (row->data().state != State::Active || row->data().owner != saga->id()) {
	    complete(false); return;
	  }
	}
	row->data().owner = Fwd ? uint128_t{0} : saga->id();
	complete(row->commit());
      });
    });
    return {};
  }
  ZdbSagaStep(6, zum.grant, Delete) {
    grantDelete<Fwd>(this, ceremonyID, ZuMv(complete)); return {};
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
  (((label),		(Ctor<13>)),	(String)),
  (((beforeGrant),	(Ctor<14>)),	(UDT)),
  (((beforeUser),	(Ctor<15>)),	(UDT)));

ZumExtern int recoveryPrepare(
  const Grant &, const User &, ZuBSpan bindingDigest, RegistrationInput &,
  ZuCSpan origin, ZuCSpan rpID,
  unsigned credentialIDMax,
  int64_t now, RecoveryEnroll &);

struct CodeFamily : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"codeFamily.v6">;
  enum { NSteps = 5 };

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
  uint64_t	membershipVersion = 0;

  ZdbSagaStep(0, zum.grant, Update) {
    context->grants->run(0, [
      this, complete = ZuMv(complete)
    ]() mutable {
      saga->findUpd<0>(context->grants, 0, ZuFwdTuple(codeID),
	ZuMv(complete), [this](ZdbRow<Grant> *row, auto &&complete) mutable {
	if (!row) { complete(false); return; }
	auto &code = row->data();
	if constexpr (Fwd) {
	bool valid = beforeGrant.id == codeID && beforeGrant.state == State::Active &&
	  !beforeGrant.owner && code.kind == GrantKind::Code &&
	  Ztls::ctEqual(code.digest, codeDigest) && code.issuer == issuer &&
	  code.userID == userID && code.clientID == clientID &&
	  code.credentialID == credentialID && code.audience == audience &&
	  code.scope == beforeGrant.scope &&
	  code.requestedRoleIDs == beforeGrant.requestedRoleIDs &&
	  code.actions == beforeGrant.actions &&
	  code.nonce == nonce &&
	  code.appID == appID &&
	  code.authorityProviderID == authorityProviderID &&
	  code.policyVersion == policyVersion &&
	  code.evidenceVersion == evidenceVersion &&
	  code.authoritySource == authoritySource &&
	  code.roleIDs == roleIDs &&
	  code.userVersion == userVersion && code.expires > created;
	valid &= code.state == State::Active && !code.owner;
	if (!valid) {
	  complete(false);
	  return;
	}
	}
	code.state = Fwd ? State::Consumed : beforeGrant.state;
	code.owner = Fwd ? saga->id() : beforeGrant.owner;
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaStep(1, zum.refresh, Insert) {
    context->refresh->run(0, [
      this, complete = ZuMv(complete)
    ]() mutable {
      if constexpr (Fwd) {
        context->refresh->find<0>(0, ZuFwdTuple(familyID), [
	  this, complete = ZuMv(complete)
	](ZdbRowRef<Refresh> existing) mutable {
        ZdbRowRef<Refresh> row =
          new ZdbRow<Refresh>{context->refresh, ZdbShard{0}};
        saga->insert(context->refresh, ZuMv(row), ZuMv(complete),
          [this, duplicate = bool(existing)](ZdbRow<Refresh> *row, auto &&complete) mutable {
          new (row->ptr()) Refresh{
	    .id = familyID,
	    .owner = saga->id(),
            .authVersion = authVersion,
            .userVersion = userVersion,
            .clientVersion = clientVersion,
            .membershipVersion = membershipVersion,
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
        saga->findDel<0>(context->refresh, 0, ZuFwdTuple(familyID),
	  ZuMv(complete), [](ZdbRow<Refresh> *row, auto &&complete) mutable {
	  complete(!row || row->commit());
	});
      }
    });
    return {};
  }

  ZdbSagaStep(2, zum.refresh, Update) {
    context->apps->run(0, [
      this, complete = ZuMv(complete)
    ]() mutable {
      context->apps->find<0>(0, ZuFwdTuple(appID), [
	this, complete = ZuMv(complete)
      ](ZdbRowRef<App> app) mutable {
	if constexpr (Fwd)
	  if (!appID || !app || app->data().state != State::Active ||
	      app->data().owner || app->data().authVersion != authVersion) {
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
	    context->refresh->run(0, [
	      this, complete = ZuMv(complete)
	    ]() mutable {
	      saga->findUpd<0>(context->refresh, 0, ZuFwdTuple(familyID),
		ZuMv(complete), [this](
		  ZdbRow<Refresh> *row, auto &&complete) mutable {
		if (!row) { complete(false); return; }
		auto &family = row->data();
		if (Fwd && (family.owner != saga->id() || family.state != State::Pending)) {
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

	  ZdbSagaStep(3, zum.refresh, Update) {
	    context->refresh->run(0, [this, complete = ZuMv(complete)]() mutable {
	      saga->findUpd<0>(context->refresh, 0, ZuFwdTuple(familyID),
	ZuMv(complete), [this](
	  ZdbRow<Refresh> *row, auto &&complete) mutable {
	if (!row) { complete(false); return; }
	if constexpr (Fwd) {
	  if (row->data().state != State::Active || row->data().owner != saga->id()) {
	    complete(false); return;
	  }
	}
	row->data().owner = Fwd ? uint128_t{0} : saga->id();
	complete(row->commit());
      });
    });
    return {};
  }
  ZdbSagaStep(4, zum.grant, Delete) {
    grantDelete<Fwd>(this, codeID, ZuMv(complete)); return {};
  }
};

ZfbStruct(ZumAPI, CodeFamily,
  (((codeID),		(Ctor<0>)),	(Bytes)),
  (((codeDigest),	(Ctor<1>)),	(Bytes)),
  (((familyID),	(Ctor<2>)),	(Bytes)),
  (((issuer),		(Ctor<3>)),	(String)),
  (((appID),		(Ctor<4>)),	(UInt64)),
  (((userID),		(Ctor<5>)),	(UInt64)),
  (((clientID),	(Ctor<6>)),	(String)),
  (((credentialID),	(Ctor<7>)),	(Bytes)),
  (((audience),	(Ctor<8>)),	(String)),
  (((requestedRoleIDs), (Ctor<9>)), (UInt64Vec)),
  (((roleIDs),		(Ctor<10>)),	(UInt64Vec)),
  (((actions),		(Ctor<11>)),	(UDT)),
  (((digest),		(Ctor<12>)),	(Bytes)),
  (((authVersion),	(Ctor<13>)),	(UInt64)),
  (((userVersion),	(Ctor<14>)),	(UInt64, 1)),
  (((authTime),	(Ctor<15>)),	(Int64)),
  (((created),		(Ctor<16>)),	(Int64)),
  (((expires),		(Ctor<17>)),	(Int64)),
  (((scope),		(Ctor<18>)),	(String)),
  (((nonce),		(Ctor<19>)),	(String)),
  (((authorityProviderID),(Ctor<20>)),	(UInt64)),
  (((policyVersion),	(Ctor<21>)),	(UInt64)),
  (((evidenceVersion),	(Ctor<22>)),	(UInt64)),
  (((authoritySource),	(Ctor<23>, Enum<UserSource::Map>)), (Int8)),
  (((beforeGrant),	(Ctor<24>)), (UDT)),
  (((clientVersion),	(Ctor<25>)),	(UInt64)),
  (((membershipVersion),	(Ctor<26>)),	(UInt64)));

ZumExtern bool codeFamilyPrepare(
  Ztls::Random &, const Grant &, ZuBSpan codeDigest,
  String scope, IDVec requestedRoleIDs, ZtBitmap actions, uint64_t authVersion,
  int64_t now, int64_t expires, CodeFamily &, String &refreshToken);

struct AppEnrollment : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"appEnrollment.v6">;
  enum { NSteps = 14 };

  AppID		coreAppID = 0;
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

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }

  ZdbSagaStep(1, zum.app, Insert) {
    context->apps->run(0, [this, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	ZdbRowRef<App> row = new ZdbRow<App>{context->apps, ZdbShard{0}};
	saga->insert(context->apps, ZuMv(row), ZuMv(complete),
	  [this](ZdbRow<App> *row, auto &&complete) mutable {
	    new (row->ptr()) App{.id = appID, .name = appName,
	      .label = appLabel, .state = State::Pending, .authVersion = 1,
	      .version = 1, .created = created, .updated = created,
	      .owner = saga->id(), .audience = audience};
	    complete(row->commit());
	  });
      } else {
	saga->findDel<0>(context->apps, 0, ZuFwdTuple(appID),
	  ZuMv(complete), [this](ZdbRow<App> *row, auto &&complete) mutable {
	    if (!row || row->data().owner != saga->id() ||
		row->data().state != State::Pending) { complete(true); return; }
	    complete(row->commit());
	  });
      }
    });
    return {};
  }

  ZdbSagaStep(2, zum.sign_key, Insert) {
    if (!signKey.id) { saga->skip(ZuMv(complete)); return {}; }
    context->signKeys->run(0, [this, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	ZdbRowRef<SignKey> row =
	  new ZdbRow<SignKey>{context->signKeys, ZdbShard{0}};
	saga->insert(context->signKeys, ZuMv(row), ZuMv(complete),
	  [this](ZdbRow<SignKey> *row, auto &&complete) mutable {
	    new (row->ptr()) SignKey{signKey};
	    row->data().owner = saga->id();
	    complete(row->commit());
	  });
      } else {
	saga->findDel<0>(context->signKeys, 0, ZuFwdTuple(signKey.id),
	  ZuMv(complete), [this](ZdbRow<SignKey> *row,
	      auto &&complete) mutable {
	    if (!row || row->data().owner != saga->id()) {
	      complete(true); return;
	    }
	    complete(row->commit());
	  });
      }
    });
    return {};
  }

  ZdbSagaStep(3, zum.client, Insert) {
    context->clients->run(0, [this, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	ZdbRowRef<Client> row =
	  new ZdbRow<Client>{context->clients, ZdbShard{0}};
	saga->insert(context->clients, ZuMv(row), ZuMv(complete),
	  [this](ZdbRow<Client> *row, auto &&complete) mutable {
	    Client client{.id = clientID,
	      .appID = appID,
	      .label = "default service client",
	      .secretDigest = secretDigest, .secretVersion = 1,
	      .created = created, .updated = created,
	      .type = ClientType::Confidential,
	      .authMethod = ClientAuthMethod::ClientSecretBasic,
	      .grants = uint8_t(ClientGrant::ClientCredentials),
	      .refreshAllowed = false, .state = State::Active,
	      .version = 1, .owner = saga->id()};
	    new (row->ptr()) Client{ZuMv(client)};
	    complete(row->commit());
	  });
      } else {
	saga->findDel<0>(context->clients, 0, ZuFwdTuple(clientID),
	  ZuMv(complete), [this](ZdbRow<Client> *row,
	      auto &&complete) mutable {
	    if (!row || row->data().owner != saga->id()) {
	      complete(true); return;
	    }
	    complete(row->commit());
	  });
      }
    });
    return {};
  }

  ZdbSagaStep(4, zum.client_access, Insert) {
	// Catalog publication is an app-scoped administrative capability, not a
	// role granted through the core application's client_access table.
	saga->skip(ZuMv(complete));
    return {};
  }

  ZdbSagaStep(5, zum.admin_access, Insert) {
    if (!catalogPublishOp || !operationQueryOp ||
	catalogPublishOp == operationQueryOp) {
      complete(false);
      return {};
    }
    context->adminAccess->run(0, [this, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	ActionIDVec operations;
	operations.push(catalogPublishOp);
	operations.push(operationQueryOp);
	ZdbRowRef<AdminAccess> row =
	  new ZdbRow<AdminAccess>{context->adminAccess, ZdbShard{0}};
	saga->insert(context->adminAccess, ZuMv(row), ZuMv(complete),
	  [this, operations = ZuMv(operations)](
	      ZdbRow<AdminAccess> *row, auto &&complete) mutable {
	    new (row->ptr()) AdminAccess{.actorKind = ActorKind::Client,
	      .actorID = clientID, .appID = appID,
	      .operationIDs = ZuMv(operations), .state = State::Active,
	      .version = 1, .created = created, .updated = created,
	      .owner = saga->id()};
	    complete(row->commit());
	  });
      } else {
	saga->findDel<0>(context->adminAccess, 0,
	  ZuFwdTuple(ActorKind::Client, clientID, appID), ZuMv(complete),
	  [this](ZdbRow<AdminAccess> *row, auto &&complete) mutable {
	    if (!row || row->data().owner != saga->id()) {
	      complete(true); return;
	    }
	    complete(row->commit());
	  });
      }
    });
    return {};
  }

  ZdbSagaStep(6, zum.auth_policy, Insert) {
    context->authPolicies->run(0, [this, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	ZdbRowRef<AuthPolicy> row =
	  new ZdbRow<AuthPolicy>{context->authPolicies, ZdbShard{0}};
	saga->insert(context->authPolicies, ZuMv(row), ZuMv(complete),
	  [this](ZdbRow<AuthPolicy> *row, auto &&complete) mutable {
	    new (row->ptr()) AuthPolicy{.appID = appID, .localFirst = true,
	      .assignmentMaxAge = 300, .sessionIdle = 1800,
	      .sessionAbsolute = 43200, .tokenLifetime = 300,
	      .consentPolicy = ConsentPolicy::Explicit, .state = State::Active,
	      .version = 1, .created = created, .updated = created,
	      .owner = saga->id()};
	    complete(row->commit());
	  });
      } else {
	saga->findDel<0>(context->authPolicies, 0, ZuFwdTuple(appID),
	  ZuMv(complete), [this](ZdbRow<AuthPolicy> *row,
	      auto &&complete) mutable {
	    if (!row || row->data().owner != saga->id()) {
	      complete(true); return;
	    }
	    complete(row->commit());
	  });
      }
    });
    return {};
  }

#define ZUM_APP_ENROLL_RELEASE(N, member, tableName, keyExpr, optionalExpr) \
  ZdbSagaStep(N, zum.member, Update) { \
    if (optionalExpr) { saga->skip(ZuMv(complete)); return {}; } \
    context->tableName->run(0, [this, complete = ZuMv(complete)]() mutable { \
      saga->findUpd<0>(context->tableName, 0, keyExpr, ZuMv(complete), \
	[this](auto *row, auto &&complete) mutable { \
	  if (!row || row->data().owner != \
	      (Fwd ? saga->id() : uint128_t{0})) { \
	    complete(!Fwd); return; \
	  } \
	  row->data().owner = Fwd ? uint128_t{0} : saga->id(); \
	  complete(row->commit()); \
	}); \
    }); \
    return {}; \
  }

  ZUM_APP_ENROLL_RELEASE(7, sign_key, signKeys,
    ZuFwdTuple(signKey.id), !signKey.id)
  ZUM_APP_ENROLL_RELEASE(8, client, clients,
    ZuFwdTuple(clientID), false)
  ZUM_APP_ENROLL_RELEASE(9, client_access, clientAccess,
    ZuFwdTuple(clientID, appID), true)
  ZUM_APP_ENROLL_RELEASE(10, admin_access, adminAccess,
    ZuFwdTuple(ActorKind::Client, clientID, appID), false)
  ZUM_APP_ENROLL_RELEASE(11, auth_policy, authPolicies,
    ZuFwdTuple(appID), false)

#undef ZUM_APP_ENROLL_RELEASE

  ZdbSagaStep(12, zum.app, Update) {
    context->apps->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->apps, 0, ZuFwdTuple(appID),
	ZuMv(complete), [this](ZdbRow<App> *row,
	    auto &&complete) mutable {
	if (!row || row->data().state !=
	    (Fwd ? State::Pending : State::Active) ||
	    row->data().owner != (Fwd ? saga->id() : uint128_t{0})) {
	  complete(!Fwd); return;
	}
	row->data().state = Fwd ? State::Active : State::Pending;
	row->data().version = Fwd ? 2 : 1;
	row->data().owner = Fwd ? uint128_t{0} : saga->id();
	complete(row->commit());
      });
    });
    return {};
  }
  ZdbSagaStep(13, zum.request, Update) {
    StringVec ids;
    ids.push(String{} << appID);
    ids.push(clientID);
    requestComplete(this, ZuMv(ids), created, ZuMv(complete));
    return {};
  }

};

ZfbStruct(ZumAPI, AppEnrollment,
  (((coreAppID),	(Ctor<0>)),	(UInt64)),
  (((appID),		(Ctor<1>)),	(UInt64)),
  (((appName),		(Ctor<2>)),	(String)),
  (((appLabel),		(Ctor<3>)),	(String)),
  (((audience),	(Ctor<4>)),	(String)),
  (((signKey),		(Ctor<5>)),	(UDT)),
  (((clientID),		(Ctor<6>)),	(String)),
  (((secretDigest),	(Ctor<7>)),	(Bytes)),
  (((created),		(Ctor<8>)),	(Int64)),
  (((catalogPublishOp), (Ctor<9>)),	(UInt32)),
  (((operationQueryOp), (Ctor<10>)),	(UInt32)),
  (((request), (Ctor<11>)), (UDT)));

struct ExternalProjection : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"externalProjection.v1">;
  enum { NSteps = 5 };

  ProviderID	providerID = 0;
  String	issuer;
  String	subject;
  UserID	userID = 0;
  String	name;
  Bytes		handle;
  int64_t	created = 0;

  ZdbSagaStep(0, zum.ext_identity, Insert) {
    context->extIdentities->run(0, [this,
        complete = ZuMv(complete)]() mutable {
      auto key = ZuFwdTuple(providerID, issuer, subject);
      if constexpr (Fwd) {
	ZdbRowRef<ExtIdentity> row = new ZdbRow<ExtIdentity>{
	  context->extIdentities, ZdbShard{0}};
	saga->insert(context->extIdentities, ZuMv(row), ZuMv(complete),
	  [this](ZdbRow<ExtIdentity> *row, auto &&complete) mutable {
	    new (row->ptr()) ExtIdentity{.providerID = providerID,
	      .issuer = issuer, .subject = subject, .userID = userID,
	      .created = created, .updated = created, .owner = saga->id()};
	    complete(row->commit());
	  });
      } else {
	saga->findDel<0>(context->extIdentities, 0, ZuMv(key),
	  ZuMv(complete), [this](ZdbRow<ExtIdentity> *row,
	      auto &&complete) mutable {
	    if (!row || row->data().owner != saga->id()) {
	      complete(true);
	      return;
	    }
	    complete(row->commit());
	  });
      }
    });
    return {};
  }

  ZdbSagaStep(1, zum.user, Insert) {
    context->users->run(0, [this, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	ZdbRowRef<User> row = new ZdbRow<User>{context->users, ZdbShard{0}};
	saga->insert(context->users, ZuMv(row), ZuMv(complete),
	  [this](ZdbRow<User> *row, auto &&complete) mutable {
	    new (row->ptr()) User{.id = userID, .source = UserSource::External,
	      .name = name, .handle = handle, .created = created, .updated = created,
	      .state = State::Pending, .owner = saga->id()};
	    complete(row->commit());
	  });
      } else {
	saga->findDel<0>(context->users, 0, ZuFwdTuple(userID),
	  ZuMv(complete), [this](ZdbRow<User> *row,
	      auto &&complete) mutable {
	    if (!row || row->data().owner != saga->id() ||
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
	ZuMv(complete), [this](ZdbRow<User> *row,
	    auto &&complete) mutable {
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
	ZuMv(complete), [this](ZdbRow<User> *row,
	    auto &&complete) mutable {
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

  ZdbSagaStep(4, zum.ext_identity, Update) {
    context->extIdentities->run(0, [this,
        complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->extIdentities, 0,
	ZuFwdTuple(providerID, issuer, subject), ZuMv(complete),
	[this](ZdbRow<ExtIdentity> *row, auto &&complete) mutable {
	  if (!row || row->data().owner !=
	      (Fwd ? saga->id() : uint128_t{0})) {
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

ZfbStruct(ZumAPI, ExternalProjection,
  (((providerID),	(Ctor<0>)),	(UInt64)),
  (((issuer),		(Ctor<1>)),	(String)),
  (((subject),		(Ctor<2>)),	(String)),
  (((userID),		(Ctor<3>)),	(UInt64)),
  (((name),		(Ctor<4>)),	(String)),
  (((handle),		(Ctor<5>)),	(Bytes)),
  (((created),		(Ctor<6>)),	(Int64)));

// Keep the app reserved through action publication: failed creation restores
// its allocation/version snapshot before the app becomes available again.
struct AppActionAdd : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"appActionAdd.v2">;
  enum { NSteps = 6 };

  AppID		appID = 0;
  ActionID	actionID = 0;
  String	name;
  String	label;
  int64_t	created = 0;
  uint64_t	oldAppVersion = 0;
  uint64_t	oldAuthVersion = 0;
  int64_t	oldUpdated = 0;
  IdemRequest request;

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }

  ZdbSagaStep(1, zum.app, Update) {
    context->apps->run(0, [this, complete = ZuMv(complete)]() mutable {
      context->apps->find<0>(0, ZuFwdTuple(appID),
	[this, complete = ZuMv(complete)](ZdbRowRef<App> row) mutable {
	  if (!row) { complete(!Fwd); return; }
	  saga->update(context->apps, ZuMv(row), ZuMv(complete),
	    [this](ZdbRow<App> *row, auto &&complete) mutable {
	if constexpr (Fwd) {
	  if (!appID || !name || created <= 0 || row->data().owner ||
	      row->data().state != State::Active ||
	      row->data().nextActionID != actionID ||
	      row->data().version != oldAppVersion ||
	      row->data().authVersion != oldAuthVersion ||
	      row->data().updated != oldUpdated || actionID == UINT32_MAX ||
	      oldAppVersion == UINT64_MAX || oldAuthVersion == UINT64_MAX) {
	    complete(false);
	    return;
	  }
	  ++row->data().nextActionID;
	  ++row->data().version;
	  ++row->data().authVersion;
	  row->data().updated = created;
	  row->data().owner = saga->id();
	} else {
	  if (!row || row->data().owner != saga->id() ||
	      row->data().state != State::Active ||
	      row->data().nextActionID != actionID + 1 ||
	      row->data().version != oldAppVersion + 1 ||
	      row->data().authVersion != oldAuthVersion + 1 ||
	      row->data().updated != created) {
	    complete(true);
	    return;
	  }
	  row->data().nextActionID = actionID;
	  row->data().version = oldAppVersion;
	  row->data().authVersion = oldAuthVersion;
	  row->data().updated = oldUpdated;
	  row->data().owner = 0;
	}
	complete(row->commit());
	  });
      });
    });
    return {};
  }

  ZdbSagaStep(2, zum.action, Insert) {
    context->actions->run(0, [this, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	// Name uniqueness is a business constraint. Apply it only when Zdb
	// executes the insertion; replay handling belongs to the native wrapper.
	context->actions->find<1>(0, ZuFwdTuple(appID, name),
	  [this, complete = ZuMv(complete)](
	      ZdbRowRef<Action> existing) mutable {
	  ZdbRowRef<Action> row =
	    new ZdbRow<Action>{context->actions, ZdbShard{0}};
	  saga->insert(context->actions, ZuMv(row), ZuMv(complete),
	    [this, duplicate = bool(existing)](ZdbRow<Action> *row,
		auto &&complete) mutable {
	      new (row->ptr()) Action{.appID = appID, .id = actionID,
		.name = name, .label = label, .state = State::Active,
		.origin = Origin::Custom, .version = 1,
		.created = created, .updated = created, .owner = saga->id()};
	      if (duplicate) { complete(false); return; }
	      complete(row->commit());
	    });
	});
      } else {
	saga->findDel<0>(context->actions, 0,
	  ZuFwdTuple(appID, actionID), ZuMv(complete),
	  [this](ZdbRow<Action> *row, auto &&complete) mutable {
	    if (!row || row->data().owner != saga->id()) {
	      complete(true);
	      return;
	    }
	    complete(row->commit());
	  });
      }
    });
    return {};
  }

  ZdbSagaStep(3, zum.action, Update) {
    context->actions->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->actions, 0,
	ZuFwdTuple(appID, actionID), ZuMv(complete),
	[this](ZdbRow<Action> *row, auto &&complete) mutable {
	  if (!row || row->data().name != name ||
	      row->data().origin != Origin::Custom ||
	      row->data().state != State::Active ||
	      row->data().owner !=
		(Fwd ? saga->id() : uint128_t{0})) {
	    complete(!Fwd);
	    return;
	  }
	  row->data().owner = Fwd ? uint128_t{0} : saga->id();
	  complete(row->commit());
	});
    });
    return {};
  }

  ZdbSagaStep(4, zum.app, Update) {
    context->apps->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->apps, 0, ZuFwdTuple(appID),
	ZuMv(complete), [this](ZdbRow<App> *row,
	    auto &&complete) mutable {
	if (!row || row->data().state != State::Active ||
	    row->data().nextActionID != actionID + 1 ||
	    row->data().version != oldAppVersion + 1 ||
	    row->data().authVersion != oldAuthVersion + 1 ||
	    row->data().updated != created ||
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
  ZdbSagaStep(5, zum.request, Update) {
    StringVec ids;
    ids.push(String{} << actionID);
    requestComplete(this, ZuMv(ids), created, ZuMv(complete));
    return {};
  }

};

ZfbStruct(ZumAPI, AppActionAdd,
  (((appID),		(Ctor<0>)),	(UInt64)),
  (((actionID),		(Ctor<1>)),	(UInt32)),
  (((name),		(Ctor<2>)),	(String)),
  (((label),		(Ctor<3>)),	(String)),
  (((created),		(Ctor<4>)),	(Int64)),
  (((oldAppVersion),	(Ctor<5>)),	(UInt64)),
  (((oldAuthVersion),	(Ctor<6>)),	(UInt64)),
  (((oldUpdated),	(Ctor<7>)),	(Int64)),
  (((request), (Ctor<8>)), (UDT)));

// Both role and state changes use this transaction so neither can write through
// the other's membership/application reservation.
struct MembershipChange : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"membershipChange.v2">;
  enum { NSteps = 6 };

  AppID		appID = 0;
  UserID	userID = 0;
  IDVec		oldRoles;
  IDVec		newRoles;
  State::T	oldState = State::Pending;
  State::T	newState = State::Pending;
  uint64_t	version = 0;
  uint64_t	authVersion = 0;
  int64_t	oldUpdated = 0;
  int64_t	updated = 0;
  uint64_t	appVersion = 0;
  uint64_t	appAuthVersion = 0;
  int64_t	appUpdated = 0;

  IdemRequest request;

  bool assignRoles = false;
  String ifMatch;
  unsigned error = 0; // Live response only; not recovery state.

  ZumAPI bool appValid(const App &, bool) const;
  ZumAPI unsigned memberError(const Membership &, bool) const;
  ZumAPI void validate(Zdb_::SagaCompleteFn);
  ZumAPI void roles(unsigned, Zdb_::SagaCompleteFn);

  bool unchanged() const { return oldRoles == newRoles && oldState == newState; }

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }

  ZdbSagaStep(1, zum.app, Update) {
    if constexpr (!Fwd)
      if (unchanged()) { saga->skip(ZuMv(complete)); return {}; }
    context->apps->run(0, [this, complete = ZuMv(complete)]() mutable {
      context->apps->find<0>(0, ZuFwdTuple(appID),
	[this, complete = ZuMv(complete)](ZdbRowRef<App> row) mutable {
	  if (!row) { error = 409; complete(!Fwd); return; }
	  if (unchanged()) {
	    if (appValid(row->data(), Fwd)) saga->skip(ZuMv(complete));
	    else { error = 409; complete(false); }
	    return;
	  }
	  saga->update(context->apps, ZuMv(row), ZuMv(complete),
	    [this](ZdbRow<App> *row, auto &&complete) mutable {
	      if (!appValid(row->data(), Fwd)) {
		error = 409;
		complete(!Fwd);
		return;
	      }
	      row->data().owner = Fwd ? saga->id() : uint128_t{0};
	      complete(row->commit());
	    });
	});
    });
    return {};
  }

  ZdbSagaStep(2, zum.membership, Update) {
    if constexpr (!Fwd)
      if (unchanged()) { saga->skip(ZuMv(complete)); return {}; }
    auto apply = [this, complete = ZuMv(complete)](bool valid) mutable {
      context->memberships->run(0, [this, valid, complete = ZuMv(complete)]() mutable {
	context->memberships->find<0>(0, ZuFwdTuple(appID, userID),
	  [this, valid, complete = ZuMv(complete)](ZdbRowRef<Membership> row) mutable {
	    if (!row) { error = 404; complete(!Fwd); return; }
	    if (unchanged()) {
	      if (valid) error = memberError(row->data(), Fwd);
	      if (!valid || error) complete(false);
	      else saga->skip(ZuMv(complete));
	      return;
	    }
	    saga->update(context->memberships, ZuMv(row), ZuMv(complete),
	      [this, valid](ZdbRow<Membership> *row, auto &&complete) mutable {
		if (!valid) { complete(false); return; }
		if (auto code = memberError(row->data(), Fwd)) {
		  error = code;
		  complete(!Fwd);
		  return;
		}
		auto &item = row->data();
		item.roleIDs = Fwd ? newRoles : oldRoles;
		item.state = Fwd ? newState : oldState;
		item.version = version + Fwd;
		item.authVersion = authVersion + Fwd;
		item.updated = Fwd ? updated : oldUpdated;
		item.owner = Fwd ? saga->id() : uint128_t{0};
		complete(row->commit());
	      });
	  });
      });
    };
    if constexpr (Fwd) validate(ZuMv(apply));
    else apply(true);
    return {};
  }

  ZdbSagaStep(3, zum.membership, Update) {
    if (unchanged()) { saga->skip(ZuMv(complete)); return {}; }
    context->memberships->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->memberships, 0, ZuFwdTuple(appID, userID),
	ZuMv(complete), [this](ZdbRow<Membership> *row, auto &&complete) mutable {
	  if (!row || row->data().owner !=
		(Fwd ? saga->id() : uint128_t{0}) ||
	      row->data().version != version + 1 ||
	      row->data().authVersion != authVersion + 1 ||
	      row->data().state != newState || row->data().roleIDs != newRoles) {
	    complete(!Fwd);
	    return;
	  }
	  row->data().owner = Fwd ? uint128_t{0} : saga->id();
	  complete(row->commit());
	});
    });
    return {};
  }

  ZdbSagaStep(4, zum.app, Update) {
    if (unchanged()) { saga->skip(ZuMv(complete)); return {}; }
    context->apps->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->apps, 0, ZuFwdTuple(appID), ZuMv(complete),
	[this](ZdbRow<App> *row, auto &&complete) mutable {
	  if (!row || row->data().owner != (Fwd ? saga->id() : uint128_t{0}) ||
	      row->data().state != State::Active ||
	      row->data().version != appVersion + !Fwd ||
	      row->data().authVersion != appAuthVersion + !Fwd ||
	      row->data().updated != (Fwd ? appUpdated : updated)) {
	    complete(false);
	    return;
	  }
	  row->data().version = appVersion + Fwd;
	  row->data().authVersion = appAuthVersion + Fwd;
	  row->data().updated = Fwd ? updated : appUpdated;
	  row->data().owner = Fwd ? uint128_t{0} : saga->id();
	  complete(row->commit());
	});
    });
    return {};
  }
  ZdbSagaStep(5, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }
};

ZfbStruct(ZumAPI, MembershipChange,
  (((appID), (Ctor<0>)), (UInt64)),
  (((userID), (Ctor<1>)), (UInt64)),
  (((oldRoles), (Ctor<2>)), (UInt64Vec)),
  (((newRoles), (Ctor<3>)), (UInt64Vec)),
  (((oldState), (Ctor<4>, Enum<State::Map>)), (Int8)),
  (((newState), (Ctor<5>, Enum<State::Map>)), (Int8)),
  (((version), (Ctor<6>)), (UInt64)),
  (((authVersion), (Ctor<7>)), (UInt64)),
  (((oldUpdated), (Ctor<8>)), (Int64)),
  (((updated), (Ctor<9>)), (Int64)),
  (((appVersion), (Ctor<10>)), (UInt64)),
  (((appAuthVersion), (Ctor<11>)), (UInt64)),
  (((appUpdated), (Ctor<12>)), (Int64)),
  (((request), (Ctor<13>)), (UDT)),
  (((assignRoles), (Ctor<14>)), (Bool)),
  (((ifMatch), (Ctor<15>)), (String)));

struct MembershipAdd : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"membershipAdd.v1">;
  enum { NSteps = 4 };

  AppID appID = 0;
  UserID userID = 0;
  int64_t created = 0;
  IdemRequest request;
  unsigned error = 503; // Live response only; not recovery state.

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }

  ZdbSagaStep(1, zum.membership, Insert) {
    if constexpr (!Fwd) {
      context->memberships->run(0, [this, complete = ZuMv(complete)]() mutable {
	saga->findDel<0>(context->memberships, 0, ZuFwdTuple(appID, userID),
	  ZuMv(complete), [](ZdbRow<Membership> *row, auto &&complete) mutable {
	    complete(!row || row->commit());
	  });
      });
      return {};
    }
    context->apps->run(0, [this, complete = ZuMv(complete)]() mutable {
      context->apps->find<0>(0, ZuFwdTuple(appID),
	[this, complete = ZuMv(complete)](ZdbRowRef<App> app) mutable {
	  bool valid = app && app->data().state == State::Active && !app->data().owner;
	  context->users->run(0, [this, valid, complete = ZuMv(complete)]() mutable {
	    context->users->find<0>(0, ZuFwdTuple(userID),
	      [this, valid, complete = ZuMv(complete)](ZdbRowRef<User> user) mutable {
		bool local = user && user->data().source == UserSource::Local &&
		  user->data().state != State::Revoked && !user->data().owner;
		context->memberships->run(0, [this, valid = valid && local,
		    complete = ZuMv(complete)]() mutable {
		  context->memberships->find<0>(0, ZuFwdTuple(appID, userID),
		    [this, valid, complete = ZuMv(complete)](
			ZdbRowRef<Membership> existing) mutable {
		      ZdbRowRef<Membership> row =
			new ZdbRow<Membership>{context->memberships, ZdbShard{0}};
		      saga->insert(context->memberships, ZuMv(row), ZuMv(complete),
			[this, valid, duplicate = bool(existing)](
			    ZdbRow<Membership> *row, auto &&complete) mutable {
			  new (row->ptr()) Membership{.appID = appID, .userID = userID,
			    .state = State::Active, .version = 1, .created = created,
			    .updated = created, .owner = saga->id()};
			  if (!valid || duplicate) {
			    error = valid ? 409 : 404;
			    complete(false); return;
			  }
			  complete(row->commit());
			});
		    });
		});
	      });
	  });
	});
    });
    return {};
  }

  ZdbSagaStep(2, zum.membership, Update) {
    context->memberships->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->memberships, 0, ZuFwdTuple(appID, userID),
	ZuMv(complete), [this](ZdbRow<Membership> *row, auto &&complete) mutable {
	  if (!row) { complete(!Fwd); return; }
	  row->data().owner = Fwd ? uint128_t{0} : saga->id();
	  complete(row->commit());
	});
    });
    return {};
  }

  ZdbSagaStep(3, zum.request, Update) {
    StringVec ids;
    ids.push(String{} << appID << ':' << userID);
    requestComplete(this, ZuMv(ids), created, ZuMv(complete));
    return {};
  }
};
ZfbStruct(ZumAPI, MembershipAdd,
  (((appID), (Ctor<0>)), (UInt64)),
  (((userID), (Ctor<1>)), (UInt64)),
  (((created), (Ctor<2>)), (Int64)),
  (((request), (Ctor<3>)), (UDT)));

struct RoleEdit : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"roleEdit.v1">;
  enum { NSteps = 6 };
  enum { Actions, Label, Status };

  App app;
  Role before;
  ActionIDVec actionIDs;
  String ifMatch;
  int64_t updated = 0;
  IdemRequest request;
  int8_t kind = Actions;
  String label;
  State::T state = State::Pending;
  ZtBitmap actions; // Reconstructed by the step; not recovery state.
  unsigned error = 0;

  bool unchanged() const { return kind == Status && state == before.state; }
  ZumAPI unsigned appError(const App &) const;
  ZumAPI unsigned roleError(const Role &) const;
  ZumAPI void validate(unsigned, Zdb_::SagaCompleteFn);

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }

  ZdbSagaStep(1, zum.app, Update) {
    appEditStart<Fwd>(this, ZuMv(complete));
    return {};
  }

  ZdbSagaStep(2, zum.role, Update) {
    if constexpr (!Fwd)
      if (unchanged()) { saga->skip(ZuMv(complete)); return {}; }
    auto apply = [this, complete = ZuMv(complete)](bool valid) mutable {
      context->roles->run(0, [this, valid, complete = ZuMv(complete)]() mutable {
	context->roles->find<0>(0, ZuFwdTuple(app.id, before.id),
	  [this, valid, complete = ZuMv(complete)](ZdbRowRef<Role> row) mutable {
	    if (!row) { error = 404; complete(!Fwd); return; }
	    if (unchanged()) {
	      error = roleError(row->data());
	      if (error) complete(false);
	      else saga->skip(ZuMv(complete));
	      return;
	    }
	    saga->update(context->roles, ZuMv(row), ZuMv(complete),
	      [this, valid](ZdbRow<Role> *row, auto &&complete) mutable {
		if (Fwd && !valid) { error = 400; complete(false); return; }
		if constexpr (Fwd) {
		  if (auto code = roleError(row->data())) {
		    error = code; complete(false); return;
		  }
		  switch (kind) {
		    case Label: row->data().label = label; break;
		    case Status: row->data().state = state; break;
		    default: row->data().actions = actions; break;
		  }
		  row->data().version = before.version + 1;
		  row->data().updated = updated;
		  row->data().owner = saga->id();
		} else row->data() = before;
		complete(row->commit());
	      });
	  });
      });
    };
    if constexpr (Fwd) validate(0, ZuMv(apply));
    else apply(true);
    return {};
  }

  ZdbSagaStep(3, zum.role, Update) {
    appEditRelease<Fwd>(this, context->roles, ZuMv(complete));
    return {};
  }

  ZdbSagaStep(4, zum.app, Update) {
    appEditPublish<Fwd>(this, kind != Label, ZuMv(complete));
    return {};
  }

  ZdbSagaStep(5, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }
};
ZfbStruct(ZumAPI, RoleEdit,
  (((app), (Ctor<0>)), (UDT)),
  (((before), (Ctor<1>)), (UDT)),
  (((actionIDs), (Ctor<2>)), (UInt32Vec)),
  (((ifMatch), (Ctor<3>)), (String)),
  (((updated), (Ctor<4>)), (Int64)),
  (((request), (Ctor<5>)), (UDT)),
  (((kind), (Ctor<6>)), (Int8)),
  (((label), (Ctor<7>)), (String)),
  (((state), (Ctor<8>, Enum<State::Map>)), (Int8)));

struct ActionEdit : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"actionEdit.v1">;
  enum { NSteps = 6 };

  App app;
  Action before;
  String ifMatch;
  int64_t updated = 0;
  IdemRequest request;
  State::T state = State::Pending;
  unsigned error = 0;

  bool unchanged() const { return state == before.state; }
  ZumAPI unsigned appError(const App &) const;
  ZumAPI unsigned actionError(const Action &) const;

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(1, zum.app, Update) {
    appEditStart<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(2, zum.action, Update) {
    if constexpr (!Fwd)
      if (unchanged()) { saga->skip(ZuMv(complete)); return {}; }
    context->actions->run(0, [this, complete = ZuMv(complete)]() mutable {
      context->actions->find<0>(0, ZuFwdTuple(app.id, before.id),
	[this, complete = ZuMv(complete)](ZdbRowRef<Action> row) mutable {
	  if (!row) { error = 404; complete(!Fwd); return; }
	  if (unchanged()) {
	    error = actionError(row->data());
	    if (error) complete(false);
	    else saga->skip(ZuMv(complete));
	    return;
	  }
	  saga->update(context->actions, ZuMv(row), ZuMv(complete),
	    [this](ZdbRow<Action> *row, auto &&complete) mutable {
	      if constexpr (Fwd) {
		if (auto code = actionError(row->data())) {
		  error = code; complete(false); return;
		}
		row->data().state = state;
		row->data().version = before.version + 1;
		row->data().updated = updated;
		row->data().owner = saga->id();
	      } else row->data() = before;
	      complete(row->commit());
	    });
	});
    });
    return {};
  }
  ZdbSagaStep(3, zum.action, Update) {
    appEditRelease<Fwd>(this, context->actions, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(4, zum.app, Update) {
    appEditPublish<Fwd>(this, true, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(5, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }
};
ZfbStruct(ZumAPI, ActionEdit,
  (((app), (Ctor<0>)), (UDT)),
  (((before), (Ctor<1>)), (UDT)),
  (((ifMatch), (Ctor<2>)), (String)),
  (((updated), (Ctor<3>)), (Int64)),
  (((request), (Ctor<4>)), (UDT)),
  (((state), (Ctor<5>, Enum<State::Map>)), (Int8)));

struct AppChange : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"appChange.v1">;
  enum { NSteps = 4 };

  App before;
  String ifMatch;
  String label;
  int64_t updated = 0;
  IdemRequest request;
  bool stateOnly = false;
  State::T state = State::Pending;
  unsigned error = 0;

  bool unchanged() const { return stateOnly && state == before.state; }
  ZumAPI unsigned appError(const App &) const;

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(1, zum.app, Update) {
    if constexpr (!Fwd)
      if (unchanged()) { saga->skip(ZuMv(complete)); return {}; }
    context->apps->run(0, [this, complete = ZuMv(complete)]() mutable {
      context->apps->find<0>(0, ZuFwdTuple(before.id),
	[this, complete = ZuMv(complete)](ZdbRowRef<App> row) mutable {
	  if (!row) { error = 404; complete(!Fwd); return; }
	  if (unchanged()) {
	    error = appError(row->data());
	    if (error) complete(false);
	    else saga->skip(ZuMv(complete));
	    return;
	  }
	  saga->update(context->apps, ZuMv(row), ZuMv(complete),
	    [this](ZdbRow<App> *row, auto &&complete) mutable {
	      if constexpr (Fwd) {
		if (auto code = appError(row->data())) {
		  error = code; complete(false); return;
		}
		if (stateOnly) row->data().state = state;
		else row->data().label = label;
		row->data().version = before.version + 1;
		row->data().authVersion = before.authVersion + stateOnly;
		row->data().updated = updated;
		row->data().owner = saga->id();
	      } else row->data() = before;
	      complete(row->commit());
	    });
	});
    });
    return {};
  }
  ZdbSagaStep(2, zum.app, Update) {
    if (unchanged()) { saga->skip(ZuMv(complete)); return {}; }
    context->apps->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->apps, 0, ZuFwdTuple(before.id),
	ZuMv(complete), [this](ZdbRow<App> *row, auto &&complete) mutable {
	  if (!row) { complete(!Fwd); return; }
	  row->data().owner = Fwd ? uint128_t{0} : saga->id();
	  complete(row->commit());
	});
    });
    return {};
  }
  ZdbSagaStep(3, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }
};
ZfbStruct(ZumAPI, AppChange,
  (((before), (Ctor<0>)), (UDT)),
  (((ifMatch), (Ctor<1>)), (String)),
  (((label), (Ctor<2>)), (String)),
  (((updated), (Ctor<3>)), (Int64)),
  (((request), (Ctor<4>)), (UDT)),
  (((stateOnly), (Ctor<5>)), (Bool)),
  (((state), (Ctor<6>, Enum<State::Map>)), (Int8)));

struct UserEdit : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"userEdit.v1">;
  enum { NSteps = 4 };

  User before;
  String ifMatch;
  String profile;
  int64_t updated = 0;
  IdemRequest request;
  bool stateOnly = false;
  State::T state = State::Pending;
  String email;
  uint8_t fields = 0;
  unsigned error = 0;

  bool unchanged() const { return stateOnly && state == before.state; }
  ZumAPI unsigned userError(const User &) const;

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(1, zum.user, Update) {
    if constexpr (!Fwd)
      if (unchanged()) { saga->skip(ZuMv(complete)); return {}; }
    context->users->run(0, [this, complete = ZuMv(complete)]() mutable {
      context->users->find<0>(0, ZuFwdTuple(before.id),
	[this, complete = ZuMv(complete)](ZdbRowRef<User> row) mutable {
	  if (!row) { error = 404; complete(!Fwd); return; }
	  if (unchanged()) {
	    error = userError(row->data());
	    if (error) complete(false);
	    else saga->skip(ZuMv(complete));
	    return;
	  }
	  saga->update(context->users, ZuMv(row), ZuMv(complete),
	    [this](ZdbRow<User> *row, auto &&complete) mutable {
	      if constexpr (Fwd) {
		if (auto code = userError(row->data())) {
		  error = code; complete(false); return;
		}
		if (stateOnly) row->data().state = state;
		else {
		  if (fields & 1U) row->data().profile = profile;
		  if (fields & 2U) row->data().email = email;
		}
		row->data().version = before.version + 1;
		row->data().authVersion = before.authVersion + stateOnly;
		row->data().updated = updated;
		row->data().owner = saga->id();
	      } else row->data() = before;
	      complete(row->commit());
	    });
	});
    });
    return {};
  }
  ZdbSagaStep(2, zum.user, Update) {
    if (unchanged()) { saga->skip(ZuMv(complete)); return {}; }
    context->users->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->users, 0, ZuFwdTuple(before.id),
	ZuMv(complete), [this](ZdbRow<User> *row, auto &&complete) mutable {
	  if (!row) { complete(!Fwd); return; }
	  row->data().owner = Fwd ? uint128_t{0} : saga->id();
	  complete(row->commit());
	});
    });
    return {};
  }
  ZdbSagaStep(3, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }
};
ZfbStruct(ZumAPI, UserEdit,
  (((before), (Ctor<0>)), (UDT)),
  (((ifMatch), (Ctor<1>)), (String)),
  (((profile), (Ctor<2>)), (String)),
  (((updated), (Ctor<3>)), (Int64)),
  (((request), (Ctor<4>)), (UDT)),
  (((stateOnly), (Ctor<5>)), (Bool)),
  (((state), (Ctor<6>, Enum<State::Map>)), (Int8)),
  (((email), (Ctor<7>)), (String)),
  (((fields), (Ctor<8>)), (UInt8)));

struct CredEdit : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"credEdit.v1">;
  enum { NSteps = 4 };

  Cred before;
  String ifMatch;
  String label;
  int64_t updated = 0;
  IdemRequest request;
  bool stateOnly = false;
  State::T state = State::Pending;
  unsigned error = 0;

  bool unchanged() const { return stateOnly && state == before.state; }
  ZumAPI unsigned credError(const Cred &) const;

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(1, zum.cred, Update) {
    if constexpr (!Fwd)
      if (unchanged()) { saga->skip(ZuMv(complete)); return {}; }
    context->creds->run(0, [this, complete = ZuMv(complete)]() mutable {
      context->creds->find<0>(0, ZuFwdTuple(before.id),
	[this, complete = ZuMv(complete)](ZdbRowRef<Cred> row) mutable {
	  if (!row) { error = 404; complete(!Fwd); return; }
	  if (unchanged()) {
	    error = credError(row->data());
	    if (error) complete(false);
	    else saga->skip(ZuMv(complete));
	    return;
	  }
	  saga->update(context->creds, ZuMv(row), ZuMv(complete),
	    [this](ZdbRow<Cred> *row, auto &&complete) mutable {
	      if constexpr (Fwd) {
		if (auto code = credError(row->data())) {
		  error = code; complete(false); return;
		}
		if (stateOnly) row->data().state = state;
		else row->data().label = label;
		row->data().version = before.version + 1;
		row->data().updated = updated;
		row->data().owner = saga->id();
	      } else {
		if (stateOnly) row->data().state = before.state;
		else row->data().label = before.label;
		row->data().version = before.version;
		row->data().updated = before.updated;
		row->data().owner = before.owner;
	      }
	      complete(row->commit());
	    });
	});
    });
    return {};
  }
  ZdbSagaStep(2, zum.cred, Update) {
    if (unchanged()) { saga->skip(ZuMv(complete)); return {}; }
    context->creds->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->creds, 0, ZuFwdTuple(before.id),
	ZuMv(complete), [this](ZdbRow<Cred> *row, auto &&complete) mutable {
	  if (!row) { complete(!Fwd); return; }
	  row->data().owner = Fwd ? uint128_t{0} : saga->id();
	  complete(row->commit());
	});
    });
    return {};
  }
  ZdbSagaStep(3, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }
};
ZfbStruct(ZumAPI, CredEdit,
  (((before), (Ctor<0>)), (UDT)),
  (((ifMatch), (Ctor<1>)), (String)),
  (((label), (Ctor<2>)), (String)),
  (((updated), (Ctor<3>)), (Int64)),
  (((request), (Ctor<4>)), (UDT)),
  (((stateOnly), (Ctor<5>)), (Bool)),
  (((state), (Ctor<6>, Enum<State::Map>)), (Int8)));

struct ProviderEdit : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"providerEdit.v1">;
  enum { NSteps = 4 };

  Provider before;
  String ifMatch;
  Provider values;
  uint8_t fields = 0;
  int64_t updated = 0;
  IdemRequest request;
  bool stateOnly = false;
  State::T state = State::Pending;
  unsigned error = 0;

  bool unchanged() const { return stateOnly && state == before.state; }
  ZumAPI unsigned recordError(const Provider &) const;
  ZumAPI void edit(Provider &) const;

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(1, zum.provider, Update) {
    recordEdit<Fwd>(this, context->providers, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(2, zum.provider, Update) {
    appEditRelease<Fwd>(this, context->providers, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(3, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }
};
ZfbStruct(ZumAPI, ProviderEdit,
  (((before), (Ctor<0>)), (UDT)),
  (((ifMatch), (Ctor<1>)), (String)),
  (((values), (Ctor<2>)), (UDT)),
  (((updated), (Ctor<4>)), (Int64)),
  (((request), (Ctor<5>)), (UDT)),
  (((stateOnly), (Ctor<6>)), (Bool)),
  (((state), (Ctor<7>, Enum<State::Map>)), (Int8)),
  (((fields), (Ctor<3>)), (UInt8)));

ZumAPI bool clientConfigValid(ClientType::T, uint8_t, bool, const StringVec &);

struct ClientEdit : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"clientEdit.v1">;
  enum { NSteps = 4, Secret = 16 };

  Client before;
  String ifMatch;
  Client values;
  uint8_t fields = 0;
  int64_t updated = 0;
  IdemRequest request;
  bool stateOnly = false;
  State::T state = State::Pending;
  uint32_t overlapSeconds = 0;
  unsigned error = 0;

  bool unchanged() const { return stateOnly && state == before.state; }
  ZumAPI unsigned recordError(const Client &) const;
  ZumAPI void edit(Client &) const;
  ZumAPI Client result() const;

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(1, zum.client, Update) {
    recordEdit<Fwd>(this, context->clients, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(2, zum.client, Update) {
    appEditRelease<Fwd>(this, context->clients, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(3, zum.request, Update) {
    StringVec ids;
    if (fields == Secret) ids.push(before.id);
    requestComplete(this, ZuMv(ids), updated, ZuMv(complete));
    return {};
  }
};
ZfbStruct(ZumAPI, ClientEdit,
  (((before), (Ctor<0>)), (UDT)),
  (((ifMatch), (Ctor<1>)), (String)),
  (((values), (Ctor<2>)), (UDT)),
  (((updated), (Ctor<4>)), (Int64)),
  (((request), (Ctor<5>)), (UDT)),
  (((stateOnly), (Ctor<6>)), (Bool)),
  (((state), (Ctor<7>, Enum<State::Map>)), (Int8)),
  (((fields), (Ctor<3>)), (UInt8)),
  (((overlapSeconds), (Ctor<8>)), (UInt32)));

struct KeyRetire : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"keyRetire.v1">;
  enum { NSteps = 3 };

  SignKey before;
  String ifMatch;
  int64_t retireAfter = 0;
  int64_t updated = 0;
  IdemRequest request;
  unsigned error = 0;

  bool unchanged() const { return false; }
  ZumAPI unsigned recordError(const SignKey &) const;

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(1, zum.sign_key, Update) {
    context->signKeys->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0, ZuSeq<1, 2>>(context->signKeys, 0, ZuFwdTuple(before.id),
	ZuMv(complete), [this](ZdbRow<SignKey> *row, auto &&complete) mutable {
	  if (!row) { error = 404; complete(!Fwd); return; }
	  if constexpr (Fwd) {
	    if (auto code = recordError(row->data())) {
	      error = code; complete(false); return;
	    }
	    row->data().retireAfter = retireAfter;
	    row->data().state = State::Suspended;
	    row->data().version = before.version + 1;
	    row->data().updated = updated;
	  } else {
	    row->data().retireAfter = before.retireAfter;
	    row->data().state = before.state;
	    row->data().version = before.version;
	    row->data().updated = before.updated;
	  }
	  complete(row->commit());
	});
    });
    return {};
  }
  ZdbSagaStep(2, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }
};
ZfbStruct(ZumAPI, KeyRetire,
  (((before), (Ctor<0>)), (UDT)),
  (((ifMatch), (Ctor<1>)), (String)),
  (((retireAfter), (Ctor<2>)), (Int64)),
  (((updated), (Ctor<3>)), (Int64)),
  (((request), (Ctor<4>)), (UDT)));

struct ClientAccessState : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"clientAccessState.v1">;
  enum { NSteps = 4, stateOnly = false };

  ClientAccess before;
  String ifMatch;
  int64_t updated = 0;
  IdemRequest request;
  State::T state = State::Pending;
  unsigned error = 0;

  bool unchanged() const { return state == before.state; }
  ZumAPI unsigned recordError(const ClientAccess &) const;
  void edit(ClientAccess &item) const
  {
    item.state = state;
    item.authVersion = before.authVersion + 1;
  }

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(1, zum.client_access, Update) {
    recordEdit<Fwd>(this, context->clientAccess, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(2, zum.client_access, Update) {
    appEditRelease<Fwd>(this, context->clientAccess, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(3, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }
};
ZfbStruct(ZumAPI, ClientAccessState,
  (((before), (Ctor<0>)), (UDT)),
  (((ifMatch), (Ctor<1>)), (String)),
  (((updated), (Ctor<2>)), (Int64)),
  (((request), (Ctor<3>)), (UDT)),
  (((state), (Ctor<4>, Enum<State::Map>)), (Int8)));

struct AdminAccessState : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"adminAccessState.v1">;
  enum { NSteps = 4, stateOnly = false };

  AdminAccess before;
  String ifMatch;
  int64_t updated = 0;
  IdemRequest request;
  State::T state = State::Pending;
  unsigned error = 0;

  bool unchanged() const { return state == before.state; }
  ZumAPI unsigned recordError(const AdminAccess &) const;
  void edit(AdminAccess &item) const
  {
    item.state = state;
  }

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(1, zum.admin_access, Update) {
    recordEdit<Fwd>(this, context->adminAccess, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(2, zum.admin_access, Update) {
    appEditRelease<Fwd>(this, context->adminAccess, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(3, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }
};
ZfbStruct(ZumAPI, AdminAccessState,
  (((before), (Ctor<0>)), (UDT)),
  (((ifMatch), (Ctor<1>)), (String)),
  (((updated), (Ctor<2>)), (Int64)),
  (((request), (Ctor<3>)), (UDT)),
  (((state), (Ctor<4>, Enum<State::Map>)), (Int8)));

struct RoleMapDelete : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"roleMapDelete.v1">;
  enum { NSteps = 5 };

  App app;
  RoleMap before;
  String ifMatch;
  int64_t updated = 0;
  IdemRequest request;
  unsigned error = 0;

  bool unchanged() const { return false; }
  ZumAPI unsigned appError(const App &) const;
  ZumAPI unsigned recordError(const RoleMap &) const;

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(1, zum.app, Update) {
    appEditStart<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(2, zum.role_map, Delete) {
    auto table = context->roleMaps;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
        RoleMapTable::Key<0> key{ZuStructKey<0>(before)};
        saga->findDel<0>(table, 0, key, ZuMv(complete),
	  [this](ZdbRow<RoleMap> *row, auto &&complete) mutable {
	    if (!row) { error = 404; complete(false); return; }
	    if (auto code = recordError(row->data())) {
	      error = code; complete(false); return;
	    }
	    complete(row->commit());
	  });
      } else {
        ZdbRowRef<RoleMap> row = new ZdbRow<RoleMap>{table, ZdbShard{0}};
        saga->insert(table, ZuMv(row), ZuMv(complete),
	  [this](ZdbRow<RoleMap> *row, auto &&complete) mutable {
	    new (row->ptr()) RoleMap{before};
	    complete(row->commit());
	  });
      }
    });
    return {};
  }
  ZdbSagaStep(3, zum.app, Update) {
    appEditPublish<Fwd>(this, true, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(4, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }
};
ZfbStruct(ZumAPI, RoleMapDelete,
  (((app), (Ctor<0>)), (UDT)),
  (((before), (Ctor<1>)), (UDT)),
  (((ifMatch), (Ctor<2>)), (String)),
  (((updated), (Ctor<3>)), (Int64)),
  (((request), (Ctor<4>)), (UDT)));

struct RoleMapPut : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"roleMapPut.v1">;
  enum { NSteps = 7 };

  App app;
  RoleMap before;
  RoleID roleID = 0;
  String ifMatch;
  String ifNoneMatch;
  int64_t updated = 0;
  IdemRequest request;
  unsigned error = 0;

  bool unchanged() const { return false; }
  ZumAPI unsigned appError(const App &) const;
  ZumAPI unsigned recordError(const RoleMap &) const;
  ZumAPI void validate(Zdb_::SagaCompleteFn);
  ZumAPI RoleMap result() const;

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(1, zum.app, Update) {
    appEditStart<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(2, zum.role_map, Insert) {
    recordPut<Fwd, true>(this, context->roleMaps, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(3, zum.role_map, Update) {
    recordPut<Fwd, false>(this, context->roleMaps, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(4, zum.role_map, Update) {
    appEditRelease<Fwd>(this, context->roleMaps, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(5, zum.app, Update) {
    appEditPublish<Fwd>(this, true, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(6, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }
};
ZfbStruct(ZumAPI, RoleMapPut,
  (((app), (Ctor<0>)), (UDT)),
  (((before), (Ctor<1>)), (UDT)),
  (((roleID), (Ctor<2>)), (UInt64)),
  (((ifMatch), (Ctor<3>)), (String)),
  (((ifNoneMatch), (Ctor<4>)), (String)),
  (((updated), (Ctor<5>)), (Int64)),
  (((request), (Ctor<6>)), (UDT)));

struct PolicyPut : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"policyPut.v1">;
  enum { NSteps = 7 };

  App app;
  AuthPolicy before;
  AuthPolicy values;
  String ifMatch;
  String ifNoneMatch;
  int64_t updated = 0;
  IdemRequest request;
  unsigned error = 0;

  bool unchanged() const { return false; }
  ZumAPI unsigned appError(const App &) const;
  ZumAPI unsigned recordError(const AuthPolicy &) const;
  ZumAPI void validate(Zdb_::SagaCompleteFn);
  ZumAPI AuthPolicy result() const;

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(1, zum.app, Update) {
    appEditStart<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(2, zum.auth_policy, Insert) {
    recordPut<Fwd, true>(this, context->authPolicies, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(3, zum.auth_policy, Update) {
    recordPut<Fwd, false>(this, context->authPolicies, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(4, zum.auth_policy, Update) {
    appEditRelease<Fwd>(this, context->authPolicies, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(5, zum.app, Update) {
    appEditPublish<Fwd>(this, true, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(6, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }
};
ZfbStruct(ZumAPI, PolicyPut,
  (((app), (Ctor<0>)), (UDT)),
  (((before), (Ctor<1>)), (UDT)),
  (((values), (Ctor<2>)), (UDT)),
  (((ifMatch), (Ctor<3>)), (String)),
  (((ifNoneMatch), (Ctor<4>)), (String)),
  (((updated), (Ctor<5>)), (Int64)),
  (((request), (Ctor<6>)), (UDT)));

struct ClientAccessPut : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"clientAccessPut.v1">;
  enum { NSteps = 7 };

  App app;
  ClientAccess before;
  ClientAccess values;
  String ifMatch;
  String ifNoneMatch;
  int64_t updated = 0;
  IdemRequest request;
  unsigned error = 0;

  bool unchanged() const { return false; }
  ZumAPI unsigned appError(const App &) const;
  ZumAPI unsigned recordError(const ClientAccess &) const;
  ZumAPI void validate(Zdb_::SagaCompleteFn);
  ZumAPI ClientAccess result() const;

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(1, zum.app, Update) {
    appEditStart<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(2, zum.client_access, Insert) {
    recordPut<Fwd, true>(this, context->clientAccess, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(3, zum.client_access, Update) {
    recordPut<Fwd, false>(this, context->clientAccess, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(4, zum.client_access, Update) {
    appEditRelease<Fwd>(this, context->clientAccess, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(5, zum.app, Update) {
    appEditPublish<Fwd>(this, true, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(6, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }
};
ZfbStruct(ZumAPI, ClientAccessPut,
  (((app), (Ctor<0>)), (UDT)),
  (((before), (Ctor<1>)), (UDT)),
  (((values), (Ctor<2>)), (UDT)),
  (((ifMatch), (Ctor<3>)), (String)),
  (((ifNoneMatch), (Ctor<4>)), (String)),
  (((updated), (Ctor<5>)), (Int64)),
  (((request), (Ctor<6>)), (UDT)));

struct AdminAccessPut : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"adminAccessPut.v1">;
  enum { NSteps = 7 };

  App app;
  AdminAccess before;
  AdminAccess values;
  String ifMatch;
  String ifNoneMatch;
  int64_t updated = 0;
  IdemRequest request;
  unsigned error = 0;

  bool unchanged() const { return false; }
  ZumAPI unsigned appError(const App &) const;
  ZumAPI unsigned recordError(const AdminAccess &) const;
  ZumAPI void validate(Zdb_::SagaCompleteFn);
  ZumAPI AdminAccess result() const;

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(1, zum.app, Update) {
    appEditStart<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(2, zum.admin_access, Insert) {
    recordPut<Fwd, true>(this, context->adminAccess, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(3, zum.admin_access, Update) {
    recordPut<Fwd, false>(this, context->adminAccess, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(4, zum.admin_access, Update) {
    appEditRelease<Fwd>(this, context->adminAccess, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(5, zum.app, Update) {
    appEditPublish<Fwd>(this, true, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(6, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }
};
ZfbStruct(ZumAPI, AdminAccessPut,
  (((app), (Ctor<0>)), (UDT)),
  (((before), (Ctor<1>)), (UDT)),
  (((values), (Ctor<2>)), (UDT)),
  (((ifMatch), (Ctor<3>)), (String)),
  (((ifNoneMatch), (Ctor<4>)), (String)),
  (((updated), (Ctor<5>)), (Int64)),
  (((request), (Ctor<6>)), (UDT)));

struct ProviderAdd : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"providerAdd.v1">;
  enum { NSteps = 4 };

  Provider before;
  Provider values;
  int64_t updated = 0;
  IdemRequest request;
  unsigned error = 0;

  bool unchanged() const { return false; }
  ZumAPI void validate(Zdb_::SagaCompleteFn);
  ZumAPI Provider result() const;

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(1, zum.provider, Insert) {
    recordPut<Fwd, true>(this, context->providers, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(2, zum.provider, Update) {
    appEditRelease<Fwd>(this, context->providers, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(3, zum.request, Update) {
    StringVec ids;
    ids.push(String{} << values.id);
    requestComplete(this, ZuMv(ids), updated, ZuMv(complete));
    return {};
  }
};
ZfbStruct(ZumAPI, ProviderAdd,
  (((before), (Ctor<0>)), (UDT)),
  (((values), (Ctor<1>)), (UDT)),
  (((updated), (Ctor<2>)), (Int64)),
  (((request), (Ctor<3>)), (UDT)));

struct RoleAdd : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"roleAdd.v1">;
  enum { NSteps = 6 };

  App app;
  Role before;
  Role values;
  int64_t updated = 0;
  IdemRequest request;
  unsigned error = 0;

  bool unchanged() const { return false; }
  ZumAPI unsigned appError(const App &) const;
  ZumAPI void validate(Zdb_::SagaCompleteFn);
  ZumAPI Role result() const;

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(1, zum.app, Update) {
    appEditStart<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(2, zum.role, Insert) {
    recordPut<Fwd, true>(this, context->roles, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(3, zum.role, Update) {
    appEditRelease<Fwd>(this, context->roles, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(4, zum.app, Update) {
    appEditPublish<Fwd>(this, true, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(5, zum.request, Update) {
    StringVec ids;
    ids.push(String{} << values.id);
    requestComplete(this, ZuMv(ids), updated, ZuMv(complete));
    return {};
  }
};
ZfbStruct(ZumAPI, RoleAdd,
  (((app), (Ctor<0>)), (UDT)),
  (((before), (Ctor<1>)), (UDT)),
  (((values), (Ctor<2>)), (UDT)),
  (((updated), (Ctor<3>)), (Int64)),
  (((request), (Ctor<4>)), (UDT)));

struct UserInvite : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"userInvite.v2">;
  enum { NSteps = 8 };

  User before;
  User values;
  Grant grant;
  int64_t updated = 0;
  IdemRequest request;
  User external;
  unsigned error = 0;

  bool unchanged() const { return false; }
  ZumAPI void validate(Zdb_::SagaCompleteFn);
  ZumAPI User result() const;

  template <bool Fwd, bool Release> void invalidate(Zdb_::SagaCompleteFn complete)
  {
    if (!external.id) { saga->skip(ZuMv(complete)); return; }
    auto users = context->users;
    users->run(0, [this, users, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(users, 0, ZuFwdTuple(external.id), ZuMv(complete),
        [this](ZdbRow<User> *row, auto &&complete) mutable {
          if (!row) { error = 409; complete(!Fwd); return; }
          auto &item = row->data();
          if constexpr (Release) {
            item.owner = Fwd ? uint128_t{0} : saga->id();
          } else if constexpr (Fwd) {
            if (item.owner || item.source != UserSource::External ||
                item.name != values.name || item.version != external.version ||
                item.authVersion != external.authVersion ||
                !item.authVersion || item.authVersion == UINT64_MAX ||
                !item.version || item.version == UINT64_MAX) {
              error = 409; complete(false); return;
            }
            ++item.authVersion;
            ++item.version;
            item.updated = updated;
            item.owner = saga->id();
          } else item = external;
          complete(row->commit());
        });
    });
  }

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(1, zum.user, Insert) {
    recordPut<Fwd, true>(this, context->users, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(2, zum.grant, Insert) {
    auto table = context->grants;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	table->find<0>(0, ZuFwdTuple(grant.id),
	  [this, table, complete = ZuMv(complete)](ZdbRowRef<Grant> existing) mutable {
	    ZdbRowRef<Grant> row = new ZdbRow<Grant>{table, ZdbShard{0}};
	    saga->insert(table, ZuMv(row), ZuMv(complete),
	      [this, duplicate = bool(existing)](ZdbRow<Grant> *row, auto &&complete) mutable {
		new (row->ptr()) Grant{grant};
		if (duplicate) { error = 409; complete(false); return; }
		row->data().owner = saga->id();
		complete(row->commit());
	      });
	  });
      } else {
	saga->findDel<0>(table, 0, ZuFwdTuple(grant.id), ZuMv(complete),
	  [](ZdbRow<Grant> *row, auto &&complete) mutable {
	    complete(!row || row->commit());
	  });
      }
    });
    return {};
  }
  ZdbSagaStep(3, zum.user, Update) {
    invalidate<Fwd, false>(ZuMv(complete));
    return {};
  }
  ZdbSagaStep(4, zum.user, Update) {
    invalidate<Fwd, true>(ZuMv(complete));
    return {};
  }
  ZdbSagaStep(5, zum.user, Update) {
    appEditRelease<Fwd>(this, context->users, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(6, zum.grant, Update) {
    auto table = context->grants;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(table, 0, ZuFwdTuple(grant.id), ZuMv(complete),
	[this](ZdbRow<Grant> *row, auto &&complete) mutable {
	  if (!row) { complete(!Fwd); return; }
	  row->data().owner = Fwd ? uint128_t{0} : saga->id();
	  complete(row->commit());
	});
    });
    return {};
  }
  ZdbSagaStep(7, zum.request, Update) {
    StringVec ids;
    ids.push(String{} << values.id);
    requestComplete(this, ZuMv(ids), updated, ZuMv(complete));
    return {};
  }
};
ZfbStruct(ZumAPI, UserInvite,
  (((before), (Ctor<0>)), (UDT)),
  (((values), (Ctor<1>)), (UDT)),
  (((updated), (Ctor<3>)), (Int64)),
  (((request), (Ctor<4>)), (UDT)),
  (((grant), (Ctor<2>)), (UDT)),
  (((external), (Ctor<5>)), (UDT)));

struct ClientAdd : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"clientAdd.v1">;
  enum { NSteps = 6 };

  App app;
  Client before;
  Client values;
  int64_t updated = 0;
  IdemRequest request;
  unsigned error = 0;

  bool unchanged() const { return false; }
  ZumAPI unsigned appError(const App &) const;
  ZumAPI void validate(Zdb_::SagaCompleteFn);
  ZumAPI Client result() const;

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(1, zum.app, Update) {
    appEditStart<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(2, zum.client, Insert) {
    recordPut<Fwd, true>(this, context->clients, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(3, zum.client, Update) {
    appEditRelease<Fwd>(this, context->clients, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(4, zum.app, Update) {
    appEditPublish<Fwd>(this, true, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(5, zum.request, Update) {
    StringVec ids;
    ids.push(values.id);
    requestComplete(this, ZuMv(ids), updated, ZuMv(complete));
    return {};
  }
};
ZfbStruct(ZumAPI, ClientAdd,
  (((app), (Ctor<0>)), (UDT)),
  (((before), (Ctor<1>)), (UDT)),
  (((values), (Ctor<2>)), (UDT)),
  (((updated), (Ctor<3>)), (Int64)),
  (((request), (Ctor<4>)), (UDT)));

struct KeyAdd : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"keyAdd.v1">;
  enum { NSteps = 3 };

  SignKey before;
  SignKey values;
  int64_t updated = 0;
  IdemRequest request;
  unsigned error = 0;

  bool unchanged() const { return false; }
  ZumAPI unsigned validate() const;
  ZumAPI SignKey result() const;

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(1, zum.sign_key, Insert) {
    if constexpr (Fwd)
      if ((error = validate())) { complete(false); return {}; }
    auto table = context->signKeys;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	ZdbRowRef<SignKey> row = new ZdbRow<SignKey>{table, ZdbShard{0}};
	saga->insert(table, ZuMv(row), ZuMv(complete),
	  [this](ZdbRow<SignKey> *row, auto &&complete) mutable {
	    new (row->ptr()) SignKey{result()};
	    complete(row->commit());
	  });
      } else {
	saga->findDel<0>(table, 0, ZuFwdTuple(values.id), ZuMv(complete),
	  [](ZdbRow<SignKey> *row, auto &&complete) mutable {
	    complete(!row || row->commit());
	  });
      }
    });
    return {};
  }
  ZdbSagaStep(2, zum.request, Update) {
    StringVec ids;
    ids.push(values.id);
    requestComplete(this, ZuMv(ids), updated, ZuMv(complete));
    return {};
  }
};
ZfbStruct(ZumAPI, KeyAdd,
  (((before), (Ctor<0>)), (UDT)),
  (((values), (Ctor<1>)), (UDT)),
  (((updated), (Ctor<2>)), (Int64)),
  (((request), (Ctor<3>)), (UDT)));

} // namespace Zum

#endif /* zumd_sagas_HH */
