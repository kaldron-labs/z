//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Owner-shard FIX sequence queues

#ifndef ZfixSeq_HH
#define ZfixSeq_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/ZmAssert.hh>
#include <zlib/ZmPQueue.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZfixFmt.hh>

namespace Zfix {

struct SeqLimits {
  uint64_t	sequenceLimit = 2147483647;
  uint64_t	readAheadBytes = uint64_t(Fmt::Limits::FrameBytes)<<2;
  // Initial small-order workload: at most 4096 roughly 1 KiB pooled frames.
  unsigned	readAheadCount = 4096;
};

namespace SeqResult {

  enum { Accepted, Duplicate, Low, Invalid, Overload };
}

// Recovery metadata does not manufacture a payload node or alter node operations.
template <typename Queue> class SeqRxQueue_ : public Queue {
public:
  using Key = Queue::Key;
  using Span = Queue::Span;

  void reset(Key key) {
    Queue::reset(key);
    m_received = key;
  }

  void restore_(Key received) { m_received = received; }

  Key tail() const {
    auto tail = Queue::tail();
    return tail > m_received ? tail : m_received;
  }

  Span gap() const {
    auto gap = Queue::gap();
    if (gap.length()) return gap;
    auto begin = Queue::tail();
    return begin < m_received ? Span{begin, m_received - begin} : Span{};
  }

private:
  using Queue::skip;
  Key	m_received = 0;
};

// Queue items have one sequence number and retainedBytes(). The enclosing retained
// item owns its input backing once. All entry points run on the Rx owner; the
// endpoint supplies dispatch, timers and orderly teardown. Scheduled callbacks
// carry tickets, never ownership of the session.
//
// Impl hooks: processRx(Msg *), requestRx(Span, Span), reRequestRx(Span),
// postRx(ticket), armGap(ticket), cancelGap(). postRx calls drain_(ticket),
// armGap calls retry_(ticket) at an absolute deadline on the same owner.
// Holding invalidates posted work but preserves accepted read-ahead.
template <typename Impl, typename Queue> class SeqRx :
    private ZmPQRx<SeqRx<Impl, Queue>, SeqRxQueue_<Queue>, ZmPQRxGapObserve<>> {
  using RxQueue = SeqRxQueue_<Queue>;
  using Base = ZmPQRx<SeqRx<Impl, Queue>, RxQueue, ZmPQRxGapObserve<>>;
friend Base;
public:
  using Msg = Queue::Node;
  using Key = Queue::Key;
  using Span = Queue::Span;
  ZuAssert(sizeof(Key) == sizeof(uint64_t));
  ZuAssert(!Queue::Overlap && !Queue::Overwrite);

  explicit SeqRx(SeqLimits limits = {}) : m_limits{limits} {
    ZmAssert(limits.sequenceLimit && limits.sequenceLimit < UINT64_MAX);
    Base::rxReset(1);
  }

  SeqRx(const SeqRx &) = delete;
  SeqRx &operator =(const SeqRx &) = delete;

  uint64_t expected_() const { return m_queue.head(); }
  uint64_t received_() const { return m_queue.tail(); }

  Span requested_() const {
    auto actual = m_queue.gap(), request = m_requested;
    if (!actual.length() || !request.length()) return {};
    uint64_t begin = actual.key() > request.key() ? actual.key() : request.key();
    uint64_t a = actual.key() + actual.length(), b = request.key() + request.length();
    uint64_t end = a < b ? a : b;
    return begin < end ? Span{begin, end - begin} : Span{};
  }

  uint64_t retainedBytes_() const { return m_bytes; }
  unsigned retained_() const { return m_queue.count_(); }
  bool held_() const { return m_held; }
  Span gap_() const { return m_queue.gap(); }

  unsigned receive_(ZmRef<Msg> msg) {
    auto &item = msg->data();
    typename Queue::Fn fn{item};
    uint64_t key = fn.key(), bytes = item.retainedBytes();
    if (!key || key > m_limits.sequenceLimit || fn.length() != 1 || !bytes)
      return SeqResult::Invalid;
    if (key < expected_()) return SeqResult::Low;
    if (m_queue.has(key)) return SeqResult::Duplicate;
    bool queued = m_held || m_scheduled || key != expected_();
    if (queued && (m_queue.count_() >= m_limits.readAheadCount ||
	bytes > m_limits.readAheadBytes || m_bytes > m_limits.readAheadBytes - bytes))
      return SeqResult::Overload;
    m_bytes += bytes;
    Base::rcvd(ZuMv(msg));
    return SeqResult::Accepted;
  }

  void hold_() {
    m_held = true;
    m_scheduled = false;
    ++m_ticket;
    Base::startQueuing();
    cancelReRequest();
  }

  void resume_() {
    bool held = m_held;
    // A cancelled task can leave Dequeuing set even after a forward gap fill
    // emptied the queue. Clear it on-owner without retaining new input behind
    // an empty task (important when read-ahead is configured to zero).
    if (held && !m_queue.count_()) {
      m_scheduled = false; ++m_ticket;
      Base::dequeue();
    }
    m_held = false;
    Base::stopQueuing(expected_());
    if (m_queue.count_()) scheduleDequeue();
    reconcile_(gap_(), held || !m_retry);
  }

  bool restore_(uint64_t received, uint64_t processed, Span gap = {}) {
    if (!m_held || m_queue.count_() || !processed ||
	processed > received || received - 1 > m_limits.sequenceLimit) return false;
    Key end;
    if (!Queue::endOf(gap.key(), gap.length(), end) ||
	(gap.length() ? gap.key() < processed || end > received : gap.key() != Key{}))
      return false;
    ZmAssert_(!m_bytes);
    m_scheduled = false;
    ++m_ticket;
    Base::rxReset(processed);
    Base::startQueuing();
    m_queue.restore_(received);
    m_requested = gap;
    cancelReRequest();
    return true;
  }

  // Negotiated reset only, after admitted input has drained. A disconnect
  // uses hold_/resume_ and retains progress.
  bool reset_(uint64_t next) {
    if (!m_held || !next || next - 1 > m_limits.sequenceLimit ||
	m_queue.count_() || received_() != expected_())
      return false;
    m_scheduled = false;
    ++m_ticket;
    Base::rxReset(next);
    m_requested = {};
    m_bytes = 0;
    cancelReRequest();
    if (m_held) Base::startQueuing();
    return true;
  }

  // Gap fill/reset-mode Session::SeqReset never rewinds. Purge exactly the skipped
  // prefix, preserving both the node at next and any later read-ahead.
  bool advance_(uint64_t next) {
    if (next < expected_() || !next || next - 1 > m_limits.sequenceLimit)
      return false;
    if (next == expected_()) return true;
    {
      typename Queue::CIter i{m_queue};
      while (auto msg = i()) {
	typename Queue::Fn fn{msg->data()};
	if (fn.key() >= next) break;
	m_bytes -= msg->data().retainedBytes();
      }
    }
    m_queue.head(next);
    cancelReRequest();
    if (!m_held) scheduleDequeue();
    return true;
  }

  void drain_(uint64_t ticket) {
    if (ticket != m_ticket || !m_scheduled || m_held) return;
    m_scheduled = false;
    auto expected = expected_();
    Base::dequeue();
    if (!m_held && expected_() == expected) reconcile_(gap_());
  }

  void retry_(uint64_t ticket) {
    if (ticket != m_retryTicket || !m_retry || m_held) return;
    cancelReRequest();
    auto gap = gap_();
    if (!gap.length() || gap != m_requested) {
      reconcile_(gap);
      return;
    }
    auto generation = m_retryTicket;
    impl()->reRequestRx(gap);
    arm_(gap, generation);
  }

private:
  auto impl() { return static_cast<Impl *>(this); }

  RxQueue *rxQueue() { return &m_queue; }

  void process(Msg *msg) {
    m_bytes -= msg->data().retainedBytes();
    impl()->processRx(msg);
    if (!m_held && !m_queue.count_() && expected_() == received_()) {
      m_requested = {};
      cancelReRequest();
    }
  }

  void request(const Span &, const Span &now) {
    if (!m_held) reconcile_(now);
  }

  void reconcile_(Span gap, bool advertise = false) {
    if (!gap.length()) {
      m_requested = {};
      cancelReRequest();
      return;
    }
    if (gap == m_requested && !advertise) {
      scheduleReRequest();
      return;
    }
    auto prev = m_requested;
    cancelReRequest();
    auto generation = m_retryTicket;
    m_requested = gap;
    impl()->requestRx(prev, gap);
    arm_(gap, generation);
  }

  void arm_(Span gap, uint64_t generation) {
    if (generation == m_retryTicket && !m_held && !m_retry && m_requested == gap)
      scheduleReRequest();
  }

  void scheduleDequeue() {
    if (m_held || m_scheduled) return;
    m_scheduled = true;
    impl()->postRx(++m_ticket);
  }

  void rescheduleDequeue() { scheduleDequeue(); }

  void idleDequeue() { }

  void scheduleReRequest() {
    if (m_held || m_retry) return;
    m_retry = true;
    impl()->armGap(++m_retryTicket);
  }

  void cancelReRequest() {
    ++m_retryTicket;
    if (!m_retry) return;
    m_retry = false;
    impl()->cancelGap();
  }

  const SeqLimits	m_limits;

  // Rx-owned queue, gap notification and scheduled-work state.
  RxQueue		m_queue;
  Span			m_requested{};
  uint64_t		m_bytes = 0;
  uint64_t		m_ticket = 0;
  uint64_t		m_retryTicket = 0;
  bool			m_scheduled = false;
  bool			m_retry = false;
  bool			m_held = false;
};

struct SeqTxLimits {
  // Defaults from CONTRACTS.md; batches bound missing-history scans as well as
  // emitted messages, so a large unavailable range cannot monopolize Tx.
  uint64_t	sequenceLimit = 2147483647;
  uint64_t	queuedOutputBytes = uint64_t(Fmt::Limits::FrameBytes)<<4;
  unsigned	replayBatch = 64;
};

namespace TxTask {

  enum { Send, Replay, Archive, Reset };
}

// Bound a missing-key scan to one owner turn without changing physical storage.
template <typename Queue> class SeqTxQueue_ : public Queue {
public:
  using Key = Queue::Key;

  void limit_(Key end) { m_end = end; }

  Key tail() const {
    auto tail = Queue::tail();
    return m_end && m_end < tail ? m_end : tail;
  }

private:
  Key	m_end = 0;
};

template <typename Record> struct SeqReload {
  Record	history;
  bool		pending = false;
};

// Tx owns unpublished operational nodes only. Replay always asks the application;
// a result lives only through its current admission attempt. Ordered generic
// archiving reports completed ranges without retaining message bodies.
// Hooks: retrieveTx(key, ackd, ticket, replay), validReloadTx(record,key,replay),
// reloadMsgTx(record), publishTx(record,replay), publishTx(Msg *,false),
// gapTx(Span), archiveTx(begin,end), postTx(kind,ticket).
template <typename Impl, typename Queue,
  typename Record = ZmRef<typename Queue::Node>> class SeqTx :
    private ZmPQTx<SeqTx<Impl, Queue, Record>, SeqTxQueue_<Queue>> {
  using TxQueue = SeqTxQueue_<Queue>;
  using Base = ZmPQTx<SeqTx<Impl, Queue, Record>, TxQueue>;
friend Base;
public:
  using Msg = Queue::Node;
  using Span = Queue::Span;
  using Key = Queue::Key;
  ZuAssert(sizeof(Key) == sizeof(uint64_t));
  ZuAssert(!Queue::Overlap && !Queue::Overwrite);

  explicit SeqTx(SeqTxLimits limits = {}) : m_limits{limits} {
    ZmAssert(limits.sequenceLimit && limits.sequenceLimit < UINT64_MAX &&
      limits.replayBatch);
    Base::txReset(1);
  }

  SeqTx(const SeqTx &) = delete;
  SeqTx &operator =(const SeqTx &) = delete;

  uint64_t next_() const { return m_next; }
  uint64_t published_() const { return m_published; }
  uint64_t acknowledged_() const { return m_ackd; }
  uint64_t queuedBytes_() const { return m_bytes; }
  unsigned retained_() const { return m_queue.count_(); }
  bool running_() const { return m_running; }
  bool failed_() const { return m_failed; }
  bool waiting_() const { return m_reloadKey; }
  bool replaying_() const { return m_active.length() || m_pending.length(); }

  unsigned space_(uint64_t bytes) const {
    if (!bytes) return SeqResult::Invalid;
    if (bytes > m_limits.queuedOutputBytes ||
	m_bytes > m_limits.queuedOutputBytes - bytes) return SeqResult::Overload;
    return SeqResult::Accepted;
  }

  unsigned admit_(uint64_t bytes) const {
    return m_next > m_limits.sequenceLimit ? SeqResult::Invalid : space_(bytes);
  }

  unsigned commit_(ZmRef<Msg> msg) {
    auto &item = msg->data();
    typename Queue::Fn fn{item};
    if (fn.key() != m_next || fn.length() != 1) return SeqResult::Invalid;
    unsigned result = admit_(item.wireBytes());
    if (result != SeqResult::Accepted) return result;
    ++m_next;
    m_bytes += item.wireBytes();
    Base::send(ZuMv(msg));
    return SeqResult::Accepted;
  }

  ZmRef<Msg> latest_() const { return m_queue.find(m_next - 1); }

  bool abort_(uint64_t key) {
    if (m_running || waiting_() || key < m_published || key >= m_next) return false;
    ZmRef<Msg> msg = m_queue.find(key);
    if (!msg) return false;
    auto bytes = msg->data().wireBytes();
    if (!bytes || bytes > m_bytes) return false;
    m_queue.del(key);
    m_bytes -= bytes;
    return true;
  }

  void hold_() {
    m_running = false;
    m_scheduled = 0;
    m_sendUnavailable = {};
    Base::stop();
  }

  void resume_() {
    m_running = true;
    m_failed = false;
    Base::start();
    if (m_published < m_next) scheduleSend();
    if (m_active.length()) scheduleResend();
    if (m_archiveScan < m_archiveTarget) scheduleArchive();
    feedArchive();
    feedReplay();
  }

  // Inclusive end; zero snapshots actual admission, including priority Logon.
  bool replay_(uint64_t begin, uint64_t end = 0) {
    if (!begin || begin >= m_admitted || (end && end < begin)) return false;
    uint64_t until = !end || end >= m_admitted ? m_admitted : end + 1;
    if (!m_pending.length()) m_pending = Span{begin, until - begin};
    else {
      uint64_t oldEnd = m_pending.key() + m_pending.length();
      if (begin > m_pending.key()) begin = m_pending.key();
      if (until < oldEnd) until = oldEnd;
      m_pending = Span{begin, until - begin};
    }
    feedReplay();
    return true;
  }

  bool acknowledge_(uint64_t next) {
    if (!next || next > m_published) return false;
    if (next <= m_ackd) return true;
    m_ackd = next;
    feedArchive();
    return true;
  }

  Span replayRange_() const {
    const auto &active = m_active;
    if (!active.length()) return m_pending.length() ? m_pending : Span{};
    if (!m_pending.length()) return active;
    uint64_t begin = active.key() < m_pending.key() ? active.key() : m_pending.key();
    uint64_t a = active.key() + active.length(), b = m_pending.key() + m_pending.length();
    return {begin, (a > b ? a : b) - begin};
  }

  // Empty restored storage has no physical charge from a previous process.
  bool restore_(uint64_t loaded, uint64_t published, uint64_t ackd, Span replay = {}) {
    uint64_t end;
    if (!Queue::endOf(replay.key(), replay.length(), end) ||
	(replay.length() ? !replay.key() || end > loaded : replay.key() != 0)) return false;
    if (m_running || m_queue.count_() || m_active.length() || waiting_() ||
	m_archiveScan != m_archiveTarget || !ackd || ackd > published ||
	published > loaded || !loaded || loaded - 1 > m_limits.sequenceLimit) return false;
    if (!Base::txRestore(published, ackd)) return false;
    m_queue.reset(loaded);
    m_next = loaded;
    m_published = m_admitted = published;
    m_ackd = m_archiveScan = m_archiveTarget = ackd;
    m_bytes = 0;
    m_pending = replay;
    m_active = {};
    m_sendUnavailable = {};
    m_failed = false;
    m_scheduled = 0;
    return true;
  }

  bool reset_(uint64_t next = 1) {
    if (!epochIdle_()) return false;
    return restore_(next, next, next);
  }

  bool epochIdle_() const { return !m_running && epochReady_(); }

  bool epochReady_() const {
    return !m_queue.count_() && !m_bytes && m_next == m_published &&
      m_archiveScan == m_ackd && m_archiveTarget == m_ackd &&
      !replaying_() && !waiting_();
  }

  // Defaults for queues whose application record is already their node handle.
  bool validReloadTx(const Record &record, uint64_t key, bool) {
    typename Queue::Fn fn{record->data()};
    return fn.key() == key && fn.length() == 1 && record->data().wireBytes();
  }

  ZmRef<Msg> reloadMsgTx(Record record) { return record; }

  // Only the matching obligation retires. Replay results never enter m_queue.
  bool completeReload_(uint64_t token, Record record) {
    if (!waiting_() || token != m_reloadTicket) return false;
    auto key = m_reloadKey;
    auto kind = m_reloadKind;
    m_reloadKey = 0;
    if (m_running) {
      if (!!record && (kind != TxTask::Replay ||
	  (m_active.length() && m_active.key() == key)) &&
	  !impl()->validReloadTx(record, key, kind == TxTask::Replay)) {
	m_failed = true;
	return true;
      }
      if (kind == TxTask::Replay) {
	if (m_active.length() && m_active.key() == key && !m_failed) {
	  if (!record) replayGap(Span{key, 1});
	  else if (impl()->publishTx(record, true)) replayed(key + 1);
	  else m_failed = true;
	}
      } else if (!record) {
	if (!unavailable(key)) m_failed = true;
      } else install(ZuMv(record));
    }
    continue_();
    return true;
  }

  void run_(unsigned kind, uint64_t token) {
    if (kind > TxTask::Archive || !m_running ||
	!(m_scheduled & (1U<<kind)) || ticket(kind) != token) return;
    m_scheduled &= ~(1U<<kind);
    m_lookupError = false;
    switch (kind) {
      case TxTask::Send: {
	if (m_published < m_next && !m_queue.has(m_published)) {
	  retrieve_(m_published, m_queue.head());
	  if (waiting_() || m_lookupError || !m_running) break;
	}
	uint64_t count = m_next - m_published;
	if (count > m_limits.replayBatch) count = m_limits.replayBatch;
	m_queue.limit_(m_published + count);
	Base::send();
	m_queue.limit_(0);
	if (m_running && !m_failed && !waiting_() && m_published < m_next) scheduleSend();
      } break;
      case TxTask::Replay: if (!waiting_()) replay(token); break;
      case TxTask::Archive: Base::archive(); break;
    }
    if (!m_running) { Base::stop(); Base::send(); Base::archive(); }
  }

protected:
  ZmRef<Msg> assigned_(Key key) const { return m_queue.find(key); }

  void admitted_(Key end) { if (end > m_admitted) m_admitted = end; }

private:
  auto impl() { return static_cast<Impl *>(this); }

  TxQueue *txQueue() { return &m_queue; }

  uint64_t &ticket(unsigned kind) {
    switch (kind) {
      case TxTask::Send: return m_sendTicket;
      case TxTask::Replay: return m_replayTicket;
      default: return m_archiveTicket;
    }
  }

  void schedule(unsigned kind) {
    if (!m_running || (m_scheduled & (1U<<kind))) return;
    m_scheduled |= 1U<<kind;
    impl()->postTx(kind, ++ticket(kind));
  }

  void continue_() {
    if (!m_running || m_failed || waiting_()) return;
    if (m_published < m_next) scheduleSend();
    if (m_active.length()) scheduleResend();
    feedReplay();
  }

  void feedReplay() {
    if (!m_running || m_active.length() || !m_pending.length() ||
	m_pending.key() >= m_published) return;
    uint64_t count = m_pending.length();
    auto available = m_published - m_pending.key();
    if (count > available) count = available;
    if (count > m_limits.replayBatch) count = m_limits.replayBatch;
    m_active = Span{m_pending.key(), count};
    m_pending.key() += count;
    m_pending.length() -= count;
    scheduleResend();
  }

  void replay(uint64_t token) {
    if (!m_active.length()) return;
    auto end = m_active.key() + m_active.length();
    Span gap;
    for (auto key = m_active.key(); key < end; ++key) {
      m_reloadKey = key;
      m_reloadKind = TxTask::Replay;
      auto result = impl()->retrieveTx(key, m_ackd, ++m_reloadTicket, true);
      if (!result.pending) m_reloadKey = 0;
      if (!m_running || token != m_replayTicket) return;
      if (result.pending) { if (gap.length()) replayGap(gap); return; }
      auto &record = result.history;
      if (!record) {
	if (!gap.length()) gap.key() = key;
	++gap.length();
	continue;
      }
      if (!impl()->validReloadTx(record, key, true)) { m_failed = true; return; }
      if (gap.length()) {
	if (!replayGap(gap)) return;
	gap = {};
      }
      if (!m_running || !impl()->publishTx(record, true)) {
	m_failed = true; return;
      }
      replayed(key + 1);
      if (!m_running || token != m_replayTicket) return;
    }
    if (gap.length() && !replayGap(gap)) return;
    continue_();
  }

  bool replayGap(const Span &gap) {
    if (!m_running || !impl()->gapTx(gap)) { m_failed = true; return false; }
    replayed(gap.key() + gap.length());
    return true;
  }

  void replayed(uint64_t next) {
    auto end = m_active.key() + m_active.length();
    m_active = next < end ? Span{next, end - next} : Span{};
  }

  void feedArchive() {
    if (!m_running || m_archiveScan != m_archiveTarget || m_archiveTarget == m_ackd) return;
    uint64_t count = m_ackd - m_archiveTarget;
    if (count > m_limits.replayBatch) count = m_limits.replayBatch;
    m_archiveTarget += count;
    Base::ackd(m_archiveTarget);
  }

  bool send_(Msg *msg, bool) {
    auto &item = msg->data();
    typename Queue::Fn fn{item};
    auto key = fn.key();
    auto bytes = item.wireBytes();
    if (!m_running || key != m_published || bytes > m_bytes ||
	!impl()->publishTx(msg, false)) { m_failed = true; return false; }
    ++m_published;
    m_bytes -= bytes;
    m_queue.del(key);
    admitted_(m_published);
    feedReplay();
    return true;
  }

  bool sendGap_(const Span &gap, bool) {
    auto end = gap.key() + gap.length();
    if (!m_running || waiting_() || m_lookupError || gap.key() != m_published ||
	(end == m_next && m_bytes) || !impl()->gapTx(gap)) {
      m_failed = !waiting_();
      if (m_failed) m_sendUnavailable = {};
      return false;
    }
    m_published = end;
    m_sendUnavailable = {};
    admitted_(m_published);
    feedReplay();
    return true;
  }

  ZmRef<Msg> install(Record record) {
    auto msg = impl()->reloadMsgTx(ZuMv(record));
    if (!msg || space_(msg->data().wireBytes()) != SeqResult::Accepted) {
      m_failed = m_lookupError = true; return nullptr;
    }
    auto key = typename Queue::Fn{msg->data()}.key();
    if (auto previous = m_queue.find(key)) return previous;
    m_bytes += msg->data().wireBytes();
    if (key < m_queue.head()) m_queue.unshift(msg);
    else m_queue.add(msg);
    return msg;
  }

  ZmRef<Msg> retrieve_(Key key, Key) {
    if (waiting_()) return nullptr;
    const auto &missing = m_sendUnavailable;
    if (missing.length() && key >= missing.key() &&
	key - missing.key() < missing.length()) return nullptr;
    m_reloadKey = key;
    m_reloadKind = TxTask::Send;
    auto result = impl()->retrieveTx(key, m_ackd, ++m_reloadTicket, false);
    if (result.pending) return nullptr;
    m_reloadKey = 0;
    if (!m_running) return nullptr;
    auto &record = result.history;
    if (!record) {
      if (!unavailable(key)) m_failed = m_lookupError = true;
      return nullptr;
    }
    if (!impl()->validReloadTx(record, key, false)) {
      m_failed = m_lookupError = true; return nullptr;
    }
    return install(ZuMv(record));
  }

  bool unavailable(uint64_t key) {
    auto &missing = m_sendUnavailable;
    if (!missing.length()) missing = Span{key, 1};
    else {
      auto begin = missing.key(), end = begin + missing.length();
      if (key >= begin && key < end) return true;
      if (key == end) ++missing.length();
      else if (key + 1 == begin) { --missing.key(); ++missing.length(); }
      else return false;
    }
    return true;
  }

  void archive_(Msg *) { }

  void scheduleSend() { schedule(TxTask::Send); }
  void rescheduleSend() { scheduleSend(); }
  void idleSend() { }

  void scheduleResend() { schedule(TxTask::Replay); }
  void idleResend() { }

  void scheduleArchive() { schedule(TxTask::Archive); }
  void rescheduleArchive() { scheduleArchive(); }

  void idleArchive() {
    auto begin = m_archiveScan, end = m_archiveTarget;
    m_archiveScan = end;
    if (begin < end) impl()->archiveTx(begin, end);
    feedArchive();
  }

  const SeqTxLimits	m_limits;

  // Tx-owned operational assignments, replay requests and scheduled work.
  TxQueue		m_queue;
  Span			m_pending;
  Span			m_active;
  Span			m_sendUnavailable;
  uint64_t		m_next = 1;
  uint64_t		m_published = 1;
  Key			m_admitted = 1;
  uint64_t		m_ackd = 1;
  uint64_t		m_archiveScan = 1;
  uint64_t		m_archiveTarget = 1;
  uint64_t		m_bytes = 0;
  uint64_t		m_sendTicket = 0;
  uint64_t		m_replayTicket = 0;
  uint64_t		m_archiveTicket = 0;
  uint64_t		m_reloadKey = 0;
  uint64_t		m_reloadTicket = 0;
  unsigned		m_reloadKind = TxTask::Send;
  unsigned		m_scheduled = 0;
  bool			m_lookupError = false;
  bool			m_running = false;
  bool			m_failed = false;
};

} // Zfix

#endif /* ZfixSeq_HH */
