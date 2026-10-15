//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_revoke.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/zumd_saga_image.hh>

namespace Zum {

template <typename Heap = ZuVoid>
struct RevokeSaga_ : public Heap,
    public ZdbSaga<RevokeSaga_<Heap>, DB> {
  using Base = ZdbSaga<RevokeSaga_<Heap>, DB>;
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
  using Type = ZuStringT<"revoke.v2">;
  enum { NPhases = 8 };

  BytesVec sessions;
  BytesVec consents;
  BytesVec grants;
  int64_t updated = 0;
  IdemRequest request;
  unsigned error = 0;
  ZmRef<RevokeResult> result;

  void setError(unsigned value) {
    error = value;
    if (result) result->error = value;
  }

  template <bool Fwd, bool Release, typename Table, typename Complete>
  void change(Table *table, const BytesVec &images, Complete complete)
  {
    using T = typename Table::T;
    T before;
    if (!SagaImage::load(images[iteration()], before)) {
      setError(400); complete(false); return;
    }
    if (!revokeEligible(before)) { skip(ZuMv(complete)); return; }
    table->run(0, [this, table, before = ZuMv(before), complete = ZuMv(complete)]() mutable {
      ZuStructKeyT<T, 0> key{ZuStructKey<0>(before)};
      table->template find<0>(0, ZuMv(key),
	[this, table, before = ZuMv(before), complete = ZuMv(complete)](
	    ZdbRowRef<T> row) mutable {
	  if (!row) { setError(409); complete(!Fwd); return; }
	  update(table, ZuMv(row), ZuMv(complete),
	    [this, before = ZuMv(before)](ZdbRow<T> *row, auto &&complete) mutable {
	      auto &item = row->data();
	      if constexpr (Release) {
		item.owner = Fwd ? uint128_t{0} : id();
	      } else {
		if constexpr (Fwd) {
		  bool valid = !item.owner && item.state == before.state;
		  if constexpr (ZuIsSame<T, Grant>{})
		    valid &= item.expires == before.expires && item.digest == before.digest;
		  else valid &= item.version == before.version && before.version != UINT64_MAX;
		  if (!valid) { setError(409); complete(false); return; }
		}
		item.state = Fwd ? State::Revoked : before.state;
		item.owner = Fwd ? id() : before.owner;
		if constexpr (!ZuIsSame<T, Grant>{}) {
		  item.version = before.version + Fwd;
		  item.updated = Fwd ? updated : before.updated;
		}
	      }
	      complete(row->commit());
	    });
	});
    });
  }

  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatPhase(1, zum.session, Update, sessions.length()) {
    change<Fwd, false>(db()->sessions, sessions, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatPhase(2, zum.consent, Update, consents.length()) {
    change<Fwd, false>(db()->consents, consents, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatPhase(3, zum.grant, Update, grants.length()) {
    change<Fwd, false>(db()->grants, grants, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatPhase(4, zum.session, Update, sessions.length()) {
    change<Fwd, true>(db()->sessions, sessions, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatPhase(5, zum.consent, Update, consents.length()) {
    change<Fwd, true>(db()->consents, consents, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatPhase(6, zum.grant, Update, grants.length()) {
    change<Fwd, true>(db()->grants, grants, ZuMv(complete)); return {};
  }
  ZdbSagaPhase(7, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete)); return {};
  }

  RevokeSaga_() = default;
  RevokeSaga_(BytesVec sessions_, BytesVec consents_, BytesVec grants_,
      int64_t updated_, IdemRequest request_,
      ZmRef<RevokeResult> result_ = {}) :
    sessions(ZuMv(sessions_)), consents(ZuMv(consents_)),
    grants(ZuMv(grants_)), updated{updated_}, request{ZuMv(request_)},
    result{ZuMv(result_)} { }
  RevokeSaga_(RevokeArgs args) :
    RevokeSaga_{ZuMv(args.sessions), ZuMv(args.consents),
      ZuMv(args.grants), args.updated, ZuMv(args.request),
      ZuMv(args.result)} { }
  ~RevokeSaga_() override;
};

using RevokeSaga = RevokeSaga_<
  ZmHeap<"Zum.RevokeSaga", RevokeSaga_<>>>;

ZfStruct(, (RevokeSaga, FB),
  (sessions,	(Ctor<0>),	BytesVec),
  (consents,	(Ctor<1>),	BytesVec),
  (grants,	(Ctor<2>),	BytesVec),
  (updated,	(Ctor<3>),	Int64),
  (request,	(Ctor<4>),	UDT));
Zfb::AsTable<"Zum.fbs.Revoke"> Zfb_Fmt(RevokeSaga *);

template <typename T> static bool revokeEligible(const T &item)
{
  return !item.owner && item.state != State::Revoked &&
    item.state != State::Consumed;
}

template <typename T> static unsigned revokeCount(const BytesVec &images)
{
  unsigned n = 0;
  for (const auto &image: images) {
    T item;
    if (SagaImage::load(image, item) && revokeEligible(item)) ++n;
  }
  return n;
}

unsigned RevokeArgs::count() const
{
  return revokeCount<Session>(sessions) + revokeCount<Consent>(consents) +
    revokeCount<Grant>(grants);
}

ZuAssert((Zdb_::SagaDefValid<RevokeSaga>{}));

template <typename Heap>
RevokeSaga_<Heap>::~RevokeSaga_() = default;

SagaRef revoke(ZuBSpan data) {
  return RevokeSaga::load(data);
}

Zdb_::SagaDef revokeDef() {
  return RevokeSaga::def();
}
SagaRef revoke(RevokeArgs args) {
  return new RevokeSaga{ZuMv(args)};
}

} // namespace Zum
