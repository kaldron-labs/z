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

template <typename Impl, typename Request>
struct PingBuilder_ : public Zrest::ReqBuilder<Impl, Request> {
  enum { Query = Zrest::QueryPolicy::URI };

  using Path = PingPath;

  using Responses = ZuTypeList<PongParser, PingUnauthorizedParser>;

  const Request &queryObject(const Request *request) const { return *request; }
};

// Application implementation skeleton:
//
// template <typename Request>
// struct PingBuilder : public PingBuilder_<PingBuilder, Request> {
//   using Base = PingBuilder_<PingBuilder, Request>;
//   using Base::header;
//
//   using Headers = ZhttpHeaders("authorization");
//
//   template <typename Key, typename L> void header(L &&) const;
// };

struct PongParser : public Zrest::ResParser<PongParser, Pong> {
  enum { Body = Zrest::BodyPolicy::JSON };
};
struct PingUnauthorizedParser : public Zrest::ResParser<
    PingUnauthorizedParser, Unauthorized> {
  enum { Status = 401, Body = Zrest::BodyPolicy::Zero };
};

#endif /* zrestproto_cli_HH */
