//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_session_cleanup.hh>
#include <zlib/zumd_identity_db.hh>

namespace Zum {

template <typename Heap = ZuVoid>
struct SessionCleanupSaga_ : public Heap,
    public ZdbSaga<SessionCleanupSaga_<Heap>, DB> {
  using Base = ZdbSaga<SessionCleanupSaga_<Heap>, DB>;
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
  using Type = ZuStringT<"sessionCleanup.v2">;
  enum { NPhases = 2 };

  SessionDeleteVec sessions;
  int64_t updated = 0;
  unsigned error = 0;

  ZdbSagaRepeatPhase(0, zum.session, Delete, sessions.length()) {
    if constexpr (!Fwd) { complete(true); return {}; }
    const auto &before = sessions[iteration()];
    if (before.owner || updated <= 0 ||
        (before.idleDeadline > updated && before.absoluteDeadline > updated)) {
      error = 400; complete(false); return {};
    }
    auto table = db()->sessions;
    table->run(0, [this, table, before, complete = ZuMv(complete)]() mutable {
      this->template findDel<0>(table, 0, ZuFwdTuple(before.digest),
        ZuMv(complete), [this, before = ZuMv(before), updated = this->updated](
            ZdbRow<Session> *row, auto &&complete) mutable {
          if (!row) { complete(true); return; }
          const auto &item = row->data();
          if (item.owner != before.owner || item.version != before.version ||
              item.idleDeadline != before.idleDeadline ||
              item.absoluteDeadline != before.absoluteDeadline ||
              (item.idleDeadline > updated && item.absoluteDeadline > updated)) {
            error = 409; complete(false); return;
          }
          complete(row->commit());
        });
    });
    return {};
  }
  ZdbSagaPhase(1, zum.session, Update) { complete(true); return {}; }

  SessionCleanupSaga_() = default;
  SessionCleanupSaga_(SessionDeleteVec sessions_, int64_t updated_) :
    sessions(ZuMv(sessions_)), updated{updated_} { }
  SessionCleanupSaga_(SessionCleanupArgs args) :
    SessionCleanupSaga_{ZuMv(args.sessions), args.updated} { }
  ~SessionCleanupSaga_() override;
};

using SessionCleanupSaga = SessionCleanupSaga_<
  ZmHeap<"Zum.SessionCleanupSaga", SessionCleanupSaga_<>>>;

ZfStruct(, (SessionCleanupSaga, FB),
  (sessions,	(Ctor<0>),	UDT),
  (updated,	(Ctor<1>),	Int64));
ZiFB::AsTable<"Zum.fbs.SessionCleanup"> ZiFB_Fmt(SessionCleanupSaga *);

ZuAssert((Zdb_::SagaDefValid<SessionCleanupSaga>{}));

template <typename Heap>
SessionCleanupSaga_<Heap>::~SessionCleanupSaga_() = default;

SagaRef sessionCleanup(ZuBSpan data) {
  return SessionCleanupSaga::load(data);
}

Zdb_::SagaDef sessionCleanupDef() {
  return SessionCleanupSaga::def();
}
SagaRef sessionCleanup(SessionCleanupArgs args) {
  return new SessionCleanupSaga{ZuMv(args)};
}

} // namespace Zum
