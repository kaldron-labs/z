//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Offline secret-key rotation mutations; no keys or plaintext in saga payloads.

#ifndef zumd_rekey_HH
#define zumd_rekey_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZuTuple.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_ssf_db.hh>
#include <zlib/zumd_secret.hh>
#include <zlib/zum_saga_fbs.h>

namespace Zum {

struct DB;
ZuDerive(RekeyFn, (ZmFn<void(bool), ZmFnHeapID<"Zum.RekeyFn">>));

// Caller has stopped all service writers; completion precedes store shutdown.
void serverRekey(DB *, DBContext *, Ztls::Random &, String issuer,
  Bytes oldKey, Bytes newKey, RekeyFn);

struct KeyBinding : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"keyBinding.v1">;
  enum { NSteps = 1 };

  String issuer;
  Bytes beforeCheck;
  Bytes afterCheck;
  Bytes beforePending;
  Bytes afterPending;

  ZdbSagaStep(0, zum.issuer, Update) {
    auto table = context->issuers;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(table, 0, ZuFwdTuple(issuer), ZuMv(complete),
	[this](ZdbRow<Issuer> *row, auto &&complete) mutable {
	  if (!row) { complete(false); return; }
	  auto &value = row->data();
	  if constexpr (Fwd) {
	    if (value.keyCheck != beforeCheck ||
		value.pendingKeyCheck != beforePending) {
	      complete(false); return;
	    }
	  }
	  value.keyCheck = Fwd ? afterCheck : beforeCheck;
	  value.pendingKeyCheck = Fwd ? afterPending : beforePending;
	  complete(row->commit());
	});
    });
    return {};
  }
};
ZfbStruct(ZumAPI, KeyBinding,
  (issuer, (Ctor<0>),			String),
  (beforeCheck, (Ctor<1>),		Bytes),
  (afterCheck, (Ctor<2>),		Bytes),
  (beforePending, (Ctor<3>),		Bytes),
  (afterPending, (Ctor<4>),		Bytes));

struct SecretRekey : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"secretRekey.v2">;
  enum { NSteps = 4 };
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
      saga->findUpd<0>(table, 0, ZuMv(key), ZuMv(complete),
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

  ZdbSagaStep(0, zum.provider, Update) {
    if (field > SSFField) { complete(false); return {}; }
    if (field != ProviderField) { saga->skip(ZuMv(complete)); return {}; }
    replace<Fwd, &Provider::clientSecret>(context->providers,
      ZuTuple{providerID}, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(1, zum.evidence, Update) {
    if (field != EvidenceField) { saga->skip(ZuMv(complete)); return {}; }
    replace<Fwd, &Evidence::protectedRefreshToken>(context->evidence,
      ZuTuple{appID, userID, providerID}, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(3, zum.ssf_rx, Update) {
    if (field != SSFField) { saga->skip(ZuMv(complete)); return {}; }
    replace<Fwd, &SSFRx::callbackAuth>(context->ssfRx,
      ZuTuple{keyID}, ZuMv(complete));
    return {};
  }
  ZdbSagaStep(2, zum.sign_key, Update) {
    if (field != SignKeyField) { saga->skip(ZuMv(complete)); return {}; }
    replace<Fwd, &SignKey::privateMaterial>(context->signKeys,
      ZuTuple{keyID}, ZuMv(complete));
    return {};
  }
};
ZfbStruct(ZumAPI, SecretRekey,
  (field, (Ctor<0>, Deflt<SecretRekey::ProviderField>),		UInt32),
  (providerID, (Ctor<1>),					UInt64),
  (appID, (Ctor<2>),						UInt64),
  (userID, (Ctor<3>),						UInt64),
  (keyID, (Ctor<4>),						String),
  (before, (Ctor<5>),						Bytes),
  (after, (Ctor<6>),						Bytes));

} // namespace Zum

#endif /* zumd_rekey_HH */
