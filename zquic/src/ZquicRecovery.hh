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

#include <zlib/ZmFn.hh>
#include <zlib/ZmHash.hh>
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

inline constexpr ZuTime timeDiv(ZuTime t, uint64_t n)
{
  return ZuTime{ZuTime::Nano{t.nanosecs() / n}};
}

using AckTrackerRxNTP = ZmPQRxGapObserve<>;

class AckTracker :
  public ZmPQRx<AckTracker, PktRxPQueue, AckTrackerRxNTP> {
public:
  static constexpr unsigned Max = 64;
  static constexpr unsigned MaxRetained = Max + 1;
  using Queue = PktRxPQueue;
  using Rx = ZmPQRx<AckTracker, Queue, AckTrackerRxNTP>;
  using Msg = Queue::Node;
  using Span = Queue::Span;
  using DequeueFn = ZmFn<void()>;

  AckTracker() :
    m_dequeueFn{this, [](AckTracker *t) { t->dequeueRx_(); }} { }

  bool add(uint64_t pn) {
    if (contains(pn)) return true;
    Rx::rcvd(new Queue::Node{RxPktMark{pn}});
    trimSparseRanges_();
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
  bool multipleRanges() const {
    unsigned n = 0;
    bool stop = false;
    bool ok = ranges_([&n, &stop](const AckRange &) {
      if (++n < 2) return true;
      stop = true;
      return false;
    });
    return (ok || stop) && n > 1;
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
  bool largest(uint64_t &v) const {
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
    return found;
  }
  uint64_t largest() const {
    uint64_t v = 0;
    if (!largest(v)) return 0;
    return v;
  }
  int snapshot(AckRange *ranges, unsigned max) const {
    if (!ranges || !max) return 0;
    unsigned n = 0;
    bool stop = false;
    bool ok = ranges_([ranges, max, &n, &stop](const AckRange &range) {
      if (n >= max) {
	stop = true;
	return false;
      }
      ranges[n++] = range;
      return true;
    });
    if (!ok && !stop) return -1;
    return int(n);
  }
  int writeFrame(
    uint8_t *out, unsigned len, uint64_t delay = 0,
    const AckECN *ecn = nullptr) const
  {
    AckRange ranges[Max];
    int n = snapshot(ranges, Max);
    if (n <= 0) return -1;
    return ecn ?
      FrameCodec::writeAckECN(out, len, ranges, n, delay, *ecn) :
      FrameCodec::writeAckRanges(out, len, ranges, n, delay);
  }
  void clear() {
    Rx::rxReset(0);
    m_ackHead = 0;
    m_ackBase = 0;
    m_ackGap = {};
  }

  void ackdByPeer(uint64_t largest) {
    if (m_ackBase <= largest && m_ackHead > m_ackBase) {
      uint64_t base =
	ZuCmp<uint64_t>::null(largest) ? largest : largest + 1;
      m_ackBase = base < m_ackHead ? base : m_ackHead;
    }
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

  void scheduleDequeue() { m_dequeueFn(); }
  void rescheduleDequeue() { m_dequeueFn(); }
  void idleDequeue() { }
  void dequeueFn(DequeueFn fn) { m_dequeueFn = ZuMv(fn); }
  void dequeueRx_() { Rx::dequeue(); }

private:
  void refreshGap_() {
    Span gap = m_packets.gap();
    m_ackGap = gap.length() ? gap : Span{};
  }

  bool delRange_(uint64_t first, uint64_t largest) {
    bool changed = false;
    auto iter = m_packets.iter(first);
    while (auto node = iter()) {
      uint64_t pn = node->data().pn;
      if (pn > largest) break;
      (void)iter.del();
      changed = true;
    }
    if (changed) refreshGap_();
    return changed;
  }

  bool delThrough_(uint64_t largest) {
    return delRange_(0, largest);
  }

  bool firstSparseRange_(AckRange &range) const {
    Span ackGap = m_ackGap;
    uint64_t gapEnd = 0;
    bool haveGap =
      ackGap.length() && Queue::endOf(ackGap.key(), ackGap.length(), gapEnd);
    bool found = false;
    (void)m_packets.spans([&range, haveGap, gapEnd, &found](
	const auto &span) {
      uint64_t first = span.key();
      uint64_t end = 0;
      if (!Queue::endOf(first, span.length(), end)) return false;
      if (haveGap && first < gapEnd) {
	if (end <= gapEnd) return true;
	first = gapEnd;
      }
      range = AckRange{end - 1, first};
      found = true;
      return false;
    });
    return found;
  }

  void trimSparseRanges_() {
    while (count() > MaxRetained) {
      AckRange range;
      if (!firstSparseRange_(range)) return;
      if (!delRange_(range.first, range.largest)) return;
    }
  }

  template <typename L>
  bool ranges_(L &&l) const {
    if (m_ackHead > m_ackBase)
      if (!l(AckRange{m_ackHead - 1, m_ackBase})) return false;
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
  uint64_t	m_ackBase = 0;
  DequeueFn	m_dequeueFn;
};

class AckManager {
public:
  static constexpr unsigned Spaces = 3;
  enum { ActiveAckThreshold = 2 };

  bool received(
    PktSpace::T space, uint64_t pn, uint64_t now, uint64_t maxAckDelay,
    bool ackEliciting = true, bool immediate = false,
    EcnMark::T ecn = EcnMark::NotECT)
  {
    unsigned i = space;
    uint64_t largest = 0;
    bool haveLargest = m_ack[i].largest(largest);
    if (m_ack[i].contains(pn)) return false;
    if (!m_ack[i].add(pn)) return false;
    noteECN_(i, ecn);
    m_pending[i] = true;
    ++m_gen[i];
    if (!haveLargest || pn >= largest) m_largestRxTime[i] = now;
    if (ackEliciting) noteAckEliciting_(i, now, maxAckDelay, immediate);
    return true;
  }

  bool ackEliciting(
    PktSpace::T space, uint64_t pn, uint64_t now, uint64_t maxAckDelay,
    bool immediate = false)
  {
    unsigned i = space;
    if (!m_ack[i].contains(pn)) return false;
    if (m_ack[i].largest() == pn) m_largestRxTime[i] = now;
    noteAckEliciting_(i, now, maxAckDelay, immediate);
    return true;
  }

	  const AckTracker &tracker(PktSpace::T space) const {
	    return m_ack[space];
	  }
	  AckTracker &tracker(PktSpace::T space) {
	    return m_ack[space];
	  }
  bool pending(PktSpace::T space) const {
    return m_pending[space];
  }
  bool deadlineSet(PktSpace::T space) const {
    return m_deadlineSet[space];
  }
  bool immediate(PktSpace::T space) const {
    return m_immediate[space];
  }
  bool ackEliciting(PktSpace::T space) const {
    return m_ackEliciting[space];
  }
  uint64_t deadline(PktSpace::T space) const {
    return m_deadline[space];
  }
  uint64_t largestRxTime(PktSpace::T space) const {
    return m_largestRxTime[space];
  }
  uint64_t gen(PktSpace::T space) const {
    return m_gen[space];
  }
  const AckECN &ackECN(PktSpace::T space) const {
    return m_ecn[space];
  }
  bool due(PktSpace::T space, uint64_t now) const {
    unsigned i = space;
    return m_pending[i] &&
      (m_immediate[i] || (m_deadlineSet[i] && now >= m_deadline[i]));
  }
  int writeFrame(
    PktSpace::T space, uint8_t *out, unsigned len,
    uint64_t delay = 0, bool ecn = false) const
  {
    unsigned i = space;
    return m_ack[i].writeFrame(out, len, delay, ecn ? &m_ecn[i] : nullptr);
  }
  void sent(PktSpace::T space, uint64_t gen = ZuCmp<uint64_t>::null()) {
    unsigned i = space;
    if (!ZuCmp<uint64_t>::null(gen) && gen != m_gen[i]) return;
    m_pending[i] = false;
    m_ackEliciting[i] = false;
    m_activeAck[i] = 0;
    m_immediate[i] = false;
    m_deadlineSet[i] = false;
    m_deadline[i] = 0;
  }
  void clear() {
    for (unsigned i = 0; i < Spaces; ++i) {
      m_ack[i].clear();
      m_pending[i] = false;
      m_ackEliciting[i] = false;
      m_activeAck[i] = 0;
      m_immediate[i] = false;
      m_deadlineSet[i] = false;
      m_deadline[i] = 0;
      m_largestRxTime[i] = 0;
      m_gen[i] = 0;
      m_ecn[i].reset();
    }
  }

private:
  void noteAckEliciting_(
    unsigned i, uint64_t now, uint64_t maxAckDelay, bool immediate)
  {
    m_ackEliciting[i] = true;
    if (m_activeAck[i] < ActiveAckThreshold) ++m_activeAck[i];
    immediate = immediate || m_activeAck[i] >= ActiveAckThreshold;
    if (immediate) {
      m_immediate[i] = true;
      m_deadlineSet[i] = false;
      m_deadline[i] = 0;
    } else if (!m_immediate[i]) {
      uint64_t deadline =
	maxAckDelay > uint64_t(-1) - now ? uint64_t(-1) : now + maxAckDelay;
      if (!m_deadlineSet[i] || deadline < m_deadline[i])
	m_deadline[i] = deadline;
      m_deadlineSet[i] = true;
    }
  }

  void noteECN_(unsigned i, EcnMark::T ecn) {
    switch (ecn) {
      case EcnMark::ECT0: ++m_ecn[i].ect0; break;
      case EcnMark::ECT1: ++m_ecn[i].ect1; break;
      case EcnMark::CE: ++m_ecn[i].ce; break;
    }
  }

  AckTracker	m_ack[Spaces];
  AckECN	m_ecn[Spaces];
  bool		m_pending[Spaces] = {};
  bool		m_ackEliciting[Spaces] = {};
  unsigned	m_activeAck[Spaces] = {};
  bool		m_immediate[Spaces] = {};
  bool		m_deadlineSet[Spaces] = {};
  uint64_t	m_deadline[Spaces] = {};
  uint64_t	m_largestRxTime[Spaces] = {};
  uint64_t	m_gen[Spaces] = {};
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
    m_variance = (m_variance * ZuDecimal{3} + diff) / ZuDecimal{4};
    m_smoothed = (m_smoothed * ZuDecimal{7} + rtt) / ZuDecimal{8};
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
    ZuTime threshold =
      (rtt * ZuDecimal{TimeThresholdNumerator}) /
	ZuDecimal{TimeThresholdDenominator};
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
  void ackd(unsigned bytes) {
    ackd(bytes, false);
  }
  void ackd(unsigned bytes, bool pmtudProbe) {
    if (bytes > m_bytesInFlight) m_bytesInFlight = 0;
    else m_bytesInFlight -= bytes;
    if (pmtudProbe) return;
    if (m_cwnd < m_ssthresh)
      m_cwnd += bytes;
    else {
      uint64_t n = (uint64_t(m_maxDatagram) * bytes) / m_cwnd;
      m_cwnd += n ? n : 1;
    }
  }
  void release(uint64_t bytes) {
    if (bytes > m_bytesInFlight) m_bytesInFlight = 0;
    else m_bytesInFlight -= bytes;
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
  ZuOpBool

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
  static SentFrameRef pathChallenge(ZuCSpan data) {
    SentFrameRef ref = control();
    ref.controlType = FrameType::PathChallenge;
    if (data.length() == 8) memcpy(ref.payload, data.data(), 8);
    return ref;
  }

  static SentFrameRef handshakeDone() {
    SentFrameRef ref = control();
    ref.controlType = FrameType::HandshakeDone;
    return ref;
  }

  static SentFrameRef resetStream(
    uint64_t id, uint64_t appError, uint64_t finalSize) {
    SentFrameRef ref = control();
    ref.controlType = FrameType::ResetStream;
    ref.streamID = id;
    ref.value = appError;
    ref.length = finalSize;
    return ref;
  }

  static SentFrameRef stopSending(uint64_t id, uint64_t appError) {
    SentFrameRef ref = control();
    ref.controlType = FrameType::StopSending;
    ref.streamID = id;
    ref.value = appError;
    return ref;
  }
};

struct SentFrameKey {
  SentFrameKind::T	kind = SentFrameKind::None;
  FrameType::T		controlType = FrameType::Unknown;
  uint64_t		streamID = 0;
  uint64_t		offset = 0;
  uint64_t		length = 0;
  uint64_t		value = 0;
  bool			fin = false;

  SentFrameKey() = default;
  explicit SentFrameKey(const SentFrameRef &ref) :
    kind{ref.kind}, controlType{ref.controlType}, streamID{ref.streamID},
    offset{ref.offset}, length{ref.length}, value{ref.value}, fin{ref.fin} { }

  bool operator !() const { return kind == SentFrameKind::None; }
  ZuOpBool
  bool operator ==(const SentFrameKey &o) const {
    return kind == o.kind && controlType == o.controlType &&
      streamID == o.streamID && offset == o.offset &&
      length == o.length && value == o.value && fin == o.fin;
  }
  int cmp(const SentFrameKey &o) const {
    if (kind != o.kind) return int(kind) - int(o.kind);
    if (controlType != o.controlType)
      return int(controlType) - int(o.controlType);
    if (streamID != o.streamID) return streamID < o.streamID ? -1 : 1;
    if (offset != o.offset) return offset < o.offset ? -1 : 1;
    if (length != o.length) return length < o.length ? -1 : 1;
    if (value != o.value) return value < o.value ? -1 : 1;
    if (fin != o.fin) return int(fin) - int(o.fin);
    return 0;
  }
  uint32_t hash() const {
    ZuHash_FNV::Value h = ZuHash_FNV::initial_();
    h = ZuHash_FNV::hash_(h, unsigned(kind));
    h = ZuHash_FNV::hash_(h, unsigned(controlType));
    h = ZuHash_FNV::hash_(h, streamID);
    h = ZuHash_FNV::hash_(h, offset);
    h = ZuHash_FNV::hash_(h, length);
    h = ZuHash_FNV::hash_(h, value);
    h = ZuHash_FNV::hash_(h, unsigned(fin));
    return uint32_t(h);
  }

  static bool retransmittable(const SentFrameRef &ref) {
    return ref.kind == SentFrameKind::Stream ||
      ref.kind == SentFrameKind::Crypto ||
      (ref.kind == SentFrameKind::Control &&
	(ref.controlType == FrameType::MaxData ||
	  ref.controlType == FrameType::MaxStreamData ||
	  ref.controlType == FrameType::MaxStreams ||
	  ref.controlType == FrameType::DataBlocked ||
	  ref.controlType == FrameType::StreamDataBlocked ||
	  ref.controlType == FrameType::StreamsBlocked ||
	  ref.controlType == FrameType::ResetStream ||
	  ref.controlType == FrameType::StopSending));
  }
};

} // namespace Zquic

template <> struct ZuCmp<Zquic::SentFrameKey> {
  static int cmp(const Zquic::SentFrameKey &l, const Zquic::SentFrameKey &r) {
    return l.cmp(r);
  }
  static bool equals(
    const Zquic::SentFrameKey &l, const Zquic::SentFrameKey &r) {
    return l == r;
  }
  static bool null(const Zquic::SentFrameKey &key) { return !key; }
  static Zquic::SentFrameKey null() { return {}; }
};

template <> struct ZuHash<Zquic::SentFrameKey> {
  static uint32_t hash(const Zquic::SentFrameKey &key) {
    return key.hash();
  }
};

namespace Zquic {

inline const SentFrameKey &SentFrameKey_IDAxor(const SentFrameKey &key)
{
  return key;
}

ZuDerive(SentFrameAckHash,
  (ZmHash<SentFrameKey,
    ZmHashNode<SentFrameKey,
      ZmHashKey<SentFrameKey_IDAxor,
	ZmHashHeapID<"Zquic.Pkt.AckdFrame">>>>));

struct SentPkt {
  static constexpr unsigned MaxFrames = 8;

  uint64_t key() const { return pn; }
  uint64_t length() const { return 1; }
  uint64_t clipHead(uint64_t) { return 0; }
  uint64_t clipTail(uint64_t) { return 0; }
  template <typename I>
  void write(const I &) { }

  bool addFrame(const SentFrameRef &frame, void *owner = nullptr) {
    if (frame.kind == SentFrameKind::None || frameCount >= MaxFrames)
      return false;
    frames[frameCount] = frame;
    frameOwners[frameCount++] = owner;
    return true;
  }
  unsigned framesUsed() const { return frameCount; }
  const SentFrameRef &frame(unsigned i) const {
    ZiAssert(i < frameCount, "Zquic", (i, frameCount),
      "sent-packet frame index out of bounds", return frames[0]);
    return frames[i];
  }
  void *frameOwner(unsigned i) const {
    ZiAssert(i < frameCount, "Zquic", (i, frameCount),
      "sent-packet frame-owner index out of bounds", return nullptr);
    return frameOwners[i];
  }

  uint64_t	pn = 0;
  ZuTime	sentTime;
  unsigned	bytes = 0;
  PktSpace::T space = PktSpace::AppData;
  bool		ackEliciting = false;
  bool		inFlight = false;
  bool		pmtudProbe = false;
  unsigned	pmtudSize = 0;
  bool		ackd = false;
  bool		lost = false;
  bool		ptoReclaimed = false;
  uint8_t	ackLevel = 3;
  uint64_t	ackLargest = 0;
  SentFrameRef	frames[MaxFrames];
  void		*frameOwners[MaxFrames] = {};
  unsigned	frameCount = 0;
};

using TxPkt = SentPkt;

struct PktTxUpdate {
  static constexpr unsigned MaxFrames = 64;

  void ackd(const SentPkt &p) {
    ackdAck_(p);
    ackdFrames_(p);
    if (p.lost) return;
    ackdBytes += p.bytes;
    if (p.pmtudProbe) {
      pmtudAckdBytes += p.bytes;
      if (p.pmtudSize > pmtudAckdSize)
	pmtudAckdSize = p.pmtudSize;
    } else
      normalAckdBytes += p.bytes;
  }
  void lost(const SentPkt &p) {
    lostFrames_(p);
    lostBytes += p.bytes;
    if (p.pmtudProbe) {
      pmtudLostBytes += p.bytes;
      if (p.pmtudSize > pmtudLostSize)
	pmtudLostSize = p.pmtudSize;
      if (p.sentTime > pmtudLostSentTime)
	pmtudLostSentTime = p.sentTime;
    } else {
      normalLostBytes += p.bytes;
      if (p.sentTime > normalLostSentTime)
	normalLostSentTime = p.sentTime;
    }
  }
  void ackdAck_(const SentPkt &p) {
    if (p.ackLevel >= 3) return;
    if (!ackdAck[p.ackLevel] || p.ackLargest > ackLargest[p.ackLevel]) {
      ackdAck[p.ackLevel] = true;
      ackLargest[p.ackLevel] = p.ackLargest;
    }
  }
  void ackdFrames_(const SentPkt &p) {
    for (unsigned i = 0; i < p.framesUsed() && nAckdFrames < MaxFrames; ++i) {
      ackdFrames[nAckdFrames++] = p.frame(i);
      ackdOwners[nAckdFrames - 1] = p.frameOwner(i);
    }
  }
  void lostFrames_(const SentPkt &p) {
    for (unsigned i = 0; i < p.framesUsed() && nLostFrames < MaxFrames; ++i) {
      lostFrames[nLostFrames++] = p.frame(i);
      lostOwners[nLostFrames - 1] = p.frameOwner(i);
    }
  }

  uint64_t	ackdBytes = 0;
  CryptoLevel::T	level = CryptoLevel::Initial;
  uint64_t	normalAckdBytes = 0;
  uint64_t	pmtudAckdBytes = 0;
  unsigned	pmtudAckdSize = 0;
  uint64_t	lostBytes = 0;
  uint64_t	normalLostBytes = 0;
  uint64_t	pmtudLostBytes = 0;
  unsigned	pmtudLostSize = 0;
  ZuTime	normalLostSentTime;
  ZuTime	pmtudLostSentTime;
  bool		ackdAck[3] = {};
  uint64_t	ackLargest[3] = {};
  SentFrameRef	ackdFrames[MaxFrames];
  SentFrameRef	lostFrames[MaxFrames];
  void		*ackdOwners[MaxFrames] = {};
  void		*lostOwners[MaxFrames] = {};
  unsigned	nAckdFrames = 0;
  unsigned	nLostFrames = 0;
};

struct PktAckBatch {
  unsigned	range = 0;
  uint64_t	nextPN = 0;
  uint64_t	largestAckdForLoss = 0;
  uint64_t	latestAckd = 0;
  unsigned	ackd = 0;
  bool		haveAckForLoss = false;
  bool		haveAckd = false;
  ZuTime	latestSentTime;
};

struct PktLossBatch {
  uint64_t	nextPN = 0;
  unsigned	lost = 0;
};

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
    if (SentFrameKey::retransmittable(frame)) {
      SentFrameKey key{frame};
      if (m_pending.findPtr(key)) {
	++m_dropped;
	return false;
      }
      m_pending.add(key);
    }
    m_frames.push(frame);
    return true;
  }

  bool pop(SentFrameRef &frame) {
    if (!m_frames.count_()) return false;
    frame = m_frames.shift();
    if (SentFrameKey::retransmittable(frame))
      m_pending.del(SentFrameKey{frame});
    return true;
  }

  unsigned count() const { return m_frames.count_(); }
  unsigned dropped() const { return m_dropped; }
  bool empty() const { return !m_frames.count_(); }
  void clear() {
    m_frames.clean();
    m_pending.clean();
    m_dropped = 0;
  }

private:
  Queue		m_frames;
  SentFrameAckHash m_pending{
    ZmHashParams().bits(8).loadFactor(1).cBits(3)};
  unsigned	m_dropped = 0;
};

class PktTxSpace :
  public ZmPQTx<PktTxSpace, TxPktQueue> {
public:
  static constexpr unsigned RetainedLostMax = 1024;
  using Queue = TxPktQueue;
  using LostPNs =
    ZmQueue<uint64_t,
      ZmQueueHeapID<"Zquic.Pkt.TxLostPNs">>;
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
    if (p.inFlight) {
      m_bytesInFlight += p.bytes;
      if (p.ackEliciting &&
	  (!*m_latestAckSentTime || p.sentTime > m_latestAckSentTime))
	m_latestAckSentTime = p.sentTime;
    }
    return true;
  }

  bool ack(uint64_t pn) {
    auto node = m_packets.find(pn);
    if (!node || !ack_(node->data())) return false;
    (void)m_packets.abort(pn);
    return true;
  }

  unsigned ack(
    const AckTracker &ranges, unsigned *lost = nullptr,
    unsigned packetThreshold = 3)
  {
    unsigned n = 0;
    auto iter = m_packets.iter();
    while (auto node = iter()) {
      SentPkt &p = node->data();
      bool wasLost = p.lost;
      if (ranges.contains(p.pn) && ack_(p)) {
	(void)iter.del();
	if (!wasLost) ++n;
      }
    }
    unsigned l = markPktThresholdLoss(ranges.largest(), packetThreshold);
    if (lost) *lost = l;
    return n;
  }

  unsigned ack(
    const AckRange *ranges, unsigned nRanges, unsigned *lost = nullptr,
    unsigned packetThreshold = 3, ZuTime *latestSentTime = nullptr,
    uint64_t *ackdBytes = nullptr, uint64_t *lostBytes = nullptr,
    ZuTime *lostSentTime = nullptr, PktTxUpdate *update = nullptr)
  {
    unsigned n = 0;
    uint64_t ackdBytes_ = 0;
    uint64_t largestAckdForLoss = 0;
    uint64_t latestAckd = 0;
    bool haveAckForLoss = false;
    bool haveAckd = false;
    if (latestSentTime) *latestSentTime = ZuTime{0};
    if (ackdBytes) *ackdBytes = 0;
    if (lostBytes) *lostBytes = 0;
    if (lostSentTime) *lostSentTime = ZuTime{0};
    for (unsigned i = 0; i < nRanges; ++i) {
      const AckRange &range = ranges[i];
      if (range.first > range.largest) continue;
      auto iter = m_packets.iter(range.first);
	while (auto node = iter()) {
	SentPkt &p = node->data();
	if (p.pn > range.largest) break;
	bool wasLost = p.lost;
	if (ack_(p)) {
	  if (!wasLost) ackdBytes_ += p.bytes;
	  if (update) update->ackd(p);
	  if (!wasLost) {
	    ++n;
	    if (!haveAckForLoss || p.pn > largestAckdForLoss) {
	      largestAckdForLoss = p.pn;
	      haveAckForLoss = true;
	    }
	    if (latestSentTime && (!haveAckd || p.pn > latestAckd)) {
	      latestAckd = p.pn;
	      *latestSentTime = p.sentTime;
	      haveAckd = true;
	    }
	  }
	  (void)iter.del();
	}
      }
    }
    unsigned l = haveAckForLoss ?
      markPktThresholdLoss(
	largestAckdForLoss, packetThreshold, lostBytes, lostSentTime,
	update) : 0;
    if (ackdBytes) *ackdBytes = ackdBytes_;
    if (lost) *lost = l;
    return n;
  }

  bool ackBatch(
    const AckRange *ranges, unsigned nRanges, PktAckBatch &batch,
    unsigned budget, CryptoLevel::T level, PktTxUpdate *update = nullptr)
  {
    if (!budget) return false;
    if (update) update->level = level;
    unsigned scanned = 0;
    while (batch.range < nRanges) {
      const AckRange &range = ranges[batch.range];
      if (range.first > range.largest) {
	++batch.range;
	batch.nextPN = 0;
	continue;
      }
      uint64_t start = batch.nextPN ? batch.nextPN : range.first;
      auto iter = m_packets.iter(start);
      while (auto node = iter()) {
	SentPkt &p = node->data();
	if (p.pn > range.largest) break;
	uint64_t nextPN = p.pn + 1;
	bool wasLost = p.lost;
	if (ack_(p)) {
	  if (update) update->ackd(p);
	  if (!wasLost) {
	    ++batch.ackd;
	    if (!batch.haveAckForLoss || p.pn > batch.largestAckdForLoss) {
	      batch.largestAckdForLoss = p.pn;
	      batch.haveAckForLoss = true;
	    }
	    if (level == CryptoLevel::OneRTT &&
		(!batch.haveAckd || p.pn > batch.latestAckd)) {
	      batch.latestAckd = p.pn;
	      batch.latestSentTime = p.sentTime;
	      batch.haveAckd = true;
	    }
	  }
	  (void)iter.del();
	}
	if (++scanned >= budget) {
	  batch.nextPN = nextPN;
	  return false;
	}
      }
      ++batch.range;
      batch.nextPN = 0;
    }
    return true;
  }

  bool lose(uint64_t pn) {
    auto node = m_packets.find(pn);
    return node && lose_(node->data());
  }

  unsigned markPktThresholdLoss(
    uint64_t largestAckd, unsigned threshold = 3,
    uint64_t *lostBytes = nullptr, ZuTime *lostSentTime = nullptr,
    PktTxUpdate *update = nullptr) {
    unsigned n = 0;
    if (lostBytes) *lostBytes = 0;
    if (lostSentTime) *lostSentTime = ZuTime{0};
    auto iter = m_packets.iter();
    while (auto node = iter()) {
      SentPkt &p = node->data();
      if (p.ackd || p.lost || p.pn + threshold > largestAckd) continue;
      if (lose_(p)) {
	++n;
	if (lostBytes) *lostBytes += p.bytes;
	if (lostSentTime && p.sentTime > *lostSentTime)
	  *lostSentTime = p.sentTime;
	if (update) update->lost(p);
      }
    }
    return n;
  }

  bool markPktThresholdLossBatch(
    uint64_t largestAckd, unsigned threshold, PktLossBatch &batch,
    unsigned budget, PktTxUpdate *update = nullptr)
  {
    if (!budget) return false;
    unsigned scanned = 0;
    auto iter = m_packets.iter(batch.nextPN);
    while (auto node = iter()) {
      SentPkt &p = node->data();
      batch.nextPN = p.pn + 1;
      if (!p.ackd && !p.lost && p.pn + threshold <= largestAckd &&
	  lose_(p)) {
	++batch.lost;
	if (update) update->lost(p);
      }
      if (++scanned >= budget) return false;
    }
    batch.nextPN = 0;
    return true;
  }

  unsigned markTimeThresholdLoss(
    ZuTime now, ZuTime threshold,
    uint64_t *lostBytes = nullptr, ZuTime *lostSentTime = nullptr,
    PktTxUpdate *update = nullptr) {
    unsigned n = 0;
    if (lostBytes) *lostBytes = 0;
    if (lostSentTime) *lostSentTime = ZuTime{0};
    auto iter = m_packets.iter();
    while (auto node = iter()) {
      SentPkt &p = node->data();
      if (p.ackd || p.lost || !*p.sentTime || p.sentTime > now ||
	  now - p.sentTime < threshold)
	continue;
      if (lose_(p)) {
	++n;
	if (lostBytes) *lostBytes += p.bytes;
	if (lostSentTime && p.sentTime > *lostSentTime)
	  *lostSentTime = p.sentTime;
	if (update) update->lost(p);
      }
    }
    return n;
  }
  bool markTimeThresholdLossBatch(
    ZuTime now, ZuTime threshold, PktLossBatch &batch,
    unsigned budget, PktTxUpdate *update = nullptr)
  {
    if (!budget) return false;
    unsigned scanned = 0;
    auto iter = m_packets.iter(batch.nextPN);
    while (auto node = iter()) {
      SentPkt &p = node->data();
      batch.nextPN = p.pn + 1;
      if (!p.ackd && !p.lost && *p.sentTime && p.sentTime <= now &&
	  now - p.sentTime >= threshold && lose_(p)) {
	++batch.lost;
	if (update) update->lost(p);
      }
      if (++scanned >= budget) return false;
    }
    batch.nextPN = 0;
    return true;
  }
  ZuTime nextLossTime(ZuTime threshold, unsigned budget = 256) const {
    if (!*threshold) return ZuTime{0};
    ZuTime out;
    bool have = false;
    auto iter = m_packets.citer();
    unsigned scanned = 0;
    while (auto node = iter()) {
      const SentPkt &p = node->data();
      if (!p.ackd && !p.lost && p.inFlight && p.ackEliciting &&
	  *p.sentTime) {
	ZuTime deadline = p.sentTime + threshold;
	if (!have || deadline < out) {
	  out = deadline;
	  have = true;
	}
      }
      if (++scanned >= budget) break;
    }
    return have ? out : ZuTime{0};
  }

  uint64_t bytesInFlight() const { return m_bytesInFlight; }
  ZuTime latestAckSentTime() const { return m_latestAckSentTime; }
  unsigned ackd() const { return m_ackd; }
  unsigned lost() const { return m_lost; }
  unsigned retainedLost() const { return m_retainedLost; }
  unsigned retransmittable() const { return m_retransmittable; }
  unsigned retransmitPending() const { return m_retransmit.count(); }
  unsigned retransmitDropped() const { return m_retransmit.dropped(); }
  bool nextRetransmit(SentFrameRef &frame) {
    while (m_retransmit.pop(frame))
      if (!frameOutstanding_(frame)) return true;
    return false;
  }
  bool requeueRetransmit(const SentFrameRef &frame) {
    if (frameOutstanding_(frame)) return false;
    return m_retransmit.push(frame);
  }
  unsigned reclaimOnPTO(unsigned limit) {
    unsigned n = 0;
    auto reclaim = [this, limit, &n](bool reclaimed) {
      auto iter = m_packets.riter();
      while (n < limit) {
	auto node = iter();
	if (!node) break;
	SentPkt &p = node->data();
	if (p.ackd || p.lost || !p.inFlight || !p.ackEliciting ||
	    (!reclaimed && p.ptoReclaimed))
	  continue;
	Tx::resend(Span{p.pn, 1});
	Tx::resend();
	p.ptoReclaimed = true;
	++n;
      }
    };
    reclaim(false);
    if (!n) reclaim(true);
    return n;
  }
  unsigned count() const { return m_packets.count_(); }
  void clear() {
    Tx::txReset(0);
    m_retransmit.clear();
    m_lostPNs.clean();
    m_bytesInFlight = 0;
    m_latestAckSentTime = {};
    m_ackd = 0;
    m_lost = 0;
    m_retainedLost = 0;
    m_retransmittable = 0;
  }
  bool persistentCongestion(ZuTime threshold, unsigned budget = 256) const {
    bool have = false;
    ZuTime first, last;
    auto iter = m_packets.citer();
    unsigned scanned = 0;
    while (auto node = iter()) {
      const SentPkt &p = node->data();
      if (p.lost && p.ackEliciting && !p.pmtudProbe) {
	if (!have || p.sentTime < first) first = p.sentTime;
	if (!have || p.sentTime > last) last = p.sentTime;
	have = true;
      }
      if (++scanned >= budget) return false;
    }
    if (!have || last <= first || last - first < threshold) return false;
    iter.reset();
    scanned = 0;
    while (auto node = iter()) {
      const SentPkt &p = node->data();
      if (p.ackd && p.ackEliciting && !p.pmtudProbe &&
	  p.sentTime >= first && p.sentTime <= last)
	return false;
      if (++scanned >= budget) return false;
    }
    return true;
  }

private:
  bool ack_(SentPkt &p) {
    if (p.ackd) return false;
    p.ackd = true;
    if (p.lost) {
      m_lostPNs.del(p.pn);
      if (m_retainedLost) --m_retainedLost;
      return true;
    }
    if (p.inFlight) release_(p);
    ++m_ackd;
    return true;
  }

  bool lose_(SentPkt &p) {
    if (p.ackd || p.lost) return false;
    p.lost = true;
    m_lostPNs.push(p.pn);
    ++m_retainedLost;
    if (p.inFlight) release_(p);
    if (p.ackEliciting && !p.pmtudProbe) {
      ++m_retransmittable;
      enqueueRetransmit_(p);
    }
    trimLost_();
    ++m_lost;
    return true;
  }

  void release_(SentPkt &p) {
    p.inFlight = false;
    if (p.bytes > m_bytesInFlight) m_bytesInFlight = 0;
    else m_bytesInFlight -= p.bytes;
  }

  void enqueueRetransmit_(const SentPkt &p) {
    for (unsigned i = 0; i < p.framesUsed(); ++i) {
      const SentFrameRef &frame = p.frame(i);
      if (frameOutstanding_(frame, &p)) continue;
      m_retransmit.push(frame);
    }
  }

  bool frameOutstanding_(
    const SentFrameRef &frame, const SentPkt *skip = nullptr) const {
    if (!SentFrameKey::retransmittable(frame)) return false;
    SentFrameKey key{frame};
    auto iter = m_packets.citer();
    while (auto node = iter()) {
      const SentPkt &p = node->data();
      if (&p == skip || p.ackd || p.lost || p.ptoReclaimed) continue;
      for (unsigned i = 0; i < p.framesUsed(); ++i)
	if (SentFrameKey{p.frame(i)} == key) return true;
    }
    return false;
  }
  void trimLost_() {
    while (m_retainedLost > RetainedLostMax) {
      uint64_t pn = m_lostPNs.shift();
      auto node = m_packets.find(pn);
      if (!node) continue;
      SentPkt &p = node->data();
      if (!p.lost || p.ackd) continue;
      (void)m_packets.abort(pn);
      --m_retainedLost;
    }
  }

  Queue		m_packets{0};
  RetransmitQueue m_retransmit;
  LostPNs	m_lostPNs{ZmQueueParams{}.initial(RetainedLostMax)};
  uint64_t	m_bytesInFlight = 0;
  ZuTime	m_latestAckSentTime;
  unsigned	m_ackd = 0;
  unsigned	m_lost = 0;
  unsigned	m_retainedLost = 0;
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
