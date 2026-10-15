//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_consent_code.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>

namespace Zum {

template <typename Heap = ZuVoid>
struct ConsentCodeSaga_ : public Heap,
    public ZdbSaga<ConsentCodeSaga_<Heap>, DB> {
  using Base = ZdbSaga<ConsentCodeSaga_<Heap>, DB>;
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
  using Type = ZuStringT<"consentCode.v1">;
  enum { NPhases = 5 };

  Grant beforeGrant;
  Grant afterGrant;
  Consent beforeConsent;
  IDVec roleIDs;
  int64_t now = 0;

  ZuStructKeyT<Consent, 0> consentKey() const
  {
    return {beforeGrant.userID, beforeGrant.clientID,
      beforeGrant.appID};
  }

  Consent result() const
  {
    Consent item = beforeConsent;
    if (!beforeConsent.version) {
      item.userID = beforeGrant.userID;
      item.clientID = beforeGrant.clientID;
      item.appID = beforeGrant.appID;
      item.created = now;
    }
    // A revoked consent does not carry its former role views into a new approval.
    if (beforeConsent.state != State::Active) item.roleIDs.null();
    for (auto id: roleIDs) {
      bool found = false;
      for (auto existing: item.roleIDs)
	if (existing == id) { found = true; break; }
      if (!found) item.roleIDs.push(id);
    }
    item.state = State::Active;
    item.version = beforeConsent.version + 1;
    item.updated = now;
    return item;
  }

  template <typename Complete> void validate(Complete complete)
  {
    if (now <= 0 || beforeGrant.owner || beforeGrant.expires <= now ||
	beforeGrant.kind != GrantKind::Ceremony ||
	beforeGrant.purpose != GrantPurpose::Authorization ||
	beforeGrant.state != State::Pending ||
	beforeGrant.id != afterGrant.id || beforeGrant.appID != afterGrant.appID ||
	beforeGrant.userID != afterGrant.userID || !afterGrant.digest ||
	afterGrant.kind != GrantKind::Code || afterGrant.expires <= now ||
	beforeConsent.owner || beforeConsent.version == UINT64_MAX ||
	(beforeConsent.version && (beforeConsent.userID != beforeGrant.userID ||
	  beforeConsent.clientID != beforeGrant.clientID ||
	  beforeConsent.appID != beforeGrant.appID))) {
      complete(false); return;
    }
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      db()->apps->template find<0>(0, ZuFwdTuple(afterGrant.appID),
	[this, complete = ZuMv(complete)](ZdbRowRef<App> app) mutable {
	  if (!app || app->data().owner || app->data().state != State::Active ||
	      app->data().authVersion != afterGrant.authVersion) {
	    complete(false); return;
	  }
	  db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
	    db()->users->template find<0>(0, ZuFwdTuple(afterGrant.userID),
	      [this, complete = ZuMv(complete)](ZdbRowRef<User> user) mutable {
		if (!user || user->data().owner || user->data().state != State::Active ||
		    user->data().authVersion != afterGrant.userVersion) {
		  complete(false); return;
		}
		db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
		  db()->clients->template find<0>(0, ZuFwdTuple(afterGrant.clientID),
		    [this, complete = ZuMv(complete)](ZdbRowRef<Client> client) mutable {
		      complete(client && !client->data().owner &&
			client->data().state == State::Active &&
			client->data().appID == afterGrant.appID);
		    });
		});
	      });
	  });
	});
    });
  }

  template <bool Fwd, bool Publish, typename Complete>
  void grant(Complete complete)
  {
    auto table = db()->grants;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0, ZuSeq<1, 2, 3>>(table, 0,
	ZuFwdTuple(beforeGrant.id), ZuMv(complete),
	[this](ZdbRow<Grant> *row, auto &&complete) mutable {
	  if (!row) { complete(false); return; }
	  if constexpr (Fwd && !Publish) {
	    const auto &item = row->data();
	    if (item.owner || item.state != beforeGrant.state ||
		item.kind != beforeGrant.kind || item.userID != beforeGrant.userID ||
		item.appID != beforeGrant.appID || item.clientID != beforeGrant.clientID ||
		item.expires != beforeGrant.expires || item.authVersion != beforeGrant.authVersion ||
		item.authTime != beforeGrant.authTime ||
		item.requestedRoleIDs != beforeGrant.requestedRoleIDs ||
		item.userVersion != beforeGrant.userVersion ||
		item.bindingDigest != beforeGrant.bindingDigest || item.digest != beforeGrant.digest) {
	      complete(false); return;
	    }
	    row->data().owner = id();
	  } else if constexpr (Fwd) {
	    row->data() = afterGrant;
	    row->data().owner = 0;
	  } else {
	    row->data() = beforeGrant;
	    if constexpr (Publish) row->data().owner = id();
	  }
	  complete(row->commit());
	});
    });
  }

  ZdbSagaPhase(0, zum.grant, Update) {
    if constexpr (Fwd) {
      validate([this, complete = ZuMv(complete)](bool ok) mutable {
	if (!ok) { complete(false); return; }
	grant<true, false>(ZuMv(complete));
      });
    } else grant<false, false>(ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(1, zum.consent, Insert) {
    if (beforeConsent.version) { skip(ZuMv(complete)); return {}; }
    auto table = db()->consents;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	table->template find<0>(0, consentKey(),
	  [this, table, complete = ZuMv(complete)](ZdbRowRef<Consent> existing) mutable {
	    ZdbRowRef<Consent> row = new ZdbRow<Consent>{table, ZdbShard{0}};
	    insert(table, ZuMv(row), ZuMv(complete),
	      [this, duplicate = bool(existing)](ZdbRow<Consent> *row, auto &&complete) mutable {
		new (row->ptr()) Consent{result()};
		if (duplicate) { complete(false); return; }
		row->data().owner = id();
		complete(row->commit());
	      });
	  });
      } else {
	this->template findDel<0>(table, 0, consentKey(),
	  ZuMv(complete), [](ZdbRow<Consent> *row, auto &&complete) mutable {
	    complete(!row || row->commit());
	  });
      }
    });
    return {};
  }
  ZdbSagaPhase(2, zum.consent, Update) {
    if (!beforeConsent.version) { skip(ZuMv(complete)); return {}; }
    auto table = db()->consents;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(table, 0, consentKey(),
	ZuMv(complete), [this](ZdbRow<Consent> *row, auto &&complete) mutable {
	  if (!row) { complete(false); return; }
	  if constexpr (Fwd) {
	    if (row->data().owner || row->data().version != beforeConsent.version) {
	      complete(false); return;
	    }
	    row->data() = result();
	    row->data().owner = id();
	  } else row->data() = beforeConsent;
	  complete(row->commit());
	});
    });
    return {};
  }
  ZdbSagaPhase(3, zum.grant, Update) {
    grant<Fwd, true>(ZuMv(complete)); return {};
  }
  ZdbSagaPhase(4, zum.consent, Update) {
    auto table = db()->consents;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(table, 0, consentKey(),
	ZuMv(complete), [this](ZdbRow<Consent> *row, auto &&complete) mutable {
	  if (!row) { complete(false); return; }
	  row->data().owner = Fwd ? uint128_t{0} : id();
	  complete(row->commit());
	});
    });
    return {};
  }

  ConsentCodeSaga_() = default;
  ConsentCodeSaga_(Grant beforeGrant_, Grant afterGrant_,
      Consent beforeConsent_, IDVec roleIDs_, int64_t now_) :
    beforeGrant{ZuMv(beforeGrant_)}, afterGrant{ZuMv(afterGrant_)},
    beforeConsent{ZuMv(beforeConsent_)}, roleIDs{ZuMv(roleIDs_)}, now{now_} { }
  ConsentCodeSaga_(ConsentCodeArgs args) :
    ConsentCodeSaga_{ZuMv(args.beforeGrant), ZuMv(args.afterGrant),
      ZuMv(args.beforeConsent), ZuMv(args.roleIDs), args.now} { }
  ~ConsentCodeSaga_() override;
};

using ConsentCodeSaga = ConsentCodeSaga_<
  ZmHeap<"Zum.ConsentCodeSaga", ConsentCodeSaga_<>>>;

ZfStruct(, (ConsentCodeSaga, FB),
  (beforeGrant,		(Ctor<0>),	UDT),
  (afterGrant,		(Ctor<1>),	UDT),
  (beforeConsent,	(Ctor<2>),	UDT),
  (roleIDs,		(Ctor<3>),	UInt64Vec),
  (now,			(Ctor<4>),	Int64));
Zfb::AsTable<"Zum.fbs.ConsentCode"> Zfb_Fmt(ConsentCodeSaga *);

ZuAssert((Zdb_::SagaDefValid<ConsentCodeSaga>{}));

template <typename Heap>
ConsentCodeSaga_<Heap>::~ConsentCodeSaga_() = default;

SagaRef consentCode(ZuBSpan data) {
  return ConsentCodeSaga::load(data);
}

Zdb_::SagaDef consentCodeDef() {
  return ConsentCodeSaga::def();
}
SagaRef consentCode(ConsentCodeArgs args) {
  return new ConsentCodeSaga{ZuMv(args)};
}

} // namespace Zum
