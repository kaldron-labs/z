//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC recovery helpers

#ifndef ZquicRecovery_HH
#define ZquicRecovery_HH

#ifndef ZquicSched_HH
#include <zlib/ZquicSched.hh>
#endif

namespace Zquic {

class AckTracker {
public:
  static constexpr unsigned Max = 16;

  bool add(uint64_t pn) {
    for (unsigned i = 0; i < m_count; ++i) {
      if (pn >= m_first[i] && pn <= m_last[i]) return true;
      bool before = pn != uint64_t(-1) && pn + 1 == m_first[i];
      bool after = m_last[i] != uint64_t(-1) && pn == m_last[i] + 1;
      if (!before && !after) continue;
      if (before) m_first[i] = pn;
      else m_last[i] = pn;
      merge_();
      return true;
    }
    if (m_count >= Max) return false;
    m_first[m_count] = m_last[m_count] = pn;
    ++m_count;
    merge_();
    return true;
  }

  bool contains(uint64_t pn) const {
    for (unsigned i = 0; i < m_count; ++i) {
      if (pn >= m_first[i] && pn <= m_last[i]) return true;
    }
    return false;
  }

  unsigned count() const { return m_count; }
  uint64_t first(unsigned i) const { return i < m_count ? m_first[i] : 0; }
  uint64_t last(unsigned i) const { return i < m_count ? m_last[i] : 0; }
  bool range(unsigned i, AckRange &range) const {
    if (i >= m_count) return false;
    range = AckRange{m_last[i], m_first[i]};
    return true;
  }
  uint64_t largest() const {
    uint64_t v = 0;
    for (unsigned i = 0; i < m_count; ++i)
      if (!i || m_last[i] > v) v = m_last[i];
    return v;
  }
  int writeFrame(uint8_t *out, unsigned len, uint64_t delay = 0) const {
    AckRange ranges[Max];
    for (unsigned i = 0; i < m_count; ++i)
      ranges[i] = AckRange{m_last[i], m_first[i]};
    return FrameCodec::writeAckRanges(out, len, ranges, m_count, delay);
  }
  void clear() { m_count = 0; }

private:
  static bool adjacent_(uint64_t l, uint64_t r) {
    return l == r || (l < r && r - l == 1);
  }

  void merge_() {
    for (unsigned i = 1; i < m_count; ++i) {
      uint64_t first = m_first[i], last = m_last[i];
      unsigned j = i;
      while (j && first < m_first[j - 1]) {
	m_first[j] = m_first[j - 1];
	m_last[j] = m_last[j - 1];
	--j;
      }
      m_first[j] = first;
      m_last[j] = last;
    }
    unsigned out = 0;
    for (unsigned i = 0; i < m_count; ++i) {
      if (out && (m_first[i] <= m_last[out - 1] ||
	    adjacent_(m_last[out - 1], m_first[i]))) {
	if (m_last[i] > m_last[out - 1]) m_last[out - 1] = m_last[i];
      } else {
	m_first[out] = m_first[i];
	m_last[out] = m_last[i];
	++out;
      }
    }
    m_count = out;
  }

  uint64_t	m_first[Max] = {};
  uint64_t	m_last[Max] = {};
  unsigned	m_count = 0;
};

class AckManager {
public:
  static constexpr unsigned Spaces = 3;

  bool received(
    PacketSpace::T space, uint64_t pn, uint64_t now, uint64_t maxAckDelay,
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

  const AckTracker &tracker(PacketSpace::T space) const {
    return m_ack[index_(space)];
  }
  bool pending(PacketSpace::T space) const {
    return m_pending[index_(space)];
  }
  bool deadlineSet(PacketSpace::T space) const {
    return m_deadlineSet[index_(space)];
  }
  uint64_t deadline(PacketSpace::T space) const {
    return m_deadline[index_(space)];
  }
  bool due(PacketSpace::T space, uint64_t now) const {
    unsigned i = index_(space);
    return m_pending[i] && m_deadlineSet[i] && now >= m_deadline[i];
  }
  int writeFrame(
    PacketSpace::T space, uint8_t *out, unsigned len,
    uint64_t delay = 0) const
  {
    return m_ack[index_(space)].writeFrame(out, len, delay);
  }
  void sent(PacketSpace::T space) {
    unsigned i = index_(space);
    m_ack[i].clear();
    m_pending[i] = false;
    m_deadlineSet[i] = false;
    m_deadline[i] = 0;
  }

private:
  static unsigned index_(PacketSpace::T space) {
    if (space == PacketSpace::Initial) return 0;
    if (space == PacketSpace::Handshake) return 1;
    return 2;
  }

  AckTracker	m_ack[Spaces];
  bool		m_pending[Spaces] = {};
  bool		m_deadlineSet[Spaces] = {};
  uint64_t	m_deadline[Spaces] = {};
};

class RttEstimator {
public:
  uint64_t latest() const { return m_latest; }
  uint64_t smoothed() const { return m_smoothed; }
  uint64_t variance() const { return m_variance; }

  void sample(uint64_t rtt, uint64_t ackDelay, bool appData) {
    if (appData && rtt > ackDelay) rtt -= ackDelay;
    m_latest = rtt;
    if (!m_smoothed) {
      m_smoothed = rtt;
      m_variance = rtt >> 1;
      return;
    }
    uint64_t diff = m_smoothed > rtt ? m_smoothed - rtt : rtt - m_smoothed;
    m_variance = (m_variance * 3 + diff) >> 2;
    m_smoothed = (m_smoothed * 7 + rtt) >> 3;
  }

  uint64_t pto(uint64_t maxAckDelay) const {
    if (!m_smoothed) return 1000000;
    return m_smoothed + (m_variance<<2) + maxAckDelay;
  }

private:
  uint64_t	m_latest = 0;
  uint64_t	m_smoothed = 0;
  uint64_t	m_variance = 0;
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
  uint64_t		streamID = 0;
  uint64_t		offset = 0;
  unsigned		length = 0;
  bool			fin = false;
  TxRange		range;

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

  static SentFrameRef crypto(uint64_t offset_, unsigned length_) {
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
};

struct SentPacket {
  static constexpr unsigned MaxFrames = 8;

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
  uint64_t	sentTime = 0;
  unsigned	bytes = 0;
  PacketSpace::T space = PacketSpace::AppData;
  bool		ackEliciting = false;
  bool		inFlight = false;
  bool		pmtudProbe = false;
  bool		acked = false;
  bool		lost = false;
  SentFrameRef	frames[MaxFrames];
  unsigned	frameCount = 0;
};

class RetransmitQueue {
public:
  static constexpr unsigned Max = 128;

  bool push(const SentFrameRef &frame) {
    if (frame.kind == SentFrameKind::None) return false;
    if (m_count >= Max) {
      ++m_dropped;
      return false;
    }
    unsigned tail = (m_head + m_count) % Max;
    m_frames[tail] = frame;
    ++m_count;
    return true;
  }

  bool pop(SentFrameRef &frame) {
    if (!m_count) return false;
    frame = m_frames[m_head];
    m_head = (m_head + 1) % Max;
    --m_count;
    return true;
  }

  unsigned count() const { return m_count; }
  unsigned dropped() const { return m_dropped; }
  bool empty() const { return !m_count; }
  void clear() {
    m_head = 0;
    m_count = 0;
  }

private:
  SentFrameRef	m_frames[Max];
  unsigned	m_head = 0;
  unsigned	m_count = 0;
  unsigned	m_dropped = 0;
};

class SentPacketTracker {
public:
  static constexpr unsigned Max = 64;

  bool add(const SentPacket &p) {
    if (find_(p.pn)) return false;
    if (m_count >= Max) return false;
    m_packets[m_count++] = p;
    if (p.inFlight) m_bytesInFlight += p.bytes;
    return true;
  }

  bool ack(uint64_t pn) {
    SentPacket *p = find_(pn);
    if (!p || p->acked || p->lost) return false;
    p->acked = true;
    if (p->inFlight) release_(p);
    ++m_acked;
    return true;
  }

  unsigned ack(
    const AckTracker &ranges, unsigned *lost = nullptr,
    unsigned packetThreshold = 3)
  {
    unsigned n = 0;
    for (unsigned i = 0; i < m_count; ++i)
      if (ranges.contains(m_packets[i].pn) && ack(m_packets[i].pn)) ++n;
    unsigned l = markPacketThresholdLoss(ranges.largest(), packetThreshold);
    if (lost) *lost = l;
    return n;
  }

  bool lose(uint64_t pn) {
    SentPacket *p = find_(pn);
    if (!p || p->acked || p->lost) return false;
    p->lost = true;
    if (p->inFlight) release_(p);
    if (p->ackEliciting && !p->pmtudProbe) {
      ++m_retransmittable;
      enqueueRetransmit_(*p);
    }
    ++m_lost;
    return true;
  }

  unsigned markPacketThresholdLoss(uint64_t largestAcked, unsigned threshold = 3) {
    unsigned n = 0;
    for (unsigned i = 0; i < m_count; ++i) {
      SentPacket &p = m_packets[i];
      if (p.acked || p.lost || p.pn + threshold > largestAcked) continue;
      if (lose(p.pn)) ++n;
    }
    return n;
  }

  unsigned markTimeThresholdLoss(uint64_t now, uint64_t threshold) {
    unsigned n = 0;
    for (unsigned i = 0; i < m_count; ++i) {
      SentPacket &p = m_packets[i];
      if (p.acked || p.lost || p.sentTime > now ||
	  now - p.sentTime < threshold)
	continue;
      if (lose(p.pn)) ++n;
    }
    return n;
  }

  uint64_t bytesInFlight() const { return m_bytesInFlight; }
  unsigned acked() const { return m_acked; }
  unsigned lost() const { return m_lost; }
  unsigned retransmittable() const { return m_retransmittable; }
  unsigned retransmitPending() const { return m_retransmit.count(); }
  unsigned retransmitDropped() const { return m_retransmit.dropped(); }
  bool nextRetransmit(SentFrameRef &frame) {
    return m_retransmit.pop(frame);
  }
  unsigned count() const { return m_count; }
  bool persistentCongestion(uint64_t threshold) const {
    bool have = false;
    uint64_t first = 0, last = 0;
    for (unsigned i = 0; i < m_count; ++i) {
      const SentPacket &p = m_packets[i];
      if (!p.lost || !p.ackEliciting || p.pmtudProbe) continue;
      if (!have || p.sentTime < first) first = p.sentTime;
      if (!have || p.sentTime > last) last = p.sentTime;
      have = true;
    }
    if (!have || last <= first || last - first < threshold) return false;
    for (unsigned i = 0; i < m_count; ++i) {
      const SentPacket &p = m_packets[i];
      if (p.acked && p.ackEliciting && !p.pmtudProbe &&
	  p.sentTime >= first && p.sentTime <= last)
	return false;
    }
    return true;
  }

private:
  SentPacket *find_(uint64_t pn) {
    for (unsigned i = 0; i < m_count; ++i)
      if (m_packets[i].pn == pn) return &m_packets[i];
    return nullptr;
  }

  void release_(SentPacket *p) {
    p->inFlight = false;
    if (p->bytes > m_bytesInFlight) m_bytesInFlight = 0;
    else m_bytesInFlight -= p->bytes;
  }

  void enqueueRetransmit_(const SentPacket &p) {
    for (unsigned i = 0; i < p.framesUsed(); ++i)
      m_retransmit.push(p.frame(i));
  }

  SentPacket	m_packets[Max];
  RetransmitQueue m_retransmit;
  unsigned	m_count = 0;
  uint64_t	m_bytesInFlight = 0;
  unsigned	m_acked = 0;
  unsigned	m_lost = 0;
  unsigned	m_retransmittable = 0;
};

class PTOBackoff {
public:
  uint64_t timeout(const RttEstimator &rtt, uint64_t maxAckDelay) const {
    return rtt.pto(maxAckDelay) << m_count;
  }
  void expired() { if (m_count < 16) ++m_count; }
  void reset() { m_count = 0; }
  unsigned count() const { return m_count; }

private:
  unsigned	m_count = 0;
};

} // namespace Zquic

#endif /* ZquicRecovery_HH */
