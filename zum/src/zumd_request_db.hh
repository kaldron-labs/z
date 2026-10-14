//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// typed server tables

#ifndef zumd_request_db_HH
#define zumd_request_db_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/Zdb.hh>
#include <zlib/zumd.hh>
#include <zlib/zumd_db.hh>

namespace Zum {

ZdbTableDerive(IdemRequestTable, IdemRequest);

// Shared row effects for request-owning business sagas. These use the caller's
// native saga step/UN and continuation; they do not submit a separate saga.
template <bool Fwd, typename Def, typename Complete>
void requestInsert(Def *def, Complete complete)
{
  if (!def->request.idempotence) {
    def->skip(ZuMv(complete));
    return;
  }
  auto table = def->db()->requests;
  table->run(0, [def, table, complete = ZuMv(complete)]() mutable {
    if constexpr (Fwd) {
      ZdbRowRef<IdemRequest> row =
	new ZdbRow<IdemRequest>{table, ZdbShard{0}};
      def->insert(table, ZuMv(row), ZuMv(complete),
	[def](ZdbRow<IdemRequest> *row, auto &&complete) mutable {
	  new (row->ptr()) IdemRequest{def->request};
	  row->data().owner = def->id();
	  row->data().sagaID = def->id();
	  complete(row->commit());
	});
    } else {
      const auto &request = def->request;
      def->template findDel<0>(table, 0,
	ZuFwdTuple(request.actorKind, request.actorID, request.operation,
	  request.idempotence), ZuMv(complete),
	[](ZdbRow<IdemRequest> *row, auto &&complete) mutable {
	  complete(!row || row->commit());
	});
    }
  });
}

template <typename Def, typename Complete>
void requestComplete(Def *def, StringVec ids, int64_t updated, Complete complete)
{
  if (!def->request.idempotence) {
    def->skip(ZuMv(complete));
    return;
  }
  auto table = def->db()->requests;
  table->run(0, [def, table, ids = ZuMv(ids), updated,
      complete = ZuMv(complete)]() mutable {
    const auto &request = def->request;
    def->template findUpd<0>(table, 0,
      ZuFwdTuple(request.actorKind, request.actorID, request.operation,
	request.idempotence), ZuMv(complete),
      [def, ids = ZuMv(ids), updated](ZdbRow<IdemRequest> *row,
	  auto &&complete) mutable {
	if (!row || row->data().owner != def->id()) {
	  complete(false);
	  return;
	}
	auto &current = row->data();
	current.status = RequestStatus::Complete;
	current.resultIDs = ZuMv(ids);
	current.version = def->request.version + 1;
	current.updated = updated;
	current.owner = 0;
	complete(row->commit());
      });
  });
}

} // namespace Zum

#endif /* zumd_request_db_HH */
