//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_role_delete.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_saga_image.hh>
#include <zlib/zumd_request_db.hh>

namespace Zum {

template <typename Heap = ZuVoid>
struct RoleDeleteSaga_ : public Heap,
    public ZdbSaga<RoleDeleteSaga_<Heap>, DB> {
  using Base = ZdbSaga<RoleDeleteSaga_<Heap>, DB>;
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
  using Type = ZuStringT<"roleDelete.v2">;
  enum { NPhases = 13 };

  App app;
  Role role;
  BytesVec members;
  BytesVec clients;
  BytesVec admins;
  BytesVec maps;
  int64_t updated = 0;
  IdemRequest request;

  template <typename T>
  static auto authVersion(const T &item)
  {
    if constexpr (ZuIsSame<T, Assignment>{} || ZuIsSame<T, ClientAccess>{})
      return item.authVersion;
    else
      return uint64_t{0};
  }

  template <typename T>
  static void authVersion(T &item, uint64_t value)
  {
    if constexpr (ZuIsSame<T, Assignment>{} || ZuIsSame<T, ClientAccess>{})
      item.authVersion = value;
  }

  template <bool Fwd, bool Release, typename Table, typename Complete>
  void refs(Table *table, const BytesVec &images, Complete complete)
  {
    using T = typename Table::T;
    // Decode on the destination shard; the immutable image remains saga-owned.
    auto image = &images[iteration()];
    table->run(0, [this, table, image, complete = ZuMv(complete)]() mutable {
      T old;
      if (!SagaImage::load(*image, old) || old.appID != app.id || old.owner ||
	  old.version == UINT64_MAX || authVersion(old) == UINT64_MAX) {
	complete(false);
	return;
      }
      IDVec next;
      next.size(old.roleIDs.length());
      bool found = false;
      for (auto id: old.roleIDs) {
	if (id == role.id) found = true;
	else next.push(id);
      }
      if (!found) { complete(false); return; }
      ZuStructKeyT<T, 0> key{ZuStructKey<0>(old)};
      this->template findUpd<0>(table, 0, ZuMv(key), ZuMv(complete),
	[this, old = ZuMv(old), next = ZuMv(next)](
	    ZdbRow<T> *row, auto &&complete) mutable {
	  if (!row) { complete(!Fwd); return; }
	  auto &item = row->data();
	  constexpr bool changed = Release || !Fwd;
	  const auto owner = (Release ? Fwd : !Fwd) ? id() : uint128_t{0};
	  if (item.owner != owner || item.version != old.version + changed ||
	      authVersion(item) != authVersion(old) +
		(changed && (ZuIsSame<T, Assignment>{} || ZuIsSame<T, ClientAccess>{})) ||
	      item.updated != (changed ? updated : old.updated) ||
	      item.roleIDs != (changed ? next : old.roleIDs)) {
	    complete(!Fwd);
	    return;
	  }
	  if constexpr (Release) {
	    item.owner = Fwd ? uint128_t{0} : id();
	  } else {
	    item.roleIDs = Fwd ? ZuMv(next) : ZuMv(old.roleIDs);
	    item.version = old.version + Fwd;
	    authVersion(item, authVersion(old) + Fwd);
	    item.updated = Fwd ? updated : old.updated;
	    item.owner = Fwd ? id() : uint128_t{0};
	  }
	  complete(row->commit());
	});
    });
  }

  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }

  ZdbSagaPhase(1, zum.app, Update) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(db()->apps, 0, ZuFwdTuple(app.id), ZuMv(complete),
	[this](ZdbRow<App> *row, auto &&complete) mutable {
	  if (!row || app.owner || app.version == UINT64_MAX ||
	      app.authVersion == UINT64_MAX || role.appID != app.id ||
	      row->data().owner != (Fwd ? uint128_t{0} : id()) ||
	      row->data().version != app.version ||
	      row->data().authVersion != app.authVersion ||
	      row->data().state != app.state || row->data().updated != app.updated) {
	    complete(!Fwd);
	    return;
	  }
	  row->data().owner = Fwd ? id() : uint128_t{0};
	  complete(row->commit());
	});
    });
    return {};
  }

  ZdbSagaPhase(2, zum.role, Update) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(db()->roles, 0, ZuFwdTuple(app.id, role.id),
	ZuMv(complete), [this](ZdbRow<Role> *row, auto &&complete) mutable {
	  if (!row || role.owner || role.tombstone || role.version == UINT64_MAX ||
	      row->data().owner != (Fwd ? uint128_t{0} : id()) ||
	      row->data().version != role.version + !Fwd ||
	      row->data().tombstone != !Fwd) {
	    complete(!Fwd);
	    return;
	  }
	  auto &item = row->data();
	  item.actions = Fwd ? ZtBitmap{} : role.actions;
	  item.state = Fwd ? State::Revoked : role.state;
	  item.tombstone = Fwd;
	  item.version = role.version + Fwd;
	  item.updated = Fwd ? updated : role.updated;
	  item.owner = Fwd ? id() : uint128_t{0};
	  complete(row->commit());
	});
    });
    return {};
  }

  ZdbSagaRepeatPhase(3, zum.assignment, Update, members.length()) {
    refs<Fwd, false>(db()->assignments, members, ZuMv(complete));
    return {};
  }
  ZdbSagaRepeatPhase(4, zum.client_access, Update, clients.length()) {
    refs<Fwd, false>(db()->clientAccess, clients, ZuMv(complete));
    return {};
  }
  ZdbSagaRepeatPhase(5, zum.admin_access, Update, admins.length()) {
    refs<Fwd, false>(db()->adminAccess, admins, ZuMv(complete));
    return {};
  }
  ZdbSagaRepeatPhase(6, zum.role_map, Delete, maps.length()) {
    auto image = &maps[iteration()];
    db()->shardRun(0, [this, image, complete = ZuMv(complete)]() mutable {
      RoleMap old;
      if (!SagaImage::load(*image, old) || old.appID != app.id ||
	  old.roleID != role.id || old.owner) {
	complete(false);
	return;
      }
      if constexpr (Fwd) {
	ZuStructKeyT<RoleMap, 0> key{ZuStructKey<0>(old)};
	this->template findDel<0>(db()->roleMaps, 0, ZuMv(key), ZuMv(complete),
	  [old = ZuMv(old)](ZdbRow<RoleMap> *row, auto &&complete) mutable {
	    if (!row || row->data().owner || row->data().version != old.version ||
		row->data().roleID != old.roleID) {
	      complete(false);
	      return;
	    }
	    complete(row->commit());
	  });
      } else {
	ZdbRowRef<RoleMap> row = new ZdbRow<RoleMap>{db()->roleMaps, ZdbShard{0}};
	insert(db()->roleMaps, ZuMv(row), ZuMv(complete),
	  [old = ZuMv(old)](ZdbRow<RoleMap> *row, auto &&complete) mutable {
	    if (!row) { complete(false); return; }
	    new (row->ptr()) RoleMap{ZuMv(old)};
	    complete(row->commit());
	  });
      }
    });
    return {};
  }

  ZdbSagaRepeatPhase(7, zum.assignment, Update, members.length()) {
    refs<Fwd, true>(db()->assignments, members, ZuMv(complete));
    return {};
  }
  ZdbSagaRepeatPhase(8, zum.client_access, Update, clients.length()) {
    refs<Fwd, true>(db()->clientAccess, clients, ZuMv(complete));
    return {};
  }
  ZdbSagaRepeatPhase(9, zum.admin_access, Update, admins.length()) {
    refs<Fwd, true>(db()->adminAccess, admins, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(10, zum.role, Update) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(db()->roles, 0, ZuFwdTuple(app.id, role.id),
	ZuMv(complete), [this](ZdbRow<Role> *row, auto &&complete) mutable {
	  if (!row || row->data().owner != (Fwd ? id() : uint128_t{0}) ||
	      row->data().version != role.version + 1 || !row->data().tombstone) {
	    complete(!Fwd);
	    return;
	  }
	  row->data().owner = Fwd ? uint128_t{0} : id();
	  complete(row->commit());
	});
    });
    return {};
  }

  ZdbSagaPhase(11, zum.app, Update) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(db()->apps, 0, ZuFwdTuple(app.id), ZuMv(complete),
	[this](ZdbRow<App> *row, auto &&complete) mutable {
	  if (!row || row->data().owner != (Fwd ? id() : uint128_t{0}) ||
	      row->data().version != app.version + !Fwd ||
	      row->data().authVersion != app.authVersion + !Fwd) {
	    complete(!Fwd);
	    return;
	  }
	  row->data().version = app.version + Fwd;
	  row->data().authVersion = app.authVersion + Fwd;
	  row->data().updated = Fwd ? updated : app.updated;
	  row->data().owner = Fwd ? uint128_t{0} : id();
	  complete(row->commit());
	});
    });
    return {};
  }
  ZdbSagaPhase(12, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }



  RoleDeleteSaga_() = default;
  RoleDeleteSaga_(App app_, Role role_, BytesVec members_, BytesVec clients_,
      BytesVec admins_, BytesVec maps_, int64_t updated_, IdemRequest request_) :
    app{ZuMv(app_)}, role{ZuMv(role_)}, members(ZuMv(members_)),
    clients(ZuMv(clients_)), admins(ZuMv(admins_)), maps(ZuMv(maps_)),
    updated{updated_}, request{ZuMv(request_)} { }
  RoleDeleteSaga_(RoleDeleteArgs args) :
    RoleDeleteSaga_{ZuMv(args.app), ZuMv(args.role), ZuMv(args.members),
      ZuMv(args.clients), ZuMv(args.admins), ZuMv(args.maps), args.updated,
      ZuMv(args.request)} { }
  ~RoleDeleteSaga_() override;
};

using RoleDeleteSaga = RoleDeleteSaga_<
  ZmHeap<"Zum.RoleDeleteSaga", RoleDeleteSaga_<>>>;

ZfStruct(ZumAPI, (RoleDeleteSaga, FB),
  (app,		(Ctor<0>),	UDT),
  (role,	(Ctor<1>),	UDT),
  (members,	(Ctor<2>),	BytesVec),
  (clients,	(Ctor<3>),	BytesVec),
  (admins,	(Ctor<4>),	BytesVec),
  (maps,	(Ctor<5>),	BytesVec),
  (updated,	(Ctor<6>),	Int64),
  (request,	(Ctor<7>),	UDT));
Zfb::AsTable<"Zum.fbs.RoleDelete"> Zfb_Fmt(RoleDeleteSaga *);

ZuAssert((Zdb_::SagaDefValid<RoleDeleteSaga>{}));

template <typename Heap>
RoleDeleteSaga_<Heap>::~RoleDeleteSaga_() = default;
SagaRef roleDelete(ZuBSpan data) {
  return RoleDeleteSaga::load(data);
}
Zdb_::SagaDef roleDeleteDef() {
  return RoleDeleteSaga::def();
}
SagaRef roleDelete(RoleDeleteArgs args) { return new RoleDeleteSaga{ZuMv(args)}; }

} // namespace Zum
