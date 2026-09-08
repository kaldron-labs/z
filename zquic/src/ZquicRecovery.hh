//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC recovery utilities

#ifndef Zquic_HH
#error "include zlib/Zquic.hh before this header"
#endif

#include <string.h>

#include <zlib/ZmFn.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmQueue.hh>
#include <zlib/ZmRBTree.hh>

namespace Zquic {

inline constexpr uint64_t TimeUSPerMS = 1000;
inline constexpr uint64_t TimeMSPerSec = 1000;
inline constexpr uint64_t TimeUSPerSec = TimeUSPerMS * TimeMSPerSec;
inline constexpr uint64_t TimeNSPerUS = 1000;
inline constexpr uint64_t TimeNSPerMS = TimeNSPerUS * TimeUSPerMS;

inline constexpr ZuTime timeUS(uint64_t usec)
{
  return ZuTime{
    int64_t(usec / TimeUSPerSec),
    int32_t((usec % TimeUSPerSec) * TimeNSPerUS)};
}

inline constexpr ZuTime timePow2(ZuTime t, unsigned n)
{
  return ZuTime{ZuTime::Nano{t.nanosecs() << n}};
}

inline constexpr ZuTime timeDiv(ZuTime t, uint64_t n)
{
  return ZuTime{ZuTime::Nano{t.nanosecs() / n}};
}

inline unsigned ackRangeLowerBound(
  ZuSpan<const AckRange> ranges, uint64_t pn)
{
  unsigned l = 0, r = ranges.length();
  while (l < r) {
    unsigned m = (l + r) >> 1;
    if (ranges[m].largest < pn)
      l = m + 1;
    else
      r = m;
  }
  return l;
}

class AckTracker {
public:
  static constexpr unsigned Max = 64;
  static constexpr unsigned MaxRetained = Max + 1;
  static constexpr unsigned BuiltinRetained = 32;
  using RetainedRanges = ZtBuiltin<
    ZtArray<AckRange,
      ZtArrayHeapMax<MaxRetained,
	ZtArrayHeapID<"Zquic.Ack.RetainedRanges">>>,
    BuiltinRetained>;
  using DequeueFn = ZmFn<void(), ZmFnHeapID<"Zquic.Ack.DequeueFn">>;

  bool add(uint64_t pn) {
    if (contains(pn)) return true;
    if (pn == m_ackHead)
      advanceHead_(pn + 1);
    else
      insertSparse_(pn);
    trimSparseRanges_();
    return true;
  }

  bool contains(uint64_t pn) const {
    if (pn < m_ackHead) return true;
    unsigned i = findSparse_(pn);
    return i < m_sparse.length() && m_sparse[i].first <= pn;
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
  // These dependent index APIs address the bounded ACK wire-order view, which
  // combines the implicit head with sparse storage and has no direct index.
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
    if (m_sparse) {
      v = m_sparse[m_sparse.length() - 1].largest;
      found = true;
    }
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
  Frame::AckRanges snapshot(unsigned max = Max) const {
    if (max > Max) max = Max;
    return snapshot_(max);
  }
  int snapshot(AckRange *ranges, unsigned max) const {
    if (!ranges || !max) return 0;
    if (max > Max) max = Max;
    Frame::AckRanges retained = snapshot_(max);
    for (unsigned i = 0, n = retained.length(); i < n; ++i)
      ranges[i] = retained[i];
    return int(retained.length());
  }
  int writeFrame(
    uint8_t *out, unsigned len, uint64_t delay = 0,
    const AckECN *ecn = nullptr) const
  {
    Frame::AckRanges ranges = snapshot_(Max);
    if (!ranges) return -1;
    return ecn ?
      FrameCodec::writeAckECN(
	out, len, ranges.data(), ranges.length(), delay, *ecn) :
      FrameCodec::writeAckRanges(
	out, len, ranges.data(), ranges.length(), delay);
  }
  void clear() {
    m_ackHead = 0;
    m_ackBase = 0;
    m_sparse.clear();
    m_retiredRanges = 0;
  }

  void ackdByPeer(uint64_t largest) {
    if (m_ackBase <= largest && m_ackHead > m_ackBase) {
      uint64_t base =
	ZuNull(largest) ? largest : largest + 1;
      m_ackBase = base < m_ackHead ? base : m_ackHead;
    }
  }

  void dequeueFn(DequeueFn fn) { m_dequeueFn = ZuMv(fn); }
  void dequeueRx_() { }
  uint64_t retiredRanges() const { return m_retiredRanges; }

private:
  static bool before_(uint64_t largest, uint64_t first) {
    return largest < first && first - largest > 1;
  }

  unsigned findSparse_(uint64_t pn) const {
    return ackRangeLowerBound(m_sparse.cspan(), pn);
  }

  void insertSparse_(uint64_t pn) {
    AckRange range{pn, pn};
    unsigned i = findSparse_(pn);
    unsigned begin = i;
    if (i && !before_(m_sparse[i - 1].largest, range.first)) {
      begin = --i;
      range.first = m_sparse[begin].first;
      if (m_sparse[begin].largest > range.largest)
	range.largest = m_sparse[begin].largest;
    }
    unsigned end = i;
    unsigned n = m_sparse.length();
    while (end < n &&
	!before_(range.largest, m_sparse[end].first)) {
      if (m_sparse[end].largest > range.largest)
	range.largest = m_sparse[end].largest;
      ++end;
    }
    if (m_sparse.length() == MaxRetained && begin == end) {
      m_sparse.splice(0, 1);
      ++m_retiredRanges;
      if (begin) --begin;
      if (end) --end;
    }
    m_sparse.splice(
      begin, end - begin, ZuSpan<const AckRange>{&range, 1});
  }

  void advanceHead_(uint64_t head) {
    m_ackHead = head;
    unsigned consumed = 0, n = m_sparse.length();
    while (consumed < n &&
	m_sparse[consumed].first <= m_ackHead) {
      if (m_sparse[consumed].largest >= m_ackHead)
	m_ackHead = m_sparse[consumed].largest + 1;
      ++consumed;
    }
    if (consumed) m_sparse.splice(0, consumed);
  }

  void trimSparseRanges_() {
    unsigned total = m_sparse.length() + (m_ackHead > m_ackBase);
    if (total <= MaxRetained || !m_sparse) return;
    unsigned retire = total - MaxRetained;
    m_sparse.splice(0, retire);
    m_retiredRanges += retire;
  }

  Frame::AckRanges snapshot_(unsigned max) const {
    Frame::AckRanges ranges;
    unsigned total = m_sparse.length() + (m_ackHead > m_ackBase);
    unsigned skip = total > max ? total - max : 0;
    ranges_([&ranges, &skip](const AckRange &range) {
      if (skip) {
	--skip;
	return true;
      }
      ranges.push(range);
      return true;
    });
    return ranges;
  }

  template <typename L>
  bool ranges_(L &&l) const {
    if (m_ackHead > m_ackBase)
      if (!l(AckRange{m_ackHead - 1, m_ackBase})) return false;
    for (unsigned i = 0, n = m_sparse.length(); i < n; ++i)
      if (!l(m_sparse[i])) return false;
    return true;
  }

  uint64_t	m_ackHead = 0;
  uint64_t	m_ackBase = 0;
  RetainedRanges m_sparse;
  uint64_t	m_retiredRanges = 0;
  DequeueFn	m_dequeueFn;
};

class AckManager {
public:
  enum { ActiveAckThreshold = 2 };

  bool received(
    PktNumSpace::T space, uint64_t pn, uint64_t now, uint64_t maxAckDelay,
    bool ackEliciting = true, bool immediate = false,
    EcnMark::T ecn = EcnMark::NotECT)
  {
    unsigned i = space;
    uint64_t largest = 0;
    bool haveLargest = m_ack[i].largest(largest);
    if (m_ack[i].contains(pn)) return false;
    if (!m_ack[i].add(pn)) return false;
    noteECN_(i, ecn);
    if (!haveLargest || pn >= largest) m_largestRxTime[i] = now;
    if (ackEliciting) {
      ++m_gen[i];
      m_pending[i] = true;
      noteAckEliciting_(i, now, maxAckDelay, immediate);
    }
    return true;
  }

  bool ackEliciting(
    PktNumSpace::T space, uint64_t pn, uint64_t now, uint64_t maxAckDelay,
    bool immediate = false)
  {
    unsigned i = space;
    if (!m_ack[i].contains(pn)) return false;
    ++m_gen[i];
    if (m_ack[i].largest() == pn) m_largestRxTime[i] = now;
    m_pending[i] = true;
    noteAckEliciting_(i, now, maxAckDelay, immediate);
    return true;
  }

  const AckTracker &tracker(PktNumSpace::T space) const {
    return m_ack[space];
  }
  AckTracker &tracker(PktNumSpace::T space) {
    return m_ack[space];
  }
  bool pending(PktNumSpace::T space) const {
    return m_pending[space];
  }
  bool post(PktNumSpace::T space) {
    unsigned i = space;
    if (!m_pending[i] || m_postedGen[i] == m_gen[i]) return false;
    m_postedGen[i] = m_gen[i];
    return true;
  }
  bool deadlineSet(PktNumSpace::T space) const {
    return m_deadlineSet[space];
  }
  bool immediate(PktNumSpace::T space) const {
    return m_immediate[space];
  }
  bool ackEliciting(PktNumSpace::T space) const {
    return m_ackEliciting[space];
  }
  uint64_t deadline(PktNumSpace::T space) const {
    return m_deadline[space];
  }
  uint64_t largestRxTime(PktNumSpace::T space) const {
    return m_largestRxTime[space];
  }
  uint64_t gen(PktNumSpace::T space) const {
    return m_gen[space];
  }
  const AckECN &ackECN(PktNumSpace::T space) const {
    return m_ecn[space];
  }
  bool due(PktNumSpace::T space, uint64_t now) const {
    unsigned i = space;
    return m_pending[i] &&
      (m_immediate[i] || (m_deadlineSet[i] && now >= m_deadline[i]));
  }
  int writeFrame(
    PktNumSpace::T space, uint8_t *out, unsigned len,
    uint64_t delay = 0, bool ecn = false) const
  {
    unsigned i = space;
    return m_ack[i].writeFrame(out, len, delay, ecn ? &m_ecn[i] : nullptr);
  }
  void sent(PktNumSpace::T space, uint64_t gen = U64Null) {
    unsigned i = space;
    if (!ZuNull(gen) && gen != m_gen[i]) return;
    m_pending[i] = false;
    m_ackEliciting[i] = false;
    m_activeAck[i] = 0;
    m_immediate[i] = false;
    m_deadlineSet[i] = false;
    m_deadline[i] = 0;
  }
  void clear() {
    for (unsigned i = 0; i < PktNumSpace::N; ++i) {
      m_ack[i].clear();
      m_pending[i] = false;
      m_ackEliciting[i] = false;
      m_activeAck[i] = 0;
      m_immediate[i] = false;
      m_deadlineSet[i] = false;
      m_deadline[i] = 0;
      m_largestRxTime[i] = 0;
      m_gen[i] = 0;
      m_postedGen[i] = 0;
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

  ZuArray<AckTracker, PktNumSpace::N> m_ack =
    ZuArray<AckTracker, PktNumSpace::N>(PktNumSpace::N);
  ZuArray<AckECN, PktNumSpace::N> m_ecn =
    ZuArray<AckECN, PktNumSpace::N>(PktNumSpace::N);
  ZuArray<bool, PktNumSpace::N> m_pending =
    fixedArray<bool, PktNumSpace::N>();
  ZuArray<bool, PktNumSpace::N> m_ackEliciting =
    fixedArray<bool, PktNumSpace::N>();
  ZuArray<unsigned, PktNumSpace::N> m_activeAck =
    fixedArray<unsigned, PktNumSpace::N>();
  ZuArray<bool, PktNumSpace::N> m_immediate =
    fixedArray<bool, PktNumSpace::N>();
  ZuArray<bool, PktNumSpace::N> m_deadlineSet =
    fixedArray<bool, PktNumSpace::N>();
  ZuArray<uint64_t, PktNumSpace::N> m_deadline =
    fixedArray<uint64_t, PktNumSpace::N>();
  ZuArray<uint64_t, PktNumSpace::N> m_largestRxTime =
    fixedArray<uint64_t, PktNumSpace::N>();
  ZuArray<uint64_t, PktNumSpace::N> m_gen =
    fixedArray<uint64_t, PktNumSpace::N>();
  ZuArray<uint64_t, PktNumSpace::N> m_postedGen =
    fixedArray<uint64_t, PktNumSpace::N>();
};

class RttEstimator {
public:
  static constexpr unsigned InitialRTTMS = 333;
  static constexpr unsigned GranularityMS = 1;
  static constexpr ZuTime InitialRTT{
    0, int32_t(InitialRTTMS * TimeNSPerMS)};
  static constexpr ZuTime Granularity{
    0, int32_t(GranularityMS * TimeNSPerMS)};
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
    congestionEventAt(sentTime);
  }
  bool congestionEventAt(uint64_t sentTime) {
    if (m_recoveryStartTime && sentTime <= m_recoveryStartTime) return false;
    m_recoveryStartTime = sentTime;
    m_ssthresh = m_cwnd >> 1;
    if (m_ssthresh < uint64_t(m_maxDatagram) * 2)
      m_ssthresh = uint64_t(m_maxDatagram) * 2;
    m_cwnd = m_ssthresh;
    return true;
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
  Zquic::StreamType::T	streamType = Zquic::StreamType::Duplex;
  bool			fin = false;
  PathData		payload;
  CxnID			cxnID;
  ResetToken		resetToken;
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
    Zquic::StreamType::T streamType_ = Zquic::StreamType::Duplex) {
    SentFrameRef ref = control();
    ref.controlType = type;
    ref.streamID = streamID_;
    ref.value = limit;
    ref.streamType = streamType_;
    return ref;
  }

  static SentFrameRef pathResponse(ZuBSpan data) {
    SentFrameRef ref = control();
    ref.controlType = FrameType::PathResponse;
    ref.payload = data.length() == PathChallenge::Length ?
      PathData{data} : PathData(PathChallenge::Length, true);
    return ref;
  }
  static SentFrameRef pathChallenge(ZuBSpan data) {
    SentFrameRef ref = control();
    ref.controlType = FrameType::PathChallenge;
    ref.payload = data.length() == PathChallenge::Length ?
      PathData{data} : PathData(PathChallenge::Length, true);
    return ref;
  }

  static SentFrameRef handshakeDone() {
    SentFrameRef ref = control();
    ref.controlType = FrameType::HandshakeDone;
    return ref;
  }

  static SentFrameRef newCxnID(
    uint64_t sequence, uint64_t retirePriorTo,
    const CxnID &id, const ResetToken &token) {
    SentFrameRef ref = control();
    ref.controlType = FrameType::NewCxnID;
    ref.streamID = sequence;
    ref.value = retirePriorTo;
    ref.length = id.length();
    ref.cxnID = id;
    ref.resetToken = token;
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
  bool equals(const SentFrameKey &o) const {
    return kind == o.kind && controlType == o.controlType &&
      streamID == o.streamID && offset == o.offset &&
      length == o.length && value == o.value && fin == o.fin;
  }
  int cmp(const SentFrameKey &o) const {
    int i;
    if (i = ZuCompare(kind, o.kind)) return i;
    if (i = ZuCompare(controlType, o.controlType)) return i;
    if (i = ZuCompare(streamID, o.streamID)) return i;
    if (i = ZuCompare(offset, o.offset)) return i;
    if (i = ZuCompare(length, o.length)) return i;
    if (i = ZuCompare(value, o.value)) return i;
    return ZuCompare(fin, o.fin);
  }
  friend inline bool operator ==(
    const SentFrameKey &l, const SentFrameKey &r) {
    return l.equals(r);
  }
  friend inline int operator <=>(
    const SentFrameKey &l, const SentFrameKey &r) {
    return l.cmp(r);
  }
  uint32_t hash() const {
    uint32_t h = ZuHash<unsigned>::hash(unsigned(kind));
    switch (kind) {
      case SentFrameKind::Stream:
	return h ^ ZuHash<uint64_t>::hash(streamID) ^
	  ZuHash<uint64_t>::hash(offset);
      case SentFrameKind::Crypto:
	return h ^ ZuHash<uint64_t>::hash(offset);
      case SentFrameKind::Control:
	h ^= ZuHash<unsigned>::hash(unsigned(controlType));
	switch (controlType) {
	  case FrameType::MaxData:
	  case FrameType::DataBlocked:
	    return h ^ ZuHash<uint64_t>::hash(value);
	  case FrameType::MaxStreamData:
	  case FrameType::MaxStreams:
	  case FrameType::StreamDataBlocked:
	  case FrameType::StreamsBlocked:
	  case FrameType::StopSending:
	    return h ^ ZuHash<uint64_t>::hash(streamID) ^
	      ZuHash<uint64_t>::hash(value);
	  case FrameType::ResetStream:
	    return h ^ ZuHash<uint64_t>::hash(streamID) ^
	      ZuHash<uint64_t>::hash(value) ^
	      ZuHash<uint64_t>::hash(length);
	  case FrameType::NewCxnID:
	    return h ^ ZuHash<uint64_t>::hash(streamID);
	  default:
	    return h;
	}
      default:
	return h;
    }
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
	  ref.controlType == FrameType::StopSending ||
	  ref.controlType == FrameType::NewCxnID));
  }
};

struct OutstandingFlowKey {
  SentFrameKind::T kind = SentFrameKind::None;
  uint64_t streamID = 0;

  bool operator !() const { return kind == SentFrameKind::None; }
  ZuOpBool
  int cmp(const OutstandingFlowKey &o) const {
    if (int i = ZuCompare(kind, o.kind)) return i;
    return ZuCompare(streamID, o.streamID);
  }
  bool equals(const OutstandingFlowKey &o) const {
    return kind == o.kind && streamID == o.streamID;
  }
  uint32_t hash() const {
    return ZuHash<unsigned>::hash(unsigned(kind)) ^
      ZuHash<uint64_t>::hash(streamID);
  }
};

} // namespace Zquic

template <> struct ZuCmp<Zquic::SentFrameKey> {
  static int cmp(const Zquic::SentFrameKey &l, const Zquic::SentFrameKey &r) {
    return l.cmp(r);
  }
  static bool equals(
    const Zquic::SentFrameKey &l, const Zquic::SentFrameKey &r) {
    return l.equals(r);
  }
  static bool null(const Zquic::SentFrameKey &key) { return !key; }
  static Zquic::SentFrameKey null() { return {}; }
};

template <> struct ZuHash<Zquic::SentFrameKey> {
  static uint32_t hash(const Zquic::SentFrameKey &key) {
    return key.hash();
  }
};

template <> struct ZuCmp<Zquic::OutstandingFlowKey> {
  static int cmp(
      const Zquic::OutstandingFlowKey &l,
      const Zquic::OutstandingFlowKey &r) { return l.cmp(r); }
  static bool equals(
      const Zquic::OutstandingFlowKey &l,
      const Zquic::OutstandingFlowKey &r) { return l.equals(r); }
  static bool null(const Zquic::OutstandingFlowKey &key) { return !key; }
  static Zquic::OutstandingFlowKey null() { return {}; }
};

template <> struct ZuHash<Zquic::OutstandingFlowKey> {
  static uint32_t hash(const Zquic::OutstandingFlowKey &key) {
    return key.hash();
  }
};

namespace Zquic {

inline const SentFrameKey &SentFrameKey_IDAxor(const SentFrameKey &key)
{
  return key;
}

ZmHashDerive(SentFrameAckHash, SentFrameKey,
  (ZmHashNode<SentFrameKey,
    ZmHashKey<SentFrameKey_IDAxor,
	ZmHashHeapID<"Zquic.Pkt.AckdFrame">>>));

struct SentFrameUpdate {
  SentFrameRef	ref;
  void		*owner = nullptr;
};

struct SentPkt {
  static constexpr unsigned MaxFrames = 8;

  uint64_t key() const { return pn; }
  uint64_t length() const { return 1; }
  uint64_t clipHead(uint64_t) { return 0; }
  uint64_t clipTail(uint64_t) { return 0; }
  template <typename I>
  void write(const I &) { }

  bool addFrame(const SentFrameRef &frame, void *owner = nullptr) {
    if (frame.kind == SentFrameKind::None || frames.length() >= MaxFrames)
      return false;
    frames.push(SentFrameUpdate{frame, owner});
    return true;
  }
  unsigned framesUsed() const { return frames.length(); }
  const SentFrameRef &frame(unsigned i) const {
    ZiAssert(i < frames.length(), "Zquic", (),
      "sent-packet frame index out of bounds", return frames[0].ref);
    return frames[i].ref;
  }
  void *frameOwner(unsigned i) const {
    ZiAssert(i < frames.length(), "Zquic", (),
      "sent-packet frame-owner index out of bounds", return nullptr);
    return frames[i].owner;
  }

  uint64_t	pn = 0;
  ZuTime	sentTime;
  unsigned	bytes = 0;
  PktNumSpace::T space = PktNumSpace::AppData;
  PktType::T	packetType = PktType::N;
  EcnMark::T	ecn = EcnMark::NotECT;
  bool		ackEliciting = false;
  bool		inFlight = false;
  bool		pmtudProbe = false;
  unsigned	pmtudSize = 0;
  bool		ackd = false;
  bool		lost = false;
  bool		ptoReclaimed = false;
  void		*recoveryNode = nullptr;
  uint8_t	ackLevel = PktNumSpace::N;
  uint64_t	ackLargest = 0;
  ZuArray<SentFrameUpdate, MaxFrames> frames;
};

class OutstandingFrames {
  struct RangeVal {
    uint64_t end = 0;
    unsigned count = 0;
  };
  using RangeTree = ZmRBTreeKV<
    uint64_t, RangeVal,
    ZmRBTreeUnique<true,
      ZmRBTreeHeapID<"Zquic.Pkt.OutstandingRange">>>;
  struct Flow : public ZuObject {
    RangeTree ranges;
  };
  using Flows = ZmHashKV<
    OutstandingFlowKey, ZmRef<Flow>,
    ZmHashHeapID<"Zquic.Pkt.OutstandingFlow">>;
  using Exact = ZmHashKV<
    SentFrameKey, unsigned,
    ZmHashHeapID<"Zquic.Pkt.OutstandingExact">>;

public:
  void add(const SentPkt &p) {
    for (unsigned i = 0, n = p.framesUsed(); i < n; ++i) add_(p.frame(i));
  }
  void del(const SentPkt &p) {
    for (unsigned i = 0, n = p.framesUsed(); i < n; ++i) del_(p.frame(i));
  }
  void clear() {
    if (m_flows) m_flows->clean();
    if (m_exact) m_exact->clean();
  }
  bool outstanding(const SentFrameRef &frame) const {
    if (!SentFrameKey::retransmittable(frame)) return false;
    if (frame.kind == SentFrameKind::Control)
      return exact_(SentFrameKey{frame});
    uint64_t end;
    if (!rangeEnd_(frame, end)) return false;
    if (frame.length && covered_(flowKey_(frame), frame.offset, end))
      return true;
    return frame.kind == SentFrameKind::Stream && frame.fin &&
      exact_(finKey_(frame, end));
  }
  template <typename Queue>
  bool clip(SentFrameRef &frame, Queue &queue) const {
    if (frame.kind != SentFrameKind::Stream &&
	frame.kind != SentFrameKind::Crypto)
      return !outstanding(frame);
    uint64_t end;
    if (!rangeEnd_(frame, end)) return false;
    bool sendFin = frame.kind == SentFrameKind::Stream && frame.fin &&
      !exact_(finKey_(frame, end));
    const Flow *flow = findFlow_(flowKey_(frame));
    uint64_t pos = frame.offset;
    bool have = false, finQueued = false;
    SentFrameRef first;
    while (pos < end) {
      uint64_t coveredEnd = 0;
      uint64_t next = end;
      if (flow) {
	auto node = flow->ranges.template findPtr<ZmRBTreeLessEqual>(pos);
	if (node && node->val().end > pos)
	  coveredEnd = node->val().end < end ? node->val().end : end;
	else {
	  node = flow->ranges.template findPtr<ZmRBTreeGreaterEqual>(pos);
	  if (node && node->key() < next) next = node->key();
	}
      }
      if (coveredEnd) {
	pos = coveredEnd;
	continue;
      }
      if (next <= pos) continue;
      SentFrameRef part = frame;
      bool fin = sendFin && next == end;
      if (clip_(part, pos, next, fin)) {
	if (fin) finQueued = true;
	if (!have) first = part, have = true;
	else queue.push(part);
      }
      pos = next;
    }
    if (sendFin && !finQueued) {
      SentFrameRef part = frame;
      if (clip_(part, end, end, true)) {
	if (!have) first = part, have = true;
	else queue.push(part);
      }
    }
    if (have) frame = first;
    return have;
  }

#ifdef ZDEBUG
  uint64_t visits() const { return m_visits; }
  void resetVisits() const { m_visits = 0; }
  bool equals(const OutstandingFrames &o) const {
    unsigned exactCount = m_exact ? m_exact->count_() : 0;
    if (exactCount != (o.m_exact ? o.m_exact->count_() : 0)) return false;
    if (m_exact) {
      auto iter = m_exact->citer();
      while (auto node = iter()) {
	auto other = o.m_exact ? o.m_exact->findPtr(node->key()) : nullptr;
	if (!other || other->val() != node->val()) return false;
      }
    }
    unsigned flowCount = m_flows ? m_flows->count_() : 0;
    if (flowCount != (o.m_flows ? o.m_flows->count_() : 0)) return false;
    if (m_flows) {
      auto iter = m_flows->citer();
      while (auto node = iter()) {
	auto other = o.m_flows ? o.m_flows->findPtr(node->key()) : nullptr;
	if (!other) return false;
	const RangeTree &l = node->val()->ranges;
	const RangeTree &r = other->val()->ranges;
	if (l.count_() != r.count_()) return false;
	auto rangeIter = l.citer();
	while (auto range = rangeIter()) {
	  auto otherRange = r.findPtr(range->key());
	  if (!otherRange || otherRange->val().end != range->val().end ||
	      otherRange->val().count != range->val().count)
	    return false;
	}
      }
    }
    return true;
  }
#endif

private:
  static bool rangeEnd_(const SentFrameRef &frame, uint64_t &end) {
    end = frame.offset + frame.length;
    return end >= frame.offset;
  }
  static OutstandingFlowKey flowKey_(const SentFrameRef &frame) {
    return {frame.kind,
      frame.kind == SentFrameKind::Stream ? frame.streamID : 0};
  }
  static SentFrameKey finKey_(const SentFrameRef &frame, uint64_t end) {
    SentFrameKey key{frame};
    key.offset = end;
    key.length = 0;
    key.fin = true;
    return key;
  }
  static bool clip_(
      SentFrameRef &frame, uint64_t first, uint64_t end, bool fin) {
    if (end < first) return false;
    uint64_t length = end - first;
    if (frame.kind == SentFrameKind::Stream) {
      if (first < frame.offset || length > uint64_t(uint32_t(-1)))
	return false;
      uint64_t advance = first - frame.offset;
      uint64_t rangeOffset = uint64_t(frame.range.offset) + advance;
      if (rangeOffset > uint64_t(uint32_t(-1))) return false;
      frame.range.offset = uint32_t(rangeOffset);
      frame.range.length = uint32_t(length);
      frame.range.streamOffset = first;
      frame.fin = fin;
    }
    frame.offset = first;
    frame.length = length;
    return length || frame.fin;
  }
  Flow *flow_(const OutstandingFlowKey &key) {
    if (m_flows)
      if (auto node = m_flows->findPtr(key)) return node->val();
    if (!m_flows)
      m_flows = new Flows{ZmHashParams().bits(3).loadFactor(1).cBits(2)};
    ZmRef<Flow> flow = new Flow{};
    Flow *ptr = flow;
    m_flows->add(key, ZuMv(flow));
    return ptr;
  }
  const Flow *findFlow_(const OutstandingFlowKey &key) const {
    if (!m_flows) return nullptr;
    auto node = m_flows->findPtr(key);
    return node ? node->val().ptr() : nullptr;
  }
  void split_(Flow &flow, uint64_t offset) {
    auto node = flow.ranges.template findPtr<ZmRBTreeLessEqual>(offset);
    if (!node || node->key() >= offset || node->val().end <= offset) return;
    uint64_t end = node->val().end;
    unsigned count = node->val().count;
    node->val().end = offset;
    flow.ranges.add(offset, RangeVal{end, count});
  }
  void merge_(Flow &flow, uint64_t offset) {
    auto right = flow.ranges.findPtr(offset);
    if (!right) return;
    auto left = flow.ranges.template findPtr<ZmRBTreeLess>(offset);
    if (!left || left->val().end != offset ||
	left->val().count != right->val().count)
      return;
    left->val().end = right->val().end;
    (void)flow.ranges.del(offset);
  }
  void addRange_(const OutstandingFlowKey &key, uint64_t first, uint64_t end) {
    if (first >= end) return;
    Flow &flow = *flow_(key);
    split_(flow, first);
    split_(flow, end);
    uint64_t pos = first;
    while (pos < end) {
      uint64_t boundary = pos;
      auto node = flow.ranges.template findPtr<ZmRBTreeGreaterEqual>(pos);
      if (!node || node->key() > pos) {
	uint64_t next = node && node->key() < end ? node->key() : end;
	flow.ranges.add(pos, RangeVal{next, 1});
	pos = next;
      } else {
	++node->val().count;
	pos = node->val().end;
      }
      merge_(flow, boundary);
    }
    merge_(flow, end);
  }
  void delRange_(const OutstandingFlowKey &key, uint64_t first, uint64_t end) {
    if (first >= end || !m_flows) return;
    auto flowNode = m_flows->findPtr(key);
    if (!flowNode) return;
    Flow &flow = *flowNode->val();
    split_(flow, first);
    split_(flow, end);
    uint64_t pos = first;
    while (pos < end) {
      uint64_t boundary = pos;
      auto node = flow.ranges.template findPtr<ZmRBTreeGreaterEqual>(pos);
      if (!node || node->key() != pos || node->key() >= end) break;
      uint64_t next = node->val().end;
      if (!--node->val().count) (void)flow.ranges.del(pos);
      pos = next;
      merge_(flow, boundary);
    }
    merge_(flow, end);
    if (!flow.ranges.count_()) (void)m_flows->del(key);
  }
  bool covered_(
      const OutstandingFlowKey &key, uint64_t first, uint64_t end) const {
    const Flow *flow = findFlow_(key);
    if (!flow) return false;
#ifdef ZDEBUG
    ++m_visits;
#endif
    auto node = flow->ranges.template findPtr<ZmRBTreeLessEqual>(first);
    return node && node->val().end >= end;
  }
  void addExact_(const SentFrameKey &key) {
    if (!m_exact)
      m_exact = new Exact{ZmHashParams().bits(4).loadFactor(1).cBits(2)};
    if (auto node = m_exact->findPtr(key)) ++node->val();
    else m_exact->add(key, 1U);
  }
  void delExact_(const SentFrameKey &key) {
    if (!m_exact) return;
    auto node = m_exact->findPtr(key);
    if (!node) return;
    if (--node->val()) return;
    (void)m_exact->del(key);
  }
  bool exact_(const SentFrameKey &key) const {
#ifdef ZDEBUG
    ++m_visits;
#endif
    return m_exact && m_exact->findPtr(key);
  }
  void add_(const SentFrameRef &frame) {
    if (!SentFrameKey::retransmittable(frame)) return;
    if (frame.kind == SentFrameKind::Control) {
      addExact_(SentFrameKey{frame});
      return;
    }
    uint64_t end;
    if (!rangeEnd_(frame, end)) return;
    addRange_(flowKey_(frame), frame.offset, end);
    if (frame.kind == SentFrameKind::Stream && frame.fin)
      addExact_(finKey_(frame, end));
  }
  void del_(const SentFrameRef &frame) {
    if (!SentFrameKey::retransmittable(frame)) return;
    if (frame.kind == SentFrameKind::Control) {
      delExact_(SentFrameKey{frame});
      return;
    }
    uint64_t end;
    if (!rangeEnd_(frame, end)) return;
    delRange_(flowKey_(frame), frame.offset, end);
    if (frame.kind == SentFrameKind::Stream && frame.fin)
      delExact_(finKey_(frame, end));
  }

  ZmRef<Flows> m_flows;
  ZmRef<Exact> m_exact;
#ifdef ZDEBUG
  mutable uint64_t m_visits = 0;
#endif
};

ZmListDerive(RecoveryPktList, SentPkt *,
  ZmListHeapID<"Zquic.Pkt.Recovery">);

using TxPkt = SentPkt;

// 64 matches the former common batch size; valid larger batches grow on heap.
using SentFrameUpdates = ZtBuiltin<
  ZtArray<SentFrameUpdate,
    ZtArrayHeapID<"Zquic.Recovery.FrameUpdates">>,
  64>;

struct PktTxUpdate {
  static constexpr unsigned MaxFrames = 64; // preserved qlog sample policy
  static constexpr unsigned AckedPNSampleMax = 64;
  static constexpr unsigned MaxAckedPNs = AckedPNSampleMax;

  void ackd(const SentPkt &p) {
    if (nAckedPNs < AckedPNSampleMax)
      ackedPNs[nAckedPNs++] = p.pn;
    else
      ackedPNsTruncated = true;
    ackdAck_(p);
    ackdFrames_(p);
    if (p.lost) return;
    ackdBytes += p.bytes;
    switch (p.ecn) {
      case EcnMark::ECT0:
	++ecnAckdPackets;
	ecnAckdBytes += p.bytes;
	if (p.sentTime > ecnAckdSentTime) ecnAckdSentTime = p.sentTime;
	break;
      case EcnMark::ECT1:
	++ecnAckdPackets;
	ecnAckdBytes += p.bytes;
	if (p.sentTime > ecnAckdSentTime) ecnAckdSentTime = p.sentTime;
	break;
      case EcnMark::CE:
	++ecnAckdPackets;
	ecnAckdBytes += p.bytes;
	if (p.sentTime > ecnAckdSentTime) ecnAckdSentTime = p.sentTime;
	break;
      default:
	break;
    }
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
    if (p.ackLevel >= PktNumSpace::N) return;
    if (!ackdAck[p.ackLevel] || p.ackLargest > ackLargest[p.ackLevel]) {
      ackdAck[p.ackLevel] = true;
      ackLargest[p.ackLevel] = p.ackLargest;
    }
  }
  void ackdFrames_(const SentPkt &p) {
    unsigned n = ackdFrames.length();
    for (unsigned i = 0, l = p.framesUsed(); i < l; ++i)
      ackdFrames.push(SentFrameUpdate{p.frame(i), p.frameOwner(i)});
    ZiAssert(ackdFrames.length() - n == p.framesUsed(), "Zquic", (),
      "ACK frame update truncation", return);
  }
  void lostFrames_(const SentPkt &p) {
    unsigned n = lostFrames.length();
    for (unsigned i = 0, l = p.framesUsed(); i < l; ++i)
      lostFrames.push(SentFrameUpdate{p.frame(i), p.frameOwner(i)});
    ZiAssert(lostFrames.length() - n == p.framesUsed(), "Zquic", (),
      "lost frame update truncation", return);
  }
  void clearAckdFrames() { ackdFrames.clear(); }

  uint64_t	ackdBytes = 0;
  ZuArray<uint64_t, AckedPNSampleMax>
		ackedPNs =
		  fixedArray<uint64_t, AckedPNSampleMax>();
  PktNumSpace::T	level = PktNumSpace::Initial;
  unsigned	ecnAckdPackets = 0;
  uint64_t	ecnAckdBytes = 0;
  ZuTime	ecnAckdSentTime;
  uint64_t	normalAckdBytes = 0;
  uint64_t	pmtudAckdBytes = 0;
  unsigned	pmtudAckdSize = 0;
  uint64_t	lostBytes = 0;
  uint64_t	normalLostBytes = 0;
  uint64_t	pmtudLostBytes = 0;
  unsigned	pmtudLostSize = 0;
  ZuTime	normalLostSentTime;
  ZuTime	pmtudLostSentTime;
  ZuArray<bool, PktNumSpace::N>
		ackdAck =
		  fixedArray<bool, PktNumSpace::N>();
  ZuArray<uint64_t, PktNumSpace::N>
		ackLargest =
		  fixedArray<uint64_t, PktNumSpace::N>();
  SentFrameUpdates ackdFrames;
  SentFrameUpdates lostFrames;
  unsigned	nAckedPNs = 0;
  bool		ackedPNsTruncated = false;
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
      if (m_pending && m_pending->findPtr(key)) {
	++m_dropped;
	return false;
      }
      pending_()->add(key);
    }
    m_frames.push(frame);
    return true;
  }

  bool pop(SentFrameRef &frame) {
    if (!m_frames.count_()) return false;
    frame = m_frames.shift();
    if (m_pending && SentFrameKey::retransmittable(frame))
      m_pending->del(SentFrameKey{frame});
    return true;
  }

  unsigned count() const { return m_frames.count_(); }
  unsigned dropped() const { return m_dropped; }
  bool empty() const { return !m_frames.count_(); }
  void clear() {
    m_frames.clean();
    m_pending = nullptr;
    m_dropped = 0;
  }

private:
  SentFrameAckHash *pending_() {
    if (!m_pending)
      m_pending = new SentFrameAckHash{
	ZmHashParams().bits(8).loadFactor(1).cBits(3)};
    return m_pending.ptr();
  }

  Queue		m_frames;
  ZmRef<SentFrameAckHash> m_pending;
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
    ZmRef<Msg> node = new Queue::Node{p};
    TxPkt &packet = node->data();
    Tx::send(node);
    m_outstanding.add(packet);
    if (packet.inFlight) {
      m_bytesInFlight += packet.bytes;
      if (packet.ackEliciting) {
	linkRecovery_(packet);
	if (!*m_latestAckSentTime || packet.sentTime > m_latestAckSentTime)
	  m_latestAckSentTime = packet.sentTime;
      }
    }
    return true;
  }

  bool discard(uint64_t pn) {
    auto node = m_packets.find(pn);
    if (!node) return false;
    TxPkt &p = node->data();
    if (!p.ackd && !p.lost && !p.ptoReclaimed) m_outstanding.del(p);
    if (p.inFlight) release_(p);
    (void)m_packets.del(pn);
    return true;
  }

  bool ack(uint64_t pn) {
    auto node = m_packets.find(pn);
    if (!node || !ack_(node->data())) return false;
    (void)m_packets.del(pn);
    return true;
  }

  unsigned ack(
    const AckTracker &ranges, unsigned *lost = nullptr,
    unsigned packetThreshold = 3)
  {
    unsigned n = 0;
    auto iter = m_packets.iter();
    while (auto node = iter()) {
      TxPkt &p = node->data();
      bool wasLost = p.lost;
      if (ranges.contains(p.pn) && ack_(p)) {
	(void)iter.del();
	if (!wasLost) ++n;
      }
    }
    unsigned l = markPktThreshLoss(ranges.largest(), packetThreshold);
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
	TxPkt &p = node->data();
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
      markPktThreshLoss(
	largestAckdForLoss, packetThreshold, lostBytes, lostSentTime,
	update) : 0;
    if (ackdBytes) *ackdBytes = ackdBytes_;
    if (lost) *lost = l;
    return n;
  }

  bool ackBatch(
    const AckRange *ranges, unsigned nRanges, PktAckBatch &batch,
    unsigned budget, PktNumSpace::T level, PktTxUpdate *update = nullptr)
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
	TxPkt &p = node->data();
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
	    if (!batch.haveAckd || p.pn > batch.latestAckd) {
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

  unsigned markPktThreshLoss(
    uint64_t largestAckd, unsigned threshold = 3,
    uint64_t *lostBytes = nullptr, ZuTime *lostSentTime = nullptr,
    PktTxUpdate *update = nullptr) {
    unsigned n = 0;
    if (lostBytes) *lostBytes = 0;
    if (lostSentTime) *lostSentTime = ZuTime{0};
    if (largestAckd < threshold) return 0;
    uint64_t last = largestAckd - threshold;
    auto iter = m_recovery.iter();
    while (auto node = iter()) {
#ifdef ZDEBUG
      ++m_recoveryVisits;
#endif
      TxPkt &p = *node->data();
      if (p.pn > last) break;
      if (lose_(p, false)) {
	p.recoveryNode = nullptr;
	(void)iter.del();
	++n;
	if (lostBytes) *lostBytes += p.bytes;
	if (lostSentTime && p.sentTime > *lostSentTime)
	  *lostSentTime = p.sentTime;
	if (update) update->lost(p);
      }
    }
    return n;
  }

  bool markPktThreshLossBatch(
    uint64_t largestAckd, unsigned threshold, PktLossBatch &batch,
    unsigned budget, PktTxUpdate *update = nullptr)
  {
    if (!budget) return false;
    if (largestAckd < threshold) {
      batch.nextPN = 0;
      return true;
    }
    uint64_t last = largestAckd - threshold;
    unsigned scanned = 0;
    auto iter = m_recovery.iter();
    while (auto node = iter()) {
#ifdef ZDEBUG
      ++m_recoveryVisits;
#endif
      TxPkt &p = *node->data();
      if (p.pn > last) break;
      batch.nextPN = ZuNull(p.pn) ? p.pn : p.pn + 1;
      if (lose_(p, false)) {
	p.recoveryNode = nullptr;
	(void)iter.del();
	++batch.lost;
	if (update) update->lost(p);
      }
      if (++scanned >= budget) {
	const auto *head = m_recovery.headPtr();
	if (head && head->data()->pn <= last) return false;
	batch.nextPN = 0;
	return true;
      }
    }
    batch.nextPN = 0;
    return true;
  }

  unsigned markTimeThreshLoss(
    uint64_t largestAckd, ZuTime now, ZuTime threshold,
    uint64_t *lostBytes = nullptr, ZuTime *lostSentTime = nullptr,
    PktTxUpdate *update = nullptr) {
    unsigned n = 0;
    if (lostBytes) *lostBytes = 0;
    if (lostSentTime) *lostSentTime = ZuTime{0};
    if (ZuNull(largestAckd)) return 0;
    auto iter = m_recovery.iter();
    while (auto node = iter()) {
#ifdef ZDEBUG
      ++m_recoveryVisits;
#endif
      TxPkt &p = *node->data();
      if (p.pn >= largestAckd) break;
      if (!*p.sentTime || p.sentTime > now || now - p.sentTime < threshold)
	break;
      if (lose_(p, false)) {
	p.recoveryNode = nullptr;
	(void)iter.del();
	++n;
	if (lostBytes) *lostBytes += p.bytes;
	if (lostSentTime && p.sentTime > *lostSentTime)
	  *lostSentTime = p.sentTime;
	if (update) update->lost(p);
      }
    }
    return n;
  }
  bool markTimeThreshLossBatch(
    uint64_t largestAckd, ZuTime now, ZuTime threshold, PktLossBatch &batch,
    unsigned budget, PktTxUpdate *update = nullptr)
  {
    if (!budget) return false;
    if (ZuNull(largestAckd)) return true;
    unsigned scanned = 0;
    auto iter = m_recovery.iter();
    while (auto node = iter()) {
#ifdef ZDEBUG
      ++m_recoveryVisits;
#endif
      TxPkt &p = *node->data();
      if (p.pn >= largestAckd) break;
      if (!*p.sentTime || p.sentTime > now || now - p.sentTime < threshold)
	break;
      batch.nextPN = ZuNull(p.pn) ? p.pn : p.pn + 1;
      if (lose_(p, false)) {
	p.recoveryNode = nullptr;
	(void)iter.del();
	++batch.lost;
	if (update) update->lost(p);
      }
      if (++scanned >= budget) {
	const auto *head = m_recovery.headPtr();
	if (head) {
	  const SentPkt &next = *head->data();
	  if (next.pn < largestAckd && *next.sentTime &&
	      next.sentTime <= now && now - next.sentTime >= threshold)
	    return false;
	}
	batch.nextPN = 0;
	return true;
      }
    }
    batch.nextPN = 0;
    return true;
  }
  ZuTime nextLossTime(
    uint64_t largestAckd, ZuTime threshold, unsigned = 256) const {
    if (ZuNull(largestAckd) || !*threshold)
      return ZuTime{0};
    const auto *head = m_recovery.headPtr();
#ifdef ZDEBUG
    if (head) ++m_recoveryVisits;
#endif
    const SentPkt *p = head ? head->data() : nullptr;
    if (!p || p->pn >= largestAckd || !*p->sentTime) return ZuTime{0};
    return p->sentTime + threshold;
  }
  bool ackElicitingInFlight() const {
    return m_recovery.count_();
  }

  unsigned reject(
    PktType::T packetType, uint64_t *releasedBytes = nullptr,
    PktTxUpdate *update = nullptr)
  {
    unsigned n = 0;
    if (releasedBytes) *releasedBytes = 0;
    auto iter = m_packets.iter();
    while (auto node = iter()) {
      TxPkt &p = node->data();
      if (p.packetType != packetType) continue;
      bool wasInFlight = p.inFlight;
      if (!p.ackd && !p.lost && lose_(p) && update) update->lost(p);
      if (wasInFlight && releasedBytes) *releasedBytes += p.bytes;
      if (p.lost) {
	m_lostPNs.del(p.pn);
	if (m_retainedLost) --m_retainedLost;
      }
      (void)iter.del();
      ++n;
    }
    return n;
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
      if (clipOutstanding_(frame)) return true;
    return false;
  }
  bool requeueRetransmit(const SentFrameRef &frame) {
    if (m_outstanding.outstanding(frame)) return false;
    return m_retransmit.push(frame);
  }
  unsigned reclaimOnPTO(unsigned limit) {
    unsigned n = 0;
    auto reclaim = [this, limit, &n](bool reclaimed, bool needFrames) {
      auto iter = m_packets.riter();
      while (n < limit) {
	auto node = iter();
	if (!node) break;
	TxPkt &p = node->data();
	if (p.ackd || p.lost || !p.inFlight || !p.ackEliciting ||
	    (!reclaimed && p.ptoReclaimed) ||
	    (needFrames && !p.framesUsed()))
	  continue;
	if (!p.ptoReclaimed) m_outstanding.del(p);
	Tx::resend(Span{p.pn, 1});
	Tx::resend();
	p.ptoReclaimed = true;
	++n;
      }
    };
    reclaim(false, true);
    if (!n) reclaim(true, true);
    if (!n) reclaim(false, false);
    if (!n) reclaim(true, false);
    return n;
  }
  unsigned count() const { return m_packets.count_(); }
  unsigned recoveryCount() const { return m_recovery.count_(); }
#ifdef ZDEBUG
  uint64_t recoveryVisits() const { return m_recoveryVisits; }
  void resetRecoveryVisits() const { m_recoveryVisits = 0; }
  uint64_t outstandingVisits() const { return m_outstanding.visits(); }
  void resetOutstandingVisits() const { m_outstanding.resetVisits(); }
  bool verifyOutstanding() const {
    OutstandingFrames expected;
    auto iter = m_packets.citer();
    while (auto node = iter()) {
      const TxPkt &p = node->data();
      if (!p.ackd && !p.lost && !p.ptoReclaimed) expected.add(p);
    }
    return m_outstanding.equals(expected);
  }
#endif
  void clear() {
    m_recovery.clean();
    m_outstanding.clear();
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
    bool havePN = false, haveTime = false;
    uint64_t lastPN = 0;
    ZuTime first, last;
    auto iter = m_packets.citer();
    unsigned scanned = 0;
    while (auto node = iter()) {
      const SentPkt &p = node->data();
      if (p.ackd && p.ackEliciting && !p.pmtudProbe) {
	havePN = haveTime = false;
      } else if (p.lost && p.ackEliciting) {
	if (havePN && p.pn != lastPN + 1) haveTime = false;
	havePN = true;
	lastPN = p.pn;
	if (!p.pmtudProbe) {
	  if (!haveTime) {
	    first = last = p.sentTime;
	    haveTime = true;
	  } else {
	    if (p.sentTime < first) first = p.sentTime;
	    if (p.sentTime > last) last = p.sentTime;
	  }
	  if (last > first && last - first >= threshold) return true;
	}
      }
      if (++scanned >= budget) return false;
    }
    return false;
  }

private:
  bool ack_(TxPkt &p) {
    if (p.ackd) return false;
    if (!p.lost && !p.ptoReclaimed) m_outstanding.del(p);
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

  bool lose_(TxPkt &p, bool unlink = true) {
    if (p.ackd || p.lost) return false;
    if (!p.ptoReclaimed) m_outstanding.del(p);
    p.lost = true;
    m_lostPNs.push(p.pn);
    ++m_retainedLost;
    if (p.inFlight) release_(p, unlink);
    if (p.ackEliciting && !p.pmtudProbe) {
      ++m_retransmittable;
      enqueueRetransmit_(p);
    }
    trimLost_();
    ++m_lost;
    return true;
  }

  void release_(TxPkt &p, bool unlink = true) {
    p.inFlight = false;
    if (p.ackEliciting && unlink) unlinkRecovery_(p);
    if (p.bytes > m_bytesInFlight) m_bytesInFlight = 0;
    else m_bytesInFlight -= p.bytes;
  }

  void linkRecovery_(TxPkt &p) {
#ifdef ZDEBUG
    const auto *tail = m_recovery.tailPtr();
    ZmAssert(!tail || tail->data()->pn < p.pn);
#endif
    p.recoveryNode = m_recovery.push(&p);
  }

  void unlinkRecovery_(TxPkt &p) {
    if (!p.recoveryNode) return;
    (void)m_recovery.delNode(
      static_cast<RecoveryPktList::Node *>(p.recoveryNode));
    p.recoveryNode = nullptr;
  }

  void enqueueRetransmit_(const SentPkt &p) {
    for (unsigned i = 0, n = p.framesUsed(); i < n; ++i) {
      const SentFrameRef &frame = p.frame(i);
      if (m_outstanding.outstanding(frame)) continue;
      m_retransmit.push(frame);
    }
  }

  bool clipOutstanding_(SentFrameRef &frame) {
    return m_outstanding.clip(frame, m_retransmit);
  }
  void trimLost_() {
    while (m_retainedLost > RetainedLostMax) {
      uint64_t pn = m_lostPNs.shift();
      auto node = m_packets.find(pn);
      if (!node) continue;
      TxPkt &p = node->data();
      if (!p.lost || p.ackd) continue;
      (void)m_packets.del(pn);
      --m_retainedLost;
    }
  }

  Queue		m_packets{0};
  RetransmitQueue m_retransmit;
  OutstandingFrames m_outstanding;
  LostPNs	m_lostPNs{ZmQueueParams{}.initial(RetainedLostMax)};
  uint64_t	m_bytesInFlight = 0;
  ZuTime	m_latestAckSentTime;
  unsigned	m_ackd = 0;
  unsigned	m_lost = 0;
  unsigned	m_retainedLost = 0;
  unsigned	m_retransmittable = 0;
  RecoveryPktList m_recovery;
#ifdef ZDEBUG
  mutable uint64_t m_recoveryVisits = 0;
#endif
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
