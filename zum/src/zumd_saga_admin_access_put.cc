//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_admin_access_put.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/zumd_saga_admin_ops.hh>

namespace Zum {

template <typename Heap = ZuVoid>
struct AdminAccessPutSaga_ : public Heap,
    public ZdbSaga<AdminAccessPutSaga_<Heap>, DB> {
  using Base = ZdbSaga<AdminAccessPutSaga_<Heap>, DB>;
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
  using Type = ZuStringT<"adminAccessPut.v1">;
  enum { NPhases = 7 };

  App app;
  AdminAccess before;
  AdminAccess values;
  String ifMatch;
  String ifNoneMatch;
  int64_t updated = 0;
  IdemRequest request;
  unsigned error = 0;

  bool unchanged() const { return false; }
  ZumAPI unsigned appError(const App &) const;
  ZumAPI unsigned recordError(const AdminAccess &) const;
  ZumAPI void validate(Zdb_::SagaCompleteFn);
  ZumAPI AdminAccess result() const;

  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(1, zum.app, Update) {
    appEditStart<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(2, zum.admin_access, Insert) {
    recordPut<Fwd, true>(this, db()->adminAccess, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(3, zum.admin_access, Update) {
    recordPut<Fwd, false>(this, db()->adminAccess, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(4, zum.admin_access, Update) {
    appEditRelease<Fwd>(this, db()->adminAccess, ZuMv(complete));
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

  AdminAccessPutSaga_() = default;
  AdminAccessPutSaga_(App app_, AdminAccess before_, AdminAccess values_, String ifMatch_, String ifNoneMatch_, int64_t updated_, IdemRequest request_) :
    app{ZuMv(app_)},
    before{ZuMv(before_)},
    values{ZuMv(values_)},
    ifMatch{ZuMv(ifMatch_)},
    ifNoneMatch{ZuMv(ifNoneMatch_)},
    updated{ZuMv(updated_)},
    request{ZuMv(request_)} { }
  AdminAccessPutSaga_(AdminAccessPutArgs args) :
    AdminAccessPutSaga_{ZuMv(args.app), ZuMv(args.before), ZuMv(args.values), ZuMv(args.ifMatch), ZuMv(args.ifNoneMatch), ZuMv(args.updated), ZuMv(args.request)} {
    response = ZuMv(args.result);
    if (response) response->item = result();
  }
  ZmRef<AdminEditResult<AdminAccess>> response;
  void setError(unsigned value) { error = value; if (response) response->error = value; }
  ~AdminAccessPutSaga_() override;
};


using AdminAccessPutSaga = AdminAccessPutSaga_<
  ZmHeap<"Zum.AdminAccessPutSaga", AdminAccessPutSaga_<>>>;

ZfStruct(, (AdminAccessPutSaga, FB),
  (app,		(Ctor<0>),	UDT),
  (before,	(Ctor<1>),	UDT),
  (values,	(Ctor<2>),	UDT),
  (ifMatch,	(Ctor<3>),	String),
  (ifNoneMatch,	(Ctor<4>),	String),
  (updated,	(Ctor<5>),	Int64),
  (request,	(Ctor<6>),	UDT));
Zfb::AsTable<"Zum.fbs.AdminAccessPut"> Zfb_Fmt(AdminAccessPutSaga *);

template <typename Heap>
unsigned AdminAccessPutSaga_<Heap>::appError(const App &item) const
{
  return appVersionError(app, item);
}

template <typename Heap>
unsigned AdminAccessPutSaga_<Heap>::recordError(const AdminAccess &item) const
{
  return putError(*this, item);
}

template <typename Heap>
AdminAccess AdminAccessPutSaga_<Heap>::result() const { return accessResult(*this); }



template <typename Heap>
void AdminAccessPutSaga_<Heap>::validate(Zdb_::SagaCompleteFn complete)
{
  if (!values.appID || values.appID != app.id || values.appID != before.appID ||
      !values.actorID || values.actorID != before.actorID || values.actorKind != before.actorKind ||
      (values.actorKind != ActorKind::User && values.actorKind != ActorKind::Client) ||
      !uniqueRefs(values.roleIDs) || !uniqueRefs(values.operationIDs, false)) {
    setError(400); complete(false); return;
  }
  for (auto id: values.operationIDs)
    if (id >= MgmtOp::N || !managementRoute(id)) { setError(400); complete(false); return; }
  if (!before.version && (ifMatch || ifNoneMatch != "*")) {
    setError(ifMatch ? 412 : 428); complete(false); return;
  }
  switch (values.actorKind) {
    case ActorKind::User: {
      ZuBox<UserID> id;
      if (id.scan(values.actorID) != int(values.actorID.length()) ||
	  ZuCmp<UserID>::null(id) || !id) {
	setError(400); complete(false); return;
      }
      auto table = db()->users;
      table->run(0, [this, table, id = UserID{id}, complete = ZuMv(complete)]() mutable {
	table->template find<0>(0, ZuFwdTuple(id),
	  [this, complete = ZuMv(complete)](ZdbRowRef<User> row) mutable {
	    if (!row || row->data().owner || row->data().state == State::Revoked ||
		row->data().state == State::Consumed) {
	      setError(400); complete(false); return;
	    }
	    accessRefs(this, 0, ZuMv(complete));
	  });
      });
      return;
    }
    case ActorKind::Client: {
      auto table = db()->clients;
      table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
	table->template find<0>(0, ZuFwdTuple(values.actorID),
	  [this, complete = ZuMv(complete)](ZdbRowRef<Client> row) mutable {
	    if (!row || row->data().owner || row->data().state != State::Active ||
		clientType(row->data().profile) != ClientType::Confidential) {
	      setError(400); complete(false); return;
	    }
	    accessRefs(this, 0, ZuMv(complete));
	  });
      });
      return;
    }
  }
}

ZuAssert((Zdb_::SagaDefValid<AdminAccessPutSaga>{}));

template <typename Heap>
AdminAccessPutSaga_<Heap>::~AdminAccessPutSaga_() = default;

SagaRef adminAccessPut(ZuBSpan data) {
  return AdminAccessPutSaga::load(data);
}

Zdb_::SagaDef adminAccessPutDef() {
  return AdminAccessPutSaga::def();
}
SagaRef adminAccessPut(AdminAccessPutArgs args) {
  return new AdminAccessPutSaga{ZuMv(args)};
}

} // namespace Zum
