//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC Tx scheduling helpers

#ifndef ZquicSched_HH
#define ZquicSched_HH

#ifndef ZquicLib_HH
#include <zlib/ZquicLib.hh>
#endif

#include <zlib/ZquicBuf.hh>
#include <zlib/ZquicStream.hh>

namespace Zquic {

struct PacketBudget {
  unsigned	pmtu = MinUDPPayload;
  unsigned	congestion = MinUDPPayload;
  unsigned	antiAmplification = MinUDPPayload;
  unsigned	used = 0;

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
  bool canFit(unsigned n) const { return used + n <= limit(); }
  bool add(unsigned n) {
    if (!canFit(n)) return false;
    used += n;
    return true;
  }
};

class PacketAssembly {
public:
  bool streamAdded() const { return m_streamAdded; }
  unsigned controlFrames() const { return m_controlFrames; }

  bool addControl(PacketBudget &budget, unsigned bytes) {
    if (!budget.add(bytes)) return false;
    ++m_controlFrames;
    return true;
  }
  bool addStream(PacketBudget &budget, unsigned bytes) {
    if (m_streamAdded || !budget.add(bytes)) return false;
    m_streamAdded = true;
    return true;
  }

private:
  bool		m_streamAdded = false;
  unsigned	m_controlFrames = 0;
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
    if (n == uint64_t(-1)) return n;
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
};

class StreamPacketizer {
public:
  template <typename Stream>
  static int writeNext(
    uint8_t *out, unsigned len, PacketBudget &budget,
    PacketAssembly &assembly, Stream &stream,
    StreamFrameInfo *info = nullptr)
  {
    if (info) *info = {};
    int64_t id = stream.id();
    if (!out || !len || id < 0 || assembly.streamAdded()) return -1;
    unsigned avail = budget.remaining();
    if (len < avail) avail = len;
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
    uint8_t *out, unsigned len, PacketBudget &budget,
    PacketAssembly &assembly, Stream &stream, uint64_t id,
    StreamFrameInfo *info, const TxRange &range)
  {
    ZiAssert(range.buf && range.length, "Zquic",
      (range.buf, range.length),
      "empty stream Tx range selected for packetization", return -1);
    ZiAssert(range.offset + range.length <= range.buf->size, "Zquic",
      (range.offset, range.length, range.buf->size),
      "stream Tx range exceeds buffer capacity", return -1);
    const char *data =
      reinterpret_cast<const char *>(range.buf->data_() + range.offset);
    unsigned payloadLen = range.length;
    if (payloadLen > len) payloadLen = len;
    int n = -1;
    while (payloadLen) {
      ZuCSpan payload{data, payloadLen};
      n = FrameCodec::writeStream(
	out, len, id, range.streamOffset, payload, false);
      if (n > 0) break;
      --payloadLen;
    }
    if (n <= 0) return -1;
    if (!assembly.addStream(budget, unsigned(n))) return -1;
    TxRange consumed;
    ZiAssert(stream.commitTxRange(consumed, payloadLen), "Zquic",
      (id, payloadLen),
      "stream Tx range disappeared during packetization", return -1);
    if (info)
      *info = StreamFrameInfo{
	id, range.streamOffset, payloadLen, unsigned(n), false};
    return n;
  }

  template <typename Stream>
  static int writeFin_(
    uint8_t *out, unsigned len, PacketBudget &budget,
    PacketAssembly &assembly, Stream &stream, uint64_t id,
    StreamFrameInfo *info)
  {
    uint64_t offset = stream.txBytes();
    int n = FrameCodec::writeStream(out, len, id, offset, {}, true);
    if (n <= 0) return -1;
    if (!assembly.addStream(budget, unsigned(n))) return -1;
    uint64_t dequeuedOffset = 0;
    ZiAssert(stream.dequeueFin(dequeuedOffset) && dequeuedOffset == offset,
      "Zquic", (id, dequeuedOffset, offset),
      "stream FIN disappeared during packetization", return -1);
    if (info)
      *info = StreamFrameInfo{id, offset, 0, unsigned(n), true};
    return n;
  }
};

class StreamScheduler {
public:
  static constexpr unsigned Max = 64;

  bool add(uint64_t id) {
    if (contains(id)) return true;
    if (m_count >= Max) return false;
    m_ids[m_count++] = id;
    return true;
  }
  bool empty() const { return !m_count; }
  unsigned count() const { return m_count; }
  bool contains(uint64_t id) const {
    for (unsigned i = 0; i < m_count; ++i)
      if (m_ids[i] == id) return true;
    return false;
  }

  uint64_t next() {
    if (!m_count) return uint64_t(-1);
    if (m_next >= m_count) m_next = 0;
    return m_ids[m_next++];
  }

  void remove(uint64_t id) {
    for (unsigned i = 0; i < m_count; ++i) {
      if (m_ids[i] != id) continue;
      for (unsigned j = i + 1; j < m_count; ++j) m_ids[j - 1] = m_ids[j];
      --m_count;
      if (m_next > i) --m_next;
      return;
    }
  }

private:
  uint64_t	m_ids[Max] = {};
  unsigned	m_count = 0;
  unsigned	m_next = 0;
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
