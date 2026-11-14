//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC Tx scheduling utilities

#ifndef Zutcp_HH
#error "include zlib/Zutcp.hh before this header"
#endif

#include <string.h>

#include <zlib/ZmHash.hh>
#include <zlib/ZmQueue.hh>
#include <zlib/ZutcpAlloc.hh>

namespace Zutcp {

struct PktBudget {
  unsigned	pmtu = MinPktPayload;
  unsigned	congestion = MinPktPayload;
  unsigned	antiAmplification = MinPktPayload;
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
  bool useFlow(unsigned n, bool wholePacket = false) {
    if (uint64_t(n) > flow) {
      if (!wholePacket || !flow) return false;
      flow = 0;
    } else flow -= n;
    return true;
  }
};

class PktAssembly {
public:
  bool dataAdded() const { return m_dataFrames; }
  unsigned controlFrames() const { return m_controlFrames; }
  unsigned dataFrames() const { return m_dataFrames; }

  bool addControl(PktBudget &budget, unsigned bytes) {
    if (!budget.add(bytes)) return false;
    ++m_controlFrames;
    return true;
  }
  bool addData(PktBudget &budget, unsigned bytes) {
    if (!budget.add(bytes)) return false;
    ++m_dataFrames;
    return true;
  }

private:
  unsigned	m_controlFrames = 0;
  unsigned	m_dataFrames = 0;
};

class Pacer {
public:
  explicit Pacer(unsigned maxDatagram = MinPktPayload) :
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
    if (ZuNull(n)) return n;
    if (n > uint64_t(-1) - (cwnd - 1)) return uint64_t(-1);
    n = (n + cwnd - 1) / cwnd;
    return n ? n : 1;
  }

  unsigned	m_maxDatagram = MinPktPayload;
  uint64_t	m_interval = 0;
  uint64_t	m_nextSend = 0;
};

struct DataFrameInfo {
  uint64_t	offset = 0;
  unsigned	length = 0;
  unsigned	bytes = 0;
  bool		fin = false;
  TxRange	range;
};

struct ControlFrame {
  FrameType::T		type = FrameType::Unknown;
  uint64_t		value = 0;
  uint64_t		errorCode = 0;

  bool operator !() const { return type == FrameType::Unknown; }
  ZuOpBool
  bool operator ==(const ControlFrame &o) const {
    if (type != o.type || value != o.value ||
	errorCode != o.errorCode)
      return false;
    return true;
  }

  static ControlFrame flowUpdate(const FlowUpdate &update) {
    return ControlFrame{
       .type = update.type, .value = update.maximum};
  }
  static ControlFrame blocked(uint64_t maximum) {
    return ControlFrame{.type = FrameType::DataBlocked, .value = maximum};
  }
  static ControlFrame resetData(uint64_t appError, uint64_t finalSize) {
    return ControlFrame{
      .type = FrameType::Reset, .value = finalSize, .errorCode = appError};
  }
  static ControlFrame stopSending(uint64_t appError) {
    return ControlFrame{.type = FrameType::StopSending, .errorCode = appError};
  }





  int write(uint8_t *out, unsigned len) const {
    switch (type) {
      case FrameType::MaxData:
	return FrameCodec::writeMaxData(out, len, value);
      case FrameType::DataBlocked:
	return FrameCodec::writeDataBlocked(out, len, value);
      case FrameType::Reset:
	return FrameCodec::writeReset(out, len, errorCode, value);
      case FrameType::StopSending:
	return FrameCodec::writeStopSending(out, len, errorCode);
      default:
	return -1;
    }
  }
};

class DataPktizer {
public:
  enum { PrefixMax = 1 + 8 + 8 }; // type plus maximum offset/length VarInts
  template <typename Cxn>
  static int writeNext(
    uint8_t *out, unsigned len, PktBudget &budget,
    PktAssembly &assembly, Cxn &cxn,
    DataFrameInfo *info = nullptr)
  {
    if (info) *info = {};
    if (!out || !len) return -1;
    unsigned avail = budget.remaining();
    if (avail > len) avail = len;
    if (!avail) return -1;

    TxRange range;
    bool fin = false;
    if (!cxn.nextTxRange(budget, range, fin)) return 0;
    if (!fin)
      return writeRange(out, avail, budget, assembly, cxn,
	info, range);

    return writeFin(out, avail, budget, assembly, cxn, info);
  }

private:
  template <typename Cxn>
  static int writeRange(
    uint8_t *out, unsigned len, PktBudget &budget,
    PktAssembly &assembly, Cxn &cxn,
    DataFrameInfo *info, const TxRange &range)
  {
    ZiAssert(range.storage && range.length, "Zutcp", (),
      "empty cxn Tx range selected for packetization", return -1);
    ZiAssert(range.offset + range.length <= range.storage->size, "Zutcp",
      (),
      "cxn Tx range exceeds buffer capacity", return -1);
    bool direct = range.storage->native;
    bool wholePacket = direct && cxn.app()->nativeTx();
    unsigned payloadLen = range.length;
    if (payloadLen > budget.remaining()) payloadLen = budget.remaining();
    if (wholePacket) {
      if (!budget.flowRemaining() || !cxn.txCreditAvailable()) return -1;
    } else {
      if (payloadLen > budget.flowRemaining()) payloadLen = budget.flowRemaining();
      if (payloadLen > cxn.txCreditAvailable())
	payloadLen = unsigned(cxn.txCreditAvailable());
    }
    if (direct && payloadLen != range.length) return -1;
    bool fin = false;
    int n = -1;
    while (payloadLen) {
      fin = cxn.finSent() && cxn.finCredit() && cxn.txRangeCount() == 1 &&
	payloadLen == range.length;
      n = FrameCodec::writeDataPrefix(
	out, len, range.dataOffset, payloadLen, fin);
      if (n > 0 && unsigned(n) + payloadLen <= budget.remaining()) break;
      if (direct) return -1;
      --payloadLen;
    }
    if (n <= 0 || !payloadLen) return -1;
    // Native DATA starts on positive credit and keeps its original packet,
    // including bytes beyond the current right edge. Charge every byte once.
    if (range.storage->native && payloadLen != range.length) return -1;
    unsigned frameBytes = unsigned(n) + payloadLen;
    if (!assembly.addData(budget, frameBytes)) return -1;
    TxRange consumed;
    if (!cxn.commitTxRange(consumed, range, payloadLen)) return -1;
    ZiAssert(cxn.consumeTxCredit(payloadLen, wholePacket), "Zutcp",
      (),
      "cxn Tx exceeded MAX_DATA", return -1);
    ZiAssert(budget.useFlow(payloadLen, wholePacket), "Zutcp",
      (),
      "cxn Tx exceeded MAX_DATA packet budget", return -1);
    if (fin) {
      uint64_t dequeuedOffset = 0;
      ZiAssert(cxn.dequeueFin(dequeuedOffset) &&
	  dequeuedOffset == range.dataOffset + payloadLen,
	"Zutcp", (),
	"cxn FIN disappeared during data packetization", return -1);
    }
    if (info)
      *info = DataFrameInfo{
	range.dataOffset, payloadLen, frameBytes, fin, ZuMv(consumed)};
    return n;
  }

  template <typename Cxn>
  static int writeFin(
    uint8_t *out, unsigned len, PktBudget &budget,
    PktAssembly &assembly, Cxn &cxn,
    DataFrameInfo *info)
  {
    uint64_t offset = cxn.txBytes();
    int n = FrameCodec::writeData(out, len, offset, {}, true);
    if (n <= 0) return -1;
    if (!assembly.addData(budget, unsigned(n))) return -1;
    uint64_t dequeuedOffset = 0;
    ZiAssert(cxn.dequeueFin(dequeuedOffset) && dequeuedOffset == offset,
      "Zutcp", (),
      "cxn FIN disappeared during packetization", return -1);
    if (info)
      *info = DataFrameInfo{offset, 0, unsigned(n), true, {}};
    return n;
  }
};

} // namespace Zutcp
