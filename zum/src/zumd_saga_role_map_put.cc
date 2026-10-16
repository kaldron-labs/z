//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_role_map_put.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_saga_admin_ops.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/zumd_provider_db.hh>

namespace Zum {

struct RoleMapPutSaga : public ZmNHeap<RoleMapPutSaga>,
    public ZdbSaga<RoleMapPutSaga, DB> {
friend inline ZuStringT<"Zum.RoleMapPutSaga"> ZmNHeapID(RoleMapPutSaga *);

  using Base = ZdbSaga<RoleMapPutSaga, DB>;
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
  using Type = ZuStringT<"roleMapPut.v1">;
  enum { NPhases = 7 };

  App app;
  RoleMap before;
  RoleID roleID = 0;
  String ifMatch;
  String ifNoneMatch;
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
  void validate(Zdb_::SagaCompleteFn);
  RoleMap result() const;


  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(1, zum.app, Update) {
    appEditStart<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(2, zum.role_map, Insert) {
    recordPut<Fwd, true>(this, db()->roleMaps, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(3, zum.role_map, Update) {
    recordPut<Fwd, false>(this, db()->roleMaps, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(4, zum.role_map, Update) {
    appEditRelease<Fwd>(this, db()->roleMaps, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(5, zum.app, Update) {
    appEditPublish<Fwd>(this, true, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(6, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }

  RoleMapPutSaga() = default;
  RoleMapPutSaga(App app_, RoleMap before_, RoleID roleID_, String ifMatch_,
      String ifNoneMatch_, int64_t updated_, IdemRequest request_,
      ZmRef<AdminEditResult<RoleMap>> response_ = {}) :
    app{ZuMv(app_)}, before{ZuMv(before_)}, roleID{roleID_},
    ifMatch{ZuMv(ifMatch_)}, ifNoneMatch{ZuMv(ifNoneMatch_)},
    updated{updated_}, request{ZuMv(request_)}, response{ZuMv(response_)} {
    if (response) response->item = result();
  }
  RoleMapPutSaga(RoleMapPutArgs args) :
    RoleMapPutSaga{ZuMv(args.app), ZuMv(args.before), args.roleID, ZuMv(args.ifMatch), ZuMv(args.ifNoneMatch), args.updated, ZuMv(args.request), ZuMv(args.result)} { }
  ~RoleMapPutSaga() override;
};

ZfStruct(ZumAPI, (RoleMapPutSaga, FB),
  (app,		(Ctor<0>),	UDT),
  (before,	(Ctor<1>),	UDT),
  (roleID,	(Ctor<2>),	UInt64),
  (ifMatch,	(Ctor<3>),	String),
  (ifNoneMatch,	(Ctor<4>),	String),
  (updated,	(Ctor<5>),	Int64),
  (request,	(Ctor<6>),	UDT));
Zfb::AsTable<"Zum.fbs.RoleMapPut"> Zfb_Fmt(RoleMapPutSaga *);

unsigned RoleMapPutSaga::appError(const App &item) const
{
  return appVersionError(app, item);
}

unsigned RoleMapPutSaga::recordError(const RoleMap &item) const
{
  return putError(*this, item);
}

RoleMap RoleMapPutSaga::result() const
{
  return RoleMap{.appID = before.appID, .providerID = before.providerID,
    .value = before.value, .roleID = roleID, .state = State::Active,
    .version = before.version + 1, .created = before.version ? before.created : updated,
    .updated = updated};
}

void RoleMapPutSaga::validate(Zdb_::SagaCompleteFn complete)
{
  if (!before.appID || before.appID != app.id || !before.providerID || !before.value || !roleID) {
    setError(400); complete(false); return;
  }
  if (!before.version && (ifMatch || ifNoneMatch != "*")) {
    setError(ifMatch ? 412 : 428); complete(false); return;
  }
  auto providers = db()->providers;
  providers->run(0, [this, providers, complete = ZuMv(complete)]() mutable {
    providers->template find<0>(0, ZuFwdTuple(before.providerID),
      [this, complete = ZuMv(complete)](ZdbRowRef<Provider> row) mutable {
	if (!row || row->data().owner || row->data().state != State::Active) {
	  setError(400); complete(false); return;
	}
	auto roles = db()->roles;
	roles->run(0, [this, roles, complete = ZuMv(complete)]() mutable {
	  roles->template find<0>(0, ZuFwdTuple(before.appID, roleID),
	    [this, complete = ZuMv(complete)](ZdbRowRef<Role> row) mutable {
	      if (!row || row->data().owner || row->data().tombstone ||
		  row->data().state != State::Active) {
		setError(400); complete(false); return;
	      }
	      complete(true);
	    });
	});
      });
  });
}

ZuAssert((Zdb_::SagaDefValid<RoleMapPutSaga>{}));

RoleMapPutSaga::~RoleMapPutSaga() = default;

SagaRef roleMapPut(ZuBSpan data) {
  return RoleMapPutSaga::load(data);
}

Zdb_::SagaDef roleMapPutDef() {
  return RoleMapPutSaga::def();
}
SagaRef roleMapPut(RoleMapPutArgs args) { return new RoleMapPutSaga{ZuMv(args)}; }

} // namespace Zum
