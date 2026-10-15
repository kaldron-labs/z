//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_client_add.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/zumd_saga_admin_ops.hh>
#include <zlib/ZtlsSec.hh>

namespace Zum {

template <typename Heap = ZuVoid>
struct ClientAddSaga_ : public Heap,
    public ZdbSaga<ClientAddSaga_<Heap>, DB> {
  using Base = ZdbSaga<ClientAddSaga_<Heap>, DB>;
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
  using Type = ZuStringT<"clientAdd.v1">;
  enum { NPhases = 6 };

  App app;
  Client before;
  Client values;
  int64_t updated = 0;
  IdemRequest request;
  unsigned error = 0;

  bool unchanged() const { return false; }
  ZumAPI unsigned appError(const App &) const;
  ZumAPI void validate(Zdb_::SagaCompleteFn);
  ZumAPI Client result() const;

  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(1, zum.app, Update) {
    appEditStart<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(2, zum.client, Insert) {
    recordPut<Fwd, true>(this, db()->clients, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(3, zum.client, Update) {
    appEditRelease<Fwd>(this, db()->clients, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(4, zum.app, Update) {
    appEditPublish<Fwd>(this, true, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(5, zum.request, Update) {
    StringVec ids;
    ids.push(values.id);
    requestComplete(this, ZuMv(ids), updated, ZuMv(complete));
    return {};
  }

  ClientAddSaga_() = default;
  ClientAddSaga_(App app_, Client before_, Client values_, int64_t updated_, IdemRequest request_) :
    app{ZuMv(app_)},
    before{ZuMv(before_)},
    values{ZuMv(values_)},
    updated{ZuMv(updated_)},
    request{ZuMv(request_)} { }
  ClientAddSaga_(ClientAddArgs args) :
    ClientAddSaga_{ZuMv(args.app), ZuMv(args.before), ZuMv(args.values), ZuMv(args.updated), ZuMv(args.request)} {
    response = ZuMv(args.result);
    if (response) response->item = result();
  }
  ZmRef<AdminEditResult<Client>> response;
  void setError(unsigned value) { error = value; if (response) response->error = value; }
  ~ClientAddSaga_() override;
};


using ClientAddSaga = ClientAddSaga_<
  ZmHeap<"Zum.ClientAddSaga", ClientAddSaga_<>>>;

ZfStruct(, (ClientAddSaga, FB),
  (app,		(Ctor<0>),	UDT),
  (before,	(Ctor<1>),	UDT),
  (values,	(Ctor<2>),	UDT),
  (updated,	(Ctor<3>),	Int64),
  (request,	(Ctor<4>),	UDT));
Zfb::AsTable<"Zum.fbs.ClientAdd"> Zfb_Fmt(ClientAddSaga *);

template <typename Heap>
unsigned ClientAddSaga_<Heap>::appError(const App &item) const
{
  return appVersionError(app, item);
}

template <typename Heap>
Client ClientAddSaga_<Heap>::result() const
{
  Client item = values;
  item.state = State::Active;
  item.version = item.secretVersion = 1;
  item.created = item.updated = updated;
  item.owner = 0;
  return item;
}

template <typename Heap>
void ClientAddSaga_<Heap>::validate(Zdb_::SagaCompleteFn complete)
{
  if (!values.id || values.id != before.id || !values.appID ||
      values.appID != app.id || values.appID != before.appID ||
      !clientConfigValid(values.profile, values.grants, values.refreshAllowed,
        values.redirects)) {
    setError(400); complete(false); return;
  }
  if (clientType(values.profile) == ClientType::Confidential ?
      values.secretDigest.length() != Ztls::SecretHash::Size :
      bool(values.secretDigest)) {
    setError(400); complete(false); return;
  }
  if (before.version) { setError(409); complete(false); return; }
  complete(true);
}

ZuAssert((Zdb_::SagaDefValid<ClientAddSaga>{}));

template <typename Heap>
ClientAddSaga_<Heap>::~ClientAddSaga_() = default;

SagaRef clientAdd(ZuBSpan data) {
  return ClientAddSaga::load(data);
}

Zdb_::SagaDef clientAddDef() {
  return ClientAddSaga::def();
}
SagaRef clientAdd(ClientAddArgs args) {
  return new ClientAddSaga{ZuMv(args)};
}

} // namespace Zum
