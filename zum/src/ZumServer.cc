//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumServer.hh>

#include <zlib/ZuBase64URL.hh>

#include <zlib/ZfJSON.hh>


namespace Zum {

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
  formEach({query.data(), query.length()},
    [&encoded](ZuCSpan name, ZuCSpan value) {
      if (name == "id") encoded = value;
    });
  return encoded && decodeID(encoded, id);
}

static bool passkeyStart(String &json, PasskeyStart &start)
{
  if (!json.mutable_()) json.length(json.length());
  auto parsed = ZfJSON::scan({json.data(), json.length()});
  if (parsed.p<0>() < 0 || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (!roots || !roots[0]->has<ZfJSON::AnyNode::Object>()) return false;
  String purpose;
  PasskeyStart next;
  unsigned seen = 0;
  for (auto &field: roots[0]->data<ZfJSON::AnyNode::Object>()) {
    auto value = field.p<1>().ptr();
    unsigned bit;
    if (field.p<0>() == "purpose") {
      bit = 1U;
      if (!value->has<ZfJSON::AnyNode::String>()) return false;
      purpose = value->data<ZfJSON::AnyNode::String>();
    } else if (field.p<0>() == "capability") {
      bit = 2U;
      if (!value->has<ZfJSON::AnyNode::String>()) return false;
      next.capability = value->data<ZfJSON::AnyNode::String>();
    } else {
      continue;
    }
    seen |= bit;
  }
  if (!(seen & 1U)) return false;
  if (purpose == "enrollment")
    next.type = PasskeyStartType::Enrollment;
  else if (purpose == "bootstrap")
    next.type = PasskeyStartType::Bootstrap;
  else if (purpose == "add")
    next.type = PasskeyStartType::AddCredential;
  else if (purpose == "recovery")
    next.type = PasskeyStartType::Recovery;
  else
    return false;
  bool needsCapability = next.type == PasskeyStartType::Bootstrap ||
    next.type == PasskeyStartType::Recovery;
  if (needsCapability && !next.capability) return false;
  start = ZuMv(next);
  return true;
}

static String ceremonyJSON(ZuBSpan id, ZuCSpan options)
{
  String body{"{\"ceremony\":"};
  auto encoded = encodeID(id);
  ZfJSON::quote(body, encoded);
  body << ",\"options\":" << options << '}';
  return body;
}

bool Server::init(
    DB *db, DBContext *context, Requests *requests, ServerConfig config,
    ClockFn clock, PageFn page, PolicyFn policy, AdmitFn admit, SignFn sign,
    OIDCHTTPFn oidcHTTP)
{
  if (m_db || !db || !context || !requests || !clock ||
      !policy || !sign || !config.issuer || !config.keyID ||
      !config.cookieName || !config.cookiePath || !config.requestTimeout ||
      config.ceremonyLifetime <= 0 || config.codeLifetime <= 0 ||
      config.accessLifetime <= 0 ||
      config.refreshLifetime <= 0 || !config.refreshGenerations ||
      !config.spentTokens || !config.limits.cookie || !config.limits.jwks ||
      (config.authMethod != AuthMethod::Passkey &&
       config.authMethod != AuthMethod::OIDC) ||
      (config.authMethod == AuthMethod::Passkey &&
       (!config.rpID || !config.rpName || !config.passkeyTimeout ||
	!config.limits.credentialID || !page || !admit)) ||
      (config.authMethod == AuthMethod::OIDC &&
       (!config.oidcTimeout || !oidcHTTP)) ||
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
  if (m_config.authMethod == AuthMethod::OIDC && !m_oidc.init(
      requests->scheduler(), requests->sid(), context, m_config.oidc,
      m_config.limits.oidc, m_config.limits.oidcPending,
      m_config.oidcTimeout, [this]() { return now_(); }, ZuMv(oidcHTTP))) {
    final();
    return false;
  }
  return true;
}

void Server::final()
{
  m_oidc.final();
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

String Server::setCookie_(ZuCSpan value, bool clear) const
{
  String header;
  header << m_config.cookieName << '=' << value << "; Path=" <<
    m_config.cookiePath << "; Secure; HttpOnly; SameSite=Lax; Max-Age=" <<
    ZuBoxed(clear ? 0 : m_config.ceremonyLifetime);
  return header;
}

bool Server::binding_(String &cookie, Bytes &digest) const
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
  return value && opaqueParse(value, id, digest);
}

void Server::authorize(String query, ServerFn complete)
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
  if (!authorizeRequest(m_requests, deadline_(), m_context, m_rng,
      ZuMv(query), ZuMv(binding), AuthorizeConfig{
        .issuer = m_config.issuer,
        .rpID = m_config.authMethod == AuthMethod::Passkey ?
          m_config.rpID : String{},
        .now = now,
        .expires = now + m_config.ceremonyLifetime,
        .timeout = m_config.authMethod == AuthMethod::Passkey ?
          m_config.passkeyTimeout : 0,
        .passkey = m_config.authMethod == AuthMethod::Passkey
      }, [this, setCookie = ZuMv(setCookie), done](
          int error, AuthorizeResult result) mutable {
        if (error == AuthorizeIssue::OK) {
          if (m_config.authMethod == AuthMethod::Passkey) {
            String body = m_page(
              ZuMv(result.ceremonyID), ZuMv(result.options));
            done->finish(ServerReply{.body = ZuMv(body),
              .setCookie = ZuMv(setCookie), .type = ReplyType::Page});
            return;
          }
          if (!m_oidc.begin(ZuMv(result.ceremonyID),
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
      })) done->finish(serverError());
}

void Server::token(String form, String authorization, ServerFn complete)
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
        .keyID = m_config.keyID,
        .jwtLimits = m_config.limits.jwt,
        .now = now,
        .accessExpires = now + m_config.accessLifetime,
        .refreshExpires = now + m_config.refreshLifetime,
        .generationLimit = m_config.refreshGenerations,
        .spentLimit = m_config.spentTokens
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
  if (!m_db || !complete) return;
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  if (form.length() > m_config.limits.form) {
    done->finish(jsonReply(OAuthError::InvalidRequest));
    return;
  }
  int64_t now = now_();
  if (now <= 0 || !revokeRequest(m_requests, deadline_(), m_context,
      ZuMv(form), ZuMv(authorization),
      RevokeConfig{.issuer = m_config.issuer, .now = now},
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

void Server::passkeyBegin(String json, ServerFn complete)
{
  if (!m_db || !complete) return;
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  if (m_config.authMethod != AuthMethod::Passkey) {
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
      started = enrollmentBegin(m_requests, deadline_(), m_context, m_rng,
        ZuMv(binding), ZuMv(config), ZuMv(done));
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
  if (m_config.authMethod != AuthMethod::Passkey) {
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
        AssertionInput input;
        int error = parseAssertion({json.data(), json.length()},
          WebAuthnInputLimits{}, input);
        if (error || !authorizeFinish(m_requests, deadline_(), m_context,
            m_rng, ZuMv(id), ZuMv(binding), ZuMv(input),
            AuthorizeFinishConfig{
              .origin = m_config.issuer, .rpID = m_config.rpID,
              .now = now, .codeExpires = now + m_config.codeLifetime
            }, m_policy, [this, done](
                int error, String location) mutable {
              if (error == AuthorizeIssue::OK)
                done->finish(ServerReply{.location = ZuMv(location),
                  .setCookie = setCookie_({}, true),
                  .type = ReplyType::Redirect});
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
      auto finish = [this, done](int error) mutable {
        if (error) {
          auto reply = jsonReply(OAuthError::AccessDenied);
          reply.setCookie = setCookie_({}, true);
          done->finish(ZuMv(reply));
        } else {
          done->finish(ServerReply{.body = "{\"ok\":true}",
            .setCookie = setCookie_({}, true), .type = ReplyType::OK});
        }
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

void Server::oidcCallback(String query, String cookie, ServerFn complete)
{
  if (!m_db || !complete) return;
  ZmRef<ReplyComplete_> done = new ReplyComplete_{ZuMv(complete)};
  Bytes binding;
  if (m_config.authMethod != AuthMethod::OIDC ||
      !binding_(cookie, binding)) {
    auto reply = jsonReply(OAuthError::InvalidRequest);
    reply.setCookie = setCookie_({}, true);
    done->finish(ZuMv(reply));
    return;
  }
  if (!m_oidc.finish(ZuMv(query), [this, binding = ZuMv(binding), done](
      bool ok, Bytes grantID, User user, IDVec roleIDs,
      int64_t authTime) mutable {
    int64_t now = now_();
    if (!ok || now <= 0 || !authorizeOIDCFinish(
        m_requests, deadline_(), m_context, m_rng, ZuMv(grantID),
        ZuMv(binding), ZuMv(user), ZuMv(roleIDs), authTime,
        AuthorizeFinishConfig{.now = now,
          .codeExpires = now + m_config.codeLifetime}, m_policy,
        [this, done](int error, String location) mutable {
          if (error == AuthorizeIssue::OK)
            done->finish(ServerReply{.location = ZuMv(location),
              .setCookie = setCookie_({}, true),
              .type = ReplyType::Redirect});
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
