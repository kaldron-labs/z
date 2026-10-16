//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_key_add.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/zumd_saga_admin_ops.hh>
#include <zlib/ZtlsSec.hh>

namespace Zum {

struct KeyAddSaga : public ZmNHeap<KeyAddSaga>,
    public ZdbSaga<KeyAddSaga, DB> {
friend inline ZuStringT<"Zum.KeyAddSaga"> ZmNHeapID(KeyAddSaga *);

  using Base = ZdbSaga<KeyAddSaga, DB>;
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
  using Type = ZuStringT<"keyAdd.v1">;
  enum { NPhases = 3 };

  SignKey before;
  SignKey values;
  int64_t updated = 0;
  IdemRequest request;
  unsigned error = 0;

  bool unchanged() const { return false; }
  ZumAPI unsigned validate() const;
  ZumAPI SignKey result() const;

  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(1, zum.sign_key, Insert) {
    if constexpr (Fwd)
      if (auto code = validate()) { setError(code); complete(false); return {}; }
    auto table = db()->signKeys;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	ZdbRowRef<SignKey> row = new ZdbRow<SignKey>{table, ZdbShard{0}};
	this->insert(table, ZuMv(row), ZuMv(complete),
	  [this](ZdbRow<SignKey> *row, auto &&complete) mutable {
	    new (row->ptr()) SignKey{result()};
	    complete(row->commit());
	  });
      } else {
	this->template findDel<0>(table, 0, ZuFwdTuple(values.id), ZuMv(complete),
	  [](ZdbRow<SignKey> *row, auto &&complete) mutable {
	    complete(!row || row->commit());
	  });
      }
    });
    return {};
  }
  ZdbSagaPhase(2, zum.request, Update) {
    StringVec ids;
    ids.push(values.id);
    requestComplete(this, ZuMv(ids), updated, ZuMv(complete));
    return {};
  }

  KeyAddSaga() = default;
  KeyAddSaga(SignKey before_, SignKey values_, int64_t updated_, IdemRequest request_) :
    before{ZuMv(before_)},
    values{ZuMv(values_)},
    updated{ZuMv(updated_)},
    request{ZuMv(request_)} { }
  KeyAddSaga(KeyAddArgs args) :
    KeyAddSaga{ZuMv(args.before), ZuMv(args.values), ZuMv(args.updated), ZuMv(args.request)} {
    response = ZuMv(args.result);
    if (response) response->item = result();
  }
  ZmRef<AdminEditResult<SignKey>> response;
  void setError(unsigned value) { error = value; if (response) response->error = value; }
  ~KeyAddSaga() override;
};

ZfStruct(, (KeyAddSaga, FB),
  (before,	(Ctor<0>),	UDT),
  (values,	(Ctor<1>),	UDT),
  (updated,	(Ctor<2>),	Int64),
  (request,	(Ctor<3>),	UDT));
Zfb::AsTable<"Zum.fbs.KeyAdd"> Zfb_Fmt(KeyAddSaga *);

static bool publicKeyValid(const SignKey &key, ZuBSpan expected = {})
{
  Bytes point;
  return signKeyPublic(key, point) &&
    (!expected || Ztls::ctEqual(point, expected));
}

unsigned KeyAddSaga::validate() const
{
  if (!values.id || values.id != before.id || !values.issuer || values.algorithm != "ES256" ||
      !values.publicJwk || bool(values.providerRef) == bool(values.privateMaterial) ||
      values.notBefore <= 0 || (values.retireAfter && values.retireAfter <= values.notBefore) ||
      updated <= 0 || !publicKeyValid(values)) return 400;
  return before.version ? 409 : 0;
}

SignKey KeyAddSaga::result() const
{
  SignKey item = values;
  item.state = State::Active;
  item.version = 1;
  item.created = item.updated = updated;
  return item;
}

ZuAssert((Zdb_::SagaDefValid<KeyAddSaga>{}));

KeyAddSaga::~KeyAddSaga() = default;

SagaRef keyAdd(ZuBSpan data) {
  return KeyAddSaga::load(data);
}

Zdb_::SagaDef keyAddDef() {
  return KeyAddSaga::def();
}
SagaRef keyAdd(KeyAddArgs args) {
  return new KeyAddSaga{ZuMv(args)};
}

} // namespace Zum
