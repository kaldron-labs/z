//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Externally managed FIX frames over shared TCP/TLS transport

#ifndef ZfixRaw_HH
#define ZfixRaw_HH

#ifndef ZfixLib_HH
#include <zlib/ZiTx.hh>
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/ZmFn.hh>
#include <zlib/ZmPolymorph.hh>
#include <zlib/ZfixContext.hh>
#include <zlib/ZfixIOBuf.hh>
#include <zlib/ZfixLink.hh>

namespace Zfix {

struct RawInput : public ZmNHeap<RawInput>, public ZmObject {
friend inline ZuStringT<"Zfix.RawInput"> ZmNHeapID(RawInput *);
  ZmRef<ZiIOBuf>	buffer;
  uint64_t		charge = 0;
  Fmt::FrameView	frame;
  uint64_t		connection = 0;
  ZuTime		received;
};

// Routing configuration and fixed transmission metadata are supplied by the
// external manager. Only retained complete-frame handles cross
// to Tx. Neither the engine nor this envelope assigns sequence/epoch values.
// Each transmission attempt uses its own buffer; retain bodies for reconstruction.
class ZfixAPI RawOutput : public ZmNHeap<RawOutput>, public ZmObject {
friend inline ZuStringT<"Zfix.RawOutput"> ZmNHeapID(RawOutput *);
public:
  using Ref = ZmRef<const RawOutput>;

  template <typename Signer = NoSign>
  static Ref encode(
      ZmRef<ZiIOBuf> buf, TxBody::Ref body, const TxConfig &config, TxContext context,
      Fmt::Error &error, Fmt::Limits limits = {}, const Signer &signer = {}) {
    if (!body) { error = {Fmt::ErrorCode::Required, 35}; return {}; }
    auto frame = txFrame(ZuMv(buf), *body, config, context, error, limits, signer);
    if (!frame) return {};
    return new RawOutput{ZuMv(body), context, ZuMv(frame),
      (context.possDup || context.replay) ? context.origSendingTime : context.sendingTime};
  }

  // Also admit standalone framing output without imposing managed session
  // policy. The caller owns the external metadata and native body type.
  static Ref fromFrame(TxBody::Ref, TxContext, ZmRef<ZiIOBuf>, Fmt::Error &,
    Fmt::Limits = {}, ZuDateTime original = {});

  const TxBody::Ref &body() const { return m_body; }
  TxContext context() const { return m_context; }
  const ZmRef<ZiIOBuf> &frame() const { return m_frame; }
  ZuDateTime originalTime() const { return m_original; }

private:
  RawOutput(TxBody::Ref b, TxContext c, ZmRef<ZiIOBuf> f, ZuDateTime original) :
    m_body{ZuMv(b)}, m_context{c}, m_frame{ZuMv(f)}, m_original{original} { }

  const TxBody::Ref	m_body;
  const TxContext	m_context;
  const ZmRef<ZiIOBuf>	m_frame;
  const ZuDateTime	m_original;
};

namespace RawSend {

  enum { Accepted, Held, Invalid, Overload };
}

using RawStopFn = ZmFn<void()>;

// Borrowed by one stop hook. Retain defer()'s one-shot callback, never this
// stack request. Only an asynchronous stop allocates a continuation handle.
class RawStopRequest {
public:
  using DeferFn = RawStopFn (*)(void *);

  RawStopRequest(void *owner, DeferFn defer) : m_owner{owner}, m_defer{defer} { }

  RawStopFn defer() {
    if (!m_reply) m_reply = m_defer(m_owner);
    return m_reply;
  }

  bool deferred() const { return bool(m_reply); }

private:
  void *const	m_owner;
  const DeferFn	m_defer;
  RawStopFn	m_reply;
};

// A raw link runs framing/output only. It has no LinkState, sequencer, FIX timer,
// control dispatch, dictionary choice, admission/authentication or recovery.
// The external manager owns all of those policies and supplies complete output.
// Input false pauses on that exact retained frame until resumeRaw(connection).
// Each connection's stop hooks may defer independently. Their callbacks settle
// aggregate owner obligations; they cannot change the successor's state. Final
// stopped is delivered only without a current association, after all owners drain.
template <typename Impl, typename NativeRx = NoLinkRx> class Raw {
  // The immutable configuration snapshot owns its strings on the destination
  // side. Only this named heap handle and fixed metadata cross the shard post.
  struct Encode : ZmNHeap<Encode>, ZmObject {
  friend ZuStringT<"Zfix.RawEncode"> ZmNHeapID(Encode *);
    const TxBody::Ref body;
    const TxConfig config;
    const TxContext context;

    Encode(TxBody::Ref b, const TxConfig &c, TxContext x) :
      body{ZuMv(b)}, config{c}, context{x} { }
    uint64_t memory() const {
      const auto &ids = config.identity;
      return ZmHeapAllocSize<sizeof(Encode)>{} + body->memory() +
	ids.local.size() + ids.peer.size() + ids.localSub.size() + ids.peerSub.size() +
	ids.localLocation.size() + ids.peerLocation.size() + config.beginString.size() +
	config.appVersion.size() + config.encoding.size();
    }
  };

  struct StopReply : ZmNHeap<StopReply>, ZmPolymorph {
  friend inline ZuStringT<"Zfix.RawStopReply"> ZmNHeapID(StopReply *);
    const ZmRef<Impl>	owner;
    const bool		tx;
    bool		done = false; // owned by the callback's named shard

    StopReply(Impl *p, bool t) : owner{p}, tx{t} { }

    void complete() {
      auto finish = [reply = ZmRef(this)] {
	if (reply->done) return;
	reply->done = true;
	auto link = static_cast<Raw *>(reply->owner.ptr());
	if (reply->tx) {
	  ZmAssert(link->m_tx.stops); --link->m_tx.stops;
	  link->m_tx.checkStop();
	} else {
	  ZmAssert(link->m_stops); --link->m_stops;
	  link->finishStop_();
	}
      };
      if (tx) owner->app()->txRun(ZuMv(finish));
      else owner->app()->rxRun(ZuMv(finish));
    }
  };

  struct TxSide : public WireTx<TxSide> {
    using Wire = WireTx<TxSide>;
    Raw		*link;
    uint64_t	connection = 0;
    uint64_t	completions = 0;
    uint64_t	stops = 0;
    uint64_t	faults = 0, outputRefused = 0;
    bool	live = false, stopping = false, posted = false;

    TxSide(Raw *l, WireLimits limits) : Wire{limits}, link{l} { }

    void bind(uint64_t c) {
      live = false; Wire::close_(connection);
      connection = c; stopping = posted = false;
    }

    void open(uint64_t c) {
      if (c != connection || stopping) return;
      live = Wire::open_(c, link->impl()->fixGeometryTx());
      if (!live) {
	++faults;
	link->impl()->fixRawFaultTx(c, {Fmt::ErrorCode::Output});
	link->app()->rxRun([link = link, c] { link->closeRx_(c); }); return;
      }
      link->app()->rxRun([link = link, c] { link->openedRx_(c); });
      link->impl()->fixRawWritableTx(c);
    }

    unsigned send(const RawOutput::Ref &out) {
      if (!out) return RawSend::Invalid;
      if (!live || out->context().connection != connection) return RawSend::Held;
      if (out->frame()->length > link->outputLimits_().frameBytes ||
	  !Wire::admitRaw_(out->body(), out->context(), out->frame())) {
	++outputRefused; return RawSend::Overload;
      }
      ++completions;
      link->impl()->fixRawPublishedTx(*out);
      Wire::flush_(); return RawSend::Accepted;
    }

    void frameTrace(ZuCSpan wire, TxContext c) { link->traceTx_(wire, c); }

    ZmRef<ZiIOBuf> recordAlloc(unsigned head) { return link->impl()->fixAllocTx(head); }

    bool recordSend(ZmRef<ZiIOBuf> b) { return link->impl()->fixSendTx(ZuMv(b)); }

    void outputDone(TxBody::Ref b, TxContext c, bool ok) {
      link->app()->txRun([this, b = ZuMv(b), c, ok]() mutable {
	ZmAssert(completions); --completions;
	link->impl()->fixRawCompleteTx(ZuMv(b), c, ok);
	if (ok && live && c.connection == connection)
	  link->impl()->fixRawWritableTx(connection);
	if (!ok && live && c.connection == connection) {
	  ++faults;
	  live = false;
	  link->impl()->fixRawFaultTx(connection, {Fmt::ErrorCode::Output});
	  link->app()->rxRun([link = link, c] { link->closeRx_(c.connection); });
	}
	checkStop();
      });
    }

    void stop(uint64_t c) {
      if (c != connection) return;
      live = false; stopping = true; posted = false; Wire::close_(c);
      ++stops;
      RawStopRequest request{link, deferTx_};
      link->impl()->fixRawStopTx(c, request);
      if (!request.deferred()) --stops;
      checkStop();
    }

    void checkStop() {
      if (!stopping || stops || completions || posted) return;
      posted = true;
      link->app()->txRun([link = link, c = connection] {
	link->app()->rxRun([link, c] { link->stoppedTx_(c); });
      });
    }
  };

public:
  NativeRx &nativeRx_() { ZmAssert(app()->rxInvoked()); return m_native; }

  bool connectAllowed_() const { return true; }

  explicit Raw(SessionConfig config, LinkLimits limits = {}, uint64_t = 0) :
    m_outputLimits{limits.tx.codec},
    m_bufferBytes{limits.rx.bufferBytes}, m_native{limits}, m_framer{limits.rx.codec},
    m_traceRx{limits.traceRx}, m_tx{this, limits.wire},
    m_traceTx{limits.traceTx} { ZmAssert(config.mode == Transport::Raw); }

  Raw(const Raw &) = delete;
  Raw &operator =(const Raw &) = delete;

  bool transportBind_(bool) {
    ZmAssert(app()->rxInvoked());
    if (m_connection == UINT64_MAX) return false;
    uint64_t old = m_connection;
    bool retire = m_live;
    m_live = true; m_ready = m_txDone = false;
    m_pending = nullptr; m_framer.reset_();
    ++m_connection;
    if (retire) retireRx_(old);
    app()->txRun([this, c = m_connection] { m_tx.bind(c); });
    return true;
  }

  void transportUp_() {
    ZmAssert(app()->rxInvoked());
    if (!m_live) return;
    app()->txRun([this, c = m_connection] { m_tx.open(c); });
  }

  void transportDown_(bool peer) {
    ZmAssert(app()->rxInvoked());
    if (!m_live && m_closeRef) return;
    m_live = m_ready = false; m_peer = peer;
    m_txDone = false;
    m_pending = nullptr; m_framer.reset_();
    retireRx_(m_connection);
  }

  void transportSent(ZmRef<ZiIOBuf> buf, bool ok) {
    app()->txRun([this, buf = ZuMv(buf), ok] {
      m_tx.record_(buf.ptr(), ok);
    });
  }

  template <typename Stream> int process_(Stream &stream) {
    ZmAssert(app()->rxInvoked());
    if (!m_live) return -1;
    if (!m_ready || m_pending) return 0;
    ZmRef<ZiIOBuf> bytes; Fmt::FrameView frame;
    int n = m_framer.next_(stream, [this] { return impl()->fixAllocRx(); }, bytes, &frame);
    if (n < 0) {
      ++m_rxStats.framingErrors;
      faultRx_(m_framer.error_()); return -1;
    }
    if (!n) return 0;
    ++m_rxStats.frames; m_rxStats.bytes += frame.wire.length();
    uint64_t cap = m_bufferBytes;
    if (!cap) {
      cap = m_framer.limits().frameBytes;
      if (cap < Impl::RxBufAlloc::Size) cap = Impl::RxBufAlloc::Size;
    }
    if (bytes->size > cap) { faultRx_({Fmt::ErrorCode::Size}); return -1; }
    m_pending = new RawInput;
    m_pending->charge = ZmHeapAllocSize<sizeof(typename Impl::RxBufAlloc)>{} +
      ZmHeapAllocSize<sizeof(RawInput)>{} +
      (!bytes->builtin() ? Zfix::jumboBytes(bytes->size) : 0);
    m_pending->buffer = ZuMv(bytes); m_pending->frame = frame;
    m_pending->connection = m_connection; m_pending->received = Zm::now();
    if (m_traceRx.enabled_()) {
      auto entry = m_traceRx.capture_(frame, {0, m_connection, 0}, traceField_, impl());
      if (entry) impl()->fixTraceRx(ZuMv(entry));
    }
    receive_(); return m_pending ? 0 : 1;
  }

  TraceStats rxTraceStats_() const { ZmAssert(app()->rxInvoked()); return m_traceRx.stats_(); }
  TraceStats txTraceStats_() const { ZmAssert(app()->txInvoked()); return m_traceTx.stats_(); }

  enum { SignMsg = 0 };
  template <typename S>
  void signMsg(S &, const TxBody *, TxContext, ZuCSpan) const { }

  TraceField fixTraceField(ZuCSpan, unsigned) { return {}; }
  bool fixTraceSecret(ZuCSpan, unsigned) { return false; }
  void fixTraceRx(TraceRef) { }
  void fixTraceTx(TraceRef) { }

  RxStats rxStats_() const {
    ZmAssert(app()->rxInvoked()); RxStats s{m_rxStats};
    s.pendingBytes = m_pending ? m_pending->buffer->size : 0; return s;
  }

  TxStats txStats_() const {
    ZmAssert(app()->txInvoked()); TxStats s; s.wire = m_tx.stats_();
    s.faults = m_tx.faults; s.outputRefused = m_tx.outputRefused;
    s.inFlight = m_tx.completions; return s;
  }

  uint64_t rawConnection_() const { ZmAssert(app()->rxInvoked()); return m_connection; }
  uint64_t rawOutputBytes_() const { ZmAssert(app()->txInvoked()); return m_tx.bytes_(); }
  uint64_t rawInFlight_() const { ZmAssert(app()->txInvoked()); return m_tx.completions; }

  unsigned sendRaw_(const RawOutput::Ref &out) {
    ZmAssert(app()->txInvoked()); return m_tx.send(out);
  }

  [[nodiscard]] bool sendRaw(const RawOutput::Ref &out) {
    if (!out) return false;
    if (app()->txInvoked()) {
      unsigned result = sendRaw_(out); impl()->fixRawSubmittedTx(out, result); return true;
    }
    if (!app()->rxInvoked() || !m_live) return false;
    uint64_t bytes = sizeof(RawOutput) + out->body()->memory() +
      sizeof(ZiTxBuf) + uint64_t(out->frame()->size);
    if (!impl()->txPost_(bytes)) return false;
    app()->txRun([this, out, bytes] {
      unsigned result = sendRaw_(out); impl()->fixRawSubmittedTx(out, result);
      app()->rxRun([this, bytes] { impl()->txUnpost_(bytes); finishStop_(); });
    });
    return true;
  }

  // External managers explicitly supply routing configuration and transmission metadata.
  template <typename Entry>
  [[nodiscard]] bool send(const typename Entry::Object &object, uint64_t id,
      const TxConfig &config, TxContext context, bool admin, Fmt::Error &error) {
    using Dictionary = typename Impl::MessageCatalog;
    ZuAssert((ZuTypeIn<Entry, typename Dictionary::Messages>{}));
    if (!app()->rxInvoked() && !app()->txInvoked()) {
      error = {Fmt::ErrorCode::Output}; return false;
    }
    auto body = TxBody::encodeRaw<Entry>(object, id, admin, error, m_outputLimits);
    if (!body) return false;
    if constexpr (Impl::SignMsg) {
      if (!app()->txInvoked()) {
	if (!m_live) { error = {Fmt::ErrorCode::Output}; return false; }
	ZmRef<Encode> pending = new Encode{ZuMv(body), config, context};
	uint64_t bytes = pending->memory();
	if (!impl()->txPost_(bytes)) { error = {Fmt::ErrorCode::Output}; return false; }
	app()->txRun([this, pending = ZuMv(pending), bytes] {
	  Fmt::Error error;
	  auto output = RawOutput::encode(impl()->fixAllocTx(0), pending->body, pending->config,
	    pending->context, error, m_outputLimits, *impl());
	  if (output) {
	    unsigned result = sendRaw_(output); impl()->fixRawSubmittedTx(output, result);
	  } else impl()->fixRawFaultTx(pending->context.connection, error);
	  app()->rxRun([this, bytes] { impl()->txUnpost_(bytes); finishStop_(); });
	});
	return true;
      }
    }
    auto output = RawOutput::encode(impl()->fixAllocTx(0), ZuMv(body), config, context,
      error, m_outputLimits, *impl());
    if (!output) return false;
    if (!sendRaw(output)) { error = {Fmt::ErrorCode::Output}; return false; }
    return true;
  }

  template <typename Request, typename Response>
  [[nodiscard]] bool reply(const RawInput &input, const typename Response::Object &object,
      uint64_t id, const TxConfig &config, TxContext context, bool admin, Fmt::Error &error) {
    using Dictionary = typename Impl::MessageCatalog;
    ZuAssert((ZuTypeIn<Request, typename Dictionary::Messages>{}));
    ZuAssert((ZuTypeIn<Response, typename Dictionary::Messages>{}));
    ZuAssert((ZuTypeIn<Response, typename Request::Responses>{}));
    if (!app()->rxInvoked()) { error = {Fmt::ErrorCode::Output}; return false; }
    if (!input.buffer || input.frame.msgType != ZuCSpan{Request::MsgType}) {
      error = {Fmt::ErrorCode::Unresolved, 35}; return false;
    }
    error = Context_::Reply<Impl, Request, Response, RawInput>::call(impl(), input);
    return !error && send<Response>(object, id, config, context, admin, error);
  }

  auto work(const ZmRef<const RawInput> &input, Fmt::Error &error, uint64_t extra = 0) {
    using W = Zfix::Work<ZmRef<const RawInput>, Impl>;
    if (!app()->rxInvoked() || !m_live) {
      error = {Fmt::ErrorCode::Output}; return typename W::Ref{};
    }
    if (!input || !input->buffer) {
      error = {Fmt::ErrorCode::Required}; return typename W::Ref{};
    }
    return impl()->workRx_(input, input->charge, extra, error);
  }

  void workDrainedRx_() { ZmAssert(app()->rxInvoked()); finishStop_(); }

  void resumeRaw_(uint64_t connection) {
    ZmAssert(app()->rxInvoked());
    if (!m_live || !m_ready || connection != m_connection) return;
    receive_(); if (!m_pending) impl()->resumeRx_();
  }

  void resumeRaw(uint64_t connection) {
    app()->rxRun([this, connection] { resumeRaw_(connection); });
  }

  void fixRawUpRx(uint64_t) { }
  bool fixRawRx(ZmRef<RawInput>, Fmt::Limits) { return false; }
  void fixRawPublishedTx(const RawOutput &) { }
  void fixRawSubmittedTx(RawOutput::Ref, unsigned) { }
  void fixRawCompleteTx(TxBody::Ref, TxContext, bool) { }
  void fixRawWritableTx(uint64_t) { }
  void fixRawFaultRx(uint64_t, Fmt::Error) { }
  void fixRawFaultTx(uint64_t, Fmt::Error) { }
  void fixRawStopRx(uint64_t, RawStopRequest &) { }
  void fixRawStopTx(uint64_t, RawStopRequest &) { }
  void fixRawStoppedRx(uint64_t, bool) { }

private:
  static RawStopFn deferRx_(void *owner) { return defer_(owner, false); }
  static RawStopFn deferTx_(void *owner) { return defer_(owner, true); }

  static RawStopFn defer_(void *owner, bool tx) {
    auto link = static_cast<Raw *>(owner);
    ZmRef<StopReply> reply = new StopReply{link->impl(), tx};
    return RawStopFn{ZuMv(reply), [](StopReply *p) { p->complete(); }};
  }

  auto impl() { return static_cast<Impl *>(this); }

  auto app() { return impl()->app(); }
  auto app() const { return static_cast<const Impl *>(this)->app(); }
  const Fmt::Limits &outputLimits_() const { return m_outputLimits; }

  void openedRx_(uint64_t connection) {
    if (!m_live || connection != m_connection) return;
    m_ready = true;
    impl()->fixRawUpRx(connection);
    if (m_live) impl()->resumeRx_();
  }

  void stoppedTx_(uint64_t c) {
    if (m_live || c != m_connection) return;
    m_txDone = true; finishStop_();
  }

  void retireRx_(uint64_t c) {
    if (!m_closeRef) m_closeRef = impl();
    ++m_stops;
    RawStopRequest request{this, deferRx_};
    impl()->fixRawStopRx(c, request);
    if (!request.deferred()) --m_stops;
    app()->txRun([this, c] { m_tx.stop(c); });
    finishStop_();
  }

  static TraceField traceField_(void *owner, ZuCSpan type, int32_t tag) {
    auto p = static_cast<Impl *>(owner); auto f = p->fixTraceField(type, tag);
    f.secret |= p->fixTraceSecret(type, tag); return f;
  }

  void traceTx_(ZuCSpan wire, TxContext c) {
    if (!m_traceTx.enabled_()) return;
    auto entry = m_traceTx.capture_(wire, c, traceField_, impl());
    if (entry) impl()->fixTraceTx(ZuMv(entry));
  }

  void receive_() {
    if (m_pending && impl()->fixRawRx(m_pending, m_framer.limits())) m_pending = nullptr;
  }

  void faultRx_(Fmt::Error error) {
    ++m_rxStats.faults; if (error.code == Fmt::ErrorCode::Size) ++m_rxStats.overloads;
    impl()->fixRawFaultRx(m_connection, error);
  }

  void closeRx_(uint64_t c) { if (m_live && c == m_connection) impl()->disconnect_(); }

  void finishStop_() {
    if (m_live || !m_closeRef || m_stops || !m_txDone || impl()->txPosted_().count ||
	impl()->workState_().count) return;
    auto retain = ZuMv(m_closeRef);
    impl()->fixRawStoppedRx(m_connection, m_peer);
  }

  const Fmt::Limits	m_outputLimits;
  const uint64_t	m_bufferBytes;

  // Native ledgers/input and framing form one Rx-owned group.
  alignas(Zm::CacheLineSize)
  RxCounters		m_rxStats;
  [[no_unique_address]]
  NativeRx		m_native;
  RxFramer		m_framer;
  WireTrace		m_traceRx;
  ZmRef<RawInput>	m_pending;
  ZmRef<Impl>		m_closeRef;
  uint64_t		m_connection = 0;
  uint64_t		m_stops = 0;
  bool			m_live = false, m_ready = false, m_txDone = false,
			m_peer = false;

  alignas(Zm::CacheLineSize)
  TxSide		m_tx;
  WireTrace		m_traceTx;
};

} // Zfix

#endif /* ZfixRaw_HH */
