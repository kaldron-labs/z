//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - internal normalized client hub

#ifndef ZhttpClientHub_HH
#define ZhttpClientHub_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZhttpLink.hh>

namespace Zhttp {

template <typename App, typename Profile>
class ClientHub :
  public ProfileTraits<Profile>::Transport::template Client<App> {
  using HTTP = ProfileTraits<Profile>;
  using Traits = typename HTTP::Transport;

public:
  using Base = typename Traits::template Client<App>;
  using Base::init;
  enum {
    TLS = Traits::Secure,
    Multiplexed = HTTP::Multiplexed
  };

  auto impl() const { return static_cast<const App *>(this); }
  auto impl() { return static_cast<App *>(this); }

  bool init(const HubConfig &hub, const typename Traits::Config &config) {
    return Base::init(Traits::clientParams(hub, config));
  }

  unsigned reconnFreq() const { return 0; }

  template <typename Link>
  void connected(Link &, ConnectedInfo) { }
  template <typename Link>
  void disconnected(Link &, bool) { }
  template <typename Link>
  void connectFailed(Link &, bool) { }
};

} // namespace Zhttp

#endif /* ZhttpClientHub_HH */
