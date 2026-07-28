//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP/2 logical streams, flow control, and Tx scheduling

#ifndef ZhttpH2Session_HH
#define ZhttpH2Session_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZuDerive.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtEnum.hh>

#include <zlib/ZhttpH2.hh>

namespace Zhttp {

namespace H2 {

ZtEnumNS(StreamState, int8_t,
  Idle, Open, HalfClosedLocal, HalfClosedRemote, Closed,
  ReservedLocal, ReservedRemote);

class StreamStateMachine {
public:
  StreamState::T state() const { return m_state; }
  bool localOpen() const {
    return m_state == StreamState::Open ||
      m_state == StreamState::HalfClosedRemote;
  }
  bool remoteOpen() const {
    return m_state == StreamState::Open ||
      m_state == StreamState::HalfClosedLocal;
  }
  bool open() {
    if (m_state != StreamState::Idle) return false;
    m_state = StreamState::Open;
    return true;
  }
  bool localEnd() {
    switch (m_state) {
      case StreamState::Open:
	m_state = StreamState::HalfClosedLocal;
	return true;
      case StreamState::HalfClosedRemote:
	m_state = StreamState::Closed;
	return true;
      default:
	return false;
    }
  }
  bool remoteEnd() {
    switch (m_state) {
      case StreamState::Open:
	m_state = StreamState::HalfClosedRemote;
	return true;
      case StreamState::HalfClosedLocal:
	m_state = StreamState::Closed;
	return true;
      default:
	return false;
    }
  }
  bool close() {
    if (m_state == StreamState::Closed) return false;
    m_state = StreamState::Closed;
    return true;
  }

private:
  StreamState::T m_state = StreamState::Idle;
};

class FlowWindow {
public:
  FlowWindow(int64_t value = DefltWindow) : m_value{value} { }

  int64_t value() const { return m_value; }
  bool consume(uint32_t value) {
    if (m_value < value) return false;
    m_value -= value;
    return true;
  }
  bool update(uint32_t value) {
    if (!value || m_value > int64_t(MaxWindow) - value) return false;
    m_value += value;
    return true;
  }
  bool adjust(int64_t delta) {
    int64_t value = m_value + delta;
    if (value > MaxWindow || value < -int64_t(MaxWindow)) return false;
    m_value = value;
    return true;
  }

private:
  int64_t m_value;
};

class QueueAdmission {
public:
  void init(uint32_t max) { m_max = max; m_count = 0; }
  bool push() {
    if (++m_count <= m_max) return true;
    --m_count;
    return false;
  }
  void pop(uint32_t n = 1) {
    ZmAssert(m_count >= n);
    m_count -= n;
  }
  uint32_t count() const { return m_count; }

private:
  ZmAtomic<uint32_t>	m_count = 0;
  uint32_t		m_max = 0;
};

struct Stream {
  Stream() = default;
  Stream(uint32_t id_, uint32_t rxWindow, uint32_t txWindow) :
    id{id_}, rx{rxWindow}, tx{txWindow} {
    state.open();
  }

  uint32_t		id = 0;
  StreamStateMachine	state;
  FlowWindow		rx;
  FlowWindow		tx;
  bool			local = false;
  bool			completed = false;
};

inline uint32_t Stream_IDAxor(const Stream &stream)
{
  return stream.id;
}

ZuDerive(StreamHash,
  (ZmHash<Stream,
    ZmHashNode<Stream,
      ZmHashKey<Stream_IDAxor,
	ZmHashLock<ZmNoLock,
	  ZmHashHeapID<"Zhttp.H2">>>>>));

class StreamRegistry {
public:
  StreamRegistry() : m_hash{new StreamHash} { }

  Stream *add(uint32_t id, uint32_t rxWindow, uint32_t txWindow) {
    if (m_hash->findPtr(id)) return nullptr;
    return m_hash->add(Stream{id, rxWindow, txWindow});
  }
  Stream *find(uint32_t id) const { return m_hash->findPtr(id); }
  bool remove(uint32_t id) {
    auto stream = m_hash->findPtr(id);
    if (!stream) return false;
    m_hash->delNode(static_cast<StreamHash::Node *>(stream));
    return true;
  }
  unsigned count() const { return m_hash->count_(); }
  bool adjustTx(int64_t delta) {
    {
      auto i = m_hash->iter();
      while (auto stream = i()) {
	int64_t value = stream->tx.value() + delta;
	if (value > MaxWindow || value < -int64_t(MaxWindow))
	  return false;
      }
    }
    auto i = m_hash->iter();
    while (auto stream = i()) stream->tx.adjust(delta);
    return true;
  }
  void final() { m_hash->clean(); }

private:
  ZmRef<StreamHash> m_hash;
};

struct ScheduleEntry {
  ScheduleEntry() = default;
  ScheduleEntry(uint32_t id_) : id{id_} { }

  uint32_t	id = 0;
  ScheduleEntry	*schedPrev = nullptr;
  ScheduleEntry	*schedNext = nullptr;
};

inline uint32_t ScheduleEntry_IDAxor(const ScheduleEntry &entry)
{
  return entry.id;
}

ZuDerive(ScheduleHash,
  (ZmHash<ScheduleEntry,
    ZmHashNode<ScheduleEntry,
      ZmHashKey<ScheduleEntry_IDAxor,
	ZmHashLock<ZmNoLock,
	  ZmHashHeapID<"Zhttp.H2">>>>>));

class Schedule {
public:
  Schedule() : m_hash{new ScheduleHash} { }

  bool add(uint32_t id) {
    if (m_hash->findPtr(id)) return true;
    link_(m_hash->add(id));
    return true;
  }
  bool remove(uint32_t id) {
    auto node = m_hash->findPtr(id);
    if (!node) return false;
    unlink_(node);
    m_hash->delNode(node);
    return true;
  }
  uint32_t next() {
    if (!m_next) return uint32_t(-1);
    auto node = m_next;
    m_next = node_(node->schedNext);
    return node->id;
  }
  bool empty() const { return !m_next; }
  unsigned count() const { return m_hash->count_(); }
  void final() {
    m_next = nullptr;
    m_hash->clean();
  }

private:
  using Node = ScheduleHash::Node;

  static Node *node_(ScheduleEntry *entry) {
    return static_cast<Node *>(entry);
  }
  void link_(Node *node) {
    if (!m_next) {
      node->schedPrev = node->schedNext = node;
      m_next = node;
      return;
    }
    auto tail = node_(m_next->schedPrev);
    node->schedPrev = tail;
    node->schedNext = m_next;
    tail->schedNext = node;
    m_next->schedPrev = node;
  }
  void unlink_(Node *node) {
    if (node->schedNext == node) {
      m_next = nullptr;
      return;
    }
    if (m_next == node) m_next = node_(node->schedNext);
    node->schedPrev->schedNext = node->schedNext;
    node->schedNext->schedPrev = node->schedPrev;
    node->schedPrev = node->schedNext = nullptr;
  }

  ZmRef<ScheduleHash> m_hash;
  Node		      *m_next = nullptr;
};

namespace Work {
  enum : uint8_t {
    None,
    ConnectionControl,
    StreamControl,
    Headers,
    Data
  };
}

struct WorkItem {
  uint32_t	id = 0;
  uint8_t	type = Work::None;
};

class Scheduler {
public:
  bool connectionControl(uint32_t id = 0) {
    return m_connectionControl.add(id);
  }
  bool streamControl(uint32_t id) { return m_streamControl.add(id); }
  bool headers(uint32_t id) { return m_headers.add(id); }
  bool data(uint32_t id) { return m_data.add(id); }

  void remove(uint32_t id) {
    m_connectionControl.remove(id);
    m_streamControl.remove(id);
    m_headers.remove(id);
    m_data.remove(id);
  }
  WorkItem next() {
    if (!m_connectionControl.empty())
      return {m_connectionControl.next(), Work::ConnectionControl};
    if (!m_streamControl.empty())
      return {m_streamControl.next(), Work::StreamControl};
    if (!m_headers.empty())
      return {m_headers.next(), Work::Headers};
    if (!m_data.empty())
      return {m_data.next(), Work::Data};
    return {};
  }
  bool empty() const {
    return m_connectionControl.empty() && m_streamControl.empty() &&
      m_headers.empty() && m_data.empty();
  }
  template <typename L>
  bool run(unsigned batch, L &&l) {
    while (batch--) {
      auto item = next();
      if (!item.type) return false;
      l(item);
    }
    return !empty();
  }
  void final() {
    m_connectionControl.final();
    m_streamControl.final();
    m_headers.final();
    m_data.final();
  }

private:
  Schedule m_connectionControl;
  Schedule m_streamControl;
  Schedule m_headers;
  Schedule m_data;
};

using ClosedStreams =
  ZtArray<uint32_t, ZtArrayHeapID<"Zhttp.H2">>;

class Session {
public:
  bool init(
    bool server, uint32_t localMax, uint32_t peerMax,
    uint32_t pendingMax, unsigned recentMax = 64) {
    final();
    m_server = server;
    m_nextLocal = server ? 2 : 1;
    m_localMax = localMax;
    m_peerMax = peerMax;
    m_pendingMax = pendingMax;
    m_recentMax = recentMax;
    m_localIDMax = MaxWindow;
    return true;
  }

  Stream *openLocal() {
    if (!canOpenLocal()) return nullptr;
    uint32_t id = m_nextLocal;
    m_nextLocal += 2;
    auto stream = m_streams.add(id, m_localRxWindow, m_peerInitialWindow);
    if (!stream) return nullptr;
    stream->local = true;
    ++m_localCount;
    return stream;
  }
  bool canOpenLocal() const {
    return !m_server && m_localCount < m_localMax &&
      m_nextLocal <= m_localIDMax;
  }
  bool localExhausted() const { return m_nextLocal > m_localIDMax; }

  Stream *openPeer(uint32_t id) {
    bool odd = id & 1U;
    if (!m_server || !id || odd != m_server || id <= m_lastPeer ||
	m_peerCount >= m_peerMax)
      return nullptr;
    auto stream = m_streams.add(id, m_localRxWindow, m_peerInitialWindow);
    if (!stream) return nullptr;
    m_lastPeer = id;
    ++m_peerCount;
    return stream;
  }

  bool queueLocal() {
    if (m_pending == m_pendingMax) return false;
    ++m_pending;
    return true;
  }
  Stream *admitQueued() {
    if (!m_pending) return nullptr;
    auto stream = openLocal();
    if (stream) --m_pending;
    return stream;
  }

  bool close(uint32_t id) {
    auto stream = m_streams.find(id);
    if (!stream) return false;
    if (!stream->completed) {
      stream->completed = true;
      if (stream->local)
	--m_localCount;
      else
	--m_peerCount;
    }
    m_streams.remove(id);
    recent_(id);
    return true;
  }

  bool recentlyClosed(uint32_t id) const {
    for (unsigned i = 0; i < m_recent.length(); ++i)
      if (m_recent[i] == id) return true;
    return false;
  }
  bool closed(uint32_t id) const {
    if (!id || m_streams.find(id)) return false;
    if (recentlyClosed(id)) return true;
    if (!(id & 1U)) return false;
    return m_server ? id <= m_lastPeer : id < m_nextLocal;
  }
  bool peerIdle(uint32_t id) const {
    return m_server && id && (id & 1U) && id > m_lastPeer;
  }
  void refusePeer(uint32_t id) {
    if (!peerIdle(id)) return;
    m_lastPeer = id;
    recent_(id);
  }

  bool peerInitialWindow(uint32_t value) {
    if (value > MaxWindow) return false;
    int64_t delta = int64_t(value) - m_peerInitialWindow;
    if (!m_streams.adjustTx(delta)) return false;
    m_peerInitialWindow = value;
    return true;
  }

  Stream *find(uint32_t id) const { return m_streams.find(id); }
  unsigned count() const { return m_streams.count(); }
  uint32_t localCount() const { return m_localCount; }
  uint32_t peerCount() const { return m_peerCount; }
  uint32_t pending() const { return m_pending; }
  unsigned recentCount() const { return m_recent.length(); }
  void localMax(uint32_t value) { m_localMax = value; }
  void localIDMax(uint32_t value) { m_localIDMax = value; }

  void final() {
    m_streams.final();
    m_recent.length(0);
    m_nextLocal = 1;
    m_lastPeer = 0;
    m_localCount = 0;
    m_peerCount = 0;
    m_pending = 0;
  }

private:
  void recent_(uint32_t id) {
    if (!m_recentMax) return;
    if (m_recent.length() == m_recentMax) m_recent.shift(1);
    m_recent.push(id);
  }

  StreamRegistry	m_streams;
  ClosedStreams		m_recent;
  uint32_t		m_nextLocal = 1;
  uint32_t		m_lastPeer = 0;
  uint32_t		m_localMax = 0;
  uint32_t		m_peerMax = 0;
  uint32_t		m_pendingMax = 0;
  uint32_t		m_localIDMax = MaxWindow;
  uint32_t		m_localCount = 0;
  uint32_t		m_peerCount = 0;
  uint32_t		m_pending = 0;
  uint32_t		m_localRxWindow = DefltWindow;
  uint32_t		m_peerInitialWindow = DefltWindow;
  unsigned		m_recentMax = 0;
  bool			m_server = false;
};

} // namespace H2

} // namespace Zhttp

#endif /* ZhttpH2Session_HH */
