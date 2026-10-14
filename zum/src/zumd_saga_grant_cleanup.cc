//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_grant_cleanup.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_request_db.hh>

namespace Zum {

template <typename Heap = ZuVoid>
struct GrantCleanupSaga_ : public Heap,
    public ZdbSaga<GrantCleanupSaga_<Heap>, DB> {
  using Base = ZdbSaga<GrantCleanupSaga_<Heap>, DB>;
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
  using Type = ZuStringT<"grantCleanup.v3">;
  enum { NPhases = 3 };

  GrantDeleteVec grants;
  int64_t updated = 0;
  IdemRequest request;
  unsigned error = 0;
  ZmRef<RevokeResult> result;

  void setError(unsigned value) {
    error = value;
    if (result) result->error = value;
  }

  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatPhase(1, zum.grant, Delete, grants.length()) {
    if constexpr (!Fwd) { complete(true); return {}; }
    const auto &before = grants[iteration()];
    if (before.owner || before.state == State::Pending ||
        before.expires > updated || updated <= 0) {
      setError(400); complete(false); return {};
    }
    auto table = db()->grants;
    table->run(0, [this, table, before, complete = ZuMv(complete)]() mutable {
      this->template findDel<0>(table, 0, ZuFwdTuple(before.id),
        ZuMv(complete), [this, before = ZuMv(before)](
            ZdbRow<Grant> *row, auto &&complete) mutable {
          if (!row) { complete(true); return; }
          const auto &item = row->data();
          if (item.owner != before.owner || item.expires != before.expires ||
              item.state != before.state || item.digest != before.digest) {
            setError(409); complete(false); return;
          }
          complete(row->commit());
        });
    });
    return {};
  }
  ZdbSagaPhase(2, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete)); return {};
  }

  GrantCleanupSaga_() = default;
  GrantCleanupSaga_(GrantDeleteVec grants_, int64_t updated_,
      IdemRequest request_, ZmRef<RevokeResult> result_ = {}) :
    grants{ZuMv(grants_)}, updated{updated_}, request{ZuMv(request_)},
    result{ZuMv(result_)} { }
  GrantCleanupSaga_(GrantCleanupArgs args) :
    GrantCleanupSaga_{ZuMv(args.grants), args.updated, ZuMv(args.request),
      ZuMv(args.result)} { }
  ~GrantCleanupSaga_() override;
};

using GrantCleanupSaga = GrantCleanupSaga_<
  ZmHeap<"Zum.GrantCleanupSaga", GrantCleanupSaga_<>>>;

ZfStruct(, (GrantCleanupSaga, FB),
  (grants,	(Ctor<0>),	UDT),
  (updated,	(Ctor<1>),	Int64),
  (request,	(Ctor<2>),	UDT));
ZiFB::AsTable<"Zum.fbs.GrantCleanup"> ZiFB_Fmt(GrantCleanupSaga *);


ZuAssert((Zdb_::SagaDefValid<GrantCleanupSaga>{}));

template <typename Heap>
GrantCleanupSaga_<Heap>::~GrantCleanupSaga_() = default;

SagaRef grantCleanup(ZuBSpan data) {
  return GrantCleanupSaga::load(data);
}

Zdb_::SagaDef grantCleanupDef() {
  return GrantCleanupSaga::def();
}
SagaRef grantCleanup(GrantCleanupArgs args) {
  return new GrantCleanupSaga{ZuMv(args)};
}

} // namespace Zum
