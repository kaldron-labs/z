//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// bounded bulk revocation sagas

#ifndef ZumRevoke_HH
#define ZumRevoke_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZumIdentityDB.hh>
#include <zlib/ZumRequestDB.hh>
#include <zlib/ZumSagaImage.hh>
#include <zlib/zum_saga_fbs.h>

namespace Zum {

struct Revoke : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"revoke.v1">;
  enum { NSteps = 8 };

  BytesVec sessions;
  BytesVec consents;
  BytesVec grants;
  int64_t updated = 0;
  IdemRequest request;
  unsigned error = 0;

  template <typename T> static bool eligible(const T &item)
  {
    return !item.owner && item.state != State::Revoked && item.state != State::Consumed;
  }

  template <bool Fwd, bool Release, typename Table, typename Complete>
  void change(Table *table, const BytesVec &images, Complete complete)
  {
    using T = typename Table::T;
    T before;
    if (!SagaImage::load(images[saga->iteration()], before)) {
      error = 400; complete(false); return;
    }
    if (!eligible(before)) { saga->skip(ZuMv(complete)); return; }
    table->run(0, [this, table, before = ZuMv(before), complete = ZuMv(complete)]() mutable {
      ZuStructKeyT<T, 0> key{ZuStructKey<0>(before)};
      table->template find<0>(0, ZuMv(key),
	[this, table, before = ZuMv(before), complete = ZuMv(complete)](
	    ZdbRowRef<T> row) mutable {
	  if (!row) { error = 409; complete(!Fwd); return; }
	  saga->update(table, ZuMv(row), ZuMv(complete),
	    [this, before = ZuMv(before)](ZdbRow<T> *row, auto &&complete) mutable {
	      auto &item = row->data();
	      if constexpr (Release) {
		item.owner = Fwd ? uint128_t{0} : saga->id();
	      } else {
		if constexpr (Fwd) {
		  bool valid = !item.owner && item.state == before.state;
		  if constexpr (ZuIsSame<T, Grant>{})
		    valid &= item.generation == before.generation && item.digest == before.digest;
		  else valid &= item.version == before.version && before.version != UINT64_MAX;
		  if (!valid) { error = 409; complete(false); return; }
		}
		item.state = Fwd ? State::Revoked : before.state;
		item.owner = Fwd ? saga->id() : before.owner;
		if constexpr (!ZuIsSame<T, Grant>{}) {
		  item.version = before.version + Fwd;
		  item.updated = Fwd ? updated : before.updated;
		}
	      }
	      complete(row->commit());
	    });
	});
    });
  }

  template <typename T> static unsigned count(const BytesVec &images)
  {
    unsigned n = 0;
    for (const auto &image: images) {
      T item;
      if (SagaImage::load(image, item) && eligible(item)) ++n;
    }
    return n;
  }
  unsigned count() const
  {
    return count<Session>(sessions) + count<Consent>(consents) + count<Grant>(grants);
  }

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatStep(1, zum.session, Update, sessions.length()) {
    change<Fwd, false>(context->sessions, sessions, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatStep(2, zum.consent, Update, consents.length()) {
    change<Fwd, false>(context->consents, consents, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatStep(3, zum.grant, Update, grants.length()) {
    change<Fwd, false>(context->grants, grants, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatStep(4, zum.session, Update, sessions.length()) {
    change<Fwd, true>(context->sessions, sessions, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatStep(5, zum.consent, Update, consents.length()) {
    change<Fwd, true>(context->consents, consents, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatStep(6, zum.grant, Update, grants.length()) {
    change<Fwd, true>(context->grants, grants, ZuMv(complete)); return {};
  }
  ZdbSagaStep(7, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete)); return {};
  }
};
ZfbStruct(ZumAPI, Revoke,
  (((sessions), (Ctor<0>)), (BytesVec)),
  (((consents), (Ctor<1>)), (BytesVec)),
  (((grants), (Ctor<2>)), (BytesVec)),
  (((updated), (Ctor<3>)), (Int64)),
  (((request), (Ctor<4>)), (UDT)));

struct GrantCleanup : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"grantCleanup.v1">;
  enum { NSteps = 3 };

  BytesVec grants;
  int64_t updated = 0;
  IdemRequest request;
  unsigned error = 0;

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatStep(1, zum.grant, Delete, grants.length()) {
    Grant before;
    if (!SagaImage::load(grants[saga->iteration()], before) ||
	before.owner || before.expires > updated || updated <= 0) {
      error = 400; complete(false); return {};
    }
    auto table = context->grants;
    table->run(0, [this, table, before = ZuMv(before), complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	auto id = before.id;
	saga->template findDel<0>(table, 0, ZuFwdTuple(ZuMv(id)), ZuMv(complete),
	  [this, before = ZuMv(before)](ZdbRow<Grant> *row, auto &&complete) mutable {
	    if (!row || row->data().owner || row->data().expires != before.expires ||
		row->data().generation != before.generation ||
		row->data().state != before.state || row->data().digest != before.digest) {
	      error = 409; complete(false); return;
	    }
	    complete(row->commit());
	  });
      } else {
	ZdbRowRef<Grant> row = new ZdbRow<Grant>{table, ZdbShard{0}};
	saga->insert(table, ZuMv(row), ZuMv(complete),
	  [before = ZuMv(before)](ZdbRow<Grant> *row, auto &&complete) mutable {
	    new (row->ptr()) Grant{ZuMv(before)};
	    complete(row->commit());
	  });
      }
    });
    return {};
  }
  ZdbSagaStep(2, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete)); return {};
  }
};
ZfbStruct(ZumAPI, GrantCleanup,
  (((grants), (Ctor<0>)), (BytesVec)),
  (((updated), (Ctor<1>)), (Int64)),
  (((request), (Ctor<2>)), (UDT)));

} // namespace Zum

#endif /* ZumRevoke_HH */
