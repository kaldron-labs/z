//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_daemon.hh>
#include <zlib/zumd_db.hh>
#include <zlib/zumd_role_delete.hh>
#include <zlib/zumd_saga_image.hh>

namespace Zum {

struct RoleDeleteItem { String etag; };
ZfStruct(, (RoleDeleteItem, JSON),
  (((etag),		(Required)),	(String)));
struct RoleDeleteReply { RoleDeleteItem item; };
ZfStruct(, (RoleDeleteReply, JSON),
  (((item),		(Required)),	(UDT)));
struct RoleDeleteError { String error; String message; String correlationID; };
ZfStruct(, (RoleDeleteError, JSON),
  (((error),		(Required)),	(String)),
  (((message),		(Required)),	(String)),
  (((correlationID),	(Required)),	(String)));

// The scan is bounded per store request and posts the next page. Before-images
// are revalidated by the saga's ordinary shard-owned mutation continuations.
template <typename Table>
template <typename Heap>
class RoleScan__ : public Heap, public ZmObject  {
  using T = typename Table::T;
  using Tuple = typename Table::Tuple;
  enum { KeyID = ZuIsSame<T, ClientAccess>{} || ZuIsSame<T, RoleMap>{} ? 1 : 0 };
  using Key = typename Table::template Key<KeyID>;

public:
  RoleScan__(Table *table, AppID appID, RoleID roleID,
      BytesVec *images, SagaFn complete) :
    m_table{table}, m_appID{appID}, m_roleID{roleID},
    m_images{images}, m_complete{ZuMv(complete)} { }

  void start()
  {
    m_table->run(0, [self = ZmRef<RoleScan__>{this}]() mutable {
      auto receive = [self](ZuUnion<void, Tuple> result, unsigned count) mutable {
	self->receive_(ZuMv(result), count);
      };
      if constexpr (KeyID)
	self->m_table->template selectRows<KeyID>(
	  ZuFwdTuple(self->m_appID), PageSize, ZuMv(receive));
      else
	self->m_table->template selectRows<KeyID>({}, PageSize, ZuMv(receive));
    });
  }

private:
  // Match the management API's maximum page size; do not monopolize a shard.
  enum { PageSize = AdminQueryLimit::Results };

  void receive_(ZuUnion<void, Tuple> result, unsigned count)
  {
    if (result.template is<Tuple>()) {
      auto tuple = ZuMv(result).template p<Tuple>();
      m_count = count;
      m_key = ZuStructKey<KeyID>(tuple);
      constexpr unsigned app = ZuTypeIndex<ZuStringT<"appID">, ZuFieldIDs<T>>{};
      if (tuple.template p<app>() != m_appID) return;
      bool found = false;
      if constexpr (ZuIsSame<T, RoleMap>{}) {
	constexpr unsigned role = ZuTypeIndex<ZuStringT<"roleID">, ZuFieldIDs<T>>{};
	found = tuple.template p<role>() == m_roleID;
      } else {
	constexpr unsigned roles = ZuTypeIndex<ZuStringT<"roleIDs">, ZuFieldIDs<T>>{};
	for (auto id: tuple.template p<roles>())
	  if (id == m_roleID) { found = true; break; }
      }
      if (!found) return;
      constexpr unsigned owner = ZuTypeIndex<ZuStringT<"owner">, ZuFieldIDs<T>>{};
      if (tuple.template p<owner>()) { m_ok = false; return; }
      m_images->push(SagaImage::save(tuple));
      return;
    }
    if (m_ok && m_count == PageSize) {
      m_count = 0;
      m_table->run(0, [self = ZmRef<RoleScan__>{this}]() mutable {
	self->m_table->template nextRows<KeyID>(self->m_key, false, PageSize,
	  [self = ZuMv(self)](ZuUnion<void, Tuple> result, unsigned count) mutable {
	    self->receive_(ZuMv(result), count);
	  });
      });
      return;
    }
    auto complete = ZuMv(m_complete);
    complete(m_ok);
  }

  Table		*m_table;
  AppID		m_appID;
  RoleID	m_roleID;
  BytesVec	*m_images;
  SagaFn	m_complete;
  Key		m_key;
  unsigned	m_count = 0;
  bool		m_ok = true;
};
using RoleScan_ = RoleScan__<ZmHeap<"Zum.zumd.role.delete.RoleScan", RoleScan__<ZuVoid>>>;

template <typename Heap>
class RoleDelete__ : public Heap, public ZmObject  {
public:
  RoleDelete__(DB *db, DBContext *context, Ztls::Random *rng,
      AppID appID, RoleID roleID, String ifMatch, IdemRequest request,
      AdminDoneFn complete) :
    m_db{db}, m_context{context}, m_rng{rng}, m_appID{appID}, m_roleID{roleID},
    m_ifMatch{ZuMv(ifMatch)}, m_complete{ZuMv(complete)}
    { m_change.request = ZuMv(request); }

  void start()
  {
    if (!m_ifMatch) { finish_(428, "precondition_required"); return; }
    auto apps = m_context->apps;
    apps->run(0, [self = ZmRef<RoleDelete__>{this}, apps]() mutable {
      apps->find<0>(0, ZuFwdTuple(self->m_appID), [self = ZuMv(self)](
	  ZdbRowRef<App> row) mutable {
	if (!row) { self->finish_(404, "not_found"); return; }
	if (row->data().owner || row->data().version == UINT64_MAX ||
	    row->data().authVersion == UINT64_MAX) {
	  self->finish_(409, "conflict"); return;
	}
	self->m_change.app = row->data();
	self->role_();
      });
    });
  }

private:
  void role_()
  {
    auto roles = m_context->roles;
    roles->run(0, [self = ZmRef<RoleDelete__>{this}, roles]() mutable {
      roles->find<0>(0, ZuFwdTuple(self->m_appID, self->m_roleID),
	[self = ZuMv(self)](ZdbRowRef<Role> row) mutable {
	  if (!row) { self->finish_(404, "not_found"); return; }
	  String etag{"\"v"};
	  etag << row->data().version << '"';
	  if (self->m_ifMatch != etag) {
	    self->finish_(412, "precondition_failed"); return;
	  }
	  if (row->data().owner || row->data().tombstone ||
	      row->data().version == UINT64_MAX) {
	    self->finish_(409, "conflict"); return;
	  }
	  self->m_version = row->data().version;
	  self->m_change.role = row->data();
	  self->m_change.updated = Zm::now().sec();
	  self->scan_();
	});
    });
  }

  template <typename Table>
  void scan_(Table *table, BytesVec &images)
  {
    ZmRef<RoleScan_<Table>> scan = new RoleScan_<Table>{
      table, m_appID, m_roleID, &images,
      [self = ZmRef<RoleDelete__>{this}](bool ok) mutable {
	if (!ok) { self->finish_(409, "conflict"); return; }
	++self->m_phase;
	self->scan_();
      }};
    scan->start();
  }

  void scan_()
  {
    switch (m_phase) {
      case 0: scan_(m_context->memberships, m_change.members); break;
      case 1: scan_(m_context->clientAccess, m_change.clients); break;
      case 2: scan_(m_context->adminAccess, m_change.admins); break;
      case 3: scan_(m_context->roleMaps, m_change.maps); break;
      default: submit_(); break;
    }
  }

  void submit_()
  {
    ZdbSagaID id;
    if (!m_rng->random({reinterpret_cast<uint8_t *>(&id), sizeof(id)}) || !id) {
      finish_(503, "unavailable"); return;
    }
    ZmRef<MSaga> saga = new MSaga{};
    saga->init(ZuMv(m_change));
    if (!sagaSubmit(m_db, id, ZuMv(saga),
	[self = ZmRef<RoleDelete__>{this}](bool ok) mutable {
	  if (!ok) self->finish_(503, "unavailable");
	}, [self = ZmRef<RoleDelete__>{this}](bool ok) mutable {
	  self->completed_(ok);
	})) finish_(503, "unavailable");
  }

  void completed_(bool ok)
  {
    if (ok) { finish_(200); return; }
    auto roles = m_context->roles;
    roles->run(0, [self = ZmRef<RoleDelete__>{this}, roles]() mutable {
      roles->find<0>(0, ZuFwdTuple(self->m_appID, self->m_roleID),
	[self = ZuMv(self)](ZdbRowRef<Role> row) mutable {
	  if (row && row->data().version != self->m_version)
	    self->finish_(412, "precondition_failed");
	  else
	    self->finish_(409, "conflict");
	});
    });
  }

  void finish_(unsigned status, ZuCSpan error = {})
  {
    auto complete = ZuMv(m_complete);
    if (!complete) return;
    String json;
    if (status == 200) {
      String etag{"\"v"};
      etag << m_version + 1 << '"';
      ZfJSON::save(json, RoleDeleteReply{{ZuMv(etag)}});
    } else {
      ZuCSpan message;
      switch (status) {
	case 404: message = "role or application not found"; break;
	case 409: message = "role deletion conflicts with current state"; break;
	case 412: message = "ETag mismatch"; break;
	case 428: message = "If-Match is required"; break;
	default: message = "role deletion unavailable"; break;
      }
      ZfJSON::save(json, RoleDeleteError{error, message, {}});
    }
    complete(AdminResult{ZuMv(json), status});
  }

  DB		*m_db;
  DBContext	*m_context;
  Ztls::Random	*m_rng;
  AppID		m_appID;
  RoleID	m_roleID;
  String	m_ifMatch;
  AdminDoneFn	m_complete;
  RoleDelete	m_change;
  uint64_t	m_version = 0;
  unsigned	m_phase = 0;
};
using RoleDelete_ = RoleDelete__<ZmHeap<"Zum.zumd.role.delete.RoleDelete", RoleDelete__<ZuVoid>>>;

void Daemon::roleDelete_(AppID appID, RoleID roleID,
    String ifMatch, IdemRequest request, AdminDoneFn complete)
{
  ZmRef<RoleDelete_> remove = new RoleDelete_{
    m_db, m_context, &m_rng, appID, roleID, ZuMv(ifMatch),
    ZuMv(request), ZuMv(complete)};
  remove->start();
}

} // namespace Zum
