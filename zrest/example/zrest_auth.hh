//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zrest_auth_HH
#define zrest_auth_HH

#include "zrestauth.hh"

class Client;

struct TokenState : public RefreshRequest {
  TokenString bearer;
  int64_t deadline = 0;
};

struct AuthReq : public Credentials {
  Client *client = nullptr;
  template <typename Link, typename Response>
  void process(Link *, const Response *) const;
  template <typename Link> void failed(Link *) const;
};

struct RefreshReq : public ExampleObject, public ZmObject {
  Client *client = nullptr;
  ZmRef<const TokenState> state;
  template <typename Link, typename Response>
  void process(Link *, const Response *) const;
  template <typename Link> void failed(Link *) const;
};

#include "zrestauth_cli.hh"

using AuthBuilder = AuthBuilder_<AuthReq>;
using RefreshBuilder = RefreshBuilder_<RefreshReq>;

#endif /* zrest_auth_HH */
