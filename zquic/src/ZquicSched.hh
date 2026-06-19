//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC Tx scheduling utilities

#ifndef ZquicSched_HH
#define ZquicSched_HH

#ifndef ZquicLib_HH
#include <zlib/ZquicLib.hh>
#endif

#include <string.h>

#include <zlib/ZmHash.hh>
#include <zlib/ZmQueue.hh>

#include <zlib/ZquicBuf.hh>
#include <zlib/ZquicStream.hh>

namespace Zquic {

struct PktBudget {
  unsigned	pmtu = MinUDPPayload;
  unsigned	congestion = MinUDPPayload;
  unsigned	antiAmplification = MinUDPPayload;
  unsigned	used = 0;
  uint64_t	flow = uint64_t(-1);

  unsigned limit() const {
    unsigned n = pmtu;
    if (congestion < n) n = congestion;
    if (antiAmplification < n) n = antiAmplification;
    if (BufSize < n) n = BufSize;
    return n;
  }
  unsigned remaining() const {
    unsigned n = limit();
    return used < n ? n - used : 0;
  }
  unsigned flowRemaining() const {
    return flow > unsigned(-1) ? unsigned(-1) : unsigned(flow);
  }
  bool canFit(unsigned n) const { return used + n <= limit(); }
  bool add(unsigned n) {
    if (!canFit(n)) return false;
    used += n;
    return true;
  }
};

class PktAssembly {
public:
  bool streamAdded() const { return m_streamFrames; }
  unsigned controlFrames() const { return m_controlFrames; }
  unsigned streamFrames() const { return m_streamFrames; }

  bool addControl(PktBudget &budget, unsigned bytes) {
    if (!budget.add(bytes)) return false;
    ++m_controlFrames;
    return true;
  }
  bool addStream(PktBudget &budget, unsigned bytes) {
    if (!budget.add(bytes)) return false;
    ++m_streamFrames;
    return true;
  }

private:
  unsigned	m_controlFrames = 0;
  unsigned	m_streamFrames = 0;
};

class Pacer {
public:
  explicit Pacer(unsigned maxDatagram = MinUDPPayload) :
    m_maxDatagram{maxDatagram} { }

  unsigned maxDatagram() const { return m_maxDatagram; }
  uint64_t interval() const { return m_interval; }
  uint64_t nextSendTime() const { return m_nextSend; }
  bool armed() const { return m_nextSend; }
  bool canSend(uint64_t now) const {
    return !m_nextSend || now >= m_nextSend;
  }
  uint64_t delay(uint64_t now) const {
    return canSend(now) ? 0 : m_nextSend - now;
  }

  void sent(uint64_t now, unsigned bytes, uint64_t cwnd, uint64_t srtt) {
    if (!bytes || !cwnd || !srtt) {
      m_interval = 0;
      m_nextSend = now;
      return;
    }
    m_interval = interval_(bytes, cwnd, srtt);
    m_nextSend =
      m_interval > uint64_t(-1) - now ? uint64_t(-1) : now + m_interval;
  }

  void reset() {
    m_interval = 0;
    m_nextSend = 0;
  }

private:
  static uint64_t interval_(unsigned bytes, uint64_t cwnd, uint64_t srtt) {
    uint64_t n =
      bytes && srtt > uint64_t(-1) / bytes ? uint64_t(-1) :
      srtt * uint64_t(bytes);
    if (ZuCmp<uint64_t>::null(n)) return n;
    if (n > uint64_t(-1) - (cwnd - 1)) return uint64_t(-1);
    n = (n + cwnd - 1) / cwnd;
    return n ? n : 1;
  }

  unsigned	m_maxDatagram = MinUDPPayload;
  uint64_t	m_interval = 0;
  uint64_t	m_nextSend = 0;
};

struct StreamFrameInfo {
  uint64_t	streamID = 0;
  uint64_t	offset = 0;
  unsigned	length = 0;
  unsigned	bytes = 0;
  bool		fin = false;
  TxRange	range;
};

struct ControlFrame {
  FrameType::T		type = FrameType::Unknown;
  uint64_t		streamID = 0;
  uint64_t		value = 0;
  uint64_t		errorCode = 0;
  Zi::StreamType::T	streamType = Zi::StreamType::Duplex;
  uint8_t		payload[8]{};

  bool operator !() const { return type == FrameType::Unknown; }
  ZuOpBool
  bool operator ==(const ControlFrame &o) const {
    if (type != o.type || streamID != o.streamID || value != o.value ||
	errorCode != o.errorCode || streamType != o.streamType)
      return false;
    if (type == FrameType::PathChallenge ||
	type == FrameType::PathResponse)
      return !memcmp(payload, o.payload, sizeof(payload));
    return true;
  }

  static ControlFrame flowUpdate(const FlowUpdate &update) {
    return ControlFrame{
      update.type, update.streamID, update.maximum, 0, update.streamType, {}};
  }
  static ControlFrame blocked(
    FrameType::T type_, uint64_t streamID_, uint64_t value_,
    Zi::StreamType::T streamType_ = Zi::StreamType::Duplex) {
    return ControlFrame{type_, streamID_, value_, 0, streamType_, {}};
  }
  static ControlFrame resetStream(
    uint64_t streamID_, uint64_t appError, uint64_t finalSize) {
    return ControlFrame{
      FrameType::ResetStream, streamID_, finalSize, appError,
      Zi::StreamType::Duplex, {}};
  }
  static ControlFrame stopSending(uint64_t streamID_, uint64_t appError) {
    return ControlFrame{
      FrameType::StopSending, streamID_, 0, appError,
      Zi::StreamType::Duplex, {}};
  }
  static ControlFrame pathResponse(ZuCSpan data) {
    ControlFrame frame;
    frame.type = FrameType::PathResponse;
    if (data.length() == sizeof(frame.payload))
      memcpy(frame.payload, data.data(), sizeof(frame.payload));
    return frame;
  }
  static ControlFrame pathChallenge(ZuCSpan data) {
    ControlFrame frame;
    frame.type = FrameType::PathChallenge;
    if (data.length() == sizeof(frame.payload))
      memcpy(frame.payload, data.data(), sizeof(frame.payload));
    return frame;
  }
  static ControlFrame handshakeDone() {
    ControlFrame frame;
    frame.type = FrameType::HandshakeDone;
    return frame;
  }

  int write(uint8_t *out, unsigned len) const {
    switch (type) {
      case FrameType::MaxData:
	return FrameCodec::writeMaxData(out, len, value);
      case FrameType::MaxStreamData:
	return FrameCodec::writeMaxStreamData(out, len, streamID, value);
      case FrameType::MaxStreams:
	return FrameCodec::writeMaxStreams(out, len, streamType, value);
      case FrameType::DataBlocked:
	return FrameCodec::writeDataBlocked(out, len, value);
      case FrameType::StreamDataBlocked:
	return FrameCodec::writeStreamDataBlocked(out, len, streamID, value);
      case FrameType::StreamsBlocked:
	return FrameCodec::writeStreamsBlocked(out, len, streamType, value);
      case FrameType::ResetStream:
	return FrameCodec::writeResetStream(out, len, streamID, errorCode, value);
      case FrameType::StopSending:
	return FrameCodec::writeStopSending(out, len, streamID, errorCode);
      case FrameType::PathChallenge:
	return FrameCodec::writePathChallenge(out, len, ZuCSpan{
	  payload, sizeof(payload)});
      case FrameType::PathResponse:
	return FrameCodec::writePathResponse(out, len, ZuCSpan{
	  payload, sizeof(payload)});
      case FrameType::HandshakeDone:
	return FrameCodec::writeHandshakeDone(out, len);
      default:
	return -1;
    }
  }
};

class StreamPktizer {
public:
  template <typename Stream>
  static int writeNext(
    uint8_t *out, unsigned len, PktBudget &budget,
    PktAssembly &assembly, Stream &stream,
    StreamFrameInfo *info = nullptr)
  {
    if (info) *info = {};
    int64_t id = stream.id();
    if (!out || !len || id < 0) return -1;
    unsigned avail = budget.remaining();
    if (!avail) return -1;

    TxRange range;
    bool fin = false;
    if (!stream.nextTxRange(budget, range, fin)) return 0;
    if (!fin)
      return writeRange_(out, avail, budget, assembly, stream, uint64_t(id),
	info, range);

    return writeFin_(out, avail, budget, assembly, stream, uint64_t(id), info);
  }

private:
  template <typename Stream>
  static int writeRange_(
    uint8_t *out, unsigned len, PktBudget &budget,
    PktAssembly &assembly, Stream &stream, uint64_t id,
    StreamFrameInfo *info, const TxRange &range)
  {
    ZiAssert(range.buf && range.length, "Zquic", (),
      "empty stream Tx range selected for packetization", return -1);
    ZiAssert(range.offset + range.length <= range.buf->size, "Zquic",
      (),
      "stream Tx range exceeds buffer capacity", return -1);
    unsigned payloadLen = range.length;
    if (payloadLen > len) payloadLen = len;
    if (payloadLen > budget.remaining()) payloadLen = budget.remaining();
    if (payloadLen > budget.flowRemaining()) payloadLen = budget.flowRemaining();
    if (payloadLen > stream.txCreditAvailable())
      payloadLen = unsigned(stream.txCreditAvailable());
    bool fin = false;
    int n = -1;
    while (payloadLen) {
      fin = stream.finSent() && stream.txRangeCount() == 1 &&
	payloadLen == range.length;
      n = FrameCodec::writeStreamPrefix(
	out, len, id, range.streamOffset, payloadLen, fin);
      if (n > 0 && unsigned(n) + payloadLen <= budget.remaining()) break;
      --payloadLen;
    }
    if (n <= 0) return -1;
    unsigned frameBytes = unsigned(n) + payloadLen;
    if (!assembly.addStream(budget, frameBytes)) return -1;
    TxRange consumed;
    if (!stream.commitTxRange(consumed, range, payloadLen)) return -1;
    ZiAssert(stream.consumeTxCredit(payloadLen), "Zquic",
      (),
      "stream Tx exceeded MAX_STREAM_DATA", return -1);
    if (fin) {
      uint64_t dequeuedOffset = 0;
      ZiAssert(stream.dequeueFin(dequeuedOffset) &&
	  dequeuedOffset == range.streamOffset + payloadLen,
	"Zquic", (),
	"stream FIN disappeared during data packetization", return -1);
    }
    if (info)
      *info = StreamFrameInfo{
	id, range.streamOffset, payloadLen, frameBytes, fin, ZuMv(consumed)};
    return n;
  }

  template <typename Stream>
  static int writeFin_(
    uint8_t *out, unsigned len, PktBudget &budget,
    PktAssembly &assembly, Stream &stream, uint64_t id,
    StreamFrameInfo *info)
  {
    uint64_t offset = stream.txBytes();
    int n = FrameCodec::writeStream(out, len, id, offset, {}, true);
    if (n <= 0) return -1;
    if (!assembly.addStream(budget, unsigned(n))) return -1;
    uint64_t dequeuedOffset = 0;
    ZiAssert(stream.dequeueFin(dequeuedOffset) && dequeuedOffset == offset,
      "Zquic", (),
      "stream FIN disappeared during packetization", return -1);
    if (info)
      *info = StreamFrameInfo{id, offset, 0, unsigned(n), true, {}};
    return n;
  }
};

struct StreamScheduleEntry {
  StreamScheduleEntry() = default;
  StreamScheduleEntry(uint64_t id_) : id{id_} { }

  uint64_t	id = 0;
  StreamScheduleEntry *rrPrev = nullptr;
  StreamScheduleEntry *rrNext = nullptr;
};

inline uint64_t StreamScheduleEntry_IDAxor(const StreamScheduleEntry &entry)
{
  return entry.id;
}

ZuDerive(StreamScheduleHash,
  (ZmHash<StreamScheduleEntry,
    ZmHashNode<StreamScheduleEntry,
      ZmHashKey<StreamScheduleEntry_IDAxor,
	ZmHashLock<ZmNoLock,
	  ZmHashHeapID<"Zquic.StreamScheduler">>>>>));

class StreamScheduler {
public:
  StreamScheduler() = default;
  StreamScheduler(const ZmHashParams &params) : m_hash{params} { }

  bool add(uint64_t id) {
    if (m_hash.findPtr(id)) return true;
    link_(m_hash.add(id));
    return true;
  }
  bool empty() const { return !m_hash.count_(); }
  unsigned count() const { return m_hash.count_(); }
  bool contains(uint64_t id) const { return m_hash.findPtr(id); }

  uint64_t next() {
    if (!m_next) return uint64_t(-1);
    auto node = m_next;
    m_next = node_(node->rrNext);
    return node->id;
  }

  void remove(uint64_t id) {
    auto node = m_hash.findPtr(id);
    if (!node) return;
    unlink_(node);
    m_hash.delNode(node);
  }

  void clear() {
    while (m_next) remove(m_next->id);
  }

private:
  using Node = StreamScheduleHash::Node;

  static Node *node_(StreamScheduleEntry *entry) {
    return static_cast<Node *>(entry);
  }

  void link_(Node *node) {
    if (!m_next) {
      node->rrPrev = node->rrNext = node;
      m_next = node;
      return;
    }
    auto tail = node_(m_next->rrPrev);
    node->rrPrev = tail;
    node->rrNext = m_next;
    tail->rrNext = node;
    m_next->rrPrev = node;
  }

  void unlink_(Node *node) {
    if (node->rrNext == node) {
      m_next = nullptr;
      return;
    }
    if (m_next == node) m_next = node_(node->rrNext);
    node->rrPrev->rrNext = node->rrNext;
    node->rrNext->rrPrev = node->rrPrev;
    node->rrPrev = node->rrNext = nullptr;
  }

  StreamScheduleHash	m_hash;
  Node			*m_next = nullptr;
};

class TxScheduler {
public:
  bool addControl(uint64_t id) { return m_control.add(id); }
  bool addStream(uint64_t id) { return m_stream.add(id); }

  bool empty() const { return m_control.empty() && m_stream.empty(); }
  unsigned controlCount() const { return m_control.count(); }
  unsigned streamCount() const { return m_stream.count(); }

  uint64_t next() {
    if (!m_control.empty()) return m_control.next();
    return m_stream.next();
  }

  void remove(uint64_t id) {
    m_control.remove(id);
    m_stream.remove(id);
  }

private:
  StreamScheduler	m_control;
  StreamScheduler	m_stream;
};

} // namespace Zquic

#endif /* ZquicSched_HH */
