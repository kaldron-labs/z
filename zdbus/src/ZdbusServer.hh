//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Service-side bus client; outbound serials belong to the contained client.

#ifndef ZdbusServer_HH
#define ZdbusServer_HH

#ifndef ZdbusLib_HH
#include <zlib/ZdbusLib.hh>
#endif

#include <zlib/ZuTuple.hh>
#include <zlib/ZmHash.hh>
#include <zlib/Zdbus.hh>

namespace Zdbus_ {

struct ServerClient;

namespace ServerDefault {
  enum { RouteLimit = 1024 }; // bounded exact-key routes; tunable by service
}

struct ServerParams {
  ClientParams	cli;
  unsigned	routeLimit = ServerDefault::RouteLimit;
};

using RouteKey = ZuTuple<ZuCSpan, ZuCSpan, ZuCSpan>;
using RouteText = PathKeyText<"Zdbus.RouteText">;

struct Route_ {
  RouteText	parts;
  MethodFn	fn;

  Route_(ZuCSpan path_, ZuCSpan interface_, ZuCSpan member_, MethodFn fn_)
  : parts{path_, interface_, member_}, fn{ZuMv(fn_)} { }
  static RouteKey KeyAxor(const Route_ &route) {
    return {route.parts.path(), route.parts.interface(),
      route.parts.member()};
  }
};
ZmHashDerive(RouteTable, Route_,
  (ZmHashNode<Route_,
    ZmHashKey<Route_::KeyAxor,
      ZmHashHeapID<"Zdbus.Route">>>));
using Route = RouteTable::Node;

class ZdbusAPI Server {
public:
  Server() = default;
  ~Server();

  Server(const Server &) = delete;
  Server &operator =(const Server &) = delete;

  void init(ZmScheduler *, unsigned rxSid, unsigned txSid,
    Address, ZuCSpan name, ServerParams, ReadyFn, MethodFn,
    SignalFn, CxnFailFn);
  void start();
  void stop(CxnStopFn = {});
  void final();
  // Register exact routes before start; duplicate routes are rejected.
  bool route(ZuCSpan path, ZuCSpan interface, ZuCSpan member, MethodFn);
  template <typename Req, typename Fn>
  bool route(Fn &&fn) {
    using Fixed = FixedRoute<Req>;
    return route(Fixed::path(), Fixed::interface(), Fixed::member(), MethodFn{
      [fn = ZuFwd<Fn>(fn)](ZmRef<ZiIOBuf> frame,
          FrameInfo info) mutable {
        parseTyped<Req>(ZuMv(frame), info, fn);
      }});
  }
  void call(BuildFn, CallFn, ZuTime timeout = {});
  void cancel(uint32_t serial, CxnSendFn done = {});
  template <typename Req, typename Fn>
  void call(BuildFn build, Fn &&fn, ZuTime timeout = {}) {
    call(ZuMv(build), typedCall<Req>(ZuFwd<Fn>(fn)), timeout);
  }
  void send(BuildFn, CxnSendFn = {});
  void addMatch(ZuCSpan rule, CallFn fn, ZuTime timeout = {});
  void removeMatch(ZuCSpan rule, CallFn fn, ZuTime timeout = {});
  void subscribe(ZuCSpan path, ZuCSpan interface, ZuCSpan member,
    SignalFn fn, SubDoneFn done);
  template <typename Sig, typename Fn>
  void subscribe(Fn &&fn, SubDoneFn done) {
    using Fixed = FixedRoute<Sig>;
    subscribe(Fixed::path(), Fixed::interface(), Fixed::member(),
      typedSignal<Sig>(ZuFwd<Fn>(fn)), ZuMv(done));
  }
  void unsubscribe(ZuCSpan path, ZuCSpan interface, ZuCSpan member,
    uint64_t id, CxnSendFn done = {});

private:
  void connected_(ZuCSpan);
  void nameReply_(CallResult);
  void method_(ZmRef<ZiIOBuf>, FrameInfo);

private:
  ZtString<ZtStringHeapID<"Zdbus.ServiceName">> m_name;
  ReadyFn	m_readyFn;
  MethodFn	m_methodFn;
  CxnFailFn	m_failFn;
  ServerParams	m_params;
  RouteTable	m_routes;
  bool		m_started = false;
  ServerClient	*m_cli = nullptr;
};

} // Zdbus_

using ZdbusServer = Zdbus_::Server;

#endif /* ZdbusServer_HH */
