//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "ZumBootstrap.hh"

#include <fcntl.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <zlib/ZuBase64URL.hh>
#include <zlib/ZtlsCOSE.hh>
#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsPK.hh>
#include <zlib/ZtlsSec.hh>

#include <zlib/ZumMgmt.hh>
#include <zlib/ZumPasskey.hh>

namespace Zum {

enum { SchemaVersion = 9 };

static Bytes keyCheck(ZuBSpan key)
{
  Bytes digest;
  digest.length(Ztls::MD<>::Size, false);
  unsigned length = 0;
  if (!::HMAC(EVP_sha256(), key.data(), int(key.length()),
      reinterpret_cast<const uint8_t *>("zum.db.key.v1"),
      sizeof("zum.db.key.v1") - 1, digest.data(), &length) ||
      length != digest.length()) digest.null();
  return digest;
}

static bool encode(String &out, ZuBSpan value)
{
  auto length = ZuBase64URL::enclen(value.length());
  out.length(length);
  return ZuBase64URL::encode(out.span(), value) == length;
}

static Bytes encryptSecret(
    Ztls::Random &rng, ZuBSpan key, ZuBSpan aad, ZuBSpan plain)
{
  enum { Version = 1, AES256GCM = 1, KeyID = 1,
    Header = 6, Nonce = 12, Tag = 16 };
  if (key.length() != 32 || plain.length() > unsigned(INT_MAX) ||
      aad.length() > unsigned(INT_MAX)) return {};
  Bytes envelope;
  envelope.length(Header + Nonce + plain.length() + Tag, false);
  auto out = envelope.data();
  out[0] = Version;
  out[1] = AES256GCM;
  out[2] = uint8_t(KeyID >> 24);
  out[3] = uint8_t(KeyID >> 16);
  out[4] = uint8_t(KeyID >> 8);
  out[5] = uint8_t(KeyID);
  if (!rng.random({out + Header, Nonce})) return {};
  EVP_CIPHER_CTX *cipher = EVP_CIPHER_CTX_new();
  if (!cipher) return {};
  int n = 0, offset = 0;
  bool ok = EVP_EncryptInit_ex(
      cipher, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
    EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_SET_IVLEN, Nonce, nullptr) == 1 &&
    EVP_EncryptInit_ex(
      cipher, nullptr, nullptr, key.data(), out + Header) == 1 &&
    (!aad || EVP_EncryptUpdate(cipher, nullptr, &n,
      aad.data(), int(aad.length())) == 1) &&
    (!plain || EVP_EncryptUpdate(cipher, out + Header + Nonce, &n,
      plain.data(), int(plain.length())) == 1);
  if (ok) offset = n;
  ok = ok && EVP_EncryptFinal_ex(
    cipher, out + Header + Nonce + offset, &n) == 1;
  if (ok) offset += n;
  ok = ok && unsigned(offset) == plain.length() &&
    EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_GET_TAG, Tag,
      out + Header + Nonce + plain.length()) == 1;
  EVP_CIPHER_CTX_free(cipher);
  if (!ok) {
    ZuClear(envelope.data(), envelope.length());
    return {};
  }
  return envelope;
}

static String secretAAD(
    ZuCSpan issuer, ZuCSpan recordType, ZuCSpan recordID, ZuCSpan field)
{
  String aad;
  aad << issuer << '\0' << recordType << '\0' << recordID << '\0' << field;
  return aad;
}

bool serverSecretDecrypt(
    ZuBSpan key, ZuCSpan issuer, ZuCSpan recordType, ZuCSpan recordID,
    ZuCSpan field, ZuBSpan envelope, Bytes &plain)
{
  enum { Version = 1, AES256GCM = 1, KeyID = 1,
    Header = 6, Nonce = 12, Tag = 16 };
  plain.null();
  if (key.length() != 32 || envelope.length() < Header + Nonce + Tag ||
      envelope[0] != Version || envelope[1] != AES256GCM || envelope[2] ||
      envelope[3] || envelope[4] || envelope[5] != KeyID) return false;
  auto length = envelope.length() - Header - Nonce - Tag;
  if (length > unsigned(INT_MAX)) return false;
  auto aad = secretAAD(issuer, recordType, recordID, field);
  if (aad.length() > unsigned(INT_MAX)) return false;
  Bytes next;
  next.length(length, false);
  EVP_CIPHER_CTX *cipher = EVP_CIPHER_CTX_new();
  if (!cipher) return false;
  int n = 0, offset = 0;
  bool ok = EVP_DecryptInit_ex(
      cipher, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
    EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_SET_IVLEN, Nonce, nullptr) == 1 &&
    EVP_DecryptInit_ex(cipher, nullptr, nullptr, key.data(),
      envelope.data() + Header) == 1 &&
    (!aad || EVP_DecryptUpdate(cipher, nullptr, &n,
      reinterpret_cast<const uint8_t *>(aad.data()), int(aad.length())) == 1) &&
    (!length || EVP_DecryptUpdate(cipher, next.data(), &n,
      envelope.data() + Header + Nonce, int(length)) == 1);
  if (ok) offset = n;
  ok = ok && EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_SET_TAG, Tag,
      const_cast<uint8_t *>(envelope.data()) + Header + Nonce + length) == 1 &&
    EVP_DecryptFinal_ex(cipher, next.data() + offset, &n) == 1;
  if (ok) offset += n;
  EVP_CIPHER_CTX_free(cipher);
  if (!ok || unsigned(offset) != length) {
    if (next) ZuClear(next.data(), next.length());
    return false;
  }
  plain = ZuMv(next);
  return true;
}

bool serverSecretEncrypt(
    Ztls::Random &rng, ZuBSpan key, ZuCSpan issuer, ZuCSpan recordType,
    ZuCSpan recordID, ZuCSpan field, ZuBSpan plain, Bytes &envelope)
{
  String aad = secretAAD(issuer, recordType, recordID, field);
  Bytes next = encryptSecret(rng, key, aad, plain);
  if (!next) return false;
  envelope = ZuMv(next);
  return true;
}

static bool prepareSigner(
    Ztls::Random &rng, const ServerBootstrapConfig &config, SignKey &signer)
{
  constexpr ZuCSpan id{"bootstrap"};
  Ztls::PK::SK_EC key{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  Bytes privateKey, publicKey;
  privateKey.length(Ztls::Backend::pkey_ec_key_size(key.key), false);
  publicKey.length(Ztls::Backend::pkey_ec_public_size(key.key), false);
  if (publicKey.length() != Ztls::COSE::ES256::PublicKeySize ||
      !Ztls::Backend::pkey_ec_export_private(key.key, privateKey) ||
      !Ztls::Backend::pkey_ec_export_public(key.key, publicKey)) return false;
  String x, y;
  if (!encode(x, {publicKey.data() + 1,
        Ztls::COSE::ES256::CoordinateSize}) ||
      !encode(y, {publicKey.data() + 1 +
        Ztls::COSE::ES256::CoordinateSize,
        Ztls::COSE::ES256::CoordinateSize})) return false;
  auto aad = secretAAD(
    config.issuer, "zum.sign_key", id, "privateMaterial");
  Bytes encrypted = encryptSecret(rng, config.dbKey, aad, privateKey);
  ZuClear(privateKey.data(), privateKey.length());
  if (!encrypted) return false;
  String jwk{"{\"kid\":\"bootstrap\",\"kty\":\"EC\",\"crv\":\"P-256\","
    "\"use\":\"sig\",\"alg\":\"ES256\",\"x\":\""};
  jwk << x << "\",\"y\":\"" << y << "\"}";
  signer = SignKey{
    .id = id, .issuer = config.issuer, .algorithm = "ES256",
    .publicJwk = ZuMv(jwk), .privateMaterial = ZuMv(encrypted),
    .notBefore = config.now, .state = State::Active,
    .version = 1, .created = config.now, .updated = config.now};
  return true;
}

static bool randomID(Ztls::Random &rng, uint64_t &id)
{
  do {
    if (!rng.random({reinterpret_cast<uint8_t *>(&id), sizeof(id)}))
      return false;
  } while (!id || id == UINT64_MAX);
  return true;
}

static Bytes bootstrapID(ZuCSpan issuer)
{
  uint8_t hash[Ztls::MD<>::Size];
  Ztls::MD<> md;
  md.update(ZuBSpan{"zum.bootstrap"});
  md.update(ZuBSpan{issuer});
  md.finish(hash);
  return Bytes{ZuBSpan{hash, OpaqueIDSize}};
}

static bool writeCapability(ZuCSpan path, ZuCSpan issuer, ZuCSpan token)
{
  if (!path || !issuer || !token) return false;
  String value;
  value << issuer;
  if (value[value.length() - 1] == '/') value.length(value.length() - 1);
  value << "/bootstrap/enroll?capability=" << token << '\n';
  int fd = ::open(String{path}.data(),
    O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (fd < 0) return false;
  unsigned offset = 0;
  while (offset < value.length()) {
    auto n = ::write(fd, value.data() + offset, value.length() - offset);
    if (n <= 0) { ::close(fd); return false; }
    offset += unsigned(n);
  }
  bool ok = !::fsync(fd);
  if (::close(fd)) ok = false;
  return ok;
}

class ServerBootstrap_ : public ZumPolymorph {
public:
  ServerBootstrap_(
      Requests *requests, DBContext *context, Ztls::Random &rng,
      ServerBootstrapConfig config, ServerBootstrapFn complete) :
    m_requests{requests}, m_context{context}, m_rng{&rng},
    m_config{ZuMv(config)}, m_complete{ZuMv(complete)} { }

  void start()
  {
    String admin{m_config.admin};
    if (!m_requests || !m_context || !m_config.issuer ||
        !loginNormalize(admin) || admin != m_config.admin ||
        !m_config.output || !m_config.dbKey || m_config.now <= 0 ||
        !m_config.ttl) {
      finish_(false);
      return;
    }
    m_check = keyCheck(m_config.dbKey);
    auto issuers = m_context->issuers;
    String id = m_config.issuer;
    issuers->run(0, [self = ZmRef<ServerBootstrap_>{this}, issuers,
	id = ZuMv(id)]() mutable {
      issuers->find<0>(0, ZuFwdTuple(ZuMv(id)), [self = ZuMv(self)](
	  ZdbRowRef<Issuer> row) mutable { self->issuer_(ZuMv(row)); });
    });
  }

private:
  using Next = void (ServerBootstrap_::*)(bool);

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
      ZuClear(m_config.dbKey.data(), m_config.dbKey.length());
    auto complete = ZuMv(m_complete);
    complete(ok, ZuMv(m_result));
  }

  template <typename Table, typename T>
  void ensure_(Table *table, T data, Next next)
  {
    table->run(0, [self = ZmRef<ServerBootstrap_>{this}, table,
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
    Issuer issuer{
      .id = m_config.issuer, .schemaVersion = SchemaVersion,
      .bootstrapPhase = BootstrapPhase::Empty,
      .initialClientID = m_config.adminClientID,
      .keyCheck = m_check, .nextActionID = CoreAction::N,
      .authVersion = 1
    };
    if (!randomID(*m_rng, issuer.coreAppID) ||
        !randomID(*m_rng, issuer.initialUserID)) {
      finish_(false);
      return;
    }
    m_issuer = issuer;
    m_result.initialized = true;
    ensure_(m_context->issuers, ZuMv(issuer), &ServerBootstrap_::issuerAdded_);
  }

  void issuerAdded_(bool ok)
  {
    if (!ok) { finish_(false); return; }
    validate_();
  }

  void validate_()
  {
    if (!m_check || m_issuer.schemaVersion != SchemaVersion ||
        !Ztls::ctEqual(m_issuer.keyCheck, m_check) ||
        !m_issuer.coreAppID || !m_issuer.initialUserID ||
        !m_issuer.initialClientID) {
      finish_(false);
      return;
    }
    switch (m_issuer.bootstrapPhase) {
      case BootstrapPhase::Empty: seedApp_(); return;
      case BootstrapPhase::Core: issue_(); return;
      case BootstrapPhase::AdminPending: admin_(); return;
      case BootstrapPhase::Ready:
	finish_(!m_config.reissue);
	return;
      default: finish_(false); return;
    }
  }

  void seedApp_()
  {
    auto now = m_config.now;
    ensure_(m_context->apps, App{
      .id = m_issuer.coreAppID, .name = "zum", .label = "Zum",
      .state = State::Active, .nextActionID = CoreAction::N,
      .authVersion = 1, .catalogRevision = 1,
      .serviceClientID = m_issuer.initialClientID,
      .created = now, .updated = now}, &ServerBootstrap_::seedActions_);
  }

  void seedActions_(bool ok)
  {
    if (!ok) { finish_(false); return; }
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
      &ServerBootstrap_::seedActions_);
  }

  void seedSuperuser_()
  {
    ZtBitmap actions{CoreAction::N};
    for (ActionID i = 0; i < CoreAction::N; ++i) actions.set(i);
    auto now = m_config.now;
    ensure_(m_context->roles, Role{
      .appID = m_issuer.coreAppID, .id = CoreRole::Superuser,
      .name = "superuser", .label = "Zum superuser",
      .actions = ZuMv(actions), .state = State::Active,
      .origin = Origin::Standard, .catalogRevision = 1,
      .created = now, .updated = now}, &ServerBootstrap_::seedServiceRole_);
  }

  void seedServiceRole_(bool ok)
  {
    if (!ok) { finish_(false); return; }
    ZtBitmap actions{CoreAction::N};
    actions.set(MgmtOp::operationQuery);
    actions.set(MgmtOp::catalogPublish);
    actions.set(CoreAction::FacadeAuthorize);
    actions.set(CoreAction::FacadeToken);
    actions.set(CoreAction::FacadeRevoke);
    auto now = m_config.now;
    ensure_(m_context->roles, Role{
      .appID = m_issuer.coreAppID, .id = CoreRole::AppService,
      .name = "appService", .label = "Zum application service",
      .actions = ZuMv(actions), .state = State::Active,
      .origin = Origin::Standard, .catalogRevision = 1,
      .created = now, .updated = now}, &ServerBootstrap_::seedAudience_);
  }

  void seedAudience_(bool ok)
  {
    if (!ok) { finish_(false); return; }
    m_adminURI = m_config.issuer;
    if (m_adminURI[m_adminURI.length() - 1] == '/')
      m_adminURI.length(m_adminURI.length() - 1);
    m_adminURI << "/admin";
    auto now = m_config.now;
    ensure_(m_context->audiences, Audience{
      .id = CoreAudience::Admin, .appID = m_issuer.coreAppID,
      .name = "admin", .uri = m_adminURI,
      .state = State::Active, .created = now, .updated = now},
      &ServerBootstrap_::seedAdminScope_);
  }

  void seedAdminScope_(bool ok)
  {
    if (!ok) { finish_(false); return; }
    auto now = m_config.now;
    ensure_(m_context->scopes, Scope{
      .appID = m_issuer.coreAppID, .id = CoreScope::Admin,
      .audienceID = CoreAudience::Admin, .audience = m_adminURI,
      .name = "zum.admin", .roleIDs = IDVec{CoreRole::Superuser},
      .state = State::Active, .origin = Origin::Standard,
      .catalogRevision = 1, .created = now, .updated = now},
      &ServerBootstrap_::seedServiceScope_);
  }

  void seedServiceScope_(bool ok)
  {
    if (!ok) { finish_(false); return; }
    auto now = m_config.now;
    ensure_(m_context->scopes, Scope{
      .appID = m_issuer.coreAppID, .id = CoreScope::AppService,
      .audienceID = CoreAudience::Admin, .audience = m_adminURI,
      .name = "zum.service", .roleIDs = IDVec{CoreRole::AppService},
      .state = State::Active, .origin = Origin::Standard,
      .catalogRevision = 1, .created = now, .updated = now},
      &ServerBootstrap_::seedClient_);
  }

  void seedClient_(bool ok)
  {
    if (!ok) { finish_(false); return; }
    auto now = m_config.now;
    Client client{
      .id = m_issuer.initialClientID, .appID = m_issuer.coreAppID,
      .label = "Zum administrator CLI", .created = now, .updated = now,
      .type = ClientType::Native, .authMethod = ClientAuthMethod::None,
      .grants = uint8_t(ClientGrant::AuthorizationCode |
	  ClientGrant::RefreshToken), .refreshAllowed = true,
      .state = State::Active};
    client.redirects.push(m_config.adminRedirect);
    client.identityScopes.push("openid");
    client.identityScopes.push("profile");
    client.identityScopes.push("email");
    client.audiences.push(m_adminURI);
    client.scopeIDs.push(CoreScope::Admin);
    ensure_(m_context->clients, ZuMv(client),
      &ServerBootstrap_::seedClientAccess_);
  }

  void seedClientAccess_(bool ok)
  {
    if (!ok) { finish_(false); return; }
    auto now = m_config.now;
    ensure_(m_context->clientAccess, ClientAccess{
      .clientID = m_issuer.initialClientID, .appID = m_issuer.coreAppID,
      .audienceIDs = IDVec{CoreAudience::Admin},
      .scopeIDs = IDVec{CoreScope::Admin}, .state = State::Active,
      .created = now, .updated = now}, &ServerBootstrap_::seedUser_);
  }

  void seedUser_(bool ok)
  {
    if (!ok) { finish_(false); return; }
    auto now = m_config.now;
    ensure_(m_context->users, User{
      .id = m_issuer.initialUserID, .source = UserSource::Local,
      .name = m_config.admin, .email = m_config.admin,
      .created = now, .updated = now, .state = State::Pending},
      &ServerBootstrap_::seedMembership_);
  }

  void seedMembership_(bool ok)
  {
    if (!ok) { finish_(false); return; }
    auto now = m_config.now;
    ensure_(m_context->memberships, Membership{
      .appID = m_issuer.coreAppID, .userID = m_issuer.initialUserID,
      .roleIDs = IDVec{CoreRole::Superuser}, .state = State::Active,
      .created = now, .updated = now}, &ServerBootstrap_::seedPolicy_);
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
      &ServerBootstrap_::seedSigner_);
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
      &ServerBootstrap_::seedComplete_);
  }

  void seedComplete_(bool ok)
  {
    if (!ok) { finish_(false); return; }
    phase_(BootstrapPhase::Empty, BootstrapPhase::Core,
      &ServerBootstrap_::core_);
  }

  void phase_(BootstrapPhase::T from, BootstrapPhase::T to, Next next)
  {
    auto issuers = m_context->issuers;
    String id = m_config.issuer;
    issuers->run(0, [self = ZmRef<ServerBootstrap_>{this}, issuers,
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
    IDVec roles{CoreRole::Superuser};
    if (!bootstrapIssue(m_requests, Zm::now() + ZuTime{15}, m_context, *m_rng,
        BootstrapConfig{.issuer = m_config.issuer,
          .appID = m_issuer.coreAppID, .roleIDs = ZuMv(roles),
          .userName = m_config.admin, .label = "bootstrap passkey",
          .userID = m_issuer.initialUserID, .now = m_config.now,
          .expires = m_config.now + m_config.ttl},
        [self = ZmRef<ServerBootstrap_>{this}](bool ok, String token) mutable {
          self->issued_(ok, ZuMv(token));
        })) finish_(false);
  }

  void issued_(bool ok, String token)
  {
    bool written = ok && writeCapability(
      m_config.output, m_config.issuer, token);
    if (token) ZuClear(token.data(), token.length());
    if (!written) { finish_(false); return; }
    m_result.capabilityWritten = true;
    phase_(BootstrapPhase::Core, BootstrapPhase::AdminPending,
      &ServerBootstrap_::pending_);
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
    users->run(0, [self = ZmRef<ServerBootstrap_>{this}, users, id]() mutable {
      users->find<0>(0, ZuFwdTuple(id), [self = ZuMv(self)](
	  ZdbRowRef<User> row) mutable { self->adminLoaded_(ZuMv(row)); });
    });
  }

  void adminLoaded_(ZdbRowRef<User> row)
  {
    if (row && row->data().state == State::Active && row->data().handle &&
        !row->data().owner) {
      phase_(BootstrapPhase::AdminPending, BootstrapPhase::Ready,
        &ServerBootstrap_::ready_);
      return;
    }
    if (!m_config.reissue) { finish_(true); return; }
    reissue_();
  }

  void ready_(bool ok) { finish_(ok); }

  void reissue_()
  {
    OpaqueToken capability;
    Bytes id = bootstrapID(m_config.issuer);
    if (!opaqueIssue(*m_rng, id, capability)) {
      finish_(false);
      return;
    }
    auto grants = m_context->grants;
    id = capability.id;
    grants->run(0, [self = ZmRef<ServerBootstrap_>{this}, grants,
	id = ZuMv(id), capability = ZuMv(capability)]() mutable {
      grants->findUpd<0>(0, ZuFwdTuple(ZuMv(id)), [self = ZuMv(self),
	  capability = ZuMv(capability)](ZdbRow<Grant> *row) mutable {
	if (!row || row->data().issuer != self->m_config.issuer ||
	    row->data().appID != self->m_issuer.coreAppID ||
	    row->data().userID != self->m_issuer.initialUserID ||
	    row->data().kind != GrantKind::Capability ||
	    row->data().purpose != GrantPurpose::Bootstrap ||
	    row->data().state != State::Active || row->data().owner) {
	  self->finish_(false);
	  return;
	}
	row->data().digest = ZuMv(capability.digest);
	row->data().created = self->m_config.now;
	row->data().expires = self->m_config.now + self->m_config.ttl;
	bool ok = row->commit();
	String token = ZuMv(capability.token);
	bool written = ok && writeCapability(
	  self->m_config.output, self->m_config.issuer, token);
	if (token) ZuClear(token.data(), token.length());
	self->m_result.capabilityWritten = written;
	self->finish_(written);
      });
    });
  }

  Requests		*m_requests = nullptr;
  DBContext		*m_context = nullptr;
  Ztls::Random		*m_rng = nullptr;
  ServerBootstrapConfig m_config;
  ServerBootstrapFn	m_complete;
  ServerBootstrapResult m_result;
  Issuer		m_issuer;
  Bytes		m_check;
  String		m_adminURI;
  ActionID		m_actionID = 0;
  bool			m_done = false;
};

void serverBootstrap(
    DB *, Requests *requests, DBContext *context, Ztls::Random &rng,
    ServerBootstrapConfig config, ServerBootstrapFn complete)
{
  ZmRef<ServerBootstrap_> bootstrap = new ServerBootstrap_{
    requests, context, rng, ZuMv(config), ZuMv(complete)};
  bootstrap->start();
}

} // namespace Zum
