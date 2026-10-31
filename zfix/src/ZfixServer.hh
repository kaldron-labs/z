//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Thin CRTP FIX acceptor over shared session owners

#ifndef ZfixServer_HH
#define ZfixServer_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/ZfixNet.hh>

namespace Zfix {

template <typename App, typename Impl, typename MTList, typename Net = TCP,
  typename RxAlloc = typename Net::template RxBufAlloc<>,
  typename TxAlloc = WireBufAlloc<>, template <typename, typename> class Engine = Link>
class SrvLink : public Net::template SrvLink<App, Impl, RxAlloc, TxAlloc>,
    public Engine<Impl, NativeRx<Net>>, public LinkHooks_<Impl, Net>,
    public NetInput<Impl, typename Net::RxStream> {
  ZuAssert((ZuIs_<TxAlloc, WireBuf>{}));
  using NetBase = Net::template SrvLink<App, Impl, RxAlloc, TxAlloc>;
  using EngineBase = Engine<Impl, NativeRx<Net>>;
  using Hooks = LinkHooks_<Impl, Net>;
public:
  using MessageCatalog = MTList;
  using NetBase::app;
  using NetBase::impl;
  using EngineBase::send;
  using EngineBase::reply;
  using EngineBase::work;
  using Hooks::fixTraceField;
  using Hooks::fixTraceSecret;
  using typename NetBase::Cxn;
  using typename NetBase::RxBufAlloc;
  using typename NetBase::TxBufAlloc;

  SrvLink(App *app, SessionConfig config, uint64_t epoch = 1) :
    SrvLink{app, ZuMv(config), app->fixLinkLimits(), epoch} { }

  SrvLink(App *app, SessionConfig config, LinkLimits limits, uint64_t epoch = 1) :
    NetBase{app}, EngineBase{Hooks::role(ZuMv(config), false), limits, epoch} { }

  bool connectSocket_(bool replacing) {
    if (!NetBase::connectSocket_(replacing)) return false;
    this->clearInput_(); return this->transportBind_(replacing);
  }

  bool connectReady_() {
    // A late accept still settles native accounting; terminal stop cannot
    // start another FIX session while its socket awaits the listener barrier.
    return NetBase::connectReady_() && app()->running();
  }

  void connected(typename Net::Connected) { this->clearInput_(); this->transportUp_(); }

  void disconnected(bool peer) { this->clearInput_(); this->transportDown_(peer); }

  int process(typename Net::RxStream &s) { this->input_(s); return this->process_(s); }

  bool sendRecord_(ZmRef<ZiIOBuf> b) { return NetBase::send_(ZuMv(b)); }

  void sent(ZmRef<ZiTxBuf> b, bool ok) {
    this->transportSent(static_cast<WireBuf *>(b.ptr()), ok);
    NetBase::sent(ZuMv(b), ok);
  }
};

// Hubs retain Ztcp/Ztls lifecycle and listener/certificate configuration.
// Applications own their session links and wait for their stopped callback before hub
// stop/final, including cancellation/completion of their deferred hooks.
template <typename App, typename MTList, typename Net = TCP,
  template <typename, typename> class Engine = Link>
class Server : public Net::template Server<App>, public EndpointHub<App> {
public:
  explicit Server(HubLimits limits = {}) : EndpointHub<App>{limits} { }

  using Params = typename Net::ServerParams;
  using MessageCatalog = MTList;
  template <typename Impl> using LinkBase = SrvLink<App, Impl, MTList, Net,
    typename Net::template RxBufAlloc<>, WireBufAlloc<>, Engine>;
};

template <typename App, typename MTList, typename Net = TCP>
using RawServer = Server<App, MTList, Net, Raw>;
template <typename App, typename Impl, typename MTList, typename Net = TCP,
  typename RxAlloc = typename Net::template RxBufAlloc<>, typename TxAlloc = WireBufAlloc<>>
using RawSrvLink = SrvLink<App, Impl, MTList, Net, RxAlloc, TxAlloc, Raw>;
} // Zfix

#endif /* ZfixServer_HH */
