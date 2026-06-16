//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC recovery utilities

#ifndef ZquicRecovery_HH
#define ZquicRecovery_HH

#ifndef ZquicLib_HH
#include <zlib/ZquicLib.hh>
#endif

#include <string.h>

#include <zlib/ZmQueue.hh>

#include <zlib/ZquicSched.hh>

namespace Zquic {

inline constexpr ZuTime timeUS(uint64_t usec)
{
  return ZuTime{
    int64_t(usec / 1000000), int32_t((usec % 1000000) * 1000)};
}

inline constexpr ZuTime timePow2(ZuTime t, unsigned n)
{
  return ZuTime{ZuTime::Nano{t.nanosecs() << n}};
}

inline constexpr ZuTime timeMul(ZuTime t, uint64_t n)
{
  return ZuTime{ZuTime::Nano{t.nanosecs() * n}};
}

inline constexpr ZuTime timeDiv(ZuTime t, uint64_t n)
{
  return ZuTime{ZuTime::Nano{t.nanosecs() / n}};
}

using AckTrackerRxNTP = ZmPQRxGapObserve<>;

class AckTracker :
  public ZmPQRx<AckTracker, PktRxPQueue, AckTrackerRxNTP> {
public:
  static constexpr unsigned Max = 64;
  using Queue = PktRxPQueue;
  using Rx = ZmPQRx<AckTracker, Queue, AckTrackerRxNTP>;
  using Msg = Queue::Node;
  using Span = Queue::Span;

  bool add(uint64_t pn) {
    if (contains(pn)) return true;
    Rx::rcvd(new Queue::Node{RxPktMark{pn}});
    return true;
  }

  bool contains(uint64_t pn) const {
    return pn < m_ackHead || m_packets.has(pn);
  }

  unsigned count() const {
    unsigned n = 0;
    ranges_([&n](const AckRange &) { ++n; return true; });
    return n;
  }
  uint64_t first(unsigned i) const {
    uint64_t v = 0;
    unsigned n = 0;
    ranges_([i, &n, &v](const AckRange &range) {
      if (n++ == i) { v = range.first; return false; }
      return true;
    });
    return v;
  }
  uint64_t last(unsigned i) const {
    uint64_t v = 0;
    unsigned n = 0;
    ranges_([i, &n, &v](const AckRange &range) {
      if (n++ == i) { v = range.largest; return false; }
      return true;
    });
    return v;
  }
  bool range(unsigned i, AckRange &range) const {
    bool found = false;
    unsigned n = 0;
    ranges_([i, &n, &found, &range](const AckRange &range_) {
      if (n++ != i) return true;
      range = range_;
      found = true;
      return false;
    });
    return found;
  }
  uint64_t largest() const {
    uint64_t v = 0;
    bool found = false;
    m_packets.rspans([&v, &found](const auto &span) {
      v = span.key() + span.length() - 1;
      found = true;
      return false;
    });
    if (m_ackHead && (!found || m_ackHead - 1 > v)) {
      v = m_ackHead - 1;
      found = true;
    }
    if (!found) return 0;
    return v;
  }
  int writeFrame(uint8_t *out, unsigned len, uint64_t delay = 0) const {
    AckRange ranges[Max];
    unsigned n = 0;
    bool ok = ranges_([&ranges, &n](const AckRange &range) {
      if (n >= Max) return false;
      ranges[n++] = range;
      return true;
    });
    if (!ok || !n) return -1;
    return FrameCodec::writeAckRanges(out, len, ranges, n, delay);
  }
  void clear() {
    Rx::rxReset(0);
    m_ackHead = 0;
    m_ackGap = {};
  }

  Queue *rxQueue() { return &m_packets; }

  void process(Msg *msg) {
    if (!msg) return;
    uint64_t pn = msg->data().pn;
    if (pn >= m_ackHead) m_ackHead = pn + 1;
    refreshGap_();
  }

  void request(const Span &, const Span &now) {
    m_ackGap = now;
  }

  void scheduleDequeue() { Rx::dequeue(); }
  void rescheduleDequeue() { Rx::dequeue(); }
  void idleDequeue() { }

private:
  void refreshGap_() {
    Span gap = m_packets.gap();
    m_ackGap = gap.length() ? gap : Span{};
  }

  template <typename L>
  bool ranges_(L &&l) const {
    if (m_ackHead)
      if (!l(AckRange{m_ackHead - 1, 0})) return false;
    Span ackGap = m_ackGap;
    uint64_t gapEnd = 0;
    bool haveGap =
      ackGap.length() && Queue::endOf(ackGap.key(), ackGap.length(), gapEnd);
    return m_packets.spans([&l, haveGap, gapEnd](const auto &span) {
      uint64_t first = span.key();
      uint64_t end = 0;
      if (!Queue::endOf(first, span.length(), end)) return false;
      if (haveGap && first < gapEnd) {
	if (end <= gapEnd) return true;
	first = gapEnd;
      }
      return l(AckRange{end - 1, first});
    });
  }

  Queue		m_packets{0};
  Span		m_ackGap;
  uint64_t	m_ackHead = 0;
};

class AckManager {
public:
  static constexpr unsigned Spaces = 3;

  bool received(
    PktSpace::T space, uint64_t pn, uint64_t now, uint64_t maxAckDelay,
    bool ackEliciting = true)
  {
    unsigned i = index_(space);
    if (!m_ack[i].add(pn)) return false;
    m_pending[i] = true;
    if (ackEliciting) {
      uint64_t deadline =
	maxAckDelay > uint64_t(-1) - now ? uint64_t(-1) : now + maxAckDelay;
      if (!m_deadlineSet[i] || deadline < m_deadline[i])
	m_deadline[i] = deadline;
      m_deadlineSet[i] = true;
    }
    return true;
  }

  const AckTracker &tracker(PktSpace::T space) const {
    return m_ack[index_(space)];
  }
  bool pending(PktSpace::T space) const {
    return m_pending[index_(space)];
  }
  bool deadlineSet(PktSpace::T space) const {
    return m_deadlineSet[index_(space)];
  }
  uint64_t deadline(PktSpace::T space) const {
    return m_deadline[index_(space)];
  }
  bool due(PktSpace::T space, uint64_t now) const {
    unsigned i = index_(space);
    return m_pending[i] && m_deadlineSet[i] && now >= m_deadline[i];
  }
  int writeFrame(
    PktSpace::T space, uint8_t *out, unsigned len,
    uint64_t delay = 0) const
  {
    return m_ack[index_(space)].writeFrame(out, len, delay);
  }
  void sent(PktSpace::T space) {
    unsigned i = index_(space);
    m_pending[i] = false;
    m_deadlineSet[i] = false;
    m_deadline[i] = 0;
  }

private:
  static unsigned index_(PktSpace::T space) {
    if (space == PktSpace::Initial) return 0;
    if (space == PktSpace::Handshake) return 1;
    return 2;
  }

  AckTracker	m_ack[Spaces];
  bool		m_pending[Spaces] = {};
  bool		m_deadlineSet[Spaces] = {};
  uint64_t	m_deadline[Spaces] = {};
};

class RttEstimator {
public:
  static constexpr ZuTime InitialRTT = timeUS(333000);
  static constexpr ZuTime Granularity = timeUS(1000);
  static constexpr uint64_t TimeThresholdNumerator = 9;
  static constexpr uint64_t TimeThresholdDenominator = 8;

  ZuTime latest() const { return m_latest; }
  ZuTime min() const { return m_min; }
  ZuTime smoothed() const { return m_smoothed; }
  ZuTime variance() const { return m_variance; }

  void sample(ZuTime rtt, ZuTime ackDelay, bool appData) {
    if (!*rtt || !rtt) return;
    if (!*m_min || rtt < m_min) m_min = rtt;
    if (appData) {
      ZuTime minAck = *ackDelay && ackDelay ? m_min + ackDelay : m_min;
      if (*ackDelay && ackDelay && rtt >= minAck) rtt -= ackDelay;
    }
    m_latest = rtt;
    if (!*m_smoothed) {
      m_smoothed = rtt;
      m_variance = timeDiv(rtt, 2);
      return;
    }
    ZuTime diff = m_smoothed > rtt ? m_smoothed - rtt : rtt - m_smoothed;
    m_variance = timeDiv(timeMul(m_variance, 3) + diff, 4);
    m_smoothed = timeDiv(timeMul(m_smoothed, 7) + rtt, 8);
  }

  ZuTime pto(ZuTime maxAckDelay) const {
    ZuTime smoothed = *m_smoothed ? m_smoothed : InitialRTT;
    ZuTime variance = *m_smoothed ? m_variance : timeDiv(InitialRTT, 2);
    ZuTime var4 = timePow2(variance, 2);
    if (var4 < Granularity) var4 = Granularity;
    ZuTime pto = smoothed + var4;
    return *maxAckDelay && maxAckDelay ? pto + maxAckDelay : pto;
  }
  ZuTime timeThreshold() const {
    ZuTime rtt = m_latest ? m_latest : m_smoothed;
    if (!*rtt) rtt = m_min;
    if (!*rtt) rtt = InitialRTT;
    if (*m_min && m_min > rtt) rtt = m_min;
    ZuTime threshold = timeDiv(timeMul(rtt, TimeThresholdNumerator),
      TimeThresholdDenominator);
    return threshold < Granularity ? Granularity : threshold;
  }

private:
  ZuTime	m_latest;
  ZuTime	m_min;
  ZuTime	m_smoothed;
  ZuTime	m_variance;
};

class NewReno {
public:
  explicit NewReno(unsigned maxDatagram = MinUDPPayload) :
    m_maxDatagram{maxDatagram}, m_cwnd{maxDatagram * 10} { }

  uint64_t cwnd() const { return m_cwnd; }
  uint64_t ssthresh() const { return m_ssthresh; }
  uint64_t bytesInFlight() const { return m_bytesInFlight; }
  uint64_t recoveryStartTime() const { return m_recoveryStartTime; }
  bool inRecovery() const { return m_recoveryStartTime; }

  bool canSend(unsigned bytes) const {
    return m_bytesInFlight + bytes <= m_cwnd;
  }
  void sent(unsigned bytes, bool inFlight = true) {
    if (inFlight) m_bytesInFlight += bytes;
  }
  void acked(unsigned bytes) {
    if (bytes > m_bytesInFlight) m_bytesInFlight = 0;
    else m_bytesInFlight -= bytes;
    if (m_cwnd < m_ssthresh)
      m_cwnd += bytes;
    else {
      uint64_t n = (uint64_t(m_maxDatagram) * bytes) / m_cwnd;
      m_cwnd += n ? n : 1;
    }
  }
  void lost(unsigned bytes, bool pmtudProbe = false) {
    lostAt(bytes, 0, pmtudProbe);
  }
  void lostAt(unsigned bytes, uint64_t sentTime, bool pmtudProbe = false) {
    if (bytes > m_bytesInFlight) m_bytesInFlight = 0;
    else m_bytesInFlight -= bytes;
    if (pmtudProbe) return;
    if (m_recoveryStartTime && sentTime <= m_recoveryStartTime) return;
    m_recoveryStartTime = sentTime;
    m_ssthresh = m_cwnd >> 1;
    if (m_ssthresh < uint64_t(m_maxDatagram) * 2)
      m_ssthresh = uint64_t(m_maxDatagram) * 2;
    m_cwnd = m_ssthresh;
  }
  void persistentCongestion() {
    m_cwnd = uint64_t(m_maxDatagram) * 2;
    m_ssthresh = m_cwnd;
  }

private:
  unsigned	m_maxDatagram = MinUDPPayload;
  uint64_t	m_cwnd = 0;
  uint64_t	m_ssthresh = uint64_t(-1);
  uint64_t	m_bytesInFlight = 0;
  uint64_t	m_recoveryStartTime = 0;
};

struct SentFrameRef {
  SentFrameKind::T	kind = SentFrameKind::None;
  FrameType::T		controlType = FrameType::Unknown;
  uint64_t		streamID = 0;
  uint64_t		offset = 0;
  uint64_t		length = 0;
  uint64_t		value = 0;
  Zi::StreamType::T	streamType = Zi::StreamType::Duplex;
  bool			fin = false;
  uint8_t		payload[8]{};
  TxRange		range;

  bool operator !() const { return kind == SentFrameKind::None; }

  static SentFrameRef stream(uint64_t id, const TxRange &range_, bool fin_) {
    SentFrameRef ref;
    ref.kind = SentFrameKind::Stream;
    ref.streamID = id;
    ref.offset = range_.streamOffset;
    ref.length = range_.length;
    ref.fin = fin_;
    ref.range = range_;
    return ref;
  }

  static SentFrameRef crypto(uint64_t offset_, uint64_t length_) {
    SentFrameRef ref;
    ref.kind = SentFrameKind::Crypto;
    ref.offset = offset_;
    ref.length = length_;
    return ref;
  }

  static SentFrameRef control() {
    SentFrameRef ref;
    ref.kind = SentFrameKind::Control;
    return ref;
  }

  static SentFrameRef flowUpdate(const FlowUpdate &update) {
    SentFrameRef ref = control();
    ref.controlType = update.type;
    ref.streamID = update.streamID;
    ref.value = update.maximum;
    ref.streamType = update.streamType;
    return ref;
  }

  static SentFrameRef blocked(
    FrameType::T type, uint64_t streamID_, uint64_t limit,
    Zi::StreamType::T streamType_ = Zi::StreamType::Duplex) {
    SentFrameRef ref = control();
    ref.controlType = type;
    ref.streamID = streamID_;
    ref.value = limit;
    ref.streamType = streamType_;
    return ref;
  }

  static SentFrameRef pathResponse(ZuCSpan data) {
    SentFrameRef ref = control();
    ref.controlType = FrameType::PathResponse;
    if (data.length() == 8) memcpy(ref.payload, data.data(), 8);
    return ref;
  }

  static SentFrameRef handshakeDone() {
    SentFrameRef ref = control();
    ref.controlType = FrameType::HandshakeDone;
    return ref;
  }
};

struct SentPkt {
  static constexpr unsigned MaxFrames = 8;

  uint64_t key() const { return pn; }
  uint64_t length() const { return 1; }
  uint64_t clipHead(uint64_t) { return 0; }
  uint64_t clipTail(uint64_t) { return 0; }
  template <typename I>
  void write(const I &) { }

  bool addFrame(const SentFrameRef &frame) {
    if (frame.kind == SentFrameKind::None || frameCount >= MaxFrames)
      return false;
    frames[frameCount++] = frame;
    return true;
  }
  unsigned framesUsed() const { return frameCount; }
  const SentFrameRef &frame(unsigned i) const {
    ZiAssert(i < frameCount, "Zquic", (i, frameCount),
      "sent-packet frame index out of bounds", return frames[0]);
    return frames[i];
  }

  uint64_t	pn = 0;
  ZuTime	sentTime;
  unsigned	bytes = 0;
  PktSpace::T space = PktSpace::AppData;
  bool		ackEliciting = false;
  bool		inFlight = false;
  bool		pmtudProbe = false;
  bool		acked = false;
  bool		lost = false;
  SentFrameRef	frames[MaxFrames];
  unsigned	frameCount = 0;
};

using TxPkt = SentPkt;

using TxPktQueue =
  ZmPQueue<TxPkt,
    ZmPQueueNode<ZuObject,
      ZmPQueueHeapID<"Zquic.Pkt.TxSentNode",
	ZmPQueueBits<3,
	  ZmPQueueLevels<4>>>>>;

class RetransmitQueue {
public:
  using Queue =
    ZmQueue<SentFrameRef,
      ZmQueueHeapID<"Zquic.Pkt.RetransmitQueue">>;

  bool push(const SentFrameRef &frame) {
    if (frame.kind == SentFrameKind::None) return false;
    m_frames.push(frame);
    return true;
  }

  bool pop(SentFrameRef &frame) {
    if (!m_frames.count_()) return false;
    frame = m_frames.shift();
    return true;
  }

  unsigned count() const { return m_frames.count_(); }
  // No retransmit drop policy exists for this unbounded queue.
  unsigned dropped() const { return 0; }
  bool empty() const { return !m_frames.count_(); }
  void clear() {
    m_frames.clean();
  }

private:
  Queue		m_frames;
};

class PktTxSpace :
  public ZmPQTx<PktTxSpace, TxPktQueue> {
public:
  using Queue = TxPktQueue;
  using Tx = ZmPQTx<PktTxSpace, Queue>;
  using Msg = Queue::Node;
  using Span = Queue::Span;
  using Key = Queue::Key;

  PktTxSpace() { Tx::start(); }

  Queue *txQueue() { return &m_packets; }

  bool send_(Msg *, bool) { return true; }
  bool resend_(Msg *msg, bool) {
    if (msg) enqueueRetransmit_(msg->data());
    return true;
  }
  bool sendGap_(const Span &, bool) { return true; }
  bool resendGap_(const Span &, bool) { return true; }
  void archive_(Msg *) { }
  ZmRef<Msg> retrieve_(Key, Key) { return nullptr; }
  void scheduleSend() { }
  void rescheduleSend() { }
  void idleSend() { }
  void scheduleResend() { }
  void rescheduleResend() { }
  void idleResend() { }
  void scheduleArchive() { }
  void rescheduleArchive() { }
  void idleArchive() { }

  bool add(const SentPkt &p) {
    if (m_packets.has(p.pn)) return false;
    Tx::send(new Queue::Node{p});
    if (p.inFlight) m_bytesInFlight += p.bytes;
    return true;
  }

  bool ack(uint64_t pn) {
    auto node = m_packets.find(pn);
    return node && ack_(node->data());
  }

  unsigned ack(
    const AckTracker &ranges, unsigned *lost = nullptr,
    unsigned packetThreshold = 3)
  {
    unsigned n = 0;
    auto iter = m_packets.iter();
    while (auto node = iter())
      if (ranges.contains(node->data().pn) && ack_(node->data())) ++n;
    unsigned l = markPktThresholdLoss(ranges.largest(), packetThreshold);
    if (lost) *lost = l;
    return n;
  }

  unsigned ack(
    const AckRange *ranges, unsigned nRanges, unsigned *lost = nullptr,
    unsigned packetThreshold = 3, ZuTime *latestSentTime = nullptr,
    uint64_t *ackedBytes = nullptr, uint64_t *lostBytes = nullptr,
    ZuTime *lostSentTime = nullptr)
  {
    unsigned n = 0;
    uint64_t ackedBytes_ = 0;
    uint64_t largest = 0;
    uint64_t largestAcked = 0;
    bool have = false;
    bool haveAcked = false;
    if (latestSentTime) *latestSentTime = ZuTime{0};
    if (ackedBytes) *ackedBytes = 0;
    if (lostBytes) *lostBytes = 0;
    if (lostSentTime) *lostSentTime = ZuTime{0};
    for (unsigned i = 0; i < nRanges; ++i) {
      const AckRange &range = ranges[i];
      if (range.first > range.largest) continue;
      if (!have || range.largest > largest) largest = range.largest;
      have = true;
      auto iter = m_packets.iter(range.first);
      while (auto node = iter()) {
	SentPkt &p = node->data();
	if (p.pn > range.largest) break;
	if (ack_(p)) {
	  ++n;
	  ackedBytes_ += p.bytes;
	  if (latestSentTime && (!haveAcked || p.pn > largestAcked)) {
	    largestAcked = p.pn;
	    *latestSentTime = p.sentTime;
	    haveAcked = true;
	  }
	}
      }
    }
    unsigned l = have ?
      markPktThresholdLoss(
	largest, packetThreshold, lostBytes, lostSentTime) : 0;
    if (ackedBytes) *ackedBytes = ackedBytes_;
    if (lost) *lost = l;
    return n;
  }

  bool lose(uint64_t pn) {
    auto node = m_packets.find(pn);
    return node && lose_(node->data());
  }

  unsigned markPktThresholdLoss(
    uint64_t largestAcked, unsigned threshold = 3,
    uint64_t *lostBytes = nullptr, ZuTime *lostSentTime = nullptr) {
    unsigned n = 0;
    if (lostBytes) *lostBytes = 0;
    if (lostSentTime) *lostSentTime = ZuTime{0};
    auto iter = m_packets.iter();
    while (auto node = iter()) {
      SentPkt &p = node->data();
      if (p.acked || p.lost || p.pn + threshold > largestAcked) continue;
      if (lose_(p)) {
	++n;
	if (lostBytes) *lostBytes += p.bytes;
	if (lostSentTime && p.sentTime > *lostSentTime)
	  *lostSentTime = p.sentTime;
      }
    }
    return n;
  }

  unsigned markTimeThresholdLoss(
    ZuTime now, ZuTime threshold,
    uint64_t *lostBytes = nullptr, ZuTime *lostSentTime = nullptr) {
    unsigned n = 0;
    if (lostBytes) *lostBytes = 0;
    if (lostSentTime) *lostSentTime = ZuTime{0};
    auto iter = m_packets.iter();
    while (auto node = iter()) {
      SentPkt &p = node->data();
      if (p.acked || p.lost || !*p.sentTime || p.sentTime > now ||
	  now - p.sentTime < threshold)
	continue;
      if (lose_(p)) {
	++n;
	if (lostBytes) *lostBytes += p.bytes;
	if (lostSentTime && p.sentTime > *lostSentTime)
	  *lostSentTime = p.sentTime;
      }
    }
    return n;
  }
  ZuTime nextLossTime(ZuTime threshold) const {
    if (!*threshold) return ZuTime{0};
    ZuTime out;
    bool have = false;
    auto iter = m_packets.citer();
    while (auto node = iter()) {
      const SentPkt &p = node->data();
      if (p.acked || p.lost || !p.inFlight || !p.ackEliciting ||
	  !*p.sentTime)
	continue;
      ZuTime deadline = p.sentTime + threshold;
      if (!have || deadline < out) {
	out = deadline;
	have = true;
      }
    }
    return have ? out : ZuTime{0};
  }

  uint64_t bytesInFlight() const { return m_bytesInFlight; }
  ZuTime latestAckSentTime() const {
    ZuTime t{0};
    auto iter = m_packets.citer();
    while (auto node = iter()) {
      const SentPkt &p = node->data();
      if (p.acked || p.lost || !p.inFlight || !p.ackEliciting)
	continue;
      if (!*t || p.sentTime > t) t = p.sentTime;
    }
    return t;
  }
  unsigned acked() const { return m_acked; }
  unsigned lost() const { return m_lost; }
  unsigned retransmittable() const { return m_retransmittable; }
  unsigned retransmitPending() const { return m_retransmit.count(); }
  unsigned retransmitDropped() const { return m_retransmit.dropped(); }
  bool nextRetransmit(SentFrameRef &frame) {
    return m_retransmit.pop(frame);
  }
  unsigned reclaimOnPTO(unsigned limit) {
    unsigned n = 0;
    auto iter = m_packets.riter();
    while (n < limit) {
      auto node = iter();
      if (!node) break;
      SentPkt &p = node->data();
      if (p.acked || p.lost || !p.inFlight || !p.ackEliciting)
	continue;
      Tx::resend(Span{p.pn, 1});
      Tx::resend();
      ++n;
    }
    return n;
  }
  unsigned count() const { return m_packets.count_(); }
  void clear() {
    Tx::txReset(0);
    m_retransmit.clear();
    m_bytesInFlight = 0;
    m_acked = 0;
    m_lost = 0;
    m_retransmittable = 0;
  }
  bool persistentCongestion(ZuTime threshold) const {
    bool have = false;
    ZuTime first, last;
    auto iter = m_packets.citer();
    while (auto node = iter()) {
      const SentPkt &p = node->data();
      if (!p.lost || !p.ackEliciting || p.pmtudProbe) continue;
      if (!have || p.sentTime < first) first = p.sentTime;
      if (!have || p.sentTime > last) last = p.sentTime;
      have = true;
    }
    if (!have || last <= first || last - first < threshold) return false;
    iter.reset();
    while (auto node = iter()) {
      const SentPkt &p = node->data();
      if (p.acked && p.ackEliciting && !p.pmtudProbe &&
	  p.sentTime >= first && p.sentTime <= last)
	return false;
    }
    return true;
  }

private:
  bool ack_(SentPkt &p) {
    if (p.acked || p.lost) return false;
    p.acked = true;
    if (p.inFlight) release_(p);
    ++m_acked;
    return true;
  }

  bool lose_(SentPkt &p) {
    if (p.acked || p.lost) return false;
    p.lost = true;
    if (p.inFlight) release_(p);
    if (p.ackEliciting && !p.pmtudProbe) {
      ++m_retransmittable;
      enqueueRetransmit_(p);
    }
    ++m_lost;
    return true;
  }

  void release_(SentPkt &p) {
    p.inFlight = false;
    if (p.bytes > m_bytesInFlight) m_bytesInFlight = 0;
    else m_bytesInFlight -= p.bytes;
  }

  void enqueueRetransmit_(const SentPkt &p) {
    for (unsigned i = 0; i < p.framesUsed(); ++i)
      m_retransmit.push(p.frame(i));
  }

  Queue		m_packets{0};
  RetransmitQueue m_retransmit;
  uint64_t	m_bytesInFlight = 0;
  unsigned	m_acked = 0;
  unsigned	m_lost = 0;
  unsigned	m_retransmittable = 0;
};

using SentPktTracker = PktTxSpace;

class PTOBackoff {
public:
  ZuTime timeout(const RttEstimator &rtt, ZuTime maxAckDelay) const {
    return timePow2(rtt.pto(maxAckDelay), m_count);
  }
  void expired() { if (m_count < 16) ++m_count; }
  void reset() { m_count = 0; }
  unsigned count() const { return m_count; }

private:
  unsigned	m_count = 0;
};

} // namespace Zquic

#endif /* ZquicRecovery_HH */
