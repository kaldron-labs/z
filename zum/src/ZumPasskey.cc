//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumPasskey.hh>

#include <zlib/ZumAdmin.hh>


#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsRandom.hh>

namespace Zum {

class PasskeyBeginComplete_ : public ZumObject {
public:
  PasskeyBeginComplete_(EnrollmentBeginFn complete) :
    m_complete{ZuMv(complete)} { }

  void request(ZmRef<Request> request) { m_request = ZuMv(request); }

  void complete(int error, EnrollmentBeginResult result)
  {
    m_request->complete([
      self = ZmRef<PasskeyBeginComplete_>{this}, error,
      result = ZuMv(result)
    ]() mutable {
      auto complete = ZuMv(self->m_complete);
      complete(error, ZuMv(result));
    });
  }

  void cancel()
  {
    auto complete = ZuMv(m_complete);
    complete(WebAuthnError::Storage, EnrollmentBeginResult{});
  }

private:
  ZmRef<Request>	m_request;
  EnrollmentBeginFn m_complete;
};

class PasskeyFinishComplete_ : public ZumObject {
public:
  PasskeyFinishComplete_(EnrollmentFinishFn complete) :
    m_complete{ZuMv(complete)} { }

  void request(ZmRef<Request> request) { m_request = ZuMv(request); }

  void complete(int error)
  {
    m_request->complete([
      self = ZmRef<PasskeyFinishComplete_>{this}, error
    ]() mutable {
      auto complete = ZuMv(self->m_complete);
      complete(error);
    });
  }

  void cancel()
  {
    auto complete = ZuMv(m_complete);
    complete(WebAuthnError::Storage);
  }

private:
  ZmRef<Request>	m_request;
  EnrollmentFinishFn m_complete;
};

class CapabilityResult_ : public ZumObject {
public:
  CapabilityResult_(bool ok_, String value_) :
    ok{ok_}, value{ZuMv(value_)} { }

  ~CapabilityResult_()
  {
    if (value && value.mutable_()) ZuClear(value.data(), value.length());
  }

  bool		ok;
  String	value;
};

class CapabilityComplete_ : public ZumObject {
public:
  CapabilityComplete_(CapabilityFn complete) :
    m_complete{ZuMv(complete)} { }

  void request(ZmRef<Request> request) { m_request = ZuMv(request); }

  void complete(bool ok, String value)
  {
    ZmRef<CapabilityResult_> delivery =
      new CapabilityResult_{ok, ZuMv(value)};
    m_request->complete([
      self = ZmRef<CapabilityComplete_>{this}, delivery = ZuMv(delivery)
    ]() mutable {
      auto complete = ZuMv(self->m_complete);
      complete(delivery->ok, ZuMv(delivery->value));
    });
  }

  void cancel()
  {
    auto complete = ZuMv(m_complete);
    complete(false, String{});
  }

private:
  ZmRef<Request>	m_request;
  CapabilityFn	m_complete;
};

class EnrollmentBegin_ : public ZumPolymorph {
public:
  EnrollmentBegin_(
      DBContext *context, Ztls::Random *rng, String capability,
      Bytes bindingDigest,
      EnrollmentBeginConfig config, EnrollmentBeginFn complete) :
    m_context{context}, m_rng{rng}, m_capability{ZuMv(capability)},
    m_bindingDigest{ZuMv(bindingDigest)},
    m_config{ZuMv(config)}, m_complete{ZuMv(complete)} { }

  ~EnrollmentBegin_() { clear_(); }

  void start()
  {
    if (!m_context || !m_rng || !m_bindingDigest || !m_config.issuer ||
	!m_config.rpID || !m_config.rpName || !m_config.name ||
	!m_config.displayName || !m_config.userID || m_config.now <= 0 ||
	m_config.expires <= m_config.now || !m_config.timeout) {
      finish_(WebAuthnError::Storage, {});
      return;
    }
    if (m_capability) {
      bool ok = opaqueParse(m_capability, m_capID, m_capDigest);
      ZuClear(m_capability.data(), m_capability.length());
      m_capability.null();
      if (!ok) {
	finish_(WebAuthnError::Ceremony, {});
	return;
      }
      random_(false);
      return;
    }
    String issuer = m_config.issuer;
    m_context->issuers->run(0, [
      self = ZmRef<EnrollmentBegin_>{this}, issuer = ZuMv(issuer)
    ]() mutable {
      self->m_context->issuers->find<0>(0, ZuFwdTuple(ZuMv(issuer)), [
	self = ZuMv(self)
      ](ZdbRowRef<Issuer> row) mutable { self->issuer_(bool(row)); });
    });
  }

private:
  void clear_()
  {
    if (m_capability && m_capability.mutable_())
      ZuClear(m_capability.data(), m_capability.length());
    m_capability.null();
    if (m_capDigest && m_capDigest.mutable_())
      ZuClear(m_capDigest.data(), m_capDigest.length());
    m_capDigest.null();
  }

  void finish_(int error, EnrollmentBeginResult result)
  {
    if (m_done) return;
    m_done = true;
    clear_();
    auto complete = ZuMv(m_complete);
    complete(error, ZuMv(result));
  }

  void issuer_(bool found)
  {
    if (!found) {
      finish_(WebAuthnError::Storage, {});
      return;
    }
    random_(true);
  }

  void random_(bool create)
  {
    enum { IDSize = 16, ChallengeSize = 32, HandleSize = 32 };
    Bytes random;
    unsigned idSize = create ? IDSize : 0;
    random.length(idSize + ChallengeSize + HandleSize, false);
    if (!m_rng->random(random)) {
      ZuClear(random.data(), random.length());
      finish_(WebAuthnError::Storage, {});
      return;
    }
    ZuBSpan challenge{random.data() + idSize, ChallengeSize};
    ZuBSpan handle{
      random.data() + idSize + ChallengeSize, HandleSize};
    Bytes id = create ? Bytes{ZuBSpan{random.data(), IDSize}} : m_capID;
    EnrollmentBeginResult result{
      .ceremonyID = id,
      .options = registrationOptions(challenge,
	m_config.rpID, m_config.rpName, handle,
	m_config.name, m_config.displayName, m_config.timeout)
    };
    if (!create) {
      m_config.roleIDs.null();
      m_context->grants->run(0, [
	self = ZmRef<EnrollmentBegin_>{this}, id = ZuMv(id),
	challenge = Bytes{challenge}, handle = Bytes{handle},
	result = ZuMv(result)
      ]() mutable {
	self->m_context->grants->findUpd<0, ZuSeq<1>>(
	  0, ZuFwdTuple(ZuMv(id)), [self = ZuMv(self),
	    challenge = ZuMv(challenge), handle = ZuMv(handle),
	    result = ZuMv(result)](ZdbRow<Grant> *row) mutable {
	  bool ok = row && row->data().kind == GrantKind::Capability &&
	    row->data().purpose == GrantPurpose::Bootstrap &&
	    row->data().state == State::Active && !row->data().owner &&
	    row->data().issuer == self->m_config.issuer &&
	    row->data().expires > self->m_config.now &&
	    Ztls::ctEqual(row->data().digest, self->m_capDigest);
	  if (ok) {
	    auto &grant = row->data();
	    ZuClear(grant.digest.data(), grant.digest.length());
	    grant.digest.null();
	    grant.userID = self->m_config.userID;
	    grant.challenge = ZuMv(challenge);
	    grant.bindingDigest = self->m_bindingDigest;
	    grant.created = self->m_config.now;
	    if (self->m_config.expires < grant.expires)
	      grant.expires = self->m_config.expires;
	    grant.kind = GrantKind::Ceremony;
	    grant.userName = self->m_config.name;
	    grant.userHandle = ZuMv(handle);
	    grant.label = ZuMv(self->m_config.label);
	    ok = row->commit();
	  }
	  self->finish_(ok ? WebAuthnError::OK : WebAuthnError::Ceremony,
	    ok ? ZuMv(result) : EnrollmentBeginResult{});
	});
      });
      ZuClear(random.data(), random.length());
      return;
    }
    Grant grant{
      .id = id,
      .userID = m_config.userID,
      .created = m_config.now,
      .expires = m_config.expires,
      .kind = GrantKind::Ceremony,
      .purpose = GrantPurpose::Enrollment,
      .state = State::Active,
      .issuer = m_config.issuer,
      .roleIDs = ZuMv(m_config.roleIDs),
      .challenge = Bytes{challenge},
      .bindingDigest = m_bindingDigest,
      .userName = m_config.name,
      .userHandle = Bytes{handle},
      .label = ZuMv(m_config.label)
    };
    ZuClear(random.data(), random.length());
    authorizationInsert(m_context, ZuMv(grant), [
      self = ZmRef<EnrollmentBegin_>{this}, result = ZuMv(result)
    ](bool ok) mutable {
      self->finish_(ok ? WebAuthnError::OK : WebAuthnError::Storage,
	ok ? ZuMv(result) : EnrollmentBeginResult{});
    });
  }

  DBContext	*m_context = nullptr;
  Ztls::Random	*m_rng = nullptr;
  String	m_capability;
  Bytes		m_capID;
  Bytes		m_capDigest;
  Bytes		m_bindingDigest;
  EnrollmentBeginConfig m_config;
  EnrollmentBeginFn m_complete;
  bool		m_done = false;
};

class EnrollmentFinish_ : public ZumPolymorph {
public:
  EnrollmentFinish_(
      DB *db, DBContext *context, Bytes ceremonyID, Bytes bindingDigest,
      RegistrationInput input, EnrollmentFinishConfig config,
      EnrollmentFinishFn complete) :
    m_db{db}, m_context{context}, m_ceremonyID{ZuMv(ceremonyID)},
    m_bindingDigest{ZuMv(bindingDigest)}, m_input{ZuMv(input)},
    m_config{ZuMv(config)}, m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_db || !m_context || m_ceremonyID.length() != 16 ||
	!m_bindingDigest) {
      finish_(WebAuthnError::Storage);
      return;
    }
    Bytes id = m_ceremonyID;
    m_context->grants->run(0, [
      self = ZmRef<EnrollmentFinish_>{this}, id = ZuMv(id)
    ]() mutable {
      self->m_context->grants->find<0>(0, ZuFwdTuple(ZuMv(id)), [
	self = ZuMv(self)
      ](ZdbRowRef<Grant> row) mutable { self->grant_(ZuMv(row)); });
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

  void grant_(ZdbRowRef<Grant> row)
  {
    if (!row) {
      finish_(WebAuthnError::Ceremony);
      return;
    }
    Enrollment enrollment;
    int error = enrollmentPrepare(row->data(), m_bindingDigest, m_input,
      m_config.origin, m_config.rpID,
      m_config.credentialIDMax,
      m_config.now, enrollment);
    if (error) {
      finish_(error);
      return;
    }
    m_principalAudit = Audit{
      .time = m_config.now,
      .issuer = row->data().issuer,
      .subject = auditID(enrollment.handle),
      .event = AuditEvent::PrincipalChange,
      .outcome = AuditOutcome::Success,
      .detail = "enrollment"
    };
    m_credentialAudit = Audit{
      .time = m_config.now,
      .issuer = row->data().issuer,
      .subject = m_principalAudit.subject,
      .target = auditID(enrollment.credentialID),
      .event = AuditEvent::CredentialChange,
      .outcome = AuditOutcome::Success,
      .detail = "enrollment"
    };
    static_assert(sizeof(m_sagaID) == 16);
    memcpy(&m_sagaID, m_ceremonyID.data(), sizeof(m_sagaID));
    using M = ZdbMSaga<Sagas>;
    ZmRef<M> saga = new M{};
    saga->init(ZuMv(enrollment));
    if (!m_db->saga(0, m_sagaID, ZuMv(saga), [
      self = ZmRef<EnrollmentFinish_>{this}
    ](bool ok) mutable {
      if (!ok) self->finish_(WebAuthnError::Storage);
    }, [self = ZmRef<EnrollmentFinish_>{this}](bool ok) mutable {
      self->saga_(ok);
    })) finish_(WebAuthnError::Storage);
  }

  void saga_(bool ok)
  {
    if (!ok) {
      finish_(WebAuthnError::Storage);
      return;
    }
    auto grants = m_context->grants;
    Bytes id = m_ceremonyID;
    grants->run(0, [
      self = ZmRef<EnrollmentFinish_>{this}, grants, id = ZuMv(id)
    ]() mutable {
      grants->findDel<0>(0, ZuFwdTuple(ZuMv(id)), [self = ZuMv(self)](
	  ZdbRow<Grant> *row) mutable {
	if (!row || row->data().kind != GrantKind::Ceremony ||
	    row->data().state != State::Consumed ||
	    row->data().owner != self->m_sagaID || !row->commit()) {
	  self->finish_(WebAuthnError::Storage);
	  return;
	}
	self->audit_();
      });
    });
  }

  void audit_()
  {
    auditWrite(m_context, ZuMv(m_principalAudit), [
      self = ZmRef<EnrollmentFinish_>{this}
    ](int error) mutable {
      if (error) {
	self->finish_(WebAuthnError::Storage);
	return;
      }
      auditWrite(self->m_context, ZuMv(self->m_credentialAudit), [
	self = ZuMv(self)
      ](int error) mutable {
	self->finish_(error ? WebAuthnError::Storage : WebAuthnError::OK);
      });
    });
  }

  DB		*m_db = nullptr;
  DBContext	*m_context = nullptr;
  Bytes		m_ceremonyID;
  Bytes		m_bindingDigest;
  RegistrationInput m_input;
  EnrollmentFinishConfig m_config;
  EnrollmentFinishFn m_complete;
  Audit		m_principalAudit;
  Audit		m_credentialAudit;
  ZdbSagaID	m_sagaID = 0;
  bool		m_done = false;
};

class CredentialBegin_ : public ZumPolymorph {
public:
  CredentialBegin_(
      DBContext *context, Ztls::Random *rng, Bytes bindingDigest,
      CredentialBeginConfig config, EnrollmentBeginFn complete) :
    m_context{context}, m_rng{rng}, m_bindingDigest{ZuMv(bindingDigest)},
    m_config{ZuMv(config)}, m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_context || !m_rng || !m_bindingDigest || !m_config.issuer ||
	!m_config.rpID || !m_config.rpName || !m_config.displayName ||
	!m_config.userID || m_config.now <= 0 ||
	m_config.expires <= m_config.now || !m_config.timeout) {
      finish_(WebAuthnError::Storage, {});
      return;
    }
    String issuer = m_config.issuer;
    m_context->issuers->run(0, [
      self = ZmRef<CredentialBegin_>{this}, issuer = ZuMv(issuer)
    ]() mutable {
      self->m_context->issuers->find<0>(0, ZuFwdTuple(ZuMv(issuer)), [
	self = ZuMv(self)
      ](ZdbRowRef<Issuer> issuer) mutable {
	if (!issuer) {
	  self->finish_(WebAuthnError::Storage, {});
	  return;
	}
	self->user_();
      });
    });
  }

private:
  void finish_(int error, EnrollmentBeginResult result)
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    complete(error, ZuMv(result));
  }

  void user_()
  {
    auto users = m_context->users;
    UserID id = m_config.userID;
    users->run(0, [self = ZmRef<CredentialBegin_>{this}, users, id]() {
      users->find<0>(0, ZuFwdTuple(id), [self = ZuMv(self)](
	  ZdbRowRef<User> user) mutable {
	if (!user || user->data().state != State::Active ||
	    !user->data().name || !user->data().handle) {
	  self->finish_(WebAuthnError::Credential, {});
	  return;
	}
	self->create_(user->data());
      });
    });
  }

  void create_(const User &user)
  {
    enum { IDSize = 16, ChallengeSize = 32 };
    Bytes random;
    random.length(IDSize + ChallengeSize, false);
    if (!m_rng->random(random)) {
      ZuClear(random.data(), random.length());
      finish_(WebAuthnError::Storage, {});
      return;
    }
    ZuBSpan id{random.data(), IDSize};
    ZuBSpan challenge{random.data() + IDSize, ChallengeSize};
    EnrollmentBeginResult result{
      .ceremonyID = Bytes{id},
      .options = registrationOptions(challenge, m_config.rpID,
	m_config.rpName, user.handle, user.name,
	m_config.displayName, m_config.timeout)
    };
    Grant grant{
      .id = Bytes{id},
      .userVersion = user.authVersion,
      .userID = user.id,
      .created = m_config.now,
      .expires = m_config.expires,
      .kind = GrantKind::Ceremony,
      .purpose = GrantPurpose::AddCredential,
      .state = State::Active,
      .issuer = m_config.issuer,
      .challenge = Bytes{challenge},
      .bindingDigest = m_bindingDigest,
      .userHandle = user.handle,
      .label = ZuMv(m_config.label)
    };
    ZuClear(random.data(), random.length());
    authorizationInsert(m_context, ZuMv(grant), [
      self = ZmRef<CredentialBegin_>{this}, result = ZuMv(result)
    ](bool ok) mutable {
      self->finish_(ok ? WebAuthnError::OK : WebAuthnError::Storage,
	ok ? ZuMv(result) : EnrollmentBeginResult{});
    });
  }

  DBContext	*m_context = nullptr;
  Ztls::Random	*m_rng = nullptr;
  Bytes		m_bindingDigest;
  CredentialBeginConfig m_config;
  EnrollmentBeginFn m_complete;
  bool		m_done = false;
};

class CredentialFinish_ : public ZumPolymorph {
public:
  CredentialFinish_(
      DB *db, DBContext *context, Bytes ceremonyID, Bytes bindingDigest,
      RegistrationInput input, EnrollmentFinishConfig config,
      EnrollmentFinishFn complete) :
    m_db{db}, m_context{context}, m_ceremonyID{ZuMv(ceremonyID)},
    m_bindingDigest{ZuMv(bindingDigest)}, m_input{ZuMv(input)},
    m_config{ZuMv(config)}, m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_db || !m_context || m_ceremonyID.length() != 16 ||
	!m_bindingDigest) {
      finish_(WebAuthnError::Storage);
      return;
    }
    Bytes id = m_ceremonyID;
    m_context->grants->run(0, [
      self = ZmRef<CredentialFinish_>{this}, id = ZuMv(id)
    ]() mutable {
      self->m_context->grants->find<0>(0, ZuFwdTuple(ZuMv(id)), [
	self = ZuMv(self)
      ](ZdbRowRef<Grant> row) mutable { self->grant_(ZuMv(row)); });
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

  void grant_(ZdbRowRef<Grant> row)
  {
    if (!row) {
      finish_(WebAuthnError::Ceremony);
      return;
    }
    CredentialAdd add;
    int error = credentialPrepare(row->data(), m_bindingDigest, m_input,
      m_config.origin, m_config.rpID,
      m_config.credentialIDMax, m_config.now, add);
    if (error) {
      finish_(error);
      return;
    }
    String subject = auditID(add.userHandle);
    m_audit = Audit{
      .time = m_config.now,
      .issuer = add.issuer,
      .actor = subject,
      .subject = ZuMv(subject),
      .target = auditID(add.credentialID),
      .event = AuditEvent::CredentialChange,
      .outcome = AuditOutcome::Success,
      .detail = "add"
    };
    static_assert(sizeof(m_sagaID) == 16);
    memcpy(&m_sagaID, m_ceremonyID.data(), sizeof(m_sagaID));
    using M = ZdbMSaga<Sagas>;
    ZmRef<M> saga = new M{};
    saga->init(ZuMv(add));
    if (!m_db->saga(0, m_sagaID, ZuMv(saga), [
      self = ZmRef<CredentialFinish_>{this}
    ](bool ok) mutable {
      if (!ok) self->finish_(WebAuthnError::Storage);
    }, [self = ZmRef<CredentialFinish_>{this}](bool ok) mutable {
      if (!ok) {
	self->finish_(WebAuthnError::Storage);
	return;
      }
      auto grants = self->m_context->grants;
      Bytes id = self->m_ceremonyID;
      grants->run(0, [self = ZuMv(self), grants, id = ZuMv(id)]() mutable {
	grants->findDel<0>(0, ZuFwdTuple(ZuMv(id)), [self = ZuMv(self)](
	    ZdbRow<Grant> *row) mutable {
	  if (!row || row->data().kind != GrantKind::Ceremony ||
	      row->data().state != State::Consumed ||
	      row->data().owner != self->m_sagaID || !row->commit()) {
	    self->finish_(WebAuthnError::Storage);
	    return;
	  }
	  auditWrite(self->m_context, ZuMv(self->m_audit), [
	    self = ZuMv(self)
	  ](int error) mutable {
	    self->finish_(error ? WebAuthnError::Storage : WebAuthnError::OK);
	  });
	});
      });
    })) finish_(WebAuthnError::Storage);
  }

  DB		*m_db = nullptr;
  DBContext	*m_context = nullptr;
  Bytes		m_ceremonyID;
  Bytes		m_bindingDigest;
  RegistrationInput m_input;
  EnrollmentFinishConfig m_config;
  EnrollmentFinishFn m_complete;
  Audit		m_audit;
  ZdbSagaID	m_sagaID = 0;
  bool		m_done = false;
};

class RecoveryIssue_ : public ZumPolymorph {
public:
  RecoveryIssue_(
      DB *db, DBContext *context, Ztls::Random *rng,
      RecoveryIssueConfig config, RecoveryIssueFn complete) :
    m_db{db}, m_context{context}, m_rng{rng},
    m_config{ZuMv(config)}, m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_db || !m_context || !m_rng || !m_config.issuer ||
	!m_config.actor || !m_config.userID || m_config.now <= 0 ||
	m_config.expires <= m_config.now) {
      finish_(false);
      return;
    }
    String issuer = m_config.issuer;
    m_context->issuers->run(0, [
      self = ZmRef<RecoveryIssue_>{this}, issuer = ZuMv(issuer)
    ]() mutable {
      self->m_context->issuers->find<0>(0, ZuFwdTuple(ZuMv(issuer)), [
	self = ZuMv(self)
      ](ZdbRowRef<Issuer> issuer) mutable {
	if (!issuer) {
	  self->finish_(false);
	  return;
	}
	self->user_();
      });
    });
  }

private:
  void finish_(bool ok)
  {
    if (m_done) return;
    m_done = true;
    if (!ok && m_token) {
      ZuClear(m_token.data(), m_token.length());
      m_token.null();
    }
    auto complete = ZuMv(m_complete);
    complete(ok, ok ? ZuMv(m_token) : String{});
  }

  void user_()
  {
    UserID id = m_config.userID;
    m_context->users->run(0, [
      self = ZmRef<RecoveryIssue_>{this}, id
    ]() mutable {
      self->m_context->users->find<0>(0, ZuFwdTuple(id), [
	self = ZuMv(self)
      ](ZdbRowRef<User> user) mutable {
	if (!user || user->data().owner ||
	    (user->data().state != State::Active &&
	     user->data().state != State::Suspended)) {
	  self->finish_(false);
	  return;
	}
	self->issue_(user->data());
      });
    });
  }

  void issue_(const User &user)
  {
    OpaqueToken capability;
    if (!opaqueIssue(*m_rng, capability)) {
      finish_(false);
      return;
    }
    m_token = ZuMv(capability.token);
    m_audit = Audit{
      .time = m_config.now,
      .issuer = m_config.issuer,
      .actor = m_config.actor,
      .subject = auditID(user.handle),
      .event = AuditEvent::PrincipalChange,
      .outcome = AuditOutcome::Success,
      .detail = "recovery suspended"
    };
    RecoveryStart recovery{
      .capabilityID = ZuMv(capability.id),
      .digest = ZuMv(capability.digest),
      .issuer = m_config.issuer,
      .userID = user.id,
      .userVersion = user.authVersion + 1,
      .created = m_config.now,
      .expires = m_config.expires,
      .actor = m_config.actor
    };
    ZdbSagaID sagaID;
    static_assert(sizeof(sagaID) == 16);
    memcpy(&sagaID, recovery.capabilityID.data(), sizeof(sagaID));
    using M = ZdbMSaga<Sagas>;
    ZmRef<M> saga = new M{};
    saga->init(ZuMv(recovery));
    if (!m_db->saga(0, sagaID, ZuMv(saga), [
      self = ZmRef<RecoveryIssue_>{this}
    ](bool ok) mutable {
      if (!ok) self->finish_(false);
    }, [self = ZmRef<RecoveryIssue_>{this}](bool ok) mutable {
      if (!ok) {
	self->finish_(false);
	return;
      }
      auditWrite(self->m_context, ZuMv(self->m_audit), [
	self = ZuMv(self)
      ](int error) mutable { self->finish_(!error); });
    })) finish_(false);
  }

  DB		*m_db = nullptr;
  DBContext	*m_context = nullptr;
  Ztls::Random	*m_rng = nullptr;
  RecoveryIssueConfig m_config;
  RecoveryIssueFn m_complete;
  String	m_token;
  Audit		m_audit;
  bool		m_done = false;
};

class RecoveryBegin_ : public ZumPolymorph {
public:
  RecoveryBegin_(
      DBContext *context, Ztls::Random *rng, String capability,
      Bytes bindingDigest, RecoveryBeginConfig config,
      EnrollmentBeginFn complete) :
    m_context{context}, m_rng{rng}, m_capability{ZuMv(capability)},
    m_bindingDigest{ZuMv(bindingDigest)}, m_config{ZuMv(config)},
    m_complete{ZuMv(complete)} { }

  ~RecoveryBegin_() { clear_(); }

  void start()
  {
    if (!m_context || !m_rng || !m_capability || !m_bindingDigest ||
	!m_config.issuer || !m_config.rpID || !m_config.rpName ||
	!m_config.displayName || m_config.now <= 0 ||
	m_config.expires <= m_config.now || !m_config.timeout ||
	!opaqueParse(m_capability, m_capID, m_capDigest)) {
      finish_(WebAuthnError::Ceremony, {});
      return;
    }
    ZuClear(m_capability.data(), m_capability.length());
    m_capability.null();
    Bytes id = m_capID;
    m_context->grants->run(0, [
      self = ZmRef<RecoveryBegin_>{this}, id = ZuMv(id)
    ]() mutable {
      self->m_context->grants->find<0>(0, ZuFwdTuple(ZuMv(id)), [
	self = ZuMv(self)
      ](ZdbRowRef<Grant> row) mutable { self->grant_(ZuMv(row)); });
    });
  }

private:
  void clear_()
  {
    if (m_capability && m_capability.mutable_())
      ZuClear(m_capability.data(), m_capability.length());
    m_capability.null();
    if (m_capDigest && m_capDigest.mutable_())
      ZuClear(m_capDigest.data(), m_capDigest.length());
    m_capDigest.null();
  }

  void finish_(int error, EnrollmentBeginResult result)
  {
    if (m_done) return;
    m_done = true;
    clear_();
    auto complete = ZuMv(m_complete);
    complete(error, ZuMv(result));
  }

  void grant_(ZdbRowRef<Grant> row)
  {
    if (!row || row->data().kind != GrantKind::Capability ||
	row->data().purpose != GrantPurpose::Recovery ||
	row->data().state != State::Active || row->data().owner ||
	row->data().issuer != m_config.issuer ||
	row->data().expires <= m_config.now ||
	!Ztls::ctEqual(row->data().digest, m_capDigest)) {
      finish_(WebAuthnError::Ceremony, {});
      return;
    }
    m_userID = row->data().userID;
    m_userVersion = row->data().userVersion;
    m_actor = row->data().actor;
    m_context->users->run(0, [self = ZmRef<RecoveryBegin_>{this}]() {
      self->m_context->users->find<0>(0, ZuFwdTuple(self->m_userID), [
	self = ZuMv(self)
      ](ZdbRowRef<User> row) mutable { self->user_(ZuMv(row)); });
    });
  }

  void user_(ZdbRowRef<User> row)
  {
    if (!row || row->data().state != State::Suspended || row->data().owner ||
	row->data().authVersion != m_userVersion || !row->data().name ||
	!row->data().handle || !m_actor) {
      finish_(WebAuthnError::Credential, {});
      return;
    }
    m_userName = row->data().name;
    random_();
  }

  void random_()
  {
    enum { ChallengeSize = 32, HandleSize = 32 };
    Bytes random;
    random.length(ChallengeSize + HandleSize, false);
    if (!m_rng->random(random)) {
      ZuClear(random.data(), random.length());
      finish_(WebAuthnError::Storage, {});
      return;
    }
    Bytes challenge{ZuBSpan{random.data(), ChallengeSize}};
    Bytes handle{ZuBSpan{random.data() + ChallengeSize, HandleSize}};
    EnrollmentBeginResult result{
      .ceremonyID = m_capID,
      .options = registrationOptions(challenge, m_config.rpID,
	m_config.rpName, handle, m_userName,
	m_config.displayName, m_config.timeout)
    };
    ZuClear(random.data(), random.length());
    Bytes id = m_capID;
    m_context->grants->run(0, [
      self = ZmRef<RecoveryBegin_>{this}, id = ZuMv(id),
      challenge = ZuMv(challenge), handle = ZuMv(handle),
      result = ZuMv(result)
    ]() mutable {
      self->m_context->grants->findUpd<0, ZuSeq<1>>(
	0, ZuFwdTuple(ZuMv(id)), [
	self = ZuMv(self), challenge = ZuMv(challenge),
	handle = ZuMv(handle), result = ZuMv(result)
      ](ZdbRow<Grant> *row) mutable {
	bool ok = row && row->data().kind == GrantKind::Capability &&
	  row->data().state == State::Active && !row->data().owner &&
	  row->data().expires > self->m_config.now &&
	  row->data().userID == self->m_userID &&
	  row->data().userVersion == self->m_userVersion &&
	  Ztls::ctEqual(row->data().digest, self->m_capDigest);
	if (ok) {
	  auto &grant = row->data();
	  ZuClear(grant.digest.data(), grant.digest.length());
	  grant.digest.null();
	  grant.challenge = ZuMv(challenge);
	  grant.bindingDigest = self->m_bindingDigest;
	  grant.created = self->m_config.now;
	  if (self->m_config.expires < grant.expires)
	    grant.expires = self->m_config.expires;
	  grant.kind = GrantKind::Ceremony;
	  grant.userHandle = ZuMv(handle);
	  grant.label = ZuMv(self->m_config.label);
	  ok = row->commit();
	}
	self->finish_(ok ? WebAuthnError::OK : WebAuthnError::Ceremony,
	  ok ? ZuMv(result) : EnrollmentBeginResult{});
      });
    });
  }

  DBContext	*m_context = nullptr;
  Ztls::Random	*m_rng = nullptr;
  String	m_capability;
  Bytes		m_capID;
  Bytes		m_capDigest;
  Bytes		m_bindingDigest;
  RecoveryBeginConfig m_config;
  EnrollmentBeginFn m_complete;
  UserID	m_userID = 0;
  uint64_t	m_userVersion = 0;
  String	m_userName;
  String	m_actor;
  bool		m_done = false;
};

class RecoveryFinish_ : public ZumPolymorph {
public:
  RecoveryFinish_(
      DB *db, DBContext *context, Bytes ceremonyID, Bytes bindingDigest,
      RegistrationInput input, EnrollmentFinishConfig config,
      EnrollmentFinishFn complete) :
    m_db{db}, m_context{context}, m_ceremonyID{ZuMv(ceremonyID)},
    m_bindingDigest{ZuMv(bindingDigest)}, m_input{ZuMv(input)},
    m_config{ZuMv(config)}, m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_db || !m_context || m_ceremonyID.length() != 16 ||
	!m_bindingDigest) {
      finish_(WebAuthnError::Storage);
      return;
    }
    Bytes id = m_ceremonyID;
    m_context->grants->run(0, [
      self = ZmRef<RecoveryFinish_>{this}, id = ZuMv(id)
    ]() mutable {
      self->m_context->grants->find<0>(0, ZuFwdTuple(ZuMv(id)), [
	self = ZuMv(self)
      ](ZdbRowRef<Grant> row) mutable { self->grant_(ZuMv(row)); });
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

  void grant_(ZdbRowRef<Grant> row)
  {
    if (!row) {
      finish_(WebAuthnError::Ceremony);
      return;
    }
    m_ceremony = row->data();
    UserID id = m_ceremony.userID;
    m_context->users->run(0, [
      self = ZmRef<RecoveryFinish_>{this}, id
    ]() mutable {
      self->m_context->users->find<0>(0, ZuFwdTuple(id), [
	self = ZuMv(self)
      ](ZdbRowRef<User> row) mutable { self->user_(ZuMv(row)); });
    });
  }

  void user_(ZdbRowRef<User> row)
  {
    if (!row) {
      finish_(WebAuthnError::Credential);
      return;
    }
    RecoveryEnroll recovery;
    int error = recoveryPrepare(m_ceremony, row->data(), m_bindingDigest,
      m_input, m_config.origin, m_config.rpID,
      m_config.credentialIDMax, m_config.now, recovery);
    if (error) {
      finish_(error);
      return;
    }
    String subject = auditID(recovery.newHandle);
    m_principalAudit = Audit{
      .time = m_config.now,
      .issuer = recovery.issuer,
      .actor = recovery.actor,
      .subject = subject,
      .event = AuditEvent::PrincipalChange,
      .outcome = AuditOutcome::Success,
      .detail = "recovery completed"
    };
    m_credentialAudit = Audit{
      .time = m_config.now,
      .issuer = recovery.issuer,
      .actor = recovery.actor,
      .subject = ZuMv(subject),
      .target = auditID(recovery.credentialID),
      .event = AuditEvent::CredentialChange,
      .outcome = AuditOutcome::Success,
      .detail = "recovery"
    };
    static_assert(sizeof(m_sagaID) == 16);
    memcpy(&m_sagaID, m_ceremonyID.data(), sizeof(m_sagaID));
    reinterpret_cast<uint8_t *>(&m_sagaID)[0] ^= 0x80;
    using M = ZdbMSaga<Sagas>;
    ZmRef<M> saga = new M{};
    saga->init(ZuMv(recovery));
    if (!m_db->saga(0, m_sagaID, ZuMv(saga), [
      self = ZmRef<RecoveryFinish_>{this}
    ](bool ok) mutable {
      if (!ok) self->finish_(WebAuthnError::Storage);
    }, [self = ZmRef<RecoveryFinish_>{this}](bool ok) mutable {
      self->saga_(ok);
    })) finish_(WebAuthnError::Storage);
  }

  void saga_(bool ok)
  {
    if (!ok) {
      finish_(WebAuthnError::Storage);
      return;
    }
    auto grants = m_context->grants;
    Bytes id = m_ceremonyID;
    grants->run(0, [
      self = ZmRef<RecoveryFinish_>{this}, grants, id = ZuMv(id)
    ]() mutable {
      grants->findDel<0>(0, ZuFwdTuple(ZuMv(id)), [self = ZuMv(self)](
	  ZdbRow<Grant> *row) mutable {
	if (!row || row->data().state != State::Consumed ||
	    row->data().owner != self->m_sagaID || !row->commit()) {
	  self->finish_(WebAuthnError::Storage);
	  return;
	}
	self->audit_();
      });
    });
  }

  void audit_()
  {
    auditWrite(m_context, ZuMv(m_principalAudit), [
      self = ZmRef<RecoveryFinish_>{this}
    ](int error) mutable {
      if (error) {
	self->finish_(WebAuthnError::Storage);
	return;
      }
      auditWrite(self->m_context, ZuMv(self->m_credentialAudit), [
	self = ZuMv(self)
      ](int error) mutable {
	self->finish_(error ? WebAuthnError::Storage : WebAuthnError::OK);
      });
    });
  }

  DB		*m_db = nullptr;
  DBContext	*m_context = nullptr;
  Bytes		m_ceremonyID;
  Bytes		m_bindingDigest;
  Grant		m_ceremony;
  RegistrationInput m_input;
  EnrollmentFinishConfig m_config;
  EnrollmentFinishFn m_complete;
  Audit		m_principalAudit;
  Audit		m_credentialAudit;
  ZdbSagaID	m_sagaID = 0;
  bool		m_done = false;
};

static void bootstrapIssue_(
    DBContext *context, Ztls::Random &rng, BootstrapConfig config,
    BootstrapFn complete)
{
  if (!context || !config.issuer || config.now <= 0 ||
      config.expires <= config.now) {
    complete(false, String{});
    return;
  }
  uint8_t hash[Ztls::MD<>::Size];
  {
    Ztls::MD<> md;
    md.update(ZuBSpan{"zum.bootstrap"});
    md.update(ZuBSpan{config.issuer});
    md.finish(hash);
  }
  OpaqueToken capability;
  if (!opaqueIssue(rng, ZuBSpan{hash, OpaqueIDSize}, capability)) {
    complete(false, String{});
    return;
  }
  Grant grant{
    .id = ZuMv(capability.id),
    .created = config.now,
    .expires = config.expires,
    .kind = GrantKind::Capability,
    .purpose = GrantPurpose::Bootstrap,
    .state = State::Active,
    .issuer = config.issuer,
    .roleIDs = ZuMv(config.roleIDs),
    .digest = ZuMv(capability.digest)
  };
  String token = ZuMv(capability.token);
  String issuer = config.issuer;
  context->issuers->run(0, [
    context, issuer = ZuMv(issuer), grant = ZuMv(grant),
    token = ZuMv(token), complete = ZuMv(complete)
  ]() mutable {
    context->issuers->find<0>(0, ZuFwdTuple(ZuMv(issuer)), [
      context, grant = ZuMv(grant), token = ZuMv(token),
      complete = ZuMv(complete)
    ](ZdbRowRef<Issuer> row) mutable {
      if (!row) {
	ZuClear(token.data(), token.length());
	complete(false, String{});
	return;
      }
      Bytes id = grant.id;
      context->grants->find<0>(0, ZuFwdTuple(ZuMv(id)), [
	context, grant = ZuMv(grant), token = ZuMv(token),
	complete = ZuMv(complete)
      ](ZdbRowRef<Grant> existing) mutable {
	if (existing) {
	  ZuClear(token.data(), token.length());
	  complete(false, String{});
	  return;
	}
	authorizationInsert(context, ZuMv(grant), [
	  token = ZuMv(token), complete = ZuMv(complete)
	](bool ok) mutable {
	  if (!ok) {
	    ZuClear(token.data(), token.length());
	    token.null();
	  }
	  complete(ok, ZuMv(token));
	});
      });
    });
  });
}

static void recoveryIssue_(
    DB *db, DBContext *context, Ztls::Random &rng,
    RecoveryIssueConfig config, RecoveryIssueFn complete)
{
  ZmRef<RecoveryIssue_> request = new RecoveryIssue_{
    db, context, &rng, ZuMv(config), ZuMv(complete)};
  request->start();
}

bool bootstrapIssue(
    Requests *requests, ZuTime deadline, DBContext *context,
    Ztls::Random &rng, BootstrapConfig config, BootstrapFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<CapabilityComplete_> state =
    new CapabilityComplete_{ZuMv(complete)};
  return requests->run(deadline, [
    state, context, &rng, config = ZuMv(config)
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    bootstrapIssue_(context, rng, ZuMv(config), [state](
	bool ok, String value) mutable {
      state->complete(ok, ZuMv(value));
    });
  }, [state]() mutable { state->cancel(); });
}

bool recoveryIssue(
    Requests *requests, ZuTime deadline, DB *db, DBContext *context,
    Ztls::Random &rng, RecoveryIssueConfig config, RecoveryIssueFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<CapabilityComplete_> state =
    new CapabilityComplete_{ZuMv(complete)};
  return requests->run(deadline, [
    state, db, context, &rng, config = ZuMv(config)
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    recoveryIssue_(db, context, rng, ZuMv(config), [state](
	bool ok, String value) mutable {
      state->complete(ok, ZuMv(value));
    });
  }, [state]() mutable { state->cancel(); });
}

bool recoveryBegin(
    Requests *requests, ZuTime deadline, DBContext *context,
    Ztls::Random &rng, String capability, Bytes bindingDigest,
    RecoveryBeginConfig config, EnrollmentBeginFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<PasskeyBeginComplete_> state =
    new PasskeyBeginComplete_{ZuMv(complete)};
  ZmRef<RecoveryBegin_> operation = new RecoveryBegin_{context, &rng,
    ZuMv(capability), ZuMv(bindingDigest), ZuMv(config), [state](
	int error, EnrollmentBeginResult result) mutable {
      state->complete(error, ZuMv(result));
    }};
  return requests->run(deadline, [state, operation](
      ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    operation->start();
  }, [state]() mutable { state->cancel(); });
}

bool recoveryFinish(
    Requests *requests, ZuTime deadline, DB *db, DBContext *context,
    Bytes ceremonyID, Bytes bindingDigest, RegistrationInput input,
    EnrollmentFinishConfig config, EnrollmentFinishFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<PasskeyFinishComplete_> state =
    new PasskeyFinishComplete_{ZuMv(complete)};
  return requests->run(deadline, [
    state, db, context, ceremonyID = ZuMv(ceremonyID),
    bindingDigest = ZuMv(bindingDigest), input = ZuMv(input),
    config = ZuMv(config)
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    ZmRef<RecoveryFinish_> operation = new RecoveryFinish_{db, context,
      ZuMv(ceremonyID), ZuMv(bindingDigest), ZuMv(input), ZuMv(config),
      [state](int error) mutable { state->complete(error); }};
    operation->start();
  }, [state]() mutable { state->cancel(); });
}

bool enrollmentBegin(
    Requests *requests, ZuTime deadline, DBContext *context,
    Ztls::Random &rng, Bytes bindingDigest, EnrollmentBeginConfig config,
    EnrollmentBeginFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<PasskeyBeginComplete_> state =
    new PasskeyBeginComplete_{ZuMv(complete)};
  return requests->run(deadline, [
    state, context, &rng, bindingDigest = ZuMv(bindingDigest),
    config = ZuMv(config)
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    ZmRef<EnrollmentBegin_> operation = new EnrollmentBegin_{context, &rng,
      {}, ZuMv(bindingDigest), ZuMv(config), [state](
	  int error, EnrollmentBeginResult result) mutable {
	state->complete(error, ZuMv(result));
      }};
    operation->start();
  }, [state]() mutable { state->cancel(); });
}

bool bootstrapBegin(
    Requests *requests, ZuTime deadline, DBContext *context,
    Ztls::Random &rng, String capability, Bytes bindingDigest,
    EnrollmentBeginConfig config, EnrollmentBeginFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<PasskeyBeginComplete_> state =
    new PasskeyBeginComplete_{ZuMv(complete)};
  ZmRef<EnrollmentBegin_> operation = new EnrollmentBegin_{context, &rng,
    ZuMv(capability), ZuMv(bindingDigest), ZuMv(config), [state](
	int error, EnrollmentBeginResult result) mutable {
      state->complete(error, ZuMv(result));
    }};
  return requests->run(deadline, [state, operation](
      ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    operation->start();
  }, [state]() mutable { state->cancel(); });
}

bool enrollmentFinish(
    Requests *requests, ZuTime deadline, DB *db, DBContext *context,
    Bytes ceremonyID, Bytes bindingDigest, RegistrationInput input,
    EnrollmentFinishConfig config, EnrollmentFinishFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<PasskeyFinishComplete_> state =
    new PasskeyFinishComplete_{ZuMv(complete)};
  return requests->run(deadline, [
    state, db, context, ceremonyID = ZuMv(ceremonyID),
    bindingDigest = ZuMv(bindingDigest), input = ZuMv(input),
    config = ZuMv(config)
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    ZmRef<EnrollmentFinish_> operation = new EnrollmentFinish_{db, context,
      ZuMv(ceremonyID), ZuMv(bindingDigest), ZuMv(input), ZuMv(config),
      [state](int error) mutable { state->complete(error); }};
    operation->start();
  }, [state]() mutable { state->cancel(); });
}

bool credentialBegin(
    Requests *requests, ZuTime deadline, DBContext *context,
    Ztls::Random &rng, Bytes bindingDigest, CredentialBeginConfig config,
    EnrollmentBeginFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<PasskeyBeginComplete_> state =
    new PasskeyBeginComplete_{ZuMv(complete)};
  return requests->run(deadline, [
    state, context, &rng, bindingDigest = ZuMv(bindingDigest),
    config = ZuMv(config)
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    ZmRef<CredentialBegin_> operation = new CredentialBegin_{context, &rng,
      ZuMv(bindingDigest), ZuMv(config), [state](
	  int error, EnrollmentBeginResult result) mutable {
	state->complete(error, ZuMv(result));
      }};
    operation->start();
  }, [state]() mutable { state->cancel(); });
}

bool credentialFinish(
    Requests *requests, ZuTime deadline, DB *db, DBContext *context,
    Bytes ceremonyID, Bytes bindingDigest, RegistrationInput input,
    EnrollmentFinishConfig config, EnrollmentFinishFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<PasskeyFinishComplete_> state =
    new PasskeyFinishComplete_{ZuMv(complete)};
  return requests->run(deadline, [
    state, db, context, ceremonyID = ZuMv(ceremonyID),
    bindingDigest = ZuMv(bindingDigest), input = ZuMv(input),
    config = ZuMv(config)
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    ZmRef<CredentialFinish_> operation = new CredentialFinish_{db, context,
      ZuMv(ceremonyID), ZuMv(bindingDigest), ZuMv(input), ZuMv(config),
      [state](int error) mutable { state->complete(error); }};
    operation->start();
  }, [state]() mutable { state->cancel(); });
}

} // namespace Zum
