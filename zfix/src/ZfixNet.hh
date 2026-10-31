//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Compile-time TCP/TLS transport bindings for managed and raw endpoints

#ifndef ZfixNet_HH
#define ZfixNet_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/Ztcp.hh>
#include <zlib/Ztls.hh>
#include <zlib/ZfixLink.hh>
#include <zlib/ZfixRaw.hh>

namespace Zfix {

// Borrow the transport-owned stream while the link is alive. Application
// continuations reuse its existing extraction operations; no transport resume
// entry point or additional receive queue is needed.
template <typename Impl, typename Stream> class NetInput {
public:
  void resumeRx_() {
    auto impl = static_cast<Impl *>(this);
    ZmAssert(impl->app()->rxInvoked());
    auto &input = impl->nativeRx_().input;
    auto stream = input;
    if (!stream) return;
    while (input == stream && *stream) {
      int n = impl->process_(*stream);
      if (!n) return;
      if (n < 0) { impl->disconnect_(); return; }
    }
  }

protected:
  void input_(Stream &stream) { static_cast<Impl *>(this)->nativeRx_().input = &stream; }
  void clearInput_() { static_cast<Impl *>(this)->nativeRx_().input = nullptr; }
};

struct TCP {
  enum { Encrypted = false };
  using Connected = Ztcp::Connected;
  using ClientParams = Ztcp::ClientParams;
  using ServerParams = Ztcp::ServerParams;
  template <typename App> using Client = Ztcp::Client<App>;
  template <typename App> using Server = Ztcp::Server<App>;
  template <typename App, typename Impl, typename Rx, typename Tx>
  using CliLink = Ztcp::CliLink<App, Impl, Rx, Tx>;
  template <typename App, typename Impl, typename Rx, typename Tx>
  using SrvLink = Ztcp::SrvLink<App, Impl, Rx, Tx>;
  using RxStream = Ztcp::RxStream;
  template <unsigned Size = ZiIOBuf_DefltSize,
    unsigned Max = ZiIOBuf_DefltMaxSize, ZuString Heap = "Zfix.TCP.Rx">
  using RxBufAlloc = Ztcp::RxBufAlloc<Size, Max, Heap>;
};

struct TLS {
  enum { Encrypted = true };
  using Connected = Ztls::Connected;
  using ClientParams = Ztls::ClientParams;
  using ServerParams = Ztls::ServerParams;
  template <typename App> using Client = Ztls::Client<App>;
  template <typename App> using Server = Ztls::Server<App>;
  template <typename App, typename Impl, typename Rx, typename Tx>
  using CliLink = Ztls::CliLink<App, Impl, Rx, Tx>;
  template <typename App, typename Impl, typename Rx, typename Tx>
  using SrvLink = Ztls::SrvLink<App, Impl, Rx, Tx>;
  using RxStream = Ztls::RxStream;
  template <unsigned Size = ZiIOBuf_DefltSize,
    unsigned Max = ZiIOBuf_DefltMaxSize, ZuString Heap = "Zfix.TLS.Rx">
  using RxBufAlloc = Ztls::RxBufAlloc<Size, Max, Heap>;
};

// Immutable defaults shared by newly constructed links; posted quotas are hub
// aggregates with the owners documented by PostedHub.
struct HubLimits {
  LinkLimits	link;
  PostedLimits	submit;
  PostedLimits	admin;
  WorkLimits	work;
};

template <typename App>
class EndpointHub : public PostedHub<App>, public WorkHub<App> {
public:
  explicit EndpointHub(HubLimits limits = {}) :
    PostedHub<App>{limits.submit, limits.admin},
    WorkHub<App>{limits.work}, m_limits{limits.link} { }

  const LinkLimits &fixLinkLimits() const { return m_limits; }

private:
  const LinkLimits	m_limits;
};

// Stored inside the selected engine's Rx group, never in an additional base
// after that engine's Tx group. Both managed and raw engines use these credits.
template <typename Net> struct NativeRx {
  PostedIngress			submit;
  WorkBudget			work;
  typename Net::RxStream	*input = nullptr;

  explicit NativeRx(const LinkLimits &limits) :
    submit{{limits.postedSubmitBytes}}, work{limits.work} { }
};

// Stateless native hooks for managed and raw links.
template <typename Impl, typename Net> class LinkHooks_ {
public:
  TraceField fixTraceField(ZuCSpan type, unsigned tag) {
    return traceField<typename Impl::MessageCatalog>(type, tag);
  }

  bool fixTraceSecret(ZuCSpan, unsigned) { return false; }

  bool txPost_(uint64_t bytes) {
    auto &hub = impl()->app()->fixSubmitRx_();
    auto &submit = impl()->nativeRx_().submit;
    if (!submit.fits_(bytes)) { submit.refuse_(); hub.refuse_(); return false; }
    if (!hub.acquire_(bytes)) { submit.refuse_(); return false; }
    [[maybe_unused]] bool acquired = submit.acquire_(bytes); ZmAssert(acquired);
    return true;
  }

  void txUnpost_(uint64_t bytes) {
    auto &hub = impl()->app()->fixSubmitRx_();
    impl()->nativeRx_().submit.release_(bytes); hub.release_(bytes);
  }

  PostedState txPosted_() {
    ZmAssert(impl()->app()->rxInvoked()); return impl()->nativeRx_().submit.state_();
  }

  WorkState workState_() {
    ZmAssert(impl()->app()->rxInvoked()); return impl()->nativeRx_().work.state_();
  }

  WorkBudget &workBudget_() {
    ZmAssert(impl()->app()->rxInvoked()); return impl()->nativeRx_().work;
  }

  template <typename Input> struct WorkReturn {
    LinkHooks_				*link;
    const Zfix::Work<Input, Impl>	*work;

    void operator ()() {
      auto bytes = work->bytes();
      delete work;
      link->workReturned_(bytes);
    }
  };

  template <typename Input> void workDone(const Zfix::Work<Input, Impl> *work) {
    impl()->app()->rxRun(WorkReturn<Input>{this, work});
  }

  template <typename Input>
  auto workRx_(const Input &input, uint64_t retained, uint64_t extra, Fmt::Error &error) {
    ZmAssert(impl()->app()->rxInvoked());
    using W = Zfix::Work<Input, Impl>;
    constexpr uint64_t Overhead = ZmHeapAllocSize<sizeof(W)>{} +
      ZmHeapAllocSize<sizeof(WorkReturn<Input>) + sizeof(ZmRingFn<ZmRingFn_Defaults>)>{};
    auto &hub = impl()->app()->fixWorkRx_();
    auto &work = impl()->nativeRx_().work;
    if (!retained || Overhead > UINT64_MAX - retained ||
	extra > UINT64_MAX - (retained + Overhead)) {
      work.refuse_(); hub.refuse_(); error = {Fmt::ErrorCode::Size}; return typename W::Ref{};
    }
    uint64_t bytes = retained + Overhead + extra;
    if (!work.fits_(bytes)) {
      work.refuse_(); hub.refuse_(); error = {Fmt::ErrorCode::Size}; return typename W::Ref{};
    }
    if (!hub.acquire_(bytes)) {
      work.refuse_(); error = {Fmt::ErrorCode::Size}; return typename W::Ref{};
    }
    [[maybe_unused]] bool acquired = work.acquire_(bytes); ZmAssert(acquired);
    error = {}; return W::alloc_(impl(), input, bytes);
  }

  ZmRef<ZiRxQueue::Node> fixAllocRx() {
    return new typename Impl::RxBufAlloc{impl()};
  }

  WireGeometry fixGeometryTx() {
    ZmAssert(impl()->app()->txInvoked());
    WireGeometry g;
    if constexpr (Net::Encrypted) {
      g.size = impl()->txMaxSize();
      g.head = impl()->template txHeadRoom<false>();
      g.tail = impl()->template txTailRoom<false>();
      g.memory = ZmHeapAllocSize<sizeof(typename Impl::TxBufAlloc)>{} +
	(g.size > Impl::TxBufAlloc::Size ? Zfix::jumboBytes(g.size) : 0);
    }
    return g;
  }

  ZmRef<WireBuf> fixAllocTx(unsigned head) {
    ZmRef<WireBuf> b = new typename Impl::TxBufAlloc{impl()}; b->skip = head;
    if constexpr (Net::Encrypted) {
      unsigned size = impl()->txMaxSize();
      if (b->size < size) b->ensure(size - head);
    }
    return b;
  }

  bool fixSendTx(ZmRef<ZiIOBuf> b) { return impl()->sendRecord_(ZuMv(b)); }

  static SessionConfig role(SessionConfig config, bool initiator) {
    config.initiator = initiator; return config;
  }

private:
  auto impl() { return static_cast<Impl *>(this); }

  void workReturned_(uint64_t bytes) {
    impl()->nativeRx_().work.release_(bytes); impl()->app()->fixWorkRx_().release_(bytes);
    impl()->workDrainedRx_();
  }
};

} // Zfix

#endif /* ZfixNet_HH */
