//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP ordered logical-stream tunnel adapter

#ifndef ZhttpTunnel_HH
#define ZhttpTunnel_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

namespace Zhttp {

// Non-owning, shard-affine adapter over a logical HTTP stream.  The callback
// receives the native pooled Tx layer and must not retain it after returning.
template <typename Link>
class Tunnel {
public:
  Tunnel(Link &link) : m_link{&link} { }

  bool peerCap() const {
    auto tx = m_link->txStream();
    return tx.extendedConnect();
  }
  bool localCap() const {
    auto tx = m_link->txStream();
    return tx.localExtendedConnect();
  }

  template <typename L>
  void send(L &&l) {
    auto tx = m_link->txStream();
    auto body = tx.body();
    ZuFwd<L>(l)(body);
    body.flush();
  }

  void end() {
    auto tx = m_link->txStream();
    tx.end();
  }
  void reset() { m_link->disconnect(); }

private:
  Link	*m_link = nullptr;
};

} // namespace Zhttp

#endif /* ZhttpTunnel_HH */
