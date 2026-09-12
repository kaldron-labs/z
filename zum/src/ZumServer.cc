//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumServer.hh>
#include <zlib/ZumIdentityDB.hh>
#include <zlib/ZumAppDB.hh>
#include <zlib/ZumKeyDB.hh>

#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuICmp.hh>
#include <zlib/ZuMatcher.hh>

#include <zlib/ZfJSON.hh>

#include <zlib/ZtlsHMAC.hh>
#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsSec.hh>

namespace Zum {

struct PasskeyStartWire {
  String purpose;
  String capability;
};
ZfStruct(, (PasskeyStartWire, JSON),
  (((purpose),		(Required)),	(String)),
  (((capability),	(JSON::Opt)),	(String)));
struct PasskeyStartTypes {
  using Keys = ZuStringTL<"enrollment", "bootstrap", "add", "recovery">;
};

struct CeremonyWire {
  String ceremony;
  ZfJSON::Union<> options;
};
ZfStruct(, (CeremonyWire, JSON),
  (((ceremony),		(Required)),	(String)),
  (((options),		(Required)),	(UDT)));

struct AuthorizationWire {
  String authorizationURL;
  uint64_t expiresIn = 0;
};
ZfStruct(, (AuthorizationWire, JSON),
  (((authorizationURL),	(Required)),	(String)),
  (((expiresIn),	(Required)),	(UInt64)));

struct StatusWire { String status; };
ZfStruct(, (StatusWire, JSON),
  (((status),		(Required)),	(String)));
struct OKWire { bool ok = false; };
ZfStruct(, (OKWire, JSON),
  (((ok),		(Required)),	(Bool)));
struct BearerErrorWire { String error; };
ZfStruct(, (BearerErrorWire, JSON),
  (((error),		(Required)),	(String)));

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
  ZuCSpan encoded;
  bool valid = true;
  unsigned seen = 0;
  valid = formEach(form.span(), [&valid, &seen, &encoded, &login](
      ZuCSpan name, ZuCSpan value) {
    unsigned bit = 0;
    if (name == "id") {
      bit = 1U;
      encoded = value;
    } else if (name == "login") {
      bit = 2U;
      login = value;
    } else {
      valid = false;
      return;
    }
    if (seen & bit) valid = false;
    seen |= bit;
  }) && valid;
  return valid && seen == 3U && login && login.length() <= 1024 &&
    decodeID(encoded, id);
}

static bool consentForm(String &form, Bytes &id, bool &approve)
{
  ZuCSpan encoded;
  ZuCSpan decision;
  bool valid = true;
  unsigned seen = 0;
  valid = formEach(form.span(), [&valid, &seen, &encoded, &decision](
      ZuCSpan name, ZuCSpan value) {
    unsigned bit = 0;
    if (name == "id") {
      bit = 1U;
      encoded = value;
    } else if (name == "decision") {
      bit = 2U;
      decision = value;
    } else {
      valid = false;
      return;
    }
    if (seen & bit) valid = false;
    seen |= bit;
  }) && valid;
  if (!valid || seen != 3U ||
      (decision != "approve" && decision != "deny") ||
      !decodeID(encoded, id)) return false;
  approve = decision == "approve";
  return true;
}

class ReplyComplete_ : public ZumObject {
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
  if (!formEach({query.data(), query.length()},
    [&encoded](ZuCSpan name, ZuCSpan value) {
      if (name == "id") encoded = value;
    })) return false;
  return encoded && decodeID(encoded, id);
}

static bool facadeQuery(String &query, Bytes &id)
{
  if (query && query[0] == '?') query.splice(0, 1);
  if (!query.mutable_()) query.length(query.length());
  ZuCSpan encoded;
  unsigned count = 0;
  if (!formEach({query.data(), query.length()},
    [&encoded, &count](ZuCSpan name, ZuCSpan value) {
      ++count;
      if (name == "request") encoded = value;
    })) return false;
  return count == 1 && encoded && decodeID(encoded, id);
}

static bool passkeyStart(String &json, PasskeyStart &start)
{
  if (!json.mutable_()) json.length(json.length());
  auto parsed = ZfJSON::scan({json.data(), json.length()});
  if (parsed.p<0>() != int(json.length()) || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 || !ZfJSON::unique(roots[0]) ||
      !roots[0]->has<ZfJSON::AnyNode::Object>()) return false;
  auto wire = ZfJSON::handler<PasskeyStartWire>(roots[0]).ctor();
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
  if (roots.length() != 1 || !ZfJSON::unique(roots[0]) ||
      !roots[0]->has<ZfJSON::AnyNode::Object>()) return {};
  auto encoded = encodeID(id);
  String body;
  ZfJSON::save(body, CeremonyWire{ZuMv(encoded),
    static_cast<const ZfJSON::AnyNode *>(roots[0].ptr())});
  return body;
}

static String consentPage(ZuBSpan id)
{
  String page{
    "<!doctype html><meta charset=utf-8><title>Zum consent</title>"
    "<meta name=referrer content=no-referrer><h1>Authorize application</h1>"
    "<p>The application requests the scopes shown in the authorization "
    "request.</p><form method=post action=/consent><input type=hidden name=id "
    "value=\""};
  page << encodeID(id) << "\"><button name=decision value=approve>Approve"
    "</button><button name=decision value=deny>Deny</button></form>";
  return page;
}

bool Server::init(
    DB *db, DBContext *context, Requests *requests, ServerConfig config,
    ClockFn clock, PageFn page, PolicyFn policy, AdmitFn admit, SignFn sign,
    OIDCHTTPFn oidcHTTP, AuthRouteFn authRoute)
{
  if (m_db || !db || !context || !requests || !clock ||
      !policy || !sign || !config.issuer ||
      !config.cookieName || !config.cookiePath || !config.requestTimeout ||
      config.ceremonyLifetime <= 0 || config.codeLifetime <= 0 ||
      config.accessLifetime <= 0 ||
      config.refreshLifetime <= 0 || config.sessionIdle <= 0 ||
      config.sessionAbsolute <= 0 ||
      config.sessionIdle > config.sessionAbsolute || !config.refreshGenerations ||
      !config.spentTokens || !config.limits.cookie || !config.limits.jwks ||
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
      !m_rng.init() || !m_cookieRng.init()) return false;
  m_db = db;
  m_context = context;
  m_requests = requests;
  m_config = ZuMv(config);
  m_clock = ZuMv(clock);
  m_page = ZuMv(page);
  m_policy = ZuMv(policy);
  m_admit = ZuMv(admit);
  m_sign = ZuMv(sign);
  m_authRoute = ZuMv(authRoute);
  if (m_config.authMethod != AuthMethod::Passkey && !m_oidc.init(
      requests->scheduler(), requests->sid(), context,
      m_config.limits.oidc, m_config.limits.oidcPending,
      m_config.oidcTimeout, [this]() { return now_(); }, ZuMv(oidcHTTP))) {
    final();
    return false;
  }
  return true;
}

void Server::stop()
{
  m_oidc.final();
}

void Server::final()
{
  stop();
  m_authRoute = AuthRouteFn{};
  m_sign = SignFn{};
  m_admit = AdmitFn{};
  m_policy = PolicyFn{};
  m_page = PageFn{};
  m_clock = ClockFn{};
  m_config = {};
  m_requests = nullptr;
  m_context = nullptr;
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
  if (!opaqueIssue(m_cookieRng, token)) return false;
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
  ZuClear(digest.data(), digest.length());
  return encoded;
}

void Server::authorize(String query, ServerFn complete)
{
  authorize(ZuMv(query), String{}, ZuMv(complete));
}

void Server::authorize(String query, String browserCookie, ServerFn complete)
{
  if (!m_db || !complete) return;
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  if (query.length() > m_config.limits.form) {
    done->finish(jsonReply(OAuthError::InvalidRequest));
    return;
  }
  String reference{query};
  String sessionToken;
  Bytes sessionDigest;
  (void)binding_(browserCookie, sessionToken, sessionDigest);
  Bytes id;
  if (!facadeQuery(reference, id)) {
    authorize_(ZuMv(query), {}, 0, ZuMv(sessionToken),
      [done](ServerReply reply) mutable {
      done->finish(ZuMv(reply));
    });
    return;
  }
  String cookie;
  Bytes binding;
  if (!cookie_(cookie, binding)) { done->finish(serverError()); return; }
  String setCookie = setCookie_(cookie);
  if (cookie.mutable_()) ZuClear(cookie.data(), cookie.length());
  int64_t now = now_();
  if (now <= 0) { done->finish(serverError()); return; }
  auto grants = m_context->grants;
  bool queued = m_requests->run(deadline_(), [this, grants,
      id = ZuMv(id), binding = ZuMv(binding),
      sessionToken = ZuMv(sessionToken), now,
      setCookie = ZuMv(setCookie), done](ZmRef<Request> request) mutable {
    grants->run(0, [this, grants, id = ZuMv(id), binding = ZuMv(binding),
        sessionToken = ZuMv(sessionToken), now, setCookie = ZuMv(setCookie), done,
        request = ZuMv(request)]() mutable {
      grants->findUpd<0>(0, ZuFwdTuple(ZuMv(id)), [this,
          binding = ZuMv(binding), sessionToken = ZuMv(sessionToken),
          now, setCookie = ZuMv(setCookie), done,
          request = ZuMv(request)](ZdbRow<Grant> *row) mutable {
        int error = OAuthError::InvalidRequest;
        AuthorizeResult result;
        if (row && row->data().kind == GrantKind::Ceremony &&
            row->data().purpose == GrantPurpose::Authorization &&
            row->data().state == State::Active && !row->data().owner &&
            row->data().facadeClientID && !row->data().generation &&
            row->data().expires > now) {
          auto &grant = row->data();
          grant.bindingDigest = binding;
          grant.generation = 1;
          result.ceremonyID = grant.id;
          result.appID = grant.appID;
          result.redirectURI = grant.redirectURI;
          result.state = grant.oauthState;
          result.statePresent = grant.oauthStatePresent;
          result.prompt = grant.prompt;
          result.promptPresent = grant.promptPresent;
          result.maxAge = grant.maxAge;
          result.maxAgePresent = grant.maxAgePresent;
          result.redirect = true;
          if (m_config.authMethod != AuthMethod::OIDC)
            result.options = assertionOptions(grant.challenge,
              m_config.rpID, m_config.passkeyTimeout);
          if (row->commit()) error = AuthorizeIssue::OK;
          else error = OAuthError::ServerError;
        }
        request->complete([this, error, result = ZuMv(result),
            binding = ZuMv(binding), sessionToken = ZuMv(sessionToken),
            setCookie = ZuMv(setCookie), done]() mutable {
          if (error == AuthorizeIssue::OK)
            sessionAuthorize_(ZuMv(result), ZuMv(binding),
              ZuMv(sessionToken), ZuMv(setCookie),
              [done](ServerReply reply) mutable { done->finish(ZuMv(reply)); });
          else
            authorizeReply_(error, ZuMv(result), ZuMv(setCookie),
              [done](ServerReply reply) mutable { done->finish(ZuMv(reply)); });
        });
      });
    });
  }, [done]() mutable {
    done->finish(jsonReply(OAuthError::TemporarilyUnavailable));
  });
  if (!queued) done->finish(serverError());
}

void Server::authorize(
    String query, String facadeClientID, AppID facadeAppID,
    ServerFn complete)
{
  authorize_(ZuMv(query), ZuMv(facadeClientID), facadeAppID, {},
    ZuMv(complete));
}

void Server::authorize_(String query, String facadeClientID,
    AppID facadeAppID, String sessionToken, ServerFn complete)
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
  if (cookie.mutable_()) ZuClear(cookie.data(), cookie.length());
  int64_t now = now_();
  if (now <= 0) {
    done->finish(serverError());
    return;
  }
  Bytes sessionBinding{binding};
  if (!authorizeRequest(m_requests, deadline_(), m_context, m_rng,
      ZuMv(query), ZuMv(binding), AuthorizeConfig{
        .issuer = m_config.issuer,
        .rpID = m_config.authMethod != AuthMethod::OIDC ?
          m_config.rpID : String{},
        .now = now,
        .expires = now + m_config.ceremonyLifetime,
        .timeout = m_config.authMethod != AuthMethod::OIDC ?
          m_config.passkeyTimeout : 0,
        .passkey = m_config.authMethod != AuthMethod::OIDC,
        .facadeClientID = ZuMv(facadeClientID),
        .facadeAppID = facadeAppID
      }, [this, binding = ZuMv(sessionBinding),
          sessionToken = ZuMv(sessionToken),
          setCookie = ZuMv(setCookie), done](
          int error, AuthorizeResult result) mutable {
        if (error == AuthorizeIssue::OK)
          sessionAuthorize_(ZuMv(result), ZuMv(binding),
            ZuMv(sessionToken), ZuMv(setCookie),
            [done](ServerReply reply) mutable { done->finish(ZuMv(reply)); });
        else
          authorizeReply_(error, ZuMv(result), ZuMv(setCookie),
            [done](ServerReply reply) mutable { done->finish(ZuMv(reply)); });
      })) done->finish(serverError());
}

void Server::sessionAuthorize_(AuthorizeResult result, Bytes binding,
    String sessionToken, String setCookie, ServerFn complete)
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
    authorizeReply_(AuthorizeIssue::OK, ZuMv(result), ZuMv(setCookie),
      [done](ServerReply reply) mutable { done->finish(ZuMv(reply)); });
    return;
  }
  int64_t now = now_();
  if (now <= 0 || !sessionUse(m_requests, deadline_(), m_context,
      ZuMv(sessionToken), String{m_config.issuer}, now, m_config.sessionIdle,
      [this, result = ZuMv(result), binding = ZuMv(binding),
	  setCookie = ZuMv(setCookie), none, now, done](int error,
	    Session session, String) mutable {
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
	  authorizeReply_(AuthorizeIssue::OK, ZuMv(result), ZuMv(setCookie),
	    [done](ServerReply reply) mutable { done->finish(ZuMv(reply)); });
	  return;
	}
	if (!authorizeSessionFinish(m_requests, deadline_(), m_context, m_rng,
	    ZuMv(result.ceremonyID), ZuMv(binding), ZuMv(session),
	    AuthorizeFinishConfig{.now = now,
	      .codeExpires = now + m_config.codeLifetime, .consent = true},
	    m_policy,
	    [this, result = ZuMv(result), setCookie = ZuMv(setCookie),
	        none, done](
		int error, String location) mutable {
	      if (error == AuthorizeIssue::OK) {
		done->finish(ServerReply{.location = ZuMv(location),
		  .type = ReplyType::Redirect});
		return;
	      }
	      if (error == AuthorizeIssue::Consent) {
		done->finish(ServerReply{
		  .body = consentPage(result.ceremonyID),
		  .setCookie = ZuMv(setCookie), .type = ReplyType::Page});
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

void Server::authorizeReply_(int error, AuthorizeResult result,
    String setCookie, ServerFn complete)
{
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  if (error == AuthorizeIssue::OK) {
          auto local = [this, done](AuthorizeResult result,
              String setCookie) mutable {
            String body = m_page(
              ZuMv(result.ceremonyID), ZuMv(result.options));
            done->finish(ServerReply{.body = ZuMv(body),
              .setCookie = ZuMv(setCookie), .type = ReplyType::Page});
          };
          auto upstream = [this, done](AuthorizeResult result,
              OIDCConfig oidc, String setCookie) mutable {
            oidc.maxAge = result.maxAge;
            oidc.maxAgePresent = result.maxAgePresent;
            if (result.promptPresent && result.prompt == "login") {
              oidc.prompt = "login";
              oidc.maxAge = 0;
              oidc.maxAgePresent = true;
            }
            if (!m_oidc.begin(ZuMv(result.ceremonyID), ZuMv(oidc),
              [this, setCookie = ZuMv(setCookie), done](
                  bool ok, String location) mutable {
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
            upstream(ZuMv(result), m_config.oidc, ZuMv(setCookie));
            return;
          }
          auto route = m_authRoute;
          AppID appID = result.appID;
          String hint = result.loginHint;
            route(appID, ZuMv(hint), [this, done, local = ZuMv(local),
              upstream = ZuMv(upstream), result = ZuMv(result),
              setCookie = ZuMv(setCookie)](AuthRoute route) mutable {
            switch (route.type) {
              case AuthRouteType::Local:
                local(ZuMv(result), ZuMv(setCookie));
                return;
              case AuthRouteType::Upstream:
                upstream(ZuMv(result), ZuMv(route.oidc), ZuMv(setCookie));
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

void Server::facadeAuthorize(String query, String facadeClientID,
    AppID facadeAppID, ServerFn complete)
{
  if (!m_db || !complete) return;
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  if (!facadeClientID || !facadeAppID ||
      query.length() > m_config.limits.form) {
    done->finish(jsonReply(OAuthError::InvalidRequest));
    return;
  }
  String discarded;
  Bytes binding;
  if (!cookie_(discarded, binding)) { done->finish(serverError()); return; }
  if (discarded.mutable_()) ZuClear(discarded.data(), discarded.length());
  int64_t now = now_();
  if (now <= 0 || !authorizeRequest(m_requests, deadline_(), m_context, m_rng,
      ZuMv(query), ZuMv(binding), AuthorizeConfig{
        .issuer = m_config.issuer,
        .rpID = m_config.authMethod != AuthMethod::OIDC ?
          m_config.rpID : String{},
        .now = now,
        .expires = now + m_config.ceremonyLifetime,
        .timeout = m_config.authMethod != AuthMethod::OIDC ?
          m_config.passkeyTimeout : 0,
        .passkey = m_config.authMethod != AuthMethod::OIDC,
        .facadeClientID = ZuMv(facadeClientID),
        .facadeAppID = facadeAppID
      }, [this, done](int error, AuthorizeResult result) mutable {
        if (error != AuthorizeIssue::OK) {
          done->finish(jsonReply(error));
          return;
        }
        String url = m_config.issuer;
        if (url[url.length() - 1] == '/') url.length(url.length() - 1);
        url << "/authorize?request=" << encodeID(result.ceremonyID);
        String body = serverJSON(AuthorizationWire{
          ZuMv(url), uint64_t(m_config.ceremonyLifetime)});
        done->finish(ServerReply{.body = ZuMv(body), .type = ReplyType::OK});
      })) done->finish(serverError());
}

void Server::token(String form, String authorization, ServerFn complete)
{
  token(ZuMv(form), ZuMv(authorization), {}, ZuMv(complete));
}

void Server::token(String form, String authorization,
    String facadeClientID, ServerFn complete)
{
  if (!m_db || !complete) return;
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  if (form.length() > m_config.limits.form) {
    done->finish(jsonReply(OAuthError::InvalidRequest));
    return;
  }
  int64_t now = now_();
  if (now <= 0 || !tokenRequest(m_requests, deadline_(), m_db, m_context,
      m_rng, ZuMv(form), ZuMv(authorization), TokenConfig{
        .issuer = m_config.issuer,
        .jwtLimits = m_config.limits.jwt,
        .maxKeys = m_config.limits.jwks,
        .now = now,
        .accessExpires = now + m_config.accessLifetime,
        .refreshExpires = now + m_config.refreshLifetime,
        .generationLimit = m_config.refreshGenerations,
        .spentLimit = m_config.spentTokens,
        .facadeClientID = ZuMv(facadeClientID)
      }, m_sign, [done](
          int error, TokenResponse response) mutable {
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
}

void Server::revoke(String form, String authorization, ServerFn complete)
{
  revoke(ZuMv(form), ZuMv(authorization), {}, ZuMv(complete));
}

void Server::revoke(String form, String authorization,
    String facadeClientID, ServerFn complete)
{
  if (!m_db || !complete) return;
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  if (form.length() > m_config.limits.form) {
    done->finish(jsonReply(OAuthError::InvalidRequest));
    return;
  }
  int64_t now = now_();
  if (now <= 0 || !revokeRequest(m_requests, deadline_(), m_context,
      ZuMv(form), ZuMv(authorization),
      RevokeConfig{.issuer = m_config.issuer, .now = now,
	.facadeClientID = ZuMv(facadeClientID)},
      [done](int error) mutable {
        if (error == RevokeIssue::OK)
          done->finish(ServerReply{.type = ReplyType::Empty});
        else {
          auto reply = jsonReply(error);
          if (error == OAuthError::InvalidClient)
            reply.type = ReplyType::ClientError;
          done->finish(ZuMv(reply));
        }
      })) done->finish(serverError());
}

void Server::login(String cookie, ServerFn complete)
{
  if (!m_db || !complete) return;
  String token;
  Bytes digest;
  String csrf;
  if (binding_(cookie, token, digest)) csrf = csrf_(digest);
  if (token && token.mutable_()) ZuClear(token.data(), token.length());
  String body{
    "<!doctype html><meta charset=utf-8><title>Zum login</title>"
    "<meta name=referrer content=no-referrer><h1>Zum</h1>"};
  if (csrf) {
    body << "<p>Signed in to Zum.</p><form method=post action=/logout>"
      "<input type=hidden name=csrf value=\"" << csrf <<
      "\"><button>Sign out of Zum</button></form>";
  } else {
    body << "<p>Start sign-in from an enrolled application.</p>";
  }
  complete(ServerReply{.body = ZuMv(body), .type = ReplyType::Page});
}

void Server::login(String form, String cookie, ServerFn complete)
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
  auto grants = m_context->grants;
  bool queued = m_requests->run(deadline_(), [this, grants, id = ZuMv(id),
      login = ZuMv(login), binding = ZuMv(binding), now, done](
      ZmRef<Request> request) mutable {
    grants->run(0, [this, grants, id = ZuMv(id), login = ZuMv(login),
        binding = ZuMv(binding), now, done, request = ZuMv(request)]() mutable {
      grants->find<0>(0, ZuFwdTuple(ZuMv(id)), [this,
          login = ZuMv(login), binding = ZuMv(binding), now, done,
          request = ZuMv(request)](ZdbRowRef<Grant> row) mutable {
        int error = OAuthError::InvalidRequest;
        AuthorizeResult result;
        if (row && row->data().kind == GrantKind::Ceremony &&
            row->data().purpose == GrantPurpose::Authorization &&
            row->data().state == State::Active && !row->data().owner &&
            row->data().expires > now &&
            Ztls::ctEqual(row->data().bindingDigest, binding)) {
          const auto &grant = row->data();
          result.ceremonyID = grant.id;
          result.appID = grant.appID;
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
        request->complete([this, error, result = ZuMv(result), done]() mutable {
          authorizeReply_(error, ZuMv(result), {},
            [done](ServerReply reply) mutable { done->finish(ZuMv(reply)); });
        });
      });
    });
  }, [done]() mutable {
    done->finish(jsonReply(OAuthError::TemporarilyUnavailable));
  });
  if (!queued) done->finish(serverError());
}

void Server::consent(String form, String cookie, ServerFn complete)
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
  if (now <= 0 || !authorizeConsentFinish(m_requests, deadline_(), m_db, m_context,
      m_rng, ZuMv(id), ZuMv(binding), approve,
      AuthorizeFinishConfig{.now = now,
        .codeExpires = now + m_config.codeLifetime}, m_policy,
      [this, id = ZuMv(sessionGrant), done](
          int error, String location) mutable {
        if (error == AuthorizeIssue::OK) {
          sessionReply_(ZuMv(id), ZuMv(location),
            [done](ServerReply reply) mutable { done->finish(ZuMv(reply)); });
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

void Server::logout(String form, String cookie, ServerFn complete)
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
    ZuClear(submitted.data(), submitted.length());
  if (expected && expected.mutable_())
    ZuClear(expected.data(), expected.length());
  if (!matches) {
    if (token && token.mutable_()) ZuClear(token.data(), token.length());
    done->finish(jsonReply(OAuthError::InvalidRequest));
    return;
  }
  int64_t now = now_();
  if (now <= 0 || !sessionRevoke(m_requests, deadline_(), m_context,
      ZuMv(token), now, [this, done](int error) mutable {
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

struct ReadyKey_ : public ZumObject {
  SignKey key;
  Bytes digest;
};

void Server::ready(ServerFn complete)
{
  if (!m_db || !complete) return;
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  if (!m_requests->run(deadline_(), [this, done](ZmRef<Request> request) mutable {
    ServerFn finish{[done, request = ZuMv(request)](ServerReply reply) mutable {
      request->complete([done, reply = ZuMv(reply)]() mutable {
        done->finish(ZuMv(reply));
      });
    }};
    m_context->issuers->find<0>(0, ZuFwdTuple(m_config.issuer),
      [this, finish = ZuMv(finish)](ZdbRowRef<Issuer> issuer) mutable {
        int64_t now = now_();
        if (!issuer || issuer->data().bootstrapPhase != BootstrapPhase::Ready ||
            now <= 0 || m_config.accessLifetime > INT64_MAX - now) {
          finish(serverError());
          return;
        }
        signKeyLoad(m_context, m_config.issuer, now, now + m_config.accessLifetime,
          m_config.limits.jwks, [this, finish = ZuMv(finish)](SignKey key) mutable {
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
            m_sign(probe->key, probe->digest, [probe, finish = ZuMv(finish)](
                Bytes signature) mutable {
              bool ok = bool(signature);
              if (signature.mutable_()) ZuClear(signature.data(), signature.length());
              finish(ok ? ServerReply{.body = serverJSON(StatusWire{"ready"}),
                .type = ReplyType::OK} : serverError());
            });
          });
      });
  }, [done]() mutable { done->finish(serverError()); }))
    done->finish(serverError());
}

void Server::metadata(ServerFn complete)
{
  if (!m_db || !complete) return;
  complete(ServerReply{
    .body = metadataJSON(m_config.issuer), .type = ReplyType::Discovery});
}

void Server::jwks(ServerFn complete)
{
  if (!m_db || !complete) return;
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  int64_t now = now_();
  if (now <= 0 || !jwksLoad(m_requests, deadline_(), m_context, now,
      m_config.limits.jwks, [done](
          bool ok, String json) mutable {
        if (ok) done->finish(ServerReply{
          .body = ZuMv(json), .type = ReplyType::Discovery});
        else done->finish(serverError());
      })) done->finish(serverError());
}

void Server::userInfo(String authorization, ServerFn complete)
{
  if (!m_db || !complete) return;
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  if (!m_requests->run(deadline_(), [this, done,
      authorization = ZuMv(authorization)](ZmRef<Request> request) mutable {
    userInfo_(ZuMv(authorization), [done, request = ZuMv(request)](
        ServerReply reply) mutable {
      request->complete([done, reply = ZuMv(reply)]() mutable {
        done->finish(ZuMv(reply));
      });
    });
  }, [done]() mutable { done->finish(serverError()); }))
    done->finish(serverError());
}

void Server::userInfo_(String authorization, ServerFn complete)
{
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  static constexpr ZuCSpan prefix{"Bearer "};
  if (authorization.length() <= prefix.length() ||
      !ZuICmp<ZuCSpan>::equals(
        ZuCSpan{authorization.data(), prefix.length()}, prefix)) {
    done->finish(ServerReply{
      .body = serverJSON(BearerErrorWire{"invalid_token"}),
      .type = ReplyType::BearerError});
    return;
  }
  authorization.splice(0, prefix.length());
  JWTHeader header;
  int64_t now = now_();
  if (now <= 0 || !jwtHeader(authorization, m_config.limits.jwt, header)) {
    done->finish(ServerReply{
      .body = serverJSON(BearerErrorWire{"invalid_token"}),
      .type = ReplyType::BearerError});
    return;
  }
  m_context->signKeys->find<0>(0, ZuFwdTuple(ZuMv(header.keyID)), [
    this, done, now, authorization = ZuMv(authorization)
  ](ZdbRowRef<SignKey> row) mutable {
    Principal principal;
    if (!row || !signKeyVerify(row->data(), authorization,
	m_config.issuer, {}, now, m_config.limits.jwt, principal) ||
	!scopeContains(principal.scope, "openid")) {
      done->finish(ServerReply{
	.body = serverJSON(BearerErrorWire{"invalid_token"}),
	.type = ReplyType::BearerError});
      return;
    }
    userInfo_(ZuMv(principal), [done](ServerReply reply) mutable {
      done->finish(ZuMv(reply));
    });
  });
}

void Server::userInfo_(Principal principal, ServerFn complete)
{
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  Bytes handle;
  if (!decodeID(principal.subject, handle)) {
    done->finish(ServerReply{
      .body = serverJSON(BearerErrorWire{"invalid_token"}),
      .type = ReplyType::BearerError});
    return;
  }
  String clientID = principal.clientID;
  m_context->clients->find<0>(0, ZuFwdTuple(ZuMv(clientID)), [
    this, done, handle = ZuMv(handle), principal = ZuMv(principal)
  ](ZdbRowRef<Client> client) mutable {
    if (!client || client->data().state != State::Active ||
        client->data().owner) {
      done->finish(ServerReply{
        .body = serverJSON(BearerErrorWire{"invalid_token"}),
        .type = ReplyType::BearerError});
      return;
    }
    m_context->users->find<1>(0, ZuFwdTuple(ZuMv(handle)), [
      done, principal = ZuMv(principal)
    ](ZdbRowRef<User> user) mutable {
      String json;
      if (!user || !userInfoJSON(user->data(), principal, json)) {
        done->finish(ServerReply{
          .body = serverJSON(BearerErrorWire{"invalid_token"}),
          .type = ReplyType::BearerError});
        return;
      }
      done->finish(ServerReply{.body = ZuMv(json), .type = ReplyType::OK});
    });
  });
}

void Server::passkeyBegin(String json, ServerFn complete)
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
  if (cookie.mutable_()) ZuClear(cookie.data(), cookie.length());
  auto admit = m_admit;
  admit(start, [this, start = ZuMv(start), binding = ZuMv(binding),
      setCookie = ZuMv(setCookie), done](
      PasskeyAdmission admission) mutable {
    passkeyAdmitted_(ZuMv(start), ZuMv(admission), ZuMv(binding),
      ZuMv(setCookie), [done](ServerReply reply) mutable {
        done->finish(ZuMv(reply));
      });
  });
}

void Server::passkeyAdmitted_(
    PasskeyStart start, PasskeyAdmission admission, Bytes binding,
    String setCookie, ServerFn complete)
{
  ZmRef<ReplyComplete_> replyDone = new ReplyComplete_{ZuMv(complete)};
  if (!admission.allowed) {
    auto reply = jsonReply(OAuthError::AccessDenied);
    reply.setCookie = setCookie_({}, true);
    replyDone->finish(ZuMv(reply));
    return;
  }
  int64_t now = now_();
  auto done = [setCookie = ZuMv(setCookie), replyDone](
      int error, EnrollmentBeginResult result) mutable {
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
      config.issuer = m_config.issuer;
      config.rpID = m_config.rpID;
      config.rpName = m_config.rpName;
      config.now = now;
      config.expires = now + m_config.ceremonyLifetime;
      config.timeout = m_config.passkeyTimeout;
      started = bootstrapBegin(m_requests, deadline_(), m_context, m_rng,
        ZuMv(start.capability), ZuMv(binding), ZuMv(config), ZuMv(done));
    } break;
    case PasskeyStartType::Bootstrap: {
      auto config = ZuMv(admission.enrollment);
      config.issuer = m_config.issuer;
      config.rpID = m_config.rpID;
      config.rpName = m_config.rpName;
      config.now = now;
      config.expires = now + m_config.ceremonyLifetime;
      config.timeout = m_config.passkeyTimeout;
      started = bootstrapBegin(m_requests, deadline_(), m_context, m_rng,
        ZuMv(start.capability), ZuMv(binding), ZuMv(config), ZuMv(done));
    } break;
    case PasskeyStartType::AddCredential: {
      auto config = ZuMv(admission.credential);
      config.issuer = m_config.issuer;
      config.rpID = m_config.rpID;
      config.rpName = m_config.rpName;
      config.now = now;
      config.expires = now + m_config.ceremonyLifetime;
      config.timeout = m_config.passkeyTimeout;
      started = credentialBegin(m_requests, deadline_(), m_context, m_rng,
        ZuMv(binding), ZuMv(config), ZuMv(done));
    } break;
    case PasskeyStartType::Recovery: {
      auto config = ZuMv(admission.recovery);
      config.issuer = m_config.issuer;
      config.rpID = m_config.rpID;
      config.rpName = m_config.rpName;
      config.now = now;
      config.expires = now + m_config.ceremonyLifetime;
      config.timeout = m_config.passkeyTimeout;
      started = recoveryBegin(m_requests, deadline_(), m_context, m_rng,
        ZuMv(start.capability), ZuMv(binding), ZuMv(config), ZuMv(done));
    } break;
  }
  if (!started) replyDone->finish(serverError());
}

void Server::passkeyFinish(
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
  finishGrant_(ZuMv(id), ZuMv(binding), ZuMv(json), ZuMv(complete));
}

void Server::finishGrant_(
    Bytes id, Bytes binding, String json, ServerFn complete)
{
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  auto grants = m_context->grants;
  grants->run(0, [this, grants, id = ZuMv(id), binding = ZuMv(binding),
      json = ZuMv(json), done]() mutable {
    grants->find<0>(0, ZuFwdTuple(id), [this, id = ZuMv(id),
      binding = ZuMv(binding), json = ZuMv(json),
      done](ZdbRowRef<Grant> row) mutable {
      if (!row || row->data().kind != GrantKind::Ceremony ||
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
        int error = parseAssertion({json.data(), json.length()},
          WebAuthnInputLimits{}, input);
        if (error || !authorizeFinish(m_requests, deadline_(), m_context,
            m_rng, ZuMv(id), ZuMv(binding), ZuMv(input),
            AuthorizeFinishConfig{
              .origin = m_config.issuer, .rpID = m_config.rpID,
              .now = now, .codeExpires = now + m_config.codeLifetime,
              .consent = true
            }, m_policy, [this, id = ZuMv(sessionGrant), done](
                int error, String location) mutable {
              if (error == AuthorizeIssue::OK) {
                sessionReply_(ZuMv(id), ZuMv(location),
                  [done](ServerReply reply) mutable {
                    done->finish(ZuMv(reply));
                  });
              }
              else if (error == AuthorizeIssue::Consent) {
                done->finish(ServerReply{.body = consentPage(id),
                  .type = ReplyType::Page});
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
      int error = parseRegistration({json.data(), json.length()},
        WebAuthnInputLimits{}, input);
      if (error) {
        auto reply = jsonReply(OAuthError::AccessDenied);
        reply.setCookie = setCookie_({}, true);
        done->finish(ZuMv(reply));
        return;
      }
      auto finish = [this, done, purpose](int error) mutable {
        if (error) {
          auto reply = jsonReply(OAuthError::AccessDenied);
          reply.setCookie = setCookie_({}, true);
          done->finish(ZuMv(reply));
          return;
        }
        if (purpose != GrantPurpose::Bootstrap) {
          done->finish(ServerReply{.body = serverJSON(OKWire{true}),
            .setCookie = setCookie_({}, true), .type = ReplyType::OK});
          return;
        }
        auto issuers = m_context->issuers;
        issuers->run(0, [this, issuers, done]() mutable {
          String issuer = m_config.issuer;
          issuers->findUpd<0>(0, ZuFwdTuple(ZuMv(issuer)), [this, done](
              ZdbRow<Issuer> *row) mutable {
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
            done->finish(ServerReply{.body = serverJSON(OKWire{true}),
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
          started = enrollmentFinish(m_requests, deadline_(), m_db,
            m_context, ZuMv(id), ZuMv(binding), ZuMv(input),
            ZuMv(config), ZuMv(finish));
          break;
        case GrantPurpose::AddCredential:
          started = credentialFinish(m_requests, deadline_(), m_db,
            m_context, ZuMv(id), ZuMv(binding), ZuMv(input),
            ZuMv(config), ZuMv(finish));
          break;
        case GrantPurpose::Recovery:
          started = recoveryFinish(m_requests, deadline_(), m_db,
            m_context, ZuMv(id), ZuMv(binding), ZuMv(input),
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

void Server::sessionReply_(Bytes grantID, String location, ServerFn complete)
{
  int64_t now = now_();
  if (now <= 0 || !sessionIssueGrant(m_requests, deadline_(), m_context,
      m_rng, ZuMv(grantID), now, m_config.sessionIdle,
      m_config.sessionAbsolute, [this, location = ZuMv(location),
        complete = ZuMv(complete)](int error, Session, String token) mutable {
        complete(ServerReply{.location = ZuMv(location),
          .setCookie = error == SessionError::OK ?
            setCookie_(token, false, m_config.sessionAbsolute) :
            setCookie_({}, true), .type = ReplyType::Redirect});
      })) {
    complete(ServerReply{.location = ZuMv(location),
      .setCookie = setCookie_({}, true), .type = ReplyType::Redirect});
  }
}

void Server::oidcCallback(String query, String cookie, ServerFn complete)
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
  if (!m_oidc.finish(ZuMv(query), [this, binding = ZuMv(binding), done](
      bool ok, Bytes grantID, User user, IDVec roleIDs,
      Evidence evidence, int64_t authTime) mutable {
    int64_t now = now_();
    Bytes sessionGrant{grantID};
    if (!ok || now <= 0 || !authorizeOIDCFinish(
        m_requests, deadline_(), m_context, m_rng, ZuMv(grantID),
        ZuMv(binding), ZuMv(user), ZuMv(roleIDs), ZuMv(evidence),
        authTime,
        AuthorizeFinishConfig{.now = now,
          .codeExpires = now + m_config.codeLifetime, .consent = true}, m_policy,
        [this, id = ZuMv(sessionGrant), done](
            int error, String location) mutable {
          if (error == AuthorizeIssue::OK)
            sessionReply_(ZuMv(id), ZuMv(location),
              [done](ServerReply reply) mutable {
                done->finish(ZuMv(reply));
              });
          else if (error == AuthorizeIssue::Consent)
            done->finish(ServerReply{.body = consentPage(id),
              .type = ReplyType::Page});
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
