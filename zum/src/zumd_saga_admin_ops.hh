//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zumd_saga_admin_ops_HH
#define zumd_saga_admin_ops_HH

#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_admin.hh>

namespace Zum {

ZumAPI bool clientConfigValid(ClientProfile::T, uint8_t, bool, const StringVec &);

static unsigned appVersionError(const App &app, const App &item)
{
  return !app.version || app.owner || item.owner || item.state != State::Active ||
    item.version != app.version || item.authVersion != app.authVersion ||
    item.updated != app.updated || app.version == UINT64_MAX ||
    app.authVersion == UINT64_MAX ? 409 : 0;
}

template <typename Add>
static unsigned catalogAddError(const Add &def)
{
  if (!def.values.id || def.values.id == UINT64_MAX || def.values.id != def.before.id ||
      !def.values.appID || def.values.appID != def.app.id ||
      def.values.appID != def.before.appID || !def.values.name) return 400;
  return def.before.version ? 409 : 0;
}

template <typename Edit, typename Record>
static unsigned putError(const Edit &edit, const Record &item)
{
  if (edit.ifNoneMatch) return 412;
  if (!edit.ifMatch) return 428;
  String etag{"\"v"};
  etag << item.version << '"';
  if (edit.ifMatch != etag || item.version != edit.before.version) return 412;
  return edit.before.owner || item.owner || edit.before.version == UINT64_MAX ? 409 : 0;
}

template <typename IDs>
static bool uniqueRefs(const IDs &ids, bool nonzero = true)
{
  unsigned n = ids.length();
  for (unsigned i = 0; i < n; ++i) {
    auto &id = ids[i];
    if (nonzero && !id) return false;
    for (unsigned j = 0; j < i; ++j) if (id == ids[j]) return false;
  }
  return true;
}

template <typename Edit>
static void accessRefs(Edit *def, unsigned offset, Zdb_::SagaCompleteFn complete)
{
  const auto &ids = def->values.roleIDs;
  if (offset == ids.length()) { complete(true); return; }
  auto roles = def->db()->roles;
  auto id = ids[offset];
  roles->run(0, [def, roles, id, offset, complete = ZuMv(complete)]() mutable {
    roles->template find<0>(0, ZuFwdTuple(def->values.appID, id),
      [def, offset, complete = ZuMv(complete)](ZdbRowRef<Role> row) mutable {
        if (!row || row->data().owner || row->data().state != State::Active) {
          def->setError(400); complete(false); return;
        }
        accessRefs(def, offset + 1, ZuMv(complete));
      });
  });
}

template <typename Edit>
static auto accessResult(const Edit &def)
{
  auto item = def.values;
  item.state = State::Active;
  item.version = def.before.version + 1;
  item.created = def.before.version ? def.before.created : def.updated;
  item.updated = def.updated;
  item.owner = 0;
  if constexpr (ZuIsSame<decltype(item), ClientAccess>{})
    item.authVersion = def.before.version ? def.before.authVersion + 1 : 1;
  return item;
}

template <typename Edit, typename Record>
static unsigned accessStateError(const Edit &edit, const Record &item)
{
  if (edit.state < 0 || edit.state >= State::N ||
      edit.state == State::Pending || edit.state == State::Consumed) return 400;
  String etag{"\"v"};
  etag << item.version << '"';
  if (edit.ifMatch != etag || item.version != edit.before.version) return 412;
  if (!edit.before.version || edit.before.owner || item.owner || item.state == State::Revoked ||
      (!edit.unchanged() && edit.before.version == UINT64_MAX)) return 409;
  if constexpr (ZuIsSame<Record, ClientAccess>{})
    if (item.authVersion != edit.before.authVersion ||
        (!edit.unchanged() && edit.before.authVersion == UINT64_MAX)) return 409;
  return 0;
}

} // namespace Zum

#endif /* zumd_saga_admin_ops_HH */
