//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumAdmin.hh>

#include <string.h>

#include <zlib/ZuBase64URL.hh>


#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsSec.hh>

namespace Zum {

class AdminComplete_ : public ZumObject {
public:
  AdminComplete_(AdminFn complete) : m_complete{ZuMv(complete)} { }

  void request(ZmRef<Request> request) { m_request = ZuMv(request); }

  void complete(int error)
  {
    m_request->complete([
      self = ZmRef<AdminComplete_>{this}, error
    ]() mutable {
      auto complete = ZuMv(self->m_complete);
      complete(error);
    });
  }

  void cancel()
  {
    auto complete = ZuMv(m_complete);
    complete(AdminError::Storage);
  }

private:
  ZmRef<Request>	m_request;
  AdminFn	m_complete;
};

class ActionComplete_ : public ZumObject {
public:
  ActionComplete_(ActionFn complete) : m_complete{ZuMv(complete)} { }

  void request(ZmRef<Request> request) { m_request = ZuMv(request); }

  void complete(int error, ActionID id)
  {
    m_request->complete([
      self = ZmRef<ActionComplete_>{this}, error, id
    ]() mutable {
      auto complete = ZuMv(self->m_complete);
      complete(error, id);
    });
  }

  void cancel()
  {
    auto complete = ZuMv(m_complete);
    complete(AdminError::Storage, 0);
  }

private:
  ZmRef<Request>	m_request;
  ActionFn	m_complete;
};

class CleanupComplete_ : public ZumObject {
public:
  CleanupComplete_(CleanupFn complete) : m_complete{ZuMv(complete)} { }

  void request(ZmRef<Request> request) { m_request = ZuMv(request); }

  void complete(int error, unsigned removed)
  {
    m_request->complete([
      self = ZmRef<CleanupComplete_>{this}, error, removed
    ]() mutable {
      auto complete = ZuMv(self->m_complete);
      complete(error, removed);
    });
  }

  void cancel()
  {
    auto complete = ZuMv(m_complete);
    complete(AdminError::Storage, 0);
  }

private:
  ZmRef<Request>	m_request;
  CleanupFn	m_complete;
};

void auditWrite(DBContext *context, Audit audit, AdminFn complete)
{
  if (!context || !audit.issuer || audit.time <= 0) {
    complete(AdminError::Invalid);
    return;
  }
  auto issuers = context->issuers;
  auto audits = context->audits;
  String issuer = audit.issuer;
  issuers->run(0, [
    issuers, audits, issuer = ZuMv(issuer), audit = ZuMv(audit),
    complete = ZuMv(complete)
  ]() mutable {
    issuers->findUpd<0>(0, ZuFwdTuple(ZuMv(issuer)), [
      audits, audit = ZuMv(audit), complete = ZuMv(complete)
    ](ZdbRow<Issuer> *issuer) mutable {
      if (!issuer) {
	complete(AdminError::Invalid);
	return;
      }
      audit.id = issuer->data().nextAuditID++;
      if (!issuer->commit()) {
	complete(AdminError::Storage);
	return;
      }
      audits->run(0, [
	audits, audit = ZuMv(audit), complete = ZuMv(complete)
      ]() mutable {
	ZdbRowRef<Audit> row = new ZdbRow<Audit>{audits, ZdbShard{0}};
	audits->insert(ZuMv(row), [
	  audit = ZuMv(audit), complete = ZuMv(complete)
	](ZdbRow<Audit> *row) mutable {
	  if (!row) {
	    complete(AdminError::Storage);
	    return;
	  }
	  new (row->ptr()) Audit{ZuMv(audit)};
	  complete(row->commit() ? AdminError::OK : AdminError::Storage);
	});
      });
    });
  });
}

String auditID(ZuBSpan id)
{
  String target;
  target.length(ZuBase64URL::enclen(id.length()));
  target.length(ZuBase64URL::encode(target.span(), id));
  return target;
}

Audit managementAuditRecord(
    String issuer, int operation, String actor, AppID appID, String target,
    String correlationID, unsigned status, int64_t now)
{
  return Audit{
    .time = now,
    .issuer = ZuMv(issuer),
    .appID = appID,
    .operationID = ActionID(operation),
    .actor = ZuMv(actor),
    .target = ZuMv(target),
    .event = AuditEvent::Administration,
    .outcome = AuditOutcome::T(status >= 200 && status < 300 ?
      AuditOutcome::Success : AuditOutcome::Failure),
    .correlationID = ZuMv(correlationID),
    .detail = MgmtOp::name(operation)
  };
}

static void actionAdd_(
    DBContext *context, String issuer, String actor, String name,
    int64_t now, ActionFn complete)
{
  if (!context || !issuer || !actor || !name || now <= 0) {
    complete(AdminError::Invalid, ActionID{});
    return;
  }
  String target = name;
  actionCreate(context, issuer, ZuMv(name), [
    context, issuer = ZuMv(issuer), actor = ZuMv(actor),
    target = ZuMv(target), now, complete = ZuMv(complete)
  ](bool ok, ActionID id) mutable {
    if (!ok) {
      complete(AdminError::Invalid, ActionID{});
      return;
    }
    auditWrite(context, Audit{
      .time = now,
      .issuer = ZuMv(issuer),
      .actor = ZuMv(actor),
      .target = ZuMv(target),
      .event = AuditEvent::RBACChange,
      .outcome = AuditOutcome::Success,
      .detail = "action added"
    }, [id, complete = ZuMv(complete)](int error) mutable {
      complete(error, id);
    });
  });
}

class ActionChange_ : public ZumPolymorph {
public:
  ActionChange_(
      DB *db, DBContext *context, Ztls::Random *rng, String issuer,
      String actor, ActionID actionID, State::T state, int64_t now,
      AdminFn complete) :
    m_db{db}, m_context{context}, m_rng{rng}, m_issuer{ZuMv(issuer)},
    m_actor{ZuMv(actor)}, m_actionID{actionID}, m_state{state}, m_now{now},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_db || !m_context || !m_rng || !m_issuer || !m_actor ||
	m_now <= 0 || (m_state != State::Active &&
	 m_state != State::Disabled && m_state != State::Revoked)) {
      finish_(AdminError::Invalid);
      return;
    }
    uint8_t id[sizeof(m_sagaID)];
    if (!m_rng->random(id)) {
      finish_(AdminError::Storage);
      return;
    }
    memcpy(&m_sagaID, id, sizeof(m_sagaID));
    String issuer = m_issuer;
    m_context->issuers->run(0, [
      self = ZmRef<ActionChange_>{this}, issuer = ZuMv(issuer)
    ]() mutable {
      self->m_context->issuers->find<0>(0, ZuFwdTuple(ZuMv(issuer)), [
	self = ZuMv(self)
      ](ZdbRowRef<Issuer> row) mutable { self->issuer_(ZuMv(row)); });
    });
  }

private:
  void finish_(int error)
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    complete(error);
  }

  void issuer_(ZdbRowRef<Issuer> issuer)
  {
    if (!issuer || m_actionID >= issuer->data().nextActionID) {
      finish_(AdminError::Invalid);
      return;
    }
    m_authVersion = issuer->data().authVersion;
    m_context->actions->find<0>(0, ZuFwdTuple(AppID{0}, m_actionID), [
      self = ZmRef<ActionChange_>{this}
    ](ZdbRowRef<Action> row) mutable { self->action_(ZuMv(row)); });
  }

  void action_(ZdbRowRef<Action> row)
  {
    if (!row || row->data().owner ||
	(row->data().state == State::Revoked && m_state != State::Revoked)) {
      finish_(AdminError::Invalid);
      return;
    }
    if (row->data().state == m_state) {
      finish_(AdminError::OK);
      return;
    }
    m_target = row->data().name;
    ActionChange change{
      .issuer = m_issuer,
      .actionID = m_actionID,
      .name = row->data().name,
      .authVersion = m_authVersion,
      .oldState = row->data().state,
      .newState = m_state
    };
    ZmRef<MSaga> saga = new MSaga{};
    saga->init(ZuMv(change));
    if (!sagaSubmit(m_db, m_sagaID, ZuMv(saga),
      SagaFn{ZmRef<ActionChange_>{this}, ZmFnPtr<&ActionChange_::sagaSubmit_>{}},
      SagaFn{ZmRef<ActionChange_>{this}, ZmFnPtr<&ActionChange_::saga_>{}}))
      sagaSubmit_(false);
  }

  void sagaSubmit_(bool ok) { if (!ok) finish_(AdminError::Storage); }

  void saga_(bool ok)
  {
    if (!ok) {
      finish_(AdminError::Storage);
      return;
    }
    auditWrite(m_context, Audit{
      .time = m_now,
      .issuer = ZuMv(m_issuer),
      .actor = ZuMv(m_actor),
      .target = ZuMv(m_target),
      .event = AuditEvent::RBACChange,
      .outcome = AuditOutcome::Success,
      .detail = "action state"
    }, [self = ZmRef<ActionChange_>{this}](int error) mutable {
      self->finish_(error);
    });
  }

  DB		*m_db = nullptr;
  DBContext	*m_context = nullptr;
  Ztls::Random	*m_rng = nullptr;
  String	m_issuer;
  String	m_actor;
  ActionID	m_actionID = 0;
  State::T	m_state = State::Pending;
  int64_t	m_now = 0;
  AdminFn	m_complete;
  String	m_target;
  uint64_t	m_authVersion = 0;
  ZdbSagaID	m_sagaID = 0;
  bool		m_done = false;
};

static void actionState_(
    DB *db, DBContext *context, Ztls::Random &rng, String issuer,
    String actor, ActionID actionID, State::T state, int64_t now,
    AdminFn complete)
{
  ZmRef<ActionChange_> change = new ActionChange_{db, context, &rng,
    ZuMv(issuer), ZuMv(actor), actionID, state, now, ZuMv(complete)};
  change->start();
}

static void signKeyAdd_(
    DBContext *context, String issuer, String actor, SignKey key,
    int64_t now, AdminFn complete)
{
  if (!context || !issuer || !actor || !key.id || !key.providerRef ||
      !key.publicJwk || key.notBefore <= 0 ||
      (key.retireAfter && key.retireAfter <= key.notBefore) ||
      key.state != State::Active || now <= 0) {
    complete(AdminError::Invalid);
    return;
  }
  String target = key.id;
  auto keys = context->signKeys;
  keys->run(0, [
    context, keys, issuer = ZuMv(issuer), actor = ZuMv(actor),
    target = ZuMv(target), key = ZuMv(key), now,
    complete = ZuMv(complete)
  ]() mutable {
    ZdbRowRef<SignKey> row = new ZdbRow<SignKey>{keys, ZdbShard{0}};
    keys->insert(ZuMv(row), [
      context, issuer = ZuMv(issuer), actor = ZuMv(actor),
      target = ZuMv(target), key = ZuMv(key), now,
      complete = ZuMv(complete)
    ](ZdbRow<SignKey> *row) mutable {
      if (!row) {
	complete(AdminError::Invalid);
	return;
      }
      new (row->ptr()) SignKey{ZuMv(key)};
      if (!row->commit()) {
	complete(AdminError::Storage);
	return;
      }
      auditWrite(context, Audit{
	.time = now,
	.issuer = ZuMv(issuer),
	.actor = ZuMv(actor),
	.target = ZuMv(target),
	.event = AuditEvent::KeyRotation,
	.outcome = AuditOutcome::Success,
	.detail = "added"
      }, ZuMv(complete));
    });
  });
}

static void signKeyRetire_(
    DBContext *context, String issuer, String actor, String id,
    int64_t retireAfter, int64_t now, AdminFn complete)
{
  if (!context || !issuer || !actor || !id || now <= 0 ||
      retireAfter <= now) {
    complete(AdminError::Invalid);
    return;
  }
  auto keys = context->signKeys;
  keys->run(0, [
    context, keys, issuer = ZuMv(issuer), actor = ZuMv(actor),
    id = ZuMv(id), retireAfter, now, complete = ZuMv(complete)
  ]() mutable {
    String lookup = id;
    keys->findUpd<0, ZuSeq<1>>(0, ZuFwdTuple(ZuMv(lookup)), [
      context, issuer = ZuMv(issuer), actor = ZuMv(actor), id = ZuMv(id),
      retireAfter, now, complete = ZuMv(complete)
    ](ZdbRow<SignKey> *row) mutable {
      if (!row || row->data().state != State::Active ||
	  (row->data().retireAfter &&
	   row->data().retireAfter > retireAfter)) {
	complete(AdminError::Invalid);
	return;
      }
      if (row->data().retireAfter == retireAfter) {
	complete(AdminError::OK);
	return;
      }
      row->data().retireAfter = retireAfter;
      if (!row->commit()) {
	complete(AdminError::Storage);
	return;
      }
      auditWrite(context, Audit{
	.time = now,
	.issuer = ZuMv(issuer),
	.actor = ZuMv(actor),
	.target = ZuMv(id),
	.event = AuditEvent::KeyRotation,
	.outcome = AuditOutcome::Success,
	.detail = "retiring"
      }, ZuMv(complete));
    });
  });
}

class UserChange_ : public ZumPolymorph {
public:
  UserChange_(
      DB *db, DBContext *context, Ztls::Random *rng, String issuer,
      String actor, UserID userID, IDVec roleIDs, State::T state,
      bool roles, int64_t now, AdminFn complete) :
    m_db{db}, m_context{context}, m_rng{rng}, m_issuer{ZuMv(issuer)},
    m_actor{ZuMv(actor)}, m_userID{userID}, m_roleIDs{ZuMv(roleIDs)},
    m_state{state}, m_roles{roles}, m_now{now},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_db || !m_context || !m_rng || !m_issuer || !m_actor ||
	!m_userID || m_now <= 0 || (!m_roles &&
	 m_state != State::Active && m_state != State::Suspended &&
	 m_state != State::Disabled && m_state != State::Revoked)) {
      finish_(AdminError::Invalid);
      return;
    }
    uint8_t id[sizeof(m_sagaID)];
    if (!m_rng->random(id)) {
      finish_(AdminError::Storage);
      return;
    }
    memcpy(&m_sagaID, id, sizeof(m_sagaID));
    String issuer = m_issuer;
    m_context->issuers->run(0, [
      self = ZmRef<UserChange_>{this}, issuer = ZuMv(issuer)
    ]() mutable {
      self->m_context->issuers->find<0>(0, ZuFwdTuple(ZuMv(issuer)), [
	self = ZuMv(self)
      ](ZdbRowRef<Issuer> row) mutable { self->issuer_(ZuMv(row)); });
    });
  }

private:
  void finish_(int error)
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    complete(error);
  }

  void issuer_(ZdbRowRef<Issuer> issuer)
  {
    if (!issuer) {
      finish_(AdminError::Invalid);
      return;
    }
    m_authVersion = issuer->data().authVersion;
    if (m_roles) {
      role_();
      return;
    }
    loadUser_();
  }

  void role_()
  {
    if (m_roleIndex >= m_roleIDs.length()) {
      loadUser_();
      return;
    }
    auto id = m_roleIDs[m_roleIndex];
    m_context->roles->find<0>(0, ZuFwdTuple(AppID{0}, id), [
      self = ZmRef<UserChange_>{this}
    ](ZdbRowRef<Role> row) mutable {
      if (!row || row->data().owner) {
	self->finish_(AdminError::Invalid);
	return;
      }
      ++self->m_roleIndex;
      self->role_();
    });
  }

  void loadUser_()
  {
    m_context->users->find<0>(0, ZuFwdTuple(m_userID), [
      self = ZmRef<UserChange_>{this}
    ](ZdbRowRef<User> row) mutable { self->user_(ZuMv(row)); });
  }

  void user_(ZdbRowRef<User> row)
  {
    if (!row || row->data().owner) {
      finish_(AdminError::Invalid);
      return;
    }
    auto &user = row->data();
    IDVec roleIDs = m_roles ? ZuMv(m_roleIDs) : user.roleIDs;
    State::T state = m_roles ? user.state : m_state;
    if (roleIDs == user.roleIDs && state == user.state) {
      finish_(AdminError::OK);
      return;
    }
    m_subject = auditID(user.handle);
    UserChange change{
      .issuer = m_issuer,
      .userID = m_userID,
      .oldRoleIDs = user.roleIDs,
      .newRoleIDs = ZuMv(roleIDs),
      .oldUpdated = user.updated,
      .updated = m_now,
      .authVersion = m_authVersion,
      .oldState = user.state,
      .newState = state
    };
    ZmRef<MSaga> saga = new MSaga{};
    saga->init(ZuMv(change));
    if (!sagaSubmit(m_db, m_sagaID, ZuMv(saga),
      SagaFn{ZmRef<UserChange_>{this}, ZmFnPtr<&UserChange_::sagaSubmit_>{}},
      SagaFn{ZmRef<UserChange_>{this}, ZmFnPtr<&UserChange_::saga_>{}}))
      sagaSubmit_(false);
  }

  void sagaSubmit_(bool ok) { if (!ok) finish_(AdminError::Storage); }

  void saga_(bool ok)
  {
    if (!ok) {
      finish_(AdminError::Storage);
      return;
    }
    auditWrite(m_context, Audit{
      .time = m_now,
      .issuer = ZuMv(m_issuer),
      .actor = ZuMv(m_actor),
      .subject = ZuMv(m_subject),
      .event = AuditEvent::T(
	m_roles ? AuditEvent::RBACChange : AuditEvent::PrincipalChange),
      .outcome = AuditOutcome::Success,
      .detail = m_roles ? String{"roles"} : String{"state"}
    }, [self = ZmRef<UserChange_>{this}](int error) mutable {
      self->finish_(error);
    });
  }

  DB		*m_db = nullptr;
  DBContext	*m_context = nullptr;
  Ztls::Random	*m_rng = nullptr;
  String	m_issuer;
  String	m_actor;
  UserID	m_userID = 0;
  IDVec		m_roleIDs;
  State::T	m_state = State::Pending;
  bool		m_roles = false;
  int64_t	m_now = 0;
  AdminFn	m_complete;
  String	m_subject;
  uint64_t	m_authVersion = 0;
  unsigned	m_roleIndex = 0;
  ZdbSagaID	m_sagaID = 0;
  bool		m_done = false;
};

static void userRoles_(
    DB *db, DBContext *context, Ztls::Random &rng, String issuer,
    String actor, UserID userID, IDVec roleIDs, int64_t now,
    AdminFn complete)
{
  ZmRef<UserChange_> change = new UserChange_{db, context, &rng,
    ZuMv(issuer), ZuMv(actor), userID, ZuMv(roleIDs), State::Pending,
    true, now, ZuMv(complete)};
  change->start();
}

static void userState_(
    DB *db, DBContext *context, Ztls::Random &rng, String issuer,
    String actor, UserID userID, State::T state, int64_t now,
    AdminFn complete)
{
  ZmRef<UserChange_> change = new UserChange_{db, context, &rng,
    ZuMv(issuer), ZuMv(actor), userID, {}, state, false, now,
    ZuMv(complete)};
  change->start();
}

class RoleChange_ : public ZumPolymorph {
public:
  RoleChange_(
      DB *db, DBContext *context, Ztls::Random *rng, String issuer,
      String actor, RoleID roleID, ZtBitmap actions, State::T state,
      bool actionChange, int64_t now, AdminFn complete) :
    m_db{db}, m_context{context}, m_rng{rng}, m_issuer{ZuMv(issuer)},
    m_actor{ZuMv(actor)}, m_roleID{roleID}, m_actions{ZuMv(actions)},
    m_state{state}, m_actionChange{actionChange}, m_now{now},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_db || !m_context || !m_rng || !m_issuer || !m_actor ||
	!m_roleID || m_now <= 0 || (!m_actionChange &&
	 m_state != State::Active && m_state != State::Suspended &&
	 m_state != State::Disabled && m_state != State::Revoked)) {
      finish_(AdminError::Invalid);
      return;
    }
    uint8_t id[sizeof(m_sagaID)];
    if (!m_rng->random(id)) {
      finish_(AdminError::Storage);
      return;
    }
    memcpy(&m_sagaID, id, sizeof(m_sagaID));
    String issuer = m_issuer;
    m_context->issuers->run(0, [
      self = ZmRef<RoleChange_>{this}, issuer = ZuMv(issuer)
    ]() mutable {
      self->m_context->issuers->find<0>(0, ZuFwdTuple(ZuMv(issuer)), [
	self = ZuMv(self)
      ](ZdbRowRef<Issuer> row) mutable { self->issuer_(ZuMv(row)); });
    });
  }

private:
  void finish_(int error)
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    complete(error);
  }

  void issuer_(ZdbRowRef<Issuer> issuer)
  {
    if (!issuer) {
      finish_(AdminError::Invalid);
      return;
    }
    m_authVersion = issuer->data().authVersion;
    m_nextActionID = issuer->data().nextActionID;
    if (!m_actionChange) {
      role_();
      return;
    }
    m_action = m_actions.first();
    action_();
  }

  void action_()
  {
    if (m_action < 0) {
      role_();
      return;
    }
    if (unsigned(m_action) >= m_nextActionID) {
      finish_(AdminError::Invalid);
      return;
    }
    auto id = ActionID(m_action);
    m_context->actions->find<0>(0, ZuFwdTuple(AppID{0}, id), [
      self = ZmRef<RoleChange_>{this}
    ](ZdbRowRef<Action> row) mutable {
      if (!row) {
	self->finish_(AdminError::Invalid);
	return;
      }
      self->m_action = self->m_actions.next(self->m_action);
      self->action_();
    });
  }

  void role_()
  {
    m_context->roles->find<0>(0, ZuFwdTuple(AppID{0}, m_roleID), [
      self = ZmRef<RoleChange_>{this}
    ](ZdbRowRef<Role> row) mutable {
      if (!row || row->data().owner) {
	self->finish_(AdminError::Invalid);
	return;
      }
      self->submit_(row->data());
    });
  }

  void submit_(const Role &role)
  {
    ZtBitmap actions = m_actionChange ? ZuMv(m_actions) : role.actions;
    State::T state = m_actionChange ? role.state : m_state;
    if (actions == role.actions && state == role.state) {
      finish_(AdminError::OK);
      return;
    }
    m_target = role.name;
    RoleChange change{
      .issuer = m_issuer,
      .roleID = m_roleID,
      .name = role.name,
      .oldActions = role.actions,
      .newActions = ZuMv(actions),
      .authVersion = m_authVersion,
      .oldState = role.state,
      .newState = state
    };
    ZmRef<MSaga> saga = new MSaga{};
    saga->init(ZuMv(change));
    if (!sagaSubmit(m_db, m_sagaID, ZuMv(saga),
      SagaFn{ZmRef<RoleChange_>{this}, ZmFnPtr<&RoleChange_::sagaSubmit_>{}},
      SagaFn{ZmRef<RoleChange_>{this}, ZmFnPtr<&RoleChange_::saga_>{}}))
      sagaSubmit_(false);
  }

  void sagaSubmit_(bool ok) { if (!ok) finish_(AdminError::Storage); }

  void saga_(bool ok)
  {
    if (!ok) {
      finish_(AdminError::Storage);
      return;
    }
    auditWrite(m_context, Audit{
      .time = m_now,
      .issuer = ZuMv(m_issuer),
      .actor = ZuMv(m_actor),
      .target = ZuMv(m_target),
      .event = AuditEvent::RBACChange,
      .outcome = AuditOutcome::Success,
      .detail = m_actionChange ? String{"role actions"} : String{"role state"}
    }, [self = ZmRef<RoleChange_>{this}](int error) mutable {
      self->finish_(error);
    });
  }

  DB		*m_db = nullptr;
  DBContext	*m_context = nullptr;
  Ztls::Random	*m_rng = nullptr;
  String	m_issuer;
  String	m_actor;
  RoleID	m_roleID = 0;
  ZtBitmap	m_actions;
  State::T	m_state = State::Pending;
  bool		m_actionChange = false;
  int64_t	m_now = 0;
  AdminFn	m_complete;
  String	m_target;
  uint64_t	m_authVersion = 0;
  ActionID	m_nextActionID = 0;
  int		m_action = -1;
  ZdbSagaID	m_sagaID = 0;
  bool		m_done = false;
};

static void roleActions_(
    DB *db, DBContext *context, Ztls::Random &rng, String issuer,
    String actor, RoleID roleID, ZtBitmap actions, int64_t now,
    AdminFn complete)
{
  ZmRef<RoleChange_> change = new RoleChange_{db, context, &rng,
    ZuMv(issuer), ZuMv(actor), roleID, ZuMv(actions), State::Pending,
    true, now, ZuMv(complete)};
  change->start();
}

static void roleState_(
    DB *db, DBContext *context, Ztls::Random &rng, String issuer,
    String actor, RoleID roleID, State::T state, int64_t now,
    AdminFn complete)
{
  ZmRef<RoleChange_> change = new RoleChange_{db, context, &rng,
    ZuMv(issuer), ZuMv(actor), roleID, {}, state, false,
    now, ZuMv(complete)};
  change->start();
}

class CredChange_ : public ZumPolymorph {
public:
  CredChange_(
      DB *db, DBContext *context, Ztls::Random *rng, String issuer,
      String actor, Bytes credentialID, State::T state, int64_t now,
      AdminFn complete) :
    m_db{db}, m_context{context}, m_rng{rng}, m_issuer{ZuMv(issuer)},
    m_actor{ZuMv(actor)}, m_credentialID{ZuMv(credentialID)},
    m_state{state}, m_now{now}, m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_db || !m_context || !m_rng || !m_issuer || !m_actor ||
	!m_credentialID || m_now <= 0 ||
	(m_state != State::Active && m_state != State::Disabled &&
	 m_state != State::Revoked)) {
      finish_(AdminError::Invalid);
      return;
    }
    uint8_t id[sizeof(m_sagaID)];
    if (!m_rng->random(id)) {
      finish_(AdminError::Storage);
      return;
    }
    memcpy(&m_sagaID, id, sizeof(m_sagaID));
    String issuer = m_issuer;
    m_context->issuers->run(0, [
      self = ZmRef<CredChange_>{this}, issuer = ZuMv(issuer)
    ]() mutable {
      self->m_context->issuers->find<0>(0, ZuFwdTuple(ZuMv(issuer)), [
	self = ZuMv(self)
      ](ZdbRowRef<Issuer> row) mutable { self->issuer_(ZuMv(row)); });
    });
  }

private:
  void finish_(int error)
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    complete(error);
  }

  void issuer_(ZdbRowRef<Issuer> issuer)
  {
    if (!issuer) {
      finish_(AdminError::Invalid);
      return;
    }
    m_authVersion = issuer->data().authVersion;
    Bytes id = m_credentialID;
    m_context->creds->find<0>(0, ZuFwdTuple(ZuMv(id)), [
      self = ZmRef<CredChange_>{this}
    ](ZdbRowRef<Cred> row) mutable { self->cred_(ZuMv(row)); });
  }

  void cred_(ZdbRowRef<Cred> row)
  {
    if (!row || row->data().owner ||
	(row->data().state != State::Active &&
	 row->data().state != State::Disabled &&
	 row->data().state != State::Revoked) ||
	(row->data().state == State::Revoked && m_state != State::Revoked)) {
      finish_(AdminError::Invalid);
      return;
    }
    if (row->data().state == m_state) {
      finish_(AdminError::OK);
      return;
    }
    CredChange change{
      .issuer = m_issuer,
      .credentialID = m_credentialID,
      .oldUpdated = row->data().updated,
      .updated = m_now,
      .authVersion = m_authVersion,
      .oldState = row->data().state,
      .newState = m_state
    };
    ZmRef<MSaga> saga = new MSaga{};
    saga->init(ZuMv(change));
    if (!sagaSubmit(m_db, m_sagaID, ZuMv(saga),
      SagaFn{ZmRef<CredChange_>{this}, ZmFnPtr<&CredChange_::sagaSubmit_>{}},
      SagaFn{ZmRef<CredChange_>{this}, ZmFnPtr<&CredChange_::saga_>{}}))
      sagaSubmit_(false);
  }

  void sagaSubmit_(bool ok) { if (!ok) finish_(AdminError::Storage); }

  void saga_(bool ok)
  {
    if (!ok) {
      finish_(AdminError::Storage);
      return;
    }
    auditWrite(m_context, Audit{
      .time = m_now,
      .issuer = ZuMv(m_issuer),
      .actor = ZuMv(m_actor),
      .target = auditID(m_credentialID),
      .event = AuditEvent::CredentialChange,
      .outcome = AuditOutcome::Success,
      .detail = "state"
    }, [self = ZmRef<CredChange_>{this}](int error) mutable {
      self->finish_(error);
    });
  }

  DB		*m_db = nullptr;
  DBContext	*m_context = nullptr;
  Ztls::Random	*m_rng = nullptr;
  String	m_issuer;
  String	m_actor;
  Bytes		m_credentialID;
  State::T	m_state = State::Pending;
  int64_t	m_now = 0;
  AdminFn	m_complete;
  uint64_t	m_authVersion = 0;
  ZdbSagaID	m_sagaID = 0;
  bool		m_done = false;
};

static void credentialState_(
    DB *db, DBContext *context, Ztls::Random &rng, String issuer,
    String actor, Bytes credentialID, State::T state, int64_t now,
    AdminFn complete)
{
  ZmRef<CredChange_> change = new CredChange_{db, context, &rng,
    ZuMv(issuer), ZuMv(actor), ZuMv(credentialID), state,
    now, ZuMv(complete)};
  change->start();
}

class ScopeChange_ : public ZumPolymorph {
public:
  ScopeChange_(
      DB *db, DBContext *context, Ztls::Random *rng, String issuer,
      String actor, ScopeID scopeID, IDVec roleIDs, State::T state,
      bool roles, int64_t now, AdminFn complete) :
    m_db{db}, m_context{context}, m_rng{rng}, m_issuer{ZuMv(issuer)},
    m_actor{ZuMv(actor)}, m_scopeID{scopeID}, m_roleIDs{ZuMv(roleIDs)},
    m_state{state}, m_roles{roles}, m_now{now},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_db || !m_context || !m_rng || !m_issuer || !m_actor ||
	!m_scopeID || m_now <= 0 || (!m_roles &&
	 m_state != State::Active && m_state != State::Disabled &&
	 m_state != State::Revoked)) {
      finish_(AdminError::Invalid);
      return;
    }
    uint8_t id[sizeof(m_sagaID)];
    if (!m_rng->random(id)) {
      finish_(AdminError::Storage);
      return;
    }
    memcpy(&m_sagaID, id, sizeof(m_sagaID));
    String issuer = m_issuer;
    m_context->issuers->run(0, [
      self = ZmRef<ScopeChange_>{this}, issuer = ZuMv(issuer)
    ]() mutable {
      self->m_context->issuers->find<0>(0, ZuFwdTuple(ZuMv(issuer)), [
	self = ZuMv(self)
      ](ZdbRowRef<Issuer> row) mutable { self->issuer_(ZuMv(row)); });
    });
  }

private:
  void finish_(int error)
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    complete(error);
  }

  void issuer_(ZdbRowRef<Issuer> issuer)
  {
    if (!issuer) {
      finish_(AdminError::Invalid);
      return;
    }
    m_authVersion = issuer->data().authVersion;
    if (m_roles) role_(); else scope_();
  }

  void role_()
  {
    if (m_roleIndex >= m_roleIDs.length()) {
      scope_();
      return;
    }
    auto id = m_roleIDs[m_roleIndex];
    m_context->roles->find<0>(0, ZuFwdTuple(AppID{0}, id), [
      self = ZmRef<ScopeChange_>{this}
    ](ZdbRowRef<Role> row) mutable {
      if (!row || row->data().owner) {
	self->finish_(AdminError::Invalid);
	return;
      }
      ++self->m_roleIndex;
      self->role_();
    });
  }

  void scope_()
  {
    m_context->scopes->find<0>(0, ZuFwdTuple(AppID{0}, m_scopeID), [
      self = ZmRef<ScopeChange_>{this}
    ](ZdbRowRef<Scope> row) mutable {
      if (!row || row->data().owner) {
	self->finish_(AdminError::Invalid);
	return;
      }
      self->submit_(row->data());
    });
  }

  void submit_(const Scope &scope)
  {
    IDVec roleIDs = m_roles ? ZuMv(m_roleIDs) : scope.roleIDs;
    State::T state = m_roles ? scope.state : m_state;
    if (roleIDs == scope.roleIDs && state == scope.state) {
      finish_(AdminError::OK);
      return;
    }
    m_target = scope.name;
    ScopeChange change{
      .issuer = m_issuer,
      .scopeID = m_scopeID,
      .audience = scope.audience,
      .name = scope.name,
      .oldRoleIDs = scope.roleIDs,
      .newRoleIDs = ZuMv(roleIDs),
      .authVersion = m_authVersion,
      .oldState = scope.state,
      .newState = state
    };
    ZmRef<MSaga> saga = new MSaga{};
    saga->init(ZuMv(change));
    if (!sagaSubmit(m_db, m_sagaID, ZuMv(saga),
      SagaFn{ZmRef<ScopeChange_>{this}, ZmFnPtr<&ScopeChange_::sagaSubmit_>{}},
      SagaFn{ZmRef<ScopeChange_>{this}, ZmFnPtr<&ScopeChange_::saga_>{}}))
      sagaSubmit_(false);
  }

  void sagaSubmit_(bool ok) { if (!ok) finish_(AdminError::Storage); }

  void saga_(bool ok)
  {
    if (!ok) {
      finish_(AdminError::Storage);
      return;
    }
    auditWrite(m_context, Audit{
      .time = m_now,
      .issuer = ZuMv(m_issuer),
      .actor = ZuMv(m_actor),
      .target = ZuMv(m_target),
      .event = AuditEvent::RBACChange,
      .outcome = AuditOutcome::Success,
      .detail = m_roles ? String{"scope roles"} : String{"scope state"}
    }, [self = ZmRef<ScopeChange_>{this}](int error) mutable {
      self->finish_(error);
    });
  }

  DB		*m_db = nullptr;
  DBContext	*m_context = nullptr;
  Ztls::Random	*m_rng = nullptr;
  String	m_issuer;
  String	m_actor;
  ScopeID	m_scopeID = 0;
  IDVec		m_roleIDs;
  State::T	m_state = State::Pending;
  bool		m_roles = false;
  int64_t	m_now = 0;
  AdminFn	m_complete;
  String	m_target;
  uint64_t	m_authVersion = 0;
  unsigned	m_roleIndex = 0;
  ZdbSagaID	m_sagaID = 0;
  bool		m_done = false;
};

static void scopeRoles_(
    DB *db, DBContext *context, Ztls::Random &rng, String issuer,
    String actor, ScopeID scopeID, IDVec roleIDs, int64_t now,
    AdminFn complete)
{
  ZmRef<ScopeChange_> change = new ScopeChange_{db, context, &rng,
    ZuMv(issuer), ZuMv(actor), scopeID, ZuMv(roleIDs), State::Pending,
    true, now, ZuMv(complete)};
  change->start();
}

static void scopeState_(
    DB *db, DBContext *context, Ztls::Random &rng, String issuer,
    String actor, ScopeID scopeID, State::T state, int64_t now,
    AdminFn complete)
{
  ZmRef<ScopeChange_> change = new ScopeChange_{db, context, &rng,
    ZuMv(issuer), ZuMv(actor), scopeID, {}, state, false,
    now, ZuMv(complete)};
  change->start();
}

class ClientChange_ : public ZumPolymorph {
public:
  enum Change { RolesChange, StateChange, SecretChange };

  ClientChange_(
      DB *db, DBContext *context, Ztls::Random *rng, String issuer,
      String actor, String clientID, IDVec roleIDs, Bytes secretDigest,
      State::T state, Change change, int64_t now, AdminFn complete) :
    m_db{db}, m_context{context}, m_rng{rng}, m_issuer{ZuMv(issuer)},
    m_actor{ZuMv(actor)}, m_clientID{ZuMv(clientID)},
    m_roleIDs{ZuMv(roleIDs)}, m_secretDigest{ZuMv(secretDigest)},
    m_state{state}, m_change{change}, m_now{now},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_db || !m_context || !m_rng || !m_issuer || !m_actor ||
	!m_clientID || m_now <= 0 ||
	(m_change == StateChange && m_state != Zum::State::Active &&
	 m_state != Zum::State::Disabled && m_state != Zum::State::Revoked) ||
	(m_change == SecretChange &&
	 (m_secretDigest.length() != Ztls::SecretHash::Size ||
	  m_secretDigest[0] != Ztls::SecretHash::Version))) {
      finish_(AdminError::Invalid);
      return;
    }
    uint8_t id[sizeof(m_sagaID)];
    if (!m_rng->random(id)) {
      finish_(AdminError::Storage);
      return;
    }
    memcpy(&m_sagaID, id, sizeof(m_sagaID));
    String issuer = m_issuer;
    m_context->issuers->run(0, [
      self = ZmRef<ClientChange_>{this}, issuer = ZuMv(issuer)
    ]() mutable {
      self->m_context->issuers->find<0>(0, ZuFwdTuple(ZuMv(issuer)), [
	self = ZuMv(self)
      ](ZdbRowRef<Issuer> row) mutable { self->issuer_(ZuMv(row)); });
    });
  }

private:
  void finish_(int error)
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    complete(error);
  }

  void issuer_(ZdbRowRef<Issuer> issuer)
  {
    if (!issuer) {
      finish_(AdminError::Invalid);
      return;
    }
    m_authVersion = issuer->data().authVersion;
    if (m_change == RolesChange) role_(); else client_();
  }

  void role_()
  {
    if (m_roleIndex >= m_roleIDs.length()) {
      client_();
      return;
    }
    auto id = m_roleIDs[m_roleIndex];
    m_context->roles->find<0>(0, ZuFwdTuple(AppID{0}, id), [
      self = ZmRef<ClientChange_>{this}
    ](ZdbRowRef<Role> row) mutable {
      if (!row || row->data().owner) {
	self->finish_(AdminError::Invalid);
	return;
      }
      ++self->m_roleIndex;
      self->role_();
    });
  }

  void client_()
  {
    String id = m_clientID;
    m_context->clients->find<0>(0, ZuFwdTuple(ZuMv(id)), [
      self = ZmRef<ClientChange_>{this}
    ](ZdbRowRef<Client> row) mutable {
      if (!row || row->data().owner ||
	  (self->m_change == SecretChange &&
	   row->data().type != ClientType::Confidential) ||
	  (self->m_change == StateChange &&
	   row->data().state == State::Revoked &&
	   self->m_state != State::Revoked)) {
	self->finish_(AdminError::Invalid);
	return;
      }
      self->submit_(row->data());
    });
  }

  void submit_(const Client &client)
  {
    Bytes secret = m_change == SecretChange ?
      ZuMv(m_secretDigest) : client.secretDigest;
    IDVec roles = m_change == RolesChange ? ZuMv(m_roleIDs) : client.roleIDs;
    State::T state = m_change == StateChange ? m_state : client.state;
    if (secret == client.secretDigest && roles == client.roleIDs &&
	state == client.state) {
      finish_(AdminError::OK);
      return;
    }
    ClientChange change{
      .issuer = m_issuer,
      .clientID = m_clientID,
      .oldSecretDigest = client.secretDigest,
      .newSecretDigest = ZuMv(secret),
      .oldRoleIDs = client.roleIDs,
      .newRoleIDs = ZuMv(roles),
      .oldUpdated = client.updated,
      .updated = m_now,
      .authVersion = m_authVersion,
      .oldState = client.state,
      .newState = state
    };
    ZmRef<MSaga> saga = new MSaga{};
    saga->init(ZuMv(change));
    if (!sagaSubmit(m_db, m_sagaID, ZuMv(saga),
      SagaFn{ZmRef<ClientChange_>{this}, ZmFnPtr<&ClientChange_::sagaSubmit_>{}},
      SagaFn{ZmRef<ClientChange_>{this}, ZmFnPtr<&ClientChange_::saga_>{}}))
      sagaSubmit_(false);
  }

  void sagaSubmit_(bool ok) { if (!ok) finish_(AdminError::Storage); }

  void saga_(bool ok)
  {
    if (!ok) {
      finish_(AdminError::Storage);
      return;
    }
    AuditEvent::T event = m_change == RolesChange ?
      AuditEvent::RBACChange : AuditEvent::PrincipalChange;
    String detail;
    switch (m_change) {
      case RolesChange: detail = "client roles"; break;
      case StateChange: detail = "client state"; break;
      case SecretChange: detail = "client secret"; break;
    }
    auditWrite(m_context, Audit{
      .time = m_now,
      .issuer = ZuMv(m_issuer),
      .actor = ZuMv(m_actor),
      .target = ZuMv(m_clientID),
      .event = event,
      .outcome = AuditOutcome::Success,
      .detail = ZuMv(detail)
    }, [self = ZmRef<ClientChange_>{this}](int error) mutable {
      self->finish_(error);
    });
  }

  DB		*m_db = nullptr;
  DBContext	*m_context = nullptr;
  Ztls::Random	*m_rng = nullptr;
  String	m_issuer;
  String	m_actor;
  String	m_clientID;
  IDVec		m_roleIDs;
  Bytes		m_secretDigest;
  State::T	m_state = State::Pending;
  Change	m_change = RolesChange;
  int64_t	m_now = 0;
  AdminFn	m_complete;
  uint64_t	m_authVersion = 0;
  unsigned	m_roleIndex = 0;
  ZdbSagaID	m_sagaID = 0;
  bool		m_done = false;
};

static void clientRoles_(
    DB *db, DBContext *context, Ztls::Random &rng, String issuer,
    String actor, String clientID, IDVec roleIDs, int64_t now,
    AdminFn complete)
{
  ZmRef<ClientChange_> change = new ClientChange_{db, context, &rng,
    ZuMv(issuer), ZuMv(actor), ZuMv(clientID), ZuMv(roleIDs), {},
    State::Pending, ClientChange_::RolesChange, now, ZuMv(complete)};
  change->start();
}

static void clientState_(
    DB *db, DBContext *context, Ztls::Random &rng, String issuer,
    String actor, String clientID, State::T state, int64_t now,
    AdminFn complete)
{
  ZmRef<ClientChange_> change = new ClientChange_{db, context, &rng,
    ZuMv(issuer), ZuMv(actor), ZuMv(clientID), {}, {}, state,
    ClientChange_::StateChange, now, ZuMv(complete)};
  change->start();
}

static void clientSecretDigest_(
    DB *db, DBContext *context, Ztls::Random &rng, String issuer,
    String actor, String clientID, Bytes secretDigest, int64_t now,
    AdminFn complete)
{
  ZmRef<ClientChange_> change = new ClientChange_{db, context, &rng,
    ZuMv(issuer), ZuMv(actor), ZuMv(clientID), {}, ZuMv(secretDigest),
    State::Pending, ClientChange_::SecretChange, now, ZuMv(complete)};
  change->start();
}

class GrantCleanup_ : public ZumPolymorph {
public:
  GrantCleanup_(
      DBContext *context, int64_t now, unsigned limit, CleanupFn complete) :
    m_context{context}, m_now{now}, m_limit{limit},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_context || m_now <= 0 || !m_limit) {
      finish_(AdminError::Invalid);
      return;
    }
    using Table = GrantTable;
    using Tuple = Table::Tuple;
    m_context->grants->selectRows<1>({}, m_limit, [
      self = ZmRef<GrantCleanup_>{this}
    ](ZuUnion<void, Tuple> result, unsigned) mutable {
      if (result.template is<Tuple>()) {
	auto row = ZuMv(result).template p<Tuple>();
	if (row.template p<21>() <= self->m_now &&
	    !row.template p<26>())
	  self->m_ids.push(Bytes{row.template p<0>()});
	return;
      }
      self->remove_();
    });
  }

private:
  void finish_(int error)
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    complete(error, m_removed);
  }

  void remove_()
  {
    if (m_index >= m_ids.length()) {
      finish_(AdminError::OK);
      return;
    }
    Bytes id = m_ids[m_index++];
    auto grants = m_context->grants;
    grants->run(0, [
      self = ZmRef<GrantCleanup_>{this}, grants, id = ZuMv(id)
    ]() mutable {
      grants->findDel<0>(0, ZuFwdTuple(ZuMv(id)), [self = ZuMv(self)](
	  ZdbRow<Grant> *row) mutable {
	if (!row || row->data().owner || row->data().expires > self->m_now) {
	  self->remove_();
	  return;
	}
	if (!row->commit()) {
	  self->finish_(AdminError::Storage);
	  return;
	}
	++self->m_removed;
	self->remove_();
      });
    });
  }

  DBContext	*m_context = nullptr;
  int64_t	m_now = 0;
  unsigned	m_limit = 0;
  CleanupFn	m_complete;
  BytesVec	m_ids;
  unsigned	m_index = 0;
  unsigned	m_removed = 0;
  bool		m_done = false;
};

static void grantCleanup_(
    DBContext *context, int64_t now, unsigned limit, CleanupFn complete)
{
  ZmRef<GrantCleanup_> cleanup =
    new GrantCleanup_{context, now, limit, ZuMv(complete)};
  cleanup->start();
}

class AuditCleanup_ : public ZumPolymorph {
public:
  AuditCleanup_(
      DBContext *context, int64_t before, unsigned limit,
      CleanupFn complete) :
    m_context{context}, m_before{before}, m_limit{limit},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_context || m_before <= 0 || !m_limit) {
      finish_(AdminError::Invalid);
      return;
    }
    using Table = AuditTable;
    using Tuple = Table::Tuple;
    m_context->audits->selectRows<1>({}, m_limit, [
      self = ZmRef<AuditCleanup_>{this}
    ](ZuUnion<void, Tuple> result, unsigned) mutable {
      if (result.template is<Tuple>()) {
	auto row = ZuMv(result).template p<Tuple>();
	if (row.template p<3>() <= self->m_before)
	  self->m_keys.push(Key{
	    row.template p<1>(), String{row.template p<0>()}});
	return;
      }
      self->remove_();
    });
  }

private:
  struct Key {
    uint64_t	id = 0;
    String	issuer;
  };

  void finish_(int error)
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    complete(error, m_removed);
  }

  void remove_()
  {
    if (m_index >= m_keys.length()) {
      finish_(AdminError::OK);
      return;
    }
    auto key = ZuMv(m_keys[m_index++]);
    auto audits = m_context->audits;
    audits->run(0, [
      self = ZmRef<AuditCleanup_>{this}, audits, key = ZuMv(key)
    ]() mutable {
      audits->findDel<0>(0,
	ZuFwdTuple(ZuMv(key.issuer), key.id), [self = ZuMv(self)](
	  ZdbRow<Audit> *row) mutable {
	if (!row || row->data().time > self->m_before) {
	  self->remove_();
	  return;
	}
	if (!row->commit()) {
	  self->finish_(AdminError::Storage);
	  return;
	}
	++self->m_removed;
	self->remove_();
      });
    });
  }

  DBContext	*m_context = nullptr;
  int64_t	m_before = 0;
  unsigned	m_limit = 0;
  CleanupFn	m_complete;
  ZtArray<Key, VecHeap> m_keys;
  unsigned	m_index = 0;
  unsigned	m_removed = 0;
  bool		m_done = false;
};

static void auditCleanup_(
    DBContext *context, int64_t before, unsigned limit, CleanupFn complete)
{
  ZmRef<AuditCleanup_> cleanup =
    new AuditCleanup_{context, before, limit, ZuMv(complete)};
  cleanup->start();
}

static void grantRevoke_(
    DBContext *context, String issuer, Bytes id, String actor,
    int64_t now, AdminFn complete)
{
  if (!context || !issuer || !id || !actor || now <= 0) {
    complete(AdminError::Invalid);
    return;
  }
  auto grants = context->grants;
  grants->run(0, [
    context, grants, issuer = ZuMv(issuer), id = ZuMv(id),
    actor = ZuMv(actor), now,
    complete = ZuMv(complete)
  ]() mutable {
    grants->findUpd<0>(0, ZuFwdTuple(ZuMv(id)), [
      context, issuer = ZuMv(issuer), actor = ZuMv(actor), now,
      complete = ZuMv(complete)
    ](ZdbRow<Grant> *row) mutable {
      if (!row || row->data().issuer != issuer || row->data().owner ||
	  row->data().state == State::Consumed) {
	complete(AdminError::Invalid);
	return;
      }
      if (row->data().state == State::Revoked) {
	complete(AdminError::OK);
	return;
      }
      row->data().state = State::Revoked;
      if (!row->commit()) {
	complete(AdminError::Storage);
	return;
      }
      auditWrite(context, Audit{
	.time = now,
	.issuer = ZuMv(issuer),
	.actor = ZuMv(actor),
	.target = auditID(row->data().id),
	.event = AuditEvent::Revocation,
	.outcome = AuditOutcome::Success
      }, ZuMv(complete));
    });
  });
}

bool actionAdd(
    Requests *requests, ZuTime deadline, DBContext *context,
    String issuer, String actor, String name, int64_t now,
    ActionFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<ActionComplete_> state = new ActionComplete_{ZuMv(complete)};
  return requests->run(deadline, [
    state, context, issuer = ZuMv(issuer), actor = ZuMv(actor),
    name = ZuMv(name), now
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    actionAdd_(context, ZuMv(issuer), ZuMv(actor), ZuMv(name), now, [state](
	int error, ActionID id) mutable { state->complete(error, id); });
  }, [state]() mutable { state->cancel(); });
}

bool actionState(
    Requests *requests, ZuTime deadline, DB *db, DBContext *context,
    Ztls::Random &rng, String issuer, String actor, ActionID id,
    State::T value, int64_t now, AdminFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<AdminComplete_> state = new AdminComplete_{ZuMv(complete)};
  return requests->run(deadline, [
    state, db, context, &rng, issuer = ZuMv(issuer), actor = ZuMv(actor),
    id, value, now
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    actionState_(db, context, rng, ZuMv(issuer), ZuMv(actor), id, value,
      now, [state](int error) mutable { state->complete(error); });
  }, [state]() mutable { state->cancel(); });
}

bool grantRevoke(
    Requests *requests, ZuTime deadline, DBContext *context,
    String issuer, Bytes id, String actor, int64_t now, AdminFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<AdminComplete_> state = new AdminComplete_{ZuMv(complete)};
  return requests->run(deadline, [
    state, context, issuer = ZuMv(issuer), id = ZuMv(id),
    actor = ZuMv(actor), now
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    grantRevoke_(context, ZuMv(issuer), ZuMv(id), ZuMv(actor), now,
      [state](int error) mutable { state->complete(error); });
  }, [state]() mutable { state->cancel(); });
}

bool signKeyAdd(
    Requests *requests, ZuTime deadline, DBContext *context,
    String issuer, String actor, SignKey key, int64_t now,
    AdminFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<AdminComplete_> state = new AdminComplete_{ZuMv(complete)};
  return requests->run(deadline, [
    state, context, issuer = ZuMv(issuer), actor = ZuMv(actor),
    key = ZuMv(key), now
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    signKeyAdd_(context, ZuMv(issuer), ZuMv(actor), ZuMv(key), now,
      [state](int error) mutable { state->complete(error); });
  }, [state]() mutable { state->cancel(); });
}

bool signKeyRetire(
    Requests *requests, ZuTime deadline, DBContext *context,
    String issuer, String actor, String id, int64_t retireAfter,
    int64_t now, AdminFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<AdminComplete_> state = new AdminComplete_{ZuMv(complete)};
  return requests->run(deadline, [
    state, context, issuer = ZuMv(issuer), actor = ZuMv(actor),
    id = ZuMv(id), retireAfter, now
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    signKeyRetire_(context, ZuMv(issuer), ZuMv(actor), ZuMv(id),
      retireAfter, now,
      [state](int error) mutable { state->complete(error); });
  }, [state]() mutable { state->cancel(); });
}

bool userRoles(
    Requests *requests, ZuTime deadline, DB *db, DBContext *context,
    Ztls::Random &rng, String issuer, String actor, UserID id,
    IDVec roles, int64_t now, AdminFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<AdminComplete_> state = new AdminComplete_{ZuMv(complete)};
  return requests->run(deadline, [
    state, db, context, &rng, issuer = ZuMv(issuer), actor = ZuMv(actor),
    id, roles = ZuMv(roles), now
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    userRoles_(db, context, rng, ZuMv(issuer), ZuMv(actor), id,
      ZuMv(roles), now,
      [state](int error) mutable { state->complete(error); });
  }, [state]() mutable { state->cancel(); });
}

bool userState(
    Requests *requests, ZuTime deadline, DB *db, DBContext *context,
    Ztls::Random &rng, String issuer, String actor, UserID id,
    State::T value, int64_t now, AdminFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<AdminComplete_> state = new AdminComplete_{ZuMv(complete)};
  return requests->run(deadline, [
    state, db, context, &rng, issuer = ZuMv(issuer), actor = ZuMv(actor),
    id, value, now
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    userState_(db, context, rng, ZuMv(issuer), ZuMv(actor), id, value, now,
      [state](int error) mutable { state->complete(error); });
  }, [state]() mutable { state->cancel(); });
}

bool roleActions(
    Requests *requests, ZuTime deadline, DB *db, DBContext *context,
    Ztls::Random &rng, String issuer, String actor, RoleID id,
    ZtBitmap actions, int64_t now, AdminFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<AdminComplete_> state = new AdminComplete_{ZuMv(complete)};
  return requests->run(deadline, [
    state, db, context, &rng, issuer = ZuMv(issuer), actor = ZuMv(actor),
    id, actions = ZuMv(actions), now
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    roleActions_(db, context, rng, ZuMv(issuer), ZuMv(actor), id,
      ZuMv(actions), now,
      [state](int error) mutable { state->complete(error); });
  }, [state]() mutable { state->cancel(); });
}

bool roleState(
    Requests *requests, ZuTime deadline, DB *db, DBContext *context,
    Ztls::Random &rng, String issuer, String actor, RoleID id,
    State::T value, int64_t now, AdminFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<AdminComplete_> state = new AdminComplete_{ZuMv(complete)};
  return requests->run(deadline, [
    state, db, context, &rng, issuer = ZuMv(issuer), actor = ZuMv(actor),
    id, value, now
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    roleState_(db, context, rng, ZuMv(issuer), ZuMv(actor), id, value, now,
      [state](int error) mutable { state->complete(error); });
  }, [state]() mutable { state->cancel(); });
}

bool credentialState(
    Requests *requests, ZuTime deadline, DB *db, DBContext *context,
    Ztls::Random &rng, String issuer, String actor, Bytes id,
    State::T value, int64_t now, AdminFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<AdminComplete_> state = new AdminComplete_{ZuMv(complete)};
  return requests->run(deadline, [
    state, db, context, &rng, issuer = ZuMv(issuer), actor = ZuMv(actor),
    id = ZuMv(id), value, now
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    credentialState_(db, context, rng, ZuMv(issuer), ZuMv(actor),
      ZuMv(id), value, now,
      [state](int error) mutable { state->complete(error); });
  }, [state]() mutable { state->cancel(); });
}

bool scopeRoles(
    Requests *requests, ZuTime deadline, DB *db, DBContext *context,
    Ztls::Random &rng, String issuer, String actor, ScopeID id,
    IDVec roles, int64_t now, AdminFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<AdminComplete_> state = new AdminComplete_{ZuMv(complete)};
  return requests->run(deadline, [
    state, db, context, &rng, issuer = ZuMv(issuer), actor = ZuMv(actor),
    id, roles = ZuMv(roles), now
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    scopeRoles_(db, context, rng, ZuMv(issuer), ZuMv(actor), id,
      ZuMv(roles), now,
      [state](int error) mutable { state->complete(error); });
  }, [state]() mutable { state->cancel(); });
}

bool scopeState(
    Requests *requests, ZuTime deadline, DB *db, DBContext *context,
    Ztls::Random &rng, String issuer, String actor, ScopeID id,
    State::T value, int64_t now, AdminFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<AdminComplete_> state = new AdminComplete_{ZuMv(complete)};
  return requests->run(deadline, [
    state, db, context, &rng, issuer = ZuMv(issuer), actor = ZuMv(actor),
    id, value, now
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    scopeState_(db, context, rng, ZuMv(issuer), ZuMv(actor), id, value,
      now, [state](int error) mutable { state->complete(error); });
  }, [state]() mutable { state->cancel(); });
}

bool clientRoles(
    Requests *requests, ZuTime deadline, DB *db, DBContext *context,
    Ztls::Random &rng, String issuer, String actor, String id,
    IDVec roles, int64_t now, AdminFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<AdminComplete_> state = new AdminComplete_{ZuMv(complete)};
  return requests->run(deadline, [
    state, db, context, &rng, issuer = ZuMv(issuer), actor = ZuMv(actor),
    id = ZuMv(id), roles = ZuMv(roles), now
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    clientRoles_(db, context, rng, ZuMv(issuer), ZuMv(actor), ZuMv(id),
      ZuMv(roles), now,
      [state](int error) mutable { state->complete(error); });
  }, [state]() mutable { state->cancel(); });
}

bool clientState(
    Requests *requests, ZuTime deadline, DB *db, DBContext *context,
    Ztls::Random &rng, String issuer, String actor, String id,
    State::T value, int64_t now, AdminFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<AdminComplete_> state = new AdminComplete_{ZuMv(complete)};
  return requests->run(deadline, [
    state, db, context, &rng, issuer = ZuMv(issuer), actor = ZuMv(actor),
    id = ZuMv(id), value, now
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    clientState_(db, context, rng, ZuMv(issuer), ZuMv(actor), ZuMv(id),
      value, now, [state](int error) mutable { state->complete(error); });
  }, [state]() mutable { state->cancel(); });
}

bool clientSecretDigest(
    Requests *requests, ZuTime deadline, DB *db, DBContext *context,
    Ztls::Random &rng, String issuer, String actor, String id,
    Bytes digest, int64_t now, AdminFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<AdminComplete_> state = new AdminComplete_{ZuMv(complete)};
  return requests->run(deadline, [
    state, db, context, &rng, issuer = ZuMv(issuer), actor = ZuMv(actor),
    id = ZuMv(id), digest = ZuMv(digest), now
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    clientSecretDigest_(db, context, rng, ZuMv(issuer), ZuMv(actor),
      ZuMv(id), ZuMv(digest), now,
      [state](int error) mutable { state->complete(error); });
  }, [state]() mutable { state->cancel(); });
}

bool grantCleanup(
    Requests *requests, ZuTime deadline, DBContext *context,
    int64_t now, unsigned limit, CleanupFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<CleanupComplete_> state = new CleanupComplete_{ZuMv(complete)};
  return requests->run(deadline, [state, context, now, limit](
      ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    grantCleanup_(context, now, limit, [state](
	int error, unsigned removed) mutable {
      state->complete(error, removed);
    });
  }, [state]() mutable { state->cancel(); });
}

bool auditCleanup(
    Requests *requests, ZuTime deadline, DBContext *context,
    int64_t before, unsigned limit, CleanupFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<CleanupComplete_> state = new CleanupComplete_{ZuMv(complete)};
  return requests->run(deadline, [state, context, before, limit](
      ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    auditCleanup_(context, before, limit, [state](
	int error, unsigned removed) mutable {
      state->complete(error, removed);
    });
  }, [state]() mutable { state->cancel(); });
}

} // namespace Zum
