//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zrestauth_srv_HH
#define zrestauth_srv_HH

#include <zlib/ZrestServer.hh>

#include "zrestauth.hh"

struct AuthOK;
struct AuthUnauthorized;
struct AuthInternalError;
struct RefreshOK;
struct RefreshUnauthorized;
struct RefreshInternalError;
class App;

struct AuthParser : public Zrest::ReqParser<AuthParser, Credentials> {
  enum { Method = Zhttp::Method::POST, Body = Zrest::BodyPolicy::JSON };

  using Path = AuthPath;

  using Responses = ZuTypeList<AuthOK, AuthUnauthorized, AuthInternalError>;

  App *app = nullptr;

  template <typename Link> void complete(Link *, bool);
};

struct RefreshParser : public Zrest::ReqParser<RefreshParser, RefreshRequest> {
  enum { Method = Zhttp::Method::POST, Body = Zrest::BodyPolicy::JSON };

  using Path = RefreshPath;

  using Responses = ZuTypeList<
    RefreshOK, RefreshUnauthorized, RefreshInternalError>;

  App *app = nullptr;

  template <typename Link> void complete(Link *, bool);
};

struct AuthOK : public Zrest::ResBuilder<AuthOK, TokenResponse> {
  enum { Body = Zrest::BodyPolicy::JSON };
};
struct AuthUnauthorized : public Zrest::ResBuilder<
    AuthUnauthorized, Unauthorized> {
  enum { Status = 401, Body = Zrest::BodyPolicy::Zero };
};
struct AuthInternalError : public Zrest::ResBuilder<
    AuthInternalError, InternalError> {
  enum { Status = 500, Body = Zrest::BodyPolicy::Zero };
};
struct RefreshOK : public Zrest::ResBuilder<RefreshOK, TokenResponse> {
  enum { Body = Zrest::BodyPolicy::JSON };
};
struct RefreshUnauthorized : public Zrest::ResBuilder<
    RefreshUnauthorized, Unauthorized> {
  enum { Status = 401, Body = Zrest::BodyPolicy::Zero };
};
struct RefreshInternalError : public Zrest::ResBuilder<
    RefreshInternalError, InternalError> {
  enum { Status = 500, Body = Zrest::BodyPolicy::Zero };
};

#endif /* zrestauth_srv_HH */
