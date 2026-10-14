//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_role_map_delete.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_saga_admin_ops.hh>
#include <zlib/zumd_request_db.hh>

namespace Zum {

template <typename Heap = ZuVoid>
struct RoleMapDeleteSaga_ : public Heap,
    public ZdbSaga<RoleMapDeleteSaga_<Heap>, DB> {
  using Base = ZdbSaga<RoleMapDeleteSaga_<Heap>, DB>;
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
  using Type = ZuStringT<"roleMapDelete.v1">;
  enum { NPhases = 5 };

  App app;
  RoleMap before;
  String ifMatch;
  int64_t updated = 0;
  IdemRequest request;
  unsigned error = 0;
  ZmRef<AdminEditResult<RoleMap>> response;

  void setError(unsigned value) {
    error = value;
    if (response) response->error = value;
  }

  bool unchanged() const { return false; }
  unsigned appError(const App &) const;
  unsigned recordError(const RoleMap &) const;


  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(1, zum.app, Update) {
    appEditStart<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(2, zum.role_map, Delete) {
    auto table = db()->roleMaps;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
        RoleMapTable::Key<0> key{ZuStructKey<0>(before)};
        this->template findDel<0>(table, 0, key, ZuMv(complete),
	  [this](ZdbRow<RoleMap> *row, auto &&complete) mutable {
	    if (!row) { setError(404); complete(false); return; }
	    if (auto code = recordError(row->data())) {
	      setError(code); complete(false); return;
	    }
	    complete(row->commit());
	  });
      } else {
        ZdbRowRef<RoleMap> row = new ZdbRow<RoleMap>{table, ZdbShard{0}};
        insert(table, ZuMv(row), ZuMv(complete),
	  [this](ZdbRow<RoleMap> *row, auto &&complete) mutable {
	    new (row->ptr()) RoleMap{before};
	    complete(row->commit());
	  });
      }
    });
    return {};
  }
  ZdbSagaPhase(3, zum.app, Update) {
    appEditPublish<Fwd>(this, true, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(4, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }

  RoleMapDeleteSaga_() = default;
  RoleMapDeleteSaga_(App app_, RoleMap before_, String ifMatch_,
      int64_t updated_, IdemRequest request_,
      ZmRef<AdminEditResult<RoleMap>> response_ = {}) :
    app{ZuMv(app_)}, before{ZuMv(before_)}, ifMatch{ZuMv(ifMatch_)},
    updated{updated_}, request{ZuMv(request_)}, response{ZuMv(response_)} { }
  RoleMapDeleteSaga_(RoleMapDeleteArgs args) :
    RoleMapDeleteSaga_{ZuMv(args.app), ZuMv(args.before), ZuMv(args.ifMatch), args.updated, ZuMv(args.request), ZuMv(args.result)} { }
  ~RoleMapDeleteSaga_() override;
};

using RoleMapDeleteSaga = RoleMapDeleteSaga_<
  ZmHeap<"Zum.RoleMapDeleteSaga", RoleMapDeleteSaga_<>>>;

ZfStruct(ZumAPI, (RoleMapDeleteSaga, FB),
  (app,		(Ctor<0>),	UDT),
  (before,	(Ctor<1>),	UDT),
  (ifMatch,	(Ctor<2>),	String),
  (updated,	(Ctor<3>),	Int64),
  (request,	(Ctor<4>),	UDT));
ZiFB::AsTable<"Zum.fbs.RoleMapDelete"> ZiFB_Fmt(RoleMapDeleteSaga *);

template <typename Heap>
unsigned RoleMapDeleteSaga_<Heap>::appError(const App &item) const
{
  return appVersionError(app, item);
}

template <typename Heap>
unsigned RoleMapDeleteSaga_<Heap>::recordError(const RoleMap &item) const
{
  String etag{"\"v"};
  etag << item.version << '"';
  if (ifMatch != etag || item.version != before.version) return 412;
  return !before.version || before.appID != app.id || before.owner || item.owner ? 409 : 0;
}

ZuAssert((Zdb_::SagaDefValid<RoleMapDeleteSaga>{}));

template <typename Heap>
RoleMapDeleteSaga_<Heap>::~RoleMapDeleteSaga_() = default;

SagaRef roleMapDelete(ZuBSpan data) {
  return RoleMapDeleteSaga::load(data);
}

Zdb_::SagaDef roleMapDeleteDef() {
  return RoleMapDeleteSaga::def();
}
SagaRef roleMapDelete(RoleMapDeleteArgs args) { return new RoleMapDeleteSaga{ZuMv(args)}; }

} // namespace Zum
