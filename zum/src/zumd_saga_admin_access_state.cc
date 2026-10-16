//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_admin_access_state.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/zumd_saga_admin_ops.hh>

namespace Zum {

struct AdminAccessStateSaga : public ZmNHeap<AdminAccessStateSaga>,
    public ZdbSaga<AdminAccessStateSaga, DB> {
friend inline ZuStringT<"Zum.AdminAccessStateSaga"> ZmNHeapID(AdminAccessStateSaga *);

  using Base = ZdbSaga<AdminAccessStateSaga, DB>;
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
  using Type = ZuStringT<"adminAccessState.v1">;
  enum { NPhases = 4, stateOnly = false };

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

  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(1, zum.admin_access, Update) {
    recordEdit<Fwd>(this, db()->adminAccess, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(2, zum.admin_access, Update) {
    appEditRelease<Fwd>(this, db()->adminAccess, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(3, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }

  AdminAccessStateSaga() = default;
  AdminAccessStateSaga(AdminAccess before_, String ifMatch_, int64_t updated_, IdemRequest request_, State::T state_) :
    before{ZuMv(before_)},
    ifMatch{ZuMv(ifMatch_)},
    updated{ZuMv(updated_)},
    request{ZuMv(request_)},
    state{ZuMv(state_)} { }
  AdminAccessStateSaga(AdminAccessStateArgs args) :
    AdminAccessStateSaga{ZuMv(args.before), ZuMv(args.ifMatch), ZuMv(args.updated), ZuMv(args.request), ZuMv(args.state)} {
    response = ZuMv(args.result);
  }
  ZmRef<AdminEditResult<AdminAccess>> response;
  void setError(unsigned value) { error = value; if (response) response->error = value; }
  ~AdminAccessStateSaga() override;
};

ZfStruct(, (AdminAccessStateSaga, FB),
  (before,	(Ctor<0>),			UDT),
  (ifMatch,	(Ctor<1>),			String),
  (updated,	(Ctor<2>),			Int64),
  (request,	(Ctor<3>),			UDT),
  (state,	(Ctor<4>, Enum<State::Map>),	Int8));
Zfb::AsTable<"Zum.fbs.AdminAccessState"> Zfb_Fmt(AdminAccessStateSaga *);

unsigned AdminAccessStateSaga::recordError(const AdminAccess &item) const
{
  return accessStateError(*this, item);
}

ZuAssert((Zdb_::SagaDefValid<AdminAccessStateSaga>{}));

AdminAccessStateSaga::~AdminAccessStateSaga() = default;

SagaRef adminAccessState(ZuBSpan data) {
  return AdminAccessStateSaga::load(data);
}

Zdb_::SagaDef adminAccessStateDef() {
  return AdminAccessStateSaga::def();
}
SagaRef adminAccessState(AdminAccessStateArgs args) {
  return new AdminAccessStateSaga{ZuMv(args)};
}

} // namespace Zum
