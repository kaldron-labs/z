//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZhttpClient.hh>

#include <zlib/Zrest.hh>

struct Pong;
struct Ping {
  template <typename Link>
  void process(Link *link, const Pong *pong) { }

  template <typename Link>
  void failed(Link *link) { }
};
struct Pong { };

ZfStruct((Ping, URI));
ZfStruct((Pong, JSON));

struct PongParser;

struct PingBuilder : public Zrest::ReqBuilder<PingBuilder, Ping> {
  enum { Query = QueryPolicy::URI };

  using Responses = ZuTypeList<PongParser>;
};

struct PongParser : public Zrest::ResParser<PongParser, Pong> {
  enum { Body = BodyPolicy::JSON };
};

using Builder = Zrest::MReqBuilder<PingBuilder>;
using Parser = Zrest::MResParser<PongParser>;

int main()
{
  // FIXME - start zhttp client, send ping
  return 0;
}
