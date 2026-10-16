//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_key_binding.hh>
#include <zlib/zumd_identity_db.hh>

namespace Zum {

struct KeyBindingSaga : public ZmNHeap<KeyBindingSaga>,
    public ZdbSaga<KeyBindingSaga, DB> {
friend inline ZuStringT<"Zum.KeyBindingSaga"> ZmNHeapID(KeyBindingSaga *);

  using Base = ZdbSaga<KeyBindingSaga, DB>;
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
  using Type = ZuStringT<"keyBinding.v1">;
  enum { NPhases = 1 };

  String issuer;
  Bytes beforeCheck;
  Bytes afterCheck;
  Bytes beforePending;
  Bytes afterPending;

  ZdbSagaPhase(0, zum.issuer, Update) {
    auto table = db()->issuers;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(table, 0, ZuFwdTuple(issuer), ZuMv(complete),
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

  KeyBindingSaga() = default;
  KeyBindingSaga(String issuer_, Bytes beforeCheck_, Bytes afterCheck_,
      Bytes beforePending_, Bytes afterPending_) :
    issuer{ZuMv(issuer_)}, beforeCheck{ZuMv(beforeCheck_)},
    afterCheck{ZuMv(afterCheck_)}, beforePending{ZuMv(beforePending_)},
    afterPending{ZuMv(afterPending_)} { }
  KeyBindingSaga(KeyBindingArgs args) :
    KeyBindingSaga{ZuMv(args.issuer), ZuMv(args.beforeCheck),
      ZuMv(args.afterCheck), ZuMv(args.beforePending),
      ZuMv(args.afterPending)} { }
  ~KeyBindingSaga() override;
};

ZfStruct(, (KeyBindingSaga, FB),
  (issuer,		(Ctor<0>),	String),
  (beforeCheck,		(Ctor<1>),	Bytes),
  (afterCheck,		(Ctor<2>),	Bytes),
  (beforePending,	(Ctor<3>),	Bytes),
  (afterPending,		(Ctor<4>),	Bytes));
Zfb::AsTable<"Zum.fbs.KeyBinding"> Zfb_Fmt(KeyBindingSaga *);

ZuAssert((Zdb_::SagaDefValid<KeyBindingSaga>{}));

KeyBindingSaga::~KeyBindingSaga() = default;

SagaRef keyBinding(ZuBSpan data) {
  return KeyBindingSaga::load(data);
}

Zdb_::SagaDef keyBindingDef() {
  return KeyBindingSaga::def();
}
SagaRef keyBinding(KeyBindingArgs args) {
  return new KeyBindingSaga{ZuMv(args)};
}

} // namespace Zum
