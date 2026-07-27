//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - normalized server engine

#ifndef ZhttpServer_HH
#define ZhttpServer_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZhttpLink.hh>

namespace Zhttp {

template <typename App, typename Protocol>
class Server :
  public Transport_::Traits<Protocol>::template Server<App> {
  using Traits = Transport_::Traits<Protocol>;

public:
  using Base = typename Traits::template Server<App>;
  using Base::init;
  using Base::start;
  enum {
    TLS = Traits::Secure,
    Multiplexed = Traits::Multiplexed
  };

  auto impl() const { return static_cast<const App *>(this); }
  auto impl() { return static_cast<App *>(this); }

  bool init(const EngineConfig &engine, const typename Traits::Config &config) {
    return Base::init(Traits::serverParams(engine, config));
  }
  bool start() {
    if (!Base::start()) return false;
    Base::listen();
    return true;
  }
  template <typename Done>
  void start(Done &&done) {
    Base::start([this, done = ZuFwd<Done>(done)](bool ok) mutable {
      if (ok) Base::listen();
      done(ok);
    });
  }
  void stopAccepting() { Base::stopListening(); }

  // Native TCP/TLS transport teardown reaches this hook before the
  // application timer-drain continuation below has completed.
  void linkDisconnected_() { }
  void linkDrained_() { Base::linkDisconnected_(); }

  ZiConnection *accepted(const ZiCxnInfo &ci) {
    if (!impl()->admit(ci)) return nullptr;
    using Link = typename App::Link;
    return new typename Link::Cxn(new Link{impl(), ci}, ci);
  }

  bool admit(const ZiCxnInfo &) { return true; }
  void release() { }
  unsigned idleTimeout() const { return 0; }

  template <typename Link>
  void connected(Link &, ConnectedInfo) { }
  template <typename Link>
  void disconnected(Link &, bool) { }
};

} // namespace Zhttp

#endif /* ZhttpServer_HH */
