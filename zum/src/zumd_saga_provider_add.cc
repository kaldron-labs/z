//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_provider_add.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/zumd_saga_admin_ops.hh>

namespace Zum {

template <typename Heap = ZuVoid>
struct ProviderAddSaga_ : public Heap,
    public ZdbSaga<ProviderAddSaga_<Heap>, DB> {
  using Base = ZdbSaga<ProviderAddSaga_<Heap>, DB>;
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
  using Type = ZuStringT<"providerAdd.v1">;
  enum { NPhases = 4 };

  Provider before;
  Provider values;
  int64_t updated = 0;
  IdemRequest request;
  unsigned error = 0;

  bool unchanged() const { return false; }
  ZumAPI void validate(Zdb_::SagaCompleteFn);
  ZumAPI Provider result() const;

  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(1, zum.provider, Insert) {
    recordPut<Fwd, true>(this, db()->providers, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(2, zum.provider, Update) {
    appEditRelease<Fwd>(this, db()->providers, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(3, zum.request, Update) {
    StringVec ids;
    ids.push(String{} << values.id);
    requestComplete(this, ZuMv(ids), updated, ZuMv(complete));
    return {};
  }

  ProviderAddSaga_() = default;
  ProviderAddSaga_(Provider before_, Provider values_, int64_t updated_, IdemRequest request_) :
    before{ZuMv(before_)},
    values{ZuMv(values_)},
    updated{ZuMv(updated_)},
    request{ZuMv(request_)} { }
  ProviderAddSaga_(ProviderAddArgs args) :
    ProviderAddSaga_{ZuMv(args.before), ZuMv(args.values), ZuMv(args.updated), ZuMv(args.request)} {
    response = ZuMv(args.result);
    if (response) response->item = result();
  }
  ZmRef<AdminEditResult<Provider>> response;
  void setError(unsigned value) { error = value; if (response) response->error = value; }
  ~ProviderAddSaga_() override;
};


using ProviderAddSaga = ProviderAddSaga_<
  ZmHeap<"Zum.ProviderAddSaga", ProviderAddSaga_<>>>;

ZfStruct(, (ProviderAddSaga, FB),
  (before,	(Ctor<0>),	UDT),
  (values,	(Ctor<1>),	UDT),
  (updated,	(Ctor<2>),	Int64),
  (request,	(Ctor<3>),	UDT));
Zfb::AsTable<"Zum.fbs.ProviderAdd"> Zfb_Fmt(ProviderAddSaga *);

template <typename Heap>
Provider ProviderAddSaga_<Heap>::result() const
{
  Provider item = values;
  item.state = State::Active;
  item.version = 1;
  item.created = item.updated = updated;
  item.owner = 0;
  return item;
}

template <typename Heap>
void ProviderAddSaga_<Heap>::validate(Zdb_::SagaCompleteFn complete)
{
  if (!values.id || values.id == UINT64_MAX || values.id != before.id ||
      !values.name || !values.issuer || !values.clientID || !values.roleClaim ||
      values.claimSource < 0 || values.claimSource >= ClaimSource::N) {
    setError(400); complete(false); return;
  }
  if (before.version) { setError(409); complete(false); return; }
  complete(true);
}

ZuAssert((Zdb_::SagaDefValid<ProviderAddSaga>{}));

template <typename Heap>
ProviderAddSaga_<Heap>::~ProviderAddSaga_() = default;

SagaRef providerAdd(ZuBSpan data) {
  return ProviderAddSaga::load(data);
}

Zdb_::SagaDef providerAddDef() {
  return ProviderAddSaga::def();
}
SagaRef providerAdd(ProviderAddArgs args) {
  return new ProviderAddSaga{ZuMv(args)};
}

} // namespace Zum
