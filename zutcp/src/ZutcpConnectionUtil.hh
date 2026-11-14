//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// TCP byte credit and receive final-size state

#ifndef Zutcp_HH
#error "include zlib/Zutcp.hh before this header"
#endif

#include <zlib/ZuTime.hh>

namespace Zutcp {

inline uint64_t tcpPosition(uint32_t wire, uint64_t near) {
  int64_t delta = int32_t(wire - uint32_t(near));
  if (delta < 0 && uint64_t(-delta) > near) return U64Null;
  return delta < 0 ? near - uint64_t(-delta) : near + uint64_t(delta);
}

class FlowCredit {
public:
  FlowCredit() = default;
  explicit FlowCredit(uint64_t limit) : m_limit{limit} { }

  uint64_t used() const { return m_used; }
  uint64_t limit() const { return m_limit; }
  uint64_t available() const { return m_used < m_limit ? m_limit - m_used : 0; }
  bool blocked() const { return m_used >= m_limit; }
  bool allows(uint64_t n) const { return n <= available(); }
  bool allows(uint64_t offset, uint64_t length) const {
    uint64_t end = offset + length;
    return end >= offset && end <= m_limit;
  }
  bool shouldUpdate(unsigned divisor = 2) const {
    return divisor && available() <= m_limit / divisor;
  }

  bool consume(uint64_t n) {
    if (n > available()) return false;
    m_used += n;
    return true;
  }
  // Charge an original native packet in full when its first byte has credit.
  bool consumePacket(uint64_t n) {
    if (!available() || n > uint64_t(-1) - m_used) return false;
    m_used += n;
    return true;
  }
  void set(uint64_t limit) {
    m_limit = limit;
    if (m_used > m_limit) m_used = m_limit;
  }
  bool consumeTo(uint64_t n) {
    if (n > m_limit) return false;
    if (n > m_used) m_used = n;
    return true;
  }
  void extend(uint64_t limit) { if (limit > m_limit) m_limit = limit; }
  void advertise(uint64_t limit) { m_limit = limit; }
  void release(uint64_t n) { m_used = n > m_used ? 0 : m_used - n; }

private:
  uint64_t	m_used = 0;
  uint64_t	m_limit = 0;
};

class RxState {
public:
  bool finSeen() const { return m_finSeen; }
  bool finalSizeKnown() const { return m_finalSizeKnown; }
  uint64_t finalSize() const { return m_finalSize; }
  uint64_t delivered() const { return m_delivered; }

  bool validate(uint64_t offset, uint64_t length, bool fin) const {
    uint64_t end = offset + length;
    if (end < offset) return false;
    if (m_finalSizeKnown && end > m_finalSize) return false;
    if (fin) {
      if (m_finalSizeKnown && m_finalSize != end) return false;
    }
    return !m_finalSizeKnown || m_delivered <= m_finalSize;
  }

  bool receive(uint64_t offset, uint64_t length, bool fin) {
    if (!validate(offset, length, fin)) return false;
    uint64_t end = offset + length;
    if (fin) {
      m_finalSizeKnown = true;
      m_finSeen = true;
      m_finalSize = end;
    }
    if (offset <= m_delivered && end > m_delivered) m_delivered = end;
    return true;
  }

  void delivered(uint64_t n) {
    if (n > m_delivered) m_delivered = n;
  }

  bool complete() const {
    return m_finalSizeKnown && m_delivered == m_finalSize;
  }

private:
  bool		m_finSeen = false;
  bool		m_finalSizeKnown = false;
  uint64_t	m_finalSize = 0;
  uint64_t	m_delivered = 0;
};



} // namespace Zutcp
