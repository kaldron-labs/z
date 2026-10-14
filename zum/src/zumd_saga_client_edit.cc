//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_client_edit.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/zumd_saga_admin_ops.hh>
#include <zlib/ZtlsSec.hh>

namespace Zum {

template <typename Heap = ZuVoid>
struct ClientEditSaga_ : public Heap,
    public ZdbSaga<ClientEditSaga_<Heap>, DB> {
  using Base = ZdbSaga<ClientEditSaga_<Heap>, DB>;
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
  using Type = ZuStringT<"clientEdit.v1">;
  enum { NPhases = 4, Secret = 16 };

  Client before;
  String ifMatch;
  Client values;
  uint8_t fields = 0;
  int64_t updated = 0;
  IdemRequest request;
  bool stateOnly = false;
  State::T state = State::Pending;
  uint32_t overlapSeconds = 0;
  unsigned error = 0;

  bool unchanged() const { return stateOnly && state == before.state; }
  ZumAPI unsigned recordError(const Client &) const;
  ZumAPI void edit(Client &) const;
  ZumAPI Client result() const;

  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(1, zum.client, Update) {
    recordEdit<Fwd>(this, db()->clients, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(2, zum.client, Update) {
    appEditRelease<Fwd>(this, db()->clients, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(3, zum.request, Update) {
    StringVec ids;
    if (fields == Secret) ids.push(before.id);
    requestComplete(this, ZuMv(ids), updated, ZuMv(complete));
    return {};
  }

  ClientEditSaga_() = default;
  ClientEditSaga_(Client before_, String ifMatch_, Client values_, uint8_t fields_, int64_t updated_, IdemRequest request_, bool stateOnly_, State::T state_, uint32_t overlapSeconds_) :
    before{ZuMv(before_)},
    ifMatch{ZuMv(ifMatch_)},
    values{ZuMv(values_)},
    fields{ZuMv(fields_)},
    updated{ZuMv(updated_)},
    request{ZuMv(request_)},
    stateOnly{ZuMv(stateOnly_)},
    state{ZuMv(state_)},
    overlapSeconds{ZuMv(overlapSeconds_)} { }
  ClientEditSaga_(ClientEditArgs args) :
    ClientEditSaga_{ZuMv(args.before), ZuMv(args.ifMatch), ZuMv(args.values), ZuMv(args.fields), ZuMv(args.updated), ZuMv(args.request), ZuMv(args.stateOnly), ZuMv(args.state), ZuMv(args.overlapSeconds)} {
    response = ZuMv(args.result);
    if (response) response->item = result();
  }
  ZmRef<AdminEditResult<Client>> response;
  void setError(unsigned value) { error = value; if (response) response->error = value; }
  ~ClientEditSaga_() override;
};


using ClientEditSaga = ClientEditSaga_<
  ZmHeap<"Zum.ClientEditSaga", ClientEditSaga_<>>>;

ZfStruct(, (ClientEditSaga, FB),
  (before,		(Ctor<0>),			UDT),
  (ifMatch,		(Ctor<1>),			String),
  (values,		(Ctor<2>),			UDT),
  (updated,		(Ctor<4>),			Int64),
  (request,		(Ctor<5>),			UDT),
  (stateOnly,		(Ctor<6>),			Bool),
  (state,		(Ctor<7>, Enum<State::Map>),	Int8),
  (fields,		(Ctor<3>),			UInt8),
  (overlapSeconds,	(Ctor<8>),			UInt32));
ZiFB::AsTable<"Zum.fbs.ClientEdit"> ZiFB_Fmt(ClientEditSaga *);

template <typename Heap>
void ClientEditSaga_<Heap>::edit(Client &item) const
{
  if (fields == Secret) {
    if (overlapSeconds) {
	item.previousSecretDigest = item.secretDigest;
	item.previousSecretExpires = updated + overlapSeconds;
    } else {
	item.previousSecretDigest.null();
	item.previousSecretExpires = 0;
    }
    item.secretDigest = values.secretDigest;
    item.secretVersion = before.secretVersion + 1;
    return;
  }
  if (fields & 1) item.label = values.label;
  if (fields & 2) item.redirects = values.redirects;
  if (fields & 4) item.grants = values.grants;
  if (fields & 8) item.identityScopes = values.identityScopes;
}

template <typename Heap>
unsigned ClientEditSaga_<Heap>::recordError(const Client &item) const
{
  if (stateOnly ? state < 0 || state >= State::N :
      !fields || (fields != Secret && (fields & ~15U)))
    return 400;
  String etag{"\"v"};
  etag << item.version << '"';
  if (ifMatch != etag || item.version != before.version) return 412;
  if (!before.version || before.owner || item.owner || item.state == State::Revoked ||
      (!unchanged() && before.version == UINT64_MAX)) return 409;
  if (!stateOnly && fields == Secret) {
    if (values.secretDigest.length() != Ztls::SecretHash::Size ||
        updated > INT64_MAX - overlapSeconds) return 400;
    return clientType(item.profile) != ClientType::Confidential ||
      item.secretVersion != before.secretVersion || before.secretVersion == UINT64_MAX ? 409 : 0;
  }
  if (!stateOnly && !clientConfigValid(item.profile,
      (fields & 4) ? values.grants : item.grants,
      item.refreshAllowed,
      (fields & 2) ? values.redirects : item.redirects)) return 409;
  return 0;
}

template <typename Heap>
Client ClientEditSaga_<Heap>::result() const
{
  Client item = before;
  if (!unchanged()) {
    if (stateOnly) item.state = state;
    else edit(item);
    item.version = before.version + 1;
    item.updated = updated;
  }
  item.owner = 0;
  return item;
}

ZuAssert((Zdb_::SagaDefValid<ClientEditSaga>{}));

template <typename Heap>
ClientEditSaga_<Heap>::~ClientEditSaga_() = default;

SagaRef clientEdit(ZuBSpan data) {
  return ClientEditSaga::load(data);
}

Zdb_::SagaDef clientEditDef() {
  return ClientEditSaga::def();
}
SagaRef clientEdit(ClientEditArgs args) {
  return new ClientEditSaga{ZuMv(args)};
}

} // namespace Zum
