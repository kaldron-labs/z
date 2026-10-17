//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Private concrete TLS server bindings
#ifndef ZhttpTLSImpl_HH
#define ZhttpTLSImpl_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif
#include "ZhttpServerImpl.hh"
namespace Zhttp {
struct ServerImpl::TLSHub final : public ZmNHeap<ServerImpl::TLSHub>,
  public TLS_::ServerHub<TLSHub> {
    friend inline ZuStringT<"Zhttp.Server.TLSHub"> ZmNHeapID(TLSHub *);
    using H1Link = TLSH1Link;
    using H2Link = TLSH2Link;
    using ResponseHeaders = ServerImpl::ResponseHeaders;
    ServerImpl *server = nullptr;

    TLSHub(ServerImpl *server_) : server{server_} { }
    ZiIP localIP() const { return server->config().localIP(); }
    unsigned localPort() const { return server->config().port(); }
    unsigned idleTimeout() const {
      return server->config().idleTimeout();
    }
    bool admit(const ZiCxnInfo &) { return server->admit(); }
    const HPackSeedPlans &hpackSeedPlans() const {
      return server->hpackSeedPlans();
    }
    template <typename Link_>
    void connected(Link_ &link, const ConnectedInfo &info) {
      link.rxState().parser.connectedInfo(info);
      server->registerTxError_(link);
      server->impl()->connected(Transport::TLS);
    }
    template <typename Link_>
    void disconnected(Link_ &link, bool) {
      link.responseDisconnected_();
    }
    void connected(Zhttp::Session session) {
      server->sessionConnected_(session);
    }
    void disconnected(Zhttp::Session session) {
      server->sessionDisconnected_(session);
    }
    void release() { server->release(Transport::TLS); }
    void listening(const ZiListenInfo &info) {
      server->impl()->listening(Transport::TLS, info.port);
    }
    void listenFailed(bool transient) {
      server->impl()->listenFailed(Transport::TLS, transient);
      server->failServer_();
    }
  };

struct ServerImpl::TLSH1Link :
    public ZmNHeap<TLSH1Link>,
    public TLS_::ServerH1AppLink<
      TLSHub, TLSH1Link, Session<H1TLS>,
      TLS_::SrvLink<TLSHub>>,
    public Responses<H1TLS, TLSH1Link> {
    friend inline ZuStringT<"Zhttp.Server.TLSH1"> ZmNHeapID(TLSH1Link *);
    using Base = TLS_::ServerH1AppLink<
      TLSHub, TLSH1Link, Session<H1TLS>,
      TLS_::SrvLink<TLSHub>>;
    template <typename ...Args>
    TLSH1Link(Args &&...args) : Base{ZuFwd<Args>(args)...} {
      this->rxState().parser.bind(this->app()->server, this);
    }
    using Responses<H1TLS, TLSH1Link>::send;
    Channel &channel() { return m_channel; }
  private:
    Channel m_channel = Native_::serverChannel(static_cast<TLSH1Link *>(this));
  };

struct ServerImpl::TLSH2Link :
    public ZmNHeap<TLSH2Link>,
    public H2_::ServerAppLink<
      TLSHub, TLSH2Link, Session<H2TLS>,
      TLS_::SrvLink<TLSHub>>,
    public Responses<H2TLS, TLSH2Link> {
    friend inline ZuStringT<"Zhttp.Server.TLSH2"> ZmNHeapID(TLSH2Link *);
    using Base = H2_::ServerAppLink<
      TLSHub, TLSH2Link, Session<H2TLS>,
      TLS_::SrvLink<TLSHub>>;
    template <typename ...Args>
    TLSH2Link(Args &&...args) : Base{ZuFwd<Args>(args)...} {
      this->rxState().parser.bind(this->app()->server, this);
    }
    using Responses<H2TLS, TLSH2Link>::send;
    Channel &channel() { return m_channel; }
  private:
    Channel m_channel = Native_::serverChannel(static_cast<TLSH2Link *>(this));
  };

} // namespace Zhttp
#endif
