//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_role_add.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_saga_admin_ops.hh>
#include <zlib/zumd_request_db.hh>

namespace Zum {

template <typename Heap = ZuVoid>
struct RoleAddSaga_ : public Heap,
    public ZdbSaga<RoleAddSaga_<Heap>, DB> {
  using Base = ZdbSaga<RoleAddSaga_<Heap>, DB>;
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
  using Type = ZuStringT<"roleAdd.v1">;
  enum { NPhases = 6 };

  App app;
  Role before;
  Role values;
  int64_t updated = 0;
  IdemRequest request;
  unsigned error = 0;
  ZmRef<AdminEditResult<Role>> response;

  void setError(unsigned value) {
    error = value;
    if (response) response->error = value;
  }

  bool unchanged() const { return false; }
  unsigned appError(const App &) const;
  void validate(Zdb_::SagaCompleteFn);
  Role result() const;


  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(1, zum.app, Update) {
    appEditStart<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(2, zum.role, Insert) {
    recordPut<Fwd, true>(this, db()->roles, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(3, zum.role, Update) {
    appEditRelease<Fwd>(this, db()->roles, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(4, zum.app, Update) {
    appEditPublish<Fwd>(this, true, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(5, zum.request, Update) {
    StringVec ids;
    ids.push(String{} << values.id);
    requestComplete(this, ZuMv(ids), updated, ZuMv(complete));
    return {};
  }

  RoleAddSaga_() = default;
  RoleAddSaga_(App app_, Role before_, Role values_, int64_t updated_,
      IdemRequest request_, ZmRef<AdminEditResult<Role>> response_ = {}) :
    app{ZuMv(app_)}, before{ZuMv(before_)}, values{ZuMv(values_)},
    updated{updated_}, request{ZuMv(request_)}, response{ZuMv(response_)} {
    if (response) response->item = result();
  }
  RoleAddSaga_(RoleAddArgs args) :
    RoleAddSaga_{ZuMv(args.app), ZuMv(args.before), ZuMv(args.values), args.updated, ZuMv(args.request), ZuMv(args.result)} { }
  ~RoleAddSaga_() override;
};

using RoleAddSaga = RoleAddSaga_<
  ZmHeap<"Zum.RoleAddSaga", RoleAddSaga_<>>>;

ZfStruct(ZumAPI, (RoleAddSaga, FB),
  (app,		(Ctor<0>),	UDT),
  (before,	(Ctor<1>),	UDT),
  (values,	(Ctor<2>),	UDT),
  (updated,	(Ctor<3>),	Int64),
  (request,	(Ctor<4>),	UDT));
Zfb::AsTable<"Zum.fbs.RoleAdd"> Zfb_Fmt(RoleAddSaga *);

template <typename Heap>
unsigned RoleAddSaga_<Heap>::appError(const App &item) const
{
  return appVersionError(app, item);
}

template <typename Heap>
Role RoleAddSaga_<Heap>::result() const
{
  return Role{.appID = values.appID, .id = values.id, .name = values.name,
    .label = values.label, .state = State::Active, .origin = Origin::Custom,
    .version = 1, .created = updated, .updated = updated};
}

template <typename Heap>
void RoleAddSaga_<Heap>::validate(Zdb_::SagaCompleteFn complete)
{
  setError(catalogAddError(*this));
  if (error) { complete(false); return; }
  // The app is owned by this saga until publication. Names, including
  // tombstones, remain reserved throughout that interval.
  auto roles = db()->roles;
  roles->run(0, [this, roles, complete = ZuMv(complete)]() mutable {
    roles->template find<1>(0, ZuFwdTuple(values.appID, values.name),
	[this, complete = ZuMv(complete)](ZdbRowRef<Role> row) mutable {
      // A recovered insert can have reached the later release step before
      // restart, leaving this saga's row with no owner. The saved insert UN
      // still determines whether to replay the insert or skip its effect.
      if (row && !(row->data().id == values.id &&
	  (row->data().owner == id() || !row->data().owner) &&
	  row->data().version == 1)) {
	setError(409); complete(false); return;
      }
      complete(true);
    });
  });
}

ZuAssert((Zdb_::SagaDefValid<RoleAddSaga>{}));

template <typename Heap>
RoleAddSaga_<Heap>::~RoleAddSaga_() = default;

SagaRef roleAdd(ZuBSpan data) {
  return RoleAddSaga::load(data);
}

Zdb_::SagaDef roleAddDef() {
  return RoleAddSaga::def();
}
SagaRef roleAdd(RoleAddArgs args) { return new RoleAddSaga{ZuMv(args)}; }

} // namespace Zum
