//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zrestproto_cli_HH
#define zrestproto_cli_HH

#include <zlib/ZrestClient.hh>

#include "zrestproto.hh"

struct PongParser;
struct PingUnauthorizedParser;

template <typename Request>
struct PingBuilder_ : public Zrest::ReqBuilder<PingBuilder_<Request>, Request> {
  using Base = Zrest::ReqBuilder<PingBuilder_<Request>, Request>;
  using Base::header;
  enum { Query = Zrest::QueryPolicy::URI };
  using Path = PingPath;
  using Headers = ZhttpHeaders("authorization");
  using Responses = ZuTypeList<PongParser, PingUnauthorizedParser>;
  const Ping &queryObject(const Request *request) const { return *request; }
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "authorization") l(this->object->state->bearer);
    else Base::template header<Key>(ZuFwd<L>(l));
  }
};

struct PongParser : public Zrest::ResParser<PongParser, Pong> {
  enum { Body = Zrest::BodyPolicy::JSON };
};
struct PingUnauthorizedParser : public Zrest::ResParser<
    PingUnauthorizedParser, Unauthorized> {
  enum { Status = 401, Body = Zrest::BodyPolicy::Zero };
};

#endif /* zrestproto_cli_HH */
