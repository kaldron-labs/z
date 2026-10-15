//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_secret_rekey.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_ssf_db.hh>

namespace Zum {

template <typename Heap = ZuVoid>
struct SecretRekeySaga_ : public Heap,
    public ZdbSaga<SecretRekeySaga_<Heap>, DB> {
  using Base = ZdbSaga<SecretRekeySaga_<Heap>, DB>;
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
  using Type = ZuStringT<"secretRekey.v2">;
  enum { NPhases = 4 };
  enum { ProviderField, EvidenceField, SignKeyField, SSFField };

  unsigned field = ProviderField;
  ProviderID providerID = 0;
  AppID appID = 0;
  UserID userID = 0;
  String keyID;
  Bytes before;
  Bytes after;

  template <bool Fwd, auto Field, typename Table, typename Key>
  void replace(Table *table, Key key, Zdb_::SagaCompleteFn complete)
  {
    table->run(0, [this, table, key = ZuMv(key),
      complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(table, 0, ZuMv(key), ZuMv(complete),
	[this](auto row, auto &&complete) mutable {
	  if (!row) { complete(false); return; }
	  auto &value = row->data().*Field;
	  if constexpr (Fwd) {
	    if constexpr (!ZuIsSame<Table, SignKeyTable>{}) {
	      if (row->data().owner) { complete(false); return; }
	    }
	    if (value != before) { complete(false); return; }
	  }
	  value = Fwd ? after : before;
	  complete(row->commit());
	});
    });
  }

  ZdbSagaPhase(0, zum.provider, Update) {
    if (field > SSFField) { complete(false); return {}; }
    if (field != ProviderField) { skip(ZuMv(complete)); return {}; }
    replace<Fwd, &Provider::clientSecret>(db()->providers,
      ZuTuple{providerID}, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(1, zum.evidence, Update) {
    if (field != EvidenceField) { skip(ZuMv(complete)); return {}; }
    replace<Fwd, &Evidence::protectedRefreshToken>(db()->evidence,
      ZuTuple{appID, userID, providerID}, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(3, zum.ssf_rx, Update) {
    if (field != SSFField) { skip(ZuMv(complete)); return {}; }
    replace<Fwd, &SSFRx::callbackAuth>(db()->ssfRx,
      ZuTuple{keyID}, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(2, zum.sign_key, Update) {
    if (field != SignKeyField) { skip(ZuMv(complete)); return {}; }
    replace<Fwd, &SignKey::privateMaterial>(db()->signKeys,
      ZuTuple{keyID}, ZuMv(complete));
    return {};
  }

  SecretRekeySaga_() = default;
  SecretRekeySaga_(unsigned field_, ProviderID providerID_, AppID appID_,
      UserID userID_, String keyID_, Bytes before_, Bytes after_) :
    field{field_}, providerID{providerID_}, appID{appID_}, userID{userID_},
    keyID{ZuMv(keyID_)}, before{ZuMv(before_)}, after{ZuMv(after_)} { }
  SecretRekeySaga_(SecretRekeyArgs args) :
    SecretRekeySaga_{args.field, args.providerID, args.appID, args.userID,
      ZuMv(args.keyID), ZuMv(args.before), ZuMv(args.after)} { }
  ~SecretRekeySaga_() override;
};

using SecretRekeySaga = SecretRekeySaga_<
  ZmHeap<"Zum.SecretRekeySaga", SecretRekeySaga_<>>>;

ZfStruct(, (SecretRekeySaga, FB),
  (field,	(Ctor<0>, Deflt<SecretRekeySaga::ProviderField>),	UInt32),
  (providerID,	(Ctor<1>),					UInt64),
  (appID,	(Ctor<2>),					UInt64),
  (userID,	(Ctor<3>),					UInt64),
  (keyID,	(Ctor<4>),					String),
  (before,	(Ctor<5>),					Bytes),
  (after,	(Ctor<6>),					Bytes));
Zfb::AsTable<"Zum.fbs.SecretRekey"> Zfb_Fmt(SecretRekeySaga *);

ZuAssert((Zdb_::SagaDefValid<SecretRekeySaga>{}));

template <typename Heap>
SecretRekeySaga_<Heap>::~SecretRekeySaga_() = default;

SagaRef secretRekey(ZuBSpan data) {
  return SecretRekeySaga::load(data);
}

Zdb_::SagaDef secretRekeyDef() {
  return SecretRekeySaga::def();
}
SagaRef secretRekey(SecretRekeyArgs args) {
  return new SecretRekeySaga{ZuMv(args)};
}

} // namespace Zum
