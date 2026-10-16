//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_role_edit.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_request_db.hh>

namespace Zum {

struct RoleEditSaga : public ZmNHeap<RoleEditSaga>,
    public ZdbSaga<RoleEditSaga, DB> {
friend inline ZuStringT<"Zum.RoleEditSaga"> ZmNHeapID(RoleEditSaga *);

  using Base = ZdbSaga<RoleEditSaga, DB>;
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
  using Type = ZuStringT<"roleEdit.v1">;
  enum { NPhases = 6 };
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
  unsigned error = 0;
  ZmRef<AdminEditResult<Role>> response;
  ZtBitmap actions; // Reconstructed by the phase; not recovery state.

  void setError(unsigned value) {
    error = value;
    if (response) response->error = value;
  }

  bool unchanged() const { return kind == Status && state == before.state; }
  unsigned appError(const App &) const;
  unsigned roleError(const Role &) const;
  void validate(unsigned, Zdb_::SagaCompleteFn);


  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }

  ZdbSagaPhase(1, zum.app, Update) {
    appEditStart<Fwd>(this, ZuMv(complete));
    return {};
  }

  ZdbSagaPhase(2, zum.role, Update) {
    if constexpr (!Fwd)
      if (unchanged()) { skip(ZuMv(complete)); return {}; }
    auto apply = [this, complete = ZuMv(complete)](bool ok) mutable {
	if (!ok) { complete(false); return; }
      db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
	db()->roles->template find<0>(0, ZuFwdTuple(app.id, before.id),
	  [this, complete = ZuMv(complete)](ZdbRowRef<Role> row) mutable {
	    if (!row) { setError(404); complete(!Fwd); return; }
	    if (unchanged()) {
	      setError(roleError(row->data()));
	      if (error) complete(false);
	      else skip(ZuMv(complete));
	      return;
	    }
	    update(db()->roles, ZuMv(row), ZuMv(complete),
	      [this](ZdbRow<Role> *row, auto &&complete) mutable {
		if constexpr (Fwd) {
		  if (auto code = roleError(row->data())) {
		    setError(code); complete(false); return;
		  }
		  switch (kind) {
		    case Label: row->data().label = label; break;
		    case Status: row->data().state = state; break;
		    default: row->data().actions = actions; break;
		  }
		  row->data().version = before.version + 1;
		  row->data().updated = updated;
		  row->data().owner = id();
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

  ZdbSagaPhase(3, zum.role, Update) {
    appEditRelease<Fwd>(this, db()->roles, ZuMv(complete));
    return {};
  }

  ZdbSagaPhase(4, zum.app, Update) {
    appEditPublish<Fwd>(this, kind != Label, ZuMv(complete));
    return {};
  }

  ZdbSagaPhase(5, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }

  RoleEditSaga() = default;
  RoleEditSaga(App app_, Role before_, ActionIDVec actionIDs_,
      String ifMatch_, int64_t updated_, IdemRequest request_, int8_t kind_,
      String label_, State::T state_,
      ZmRef<AdminEditResult<Role>> response_ = {}) :
    app{ZuMv(app_)}, before{ZuMv(before_)}, actionIDs{ZuMv(actionIDs_)},
    ifMatch{ZuMv(ifMatch_)}, updated{updated_}, request{ZuMv(request_)},
    kind{kind_}, label{ZuMv(label_)}, state{state_},
    response{ZuMv(response_)} { }
  RoleEditSaga(RoleEditArgs args) :
    RoleEditSaga{ZuMv(args.app), ZuMv(args.before), ZuMv(args.actionIDs), ZuMv(args.ifMatch), args.updated, ZuMv(args.request), args.kind, ZuMv(args.label), args.state, ZuMv(args.result)} { }
  ~RoleEditSaga() override;
};

ZfStruct(ZumAPI, (RoleEditSaga, FB),
  (app,		(Ctor<0>),			UDT),
  (before,	(Ctor<1>),			UDT),
  (actionIDs,	(Ctor<2>),			UInt32Vec),
  (ifMatch,	(Ctor<3>),			String),
  (updated,	(Ctor<4>),			Int64),
  (request,	(Ctor<5>),			UDT),
  (kind,	(Ctor<6>),			Int8),
  (label,	(Ctor<7>),			String),
  (state,	(Ctor<8>, Enum<State::Map>),	Int8));
Zfb::AsTable<"Zum.fbs.RoleEdit"> Zfb_Fmt(RoleEditSaga *);

unsigned RoleEditSaga::appError(const App &item) const
{
  if (kind < Actions || kind > Status ||
      (kind == Status && (state < 0 || state >= State::N))) return 400;
  return !app.version || item.state != State::Active || item.owner || app.owner ||
    item.updated != app.updated || item.version != app.version ||
    item.authVersion != app.authVersion || item.nextActionID != app.nextActionID ||
    (!unchanged() && (app.version == UINT64_MAX ||
      (kind != Label && app.authVersion == UINT64_MAX))) ? 409 : 0;
}

unsigned RoleEditSaga::roleError(const Role &item) const
{
  String etag{"\"v"};
  etag << item.version << '"';
  if (ifMatch != etag || item.version != before.version) return 412;
  return !before.version || before.appID != app.id || before.owner || item.owner ||
    item.tombstone || (kind == Status && item.state == State::Revoked) ||
    (!unchanged() && before.version == UINT64_MAX) ? 409 : 0;
}

void RoleEditSaga::validate(unsigned offset, Zdb_::SagaCompleteFn complete)
{
  if (kind != Actions) { complete(true); return; }
  if (!offset) {
    actions.length(app.nextActionID);
    actions.zero();
  }
  if (offset == actionIDs.length()) { complete(true); return; }
  auto id = actionIDs[offset];
  if (id >= app.nextActionID) { complete(false); return; }
  for (unsigned i = 0; i < offset; ++i)
    if (actionIDs[i] == id) { complete(false); return; }
  db()->actions->run(0, [this, offset, id, complete = ZuMv(complete)]() mutable {
    db()->actions->template find<0>(0, ZuFwdTuple(app.id, id),
      [this, offset, id, complete = ZuMv(complete)](ZdbRowRef<Action> row) mutable {
	if (!row || row->data().state != State::Active ||
	    row->data().tombstone || row->data().owner) {
	  complete(false); return;
	}
	actions.set(id);
	validate(offset + 1, ZuMv(complete));
      });
  });
}

ZuAssert((Zdb_::SagaDefValid<RoleEditSaga>{}));

RoleEditSaga::~RoleEditSaga() = default;

SagaRef roleEdit(ZuBSpan data) {
  return RoleEditSaga::load(data);
}

Zdb_::SagaDef roleEditDef() {
  return RoleEditSaga::def();
}
SagaRef roleEdit(RoleEditArgs args) { return new RoleEditSaga{ZuMv(args)}; }

} // namespace Zum
