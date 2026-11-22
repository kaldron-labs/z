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
  enum { Max = 64 };
  enum { MaxRetained = Max + 1 };
  enum { BuiltinRetained = 32 };
  using RetainedRanges = ZtBuiltin<
    ZtArray<AckRange,
      ZtArrayHeapMax<MaxRetained,
	ZtArrayHeapID<"Zquic.Ack.RetainedRanges">>>,
    BuiltinRetained>;

  bool add(uint64_t pn) {
    if (contains(pn)) return true;
    if (pn == m_ackHead)
      advanceHead(pn + 1);
    else
      insertSparse(pn);
    trimSparseRanges();
    return true;
  }

  bool contains(uint64_t pn) const {
    if (pn < m_ackHead) return true;
    unsigned i = findSparse(pn);
    return i < m_sparse.length() && m_sparse[i].first <= pn;
  }

  unsigned count() const {
    unsigned n = 0;
    visitRanges([&n](const AckRange &) { ++n; return true; });
    return n;
  }
  bool multipleRanges() const {
    unsigned n = 0;
    bool stop = false;
    bool ok = visitRanges([&n, &stop](const AckRange &) {
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
    visitRanges([i, &n, &v](const AckRange &range) {
      if (n++ == i) { v = range.first; return false; }
      return true;
    });
    return v;
  }
  uint64_t last(unsigned i) const {
    uint64_t v = 0;
    unsigned n = 0;
    visitRanges([i, &n, &v](const AckRange &range) {
      if (n++ == i) { v = range.largest; return false; }
      return true;
    });
    return v;
  }
  bool range(unsigned i, AckRange &range) const {
    bool found = false;
    unsigned n = 0;
    visitRanges([i, &n, &found, &range](const AckRange &range_) {
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

  uint64_t retiredRanges() const { return m_retiredRanges; }

private:
  static bool before_(uint64_t largest, uint64_t first) {
    return largest < first && first - largest > 1;
  }

  unsigned findSparse(uint64_t pn) const {
    return ackRangeLowerBound(m_sparse.cspan(), pn);
  }

  void insertSparse(uint64_t pn) {
    AckRange range{pn, pn};
    unsigned i = findSparse(pn);
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

  void advanceHead(uint64_t head) {
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

  void trimSparseRanges() {
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
    visitRanges([&ranges, &skip](const AckRange &range) {
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
  bool visitRanges(L &&l) const {
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
    noteECN(i, ecn);
    if (!haveLargest || pn >= largest) m_largestRxTime[i] = now;
    if (ackEliciting) {
      ++m_gen[i];
      m_pending[i] = true;
      noteAckEliciting(i, now, maxAckDelay, immediate);
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
    noteAckEliciting(i, now, maxAckDelay, immediate);
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
    if (ZuNull(gen)) gen = m_gen[i];
    if (gen <= m_sentGen[i] || gen > m_gen[i]) return;
    m_sentGen[i] = gen;
    uint64_t remaining = m_gen[i] - gen;
    if (remaining) {
      m_activeAck[i] = remaining >= ActiveAckThreshold ?
	ActiveAckThreshold : unsigned(remaining);
      m_immediate[i] = remaining >= ActiveAckThreshold || m_forceGen[i] > gen;
      m_deadlineSet[i] = !m_immediate[i];
      m_deadline[i] = m_immediate[i] ? 0 : m_lastAckDeadline[i];
      return;
    }
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
      // Completion publications may still contain the previous runtime's
      // generation. Never reuse an obligation identity across a reset.
      m_sentGen[i] = m_postedGen[i] = m_gen[i];
      m_forceGen[i] = 0;
      m_lastAckDeadline[i] = 0;
      m_ecn[i].reset();
    }
  }

private:
  void noteAckEliciting(
    unsigned i, uint64_t now, uint64_t maxAckDelay, bool immediate)
  {
    m_lastAckDeadline[i] =
      maxAckDelay > uint64_t(-1) - now ? uint64_t(-1) : now + maxAckDelay;
    if (immediate) m_forceGen[i] = m_gen[i];
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

  void noteECN(unsigned i, EcnMark::T ecn) {
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
  ZuArray<uint64_t, PktNumSpace::N> m_sentGen =
    fixedArray<uint64_t, PktNumSpace::N>();
  ZuArray<uint64_t, PktNumSpace::N> m_forceGen =
    fixedArray<uint64_t, PktNumSpace::N>();
  ZuArray<uint64_t, PktNumSpace::N> m_lastAckDeadline =
    fixedArray<uint64_t, PktNumSpace::N>();
};

class RttEstimator {
public:
  enum { InitialRTTMS = 333 };
  enum { GranularityMS = 1 };
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
    ZuTime rtt = m_latest;
    if (*m_smoothed && (!*rtt || m_smoothed > rtt)) rtt = m_smoothed;
    if (!*rtt) rtt = InitialRTT;
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
  // Retirement is independent of congestion-epoch growth eligibility.
  void ackd(uint64_t bytes, uint64_t eligibleBytes) {
    release(bytes);
    grow(eligibleBytes);
  }
  void grow(uint64_t bytes) {
    if (!bytes) return;
    if (m_cwnd < m_ssthresh)
      m_cwnd += bytes;
    else {
      uint64_t n = (uint64_t(m_maxDatagram) * bytes)/m_cwnd;
      m_cwnd += n ? n : 1;
    }
  }
  uint64_t epoch() const { return m_epoch; }
  void release(uint64_t bytes) {
    if (bytes > m_bytesInFlight) m_bytesInFlight = 0;
    else m_bytesInFlight -= bytes;
  }
  void lostAt(
    uint64_t bytes, uint64_t sentTime, uint64_t eventTime,
    bool pmtudProbe = false)
  {
    release(bytes);
    if (!pmtudProbe) congestionEventAt(sentTime, eventTime);
  }
  bool congestionEventAt(uint64_t sentTime, uint64_t eventTime) {
    if (m_recoveryStartTime && sentTime <= m_recoveryStartTime) return false;
    m_recoveryStartTime = eventTime;
    ++m_epoch;
    m_ssthresh = m_cwnd >> 1;
    if (m_ssthresh < uint64_t(m_maxDatagram) * 2)
      m_ssthresh = uint64_t(m_maxDatagram) * 2;
    m_cwnd = m_ssthresh;
    return true;
  }
  void persistentCongestion() {
    ++m_epoch;
    m_cwnd = uint64_t(m_maxDatagram) * 2;
    m_ssthresh = m_cwnd;
  }

private:
  unsigned	m_maxDatagram = MinUDPPayload;
  uint64_t	m_cwnd = 0;
  uint64_t	m_ssthresh = uint64_t(-1);
  uint64_t	m_bytesInFlight = 0;
  uint64_t	m_recoveryStartTime = 0;
  uint64_t	m_epoch = 0;
};

// Bounded ACK continuation state: all credited transmissions precede a new
// congestion event's processing time, so an epoch change invalidates the sum.
struct AckGrowth {
  uint64_t epoch = 0;
  uint64_t bytes = 0;

  void stage(const NewReno &cc, uint64_t classifiedEpoch, uint64_t eligible) {
    if (epoch != cc.epoch()) {
      bytes = 0;
      epoch = cc.epoch();
    }
    if (classifiedEpoch == epoch) bytes += eligible;
  }
  void commit(NewReno &cc) {
    if (epoch == cc.epoch()) cc.grow(bytes);
    bytes = 0;
  }
};

struct SentCIDData {
  CxnID id;
  ResetToken token;
};

struct SentControl {
  uint64_t value = 0;
  FrameType::T type = FrameType::Unknown;
  StreamType::T streamType = StreamType::Duplex;
  ZuUnion<void, PathData, SentCIDData> data;
};

struct SentFrameRef {
  uint64_t streamID = 0;
  uint64_t offset = 0;
  uint64_t length = 0;
  SentFrameKind::T kind = SentFrameKind::None;
  bool fin = false;
  ZuUnion<void, TxRange, SentControl> data;

  bool operator !() const { return kind == SentFrameKind::None; }
  ZuOpBool

  auto &range() { return data.p<1>(); }
  const auto &range() const { return data.p<1>(); }
  FrameType::T controlType() const {
    return kind == SentFrameKind::Control ? data.p<2>().type : FrameType::Unknown;
  }
  uint64_t value() const {
    return kind == SentFrameKind::Control ? data.p<2>().value : 0;
  }
  StreamType::T streamType() const {
    return kind == SentFrameKind::Control ?
      data.p<2>().streamType : StreamType::Duplex;
  }
  const auto &payload() const { return data.p<2>().data.p<1>(); }
  const auto &cxnID() const { return data.p<2>().data.p<2>().id; }
  const auto &resetToken() const { return data.p<2>().data.p<2>().token; }

  static SentFrameRef stream(uint64_t id, TxRange range_, bool fin_) {
    SentFrameRef ref;
    ref.kind = SentFrameKind::Stream;
    ref.streamID = id;
    ref.offset = range_.streamOffset;
    ref.length = range_.length;
    ref.fin = fin_;
    new (ref.data.new_<1>()) TxRange{ZuMv(range_)};
    return ref;
  }

  static SentFrameRef crypto(uint64_t offset_, uint64_t length_) {
    SentFrameRef ref;
    ref.kind = SentFrameKind::Crypto;
    ref.offset = offset_;
    ref.length = length_;
    return ref;
  }

  static SentFrameRef control(
    FrameType::T type = FrameType::Unknown, uint64_t value = 0,
    StreamType::T streamType = StreamType::Duplex) {
    SentFrameRef ref;
    ref.kind = SentFrameKind::Control;
    new (ref.data.new_<2>()) SentControl{value, type, streamType, {}};
    return ref;
  }

  static SentFrameRef flowUpdate(const FlowUpdate &update) {
    auto ref = control(update.type, update.maximum, update.streamType);
    ref.streamID = update.streamID;
    return ref;
  }

  static SentFrameRef blocked(
    FrameType::T type, uint64_t streamID_, uint64_t limit,
    StreamType::T streamType = StreamType::Duplex) {
    auto ref = control(type, limit, streamType);
    ref.streamID = streamID_;
    return ref;
  }

  static SentFrameRef pathResponse(ZuBSpan bytes) {
    auto ref = control(FrameType::PathResponse);
    new (ref.data.p<2>().data.new_<1>()) PathData{
      bytes.length() == PathChallenge::Length ?
	PathData{bytes} : PathData(PathChallenge::Length, true)};
    return ref;
  }
  static SentFrameRef pathChallenge(ZuBSpan bytes) {
    auto ref = control(FrameType::PathChallenge);
    new (ref.data.p<2>().data.new_<1>()) PathData{
      bytes.length() == PathChallenge::Length ?
	PathData{bytes} : PathData(PathChallenge::Length, true)};
    return ref;
  }

  static SentFrameRef handshakeDone() {
    return control(FrameType::HandshakeDone);
  }

  static SentFrameRef newCxnID(
    uint64_t sequence, uint64_t retirePriorTo,
    const CxnID &id, const ResetToken &token) {
    auto ref = control(FrameType::NewCxnID, retirePriorTo);
    ref.streamID = sequence;
    ref.length = id.length();
    new (ref.data.p<2>().data.new_<2>()) SentCIDData{id, token};
    return ref;
  }

  static SentFrameRef resetStream(
    uint64_t id, uint64_t appError, uint64_t finalSize) {
    auto ref = control(FrameType::ResetStream, appError);
    ref.streamID = id;
    ref.length = finalSize;
    return ref;
  }

  static SentFrameRef stopSending(uint64_t id, uint64_t appError) {
    auto ref = control(FrameType::StopSending, appError);
    ref.streamID = id;
    return ref;
  }
};

inline SentFrameRef controlRef(const ControlFrame &frame) {
  switch (frame.type) {
    case FrameType::MaxData:
    case FrameType::MaxStreamData:
    case FrameType::MaxStreams:
	return SentFrameRef::flowUpdate(
	  FlowUpdate{frame.type, frame.streamID, frame.value, frame.streamType});
    case FrameType::DataBlocked:
    case FrameType::StreamDataBlocked:
    case FrameType::StreamsBlocked:
	return SentFrameRef::blocked(
	  frame.type, frame.streamID, frame.value, frame.streamType);
    case FrameType::PathChallenge:
	return SentFrameRef::pathChallenge(frame.payload);
    case FrameType::PathResponse:
	return SentFrameRef::pathResponse(frame.payload);
    case FrameType::HandshakeDone:
	return SentFrameRef::handshakeDone();
    case FrameType::NewCxnID:
	return SentFrameRef::newCxnID(
	  frame.streamID, frame.value, frame.cxnID, frame.resetToken);
    case FrameType::ResetStream:
	return SentFrameRef::resetStream(
	  frame.streamID, frame.errorCode, frame.value);
    case FrameType::StopSending:
	return SentFrameRef::stopSending(frame.streamID, frame.errorCode);
    default:
	return SentFrameRef::control();
  }
}

inline ControlFrame controlFrame(const SentFrameRef &ref) {
  ControlFrame frame;
  frame.type = ref.controlType();
  frame.streamID = ref.streamID;
  frame.value = ref.value();
  if (ref.controlType() == FrameType::ResetStream) {
    frame.errorCode = ref.value();
    frame.value = ref.length;
  } else if (ref.controlType() == FrameType::StopSending) {
    frame.errorCode = ref.value();
    frame.value = 0;
  }
  frame.streamType = ref.streamType();
  if (ref.controlType() == FrameType::PathChallenge ||
	ref.controlType() == FrameType::PathResponse)
    frame.payload = ref.payload();
  else if (ref.controlType() == FrameType::NewCxnID) {
    frame.cxnID = ref.cxnID();
    frame.resetToken = ref.resetToken();
  }
  return frame;
}

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
    kind{ref.kind}, controlType{ref.controlType()}, streamID{ref.streamID},
    offset{ref.offset}, length{ref.length}, value{ref.value()}, fin{ref.fin} { }

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
	(ref.controlType() == FrameType::MaxData ||
	  ref.controlType() == FrameType::MaxStreamData ||
	  ref.controlType() == FrameType::MaxStreams ||
	  ref.controlType() == FrameType::DataBlocked ||
	  ref.controlType() == FrameType::StreamDataBlocked ||
	  ref.controlType() == FrameType::StreamsBlocked ||
	  ref.controlType() == FrameType::ResetStream ||
	  ref.controlType() == FrameType::StopSending ||
	  ref.controlType() == FrameType::NewCxnID));
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

// Delayed qlog work owns metadata only, never retained plaintext or owners.
struct QLogTxFrame {
  SentFrameKind::T kind;
  FrameType::T controlType;
  uint64_t streamID;
  uint64_t offset;
  uint64_t length;
  uint64_t value;
  StreamType::T streamType;
  bool fin;
  CxnID cxnID;

  QLogTxFrame() = default;
  explicit QLogTxFrame(const SentFrameRef &ref) :
    kind{ref.kind}, controlType{ref.controlType()}, streamID{ref.streamID},
    offset{ref.offset}, length{ref.length}, value{ref.value()},
    streamType{ref.streamType()}, fin{ref.fin},
    cxnID{ref.controlType() == FrameType::NewCxnID ? ref.cxnID() : CxnID{}} { }
};

struct SentPkt {
  enum { MaxFrames = 8 };

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
  uint8_t	ackLevel = PktNumSpace::N;
  uint64_t	ackLargest = 0;
  ZuArray<SentFrameUpdate, MaxFrames> frames;

  uint64_t key() const { return pn; }
  uint64_t length() const { return 1; }
  uint64_t clipHead(uint64_t) { return 0; }
  uint64_t clipTail(uint64_t) { return 0; }
  template <typename I>
  void write(const I &) { }

  bool addFrame(SentFrameRef frame, void *owner = nullptr) {
    if (frame.kind == SentFrameKind::None || frames.length() >= MaxFrames)
      return false;
    frames.push(SentFrameUpdate{ZuMv(frame), owner});
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
  struct FlowData {
    OutstandingFlowKey id;
    RangeTree ranges;

    explicit FlowData(const OutstandingFlowKey &id_) : id{id_} { }
  };
  static const OutstandingFlowKey &flowID(const FlowData &flow) {
    return flow.id;
  }
  using Flows = ZmHash<FlowData,
    ZmHashNode<FlowData,
      ZmHashKey<flowID, ZmHashHeapID<"Zquic.Pkt.OutstandingFlow">>>>;
  using Flow = Flows::Node;
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
      return containsExact(SentFrameKey{frame});
    uint64_t end;
    if (!rangeEnd(frame, end)) return false;
    if (frame.length && coversRange(flowKey(frame), frame.offset, end))
      return true;
    return frame.kind == SentFrameKind::Stream && frame.fin &&
      containsExact(finKey(frame, end));
  }
  template <typename Queue>
  bool clip(SentFrameRef &frame, Queue &queue) const {
    if (frame.kind != SentFrameKind::Stream &&
	frame.kind != SentFrameKind::Crypto)
      return !outstanding(frame);
    uint64_t end;
    if (!rangeEnd(frame, end)) return false;
    bool sendFin = frame.kind == SentFrameKind::Stream && frame.fin &&
      !containsExact(finKey(frame, end));
    const Flow *flow = findFlow(flowKey(frame));
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
	const RangeTree &l = node->ranges;
	const RangeTree &r = other->ranges;
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
  static bool rangeEnd(const SentFrameRef &frame, uint64_t &end) {
    end = frame.offset + frame.length;
    return end >= frame.offset;
  }
  static OutstandingFlowKey flowKey(const SentFrameRef &frame) {
    return {frame.kind,
      frame.kind == SentFrameKind::Stream ? frame.streamID : 0};
  }
  static SentFrameKey finKey(const SentFrameRef &frame, uint64_t end) {
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
      uint64_t rangeOffset = uint64_t(frame.range().offset) + advance;
      if (rangeOffset > uint64_t(uint32_t(-1))) return false;
      frame.range().offset = uint32_t(rangeOffset);
      frame.range().length = uint32_t(length);
      frame.range().streamOffset = first;
      frame.fin = fin;
    }
    frame.offset = first;
    frame.length = length;
    return length || frame.fin;
  }
  Flow *ensureFlow(const OutstandingFlowKey &key) {
    if (m_flows)
      if (auto node = m_flows->findPtr(key)) return node;
    if (!m_flows)
      m_flows = new Flows{ZmHashParams().bits(3).loadFactor(1).cBits(2)
	.init(Flows::ID{}())};
    return m_flows->add(key);
  }
  const Flow *findFlow(const OutstandingFlowKey &key) const {
    if (!m_flows) return nullptr;
    return m_flows->findPtr(key);
  }
  void splitRange(Flow &flow, uint64_t offset) {
    auto node = flow.ranges.template findPtr<ZmRBTreeLessEqual>(offset);
    if (!node || node->key() >= offset || node->val().end <= offset) return;
    uint64_t end = node->val().end;
    unsigned count = node->val().count;
    node->val().end = offset;
    flow.ranges.add(offset, RangeVal{end, count});
  }
  void merge(Flow &flow, uint64_t offset) {
    auto right = flow.ranges.findPtr(offset);
    if (!right) return;
    auto left = flow.ranges.template findPtr<ZmRBTreeLess>(offset);
    if (!left || left->val().end != offset ||
	left->val().count != right->val().count)
      return;
    left->val().end = right->val().end;
    (void)flow.ranges.del(offset);
  }
  void addRange(const OutstandingFlowKey &key, uint64_t first, uint64_t end) {
    if (first >= end) return;
    Flow &flow = *ensureFlow(key);
    splitRange(flow, first);
    splitRange(flow, end);
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
      merge(flow, boundary);
    }
    merge(flow, end);
  }
  void delRange(const OutstandingFlowKey &key, uint64_t first, uint64_t end) {
    if (first >= end || !m_flows) return;
    auto flowNode = m_flows->findPtr(key);
    if (!flowNode) return;
    Flow &flow = *flowNode;
    splitRange(flow, first);
    splitRange(flow, end);
    uint64_t pos = first;
    while (pos < end) {
      uint64_t boundary = pos;
      auto node = flow.ranges.template findPtr<ZmRBTreeGreaterEqual>(pos);
      if (!node || node->key() != pos || node->key() >= end) break;
      uint64_t next = node->val().end;
      if (!--node->val().count) (void)flow.ranges.del(pos);
      pos = next;
      merge(flow, boundary);
    }
    merge(flow, end);
    if (!flow.ranges.count_()) (void)m_flows->del(key);
  }
  bool coversRange(
      const OutstandingFlowKey &key, uint64_t first, uint64_t end) const {
    const Flow *flow = findFlow(key);
    if (!flow) return false;
#ifdef ZDEBUG
    ++m_visits;
#endif
    auto node = flow->ranges.template findPtr<ZmRBTreeLessEqual>(first);
    return node && node->val().end >= end;
  }
  void addExact(const SentFrameKey &key) {
    if (!m_exact)
      m_exact = new Exact{ZmHashParams().bits(4).loadFactor(1).cBits(2)
	.init(Exact::ID{}())};
    if (auto node = m_exact->findPtr(key)) ++node->val();
    else m_exact->add(key, 1U);
  }
  void delExact(const SentFrameKey &key) {
    if (!m_exact) return;
    auto node = m_exact->findPtr(key);
    if (!node) return;
    if (--node->val()) return;
    (void)m_exact->del(key);
  }
  bool containsExact(const SentFrameKey &key) const {
#ifdef ZDEBUG
    ++m_visits;
#endif
    return m_exact && m_exact->findPtr(key);
  }
  void add_(const SentFrameRef &frame) {
    if (!SentFrameKey::retransmittable(frame)) return;
    if (frame.kind == SentFrameKind::Control) {
      addExact(SentFrameKey{frame});
      return;
    }
    uint64_t end;
    if (!rangeEnd(frame, end)) return;
    addRange(flowKey(frame), frame.offset, end);
    if (frame.kind == SentFrameKind::Stream && frame.fin)
      addExact(finKey(frame, end));
  }
  void del_(const SentFrameRef &frame) {
    if (!SentFrameKey::retransmittable(frame)) return;
    if (frame.kind == SentFrameKind::Control) {
      delExact(SentFrameKey{frame});
      return;
    }
    uint64_t end;
    if (!rangeEnd(frame, end)) return;
    delRange(flowKey(frame), frame.offset, end);
    if (frame.kind == SentFrameKind::Stream && frame.fin)
      delExact(finKey(frame, end));
  }

  ZmRef<Flows> m_flows;
  ZmRef<Exact> m_exact;
#ifdef ZDEBUG
  mutable uint64_t m_visits = 0;
#endif
};

// Snapshots contain no container links or reference count. The retained node
// owns its sent index and both non-owning recovery/loss memberships.
struct SentPktData : SentPkt, ZuObject {
  SentPktData() = default;
};
ZmListDerive(LostPktList_, SentPktData,
  ZmListNode<SentPktData, ZmListShadow<>>);
using LostPktList = Zquic_::Queue<LostPktList_>;
ZmListDerive(RecoveryPktList_, LostPktList::Node,
  ZmListNode<LostPktList::Node, ZmListShadow<>>);
using RecoveryPktList = Zquic_::Queue<RecoveryPktList_>;

using TxPkt = SentPkt;

// 64 matches the former common batch size; valid larger batches grow on heap.
using SentFrameUpdates = ZtBuiltin<
  ZtArray<SentFrameUpdate,
    ZtArrayHeapID<"Zquic.Recovery.FrameUpdates">>,
  64>;

struct PktTxQLog {
  enum { MaxFrames = 64, AckedPNSampleMax = 64 };
  using PNSample = ZuArray<uint64_t, AckedPNSampleMax>;

  PNSample ackedPNs = fixedArray<uint64_t, AckedPNSampleMax>();
  ZuArray<QLogTxFrame, MaxFrames> lostFrames;
  unsigned nAckedPNs = 0;
  bool ackedPNsTruncated = false;
};

struct PktTxUpdate {
  enum { MaxFrames = PktTxQLog::MaxFrames };
  enum { AckedPNSampleMax = PktTxQLog::AckedPNSampleMax };
  enum { MaxAckedPNs = AckedPNSampleMax };

  using AckFlags = ZuArray<bool, PktNumSpace::N>;
  using AckLargest = ZuArray<uint64_t, PktNumSpace::N>;

  uint64_t	ackdBytes = 0;
  PktNumSpace::T	level = PktNumSpace::Initial;
  unsigned	ecnAckdPackets = 0;
  uint64_t	ecnAckdBytes = 0;
  ZuTime	ecnAckdSentTime;
  uint64_t	normalAckdBytes = 0;
  uint64_t	eligibleAckdBytes = 0;
  uint64_t	pmtudAckdBytes = 0;
  unsigned	pmtudAckdSize = 0;
  uint64_t	lostBytes = 0;
  uint64_t	normalLostBytes = 0;
  uint64_t	pmtudLostBytes = 0;
  unsigned	pmtudLostSize = 0;
  ZuTime	normalLostSentTime;
  ZuTime	pmtudLostSentTime;
  AckFlags	ackdAck =
		  fixedArray<bool, PktNumSpace::N>();
  AckLargest	ackLargest =
		  fixedArray<uint64_t, PktNumSpace::N>();
  SentFrameUpdates ackdFrames;
  unsigned nLostFrames = 0;
#ifdef Zquic_DEBUG
  ZuUnion<void, PktTxQLog> qlogData;
#endif

  PktTxUpdate() {
#ifdef Zquic_DEBUG
    if (ZquicLogger::enabled())
      new (qlogData.new_<1>()) PktTxQLog{};
#endif
  }
  PktTxQLog *qlog() {
#ifdef Zquic_DEBUG
    return qlogData.type() == 1 ? &qlogData.p<1>() : nullptr;
#else
    return nullptr;
#endif
  }
  const PktTxQLog *qlog() const {
#ifdef Zquic_DEBUG
    return qlogData.type() == 1 ? &qlogData.p<1>() : nullptr;
#else
    return nullptr;
#endif
  }

  void ackd(SentPkt &p, bool wasInFlight, uint64_t boundary = 0) {
#ifdef Zquic_DEBUG
    if (auto log = qlog()) {
      if (log->nAckedPNs < AckedPNSampleMax)
	log->ackedPNs[log->nAckedPNs++] = p.pn;
      else
	log->ackedPNsTruncated = true;
    }
#endif
    noteAckdAck(p);
    addAckdFrames(p);
    if (p.lost) return;
    ackdBytes += p.bytes;
    switch (p.ecn) {
      case EcnMark::ECT0:
      case EcnMark::ECT1:
      case EcnMark::CE:
	++ecnAckdPackets;
	ecnAckdBytes += p.bytes;
	if (p.sentTime > ecnAckdSentTime) ecnAckdSentTime = p.sentTime;
	break;
      default:
	break;
    }
    if (!wasInFlight) return;
    if (p.pmtudProbe) {
      pmtudAckdBytes += p.bytes;
      if (p.pmtudSize > pmtudAckdSize)
	pmtudAckdSize = p.pmtudSize;
    } else {
      normalAckdBytes += p.bytes;
      if (!boundary || uint64_t(p.sentTime.microsecs()) > boundary)
	eligibleAckdBytes += p.bytes;
    }
  }
  void lost(const SentPkt &p) {
    addLostFrames(p);
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
  void noteAckdAck(const SentPkt &p) {
    if (p.ackLevel >= PktNumSpace::N) return;
    if (!ackdAck[p.ackLevel] || p.ackLargest > ackLargest[p.ackLevel]) {
      ackdAck[p.ackLevel] = true;
      ackLargest[p.ackLevel] = p.ackLargest;
    }
  }
  void addAckdFrames(SentPkt &p) {
    unsigned n = ackdFrames.length();
    for (unsigned i = 0, l = p.framesUsed(); i < l; ++i)
      ackdFrames.push(ZuMv(p.frames[i]));
    ZiAssert(ackdFrames.length() - n == p.framesUsed(), "Zquic", (),
      "ACK frame update truncation", return);
  }
  void addLostFrames(const SentPkt &p) {
    nLostFrames += p.framesUsed();
#ifdef Zquic_DEBUG
    if (auto log = qlog()) {
      for (unsigned i = 0, n = p.framesUsed();
	  i < n && log->lostFrames.length() < MaxFrames; ++i)
	log->lostFrames.push(QLogTxFrame{p.frame(i)});
    }
#endif
  }
  void clearAckdFrames() { ackdFrames.clear(); }
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

inline uint64_t sentPktPN(const SentPkt &p) { return p.pn; }
using SentPktTree = ZmRBTree<RecoveryPktList::Node,
  ZmRBTreeNode<RecoveryPktList::Node,
    ZmRBTreeKey<sentPktPN,
      ZmRBTreeHeapID<"Zquic.Pkt.TxSentNode">>>>;

class TxPktQueue : public Zquic_::Queue<SentPktTree> {
  using Base = Zquic_::Queue<SentPktTree>;
public:
  void addNode(Node *node) {
    Base::addNode(node);
    ++m_inCount;
  }
  NodeMvRef shift() {
    auto node = minimumPtr();
    if (!node) return {};
    ++m_outCount;
    return delNode(node);
  }
  void telemetry(Ztc::QueueTelemetry &data) const override {
    Base::telemetry(data);
    data.inCount = m_inCount;
    data.outCount = m_outCount;
  }

private:
  uint64_t m_inCount = 0;
  uint64_t m_outCount = 0;
};

class RetransmitQueue {
public:
  using Queue =
    Zquic_::Queue<ZmQueue<SentFrameRef,
      ZmQueueHeapID<"Zquic.Pkt.RetransmitQueue">>>;

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

  Queue *queue() const { return const_cast<Queue *>(&m_frames); }
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
	ZmHashParams().bits(8).loadFactor(1).cBits(3).init(SentFrameAckHash::ID{}())};
    return m_pending.ptr();
  }

  Queue		m_frames;
  ZmRef<SentFrameAckHash> m_pending;
  unsigned	m_dropped = 0;
};

class PktTxSpace {
public:
  enum { RetainedLostMax = 1024 };
  using Queue = TxPktQueue;
  using Msg = Queue::Node;

  void initQueues(Ztc::Hub *hub, ZuCSpan prefix) {
    m_packets.initQueue(hub, prefix, "sent", Ztc::QueueType::Tx, 0);
    m_retransmit.queue()->initQueue(hub, prefix, "retx", Ztc::QueueType::Tx, 0);
    m_lostPkts.initQueue(hub, prefix, "lost", Ztc::QueueType::Tx, RetainedLostMax);
    m_recovery.initQueue(hub, prefix, "recovery", Ztc::QueueType::Tx, 0);
  }
  template <typename L> void allQueues(L fn) const {
    fn(const_cast<Queue *>(&m_packets));
    fn(m_retransmit.queue());
    fn(const_cast<LostPktList *>(&m_lostPkts));
    fn(const_cast<RecoveryPktList *>(&m_recovery));
  }

  Queue *txQueue() { return &m_packets; }

  template <typename Init>
  bool add(uint64_t pn, Init init) {
    if (m_packets.findPtr(pn)) return false;
    ZmRef<Msg> node = new Msg;
    SentPkt &packet = node->data();
    packet.pn = pn;
    init(packet);
    m_packets.addNode(node.ptr());
    m_outstanding.add(packet);
    if (packet.inFlight) {
      m_bytesInFlight += packet.bytes;
      if (packet.ackEliciting) {
	linkRecovery(packet);
	if (!*m_latestAckSentTime || packet.sentTime > m_latestAckSentTime)
	  m_latestAckSentTime = packet.sentTime;
      }
    }
    return true;
  }
  bool add(SentPkt p) {
    return add(p.pn, [&p](SentPkt &packet) { packet = ZuMv(p); });
  }

  bool discard(uint64_t pn) {
    auto node = m_packets.find(pn);
    if (!node) return false;
    TxPkt &p = node->data();
    if (!p.ackd && !p.lost && !p.ptoReclaimed) m_outstanding.del(p);
    if (p.inFlight) release_(p);
    if (p.lost) unlinkLost(p);
    (void)m_packets.delNode(node.ptr());
    return true;
  }

  bool ack(uint64_t pn) {
    auto node = m_packets.find(pn);
    if (!node || !ack_(node->data())) return false;
    (void)m_packets.delNode(node.ptr());
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
	(void)m_packets.delNode(node);
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
    ZuTime *lostSentTime = nullptr, PktTxUpdate *update = nullptr,
    uint64_t boundary = 0)
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
	bool wasInFlight = p.inFlight;
	if (ack_(p)) {
	  if (!wasLost) ackdBytes_ += p.bytes;
	  if (update) update->ackd(p, wasInFlight, boundary);
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
	  (void)m_packets.delNode(node);
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
    unsigned budget, PktNumSpace::T level, PktTxUpdate *update = nullptr,
    uint64_t boundary = 0)
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
	bool wasInFlight = p.inFlight;
	if (ack_(p)) {
	  if (update) update->ackd(p, wasInFlight, boundary);
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
	  (void)m_packets.delNode(node);
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
      TxPkt &p = node->data();
      if (p.pn > last) break;
      if (lose_(p, false)) {
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
      TxPkt &p = node->data();
      if (p.pn > last) break;
      batch.nextPN = ZuNull(p.pn) ? p.pn : p.pn + 1;
      if (lose_(p, false)) {
	(void)iter.del();
	++batch.lost;
	if (update) update->lost(p);
      }
      if (++scanned >= budget) {
	const auto *head = m_recovery.headPtr();
	if (head && head->data().pn <= last) return false;
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
      TxPkt &p = node->data();
      if (p.pn >= largestAckd) break;
      if (!*p.sentTime || p.sentTime > now || now - p.sentTime < threshold)
	break;
      if (lose_(p, false)) {
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
      TxPkt &p = node->data();
      if (p.pn >= largestAckd) break;
      if (!*p.sentTime || p.sentTime > now || now - p.sentTime < threshold)
	break;
      batch.nextPN = ZuNull(p.pn) ? p.pn : p.pn + 1;
      if (lose_(p, false)) {
	(void)iter.del();
	++batch.lost;
	if (update) update->lost(p);
      }
      if (++scanned >= budget) {
	const auto *head = m_recovery.headPtr();
	if (head) {
	  const SentPkt &next = head->data();
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
    // The scheduler tests whether a deadline is set; zero is a valid time.
    // Empty spaces must not mask another space's live loss deadline.
    if (ZuNull(largestAckd) || !*threshold)
      return ZuTime{};
    const auto *head = m_recovery.headPtr();
#ifdef ZDEBUG
    if (head) ++m_recoveryVisits;
#endif
    const SentPkt *p = head ? &head->data() : nullptr;
    if (!p || p->pn >= largestAckd || !*p->sentTime) return ZuTime{};
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
      if (!p.ackd && !p.lost && lose_(p, true, false) && update) update->lost(p);
      if (wasInFlight && releasedBytes) *releasedBytes += p.bytes;
      if (p.lost) unlinkLost(p);
      (void)m_packets.delNode(node);
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
      if (clipOutstanding(frame)) return true;
    return false;
  }
  bool requeueRetransmit(const SentFrameRef &frame) {
    if (m_outstanding.outstanding(frame)) return false;
    return m_retransmit.push(frame);
  }
  unsigned reclaimOnPTO(unsigned limit) {
    unsigned n = 0;
    auto reclaim = [this, limit, &n](bool reclaimed, bool needFrames) {
      // New probes append newer packets. Start with the oldest eligible work
      // so their retransmissions cannot keep older control frames off PTO.
      auto iter = m_packets.iter();
      while (n < limit) {
	auto node = iter();
	if (!node) break;
	TxPkt &p = node->data();
	if (p.ackd || p.lost || !p.inFlight || !p.ackEliciting ||
	    (!reclaimed && p.ptoReclaimed) ||
	    (needFrames && !p.framesUsed()))
	  continue;
	if (!p.ptoReclaimed) m_outstanding.del(p);
	enqueueRetransmit(p);
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
  // The caller has stopped packet production and recovery before draining.
  // Preserve the index invariants between turns while retiring retained data.
  bool drain(unsigned &budget) {
    while (budget) {
      auto node = m_packets.shift();
      if (!node) break;
      auto &packet = node->data();
      if (!packet.ackd && !packet.lost && !packet.ptoReclaimed)
	m_outstanding.del(packet);
      if (packet.inFlight) release_(packet);
      if (packet.lost) unlinkLost(packet);
      --budget;
    }
    if (m_packets.count_()) return false;
    SentFrameRef frame;
    while (budget && m_retransmit.pop(frame)) --budget;
    if (!m_retransmit.empty()) return false;
    clear();
    return true;
  }

  void clear() {
    m_recovery.clean();
    m_outstanding.clear();
    m_lostPkts.clean();
    m_packets.clean();
    m_retransmit.clear();
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
      unlinkLost(p);
      return true;
    }
    if (p.inFlight) release_(p);
    ++m_ackd;
    return true;
  }

  bool lose_(TxPkt &p, bool unlink = true, bool trim = true) {
    if (p.ackd || p.lost) return false;
    if (!p.ptoReclaimed) m_outstanding.del(p);
    p.lost = true;
    m_lostPkts.pushNode(static_cast<LostPktList::Node *>(&p));
    ++m_retainedLost;
    if (p.inFlight) release_(p, unlink);
    if (p.ackEliciting && !p.pmtudProbe) {
      ++m_retransmittable;
      enqueueRetransmit(p);
    }
    if (trim) trimLost();
    ++m_lost;
    return true;
  }

  void release_(TxPkt &p, bool unlink = true) {
    p.inFlight = false;
    if (p.ackEliciting && unlink) unlinkRecovery(p);
    if (p.bytes > m_bytesInFlight) m_bytesInFlight = 0;
    else m_bytesInFlight -= p.bytes;
  }

  void linkRecovery(TxPkt &p) {
#ifdef ZDEBUG
    const auto *tail = m_recovery.tailPtr();
    ZmAssert(!tail || tail->data().pn < p.pn);
#endif
    m_recovery.pushNode(static_cast<RecoveryPktList::Node *>(&p));
  }

  void unlinkRecovery(TxPkt &p) {
    (void)m_recovery.delNode(static_cast<RecoveryPktList::Node *>(&p));
  }

  void unlinkLost(TxPkt &p) {
    (void)m_lostPkts.delNode(static_cast<LostPktList::Node *>(&p));
    --m_retainedLost;
  }

  void enqueueRetransmit(const SentPkt &p) {
    for (unsigned i = 0, n = p.framesUsed(); i < n; ++i) {
      const SentFrameRef &frame = p.frame(i);
      if (m_outstanding.outstanding(frame)) continue;
      m_retransmit.push(frame);
    }
  }

  bool clipOutstanding(SentFrameRef &frame) {
    return m_outstanding.clip(frame, m_retransmit);
  }
  void trimLost() {
    if (m_retainedLost > RetainedLostMax) m_lostPkts.queueFull();
    while (m_retainedLost > RetainedLostMax) {
      auto lost = m_lostPkts.shift();
      auto node = static_cast<Msg *>(lost);
      (void)m_packets.delNode(node);
      --m_retainedLost;
    }
  }

  Queue		m_packets;
  RetransmitQueue m_retransmit;
  OutstandingFrames m_outstanding;
  LostPktList	m_lostPkts;
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
