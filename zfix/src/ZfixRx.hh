//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Managed FIX receive sequencing and lifecycle coordination

#ifndef ZfixRx_HH
#define ZfixRx_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/ZmPQueue.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZiIOBuf.hh>

#include <zlib/ZfixFrame.hh>
#include <zlib/ZfixRxMsg.hh>
#include <zlib/ZfixSession.hh>
#include <zlib/ZfixHooks.hh>
#include <zlib/ZfixNotice.hh>
#include <zlib/ZfixSeq.hh>
#include <zlib/ZfixLinkState.hh>
#include <zlib/ZfixStats.hh>

namespace Zfix {

// Exclusive cursors and range end, read from the link's Rx queue.
struct RxProgress {
  uint64_t	received = 1;
  uint64_t	processed = 1;
  uint64_t	gapBegin = 0;
  uint64_t	gapEnd = 0;
};

struct RxLimits {
  Fmt::Limits	codec;
  uint64_t	readAheadBytes = uint64_t(Fmt::Limits::FrameBytes)<<2;
  unsigned	readAheadCount = SeqLimits{}.readAheadCount;
  // Zero derives a cap from codec.frameBytes and the pool's builtin size.
  uint64_t	bufferBytes = 0;
};

namespace RxResult {

  enum { Accepted, Held, Blocked, Ignored, Refused, Overload };
}

ZfixExtern int rejectReason(Fmt::Error);

// All methods are Rx-only. The endpoint dispatches to this owner and supplies
// already framed input backed by one pooled buffer. Business decoding occurs
// only in sequence, through the same typed MessageHandler as standalone users.
//
// Impl hooks:
// eventRx(SessionEvent, ZmRef<Msg>) - fixed event + retained input handle;
// rejectRx(ZmRef<Msg>, tag, reason) - emit Session::Reject using input reference context;
// resendRx(begin, end, ZmRef<Msg>) - post a replay request to Tx;
// sessionRx(ZmRef<Msg>) - observe other ordered controls, including peer Session::Reject;
// businessRx(Msg *, Fmt::Limits) -> Fmt::Error - validate/decode before application dispatch;
// rxPost(ticket), gapArm(ticket), gapCancel() - owner scheduling hooks;
// nowRx() -> ZuTime - current injected monotonic time.
template <typename Impl> class Rx : private SeqRx<Rx<Impl>, RxQueue> {
  using Base = SeqRx<Rx<Impl>, RxQueue>;
friend Base;
public:
  using Msg = RxQueue::Node;
  using Span = RxQueue::Span;

  explicit Rx(SessionConfig config, RxLimits limits = {}, uint64_t epoch = 1) :
    Base{SeqLimits{config.valid() && config.mode != Transport::Raw ?
      config.sequenceLimit : SeqLimits{}.sequenceLimit,
      limits.readAheadBytes, limits.readAheadCount}},
    m_limits{limits}, m_session{ZuMv(config), epoch} { Base::hold_(); }

  Rx(const Rx &) = delete;
  Rx &operator =(const Rx &) = delete;

  const LinkState &session_() const { return m_session; }

  RxStats stats_() const {
    RxStats s{m_stats};
    s.queuedCount = Base::retained_(); s.queuedBytes = Base::retainedBytes_();
    s.pendingBytes = m_first ? m_first->data().buffer->size : 0;
    return s;
  }

  void framingError_() { ++m_stats.framingErrors; }

  using Base::expected_;
  using Base::retained_;
  using Base::retainedBytes_;

  void drain_(uint64_t ticket) { Base::drain_(ticket); recovered(); }

  using Base::retry_;

  RxProgress progress_() const {
    auto gap = Base::requested_();
    return {Base::received_(), Base::expected_(),
      gap.length() ? uint64_t(gap.key()) : 0,
      gap.length() ? uint64_t(gap.key()) + gap.length() : 0};
  }

  // Progress belongs to this link's Rx queue. Restore before opening activity.
  bool restore_(uint64_t epoch, const RxProgress &p) {
    if (!epoch || epoch != m_session.epoch_() ||
	m_session.config_().mode == Transport::Raw || m_session.connection_() ||
	m_session.state_() != State::Disconnected || m_first ||
	(p.gapBegin ? p.gapEnd <= p.gapBegin : p.gapEnd != 0)) return false;
    if (m_session.resetOnOpen_() && (p.received != 1 || p.processed != 1 || p.gapBegin))
      return false;
    return Base::restore_(p.received, p.processed,
      p.gapBegin ? Span{p.gapBegin, p.gapEnd - p.gapBegin} : Span{});
  }

  bool resetOnOpen_(uint64_t epoch, bool enabled = true) {
    if (enabled && (m_first || Base::retained_() ||
	Base::received_() != 1 || Base::expected_() != 1)) return false;
    return m_session.resetOnOpen_(epoch, enabled);
  }

  SessionEvent open_(ZuTime now) {
    return opened(m_session.open_(now));
  }

  SessionEvent bind_(bool replace) {
    // The enclosing link sends this distinct handoff to Tx; it is not a
    // Disconnected protocol event or a whole-link stop.
    return bound(m_session.bind_(replace));
  }

  SessionEvent start_(ZuTime now) {
    auto e = m_session.start_(now); emit(e); return e;
  }

  SessionEvent replace_(ZuTime now) {
    return opened(m_session.replace_(now));
  }

  void logonSent_(uint64_t connection, uint64_t seqNo, ZuTime now) {
    apply(m_session.logonSent_(connection, seqNo, now));
  }

  void admit_(uint64_t connection, uint64_t ticket, bool accepted, ZuTime now) {
    apply(m_session.admit_(connection, ticket, accepted, now));
  }

  void timer_(uint64_t connection, unsigned timer, ZuTime now) {
    apply(m_session.timer_(connection, timer, now));
  }

  void fault_(uint64_t connection, unsigned error, unsigned tag,
      ZuTime now, bool logout = false) {
    apply(m_session.fault_(connection, error, tag, now, logout));
  }

  void lost_(uint64_t connection) { apply(m_session.lost_(connection)); }

  void outputFailed_(uint64_t connection, uint64_t epoch) {
    if (epoch == m_session.epoch_()) apply(m_session.outputFailed_(connection));
  }

  void logout_(uint64_t connection, ZuTime now) { apply(m_session.logout_(connection, now)); }

  bool drainBegin_(uint64_t connection, ZuTime now) {
    auto e = m_session.drain_(connection, now);
    bool accepted = e.actions & SessionAction::Drain;
    if (accepted && !impl()->workIdleRx()) {
      auto recovery = m_session.recovery_(connection, false, m_session.txDone_(), now);
      recovery.actions |= e.actions; e = recovery;
    }
    apply(e); return accepted;
  }

  bool prepareReset_(uint64_t connection, uint64_t epoch, uint64_t nextEpoch,
      bool initiator, ZuTime now, unsigned path = ResetPath::Live) {
    if (m_first || !rxDone()) return false;
    auto e = m_session.resetPrepare_(connection, epoch, nextEpoch, initiator, now, path);
    if (!(e.actions & SessionAction::ResetPrepare)) return false;
    Base::hold_(); emit(e); return true;
  }

  bool startReset_(uint64_t connection, uint64_t epoch, ZuTime now) {
    if (epoch != m_session.epoch_()) return false;
    auto e = m_session.resetStart_(connection, now);
    if (!(e.actions & SessionAction::Changed)) return false;
    apply(e); return true;
  }

  bool disconnectReset_(uint64_t connection, uint64_t epoch) {
    if (epoch != m_session.epoch_()) return false;
    auto e = m_session.resetDisconnect_(connection);
    if (!(e.actions & SessionAction::Close)) return false;
    apply(e); return true;
  }

  void epochRx(uint64_t, uint64_t, uint64_t, RxProgress) { }

  void probe_(uint64_t connection, ZuTime now) { apply(m_session.probe_(connection, now)); }

  void txRecovery_(uint64_t connection, bool done, ZuTime now) {
    if (connection != m_session.connection_() || m_session.state_() != State::Established) return;
    emit(m_session.recovery_(connection, rxDone(), done, now));
  }

  void workDone_() { recovered(); }

  bool workIdleRx() const { return true; }

  void closed_(uint64_t connection) { apply(m_session.closed_(connection)); }

  void notice_(const TxNotice &notice, ZuTime now, bool associated = false) {
    const auto &context = notice.context;
    if (context.epoch != m_session.epoch_() ||
	(context.connection != m_session.connection_() &&
	  notice.kind != TxNoticeKind::ResetReady && notice.kind != TxNoticeKind::ResetAborted))
      return;
    switch (notice.kind) {
      case TxNoticeKind::ResetAborted: {
	if (!m_session.resetMatches_(context.connection, context.epoch,
	    notice.nextEpoch, notice.marker)) break;
	auto e = m_session.resetAbort_(context.connection, notice.nextEpoch, notice.marker);
	e.ticket = notice.request;
	apply(e);
	break;
      }
      case TxNoticeKind::ResetReady: {
	if (!notice.request || notice.loaded != 1 || notice.published != 1 ||
	    notice.acknowledged != 1 || !m_session.resetMatches_(context.connection,
	      context.epoch, notice.nextEpoch, notice.marker)) break;
	auto previous = progress_();
	if (!Base::reset_(1)) {
	  apply(m_session.fault_(context.connection, SessionError::Protocol, 34, now, false));
	  break;
	}
	auto e = m_session.resetReady_(context.connection, notice.nextEpoch, now, associated);
	e.ticket = notice.request;
	m_logonGap = 0;
	impl()->epochRx(m_session.connection_(), context.epoch, notice.nextEpoch, previous);
	emit(e);
	break;
      }
      case TxNoticeKind::Logon: logonSent_(context.connection, context.seqNo, now); break;
      case TxNoticeKind::Probe:
	apply(m_session.probeSent_(context.connection, notice.marker, context.seqNo, now));
	break;
      case TxNoticeKind::Recovery: txRecovery_(context.connection, notice.done, now); break;
      case TxNoticeKind::DrainBoundary:
	apply(m_session.drainTx_(context.connection, notice.marker, notice.loaded, now)); break;
      case TxNoticeKind::Drained: apply(m_session.flushed_(context.connection)); break;
      default: break;
    }
  }

  // Blocked is the sole non-consuming result. The connection pauses ingress
  // and retains that frame/remaining coalesced bytes until admission completes;
  // unadmitted input never enters the continuing session's sequence queue.
  unsigned receive_(uint64_t connection, ZmRef<Msg> &msg, ZuTime now, ZuDateTime utc) {
    auto &observed = msg->data();
    if (!observed.observed) {
      observed.observed = true; ++m_stats.frames; m_stats.bytes += observed.frame.wire.length();
    }
    if (m_session.config_().mode != Transport::Raw) {
      if (connection == m_session.connection_() &&
	  (m_session.state_() == State::Resetting || m_session.resetPreparing_()))
	return RxResult::Blocked;
      auto &input = msg->data();
      if (!*input.received) {
	input.received = now; input.utc = utc;
	input.connection = connection; input.epoch = m_session.epoch_();
      } else if (input.connection != connection || input.epoch != m_session.epoch_()) {
	++m_stats.ignored; msg = nullptr; return RxResult::Ignored;
      }
      if (m_first && connection == m_session.connection_()) return RxResult::Blocked;
    }
    return receive(connection, ZuMv(msg), now, utc);
  }

  void stateChangeRx(SessionEvent) { }
  void peerRejectRx(PeerReject) { }
  bool rawRx(uint64_t, ZmRef<Msg>, ZuTime, ZuDateTime) { return false; }
  Fmt::Error businessRx(Msg *, Fmt::Limits) { return {Fmt::ErrorCode::Unresolved, 35}; }

  Fmt::Error headerRx(const Fmt::FrameView &frame, Fmt::Header &header, Fmt::Limits limits) {
    return Fmt::readHeader(frame, header, limits);
  }

private:
  SessionEvent bound(SessionEvent e) {
    if (!(e.actions & (SessionAction::Refuse | SessionAction::Bypass)) &&
	e.connection && (e.actions & SessionAction::Pause)) {
      Base::hold_(); m_first = nullptr;
      m_logonGap = 0;
    }
    return e;
  }

  SessionEvent opened(SessionEvent e) {
    bound(e);
    emit(e);
    return e;
  }

  unsigned receive(uint64_t connection, ZmRef<Msg> msg, ZuTime now, ZuDateTime utc) {
    if (m_session.config_().mode == Transport::Raw)
      return impl()->rawRx(connection, ZuMv(msg), now, utc) ?
	RxResult::Accepted : RxResult::Refused;
    auto state = m_session.state_();
    if (connection != m_session.connection_() || state == State::Disconnected ||
	state == State::Closing) { ++m_stats.ignored; return RxResult::Ignored; }
    auto &input = msg->data();
    uint64_t cap = m_limits.bufferBytes;
    if (!cap) {
      cap = m_limits.codec.frameBytes;
      if (cap < ZiIOBufAlloc<>::Size) cap = ZiIOBufAlloc<>::Size;
    }
    if (!input.buffer || input.buffer->size > cap ||
	input.frame.wire.length() > m_limits.codec.frameBytes) {
      apply(m_session.fault_(connection, SessionError::Overload, 0, now, false));
      return RxResult::Overload;
    }
    if (!input.nativeMemory) input.charge = uint64_t(input.buffer->size) + sizeof(Msg);
    if (Session::type(input.frame.msgType))
      input.error = Session::read(input.frame, input.control, input.header,
	input.trailer, m_limits.codec);
    else {
      input.trailer = input.frame.trailer;
      input.error = impl()->headerRx(input.frame, input.header, m_limits.codec);
    }
    if (input.error) ++m_stats.decodeErrors;
    input.fault = m_session.validateHeader_(input.header, input.utc);
    if (!input.header.seqNo) {
      bool logout = state == State::Established &&
	!m_session.config_().header.mismatch(input.header);
      apply(m_session.fault_(connection, SessionError::Sequence, 34, now, logout));
      return RxResult::Refused;
    }
    if (input.error.code == Fmt::ErrorCode::Pair ||
	input.error.code == Fmt::ErrorCode::Truncated) {
      apply(m_session.fault_(connection, SessionError::Protocol, input.error.tag, now, false));
      return RxResult::Refused;
    }
    if (state == State::Accepting || state == State::Logon) {
      if (!m_first) {
	if (!input.control.template is<Session::Logon>() || input.error || input.fault) {
	  bool logout = input.header.msgType == "A" &&
	    input.fault.error != SessionError::Identity;
	  apply(m_session.fault_(connection, input.fault ? input.fault.error :
	    SessionError::Type, input.fault ? input.fault.tag : input.error.tag,
	    now, logout));
	  return RxResult::Refused;
	}
	m_first = msg;
	first();
	return m_first ? RxResult::Held : m_session.state_() == State::Established ?
	  RxResult::Accepted : RxResult::Refused;
      }
    }
    return queue(ZuMv(msg));
  }

  auto impl() { return static_cast<Impl *>(this); }
  auto impl() const { return static_cast<const Impl *>(this); }

  void reject(ZmRef<Msg> msg, unsigned tag, int reason) {
    ++m_stats.rejectsOut; impl()->rejectRx(ZuMv(msg), tag, reason);
  }

  void emit(SessionEvent e, Msg *msg = nullptr) {
    if (e.actions & SessionAction::Failure) {
      ++m_stats.faults; if (e.error == SessionError::Overload) ++m_stats.overloads;
    }
    if (e.actions & SessionAction::RequestGap) ++m_stats.gaps;
    if (e.actions & SessionAction::Changed) impl()->stateChangeRx(e);
    if (e.actions) impl()->eventRx(e, ZmRef<Msg>{msg});
  }

  void apply(SessionEvent e, Msg *input = nullptr) {
    if (e.state == State::Closing || e.state == State::Disconnected) {
      Base::hold_(); m_first = nullptr;
    }
    emit(e, input);
    if ((e.actions & SessionAction::RetryInput) && m_first) first();
  }

  void first() {
    auto msg = m_first;
    auto &input = msg->data();
    auto e = m_session.peerLogon_(m_session.connection_(), input.header,
      input.control.template p<Session::Logon>(), Base::expected_(), impl()->nowRx());
    if (e.state == State::Established) {
      e.actions &= ~SessionAction::RequestGap; // ZmPQRx owns gap notifications.
      if (m_session.config_().mode == Transport::FIX44A && input.key() > Base::expected_())
	m_logonGap = input.key();
    }
    emit(e, msg);
    if (e.state == State::Closing || e.state == State::Logout) {
      Base::hold_(); m_first = nullptr; return;
    }
    if (e.state != State::Established) return;
    input.early = true;
    m_first = nullptr;
    bool immediate = !Base::retained_() && input.key() == Base::expected_();
    if (immediate) Base::resume_();
    auto result = Base::receive_(ZuMv(msg));
    if (result != SeqResult::Accepted) {
      apply(m_session.fault_(m_session.connection_(), SessionError::Overload, 0,
	impl()->nowRx(), false));
      return;
    }
    if (!immediate) Base::resume_();
    recovered();
  }

  unsigned queue(ZmRef<Msg> msg) {
    auto &input = msg->data();
    bool reset = input.control.template is<Session::SeqReset>() &&
      !input.control.template p<Session::SeqReset>().gapFill && !input.error && !input.fault;
    if (reset) { resetMode(msg); recovered(); return RxResult::Accepted; }
    if (input.key() < Base::expected_()) {
      if (!input.header.possDup) {
	apply(m_session.fault_(m_session.connection_(), SessionError::Sequence, 34,
	  impl()->nowRx(), true));
	return RxResult::Refused;
      }
      if (input.fault) {
	reject(msg, input.fault.tag, input.fault.reason);
	if (input.fault.fatal) apply(m_session.fault_(m_session.connection_(),
	  input.fault.error, input.fault.tag, impl()->nowRx(), true));
      }
      ++m_stats.duplicates; ++m_stats.ignored; return RxResult::Ignored;
    }
    bool high = input.key() > Base::expected_();
    if (high && input.fault && input.fault.fatal) {
      apply(m_session.fault_(m_session.connection_(), input.fault.error,
	input.fault.tag, impl()->nowRx(), true));
      return RxResult::Refused;
    }
    m_receiving = true;
    auto result = high ? Base::receive_(msg) : Base::receive_(ZuMv(msg));
    m_receiving = false;
    switch (result) {
      case SeqResult::Accepted:
	if (high && m_session.state_() == State::Established) {
	  emit(m_session.recovery_(m_session.connection_(), false,
	    m_session.txDone_(), impl()->nowRx()));
	  if (!input.error && !input.fault) early(msg);
	}
	if (m_deferredGap.length()) {
	  auto gap = m_deferredGap; m_deferredGap = {};
	  requestRx({}, gap);
	}
	recovered();
	return RxResult::Accepted;
      case SeqResult::Duplicate:
	++m_stats.duplicates; ++m_stats.ignored; return RxResult::Ignored;
      default:
	apply(m_session.fault_(m_session.connection_(), SessionError::Overload, 0,
	  impl()->nowRx(), false));
	return RxResult::Overload;
    }
  }

  void early(Msg *msg) {
    auto &input = msg->data();
    auto &control = input.control;
    switch (control.type()) {
      case Session::Msg::Index<Session::ResendReq>{}: {
	auto &request = control.template p<Session::ResendReq>();
	if (request.endSeqNo && request.endSeqNo < request.beginSeqNo) return;
	resend(request.beginSeqNo, request.endSeqNo, msg);
	break;
      }
      case Session::Msg::Index<Session::HeartBt>{}:
	emit(m_session.heartbeat_(m_session.connection_(),
	  control.template p<Session::HeartBt>().testReqID, input.received), msg);
	break;
      case Session::Msg::Index<Session::Logout>{}: {
	auto e = m_session.peerLogoutEarly_(m_session.connection_());
	emit(e, msg);
	input.logoutReply = bool(e.actions & SessionAction::Observe);
	return; // Ordered disposition must still complete the close.
      }
      default: return;
    }
    input.early = true;
  }

  void resetMode(Msg *msg) {
    auto &input = msg->data();
    auto next = input.control.template p<Session::SeqReset>().newSeqNo;
    if (next < Base::expected_() || !Base::advance_(next)) {
      reject(ZmRef<Msg>{msg}, 36, 5); return;
    }
    impl()->sessionRx(ZmRef<Msg>{msg});
  }

  void processRx(Msg *msg) {
    auto &input = msg->data();
    if (input.epoch != m_session.epoch_()) return;
    if (input.fault || input.error) {
      unsigned tag = input.fault ? input.fault.tag : input.error.tag;
      int reason = input.fault ? input.fault.reason : rejectReason(input.error);
      if (input.error && input.error.tag == tag) reason = rejectReason(input.error);
      reject(ZmRef<Msg>{msg}, tag, reason);
      if (input.fault.fatal) apply(m_session.fault_(input.connection,
	input.fault.error, tag, impl()->nowRx(), true));
      return;
    }
    m_session.activity_(input.connection, input.received);
    if (input.early) return;
    auto &control = input.control;
    switch (control.type()) {
      case Session::Msg::Index<Session::HeartBt>{}:
	emit(m_session.heartbeat_(input.connection,
	  control.template p<Session::HeartBt>().testReqID, input.received), msg);
	break;
      case Session::Msg::Index<Session::TestReq>{}:
	emit(m_session.testRequest_(input.connection, input.received), msg);
	break;
      case Session::Msg::Index<Session::ResendReq>{}: {
	auto &request = control.template p<Session::ResendReq>();
	if (request.endSeqNo && request.endSeqNo < request.beginSeqNo)
	  reject(ZmRef<Msg>{msg}, 16, 5);
	else resend(request.beginSeqNo, request.endSeqNo, msg);
	break;
      }
      case Session::Msg::Index<Session::SeqReset>{}: {
	auto &reset = control.template p<Session::SeqReset>();
	if (reset.newSeqNo <= input.key() || !Base::advance_(reset.newSeqNo))
	  reject(ZmRef<Msg>{msg}, 36, 5);
	else impl()->sessionRx(ZmRef<Msg>{msg});
	break;
      }
      case Session::Msg::Index<Session::Logout>{}: {
	if (input.connection == m_session.connection_()) {
	  auto e = m_session.peerLogout_(m_session.connection_(), impl()->nowRx());
	  if (input.logoutReply) e.actions &= ~SessionAction::Observe;
	  apply(e, msg);
	} else if (!input.logoutReply) impl()->sessionRx(ZmRef<Msg>{msg});
	break;
      }
      case Session::Msg::Index<Session::Logon>{}: reject(ZmRef<Msg>{msg}, 35, 11); break;
      case Session::Msg::Index<Session::Reject>{}:
	++m_stats.rejectsIn;
	impl()->sessionRx(ZmRef<Msg>{msg});
	impl()->peerRejectRx(PeerReject{.buffer = input.buffer,
	  .context = {input.epoch, input.connection, input.header.seqNo},
	  .message = control.template p<Session::Reject>(), .wire = input.frame.wire,
	  .possDup = input.header.possDup});
	break;
      default:
	if (auto error = impl()->businessRx(msg, m_limits.codec)) {
	  ++m_stats.decodeErrors; ++m_stats.hookFailures;
	  input.error = error;
	  reject(ZmRef<Msg>{msg}, error.tag, rejectReason(error));
	}
    }
  }

  void resend(uint64_t begin, uint64_t end, Msg *msg) {
    ++m_stats.replays;
    emit(m_session.recovery_(m_session.connection_(), rxDone(), false, impl()->nowRx()));
    impl()->sessionRx(ZmRef<Msg>{msg});
    impl()->resendRx(begin, end, ZmRef<Msg>{msg});
  }

  bool rxDone() const {
    return !Base::retained_() && Base::expected_() == Base::received_() &&
      (!m_session.draining_() || impl()->workIdleRx());
  }

  void recovered() {
    if (m_session.state_() != State::Established) return;
    if (m_logonGap && Base::expected_() > m_logonGap) m_logonGap = 0;
    bool done = rxDone();
    // Ready already proves completed Rx/Tx recovery. Steady in-order input
    // cannot change it; gaps, drain and initial recovery take the full path.
    if (done && m_session.ready_()) return;
    emit(m_session.recovery_(m_session.connection_(), done,
      m_session.txDone_(), impl()->nowRx()));
  }

  void requestGap(const Span &gap, bool force) {
    if (m_session.state_() != State::Established) return;
    auto e = m_session.recovery_(m_session.connection_(), false,
      m_session.txDone_(), impl()->nowRx());
    if (force || !m_logonGap || gap.key() + gap.length() > m_logonGap) {
      e.actions |= SessionAction::RequestGap;
      e.gapBegin = gap.key(); e.gapEnd = gap.key() + gap.length() - 1;
    }
    emit(e);
  }

  void requestRx(const Span &, const Span &gap) {
    if (m_receiving) { m_deferredGap = gap; return; }
    requestGap(gap, false);
  }

  void reRequestRx(const Span &gap) { requestGap(gap, true); }

  void postRx(uint64_t ticket) { impl()->rxPost(ticket); }

  void armGap(uint64_t ticket) { impl()->gapArm(ticket); }

  void cancelGap() { impl()->gapCancel(); }

  const RxLimits	m_limits;

  // Rx-owned input, lifecycle and statistics.
  RxCounters		m_stats;
  LinkState		m_session;
  ZmRef<Msg>		m_first;
  Span			m_deferredGap;
  uint64_t		m_logonGap = 0;
  bool			m_receiving = false;
};

} // Zfix

#endif /* ZfixRx_HH */
