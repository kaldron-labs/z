//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zrestproto_srv_HH
#define zrestproto_srv_HH

#include <zlib/ZrestServer.hh>

#include "zrestproto.hh"

struct PingOK;
struct PingUnauthorized;
class App;

struct PingParser : public Zrest::ReqParser<PingParser, Ping> {
  using Base = Zrest::ReqParser<PingParser, Ping>;
  using Base::header;

  enum { Method = Zhttp::Method::GET, Query = Zrest::QueryPolicy::URI };

  using Path = PingPath;

  using Headers = ZhttpHeaders("authorization");

  using Responses = ZuTypeList<PingOK, PingUnauthorized>;

  App *app = nullptr;
  TokenString authorization;
  unsigned authorizationCount = 0;

  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "authorization") {
      ++authorizationCount;
      if (value.length() <= sizeof("Bearer ") - 1 + JWTMax)
	authorization = value;
    }
  }

  template <typename Link> void complete(Link *, bool);
};

struct PingOK : public Zrest::ResBuilder<PingOK, Pong> {
  enum { Body = Zrest::BodyPolicy::JSON };
};
struct PingUnauthorized : public Zrest::ResBuilder<
    PingUnauthorized, Unauthorized> {
  enum { Status = 401, Body = Zrest::BodyPolicy::Zero };
};

#endif /* zrestproto_srv_HH */
