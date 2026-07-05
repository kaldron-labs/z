//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// server-side user DB with MFA, API keys, etc.

#include <zlib/ZumServer.hh>

#include <zlib/ZuBase32.hh>
#include <zlib/ZuBase64.hh>

#include <zlib/ZtQuote.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiAssert.hh>

#include <zlib/ZtlsTOTP.hh>

namespace Zum::Server {

UserDB::UserDB(Ztls::Random *rng) : m_rng{rng}
{
  for (unsigned i = 0; i < nPerms(); i++)
    m_perms[i] = ZuCmp<PermID>::null();
}

UserDB::~UserDB()
{
}

void UserDB::dbCf(const ZvCf *cf, ZdbCf &dbCf)
{
  // ensure all tables are running on the same thread
  // - ensures that direct references to the user and key DB objects
  //   can be cached in the session, permitting single-threaded access
  //   without contention on the object data
  // - UserDB relies on being a single-writer to the DB (Zdb guarantee)

  StringVec threads;
  threads.push(cf->get<true>("thread"));

  static ZtArray<ZuCSpan> tables{
    "zum.user",
    "zum.role",
    "zum.key",
    "zum.perm"
  };

  for (auto &&tblID: tables) {
    auto node = dbCf.tableCfs.find(tblID);
    using Node = ZuDecay<decltype(*node)>;
    if (!node) dbCf.tableCfs.addNode(node = new Node{tblID});
    node->data().threads = threads;
  }
}

void UserDB::init(ZvCf *cf, Zdb *db)
{
  ZiAssert(m_state == UserDBState::Uninitialized, "Zum",
    (state = m_state), "invalid state=" << state, return);

  m_passLen = cf->getInt("passLen", 6, 60, m_passLen);
  m_totpRange = cf->getInt("totpRange", 0, 100, m_totpRange);
  m_keyInterval = cf->getInt("keyInterval", 0, 36000, m_keyInterval);

  m_userTbl = db->initTable<User>("zum.user");
  m_roleTbl = db->initTable<Role>("zum.role");
  m_keyTbl = db->initTable<Key>("zum.key");
  m_permTbl = db->initTable<Perm>("zum.perm");

  m_mx = db->mx();
  m_sid = m_userTbl->config().sids[0];

  m_state = UserDBState::Initialized;
}

void UserDB::final()
{
  m_state = UserDBState::Uninitialized;

  m_userTbl = nullptr;
  m_roleTbl = nullptr;
  m_keyTbl = nullptr;
  m_permTbl = nullptr;
}

// initiate open sequence
void UserDB::open(StringVec perms, OpenFn fn)
{
  ZuPtr<Open> context = new Open{
    .fn = ZuMv(fn),
    .perms = ZuMv(perms)
  };
  context->permIDs.size(context->perms.length());
  run([this, context = ZuMv(context)]() mutable { open_(ZuMv(context)); });
}
void UserDB::open_(ZuPtr<Open> context)
{
  // check for overlapping open/bootstrap or already opened
  if (m_state.load_() != UserDBState::Initialized) {
    (context->fn)(false, ZtArray<unsigned>{});
    return;
  }
  m_state = UserDBState::Opening;

  open_recoverNextUserID(ZuMv(context));
}
// recover nextUserID
void UserDB::open_recoverNextUserID(ZuPtr<Open> context)
{
  m_userTbl->selectKeys<0>(ZuTuple<>{}, 1, [
    this, context = ZuMv(context)
  ](auto max, unsigned) mutable {
    run([this, context = ZuMv(context), max = ZuMv(max)]() mutable {
      using Key = ZuStructKeyT<User>;
      if (max.template is<Key>())
	m_nextUserID = max.template p<Key>().template p<0>() + 1;
      else
	open_recoverNextPermID(ZuMv(context));
    });
  });
}
// recover nextPermID
void UserDB::open_recoverNextPermID(ZuPtr<Open> context)
{
  m_permTbl->selectKeys<0>(ZuTuple<>{}, 1, [
    this, context = ZuMv(context)
  ](auto max, unsigned) mutable {
    run([this, context = ZuMv(context), max = ZuMv(max)]() mutable {
      using RowKey = ZuStructKeyT<Perm>;
      if (max.template is<RowKey>())
	m_nextPermID = max.template p<RowKey>().template p<0>() + 1;
      else
	open_findAddPerm(ZuMv(context));
    });
  });
}
// find permission and update m_perms[]
void UserDB::open_findAddPerm(ZuPtr<Open> context)
{
  static auto permName = [](const Open *context) -> String {
    auto i = context->perm;
    if (i < nPerms()) {
      String s{"UserMgmt."};
      auto loginReqEnd = unsigned(fbs::LoginReqData::MAX);
      if (i < loginReqEnd) return s << fbs::EnumNamesLoginReqData()[i + 1];
      return s << fbs::EnumNamesReqData()[(i - loginReqEnd) + 1];
    } else {
      return context->perms[i - nPerms()];
    }
  };

  static auto stashPermID = [](UserDB *this_, Open *context, unsigned id) {
    auto i = context->perm;
    if (i < nPerms())
      this_->m_perms[i] = id;
    else
      context->permIDs.push(id);
  };

  m_permTbl->find<1>(0, ZuMvTuple(permName(context)), [
    this, context = ZuMv(context)
  ](ZmRef<ZdbObject<Perm>> dbPerm) mutable {
    if (!dbPerm) {
      ZdbObjRef<Perm> dbPerm = new ZdbObject<Perm>{m_permTbl, 0};
      m_permTbl->insert(dbPerm, [
	this, context = ZuMv(context)
      ](ZdbObject<Perm> *dbPerm) mutable {
	if (!dbPerm) { opened(ZuMv(context), false); return; }
	initPerm(dbPerm, permName(context)); // performs commit
	stashPermID(this, context, dbPerm->data().id);
	open_nextPerm(ZuMv(context));
      });
    } else {
      stashPermID(this, context, dbPerm->data().id);
      open_nextPerm(ZuMv(context));
    }
  });
}
// iterate to next permission
void UserDB::open_nextPerm(ZuPtr<Open> context)
{
  if (++context->perm < nPerms() + context->perms.length())
    run([this, context = ZuMv(context)]() mutable {
      open_findAddPerm(ZuMv(context));
    });
  else
    opened(ZuMv(context), true);
}
// inform app of open result
void UserDB::opened(ZuPtr<Open> context, bool ok)
{
  run([this, context = ZuMv(context), ok]() {
    m_state = ok ? UserDBState::Opened : UserDBState::OpenFailed;
    (context->fn)(ok, ZuMv(context->permIDs));
  });
}

// initiate bootstrap
void UserDB::bootstrap(String userName, BootstrapFn fn)
{
  ZuPtr<Bootstrap> context = new Bootstrap{
    .userName = ZuMv(userName),
    .fn = ZuMv(fn)
  };
  run([this, context = ZuMv(context)]() mutable { bootstrap_(ZuMv(context)); });
}
void UserDB::bootstrap_(ZuPtr<Bootstrap> context)
{
  // check for overlapping open/bootstrap or failed open
  if (m_state.load_() != UserDBState::Opened) {
    (context->fn)(false);
    return;
  }
  m_state = UserDBState::Bootstrap;

  run([this, context = ZuMv(context)]() mutable {
    bootstrap_findAddUser(ZuMv(context));
  });
}
// idempotent insert admin user
void UserDB::bootstrap_findAddUser(ZuPtr<Bootstrap> context)
{
  m_userTbl->find<1>(0, ZuFwdTuple(context->userName), [
    this, context = ZuMv(context)
  ](ZmRef<ZdbObject<User>> dbUser) mutable {
    if (dbUser) {
      bootstrapped(ZuMv(context), BootstrapResult{true});
      return;
    }
    dbUser = new ZdbObject<User>{m_userTbl, 0};
    m_userTbl->insert(dbUser, [
      this, context = ZuMv(context)
    ](ZdbObject<User> *dbUser) mutable {
      if (!dbUser) {
	bootstrapped(ZuMv(context), BootstrapResult{false});
	return;
      }
      String passwd;
      initUser(dbUser, m_nextUserID++,
	ZuMv(context->userName), { },
	UserFlags::Immutable() |
	UserFlags::Enabled() |
	UserFlags::SuperUser(),
	passwd);
      auto &user = dbUser->data();
      String secret{ZuBase32::enclen(user.secret.length())};
      secret.length(secret.size());
      secret.length(ZuBase32::encode(secret, user.secret));
      bootstrapped(ZuMv(context),
	BootstrapResult{BootstrapData{ZuMv(passwd), ZuMv(secret)}});
    });
  });
}
// inform app of bootstrap result
void UserDB::bootstrapped(ZuPtr<Bootstrap> context, BootstrapResult result)
{
  run([this, context = ZuMv(context), result = ZuMv(result)]() mutable {
    m_state = UserDBState::Opened;
    (context->fn)(ZuMv(result));
  });
}

// initialize API key
void UserDB::initKey(ZdbObject<Key> *dbKey, UserID userID, KeyIDData keyID)
{
  auto key = new (dbKey->ptr_()) Key{.userID = userID, .id = keyID};
  key->secret.length(key->secret.size());
  m_rng->random(key->secret);
  dbKey->commit();
}

// initialize permission
void UserDB::initPerm(ZdbObject<Perm> *dbPerm, String name)
{
  new (dbPerm->ptr_()) Perm{m_nextPermID++, ZuMv(name)};
  dbPerm->commit();
}

// initialize role
void UserDB::initRole(
  ZdbObject<Role> *dbRole, String name,
  ZtBitmap perms, ZtBitmap apiperms, RoleFlags::T flags)
{
  new (dbRole->ptr_()) Role{
    ZuMv(name),
    ZuMv(perms),
    ZuMv(apiperms),
    flags
  };
  dbRole->commit();
};

// initialize user
void UserDB::initUser(
  ZdbObject<User> *dbUser, UserID id, String name,
  StringVec roles, UserFlags::T flags,
  String &passwd)
{
  auto &user =
    *(new (dbUser->ptr_()) User{
      .id = id,
      .name = ZuMv(name),
      .roles = ZuMv(roles),
      .flags = flags
    });
  {
    passwd.length(m_passLen);
    m_rng->random(passwd);
    ZuSpan<uint8_t> passwd_(passwd);
    for (unsigned i = 0; i < m_passLen; i++) {
      unsigned c = passwd_[i];
      c = ((c * 23040)>>16) + 33;	// ASCII 33-122 inclusive
      switch (c) {			// remap quotes and backslash
	case '\'': c = '{'; break;
	case '"':  c = '|'; break;
	case '`':  c = '}'; break;
	case '\\': c = '~'; break;
      }
      passwd_[i] = c;
    }
  }
  user.secret.length(user.secret.size());
  m_rng->random(user.secret);
  {
    Ztls::HMAC<KeyType> hmac;
    hmac.start(user.secret);
    hmac.update(passwd);
    user.hmac.length(user.hmac.size());
    hmac.finish(user.hmac.data());
  }
  dbUser->commit();
}

// start a new session (a user is logging in)
void UserDB::sessionLoad_login(String userName, SessionFn fn)
{
  ZuPtr<SessionLoad> context = new SessionLoad{ZuMv(userName), ZuMv(fn)};
  run([this, context = ZuMv(context)]() mutable {
    sessionLoad_findUser(ZuMv(context));
  });
}
// start a new session (using an API key)
void UserDB::sessionLoad_access(KeyIDData keyID, SessionFn fn)
{
  ZuPtr<SessionLoad> context = new SessionLoad{ZuMv(keyID), ZuMv(fn)};
  run([this, context = ZuMv(context)]() mutable {
    sessionLoad_findKey(ZuMv(context));
  });
}
// find and load the user
void UserDB::sessionLoad_findUser(ZuPtr<SessionLoad> context)
{
  m_userTbl->find<1>(0, ZuFwdTuple(context->cred.p<String>()), [
    this, context = ZuMv(context)
  ](ZmRef<ZdbObject<User>> dbUser) mutable {
    if (!dbUser) { sessionLoaded(ZuMv(context), false); return; }
    dbUser->pin();
    const auto &user = dbUser->data();
    context->session = new Session{this, ZuMv(dbUser)};
    if (!user.roles)
      sessionLoaded(ZuMv(context), true);
    else
      run([this, context = ZuMv(context)]() mutable {
	sessionLoad_findRole(ZuMv(context));
      });
  });
}
// find and load the key for an API session
void UserDB::sessionLoad_findKey(ZuPtr<SessionLoad> context)
{
  m_keyTbl->find<1>(0, ZuFwdTuple(context->cred.p<KeyIDData>()), [
    this, context = ZuMv(context)
  ](ZmRef<ZdbObject<Key>> dbKey) mutable {
    if (!dbKey) { sessionLoaded(ZuMv(context), false); return; }
    dbKey->pin();
    context->key = ZuMv(dbKey);
    run([this, context = ZuMv(context)]() mutable {
      sessionLoad_findUserID(ZuMv(context));
    });
  });
}
// find and load the user using the userID from the API key
void UserDB::sessionLoad_findUserID(ZuPtr<SessionLoad> context)
{
  m_userTbl->find<0>(0, ZuFwdTuple(context->key->data().userID), [
    this, context = ZuMv(context)
  ](ZmRef<ZdbObject<User>> dbUser) mutable {
    if (!dbUser) { sessionLoaded(ZuMv(context), false); return; }
    dbUser->pin();
    const auto &user = dbUser->data();
    context->session = new Session{this, ZuMv(dbUser), ZuMv(context->key)};
    if (context->session->key)
      context->session->flags |= SessionFlags::Interactive();
    if (!user.roles)
      sessionLoaded(ZuMv(context), true);
    else
      run([this, context = ZuMv(context)]() mutable {
	sessionLoad_findRole(ZuMv(context));
      });
  });
}
// find and load the user's roles and permissions
void UserDB::sessionLoad_findRole(ZuPtr<SessionLoad> context)
{
  const auto &role = context->session->user->data().roles[context->roleIndex];
  m_roleTbl->find<0>(0, ZuFwdTuple(role), [
    this, context = ZuMv(context)
  ](ZmRef<ZdbObject<Role>> dbRole) mutable {
    if (dbRole) {
      const auto &role = dbRole->data();
      if (!context->key)
	context->session->perms |= role.perms;
      else
	context->session->perms |= role.apiperms;
    }
    if (++context->roleIndex < context->session->user->data().roles.length())
      run([this, context = ZuMv(context)]() mutable {
	sessionLoad_findRole(ZuMv(context));
      });
    else
      sessionLoaded(ZuMv(context), true);
  });
}
// inform app (session remains unauthenticated at this point)
void UserDB::sessionLoaded(ZuPtr<SessionLoad> context, bool ok)
{
  run([context = ZuMv(context), ok]() mutable {
    SessionFn fn = ZuMv(context->fn);
    ZmRef<Session> session = ZuMv(context->session);
    context = nullptr;
    if (!ok)
      fn(nullptr);
    else
      fn(ZuMv(session));
  });
}

// login succeeded - zero failure count and inform app
void UserDB::loginSucceeded(ZmRef<Session> session, LoginFn fn)
{
  static auto loginAck = [](Session *session) {
    const auto &user = session->user->data();
    IOBuilder fbb;
    fbb.Finish(fbs::CreateLoginAck(fbb,
      user.id, Zfb::Save::str(fbb, user.name),
      Zfb::Save::strVecIter(fbb, user.roles.length(),
	[&roles = user.roles](unsigned k) { return roles[k]; }),
      ZfbTransform::Bitmap::save(fbb, session->perms),
      user.flags, 1));
    return fbb.buf();
  };

  auto &user = session->user->data();
  if (user.failures) {
    user.failures = 0;
    run([
      this, session = ZuMv(session), fn = ZuMv(fn)
    ]() mutable {
      ZmRef<ZdbObject<User>> user = session->user;
      m_userTbl->update(ZuMv(user), [
	session = ZuMv(session), fn = ZuMv(fn)
      ](ZdbObject<User> *dbUser) mutable {
	if (dbUser) dbUser->commit();
	auto ptr = session.ptr();
	fn(ZuMv(session), loginAck(ptr));
      });
    });
  } else {
    auto ptr = session.ptr();
    fn(ZuMv(session), loginAck(ptr));
  }
}

// login failed - update user and inform app
void UserDB::loginFailed(ZmRef<Session> session, LoginFn fn)
{
  static auto loginNak = []() {
    IOBuilder fbb;
    fbs::LoginAckBuilder fbb_{fbb};
    fbb_.add_ok(0);
    fbb.Finish(fbb_.Finish());
    return fbb.buf();
  };

  if (!session) {
    fn(nullptr, loginNak());
    return;
  }
  run([this, session = ZuMv(session), fn = ZuMv(fn)]() mutable {
    ZmRef<ZdbObject<User>> dbUser = session->user;
    m_userTbl->update(ZuMv(dbUser), [fn = ZuMv(fn)](ZdbObject<User> *dbUser) {
      if (dbUser) dbUser->commit();
      fn(nullptr, loginNak());
    });
  });
}

// interactive login
void UserDB::login(
  String name, String passwd, unsigned totp, LoginFn fn)
{
  sessionLoad_login(ZuMv(name), [
    this, passwd = ZuMv(passwd), totp, fn = ZuMv(fn)
  ](ZmRef<Session> session) {
    if (!session) { loginFailed(nullptr, ZuMv(fn)); return; }
    auto &user = session->user->data();
    if (!(user.flags & UserFlags::Enabled())) {
      if (++user.failures < 3) {
	ZiLOG(Warning, "Zum", ([name = user.name](auto &s) {
	  s << "authentication failure: disabled user "
	    << ZtQuote::String{name} << " attempted login"; }));
      }
      loginFailed(ZuMv(session), ZuMv(fn));
      return;
    }
    if (!(user.flags & UserFlags::SuperUser()) &&
	!session->perms[m_perms[
	  loginReqPerm(unsigned(fbs::LoginReqData::Login))]]) {
      if (++user.failures < 3) {
	ZiLOG(Warning, "Zum", ([name = user.name](auto &s) {
	  s << "authentication failure: user without login permission "
	    << ZtQuote::String{name} << " attempted login"; }));
      }
      loginFailed(ZuMv(session), ZuMv(fn));
      return;
    }
    {
      Ztls::HMAC<KeyType> hmac;
      KeyData verify;
      hmac.start(user.secret);
      hmac.update(passwd);
      verify.length(verify.size());
      hmac.finish(verify.data());
      if (verify != user.hmac) {
	if (++user.failures < 3) {
	  ZiLOG(Warning, "Zum", ([name = user.name](auto &s) {
	    s << "authentication failure: user "
	      << ZtQuote::String{name} << " provided invalid password"; }));
	}
	loginFailed(ZuMv(session), ZuMv(fn));
	return;
      }
    }
    if (!Ztls::TOTP::verify(user.secret, totp, m_totpRange)) {
      if (++user.failures < 3) {
	ZiLOG(Warning, "Zum", ([name = user.name](auto &s) {
	  s << "authentication failure: user "
	    << ZtQuote::String{name} << " provided invalid OTP"; }));
      }
      loginFailed(ZuMv(session), ZuMv(fn));
      return;
    }
    loginSucceeded(ZuMv(session), ZuMv(fn));
  });
}

// non-interactive API access
void UserDB::access(
  KeyIDData keyID, ZtArray<uint8_t> token, int64_t stamp,
  ZtArray<uint8_t> hmac, LoginFn fn)
{
  sessionLoad_access(ZuMv(keyID), [
    this, token = ZuMv(token), stamp, hmac = ZuMv(hmac), fn = ZuMv(fn)
  ](ZmRef<Session> session) mutable {
    if (!session) { loginFailed(nullptr, ZuMv(fn)); return; }
    auto &user = session->user->data();
    if (!(user.flags & UserFlags::Enabled())) {
      if (++user.failures < 3) {
	ZiLOG(Warning, "Zum", ([name = user.name](auto &s) {
	  s << "authentication failure: disabled user "
	    << ZtQuote::String{name} << " attempted API key access"; }));
      }
      loginFailed(ZuMv(session), ZuMv(fn));
      return;
    }
    if (!(user.flags & UserFlags::SuperUser()) &&
	!session->perms[m_perms[
	  loginReqPerm(unsigned(fbs::LoginReqData::Access))]]) {
      if (++user.failures < 3) {
	ZiLOG(Warning, "Zum", ([name = user.name](auto &s) {
	  s << "authentication failure: user without API access permission "
	    << ZtQuote::String{name} << " attempted access"; }));
      }
      loginFailed(ZuMv(session), ZuMv(fn));
      return;
    }
    {
      int64_t delta = Zm::now().sec() - stamp;
      if (delta < 0) delta = -delta;
      if (delta >= m_keyInterval) {
	loginFailed(ZuMv(session), ZuMv(fn));
	return;
      }
    }
    {
      Ztls::HMAC<KeyType> hmac_;
      KeyData verify;
      hmac_.start(session->key->data().secret);
      hmac_.update(token);
      hmac_.update({
	reinterpret_cast<const uint8_t *>(&stamp),
	sizeof(int64_t)});
      verify.length(verify.size());
      hmac_.finish(verify.data());
      if (verify != hmac) {
	if (++user.failures < 3) {
	  ZiLOG(Warning, "Zum", ([name = user.name](auto &s) {
	    s << "authentication failure: user "
	      << ZtQuote::String{name}
	      << " provided invalid API key HMAC"; }));
	}
	loginFailed(ZuMv(session), ZuMv(fn));
	return;
      }
    }
    loginSucceeded(ZuMv(session), ZuMv(fn));
  });
}

// login/access request dispatch
bool UserDB::loginReq(ZmRef<ZiIOBuf> buf, LoginFn fn)
{
  if (!Zfb::Verifier{buf->data(), buf->length}.VerifyBuffer<fbs::LoginReq>())
    return false;

  auto fbLoginReq = Zfb::GetRoot<fbs::LoginReq>(buf->data());
  switch (fbLoginReq->data_type()) {
    default:
      return false;
    case fbs::LoginReqData::Access:
    case fbs::LoginReqData::Login:
      break;
  }

  run([this, buf = ZuMv(buf), fn = ZuMv(fn)]() mutable {
    loginReq_(ZuMv(buf), ZuMv(fn));
  });
  return true;
}
void UserDB::loginReq_(ZmRef<ZiIOBuf> buf, LoginFn fn)
{
  auto fbLoginReq = Zfb::GetRoot<fbs::LoginReq>(buf->data());

  switch (fbLoginReq->data_type()) {
    default: break;
    case fbs::LoginReqData::Access: {
      auto access = static_cast<const fbs::Access *>(fbLoginReq->data());
      this->access(
	Zfb::Load::str(access->keyID()),
	Zfb::Load::bytes(access->token()),
	access->stamp(),
	Zfb::Load::bytes(access->hmac()),
	ZuMv(fn));
    } break;
    case fbs::LoginReqData::Login: {
      auto login = static_cast<const fbs::Login *>(fbLoginReq->data());
      this->login(
	Zfb::Load::str(login->user()),
	Zfb::Load::str(login->passwd()),
	login->totp(),
	ZuMv(fn));
    } break;
  }
}

// respond to a request
ZmRef<ZiIOBuf> UserDB::respond(
  Zfb::IOBuilder &fbb, SeqNo seqNo,
  fbs::ReqAckData ackType, Offset<void> ackData)
{
  fbs::ReqAckBuilder fbb_(fbb);
  fbb_.add_seqNo(seqNo);
  fbb_.add_data_type(ackType);
  fbb_.add_data(ackData);
  fbb.Finish(fbb_.Finish());
  return fbb.buf();
}

// reject a request
ZmRef<ZiIOBuf> UserDB::reject(SeqNo seqNo, unsigned rejCode, String text)
{
  IOBuilder fbb;
  auto text_ = Zfb::Save::str(fbb, text);
  fbs::ReqAckBuilder fbb_(fbb);
  fbb_.add_seqNo(seqNo);
  fbb_.add_rejCode(rejCode);
  fbb_.add_rejText(text_);
  fbb.Finish(fbb_.Finish());
  return fbb.buf();
}

// validate, permission check and dispatch a request
bool UserDB::request(ZmRef<Session> session, ZmRef<ZiIOBuf> buf, ResponseFn fn)
{
  if (!Zfb::Verifier{buf->data(), buf->length}.VerifyBuffer<fbs::Request>())
    return false;

  run([
    this, session = ZuMv(session), buf = ZuMv(buf), fn = ZuMv(fn)
  ]() mutable {
    request_(ZuMv(session), ZuMv(buf), ZuMv(fn));
  });
  return true;
}
void UserDB::request_(
  ZmRef<Session> session, ZmRef<ZiIOBuf> buf, ResponseFn fn)
{
  auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
  auto reqType = unsigned(fbRequest->data_type());

  if (ZuUnlikely(!ok(session, m_perms[reqPerm(reqType)]))) {
    String text = "permission denied";
    if (session->user->data().flags & UserFlags::ChPass())
      text << " (user must change password)\n";
    fn(reject(fbRequest->seqNo(), __LINE__, ZuMv(text)));
    return;
  }

  switch (reqType) {
    case int(fbs::ReqData::ChPass):
      chPass(ZuMv(session), ZuMv(buf), ZuMv(fn));
      break;

    case int(fbs::ReqData::OwnKeyGet):
      ownKeyGet(ZuMv(session), ZuMv(buf), ZuMv(fn));
      break;
    case int(fbs::ReqData::OwnKeyAdd):
      ownKeyAdd(ZuMv(session), ZuMv(buf), ZuMv(fn));
      break;
    case int(fbs::ReqData::OwnKeyClr):
      ownKeyClr(ZuMv(session), ZuMv(buf), ZuMv(fn));
      break;
    case int(fbs::ReqData::OwnKeyDel):
      ownKeyDel(ZuMv(session), ZuMv(buf), ZuMv(fn));
      break;

    case int(fbs::ReqData::UserGet): userGet(ZuMv(buf), ZuMv(fn)); break;
    case int(fbs::ReqData::UserAdd): userAdd(ZuMv(buf), ZuMv(fn)); break;
    case int(fbs::ReqData::ResetPass): resetPass(ZuMv(buf), ZuMv(fn)); break;
    case int(fbs::ReqData::UserMod): userMod(ZuMv(buf), ZuMv(fn)); break;
    case int(fbs::ReqData::UserDel): userDel(ZuMv(buf), ZuMv(fn)); break;

    case int(fbs::ReqData::RoleGet): roleGet(ZuMv(buf), ZuMv(fn)); break;
    case int(fbs::ReqData::RoleAdd): roleAdd(ZuMv(buf), ZuMv(fn)); break;
    case int(fbs::ReqData::RoleMod): roleMod(ZuMv(buf), ZuMv(fn)); break;
    case int(fbs::ReqData::RoleDel): roleDel(ZuMv(buf), ZuMv(fn)); break;

    case int(fbs::ReqData::PermGet): permGet(ZuMv(buf), ZuMv(fn)); break;
    case int(fbs::ReqData::PermAdd): permAdd(ZuMv(buf), ZuMv(fn)); break;
    case int(fbs::ReqData::PermMod): permMod(ZuMv(buf), ZuMv(fn)); break;
    case int(fbs::ReqData::PermDel): permDel(ZuMv(buf), ZuMv(fn)); break;

    case int(fbs::ReqData::KeyGet): keyGet(ZuMv(buf), ZuMv(fn)); break;
    case int(fbs::ReqData::KeyAdd): keyAdd(ZuMv(buf), ZuMv(fn)); break;
    case int(fbs::ReqData::KeyClr): keyClr(ZuMv(buf), ZuMv(fn)); break;
    case int(fbs::ReqData::KeyDel): keyDel(ZuMv(buf), ZuMv(fn)); break;
  }
}

// change password
void UserDB::chPass(ZmRef<Session> session, ZmRef<ZiIOBuf> buf, ResponseFn fn)
{
  auto &user = session->user->data();
  auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
  auto chPass = static_cast<const fbs::UserChPass *>(fbRequest->data());
  ZuCSpan oldPass = Zfb::Load::str(chPass->oldpass());
  ZuCSpan newPass = Zfb::Load::str(chPass->newpass());
  // verify old password
  Ztls::HMAC<KeyType> hmac;
  KeyData verify;
  hmac.start(user.secret);
  hmac.update(oldPass);
  verify.length(verify.size());
  hmac.finish(verify.data());
  if (verify != user.hmac) {
    fn(reject(fbRequest->seqNo(), __LINE__, String{}
	<< "old password did not match"));
    return;
  }
  // clear change password flag and update user with new HMAC
  user.flags &= ~UserFlags::ChPass();
  hmac.reset();
  hmac.update(newPass);
  hmac.finish(user.hmac.data());
  run([
    this, seqNo = fbRequest->seqNo(), session = ZuMv(session), fn = ZuMv(fn)
  ]() mutable {
    ZdbObjRef<User> dbUser = session->user;
    m_userTbl->update(ZuMv(dbUser), [
      this, seqNo, fn = ZuMv(fn)
    ](ZdbObject<User> *dbUser) {
      if (dbUser) dbUser->commit();
      IOBuilder fbb;
      auto ackData = fbs::CreateAck(fbb);
      fn(respond(fbb, seqNo, fbs::ReqAckData::ChPass, ackData.Union()));
    });
  });
}

// query users
void UserDB::userGet(ZmRef<ZiIOBuf> buf, ResponseFn fn)
{
  auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
  auto query = static_cast<const fbs::UserQuery *>(fbRequest->data());
  if (Zfb::IsFieldPresent(query, fbs::UserQuery::VT_USERKEY_TYPE)) {
    if (query->userKey_type() != fbs::UserKey::ID &&
	query->userKey_type() != fbs::UserKey::Name) {
      fn(reject(fbRequest->seqNo(), __LINE__, String{}
	  << "unknown query key type (" << int(query->userKey_type()) << ')'));
      return;
    }
  }
  if (query->limit() > MaxQueryLimit) {
    fn(reject(fbRequest->seqNo(), __LINE__, String{}
	<< "maximum query limit exceeded ("
	<< query->limit() << " > " << MaxQueryLimit << ')'));
    return;
  }
  run([this, buf = ZuMv(buf), fn = ZuMv(fn)]() mutable {
    auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
    auto query = static_cast<const fbs::UserQuery *>(fbRequest->data());
    auto tupleFn = [
      this,
      seqNo = fbRequest->seqNo(),
      fbb_ = ZuPtr<IOBuilder>{new IOBuilder{}},
      offsets = ZtArray<Offset<fbs::User>>(query->limit()),
      fn = ZuMv(fn)
    ](auto result, unsigned) mutable {
      auto &fbb = *fbb_;
      using Row = ZuStructTuple<User>;
      if (result.template is<Row>()) {
	offsets.push(ZfbStruct::save(fbb, result.template p<Row>()));
      } else {
	auto ackData = fbs::CreateUserList(fbb,
	  fbb.CreateVector(&offsets[0], offsets.length()));
	fn(respond(fbb, seqNo, fbs::ReqAckData::UserGet, ackData.Union()));
      }
    };
    if (!Zfb::IsFieldPresent(query, fbs::UserQuery::VT_USERKEY_TYPE)) {
      m_userTbl->selectRows<0>(
	ZuTuple<>{},
	query->limit(),
	ZuMv(tupleFn));
    } else if (query->userKey_type() == fbs::UserKey::ID) {
      auto userID = static_cast<const fbs::UserID *>(query->userKey())->id();
      m_userTbl->nextRows<0>(
	ZuMvTuple(ZuMv(userID)),
	query->inclusive(),
	query->limit(),
	ZuMv(tupleFn));
    } else {
      auto userName = Zfb::Load::str(
	static_cast<const fbs::UserName *>(query->userKey())->name());
      m_userTbl->nextRows<1>(
	ZuMvTuple(ZuMv(userName)),
	query->inclusive(),
	query->limit(),
	ZuMv(tupleFn));
    }
  });
}

// add a new user
void UserDB::userAdd(ZmRef<ZiIOBuf> buf, ResponseFn fn)
{
  run([this, buf = ZuMv(buf), fn = ZuMv(fn)]() mutable {
    auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
    auto fbUser = static_cast<const fbs::User *>(fbRequest->data());
    m_userTbl->find<1>(0, ZuMvTuple(Zfb::Load::str(fbUser->name())), [
      this, buf = ZuMv(buf), fn = ZuMv(fn)
    ](ZdbObjRef<User> dbUser) mutable {
      if (dbUser) {
	auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
	auto fbUser = static_cast<const fbs::User *>(fbRequest->data());
	auto userName = Zfb::Load::str(fbUser->name());
	fn(reject(fbRequest->seqNo(), __LINE__, String{}
	    << "user " << ZtQuote::String{userName} << " already exists"));
	return;
      }
      dbUser = new ZdbObject<User>{m_userTbl, 0};
      m_userTbl->insert(dbUser, [
	this, buf = ZuMv(buf), fn = ZuMv(fn)
      ](ZdbObject<User> *dbUser) mutable {
	auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
	auto fbUser = static_cast<const fbs::User *>(fbRequest->data());
	auto userName = Zfb::Load::str(fbUser->name());
	if (!dbUser) {
	  fn(reject(fbRequest->seqNo(), __LINE__, String{}
	      << "user " << ZtQuote::String{userName} << " insert failed"));
	  return;
	}
	StringVec roles;
	roles.size(fbUser->roles()->size());
	Zfb::Load::all(fbUser->roles(),
	  [&roles](unsigned, const Zfb::String *role) {
	    roles.push(Zfb::Load::str(role));
	  });
	String passwd;
	initUser(
	  dbUser, m_nextUserID++, userName,
	  ZuMv(roles), fbUser->flags(), passwd);
	auto &user = dbUser->data();
	IOBuilder fbb;
	auto ackData = fbs::CreateUserPass(fbb,
	  ZfbStruct::save(fbb, user), Zfb::Save::str(fbb, passwd));
	fn(respond(
	    fbb, fbRequest->seqNo(),
	    fbs::ReqAckData::UserAdd, ackData.Union()));
      });
    });
  });
}

// delete all API keys for a user
template <typename L>
void UserDB::keyClr__(UserID id, L &&l)
{
  run([this, id, l = ZuFwd<L>(l)]() {
    m_keyTbl->selectKeys<0>(ZuMvTuple(ZuMv(id)), MaxAPIKeys, [
      this, l = ZuMv(l)
    ](auto result, unsigned) mutable {
      using KeyID = ZuStructKeyT<Key>;
      if (result.template is<KeyID>()) {
	run([this, id = ZuMv(result).template p<KeyID>()]() mutable {
	  m_keyTbl->findDel<1>(
	    0, ZuMvTuple(ZuMv(id).template p<1>()),
	    [](ZdbObject<Key> *dbKey) mutable {
	      if (dbKey) dbKey->commit();
	    });
	});
	return;
      }
      // EOR - serialize the completion callback after the key deletions
      run([l = ZuMv(l)]() { l(); });
    });
  });
}

// reset password (also clears all API keys)
void UserDB::resetPass(ZmRef<ZiIOBuf> buf, ResponseFn fn)
{
  auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
  auto userID = static_cast<const fbs::UserID *>(fbRequest->data());
  run([
    this, seqNo = fbRequest->seqNo(), id = userID->id(), fn = ZuMv(fn)
  ]() mutable {
    m_userTbl->findUpd<0>(0, ZuMvTuple(ZuMv(id)), [
      this, seqNo, id, fn = ZuMv(fn)
    ](ZdbObjRef<User> dbUser) mutable {
      if (!dbUser) {
	fn(reject(seqNo, __LINE__, String{}
	    << "user ID " << id << " not found"));
	return;
      }
      auto &user = dbUser->data();
      String passwd;
      {
	KeyData passwd_;
	unsigned passLen_ = ZuBase64::declen(m_passLen);
	if (passLen_ > passwd_.size()) passLen_ = passwd_.size();
	passwd_.length(passLen_);
	m_rng->random(passwd_);
	passwd.length(m_passLen);
	passwd.length(ZuBase64::encode(passwd, passwd_));
      }
      {
	Ztls::HMAC<KeyType> hmac;
	hmac.start(user.secret);
	hmac.update(passwd);
	user.hmac.length(user.hmac.size());
	hmac.finish(user.hmac.data());
      }
      dbUser->commit();
      keyClr__(id, [
	this, seqNo, dbUser = ZuMv(dbUser),
	passwd = ZuMv(passwd), fn = ZuMv(fn)
      ]() {
	const auto &user = dbUser->data();
	IOBuilder fbb;
	auto ackData = fbs::CreateUserPass(fbb,
	  ZfbStruct::save(fbb, user), Zfb::Save::str(fbb, passwd));
	fn(respond(fbb, seqNo, fbs::ReqAckData::ResetPass, ackData.Union()));
      });
    });
  });
}

// modify user (name, roles, flags)
void UserDB::userMod(ZmRef<ZiIOBuf> buf, ResponseFn fn)
{
  run([this, buf = ZuMv(buf), fn = ZuMv(fn)]() mutable {
    auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
    auto fbUser = static_cast<const fbs::User *>(fbRequest->data());

    auto updateFn = [
      this, buf = ZuMv(buf), fn = ZuMv(fn)
    ](ZdbObjRef<User> dbUser) mutable {
      auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
      auto fbUser = static_cast<const fbs::User *>(fbRequest->data());
      if (!dbUser) {
	fn(reject(fbRequest->seqNo(), __LINE__, String{}
	    << "user ID " << fbUser->id() << " not found"));
	return;
      }
      auto &user = dbUser->data();
      if (user.flags & UserFlags::Immutable()) {
	fn(reject(fbRequest->seqNo(), __LINE__, String{}
	    << "user ID " << fbUser->id() << " is immutable"));
	return;
      }
      if (Zfb::IsFieldPresent(fbUser, fbs::User::VT_NAME))
	user.name = Zfb::Load::str(fbUser->name());
      if (Zfb::IsFieldPresent(fbUser, fbs::User::VT_ROLES)) {
	user.roles.length(0);
	user.roles.size(fbUser->roles()->size());
	Zfb::Load::all(fbUser->roles(),
	  [&user](unsigned, const Zfb::String *role) {
	    user.roles.push(Zfb::Load::str(role));
	  });
      }
      if (Zfb::IsFieldPresent(fbUser, fbs::User::VT_FLAGS))
	user.flags = fbUser->flags();
      dbUser->commit();
      IOBuilder fbb;
      auto fbName = Zfb::Save::str(fbb, user.name);
      auto fbRoles = Zfb::Save::strVecIter(fbb, user.roles.length(),
	[&user](unsigned i) { return user.roles[i]; });
      fbs::UserBuilder fbb_{fbb};
      fbb_.add_id(user.id);
      fbb_.add_name(fbName);
      fbb_.add_roles(fbRoles);
      fbb_.add_flags(user.flags);
      fn(respond(
	  fbb, fbRequest->seqNo(),
	  fbs::ReqAckData::UserMod, fbb_.Finish().Union()));
    };
    if (Zfb::Load::str(fbUser->name())) 
      m_userTbl->findUpd<0, ZuSeq<1>>(
	0, ZuMvTuple(fbUser->id()), ZuMv(updateFn));
    else
      m_userTbl->findUpd<0>(0, ZuMvTuple(fbUser->id()), ZuMv(updateFn));
  });
}

// delete user (and associated API keys)
void UserDB::userDel(ZmRef<ZiIOBuf> buf, ResponseFn fn)
{
  run([this, buf = ZuMv(buf), fn = ZuMv(fn)]() mutable {
    auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
    auto fbUser = static_cast<const fbs::UserID *>(fbRequest->data());
    m_userTbl->findDel<0>(0, ZuMvTuple(fbUser->id()), [
      this, buf = ZuMv(buf), fn = ZuMv(fn)
    ](ZdbObjRef<User> dbUser) mutable {
      auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
      auto fbUser = static_cast<const fbs::User *>(fbRequest->data());
      if (!dbUser) {
	fn(reject(fbRequest->seqNo(), __LINE__, String{}
	    << "user ID " << fbUser->id() << " not found"));
	return;
      }
      dbUser->commit();
      keyClr__(fbUser->id(), [
	this, seqNo = fbRequest->seqNo(), dbUser = ZuMv(dbUser), fn = ZuMv(fn)
      ]() {
	const auto &user = dbUser->data();
	IOBuilder fbb;
	auto fbName = Zfb::Save::str(fbb, user.name);
	auto fbRoles = Zfb::Save::strVecIter(fbb, user.roles.length(),
	  [&user](unsigned i) { return user.roles[i]; });
	fbs::UserBuilder fbb_{fbb};
	fbb_.add_id(user.id);
	fbb_.add_name(fbName);
	fbb_.add_roles(fbRoles);
	fbb_.add_flags(user.flags);
	fn(respond(
	    fbb, seqNo,
	    fbs::ReqAckData::UserDel, fbb_.Finish().Union()));
      });
    });
  });
}

// query roles
void UserDB::roleGet(ZmRef<ZiIOBuf> buf, ResponseFn fn)
{
  auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
  auto query = static_cast<const fbs::RoleQuery *>(fbRequest->data());
  if (query->limit() > MaxQueryLimit) {
    fn(reject(fbRequest->seqNo(), __LINE__, String{}
	<< "maximum query limit exceeded ("
	<< query->limit() << " > " << MaxQueryLimit << ')'));
    return;
  }
  run([this, buf = ZuMv(buf), fn = ZuMv(fn)]() mutable {
    auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
    auto query = static_cast<const fbs::RoleQuery *>(fbRequest->data());
    auto tupleFn = [
      this,
      seqNo = fbRequest->seqNo(),
      fbb_ = ZuPtr<IOBuilder>{new IOBuilder{}},
      offsets = ZtArray<Offset<fbs::Role>>(query->limit()),
      fn = ZuMv(fn)
    ](auto result, unsigned) mutable {
      auto &fbb = *fbb_;
      using Row = ZuStructTuple<Role>;
      if (result.template is<Row>()) {
	offsets.push(ZfbStruct::save(fbb, result.template p<Row>()));
      } else {
	auto ackData = fbs::CreateRoleList(fbb,
	  fbb.CreateVector(&offsets[0], offsets.length()));
	fn(respond(fbb, seqNo, fbs::ReqAckData::RoleGet, ackData.Union()));
      }
    };
    if (!Zfb::IsFieldPresent(query, fbs::RoleQuery::VT_ROLEKEY)) {
      m_roleTbl->selectRows<0>(
	ZuTuple<>{},
	query->limit(),
	ZuMv(tupleFn));
    } else {
      m_roleTbl->nextRows<0>(
	ZuMvTuple(Zfb::Load::str(query->roleKey())),
	query->inclusive(),
	query->limit(),
	ZuMv(tupleFn));
    }
  });
}

// add new role
void UserDB::roleAdd(ZmRef<ZiIOBuf> buf, ResponseFn fn)
{
  run([this, buf = ZuMv(buf), fn = ZuMv(fn)]() mutable {
    auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
    auto fbRole = static_cast<const fbs::Role *>(fbRequest->data());
    auto roleName = Zfb::Load::str(fbRole->name());
    m_roleTbl->find<0>(0, ZuMvTuple(ZuMv(roleName)), [
      this, buf = ZuMv(buf), fn = ZuMv(fn)
    ](ZdbObjRef<Role> dbRole) mutable {
      if (dbRole) {
	auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
	auto fbRole = static_cast<const fbs::Role *>(fbRequest->data());
	auto roleName = Zfb::Load::str(fbRole->name());
	fn(reject(fbRequest->seqNo(), __LINE__, String{}
	    << "role " << ZtQuote::String{roleName} << " already exists"));
	return;
      }
      dbRole = new ZdbObject<Role>{m_roleTbl, 0};
      m_roleTbl->insert(dbRole, [
	this, buf = ZuMv(buf), fn = ZuMv(fn)
      ](ZdbObject<Role> *dbRole) mutable {
	auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
	auto fbRole = static_cast<const fbs::Role *>(fbRequest->data());
	auto roleName = Zfb::Load::str(fbRole->name());
	if (!dbRole) {
	  fn(reject(fbRequest->seqNo(), __LINE__, String{}
	      << "role " << ZtQuote::String{roleName} << " insert failed"));
	  return;
	}
	initRole(dbRole, ZuMv(roleName),
	  ZfbTransform::Bitmap::load<ZtBitmap>(fbRole->perms()),
	  ZfbTransform::Bitmap::load<ZtBitmap>(fbRole->apiperms()),
	  fbRole->flags());
	dbRole->commit();
	IOBuilder fbb;
	fn(respond(
	    fbb, fbRequest->seqNo(),
	    fbs::ReqAckData::RoleAdd,
	    ZfbStruct::save(fbb, dbRole->data()).Union()));
      });
    });
  });
}

// modify role (name, perms, apiperms, flags)
void UserDB::roleMod(ZmRef<ZiIOBuf> buf, ResponseFn fn)
{
  run([this, buf = ZuMv(buf), fn = ZuMv(fn)]() mutable {
    auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
    auto fbRole = static_cast<const fbs::Role *>(fbRequest->data());
    m_roleTbl->findUpd<0>(0, ZuMvTuple(Zfb::Load::str(fbRole->name())), [
      this, buf = ZuMv(buf), fn = ZuMv(fn)
    ](ZdbObjRef<Role> dbRole) mutable {
      auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
      auto fbRole = static_cast<const fbs::Role *>(fbRequest->data());
      auto roleName = Zfb::Load::str(fbRole->name());
      if (!dbRole) {
	fn(reject(fbRequest->seqNo(), __LINE__, String{}
	    << "role " << ZtQuote::String{roleName} << " not found"));
	return;
      }
      auto &role = dbRole->data();
      if (role.flags & RoleFlags::Immutable()) {
	fn(reject(fbRequest->seqNo(), __LINE__, String{}
	    << "role " << ZtQuote::String{roleName} << " is immutable"));
	return;
      }
      if (Zfb::IsFieldPresent(fbRole, fbs::Role::VT_PERMS))
	role.perms = ZfbTransform::Bitmap::load<ZtBitmap>(fbRole->perms());
      if (Zfb::IsFieldPresent(fbRole, fbs::Role::VT_APIPERMS))
	role.apiperms = ZfbTransform::Bitmap::load<ZtBitmap>(fbRole->apiperms());
      if (Zfb::IsFieldPresent(fbRole, fbs::Role::VT_FLAGS))
	role.flags = fbRole->flags();
      dbRole->commit();
      IOBuilder fbb;
      fn(respond(
	  fbb, fbRequest->seqNo(),
	  fbs::ReqAckData::RoleMod,
	  ZfbStruct::save(fbb, dbRole->data()).Union()));
    });
  });
}

// delete role
void UserDB::roleDel(ZmRef<ZiIOBuf> buf, ResponseFn fn)
{
  run([this, buf = ZuMv(buf), fn = ZuMv(fn)]() mutable {
    auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
    auto fbRole = static_cast<const fbs::RoleID *>(fbRequest->data());
    auto roleName = Zfb::Load::str(fbRole->name());
    m_roleTbl->findDel<0>(0, ZuMvTuple(ZuMv(roleName)), [
      this, buf = ZuMv(buf), fn = ZuMv(fn)
    ](ZdbObjRef<Role> dbRole) mutable {
      auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
      if (!dbRole) {
	auto fbRole = static_cast<const fbs::RoleID *>(fbRequest->data());
	auto roleName = Zfb::Load::str(fbRole->name());
	fn(reject(fbRequest->seqNo(), __LINE__, String{}
	    << "role " << ZtQuote::String{roleName} << " not found"));
	return;
      }
      dbRole->commit();
      IOBuilder fbb;
      fn(respond(
	  fbb, fbRequest->seqNo(),
	  fbs::ReqAckData::RoleDel,
	  ZfbStruct::save(fbb, dbRole->data()).Union()));
    });
  });
}

// query permissions
void UserDB::permGet(ZmRef<ZiIOBuf> buf, ResponseFn fn)
{
  auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
  auto query = static_cast<const fbs::PermQuery *>(fbRequest->data());
  if (Zfb::IsFieldPresent(query, fbs::PermQuery::VT_PERMKEY_TYPE)) {
    if (query->permKey_type() != fbs::PermKey::ID &&
	query->permKey_type() != fbs::PermKey::Name) {
      fn(reject(fbRequest->seqNo(), __LINE__, String{}
	  << "unknown query key type (" << int(query->permKey_type()) << ')'));
      return;
    }
  }
  if (query->limit() > MaxQueryLimit) {
    fn(reject(fbRequest->seqNo(), __LINE__, String{}
	<< "maximum query limit exceeded ("
	<< query->limit() << " > " << MaxQueryLimit << ')'));
    return;
  }
  run([this, buf = ZuMv(buf), fn = ZuMv(fn)]() mutable {
    auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
    auto query = static_cast<const fbs::PermQuery *>(fbRequest->data());
    auto tupleFn = [
      this,
      seqNo = fbRequest->seqNo(),
      fbb_ = ZuPtr<IOBuilder>{new IOBuilder{}},
      offsets = ZtArray<Offset<fbs::Perm>>(query->limit()),
      fn = ZuMv(fn)
    ](auto result, unsigned) mutable {
      auto &fbb = *fbb_;
      using Row = ZuStructTuple<Perm>;
      if (result.template is<Row>()) {
	offsets.push(ZfbStruct::save(fbb, result.template p<Row>()));
      } else {
	auto ackData = fbs::CreatePermList(fbb,
	  fbb.CreateVector(&offsets[0], offsets.length()));
	fn(respond(fbb, seqNo, fbs::ReqAckData::PermGet, ackData.Union()));
      }
    };
    if (!Zfb::IsFieldPresent(query, fbs::PermQuery::VT_PERMKEY_TYPE)) {
      m_permTbl->selectRows<0>(
	ZuTuple<>{},
	query->limit(),
	ZuMv(tupleFn));
    } else if (query->permKey_type() == fbs::PermKey::ID) {
      auto permID = static_cast<const fbs::PermID *>(query->permKey())->id();
      m_permTbl->nextRows<0>(
	ZuMvTuple(ZuMv(permID)),
	query->inclusive(),
	query->limit(),
	ZuMv(tupleFn));
    } else {
      auto permName = Zfb::Load::str(
	static_cast<const fbs::PermName *>(query->permKey())->name());
      m_permTbl->nextRows<1>(
	ZuMvTuple(ZuMv(permName)),
	query->inclusive(),
	query->limit(),
	ZuMv(tupleFn));
    }
  });
}

// add new permission
void UserDB::permAdd(ZmRef<ZiIOBuf> buf, ResponseFn fn)
{
  run([this, buf = ZuMv(buf), fn = ZuMv(fn)]() mutable {
    auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
    auto fbPerm = static_cast<const fbs::PermName *>(fbRequest->data());
    m_permTbl->find<1>(0, ZuMvTuple(Zfb::Load::str(fbPerm->name())), [
      this, buf = ZuMv(buf), fn = ZuMv(fn)
    ](ZdbObjRef<Perm> dbPerm) mutable {
      if (dbPerm) {
	auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
	auto fbPerm = static_cast<const fbs::Perm *>(fbRequest->data());
	auto permName = Zfb::Load::str(fbPerm->name());
	fn(reject(fbRequest->seqNo(), __LINE__, String{}
	    << "perm " << ZtQuote::String{permName} << " already exists"));
	return;
      }
      dbPerm = new ZdbObject<Perm>{m_permTbl, 0};
      m_permTbl->insert(dbPerm, [
	this, buf = ZuMv(buf), fn = ZuMv(fn)
      ](ZdbObject<Perm> *dbPerm) mutable {
	auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
	auto fbPerm = static_cast<const fbs::PermName *>(fbRequest->data());
	auto permName = Zfb::Load::str(fbPerm->name());
	if (!dbPerm) {
	  fn(reject(fbRequest->seqNo(), __LINE__, String{}
	      << "perm " << ZtQuote::String{permName} << " insert failed"));
	  return;
	}
	initPerm(dbPerm, permName);
	IOBuilder fbb;
	auto ackData = fbs::CreatePermID(fbb, dbPerm->data().id);
	fn(respond(
	    fbb, fbRequest->seqNo(),
	    fbs::ReqAckData::PermAdd, ackData.Union()));
      });
    });
  });
}

// modify permission (name)
void UserDB::permMod(ZmRef<ZiIOBuf> buf, ResponseFn fn)
{
  run([this, buf = ZuMv(buf), fn = ZuMv(fn)]() mutable {
    auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
    auto fbPerm = static_cast<const fbs::Perm *>(fbRequest->data());
    m_permTbl->findUpd<0, ZuSeq<1>>(0, ZuMvTuple(fbPerm->id()), [
      this, buf = ZuMv(buf), fn = ZuMv(fn)
    ](ZdbObjRef<Perm> dbPerm) mutable {
      auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
      auto fbPerm = static_cast<const fbs::Perm *>(fbRequest->data());
      if (!dbPerm) {
	fn(reject(fbRequest->seqNo(), __LINE__, String{}
	    << "perm ID " << fbPerm->id() << " not found"));
	return;
      }
      auto &perm = dbPerm->data();
      perm.name = Zfb::Load::str(fbPerm->name());
      dbPerm->commit();
      IOBuilder fbb;
      auto ackData = fbs::CreateAck(fbb);
      fn(respond(
	  fbb, fbRequest->seqNo(),
	  fbs::ReqAckData::PermMod, ackData.Union()));
    });
  });
}

// delete permission
void UserDB::permDel(ZmRef<ZiIOBuf> buf, ResponseFn fn)
{
  run([this, buf = ZuMv(buf), fn = ZuMv(fn)]() mutable {
    auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
    auto fbPerm = static_cast<const fbs::PermID *>(fbRequest->data());
    m_permTbl->findDel<0>(0, ZuMvTuple(fbPerm->id()), [
      this, buf = ZuMv(buf), fn = ZuMv(fn)
    ](ZdbObjRef<Perm> dbPerm) mutable {
      auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
      if (!dbPerm) {
	auto fbPerm = static_cast<const fbs::PermID *>(fbRequest->data());
	fn(reject(fbRequest->seqNo(), __LINE__, String{}
	    << "perm ID " << fbPerm->id() << " not found"));
	return;
      }
      dbPerm->commit();
      IOBuilder fbb;
      auto ackData = fbs::CreateAck(fbb);
      fn(respond(
	  fbb, fbRequest->seqNo(),
	  fbs::ReqAckData::PermMod, ackData.Union()));
    });
  });
}

// query keys
void UserDB::ownKeyGet(
  ZmRef<Session> session, ZmRef<ZiIOBuf> buf, ResponseFn fn)
{
  auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
  keyGet_(
    fbRequest->seqNo(), session->user->data().id,
    fbs::ReqAckData::OwnKeyGet, ZuMv(fn));
}
void UserDB::keyGet(ZmRef<ZiIOBuf> buf, ResponseFn fn)
{
  auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
  auto query = static_cast<const fbs::UserID *>(fbRequest->data());
  keyGet_(fbRequest->seqNo(), query->id(), fbs::ReqAckData::KeyGet, ZuMv(fn));
}
void UserDB::keyGet_(
  SeqNo seqNo, UserID userID, fbs::ReqAckData ackType, ResponseFn fn)
{
  run([this, seqNo, userID, ackType, fn = ZuMv(fn)]() mutable {
    m_keyTbl->selectKeys<0>(ZuMvTuple(ZuMv(userID)), MaxAPIKeys, [
      this, seqNo,
      fbb_ = ZuPtr<IOBuilder>{new IOBuilder{}},
      offsets = ZtArray<Offset<Zfb::Bytes>>(MaxAPIKeys),
      ackType, fn = ZuMv(fn)
    ](auto result, unsigned) mutable {
      auto &fbb = *fbb_;
      using KeyID = ZuStructKeyT<Key>;
      if (result.template is<KeyID>()) {
	offsets.push(Zfb::CreateBytes(fbb, Zfb::Save::bytes(fbb,
	      result.template p<KeyID>().template p<1>())));
      } else {
	auto ackData = fbs::CreateKeyIDList(fbb,
	  fbb.CreateVector(&offsets[0], offsets.length()));
	fn(respond(fbb, seqNo, ackType, ackData.Union()));
      }
    });
  });
}

// add key
void UserDB::ownKeyAdd(
  ZmRef<Session> session, ZmRef<ZiIOBuf> buf, ResponseFn fn)
{
  auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
  keyAdd_(
    fbRequest->seqNo(), session->user->data().id,
    fbs::ReqAckData::OwnKeyAdd, ZuMv(fn));
}
void UserDB::keyAdd(ZmRef<ZiIOBuf> buf, ResponseFn fn)
{
  auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
  auto fbUserID = static_cast<const fbs::UserID *>(fbRequest->data());
  keyAdd_(
    fbRequest->seqNo(), fbUserID->id(), fbs::ReqAckData::KeyAdd, ZuMv(fn));
}
void UserDB::keyAdd_(
  SeqNo seqNo, UserID userID, fbs::ReqAckData ackType, ResponseFn fn)
{
  run([this, seqNo, userID, ackType, fn = ZuMv(fn)]() mutable {
    // generate random key ID
    KeyIDData keyID;
    keyID.length(keyID.size());
    m_rng->random(keyID);
    m_keyTbl->find<1>(0, ZuFwdTuple(keyID), [
      this, seqNo, userID, keyID, ackType, fn = ZuMv(fn)
    ](ZdbObjRef<Key> dbKey) mutable {
      if (dbKey) {
	// key ID collision - regenerate and retry
	run([this, seqNo, userID, ackType, fn = ZuMv(fn)]() mutable {
	  keyAdd_(seqNo, userID, ackType, ZuMv(fn));
	});
	return;
      }
      dbKey = new ZdbObject<Key>{m_keyTbl, 0};
      m_keyTbl->insert(dbKey, [
	this, seqNo, userID, keyID, ackType, fn = ZuMv(fn)
      ](ZdbObject<Key> *dbKey) mutable {
	if (!dbKey) {
	  fn(reject(seqNo, __LINE__, String{}
	      << "key insert failed for user ID " << userID));
	  return;
	}
	initKey(dbKey, userID, keyID);
	IOBuilder fbb;
	auto ackData = ZfbStruct::save(fbb, dbKey->data());
	fn(respond(fbb, seqNo, ackType, ackData.Union()));
      });
    });
  });
}

// clear keys
void UserDB::ownKeyClr(
  ZmRef<Session> session, ZmRef<ZiIOBuf> buf, ResponseFn fn)
{
  auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
  keyClr_(
    fbRequest->seqNo(), session->user->data().id,
    fbs::ReqAckData::OwnKeyClr, ZuMv(fn));
}
void UserDB::keyClr(ZmRef<ZiIOBuf> buf, ResponseFn fn)
{
  auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
  auto fbUserID = static_cast<const fbs::UserID *>(fbRequest->data());
  keyClr_(
    fbRequest->seqNo(), fbUserID->id(), fbs::ReqAckData::KeyClr, ZuMv(fn));
}
void UserDB::keyClr_(
  SeqNo seqNo, UserID userID, fbs::ReqAckData ackType, ResponseFn fn)
{
  run([this, seqNo, userID, ackType, fn = ZuMv(fn)]() mutable {
    keyClr__(userID, [this, seqNo, ackType, fn = ZuMv(fn)]() {
      IOBuilder fbb;
      auto ackData = fbs::CreateAck(fbb);
      fn(respond(fbb, seqNo, ackType, ackData.Union()));
    });
  });
}

// delete key
void UserDB::ownKeyDel(
  ZmRef<Session> session, ZmRef<ZiIOBuf> buf, ResponseFn fn)
{
  auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
  auto fbKeyID = static_cast<const fbs::KeyID *>(fbRequest->data());
  auto userID = session->user->data().id;
  auto keyID = Zfb::Load::bytes(fbKeyID->id());
  m_keyTbl->findDel<0>(0, ZuMvTuple(ZuMv(userID), ZuMv(keyID)), [
      this, buf = ZuMv(buf), fn = ZuMv(fn)
    ](ZdbObjRef<Key> dbKey) mutable {
      auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
      auto fbKeyID = static_cast<const fbs::KeyID *>(fbRequest->data());
      auto keyID = Zfb::Load::bytes(fbKeyID->id());
      if (!dbKey) {
	fn(reject(fbRequest->seqNo(), __LINE__, String{}
	    << "key " << ZtQuote::Base64{keyID} << " not found"));
	return;
      }
      dbKey->commit();
      IOBuilder fbb;
      auto ackData = ZfbStruct::save(fbb, dbKey->data());
      fn(respond(fbb, fbRequest->seqNo(),
	  fbs::ReqAckData::OwnKeyDel, ackData.Union()));
    });
}
void UserDB::keyDel(ZmRef<ZiIOBuf> buf, ResponseFn fn)
{
  auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
  auto fbKeyID = static_cast<const fbs::KeyID *>(fbRequest->data());
  auto keyID = Zfb::Load::bytes(fbKeyID->id());
  m_keyTbl->findDel<1>(0, ZuMvTuple(ZuMv(keyID)), [
      this, buf = ZuMv(buf), fn = ZuMv(fn)
    ](ZdbObjRef<Key> dbKey) mutable {
      auto fbRequest = Zfb::GetRoot<fbs::Request>(buf->data());
      auto fbKeyID = static_cast<const fbs::KeyID *>(fbRequest->data());
      auto keyID = Zfb::Load::bytes(fbKeyID->id());
      if (!dbKey) {
	fn(reject(fbRequest->seqNo(), __LINE__, String{}
	    << "key " << ZtQuote::Base64{keyID} << " not found"));
	return;
      }
      dbKey->commit();
      IOBuilder fbb;
      auto ackData = ZfbStruct::save(fbb, dbKey->data());
      fn(respond(
	  fbb, fbRequest->seqNo(),
	  fbs::ReqAckData::KeyDel, ackData.Union()));
    });
}

} // Zum::Server
