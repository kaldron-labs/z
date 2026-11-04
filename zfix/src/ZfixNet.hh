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

#include <limits.h>

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

// Growing framing reserves the maximum BodyLength width. Its final subspan
// can leave this much unused prefix room even for a single TLS-sized frame.
inline constexpr unsigned wirePrefixGap() {
  unsigned digits = 0;
  for (unsigned n = UINT_MAX; n; n /= 10) ++digits;
  return digits - 1;
}

struct TCP {
  enum { Encrypted = false };
  using Connected = Ztcp::Connected;
  using ClientParams = Ztcp::ClientParams;
  using ServerParams = Ztcp::ServerParams;
  template <typename App> using Client = Ztcp::Client<App>;
  template <typename App> using Server = Ztcp::Server<App>;
  template <typename App, typename Impl, typename Tx>
  using CliLink = Ztcp::CliLink<App, Impl, Tx, WireOps>;
  template <typename App, typename Impl, typename Tx>
  using SrvLink = Ztcp::SrvLink<App, Impl, Tx, WireOps>;
  template <unsigned Size = ZiIOBuf_DefltSize,
    unsigned Max = ZiIOBuf_DefltMaxSize, ZuString Heap = "Zfix.TCP.Tx">
  using TxAllocator = Zi::TxTCPBufAllocator<WireBuf, Size, Max, ZuStringT<Heap>>;
  template <unsigned Size = ZiIOBuf_DefltSize,
    unsigned Max = ZiIOBuf_DefltMaxSize, ZuString Heap = "Zfix.TCP.Rx">
  using RxAllocator = Zi::RxTCPBufAllocator<ZiRxBuf, Size, Max, ZuStringT<Heap>>;
  using RxStream = Ztcp::RxStream;
};

struct TLS {
  enum { Encrypted = true };
  using Connected = Ztls::Connected;
  using ClientParams = Ztls::ClientParams;
  using ServerParams = Ztls::ServerParams;
  template <typename App> using Client = Ztls::Client<App>;
  template <typename App> using Server = Ztls::Server<App>;
  template <typename App, typename Impl, typename Tx>
  using CliLink = Ztls::CliLink<App, Impl, Tx>;
  template <typename App, typename Impl, typename Tx>
  using SrvLink = Ztls::SrvLink<App, Impl, Tx>;
  template <unsigned Size = Ztls_::TxRecordCapacity + wirePrefixGap(),
    unsigned Max = ZiIOBuf_DefltMaxSize, ZuString Heap = "Zfix.TLS.Tx">
  using TxAllocator = Zi::TxTLSBufAllocator<WireBuf, Size, Max, ZuStringT<Heap>>;
  template <unsigned Size = Ztls_::TxRecordCapacity,
    unsigned Max = ZiIOBuf_DefltMaxSize, ZuString Heap = "Zfix.TLS.Rx">
  using RxAllocator = Zi::RxTLSBufAllocator<ZiRxBuf, Size, Max, ZuStringT<Heap>>;
  using RxStream = Ztls::RxStream;
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
  enum { WireMode = Net::Encrypted ? Zfix::WireMode::Records : Zfix::WireMode::Pending };
  PostedIngress			submit;
  WorkBudget			work;
  typename Net::RxStream	*input = nullptr;

  explicit NativeRx(const LinkLimits &limits) :
    submit{{limits.postedSubmitBytes}}, work{limits.work} { }
};

// Stateless native hooks for managed and raw links.
template <typename Impl, typename Net> class LinkHooks_ {
public:
  TraceField fixTraceField(ZuCSpan type, int32_t tag) {
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
    auto allocator = impl()->app()->mx()->rxAllocator(false);
    return ZmRef<ZiRxQueue::Node>::acquire(static_cast<ZiRxQueue::Node *>(
      allocator->alloc(impl()).release()));
  }

  WireGeometry fixGeometryTx() {
    ZmAssert(impl()->app()->txInvoked());
    WireGeometry g;
    if constexpr (Net::Encrypted) {
      g.size = impl()->txMaxSize();
      g.head = impl()->template txHeadRoom<false>();
      g.tail = impl()->template txTailRoom<false>();
      auto probe = fixAllocTx(g.head);
      g.memory = probe->memory();
      // Reserve the larger ordinary fallback if the registered pool depletes.
      // This is admission geometry sampled once at connection initialization.
      if (probe->uring()) {
	auto fallback = g.memory - probe->size + jumboBytes(probe->size);
	if (fallback > g.memory) g.memory = fallback;
      }
    }
    return g;
  }

  ZmRef<WireBuf> fixAllocTx(unsigned head) {
    ZmRef<ZiIOBuf> buf;
    if constexpr (Net::Encrypted)
      buf = impl()->template txAllocBuf<false>(head);
    else {
      buf = impl()->template allocTxBuf_<false>();
      buf->skip = head;
    }
    return ZmRef<WireBuf>::acquire(static_cast<WireBuf *>(ZuMv(buf).release()));
  }

  template <bool AppThread = true> TxFrameBuf fixFrameBuf() {
    ZmAssert(AppThread != impl()->app()->txInvoked());
    unsigned head = 0, tail = 0;
    ZmRef<ZiIOBuf> buf;
    if constexpr (Net::Encrypted) {
      head = impl()->template txHeadRoom<AppThread>();
      tail = impl()->template txTailRoom<AppThread>();
      buf = impl()->template txAllocBuf<AppThread>(head);
    } else {
      buf = impl()->template allocTxBuf_<AppThread>();
    }
    return {ZuMv(buf), tail, true};
  }

  bool fixSendTx(ZmRef<ZiIOBuf> b) { return impl()->sendRecord_(ZuMv(b)); }
  bool fixSendRangeTx(Zi::RxRange source, ZmRef<WireBuf> wire) {
    return impl()->sendRecord_(ZuMv(source), ZuMv(wire));
  }


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
