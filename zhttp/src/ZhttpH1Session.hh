//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - HTTP/1 session utilities

#ifndef ZhttpH1Session_HH
#define ZhttpH1Session_HH

#ifndef Zhttp_HH
#include <zlib/Zhttp.hh>
#endif

namespace Zhttp { namespace H1 {

template <typename Impl, typename Link>
struct Session {
  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  void connected(Link &link) { impl()->send(link); }
  void disconnected(Link &link, bool peer) { impl()->closed(link, peer); }

  template <typename Rx>
  int process(Link &link, Rx &rx) {
    return impl()->recv(link, rx);
  }

  void send(Link &) { }
  void closed(Link &, bool) { }
};

template <typename Stream, typename Builder>
void sendReq(Stream &stream, Builder &builder) {
  auto tx = stream.txStream();
  builder.request(tx);
  builder.finish(tx);
}

template <typename Stream, typename Builder>
void sendResp(Stream &stream, Builder &builder) {
  auto tx = stream.txStream();
  builder.response(tx);
  builder.finish(tx);
}

template <typename Impl, typename Parser_>
struct Server {
  using Parser = Parser_;
  using State = typename Parser::State;

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  void connected(auto &) { }
  void disconnected(auto &, bool) { }

  template <typename Link, typename Rx>
  int process(Link &link, Rx &rx) {
    auto state = parser.process(rx);
    if (state == State::Error) return impl()->error(link, parser);
    if (state == State::Complete) {
      int rc = impl()->request(link, parser);
      parser.reset();
      return rc;
    }
    return 0;
  }

  template <typename Link>
  int error(Link &, Parser &) { return -1; }
  template <typename Link>
  int request(Link &, Parser &) { return 1; }

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  Parser	parser;
};

}} // Zhttp::H1

#endif /* ZhttpH1Session_HH */
