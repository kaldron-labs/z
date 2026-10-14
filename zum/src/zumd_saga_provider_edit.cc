//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_provider_edit.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/zumd_saga_admin_ops.hh>

namespace Zum {

template <typename Heap = ZuVoid>
struct ProviderEditSaga_ : public Heap,
    public ZdbSaga<ProviderEditSaga_<Heap>, DB> {
  using Base = ZdbSaga<ProviderEditSaga_<Heap>, DB>;
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
  using Type = ZuStringT<"providerEdit.v1">;
  enum { NPhases = 4 };

  Provider before;
  String ifMatch;
  Provider values;
  uint8_t fields = 0;
  int64_t updated = 0;
  IdemRequest request;
  bool stateOnly = false;
  State::T state = State::Pending;
  unsigned error = 0;

  bool unchanged() const { return stateOnly && state == before.state; }
  ZumAPI unsigned recordError(const Provider &) const;
  ZumAPI void edit(Provider &) const;

  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(1, zum.provider, Update) {
    recordEdit<Fwd>(this, db()->providers, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(2, zum.provider, Update) {
    appEditRelease<Fwd>(this, db()->providers, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(3, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }

  ProviderEditSaga_() = default;
  ProviderEditSaga_(Provider before_, String ifMatch_, Provider values_, uint8_t fields_, int64_t updated_, IdemRequest request_, bool stateOnly_, State::T state_) :
    before{ZuMv(before_)},
    ifMatch{ZuMv(ifMatch_)},
    values{ZuMv(values_)},
    fields{ZuMv(fields_)},
    updated{ZuMv(updated_)},
    request{ZuMv(request_)},
    stateOnly{ZuMv(stateOnly_)},
    state{ZuMv(state_)} { }
  ProviderEditSaga_(ProviderEditArgs args) :
    ProviderEditSaga_{ZuMv(args.before), ZuMv(args.ifMatch), ZuMv(args.values), ZuMv(args.fields), ZuMv(args.updated), ZuMv(args.request), ZuMv(args.stateOnly), ZuMv(args.state)} {
    response = ZuMv(args.result);
  }
  ZmRef<AdminEditResult<Provider>> response;
  void setError(unsigned value) { error = value; if (response) response->error = value; }
  ~ProviderEditSaga_() override;
};


using ProviderEditSaga = ProviderEditSaga_<
  ZmHeap<"Zum.ProviderEditSaga", ProviderEditSaga_<>>>;

ZfStruct(, (ProviderEditSaga, FB),
  (before,	(Ctor<0>),			UDT),
  (ifMatch,	(Ctor<1>),			String),
  (values,	(Ctor<2>),			UDT),
  (updated,	(Ctor<4>),			Int64),
  (request,	(Ctor<5>),			UDT),
  (stateOnly,	(Ctor<6>),			Bool),
  (state,	(Ctor<7>, Enum<State::Map>),	Int8),
  (fields,	(Ctor<3>),			UInt8));
ZiFB::AsTable<"Zum.fbs.ProviderEdit"> ZiFB_Fmt(ProviderEditSaga *);

template <typename Heap>
void ProviderEditSaga_<Heap>::edit(Provider &item) const
{
  if (fields & 1) item.issuer = values.issuer;
  if (fields & 2) item.clientID = values.clientID;
  if (fields & 4) item.clientSecret = values.clientSecret;
  if (fields & 8) item.scopes = values.scopes;
  if (fields & 16) item.roleClaim = values.roleClaim;
  if (fields & 32) item.claimSource = values.claimSource;
}

template <typename Heap>
unsigned ProviderEditSaga_<Heap>::recordError(const Provider &item) const
{
  if (stateOnly ? state < 0 || state >= State::N : !fields || (fields & ~63U))
    return 400;
  String etag{"\"v"};
  etag << item.version << '"';
  if (ifMatch != etag || item.version != before.version) return 412;
  if (!before.version || before.owner || item.owner || item.state == State::Revoked ||
      (!unchanged() && before.version == UINT64_MAX)) return 409;
  if (stateOnly) return 0;
  if (!((fields & 1) ? values.issuer : item.issuer) ||
      !((fields & 2) ? values.clientID : item.clientID) ||
      !((fields & 16) ? values.roleClaim : item.roleClaim) ||
      ((fields & 4) && !values.clientSecret) ||
      ((fields & 32) && (values.claimSource < 0 || values.claimSource >= ClaimSource::N)))
    return 409;
  return 0;
}

ZuAssert((Zdb_::SagaDefValid<ProviderEditSaga>{}));

template <typename Heap>
ProviderEditSaga_<Heap>::~ProviderEditSaga_() = default;

SagaRef providerEdit(ZuBSpan data) {
  return ProviderEditSaga::load(data);
}

Zdb_::SagaDef providerEditDef() {
  return ProviderEditSaga::def();
}
SagaRef providerEdit(ProviderEditArgs args) {
  return new ProviderEditSaga{ZuMv(args)};
}

} // namespace Zum
