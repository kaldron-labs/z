//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZhttpClient.hh>

#include <zlib/Zrest.hh>

struct Pong;
struct Ping {
  template <typename Link>
  void process(Link *link) {
    // FIXME - send Pong
  }

  template <typename Link>
  void failed(Link *link) { }
};
struct Pong { };

ZfStruct((Ping, URI));
ZfStruct((Pong, JSON));

struct PongBuilder;

struct PingParser : public Zrest::ResParser<PingParser, Ping> {
  enum { Query = BodyPolicy::URI };

  using Responses = ZuTypeList<PongBuilder>;
};

struct PongBuilder : public Zrest::ReqBuilder<PongBuilder, Pong> {
  enum { Body = BodyPolicy::JSON };
};

using Parser = Zrest::MReqParser<PingParser>;
using Builder = Zrest::MResBuilder<PongBuilder>;

int main()
{
  // FIXME - start zhttp server, listen, respond to ping with pong
  return 0;
}
