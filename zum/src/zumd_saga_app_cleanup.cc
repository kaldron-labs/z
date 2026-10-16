//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_app_cleanup.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_provider_db.hh>

namespace Zum {

struct AppCleanupSaga : public ZmNHeap<AppCleanupSaga>,
    public ZdbSaga<AppCleanupSaga, DB> {
friend inline ZuStringT<"Zum.AppCleanupSaga"> ZmNHeapID(AppCleanupSaga *);

  using Base = ZdbSaga<AppCleanupSaga, DB>;
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
  using Type = ZuStringT<"appCleanup.v2">;
  enum { NPhases = 3 };

  AppID appID = 0;
  EvidenceDeleteVec evidence;
  ConsentDeleteVec consents;
  int64_t updated = 0;
  unsigned error = 0;

  ZdbSagaRepeatPhase(0, zum.evidence, Delete, evidence.length()) {
    if constexpr (!Fwd) { complete(true); return {}; }
    const auto &before = evidence[iteration()];
    if (!before.appID || before.appID != appID || before.owner || updated <= 0) {
      error = 400; complete(false); return {};
    }
    auto table = db()->evidence;
    table->run(0, [this, table, before, complete = ZuMv(complete)]() mutable {
      this->template findDel<0>(table, 0,
        ZuFwdTuple(before.appID, before.userID, before.providerID),
        ZuMv(complete), [this, before = ZuMv(before)](
            ZdbRow<Evidence> *row, auto &&complete) mutable {
          if (!row) { complete(true); return; }
          const auto &item = row->data();
          if (item.owner != before.owner || item.appID != before.appID ||
              item.version != before.version || item.updated != before.updated ||
              item.deadline != before.deadline) {
            error = 409; complete(false); return;
          }
          complete(row->commit());
        });
    });
    return {};
  }
  ZdbSagaRepeatPhase(1, zum.consent, Delete, consents.length()) {
    if constexpr (!Fwd) { complete(true); return {}; }
    const auto &before = consents[iteration()];
    if (!before.appID || before.appID != appID || before.owner || updated <= 0) {
      error = 400; complete(false); return {};
    }
    auto table = db()->consents;
    table->run(0, [this, table, before, complete = ZuMv(complete)]() mutable {
      this->template findDel<0>(table, 0,
        ZuFwdTuple(before.userID, before.clientID, before.appID),
        ZuMv(complete), [this, before = ZuMv(before)](
            ZdbRow<Consent> *row, auto &&complete) mutable {
          if (!row) { complete(true); return; }
          const auto &item = row->data();
          if (item.owner != before.owner || item.appID != before.appID ||
              item.version != before.version || item.updated != before.updated ||
              item.state != before.state) {
            error = 409; complete(false); return;
          }
          complete(row->commit());
        });
    });
    return {};
  }
  ZdbSagaPhase(2, zum.app, Update) { complete(true); return {}; }

  AppCleanupSaga() = default;
  AppCleanupSaga(AppID appID_, EvidenceDeleteVec evidence_,
      ConsentDeleteVec consents_, int64_t updated_) :
    appID{appID_}, evidence{ZuMv(evidence_)},
    consents{ZuMv(consents_)}, updated{updated_} { }
  AppCleanupSaga(AppCleanupArgs args) :
    AppCleanupSaga{args.appID, ZuMv(args.evidence),
      ZuMv(args.consents), args.updated} { }
  ~AppCleanupSaga() override;
};

ZfStruct(, (AppCleanupSaga, FB),
  (appID,	(Ctor<0>),	UInt64),
  (evidence,	(Ctor<1>),	UDT),
  (consents,	(Ctor<2>),	UDT),
  (updated,	(Ctor<3>),	Int64));
Zfb::AsTable<"Zum.fbs.AppCleanup"> Zfb_Fmt(AppCleanupSaga *);



ZuAssert((Zdb_::SagaDefValid<AppCleanupSaga>{}));

AppCleanupSaga::~AppCleanupSaga() = default;

SagaRef appCleanup(ZuBSpan data) {
  return AppCleanupSaga::load(data);
}

Zdb_::SagaDef appCleanupDef() {
  return AppCleanupSaga::def();
}
SagaRef appCleanup(AppCleanupArgs args) {
  return new AppCleanupSaga{ZuMv(args)};
}

} // namespace Zum
