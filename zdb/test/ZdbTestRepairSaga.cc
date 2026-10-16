//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "ZdbRepairDefs.hh"

namespace zdbtest {

// Exercise native row commits through saga intent/effect and compensation.
struct RepairSaga : public ZmNHeap<RepairSaga>, public ZdbSaga<RepairSaga, RepairDB> {
friend inline ZuStringT<"Zdb.Test.Repair"> ZmNHeapID(RepairSaga *);

  using Base = ZdbSaga<RepairSaga, RepairDB>;
  using Base::db;
  using Type = ZuStringT<"repairSaga">;
  enum { NPhases = 3 };
  uint64_t id;
  bool fail;

  ZdbSagaPhase(0, repair_saga, Insert) {
    auto table = db()->rows;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	ZdbRowRef<RepairRow> row = new ZdbRow<RepairRow>{table, 0};
	this->insert(table, ZuMv(row), ZuMv(complete),
	  [this](ZdbRow<RepairRow> *row, auto &&complete) mutable {
	    ++db()->inserts;
	    new (row->ptr()) RepairRow{id, {-0.0f, -7.25f},
	      {"initial", "a long CStringVec element retained in the store"},
	      "required immutable value", 1.5f, {}, 12.5, {"control"}};
	    complete(bool(row->commit()));
	  });
      } else {
	this->template findDel<0>(table, 0, ZuFwdTuple(id), ZuMv(complete),
	  [this](ZdbRow<RepairRow> *row, auto &&complete) mutable {
	    ++db()->deletes;
	    db()->valid &= repairValues(row->data(), false);
	    complete(bool(row->commit()));
	  });
      }
    });
    return {};
  }

  ZdbSagaPhase(1, repair_saga, Update) {
    auto table = db()->rows;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      this->template findUpd<1, ZuSeq<1>>(table, 0, ZuFwdTuple(Fwd ? 1.5f : -2.5f),
	ZuMv(complete), [this](ZdbRow<RepairRow> *row, auto &&complete) mutable {
	  ++db()->updates;
	  db()->valid &= repairValues(row->data(), !Fwd);
	  row->data().real = Fwd ? -2.5f : 1.5f;
	  row->data().reals[0] = Fwd ? 3.5f : -0.0f;
	  row->data().names[0] = Fwd ? "updated" : "initial";
	  complete(bool(row->commit()));
	});
    });
    return {};
  }

  ZdbSagaPhase(2, repair_saga, Update) {
    ZuAssert(Fwd);
    if (db()->paused) {
      db()->resume = ZuMv(complete);
      db()->paused->post();
      return {};
    }
    if (fail) complete(false);
    else this->skip(ZuMv(complete));
    return {};
  }
  RepairSaga(uint64_t id_, bool fail_) : id{id_}, fail{fail_} { }
};

ZfStruct(, (RepairSaga, FB),
  (id,		(Ctor<0>),	UInt64),
  (fail,	(Ctor<1>),	Bool));
Zfb::AsTable<"repair.Saga"> Zfb_Fmt(RepairSaga *);
ZuAssert((Zdb_::SagaDefValid<RepairSaga>{}));

ZmRef<ZdbAnySaga<RepairDB>> repairDef(uint64_t id, bool fail) {
  return new RepairSaga{id, fail};
}

Zdb_::SagaDef repairSagaDef() {
  return RepairSaga::def();
}

} // zdbtest
