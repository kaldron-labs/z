//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC stream utilities

#ifndef ZquicStreamUtil_HH
#define ZquicStreamUtil_HH

#ifndef ZquicLib_HH
#include <zlib/ZquicLib.hh>
#endif

#include <zlib/ZquicFrame.hh>
#include <zlib/ZquicPQueue.hh>

namespace Zquic {

struct StreamID {
  static bool client(uint64_t id) { return !(id & 1); }
  static bool server(uint64_t id) { return id & 1; }
  static bool uni(uint64_t id) { return id & 2; }
  static bool bidi(uint64_t id) { return !(id & 2); }
  static uint64_t ordinal(uint64_t id) { return id >> 2; }
  static uint64_t make(bool server, Zi::StreamType::T type, uint64_t ordinal) {
    return (ordinal<<2) | (server ? 1U : 0U) |
      (type == Zi::StreamType::Simplex ? 2U : 0U);
  }
};

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
  void release(uint64_t n) { m_used = n > m_used ? 0 : m_used - n; }

private:
  uint64_t	m_used = 0;
  uint64_t	m_limit = 0;
};

struct FlowUpdate {
  FrameType::T	type = FrameType::Unknown;
  uint64_t	streamID = 0;
  uint64_t	maximum = 0;
  Zi::StreamType::T	streamType = Zi::StreamType::Duplex;

  bool needed() const { return type != FrameType::Unknown; }

  int write(uint8_t *out, unsigned len) const {
    if (type == FrameType::MaxData)
      return FrameCodec::writeMaxData(out, len, maximum);
    if (type == FrameType::MaxStreamData)
      return FrameCodec::writeMaxStreamData(out, len, streamID, maximum);
    if (type == FrameType::MaxStreams)
      return FrameCodec::writeMaxStreams(out, len, streamType, maximum);
    return -1;
  }
};

class ReceiveFlow {
public:
  ReceiveFlow() = default;
  ReceiveFlow(
    uint64_t maxData, uint64_t maxStreamData,
    uint64_t dataWindow = 0, uint64_t streamWindow = 0) :
      m_connection{maxData}, m_stream{maxStreamData},
      m_dataWindow{dataWindow ? dataWindow : maxData},
      m_streamWindow{streamWindow ? streamWindow : maxStreamData}
    { }

  uint64_t dataUsed() const { return m_connection.used(); }
  uint64_t dataLimit() const { return m_connection.limit(); }
  uint64_t streamUsed() const { return m_stream.used(); }
  uint64_t streamLimit() const { return m_stream.limit(); }
  TransportError::T error() const { return m_error; }

  void extendData(uint64_t limit) { m_connection.extend(limit); }
  void extendStream(uint64_t limit) { m_stream.extend(limit); }

  bool receive(uint64_t streamEnd, uint64_t newBytes) {
    m_error = TransportError::NoError;
    if (streamEnd > m_stream.limit() ||
	newBytes > m_connection.available()) {
      m_error = TransportError::FlowControl;
      return false;
    }
    m_stream.consumeTo(streamEnd);
    m_connection.consume(newBytes);
    return true;
  }

  bool maxDataUpdate(FlowUpdate &update) {
    return update_(m_connection, m_dataWindow,
      FlowUpdate{FrameType::MaxData, 0, 0, Zi::StreamType::Duplex}, update);
  }

  bool maxStreamDataUpdate(uint64_t streamID, FlowUpdate &update) {
    return update_(m_stream, m_streamWindow,
      FlowUpdate{FrameType::MaxStreamData, streamID, 0, Zi::StreamType::Duplex},
      update);
  }

private:
  static bool update_(
    FlowCredit &credit, uint64_t window, FlowUpdate base, FlowUpdate &update)
  {
    update = {};
    if (!window || credit.available() > window / 2) return false;
    uint64_t maximum =
      credit.used() > uint64_t(-1) - window ? uint64_t(-1) :
      credit.used() + window;
    if (maximum <= credit.limit()) return false;
    credit.extend(maximum);
    base.maximum = maximum;
    update = base;
    return true;
  }

  FlowCredit	m_connection;
  FlowCredit	m_stream;
  uint64_t	m_dataWindow = 0;
  uint64_t	m_streamWindow = 0;
  TransportError::T m_error = TransportError::NoError;
};

class StreamLimit {
public:
  StreamLimit() = default;
  explicit StreamLimit(uint64_t limit) : m_limit{limit} { }

  uint64_t opened() const { return m_opened; }
  uint64_t limit() const { return m_limit; }
  bool blocked() const { return m_opened >= m_limit; }
  bool allowsTo(uint64_t opened) const { return opened <= m_limit; }

  bool open() {
    if (blocked()) return false;
    ++m_opened;
    return true;
  }
  bool openTo(uint64_t opened) {
    if (opened > m_limit) return false;
    if (opened > m_opened) m_opened = opened;
    return true;
  }
  void set(uint64_t limit) { m_limit = limit; }
  void extend(uint64_t limit) { if (limit > m_limit) m_limit = limit; }

private:
  uint64_t	m_opened = 0;
  uint64_t	m_limit = 0;
};

class StreamRxState {
public:
  bool finSeen() const { return m_finSeen; }
  bool finalSizeKnown() const { return m_finalSizeKnown; }
  uint64_t finalSize() const { return m_finalSize; }
  uint64_t delivered() const { return m_delivered; }
  unsigned rangeCount() const { return 0; }
  uint64_t rangeFirst(unsigned) const { return 0; }
  uint64_t rangeLast(unsigned) const { return 0; }

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

class StreamTxState {
public:
  uint64_t sent() const { return m_sent; }
  bool finSent() const { return m_finSent; }
  bool resetSent() const { return m_resetSent; }
  bool stopReceived() const { return m_stopReceived; }

  void sent(uint64_t n) { m_sent += n; }
  void fin() { m_finSent = true; }
  void reset() { m_resetSent = true; }
  void stop() { m_stopReceived = true; }

private:
  uint64_t	m_sent = 0;
  bool		m_finSent = false;
  bool		m_resetSent = false;
  bool		m_stopReceived = false;
};

} // namespace Zquic

#endif /* ZquicStreamUtil_HH */
