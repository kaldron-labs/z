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

template <typename Impl>
struct PingParser_ : public Zrest::ReqParser<Impl, Ping> {
  enum { Method = Zhttp::Method::GET, Query = Zrest::QueryPolicy::URI };

  using Path = PingPath;

  using Responses = ZuTypeList<PingOK, PingUnauthorized>;
};

// Application implementation skeleton:
//
// struct PingParser : public PingParser_<PingParser> {
//   using Base = PingParser_<PingParser>;
//   using Base::header;
//
//   using Headers = ZhttpHeaders("authorization");
//
//   // application state
//
//   template <typename Key>
//   void header(Zhttp::FieldSection::T, ZuSpan<uint8_t>);
//
//   template <typename Link> void complete(Link *, bool);
// };

struct PingOK : public Zrest::ResBuilder<PingOK, Pong> {
  enum { Body = Zrest::BodyPolicy::JSON };
};
struct PingUnauthorized : public Zrest::ResBuilder<
    PingUnauthorized, Unauthorized> {
  enum { Status = 401, Body = Zrest::BodyPolicy::Zero };
};

#endif /* zrestproto_srv_HH */
