//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "zumd_bootstrap.hh"
#include <zlib/zumd_db.hh>
#include <zlib/zumd_key_db.hh>

#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include <zlib/ZuArray.hh>
#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZePlatform.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZtlsCOSE.hh>
#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsPK.hh>
#include <zlib/ZtlsSec.hh>

#include <zlib/ZuDerive.hh>
#include <zlib/ZumMgmt.hh>
#include <zlib/zumd_discovery.hh>
#include <zlib/zumd_passkey.hh>
#include <zlib/zumd_jwt.hh>

namespace Zum {

static bool prepareSigner(
    Ztls::Random &rng, const ServerBootstrapConfig &config, SignKey &signer)
{
  return signKeyCreate(rng, config.dbKey, config.issuer,
    "bootstrap", config.now, signer);
}

static bool randomID(Ztls::Random &rng, uint64_t &id)
{
  do {
    if (!rng.random({reinterpret_cast<uint8_t *>(&id), sizeof(id)}))
      return false;
  } while (!id || id == ZuCmp<uint64_t>::null());
  return true;
}

static Bytes bootstrapID(ZuCSpan issuer)
{
  ZuBArray<Ztls::MD<>::Size> hash(Ztls::MD<>::Size, false);
  Ztls::MD<> md;
  md.update(ZuBSpan{"zum.bootstrap"});
  md.update(ZuBSpan{issuer});
  md.finish(hash);
  hash.length(OpaqueIDSize);
  return Bytes{hash};
}

static bool writeCapability(
    ZuCSpan path, ZuCSpan issuer, ZuCSpan token, bool replace)
{
  if (!path || !issuer || !token) return false;
  auto fail = [path](ZuCSpan op, int error) {
    ZiLOG(Error, "Zum", ([path = String{path}, op, e = ZeError{error}](auto &s) {
      s << "bootstrap output " << op << '(' << path << ") failed: " << e;
    }));
  };
  String value;
  value << issuer;
  auto baseLength = value.length();
  if (baseLength && value[baseLength - 1] == '/')
    value.length(baseLength - 1);
  value << "/bootstrap/enroll?capability=" << token << '\n';
  int fd = ::open(String{path}.data(),
    O_WRONLY | O_CREAT | (replace ? 0 : O_EXCL) | O_NOFOLLOW | O_CLOEXEC,
    0600);
  if (fd < 0) { fail("open", errno); return false; }
  auto close = [fd, &fail]() {
    if (!::close(fd)) return true;
    fail("close", errno);
    return false;
  };
  if (replace) {
    // Restrict existing permissions before writing the new capability.
    if (::fchmod(fd, 0600)) {
      fail("fchmod", errno);
      close();
      return false;
    }
    if (::ftruncate(fd, 0)) {
      fail("ftruncate", errno);
      close();
      return false;
    }
  }
  unsigned offset = 0;
  unsigned length = value.length();
  while (offset < length) {
    auto n = ::write(fd, value.data() + offset, length - offset);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) {
      fail("write", n < 0 ? errno : EIO);
      close();
      return false;
    }
    offset += unsigned(n);
  }
  bool ok = !::fsync(fd);
  if (!ok) fail("fsync", errno);
  if (!close()) ok = false;
  return ok;
}

static bool seedClientSettings(
    const ServerBootstrapConfig &config, Client &client)
{
  bool changed = false;
  if (!client.redirects) {
    client.redirects.push(config.adminRedirect);
    changed = true;
  }
  if (!client.identityScopes) {
    client.identityScopes.push("openid");
    client.identityScopes.push("profile");
    client.identityScopes.push("email");
    changed = true;
  }
  return changed;
}

template <typename Heap = ZuVoid>
class ServerBootstrap__ : public Heap, public ZmPolymorph  {
public:
  ServerBootstrap__(
      Requests *requests, DBContext *context, Ztls::Random &rng,
      ServerBootstrapConfig config, ServerBootstrapFn complete) :
    m_requests{requests}, m_context{context}, m_rng{&rng},
    m_config{ZuMv(config)}, m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_requests || !m_context || !m_config.issuer ||
        !m_config.dbKey || m_config.now <= 0 ||
        !m_config.ttl) {
      finish_(false);
      return;
    }
    m_check = serverKeyCheck(m_config.dbKey);
    if (m_config.reissue && !m_config.output) {
      ZiLOG(Error, "Zum", "bootstrap reissue requires --bootstrap-output");
      finish_(false);
      return;
    }
    auto issuers = m_context->issuers;
    String id = m_config.issuer;
    issuers->run(0, [self = ZmRef<ServerBootstrap__>{this}, issuers,
	id = ZuMv(id)]() mutable {
      issuers->find<0>(0, ZuFwdTuple(ZuMv(id)), [self = ZuMv(self)](
	  ZdbRowRef<Issuer> row) mutable { self->issuer_(ZuMv(row)); });
    });
  }

private:
  using Next = void (ServerBootstrap__::*)(bool);

  bool seedOptions_()
  {
    String admin{m_config.admin};
    if (!loginNormalize(admin) || admin != m_config.admin || !m_config.output) {
      ZiLOG(Error, "Zum", "initial bootstrap requires --admin and --bootstrap-output");
      return false;
    }
    return true;
  }

  void finish_(bool ok)
  {
    if (m_done) return;
    m_done = true;
    if (ok) {
      m_result.phase = m_issuer.bootstrapPhase;
      m_result.coreAppID = m_issuer.coreAppID;
      m_result.adminUserID = m_issuer.initialUserID;
      m_result.adminClientID = m_issuer.initialClientID;
    }
    if (m_config.dbKey)
      ZuClear(m_config.dbKey);
    auto complete = ZuMv(m_complete);
    complete(ok, ZuMv(m_result));
  }

  template <typename Table, typename T>
  void ensure_(Table *table, T data, Next next)
  {
    table->run(0, [self = ZmRef<ServerBootstrap__>{this}, table,
	data = ZuMv(data), next]() mutable {
      ZuStructKeyT<T, 0> key{ZuStructKey<0>(data)};
      table->template find<0>(0, ZuMv(key), [self = ZuMv(self), table,
	  data = ZuMv(data), next](ZdbRowRef<T> existing) mutable {
	if (existing) {
	  (self.ptr()->*next)(true);
	  return;
	}
	ZdbRowRef<T> row = new ZdbRow<T>{table, ZdbShard{0}};
	table->insert(ZuMv(row), [self = ZuMv(self), data = ZuMv(data),
	    next](ZdbRow<T> *row) mutable {
	  if (!row) {
	    (self.ptr()->*next)(false);
	    return;
	  }
	  new (row->ptr()) T{ZuMv(data)};
	  (self.ptr()->*next)(row->commit());
	});
      });
    });
  }

  void issuer_(ZdbRowRef<Issuer> row)
  {
    if (row) {
      m_issuer = row->data();
      validate_();
      return;
    }
    if (m_config.repairAdmin) {
      ZiLOG(Error, "Zum", "administrator client repair requires an existing issuer");
      finish_(false);
      return;
    }
    if (!seedOptions_()) { finish_(false); return; }
    Issuer issuer{
      .id = m_config.issuer, .schemaVersion = SchemaVersion,
      .bootstrapPhase = BootstrapPhase::Empty,
      .initialClientID = m_config.adminClientID,
      .keyCheck = m_check
    };
    if (!randomID(*m_rng, issuer.coreAppID) ||
        !randomID(*m_rng, issuer.initialUserID)) {
      finish_(false);
      return;
    }
    m_issuer = issuer;
    m_result.initialized = true;
    ensure_(m_context->issuers, ZuMv(issuer), &ServerBootstrap__::issuerAdded_);
  }

  void issuerAdded_(bool ok)
  {
    if (!ok) { finish_(false); return; }
    validate_();
  }

  void validate_()
  {
    if (m_issuer.schemaVersion != SchemaVersion) {
      ZiLOG(Error, "Zum", ([version = m_issuer.schemaVersion](auto &s) {
	s << "unsupported database schema: issuer schema version " << version
	  << ", supported version " << SchemaVersion;
      }));
      finish_(false);
      return;
    }
    if (!m_check ||
        !Ztls::ctEqual(m_issuer.keyCheck, m_check) ||
        !m_issuer.coreAppID || !m_issuer.initialUserID ||
        !m_issuer.initialClientID) {
      finish_(false);
      return;
    }
    if (m_issuer.pendingKeyCheck) {
      ZiLOG(Error, "Zum", "offline database secret-key rotation incomplete");
      finish_(false);
      return;
    }
    if (m_config.repairAdmin) { repairAdmin_(); return; }
    switch (m_issuer.bootstrapPhase) {
      case BootstrapPhase::Empty:
	if (!seedOptions_()) { finish_(false); return; }
	seedApp_();
	return;
      case BootstrapPhase::Core: admin_(); return;
      case BootstrapPhase::AdminPending: admin_(); return;
      case BootstrapPhase::Ready:
	if (m_config.reissue) admin_();
	else finish_(true);
	return;
      default: finish_(false); return;
    }
  }

  void repairAdmin_()
  {
    auto clients = m_context->clients;
    String id = m_issuer.initialClientID;
    clients->run(0, [self = ZmRef<ServerBootstrap__>{this}, clients,
	id = ZuMv(id)]() mutable {
      clients->findUpd<0>(0, ZuFwdTuple(ZuMv(id)), [self = ZuMv(self)](
	  ZdbRow<Client> *row) mutable {
	if (!row || row->data().appID != self->m_issuer.coreAppID ||
	    row->data().profile != ClientProfile::Native || row->data().owner) {
	  ZiLOG(Error, "Zum", "administrator client repair requires an unowned seeded native client");
	  self->finish_(false);
	  return;
	}
	auto &client = row->data();
	if (!seedClientSettings(self->m_config, client)) {
	  self->finish_(true);
	  return;
	}
	client.updated = self->m_config.now;
	++client.version;
	bool ok = row->commit();
	if (ok) ZiLOG(Info, "Zum", "restored missing administrator client redirect/scopes");
	self->finish_(ok);
      });
    });
  }

  void seedApp_()
  {
    String audience = m_config.issuer;
    if (audience[audience.length() - 1] == '/')
      audience.length(audience.length() - 1);
    audience << "/admin";
    auto now = m_config.now;
    ensure_(m_context->apps, App{
      .id = m_issuer.coreAppID, .name = "zum", .label = "Zum",
      .state = State::Active, .nextActionID = CoreAction::N,
      .authVersion = 1, .catalogRevision = 1,
      .created = now, .updated = now, .audience = ZuMv(audience)},
      &ServerBootstrap__::seedActions_);
  }

  void seedActions_(bool ok)
  {
    if (!ok) { finish_(false); return; }
    while (m_actionID < CoreAction::N && !coreAction(m_actionID))
      ++m_actionID;
    if (m_actionID >= CoreAction::N) {
      seedSuperuser_();
      return;
    }
    auto id = m_actionID++;
    auto now = m_config.now;
    ensure_(m_context->actions, Action{
      .appID = m_issuer.coreAppID, .id = id,
      .name = coreAction(id), .label = coreAction(id),
      .state = State::Active, .origin = Origin::Standard,
      .catalogRevision = 1, .created = now, .updated = now},
      &ServerBootstrap__::seedActions_);
  }

  void seedSuperuser_()
  {
    ZtBitmap actions{CoreAction::N};
    for (ActionID i = 0; i < CoreAction::N; ++i)
      if (coreAction(i)) actions.set(i);
    auto now = m_config.now;
    ensure_(m_context->roles, Role{
      .appID = m_issuer.coreAppID, .id = CoreRole::Superuser,
      .name = "zum.admin", .label = "Zum superuser",
      .actions = ZuMv(actions), .state = State::Active,
      .origin = Origin::Standard, .catalogRevision = 1,
      .created = now, .updated = now}, &ServerBootstrap__::seedPublisherRole_);
  }

  void seedPublisherRole_(bool ok)
  {
    if (!ok) { finish_(false); return; }
    ZtBitmap actions{CoreAction::N};
    actions.set(MgmtOp::operationQuery);
    actions.set(MgmtOp::catalogPublish);
    auto now = m_config.now;
    ensure_(m_context->roles, Role{
      .appID = m_issuer.coreAppID, .id = CoreRole::CatalogPublisher,
      .name = "zum.catalog", .label = "Zum catalog publisher",
      .actions = ZuMv(actions), .state = State::Active,
      .origin = Origin::Standard, .catalogRevision = 1,
      .created = now, .updated = now}, &ServerBootstrap__::seedClient_);
  }

  void seedClient_(bool ok)
  {
    if (!ok) { finish_(false); return; }
    auto now = m_config.now;
    Client client{
      .id = m_issuer.initialClientID, .appID = m_issuer.coreAppID,
      .label = "Zum administrator CLI", .created = now, .updated = now,
      .profile = ClientProfile::Native,
      .grants = uint8_t(ClientGrant::AuthCode() |
	  ClientGrant::Refresh()), .refreshAllowed = true,
      .state = State::Active};
    seedClientSettings(m_config, client);
    ensure_(m_context->clients, ZuMv(client),
      &ServerBootstrap__::seedClientAccess_);
  }

  void seedClientAccess_(bool ok)
  {
    if (!ok) { finish_(false); return; }
    auto now = m_config.now;
    ensure_(m_context->clientAccess, ClientAccess{
      .clientID = m_issuer.initialClientID, .appID = m_issuer.coreAppID,
      .roleIDs = IDVec{CoreRole::Superuser, CoreRole::CatalogPublisher},
      .state = State::Active,
      .created = now, .updated = now}, &ServerBootstrap__::seedUser_);
  }

  void seedUser_(bool ok)
  {
    if (!ok) { finish_(false); return; }
    auto now = m_config.now;
    ensure_(m_context->users, User{
      .id = m_issuer.initialUserID, .source = UserSource::Local,
      .name = m_config.admin, .email = m_config.admin,
      .created = now, .updated = now, .state = State::Pending},
      &ServerBootstrap__::seedAssignment_);
  }

  void seedAssignment_(bool ok)
  {
    if (!ok) { finish_(false); return; }
    auto now = m_config.now;
    ensure_(m_context->assignments, Assignment{
      .appID = m_issuer.coreAppID, .userID = m_issuer.initialUserID,
      .roleIDs = IDVec{CoreRole::Superuser}, .state = State::Active,
      .created = now, .updated = now}, &ServerBootstrap__::seedPolicy_);
  }

  void seedPolicy_(bool ok)
  {
    if (!ok) { finish_(false); return; }
    auto now = m_config.now;
    ensure_(m_context->authPolicies, AuthPolicy{
      .appID = m_issuer.coreAppID, .localFirst = true,
      .assignmentMaxAge = 300, .sessionIdle = 1800,
      .sessionAbsolute = 43200, .tokenLifetime = 300,
      .consentPolicy = ConsentPolicy::Preauthorized,
      .state = State::Active, .created = now, .updated = now},
      &ServerBootstrap__::seedSigner_);
  }

  void seedSigner_(bool ok)
  {
    if (!ok) { finish_(false); return; }
    SignKey signer;
    if (!prepareSigner(*m_rng, m_config, signer)) {
      finish_(false);
      return;
    }
    ensure_(m_context->signKeys, ZuMv(signer),
      &ServerBootstrap__::seedCoreSigner_);
  }

  void seedCoreSigner_(bool ok)
  {
    if (!ok) { finish_(false); return; }
    String issuer;
    String id;
    id << "app_" << m_issuer.coreAppID << "_1";
    SignKey signer;
    if (!appIssuer(m_config.issuer, m_issuer.coreAppID, issuer) ||
	!signKeyCreate(*m_rng, m_config.dbKey, issuer, id,
	  m_config.now, signer)) {
      finish_(false);
      return;
    }
    ensure_(m_context->signKeys, ZuMv(signer),
      &ServerBootstrap__::seedComplete_);
  }

  void seedComplete_(bool ok)
  {
    if (!ok) { finish_(false); return; }
    phase_(BootstrapPhase::Empty, BootstrapPhase::Core,
      &ServerBootstrap__::core_);
  }

  void phase_(BootstrapPhase::T from, BootstrapPhase::T to, Next next)
  {
    auto issuers = m_context->issuers;
    String id = m_config.issuer;
    issuers->run(0, [self = ZmRef<ServerBootstrap__>{this}, issuers,
	id = ZuMv(id), from, to, next]() mutable {
      issuers->findUpd<0>(0, ZuFwdTuple(ZuMv(id)), [self = ZuMv(self),
	  from, to, next](ZdbRow<Issuer> *row) mutable {
	bool ok = row && row->data().bootstrapPhase == from;
	if (ok) {
	  row->data().bootstrapPhase = to;
	  ok = row->commit();
	}
	if (ok) self->m_issuer.bootstrapPhase = to;
	(self.ptr()->*next)(ok);
      });
    });
  }

  void core_(bool ok)
  {
    if (!ok) { finish_(false); return; }
    issue_();
  }

  void issue_()
  {
    if (!m_config.output) {
      ZiLOG(Error, "Zum", "bootstrap capability issuance requires --bootstrap-output");
      finish_(false);
      return;
    }
    String issuer;
    if (!appIssuer(m_config.issuer, m_issuer.coreAppID, issuer)) {
      finish_(false);
      return;
    }
    if (!bootstrapIssue(m_requests, Zm::now() + ZuTime{15}, m_context, *m_rng,
        BootstrapConfig{.issuer = ZuMv(issuer),
          .appID = m_issuer.coreAppID,
          .userName = m_config.admin, .label = "bootstrap passkey",
          .userID = m_issuer.initialUserID, .now = m_config.now,
          .expires = m_config.now + m_config.ttl},
        [self = ZmRef<ServerBootstrap__>{this}](bool ok, String token) mutable {
          self->issued_(ok, ZuMv(token));
        })) finish_(false);
  }

  void issued_(bool ok, String token)
  {
    bool written = ok && writeCapability(
      m_config.output, m_config.issuer, token, m_config.reissue);
    if (token) ZuClear(token);
    if (!written) { finish_(false); return; }
    m_result.capabilityWritten = true;
    phase_(BootstrapPhase::Core, BootstrapPhase::AdminPending,
      &ServerBootstrap__::pending_);
  }

  void pending_(bool ok)
  {
    if (!ok) { finish_(false); return; }
    admin_();
  }

  void admin_()
  {
    auto users = m_context->users;
    auto id = m_issuer.initialUserID;
    users->run(0, [self = ZmRef<ServerBootstrap__>{this}, users, id]() mutable {
      users->find<0>(0, ZuFwdTuple(id), [self = ZuMv(self)](
	  ZdbRowRef<User> row) mutable { self->adminLoaded_(ZuMv(row)); });
    });
  }

  void adminLoaded_(ZdbRowRef<User> row)
  {
    if (!row || !row->data().name) { finish_(false); return; }
    m_config.admin = row->data().name;
    if (m_issuer.bootstrapPhase == BootstrapPhase::Core) { issue_(); return; }
    if (row->data().state == State::Active && row->data().handle &&
        !row->data().owner) {
      if (m_issuer.bootstrapPhase == BootstrapPhase::AdminPending) {
	phase_(BootstrapPhase::AdminPending, BootstrapPhase::Ready,
	  &ServerBootstrap__::ready_);
	return;
      }
      if (m_config.reissue && !m_result.capabilityWritten) {
	reissue_(row->data());
	return;
      }
      if (m_issuer.bootstrapPhase == BootstrapPhase::Ready) {
	finish_(true);
	return;
      }
    }
    if (!m_config.reissue || m_result.capabilityWritten) {
      finish_(true);
      return;
    }
    reissue_(row->data());
  }

  void ready_(bool ok)
  {
    if (ok && m_config.reissue && !m_result.capabilityWritten) admin_();
    else finish_(ok);
  }

  void reissue_(const User &user)
  {
    bool enrolled = user.state == State::Active && user.handle;
    if (user.owner || user.source != UserSource::Local ||
	(!enrolled && (user.state != State::Pending || user.handle))) {
      finish_(false);
      return;
    }
    String issuer;
    if (!appIssuer(m_config.issuer, m_issuer.coreAppID, issuer)) {
      finish_(false);
      return;
    }
    OpaqueToken capability;
    Bytes id = bootstrapID(issuer);
    if (!opaqueIssue(*m_rng, id, capability)) {
      finish_(false);
      return;
    }
    auto grants = m_context->grants;
    GrantPurpose::T purpose =
      enrolled ? GrantPurpose::AddCredential : GrantPurpose::Bootstrap;
    Grant replacement{
      .id = ZuMv(capability.id),
      .userVersion = user.authVersion,
      .appID = m_issuer.coreAppID,
      .userID = m_issuer.initialUserID,
      .created = m_config.now,
      .expires = m_config.now + m_config.ttl,
      .kind = GrantKind::Capability,
      .purpose = purpose,
      .state = State::Active,
      .issuer = ZuMv(issuer),
      .digest = ZuMv(capability.digest),
      .userName = user.name,
      .userHandle = user.handle,
      .label = "bootstrap passkey"
    };
    id = replacement.id;
    grants->run(0, [self = ZmRef<ServerBootstrap__>{this}, grants,
        id = ZuMv(id), replacement = ZuMv(replacement),
        token = ZuMv(capability.token)]() mutable {
      grants->findUpd<0, ZuSeq<1>>(0, ZuFwdTuple(ZuMv(id)), [self = ZuMv(self),
          replacement = ZuMv(replacement),
          token = ZuMv(token)](ZdbRow<Grant> *row) mutable {
	if (!row) {
	  auto grants = self->m_context->grants;
	  ZdbRowRef<Grant> fresh = new ZdbRow<Grant>{grants, ZdbShard{0}};
	  grants->insert(ZuMv(fresh), [self = ZuMv(self),
	      replacement = ZuMv(replacement), token = ZuMv(token)](
	      ZdbRow<Grant> *row) mutable {
	    bool ok = false;
	    if (row) {
	      new (row->ptr()) Grant{ZuMv(replacement)};
	      ok = row->commit();
	    }
	    self->reissued_(ok, ZuMv(token));
	  });
	  return;
	}
	if (row->data().issuer != replacement.issuer ||
	    row->data().appID != self->m_issuer.coreAppID ||
	    row->data().userID != self->m_issuer.initialUserID ||
	    (row->data().kind != GrantKind::Capability &&
	     row->data().kind != GrantKind::Ceremony) ||
	    (row->data().purpose != GrantPurpose::Bootstrap &&
	     row->data().purpose != GrantPurpose::AddCredential) ||
	    (row->data().state != State::Active &&
	     row->data().state != State::Consumed) || row->data().owner) {
	  self->reissued_(false, ZuMv(token));
	  return;
	}
	row->data() = ZuMv(replacement);
	self->reissued_(row->commit(), ZuMv(token));
      });
    });
  }

  void reissued_(bool ok, String token)
  {
    bool written = ok && writeCapability(
      m_config.output, m_config.issuer, token, true);
    if (token) ZuClear(token);
    m_result.capabilityWritten = written;
    finish_(written);
  }

  Requests		*m_requests = nullptr;
  DBContext		*m_context = nullptr;
  Ztls::Random		*m_rng = nullptr;
  ServerBootstrapConfig m_config;
  ServerBootstrapFn	m_complete;
  ServerBootstrapResult m_result;
  Issuer		m_issuer;
  Bytes		m_check;
  ActionID		m_actionID = 0;
  bool			m_done = false;
};
ZuDerive(ServerBootstrap_Heap,
  (ZmHeap<"Zum.zumd.bootstrap.ServerBootstrap", ServerBootstrap__<>>));
ZuDerive(ServerBootstrap_, (ServerBootstrap__<ServerBootstrap_Heap>));

void serverBootstrap(
    DB *, Requests *requests, DBContext *context, Ztls::Random &rng,
    ServerBootstrapConfig config, ServerBootstrapFn complete)
{
  ZmRef<ServerBootstrap_> bootstrap = new ServerBootstrap_{
    requests, context, rng, ZuMv(config), ZuMv(complete)};
  bootstrap->start();
}

} // namespace Zum
