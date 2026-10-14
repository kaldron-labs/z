//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_client_access_state.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/zumd_saga_admin_ops.hh>

namespace Zum {

template <typename Heap = ZuVoid>
struct ClientAccessStateSaga_ : public Heap,
    public ZdbSaga<ClientAccessStateSaga_<Heap>, DB> {
  using Base = ZdbSaga<ClientAccessStateSaga_<Heap>, DB>;
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
  using Type = ZuStringT<"clientAccessState.v1">;
  enum { NPhases = 4, stateOnly = false };

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

  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(1, zum.client_access, Update) {
    recordEdit<Fwd>(this, db()->clientAccess, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(2, zum.client_access, Update) {
    appEditRelease<Fwd>(this, db()->clientAccess, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(3, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }

  ClientAccessStateSaga_() = default;
  ClientAccessStateSaga_(ClientAccess before_, String ifMatch_, int64_t updated_, IdemRequest request_, State::T state_) :
    before{ZuMv(before_)},
    ifMatch{ZuMv(ifMatch_)},
    updated{ZuMv(updated_)},
    request{ZuMv(request_)},
    state{ZuMv(state_)} { }
  ClientAccessStateSaga_(ClientAccessStateArgs args) :
    ClientAccessStateSaga_{ZuMv(args.before), ZuMv(args.ifMatch), ZuMv(args.updated), ZuMv(args.request), ZuMv(args.state)} {
    response = ZuMv(args.result);
  }
  ZmRef<AdminEditResult<ClientAccess>> response;
  void setError(unsigned value) { error = value; if (response) response->error = value; }
  ~ClientAccessStateSaga_() override;
};


using ClientAccessStateSaga = ClientAccessStateSaga_<
  ZmHeap<"Zum.ClientAccessStateSaga", ClientAccessStateSaga_<>>>;

ZfStruct(, (ClientAccessStateSaga, FB),
  (before,	(Ctor<0>),			UDT),
  (ifMatch,	(Ctor<1>),			String),
  (updated,	(Ctor<2>),			Int64),
  (request,	(Ctor<3>),			UDT),
  (state,	(Ctor<4>, Enum<State::Map>),	Int8));
ZiFB::AsTable<"Zum.fbs.ClientAccessState"> ZiFB_Fmt(ClientAccessStateSaga *);

template <typename Heap>
unsigned ClientAccessStateSaga_<Heap>::recordError(const ClientAccess &item) const
{
  return accessStateError(*this, item);
}

ZuAssert((Zdb_::SagaDefValid<ClientAccessStateSaga>{}));

template <typename Heap>
ClientAccessStateSaga_<Heap>::~ClientAccessStateSaga_() = default;

SagaRef clientAccessState(ZuBSpan data) {
  return ClientAccessStateSaga::load(data);
}

Zdb_::SagaDef clientAccessStateDef() {
  return ClientAccessStateSaga::def();
}
SagaRef clientAccessState(ClientAccessStateArgs args) {
  return new ClientAccessStateSaga{ZuMv(args)};
}

} // namespace Zum
