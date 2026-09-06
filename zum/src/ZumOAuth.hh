//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// request parameters for Zum's restricted OAuth profile

#ifndef ZumOAuth_HH
#define ZumOAuth_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZuSpan.hh>

#include <zlib/Zum.hh>

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
    ServerError,
    TemporarilyUnavailable
  };
}

namespace ClientAuth {
  enum { OK = 0, InvalidClient, UnauthorizedGrant };
}

struct TokenResponse {
  String	accessToken;
  String	refreshToken;
  String	scope;
  uint64_t	expiresIn = 0;
};

inline void tokenClear(TokenResponse &response) {
  if (response.accessToken && response.accessToken.mutable_())
    ZuClear(response.accessToken.data(), response.accessToken.length());
  if (response.refreshToken && response.refreshToken.mutable_())
    ZuClear(response.refreshToken.data(), response.refreshToken.length());
  response = {};
}

struct AuthorizeParams {
  enum Field {
    ResponseType,
    ClientID,
    RedirectURI,
    Scope,
    State,
    CodeChallenge,
    CodeChallengeMethod
  };

  ZuCSpan	responseType;
  ZuCSpan	clientID;
  ZuCSpan	redirectURI;
  ZuCSpan	scope;
  ZuCSpan	state;
  ZuCSpan	codeChallenge;
  ZuCSpan	codeChallengeMethod;
  uint8_t	seen = 0;

  bool has(Field field) const { return seen & (1U << field); }
};

struct TokenParams {
  enum Field {
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

  bool has(Field field) const { return seen & (1U << field); }
};

struct RevokeParams {
  enum Field { Token, TokenTypeHint, ClientID };

  ZuCSpan	token;
  ZuCSpan	tokenTypeHint;
  ZuCSpan	clientID;
  uint8_t	seen = 0;

  bool has(Field field) const { return seen & (1U << field); }
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

inline int formHex_(char c)
{
  c |= 0x20;
  return c >= '0' && c <= '9' ? int(c - '0') :
    c >= 'a' && c <= 'f' ? int(c - 'a') + 10 : -1;
}

inline ZuCSpan formDecode_(ZuSpan<char> data)
{
  unsigned out = 0;
  for (unsigned in = 0, n = data.length(); in < n; ++in) {
    char c = data[in];
    if (c == '+') {
      data[out++] = ' ';
    } else if (c == '%' && in + 2 < n) {
      int hi = formHex_(data[in + 1]);
      int lo = formHex_(data[in + 2]);
      if (hi >= 0 && lo >= 0) {
	data[out++] = char((hi << 4) | lo);
	in += 2;
      } else {
	data[out++] = c;
      }
    } else {
      data[out++] = c;
    }
  }
  return {data.data(), out};
}

template <typename L>
void formEach(ZuSpan<char> data, L field)
{
  while (data) {
    unsigned end = 0;
    while (end < data.length() && data[end] != '&') ++end;
    auto part = data;
    part.trunc(end);
    if (end < data.length())
      data.offset(end + 1);
    else
      data = {};
    if (!part) continue;

    unsigned equal = 0;
    while (equal < part.length() && part[equal] != '=') ++equal;
    auto name = part;
    name.trunc(equal);
    auto value = part;
    if (equal < part.length())
      value.offset(equal + 1);
    else
      value = {part.data() + part.length(), 0};

    field(formDecode_(name), formDecode_(value));
  }
}

ZumExtern void parseAuthorize(ZuSpan<char>, AuthorizeParams &);
ZumExtern void parseToken(ZuSpan<char>, TokenParams &);
ZumExtern void parseRevoke(ZuSpan<char>, RevokeParams &);
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
  Ztls::Random &, Grant &, ZuCSpan issuer, const AuthorizeParams &,
  const ScopeSelection &, bool passkey, ZuBSpan bindingDigest,
  uint64_t authVersion,
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
  const Client &, int grant, const TokenParams &, const BasicAuth *);
ZumExtern bool redirectMatches(ClientType::T, ZuBSpan registered,
  ZuBSpan requested);
ZumExtern bool pkceVerify(ZuCSpan challenge, ZuCSpan verifier);

} // namespace Zum

#endif /* ZumOAuth_HH */
