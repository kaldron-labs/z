//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zrest_auth_HH
#define zrest_auth_HH

#include "zrestjwt.hh"

class Client;

using ClientDefaultUser = ZuStringT<"test">;
using ClientDefaultPass = ZuStringT<"test123">;

template <typename Heap>
struct TokenState_ : public Heap, public ZmObject {
  TokenString refreshToken;
  TokenString bearer;
  int64_t deadline = 0;
};
using TokenState_Heap = ZmHeap<"zrest.TokenState", TokenState_<ZuVoid>>;
ZuDerive(TokenState, (TokenState_<TokenState_Heap>));

template <typename Heap>
struct AuthReq_ : public Heap, public ZmObject {
  CredString username;
  CredString password;
  Client *client = nullptr;
  template <typename Link, typename Response>
  void process(Link *, const Response *) const;
  template <typename Link> void failed(Link *) const;
};
using AuthReq_Heap = ZmHeap<"zrest.AuthReq", AuthReq_<ZuVoid>>;
ZuDerive(AuthReq, (AuthReq_<AuthReq_Heap>));

template <typename Heap>
struct RefreshReq_ : public Heap, public ZmObject {
  Client *client = nullptr;
  ZmRef<const TokenState> state;
  template <typename Link, typename Response>
  void process(Link *, const Response *) const;
  template <typename Link> void failed(Link *) const;
};
using RefreshReq_Heap = ZmHeap<"zrest.RefreshReq", RefreshReq_<ZuVoid>>;
ZuDerive(RefreshReq, (RefreshReq_<RefreshReq_Heap>));

ZfStruct(, (TokenState, JSON),
  (((refreshToken), (JSON::ID<"refresh_token">, Required)), (String)));
ZfStruct(, (AuthReq, JSON),
  (((username), (Required)), (String)),
  (((password), (Required)), (String)));

#include "zrestauth_cli.hh"

struct AuthBuilder : public AuthBuilder_<AuthBuilder, AuthReq> { };

struct RefreshBuilder : public RefreshBuilder_<RefreshBuilder, RefreshReq> {
  const auto &bodyObject(const RefreshReq *request) const {
    return *request->state;
  }
};

#endif /* zrest_auth_HH */
