//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zrestauth_cli_HH
#define zrestauth_cli_HH

#include <zlib/ZrestClient.hh>

#include "zrestauth.hh"

struct AuthTokensParser;
struct AuthUnauthorizedParser;
struct AuthInternalParser;
struct RefreshTokensParser;
struct RefreshUnauthorizedParser;
struct RefreshInternalParser;

template <typename Request>
struct AuthBuilder_ : public Zrest::ReqBuilder<AuthBuilder_<Request>, Request> {
  enum { Method = Zhttp::Method::POST, Body = Zrest::BodyPolicy::JSON };
  using Path = AuthPath;
  using Responses = ZuTypeList<
    AuthTokensParser, AuthUnauthorizedParser, AuthInternalParser>;
  const Credentials &bodyObject(const Request *request) const {
    return *request;
  }
};
template <typename Request>
struct RefreshBuilder_ : public Zrest::ReqBuilder<RefreshBuilder_<Request>, Request> {
  enum { Method = Zhttp::Method::POST, Body = Zrest::BodyPolicy::JSON };
  using Path = RefreshPath;
  using Responses = ZuTypeList<
    RefreshTokensParser, RefreshUnauthorizedParser, RefreshInternalParser>;
  const RefreshRequest &bodyObject(const Request *request) const {
    return *request->state;
  }
};

struct AuthTokensParser : public Zrest::ResParser<
    AuthTokensParser, TokenResponse> {
  enum { Body = Zrest::BodyPolicy::JSON };
};
struct AuthUnauthorizedParser : public Zrest::ResParser<
    AuthUnauthorizedParser, Unauthorized> {
  enum { Status = 401, Body = Zrest::BodyPolicy::Zero };
};
struct AuthInternalParser : public Zrest::ResParser<
    AuthInternalParser, InternalError> {
  enum { Status = 500, Body = Zrest::BodyPolicy::Zero };
};
struct RefreshTokensParser : public Zrest::ResParser<
    RefreshTokensParser, TokenResponse> {
  enum { Body = Zrest::BodyPolicy::JSON };
};
struct RefreshUnauthorizedParser : public Zrest::ResParser<
    RefreshUnauthorizedParser, Unauthorized> {
  enum { Status = 401, Body = Zrest::BodyPolicy::Zero };
};
struct RefreshInternalParser : public Zrest::ResParser<
    RefreshInternalParser, InternalError> {
  enum { Status = 500, Body = Zrest::BodyPolicy::Zero };
};

#endif /* zrestauth_cli_HH */
