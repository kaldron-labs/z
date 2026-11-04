//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Managed control commands and Tx-to-Rx publication notices

#ifndef ZfixLinkTx_HH
#define ZfixLinkTx_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/ZmList.hh>

#include <zlib/ZfixHooks.hh>
#include <zlib/ZfixTx.hh>

namespace Zfix {

struct ControlItem {
  TxBody::Ref	body;
  uint64_t	connection = 0;
  uint64_t	charge = 0;
};

using ControlQueue = ZmList<ControlItem, ZmListHeapID<"Zfix.LinkTx",
  ZmListNode<ZmObject>>>;
namespace ControlTask {

  enum { Pump = TxTask::Reset + 1 };
}

struct ControlLimits {
  // Unsequenced commands have their own bound, separate from SeqTx output.
  uint64_t	queuedBytes = Fmt::Limits::FrameBytes;
};

// Rx-only factory. It consumes borrowed session/input values immediately and
// returns immutable owned bytes. No wire sequence or SendingTime is captured.
// rxFrameBytes advertises local receive capacity in Session::Logon; txLimits bounds
// encoding independently. Neither limit is inferred from the peer.
// action is one output bit from SessionAction; echo preserves opaque tag 112.
ZfixExtern TxBody::Ref controlBody(unsigned action, const SessionEvent &,
  const LinkState &, uint64_t expected, ZuCSpan echo, Fmt::Error &, unsigned rxFrameBytes,
  Fmt::Limits txLimits = {});

// The emitter queues one retained body and fixed event to Tx. Subsequent bodies
// from a combined event do not repeat lifecycle/replay side effects. A false
// emission reports bounded admission failure; accepted prefixes are not undone.
template <typename Emit>
Fmt::Error controlCommands(SessionEvent event, const LinkState &session,
    uint64_t expected, ZuCSpan echo, unsigned rxFrameBytes, Emit emit,
    Fmt::Limits txLimits = {}) {
  unsigned actions = event.actions & (SessionAction::SendLogon |
    SessionAction::SendProbe | SessionAction::SendHeartbeat | SessionAction::Echo |
    SessionAction::RequestGap | SessionAction::SendLogout);
  if (!actions)
    return emit(event, TxBody::Ref{}) ? Fmt::Error{} : Fmt::Error{Fmt::ErrorCode::Size};
  while (actions) {
    unsigned action = actions & -actions;
    actions &= actions - 1;
    Fmt::Error error;
    auto body = controlBody(action, event, session, expected, echo, error,
      rxFrameBytes, txLimits);
    if (error) return error;
    if (!emit(event, ZuMv(body))) return {Fmt::ErrorCode::Size};
    event.actions = 0;
  }
  return {};
}

// Tx-only managed command coordinator. The enclosing endpoint posts commands
// and notices between owners; it never reads another owner's live state.
//
// Impl hooks (all called on Tx):
// txSchedule(kind, ticket) posts run_; associateTx, releaseTx, retrieveTx,
// archiveTx, clockTx and faultTx implement Tx's application/clock hooks;
// admitFrameTx(body, context, frame, original) reserves complete transport output
//   and retains no bytes on false; it MUST NOT expose bytes to the peer yet;
// notifyTx(TxNotice) queues an ordered post to Rx, never invokes Rx inline;
// flushTx() starts transport only after that post is queued;
// completeTx(body, context, ok) observes one-shot local completion, not ack.
// nowTx() samples the monotonic timer clock. idleArmTx(connection, ticket,
// deadline) arms the owner's dedicated idle timer at an absolute deadline;
// idleCancelTx() cancels it. Its callback invokes idle_(connection, ticket).
// Cancel and drain callbacks on Tx before destruction, as for owner tasks.
// The transport calls sent_ exactly once per admitted frame, including replay.
// Frames/continuations and owner tasks must be drained before destruction.
template <typename Impl> class LinkTx :
    private Tx<LinkTx<Impl>>, public Hooks<Impl> {
  using Base = Tx<LinkTx<Impl>>;
friend Base;
public:
  using Hooks<Impl>::SignMsg;
  using Hooks<Impl>::signMsg;
  TxFrameBuf allocFrameTx() { return impl()->frameStorageTx(); }

  const Impl &signer_() const { return *static_cast<const Impl *>(this); }

  explicit LinkTx(SessionConfig config, TxLimits limits = {},
      ControlLimits controlLimits = {}, uint64_t epoch = 1) :
    Base{ZuMv(config), limits, epoch}, m_limits{controlLimits} { }

  LinkTx(const LinkTx &) = delete;
  LinkTx &operator =(const LinkTx &) = delete;

  using Base::limits;
  using Base::next_;
  using Base::published_;
  using Base::acknowledged_;
  using Base::queuedBytes_;
  using Base::retained_;
  using Base::epoch_;
  using Base::connection_;
  using Base::loading_;
  using Base::failed_;
  using Base::waiting_;
  using Base::restore_;
  using Base::progress_;

  TxStats stats_() const {
    auto s = Base::stats_(); s.controlBytes = m_bytes; s.inFlight = m_inFlight;
    s.enqueueRefused = m_enqueueRefused; s.faults = m_faults; return s;
  }

  // Physical close is independently available when an enclosing owner refuses
  // further lifecycle posts. Pending application hooks still retire normally.
  void close_(uint64_t connection) {
    if (Base::attached_() && connection == Base::connection_()) closeTx();
  }

  uint64_t controlBytes_() const { return m_bytes; }
  uint64_t inFlight_() const { return m_inFlight; }
  bool preparing_() const { return m_preparing; }

  using Base::abortTx_;

  void epochTx(TxContext, uint64_t, TxProgress, uint64_t) { }

  ZuTime idleDeadline_() const { return m_idleDeadline; }

  // Physical association holds publication before authentication. Sequence
  // operations belong to the persistent owner; only old controls are retired.
  unsigned bind_(SessionEvent event) {
    if (!event.connection || event.connection <= Base::connection_())
      return TxSelect::Invalid;
    bool agreement = m_resetTarget && event.nextEpoch == m_resetTarget &&
      event.drain == m_drain && (event.epoch == m_prepareEpoch ||
	(m_resetCommitted && event.epoch == Base::epoch_()));
    if ((m_resetTarget && !agreement) ||
	(event.epoch != Base::epoch_() && !agreement)) return TxSelect::Invalid;
    if (!agreement && event.nextEpoch) {
      auto p = Base::progress_();
      if (event.nextEpoch != event.epoch || p.loaded != 1 || p.sent != 1 ||
	  p.acknowledged != 1 || p.replayBegin || Base::retained_() ||
	  Base::loading_() || Base::waiting_()) return TxSelect::Invalid;
    }
    bool attached = Base::attached_();
    if (!(attached ? Base::replace_(event.connection) : Base::attach_(event.connection)))
      return TxSelect::Invalid;
    clear();
    m_started = m_wireStarted = m_faulted = m_closeAfter = m_established = false;
    m_inFlight = m_replayTask = 0;
    if (!agreement) clearReset(event.nextEpoch && event.nextEpoch == event.epoch);
    m_change = event.change;
    resetProgress();
    return TxSelect::Accepted;
  }

  // Rx posts every lifecycle event in order. A supplied body is one already
  // encoded control command from that event (or a Session::Reject made by the Rx owner).
  unsigned apply_(SessionEvent event, TxBody::Ref body = {}) {
    if (!event.connection) return TxSelect::Invalid;
    auto outcome = event.actions & (SessionAction::ResetReady | SessionAction::ResetAbort);
    if (outcome) {
      bool committed = outcome == SessionAction::ResetReady;
      if (outcome == (SessionAction::ResetReady | SessionAction::ResetAbort) ||
	  !m_resetTarget || event.connection != Base::connection_() ||
	  event.change < m_change || event.ticket != m_resetRequest ||
	  event.drain != m_drain || event.nextEpoch != m_resetTarget ||
	  committed != m_resetCommitted || event.epoch != Base::epoch_() ||
	  event.epoch != (committed ? m_resetTarget : m_prepareEpoch) ||
	  (body && !(event.actions & SessionAction::SendLogon))) return TxSelect::Invalid;
      clearReset(committed);
      m_change = event.change;
    }
    // The physical owner can change while the agreed reset completion is
    // queued to Rx. Keep that logical transaction, but revoke old send authority.
    if (event.connection >= Base::connection_() && m_resetTarget && !body &&
	(event.state == State::Logon || event.state == State::Accepting) &&
	event.epoch == m_prepareEpoch && event.nextEpoch == m_resetTarget &&
	event.drain == m_drain) {
      if (event.connection > Base::connection_()) return bind_(event);
      if (event.change < m_change) return TxSelect::Invalid;
      m_change = event.change;
      return TxSelect::Accepted;
    }
    if (event.epoch != Base::epoch_()) {
      if (m_resetTarget && event.epoch == m_prepareEpoch && !body &&
	  event.connection == Base::connection_() &&
	  event.nextEpoch == m_resetTarget && event.drain == m_drain &&
	  ((event.actions & SessionAction::Close) || event.state == State::Disconnected)) {
	closeTx(); return TxSelect::Refused;
      }
      return TxSelect::Invalid;
    }
    if (event.connection > Base::connection_()) {
      if (event.state != State::Logon && event.state != State::Accepting)
	return TxSelect::Invalid;
      auto result = bind_(event);
      if (result != TxSelect::Accepted) return result;
    }
    if (!Base::attached_() && event.connection == Base::connection_() && !body &&
	((event.actions & SessionAction::Close) || event.state == State::Closing ||
	  event.state == State::Disconnected))
      return TxSelect::Accepted;
    if (!Base::attached_() || event.connection != Base::connection_() || event.change < m_change)
      return TxSelect::Invalid;
    m_change = event.change;
    if (event.actions & SessionAction::Drain) {
      if (!event.drain || (m_drain && event.drain != m_drain)) return TxSelect::Invalid;
      m_drain = event.drain; m_drainProven = false;
      Base::ready_(event.connection, false);
    }
    if (event.actions & SessionAction::Recovery) m_drainProven = false;
    if (event.actions & SessionAction::Drained) {
      if (!m_drain || event.drain != m_drain || !m_drainBoundary ||
	  !(event.actions & SessionAction::Ackd) || event.acknowledged < m_drainBoundary)
	return TxSelect::Invalid;
      m_drainProven = true;
    }
    if (event.actions & SessionAction::ResetPrepare) {
      if (body || !m_drainProven || event.drain != m_drain || m_preparing ||
	  event.nextEpoch <= event.epoch) return TxSelect::Invalid;
      m_preparing = true; m_resetCommitted = false;
      m_prepareEpoch = event.epoch; m_resetTarget = event.nextEpoch;
      stopIdle();
    }
    if (event.actions & SessionAction::Pause) Base::ready_(event.connection, false);
    if ((event.actions & SessionAction::Ready) && !m_drain)
      Base::ready_(event.connection, true);
    if (event.state == State::Disconnected && outcome)
      return TxSelect::Accepted;
    if ((event.actions & SessionAction::Close) || event.state == State::Disconnected) {
      closeTx(); return TxSelect::Refused;
    }
    if ((event.actions & SessionAction::Ackd) &&
	!Base::acknowledge_(event.epoch, event.acknowledged))
      return TxSelect::Invalid;
    if (event.actions & SessionAction::Replay) {
      // Lifecycle snapshots use an exclusive boundary; Session::ResendReq uses the
      // separate inclusive replay_ entry point below.
      if (event.replayEnd <= event.replayBegin ||
	  !replay_(event.connection, event.epoch, event.replayBegin, event.replayEnd - 1))
	return TxSelect::Invalid;
    }
    if (event.state == State::Established && !m_established) {
      m_established = true;
      m_heartBtInt = event.heartBtInt;
      if (m_heartBtInt) {
	m_idleDeadline = (m_wireStarted ? m_lastSend : impl()->nowTx()) + ZuTime{m_heartBtInt};
	armIdle();
      }
      m_recovered = !Base::replaying_() && !Base::waiting_();
      notice(TxNoticeKind::Recovery, {}, 0, m_recovered);
    }
    if (event.state != State::Established ||
	(event.actions & (SessionAction::SendLogout | SessionAction::CloseAfterTx))) stopIdle();
    unsigned result = body ? control_(event.connection, event.epoch, ZuMv(body)) :
      TxSelect::Accepted;
    if (result != TxSelect::Accepted) return result;
    if (event.actions & SessionAction::CloseAfterTx) m_closeAfter = true;
    progress();
    postPump();
    resetProgress();
    drainReady();
    closeReady();
    return result;
  }

  unsigned control_(uint64_t connection, uint64_t epoch, TxBody::Ref body) {
    if (!Base::attached_() || connection != Base::connection_() || epoch != Base::epoch_() ||
	!body || !body->administrative()) return TxSelect::Invalid;
    if (m_preparing || (m_resetArmed && body->msgType() != "A" && body->msgType() != "5"))
      return TxSelect::Held;
    uint64_t charge = body->memory();
    constexpr uint64_t overhead = ZmHeapAllocSize<sizeof(ControlQueue::Node)>{};
    if (charge > UINT64_MAX - overhead) return TxSelect::Overload;
    charge += overhead;
    if (charge > m_limits.queuedBytes || m_bytes > m_limits.queuedBytes - charge)
      return TxSelect::Overload;
    m_commands.push(ControlItem{ZuMv(body), connection, charge});
    m_bytes += charge;
    postPump();
    return TxSelect::Accepted;
  }

  // These run on Tx. The application owns/bounds the unsequenced queue.
  bool enqueue_(TxBody::Ref body) {
    if (!body || body->administrative()) return false;
    if (!impl()->enqueueTx(ZuMv(body))) { ++m_enqueueRefused; return false; }
    queued_(); return true;
  }

  void queued_() { m_queued = true; postPump(); }

  // An ordered Rx post to the same owner as association. A prior connection
  // in this epoch may have supplied a retained Session::Reject; connection is provenance,
  // not the key for the referenced outbound application association.
  bool peerReject_(PeerReject report) {
    const auto &context = report.context;
    if (!report.buffer || context.epoch != Base::epoch_() || !context.connection ||
	context.connection > Base::connection_() || !context.seqNo || !report.message.refSeqNo)
      return false;
    impl()->rejectedTx(ZuMv(report));
    return true;
  }

  unsigned candidate_(TxBody::Ref body) {
    if (!body || body->administrative()) return TxSelect::Invalid;
    if (!Base::attached_() || !m_started || m_faulted || m_drain || m_preparing || m_resetArmed)
      return TxSelect::Held;
    if (m_commands.count_() || m_selected || m_candidate || m_queued) return TxSelect::Busy;
    return Base::select_(ZuMv(body));
  }

  unsigned loaded_(uint64_t ticket, bool accepted) {
    auto result = Base::loaded_(ticket, accepted);
    if (result != TxSelect::Invalid && m_selected) {
      bool current = m_selected->data().connection == Base::connection_();
      dropSelected();
      if (current && result == TxSelect::Refused && !Base::loading_())
	fail({Fmt::ErrorCode::Output});
      if (result == TxSelect::Accepted) start();
    }
    if (result != TxSelect::Invalid && m_candidate) selected(result);
    postPump();
    drainReady();
    return result;
  }

  bool unloaded_(uint64_t ticket) {
    bool result = Base::unloaded_(ticket);
    if (result) { postPump(); drainReady(); }
    return result;
  }

  bool reloaded_(uint64_t ticket, TxHistory history) {
    bool result = Base::reloaded_(ticket, ZuMv(history));
    if (result) { progress(); postPump(); drainReady(); }
    return result;
  }

  bool replay_(uint64_t connection, uint64_t epoch, uint64_t begin, uint64_t end = 0) {
    if (!Base::attached_() || m_preparing || m_resetArmed || connection != Base::connection_() ||
	!Base::replay_(epoch, begin, end)) return false;
    if (m_recovered) { m_recovered = false; notice(TxNoticeKind::Recovery, {}, 0, false); }
    return true;
  }

  bool acknowledge_(uint64_t connection, uint64_t epoch, uint64_t next) {
    return Base::attached_() && connection == Base::connection_() &&
      Base::acknowledge_(epoch, next);
  }

  bool writable_(uint64_t connection) {
    if (!Base::attached_() || connection != Base::connection_() || m_faulted) return false;
    if (m_started) Base::resume_();
    postPump();
    return true;
  }

  void sent_(TxBody::Ref body, TxContext context, bool ok) {
    if (context.epoch == Base::epoch_() && context.connection == Base::connection_()) {
      ZmAssert(m_inFlight);
      --m_inFlight;
      if (!ok && Base::attached_()) fail({Fmt::ErrorCode::Output});
    }
    impl()->completeTx(ZuMv(body), context, ok);
    resetProgress();
    drainReady();
    closeReady();
  }

  // Optional progress notification on the same Tx owner as association. The
  // application posts it to its association owner, preserving that hook order.
  void publishedTx(TxBody::Ref, TxContext, ZuDateTime) { }
  void acknowledgedTx(TxContext, uint64_t, uint64_t) { }
  void sequenceLimitTx(TxContext, uint64_t, unsigned) { }

  // A consumed/duplicate/old-connection callback cannot emit another heartbeat.
  // Early callbacks re-arm at the current logical deadline; output activity
  // moves that deadline without rescheduling the physical timer per message.
  bool idle_(uint64_t connection, uint64_t ticket) {
    if (!Base::attached_() || m_faulted || !m_idleArmed || !m_heartBtInt ||
	connection != Base::connection_() || ticket != m_idleTicket) return false;
    m_idleArmed = false;
    auto now = impl()->nowTx();
    if (now < m_idleDeadline) { armIdle(); return true; }
    m_idleDeadline = now + ZuTime{m_heartBtInt};
    if (!m_commands.count_() && !m_selected && !Base::loading_() &&
	Base::next_() == Base::published_()) {
      Session::Msg control;
      control.p<Session::HeartBt>(Session::HeartBt{});
      Fmt::Error error;
      auto body = TxBody::captureControl(control, error, Transport::FIX4, Base::limits().codec);
      if (!error && control_(connection, Base::epoch_(), ZuMv(body)) != TxSelect::Accepted)
	error = {Fmt::ErrorCode::Size};
      if (error) { fail(error); return true; }
    }
    armIdle();
    return true;
  }

  void run_(unsigned kind, uint64_t ticket) {
    if (kind == ControlTask::Pump) {
      if (!m_pumpPosted || ticket != m_pumpTicket) return;
      m_pumpPosted = false;
      pump();
    } else {
      Base::run_(kind, ticket);
      postPump();
    }
    progress();
    resetProgress();
    drainReady();
    closeReady();
  }

private:
  auto impl() { return static_cast<Impl *>(this); }

  void closeTx() {
    bool aborted = m_resetTarget && !m_resetCommitted && m_drainProven &&
      Base::epoch_() == m_prepareEpoch;
    // Close cancels commit authority, not the agreement whose outcome is still
    // queued to Rx. detach_ may cancel its reset task; that callback is inert.
    m_preparing = false;
    m_drainProven = false;
    Base::detach_(Base::connection_());
    clear();
    if (aborted)
      impl()->notifyTx(TxNotice{.context = {m_prepareEpoch, Base::connection_(), 0},
	.marker = m_drain, .kind = TxNoticeKind::ResetAborted,
	.nextEpoch = m_resetTarget, .request = m_resetRequest});
  }

  void clearReset(bool armed) {
    m_preparing = m_resetCommitted = false;
    m_resetRequest = m_prepareEpoch = m_resetTarget = 0;
    m_resetArmed = armed;
    m_drainProven = false;
    m_drain = m_drainBoundary = 0;
  }

  void clear() {
    stopIdle();
    m_lastSend.null();
    while (auto item = m_commands.shift()) m_bytes -= item->data().charge;
    m_pumpPosted = false;
    ++m_pumpTicket;
  }

  void armIdle() {
    if (m_idleArmed || !m_heartBtInt) return;
    m_idleArmed = true;
    impl()->idleArmTx(Base::connection_(), ++m_idleTicket, m_idleDeadline);
  }

  void stopIdle() {
    m_heartBtInt = 0;
    m_idleDeadline.null();
    if (!m_idleArmed) return;
    m_idleArmed = false;
    ++m_idleTicket;
    impl()->idleCancelTx();
  }

  void dropSelected() {
    m_bytes -= m_selected->data().charge;
    m_selected = nullptr;
  }

  bool businessReady() const {
    return m_queued && m_started && Base::ready_() && !m_drain &&
      !m_preparing && !m_resetArmed && !m_closeAfter;
  }

  void selected(unsigned result) {
    auto body = ZuMv(m_candidate);
    if (result != TxSelect::Accepted) m_queued = false;
    impl()->selectedTx(ZuMv(body), m_candidateCtx, result);
  }

  void postPump() {
    if (m_started && !m_wireStarted) return;
    if (!Base::attached_() || m_faulted || Base::resetting_() || m_resetCommitted ||
	m_pumpPosted || m_selected || (!m_commands.count_() && !businessReady()) ||
	Base::loading_()) return;
    m_pumpPosted = true;
    impl()->txSchedule(ControlTask::Pump, ++m_pumpTicket);
  }

  void start() {
    if (m_started) return;
    m_started = true;
    Base::resume_();
  }

  void pump() {
    if (m_started && !m_wireStarted) return;
    if (!Base::attached_() || m_faulted || Base::resetting_() || m_resetCommitted ||
	m_selected || Base::loading_()) return;
    auto item = m_commands.headPtr();
    if (!item) {
      if (!businessReady()) return;
      m_candidate = impl()->dequeueTx();
      if (!m_candidate) { m_queued = false; return; }
      m_candidateCtx = {Base::epoch_(), Base::connection_(), Base::next_()};
      unsigned result = m_candidate->administrative() ? TxSelect::Invalid :
	Base::select_(m_candidate);
      if (result == TxSelect::Pending) return;
      selected(result);
      if (result == TxSelect::Accepted) postPump();
      return;
    }
    auto result = Base::select_(item->data().body);
    if (result == TxSelect::Overload) return; // publication/writability releases capacity
    m_selected = m_commands.shift();
    if (result == TxSelect::Pending) return;
    dropSelected();
    if (result != TxSelect::Accepted) { fail({Fmt::ErrorCode::Output}); return; }
    start();
    postPump();
  }

  void notice(unsigned kind, TxContext context = {}, uint64_t marker = 0, bool done = false) {
    if (!context.connection) context = {Base::epoch_(), Base::connection_(), 0};
    auto published = Base::published_();
    if (kind == TxNoticeKind::Logon || kind == TxNoticeKind::Probe) published = context.seqNo + 1;
    impl()->notifyTx(TxNotice{context, marker, Base::next_(), published,
      Base::acknowledged_(), kind, done});
  }

  void progress() {
    if (Base::attached_() && !m_recovered && !Base::replaying_() && !Base::waiting_()) {
      m_recovered = true;
      notice(TxNoticeKind::Recovery, {}, 0, true);
    }
  }

  void drainReady() {
    if (!Base::attached_() || m_faulted || !m_drain || m_drainBoundary || !outputIdle()) return;
    m_drainBoundary = Base::next_();
    notice(TxNoticeKind::DrainBoundary, {}, m_drain);
  }

  bool outputIdle() const {
    return !m_inFlight && !m_commands.count_() && !m_selected && !Base::loading_() &&
      !Base::replaying_() && !Base::waiting_() &&
      Base::next_() == Base::published_();
  }

  void resetProgress() {
    if (!Base::attached_() || m_faulted || !m_preparing || m_resetCommitted ||
	m_resetRequest || !outputIdle() || !Base::resetReady_()) return;
    Base::hold_();
    m_resetRequest = Base::resetBegin_(m_prepareEpoch, m_resetTarget);
    if (!m_resetRequest) fail({Fmt::ErrorCode::Output});
  }

  void resetTx(TxContext context, uint64_t epoch, TxProgress previous, uint64_t id) {
    if (!m_preparing || id != m_resetRequest || context.epoch != m_prepareEpoch ||
	epoch != m_resetTarget) { fail({Fmt::ErrorCode::Output}); return; }
    m_resetCommitted = true;
    m_started = m_wireStarted = m_established = false;
    m_recovered = true;
    impl()->epochTx(context, epoch, previous, id);
    impl()->notifyTx(TxNotice{.context = context, .marker = m_drain,
      .kind = TxNoticeKind::ResetReady, .nextEpoch = epoch, .request = id});
  }

  void resetFailedTx(TxContext context, uint64_t epoch, uint64_t id, Fmt::Error error) {
    if (!m_preparing || id != m_resetRequest || context.epoch != m_prepareEpoch ||
	epoch != m_resetTarget) return;
    m_resetRequest = 0;
    m_preparing = false;
    fail(error);
  }

  void closeReady() {
    if (!Base::attached_() || m_faulted || !m_closeAfter || !outputIdle()) return;
    stopIdle();
    notice(TxNoticeKind::Drained);
    Base::detach_(Base::connection_());
  }

  void fail(Fmt::Error error) {
    ++m_faults;
    m_faulted = true;
    stopIdle();
    Base::hold_();
    impl()->faultTx(TxContext{Base::epoch_(), Base::connection_(), Base::next_()}, error);
  }

  void txPost(unsigned kind, uint64_t ticket) {
    if (kind == TxTask::Replay && !m_wireStarted) { m_replayTask = ticket; return; }
    impl()->txSchedule(kind, ticket);
  }

  unsigned loadTx(TxBody::Ref body, TxContext context, uint64_t ticket) {
    return impl()->associateTx(ZuMv(body), context, ticket);
  }

  bool unloadTx(TxBody::Ref body, TxContext context, uint64_t ticket) {
    return impl()->releaseTx(ZuMv(body), context, ticket);
  }

  TxReload historyTx(TxContext context, uint64_t ackd, uint64_t ticket) {
    return impl()->retrieveTx(context, ackd, ticket);
  }

  void limitTx(TxContext context, uint64_t maximum, unsigned headroom) {
    impl()->sequenceLimitTx(context, maximum, headroom);
  }

  void ackdTx(TxContext context, uint64_t previous, uint64_t next) {
    impl()->acknowledgedTx(context, previous, next);
  }

  void historyArchive(uint64_t epoch, uint64_t begin, uint64_t end) {
    impl()->archiveTx(epoch, begin, end);
  }

  ZuDateTime utcTx() { return impl()->clockTx(); }

  void errorTx(TxContext context, Fmt::Error error) {
    ++m_faults; impl()->faultTx(context, error);
  }

  bool frameTx(TxBody::Ref body, TxContext context, TxImage frame, ZuDateTime original) {
    if (m_resetArmed && body->msgType() == "A" && context.seqNo != 1) {
      fail({Fmt::ErrorCode::Value, 34}); return false;
    }
    if (!impl()->admitFrameTx(body, context, ZuMv(frame), original)) return false;
    if (!context.replay && body->msgType() == "A") m_resetArmed = false;
    ++m_inFlight;
    m_lastSend = impl()->nowTx();
    if (m_heartBtInt) {
      m_idleDeadline = m_lastSend + ZuTime{m_heartBtInt};
      armIdle();
    }
    if (!context.replay) {
      m_wireStarted = true;
      if (body->msgType() == "A") notice(TxNoticeKind::Logon, context);
      else if (body->msgType() == "1" && body->id())
	notice(TxNoticeKind::Probe, context, body->id());
      if (m_replayTask) {
	impl()->txSchedule(TxTask::Replay, m_replayTask);
	m_replayTask = 0;
      }
    }
    if (body->administrative()) impl()->sessionTx(body, context);
    impl()->publishedTx(ZuMv(body), context, original);
    impl()->flushTx();
    return true;
  }

  using Command = ZmRef<ControlQueue::Node>;
  // Immutable after construction.
  const ControlLimits	m_limits;
  // Tx thread exclusive; the enclosing Link aligns this complete owner group.
  ControlQueue		m_commands;
  Command		m_selected;
  TxBody::Ref		m_candidate;
  TxContext		m_candidateCtx;
  ZuTime		m_lastSend;
  ZuTime		m_idleDeadline;
  uint64_t		m_enqueueRefused = 0, m_faults = 0;
  uint64_t		m_idleTicket = 0;
  uint64_t		m_bytes = 0;
  uint64_t		m_change = 0;
  uint64_t		m_inFlight = 0;
  uint64_t		m_pumpTicket = 0;
  uint64_t		m_replayTask = 0;
  uint64_t		m_drain = 0; // nonzero while draining
  uint64_t		m_drainBoundary = 0; // nonzero after boundary notice
  uint64_t		m_prepareEpoch = 0;
  uint64_t		m_resetTarget = 0;
  uint64_t		m_resetRequest = 0;
  uint32_t		m_heartBtInt = 0;
  bool			m_drainProven = false;
  bool			m_preparing = false;
  bool			m_resetCommitted = false;
  bool			m_resetArmed = false;
  bool			m_idleArmed = false;
  bool			m_queued = false;
  bool			m_established = false;
  bool			m_started = false;
  bool			m_wireStarted = false;
  bool			m_pumpPosted = false;
  bool			m_recovered = true;
  bool			m_faulted = false;
  bool			m_closeAfter = false;
};

} // Zfix

#endif /* ZfixLinkTx_HH */
