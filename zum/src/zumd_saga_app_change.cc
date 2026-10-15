//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_app_change.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/zumd_saga_admin_ops.hh>

namespace Zum {

template <typename Heap = ZuVoid>
struct AppChangeSaga_ : public Heap,
    public ZdbSaga<AppChangeSaga_<Heap>, DB> {
  using Base = ZdbSaga<AppChangeSaga_<Heap>, DB>;
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
  using Type = ZuStringT<"appChange.v1">;
  enum { NPhases = 4 };

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

  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(1, zum.app, Update) {
    if constexpr (!Fwd)
      if (unchanged()) { this->skip(ZuMv(complete)); return {}; }
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      db()->apps->template find<0>(0, ZuFwdTuple(before.id),
	[this, complete = ZuMv(complete)](ZdbRowRef<App> row) mutable {
	  if (!row) { setError(404); complete(!Fwd); return; }
	  if (unchanged()) {
	    setError(appError(row->data()));
	    if (error) complete(false);
	    else this->skip(ZuMv(complete));
	    return;
	  }
	  this->update(db()->apps, ZuMv(row), ZuMv(complete),
	    [this](ZdbRow<App> *row, auto &&complete) mutable {
	      if constexpr (Fwd) {
		if (auto code = appError(row->data())) {
		  setError(code); complete(false); return;
		}
		if (stateOnly) row->data().state = state;
		else row->data().label = label;
		row->data().version = before.version + 1;
		row->data().authVersion = before.authVersion + stateOnly;
		row->data().updated = updated;
		row->data().owner = this->id();
	      } else row->data() = before;
	      complete(row->commit());
	    });
	});
    });
    return {};
  }
  ZdbSagaPhase(2, zum.app, Update) {
    if (unchanged()) { this->skip(ZuMv(complete)); return {}; }
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(db()->apps, 0, ZuFwdTuple(before.id),
	ZuMv(complete), [this](ZdbRow<App> *row, auto &&complete) mutable {
	  if (!row) { complete(!Fwd); return; }
	  row->data().owner = Fwd ? uint128_t{0} : this->id();
	  complete(row->commit());
	});
    });
    return {};
  }
  ZdbSagaPhase(3, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }

  AppChangeSaga_() = default;
  AppChangeSaga_(App before_, String ifMatch_, String label_, int64_t updated_, IdemRequest request_, bool stateOnly_, State::T state_) :
    before{ZuMv(before_)},
    ifMatch{ZuMv(ifMatch_)},
    label{ZuMv(label_)},
    updated{ZuMv(updated_)},
    request{ZuMv(request_)},
    stateOnly{ZuMv(stateOnly_)},
    state{ZuMv(state_)} { }
  AppChangeSaga_(AppChangeArgs args) :
    AppChangeSaga_{ZuMv(args.before), ZuMv(args.ifMatch), ZuMv(args.label), ZuMv(args.updated), ZuMv(args.request), ZuMv(args.stateOnly), ZuMv(args.state)} {
    response = ZuMv(args.result);
  }
  ZmRef<AdminEditResult<App>> response;
  void setError(unsigned value) { error = value; if (response) response->error = value; }
  ~AppChangeSaga_() override;
};


using AppChangeSaga = AppChangeSaga_<
  ZmHeap<"Zum.AppChangeSaga", AppChangeSaga_<>>>;

ZfStruct(, (AppChangeSaga, FB),
  (before,	(Ctor<0>),			UDT),
  (ifMatch,	(Ctor<1>),			String),
  (label,	(Ctor<2>),			String),
  (updated,	(Ctor<3>),			Int64),
  (request,	(Ctor<4>),			UDT),
  (stateOnly,	(Ctor<5>),			Bool),
  (state,	(Ctor<6>, Enum<State::Map>),	Int8));
Zfb::AsTable<"Zum.fbs.AppChange"> Zfb_Fmt(AppChangeSaga *);

template <typename Heap>
unsigned AppChangeSaga_<Heap>::appError(const App &item) const
{
  if (stateOnly && (state < 0 || state >= State::N)) return 400;
  String etag{"\"v"};
  etag << item.version << '"';
  if (ifMatch != etag || item.version != before.version) return 412;
  return !before.version || before.owner || item.owner ||
    item.authVersion != before.authVersion ||
    (stateOnly && item.state == State::Revoked) ||
    (!unchanged() && (before.version == UINT64_MAX ||
      (stateOnly && before.authVersion == UINT64_MAX))) ? 409 : 0;
}

ZuAssert((Zdb_::SagaDefValid<AppChangeSaga>{}));

template <typename Heap>
AppChangeSaga_<Heap>::~AppChangeSaga_() = default;

SagaRef appChange(ZuBSpan data) {
  return AppChangeSaga::load(data);
}

Zdb_::SagaDef appChangeDef() {
  return AppChangeSaga::def();
}
SagaRef appChange(AppChangeArgs args) {
  return new AppChangeSaga{ZuMv(args)};
}

} // namespace Zum
