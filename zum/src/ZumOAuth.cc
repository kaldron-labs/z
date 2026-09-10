//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumOAuth.hh>

#include <zlib/ZuBase64.hh>
#include <zlib/ZuBase64URL.hh>

#include <zlib/ZfJSON.hh>
#include <zlib/ZfURI.hh>

#include <zlib/ZhttpURL.hh>

#include <zlib/ZiIP.hh>

#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsSec.hh>

namespace Zum {

template <typename Params, typename Field>
static void set(Params &params, Field field, ZuCSpan value, ZuCSpan &dst)
{
  auto bit = decltype(params.seen)(1U << field);
  params.seen |= bit;
  dst = value;
}

void parseAuthorize(ZuSpan<char> data, AuthorizeParams &params)
{
  params = {};
  formEach(data, [&params](ZuCSpan name, ZuCSpan value) {
      using F = AuthorizeParams;
      if (name == "response_type")
	set(params, F::ResponseType, value, params.responseType);
      else if (name == "client_id")
	set(params, F::ClientID, value, params.clientID);
      else if (name == "redirect_uri")
	set(params, F::RedirectURI, value, params.redirectURI);
      else if (name == "scope")
	set(params, F::Scope, value, params.scope);
      else if (name == "state")
	set(params, F::State, value, params.state);
      else if (name == "code_challenge")
	set(params, F::CodeChallenge, value, params.codeChallenge);
      else if (name == "code_challenge_method")
	set(params, F::CodeChallengeMethod,
	  value, params.codeChallengeMethod);
      else if (name == "nonce")
	set(params, F::Nonce, value, params.nonce);
      else if (name == "login_hint")
	set(params, F::LoginHint, value, params.loginHint);
      else if (name == "resource")
	set(params, F::Resource, value, params.resource);
      else if (name == "prompt")
	set(params, F::Prompt, value, params.prompt);
      else if (name == "max_age")
	set(params, F::MaxAge, value, params.maxAge);
    });
}

void parseToken(ZuSpan<char> data, TokenParams &params)
{
  params = {};
  formEach(data, [&params](ZuCSpan name, ZuCSpan value) {
      using F = TokenParams;
      if (name == "grant_type")
	set(params, F::GrantType, value, params.grantType);
      else if (name == "code")
	set(params, F::Code, value, params.code);
      else if (name == "client_id")
	set(params, F::ClientID, value, params.clientID);
      else if (name == "redirect_uri")
	set(params, F::RedirectURI, value, params.redirectURI);
      else if (name == "code_verifier")
	set(params, F::CodeVerifier, value, params.codeVerifier);
      else if (name == "refresh_token")
	set(params, F::RefreshToken, value, params.refreshToken);
      else if (name == "scope")
	set(params, F::Scope, value, params.scope);
    });
}

void parseRevoke(ZuSpan<char> data, RevokeParams &params)
{
  params = {};
  formEach(data, [&params](ZuCSpan name, ZuCSpan value) {
      using F = RevokeParams;
      if (name == "token")
	set(params, F::Token, value, params.token);
      else if (name == "token_type_hint")
	set(params, F::TokenTypeHint, value, params.tokenTypeHint);
      else if (name == "client_id")
	set(params, F::ClientID, value, params.clientID);
    });
}

int validateAuthorize(const AuthorizeParams &params)
{
  using F = AuthorizeParams;
  constexpr unsigned required =
    (1U<<F::ResponseType) | (1U<<F::ClientID) | (1U<<F::RedirectURI) |
    (1U<<F::Scope) | (1U<<F::CodeChallenge) |
    (1U<<F::CodeChallengeMethod);
  if ((params.seen & required) != required) return ProfileError::Missing;
  if (!params.responseType || !params.clientID || !params.redirectURI ||
      !params.scope || !params.codeChallenge || !params.codeChallengeMethod)
    return ProfileError::Empty;
  if (params.responseType != "code" ||
      params.codeChallengeMethod != "S256") return ProfileError::Unsupported;
  if (params.has(F::Nonce) && !params.nonce) return ProfileError::Empty;
  if (params.has(F::LoginHint) && !params.loginHint)
    return ProfileError::Empty;
  if (params.has(F::Resource) && !params.resource)
    return ProfileError::Empty;
  if (params.has(F::Prompt) && !params.prompt)
    return ProfileError::Empty;
  if (params.has(F::MaxAge)) {
    if (!params.maxAge) return ProfileError::Empty;
    uint64_t maxAge = 0;
    for (auto c: params.maxAge) {
      if (c < '0' || c > '9') return ProfileError::Unsupported;
      unsigned digit = unsigned(c - '0');
      if (maxAge > (UINT64_MAX - digit) / 10)
	return ProfileError::Unsupported;
      maxAge = maxAge * 10 + digit;
    }
  }
  if (params.has(F::Prompt)) {
    bool none = false, login = false;
    unsigned offset = 0;
    while (offset < params.prompt.length()) {
      unsigned end = offset;
      while (end < params.prompt.length() && params.prompt[end] != ' ')
	++end;
      ZuCSpan value{params.prompt.data() + offset, end - offset};
      if (!value || (value != "none" && value != "login"))
	return ProfileError::Unsupported;
      if (value == "none") {
	if (none) return ProfileError::Unsupported;
	none = true;
      } else {
	if (login) return ProfileError::Unsupported;
	login = true;
      }
      offset = end + (end < params.prompt.length());
    }
    if (none && login) return ProfileError::Unsupported;
  }
  return ProfileError::OK;
}

int validateToken(const TokenParams &params, int &grant)
{
  using F = TokenParams;
  grant = TokenGrant::Invalid;
  if (!params.has(F::GrantType)) return ProfileError::Missing;
  if (!params.grantType) return ProfileError::Empty;

  unsigned required;
  if (params.grantType == "authorization_code") {
    grant = TokenGrant::AuthorizationCode;
    required = (1U<<F::GrantType) | (1U<<F::Code) |
      (1U<<F::RedirectURI) | (1U<<F::CodeVerifier);
  } else if (params.grantType == "refresh_token") {
    grant = TokenGrant::RefreshToken;
    required = (1U<<F::GrantType) | (1U<<F::RefreshToken);
  } else if (params.grantType == "client_credentials") {
    grant = TokenGrant::ClientCredentials;
    required = 1U<<F::GrantType;
  } else {
    return ProfileError::Unsupported;
  }
  if ((params.seen & required) != required) return ProfileError::Missing;
  if ((params.has(F::Code) && !params.code) ||
      (params.has(F::ClientID) && !params.clientID) ||
      (params.has(F::RedirectURI) && !params.redirectURI) ||
      (params.has(F::CodeVerifier) && !params.codeVerifier) ||
      (params.has(F::RefreshToken) && !params.refreshToken))
    return ProfileError::Empty;
  return ProfileError::OK;
}

int validateRevoke(const RevokeParams &params)
{
  if (!params.has(RevokeParams::Token)) return ProfileError::Missing;
  if (!params.token ||
      (params.has(RevokeParams::ClientID) && !params.clientID))
    return ProfileError::Empty;
  return ProfileError::OK;
}

bool parseBasic(ZuSpan<char> value, BasicAuth &auth)
{
  auth = {};
  unsigned length = value.length();
  if (length < 7 ||
      (value[0] | 0x20) != 'b' || (value[1] | 0x20) != 'a' ||
      (value[2] | 0x20) != 's' || (value[3] | 0x20) != 'i' ||
      (value[4] | 0x20) != 'c' || value[5] != ' ') return false;
  unsigned i = 6;
  while (i < length && value[i] == ' ') ++i;
  if (i == length) return false;
  auto encoded = value.offset(i);
  unsigned n = encoded.length();
  while (n && encoded[n - 1] == '=') --n;
  unsigned expected = ZuBase64::declen(n);
  ZuSpan<uint8_t> decoded = encoded.trunc(expected);
  if (ZuBase64::decode(decoded, ZuBSpan{encoded.data(), n}) != expected)
    return false;
  auto plain = ZuCSpan{encoded.data(), expected};
  for (unsigned j = 0; j < expected; ++j)
    if (plain[j] == ':') {
      if (!j) return false;
      auth.clientID = {plain.data(), j};
      auth.secret = {plain.data() + j + 1, expected - j - 1};
      return true;
    }
  return false;
}

static void opaqueFinish(
    ZuBSpan id, ZuBSpan secret, OpaqueToken &opaque)
{
  OpaqueToken next;
  next.id = Bytes{id};
  next.token.length(
    ZuBase64URL::enclen(OpaqueIDSize) + 1 +
      ZuBase64URL::enclen(OpaqueSecretSize));
  unsigned n = ZuBase64URL::encode(next.token.span(), id);
  next.token[n++] = '.';
  n += ZuBase64URL::encode(next.token.span().offset(n), secret);
  next.token.length(n);
  next.digest = opaqueDigest(next.token);
  opaque = ZuMv(next);
}

bool opaqueIssue(Ztls::Random &rng, OpaqueToken &opaque)
{
  enum { RawSize = OpaqueIDSize + OpaqueSecretSize };
  Bytes raw;
  raw.length(RawSize, false);
  if (!rng.random(raw)) {
    ZuClear(raw.data(), raw.length());
    return false;
  }
  opaqueFinish({raw.data(), OpaqueIDSize},
    {raw.data() + OpaqueIDSize, OpaqueSecretSize}, opaque);
  ZuClear(raw.data(), raw.length());
  return true;
}

bool opaqueIssue(Ztls::Random &rng, ZuBSpan id, OpaqueToken &opaque)
{
  if (id.length() != OpaqueIDSize) return false;
  uint8_t secret[OpaqueSecretSize];
  if (!rng.random(secret)) return false;
  opaqueFinish(id, secret, opaque);
  ZuClear(secret, sizeof(secret));
  return true;
}

bool opaqueParse(ZuCSpan token, Bytes &id, Bytes &digest)
{
  enum { EncodedSize = ZuBase64URL::enclen(OpaqueIDSize) };
  if (token.length() <= EncodedSize || token[EncodedSize] != '.') return false;
  Bytes nextID;
  nextID.length(OpaqueIDSize, false);
  if (ZuBase64URL::decode(nextID,
      ZuBSpan{token.data(), EncodedSize}) != OpaqueIDSize) return false;
  Bytes nextDigest = opaqueDigest(token);
  id = ZuMv(nextID);
  digest = ZuMv(nextDigest);
  return true;
}

Bytes opaqueDigest(ZuCSpan token)
{
  Bytes digest;
  digest.length(Ztls::MD<>::Size, false);
  Ztls::MD<> md;
  md.update(ZuBSpan{token});
  md.finish(digest);
  return digest;
}

bool authorizeClient(const Client &client, const AuthorizeParams &params)
{
  if (client.state != State::Active || client.owner ||
      client.id != params.clientID ||
      !(client.grants & ClientGrant::AuthorizationCode) ||
      (client.type != ClientType::Browser &&
       client.type != ClientType::Native &&
       client.type != ClientType::Confidential)) return false;
  for (auto &redirect: client.redirects)
    if (redirectMatches(client.type, redirect, params.redirectURI)) return true;
  return false;
}

bool authorizationBegin(
    Ztls::Random &rng, Grant &grant, ZuCSpan issuer,
    const AuthorizeParams &params, const ScopeSelection &selection,
    bool passkey, ZuBSpan bindingDigest, uint64_t authVersion,
    int64_t created, int64_t expires)
{
  enum { ChallengeSize = 32 }; // WebAuthn/PKCE SHA-256 challenge entropy
  uint8_t random[OpaqueIDSize + ChallengeSize];
  unsigned randomSize = OpaqueIDSize + (passkey ? ChallengeSize : 0);
  if (!rng.random({random, randomSize})) return false;

  Grant next;
  next.id = Bytes{ZuBSpan{random, OpaqueIDSize}};
  next.issuer = issuer;
  next.appID = selection.appID;
  next.audienceID = selection.audienceID;
  next.clientID = params.clientID;
  next.audience = selection.audience;
  next.redirectURI = params.redirectURI;
  next.scope = selection.scope;
  next.scopeIDs = selection.scopeIDs;
  if (params.has(AuthorizeParams::Nonce)) next.nonce = params.nonce;
  if (passkey)
    next.challenge = Bytes{ZuBSpan{random + OpaqueIDSize, ChallengeSize}};
  next.bindingDigest = bindingDigest;
  next.pkceChallenge = Bytes{ZuBSpan{params.codeChallenge}};
  if (params.has(AuthorizeParams::State)) {
    next.oauthState = params.state;
    next.oauthStatePresent = true;
  }
  if (params.has(AuthorizeParams::Prompt)) {
    next.prompt = params.prompt;
    next.promptPresent = true;
  }
  if (params.has(AuthorizeParams::MaxAge)) {
    for (auto c: params.maxAge)
      next.maxAge = next.maxAge * 10 + unsigned(c - '0');
    next.maxAgePresent = true;
  }
  next.authVersion = authVersion;
  next.created = created;
  next.expires = expires;
  next.kind = GrantKind::Ceremony;
  next.purpose = GrantPurpose::Authorization;
  next.state = State::Active;
  ZuClear(random, randomSize);
  grant = ZuMv(next);
  return true;
}

bool authorizationFinish(
    Ztls::Random &rng, Grant &grant, ZuBSpan bindingDigest,
    UserID userID, Bytes credentialID, IDVec roleIDs, ZtBitmap actions,
    uint64_t authVersion, uint64_t userVersion,
    int64_t authTime, int64_t codeExpires, String &code)
{
  if (grant.kind != GrantKind::Ceremony ||
      grant.purpose != GrantPurpose::Authorization ||
      (grant.state != State::Active && grant.state != State::Pending) ||
      grant.owner ||
      grant.expires <= authTime ||
      grant.authVersion != authVersion ||
      !Ztls::ctEqual(grant.bindingDigest, bindingDigest) ||
      !userID || !userVersion || authTime <= 0 ||
      codeExpires <= authTime) return false;
  OpaqueToken next;
  if (!opaqueIssue(rng, grant.id, next)) return false;
  grant.userID = userID;
  grant.credentialID = ZuMv(credentialID);
  grant.roleIDs = ZuMv(roleIDs);
  grant.actions = ZuMv(actions);
  grant.digest = ZuMv(next.digest);
  grant.challenge.null();
  grant.bindingDigest.null();
  grant.authVersion = authVersion;
  grant.userVersion = userVersion;
  grant.authTime = authTime;
  grant.expires = codeExpires;
  grant.kind = GrantKind::Code;
  grant.state = State::Active;
  code = ZuMv(next.token);
  return true;
}

bool codeMatches(
    const Grant &grant, ZuBSpan codeDigest, ZuCSpan clientID,
    ZuCSpan redirectURI, ZuCSpan verifier, int64_t now)
{
  if (grant.kind != GrantKind::Code || grant.state != State::Active ||
      grant.owner || grant.expires <= now || grant.clientID != clientID ||
      grant.redirectURI != redirectURI ||
      !Ztls::ctEqual(grant.digest, codeDigest)) return false;
  return pkceVerify(grant.pkceChallenge, verifier);
}

String tokenResponseJSON(const TokenResponse &response)
{
  String json{"{\"access_token\":"};
  ZfJSON::quote(json, response.accessToken);
  json << ",\"token_type\":\"Bearer\",\"expires_in\":" <<
    ZuBoxed(response.expiresIn) << ",\"scope\":";
  ZfJSON::quote(json, response.scope);
  if (response.idToken) {
    json << ",\"id_token\":";
    ZfJSON::quote(json, response.idToken);
  }
  if (response.refreshToken) {
    json << ",\"refresh_token\":";
    ZfJSON::quote(json, response.refreshToken);
  }
  json << '}';
  return json;
}

static ZuCSpan errorCode(int error)
{
  switch (error) {
    case OAuthError::InvalidRequest: return "invalid_request";
    case OAuthError::InvalidClient: return "invalid_client";
    case OAuthError::InvalidGrant: return "invalid_grant";
    case OAuthError::UnauthorizedClient: return "unauthorized_client";
    case OAuthError::AccessDenied: return "access_denied";
    case OAuthError::UnsupportedResponseType:
      return "unsupported_response_type";
    case OAuthError::UnsupportedGrantType:
      return "unsupported_grant_type";
    case OAuthError::InvalidScope: return "invalid_scope";
    case OAuthError::LoginRequired: return "login_required";
    case OAuthError::ConsentRequired: return "consent_required";
    case OAuthError::TemporarilyUnavailable:
      return "temporarily_unavailable";
    default: return "server_error";
  }
}

String oauthErrorJSON(int error)
{
  String json{"{\"error\":"};
  ZfJSON::quote(json, errorCode(error));
  json << '}';
  return json;
}

static String redirect(
    ZuCSpan redirectURI, ZuCSpan name, ZuCSpan value,
    ZuCSpan state, bool statePresent)
{
  String uri{redirectURI};
  uri << (redirectURI.find<"?">() >= 0 ? '&' : '?') << name << '=';
  ZfURI::URIQuote<false>::quote(uri, value);
  if (statePresent) {
    uri << "&state=";
    ZfURI::URIQuote<false>::quote(uri, state);
  }
  return uri;
}

String codeRedirect(
    ZuCSpan redirectURI, ZuCSpan code, ZuCSpan state, bool statePresent)
{
  return redirect(redirectURI, "code", code, state, statePresent);
}

String codeRedirect(const Grant &grant, ZuCSpan code)
{
  return codeRedirect(grant.redirectURI, code,
    grant.oauthState, grant.oauthStatePresent);
}

String errorRedirect(
    ZuCSpan redirectURI, int error, ZuCSpan state, bool statePresent)
{
  return redirect(
    redirectURI, "error", errorCode(error), state, statePresent);
}

int authenticateClient(
    const Client &client, int grant, const TokenParams &params,
    const BasicAuth *basic, int64_t now)
{
  if (client.state != State::Active || client.owner)
    return ClientAuth::InvalidClient;
  unsigned flag;
  switch (grant) {
    case TokenGrant::AuthorizationCode:
      flag = ClientGrant::AuthorizationCode;
      break;
    case TokenGrant::RefreshToken:
      flag = ClientGrant::RefreshToken;
      break;
    case TokenGrant::ClientCredentials:
      flag = ClientGrant::ClientCredentials;
      break;
    default:
      return ClientAuth::UnauthorizedGrant;
  }
  if (!(client.grants & flag) ||
      (grant == TokenGrant::ClientCredentials &&
       client.type != ClientType::Confidential))
    return ClientAuth::UnauthorizedGrant;

  if (client.type == ClientType::Confidential) {
    bool secret = basic &&
      (Ztls::secretVerify(client.secretDigest, ZuBSpan{basic->secret}) ||
       (now > 0 && client.previousSecretExpires > now &&
	client.previousSecretDigest && Ztls::secretVerify(
	  client.previousSecretDigest, ZuBSpan{basic->secret})));
    if (!basic || basic->clientID != client.id ||
        (params.clientID && params.clientID != client.id) ||
        !secret)
      return ClientAuth::InvalidClient;
  } else if (basic || !params.clientID || params.clientID != client.id) {
    return ClientAuth::InvalidClient;
  }
  return ClientAuth::OK;
}

bool redirectMatches(
    ClientType::T type, ZuBSpan registered, ZuBSpan requested)
{
  if (type != ClientType::Native) return registered == requested;

  Zhttp::URL registeredURL{registered};
  Zhttp::URL requestedURL{requested};
  if (!registeredURL.error().ok() || !requestedURL.error().ok()) return false;
  auto l = registeredURL.url();
  auto r = requestedURL.url();
  if (l.scheme != Zhttp::Scheme::http || r.scheme != l.scheme ||
      !r.explicitPort || l.hasFragment || r.hasFragment ||
      l.host != r.host || l.path != r.path ||
      l.hasQuery != r.hasQuery || l.query != r.query) return false;
  ZiIP lip, rip;
  return ZiIP::parse(lip, ZuCSpan{l.host}) &&
    ZiIP::parse(rip, ZuCSpan{r.host}) && lip.loopback() && lip == rip;
}

bool pkceVerify(ZuCSpan challenge, ZuCSpan verifier)
{
  uint8_t digest[Ztls::MD<>::Size];
  { Ztls::MD<> md; md.update(ZuBSpan{verifier}); md.finish(digest); }
  char encoded[ZuBase64URL::enclen(sizeof(digest))];
  unsigned n = ZuBase64URL::encode(
    ZuSpan<uint8_t>{encoded, sizeof(encoded)}, digest);
  return Ztls::ctEqual(ZuBSpan{challenge}, ZuBSpan{encoded, n});
}

} // namespace Zum
