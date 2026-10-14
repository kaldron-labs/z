//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_catalog_publish.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_request_db.hh>

namespace Zum {

template <typename Heap = ZuVoid>
struct CatalogPublishSaga_ : public Heap,
    public ZdbSaga<CatalogPublishSaga_<Heap>, DB> {
  using Base = ZdbSaga<CatalogPublishSaga_<Heap>, DB>;
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
  using Type = ZuStringT<"catalogPublish.v3">;
  enum { NPhases = 20 };

  App before;
  App after;
  CatalogRows actions;
  CatalogRows roles;
  CatalogRows clients;
  CatalogRows clientAccess;
  IdemRequest request;

  template <typename T> bool valid(const T &item) const {
    if constexpr (ZuIsSame<T, Client>{})
      return item.appID == before.id && !item.owner && item.version &&
        item.state != State::Pending;
    else if constexpr (ZuIsSame<T, ClientAccess>{})
      return item.appID == before.id && !item.owner && item.version &&
        item.state != State::Pending;
    else
      return item.appID == before.id && !item.owner &&
        item.origin == Origin::Standard && item.version;
  }

  template <bool Fwd, typename Table, typename Complete>
  void insertRows(Table *table, const BytesVec &images, Complete complete)
  {
    using T = typename Table::T;
    auto image = &images[iteration()];
    table->run(0, [this, table, image, complete = ZuMv(complete)]() mutable {
      T item;
      if (!SagaImage::load(*image, item) || !valid(item) || item.version != 1) {
	complete(false); return;
      }
      if constexpr (Fwd) {
	auto apply = [this, table, complete = ZuMv(complete)](T item, bool ok) mutable {
	  if (!ok) { complete(false); return; }
	  ZdbRowRef<T> row = new ZdbRow<T>{table, ZdbShard{0}};
	  this->insert(
	    table, ZuMv(row), ZuMv(complete),
	    [this, item = ZuMv(item)](ZdbRow<T> *row, auto &&complete) mutable {
	      new (row->ptr()) T{ZuMv(item)};
	      row->data().owner = id();
	      complete(row->commit());
	    });
	};
	if constexpr (ZuIsSame<T, Role>{}) {
	  auto roleID = item.id;
	  auto key = ZuStructKeyT<Role, 1>{ZuStructKey<1>(item)};
	  table->template find<1>(0, ZuMv(key),
	    [this, roleID, item = ZuMv(item), apply = ZuMv(apply)](
		ZdbRowRef<Role> row) mutable {
	      // A later release can have cleared the owner before this
	      // insertion is replayed.  Let the saga's intended UN decide
	      // whether the insertion already committed.
	      bool ok = !row || row->data().id == roleID;
	      apply(ZuMv(item), ok);
	    });
	} else apply(ZuMv(item), true);
      } else {
	ZuStructKeyT<T, 0> key{ZuStructKey<0>(item)};
	this->template findDel<0>(table, 0, ZuMv(key), ZuMv(complete),
	  [this](ZdbRow<T> *row, auto &&complete) mutable {
	    if (!row) { complete(true); return; }
	    if (row->data().owner != id() || row->data().version != 1) {
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
    auto image = &images[iteration()];
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
      this->template findUpd<0>(table, 0, ZuMv(key), ZuMv(complete),
	[this, old = ZuMv(old), next = ZuMv(next)](
	    ZdbRow<T> *row, auto &&complete) mutable {
	  if (!row) { complete(!Fwd); return; }
	  auto &item = row->data();
	  if (item.owner != (Fwd ? uint128_t{0} : id()) ||
	      item.version != (Fwd ? old.version : next.version)) {
	    complete(false); return;
	  }
	  item = Fwd ? ZuMv(next) : ZuMv(old);
	  item.owner = Fwd ? id() : uint128_t{0};
	  complete(row->commit());
	});
    });
  }

  template <bool Fwd, bool Added, typename Table, typename Complete>
  void release(Table *table, const BytesVec &images, Complete complete)
  {
    using T = typename Table::T;
    auto image = &images[iteration()];
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
      this->template findUpd<0>(table, 0, ZuMv(key), ZuMv(complete),
	[this, version = next.version](ZdbRow<T> *row, auto &&complete) mutable {
	  if (!row) { complete(!Fwd); return; }
	  if (row->data().owner != (Fwd ? id() : uint128_t{0}) ||
	      row->data().version != version) {
	    complete(false); return;
	  }
	  row->data().owner = Fwd ? uint128_t{0} : id();
	  complete(row->commit());
	});
    });
  }

  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }

  ZdbSagaPhase(1, zum.app, Update) {
    if (before.version == after.version) {
      skip(ZuMv(complete)); return {};
    }
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(db()->apps, 0, ZuFwdTuple(before.id), ZuMv(complete),
	[this](ZdbRow<App> *row, auto &&complete) mutable {
	  if (!row) { complete(!Fwd); return; }
	  auto &app = row->data();
	  if (!before.id || before.owner || after.owner || before.id != after.id ||
	      before.state != State::Active || before.version == UINT64_MAX ||
	      before.authVersion == UINT64_MAX || after.version != before.version + 1 ||
	      after.authVersion != before.authVersion + 1 ||
	      after.catalogRevision < before.catalogRevision ||
	      after.nextActionID < before.nextActionID ||
	      app.owner != (Fwd ? uint128_t{0} : id()) ||
	      app.version != before.version || app.authVersion != before.authVersion) {
	    complete(false); return;
	  }
	  app.owner = Fwd ? id() : uint128_t{0};
	  complete(row->commit());
	});
    });
    return {};
  }

  ZdbSagaRepeatPhase(2, zum.action, Insert, actions.added.length()) {
    insertRows<Fwd>(db()->actions, actions.added, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatPhase(3, zum.action, Update, actions.changed.length()) {
    change<Fwd>(db()->actions, actions.changed, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatPhase(4, zum.role, Insert, roles.added.length()) {
    insertRows<Fwd>(db()->roles, roles.added, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatPhase(5, zum.role, Update, roles.changed.length()) {
    change<Fwd>(db()->roles, roles.changed, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatPhase(6, zum.client, Insert, clients.added.length()) {
    insertRows<Fwd>(db()->clients, clients.added, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatPhase(7, zum.client, Update, clients.changed.length()) {
    change<Fwd>(db()->clients, clients.changed, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatPhase(8, zum.client_access, Insert, clientAccess.added.length()) {
    insertRows<Fwd>(db()->clientAccess, clientAccess.added, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatPhase(9, zum.client_access, Update, clientAccess.changed.length()) {
    change<Fwd>(db()->clientAccess, clientAccess.changed, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatPhase(10, zum.action, Update, actions.added.length()) {
    release<Fwd, true>(db()->actions, actions.added, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatPhase(11, zum.action, Update, actions.changed.length()) {
    release<Fwd, false>(db()->actions, actions.changed, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatPhase(12, zum.role, Update, roles.added.length()) {
    release<Fwd, true>(db()->roles, roles.added, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatPhase(13, zum.role, Update, roles.changed.length()) {
    release<Fwd, false>(db()->roles, roles.changed, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatPhase(14, zum.client, Update, clients.added.length()) {
    release<Fwd, true>(db()->clients, clients.added, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatPhase(15, zum.client, Update, clients.changed.length()) {
    release<Fwd, false>(db()->clients, clients.changed, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatPhase(16, zum.client_access, Update, clientAccess.added.length()) {
    release<Fwd, true>(db()->clientAccess, clientAccess.added, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatPhase(17, zum.client_access, Update, clientAccess.changed.length()) {
    release<Fwd, false>(db()->clientAccess, clientAccess.changed, ZuMv(complete)); return {};
  }
  ZdbSagaPhase(18, zum.app, Update) {
    if (before.version == after.version) {
      skip(ZuMv(complete)); return {};
    }
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(db()->apps, 0, ZuFwdTuple(before.id), ZuMv(complete),
	[this](ZdbRow<App> *row, auto &&complete) mutable {
	  if (!row) { complete(!Fwd); return; }
	  auto &app = row->data();
	  const auto &expected = Fwd ? before : after;
	  if (app.owner != (Fwd ? id() : uint128_t{0}) ||
	      app.version != expected.version ||
	      app.authVersion != expected.authVersion) { complete(!Fwd); return; }
	  app = Fwd ? after : before;
	  if constexpr (!Fwd) app.owner = id();
	  complete(row->commit());
	});
    });
    return {};
  }
  ZdbSagaPhase(19, zum.request, Update) {
    StringVec ids;
    ids.push(String{} << before.id);
    requestComplete(this, ZuMv(ids),
      before.version == after.version ? request.created : after.updated,
      ZuMv(complete));
    return {};
  }


  CatalogPublishSaga_() = default;
  CatalogPublishSaga_(App before_, App after_, CatalogRows actions_,
      CatalogRows roles_, CatalogRows clients_, CatalogRows clientAccess_,
      IdemRequest request_) : before{ZuMv(before_)}, after{ZuMv(after_)},
    actions{ZuMv(actions_)}, roles{ZuMv(roles_)}, clients{ZuMv(clients_)},
    clientAccess{ZuMv(clientAccess_)}, request{ZuMv(request_)} { }
  CatalogPublishSaga_(CatalogPublishArgs args) :
    CatalogPublishSaga_{ZuMv(args.before), ZuMv(args.after),
      ZuMv(args.actions), ZuMv(args.roles), ZuMv(args.clients),
      ZuMv(args.clientAccess), ZuMv(args.request)} { }
  ~CatalogPublishSaga_() override;
};

using CatalogPublishSaga = CatalogPublishSaga_<
  ZmHeap<"Zum.CatalogPublishSaga", CatalogPublishSaga_<>>>;

ZfStruct(, (CatalogPublishSaga, FB),
  (before,		(Ctor<0>),	UDT),
  (after,		(Ctor<1>),	UDT),
  (actions,		(Ctor<2>),	UDT),
  (roles,		(Ctor<3>),	UDT),
  (clients,		(Ctor<4>),	UDT),
  (clientAccess,	(Ctor<5>),	UDT),
  (request,		(Ctor<6>),	UDT));
ZiFB::AsTable<"Zum.fbs.CatalogPublish"> ZiFB_Fmt(CatalogPublishSaga *);

ZuAssert((Zdb_::SagaDefValid<CatalogPublishSaga>{}));

template <typename Heap>
CatalogPublishSaga_<Heap>::~CatalogPublishSaga_() = default;

SagaRef catalogPublish(ZuBSpan data) {
  return CatalogPublishSaga::load(data);
}

Zdb_::SagaDef catalogPublishDef() {
  return CatalogPublishSaga::def();
}
SagaRef catalogPublish(CatalogPublishArgs args) {
  return new CatalogPublishSaga{ZuMv(args)};
}

} // namespace Zum
