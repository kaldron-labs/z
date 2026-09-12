//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// role tombstone and recoverable assignment/reference removal

#ifndef ZumRoleDelete_HH
#define ZumRoleDelete_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZumAppDB.hh>
#include <zlib/ZumProviderDB.hh>
#include <zlib/ZumSagaImage.hh>
#include <zlib/ZumRequestDB.hh>
#include <zlib/zum_saga_fbs.h>

namespace Zum {

struct RoleDelete : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"roleDelete.v2">;
  enum { NSteps = 15 };

  App		app;
  Role		role;
  BytesVec	members;
  BytesVec	clients;
  BytesVec	admins;
  BytesVec	scopes;
  BytesVec	maps;
  int64_t	updated = 0;
  IdemRequest	request;

  template <typename T>
  static auto authVersion(const T &item)
  {
    if constexpr (ZuIsSame<T, Membership>{} || ZuIsSame<T, ClientAccess>{})
      return item.authVersion;
    else
      return uint64_t{0};
  }

  template <typename T>
  static void authVersion(T &item, uint64_t value)
  {
    if constexpr (ZuIsSame<T, Membership>{} || ZuIsSame<T, ClientAccess>{})
      item.authVersion = value;
  }

  template <bool Fwd, bool Release, typename Table, typename Complete>
  void refs(Table *table, const BytesVec &images, Complete complete)
  {
    using T = typename Table::T;
    // Decode on the destination shard; the immutable image remains saga-owned.
    auto image = &images[saga->iteration()];
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
      saga->template findUpd<0>(table, 0, ZuMv(key), ZuMv(complete),
	[this, old = ZuMv(old), next = ZuMv(next)](
	    ZdbRow<T> *row, auto &&complete) mutable {
	  if (!row) { complete(!Fwd); return; }
	  auto &item = row->data();
	  constexpr bool changed = Release || !Fwd;
	  const auto owner = (Release ? Fwd : !Fwd) ? saga->id() : uint128_t{0};
	  if (item.owner != owner || item.version != old.version + changed ||
	      authVersion(item) != authVersion(old) +
		(changed && (ZuIsSame<T, Membership>{} || ZuIsSame<T, ClientAccess>{})) ||
	      item.updated != (changed ? updated : old.updated) ||
	      item.roleIDs != (changed ? next : old.roleIDs)) {
	    complete(!Fwd);
	    return;
	  }
	  if constexpr (Release) {
	    item.owner = Fwd ? uint128_t{0} : saga->id();
	  } else {
	    item.roleIDs = Fwd ? ZuMv(next) : ZuMv(old.roleIDs);
	    item.version = old.version + Fwd;
	    authVersion(item, authVersion(old) + Fwd);
	    item.updated = Fwd ? updated : old.updated;
	    item.owner = Fwd ? saga->id() : uint128_t{0};
	  }
	  complete(row->commit());
	});
    });
  }

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }

  ZdbSagaStep(1, zum.app, Update) {
    context->apps->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->apps, 0, ZuFwdTuple(app.id), ZuMv(complete),
	[this](ZdbRow<App> *row, auto &&complete) mutable {
	  if (!row || app.owner || app.version == UINT64_MAX ||
	      app.authVersion == UINT64_MAX || role.appID != app.id ||
	      row->data().owner != (Fwd ? uint128_t{0} : saga->id()) ||
	      row->data().version != app.version ||
	      row->data().authVersion != app.authVersion ||
	      row->data().state != app.state || row->data().updated != app.updated) {
	    complete(!Fwd);
	    return;
	  }
	  row->data().owner = Fwd ? saga->id() : uint128_t{0};
	  complete(row->commit());
	});
    });
    return {};
  }

  ZdbSagaStep(2, zum.role, Update) {
    context->roles->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->roles, 0, ZuFwdTuple(app.id, role.id),
	ZuMv(complete), [this](ZdbRow<Role> *row, auto &&complete) mutable {
	  if (!row || role.owner || role.tombstone || role.version == UINT64_MAX ||
	      row->data().owner != (Fwd ? uint128_t{0} : saga->id()) ||
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
	  item.owner = Fwd ? saga->id() : uint128_t{0};
	  complete(row->commit());
	});
    });
    return {};
  }

  ZdbSagaRepeatStep(3, zum.membership, Update, members.length()) {
    refs<Fwd, false>(context->memberships, members, ZuMv(complete));
    return {};
  }
  ZdbSagaRepeatStep(4, zum.client_access, Update, clients.length()) {
    refs<Fwd, false>(context->clientAccess, clients, ZuMv(complete));
    return {};
  }
  ZdbSagaRepeatStep(5, zum.admin_access, Update, admins.length()) {
    refs<Fwd, false>(context->adminAccess, admins, ZuMv(complete));
    return {};
  }
  ZdbSagaRepeatStep(6, zum.scope, Update, scopes.length()) {
    refs<Fwd, false>(context->scopes, scopes, ZuMv(complete));
    return {};
  }

  ZdbSagaRepeatStep(7, zum.role_map, Delete, maps.length()) {
    auto image = &maps[saga->iteration()];
    context->roleMaps->run(0, [this, image, complete = ZuMv(complete)]() mutable {
      RoleMap old;
      if (!SagaImage::load(*image, old) || old.appID != app.id ||
	  old.roleID != role.id || old.owner) {
	complete(false);
	return;
      }
      if constexpr (Fwd) {
	ZuStructKeyT<RoleMap, 0> key{ZuStructKey<0>(old)};
	saga->findDel<0>(context->roleMaps, 0, ZuMv(key), ZuMv(complete),
	  [old = ZuMv(old)](ZdbRow<RoleMap> *row, auto &&complete) mutable {
	    if (!row || row->data().owner || row->data().version != old.version ||
		row->data().roleID != old.roleID) {
	      complete(false);
	      return;
	    }
	    complete(row->commit());
	  });
      } else {
	ZdbRowRef<RoleMap> row = new ZdbRow<RoleMap>{context->roleMaps, ZdbShard{0}};
	saga->insert(context->roleMaps, ZuMv(row), ZuMv(complete),
	  [old = ZuMv(old)](ZdbRow<RoleMap> *row, auto &&complete) mutable {
	    if (!row) { complete(false); return; }
	    new (row->ptr()) RoleMap{ZuMv(old)};
	    complete(row->commit());
	  });
      }
    });
    return {};
  }

  ZdbSagaRepeatStep(8, zum.membership, Update, members.length()) {
    refs<Fwd, true>(context->memberships, members, ZuMv(complete));
    return {};
  }
  ZdbSagaRepeatStep(9, zum.client_access, Update, clients.length()) {
    refs<Fwd, true>(context->clientAccess, clients, ZuMv(complete));
    return {};
  }
  ZdbSagaRepeatStep(10, zum.admin_access, Update, admins.length()) {
    refs<Fwd, true>(context->adminAccess, admins, ZuMv(complete));
    return {};
  }
  ZdbSagaRepeatStep(11, zum.scope, Update, scopes.length()) {
    refs<Fwd, true>(context->scopes, scopes, ZuMv(complete));
    return {};
  }

  ZdbSagaStep(12, zum.role, Update) {
    context->roles->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->roles, 0, ZuFwdTuple(app.id, role.id),
	ZuMv(complete), [this](ZdbRow<Role> *row, auto &&complete) mutable {
	  if (!row || row->data().owner != (Fwd ? saga->id() : uint128_t{0}) ||
	      row->data().version != role.version + 1 || !row->data().tombstone) {
	    complete(!Fwd);
	    return;
	  }
	  row->data().owner = Fwd ? uint128_t{0} : saga->id();
	  complete(row->commit());
	});
    });
    return {};
  }

  ZdbSagaStep(13, zum.app, Update) {
    context->apps->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->apps, 0, ZuFwdTuple(app.id), ZuMv(complete),
	[this](ZdbRow<App> *row, auto &&complete) mutable {
	  if (!row || row->data().owner != (Fwd ? saga->id() : uint128_t{0}) ||
	      row->data().version != app.version + !Fwd ||
	      row->data().authVersion != app.authVersion + !Fwd) {
	    complete(!Fwd);
	    return;
	  }
	  row->data().version = app.version + Fwd;
	  row->data().authVersion = app.authVersion + Fwd;
	  row->data().updated = Fwd ? updated : app.updated;
	  row->data().owner = Fwd ? uint128_t{0} : saga->id();
	  complete(row->commit());
	});
    });
    return {};
  }
  ZdbSagaStep(14, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }

};

ZfbStruct(ZumAPI, RoleDelete,
  (((app), (Ctor<0>)), (UDT)),
  (((role), (Ctor<1>)), (UDT)),
  (((members), (Ctor<2>)), (BytesVec)),
  (((clients), (Ctor<3>)), (BytesVec)),
  (((admins), (Ctor<4>)), (BytesVec)),
  (((scopes), (Ctor<5>)), (BytesVec)),
  (((maps), (Ctor<6>)), (BytesVec)),
  (((updated), (Ctor<7>)), (Int64)),
  (((request), (Ctor<8>)), (UDT)));

} // namespace Zum

#endif /* ZumRoleDelete_HH */
