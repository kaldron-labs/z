//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_ssf_delivery_add.hh>
#include <zlib/zumd_ssf_db.hh>

namespace Zum {

struct SSFDeliveryAddSaga : public ZmNHeap<SSFDeliveryAddSaga>,
    public ZdbSaga<SSFDeliveryAddSaga, DB> {
friend inline ZuStringT<"Zum.SSFDeliveryAddSaga"> ZmNHeapID(SSFDeliveryAddSaga *);

  using Base = ZdbSaga<SSFDeliveryAddSaga, DB>;
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
  using Type = ZuStringT<"ssfDeliveryAdd.v1">;
  enum { NPhases = 2 };

  SSFDelivery delivery;

  ZdbSagaPhase(0, zum.ssf_delivery, Insert) {
    auto table = db()->ssfDeliveries;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
        ZdbRowRef<SSFDelivery> row =
          new ZdbRow<SSFDelivery>{table, ZdbShard{0}};
        insert(table, ZuMv(row), ZuMv(complete),
          [this](ZdbRow<SSFDelivery> *row, auto &&complete) mutable {
            if (!row) { complete(false); return; }
            new (row->ptr()) SSFDelivery{delivery};
            row->data().owner = id();
            complete(row->commit());
          });
      } else {
        this->template findDel<0>(table, 0,
          ZuFwdTuple(delivery.eventID, delivery.receiverID),
          ZuMv(complete), [this](ZdbRow<SSFDelivery> *row,
              auto &&complete) mutable {
            if (!row || row->data().owner != id()) {
              complete(true);
              return;
            }
            complete(row->commit());
          });
      }
    });
    return {};
  }
  ZdbSagaPhase(1, zum.ssf_delivery, Update) {
    auto table = db()->ssfDeliveries;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(table, 0,
        ZuFwdTuple(delivery.eventID, delivery.receiverID), ZuMv(complete),
        [this](ZdbRow<SSFDelivery> *row, auto &&complete) mutable {
          if (!row) { complete(false); return; }
          auto &item = row->data();
          if constexpr (Fwd) {
            if (item.owner != id()) { complete(false); return; }
            item.owner = 0;
          } else {
            if (item.owner) { complete(false); return; }
            item.owner = id();
          }
          complete(row->commit());
        });
    });
    return {};
  }

  SSFDeliveryAddSaga() = default;
  SSFDeliveryAddSaga(SSFDelivery delivery_) : delivery{ZuMv(delivery_)} { }
  ~SSFDeliveryAddSaga() override;
};

ZfStruct(, (SSFDeliveryAddSaga, FB),
  (delivery, (Ctor<0>),		UDT));
Zfb::AsTable<"Zum.fbs.SSFDeliveryAdd"> Zfb_Fmt(SSFDeliveryAddSaga *);

ZuAssert((Zdb_::SagaDefValid<SSFDeliveryAddSaga>{}));

SSFDeliveryAddSaga::~SSFDeliveryAddSaga() = default;

SagaRef ssfDeliveryAdd(ZuBSpan data) {
  return SSFDeliveryAddSaga::load(data);
}

Zdb_::SagaDef ssfDeliveryAddDef() {
  return SSFDeliveryAddSaga::def();
}
SagaRef ssfDeliveryAdd(SSFDelivery args) {
  return new SSFDeliveryAddSaga{ZuMv(args)};
}

} // namespace Zum
