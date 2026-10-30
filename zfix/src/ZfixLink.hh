//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Transport-independent managed-session owner

#ifndef ZfixLink_HH
#define ZfixLink_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/ZmFn.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmPolymorph.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmRingFn.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZiAssert.hh>
#include <zlib/ZfixContext.hh>
#include <zlib/ZfixIOBuf.hh>
#include <zlib/ZfixLinkTx.hh>
#include <zlib/ZfixDispatch.hh>
#include <zlib/ZfixRx.hh>
#include <zlib/ZfixRxFramer.hh>
#include <zlib/ZfixWire.hh>
#include <zlib/ZfixWork.hh>
#include <zlib/ZfixIngress.hh>
#include <zlib/ZfixTrace.hh>

namespace Zfix {

namespace Admission { enum { Refused, Accepted, Pending }; }

using AdmitFn = ZmFn<void(bool)>;

// Borrowed by the synchronous admission hook. Only defer() allocates; retain
// its one-shot callback, never this stack request. Complete it on any shard.
class AdmitRequest {
public:
  using DeferFn = AdmitFn (*)(void *, uint64_t, uint64_t);

  AdmitRequest(void *owner, uint64_t connection, uint64_t ticket, DeferFn defer) :
    m_owner{owner}, m_connection{connection}, m_ticket{ticket}, m_defer{defer} { }

  AdmitFn defer() {
    if (!m_reply) m_reply = m_defer(m_owner, m_connection, m_ticket);
    return m_reply;
  }

  bool deferred() const { return bool(m_reply); }

private:
  void *const	m_owner;
  const uint64_t	m_connection, m_ticket;
  const DeferFn	m_defer;
  AdmitFn		m_reply;
};

namespace ResetOp { enum { Drain, OnOpen, Prepare, Start, Disconnect }; }

struct ResetRequest {
  uint64_t	connection = 0;
  uint64_t	epoch = 0;
  uint64_t	nextEpoch = 0;
  unsigned	op = ResetOp::Drain;
  unsigned	path = ResetPath::Live;
  bool		initiator = false;
  bool		enabled = true;
};

struct LinkLimits {
  RxLimits	rx;
  TxLimits	tx;
  ControlLimits	control;
  WireLimits	wire;
  TraceLimits	traceRx, traceTx;
  WorkLimits	work;
  uint64_t	postedSubmitBytes = PostedLimits{}.bytes;
  uint64_t	postedAdminBytes = PostedLimits{}.bytes;
  ZuTime	gapRetry{1};
};

// Standalone engines need no native transport input or application ledgers.
struct NoLinkRx {
  explicit NoLinkRx(const LinkLimits &) { }
};

// Impl is a reference-counted physical transport link. Its app() supplies the
// existing hub's isolated Rx/Tx dispatch and mx(). Transport hooks are:
// fixGeometryTx_(), fixAllocTx_(head), fixSendTx_(buffer), resumeRx_(), and
// disconnect_(). Configure the link's output allocator as WireBufAlloc so socket
// callbacks can pass their fixed token/part to transportSent().
//
// Public dispatchers post retained handles/fixed values. Trailing-underscore
// entry points and observers run on their named owner. A stopped observer is
// delivered only after physical I/O, timers, owner tasks, output completions and
// outstanding load/unload/history continuations have drained. The application
// must keep hubs running until that observer; pending hooks must complete or
// be cancelled by fixStopTx_. Populous tasks use raw dependent back-pointers.
// The single teardown reference retains the enclosing link while draining.
template <typename Impl, typename NativeRx = NoLinkRx> class Link {
public:
  using Msg = RxQueue::Node;
  NativeRx &nativeRx_() { ZmAssert(app()->rxInvoked()); return m_native; }

private:
  struct AdmitReply : ZmNHeap<AdmitReply>, ZmPolymorph {
  friend inline ZuStringT<"Zfix.AdmitReply"> ZmNHeapID(AdmitReply *);
    const ZmRef<Impl>	owner;
    const uint64_t	connection, ticket;
    bool			done = false; // Rx-owned one-shot disposition

    AdmitReply(Impl *p, uint64_t c, uint64_t t) : owner{p}, connection{c}, ticket{t} { }

    void complete(bool accepted) {
      owner->app()->rxRun([reply = ZmRef(this), accepted] {
	if (reply->done) return;
	reply->done = true;
	auto link = static_cast<Link *>(reply->owner.ptr());
	ZmAssert(link->m_admissions);
	--link->m_admissions;
	link->admitted_(reply->connection, reply->ticket, accepted);
      });
    }
  };

  struct RxSide : public Rx<RxSide> {
    Link	*link;

    RxSide(Link *l, SessionConfig c, RxLimits limits, uint64_t epoch) :
      Rx<RxSide>{ZuMv(c), limits, epoch}, link{l} { }

    ZuTime nowRx_() { return Zm::now(); }

    void eventRx_(SessionEvent e, ZmRef<Msg> msg) { link->event_(e, ZuMv(msg)); }

    bool workIdleRx_() const { return !link->impl()->workState_().count; }

    void stateChangeRx_(SessionEvent e) { link->impl()->fixStateRx_(e); }

    void sessionRx_(ZmRef<Msg> msg) { link->impl()->fixControlRx_(ZuMv(msg)); }

    void peerRejectRx_(PeerReject report) { link->peerReject_(ZuMv(report)); }

    void rejectRx_(ZmRef<Msg> msg, unsigned tag, int reason) {
      link->reject_(msg->data().header.seqNo, tag, reason);
    }

    void resendRx_(uint64_t begin, uint64_t end, ZmRef<Msg>) {
      link->resend_(begin, end);
    }

    Fmt::Error headerRx_(const Fmt::FrameView &f, Fmt::Header &h, Fmt::Limits l) {
      return link->impl()->fixHeaderRx_(f, h, l);
    }

    Fmt::Error businessRx_(Msg *msg, Fmt::Limits limits) {
      return link->impl()->fixBusinessRx_(msg, limits);
    }

    void rxPost_(uint64_t ticket) {
      link->app()->rxRun([this, ticket] { this->drain_(ticket); });
    }

    void gapArm_(uint64_t ticket) { link->armGap_(ticket); }
    void gapCancel_() { link->cancelGap_(); }

    void epochRx_(uint64_t c, uint64_t old, uint64_t epoch, RxProgress p) {
      // Every timer captures its epoch. An earlier old-epoch deadline cannot
      // stand in for the new one: its callback will correctly be ignored.
      link->cancelTimers_();
      link->impl()->fixEpochRx_(c, old, epoch, p);
    }
  };

  struct TxSide : public LinkTx<TxSide>, public WireTx<TxSide> {
    using Ctl = LinkTx<TxSide>;
    using Wire = WireTx<TxSide>;

    Link	*link;
    uint64_t	stopConnection = 0; // requested drain; Ctl may never have opened
    bool	stopping = false;
    bool	stopPosted = false;

    TxSide(Link *l, SessionConfig c, const LinkLimits &limits, uint64_t epoch) :
      Ctl{ZuMv(c), limits.tx, limits.control, epoch}, Wire{limits.wire}, link{l} { }

    void bind(SessionEvent e) {
      auto old = Ctl::connection_();
      idleCancelTx_();
      auto result = Ctl::bind_(e);
      Wire::close_(old);
      stopping = stopPosted = false;
      if (result == TxSelect::Invalid || result == TxSelect::Overload)
	faultTx_({e.epoch, e.connection, 0}, {Fmt::ErrorCode::Output});
    }

    void applyEvent(SessionEvent e, TxBody::Ref body) {
      if (e.connection < Ctl::connection_()) return;

      if (e.connection > Wire::connection_() &&
	  e.state != State::Closing && e.state != State::Disconnected) {
	if (!Wire::open_(e.connection, link->impl()->fixGeometryTx_())) {
	  faultTx_({e.epoch, e.connection, 0}, {Fmt::ErrorCode::Output}); return;
	}
	stopping = stopPosted = false;
      }
      if (body && body->msgType() == "A") {
	body = link->impl()->fixLogonTx_(ZuMv(body));
	if (!body || !body->administrative() || body->msgType() != "A") {
	  faultTx_({e.epoch, e.connection, 0}, {Fmt::ErrorCode::Output}); return;
	}
      }
      auto result = Ctl::apply_(e, ZuMv(body));
      if (result == TxSelect::Invalid || result == TxSelect::Overload)
	faultTx_({e.epoch, e.connection, 0}, {Fmt::ErrorCode::Output});
      checkStop();
    }

    void txSchedule_(unsigned kind, uint64_t ticket) {
      link->app()->txRun([this, kind, ticket] { Ctl::run_(kind, ticket); checkStop(); });
    }

    unsigned associateTx_(TxBody::Ref body, TxContext c, uint64_t ticket) {
      return link->impl()->fixAssociateTx_(ZuMv(body), c, ticket);
    }

    bool releaseTx_(TxBody::Ref body, TxContext c, uint64_t ticket) {
      return link->impl()->fixReleaseTx_(ZuMv(body), c, ticket);
    }

    TxReload retrieveTx_(TxContext context, uint64_t acknowledged, uint64_t ticket) {
      return link->impl()->fixRetrieveTx_(context, acknowledged, ticket);
    }

    void archiveTx_(uint64_t epoch, uint64_t begin, uint64_t end) {
      link->impl()->fixArchiveTx_(epoch, begin, end);
    }

    bool enqueueTx_(TxBody::Ref body) { return link->impl()->fixEnqueueTx_(ZuMv(body)); }

    TxBody::Ref dequeueTx_() { return link->impl()->fixDequeueTx_(); }

    void selectedTx_(TxBody::Ref b, TxContext c, unsigned result) {
      link->impl()->fixSelectedTx_(ZuMv(b), c, result);
    }

    void rejectedTx_(PeerReject r) { link->impl()->fixRejectedTx_(ZuMv(r)); }

    void sessionTx_(TxBody::Ref b, TxContext c) { link->impl()->fixControlTx_(ZuMv(b), c); }

    void acknowledgedTx_(TxContext c, uint64_t old, uint64_t next) {
      link->impl()->fixAckdTx_(c, old, next);
    }

    void sequenceLimitTx_(TxContext c, uint64_t max, unsigned headroom) {
      link->impl()->fixLimitTx_(c, max, headroom);
    }

    void epochTx_(TxContext c, uint64_t epoch, TxProgress p, uint64_t request) {
      link->impl()->fixEpochTx_(c, epoch, p, request);
    }

    ZuDateTime clockTx_() { return ZuDateTime{Zm::now()}; }
    ZuTime nowTx_() { return Zm::now(); }

    void idleArmTx_(uint64_t connection, uint64_t ticket, ZuTime at) {
      link->app()->mx()->del(&link->m_idleTimer);
      link->app()->mx()->add(&link->m_idleTimer, at, ZmScheduler::Update,
	[this, connection, ticket](auto &&arm) {
	  return arm([this, connection, ticket] {
	    if (!stopping) Ctl::idle_(connection, ticket);
	  });
	}, link->app()->txThread());
    }

    void idleCancelTx_() { link->app()->mx()->del(&link->m_idleTimer); }

    void faultTx_(TxContext c, Fmt::Error e) {
      if (!c.connection) c = {Ctl::epoch_(), Ctl::connection_(), 0};
      link->impl()->fixFaultTx_(c, e);
      link->app()->rxRun([link = link, c] { link->outputFailed_(c); });
    }

    bool admitFrameTx_(TxBody::Ref b, TxContext c, ZmRef<ZiIOBuf> f, ZuDateTime) {
      return Wire::admit_(ZuMv(b), c, ZuMv(f));
    }

    void notifyTx_(TxNotice notice) {
      link->app()->rxRun([link = link, notice] { link->notice_(notice); });
    }

    void frameTrace_(ZuCSpan wire, TxContext c) { link->traceTx_(wire, c); }

    void flushTx_() { Wire::flush_(); }

    void publishedTx_(TxBody::Ref b, TxContext c, ZuDateTime original) {
      link->impl()->fixPublishedTx_(ZuMv(b), c, original);
    }

    void completeTx_(TxBody::Ref b, TxContext c, bool ok) {
      link->impl()->fixCompleteTx_(ZuMv(b), c, ok);
    }

    ZmRef<WireBuf> recordAlloc_(unsigned head) { return link->impl()->fixAllocTx_(head); }

    bool recordSend_(ZmRef<ZiIOBuf> b) { return link->impl()->fixSendTx_(ZuMv(b)); }

    void outputDone_(TxBody::Ref b, TxContext c, bool ok) {
      link->app()->txRun([this, b = ZuMv(b), c, ok]() mutable {
	Ctl::sent_(ZuMv(b), c, ok);
	if (ok) Ctl::writable_(c.connection);
	checkStop();
      });
    }

    void stop(uint64_t connection) {
      if (connection < Ctl::connection_()) return;
      stopConnection = connection;
      stopping = true; stopPosted = false; idleCancelTx_();
      Ctl::close_(Ctl::connection_());
      Wire::close_(Ctl::connection_()); Wire::close_(connection);
      link->impl()->fixStopTx_(connection); checkStop();
    }

    void checkStop() {
      if (!stopping || stopPosted || Ctl::inFlight_() || Wire::count_() ||
	  Ctl::loading_() || Ctl::waiting_()) return;
      stopPosted = true;
      link->app()->txRun([link = link, connection = stopConnection] {
	link->app()->rxRun([link, connection] { link->stopped_(connection); });
      });
    }
  };

public:
  explicit Link(SessionConfig config, LinkLimits limits = {}, uint64_t epoch = 1) :
    m_gapRetry{limits.gapRetry}, m_codec{limits.tx.codec},
    m_rx{this, config, limits.rx, epoch}, m_native{limits}, m_framer{limits.rx.codec},
    m_traceRx{limits.traceRx}, m_admin{{limits.postedAdminBytes}},
    m_tx{this, ZuMv(config), limits, epoch}, m_traceTx{limits.traceTx} { }

  Link(const Link &) = delete;
  Link &operator =(const Link &) = delete;

  bool connectAllowed_() const { return true; }

  // The socket is current before TLS authentication. Revoke old protocol
  // authority now; authentication starts FIX on this same generation later.
  bool transportBind_(bool replacing) {
    ZmAssert(app()->rxInvoked());
    cancelTimers_();
    m_live = true; m_txStopped = m_adminFailed = false;
    m_admissionTicket = 0;
    m_pending = nullptr; m_framer.reset_();
    const auto &session = m_rx.session_();
    bool replace = replacing || session.state_() != State::Disconnected ||
      session.resetPreparing_();
    auto e = m_rx.bind_(replace);
    if (e.actions & SessionAction::Refuse) {
      impl()->fixEventRx_(e, {}); return false;
    }
    app()->txRun([this, e] { m_tx.bind(e); });
    return true;
  }

  // Called by the transport on Rx after TCP establishment/TLS authentication.
  void transportUp_() {
    ZmAssert(app()->rxInvoked());
    if (!m_live || m_rx.session_().state_() != State::Disconnected) return;
    m_txStopped = m_adminFailed = false;
    auto e = m_rx.start_(Zm::now());
    if (m_adminFailed) return;
    if (e.actions & SessionAction::Refuse) { impl()->disconnect_(); return; }
  }

  void transportDown_(bool peer) {
    ZmAssert(app()->rxInvoked());
    if (!m_live && m_closeRef) return;
    if (!m_closeRef) m_closeRef = impl();
    m_peerClosed = peer; m_live = false;
    cancelTimers_(); m_pending = nullptr; m_framer.reset_();
    auto connection = m_rx.session_().connection_();
    m_rx.lost_(connection); m_rx.closed_(connection);
    impl()->fixStopRx_(connection, m_admissionTicket);
    app()->txRun([this, connection] { m_tx.stop(connection); });
  }

  void transportSent(const WireBuf *buf, bool ok) {
    if (!buf->token) return;
    app()->txRun([this, token = buf->token, part = buf->part, ok] {
      m_tx.Wire::record_(token, part, ok); m_tx.checkStop();
    });
  }

  template <typename Stream> int process_(Stream &stream) {
    ZmAssert(app()->rxInvoked());
    if (!m_live || m_adminFailed) return -1;
    if (m_pending) return 0;
    ZmRef<ZiIOBuf> bytes; Fmt::FrameView frame;
    int n = m_framer.next_(stream, [this] { return impl()->fixAllocRx_(); }, bytes, &frame);
    if (n < 0) {
      m_rx.framingError_(); fault_(SessionError::Protocol, m_framer.error_().tag);
      return -1;
    }
    if (!n) return 0;
    m_pending = new Msg;
    m_pending->data().charge = rxMemory_(*bytes) + ZmHeapAllocSize<sizeof(Msg)>{};
    m_pending->data().nativeMemory = true;
    m_pending->data().buffer = ZuMv(bytes); m_pending->data().frame = frame;
    auto retained = m_traceRx.enabled_() ? m_pending : ZmRef<Msg>{};
    TxContext context{m_rx.session_().epoch_(), m_rx.session_().connection_(), 0};
    receive_();
    if (retained) {
      context.seqNo = retained->data().header.seqNo;
      auto entry = m_traceRx.capture_(retained->data().frame, context,
	traceField_, impl());
      if (entry) impl()->fixTraceRx_(ZuMv(entry));
    }
    return m_pending ? 0 : 1;
  }

  const LinkState &session_() const { ZmAssert(app()->rxInvoked()); return m_rx.session_(); }

  TraceStats rxTraceStats_() const { ZmAssert(app()->rxInvoked()); return m_traceRx.stats_(); }
  TraceStats txTraceStats_() const { ZmAssert(app()->txInvoked()); return m_traceTx.stats_(); }

  TraceField fixTraceField_(ZuCSpan, unsigned) { return {}; }
  bool fixTraceSecret_(ZuCSpan, unsigned) { return false; }

  void fixTraceRx_(TraceRef) { }
  void fixTraceTx_(TraceRef) { }

  RxStats rxStats_() const {
    ZmAssert(app()->rxInvoked()); auto s = m_rx.stats_();
    if (m_pending) s.pendingBytes += m_pending->data().buffer->size;
    return s;
  }

  PostedState adminPosted_() const {
    ZmAssert(app()->rxInvoked()); return m_admin.state_();
  }

  TxStats txStats_() const {
    ZmAssert(app()->txInvoked()); auto s = m_tx.Ctl::stats_();
    s.wire = m_tx.Wire::stats_(); return s;
  }

  TxProgress txProgress_() const { ZmAssert(app()->txInvoked()); return m_tx.Ctl::progress_(); }
  RxProgress rxProgress_() const { ZmAssert(app()->rxInvoked()); return m_rx.progress_(); }

  bool snapshotRx_(Checkpoint &s) const {
    ZmAssert(app()->rxInvoked()); return m_rx.snapshot_(s);
  }

  bool snapshotTx_(Checkpoint &s) const {
    ZmAssert(app()->txInvoked()); return m_tx.Ctl::snapshot_(s);
  }

  bool restoreRx_(const Checkpoint &s) {
    ZmAssert(app()->rxInvoked()); return m_rx.restore_(s);
  }

  bool restoreTx_(const Checkpoint &s) {
    ZmAssert(app()->txInvoked()); return m_tx.Ctl::restore_(s);
  }

  unsigned submit_(TxBody::Ref body) {
    ZmAssert(app()->txInvoked()); return m_tx.candidate_(ZuMv(body));
  }

  [[nodiscard]] bool submit(const TxBody::Ref &body) {
    if (!body) return false;
    if (app()->txInvoked()) {
      unsigned result = submit_(body); impl()->fixSubmittedTx_(body, result); return true;
    }
    if (!app()->rxInvoked() || !m_live) return false;
    uint64_t bytes = body->memory();
    if (!impl()->txPost_(bytes)) return false;
    app()->txRun([this, body, bytes] {
      unsigned result = submit_(body); impl()->fixSubmittedTx_(body, result);
      app()->rxRun([this, bytes] {
	impl()->txUnpost_(bytes); if (m_txStopped) stopped_(m_rx.session_().connection_());
      });
    });
    return true;
  }

  // Local admission only. The existing Tx hooks report selection, association,
  // publication and completion; business correlation stays application-owned.
  template <typename Entry>
  [[nodiscard]] bool send(const typename Entry::Object &object, uint64_t id,
      Fmt::Error &error) {
    using Dictionary = typename Impl::MessageCatalog;
    ZuAssert((ZuTypeIn<Entry, typename Dictionary::Messages>{}));
    if (!app()->rxInvoked() && !app()->txInvoked()) {
      error = {Fmt::ErrorCode::Output}; return false;
    }
    auto body = TxBody::encode<Entry>(object, id, error, m_codec);
    if (!body) return false;
    if (!submit(body)) { error = {Fmt::ErrorCode::Output}; return false; }
    return true;
  }

  // Rx routes retained-context policy; successful handoff is not business
  // completion. Caller supplies business keys and may reply more than once.
  template <typename Request, typename Response>
  [[nodiscard]] bool reply(const RequestContext &context,
      const typename Response::Object &object, uint64_t id, Fmt::Error &error) {
    using Dictionary = typename Impl::MessageCatalog;
    ZuAssert((ZuTypeIn<Request, typename Dictionary::Messages>{}));
    ZuAssert((ZuTypeIn<Response, typename Dictionary::Messages>{}));
    ZuAssert((ZuTypeIn<Response, typename Request::Responses>{}));
    if (!app()->rxInvoked()) { error = {Fmt::ErrorCode::Output}; return false; }
    if (!context.template is<Request>()) {
      error = {Fmt::ErrorCode::Unresolved, 35}; return false;
    }
    error = Context_::Reply<Impl, Request, Response, RequestContext>::call(impl(), context);
    return !error && send<Response>(object, id, error);
  }

  template <typename Entry>
  auto work(const RequestContext &context, Fmt::Error &error, uint64_t extra = 0) {
    using W = Zfix::Work<RequestContext, Impl>;
    using Dictionary = typename Impl::MessageCatalog;
    ZuAssert((ZuTypeIn<Entry, typename Dictionary::Messages>{}));
    if (!app()->rxInvoked() || !m_live) {
      error = {Fmt::ErrorCode::Output}; return typename W::Ref{};
    }
    const auto &session = m_rx.session_();
    if (session.state_() != State::Established || session.draining_()) {
      error = {Fmt::ErrorCode::Output}; return typename W::Ref{};
    }
    if (!context.template is<Entry>()) {
      error = {Fmt::ErrorCode::Unresolved, 35}; return typename W::Ref{};
    }
    return impl()->workRx_(context, context.memory(), extra, error);
  }

  void workDrainedRx_() {
    ZmAssert(app()->rxInvoked());
    if (m_live && m_rx.session_().draining_()) m_rx.workDone_();
    if (m_txStopped) stopped_(m_rx.session_().connection_());
  }

  void loaded(uint64_t ticket, bool accepted) {
    app()->txRun([this, ticket, accepted] { m_tx.loaded_(ticket, accepted); m_tx.checkStop(); });
  }

  void unloaded(uint64_t ticket) {
    app()->txRun([this, ticket] { m_tx.unloaded_(ticket); m_tx.checkStop(); });
  }

  // Local handoff admission, not ticket validity. Complete deferred retrieval
  // only after its hook returns. Refusal retains the caller's history so it can
  // retry or complete/cancel directly on Tx, including during stop.
  [[nodiscard]] bool reloaded(uint64_t ticket, const TxHistory &history) {
    if (app()->txInvoked()) {
      m_tx.reloaded_(ticket, history); m_tx.checkStop(); return true;
    }
    if (!app()->rxInvoked() || (!m_live && !m_closeRef)) return false;
    auto bytes = history.body ? history.body->memory() : 0;
    return postAdmin_(bytes, [ticket, history](Link *link) mutable {
      link->m_tx.reloaded_(ticket, ZuMv(history));
    });
  }

  bool abortTx_(uint64_t epoch, uint64_t seqNo) {
    ZmAssert(app()->txInvoked()); return m_tx.abortTx_(epoch, seqNo);
  }

  void queued() { app()->txRun([this] { m_tx.queued_(); }); }

  void probe() { app()->rxInvoke([this] { probe_(); }); }

  void probe_() {
    ZmAssert(app()->rxInvoked());
    if (m_live) m_rx.probe_(m_rx.session_().connection_(), Zm::now());
  }

  void logout() {
    app()->rxRun([this] { m_rx.logout_(m_rx.session_().connection_(), Zm::now()); });
  }

  // The coordinator supplies captured connection/epoch identities. Acceptance
  // reports only local command admission; drain/epoch events prove completion.
  void reset(ResetRequest request) {
    app()->rxRun([this, request] {
      bool accepted = reset_(request); impl()->fixResetRx_(request, accepted);
    });
  }

  bool reset_(const ResetRequest &request) {
    ZmAssert(app()->rxInvoked());
    auto &s = m_rx.session_();
    if (request.op == ResetOp::OnOpen)
      return !m_live && !m_closeRef && request.connection == s.connection_() &&
	s.epoch_() == request.epoch &&
	m_rx.resetOnOpen_(request.epoch, request.enabled);
    if (!m_live || request.connection != s.connection_() || request.epoch != s.epoch_())
      return false;
    switch (request.op) {
      case ResetOp::Drain:
	return m_rx.drainBegin_(request.connection, Zm::now());
      case ResetOp::Prepare:
	return m_rx.prepareReset_(request.connection, request.epoch,
	  request.nextEpoch, request.initiator, Zm::now(), request.path);
      case ResetOp::Start:
	return m_rx.startReset_(request.connection, request.epoch, Zm::now());
      case ResetOp::Disconnect:
	return m_rx.disconnectReset_(request.connection, request.epoch);
      default: return false;
    }
  }

  // Side-effect-safe defaults. Business association/storage and decoding remain
  // explicit application policy; nothing is silently accepted as a business send.
  unsigned fixAssociateTx_(TxBody::Ref b, TxContext, uint64_t) {
    return b->administrative() ? TxLoad::Accepted : TxLoad::Refused;
  }

  bool fixReleaseTx_(TxBody::Ref, TxContext, uint64_t) { return true; }

  TxReload fixRetrieveTx_(TxContext, uint64_t, uint64_t) { return {}; }
  void fixArchiveTx_(uint64_t, uint64_t, uint64_t) { }

  bool fixEnqueueTx_(TxBody::Ref) { return false; }
  TxBody::Ref fixDequeueTx_() { return {}; }

  void fixSelectedTx_(TxBody::Ref, TxContext, unsigned) { }
  void fixSubmittedTx_(TxBody::Ref, unsigned) { }

  void fixRejectedTx_(PeerReject) { }

  void fixControlTx_(TxBody::Ref, TxContext) { }

  TxBody::Ref fixLogonTx_(TxBody::Ref b) { return b; }

  void fixPublishedTx_(TxBody::Ref, TxContext, ZuDateTime) { }
  void fixCompleteTx_(TxBody::Ref, TxContext, bool) { }

  void fixFaultTx_(TxContext, Fmt::Error) { }

  void fixAckdTx_(TxContext, uint64_t, uint64_t) { }

  void fixLimitTx_(TxContext, uint64_t, unsigned) { }

  void fixEpochTx_(TxContext, uint64_t, TxProgress, uint64_t) { }
  void fixEpochRx_(uint64_t, uint64_t, uint64_t, RxProgress) { }

  void fixStopTx_(uint64_t) { }
  void fixStopRx_(uint64_t, uint64_t) { }
  void fixStoppedRx_(bool) { }

  void fixBlockedRx_(TxContext) { }

  void fixStateRx_(SessionEvent) { }

  void fixResetRx_(ResetRequest, bool) { }

  void fixEventRx_(SessionEvent, ZmRef<Msg>) { }

  void fixControlRx_(ZmRef<Msg>) { }

  unsigned fixAdmissionRx_(SessionEvent, ZmRef<Msg>, AdmitRequest &) {
    return Admission::Accepted;
  }

  Fmt::Error fixHeaderRx_(const Fmt::FrameView &f, Fmt::Header &h, Fmt::Limits l) {
    return catalogHeader<typename Impl::MessageCatalog>(f, h, l);
  }

  Fmt::Error fixBusinessRx_(Msg *message, Fmt::Limits limits) {
    ZmAssert(app()->rxInvoked());
    return dispatch<typename Impl::MessageCatalog>(message->data().frame,
      [this, message]<typename Entry>(Entry *type, const typename Entry::Object &object,
	  const Fmt::Header &header, const Fmt::Trailer &trailer) {
	auto &input = message->data(); input.header = header; input.trailer = trailer;
	RequestContext context{type, m_rx.session_().instance_(), ZmRef<const Msg>{message}};
	return Context_::Receive<Impl, Entry>::call(impl(), type, object, context);
      }, limits);
  }

private:
  static AdmitFn deferAdmission_(void *owner, uint64_t connection, uint64_t ticket) {
    auto link = static_cast<Link *>(owner);
    ZmAssert(link->app()->rxInvoked());
    ++link->m_admissions;
    ZmRef<AdmitReply> reply = new AdmitReply{link->impl(), connection, ticket};
    return AdmitFn{ZuMv(reply), [](AdmitReply *p, bool accepted) { p->complete(accepted); }};
  }

  void admitted_(uint64_t connection, uint64_t ticket, bool accepted) {
    ZmAssert(app()->rxInvoked());
    if (ticket && connection == m_rx.session_().connection_() &&
	ticket == m_admissionTicket) m_admissionTicket = 0;
    m_rx.admit_(connection, ticket, accepted, Zm::now());
    if (m_txStopped) stopped_(m_rx.session_().connection_());
  }

  auto impl() { return static_cast<Impl *>(this); }
  auto impl() const { return static_cast<const Impl *>(this); }
  auto app() { return impl()->app(); }
  auto app() const { return impl()->app(); }

  static TraceField traceField_(void *owner, ZuCSpan type, unsigned tag) {
    auto p = static_cast<Impl *>(owner); auto f = p->fixTraceField_(type, tag);
    f.secret |= p->fixTraceSecret_(type, tag); return f;
  }

  void traceTx_(ZuCSpan wire, TxContext c) {
    if (!m_traceTx.enabled_()) return;
    auto entry = m_traceTx.capture_(wire, c, traceField_, impl());
    if (entry) impl()->fixTraceTx_(ZuMv(entry));
  }
  struct AdminReturn {
    Link	*link;
    uint64_t	bytes;

    void operator ()() {
      link->adminReturned_(bytes);
    }
  };

  template <typename Fn> struct AdminWork {
    Link	*link;
    uint64_t	bytes;
    Fn		fn;

    void operator ()() {
      fn(link); link->adminDone_(bytes);
    }
  };

  void adminReturned_(uint64_t bytes) {
    m_admin.release_(bytes); app()->fixAdminRx_().release_(bytes);
    if (m_txStopped) stopped_(m_rx.session_().connection_());
  }

  void adminDone_(uint64_t bytes) {
    m_tx.checkStop();
    app()->rxRun(AdminReturn{this, bytes});
  }

  void peerReject_(PeerReject report) {
    auto bytes = report.buffer ? rxMemory_(*report.buffer) : 0;
    if (!postAdmin_(bytes, [report = ZuMv(report)](Link *link) mutable {
      link->m_tx.peerReject_(ZuMv(report));
    })) adminFailed_();
  }

  void reject_(uint64_t seqNo, unsigned tag, int reason) {
    Session::Msg c;
    c.template p<Session::Reject>(Session::Reject{
      .refSeqNo = seqNo, .refTagID = tag, .reason = reason});
    Fmt::Error error;
    auto body = TxBody::control(c, error, m_rx.session_().config_().mode, m_codec);
    if (error) { fault_(SessionError::Protocol, error.tag); return; }
    const auto &s = m_rx.session_();
    auto connection = s.connection_(), epoch = s.epoch_(), bytes = body->memory();
    if (!postAdmin_(bytes, [connection, epoch, body = ZuMv(body)](Link *link) mutable {
      if (link->m_tx.control_(connection, epoch, ZuMv(body)) == TxSelect::Overload)
	link->m_tx.faultTx_({}, {Fmt::ErrorCode::Size});
    })) adminFailed_();
  }

  void resend_(uint64_t begin, uint64_t end) {
    const auto &s = m_rx.session_();
    auto connection = s.connection_(), epoch = s.epoch_();
    if (!postAdmin_(0, [connection, epoch, begin, end](Link *link) {
      link->m_tx.replay_(connection, epoch, begin, end);
    })) adminFailed_();
  }

  void outputFailed_(TxContext c) {
    const auto &session = m_rx.session_();
    if (c.connection != session.connection_() || c.epoch != session.epoch_()) return;
    if (m_live && session.state_() == State::Disconnected) {
      impl()->disconnect_(false); return;
    }
    m_rx.outputFailed_(c.connection, c.epoch);
  }

  void notice_(TxNotice notice) {
    m_rx.notice_(notice, Zm::now(), m_live);
  }

  static uint64_t rxMemory_(const ZiIOBuf &b) {
    return ZmHeapAllocSize<sizeof(typename Impl::RxBufAlloc)>{} +
      ((b.data__ & ZiIOBuf::Jumbo) ? Zfix::jumboBytes(b.size) : 0);
  }

  template <typename Fn> bool postAdmin_(uint64_t retained, Fn fn) {
    ZmAssert(app()->rxInvoked());
    if (m_adminFailed) return false;
    using Work = AdminWork<Fn>;
    // Account the callable/delegate envelopes in both directions, including
    // pending credit returns. Ring capacity and idle heap caches are separate.
    constexpr uint64_t overhead =
      ZmHeapAllocSize<sizeof(Work) + sizeof(ZmRingFn<ZmRingFn_Defaults>)>{} +
      ZmHeapAllocSize<sizeof(AdminReturn) + sizeof(ZmRingFn<ZmRingFn_Defaults>)>{};
    auto &hub = app()->fixAdminRx_();
    if (retained > UINT64_MAX - overhead || !m_admin.fits_(retained + overhead)) {
      m_admin.refuse_(); hub.refuse_(); return false;
    }
    uint64_t bytes = retained + overhead;
    if (!hub.acquire_(bytes)) { m_admin.refuse_(); return false; }
    [[maybe_unused]] bool acquired = m_admin.acquire_(bytes); ZmAssert(acquired);
    app()->txRun(Work{this, bytes, ZuMv(fn)}); return true;
  }

  void adminFailed_() {
    if (m_adminFailed) return;
    m_adminFailed = true;
    // Do not turn refusal into another queued control or recursively fault on
    // its terminal event. Physical teardown has its own fixed drain barrier.
    fault_(SessionError::Overload); impl()->disconnect_(false);
  }

  void fault_(unsigned error, unsigned tag = 0) {
    m_rx.fault_(m_rx.session_().connection_(), error, tag, Zm::now());
  }

  void receive_() {
    if (!m_pending) return;
    auto &s = m_rx.session_();
    auto now = Zm::now();
    if (m_rx.receive_(s.connection_(), m_pending, now, ZuDateTime{now}) ==
	RxResult::Blocked)
      impl()->fixBlockedRx_(TxContext{s.epoch_(), s.connection_(), 0});
  }

  void event_(SessionEvent event, ZmRef<Msg> msg) {
    if (!event.connection && (event.actions & SessionAction::Refuse)) {
      impl()->fixEventRx_(event, ZuMv(msg));
      return;
    }
    if (event.actions & SessionAction::Timers) syncTimers_();
    ZuCSpan echo;
    if ((event.actions & SessionAction::Echo) && msg)
      echo = msg->data().control.template p<Session::TestReq>().testReqID;
    if (!m_adminFailed && event.actions != SessionAction::Timers) {
      auto error = controlCommands(event, m_rx.session_(), m_rx.expected_(), echo,
	m_framer.limits().frameBytes,
	[this](SessionEvent e, TxBody::Ref body) {
	  auto bytes = body ? body->memory() : 0;
	  return postAdmin_(bytes, [e, body = ZuMv(body)](Link *link) mutable {
	    link->m_tx.applyEvent(e, ZuMv(body));
	  });
	}, m_codec);
      if (error) {
	bool terminal = event.state == State::Closing || event.state == State::Disconnected;
	adminFailed_();
	// A failure already transitioned to Closing cannot emit another fault.
	// Preserve that original terminal observer even if Tx admission failed.
	if (!terminal) return;
      }
    }
    if (event.actions & SessionAction::Admission) {
      m_admissionTicket = event.ticket;
      AdmitRequest request{this, event.connection, event.ticket, deferAdmission_};
      auto result = impl()->fixAdmissionRx_(event, msg, request);
      if (result != Admission::Pending || !request.deferred()) {
	bool accepted = result == Admission::Accepted;
	if (request.deferred()) request.defer()(accepted);
	else app()->rxRun([this, connection = event.connection, ticket = event.ticket, accepted] {
	  admitted_(connection, ticket, accepted);
	});
      }
    }
    if (event.actions & SessionAction::RetryInput) {
      app()->rxRun([this, connection = event.connection] {
	if (!m_live || connection != m_rx.session_().connection_()) return;
	receive_(); if (!m_pending) impl()->resumeRx_();
      });
    }
    impl()->fixEventRx_(event, ZuMv(msg));
    if (m_live && event.connection == m_rx.session_().connection_() &&
	(event.actions & SessionAction::Close)) impl()->disconnect_(!m_adminFailed);
  }

  void cancelTimer_() {
    app()->mx()->del(&m_sessionTimer); m_timerAt.null(); ++m_timerSerial;
  }

  void cancelGap_() {
    app()->mx()->del(&m_gapTimer); ++m_gapSerial;
  }

  void cancelTimers_() { cancelTimer_(); cancelGap_(); }

  void syncTimers_() {
    ZuTime at;
    m_rx.session_().nextTimer_(at);
    if (!m_live || !*at) { cancelTimer_(); return; }
    // Retain an earlier wake when logical activity postpones its deadline.
    if (*m_timerAt && m_timerAt <= at) return;
    m_timerAt = at;
    auto serial = ++m_timerSerial;
    auto &s = m_rx.session_(); auto connection = s.connection_(), epoch = s.epoch_();
    app()->mx()->del(&m_sessionTimer);
    app()->mx()->add(&m_sessionTimer, at, ZmScheduler::Update,
      [this, serial, connection, epoch](auto &&arm) {
	return arm([this, serial, connection, epoch] {
	  if (!m_live || serial != m_timerSerial ||
	      connection != m_rx.session_().connection_() ||
	      epoch != m_rx.session_().epoch_()) return;
	  m_timerAt.null();
	  ZuTime deadline;
	  auto kind = m_rx.session_().nextTimer_(deadline);
	  if (*deadline) m_rx.timer_(connection, kind, Zm::now());
	  // A consumed deadline can have no Timers action; other deadlines still run.
	  syncTimers_();
	});
      }, app()->rxThread());
  }

  void armGap_(uint64_t ticket) {
    auto at = Zm::now() + m_gapRetry; auto serial = ++m_gapSerial;
    auto &s = m_rx.session_(); auto connection = s.connection_(), epoch = s.epoch_();
    app()->mx()->del(&m_gapTimer);
    app()->mx()->add(&m_gapTimer, at, ZmScheduler::Update,
      [this, ticket, serial, connection, epoch](auto &&arm) {
	return arm([this, ticket, serial, connection, epoch] {
	  if (!m_live || serial != m_gapSerial ||
	      connection != m_rx.session_().connection_() ||
	      epoch != m_rx.session_().epoch_()) return;
	  m_rx.retry_(ticket);
	});
      }, app()->rxThread());
  }

  void stopped_(uint64_t connection) {
    ZmAssert(app()->rxInvoked());
    if (m_live || connection != m_rx.session_().connection_()) return;
    m_txStopped = true;
    if (m_admissionTicket || m_admissions || !m_closeRef || impl()->txPosted_().count ||
	m_admin.state_().count || impl()->workState_().count) return;
    auto retain = ZuMv(m_closeRef);
    impl()->fixStoppedRx_(m_peerClosed);
  }

  const ZuTime		m_gapRetry;
  const Fmt::Limits	m_codec;

  // Scheduler-shared nodes, all protected by the scheduler's lock. Ownership
  // applies to the whole group; owner-local arm metadata lives below.
  alignas(Zm::CacheLineSize)
  ZmScheduler::Timer	m_sessionTimer;
  ZmScheduler::Timer	m_gapTimer, m_idleTimer;

  // One Rx group, including the native hooks/input extension when present.
  // The sole teardown handle spans the owner drain.
  alignas(Zm::CacheLineSize)
  RxSide		m_rx;
  [[no_unique_address]]
  NativeRx		m_native;
  ZuTime		m_timerAt;
  uint64_t		m_timerSerial = 0, m_gapSerial = 0;
  RxFramer		m_framer;
  WireTrace		m_traceRx;
  PostedIngress		m_admin;
  ZmRef<Msg>		m_pending;
  ZmRef<Impl>		m_closeRef;
  uint64_t		m_admissionTicket = 0;
  unsigned		m_admissions = 0;
  bool			m_txStopped = false;
  bool			m_adminFailed = false;
  bool			m_live = false; // associated; FIX phase is in m_rx
  bool			m_peerClosed = false;

  // Tx owns both the sequencer/output state and its diagnostic queue.
  alignas(Zm::CacheLineSize)
  TxSide		m_tx;
  WireTrace		m_traceTx;
};

} // Zfix

#endif /* ZfixLink_HH */
