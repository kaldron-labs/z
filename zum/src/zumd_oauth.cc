//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_oauth.hh>

#include <zlib/ZuBase64.hh>
#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZuPercent.hh>

#include <zlib/ZfJSON.hh>
#include <zlib/ZfURI.hh>

#include <zlib/ZhttpURL.hh>

#include <zlib/ZiIP.hh>

#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsSec.hh>

namespace Zum {

struct TokenResponseWire {
  String accessToken;
  String tokenType{"Bearer"};
  uint64_t expiresIn = 0;
  String scope;
  String idToken;
  String refreshToken;
};
ZfStruct(, (TokenResponseWire, JSON),
  (((accessToken),	(JSON::ID<"access_token">, Required)), (String)),
  (((tokenType),	(JSON::ID<"token_type">, Required)),	(String)),
  (((expiresIn),	(JSON::ID<"expires_in">, Required)),	(UInt64)),
  (((scope),		(Required)),	(String)),
  (((idToken),		(JSON::ID<"id_token">, JSON::Opt)),	(String)),
  (((refreshToken),	(JSON::ID<"refresh_token">, JSON::Opt)), (String)));

struct OAuthErrorWire { String error; };
ZfStruct(, (OAuthErrorWire, JSON),
  (((error),		(Required)),	(String)));

struct AuthorizeFields {
  using Keys = ZuStringTL<"response_type", "client_id", "redirect_uri",
    "scope", "state", "code_challenge", "code_challenge_method", "nonce",
    "login_hint", "resource", "prompt", "max_age">;
};
struct TokenFields {
  using Keys = ZuStringTL<"grant_type", "code", "client_id", "redirect_uri",
    "code_verifier", "refresh_token", "scope">;
};
struct RevokeFields {
  using Keys = ZuStringTL<"token", "token_type_hint", "client_id">;
};
struct GrantTypes {
  using Keys = ZuStringTL<"authorization_code", "refresh_token",
    "client_credentials">;
};

int formDecode_(ZuSpan<char> data)
{
  if (!data) return 0;
  using Form = ZuPercent::Codec<ZfURI::PercentQuote<true>>;
  auto result = Form::decode(data);
  return result ? int(result.out) : -1;
}

template <typename Params, typename Field>
static bool set(Params &params, Field field, ZuCSpan value, ZuCSpan &dst)
{
  auto bit = decltype(params.seen)(1U << field);
  if (params.seen & bit) return false;
  params.seen |= bit;
  dst = value;
  return true;
}

bool parseAuthorize(ZuSpan<char> data, AuthorizeParams &params)
{
  params = {};
  bool valid = true;
  constexpr auto matcher = ZuMatcher<AuthorizeFields>();
  return formEach(data, [&params, &valid, &matcher](
      ZuCSpan name, ZuCSpan value) {
    switch (matcher.exact(name)) {
      case AuthorizeParams::ResponseType:
	valid &= set(params, AuthorizeParams::ResponseType, value,
	  params.responseType); break;
      case AuthorizeParams::ClientID:
	valid &= set(params, AuthorizeParams::ClientID, value, params.clientID); break;
      case AuthorizeParams::RedirectURI:
	valid &= set(params, AuthorizeParams::RedirectURI, value,
	  params.redirectURI); break;
      case AuthorizeParams::Scope:
	valid &= set(params, AuthorizeParams::Scope, value, params.scope); break;
      case AuthorizeParams::State:
	valid &= set(params, AuthorizeParams::State, value, params.state); break;
      case AuthorizeParams::CodeChallenge:
	valid &= set(params, AuthorizeParams::CodeChallenge, value,
	  params.codeChallenge); break;
      case AuthorizeParams::CodeChallengeMethod:
	valid &= set(params, AuthorizeParams::CodeChallengeMethod, value,
	  params.codeChallengeMethod); break;
      case AuthorizeParams::Nonce:
	valid &= set(params, AuthorizeParams::Nonce, value, params.nonce); break;
      case AuthorizeParams::LoginHint:
	valid &= set(params, AuthorizeParams::LoginHint, value,
	  params.loginHint); break;
      case AuthorizeParams::Resource:
	valid &= set(params, AuthorizeParams::Resource, value, params.resource); break;
      case AuthorizeParams::Prompt:
	valid &= set(params, AuthorizeParams::Prompt, value, params.prompt); break;
      case AuthorizeParams::MaxAge:
	valid &= set(params, AuthorizeParams::MaxAge, value, params.maxAge); break;
    }
  }) && valid;
}

bool parseToken(ZuSpan<char> data, TokenParams &params)
{
  params = {};
  bool valid = true;
  constexpr auto matcher = ZuMatcher<TokenFields>();
  return formEach(data, [&params, &valid, &matcher](
      ZuCSpan name, ZuCSpan value) {
    switch (matcher.exact(name)) {
      case TokenParams::GrantType:
	valid &= set(params, TokenParams::GrantType, value, params.grantType); break;
      case TokenParams::Code:
	valid &= set(params, TokenParams::Code, value, params.code); break;
      case TokenParams::ClientID:
	valid &= set(params, TokenParams::ClientID, value, params.clientID); break;
      case TokenParams::RedirectURI:
	valid &= set(params, TokenParams::RedirectURI, value,
	  params.redirectURI); break;
      case TokenParams::CodeVerifier:
	valid &= set(params, TokenParams::CodeVerifier, value,
	  params.codeVerifier); break;
      case TokenParams::RefreshToken:
	valid &= set(params, TokenParams::RefreshToken, value,
	  params.refreshToken); break;
      case TokenParams::Scope:
	valid &= set(params, TokenParams::Scope, value, params.scope); break;
    }
  }) && valid;
}

bool parseRevoke(ZuSpan<char> data, RevokeParams &params)
{
  params = {};
  bool valid = true;
  constexpr auto matcher = ZuMatcher<RevokeFields>();
  return formEach(data, [&params, &valid, &matcher](
      ZuCSpan name, ZuCSpan value) {
    switch (matcher.exact(name)) {
      case RevokeParams::Token:
	valid &= set(params, RevokeParams::Token, value, params.token); break;
      case RevokeParams::TokenTypeHint:
	valid &= set(params, RevokeParams::TokenTypeHint, value,
	  params.tokenTypeHint); break;
      case RevokeParams::ClientID:
	valid &= set(params, RevokeParams::ClientID, value, params.clientID); break;
    }
  }) && valid;
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
    ZuBox<uint64_t> maxAge;
    if (maxAge.scan(params.maxAge) != int(params.maxAge.length()) ||
	ZuCmp<uint64_t>::null(maxAge))
      return ProfileError::Unsupported;
  }
  if (params.has(F::Prompt)) {
    unsigned promptCount = 0;
    unsigned offset = 0;
    unsigned n = params.prompt.length();
    while (offset < n) {
      unsigned end = offset;
      while (end < n && params.prompt[end] != ' ')
	++end;
      ZuCSpan value{params.prompt.data() + offset, end - offset};
      if (!value || (value != "none" && value != "login" &&
          value != "consent")) return ProfileError::Unsupported;
      if (promptCount) return ProfileError::Unsupported;
      ++promptCount;
      offset = end + (end < params.prompt.length());
    }
  }
  return ProfileError::OK;
}

int validateToken(const TokenParams &params, int &grant)
{
  using F = TokenParams;
  grant = TokenGrant::Invalid;
  if (!params.has(F::GrantType)) return ProfileError::Missing;
  if (!params.grantType) return ProfileError::Empty;

  unsigned required = 0;
  constexpr auto matcher = ZuMatcher<GrantTypes>();
  switch (matcher.exact(params.grantType)) {
    case 0:
      grant = TokenGrant::AuthorizationCode;
      required = (1U<<F::GrantType) | (1U<<F::Code) |
	(1U<<F::RedirectURI) | (1U<<F::CodeVerifier);
      break;
    case 1:
      grant = TokenGrant::RefreshToken;
      required = (1U<<F::GrantType) | (1U<<F::RefreshToken);
      break;
    case 2:
      grant = TokenGrant::ClientCredentials;
      required = 1U<<F::GrantType;
      break;
    default: return ProfileError::Unsupported;
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
      using Form = ZuPercent::Codec<ZfURI::PercentQuote<true>>;
      auto client = Form::decode({encoded.data(), j});
      auto secret = Form::decode({encoded.data() + j + 1, expected - j - 1});
      if (!client || !client.out || !secret) return false;
      auth.clientID = {encoded.data(), unsigned(client.out)};
      auth.secret = {encoded.data() + j + 1, unsigned(secret.out)};
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
  ZuBArray<OpaqueSecretSize> secret(OpaqueSecretSize, false);
  if (!rng.random(secret)) return false;
  opaqueFinish(id, secret, opaque);
  ZuClear(secret.data(), secret.length());
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
  ZuBArray<OpaqueIDSize + ChallengeSize> random(
    OpaqueIDSize + ChallengeSize, false);
  unsigned randomSize = OpaqueIDSize + (passkey ? ChallengeSize : 0);
  if (!rng.random({random.data(), randomSize})) return false;

  Grant next;
  next.id = Bytes{ZuBSpan{random.data(), OpaqueIDSize}};
  next.issuer = issuer;
  next.appID = selection.appID;
  next.audienceID = selection.audienceID;
  next.clientID = params.clientID;
  next.audience = selection.audience;
  next.redirectURI = params.redirectURI;
  next.scope = selection.scope;
  next.requestedRoleIDs = selection.roleIDs;
  if (params.has(AuthorizeParams::Nonce)) next.nonce = params.nonce;
  if (passkey)
    next.challenge = Bytes{ZuBSpan{random.data() + OpaqueIDSize, ChallengeSize}};
  next.bindingDigest = bindingDigest;
  next.pkceChallenge = Bytes{params.codeChallenge};
  if (params.has(AuthorizeParams::State)) {
    next.oauthState = params.state;
    next.oauthStatePresent = true;
  }
  if (params.has(AuthorizeParams::Prompt)) {
    next.prompt = params.prompt;
    next.promptPresent = true;
  }
  if (params.has(AuthorizeParams::MaxAge)) {
    ZuBox<uint64_t> maxAge;
    if (maxAge.scan(params.maxAge) != int(params.maxAge.length()) ||
	ZuCmp<uint64_t>::null(maxAge))
      return false;
    next.maxAge = maxAge;
    next.maxAgePresent = true;
  }
  next.authVersion = authVersion;
  next.created = created;
  next.expires = expires;
  next.kind = GrantKind::Ceremony;
  next.purpose = GrantPurpose::Authorization;
  next.state = State::Active;
  ZuClear(random.data(), randomSize);
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
  String json;
  ZfJSON::save(json, TokenResponseWire{
    .accessToken = response.accessToken, .expiresIn = response.expiresIn,
    .scope = response.scope, .idToken = response.idToken,
    .refreshToken = response.refreshToken});
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
  String json;
  ZfJSON::save(json, OAuthErrorWire{errorCode(error)});
  return json;
}

static String redirect(
    ZuCSpan redirectURI, ZuCSpan name, ZuCSpan value,
    ZuCSpan state, bool statePresent)
{
  String uri{redirectURI};
  uri << (redirectURI.find<"?">() >= 0 ? '&' : '?') << name << '=';
  ZfURI::PathQuote::quote(uri, value);
  if (statePresent) {
    uri << "&state=";
    ZfURI::PathQuote::quote(uri, state);
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
  ZuBArray<Ztls::MD<>::Size> digest(Ztls::MD<>::Size, false);
  { Ztls::MD<> md; md.update(ZuBSpan{verifier}); md.finish(digest); }
  ZuBArray<ZuBase64URL::enclen(Ztls::MD<>::Size)> encoded(
    ZuBase64URL::enclen(Ztls::MD<>::Size), false);
  unsigned n = ZuBase64URL::encode(
    encoded.span(), digest);
  return Ztls::ctEqual(
    ZuBSpan{challenge}, ZuBSpan{encoded.data(), n});
}

} // namespace Zum
