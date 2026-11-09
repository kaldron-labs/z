//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Owned receive continuations and retained HTTP body production

#ifndef ZgrpcIO_HH
#define ZgrpcIO_HH

#ifndef ZgrpcLib_HH
#include <zlib/ZgrpcLib.hh>
#endif

#include <zlib/ZmFn.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZiRx.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZgrpcCall.hh>
#include <zlib/ZgrpcFrame.hh>
#include <zlib/ZgrpcMeta.hh>
#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpServer.hh>

namespace Zgrpc {

using Selection = Zhttp::Profiles<Zhttp::H2TLS, Zhttp::H3QUIC>;

// Instantiated matchers contain only the selected method's names/defaults.
// Catalog-wide adapters use their merged declaration, with active values
// supplied by the selected builder rather than every method's default.
template <typename H>
struct WireCatalog {
  using List = H;
  static int nameMatch(ZuBSpan key) {
    return Zhttp::Fields::nameMatch<WireCatalog>(key);
  }
  static int valueMatch(unsigned key, ZuBSpan value) {
    return Zhttp::Fields::valueMatch<WireCatalog>(key, value);
  }
  static int fieldMatch(ZuBSpan key) {
    return Zhttp::Fields::fieldMatch<WireCatalog>(key);
  }
};

using RxChunks = ZmList<ZmRef<ZiIOBuf>, ZmListHeapID<"Zgrpc.RxChunks">>;

// All members are Rx-owned. The parser and each scheduled continuation retain
// this object independently, so HTTP reset cannot erase deferred input or its
// completion marker. Callback owners must break any cycle at terminal delivery.
class ZgrpcAPI RxInput : public ZmObject, public ZmNHeap<RxInput> {
friend inline ZuStringT<"Zgrpc.RxInput"> ZmNHeapID(RxInput *);
public:
  using FrameFn = ZmFn<bool(Frame), ZmFnHeapID<"Zgrpc.RxFrame">>;
  using AcceptFn = ZmFn<void(Zhttp::Channel *, RxInput &), ZmFnHeapID<"Zgrpc.RxAccept">>;
  using EndFn = ZmFn<void(RxInput &, bool), ZmFnHeapID<"Zgrpc.RxEnd">>;

  RxInput(bool response, bool streamed, Limits,
    unsigned headerMax = Zhttp::DefltMaxHeaderSection);
  void callbacks(FrameFn frame, AcceptFn accept, EndFn end) {
    m_onFrame = ZuMv(frame); m_onAccept = ZuMv(accept); m_onEnd = ZuMv(end);
  }
  void route(CallRoute route) { m_route = route; }
  void retainedMax(uint64_t max) { m_retainedMax = max; }
  MetaRx &meta() { return m_meta; }
  const MetaRx &meta() const { return m_meta; }
  uint64_t messages() const { return m_direction.count(); }
  uint64_t retainedBytes() const { return m_retainedBytes; }
  Code::T code() const { return m_code; }
  bool httpFailed() const { return m_httpFailed; }
  bool finished() const { return m_finished; }
  bool failed() const { return m_failed; }

  bool accept(Zhttp::Channel *);
  bool enqueue(ZmRef<ZiIOBuf>);
  void complete(Zhttp::Channel *, bool);
  void fail(Code::T);

  template <typename Rx> bool body(Rx &rx) {
    if (m_failed || m_peerEnd || m_finished) return false;
    // Transfer each detached buffer; scheduled parsing owns its cursor.
    while (!rx.empty()) {
      ZmRef<ZiIOBuf> buf;
      auto n = rx.extract([](ZuSpan<uint8_t> span) -> int64_t {
	return span.length();
      }, []() { return ZmRef<typename Rx::Queue::Node>{}; }, buf);
      if (n <= 0 || !enqueue(ZuMv(buf))) {
	fail(Code::RESOURCE_EXHAUSTED);
	return false;
      }
    }
    return !m_failed;
  }

private:
  void bind_(Zhttp::Channel *);
  void kick_();
  void pump_();
  void finish_();

  const Limits m_limits;
  const bool m_response;
  CallRoute m_route;
  Zhttp::ChannelRef m_channel;
  MetaRx m_meta;
  FrameRx m_frames;
  Direction m_direction;
  RxChunks m_chunks;
  FrameFn m_onFrame;
  AcceptFn m_onAccept;
  EndFn m_onEnd;
  uint64_t m_retainedMax;
  uint64_t m_retainedBytes = 0;
  Code::T m_code = Code::OK;
  bool m_scheduled = false;
  bool m_accepted = false;
  bool m_peerEnd = false;
  bool m_httpOK = false;
  bool m_httpFailed = false;
  bool m_failed = false;
  bool m_finished = false;
};

// Final owned protobuf/envelope buffers. Queue accounting ends at HTTP handoff;
// the destination writer decides whether its concrete storage can be adopted.
class ZgrpcAPI TxFrames {
public:
  bool push(ZmRef<ZiIOBuf>, const Limits &);
  ZmRef<ZiIOBuf> shift();
  unsigned count() const { return m_frames.count_(); }
  uint64_t bytes() const { return m_bytes; }
  void clear() { m_frames.clean(); m_bytes = 0; }
private:
  ZmList<ZmRef<ZiIOBuf>, ZmListHeapID<"Zgrpc.Tx.Frames">> m_frames;
  uint64_t m_bytes = 0;
};

// Tx-only producer. No emitter or writer crosses to Rx; close() only releases
// retained producer state, without trying to produce from a close callback.
class ZgrpcAPI TxBody {
public:
  struct NoFail { void operator ()() const { } };
  explicit TxBody(bool streamed) : m_direction{streamed} { }
  bool attach(Zhttp::BodyEmitter);
  void close() { m_emit = {}; }
  bool ready() const { return m_emit.is<Zhttp::BodyEmitter>() && !m_failed; }
  bool failed() const { return m_failed; }
  Code::T code() const { return m_code; }
  uint64_t messages() const { return m_direction.count(); }
  bool end();
  bool abort(Code::T);

  template <typename Write, typename Fail = NoFail>
  bool write(Write &&write, Fail &&fail = {}) {
    if (!ready() || m_direction.ended()) return false;
    bool written = false;
    auto emit = m_emit.p<Zhttp::BodyEmitter>();
    bool admitted = emit([&write, &written, &fail](auto &out) {
      bool ok = ZuFwd<Write>(write)(out);
      bool flushed = out.flush();
      written = ok && flushed && !out.failed();
      if (!written) fail();
      return written ? Zhttp::WriteOutcome::Stream : Zhttp::WriteOutcome::Failed;
    });
    if (!admitted || !written) return failed_(Code::INTERNAL);
    return true;
  }
  bool message() { return !m_failed && m_direction.message(); }

  template <typename Fields> bool finish(Code::T code, Fields &fields) {
    if (!ready() || !m_direction.end(code == Code::OK))
      return abort(Code::INTERNAL);
    auto emit = m_emit.p<Zhttp::BodyEmitter>();
    if (!emit.trailers(fields))
      return failed_(Code::INTERNAL);
    m_emit = {};
    return true;
  }

private:
  bool failed_(Code::T code) { m_failed = true; m_code = code; return false; }

  Direction m_direction;
  ZuUnion<void, Zhttp::BodyEmitter> m_emit;
  Code::T m_code = Code::OK;
  bool m_failed = false;
};

// Structural terminal provider, usable for trailers and trailers-only replies.
// The caller's sensitive-name policy also applies to fixed descriptor defaults.
template <typename AppHeaders, typename Policy>
struct EndFields : Zhttp::Builder {
  using Headers = ZuTypeConcat<ZhttpHeaders(
    "grpc-status", "grpc-message", "grpc-status-details-bin"), AppHeaders>;
  using HdrCatalog = WireCatalog<Headers>;
  using Zhttp::Builder::header;

  const Status &status;
  const Metadata &metadata;
  const Policy &policy;
  EndFields(const Status &s, const Metadata &m, const Policy &p) :
    status{s}, metadata{m}, policy{p} { }
  bool neverIndex(ZuBSpan name) const { return policy.neverIndex(name); }

  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (ZuIsSame<Key, ZuStringT<"grpc-status">>{}) l(unsigned(status.code));
    else if constexpr (ZuIsSame<Key, ZuStringT<"grpc-message">>{}) {
      if (status.message.length()) l(TextValue{status.message});
    } else if constexpr (ZuIsSame<Key, ZuStringT<"grpc-status-details-bin">>{}) {
      if (status.details.length()) l(BinaryValue{status.details});
    }
  }
  template <typename L> void header(L &&l) const {
    MetaFields<> fields{metadata};
    fields.header(ZuFwd<L>(l));
  }
};

} // Zgrpc

#endif /* ZgrpcIO_HH */
