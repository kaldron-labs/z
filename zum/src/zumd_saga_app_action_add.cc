//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_app_action_add.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_request_db.hh>

namespace Zum {

template <typename Heap = ZuVoid>
struct AppActionAddSaga_ : public Heap,
    public ZdbSaga<AppActionAddSaga_<Heap>, DB> {
  using Base = ZdbSaga<AppActionAddSaga_<Heap>, DB>;
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
  using Type = ZuStringT<"appActionAdd.v2">;
  enum { NPhases = 6 };

  AppID		appID = 0;
  ActionID	actionID = 0;
  String	name;
  String	label;
  int64_t	created = 0;
  uint64_t	oldAppVersion = 0;
  uint64_t	oldAuthVersion = 0;
  int64_t	oldUpdated = 0;
  IdemRequest request;

  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }

  ZdbSagaPhase(1, zum.app, Update) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      db()->apps->template find<0>(0, ZuFwdTuple(appID),
	[this, complete = ZuMv(complete)](ZdbRowRef<App> row) mutable {
	  if (!row) { complete(!Fwd); return; }
	  this->update(db()->apps, ZuMv(row), ZuMv(complete),
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
	  row->data().owner = this->id();
	} else {
	  if (!row || row->data().owner != this->id() ||
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

  ZdbSagaPhase(2, zum.action, Insert) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	// Name uniqueness is a business constraint. Apply it only when Zdb
	// executes the insertion; replay handling belongs to the native wrapper.
	db()->actions->template find<1>(0, ZuFwdTuple(appID, name),
	  [this, complete = ZuMv(complete)](
	      ZdbRowRef<Action> existing) mutable {
	  ZdbRowRef<Action> row =
	    new ZdbRow<Action>{db()->actions, ZdbShard{0}};
	  this->insert(db()->actions, ZuMv(row), ZuMv(complete),
	    [this, duplicate = bool(existing)](ZdbRow<Action> *row,
		auto &&complete) mutable {
	      new (row->ptr()) Action{.appID = appID, .id = actionID,
		.name = name, .label = label, .state = State::Active,
		.origin = Origin::Custom, .version = 1,
		.created = created, .updated = created, .owner = this->id()};
	      if (duplicate) { complete(false); return; }
	      complete(row->commit());
	    });
	});
      } else {
	this->template findDel<0>(db()->actions, 0,
	  ZuFwdTuple(appID, actionID), ZuMv(complete),
	  [this](ZdbRow<Action> *row, auto &&complete) mutable {
	    if (!row || row->data().owner != this->id()) {
	      complete(true);
	      return;
	    }
	    complete(row->commit());
	  });
      }
    });
    return {};
  }

  ZdbSagaPhase(3, zum.action, Update) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(db()->actions, 0,
	ZuFwdTuple(appID, actionID), ZuMv(complete),
	[this](ZdbRow<Action> *row, auto &&complete) mutable {
	  if (!row || row->data().name != name ||
	      row->data().origin != Origin::Custom ||
	      row->data().state != State::Active ||
	      row->data().owner !=
		(Fwd ? this->id() : uint128_t{0})) {
	    complete(!Fwd);
	    return;
	  }
	  row->data().owner = Fwd ? uint128_t{0} : this->id();
	  complete(row->commit());
	});
    });
    return {};
  }

  ZdbSagaPhase(4, zum.app, Update) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(db()->apps, 0, ZuFwdTuple(appID),
	ZuMv(complete), [this](ZdbRow<App> *row,
	    auto &&complete) mutable {
	if (!row || row->data().state != State::Active ||
	    row->data().nextActionID != actionID + 1 ||
	    row->data().version != oldAppVersion + 1 ||
	    row->data().authVersion != oldAuthVersion + 1 ||
	    row->data().updated != created ||
	    row->data().owner != (Fwd ? this->id() : uint128_t{0})) {
	  complete(!Fwd);
	  return;
	}
	row->data().owner = Fwd ? uint128_t{0} : this->id();
	complete(row->commit());
      });
    });
    return {};
  }
  ZdbSagaPhase(5, zum.request, Update) {
    StringVec ids;
    ids.push(String{} << actionID);
    requestComplete(this, ZuMv(ids), created, ZuMv(complete));
    return {};
  }


  AppActionAddSaga_() = default;
  AppActionAddSaga_(AppID appID_, ActionID actionID_, String name_, String label_, int64_t created_, uint64_t oldAppVersion_, uint64_t oldAuthVersion_, int64_t oldUpdated_, IdemRequest request_) :
    appID{ZuMv(appID_)},
    actionID{ZuMv(actionID_)},
    name{ZuMv(name_)},
    label{ZuMv(label_)},
    created{ZuMv(created_)},
    oldAppVersion{ZuMv(oldAppVersion_)},
    oldAuthVersion{ZuMv(oldAuthVersion_)},
    oldUpdated{ZuMv(oldUpdated_)},
    request{ZuMv(request_)} { }
  AppActionAddSaga_(AppActionAddArgs args) :
    AppActionAddSaga_{ZuMv(args.appID), ZuMv(args.actionID), ZuMv(args.name), ZuMv(args.label), ZuMv(args.created), ZuMv(args.oldAppVersion), ZuMv(args.oldAuthVersion), ZuMv(args.oldUpdated), ZuMv(args.request)} {
  }
  ~AppActionAddSaga_() override;
};


using AppActionAddSaga = AppActionAddSaga_<
  ZmHeap<"Zum.AppActionAddSaga", AppActionAddSaga_<>>>;

ZfStruct(, (AppActionAddSaga, FB),
  (appID,		(Ctor<0>),	UInt64),
  (actionID,		(Ctor<1>),	UInt32),
  (name,		(Ctor<2>),	String),
  (label,		(Ctor<3>),	String),
  (created,		(Ctor<4>),	Int64),
  (oldAppVersion,	(Ctor<5>),	UInt64),
  (oldAuthVersion,	(Ctor<6>),	UInt64),
  (oldUpdated,		(Ctor<7>),	Int64),
  (request,		(Ctor<8>),	UDT));
ZiFB::AsTable<"Zum.fbs.AppActionAdd"> ZiFB_Fmt(AppActionAddSaga *);

ZuAssert((Zdb_::SagaDefValid<AppActionAddSaga>{}));

template <typename Heap>
AppActionAddSaga_<Heap>::~AppActionAddSaga_() = default;

SagaRef appActionAdd(ZuBSpan data) {
  return AppActionAddSaga::load(data);
}

Zdb_::SagaDef appActionAddDef() {
  return AppActionAddSaga::def();
}
SagaRef appActionAdd(AppActionAddArgs args) {
  return new AppActionAddSaga{ZuMv(args)};
}

} // namespace Zum
