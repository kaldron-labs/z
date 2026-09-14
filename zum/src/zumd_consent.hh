//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Atomic explicit consent and authorization-code publication

#ifndef zumd_consent_HH
#define zumd_consent_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zum_saga_fbs.h>

namespace Zum {

struct ConsentCode : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"consentCode.v1">;
  enum { NSteps = 5 };

  Grant beforeGrant;
  Grant afterGrant;
  Consent beforeConsent;
  IDVec scopeIDs;
  int64_t now = 0;

  ZuStructKeyT<Consent, 0> consentKey() const
  {
    return {beforeGrant.userID, beforeGrant.clientID,
      beforeGrant.appID, beforeGrant.audienceID};
  }

  Consent result() const
  {
    Consent item = beforeConsent;
    if (!beforeConsent.version) {
      item.userID = beforeGrant.userID;
      item.clientID = beforeGrant.clientID;
      item.appID = beforeGrant.appID;
      item.audienceID = beforeGrant.audienceID;
      item.created = now;
    }
    // A revoked consent does not carry its former scopes into a new approval.
    if (beforeConsent.state != State::Active) item.scopeIDs.null();
    for (auto id: scopeIDs) {
      bool found = false;
      for (auto existing: item.scopeIDs)
	if (existing == id) { found = true; break; }
      if (!found) item.scopeIDs.push(id);
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
	  beforeConsent.appID != beforeGrant.appID ||
	  beforeConsent.audienceID != beforeGrant.audienceID))) {
      complete(false); return;
    }
    context->apps->run(0, [this, complete = ZuMv(complete)]() mutable {
      context->apps->find<0>(0, ZuFwdTuple(afterGrant.appID),
	[this, complete = ZuMv(complete)](ZdbRowRef<App> app) mutable {
	  if (!app || app->data().owner || app->data().state != State::Active ||
	      app->data().authVersion != afterGrant.authVersion) {
	    complete(false); return;
	  }
	  context->users->run(0, [this, complete = ZuMv(complete)]() mutable {
	    context->users->find<0>(0, ZuFwdTuple(afterGrant.userID),
	      [this, complete = ZuMv(complete)](ZdbRowRef<User> user) mutable {
		if (!user || user->data().owner || user->data().state != State::Active ||
		    user->data().authVersion != afterGrant.userVersion) {
		  complete(false); return;
		}
		context->clients->run(0, [this, complete = ZuMv(complete)]() mutable {
		  context->clients->find<0>(0, ZuFwdTuple(afterGrant.clientID),
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
    auto table = context->grants;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      saga->template findUpd<0, ZuSeq<1, 2, 3>>(table, 0,
	ZuFwdTuple(beforeGrant.id), ZuMv(complete),
	[this](ZdbRow<Grant> *row, auto &&complete) mutable {
	  if (!row) { complete(false); return; }
	  if constexpr (Fwd && !Publish) {
	    const auto &item = row->data();
	    if (item.owner || item.state != beforeGrant.state ||
		item.kind != beforeGrant.kind || item.userID != beforeGrant.userID ||
		item.appID != beforeGrant.appID || item.clientID != beforeGrant.clientID ||
		item.expires != beforeGrant.expires || item.authVersion != beforeGrant.authVersion ||
		item.generation != beforeGrant.generation || item.authTime != beforeGrant.authTime ||
		item.scopeIDs != beforeGrant.scopeIDs ||
		item.userVersion != beforeGrant.userVersion ||
		item.bindingDigest != beforeGrant.bindingDigest || item.digest != beforeGrant.digest) {
	      complete(false); return;
	    }
	    row->data().owner = saga->id();
	  } else if constexpr (Fwd) {
	    row->data() = afterGrant;
	    row->data().owner = 0;
	  } else {
	    row->data() = beforeGrant;
	    if constexpr (Publish) row->data().owner = saga->id();
	  }
	  complete(row->commit());
	});
    });
  }

  ZdbSagaStep(0, zum.grant, Update) {
    if constexpr (Fwd) {
      validate([this, complete = ZuMv(complete)](bool ok) mutable {
	if (!ok) { complete(false); return; }
	grant<true, false>(ZuMv(complete));
      });
    } else grant<false, false>(ZuMv(complete));
    return {};
  }
  ZdbSagaStep(1, zum.consent, Insert) {
    if (beforeConsent.version) { saga->skip(ZuMv(complete)); return {}; }
    auto table = context->consents;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	table->find<0>(0, consentKey(),
	  [this, table, complete = ZuMv(complete)](ZdbRowRef<Consent> existing) mutable {
	    ZdbRowRef<Consent> row = new ZdbRow<Consent>{table, ZdbShard{0}};
	    saga->insert(table, ZuMv(row), ZuMv(complete),
	      [this, duplicate = bool(existing)](ZdbRow<Consent> *row, auto &&complete) mutable {
		new (row->ptr()) Consent{result()};
		if (duplicate) { complete(false); return; }
		row->data().owner = saga->id();
		complete(row->commit());
	      });
	  });
      } else {
	saga->template findDel<0>(table, 0, consentKey(),
	  ZuMv(complete), [](ZdbRow<Consent> *row, auto &&complete) mutable {
	    complete(row && row->commit());
	  });
      }
    });
    return {};
  }
  ZdbSagaStep(2, zum.consent, Update) {
    if (!beforeConsent.version) { saga->skip(ZuMv(complete)); return {}; }
    auto table = context->consents;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      saga->template findUpd<0>(table, 0, consentKey(),
	ZuMv(complete), [this](ZdbRow<Consent> *row, auto &&complete) mutable {
	  if (!row) { complete(false); return; }
	  if constexpr (Fwd) {
	    if (row->data().owner || row->data().version != beforeConsent.version) {
	      complete(false); return;
	    }
	    row->data() = result();
	    row->data().owner = saga->id();
	  } else row->data() = beforeConsent;
	  complete(row->commit());
	});
    });
    return {};
  }
  ZdbSagaStep(3, zum.grant, Update) {
    grant<Fwd, true>(ZuMv(complete)); return {};
  }
  ZdbSagaStep(4, zum.consent, Update) {
    auto table = context->consents;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      saga->template findUpd<0>(table, 0, consentKey(),
	ZuMv(complete), [this](ZdbRow<Consent> *row, auto &&complete) mutable {
	  if (!row) { complete(false); return; }
	  row->data().owner = Fwd ? uint128_t{0} : saga->id();
	  complete(row->commit());
	});
    });
    return {};
  }
};
ZfbStruct(ZumAPI, ConsentCode,
  (((beforeGrant), (Ctor<0>)), (UDT)),
  (((afterGrant), (Ctor<1>)), (UDT)),
  (((beforeConsent), (Ctor<2>)), (UDT)),
  (((scopeIDs), (Ctor<3>)), (UInt64Vec)),
  (((now), (Ctor<4>)), (Int64)));

} // namespace Zum

#endif /* zumd_consent_HH */
