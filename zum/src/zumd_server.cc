//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_server.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_key_db.hh>

#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuICmp.hh>
#include <zlib/ZuMatcher.hh>

#include <zlib/ZfJSON.hh>

#include <zlib/ZmPolymorph.hh>

#include <zlib/ZtlsHMAC.hh>
#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsSec.hh>

namespace Zum {

struct PasskeyStartReq {
  String purpose;
  String capability;
};
ZfStruct(, (PasskeyStartReq, JSON),
  (purpose,		(Required),	String),
  (capability,	(JSON::Opt),		String));
struct PasskeyStartTypes {
  using Keys = ZuStringTL<"enrollment", "bootstrap", "add", "recovery">;
};

struct Ceremony {
  String ceremony;
  ZfJSON::Union<> options;
};
ZfStruct(, (Ceremony, JSON),
  (ceremony,		(Required),	String),
  (options,		(Required),	UDT));

struct Authorization {
  String authorizationURL;
  uint64_t expiresIn = 0;
};
ZfStruct(, (Authorization, JSON),
  (authorizationURL,	(Required),	String),
  (expiresIn,	(Required),		UInt64));

struct Status { String status; };
ZfStruct(, (Status, JSON),
  (status,		(Required),	String));
struct OK { bool ok = false; };
ZfStruct(, (OK, JSON),
  (ok,		(Required),		Bool));
struct BearerErrorBody { String error; };
ZfStruct(, (BearerErrorBody, JSON),
  (error,		(Required),	String));

template <typename T>
static String serverJSON(T value)
{
  String body;
  ZfJSON::save(body, value);
  return body;
}

static ServerReply jsonReply(int error)
{
  return ServerReply{
    .body = oauthErrorJSON(error), .type = ReplyType::OAuthError};
}

static ServerReply serverError()
{
  return ServerReply{
    .body = oauthErrorJSON(OAuthError::TemporarilyUnavailable),
    .type = ReplyType::ServerError};
}

static bool decodeID(ZuCSpan, Bytes &);

struct LoginFieldIDs { using Keys = ZuStringTL<"id", "login">; };
struct ConsentFieldIDs { using Keys = ZuStringTL<"id", "decision">; };
struct DecisionIDs { using Keys = ZuStringTL<"approve", "deny">; };
struct ScopeIDs {
  using Keys = ZuStringTL<"openid", "profile", "email", "offline_access",
    "zum.admin">;
};

template <typename IDs, unsigned N>
static bool formLookup(String &form, ZuCSpan (&lookup)[N])
{
  ZuAssert(N < 32);
  static constexpr auto matcher = ZuMatcher<IDs>();
  unsigned seen = 0;
  bool valid = true;
  bool decoded = formEach(form.span(), [&lookup, &seen, &valid](
      ZuCSpan name, ZuCSpan value) {
    int j = matcher.exact(name);
    if (j < 0 || (seen & (1U << j))) { valid = false; return; }
    lookup[j] = value;
    seen |= 1U << j;
  });
  return decoded && valid && seen == (1U << N) - 1;
}

static bool logoutForm(String &form, String &csrf)
{
  bool valid = true;
  bool seen = false;
  valid = formEach(form.span(), [&valid, &seen, &csrf](
      ZuCSpan name, ZuCSpan value) {
    if (name != "csrf" || seen || !value) {
      valid = false;
      return;
    }
    seen = true;
    csrf = value;
  }) && valid;
  return valid && seen;
}

static bool loginForm(String &form, Bytes &id, String &login)
{
  ZuCSpan lookup[2];
  if (!formLookup<LoginFieldIDs>(form, lookup)) return false;
  login = lookup[1];
  return login && login.length() <= 1024 && decodeID(lookup[0], id);
}

static bool consentForm(String &form, Bytes &id, bool &approve)
{
  ZuCSpan lookup[2];
  if (!formLookup<ConsentFieldIDs>(form, lookup)) return false;
  static constexpr auto matcher = ZuMatcher<DecisionIDs>();
  int decision = matcher.exact(lookup[1]);
  if (decision < 0 || !decodeID(lookup[0], id)) return false;
  approve = decision == 0;
  return true;
}

class ReplyComplete_ : public ZmNHeap<ReplyComplete_>, public ZmObject  {
friend inline ZuStringT<"Zum.zumd.server.ReplyComplete"> ZmNHeapID(ReplyComplete_ *);

public:
  ReplyComplete_(ServerFn complete) : m_complete{ZuMv(complete)} { }

  void finish(ServerReply reply)
  {
    if (!m_complete) return;
    auto complete = ZuMv(m_complete);
    complete(ZuMv(reply));
  }

private:
  ServerFn	m_complete;
};

static String encodeID(ZuBSpan id)
{
  String value;
  value.length(ZuBase64URL::enclen(id.length()));
  value.length(ZuBase64URL::encode(value.span(), id));
  return value;
}

static bool decodeID(ZuCSpan value, Bytes &id)
{
  if (!value) return false;
  Bytes next;
  next.length(ZuBase64URL::declen(value.length()), false);
  if (ZuBase64URL::decode(next, ZuBSpan{value}) != next.length()) return false;
  id = ZuMv(next);
  return true;
}

static bool ceremonyQuery(String &query, Bytes &id)
{
  if (query && query[0] == '?') query.splice(0, 1);
  if (!query.mutable_()) query.length(query.length());
  ZuCSpan encoded;
  if (!formEach(query,
    [&encoded](ZuCSpan name, ZuCSpan value) {
      if (name == "id") encoded = value;
    })) return false;
  return encoded && decodeID(encoded, id);
}

static bool passkeyStart(String &json, PasskeyStart &start)
{
  if (!json.mutable_()) json.length(json.length());
  auto parsed = ZfJSON::scan(json);
  if (parsed.p<0>() != int(json.length()) || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 || !roots[0]->has<ZfJSON::AnyNode::Object>())
    return false;
  auto handler = ZfJSON::handler<PasskeyStartReq>(roots[0]);
  if (!handler.valid) return false;
  auto wire = handler.ctor();
  if (!wire.purpose) return false;
  PasskeyStart next;
  next.capability = ZuMv(wire.capability);
  constexpr auto matcher = ZuMatcher<PasskeyStartTypes>();
  switch (matcher.exact(wire.purpose)) {
    case 0: next.type = PasskeyStartType::Enrollment; break;
    case 1: next.type = PasskeyStartType::Bootstrap; break;
    case 2: next.type = PasskeyStartType::AddCredential; break;
    case 3: next.type = PasskeyStartType::Recovery; break;
    default: return false;
  }
  bool needsCapability = next.type == PasskeyStartType::Enrollment ||
    next.type == PasskeyStartType::Bootstrap ||
    next.type == PasskeyStartType::Recovery;
  if (needsCapability && !next.capability) return false;
  start = ZuMv(next);
  return true;
}

static String ceremonyJSON(ZuBSpan id, ZuCSpan options)
{
  String source{options};
  auto parsed = ZfJSON::scan(source.span());
  if (parsed.p<0>() != int(source.length()) || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return {};
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 || !roots[0]->has<ZfJSON::AnyNode::Object>())
    return {};
  auto encoded = encodeID(id);
  String body;
  ZfJSON::save(body, Ceremony{ZuMv(encoded),
    static_cast<const ZfJSON::AnyNode *>(roots[0].ptr())});
  return body;
}

static void htmlEsc(String &out, ZuCSpan text)
{
  for (char c: text) switch (c) {
    case '&': out << "&amp;"; break;
    case '<': out << "&lt;"; break;
    case '>': out << "&gt;"; break;
    case '"': out << "&quot;"; break;
    case '\'': out << "&#39;"; break;
    default: out << c; break;
  }
}

struct ConsentView {
  Bytes id;
  AppID appID = 0;
  String clientID;
  String scope;
};

static String consentPage(const ConsentView &grant, ZuCSpan client, ZuCSpan app)
{
  String action;
  if (!appEndpointPath(grant.appID, "v1", "consent", action)) return {};
  String page{
    "<!doctype html><meta charset=utf-8><title>Zum consent</title>"
    "<meta name=referrer content=no-referrer><h1>Authorize application</h1>"
    "<p><strong>"};
  htmlEsc(page, client);
  page << "</strong> (<code>";
  htmlEsc(page, grant.clientID);
  page << "</code>) requests access to <strong>";
  htmlEsc(page, app);
  page << "</strong>.</p><h2>Requested permissions</h2><ul>";
  ZuCSpan scopes{grant.scope};
  while (scopes) {
    unsigned n = 0;
    while (n < scopes.length() && scopes[n] != ' ') ++n;
    ZuCSpan scope{scopes.data(), n};
    if (scope) {
      page << "<li><code>";
      htmlEsc(page, scope);
      page << "</code>";
      static constexpr auto matcher = ZuMatcher<ScopeIDs>();
      switch (matcher.exact(scope)) {
	case 0: page << " &mdash; Verify your identity."; break;
	case 1: page << " &mdash; Read your basic profile."; break;
	case 2: page << " &mdash; Read your email address."; break;
	case 3: page << " &mdash; Keep access when you are not signed in."; break;
	case 4:
	  page << " &mdash; Manage applications, users and access permissions.";
	  break;
	default: page << " &mdash; Access this application role.";
      }
      page << "</li>";
    }
    scopes.offset(n < scopes.length() ? n + 1 : n);
  }
  page << "</ul><form method=post action=\"" << action <<
    "\"><input type=hidden name=id value=\"" << encodeID(grant.id) <<
    "\"><button name=decision value=approve>Approve</button>"
    "<button name=decision value=deny>Deny</button></form>";
  return page;
}

static void consentReply(
    DB *db, AppID appID, Bytes id, int64_t now,
    String setCookie, ServerFn complete)
{
  auto grants = db->grants;
  grants->run(0, [db, grants, appID, id = ZuMv(id), now,
      setCookie = ZuMv(setCookie), complete = ZuMv(complete)]() mutable {
    grants->find<0>(0, ZuFwdTuple(ZuMv(id)), [db, appID, now,
	setCookie = ZuMv(setCookie), complete = ZuMv(complete)](
	ZdbRowRef<Grant> row) mutable {
      if (!row || row->data().appID != appID ||
	  row->data().kind != GrantKind::Ceremony ||
	  row->data().purpose != GrantPurpose::Authorization ||
	  row->data().state != State::Pending || row->data().owner ||
	  row->data().expires <= now) {
	complete(serverError());
	return;
      }
      auto &data = row->data();
      ConsentView grant{data.id, appID, data.clientID, data.scope};
      auto clients = db->clients;
      String clientID{grant.clientID};
      clients->run(0, [db, clients, clientID = ZuMv(clientID),
	  grant = ZuMv(grant), setCookie = ZuMv(setCookie),
	  complete = ZuMv(complete)]() mutable {
	clients->find<0>(0, ZuFwdTuple(ZuMv(clientID)), [db,
	    grant = ZuMv(grant), setCookie = ZuMv(setCookie),
	    complete = ZuMv(complete)](ZdbRowRef<Client> row) mutable {
	  if (!row || row->data().state != State::Active || row->data().owner) {
	    complete(serverError());
	    return;
	  }
	  String client{row->data().label ? row->data().label : row->data().id};
	  auto apps = db->apps;
	  AppID appID = grant.appID;
	  apps->run(0, [apps, appID, client = ZuMv(client),
	      grant = ZuMv(grant), setCookie = ZuMv(setCookie),
	      complete = ZuMv(complete)]() mutable {
	    apps->find<0>(0, ZuFwdTuple(appID), [client = ZuMv(client),
		grant = ZuMv(grant), setCookie = ZuMv(setCookie),
		complete = ZuMv(complete)](ZdbRowRef<App> row) mutable {
	      if (!row || row->data().state != State::Active || row->data().owner) {
		complete(serverError());
		return;
	      }
	      auto &app = row->data();
	      complete(ServerReply{.body = consentPage(grant, client,
		app.label ? app.label : app.name),
		.setCookie = ZuMv(setCookie), .type = ReplyType::Page});
	    });
	  });
	});
      });
    });
  });
}

bool Server::init(
    DB *db, Requests *requests, ServerConfig config,
    ClockFn clock, PageFn page, PolicyFn policy, AdmitFn admit, SignFn sign,
    OIDCHTTPFn oidcHTTP, AuthRouteFn authRoute)
{
  if (m_db || !db || !requests || !clock ||
      !policy || !sign || !config.issuer ||
      !config.cookieName || !config.cookiePath || !config.requestTimeout ||
      config.ceremonyLifetime <= 0 || config.codeLifetime <= 0 ||
      config.accessLifetime <= 0 ||
      config.refreshLifetime <= 0 || config.sessionIdle <= 0 ||
      config.sessionAbsolute <= 0 ||
      config.sessionIdle > config.sessionAbsolute || !config.refreshGenerations ||
      !config.spentTokens || !config.limits.cookie || !config.limits.jwks ||
      !config.limits.valid() ||
      (config.authMethod != AuthMethod::Passkey &&
       config.authMethod != AuthMethod::OIDC &&
       config.authMethod != AuthMethod::LocalFirst) ||
      ((config.authMethod == AuthMethod::Passkey ||
        config.authMethod == AuthMethod::LocalFirst) &&
       (!config.rpID || !config.rpName || !config.passkeyTimeout ||
	!config.limits.credentialID || !page || !admit)) ||
      (config.authMethod == AuthMethod::OIDC &&
       (!config.oidcTimeout || !oidcHTTP || !oidcConfigValid(config.oidc))) ||
      (config.authMethod == AuthMethod::LocalFirst &&
       (!config.oidcTimeout || !oidcHTTP || !authRoute)) ||
      !m_rng.init()) return false;
  m_db = db;
  m_requests = requests;
  m_config = ZuMv(config);
  m_clock = ZuMv(clock);
  m_page = ZuMv(page);
  m_policy = ZuMv(policy);
  m_admit = ZuMv(admit);
  m_sign = ZuMv(sign);
  m_authRoute = ZuMv(authRoute);
  if (m_config.authMethod != AuthMethod::Passkey && !m_oidc.init(
      requests->scheduler(), requests->sid(), db,
      m_config.limits.oidc, m_config.limits.oidcPending,
      m_config.oidcTimeout, [this]() { return now_(); }, ZuMv(oidcHTTP))) {
    final();
    return false;
  }
  return true;
}

// Callback graphs carry a raw provider pointer protected by admission credit.
// No work item retains the provider itself.
struct ServerWork : public ZmNHeap<ServerWork>, public ZmPolymorph {
friend inline ZuStringT<"Zum.Server.Work"> ZmNHeapID(ServerWork *);

  Server *owner;
  explicit ServerWork(Server *owner_) : owner{owner_} { }
  ~ServerWork() { owner->workDone_(); }
};

Server::~Server() { final(); }

Server::Work Server::work_() {
  if (Engine::state() != ZmEngineState::Running) return {};
  uint64_t count;
  do {
    count = m_credit.load_();
    if (count & ServerCredit::Closed) return {};
  } while (m_credit.cmpXch(count + 1, count) != count);
  return new ServerWork{this};
}

void Server::workDone_() {
  // Do not publish a shutdown return until its owner continuation is queued:
  // the outstanding credit protects that post. Open admission needs no post.
  uint64_t count = m_credit.load_();
  while (!(count & ServerCredit::Closed)) {
    auto prior = m_credit.cmpXch(count - 1, count);
    if (prior == count) return;
    count = prior;
  }
  post_([this]() {
    --m_credit;
    postDrain_();
  });
}

void Server::start_() {
  if (!m_db) { Engine::started(false); return; }
  if (m_config.authMethod == AuthMethod::Passkey) {
    post_([this]() { startOwner_(); });
    return;
  }
  m_oidc.start([this](bool ok) {
    if (!ok) { Engine::started(false); return; }
    post_([this]() { startOwner_(); });
  });
}

void Server::startOwner_() {
  ZmAssert_(m_credit.load_() == ServerCredit::Closed);
  ZmAssert_(!m_draining && !m_drainPosted);
  m_credit = 0;
  Engine::started(true);
}

void Server::stop_() {
  m_credit |= ServerCredit::Closed;
  m_oidc.stop([this](bool ok) {
    post_([this, ok]() {
      m_stopOK = ok;
      m_draining = true;
      postDrain_();
    });
  });
}

void Server::postDrain_() {
  if (!m_draining || m_drainPosted || (m_credit.load_() & ServerCredit::Count)) return;
  m_drainPosted = true;
  post_([this]() {
    m_drainPosted = false;
    m_draining = false;
    Engine::stopped(m_stopOK);
  });
}

void Server::final()
{
  ZmAssert_(Engine::state() == ZmEngineState::Stopped);
  ZmAssert_(!(m_credit.load_() & ServerCredit::Count));
  ZmAssert_(!m_draining && !m_drainPosted);
  m_oidc.final();
  m_authRoute = AuthRouteFn{};
  m_sign = SignFn{};
  m_admit = AdmitFn{};
  m_policy = PolicyFn{};
  m_page = PageFn{};
  m_clock = ClockFn{};
  m_config = {};
  m_requests = nullptr;
  m_db = nullptr;
}

int64_t Server::now_() const { return m_clock ? m_clock() : 0; }

ZuTime Server::deadline_() const
{
  return Zm::now() + ZuTime{double(m_config.requestTimeout)};
}

bool Server::cookie_(String &value, Bytes &digest)
{
  OpaqueToken token;
  if (!opaqueIssue(m_rng, token)) return false;
  value = ZuMv(token.token);
  digest = ZuMv(token.digest);
  return true;
}

String Server::setCookie_(ZuCSpan value, bool clear, int64_t lifetime) const
{
  if (lifetime <= 0) lifetime = m_config.ceremonyLifetime;
  String header;
  header << m_config.cookieName << '=' << value << "; Path=" <<
    m_config.cookiePath << "; Secure; HttpOnly; SameSite=Lax; Max-Age=" <<
    ZuBoxed(clear ? 0 : lifetime);
  return header;
}

void Server::app_(
    const Work &work, AppID appID, AppServerFn complete)
{
  // Admission is a database snapshot taken for every operation.  A state
  // transition prevents subsequent admissions; an operation already handed
  // its AppServer snapshot is allowed to finish.  No app/key/policy cache can
  // resurrect a disabled issuer.
  if (!m_db || !appID || !complete) {
    if (complete) complete(AppServer{});
    return;
  }
  auto apps = m_db->apps;
  apps->run(0, [credit = work, this, apps, appID, complete = ZuMv(complete)]() mutable {
    auto work = ZuMv(credit);
    apps->find<0>(0, ZuFwdTuple(appID), [credit = work, this, appID,
        complete = ZuMv(complete)](ZdbRowRef<App> row) mutable {
      auto work = ZuMv(credit);
      if (!row || row->data().id != appID ||
	  row->data().state != State::Active || row->data().owner) {
	complete(AppServer{});
	return;
      }
      String issuer;
      if (!row->data().audience || !appIssuer(m_config.issuer, appID, issuer)) {
	complete(AppServer{});
	return;
      }
      policy_(work, AppServer{.issuer = ZuMv(issuer),
        .audience = row->data().audience, .appID = appID}, ZuMv(complete));
    });
  });
}

void Server::policy_(
    const Work &work, AppServer app, AppServerFn complete)
{
  auto policies = m_db->authPolicies;
  policies->run(0, [credit = work, policies, app = ZuMv(app),
      complete = ZuMv(complete)]() mutable {
    auto work = ZuMv(credit);
    policies->find<0>(0, ZuFwdTuple(app.appID), [credit = work, app = ZuMv(app),
        complete = ZuMv(complete)](ZdbRowRef<AuthPolicy> row) mutable {
      auto work = ZuMv(credit);
      if (!row || row->data().appID != app.appID || row->data().owner ||
	  row->data().state != State::Active || !row->data().tokenLifetime ||
	  !row->data().sessionIdle || !row->data().sessionAbsolute ||
	  row->data().sessionIdle > row->data().sessionAbsolute) {
	complete(AppServer{});
	return;
      }
      app.tokenLifetime = row->data().tokenLifetime;
      app.sessionIdle = row->data().sessionIdle;
      app.sessionAbsolute = row->data().sessionAbsolute;
      complete(ZuMv(app));
    });
  });
}

bool Server::binding_(String &cookie, Bytes &digest) const
{
  String value;
  return binding_(cookie, value, digest);
}

bool Server::binding_(String &cookie, String &token, Bytes &digest) const
{
  unsigned length = cookie.length();
  if (!length || length > m_config.limits.cookie) return false;
  ZuCSpan value;
  unsigned offset = 0;
  while (offset < length) {
    while (offset < length &&
        (cookie[offset] == ' ' || cookie[offset] == ';')) ++offset;
    unsigned end = offset;
    while (end < length && cookie[end] != ';') ++end;
    unsigned equal = offset;
    while (equal < end && cookie[equal] != '=') ++equal;
    unsigned nameEnd = equal;
    while (nameEnd > offset && cookie[nameEnd - 1] == ' ') --nameEnd;
    unsigned valueStart = equal < end ? equal + 1 : end;
    while (valueStart < end && cookie[valueStart] == ' ') ++valueStart;
    unsigned valueEnd = end;
    while (valueEnd > valueStart && cookie[valueEnd - 1] == ' ') --valueEnd;
    if (ZuCSpan{cookie.data() + offset, nameEnd - offset} ==
	m_config.cookieName) {
      value = {cookie.data() + valueStart, valueEnd - valueStart};
    }
    offset = end + (end < length);
  }
  Bytes id;
  if (!value) return false;
  token = value;
  return opaqueParse(token, id, digest);
}

String Server::csrf_(ZuBSpan key) const
{
  if (!key) return {};
  ZuBArray<Ztls::HMAC<>::Size> digest(Ztls::HMAC<>::Size, false);
  Ztls::HMAC<> hmac;
  hmac.start(key);
  hmac.update(ZuBSpan{"zum.logout.csrf"});
  hmac.finish(digest);
  String encoded;
  encoded.length(ZuBase64URL::enclen(digest.length()));
  encoded.length(ZuBase64URL::encode(encoded.span(), digest));
  ZuClear(digest);
  return encoded;
}

void Server::authorize(AppID appID, String query, ServerFn complete)
{
  authorize(appID, ZuMv(query), String{}, ZuMv(complete));
}

void Server::authorize(
    AppID appID, String query, String browserCookie, ServerFn complete)
{
  if (!complete) return;
  auto work = work_();
  if (!work) { complete(serverError()); return; }
  app_(work, appID, [credit = work, this, query = ZuMv(query),
      browserCookie = ZuMv(browserCookie), complete = ZuMv(complete)](
      AppServer app) mutable {
    auto work = ZuMv(credit);
    if (!app) {
      complete(jsonReply(OAuthError::InvalidRequest));
      return;
    }
    String sessionToken;
    Bytes sessionDigest;
    (void)binding_(browserCookie, sessionToken, sessionDigest);
    authorize_(work, ZuMv(app), ZuMv(query), ZuMv(sessionToken), ZuMv(complete));
  });
}

void Server::authorize_(
    const Work &work, AppServer app, String query, String sessionToken, ServerFn complete)
{
  if (!m_db || !complete) return;
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  if (query.length() > m_config.limits.form) {
    done->finish(jsonReply(OAuthError::InvalidRequest));
    return;
  }
  String cookie;
  Bytes binding;
  if (!cookie_(cookie, binding)) { done->finish(serverError()); return; }
  String setCookie = setCookie_(cookie);
  if (cookie.mutable_()) ZuClear(cookie);
  int64_t now = now_();
  if (now <= 0) {
    done->finish(serverError());
    return;
  }
  Bytes sessionBinding{binding};
  if (!authorizeRequest(work, m_requests, deadline_(), m_db, m_rng,
      ZuMv(query), ZuMv(binding), AuthorizeConfig{
        .issuer = app.issuer,
        .appID = app.appID,
        .rpID = m_config.authMethod != AuthMethod::OIDC ?
          m_config.rpID : String{},
        .now = now,
        .expires = now + m_config.ceremonyLifetime,
        .timeout = m_config.authMethod != AuthMethod::OIDC ?
          m_config.passkeyTimeout : 0,
        .passkey = m_config.authMethod != AuthMethod::OIDC
      }, [credit = work, this, binding = ZuMv(sessionBinding),
	  sessionIdle = app.sessionIdle,
          sessionToken = ZuMv(sessionToken),
          setCookie = ZuMv(setCookie), done](
          int error, AuthorizeResult result) mutable {
	auto work = ZuMv(credit);
	if (error == AuthorizeIssue::OK)
          sessionAuthorize_(work, ZuMv(result), ZuMv(binding),
            ZuMv(sessionToken), ZuMv(setCookie), sessionIdle,
            [credit = work, done](ServerReply reply) mutable {
	      auto work = ZuMv(credit);
	      done->finish(ZuMv(reply));
            });
        else
          authorizeReply_(work, error, ZuMv(result), ZuMv(setCookie),
            [credit = work, done](ServerReply reply) mutable {
	      auto work = ZuMv(credit);
	      done->finish(ZuMv(reply));
            });
      })) done->finish(serverError());
}

void Server::sessionAuthorize_(
    const Work &work, AuthorizeResult result, Bytes binding,
    String sessionToken, String setCookie, uint32_t sessionIdle,
    ServerFn complete)
{
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  bool none = result.promptPresent && result.prompt == "none";
  bool login = result.promptPresent && result.prompt == "login";
  if (!sessionToken || login) {
    if (none) {
      done->finish(ServerReply{
	.location = errorRedirect(result.redirectURI,
	  OAuthError::LoginRequired, result.state, result.statePresent),
	.setCookie = setCookie_({}, true), .type = ReplyType::Redirect});
      return;
    }
    authorizeReply_(work, AuthorizeIssue::OK, ZuMv(result), ZuMv(setCookie),
      [credit = work, done](ServerReply reply) mutable {
	auto work = ZuMv(credit);
	done->finish(ZuMv(reply));
      });
    return;
  }
  int64_t now = now_();
  String issuer{result.issuer};
  if (now <= 0 || !sessionUse(work, m_requests, deadline_(), m_db,
      ZuMv(sessionToken), ZuMv(issuer), now, sessionIdle,
      [credit = work, this, result = ZuMv(result), binding = ZuMv(binding),
	  setCookie = ZuMv(setCookie), none, now, done](int error,
	    Session session, String) mutable {
	auto work = ZuMv(credit);
	bool stale = error != SessionError::OK ||
	  session.authTime <= 0 || session.authTime > now ||
	  (result.maxAgePresent &&
	    uint64_t(now - session.authTime) > result.maxAge);
	if (stale) {
	  if (none) {
	    done->finish(ServerReply{
	      .location = errorRedirect(result.redirectURI,
		OAuthError::LoginRequired, result.state, result.statePresent),
	      .setCookie = setCookie_({}, true), .type = ReplyType::Redirect});
	    return;
	  }
	  authorizeReply_(work, AuthorizeIssue::OK, ZuMv(result), ZuMv(setCookie),
	    [credit = work, done](ServerReply reply) mutable {
	      auto work = ZuMv(credit);
	      done->finish(ZuMv(reply));
            });
	  return;
	}
	Bytes consentID{result.ceremonyID};
	if (!authorizeSessionFinish(work, m_requests, deadline_(), m_db, m_rng,
	    ZuMv(result.ceremonyID), ZuMv(binding), ZuMv(session),
	    AuthorizeFinishConfig{.issuer = result.issuer,
	      .appID = result.appID, .origin = result.issuer, .now = now,
	      .codeExpires = now + m_config.codeLifetime, .consent = true},
	    m_policy,
	    [credit = work, this, result = ZuMv(result), id = ZuMv(consentID),
	        setCookie = ZuMv(setCookie),
	        none, done](
		int error, String location) mutable {
	      auto work = ZuMv(credit);
	      if (error == AuthorizeIssue::OK) {
		done->finish(ServerReply{.location = ZuMv(location),
		  .type = ReplyType::Redirect});
		return;
	      }
	      if (error == AuthorizeIssue::Consent) {
		consentReply(m_db, result.appID, ZuMv(id),
		  now_(), ZuMv(setCookie),
		  [credit = work, done](ServerReply reply) mutable {
		    auto work = ZuMv(credit);
		    done->finish(ZuMv(reply));
                  });
		return;
	      }
	      done->finish(ServerReply{
		.location = errorRedirect(result.redirectURI,
		  none && error != OAuthError::ConsentRequired ?
		    OAuthError::LoginRequired : error,
		  result.state, result.statePresent),
		.setCookie = error == OAuthError::AccessDenied ? String{} :
		  setCookie_({}, true), .type = ReplyType::Redirect});
	    })) done->finish(serverError());
      })) done->finish(serverError());
}

void Server::authorizeReply_(
    const Work &work, int error, AuthorizeResult result,
    String setCookie, ServerFn complete)
{
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  if (error == AuthorizeIssue::OK) {
          auto local = [credit = work, this, done](AuthorizeResult result,
              String setCookie) mutable {
	    auto work = ZuMv(credit);
	    String body = m_page(
              result.appID, ZuMv(result.ceremonyID), ZuMv(result.options));
            done->finish(ServerReply{.body = ZuMv(body),
              .setCookie = ZuMv(setCookie), .type = ReplyType::Page});
          };
          auto oidc = [credit = work, this, done](AuthorizeResult result,
              OIDCConfig oidc, String setCookie) mutable {
	    auto work = ZuMv(credit);
	    oidc.maxAge = result.maxAge;
            oidc.maxAgePresent = result.maxAgePresent;
            if (result.promptPresent && result.prompt == "login") {
              oidc.prompt = "login";
              oidc.maxAge = 0;
              oidc.maxAgePresent = true;
            }
            if (!m_oidc.begin(ZuMv(result.ceremonyID), ZuMv(oidc),
              [credit = work, this, setCookie = ZuMv(setCookie), done](
                  bool ok, String location) mutable {
		auto work = ZuMv(credit);
		if (ok)
                  done->finish(ServerReply{.location = ZuMv(location),
                    .setCookie = ZuMv(setCookie),
                    .type = ReplyType::Redirect});
                else {
                  auto reply = serverError();
                  reply.setCookie = setCookie_({}, true);
                  done->finish(ZuMv(reply));
                }
              })) {
              auto reply = serverError();
              reply.setCookie = setCookie_({}, true);
              done->finish(ZuMv(reply));
            }
          };
          if (m_config.authMethod == AuthMethod::Passkey ||
              (m_config.authMethod == AuthMethod::LocalFirst &&
               !result.loginHint)) {
            local(ZuMv(result), ZuMv(setCookie));
            return;
          }
          if (m_config.authMethod == AuthMethod::OIDC) {
            oidc(ZuMv(result), m_config.oidc, ZuMv(setCookie));
            return;
          }
          auto route = m_authRoute;
          AppID appID = result.appID;
          String hint = result.loginHint;
            route(appID, ZuMv(hint), [credit = work, this, done, localFn = ZuMv(local),
              oidcFn = ZuMv(oidc), result = ZuMv(result),
              setCookie = ZuMv(setCookie)](AuthRoute route) mutable {
	    auto work = ZuMv(credit);
	    auto local = ZuMv(localFn);
            auto oidc = ZuMv(oidcFn);
            switch (route.type) {
              case AuthRouteType::Local:
                local(ZuMv(result), ZuMv(setCookie));
                return;
              case AuthRouteType::OIDC:
                oidc(ZuMv(result), ZuMv(route.oidc), ZuMv(setCookie));
                return;
              default: {
                auto reply = serverError();
                reply.setCookie = setCookie_({}, true);
                done->finish(ZuMv(reply));
              } return;
            }
          });
        } else if (result.redirect) {
          done->finish(ServerReply{
            .location = errorRedirect(result.redirectURI, error,
              result.state, result.statePresent),
            .setCookie = setCookie_({}, true),
            .type = ReplyType::Redirect});
        } else {
          auto reply = jsonReply(error);
          reply.setCookie = setCookie_({}, true);
          done->finish(ZuMv(reply));
        }
}

void Server::token(
    AppID appID, String form, String authorization, ServerFn complete)
{
  if (!complete) return;
  auto work = work_();
  if (!work) { complete(serverError()); return; }
  app_(work, appID, [credit = work, this, form = ZuMv(form), authorization = ZuMv(authorization),
      complete = ZuMv(complete)](AppServer app) mutable {
    auto work = ZuMv(credit);
    if (!app) {
      complete(jsonReply(OAuthError::InvalidRequest));
      return;
    }
    if (!m_db) return;
    ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
    if (form.length() > m_config.limits.form) {
      done->finish(jsonReply(OAuthError::InvalidRequest));
      return;
    }
    int64_t now = now_();
    if (now <= 0 || app.tokenLifetime > uint64_t(INT64_MAX - now) ||
	!tokenRequest(work, m_requests, deadline_(), m_db,
        m_rng, ZuMv(form), ZuMv(authorization), TokenConfig{
          .issuer = app.issuer,
          .appID = app.appID,
          .jwtLimits = m_config.limits.jwt,
          .maxKeys = m_config.limits.jwks,
          .now = now,
          .accessExpires = now + app.tokenLifetime,
          .refreshExpires = now + m_config.refreshLifetime,
          .generationLimit = m_config.refreshGenerations,
          .spentLimit = m_config.spentTokens,
          .event = m_config.refreshRevoke
        }, m_sign, [credit = work, done](int error, TokenResponse response) mutable {
	  auto work = ZuMv(credit);
	  if (error == TokenIssue::OK) {
            auto body = tokenResponseJSON(response);
            tokenClear(response);
            done->finish(ServerReply{
              .body = ZuMv(body), .type = ReplyType::OK});
          } else {
            auto reply = jsonReply(error);
            if (error == OAuthError::InvalidClient)
              reply.type = ReplyType::ClientError;
            done->finish(ZuMv(reply));
          }
        })) done->finish(serverError());
  });
}

void Server::revoke(
    AppID appID, String form, String authorization, ServerFn complete)
{
  if (!complete) return;
  auto work = work_();
  if (!work) { complete(serverError()); return; }
  app_(work, appID, [credit = work, this, form = ZuMv(form), authorization = ZuMv(authorization),
      complete = ZuMv(complete)](AppServer app) mutable {
    auto work = ZuMv(credit);
    if (!app) {
      complete(jsonReply(OAuthError::InvalidRequest));
      return;
    }
    if (!m_db) return;
    ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
    if (form.length() > m_config.limits.form) {
      done->finish(jsonReply(OAuthError::InvalidRequest));
      return;
    }
    int64_t now = now_();
    if (now <= 0 || !revokeRequest(work, m_requests, deadline_(), m_db,
        ZuMv(form), ZuMv(authorization), RevokeConfig{
          .issuer = app.issuer, .appID = app.appID,
          .jwtLimits = m_config.limits.jwt, .now = now,
          .event = m_config.refreshRevoke},
        [credit = work, done](int error) mutable {
	  auto work = ZuMv(credit);
	  if (error == RevokeIssue::OK)
            done->finish(ServerReply{.type = ReplyType::Empty});
          else {
            auto reply = jsonReply(error);
            if (error == OAuthError::InvalidClient)
              reply.type = ReplyType::ClientError;
            done->finish(ZuMv(reply));
          }
        })) done->finish(serverError());
  });
}

void Server::login(AppID appID, String cookie, ServerFn complete)
{
  if (!complete) return;
  auto work = work_();
  if (!work) { complete(serverError()); return; }
  app_(work, appID, [credit = work, this, cookie = ZuMv(cookie),
      complete = ZuMv(complete)](AppServer app) mutable {
    auto work = ZuMv(credit);
    if (!app) { complete(jsonReply(OAuthError::InvalidRequest)); return; }
    login_(work, ZuMv(app), ZuMv(cookie), ZuMv(complete));
  });
}

void Server::login_(
    const Work &work, AppServer app, String cookie, ServerFn complete)
{
  if (!m_db || !complete) return;
  String token;
  Bytes digest;
  String csrf;
  if (binding_(cookie, token, digest)) csrf = csrf_(digest);
  if (token && token.mutable_()) ZuClear(token);
  String body{
    "<!doctype html><meta charset=utf-8><title>Zum login</title>"
    "<meta name=referrer content=no-referrer><h1>Zum</h1>"};
  if (csrf) {
    String action;
    if (!appEndpointPath(app.appID, "v1", "logout", action)) {
      complete(serverError());
      return;
    }
    body << "<p>Signed in to Zum.</p><form method=post action=\"" << action <<
      "\">"
      "<input type=hidden name=csrf value=\"" << csrf <<
      "\"><button>Sign out of Zum</button></form>";
  } else {
    body << "<p>Start sign-in from an enrolled application.</p>";
  }
  complete(ServerReply{.body = ZuMv(body), .type = ReplyType::Page});
}

void Server::login(
    AppID appID, String form, String cookie, ServerFn complete)
{
  if (!complete) return;
  auto work = work_();
  if (!work) { complete(serverError()); return; }
  app_(work, appID, [credit = work, this, form = ZuMv(form), cookie = ZuMv(cookie),
      complete = ZuMv(complete)](AppServer app) mutable {
    auto work = ZuMv(credit);
    if (!app) { complete(jsonReply(OAuthError::InvalidRequest)); return; }
    login_(work, ZuMv(app), ZuMv(form), ZuMv(cookie), ZuMv(complete));
  });
}

void Server::login_(
    const Work &work, AppServer app, String form, String cookie, ServerFn complete)
{
  if (!m_db || !complete) return;
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  if (!form || form.length() > m_config.limits.form ||
      cookie.length() > m_config.limits.cookie) {
    done->finish(jsonReply(OAuthError::InvalidRequest));
    return;
  }
  if (!form.mutable_()) form.length(form.length());
  Bytes id;
  String login;
  Bytes binding;
  if (!loginForm(form, id, login) || !binding_(cookie, binding)) {
    done->finish(jsonReply(OAuthError::InvalidRequest));
    return;
  }
  int64_t now = now_();
  if (now <= 0) { done->finish(serverError()); return; }
  auto grants = m_db->grants;
  bool queued = m_requests->run(deadline_(), [credit = work, this, grants, app = ZuMv(app),
      id = ZuMv(id), login = ZuMv(login), binding = ZuMv(binding), now, done](
      ZmRef<Request> request) mutable {
    auto work = ZuMv(credit);
    grants->run(0, [
      credit = work, this, grants, app = ZuMv(app), id = ZuMv(id), login = ZuMv(login),
        binding = ZuMv(binding), now, done, request = ZuMv(request)]() mutable {
      auto work = ZuMv(credit);
      grants->find<0>(0, ZuFwdTuple(ZuMv(id)), [credit = work, this, app = ZuMv(app),
          login = ZuMv(login), binding = ZuMv(binding), now, done,
          request = ZuMv(request)](ZdbRowRef<Grant> row) mutable {
	auto work = ZuMv(credit);
	int error = OAuthError::InvalidRequest;
        AuthorizeResult result;
        if (row && row->data().issuer == app.issuer &&
            row->data().appID == app.appID &&
            row->data().kind == GrantKind::Ceremony &&
            row->data().purpose == GrantPurpose::Authorization &&
            row->data().state == State::Active && !row->data().owner &&
            row->data().expires > now &&
            Ztls::ctEqual(row->data().bindingDigest, binding)) {
          const auto &grant = row->data();
          result.ceremonyID = grant.id;
          result.appID = grant.appID;
          result.issuer = grant.issuer;
          result.loginHint = ZuMv(login);
          result.options = assertionOptions(grant.challenge,
            m_config.rpID, m_config.passkeyTimeout);
          result.redirectURI = grant.redirectURI;
          result.state = grant.oauthState;
          result.statePresent = grant.oauthStatePresent;
          result.prompt = grant.prompt;
          result.promptPresent = grant.promptPresent;
          result.maxAge = grant.maxAge;
          result.maxAgePresent = grant.maxAgePresent;
          result.redirect = true;
          error = AuthorizeIssue::OK;
        }
        request->complete([credit = work, this, error, result = ZuMv(result), done]() mutable {
	  auto work = ZuMv(credit);
	  authorizeReply_(work, error, ZuMv(result), {},
            [credit = work, done](ServerReply reply) mutable {
	      auto work = ZuMv(credit);
	      done->finish(ZuMv(reply));
            });
        });
      });
    });
  }, [credit = work, done]() mutable {
    auto work = ZuMv(credit);
    done->finish(jsonReply(OAuthError::TemporarilyUnavailable));
  });
  if (!queued) done->finish(serverError());
}

void Server::consent(
    AppID appID, String form, String cookie, ServerFn complete)
{
  if (!complete) return;
  auto work = work_();
  if (!work) { complete(serverError()); return; }
  app_(work, appID, [credit = work, this, form = ZuMv(form), cookie = ZuMv(cookie),
      complete = ZuMv(complete)](AppServer app) mutable {
    auto work = ZuMv(credit);
    if (!app) { complete(jsonReply(OAuthError::InvalidRequest)); return; }
    consent_(work, ZuMv(app), ZuMv(form), ZuMv(cookie), ZuMv(complete));
  });
}

void Server::consent_(
    const Work &work, AppServer app, String form, String cookie, ServerFn complete)
{
  if (!m_db || !complete) return;
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  if (!form || form.length() > m_config.limits.form ||
      cookie.length() > m_config.limits.cookie) {
    done->finish(jsonReply(OAuthError::InvalidRequest));
    return;
  }
  if (!form.mutable_()) form.length(form.length());
  Bytes id, binding;
  bool approve = false;
  if (!consentForm(form, id, approve) || !binding_(cookie, binding)) {
    done->finish(jsonReply(OAuthError::InvalidRequest));
    return;
  }
  int64_t now = now_();
  Bytes sessionGrant{id};
  if (now <= 0 || !authorizeConsentFinish(work, m_requests, deadline_(), m_db,
      m_rng, ZuMv(id), ZuMv(binding), approve,
      AuthorizeFinishConfig{.issuer = app.issuer, .appID = app.appID,
	.origin = m_config.issuer, .now = now,
        .codeExpires = now + m_config.codeLifetime}, m_policy,
      [credit = work, this, id = ZuMv(sessionGrant), issuer = ZuMv(app.issuer),
	  sessionIdle = app.sessionIdle,
	  sessionAbsolute = app.sessionAbsolute, done](
          int error, String location) mutable {
	auto work = ZuMv(credit);
	if (error == AuthorizeIssue::OK) {
          sessionReply_(work, ZuMv(id), ZuMv(location), ZuMv(issuer), sessionIdle,
	    sessionAbsolute,
            [credit = work, done](ServerReply reply) mutable {
	      auto work = ZuMv(credit);
	      done->finish(ZuMv(reply));
            });
          return;
        }
        if (error == OAuthError::AccessDenied && location) {
          done->finish(ServerReply{.location = ZuMv(location),
            .setCookie = setCookie_({}, true),
            .type = ReplyType::Redirect});
          return;
        }
        auto reply = jsonReply(error);
        reply.setCookie = setCookie_({}, true);
        done->finish(ZuMv(reply));
      })) done->finish(serverError());
}

void Server::logout(
    AppID appID, String form, String cookie, ServerFn complete)
{
  if (!complete) return;
  auto work = work_();
  if (!work) { complete(serverError()); return; }
  app_(work, appID, [credit = work, this, form = ZuMv(form), cookie = ZuMv(cookie),
      complete = ZuMv(complete)](AppServer app) mutable {
    auto work = ZuMv(credit);
    if (!app) { complete(jsonReply(OAuthError::InvalidRequest)); return; }
    logout_(work, ZuMv(app), ZuMv(form), ZuMv(cookie), ZuMv(complete));
  });
}

void Server::logout_(
    const Work &work, AppServer app, String form, String cookie, ServerFn complete)
{
  if (!m_db || !complete) return;
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  if (!form || form.length() > m_config.limits.form ||
      cookie.length() > m_config.limits.cookie) {
    done->finish(jsonReply(OAuthError::InvalidRequest));
    return;
  }
  if (!form.mutable_()) form.length(form.length());
  String submitted;
  String token;
  Bytes digest;
  if (!logoutForm(form, submitted) ||
      !binding_(cookie, token, digest)) {
    done->finish(jsonReply(OAuthError::InvalidRequest));
    return;
  }
  String expected = csrf_(digest);
  bool matches = expected.length() == submitted.length() &&
    Ztls::ctEqual(ZuBSpan{expected}, ZuBSpan{submitted});
  if (submitted && submitted.mutable_())
    ZuClear(submitted);
  if (expected && expected.mutable_())
    ZuClear(expected);
  if (!matches) {
    if (token && token.mutable_()) ZuClear(token);
    done->finish(jsonReply(OAuthError::InvalidRequest));
    return;
  }
  int64_t now = now_();
  if (now <= 0 || !sessionRevoke(work, m_requests, deadline_(), m_db,
      ZuMv(token), ZuMv(app.issuer), now, [credit = work, this, done](int error) mutable {
	auto work = ZuMv(credit);
	if (error != SessionError::OK) {
	  done->finish(serverError());
	  return;
	}
	done->finish(ServerReply{
	  .body = "<!doctype html><meta charset=utf-8><title>Zum logout</title>"
	    "<meta name=referrer content=no-referrer><h1>Signed out of Zum</h1>",
	  .setCookie = setCookie_({}, true), .type = ReplyType::Page});
      })) done->finish(serverError());
}

struct ReadyKey_ : public ZmNHeap<ReadyKey_>, public ZmObject  {
friend inline ZuStringT<"Zum.zumd.server.ReadyKey"> ZmNHeapID(ReadyKey_ *);

  SignKey key;
  Bytes digest;
};

void Server::ready(ServerFn complete)
{
  if (!complete) return;
  auto work = work_();
  if (!work) { complete(serverError()); return; }
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  if (!m_requests->run(deadline_(), [credit = work, this, done](ZmRef<Request> request) mutable {
    auto work = ZuMv(credit);
    ServerFn finish{[credit = work, done, request = ZuMv(request)](ServerReply reply) mutable {
      auto work = ZuMv(credit);
      request->complete([credit = work, done, reply = ZuMv(reply)]() mutable {
	auto work = ZuMv(credit);
	done->finish(ZuMv(reply));
      });
    }};
    m_db->issuers->find<0>(0, ZuFwdTuple(m_config.issuer),
      [credit = work, this, finish = ZuMv(finish)](ZdbRowRef<Issuer> issuer) mutable {
	auto work = ZuMv(credit);
	int64_t now = now_();
        if (!issuer || issuer->data().bootstrapPhase != BootstrapPhase::Ready ||
            now <= 0 || m_config.accessLifetime > INT64_MAX - now) {
          finish(serverError());
          return;
        }
        signKeyLoad(work, m_db, m_config.issuer, now, now + m_config.accessLifetime,
          m_config.limits.jwks, [
            credit = work, this, finish = ZuMv(finish)](SignKey key) mutable {
	    auto work = ZuMv(credit);
	    if (!key.id || !m_sign || !m_requests->active()) {
              finish(serverError());
              return;
            }
            ZmRef<ReadyKey_> probe = new ReadyKey_{};
            probe->key = ZuMv(key);
            probe->digest.length(Ztls::MD<>::Size, false);
            Ztls::MD<> md;
            md.update(ZuBSpan{"zum.readiness"});
            md.finish(probe->digest);
            m_sign(probe->key, probe->digest, [credit = work, probe, finish = ZuMv(finish)](
                Bytes signature) mutable {
	      auto work = ZuMv(credit);
	      bool ok = bool(signature);
              if (signature.mutable_()) ZuClear(signature);
              finish(ok ? ServerReply{.body = serverJSON(Status{"ready"}),
                .type = ReplyType::OK} : serverError());
            });
          });
      });
  }, [credit = work, done]() mutable {
    auto work = ZuMv(credit);
    done->finish(serverError());
  }))
    done->finish(serverError());
}

void Server::metadata(AppID appID, ServerFn complete)
{
  if (!complete) return;
  auto work = work_();
  if (!work) { complete(serverError()); return; }
  app_(work, appID, [credit = work, this, complete = ZuMv(complete)](AppServer app) mutable {
    auto work = ZuMv(credit);
    if (!app) { complete(serverError()); return; }
    unsigned limit = m_config.limits.metadataScopes;
    auto roles = m_db->roles;
    if (!roles || !limit || limit == UINT_MAX) {
      complete(serverError());
      return;
    }
    roles->selectRows<0>({}, limit + 1,
      [credit = work, this, app = ZuMv(app), limit, complete = ZuMv(complete),
          values = StringVec{}, overflow = false](
          ZuUnion<void, RoleTable::Tuple> result, unsigned) mutable {
	auto work = credit;
	if (result.template is<void>()) credit = nullptr;
        if (result.template is<RoleTable::Tuple>()) {
          if (values.length() >= limit) { overflow = true; return; }
          auto tuple = ZuMv(result).template p<RoleTable::Tuple>();
          Role role;
          ZuTupleCall(ZuMv(tuple), [&role](auto &&...args) {
            role = Role{ZuFwd<decltype(args)>(args)...};
          });
          if (role.appID == app.appID && role.state == State::Active && !role.owner &&
              !role.tombstone && role.name)
            values.push(ZuMv(role.name));
          return;
        }
        if (overflow) { complete(serverError()); return; }
        complete(ServerReply{.body = metadataJSON(
          m_config.issuer, app.appID, values),
          .type = ReplyType::Discovery});
      });
  });
}

void Server::jwks(AppID appID, ServerFn complete)
{
  if (!complete) return;
  auto work = work_();
  if (!work) { complete(serverError()); return; }
  app_(work, appID, [credit = work, this, complete = ZuMv(complete)](AppServer app) mutable {
    auto work = ZuMv(credit);
    if (!app) { complete(serverError()); return; }
    ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
    int64_t now = now_();
    if (now <= 0 || !jwksLoad(work, m_requests, deadline_(), m_db,
        app.issuer, now, m_config.limits.jwks, [credit = work, done](
            bool ok, String json) mutable {
	  auto work = ZuMv(credit);
	  if (ok) done->finish(ServerReply{
            .body = ZuMv(json), .type = ReplyType::Discovery});
          else done->finish(serverError());
        })) done->finish(serverError());
  });
}

void Server::userInfo(
    AppID appID, String authorization, ServerFn complete)
{
  if (!complete) return;
  auto work = work_();
  if (!work) { complete(serverError()); return; }
  app_(work, appID, [credit = work, this, authorization = ZuMv(authorization),
      complete = ZuMv(complete)](AppServer app) mutable {
    auto work = ZuMv(credit);
    if (!app) { complete(ServerReply{
      .body = serverJSON(BearerErrorBody{"invalid_token"}),
      .type = ReplyType::BearerError}); return; }
    userInfo_(work, ZuMv(app), ZuMv(authorization), ZuMv(complete));
  });
}

void Server::userInfo_(
    const Work &work, AppServer app, String authorization, ServerFn complete)
{
  if (!m_db || !complete) return;
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  if (!m_requests->run(deadline_(), [credit = work, this, app = ZuMv(app), done,
      authorization = ZuMv(authorization)](ZmRef<Request> request) mutable {
    auto work = ZuMv(credit);
    userInfoVerify_(work, ZuMv(app), ZuMv(authorization), [
      credit = work, done, request = ZuMv(request)](
        ServerReply reply) mutable {
      auto work = ZuMv(credit);
      request->complete([credit = work, done, reply = ZuMv(reply)]() mutable {
	auto work = ZuMv(credit);
	done->finish(ZuMv(reply));
      });
    });
  }, [credit = work, done]() mutable {
    auto work = ZuMv(credit);
    done->finish(serverError());
  }))
    done->finish(serverError());
}

void Server::userInfoVerify_(
    const Work &work, AppServer app, String authorization, ServerFn complete)
{
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  static constexpr auto prefix = "Bearer "_z;
  if (authorization.length() <= prefix.length() ||
      !ZuICmp<ZuCSpan>::equals(
        ZuCSpan{authorization.data(), prefix.length()}, prefix)) {
    done->finish(ServerReply{
      .body = serverJSON(BearerErrorBody{"invalid_token"}),
      .type = ReplyType::BearerError});
    return;
  }
  authorization.splice(0, prefix.length());
  JWTHeader header;
  int64_t now = now_();
  if (now <= 0 || !jwtHeader(authorization, m_config.limits.jwt, header)) {
    done->finish(ServerReply{
      .body = serverJSON(BearerErrorBody{"invalid_token"}),
      .type = ReplyType::BearerError});
    return;
  }
  m_db->signKeys->find<0>(0, ZuFwdTuple(ZuMv(header.keyID)), [credit = work,
    this, app = ZuMv(app), done, now, authorization = ZuMv(authorization)
  ](ZdbRowRef<SignKey> row) mutable {
    auto work = ZuMv(credit);
    Principal principal;
    if (!row || !signKeyVerify(row->data(), authorization,
	app.issuer, app.audience, now, m_config.limits.jwt, principal) ||
	!scopeContains(principal.scope, "openid")) {
      done->finish(ServerReply{
	.body = serverJSON(BearerErrorBody{"invalid_token"}),
	.type = ReplyType::BearerError});
      return;
    }
    String clientID = principal.clientID;
    m_db->clients->find<0>(0, ZuFwdTuple(ZuMv(clientID)), [credit = work, this,
	appID = app.appID, done, principal = ZuMv(principal)](
	ZdbRowRef<Client> client) mutable {
      auto work = ZuMv(credit);
      if (!client || client->data().appID != appID ||
	  client->data().state != State::Active || client->data().owner) {
        done->finish(ServerReply{
	  .body = serverJSON(BearerErrorBody{"invalid_token"}),
	  .type = ReplyType::BearerError});
	return;
      }
      userInfo_(work, ZuMv(principal), [credit = work, done](ServerReply reply) mutable {
	auto work = ZuMv(credit);
      done->finish(ZuMv(reply));
      });
    });
  });
}

void Server::userInfo_(
    const Work &work, Principal principal, ServerFn complete)
{
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  Bytes handle;
  if (!decodeID(principal.subject, handle)) {
    done->finish(ServerReply{
      .body = serverJSON(BearerErrorBody{"invalid_token"}),
      .type = ReplyType::BearerError});
    return;
  }
  String clientID = principal.clientID;
  m_db->clients->find<0>(0, ZuFwdTuple(ZuMv(clientID)), [credit = work,
    this, done, handle = ZuMv(handle), principal = ZuMv(principal)
  ](ZdbRowRef<Client> client) mutable {
    auto work = ZuMv(credit);
    if (!client || client->data().state != State::Active ||
        client->data().owner) {
      done->finish(ServerReply{
        .body = serverJSON(BearerErrorBody{"invalid_token"}),
        .type = ReplyType::BearerError});
      return;
    }
    m_db->users->find<1>(0, ZuFwdTuple(ZuMv(handle)), [credit = work,
      done, principal = ZuMv(principal)
    ](ZdbRowRef<User> user) mutable {
      auto work = ZuMv(credit);
      String json;
      if (!user || !userInfoJSON(user->data(), principal, json)) {
        done->finish(ServerReply{
          .body = serverJSON(BearerErrorBody{"invalid_token"}),
          .type = ReplyType::BearerError});
        return;
      }
      done->finish(ServerReply{.body = ZuMv(json), .type = ReplyType::OK});
    });
  });
}

void Server::passkeyBegin(AppID appID, String json, ServerFn complete)
{
  if (!complete) return;
  auto work = work_();
  if (!work) { complete(serverError()); return; }
  app_(work, appID, [credit = work, this, json = ZuMv(json),
      complete = ZuMv(complete)](AppServer app) mutable {
    auto work = ZuMv(credit);
    if (!app) { complete(jsonReply(OAuthError::InvalidRequest)); return; }
    passkeyBegin_(work, ZuMv(app), ZuMv(json), ZuMv(complete));
  });
}

void Server::passkeyBegin_(
    const Work &work, AppServer app, String json, ServerFn complete)
{
  if (!m_db || !complete) return;
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  if (m_config.authMethod == AuthMethod::OIDC) {
    done->finish(jsonReply(OAuthError::AccessDenied));
    return;
  }
  PasskeyStart start;
  if (json.length() > m_config.limits.json ||
      !::Zum::passkeyStart(json, start)) {
    done->finish(jsonReply(OAuthError::InvalidRequest));
    return;
  }
  String cookie;
  Bytes binding;
  if (!cookie_(cookie, binding)) { done->finish(serverError()); return; }
  String setCookie = setCookie_(cookie);
  if (cookie.mutable_()) ZuClear(cookie);
  auto admit = m_admit;
  admit(start, [
    credit = work, this, app = ZuMv(app), start = ZuMv(start), binding = ZuMv(binding),
      setCookie = ZuMv(setCookie), done](
      PasskeyAdmission admission) mutable {
    auto work = ZuMv(credit);
    passkeyAdmitted_(work, ZuMv(app), ZuMv(start), ZuMv(admission), ZuMv(binding),
      ZuMv(setCookie), [credit = work, done](ServerReply reply) mutable {
	auto work = ZuMv(credit);
	done->finish(ZuMv(reply));
      });
  });
}

void Server::passkeyAdmitted_(
    const Work &work, AppServer app, PasskeyStart start,
    PasskeyAdmission admission, Bytes binding, String setCookie, ServerFn complete)
{
  ZmRef<ReplyComplete_> replyDone = new ReplyComplete_{ZuMv(complete)};
  if (!admission.allowed) {
    auto reply = jsonReply(OAuthError::AccessDenied);
    reply.setCookie = setCookie_({}, true);
    replyDone->finish(ZuMv(reply));
    return;
  }
  int64_t now = now_();
  auto done = [credit = work, setCookie = ZuMv(setCookie), replyDone](
      int error, EnrollmentBeginResult result) mutable {
    auto work = ZuMv(credit);
    if (error) {
      auto reply = jsonReply(OAuthError::AccessDenied);
      reply.setCookie = ZuMv(setCookie);
      replyDone->finish(ZuMv(reply));
      return;
    }
    replyDone->finish(ServerReply{
      .body = ceremonyJSON(result.ceremonyID, result.options),
      .setCookie = ZuMv(setCookie), .type = ReplyType::OK});
  };
  bool started = false;
  switch (start.type) {
    case PasskeyStartType::Enrollment: {
      auto config = ZuMv(admission.enrollment);
      config.issuer = app.issuer;
      config.rpID = m_config.rpID;
      config.rpName = m_config.rpName;
      config.now = now;
      config.expires = now + m_config.ceremonyLifetime;
      config.timeout = m_config.passkeyTimeout;
      started = bootstrapBegin(work, m_requests, deadline_(), m_db, m_rng,
        ZuMv(start.capability), ZuMv(binding), ZuMv(config), ZuMv(done));
    } break;
    case PasskeyStartType::Bootstrap: {
      auto config = ZuMv(admission.enrollment);
      config.issuer = app.issuer;
      config.rpID = m_config.rpID;
      config.rpName = m_config.rpName;
      config.now = now;
      config.expires = now + m_config.ceremonyLifetime;
      config.timeout = m_config.passkeyTimeout;
      started = bootstrapBegin(work, m_requests, deadline_(), m_db, m_rng,
        ZuMv(start.capability), ZuMv(binding), ZuMv(config), ZuMv(done));
    } break;
    case PasskeyStartType::AddCredential: {
      auto config = ZuMv(admission.credential);
      config.issuer = app.issuer;
      config.rpID = m_config.rpID;
      config.rpName = m_config.rpName;
      config.now = now;
      config.expires = now + m_config.ceremonyLifetime;
      config.timeout = m_config.passkeyTimeout;
      started = credentialBegin(work, m_requests, deadline_(), m_db, m_rng,
        ZuMv(binding), ZuMv(config), ZuMv(done));
    } break;
    case PasskeyStartType::Recovery: {
      auto config = ZuMv(admission.recovery);
      config.issuer = app.issuer;
      config.rpID = m_config.rpID;
      config.rpName = m_config.rpName;
      config.now = now;
      config.expires = now + m_config.ceremonyLifetime;
      config.timeout = m_config.passkeyTimeout;
      started = recoveryBegin(work, m_requests, deadline_(), m_db, m_rng,
        ZuMv(start.capability), ZuMv(binding), ZuMv(config), ZuMv(done));
    } break;
  }
  if (!started) replyDone->finish(serverError());
}

void Server::passkeyFinish(
    AppID appID, String query, String cookie, String json, ServerFn complete)
{
  if (!complete) return;
  auto work = work_();
  if (!work) { complete(serverError()); return; }
  app_(work, appID, [credit = work, this, query = ZuMv(query), cookie = ZuMv(cookie),
      json = ZuMv(json), complete = ZuMv(complete)](AppServer app) mutable {
    auto work = ZuMv(credit);
    if (!app) { complete(jsonReply(OAuthError::InvalidRequest)); return; }
    passkeyFinish_(work, ZuMv(app), ZuMv(query), ZuMv(cookie), ZuMv(json),
      ZuMv(complete));
  });
}

void Server::passkeyFinish_(
    const Work &work, AppServer app,
    String query, String cookie, String json, ServerFn complete)
{
  if (!m_db || !complete) return;
  if (m_config.authMethod == AuthMethod::OIDC) {
    complete(jsonReply(OAuthError::AccessDenied));
    return;
  }
  Bytes id, binding;
  if (query.length() > m_config.limits.ceremonyQuery ||
      json.length() > m_config.limits.json || !ceremonyQuery(query, id) ||
      !binding_(cookie, binding)) {
    complete(jsonReply(OAuthError::InvalidRequest));
    return;
  }
  if (!json.mutable_()) json.length(json.length());
  finishGrant_(work, ZuMv(app), ZuMv(id), ZuMv(binding), ZuMv(json), ZuMv(complete));
}

void Server::finishGrant_(
    const Work &work, AppServer app, Bytes id, Bytes binding, String json, ServerFn complete)
{
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  auto grants = m_db->grants;
  grants->run(0, [
    credit = work, this, grants, app = ZuMv(app), id = ZuMv(id), binding = ZuMv(binding),
      json = ZuMv(json), done]() mutable {
    auto work = ZuMv(credit);
    grants->find<0>(0, ZuFwdTuple(id), [credit = work, this, app = ZuMv(app), id = ZuMv(id),
      binding = ZuMv(binding), json = ZuMv(json),
      done](ZdbRowRef<Grant> row) mutable {
      auto work = ZuMv(credit);
      if (!row || row->data().issuer != app.issuer ||
          row->data().appID != app.appID ||
          row->data().kind != GrantKind::Ceremony ||
          row->data().state != State::Active || row->data().owner) {
        auto reply = jsonReply(OAuthError::AccessDenied);
        reply.setCookie = setCookie_({}, true);
        done->finish(ZuMv(reply));
        return;
      }
      auto purpose = row->data().purpose;
      int64_t now = now_();
      if (purpose == GrantPurpose::Authorization) {
        Bytes sessionGrant{id};
        AssertionInput input;
        int error = parseAssertion(json,
          WebAuthnInputLimits{}, input);
        if (error || !authorizeFinish(work, m_requests, deadline_(), m_db,
            m_rng, ZuMv(id), ZuMv(binding), ZuMv(input),
            AuthorizeFinishConfig{
	      .issuer = app.issuer, .appID = app.appID,
	      .origin = m_config.issuer, .rpID = m_config.rpID,
              .now = now, .codeExpires = now + m_config.codeLifetime,
              .consent = true
            }, m_policy, [credit = work, this, appID = app.appID,
		issuer = app.issuer,
		sessionIdle = app.sessionIdle,
		sessionAbsolute = app.sessionAbsolute,
                id = ZuMv(sessionGrant), done](
                int error, String location) mutable {
	      auto work = ZuMv(credit);
	      if (error == AuthorizeIssue::OK) {
                sessionReply_(work, ZuMv(id), ZuMv(location), ZuMv(issuer),
		  sessionIdle,
		  sessionAbsolute,
                  [credit = work, done](ServerReply reply) mutable {
		    auto work = ZuMv(credit);
		    done->finish(ZuMv(reply));
                  });
              }
              else if (error == AuthorizeIssue::Consent) {
                consentReply(m_db, appID, ZuMv(id), now_(), {},
                  [credit = work, done](ServerReply reply) mutable {
		    auto work = ZuMv(credit);
		    done->finish(ZuMv(reply));
                  });
              }
              else {
                auto reply = jsonReply(error);
                reply.setCookie = setCookie_({}, true);
                done->finish(ZuMv(reply));
              }
            })) {
          auto reply = error ? jsonReply(OAuthError::AccessDenied) :
            serverError();
          reply.setCookie = setCookie_({}, true);
          done->finish(ZuMv(reply));
        }
        return;
      }

      RegistrationInput input;
      int error = parseRegistration(json,
        WebAuthnInputLimits{}, input);
      if (error) {
        auto reply = jsonReply(OAuthError::AccessDenied);
        reply.setCookie = setCookie_({}, true);
        done->finish(ZuMv(reply));
        return;
      }
      auto finish = [credit = work, this, done, purpose](int error) mutable {
	auto work = ZuMv(credit);
	if (error) {
          auto reply = jsonReply(OAuthError::AccessDenied);
          reply.setCookie = setCookie_({}, true);
          done->finish(ZuMv(reply));
          return;
        }
        if (purpose != GrantPurpose::Bootstrap) {
          done->finish(ServerReply{.body = serverJSON(OK{true}),
            .setCookie = setCookie_({}, true), .type = ReplyType::OK});
          return;
        }
        auto issuers = m_db->issuers;
        issuers->run(0, [credit = work, this, issuers, done]() mutable {
	  auto work = ZuMv(credit);
	  String issuer = m_config.issuer;
          issuers->findUpd<0>(0, ZuFwdTuple(ZuMv(issuer)), [credit = work, this, done](
              ZdbRow<Issuer> *row) mutable {
	    auto work = ZuMv(credit);
	    if (!row ||
                row->data().bootstrapPhase != BootstrapPhase::AdminPending) {
              done->finish(serverError());
              return;
            }
            row->data().bootstrapPhase = BootstrapPhase::Ready;
            if (!row->commit()) {
              done->finish(serverError());
              return;
            }
            done->finish(ServerReply{.body = serverJSON(OK{true}),
              .setCookie = setCookie_({}, true), .type = ReplyType::OK});
          });
        });
      };
      EnrollmentFinishConfig config{
        .origin = m_config.issuer, .rpID = m_config.rpID,
        .credentialIDMax = m_config.limits.credentialID, .now = now};
      bool started;
      switch (purpose) {
        case GrantPurpose::Enrollment:
        case GrantPurpose::Bootstrap:
          started = enrollmentFinish(work, m_requests, deadline_(), m_db,
            ZuMv(id), ZuMv(binding), ZuMv(input),
            ZuMv(config), ZuMv(finish));
          break;
        case GrantPurpose::AddCredential:
          started = credentialFinish(work, m_requests, deadline_(), m_db,
            ZuMv(id), ZuMv(binding), ZuMv(input),
            ZuMv(config), ZuMv(finish));
          break;
        case GrantPurpose::Recovery:
          started = recoveryFinish(work, m_requests, deadline_(), m_db,
            ZuMv(id), ZuMv(binding), ZuMv(input),
            ZuMv(config), ZuMv(finish));
          break;
        default:
          started = false;
          break;
      }
      if (!started) done->finish(serverError());
    });
  });
}

void Server::sessionReply_(
    const Work &work, Bytes grantID, String location, String issuer,
    uint32_t sessionIdle, uint32_t sessionAbsolute, ServerFn complete)
{
  int64_t now = now_();
  if (now <= 0 || !sessionIssueGrant(work, m_requests, deadline_(), m_db,
      m_rng, ZuMv(grantID), ZuMv(issuer), now, sessionIdle,
      sessionAbsolute, [credit = work, this, location = ZuMv(location), sessionAbsolute,
        complete = ZuMv(complete)](int error, Session, String token) mutable {
	auto work = ZuMv(credit);
	complete(ServerReply{.location = ZuMv(location),
          .setCookie = error == SessionError::OK ?
            setCookie_(token, false, sessionAbsolute) :
            setCookie_({}, true), .type = ReplyType::Redirect});
      })) {
    complete(ServerReply{.location = ZuMv(location),
      .setCookie = setCookie_({}, true), .type = ReplyType::Redirect});
  }
}

void Server::oidcCallback(
    AppID appID, String query, String cookie, ServerFn complete)
{
  if (!complete) return;
  auto work = work_();
  if (!work) { complete(serverError()); return; }
  app_(work, appID, [credit = work, this, query = ZuMv(query), cookie = ZuMv(cookie),
      complete = ZuMv(complete)](AppServer app) mutable {
    auto work = ZuMv(credit);
    if (!app) { complete(jsonReply(OAuthError::InvalidRequest)); return; }
    oidcCallback_(work, ZuMv(app), ZuMv(query), ZuMv(cookie), ZuMv(complete));
  });
}

void Server::oidcCallback_(
    const Work &work, AppServer app, String query, String cookie, ServerFn complete)
{
  if (!m_db || !complete) return;
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  Bytes binding;
  if (m_config.authMethod == AuthMethod::Passkey ||
      !binding_(cookie, binding)) {
    auto reply = jsonReply(OAuthError::InvalidRequest);
    reply.setCookie = setCookie_({}, true);
    done->finish(ZuMv(reply));
    return;
  }
  if (!m_oidc.finish(app.appID, ZuMv(query),
      [credit = work, this, app = ZuMv(app), binding = ZuMv(binding), done](
      bool ok, Bytes grantID, User user, IDVec roleIDs,
      Evidence evidence, int64_t authTime) mutable {
    auto work = ZuMv(credit);
    int64_t now = now_();
    Bytes sessionGrant{grantID};
    if (!ok || now <= 0 || !authorizeOIDCFinish(work,
        m_requests, deadline_(), m_db, m_rng, ZuMv(grantID),
        ZuMv(binding), ZuMv(user), ZuMv(roleIDs), ZuMv(evidence),
        authTime,
        AuthorizeFinishConfig{.issuer = app.issuer, .appID = app.appID,
	  .origin = m_config.issuer, .now = now,
          .codeExpires = now + m_config.codeLifetime, .consent = true}, m_policy,
        [credit = work, this, appID = app.appID, issuer = app.issuer,
	    sessionIdle = app.sessionIdle,
	    sessionAbsolute = app.sessionAbsolute,
	    id = ZuMv(sessionGrant), done](
            int error, String location) mutable {
	  auto work = ZuMv(credit);
	  if (error == AuthorizeIssue::OK)
            sessionReply_(work, ZuMv(id), ZuMv(location), ZuMv(issuer), sessionIdle,
	      sessionAbsolute,
              [credit = work, done](ServerReply reply) mutable {
		auto work = ZuMv(credit);
		done->finish(ZuMv(reply));
              });
          else if (error == AuthorizeIssue::Consent)
            consentReply(m_db, appID, ZuMv(id), now_(), {},
              [credit = work, done](ServerReply reply) mutable {
		auto work = ZuMv(credit);
		done->finish(ZuMv(reply));
              });
          else {
            auto reply = jsonReply(error);
            reply.setCookie = setCookie_({}, true);
            done->finish(ZuMv(reply));
          }
        })) {
      auto reply = ok ? serverError() : jsonReply(OAuthError::AccessDenied);
      reply.setCookie = setCookie_({}, true);
      done->finish(ZuMv(reply));
    }
  })) {
    auto reply = serverError();
    reply.setCookie = setCookie_({}, true);
    done->finish(ZuMv(reply));
  }
}

} // namespace Zum
