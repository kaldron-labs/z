//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// request parameters for Zum's restricted OAuth profile

#ifndef zumd_oauth_HH
#define zumd_oauth_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZuSpan.hh>

#include <zlib/ZfJSON.hh>

#include <zlib/zumd.hh>

namespace Ztls { class Random; }

namespace Zum {

namespace ProfileError {
  enum { OK = 0, Missing, Empty, Unsupported };
}

namespace TokenGrant {
  enum { Invalid = -1, AuthorizationCode, RefreshToken, ClientCredentials };
}

namespace OAuthError {
  enum {
    InvalidRequest,
    InvalidClient,
    InvalidGrant,
    UnauthorizedClient,
    AccessDenied,
    UnsupportedResponseType,
    UnsupportedGrantType,
    InvalidScope,
    LoginRequired,
    ConsentRequired,
    ServerError,
    TemporarilyUnavailable
  };
}

namespace ClientAuth {
  enum { OK = 0, InvalidClient, UnauthorizedGrant };
}

struct TokenResponse {
	String	accessToken;
	String	tokenType{"Bearer"};
	String	idToken;
  String	refreshToken;
  String	scope;
  uint64_t	expiresIn = 0;
};
ZfStruct(, (TokenResponse, JSON),
  (((accessToken),	(JSON::ID<"access_token">, Required)), (String)),
  (((tokenType),	(JSON::ID<"token_type">, Required)),	(String)),
  (((expiresIn),	(JSON::ID<"expires_in">, Required)),	(UInt64)),
  (((scope),		(Required)),	(String)),
  (((idToken),		(JSON::ID<"id_token">, JSON::Opt)),	(String)),
  (((refreshToken),	(JSON::ID<"refresh_token">, JSON::Opt)), (String)));

inline void tokenClear(TokenResponse &response) {
  if (response.accessToken && response.accessToken.mutable_())
    ZuClear(response.accessToken);
  if (response.idToken && response.idToken.mutable_())
    ZuClear(response.idToken);
  if (response.refreshToken && response.refreshToken.mutable_())
    ZuClear(response.refreshToken);
  response = {};
}

struct AuthorizeParams {
  enum {
    ResponseType,
    ClientID,
    RedirectURI,
    Scope,
    State,
    CodeChallenge,
    CodeChallengeMethod,
    Nonce,
    LoginHint,
    Resource,
    Prompt,
    MaxAge
  };

  ZuCSpan	responseType;
  ZuCSpan	clientID;
  ZuCSpan	redirectURI;
  ZuCSpan	scope;
  ZuCSpan	state;
  ZuCSpan	codeChallenge;
  ZuCSpan	codeChallengeMethod;
  ZuCSpan	nonce;
  ZuCSpan	loginHint;
  ZuCSpan	resource;
  ZuCSpan	prompt;
  ZuCSpan	maxAge;
  uint16_t	seen = 0;

  bool has(unsigned field) const { return seen & (1U << field); }
};

struct TokenParams {
  enum {
    GrantType,
    Code,
    ClientID,
    RedirectURI,
    CodeVerifier,
    RefreshToken,
    Scope
  };

  ZuCSpan	grantType;
  ZuCSpan	code;
  ZuCSpan	clientID;
  ZuCSpan	redirectURI;
  ZuCSpan	codeVerifier;
  ZuCSpan	refreshToken;
  ZuCSpan	scope;
  uint8_t	seen = 0;

  bool has(unsigned field) const { return seen & (1U << field); }
};

struct RevokeParams {
  enum { Token, TokenTypeHint, ClientID };

  ZuCSpan	token;
  ZuCSpan	tokenTypeHint;
  ZuCSpan	clientID;
  uint8_t	seen = 0;

  bool has(unsigned field) const { return seen & (1U << field); }
};

struct BasicAuth {
  ZuCSpan	clientID;
  ZuCSpan	secret;
};

struct OpaqueToken {
  String	token;
  Bytes		id;
  Bytes		digest;
};

// 128-bit lookup IDs and 256-bit bearer secrets balance indexed storage with
// brute-force resistance for opaque OAuth artifacts.
enum { OpaqueIDSize = 16, OpaqueSecretSize = 32 };

ZumExtern int formDecode_(ZuSpan<char>);

template <typename L>
bool formEach(ZuSpan<char> data, L field)
{
  while (data) {
    unsigned end = 0;
    unsigned dataLength = data.length();
    while (end < dataLength && data[end] != '&') ++end;
    auto part = data;
    part.trunc(end);
    if (end < dataLength)
      data.offset(end + 1);
    else
      data = {};
    if (!part) continue;

    unsigned equal = 0;
    unsigned partLength = part.length();
    while (equal < partLength && part[equal] != '=') ++equal;
    auto name = part;
    name.trunc(equal);
    auto value = part;
    if (equal < partLength)
      value.offset(equal + 1);
    else
      value = {part.data() + partLength, 0};

    int nameLength = formDecode_(name);
    int valueLength = formDecode_(value);
    if (nameLength < 0 || valueLength < 0) return false;
    field(ZuCSpan{name.data(), unsigned(nameLength)},
      ZuCSpan{value.data(), unsigned(valueLength)});
  }
  return true;
}

ZumExtern bool parseAuthorize(ZuSpan<char>, AuthorizeParams &);
ZumExtern bool parseToken(ZuSpan<char>, TokenParams &);
ZumExtern bool parseRevoke(ZuSpan<char>, RevokeParams &);
ZumExtern int validateAuthorize(const AuthorizeParams &);
ZumExtern int validateToken(const TokenParams &, int &grant);
ZumExtern int validateRevoke(const RevokeParams &);
ZumExtern bool parseBasic(ZuSpan<char>, BasicAuth &);
ZumExtern bool opaqueIssue(Ztls::Random &, OpaqueToken &);
ZumExtern bool opaqueIssue(Ztls::Random &, ZuBSpan id, OpaqueToken &);
ZumExtern bool opaqueParse(ZuCSpan token, Bytes &id, Bytes &digest);
ZumExtern Bytes opaqueDigest(ZuCSpan token);
ZumExtern bool authorizeClient(
  const Client &, const AuthorizeParams &);
ZumExtern bool authorizationBegin(
  Ztls::Random &, Grant &, ZuCSpan issuer, const App &, const AuthorizeParams &,
  const ScopeSelection &, bool passkey, ZuBSpan bindingDigest,
  int64_t created, int64_t expires);
ZumExtern bool authorizationFinish(
  Ztls::Random &, Grant &, ZuBSpan bindingDigest, UserID,
  Bytes credentialID, IDVec roleIDs, ZtBitmap actions, uint64_t authVersion,
  uint64_t userVersion, int64_t authTime, int64_t codeExpires, String &code);
ZumExtern bool codeMatches(
  const Grant &, ZuBSpan codeDigest, ZuCSpan clientID, ZuCSpan redirectURI,
  ZuCSpan verifier, int64_t now);
ZumExtern String tokenResponseJSON(const TokenResponse &);
ZumExtern String oauthErrorJSON(int error);
ZumExtern String codeRedirect(
  ZuCSpan redirectURI, ZuCSpan code, ZuCSpan state, bool statePresent);
ZumExtern String codeRedirect(const Grant &, ZuCSpan code);
ZumExtern String errorRedirect(
  ZuCSpan redirectURI, int error, ZuCSpan state, bool statePresent);
ZumExtern int authenticateClient(
  const Client &, int grant, const TokenParams &, const BasicAuth *,
  int64_t now = 0);
ZumExtern bool redirectMatches(ClientProfile::T, ZuBSpan registered,
  ZuBSpan requested);
ZumExtern bool pkceVerify(ZuCSpan challenge, ZuCSpan verifier);

} // namespace Zum

#endif /* zumd_oauth_HH */
