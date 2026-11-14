//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// TCP byte recovery and local transmission history

#ifndef Zutcp_HH
#error "include zlib/Zutcp.hh before this header"
#endif

#include <string.h>

#include <zlib/ZuTuple.hh>

#include <zlib/ZmFn.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmQueue.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZutcpAlloc.hh>

namespace Zutcp {

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

// RFC8985 state uses the existing local framework clock, not wire timestamps.
struct RackState {
  ZuTime	sentTime, rtt, minRTT, srtt, deadline, sampleTime, sampleACK;
  uint64_t	sampleEnd = 0;
  bool		sampleKnown = false;
  uint64_t	end = 0, fack = 0, round = 0, serial = 1;
  unsigned	multiplier = 1, persist = 0;
  bool		known = false, reordering = false, recovering = false;

  static bool after(ZuTime t, uint64_t e, ZuTime other, uint64_t otherEnd) {
    return t > other || (t == other && e > otherEnd);
  }
  void beginAck(ZuTime now) {
    if (!*sampleACK || now >= sampleACK) { sampleACK = now; sampleKnown = false; }
  }
  void observe(const RackTransmission &tx, ZuTime now, bool echoKnown, uint32_t echo) {
    if (!tx.known || now < tx.sentTime || (*sampleACK && now < sampleACK)) return;
    ZuTime sample = now - tx.sentTime;
    if (tx.retransmitted &&
	((echoKnown && tx.tsKnown && int32_t(echo - tx.tsval) < 0) ||
	 (*minRTT && sample < minRTT))) return;
    if (!*minRTT || sample < minRTT) minRTT = sample;
    if (!tx.retransmitted && tx.end < fack) reordering = true;
    if (tx.end > fack) fack = tx.end;
    if (!sampleKnown || after(tx.sentTime, tx.end, sampleTime, sampleEnd)) {
      sampleKnown = true;
      sampleTime = tx.sentTime;
      sampleEnd = tx.end;
      rtt = sample;
    }
    if (!known || after(tx.sentTime, tx.end, sentTime, end)) {
      known = true;
      sentTime = tx.sentTime;
      end = tx.end;
    }
    ++serial;
  }
  void ack(uint64_t next, uint64_t sent, bool dsack) {
    if (round && next >= round) round = 0;
    if (dsack && !round) {
      round = sent;
      if (multiplier != unsigned(-1)) ++multiplier;
      persist = 16;
      ++serial;
    }
  }
  void recovery(bool active) {
    if (recovering != active) ++serial;
    if (recovering && !active && persist && !--persist) multiplier = 1;
    recovering = active;
  }
  ZuTime allowance(unsigned sacked) const {
    if (!reordering && (recovering || sacked >= 3)) return ZuTime{0};
    ZuTime value = timeDiv(minRTT, 4) * ZuDecimal{multiplier};
    return *srtt && value > srtt ? srtt : value;
  }
};

struct RackLossBatch {
  uint64_t	epoch = 0, serial = 0, nextByte = 0, nextPN = 0;
  ZuTime	deadline;
};

class RttEstimator {
public:
  enum { InitialRTTMS = 333 };
  enum { GranularityMS = 1 };
  static constexpr ZuTime InitialRTT{
    0, int32_t(InitialRTTMS * TimeNSPerMS)};
  static constexpr ZuTime Granularity{
    0, int32_t(GranularityMS * TimeNSPerMS)};

  ZuTime latest() const { return m_latest; }
  ZuTime min() const { return m_min; }
  ZuTime smoothed() const { return m_smoothed; }
  ZuTime variance() const { return m_variance; }

  void sample(ZuTime rtt) {
    if (!*rtt || !rtt) return;
    if (!*m_min || rtt < m_min) m_min = rtt;
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


private:
  ZuTime	m_latest;
  ZuTime	m_min;
  ZuTime	m_smoothed;
  ZuTime	m_variance;
};

struct RtoParams {
  ZuTime initial{1};
  ZuTime min{0, 200000000};
  ZuTime max{60};
  ZuTime granularity{0, 1000000};

  bool valid() const {
    return initial > ZuTime{0} && min > ZuTime{0} && max >= min &&
      initial >= min && initial <= max && granularity > ZuTime{0};
  }
};

// Tx-owned cumulative-progress timer. Caller supplies the monotonic clock and
// accepted-send/ACK events; packet IDs and kernel completion cannot restart it.
class DataRto {
public:
  ZuTime deadline() const { return m_deadline; }
  unsigned backoff() const { return m_backoff; }

  ZuTime timeout(const RttEstimator &rtt, const RtoParams &params) const {
    ZuTime out = params.initial;
    if (*rtt.smoothed()) {
      ZuTime variance = timePow2(rtt.variance(), 2);
      if (variance < params.granularity) variance = params.granularity;
      out = rtt.smoothed() + variance;
    }
    if (out < params.min) out = params.min;
    if (out > params.max) out = params.max;
    for (unsigned i = 0; i < m_backoff && out < params.max; ++i) {
      if (out >= params.max - out) out = params.max;
      else out += out;
    }
    return out;
  }
  void sent(ZuTime now, const RttEstimator &rtt, const RtoParams &params,
      bool retransmission = false) {
    if (!*m_deadline || retransmission) m_deadline = now + timeout(rtt, params);
  }
  void ack(ZuTime now, bool outstanding,
      const RttEstimator &rtt, const RtoParams &params) {
    m_backoff = 0;
    m_deadline = outstanding ? now + timeout(rtt, params) : ZuTime{};
  }
  bool expire(ZuTime now, const RttEstimator &rtt, const RtoParams &params) {
    if (!*m_deadline || now < m_deadline) return false;
    if (m_backoff < 63) ++m_backoff;
    m_deadline = now + timeout(rtt, params);
    return true;
  }
  void rearm(ZuTime now, const RttEstimator &rtt, const RtoParams &params) {
    m_deadline = now + timeout(rtt, params);
  }
  void reset() { m_deadline = ZuTime{}; m_backoff = 0; }

private:
  ZuTime m_deadline;
  unsigned m_backoff = 0;
};

// Fixed local probe episode; deadlines use the existing owner clock and timer.
class TailLossProbe {
public:
  ZuTime deadline() const { return m_deadline; }
  uint64_t end() const { return m_end; }
  uint64_t generation() const { return m_generation; }
  ZuTime sentTime() const { return m_sentTime; }
  bool retransmission() const { return m_retransmission; }
  bool pending() const { return !ZuNull(m_end); }
  bool freshRTT() const { return m_samples != m_usedSamples; }
  bool singleOriginal(uint64_t next) const { return next >= m_lastOriginal; }

  static ZuTime interval(
    ZuTime smoothed, ZuTime initialRto, ZuTime peerDelay, bool single)
  {
    if (!*smoothed) return initialRto;
    ZuTime timeout = smoothed + smoothed;
    return single ? timeout + peerDelay : timeout;
  }
  void sampledRTT() {
    if (!++m_samples) { m_samples = 1; m_usedSamples = 0; }
  }
  void original(uint64_t first) { m_lastOriginal = first; }
  void arm(ZuTime now, ZuTime smoothed, ZuTime initialRto,
      ZuTime peerDelay, bool single, ZuTime rto, bool eligible) {
    ++m_generation;
    if (!eligible || pending() || !*rto) { m_deadline = ZuTime{}; return; }
    ZuTime out = now + interval(smoothed, initialRto, peerDelay, single);
    m_deadline = out < rto ? out : rto;
  }
  bool expire(ZuTime now) {
    if (!*m_deadline || now < m_deadline) return false;
    m_deadline = ZuTime{};
    ++m_generation;
    return !pending() && freshRTT();
  }
  void accepted(uint64_t end, bool retransmission, ZuTime now) {
    ++m_generation;
    m_sentTime = now;
    m_end = end;
    m_retransmission = retransmission;
    m_usedSamples = m_samples;
    m_deadline = ZuTime{};
  }
  // True requests the single repaired-loss congestion response, before ACK growth.
  bool ack(uint64_t next, bool duplicate, bool sack, bool matchingDSACK) {
    if (!pending() || next < m_end) return false;
    bool loss = false;
    if (!m_retransmission || matchingDSACK) m_end = U64Null;
    else if (next > m_end) { m_end = U64Null; loss = true; }
    else if (duplicate && !sack) m_end = U64Null;
    return loss;
  }
  void recovery() {
    ++m_generation; m_deadline = ZuTime{}; m_end = U64Null; m_retransmission = false;
  }
  void cancel() { ++m_generation; m_deadline = ZuTime{}; }
  void reset() { *this = TailLossProbe{}; }

private:
  ZuTime m_deadline, m_sentTime;
  uint64_t m_end = U64Null, m_lastOriginal = 0;
  uint64_t m_samples = 0, m_usedSamples = 0, m_generation = 0;
  bool m_retransmission = false;
};

class NewReno {
public:
  explicit NewReno(unsigned maxDatagram = DefaultMaxPayload) :
    m_maxDatagram{maxDatagram}, m_cwnd{maxDatagram * 10} { }

  unsigned maxDatagram() const { return m_maxDatagram; }
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
  bool ecnEventAt(uint64_t sentTime, uint64_t eventTime) {
    uint64_t previous = m_cwnd;
    bool changed = congestionEventAt(sentTime, eventTime);
    if (changed) {
      m_cwnd = previous >> 1;
      if (m_cwnd < m_maxDatagram) m_cwnd = m_maxDatagram;
      if (m_cwnd > previous) m_cwnd = previous;
    }
    return changed;
  }
  void persistentCongestion() {
    ++m_epoch;
    m_cwnd = uint64_t(m_maxDatagram) * 2;
    m_ssthresh = m_cwnd;
  }
  void timeout(uint64_t now, bool repeated = false) {
    ++m_epoch;
    m_recoveryStartTime = now;
    if (!repeated) {
      m_ssthresh = m_bytesInFlight >> 1;
      if (m_ssthresh < uint64_t(m_maxDatagram)<<1)
	m_ssthresh = uint64_t(m_maxDatagram)<<1;
    }
    m_cwnd = m_maxDatagram;
  }

private:
  unsigned	m_maxDatagram = DefaultMaxPayload;
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

struct SentFrameRef {
  SentFrameKind::T	kind = SentFrameKind::None;
  uint64_t		offset = 0;
  uint64_t		length = 0;
  bool			fin = false;
  TxRange		range;

  bool operator !() const { return kind == SentFrameKind::None; }
  ZuOpBool

  static SentFrameRef data(const TxRange &range_, bool fin_) {
    SentFrameRef ref;
    ref.kind = SentFrameKind::Data;
    ref.offset = range_.dataOffset;
    ref.length = range_.length;
    ref.fin = fin_;
    ref.range = range_;
    return ref;
  }

};

struct SentFrameKey {
  SentFrameKind::T kind = SentFrameKind::None;
  uint64_t offset = 0, length = 0;
  bool fin = false;

  SentFrameKey() = default;
  explicit SentFrameKey(const SentFrameRef &ref) :
    kind{ref.kind}, offset{ref.offset}, length{ref.length}, fin{ref.fin} { }

  bool operator !() const { return kind == SentFrameKind::None; }
  ZuOpBool
  bool equals(const SentFrameKey &o) const { return !cmp(o); }
  int cmp(const SentFrameKey &o) const {
    int i;
    if (i = ZuCompare(kind, o.kind)) return i;
    if (i = ZuCompare(offset, o.offset)) return i;
    if (i = ZuCompare(length, o.length)) return i;
    return ZuCompare(fin, o.fin);
  }
  friend inline bool operator ==(const SentFrameKey &l, const SentFrameKey &r) {
    return l.equals(r);
  }
  friend inline int operator <=>(const SentFrameKey &l, const SentFrameKey &r) {
    return l.cmp(r);
  }
  uint32_t hash() const {
    return ZuHash<unsigned>::hash(unsigned(kind)) ^ ZuHash<uint64_t>::hash(offset);
  }
  static bool retransmittable(const SentFrameRef &ref) {
    return ref.kind == SentFrameKind::Data;
  }
};

struct OutstandingFlowKey {
  SentFrameKind::T kind = SentFrameKind::None;

  bool operator !() const { return kind == SentFrameKind::None; }
  ZuOpBool
  int cmp(const OutstandingFlowKey &o) const { return ZuCompare(kind, o.kind); }
  bool equals(const OutstandingFlowKey &o) const { return kind == o.kind; }
  uint32_t hash() const { return ZuHash<unsigned>::hash(unsigned(kind)); }
};

} // namespace Zutcp

template <> struct ZuCmp<Zutcp::SentFrameKey> {
  static int cmp(const Zutcp::SentFrameKey &l, const Zutcp::SentFrameKey &r) {
    return l.cmp(r);
  }
  static bool equals(
    const Zutcp::SentFrameKey &l, const Zutcp::SentFrameKey &r) {
    return l.equals(r);
  }
  static bool null(const Zutcp::SentFrameKey &key) { return !key; }
  static Zutcp::SentFrameKey null() { return {}; }
};

template <> struct ZuHash<Zutcp::SentFrameKey> {
  static uint32_t hash(const Zutcp::SentFrameKey &key) {
    return key.hash();
  }
};

template <> struct ZuCmp<Zutcp::OutstandingFlowKey> {
  static int cmp(
      const Zutcp::OutstandingFlowKey &l,
      const Zutcp::OutstandingFlowKey &r) { return l.cmp(r); }
  static bool equals(
      const Zutcp::OutstandingFlowKey &l,
      const Zutcp::OutstandingFlowKey &r) { return l.equals(r); }
  static bool null(const Zutcp::OutstandingFlowKey &key) { return !key; }
  static Zutcp::OutstandingFlowKey null() { return {}; }
};

template <> struct ZuHash<Zutcp::OutstandingFlowKey> {
  static uint32_t hash(const Zutcp::OutstandingFlowKey &key) {
    return key.hash();
  }
};

namespace Zutcp {

inline const SentFrameKey &SentFrameKey_IDAxor(const SentFrameKey &key)
{
  return key;
}

ZmHashDerive(SentFrameAckHash, SentFrameKey,
  (ZmHashNode<SentFrameKey,
    ZmHashKey<SentFrameKey_IDAxor,
	ZmHashHeapID<"Zutcp.Pkt.AckdFrame">>>));

struct SentFrameUpdate {
  SentFrameRef	ref;
};

struct SentPkt {
  enum { MaxFrames = 8 };

  uint64_t	sackEpoch = 0; // advisory evidence; never retires original backing
  uint32_t	tsval = 0;
  bool		tsKnown = false, sackCounted = false;

  uint64_t key() const { return pn; }
  uint64_t length() const { return 1; }
  uint64_t clipHead(uint64_t) { return 0; }
  uint64_t clipTail(uint64_t) { return 0; }
  template <typename I>
  void write(const I &) { }

  bool addFrame(const SentFrameRef &frame) {
    if (frame.kind == SentFrameKind::None || frames.length() >= MaxFrames)
      return false;
    frames.push(SentFrameUpdate{frame});
    return true;
  }
  unsigned framesUsed() const { return frames.length(); }
  const SentFrameRef &frame(unsigned i) const {
    ZiAssert(i < frames.length(), "Zutcp", (),
      "sent-packet frame index out of bounds", return frames[0].ref);
    return frames[i].ref;
  }
  uint64_t	pn = 0;
  uint64_t	byteAck = 0; // cumulative byte frontier already reported for this packet
  ZuTime	sentTime;
  unsigned	bytes = 0;
  EcnMark::T	ecn = EcnMark::NotECT;
  bool		ackEliciting = false;
  bool		inFlight = false;
  bool		pmtudProbe = false;
  unsigned	pmtudSize = 0;
  bool		ackd = false;
  bool		lost = false;
  void		*byteNode = nullptr;
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
      ZmRBTreeHeapID<"Zutcp.Pkt.OutstandingRange">>>;
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
      ZmHashKey<flowID, ZmHashHeapID<"Zutcp.Pkt.OutstandingFlow">>>>;
  using Flow = Flows::Node;
  using Exact = ZmHashKV<
    SentFrameKey, unsigned,
    ZmHashHeapID<"Zutcp.Pkt.OutstandingExact">>;

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
    uint64_t end;
    if (!rangeEnd(frame, end)) return false;
    if (frame.length && covered(flowKey(frame), frame.offset, end))
      return true;
    return frame.kind == SentFrameKind::Data && frame.fin &&
      exact(finKey(frame, end));
  }
  template <typename Queue>
  bool clip(SentFrameRef &frame, Queue &queue) const {
    if (frame.kind != SentFrameKind::Data)
      return !outstanding(frame);
    uint64_t end;
    if (!rangeEnd(frame, end)) return false;
    bool sendFin = frame.kind == SentFrameKind::Data && frame.fin &&
      !exact(finKey(frame, end));
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
      if (clipRange(part, pos, next, fin)) {
	if (fin) finQueued = true;
	if (!have) first = part, have = true;
	else queue.push(part);
      }
      pos = next;
    }
    if (sendFin && !finQueued) {
      SentFrameRef part = frame;
      if (clipRange(part, end, end, true)) {
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
    return {frame.kind};
  }
  static SentFrameKey finKey(const SentFrameRef &frame, uint64_t end) {
    SentFrameKey key{frame};
    key.offset = end;
    key.length = 0;
    key.fin = true;
    return key;
  }
  static bool clipRange(
      SentFrameRef &frame, uint64_t first, uint64_t end, bool fin) {
    if (end < first) return false;
    uint64_t length = end - first;
    if (frame.kind == SentFrameKind::Data) {
      if (first < frame.offset || length > uint64_t(uint32_t(-1)))
	return false;
      uint64_t advance = first - frame.offset;
      uint64_t rangeOffset = uint64_t(frame.range.offset) + advance;
      if (rangeOffset > uint64_t(uint32_t(-1))) return false;
      frame.range.offset = uint32_t(rangeOffset);
      frame.range.length = uint32_t(length);
      frame.range.dataOffset = first;
      frame.fin = fin;
    }
    frame.offset = first;
    frame.length = length;
    return length || frame.fin;
  }
  Flow *getFlow(const OutstandingFlowKey &key) {
    if (m_flows)
      if (auto node = m_flows->findPtr(key)) return node;
    if (!m_flows)
      m_flows = new Flows{ZmHashParams().bits(3).loadFactor(1).cBits(2)};
    return m_flows->add(key);
  }
  const Flow *findFlow(const OutstandingFlowKey &key) const {
    if (!m_flows) return nullptr;
    return m_flows->findPtr(key);
  }
  void split(Flow &flow, uint64_t offset) {
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
    Flow &flow = *getFlow(key);
    split(flow, first);
    split(flow, end);
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
    split(flow, first);
    split(flow, end);
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
  bool covered(
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
      m_exact = new Exact{ZmHashParams().bits(4).loadFactor(1).cBits(2)};
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
  bool exact(const SentFrameKey &key) const {
#ifdef ZDEBUG
    ++m_visits;
#endif
    return m_exact && m_exact->findPtr(key);
  }
  void add_(const SentFrameRef &frame) {
    if (!SentFrameKey::retransmittable(frame)) return;
    uint64_t end;
    if (!rangeEnd(frame, end)) return;
    addRange(flowKey(frame), frame.offset, end);
    if (frame.kind == SentFrameKind::Data && frame.fin)
      addExact(finKey(frame, end));
  }
  void del_(const SentFrameRef &frame) {
    if (!SentFrameKey::retransmittable(frame)) return;
    uint64_t end;
    if (!rangeEnd(frame, end)) return;
    delRange(flowKey(frame), frame.offset, end);
    if (frame.kind == SentFrameKind::Data && frame.fin)
      delExact(finKey(frame, end));
  }

  ZmRef<Flows> m_flows;
  ZmRef<Exact> m_exact;
#ifdef ZDEBUG
  mutable uint64_t m_visits = 0;
#endif
};

using TxPkt = SentPkt;

// 64 matches the former common batch size; valid larger batches grow on heap.
using SentFrameUpdates = ZtBuiltin<
  ZtArray<SentFrameUpdate,
    ZtArrayHeapID<"Zutcp.Recovery.FrameUpdates">>,
  64>;

struct PktTxUpdate {
  enum { MaxFrames = 64 }; // bounded recovery update batch
  uint64_t	ackdBytes = 0;
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
  SentFrameUpdates ackdFrames;
  SentFrameUpdates lostFrames;
  void ackd(
    const SentPkt &p, bool wasInFlight, uint64_t boundary = 0, bool dataFrames = true) {
    appendAckdFrames(p, dataFrames);
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
    appendLostFrames(p);
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
  void appendAckdFrames(const SentPkt &p, bool dataFrames) {
    unsigned n = ackdFrames.length();
    unsigned expected = 0;
    for (unsigned i = 0, l = p.framesUsed(); i < l; ++i) {
      if (!dataFrames && p.frame(i).kind == SentFrameKind::Data) continue;
      ackdFrames.push(SentFrameUpdate{p.frame(i)});
      ++expected;
    }
    ZiAssert(ackdFrames.length() - n == expected, "Zutcp", (),
      "ACK frame update truncation", return);
  }
  void appendLostFrames(const SentPkt &p) {
    unsigned n = lostFrames.length();
    for (unsigned i = 0, l = p.framesUsed(); i < l; ++i)
      lostFrames.push(SentFrameUpdate{p.frame(i)});
    ZiAssert(lostFrames.length() - n == p.framesUsed(), "Zutcp", (),
      "lost frame update truncation", return);
  }
  void clearAckdFrames() { ackdFrames.clear(); }


};

struct PktAckBatch {
  uint64_t	nextPN = 0;
  uint64_t	nextByte = 0;
  uint64_t	latestAckd = 0;
  unsigned	ackd = 0;
  bool		haveAckd = false;
  ZuTime	latestSentTime;
  uint64_t	tsPN = 0;
  ZuTime	tsSentTime;
  bool		tsMatch = false, tsAmbiguous = false;
};

struct PktSACKBatch {
  uint64_t	epoch = 0;
  uint64_t	nextByte = 0, nextPN = 0;
  unsigned	range = 0, marked = 0;
};

struct PktSACKLossBatch {
  uint64_t	epoch = 0;
  uint64_t	nextByte = 0, nextPN = 0;
  unsigned	lost = 0;
};

struct PktTailBatch {
  uint64_t epoch = 0;
  uint64_t nextByte = U64Null, nextPN = U64Null;
};

struct PktRetxBatch {
  uint64_t	epoch = 0;
  unsigned	remaining = 0;
  bool		started = false, found = false;
};

using BytePkt = ZuTuple<uint64_t, SentPkt *>;
inline auto bytePktKey(const BytePkt &p) { return ZuTuple{p.p<0>(), p.p<1>()->pn}; }
using BytePktTree = ZmRBTree<BytePkt, ZmRBTreeKey<bytePktKey, ZmRBTreeShadow<>>>;

// Node-local ownership is deliberately absent from copyable packet snapshots.
struct SentPktNode : public ZuObject {
  PacketLease backing;
  BytePktTree::Node byteNode; // non-owning, non-reference-counted intrusive index hook
};

using TxPktQueue =
  ZmPQueue<TxPkt,
    ZmPQueueNode<SentPktNode,
      ZmPQueueHeapID<"Zutcp.Pkt.TxSentNode",
	ZmPQueueBits<3,
	  ZmPQueueLevels<4>>>>>;

class RetransmitQueue {
public:
  using Queue =
    ZmQueue<SentFrameRef,
      ZmQueueHeapID<"Zutcp.Pkt.RetransmitQueue">>;

  bool push(const SentFrameRef &frame) {
    if (frame.kind == SentFrameKind::None) return false;
    if (SentFrameKey::retransmittable(frame)) {
      SentFrameKey key{frame};
      if (m_pending && m_pending->findPtr(key)) {
	++m_dropped;
	return false;
      }
      pending()->add(key);
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

  // Rotate only the captured initial population. Queue keys do not change,
  // so its pending hash stays intact and every popped entry is reinserted.
  template <typename Valid>
  bool prioritize(uint64_t next, PktRetxBatch &batch, unsigned budget, Valid valid) {
    while (batch.remaining && budget) {
      if (!m_frames.count_()) { batch.remaining = 0; break; }
      auto frame = m_frames.shift();
      --batch.remaining;
      --budget;
      uint64_t end = frame.offset + frame.length;
      bool covers = frame.kind == SentFrameKind::Data && frame.offset <= next &&
	(next < end || (frame.fin && next == end));
      if (covers && valid(frame)) {
	m_frames.unshift(ZuMv(frame));
	batch.found = true;
	batch.remaining = 0;
	return true;
      }
      m_frames.push(ZuMv(frame));
    }
    return !batch.remaining;
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
  SentFrameAckHash *pending() {
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
  enum { RetainedLostMax = 1024 };
  using Queue = TxPktQueue;
  using LostPNs =
    ZmQueue<uint64_t,
      ZmQueueHeapID<"Zutcp.Pkt.TxLostPNs">>;
  using Tx = ZmPQTx<PktTxSpace, Queue>;
  using Msg = Queue::Node;
  using Span = Queue::Span;
  using Key = Queue::Key;

  PktTxSpace() { Tx::start(); }

  Queue *txQueue() { return &m_packets; }

  bool send_(Msg *, bool) { return true; }
  bool resend_(Msg *msg, bool) {
    if (msg) enqueueRetransmit(msg->data());
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

  bool add(const SentPkt &p, PacketLease **backing = nullptr) {
    if (m_packets.has(p.pn)) return false;
    ZmRef<Msg> node = new Queue::Node{p};
    TxPkt &packet = node->data();
    packet.byteNode = nullptr;
    packet.sackEpoch = 0;
    packet.sackCounted = false;
    if (backing) *backing = &node->backing;
    Tx::send(node);
    uint64_t first = U64Null;
    for (unsigned i = 0, n = packet.framesUsed(); i < n; ++i) {
      const auto &ref = packet.frame(i);
      if (ref.kind == SentFrameKind::Data && (ref.length || ref.fin) && ref.offset < first)
	first = ref.offset;
    }
    if (!ZuNull(first)) {
      node->byteNode.data() = BytePkt{first, &packet};
      packet.byteNode = &node->byteNode;
      m_bytePackets.addNode(&node->byteNode);
    }
    m_outstanding.add(packet);
    if (packet.inFlight) m_bytesInFlight += packet.bytes;
    return true;
  }

  bool discard(uint64_t pn) {
    auto node = m_packets.find(pn);
    if (!node) return false;
    TxPkt &p = node->data();
    if (!p.ackd && !p.lost) m_outstanding.del(p);
    if (p.inFlight) releaseFlight(p);
    unlinkByte(p);
    (void)m_packets.del(pn);
    return true;
  }

  // Cumulative DATA evidence uses byte coordinates, independent of packet IDs.
  // Partial progress reports only the newly ACKed slice. Keep the original
  // packet/frame coordinates and backing until all of its sequence space retires.
  // The caller validates next against the accepted send frontier before entry.
  bool ackBytesBatch(
    uint64_t next, PktAckBatch &batch, unsigned budget,
    PktTxUpdate *update = nullptr, uint64_t boundary = 0,
    bool tsKnown = false, uint32_t tsEcho = 0,
    RackState *rack = nullptr, ZuTime ackTime = {})
  {
    if (!budget) return false;
    unsigned scanned = 0;
    auto iter = m_bytePackets.iter(ZuTuple{batch.nextByte, batch.nextPN});
    while (auto node = iter()) {
      uint64_t firstByte = node->data().p<0>();
      if (firstByte >= next) break;
#ifdef ZDEBUG
      ++m_byteAckVisits;
#endif
      TxPkt &p = *node->data().p<1>();
      uint64_t nextPN = p.pn + 1;
      bool data = false, complete = true, covered = false;
      for (unsigned i = 0, n = p.framesUsed(); i < n; ++i) {
	const auto &ref = p.frame(i);
	if (ref.kind != SentFrameKind::Data ||
	    (!ref.length && !ref.fin)) continue;
	data = true;
	uint64_t end = ref.offset + ref.length;
	complete &= next >= end + unsigned(ref.fin);
	if ((!update && !tsKnown) || next <= p.byteAck) continue;
	uint64_t first = ref.offset > p.byteAck ? ref.offset : p.byteAck;
	uint64_t limit = end < next ? end : next;
	bool fin = ref.fin && next > end && p.byteAck <= end;
	if (limit <= first && !fin) continue;
	covered = true;
	if (!update) continue;
	if (first > end) first = end;
	auto slice = ref;
	slice.offset = first;
	slice.length = limit > first ? limit - first : 0;
	slice.fin = fin;
	slice.range.offset += unsigned(first - ref.offset);
	slice.range.length = unsigned(slice.length);
	slice.range.dataOffset = first;
	update->ackdFrames.push(SentFrameUpdate{ZuMv(slice)});
      }
      if (data) {
	if (covered && tsKnown && p.tsKnown && p.tsval == tsEcho) {
	  if (!batch.tsMatch) {
	    batch.tsMatch = true;
	    batch.tsPN = p.pn;
	    batch.tsSentTime = p.sentTime;
	  } else if (p.pn != batch.tsPN) batch.tsAmbiguous = true;
	}
	if (next > p.byteAck) p.byteAck = next;
	if (complete) {
	  if (rack && p.sackEpoch != m_sackEpoch)
	    rackDelivered(p, *rack, ackTime, tsKnown, tsEcho);
	  bool wasLost = p.lost, wasInFlight = p.inFlight;
	  if (ackPacket(p)) {
	    if (update) update->ackd(p, wasInFlight, boundary, false);
	    if (!wasLost) {
	      ++batch.ackd;
	      if (!batch.haveAckd || p.pn > batch.latestAckd) {
		batch.latestAckd = p.pn;
		batch.latestSentTime = p.sentTime;
		batch.haveAckd = true;
	      }
	    }
	    (void)m_packets.del(p.pn);
	  }
	}
      }
      if (++scanned >= budget) {
	batch.nextPN = nextPN;
	batch.nextByte = firstByte;
	return false;
      }
    }
    return true;
  }

  // Blocks are already lifted, validated above cumulative ACK, canonicalized
  // and stripped of DSACK by the caller. SACK records receipt without releasing
  // frames, byte-index membership, packet leases or cumulative-flight accounting.
  bool sackBytesBatch(
    const RxSpan *ranges, unsigned nRanges, PktSACKBatch &batch, unsigned budget,
    RackState *rack = nullptr, ZuTime ackTime = {},
    bool echoKnown = false, uint32_t echo = 0)
  {
    if (!batch.epoch) batch.epoch = m_sackEpoch;
    if (batch.epoch != m_sackEpoch) return true;
    if (!budget) return false;
    unsigned scanned = 0;
    while (batch.range < nRanges) {
      const auto &range = ranges[batch.range];
      uint64_t start = batch.nextByte > range.first ? batch.nextByte : range.first;
      auto iter = m_bytePackets.iter(ZuTuple{start, batch.nextPN});
      while (auto node = iter()) {
	uint64_t first = node->data().p<0>();
	if (first >= range.last) break;
	TxPkt &p = *node->data().p<1>();
	if (p.inFlight && !p.ackd && !p.lost && p.sackEpoch != m_sackEpoch &&
	    sackContains(p, range)) {
	  if (rack) rackDelivered(p, *rack, ackTime, echoKnown, echo);
	  p.sackEpoch = m_sackEpoch;
	  p.sackCounted = true;
	  for (unsigned i = 0; i < p.framesUsed(); ++i) {
	    const auto &ref = p.frame(i);
	    if (ref.kind != SentFrameKind::Data) continue;
	    const auto &tx = transmission(ref);
	    if (tx.known && tx.pn != p.pn) p.sackCounted = false;
	  }
	  if (p.sackCounted) ++m_sackedPackets;
	  m_sackedFlightBytes += p.bytes;
	  ++batch.marked;
	}
	if (++scanned >= budget) {
	  batch.nextByte = first;
	  batch.nextPN = p.pn + 1;
	  return false;
	}
      }
      ++batch.range;
      batch.nextByte = batch.nextPN = 0;
    }
    return true;
  }

  // The caller owns duplicate-ACK/loss trigger policy and the highest validated
  // SACK frontier. Select holes through the existing byte index, preserving
  // current SACKed originals and using the ordinary lost/retransmission path.
  // An exclusive recovery-entry PN fence protects fresh retransmissions.
  bool loseSACKHolesBatch(
    uint64_t highestSACK, PktSACKLossBatch &batch, unsigned budget,
    PktTxUpdate &update, uint64_t beforePN = U64Null)
  {
    if (!batch.epoch) batch.epoch = m_sackEpoch;
    if (batch.epoch != m_sackEpoch) return true;
    if (!budget) return false;
    unsigned scanned = 0;
    while (true) {
      auto iter = m_bytePackets.iter(ZuTuple{batch.nextByte, batch.nextPN});
      auto node = iter();
      if (!node) break;
      uint64_t first = node->data().p<0>();
      if (first >= highestSACK) break;
      TxPkt &p = *node->data().p<1>();
      batch.nextByte = first;
      batch.nextPN = p.pn + 1;
      if (p.inFlight && p.pn < beforePN && p.sackEpoch != m_sackEpoch && lose_(p)) {
	++batch.lost;
	update.lost(p);
      }
      if (++scanned >= budget) return false;
    }
    return true;
  }

  // Reverse through the existing byte index, retaining the canonical native original.
  // The fixed cursor bounds owner work and survives ordinary queue retirement.
  bool highestOriginal(uint64_t next, PktTailBatch &batch, unsigned budget,
      SentFrameRef &selected) {
    selected = {};
    if (!batch.epoch) batch.epoch = m_sackEpoch;
    if (batch.epoch != m_sackEpoch) return true;
    auto iter = m_bytePackets.template iter<ZmRBTreeLess>(
      ZuTuple{batch.nextByte, batch.nextPN});
    while (budget) {
      auto node = iter();
      if (!node) return true;
      --budget;
      const auto &entry = node->data();
      TxPkt &packet = *entry.p<1>();
      batch.nextByte = entry.p<0>();
      batch.nextPN = packet.pn;
      if (!packet.inFlight || packet.ackd || packet.lost ||
	packet.sackEpoch == m_sackEpoch) continue;
      for (unsigned i = packet.framesUsed(); i; --i) {
	const auto &ref = packet.frame(i - 1);
	const auto &source = ref.range.storage;
	if (ref.kind != SentFrameKind::Data || !ref.length || !source || !source->data ||
	    ref.offset + ref.length + unsigned(ref.fin) <= next) continue;
	const auto &sent = transmission(ref);
	if (!sent.known || sent.pn != packet.pn) continue;
	selected = ref;
	return true;
      }
    }
    return false;
  }

  // RTO reneging invalidates advisory state and older bounded continuations in
  // O(1). Original records/frames/leases remain available to ordinary recovery.
  void forgetSACK() {
    if (!++m_sackEpoch) ++m_sackEpoch;
    m_sackedFlightBytes = 0;
    m_sackedPackets = 0;
  }
  uint64_t sackEpoch() const { return m_sackEpoch; }
  uint64_t sackedFlightBytes() const { return m_sackedFlightBytes; }
  unsigned sackedPackets() const { return m_sackedPackets; }

  bool lose(uint64_t pn) {
    auto node = m_packets.find(pn);
    return node && lose_(node->data());
  }

  const RackTransmission &transmission(const SentFrameRef &ref) const {
    return ref.range.storage ? ref.range.storage->rack : m_finTransmission;
  }

  // Commit chronology only after the backend accepted this exact transmission.
  // All copied ranges share their original backing; refusal never changes it.
  void acceptedData(uint64_t pn, ZuTime now) {
    auto node = m_packets.find(pn);
    if (!node) return;
    TxPkt &p = node->data();
    p.sentTime = now;
    for (unsigned i = 0; i < p.framesUsed(); ++i) {
      const auto &ref = p.frame(i);
      if (ref.kind != SentFrameKind::Data || (!ref.length && !ref.fin) ||
	  (ref.length && !ref.range.storage)) continue;
      auto &tx = ref.range.storage ? ref.range.storage->rack : m_finTransmission;
      tx.retransmitted = tx.known;
      tx.known = true;
      tx.pn = pn;
      tx.end = ref.offset + ref.length + unsigned(ref.fin);
      tx.sentTime = now;
      tx.tsKnown = p.tsKnown;
      tx.tsval = p.tsval;
    }
  }

  bool rackLossBatch(RackState &rack, ZuTime now, RackLossBatch &batch,
    unsigned budget, PktTxUpdate &update, bool rto = false, uint64_t oldest = 0)
  {
    if (!budget) return false;
    if (batch.epoch != m_sackEpoch || batch.serial != rack.serial) {
      batch = {};
      batch.epoch = m_sackEpoch;
      batch.serial = rack.serial;
    }
    unsigned scanned = 0;
    auto iter = m_bytePackets.iter(ZuTuple{batch.nextByte, batch.nextPN});
    while (auto node = iter()) {
      uint64_t first = node->data().p<0>();
      TxPkt &p = *node->data().p<1>();
      batch.nextByte = first;
      batch.nextPN = p.pn + 1;
      if (p.inFlight && !p.lost && !p.ackd && p.sackEpoch != m_sackEpoch) {
	bool eligible = false, expired = false;
	for (unsigned i = 0; i < p.framesUsed(); ++i) {
	  const auto &ref = p.frame(i);
	  if (ref.kind != SentFrameKind::Data || (!ref.length && !ref.fin) ||
	      (ref.length && !ref.range.storage)) continue;
	  const auto &tx = ref.range.storage ? ref.range.storage->rack : m_finTransmission;
	  if (!tx.known) continue;
	  bool prior = rack.known && RackState::after(
	    rack.sentTime, rack.end, tx.sentTime, tx.end);
	  bool covers = ref.offset <= oldest && oldest < tx.end;
	  if (!prior && !rto) continue;
	  eligible = true;
	  ZuTime due = tx.sentTime + rack.rtt + rack.allowance(m_sackedPackets);
	  if ((rto && covers) || due <= now) expired = true;
	  else if (!*batch.deadline || due > batch.deadline) batch.deadline = due;
	}
	if (eligible && expired && lose_(p)) update.lost(p);
      }
      if (++scanned >= budget) return false;
    }
    rack.deadline = batch.deadline;
    return true;
  }

  // Retire current transmissions covering the oldest unacknowledged byte.
  // Keep lost records for cumulative ACKs, including their original backing.
  // Repeated transmissions share byte identity; only the last live copy queues
  // retransmission after the outstanding index has released the other copies.
  bool reclaimBytesBatch(
    uint64_t next, PktAckBatch &batch, unsigned budget, PktTxUpdate &update)
  {
    if (!budget) return false;
    unsigned scanned = 0;
    while (true) {
      auto iter = m_bytePackets.iter(ZuTuple{batch.nextByte, batch.nextPN});
      auto node = iter();
      if (!node) break;
      uint64_t first = node->data().p<0>();
      if (first > next) break;
      TxPkt &p = *node->data().p<1>();
      batch.nextByte = first;
      batch.nextPN = p.pn + 1;
      if (lose_(p)) update.lost(p);
      if (++scanned >= budget) return false;
    }
    return true;
  }

  uint64_t bytesInFlight() const { return m_bytesInFlight; }
  unsigned ackd() const { return m_ackd; }
  unsigned lost() const { return m_lost; }
  unsigned retainedLost() const { return m_retainedLost; }
  unsigned retransmittable() const { return m_retransmittable; }
  unsigned retransmitPending() const { return m_retransmit.count(); }
  unsigned retransmitDropped() const { return m_retransmit.dropped(); }
  template <typename Valid>
  bool prioritizeRetransmit(
    uint64_t next, PktRetxBatch &batch, unsigned budget, Valid valid)
  {
    if (!batch.epoch) batch.epoch = m_sackEpoch;
    if (batch.epoch != m_sackEpoch) { batch.found = false; return true; }
    if (!batch.started) {
      batch.remaining = m_retransmit.count();
      batch.started = true;
    }
    return m_retransmit.prioritize(next, batch, budget, valid);
  }
  bool nextRetransmit(SentFrameRef &frame) {
    while (m_retransmit.pop(frame))
      if (clipOutstanding(frame)) return true;
    return false;
  }
  bool requeueRetransmit(const SentFrameRef &frame) {
    if (m_outstanding.outstanding(frame)) return false;
    return m_retransmit.push(frame);
  }
  unsigned count() const { return m_packets.count_(); }
#ifdef ZDEBUG
  uint64_t byteAckVisits() const { return m_byteAckVisits; }
  void resetByteAckVisits() const { m_byteAckVisits = 0; }
  uint64_t outstandingVisits() const { return m_outstanding.visits(); }
  void resetOutstandingVisits() const { m_outstanding.resetVisits(); }
  bool verifyOutstanding() const {
    OutstandingFrames expected;
    auto iter = m_packets.citer();
    while (auto node = iter()) {
      const TxPkt &p = node->data();
      if (!p.ackd && !p.lost) expected.add(p);
    }
    return m_outstanding.equals(expected);
  }
#endif
  void clear() {
    m_bytePackets.clean();
    m_finTransmission = {};
    m_outstanding.clear();
    Tx::txReset(0);
    m_retransmit.clear();
    m_lostPNs.clean();
    m_bytesInFlight = 0;
    forgetSACK();
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
  static bool sackContains(const TxPkt &p, const RxSpan &range) {
    bool data = false;
    for (unsigned i = 0, n = p.framesUsed(); i < n; ++i) {
      const auto &ref = p.frame(i);
      if (ref.kind != SentFrameKind::Data) continue;
      if (ref.fin) return false;
      if (!ref.length) continue;
      if (ref.offset < range.first || ref.offset >= range.last ||
	  ref.length > range.last - ref.offset) return false;
      data = true;
    }
    return data;
  }

  void unlinkByte(TxPkt &p) {
    if (!p.byteNode) return;
    m_bytePackets.delNode(static_cast<BytePktTree::Node *>(p.byteNode));
    p.byteNode = nullptr;
  }

  bool ackPacket(TxPkt &p) {
    if (p.ackd) return false;
    unlinkByte(p);
    if (!p.lost) m_outstanding.del(p);
    p.ackd = true;
    if (p.lost) {
      m_lostPNs.del(p.pn);
      if (m_retainedLost) --m_retainedLost;
      return true;
    }
    if (p.inFlight) releaseFlight(p);
    ++m_ackd;
    return true;
  }

  bool lose_(TxPkt &p) {
    if (p.ackd || p.lost) return false;
    m_outstanding.del(p);
    p.lost = true;
    m_lostPNs.push(p.pn);
    ++m_retainedLost;
    if (p.inFlight) releaseFlight(p);
    if (p.ackEliciting && !p.pmtudProbe) {
      ++m_retransmittable;
      enqueueRetransmit(p);
    }
    trimLost();
    ++m_lost;
    return true;
  }

  void releaseFlight(TxPkt &p) {
    if (p.sackEpoch == m_sackEpoch) {
      ZmAssert_(m_sackedFlightBytes >= p.bytes);
      m_sackedFlightBytes -= p.bytes;
      if (p.sackCounted) --m_sackedPackets;
    }
    p.sackEpoch = 0;
    p.inFlight = false;
    if (p.bytes > m_bytesInFlight) m_bytesInFlight = 0;
    else m_bytesInFlight -= p.bytes;
  }

  void rackDelivered(const SentPkt &p, RackState &rack, ZuTime now,
    bool echoKnown, uint32_t echo) {
    for (unsigned i = 0; i < p.framesUsed(); ++i) {
      const auto &ref = p.frame(i);
      if (ref.kind != SentFrameKind::Data || (!ref.length && !ref.fin) ||
	  (ref.length && !ref.range.storage)) continue;
      const auto &tx = ref.range.storage ? ref.range.storage->rack : m_finTransmission;
      if (tx.known && tx.pn == p.pn) rack.observe(tx, now, echoKnown, echo);
    }
  }

  void enqueueRetransmit(const SentPkt &p) {
    for (unsigned i = 0, n = p.framesUsed(); i < n; ++i) {
      const SentFrameRef &frame = p.frame(i);
      if (m_outstanding.outstanding(frame)) continue;
      m_retransmit.push(frame);
    }
  }

  bool clipOutstanding(SentFrameRef &frame) {
    if (frame.kind == SentFrameKind::Data &&
	frame.range.storage && frame.range.storage->pool)
      return !m_outstanding.outstanding(frame);
    return m_outstanding.clip(frame, m_retransmit);
  }
  void trimLost() {
    while (m_retainedLost > RetainedLostMax) {
      uint64_t pn = m_lostPNs.shift();
      auto node = m_packets.find(pn);
      if (!node) continue;
      TxPkt &p = node->data();
      if (!p.lost || p.ackd) continue;
      unlinkByte(p);
      (void)m_packets.del(pn);
      --m_retainedLost;
    }
  }

  Queue		m_packets{0};
  BytePktTree m_bytePackets;
  RetransmitQueue m_retransmit;
  OutstandingFrames m_outstanding;
  LostPNs	m_lostPNs{ZmQueueParams{}.initial(RetainedLostMax)};
  uint64_t	m_bytesInFlight = 0;
  uint64_t	m_sackEpoch = 1, m_sackedFlightBytes = 0;
  unsigned	m_sackedPackets = 0;
  RackTransmission m_finTransmission;
  unsigned	m_ackd = 0;
  unsigned	m_lost = 0;
  unsigned	m_retainedLost = 0;
  unsigned	m_retransmittable = 0;
#ifdef ZDEBUG
  mutable uint64_t m_byteAckVisits = 0;
#endif
};

using SentPktTracker = PktTxSpace;

} // namespace Zutcp
