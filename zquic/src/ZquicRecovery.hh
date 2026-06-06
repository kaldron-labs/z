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

class AckTracker :
  public ZmPQRx<AckTracker, PacketRxPQueue, ZmNoLock> {
public:
  static constexpr unsigned Max = 64;
  using Queue = PacketRxPQueue;
  using Rx = ZmPQRx<AckTracker, Queue, ZmNoLock>;
  using Msg = Queue::Node;
  using Span = Queue::Span;

  bool add(uint64_t pn) {
    if (contains(pn)) return true;
    Rx::rcvd(new Queue::Node{RxPacketMark{pn}});
    runDequeues_();
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
    m_dequeues = 0;
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
  void reRequest(const Span &now) {
    m_ackGap = now;
  }

  void scheduleDequeue() { ++m_dequeues; }
  void rescheduleDequeue() { ++m_dequeues; }
  void idleDequeue() { }

  void scheduleReRequest() { }
  void rescheduleReRequest() { }
  void cancelReRequest() { }

private:
  void runDequeues_() {
    while (m_dequeues) {
      --m_dequeues;
      Rx::dequeue();
    }
    refreshGap_();
  }

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
  unsigned	m_dequeues = 0;
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
  uint64_t		length = 0;
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
};

struct SentPacket {
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

using TxPacket = SentPacket;

using TxPacketQueue =
  ZmPQueue<TxPacket,
    ZmPQueueNode<ZuObject,
      ZmPQueueBits<3,
	ZmPQueueLevels<4>>>>;

class RetransmitQueue {
public:
  struct Entry {
    uint64_t		seq = 0;
    SentFrameRef	frame;

    Entry() = default;
    Entry(uint64_t seq_, const SentFrameRef &frame_) :
      seq{seq_}, frame{frame_} { }

    uint64_t key() const { return seq; }
    uint64_t length() const { return 1; }
    uint64_t clipHead(uint64_t) { return 0; }
    uint64_t clipTail(uint64_t) { return 0; }
    template <typename I>
    void write(const I &) { }
  };

  using Queue =
    ZmPQueue<Entry,
      ZmPQueueBits<2,
	ZmPQueueLevels<2>>>;

  bool push(const SentFrameRef &frame) {
    if (frame.kind == SentFrameKind::None) return false;
    m_frames.add(new Queue::Node{Entry{m_next++, frame}});
    return true;
  }

  bool pop(SentFrameRef &frame) {
    auto node = m_frames.dequeue();
    if (!node) return false;
    frame = node->data().frame;
    return true;
  }

  unsigned count() const { return m_frames.count_(); }
  unsigned dropped() const { return 0; }
  bool empty() const { return !m_frames.count_(); }
  void clear() {
    m_frames.reset(m_next);
  }

private:
  Queue		m_frames{0};
  uint64_t	m_next = 0;
};

class PacketTxSpace :
  public ZmPQTx<PacketTxSpace, TxPacketQueue, ZmNoLock> {
public:
  using Queue = TxPacketQueue;
  using Tx = ZmPQTx<PacketTxSpace, Queue, ZmNoLock>;
  using Msg = Queue::Node;
  using Span = Queue::Span;
  using Key = Queue::Key;

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

  bool add(const SentPacket &p) {
    if (m_packets.has(p.pn)) return false;
    m_packets.add(new Queue::Node{p});
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
    unsigned l = markPacketThresholdLoss(ranges.largest(), packetThreshold);
    if (lost) *lost = l;
    return n;
  }

  unsigned ack(
    const AckRange *ranges, unsigned nRanges, unsigned *lost = nullptr,
    unsigned packetThreshold = 3)
  {
    unsigned n = 0;
    uint64_t largest = 0;
    bool have = false;
    for (unsigned i = 0; i < nRanges; ++i) {
      const AckRange &range = ranges[i];
      if (range.first > range.largest) continue;
      if (!have || range.largest > largest) largest = range.largest;
      have = true;
      auto iter = m_packets.iter(range.first);
      while (auto node = iter()) {
	SentPacket &p = node->data();
	if (p.pn > range.largest) break;
	if (ack_(p)) ++n;
      }
    }
    unsigned l = have ? markPacketThresholdLoss(largest, packetThreshold) : 0;
    if (lost) *lost = l;
    return n;
  }

  bool lose(uint64_t pn) {
    auto node = m_packets.find(pn);
    return node && lose_(node->data());
  }

  unsigned markPacketThresholdLoss(uint64_t largestAcked, unsigned threshold = 3) {
    unsigned n = 0;
    auto iter = m_packets.iter();
    while (auto node = iter()) {
      SentPacket &p = node->data();
      if (p.acked || p.lost || p.pn + threshold > largestAcked) continue;
      if (lose_(p)) ++n;
    }
    return n;
  }

  unsigned markTimeThresholdLoss(uint64_t now, uint64_t threshold) {
    unsigned n = 0;
    auto iter = m_packets.iter();
    while (auto node = iter()) {
      SentPacket &p = node->data();
      if (p.acked || p.lost || p.sentTime > now ||
	  now - p.sentTime < threshold)
	continue;
      if (lose_(p)) ++n;
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
  unsigned count() const { return m_packets.count_(); }
  void clear() {
    m_packets.reset(0);
    m_retransmit.clear();
    m_bytesInFlight = 0;
    m_acked = 0;
    m_lost = 0;
    m_retransmittable = 0;
  }
  bool persistentCongestion(uint64_t threshold) const {
    bool have = false;
    uint64_t first = 0, last = 0;
    auto iter = m_packets.citer();
    while (auto node = iter()) {
      const SentPacket &p = node->data();
      if (!p.lost || !p.ackEliciting || p.pmtudProbe) continue;
      if (!have || p.sentTime < first) first = p.sentTime;
      if (!have || p.sentTime > last) last = p.sentTime;
      have = true;
    }
    if (!have || last <= first || last - first < threshold) return false;
    iter.reset();
    while (auto node = iter()) {
      const SentPacket &p = node->data();
      if (p.acked && p.ackEliciting && !p.pmtudProbe &&
	  p.sentTime >= first && p.sentTime <= last)
	return false;
    }
    return true;
  }

private:
  bool ack_(SentPacket &p) {
    if (p.acked || p.lost) return false;
    p.acked = true;
    if (p.inFlight) release_(p);
    ++m_acked;
    return true;
  }

  bool lose_(SentPacket &p) {
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

  void release_(SentPacket &p) {
    p.inFlight = false;
    if (p.bytes > m_bytesInFlight) m_bytesInFlight = 0;
    else m_bytesInFlight -= p.bytes;
  }

  void enqueueRetransmit_(const SentPacket &p) {
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

using SentPacketTracker = PacketTxSpace;

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
