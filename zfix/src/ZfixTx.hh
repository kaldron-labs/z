//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Tx-owned association, wire publication and replay

#ifndef ZfixTx_HH
#define ZfixTx_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/ZtEnum.hh>

#include <zlib/ZfixNotice.hh>
#include <zlib/ZfixSeq.hh>
#include <zlib/ZfixLinkState.hh>
#include <zlib/ZfixCheckpoint.hh>
#include <zlib/ZfixTxMsg.hh>
#include <zlib/ZfixStats.hh>

namespace Zfix {

ZtEnumNS(ZfixAPI, TxLoad, int8_t, Refused, Accepted, Pending);
ZtEnumNS(ZfixAPI, TxSelect, int8_t,
  Accepted, Pending, Refused, Held, Busy, Overload, Limit, Invalid);

struct TxHistory {
  TxBody::Ref	body;
  ZuDateTime	originalTime;
  uint64_t	epoch = 0;
  uint64_t	seqNo = 0;

  bool operator !() const { return !body; }
};

using TxReload = SeqReload<TxHistory>;
struct TxOutput {
  TxHistory	history;
  uint64_t	charge = 0;
  // Zero for restored records whose original process/connection no longer exists.
  uint64_t	connection = 0;

  uint64_t key() const { return history.seqNo; }
  uint64_t length() const { return 1; }
  uint64_t wireBytes() const { return charge; }
};

using TxQueue = ZmPQueue<TxOutput, ZmPQueueHeapID<"Zfix.Tx",
  ZmPQueueNode<ZmObject, ZmPQueueOverlap<false, ZmPQueueOverwrite<false>>>>>;
struct TxLimits {
  Fmt::Limits	codec;
  uint64_t	queuedOutputBytes = uint64_t(Fmt::Limits::FrameBytes)<<4;
  uint64_t	adminReserveBytes = Fmt::Limits::FrameBytes;
  unsigned	replayBatch = 64;
  unsigned	controlHeadroom = 1024;
};

// Tx owns its durable key, header configuration, mode and sequence domain. Rx clocks,
// role and heartbeat policy stay in SessionConfig on the Rx owner.
struct TxConfig {
  SessionString	id;
  HeaderConfig	header;
  unsigned	appVerID;
  uint64_t	sequenceLimit;
  unsigned	mode;
};

ZfixExtern TxConfig txConfig(SessionConfig);
// Returned spans borrow config; retain it until the frame has been encoded.
ZfixExtern Fmt::Header txHeader(const TxConfig &, uint64_t seqNo,
  ZuDateTime now, ZuDateTime original = {});

// All entry points run on Tx. select_ receives already selected, encoded work,
// encoded with this owner's effective codec limits, never an off-shard application
// submission. The application retains ownership
// of its unsequenced queue. Header strings belong to this owner's config.
//
// Mandatory Impl hooks:
// loadTx_(TxBody::Ref, TxContext, ticket) -> TxLoad; asynchronous completion
//   invokes loaded_ on Tx after the hook returns;
// unloadTx_(TxBody::Ref, TxContext, ticket) -> bool completed; false requires
//   unloaded_ on Tx before replacing a stale, uncommitted load candidate;
// frameTx_(TxBody::Ref, TxContext, ZmRef<ZiIOBuf>, ZuDateTime original) -> bool;
//   true transfers the complete frame to transport; false retains no bytes;
// historyTx_(TxContext, acknowledged, ticket) -> TxReload; deferred completion
//   invokes reloaded_; historyArchive_(epoch, begin, end) reports archive ranges;
// txPost_(kind, ticket) posts run_ back to Tx; utcTx_() samples SendingTime;
// errorTx_(TxContext, Fmt::Error) reports an unpublished frame failure.
// Optional ackdTx_(owner context, previous, next) reports exclusive progress;
// context.seqNo is zero; history retention stays with the application.
// Optional limitTx_(next context, maximum, control headroom) reports entering the
// reserved control range once per epoch. Post policy work; do not reenter Tx.
//
// Retained handles + fixed metadata may cross to the association owner. No
// mutable queue node escapes Tx. load identifies the exact body before any
// frame can be published; sent completion never acknowledges peer progress.
template <typename Impl> class Tx : private SeqTx<Tx<Impl>, TxQueue, TxHistory> {
  using Base = SeqTx<Tx<Impl>, TxQueue, TxHistory>;
friend Base;
public:
  using Msg = TxQueue::Node;
  using Span = TxQueue::Span;

  explicit Tx(SessionConfig config, TxLimits limits = {}, uint64_t epoch = 1) :
    Base{SeqTxLimits{config.sequenceLimit,
      limits.queuedOutputBytes + limits.adminReserveBytes, limits.replayBatch}},
    m_config{txConfig(ZuMv(config))}, m_limits{limits}, m_epoch{epoch} {
    ZmAssert(epoch &&
      limits.adminReserveBytes <= UINT64_MAX - limits.queuedOutputBytes &&
      limits.controlHeadroom <= m_config.sequenceLimit);
    Base::hold_();
  }

  TxStats stats_() const {
    TxStats s{m_stats};
    s.operationalCount = Base::retained_(); s.operationalBytes = Base::queuedBytes_();
    return s;
  }

  Tx(const Tx &) = delete;
  Tx &operator =(const Tx &) = delete;

  using Base::next_;
  using Base::published_;
  using Base::acknowledged_;
  using Base::queuedBytes_;
  using Base::retained_;
  using Base::failed_;
  using Base::waiting_;
  using Base::replaying_;
  using Base::hold_;

  bool abortTx_(uint64_t epoch, uint64_t key) {
    if (epoch != m_epoch || m_pending || resetting_()) return false;
    auto msg = Base::assigned_(key);
    if (msg && msg->data().history.body->msgType() == "A" &&
	*msg->data().history.originalTime) return false;
    return Base::abort_(key);
  }

  uint64_t epoch_() const { return m_epoch; }
  uint64_t connection_() const { return m_connection; }
  bool loading_() const { return m_pending.ptr(); }
  bool resetting_() const { return m_resetEpoch; }
  bool attached_() const { return m_attached; }
  const TxLimits &limits() const { return m_limits; }

  bool resetReady_() const {
    return !m_ready && !m_pending && !resetting_() && Base::epochReady_();
  }

  bool acknowledge_(uint64_t epoch, uint64_t next) {
    if (epoch != m_epoch || resetting_()) return false;
    auto previous = Base::acknowledged_();
    if (!Base::acknowledge_(next)) return false;
    if (next > previous)
      impl()->ackdTx_(TxContext{m_epoch, m_connection, 0}, previous, next);
    return true;
  }

  bool replay_(uint64_t epoch, uint64_t begin, uint64_t end = 0) {
    return epoch == m_epoch && !resetting_() && Base::replay_(begin, end);
  }

  // Owner execution only after protocol agreement and transport quiescence.
  // Application history remains in its own epoch; no operational work is discarded.
  bool reset_(uint64_t epoch, uint64_t nextEpoch) {
    if (epoch != m_epoch || nextEpoch <= epoch || m_ready || m_pending || resetting_())
      return false;
    auto previous = progress_();
    if (!Base::reset_(1)) return false;
    finishReset(nextEpoch, previous);
    return true;
  }

  // Only after explicit reset agreement and ingress/transport preparation.
  // Completion is posted so the coordinator first records its request ID.
  uint64_t resetBegin_(uint64_t epoch, uint64_t nextEpoch) {
    if (epoch != m_epoch || nextEpoch <= epoch || m_ready || m_pending ||
	resetting_() || !Base::epochIdle_()) return 0;
    m_resetProgress = progress_();
    m_resetEpoch = nextEpoch;
    m_resetID = ++m_ticket;
    postReset();
    return m_resetID;
  }

  bool cancelReset_(uint64_t epoch, uint64_t id) {
    if (epoch != m_epoch || !resetting_() || !id || id != m_resetID) return false;
    failReset({Fmt::ErrorCode::Output});
    return true;
  }

  void resetFailedTx_(TxContext context, uint64_t, uint64_t, Fmt::Error error) {
    impl()->errorTx_(context, error);
  }

  void resetTx_(TxContext, uint64_t, TxProgress, uint64_t) { }
  // Optional ordered notification, independent of application archive policy.
  void ackdTx_(TxContext, uint64_t, uint64_t) { }
  void limitTx_(TxContext, uint64_t, unsigned) { }

  bool resume_() {
    if (!m_attached || resetting_()) return false;
    if (!Base::running_()) {
      auto latest = Base::latest_();
      if (latest && latest->data().connection == m_connection &&
	  latest->data().history.body->msgType() == "A" &&
	  latest->data().history.seqNo > Base::published_()) {
	// Owner posts are asynchronous. Establish running before the external
	// admission hook so a hook's hold/detach remains authoritative.
	auto connection = m_connection, epoch = m_epoch;
	Base::resume_();
	if (!publishTx_(latest, false)) {
	  if (connection == m_connection && epoch == m_epoch) Base::hold_();
	  return false;
	}
	if (epoch == m_epoch) Base::admitted_(latest->data().history.seqNo + 1);
	return m_attached && connection == m_connection && epoch == m_epoch &&
	  !resetting_() && Base::running_();
      }
    }
    Base::resume_();
    return true;
  }

  void run_(unsigned kind, uint64_t ticket) {
    if (kind == TxTask::Reset) {
      if (!resetting_() || !m_resetTicket || ticket != m_resetTicket) return;
      m_resetTicket = 0;
      resetNext();
      return;
    }
    Base::run_(kind, ticket);
  }

  // Attachment does not start publication. The session coordinator separately
  // resumes the queue after establishing its reconnect/recovery boundary.
  bool attach_(uint64_t connection) {
    if (!connection || connection <= m_connection || m_attached || resetting_())
      return false;
    return attach(connection);
  }

  // A validated physical replacement preserves the agreed logical reset.
  // Advance the existing connection authority before old send completions run.
  bool replace_(uint64_t connection) {
    if (!m_attached || !connection || connection <= m_connection) return false;
    return attach(connection);
  }

  bool detach_(uint64_t connection) {
    if (!m_attached || connection != m_connection) return false;
    if (resetting_()) failReset({Fmt::ErrorCode::Output});
    Base::hold_();
    m_attached = m_ready = false;
    return true;
  }

  bool ready_() const { return m_ready; }

  bool ready_(uint64_t connection, bool value) {
    if (!m_attached || connection != m_connection) return false;
    if (value && resetting_()) return false;
    m_ready = value;
    return true;
  }

  unsigned select_(TxBody::Ref body) {
    if (!body || !m_attached) return TxSelect::Invalid;
    if (m_pending || resetting_()) return TxSelect::Busy;
    if (!body->administrative() && !m_ready) return TxSelect::Held;
    auto next = Base::next_();
    if (next > m_config.sequenceLimit || (!body->administrative() &&
	next > m_config.sequenceLimit - m_limits.controlHeadroom)) return TxSelect::Limit;
    uint64_t charge = body->size + sizeof(Msg);
    if (!room(body, charge)) return TxSelect::Overload;
    m_pending = new Msg{TxOutput{TxHistory{ZuMv(body), {}, m_epoch, next}, charge, m_connection}};
    auto context = pendingContext();
    auto token = m_loadTicket = ++m_ticket;
    m_unloading = false;
    switch (impl()->loadTx_(m_pending->data().history.body, context, token)) {
      case TxLoad::Pending: return TxSelect::Pending;
      case TxLoad::Accepted: return accept();
      default: ++m_stats.loadRefused; m_pending = nullptr; return TxSelect::Refused;
    }
  }

  unsigned loaded_(uint64_t token, bool accepted) {
    if (!m_pending || m_unloading || token != m_loadTicket) return TxSelect::Invalid;
    if (!accepted) { ++m_stats.loadRefused; m_pending = nullptr; return TxSelect::Refused; }
    return accept();
  }

  bool unloaded_(uint64_t token) {
    if (!m_pending || !m_unloading || token != m_loadTicket) return false;
    return finishUnload();
  }

  bool reloaded_(uint64_t token, TxHistory history) {
    return Base::completeReload_(token, ZuMv(history));
  }

  TxProgress progress_() const {
    auto replay = Base::replayRange_();
    return {Base::next_(), Base::published_(), Base::acknowledged_(),
      replay.length() ? uint64_t(replay.key()) : 0,
      replay.length() ? uint64_t(replay.key()) + replay.length() : 0};
  }

  bool snapshot_(Checkpoint &state) const {
    if (!stampState(state, m_config.id, m_epoch)) return false;
    state.tx = progress_();
    state.fields |= StateMask::Tx;
    return true;
  }

  bool restore_(const Checkpoint &state) {
    if (m_connection || m_attached || m_pending || resetting_() ||
	!stateMatches(state, m_config.id, m_epoch)) return false;
    if (!(state.fields & StateMask::Tx)) return true;
    TxProgress p;
    if (!restoreTx(state, progress_(), m_config.sequenceLimit, p)) return false;
    return restore_(state.epoch, p.loaded, p.sent, p.acknowledged,
      p.replayBegin ? Span{p.replayBegin, p.replayEnd - p.replayBegin} : Span{});
  }

  bool restore_(uint64_t epoch, uint64_t loaded, uint64_t published,
      uint64_t ackd, Span replay = {}) {
    if (epoch != m_epoch || m_connection || m_attached || m_pending || resetting_()) return false;
    return Base::restore_(loaded, published, ackd, replay);
  }

private:
  bool attach(uint64_t connection) {
    Base::hold_();
    m_connection = connection;
    m_attached = true;
    m_ready = false;
    checkLimit();
    return true;
  }

  void checkLimit() {
    auto next = Base::next_();
    if (m_limitNotified || next <= m_config.sequenceLimit - m_limits.controlHeadroom) return;
    m_limitNotified = true;
    impl()->limitTx_(TxContext{m_epoch, m_connection, next},
      m_config.sequenceLimit, m_limits.controlHeadroom);
  }

  auto impl() { return static_cast<Impl *>(this); }

  void finishReset(uint64_t nextEpoch, TxProgress previous) {
    auto epoch = m_epoch;
    auto id = m_resetID;
    m_epoch = nextEpoch;
    m_limitNotified = false;
    m_resetTicket = m_resetEpoch = m_resetID = 0;
    impl()->resetTx_(TxContext{epoch, m_connection, 0}, nextEpoch, previous, id);
    if (m_attached) checkLimit();
  }

  void postReset() {
    m_resetTicket = ++m_ticket;
    impl()->txPost_(TxTask::Reset, m_resetTicket);
  }

  void failReset(Fmt::Error error) {
    auto nextEpoch = m_resetEpoch;
    auto id = m_resetID;
    m_resetTicket = m_resetEpoch = m_resetID = 0;
    impl()->resetFailedTx_(TxContext{m_epoch, m_connection, 0}, nextEpoch, id, error);
  }

  void resetNext() {
    if (!Base::reset_(1)) { failReset({Fmt::ErrorCode::Output}); return; }
    finishReset(m_resetEpoch, m_resetProgress);
  }

  TxContext pendingContext() const {
    const auto &item = m_pending->data();
    return {item.history.epoch, item.connection, item.history.seqNo};
  }

  bool finishUnload() {
    m_pending = nullptr;
    m_unloading = false;
    return true;
  }

  bool validReloadTx_(const TxHistory &history, uint64_t key, bool replay) {
    if (history.body && history.epoch == m_epoch && history.seqNo == key &&
	key && key < Base::next_() && (!replay || *history.originalTime)) return true;
    impl()->errorTx_(TxContext{m_epoch, m_connection, key, replay},
      {Fmt::ErrorCode::Value, 34});
    return false;
  }

  ZmRef<Msg> reloadMsgTx_(TxHistory history) {
    uint64_t charge = history.body->size + sizeof(Msg);
    if (!room(history.body, charge)) {
      impl()->errorTx_(TxContext{m_epoch, m_connection, history.seqNo},
	{Fmt::ErrorCode::Size});
      return nullptr;
    }
    history.originalTime.null();
    return new Msg{TxOutput{ZuMv(history), charge, m_connection}};
  }

  bool room(const TxBody::Ref &body, uint64_t charge) const {
    uint64_t bytes = Base::queuedBytes_();
    if (m_pending) bytes += m_pending->data().charge;
    uint64_t limit = m_limits.queuedOutputBytes +
      (body->administrative() ? m_limits.adminReserveBytes : 0);
    return charge <= limit && bytes <= limit - charge;
  }

  unsigned accept() {
    auto context = pendingContext();
    if (context.epoch != m_epoch ||
	(m_pending->data().history.body->administrative() &&
	 (!m_attached || context.connection != m_connection))) {
      m_unloading = true;
      if (impl()->unloadTx_(m_pending->data().history.body, context, m_loadTicket)) {
	m_pending = nullptr;
	m_unloading = false;
      }
      return TxSelect::Refused;
    }
    // Association loading belongs to the logical session. A completed business
    // load survives physical replacement; its first publication uses the current
    // connection. Connection-specific controls remain subject to the guard above.
    m_pending->data().connection = m_connection;
    auto result = Base::commit_(ZuMv(m_pending));
    // Reload admission includes this candidate's reserved charge. Publication
    // can release operational bytes while its association is pending.
    ZmAssert(result == SeqResult::Accepted);
    checkLimit();
    return TxSelect::Accepted;
  }

  void postTx_(unsigned kind, uint64_t ticket) { impl()->txPost_(kind, ticket); }

  bool publishTx_(Msg *msg, bool) {
    auto &history = msg->data().history;
    // These controls belong to their original connection's first-send attempt.
    if (msg->data().connection != m_connection && history.body->administrative() &&
	history.body->msgType() != "3") return gapTx_(Span{history.seqNo, 1});
    if (msg->data().connection == m_connection &&
	history.body->msgType() == "A" && *history.originalTime) return true;
    return publishTx_(history, false);
  }

  bool publishTx_(TxHistory &history, bool replay) {
    if (!m_attached || !Base::running_()) return false;
    auto now = impl()->utcTx_();
    auto header = txHeader(m_config, history.seqNo, now,
      replay ? history.originalTime : ZuDateTime{});
    Fmt::Error error;
    auto frame = txFrame(*history.body, header, error, m_limits.codec);
    TxContext context{m_epoch, m_connection, history.seqNo, replay};
    if (!frame) { ++m_stats.encodeErrors; impl()->errorTx_(context, error); return false; }
    auto original = replay ? history.originalTime : now;
    if (!impl()->frameTx_(history.body, context, ZuMv(frame), original)) {
      ++m_stats.outputRefused; return false;
    }
    if (!replay) history.originalTime = now;
    return true;
  }

  bool gapTx_(const Span &gap) {
    if (!m_attached) return false;
    Session::Msg control;
    Session::SeqReset reset;
    reset.newSeqNo = gap.key() + gap.length();
    reset.gapFill = true;
    control.template p<Session::SeqReset>(reset);
    Fmt::Error error;
    auto body = TxBody::control(control, error, m_config.mode, m_limits.codec);
    TxContext context{m_epoch, m_connection, gap.key(), true};
    if (!body) { ++m_stats.encodeErrors; impl()->errorTx_(context, error); return false; }
    auto now = impl()->utcTx_();
    auto output = txFrame(*body, txHeader(m_config, gap.key(), now, now), error, m_limits.codec);
    if (!output) { ++m_stats.encodeErrors; impl()->errorTx_(context, error); return false; }
    bool ok = impl()->frameTx_(ZuMv(body), context, ZuMv(output), now);
    if (!ok) ++m_stats.outputRefused;
    return ok;
  }

  TxReload retrieveTx_(uint64_t key, uint64_t ackd, uint64_t ticket, bool replay) {
    return impl()->historyTx_(TxContext{m_epoch, m_connection, key, replay}, ackd, ticket);
  }

  void archiveTx_(uint64_t begin, uint64_t end) {
    impl()->historyArchive_(m_epoch, begin, end);
  }

  const TxConfig	m_config;
  const TxLimits	m_limits;

  // Tx-owned association, reset transaction and statistics.
  TxCounters		m_stats;
  ZmRef<Msg>		m_pending;
  TxProgress		m_resetProgress;
  uint64_t		m_epoch;
  uint64_t		m_connection = 0;
  uint64_t		m_ticket = 0;
  uint64_t		m_loadTicket = 0;
  uint64_t		m_resetTicket = 0;
  uint64_t		m_resetID = 0;
  uint64_t		m_resetEpoch = 0;
  bool			m_attached = false;
  bool			m_ready = false;
  bool			m_limitNotified = false;
  bool			m_unloading = false;
};

} // Zfix

#endif /* ZfixTx_HH */
