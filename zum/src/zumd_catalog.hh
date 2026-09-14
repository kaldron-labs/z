//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// recoverable application-owned catalog publication

#ifndef zumd_catalog_HH
#define zumd_catalog_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_saga_image.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/zum_saga_fbs.h>

namespace Zum {

// Preserve administrator removals from the previous published role set.
// Only a newly introduced manifest binding may add effective authority.
inline IDVec catalogScopeRoles(const Scope &before, const IDVec &desired)
{
  IDVec roles;
  roles.size(desired.length());
  for (auto id: desired) {
    bool current = false, published = false;
    for (auto old: before.roleIDs) if (old == id) { current = true; break; }
    for (auto old: before.catalogRoleIDs)
      if (old == id) { published = true; break; }
    if (current || !published) roles.push(id);
  }
  return roles;
}

struct CatalogEdit {
  Bytes before;
  Bytes after;
};
ZfbStruct(ZumAPI, CatalogEdit,
  (((before), (Ctor<0>)), (Bytes)),
  (((after), (Ctor<1>)), (Bytes)));

struct CatalogRows {
  BytesVec added;
  BytesVec changed;

  template <typename T> void add(const T &after) {
    added.push(SagaImage::save(after));
  }
  template <typename T> void change(const T &before, const T &after) {
    changed.push(SagaImage::save(CatalogEdit{
      SagaImage::save(before), SagaImage::save(after)}));
  }
};
ZfbStruct(ZumAPI, CatalogRows,
  (((added), (Ctor<0>)), (BytesVec)),
  (((changed), (Ctor<1>)), (BytesVec)));

struct CatalogPublish : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"catalogPublish.v2">;
  enum { NSteps = 16 };

  App before;
  App after;
  CatalogRows actions;
  CatalogRows roles;
  CatalogRows scopes;
  IdemRequest request;

  template <typename T> bool valid(const T &item) const {
    return item.appID == before.id && !item.owner &&
      item.origin == Origin::Standard && item.version;
  }

  template <bool Fwd, typename Table, typename Complete>
  void insert(Table *table, const BytesVec &images, Complete complete)
  {
    using T = typename Table::T;
    auto image = &images[saga->iteration()];
    table->run(0, [this, table, image, complete = ZuMv(complete)]() mutable {
      T item;
      if (!SagaImage::load(*image, item) || !valid(item) || item.version != 1) {
	complete(false); return;
      }
      if constexpr (Fwd) {
	ZdbRowRef<T> row = new ZdbRow<T>{table, ZdbShard{0}};
	saga->insert(table, ZuMv(row), ZuMv(complete),
	  [this, item = ZuMv(item)](ZdbRow<T> *row, auto &&complete) mutable {
	    new (row->ptr()) T{ZuMv(item)};
	    row->data().owner = saga->id();
	    complete(row->commit());
	  });
      } else {
	ZuStructKeyT<T, 0> key{ZuStructKey<0>(item)};
	saga->template findDel<0>(table, 0, ZuMv(key), ZuMv(complete),
	  [this](ZdbRow<T> *row, auto &&complete) mutable {
	    if (row->data().owner != saga->id() || row->data().version != 1) {
	      complete(false); return;
	    }
	    complete(row->commit());
	  });
      }
    });
  }

  template <bool Fwd, typename Table, typename Complete>
  void change(Table *table, const BytesVec &images, Complete complete)
  {
    using T = typename Table::T;
    auto image = &images[saga->iteration()];
    table->run(0, [this, table, image, complete = ZuMv(complete)]() mutable {
      CatalogEdit edit;
      T old, next;
      if (!SagaImage::load(*image, edit) ||
	  !SagaImage::load(edit.before, old) || !SagaImage::load(edit.after, next) ||
	  !valid(old) || !valid(next) || old.version == UINT64_MAX ||
	  next.version != old.version + 1 ||
	  ZuStructKey<0>(old) != ZuStructKey<0>(next) ||
	  ZuStructKey<1>(old) != ZuStructKey<1>(next)) {
	complete(false); return;
      }
      ZuStructKeyT<T, 0> key{ZuStructKey<0>(old)};
      saga->template findUpd<0>(table, 0, ZuMv(key), ZuMv(complete),
	[this, old = ZuMv(old), next = ZuMv(next)](
	    ZdbRow<T> *row, auto &&complete) mutable {
	  auto &item = row->data();
	  if (item.owner != (Fwd ? uint128_t{0} : saga->id()) ||
	      item.version != (Fwd ? old.version : next.version)) {
	    complete(false); return;
	  }
	  item = Fwd ? ZuMv(next) : ZuMv(old);
	  item.owner = Fwd ? saga->id() : uint128_t{0};
	  complete(row->commit());
	});
    });
  }

  template <bool Fwd, bool Added, typename Table, typename Complete>
  void release(Table *table, const BytesVec &images, Complete complete)
  {
    using T = typename Table::T;
    auto image = &images[saga->iteration()];
    table->run(0, [this, table, image, complete = ZuMv(complete)]() mutable {
      T next;
      bool loaded;
      if constexpr (Added) loaded = SagaImage::load(*image, next);
      else {
	CatalogEdit edit;
	loaded = SagaImage::load(*image, edit) && SagaImage::load(edit.after, next);
      }
      if (!loaded || !valid(next)) { complete(false); return; }
      ZuStructKeyT<T, 0> key{ZuStructKey<0>(next)};
      saga->template findUpd<0>(table, 0, ZuMv(key), ZuMv(complete),
	[this, version = next.version](ZdbRow<T> *row, auto &&complete) mutable {
	  if (row->data().owner != (Fwd ? saga->id() : uint128_t{0}) ||
	      row->data().version != version) {
	    complete(false); return;
	  }
	  row->data().owner = Fwd ? uint128_t{0} : saga->id();
	  complete(row->commit());
	});
    });
  }

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }

  ZdbSagaStep(1, zum.app, Update) {
    if (before.version == after.version) {
      saga->skip(ZuMv(complete)); return {};
    }
    context->apps->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->apps, 0, ZuFwdTuple(before.id), ZuMv(complete),
	[this](ZdbRow<App> *row, auto &&complete) mutable {
	  auto &app = row->data();
	  if (!before.id || before.owner || after.owner || before.id != after.id ||
	      before.state != State::Active || before.version == UINT64_MAX ||
	      before.authVersion == UINT64_MAX || after.version != before.version + 1 ||
	      after.authVersion != before.authVersion + 1 ||
	      after.catalogRevision < before.catalogRevision ||
	      after.nextActionID < before.nextActionID ||
	      app.owner != (Fwd ? uint128_t{0} : saga->id()) ||
	      app.version != before.version || app.authVersion != before.authVersion) {
	    complete(false); return;
	  }
	  app.owner = Fwd ? saga->id() : uint128_t{0};
	  complete(row->commit());
	});
    });
    return {};
  }

  ZdbSagaRepeatStep(2, zum.action, Insert, actions.added.length()) {
    insert<Fwd>(context->actions, actions.added, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatStep(3, zum.action, Update, actions.changed.length()) {
    change<Fwd>(context->actions, actions.changed, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatStep(4, zum.role, Insert, roles.added.length()) {
    insert<Fwd>(context->roles, roles.added, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatStep(5, zum.role, Update, roles.changed.length()) {
    change<Fwd>(context->roles, roles.changed, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatStep(6, zum.scope, Insert, scopes.added.length()) {
    insert<Fwd>(context->scopes, scopes.added, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatStep(7, zum.scope, Update, scopes.changed.length()) {
    change<Fwd>(context->scopes, scopes.changed, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatStep(8, zum.action, Update, actions.added.length()) {
    release<Fwd, true>(context->actions, actions.added, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatStep(9, zum.action, Update, actions.changed.length()) {
    release<Fwd, false>(context->actions, actions.changed, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatStep(10, zum.role, Update, roles.added.length()) {
    release<Fwd, true>(context->roles, roles.added, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatStep(11, zum.role, Update, roles.changed.length()) {
    release<Fwd, false>(context->roles, roles.changed, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatStep(12, zum.scope, Update, scopes.added.length()) {
    release<Fwd, true>(context->scopes, scopes.added, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatStep(13, zum.scope, Update, scopes.changed.length()) {
    release<Fwd, false>(context->scopes, scopes.changed, ZuMv(complete)); return {};
  }
  ZdbSagaStep(14, zum.app, Update) {
    if (before.version == after.version) {
      saga->skip(ZuMv(complete)); return {};
    }
    context->apps->run(0, [this, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->apps, 0, ZuFwdTuple(before.id), ZuMv(complete),
	[this](ZdbRow<App> *row, auto &&complete) mutable {
	  auto &app = row->data();
	  const auto &expected = Fwd ? before : after;
	  if (app.owner != (Fwd ? saga->id() : uint128_t{0}) ||
	      app.version != expected.version ||
	      app.authVersion != expected.authVersion) { complete(!Fwd); return; }
	  app = Fwd ? after : before;
	  if constexpr (!Fwd) app.owner = saga->id();
	  complete(row->commit());
	});
    });
    return {};
  }
  ZdbSagaStep(15, zum.request, Update) {
    StringVec ids;
    ids.push(String{} << before.id);
    requestComplete(this, ZuMv(ids),
      before.version == after.version ? request.created : after.updated,
      ZuMv(complete));
    return {};
  }

};
ZfbStruct(ZumAPI, CatalogPublish,
  (((before), (Ctor<0>)), (UDT)),
  (((after), (Ctor<1>)), (UDT)),
  (((actions), (Ctor<2>)), (UDT)),
  (((roles), (Ctor<3>)), (UDT)),
  (((scopes), (Ctor<4>)), (UDT)),
  (((request), (Ctor<5>)), (UDT)));

} // namespace Zum

#endif /* zumd_catalog_HH */
