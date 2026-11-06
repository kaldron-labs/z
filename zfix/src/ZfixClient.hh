//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Thin CRTP FIX initiator over shared session owners

#ifndef ZfixClient_HH
#define ZfixClient_HH

#ifndef ZfixLib_HH
#include <zlib/ZiTx.hh>
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/ZfixNet.hh>

namespace Zfix {

template <typename App, typename Impl, typename MTList, typename Net = TCP,
  typename RxAlloc = typename Net::template RxBufAlloc<>,
  typename TxAlloc = Zi::IOBufAlloc<ZiTxBuf>,
  template <typename, typename> class Engine = Link>
class CliLink : public Net::template CliLink<App, Impl, RxAlloc, TxAlloc>,
    public Engine<Impl, NativeRx<Net>>, public LinkHooks_<Impl, Net>,
    public NetInput<Impl, typename Net::RxStream> {
  ZuAssert((ZuIs_<TxAlloc, ZiTxBuf>{}));
  using NetBase = Net::template CliLink<App, Impl, RxAlloc, TxAlloc>;
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

  CliLink(App *app, SessionConfig config, uint64_t epoch = 1) :
    CliLink{app, ZuMv(config), app->fixLinkLimits(), epoch} { }

  CliLink(App *app, SessionConfig config, LinkLimits limits, uint64_t epoch = 1) :
    NetBase{app}, EngineBase{Hooks::role(ZuMv(config), true), limits, epoch} { }

  CliLink(App *app, ZuID id, SessionConfig config, uint64_t epoch = 1) :
    CliLink{app, ZuMv(id), ZuMv(config), app->fixLinkLimits(), epoch} { }

  CliLink(App *app, ZuID id, SessionConfig config,
      LinkLimits limits, uint64_t epoch = 1) :
    NetBase{app, ZuMv(id)},
    EngineBase{Hooks::role(ZuMv(config), true), limits, epoch} { }

  using EngineBase::connectAllowed_;

  void connectRetired_() { this->clearInput_(); this->transportDown_(false); }

  bool connectSocket_(bool replacing) {
    if (!NetBase::connectSocket_(replacing)) return false;
    this->clearInput_(); return this->transportBind_(replacing);
  }

  void connected(typename Net::Connected) { this->clearInput_(); this->transportUp_(); }

  void disconnected(bool peer) { this->clearInput_(); this->transportDown_(peer); }

  int process(typename Net::RxStream &s) { this->input_(s); return this->process_(s); }

  bool sendRecord_(ZmRef<ZiIOBuf> b) { return NetBase::send_(ZuMv(b)); }

  void sent(ZmRef<ZiTxBuf> b, bool ok) {
    this->transportSent(b, ok);
    NetBase::sent(ZuMv(b), ok);
  }
};

// Hubs retain Ztcp/Ztls lifecycle and listener/certificate configuration.
// Applications own their session links and wait for their stopped callback before hub
// stop/final, including cancellation/completion of their deferred hooks.
template <typename App, typename MTList, typename Net = TCP,
  template <typename, typename> class Engine = Link>
class Client : public Net::template Client<App>, public EndpointHub<App> {
public:
  explicit Client(HubLimits limits = {}) : EndpointHub<App>{limits} { }

  using Params = typename Net::ClientParams;
  using MessageCatalog = MTList;
  template <typename Impl> using LinkBase = CliLink<App, Impl, MTList, Net,
    typename Net::template RxBufAlloc<>, Zi::IOBufAlloc<ZiTxBuf>, Engine>;
};

template <typename App, typename MTList, typename Net = TCP>
using RawClient = Client<App, MTList, Net, Raw>;
template <typename App, typename Impl, typename MTList, typename Net = TCP,
  typename RxAlloc = typename Net::template RxBufAlloc<>,
  typename TxAlloc = Zi::IOBufAlloc<ZiTxBuf>>
using RawCliLink = CliLink<App, Impl, MTList, Net, RxAlloc, TxAlloc, Raw>;
} // Zfix

#endif /* ZfixClient_HH */
