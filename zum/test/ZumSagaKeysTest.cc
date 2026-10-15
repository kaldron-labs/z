//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "ZumSagaTestSupport.hh"
#include <zlib/zumd_saga_key_binding.hh>
#include <zlib/zumd_saga_secret_rekey.hh>
#include <zlib/zumd_saga_ssf_delivery_add.hh>

namespace Zum {
ZfStruct(, (KeyBindingArgs, FB),
  (issuer,		(Ctor<0>),	String),
  (beforeCheck,		(Ctor<1>),	Bytes),
  (afterCheck,		(Ctor<2>),	Bytes),
  (beforePending,	(Ctor<3>),	Bytes),
  (afterPending,		(Ctor<4>),	Bytes));
Zfb::AsTable<"Zum.fbs.KeyBinding"> Zfb_Fmt(KeyBindingArgs *);
}

static void keys()
{
  ZuTestScope(keys);
  roundTrip(Zum::keyBindingDef(), Zum::keyBinding(Zum::KeyBindingArgs{}));
  roundTrip(Zum::secretRekeyDef(), Zum::secretRekey(Zum::SecretRekeyArgs{}));
  roundTrip(Zum::ssfDeliveryAddDef(), Zum::ssfDeliveryAdd(Zum::SSFDelivery{}));

  Zum::KeyBindingArgs binding{
    .issuer = "issuer",
    .beforeCheck = Zum::Bytes{ZuBSpan{"old-check"}},
    .afterCheck = Zum::Bytes{ZuBSpan{"old-check"}},
    .afterPending = Zum::Bytes{ZuBSpan{"new-check"}}};
  auto saga = Zum::keyBinding(ZuMv(binding));
  Zdb_::SagaPayload payload;
  ZuCheck(saga->save(payload) == Zfb::Error::OK);
  auto decoded = Zfb::handler<Zum::KeyBindingArgs>(payload).ctor();
  ZuCheck(decoded.issuer == "issuer" &&
    decoded.beforeCheck == ZuBSpan{"old-check"} &&
    decoded.afterCheck == decoded.beforeCheck && !decoded.beforePending &&
    decoded.afterPending == ZuBSpan{"new-check"});
  distinct(Zum::keyBindingDef(), ZuMv(saga),
    Zum::keyBinding(Zum::KeyBindingArgs{}));
  phase(Zum::keyBindingDef(), 0, "zum.issuer", ZdbSagaOp::Update);
  bool rejected = false;
  try { Zum::keyBinding(ZuBSpan{"invalid payload"}); }
  catch (...) { rejected = true; }
  ZuCheck(rejected);

  for (unsigned field = Zum::SecretRekeyArgs::ProviderField;
      field <= Zum::SecretRekeyArgs::SSFField; ++field) {
    Zum::SecretRekeyArgs rekey{.field = field, .providerID = 7,
      .appID = 8, .userID = 9, .keyID = "signer",
      .before = Zum::Bytes{ZuBSpan{"old-envelope"}},
      .after = Zum::Bytes{ZuBSpan{"new-envelope"}}};
    distinct(Zum::secretRekeyDef(), Zum::secretRekey(ZuMv(rekey)),
      Zum::secretRekey(Zum::SecretRekeyArgs{}));
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(keys);
  return 0;
}
