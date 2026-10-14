//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zumd_saga_grant_delete_HH
#define zumd_saga_grant_delete_HH

#include <zlib/zumd_identity_db.hh>

namespace Zum {

template <bool Fwd, typename Def, typename Complete>
void grantDelete(Def *def, Bytes id, Complete complete)
{
  auto table = def->db()->grants;
  table->run(0, [def, table, id = ZuMv(id), complete = ZuMv(complete)]() mutable {
    if constexpr (Fwd) {
      def->template findDel<0>(table, 0, ZuFwdTuple(ZuMv(id)), ZuMv(complete),
	[def](ZdbRow<Grant> *row, auto &&complete) mutable {
	  if (!row || row->data().owner != def->id() ||
	      row->data().state != State::Consumed) { complete(false); return; }
	  complete(row->commit());
	});
    } else {
      ZdbRowRef<Grant> row = new ZdbRow<Grant>{table, ZdbShard{0}};
      def->insert(table, ZuMv(row), ZuMv(complete),
	[def](ZdbRow<Grant> *row, auto &&complete) mutable {
	  new (row->ptr()) Grant{def->beforeGrant};
	  row->data().state = State::Consumed;
	  row->data().owner = def->id();
	  complete(row->commit());
	});
    }
  });
}

} // namespace Zum

#endif /* zumd_saga_grant_delete_HH */
