//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// TCP packet budgets and congestion pacing

#ifndef Zutcp_HH
#error "include zlib/Zutcp.hh before this header"
#endif

#include <string.h>

#include <zlib/ZmHash.hh>
#include <zlib/ZmQueue.hh>
#include <zlib/ZutcpAlloc.hh>

namespace Zutcp {

struct PktBudget {
  unsigned	pmtu = DefaultMaxPayload;
  unsigned	congestion = DefaultMaxPayload;
  unsigned	used = 0;
  uint64_t	flow = uint64_t(-1);

  unsigned limit() const {
    unsigned n = pmtu;
    if (congestion < n) n = congestion;
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

class Pacer {
public:
  explicit Pacer(unsigned maxDatagram = DefaultMaxPayload) :
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
    m_interval = sendInterval(bytes, cwnd, srtt);
    m_nextSend =
      m_interval > uint64_t(-1) - now ? uint64_t(-1) : now + m_interval;
  }

  void reset() {
    m_interval = 0;
    m_nextSend = 0;
  }

private:
  static uint64_t sendInterval(unsigned bytes, uint64_t cwnd, uint64_t srtt) {
    uint64_t n =
      bytes && srtt > uint64_t(-1) / bytes ? uint64_t(-1) :
      srtt * uint64_t(bytes);
    if (ZuNull(n)) return n;
    if (n > uint64_t(-1) - (cwnd - 1)) return uint64_t(-1);
    n = (n + cwnd - 1) / cwnd;
    return n ? n : 1;
  }

  unsigned	m_maxDatagram = DefaultMaxPayload;
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

} // namespace Zutcp
