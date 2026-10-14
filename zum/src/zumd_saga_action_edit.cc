//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_action_edit.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_request_db.hh>

namespace Zum {

template <typename Heap = ZuVoid>
struct ActionEditSaga_ : public Heap,
    public ZdbSaga<ActionEditSaga_<Heap>, DB> {
  using Base = ZdbSaga<ActionEditSaga_<Heap>, DB>;
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
  using Type = ZuStringT<"actionEdit.v1">;
  enum { NPhases = 6 };

  App app;
  Action before;
  String ifMatch;
  int64_t updated = 0;
  IdemRequest request;
  State::T state = State::Pending;
  unsigned error = 0;
  ZmRef<AdminEditResult<Action>> response;

  void setError(unsigned value) {
    error = value;
    if (response) response->error = value;
  }

  bool unchanged() const { return state == before.state; }
  unsigned appError(const App &) const;
  unsigned actionError(const Action &) const;


  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(1, zum.app, Update) {
    appEditStart<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(2, zum.action, Update) {
    if constexpr (!Fwd)
      if (unchanged()) { skip(ZuMv(complete)); return {}; }
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      db()->actions->template find<0>(0, ZuFwdTuple(app.id, before.id),
	[this, complete = ZuMv(complete)](ZdbRowRef<Action> row) mutable {
	  if (!row) { setError(404); complete(!Fwd); return; }
	  if (unchanged()) {
	    setError(actionError(row->data()));
	    if (error) complete(false);
	    else skip(ZuMv(complete));
	    return;
	  }
	  update(db()->actions, ZuMv(row), ZuMv(complete),
	    [this](ZdbRow<Action> *row, auto &&complete) mutable {
	      if constexpr (Fwd) {
		if (auto code = actionError(row->data())) {
		  setError(code); complete(false); return;
		}
		row->data().state = state;
		row->data().version = before.version + 1;
		row->data().updated = updated;
		row->data().owner = id();
	      } else row->data() = before;
	      complete(row->commit());
	    });
	});
    });
    return {};
  }
  ZdbSagaPhase(3, zum.action, Update) {
    appEditRelease<Fwd>(this, db()->actions, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(4, zum.app, Update) {
    appEditPublish<Fwd>(this, true, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(5, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }

  ActionEditSaga_() = default;
  ActionEditSaga_(App app_, Action before_, String ifMatch_, int64_t updated_,
      IdemRequest request_, State::T state_,
      ZmRef<AdminEditResult<Action>> response_ = {}) :
    app{ZuMv(app_)}, before{ZuMv(before_)}, ifMatch{ZuMv(ifMatch_)},
    updated{updated_}, request{ZuMv(request_)}, state{state_},
    response{ZuMv(response_)} { }
  ActionEditSaga_(ActionEditArgs args) :
    ActionEditSaga_{ZuMv(args.app), ZuMv(args.before), ZuMv(args.ifMatch), args.updated, ZuMv(args.request), args.state, ZuMv(args.result)} { }
  ~ActionEditSaga_() override;
};

using ActionEditSaga = ActionEditSaga_<
  ZmHeap<"Zum.ActionEditSaga", ActionEditSaga_<>>>;

ZfStruct(ZumAPI, (ActionEditSaga, FB),
  (app,		(Ctor<0>),			UDT),
  (before,	(Ctor<1>),			UDT),
  (ifMatch,	(Ctor<2>),			String),
  (updated,	(Ctor<3>),			Int64),
  (request,	(Ctor<4>),			UDT),
  (state,	(Ctor<5>, Enum<State::Map>),	Int8));
ZiFB::AsTable<"Zum.fbs.ActionEdit"> ZiFB_Fmt(ActionEditSaga *);

template <typename Heap>
unsigned ActionEditSaga_<Heap>::appError(const App &item) const
{
  if (state < 0 || state >= State::N) return 400;
  return !app.version || item.state != State::Active || item.owner || app.owner ||
    item.version != app.version || item.authVersion != app.authVersion ||
    item.updated != app.updated || (!unchanged() &&
      (app.version == UINT64_MAX || app.authVersion == UINT64_MAX)) ? 409 : 0;
}

template <typename Heap>
unsigned ActionEditSaga_<Heap>::actionError(const Action &item) const
{
  String etag{"\"v"};
  etag << item.version << '"';
  if (ifMatch != etag || item.version != before.version) return 412;
  return !before.version || before.appID != app.id || before.owner || item.owner ||
    item.tombstone || item.state == State::Revoked ||
    (!unchanged() && before.version == UINT64_MAX) ? 409 : 0;
}

ZuAssert((Zdb_::SagaDefValid<ActionEditSaga>{}));

template <typename Heap>
ActionEditSaga_<Heap>::~ActionEditSaga_() = default;

SagaRef actionEdit(ZuBSpan data) {
  return ActionEditSaga::load(data);
}

Zdb_::SagaDef actionEditDef() {
  return ActionEditSaga::def();
}
SagaRef actionEdit(ActionEditArgs args) { return new ActionEditSaga{ZuMv(args)}; }

} // namespace Zum
