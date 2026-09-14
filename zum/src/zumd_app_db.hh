//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// typed server tables

#ifndef zumd_app_db_HH
#define zumd_app_db_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/Zdb.hh>
#include <zlib/zumd.hh>
#include <zlib/zumd_db_context.hh>

namespace Zum {

ZdbTableDerive(AppTable, App);
ZdbTableDerive(MembershipTable, Membership);
ZdbTableDerive(ActionTable, Action);
ZdbTableDerive(RoleTable, Role);
ZdbTableDerive(ScopeTable, Scope);
ZdbTableDerive(AudienceTable, Audience);
ZdbTableDerive(ClientTable, Client);
ZdbTableDerive(ClientAccessTable, ClientAccess);
ZdbTableDerive(AdminAccessTable, AdminAccess);
ZdbTableDerive(AuthPolicyTable, AuthPolicy);

// Common row effects, executed as steps of the caller's native saga.
template <bool Fwd, bool Insert, typename Def, typename Table, typename Complete>
void recordPut(Def *def, Table *table, Complete complete)
{
  auto apply = [def, table, complete = ZuMv(complete)](bool ok) mutable {
    if (!ok) { complete(false); return; }
    if (bool(def->before.version) == Insert) {
      def->saga->skip(ZuMv(complete)); return;
    }
    table->run(0, [def, table, complete = ZuMv(complete)]() mutable {
      using Record = typename Table::T;
      ZuStructKeyT<Record, 0> key{ZuStructKey<0>(def->before)};
      if constexpr (Insert && Fwd) {
	ZdbRowRef<Record> row = new ZdbRow<Record>{table, ZdbShard{0}};
	def->saga->insert(table, ZuMv(row), ZuMv(complete),
	  [def](ZdbRow<Record> *row, auto &&complete) mutable {
	    new (row->ptr()) Record{def->result()};
	    row->data().owner = def->saga->id();
	    complete(row->commit());
	  });
      } else if constexpr (Insert) {
	def->saga->template findDel<0>(table, 0, key, ZuMv(complete),
	  [](ZdbRow<Record> *row, auto &&complete) mutable { complete(row->commit()); });
      } else {
	def->saga->template findUpd<0>(table, 0, ZuMv(key), ZuMv(complete),
	  [def](ZdbRow<Record> *row, auto &&complete) mutable {
	    if (!row) { def->error = 404; complete(!Fwd); return; }
	    if constexpr (Fwd) {
	      if (auto code = def->recordError(row->data())) {
		def->error = code; complete(false); return;
	      }
	      row->data() = def->result();
	      row->data().owner = def->saga->id();
	    } else row->data() = def->before;
	    complete(row->commit());
	  });
      }
    });
  };
  if constexpr (Fwd && Insert) def->validate(ZuMv(apply));
  else apply(true);
}

template <bool Fwd, typename Def, typename Table, typename Complete>
void recordEdit(Def *def, Table *table, Complete complete)
{
  if constexpr (!Fwd)
    if (def->unchanged()) { def->saga->skip(ZuMv(complete)); return; }
  table->run(0, [def, table, complete = ZuMv(complete)]() mutable {
    ZuStructKeyT<typename Table::T, 0> key{ZuStructKey<0>(def->before)};
    table->template find<0>(0, ZuMv(key),
      [def, table, complete = ZuMv(complete)](ZdbRowRef<typename Table::T> row) mutable {
	if (!row) { def->error = 404; complete(!Fwd); return; }
	if (def->unchanged()) {
	  def->error = def->recordError(row->data());
	  if (def->error) complete(false);
	  else def->saga->skip(ZuMv(complete));
	  return;
	}
	def->saga->update(table, ZuMv(row), ZuMv(complete),
	  [def](ZdbRow<typename Table::T> *row, auto &&complete) mutable {
	    if constexpr (Fwd) {
	      if (auto code = def->recordError(row->data())) {
		def->error = code; complete(false); return;
	      }
	      if (def->stateOnly) row->data().state = def->state;
	      else def->edit(row->data());
	      row->data().version = def->before.version + 1;
	      row->data().updated = def->updated;
	      row->data().owner = def->saga->id();
	    } else row->data() = def->before;
	    complete(row->commit());
	  });
      });
  });
}

template <bool Fwd, typename Def, typename Complete>
void appEditStart(Def *def, Complete complete)
{
  if constexpr (!Fwd)
    if (def->unchanged()) { def->saga->skip(ZuMv(complete)); return; }
  auto table = def->context->apps;
  table->run(0, [def, table, complete = ZuMv(complete)]() mutable {
    table->template find<0>(0, ZuFwdTuple(def->app.id),
      [def, table, complete = ZuMv(complete)](ZdbRowRef<App> row) mutable {
	if (!row) { def->error = 404; complete(!Fwd); return; }
	if (def->unchanged()) {
	  def->error = def->appError(row->data());
	  if (def->error) complete(false);
	  else def->saga->skip(ZuMv(complete));
	  return;
	}
	def->saga->update(table, ZuMv(row), ZuMv(complete),
	  [def](ZdbRow<App> *row, auto &&complete) mutable {
	    if constexpr (Fwd) {
	      if (auto code = def->appError(row->data())) {
		def->error = code; complete(false); return;
	      }
	    }
	    row->data().owner = Fwd ? def->saga->id() : uint128_t{0};
	    complete(row->commit());
	  });
      });
  });
}

template <bool Fwd, typename Def, typename Table, typename Complete>
void appEditRelease(Def *def, Table *table, Complete complete)
{
  if (def->unchanged()) { def->saga->skip(ZuMv(complete)); return; }
  table->run(0, [def, table, complete = ZuMv(complete)]() mutable {
    ZuStructKeyT<typename Table::T, 0> key{ZuStructKey<0>(def->before)};
    def->saga->template findUpd<0>(table, 0,
      ZuMv(key), ZuMv(complete),
      [def](ZdbRow<typename Table::T> *row, auto &&complete) mutable {
	row->data().owner = Fwd ? uint128_t{0} : def->saga->id();
	complete(row->commit());
      });
  });
}

template <bool Fwd, typename Def, typename Complete>
void appEditPublish(Def *def, bool authority, Complete complete)
{
  if (def->unchanged()) { def->saga->skip(ZuMv(complete)); return; }
  auto table = def->context->apps;
  table->run(0, [def, table, authority, complete = ZuMv(complete)]() mutable {
    def->saga->template findUpd<0>(table, 0, ZuFwdTuple(def->app.id),
      ZuMv(complete), [def, authority](ZdbRow<App> *row, auto &&complete) mutable {
	row->data().version = def->app.version + Fwd;
	row->data().authVersion = def->app.authVersion + (Fwd && authority);
	row->data().updated = Fwd ? def->updated : def->app.updated;
	row->data().owner = Fwd ? uint128_t{0} : def->saga->id();
	complete(row->commit());
      });
  });
}

} // namespace Zum

#endif /* zumd_app_db_HH */
