//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_cred_edit.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/zumd_saga_admin_ops.hh>

namespace Zum {

struct CredEditSaga : public ZmNHeap<CredEditSaga>,
    public ZdbSaga<CredEditSaga, DB> {
friend inline ZuStringT<"Zum.CredEditSaga"> ZmNHeapID(CredEditSaga *);

  using Base = ZdbSaga<CredEditSaga, DB>;
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
  using Type = ZuStringT<"credEdit.v1">;
  enum { NPhases = 4 };

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

  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(1, zum.cred, Update) {
    if constexpr (!Fwd)
      if (unchanged()) { this->skip(ZuMv(complete)); return {}; }
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      db()->creds->template find<0>(0, ZuFwdTuple(before.id),
	[this, complete = ZuMv(complete)](ZdbRowRef<Cred> row) mutable {
	  if (!row) { setError(404); complete(!Fwd); return; }
	  if (unchanged()) {
	    setError(credError(row->data()));
	    if (error) complete(false);
	    else this->skip(ZuMv(complete));
	    return;
	  }
	  this->update(db()->creds, ZuMv(row), ZuMv(complete),
	    [this](ZdbRow<Cred> *row, auto &&complete) mutable {
	      if constexpr (Fwd) {
		if (auto code = credError(row->data())) {
		  setError(code); complete(false); return;
		}
		if (stateOnly) row->data().state = state;
		else row->data().label = label;
		row->data().version = before.version + 1;
		row->data().updated = updated;
		row->data().owner = this->id();
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
  ZdbSagaPhase(2, zum.cred, Update) {
    if (unchanged()) { this->skip(ZuMv(complete)); return {}; }
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(db()->creds, 0, ZuFwdTuple(before.id),
	ZuMv(complete), [this](ZdbRow<Cred> *row, auto &&complete) mutable {
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

  CredEditSaga() = default;
  CredEditSaga(Cred before_, String ifMatch_, String label_, int64_t updated_, IdemRequest request_, bool stateOnly_, State::T state_) :
    before{ZuMv(before_)},
    ifMatch{ZuMv(ifMatch_)},
    label{ZuMv(label_)},
    updated{ZuMv(updated_)},
    request{ZuMv(request_)},
    stateOnly{ZuMv(stateOnly_)},
    state{ZuMv(state_)} { }
  CredEditSaga(CredEditArgs args) :
    CredEditSaga{ZuMv(args.before), ZuMv(args.ifMatch), ZuMv(args.label), ZuMv(args.updated), ZuMv(args.request), ZuMv(args.stateOnly), ZuMv(args.state)} {
    response = ZuMv(args.result);
  }
  ZmRef<AdminEditResult<Cred>> response;
  void setError(unsigned value) { error = value; if (response) response->error = value; }
  ~CredEditSaga() override;
};

ZfStruct(, (CredEditSaga, FB),
  (before,	(Ctor<0>),			UDT),
  (ifMatch,	(Ctor<1>),			String),
  (label,	(Ctor<2>),			String),
  (updated,	(Ctor<3>),			Int64),
  (request,	(Ctor<4>),			UDT),
  (stateOnly,	(Ctor<5>),			Bool),
  (state,	(Ctor<6>, Enum<State::Map>),	Int8));
Zfb::AsTable<"Zum.fbs.CredEdit"> Zfb_Fmt(CredEditSaga *);

unsigned CredEditSaga::credError(const Cred &item) const
{
  if (stateOnly && (state < 0 || state >= State::N)) return 400;
  String etag{"\"v"};
  etag << item.version << '"';
  if (ifMatch != etag || item.version != before.version) return 412;
  return !before.version || before.owner || item.owner ||
    item.updated != before.updated ||
    (stateOnly && item.state == State::Revoked) ||
    (!unchanged() && before.version == UINT64_MAX) ? 409 : 0;
}

ZuAssert((Zdb_::SagaDefValid<CredEditSaga>{}));

CredEditSaga::~CredEditSaga() = default;

SagaRef credEdit(ZuBSpan data) {
  return CredEditSaga::load(data);
}

Zdb_::SagaDef credEditDef() {
  return CredEditSaga::def();
}
SagaRef credEdit(CredEditArgs args) {
  return new CredEditSaga{ZuMv(args)};
}

} // namespace Zum
