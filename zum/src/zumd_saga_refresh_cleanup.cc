//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_refresh_cleanup.hh>
#include <zlib/zumd_identity_db.hh>

namespace Zum {

struct RefreshCleanupSaga : public ZmNHeap<RefreshCleanupSaga>,
    public ZdbSaga<RefreshCleanupSaga, DB> {
friend inline ZuStringT<"Zum.RefreshCleanupSaga"> ZmNHeapID(RefreshCleanupSaga *);

  using Base = ZdbSaga<RefreshCleanupSaga, DB>;
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
  using Type = ZuStringT<"refreshCleanup.v2">;
  enum { NPhases = 2 };

  RefreshDeleteVec refreshes;
  int64_t updated = 0;
  unsigned error = 0;

  ZdbSagaRepeatPhase(0, zum.refresh, Delete, refreshes.length()) {
    if constexpr (!Fwd) { complete(true); return {}; }
    const auto &before = refreshes[iteration()];
    if (before.owner || updated <= 0 || before.expires > updated) {
      error = 400; complete(false); return {};
    }
    auto table = db()->refresh;
    table->run(0, [this, table, before, complete = ZuMv(complete)]() mutable {
      this->template findDel<0>(table, 0, ZuFwdTuple(before.id),
        ZuMv(complete), [this, before = ZuMv(before)](
            ZdbRow<Refresh> *row, auto &&complete) mutable {
          if (!row) { complete(true); return; }
          const auto &item = row->data();
          if (item.owner != before.owner || item.version != before.version ||
              item.expires != before.expires || item.state != before.state) {
            error = 409; complete(false); return;
          }
          complete(row->commit());
        });
    });
    return {};
  }
  ZdbSagaPhase(1, zum.refresh, Update) { complete(true); return {}; }

  RefreshCleanupSaga() = default;
  RefreshCleanupSaga(RefreshDeleteVec refreshes_, int64_t updated_) :
    refreshes{ZuMv(refreshes_)}, updated{updated_} { }
  RefreshCleanupSaga(RefreshCleanupArgs args) :
    RefreshCleanupSaga{ZuMv(args.refreshes), args.updated} { }
  ~RefreshCleanupSaga() override;
};

ZfStruct(, (RefreshCleanupSaga, FB),
  (refreshes,	(Ctor<0>),	UDT),
  (updated,	(Ctor<1>),	Int64));
Zfb::AsTable<"Zum.fbs.RefreshCleanup"> Zfb_Fmt(RefreshCleanupSaga *);


ZuAssert((Zdb_::SagaDefValid<RefreshCleanupSaga>{}));

RefreshCleanupSaga::~RefreshCleanupSaga() = default;

SagaRef refreshCleanup(ZuBSpan data) {
  return RefreshCleanupSaga::load(data);
}

Zdb_::SagaDef refreshCleanupDef() {
  return RefreshCleanupSaga::def();
}
SagaRef refreshCleanup(RefreshCleanupArgs args) {
  return new RefreshCleanupSaga{ZuMv(args)};
}

} // namespace Zum
