//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP/2 native TLS Links and application logical-link adapters

#ifndef ZhttpH2Hub_HH
#define ZhttpH2Hub_HH

#ifndef Zhttp_HH
#define Zhttp_CORE_ONLY
#include <zlib/Zhttp.hh>
#undef Zhttp_CORE_ONLY
#endif

#include <string.h>

#include <zlib/ZmBlock.hh>
#include <zlib/ZmContext.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmScheduler.hh>

#include <zlib/ZhttpClientHub.hh>
#include <zlib/ZhttpServer.hh>

namespace Zhttp {

namespace H2_ {

using namespace H2;

enum {
  // Bounds one scheduler turn while amortizing cross-shard post overhead.
  FrameDrainBatch = 64,
  RequeueBatch = 64,
  // Bounds closed-Stream history retained for late peer frames.
  RecentStreamMax = 64
};

struct EventType {
  enum T : uint8_t {
    Begin, Field, EndHeaders, DataLength, Data, Reset
  };
};

struct Event {
  EventType::T	type = EventType::Begin;
  ZuCSpan	name;
  ZuCSpan	value;
  Ztls::RxStream *wire = nullptr;
  uint64_t	consumed = 0;
  uint32_t	frameLength = 0;
  unsigned	headLen = 0;
  unsigned	tailLen = 0;
  Error::T	error = Error::NoError;
  bool		trailers = false;
  bool		endStream = false;
  bool		transferred = false;
};

struct EventRx {
  template <typename Parser>
  auto process(Parser &parser) {
    uint64_t before = parser.consumed();
    switch (event.type) {
      case EventType::Begin:
	parser.beginHeaders(event.trailers);
	break;
      case EventType::Field:
	parser.field(event.name, event.value);
	break;
      case EventType::EndHeaders:
	parser.endHeaders(event.endStream);
	break;
      case EventType::DataLength:
	parser.dataLength(event.frameLength, event.endStream);
	break;
      case EventType::Data:
	parser.data(
	  *event.wire, event.frameLength, event.headLen, event.tailLen,
	  event.endStream, event.transferred);
	break;
      case EventType::Reset:
	parser.cancel();
	break;
    }
    event.consumed = parser.consumed() - before;
    return parser.state();
  }

  Event &event;
};

struct RxDataFrame {
  uint32_t	id = 0;
  uint32_t	length = 0;
  unsigned	prefix = 0;
  unsigned	pad = 0;
  bool		endStream = false;
  bool		ready = false;
};

struct CountBytes {
  void push(uint8_t) { ++m_length; }
  void skip(uint64_t n) { m_length += unsigned(n); }
  unsigned length() const { return m_length; }
  unsigned m_length = 0;
};

template <typename Tx>
struct StreamBytes {
  void push(uint8_t value) { tx << char(value); ++m_length; }
  unsigned length() const { return m_length; }

  Tx		&tx;
  unsigned	m_length = 0;
};

using HeaderFrames =
  ZtArray<ZmRef<ZiIOBuf>, ZtArrayHeapID<"Zhttp.H2.Headers">>;

template <typename Lower>
class DataStream : public ZiTxLayer<DataStream<Lower>, Lower> {
public:
  using Base = ZiTxLayer<DataStream<Lower>, Lower>;

  DataStream(
    Lower &lower, uint32_t streamID, uint64_t remaining = uint64_t(-1)) :
    Base{lower, 9, 0}, m_remaining{remaining}, m_streamID{streamID} { }

  void prepareBuf_(ZiIOBuf *buf, bool) {
    if (m_remaining != uint64_t(-1)) {
      if (ZuUnlikely(m_remaining < buf->length)) {
	m_valid = false;
	buf->length = unsigned(m_remaining);
      }
      m_remaining -= buf->length;
    }
    m_produced += buf->length;
    uint32_t length = buf->length;
    ZiAssert(buf->skip >= 9, "Zhttp", (),
      "H2 DATA headroom error", return);
    buf->rewind(9);
    auto out = buf->data();
    out[0] = uint8_t(length>>16);
    out[1] = uint8_t(length>>8);
    out[2] = uint8_t(length);
    out[3] = FrameType::Data;
    out[4] = 0;
    out[5] = uint8_t(m_streamID>>24);
    out[6] = uint8_t(m_streamID>>16);
    out[7] = uint8_t(m_streamID>>8);
    out[8] = uint8_t(m_streamID);
  }
  uint64_t produced() const { return m_produced; }
  bool valid() const { return m_valid; }
  bool complete() const {
    return m_valid &&
      (m_remaining == uint64_t(-1) || !m_remaining);
  }

private:
  uint64_t m_remaining;
  uint64_t m_produced = 0;
  uint32_t m_streamID;
  bool m_valid = true;
};

template <typename Native>
class FrameStream : public ZiTxStream<FrameStream<Native>> {
  using Base = ZiTxStream<FrameStream<Native>>;

public:
  FrameStream(
    Native &native, uint32_t streamID, HeaderFrames *frames = nullptr) :
    Base(
      native.txStream().maxSize(),
      native.txStream().headRoom(),
      native.txStream().tailRoom()),
    m_native{&native}, m_frames{frames}, m_streamID{streamID}
  {
  }

  ZmRef<ZiIOBuf> allocBuf_(unsigned headRoom) {
    auto tx = m_native->txStream();
    return tx.allocBuf_(headRoom);
  }
  bool sendBuf_(ZmRef<ZiIOBuf> buf, bool) {
    if (m_frames) {
      m_frames->push(ZuMv(buf));
      return true;
    }
    return m_native->sendFrame(m_streamID, ZuMv(buf));
  }

private:
  Native	*m_native;
  HeaderFrames	*m_frames = nullptr;
  uint32_t	m_streamID = 0;
};

struct HPackStagedPlan {
  HPackStagedPlan() = default;
  HPackStagedPlan(const HPackPlan &plan) :
    name{plan.field.name}, value{plan.field.value},
    index{plan.index}, rep{plan.rep} { }

  HPackPlan plan() const {
    return {.field = {name, value}, .index = index, .rep = rep};
  }

  HPackString	name;
  HPackString	value;
  uint64_t	index = 0;
  HPackRep::T	rep = HPackRep::NonIndexed;
};

using HPackStagedPlans =
  ZtArray<HPackStagedPlan, ZtArrayHeapID<"Zhttp.H2.HPackPlans">>;

template <typename Native>
class HeaderBlock {
  using FrameTx = FrameStream<Native>;
  using DataFrames =
    ZtArray<ZmRef<ZiIOBuf>, ZtArrayHeapID<"Zhttp.H2.Data">>;


  class Bytes {
  public:
    Bytes(
      Native &native, uint32_t streamID, uint32_t length,
      uint32_t frameSize, bool first, bool endStream,
      HeaderFrames &frames) :
	m_native{native}, m_streamID{streamID}, m_left{length},
	m_frameSize{frameSize}, m_frames{frames},
	m_first{first}, m_endStream{endStream}
    {
      next_();
    }

    void push(uint8_t value) {
      m_tx << char(value);
      ++m_length;
      if (!--m_frameLeft && m_left) next_();
    }
    unsigned length() const { return m_length; }
    void flush() { m_tx.flush(); }

  private:
    void next_() {
      if (m_length) m_tx.flush();
      uint32_t length = m_left > m_frameSize ? m_frameSize : m_left;
      m_left -= length;
      m_frameLeft = length;
      m_tx = FrameTx{m_native, m_streamID, &m_frames};
      StreamBytes<FrameTx> sink{m_tx};
      putHeader(sink, {
	.length = length,
	.streamID = m_streamID,
	.type = uint8_t(m_first ? FrameType::Headers :
	  FrameType::Continuation),
	.flags = uint8_t(
	  m_first && m_endStream ? Flag::EndStream : 0)
      });
      m_first = false;
    }

    Native	&m_native;
    uint32_t	m_streamID = 0;
    FrameTx	m_tx{m_native, m_streamID};
    uint32_t	m_left = 0;
    uint32_t	m_frameLeft = 0;
    uint32_t	m_frameSize = DefltFrameSize;
    HeaderFrames &m_frames;
    unsigned	m_length = 0;
    bool	m_first = false;
    bool	m_endStream = false;
  };

public:
  HeaderBlock(
    Native &native, HPackEncoder &encoder, uint32_t streamID,
    uint32_t frameSize) :
      m_native{native}, m_encoder{encoder}, m_streamID{streamID},
      m_frameSize{frameSize} { }

  void beginHeaders(bool endStream = false) {
    if (m_deferred && m_initialHeaders) {
      m_trailers.length(0);
      m_current = &m_trailers;
      m_updates = {};
    } else {
      m_frames.length(0);
      m_current = &m_frames;
      if (!m_deferred) {
	m_plans.length(0);
	m_stagedUpdates = {};
	m_commitUpdates = false;
      }
      m_updates = m_encoder.updates();
    }
    m_first = true;
    m_open = true;
    m_endStream = endStream;
  }
  void field(ZuCSpan name, ZuCSpan value) {
    field_({name, value});
  }
  template <typename P>
  ZuIfT<!Compression::IsPrintString<P>{}>
  field(ZuCSpan name, const P &value) {
    fieldPrint_(name, value);
  }
  void field(
    ZuCSpan name, ZuCSpan value1, char separator, ZuCSpan value2) {
    HPackString value;
    unsigned n1 = value1.length(), n2 = value2.length();
    value.length(uint64_t(n1) + 1 + n2);
    auto data = value.data();
    if (n1) memcpy(data, value1.data(), n1);
    data[n1] = separator;
    if (n2) memcpy(data + n1 + 1, value2.data(), n2);
    field_({name, value});
  }
  void endHeaders(bool) {
    if (!m_open) return;
    if (m_first && m_updates.count) {
      CountBytes count;
      if (m_encoder.emit(count, m_updates) < 0) return;
      Bytes bytes{
	m_native, m_streamID, count.length(), m_frameSize,
	true, m_endStream, *m_current};
      if (m_encoder.emit(bytes, m_updates) < 0) return;
      bytes.flush();
      if (m_deferred) {
	m_stagedUpdates = m_updates;
	m_commitUpdates = true;
      }
      else
	m_encoder.commit(m_updates);
      m_updates = {};
      m_first = false;
    }
    FrameTx tx{m_native, m_streamID, m_current};
    StreamBytes<FrameTx> sink{tx};
    putHeader(sink, {
      .length = 0,
      .streamID = m_streamID,
      .type = uint8_t(m_first ? FrameType::Headers :
	FrameType::Continuation),
      .flags = uint8_t(Flag::EndHeaders |
	(m_first && m_endStream ? Flag::EndStream : 0))
    });
    tx.flush();
    m_open = false;
    if (!m_deferred)
      m_native.sendHeaders(m_streamID, ZuMv(m_frames), m_endStream);
    else if (!m_initialHeaders) {
      m_initialHeaders = true;
      m_initialEndStream = m_endStream;
    } else
      m_trailerEndStream = m_endStream;
  }
  auto body() { return DataStream<HeaderBlock>{*this, m_streamID}; }
  auto body(uint64_t length) {
    return DataStream<HeaderBlock>{*this, m_streamID, length};
  }
  void end() {
    if (m_deferred)
      m_endData = true;
    else
      m_native.endData(m_streamID);
  }
  bool extendedConnect() const {
    return m_native.peerExtendedConnect();
  }
  bool localExtendedConnect() const {
    return m_native.localExtendedConnect();
  }
  void flush() { }
  unsigned maxSize() { return m_native.dataMaxSize(m_streamID); }
  unsigned headRoom() const {
    auto tx = m_native.txStream();
    return tx.headRoom();
  }
  unsigned tailRoom() const {
    auto tx = m_native.txStream();
    return tx.tailRoom();
  }
  ZmRef<ZiIOBuf> allocBuf_(unsigned headRoom) {
    auto tx = m_native.txStream();
    return tx.allocBuf_(headRoom);
  }
  bool sendBuf_(ZmRef<ZiIOBuf> buf, bool) {
    if (m_deferred) {
      m_data.push(ZuMv(buf));
      return true;
    }
    return m_native.sendData(m_streamID, ZuMv(buf));
  }
  void defer(uint64_t max = uint64_t(-1)) {
    m_frames.length(0);
    m_trailers.length(0);
    m_data.length(0);
    m_plans.length(0);
    m_stagedUpdates = {};
    m_commitUpdates = false;
    m_initialHeaders = false;
    m_endData = false;
    m_initialEndStream = false;
    m_trailerEndStream = false;
    m_retainedMax = max;
    m_deferred = true;
  }
  bool valid() const {
    uint64_t n = 0;
    for (unsigned i = 0; i < m_frames.length(); ++i) {
      if (m_frames[i]->length > m_retainedMax - n) return false;
      n += m_frames[i]->length;
    }
    for (unsigned i = 0; i < m_data.length(); ++i) {
      if (m_data[i]->length > m_retainedMax - n) return false;
      n += m_data[i]->length;
    }
    for (unsigned i = 0; i < m_trailers.length(); ++i) {
      if (m_trailers[i]->length > m_retainedMax - n) return false;
      n += m_trailers[i]->length;
    }
    return true;
  }
  void commit() {
    if (!m_deferred || !valid()) return;
    if (m_commitUpdates) m_encoder.commit(m_stagedUpdates);
    for (unsigned i = 0; i < m_plans.length(); ++i)
      m_encoder.commit(m_plans[i].plan());
    m_native.sendHeaders(
      m_streamID, ZuMv(m_frames), m_initialEndStream);
    for (unsigned i = 0; i < m_data.length(); ++i)
      m_native.sendData(m_streamID, ZuMv(m_data[i]));
    m_data.length(0);
    if (m_trailers)
      m_native.sendHeaders(
	m_streamID, ZuMv(m_trailers), m_trailerEndStream);
    else if (m_endData)
      m_native.endData(m_streamID);
    m_deferred = false;
  }

private:
  template <typename P>
  void fieldPrint_(ZuCSpan name, const P &value) {
    auto emit = [this, name, &value](auto &out) {
      int nameIndex = HPack::staticNameIndex(name);
      uint8_t prefix = m_encoder.neverIndexed(name) ? 0x10 : 0;
      if (Compression::putPref(
	    out, prefix, 4, nameIndex > 0 ? unsigned(nameIndex) : 0) < 0)
	return false;
      if (nameIndex <= 0 && Compression::putString(out, 0, 7, name) < 0)
	return false;
      return Compression::putPrint(out, 0, 7, value) >= 0;
    };
    CountBytes count;
    if ((m_first && m_encoder.emit(count, m_updates) < 0) || !emit(count))
      return;
    Bytes bytes{
      m_native, m_streamID, count.length(), m_frameSize,
      m_first, m_endStream, *m_current};
    if ((m_first && m_encoder.emit(bytes, m_updates) < 0) || !emit(bytes))
      return;
    bytes.flush();
    if (m_first) {
      if (m_deferred) {
	m_stagedUpdates = m_updates;
	m_commitUpdates = true;
      } else
	m_encoder.commit(m_updates);
      m_updates = {};
    }
    m_first = false;
  }

  void field_(Field field) {
    auto plan = m_encoder.plan(field);
    CountBytes count;
    if ((m_first && m_encoder.emit(count, m_updates) < 0) ||
	m_encoder.emit(count, plan) < 0)
      return;
    Bytes bytes{
      m_native, m_streamID, count.length(), m_frameSize,
      m_first, m_endStream, *m_current};
    if ((m_first && m_encoder.emit(bytes, m_updates) < 0) ||
	m_encoder.emit(bytes, plan) < 0)
      return;
    bytes.flush();
    if (m_first) {
      if (m_deferred) {
	m_stagedUpdates = m_updates;
	m_commitUpdates = true;
      }
      else
	m_encoder.commit(m_updates);
      m_updates = {};
    }
    if (m_deferred)
      new (m_plans.push()) HPackStagedPlan{plan};
    else
      m_encoder.commit(plan);
    m_first = false;
  }

  Native	&m_native;
  HPackEncoder	&m_encoder;
  HPackUpdates	m_updates;
  HPackUpdates	m_stagedUpdates;
  uint32_t	m_streamID = 0;
  uint32_t	m_frameSize = DefltFrameSize;
  uint64_t	m_retainedMax = uint64_t(-1);
  HeaderFrames	m_frames;
  HeaderFrames	m_trailers;
  HeaderFrames	*m_current = &m_frames;
  DataFrames	m_data;
  HPackStagedPlans m_plans;
  bool		m_first = false;
  bool		m_open = false;
  bool		m_endStream = false;
  bool		m_endData = false;
  bool		m_deferred = false;
  bool		m_commitUpdates = false;
  bool		m_initialHeaders = false;
  bool		m_initialEndStream = false;
  bool		m_trailerEndStream = false;
};

using ClosedStreams =
  ZtArray<uint32_t, ZtArrayHeapID<"Zhttp.H2.ClosedStreams">>;
using ClosedStreamSet = ZmHashKV<
  uint32_t, bool,
  ZmHashLock<ZmNoLock,
    ZmHashHeapID<"Zhttp.H2.ClosedStreams">>>;

struct PendingFrame {
  uint32_t	id = 0;
  uint32_t	length = 0;
  uint64_t	seq = 0;
  bool		endStream = false;
  ZmRef<ZiIOBuf>	buf;
};

using PendingFrameQueue =
  ZtArray<PendingFrame, ZtArrayHeapID<"Zhttp.H2">>;

struct TxWindowEntry {
  TxWindowEntry() = default;
  TxWindowEntry(uint32_t id_, int64_t window_) :
    id{id_}, window{window_} { }

  uint32_t	id = 0;
  int64_t	window = DefltWindow;
  uint64_t	headSeq = 0;
  uint64_t	nextSeq = 0;
  bool		localEndQueued = false;
};

inline uint32_t TxWindowEntry_IDAxor(const TxWindowEntry &entry)
{
  return entry.id;
}

using TxWindowHash = ZmHash<TxWindowEntry,
  ZmHashNode<TxWindowEntry,
    ZmHashKey<TxWindowEntry_IDAxor,
      ZmHashLock<ZmNoLock,
	ZmHashHeapID<"Zhttp.H2">>>>>;

template <typename Impl, typename Logical>
class Wire : public H2::Connection<Wire<Impl, Logical>> {
  using Base = H2::Connection<Wire>;
  using Streams = StreamHash<Logical>;

public:
  bool initWire(bool server, const H2Config &config) {
    m_server = server;
    m_nextLocalStream = server ? 2 : 1;
    m_localStreamMax = config.maxConcurrentStreams();
    m_peerStreamMax = config.maxConcurrentStreams();
    m_localStreamIDMax = server ? MaxWindow : config.maxStreamID();
    m_rxPreface = server;
    m_config = config;
    m_frameAdmission.init(config.maxQueuedFrames());
    if (!Base::init(server, config.maxFrameSize()) ||
	!m_decoder.init(
	  config.hpackRxCapacity(), config.maxHeaderListSize()) ||
	!m_encoder.init(config.hpackTxCapacity()))
      return false;
    m_streams = new Streams;
    m_recentStreamSet = new ClosedStreamSet;
    m_txWindows = new TxWindowHash;
    return true;
  }
  void finalWire() {
    ZmAssert(!streamCount());
    ZmAssert(!m_pendingFrames);
    ZmAssert(!m_txWindows || !m_txWindows->count_());
    ZmAssert(!m_frameAdmission.count());
    ZmAssert(!m_frameDrainPosted);
    stopWire();
    m_decoder.final();
    m_encoder.final();
    m_streams = nullptr;
    m_recentStreams.length(0);
    m_recentStreamSet = nullptr;
    m_recentStreamHead = 0;
    m_nextLocalStream = m_server ? 2 : 1;
    m_lastPeerStream = 0;
    m_lastProcessedPeerStream = 0;
    m_localStreamCount = 0;
    m_peerStreamCount = 0;
    m_peerInitialWindow = DefltWindow;
    m_txWindows = nullptr;
  }
  void stopWire() {
    if (m_stopping) return;
    m_stopping = true;
    impl_()->app()->mx()->del(&m_settingsTimer);
  }

  Impl *impl_() { return static_cast<Impl *>(this); }
  HPackEncoder &encoder() { return m_encoder; }
  uint32_t peerFrameSize() {
    if (impl_()->app()->txInvoked()) return m_txFrameSize;
    return Base::peerSettings().maxFrameSize;
  }
  bool peerExtendedConnect() {
    if (impl_()->app()->txInvoked()) return m_txExtendedConnect;
    return Base::peerSettings().enableConnectProtocol;
  }
  bool localExtendedConnect() const { return m_config.extendedConnect(); }

  Stream<Logical> *h2Stream(uint32_t id) const {
    return m_streams ? m_streams->findPtr(id) : nullptr;
  }
  Stream<Logical> *openLocalStream(ZmRef<Logical> logical) {
    if (!canOpenLocalStream()) return nullptr;
    uint32_t id = m_nextLocalStream;
    auto stream_ = addStream_(id, ZuMv(logical), true);
    if (!stream_) return nullptr;
    m_nextLocalStream += 2;
    ++m_localStreamCount;
    return stream_;
  }
  Stream<Logical> *openPeerStream(
    uint32_t id, ZmRef<Logical> logical) {
    if (!canOpenPeerStream(id)) return nullptr;
    auto stream_ = addStream_(id, ZuMv(logical), false);
    if (!stream_) return nullptr;
    m_lastPeerStream = id;
    ++m_peerStreamCount;
    return stream_;
  }
  bool canOpenPeerStream(uint32_t id) const {
    bool odd = id & 1U;
    return m_server && id && odd == m_server && id > m_lastPeerStream &&
      m_peerStreamCount < m_peerStreamMax;
  }
  bool canOpenLocalStream() const {
    return !m_server && m_localStreamCount < m_localStreamMax &&
      m_nextLocalStream <= m_localStreamIDMax;
  }
  bool localStreamsExhausted() const {
    return m_nextLocalStream > m_localStreamIDMax;
  }
  void localStreamMax(uint32_t value) { m_localStreamMax = value; }
  template <typename Done>
  void removeStream(uint32_t id, Done &&done) {
    if (!m_streams) {
      ZuFwd<Done>(done)();
      return;
    }
    auto stream_ = m_streams->findPtr(id);
    if (stream_) {
      uint64_t deferred = stream_->deferred;
      if (stream_->local)
	--m_localStreamCount;
      else
	--m_peerStreamCount;
      m_streams->delNode(static_cast<typename Streams::Node *>(stream_));
      recentStream_(id);
      retireConnection_(deferred);
    }
    auto link = impl_();
    impl_()->app()->txRun([
	link, id, done = ZuFwd<Done>(done)]() mutable {
      link->removeFramesTx_(id);
      link->app()->rxRun(
	[done = ZuMv(done)]() mutable { done(); });
    });
  }
  template <typename Done>
  void clearStreams(Done &&done) {
    uint64_t deferred = 0;
    if (m_streams) {
      auto i = m_streams->iter();
      while (auto stream_ = i()) {
	deferred += stream_->deferred;
	recentStream_(stream_->id);
      }
      m_streams->clean();
    }
    m_localStreamCount = 0;
    m_peerStreamCount = 0;
    settleConnection_(deferred);
    auto link = impl_();
    impl_()->app()->txRun([
	link, done = ZuFwd<Done>(done)]() mutable {
      link->clearFramesTx_();
      link->app()->rxRun(
	[done = ZuMv(done)]() mutable { done(); });
    });
  }
  template <typename L>
  void allStreams(L &&l) {
    if (!m_streams) return;
    auto i = m_streams->iter();
    while (auto stream_ = i()) l(*stream_);
  }
  unsigned streamCount() const {
    return m_streams ? m_streams->count_() : 0;
  }
  bool streamClosed(uint32_t id) const {
    if (!id || h2Stream(id)) return false;
    if (recentStreamClosed(id)) return true;
    if (!(id & 1U)) return false;
    return m_server ? id <= m_lastPeerStream : id < m_nextLocalStream;
  }
  bool peerStreamIdle(uint32_t id) const {
    return m_server && id && (id & 1U) && id > m_lastPeerStream;
  }
  void refusePeerStream(uint32_t id) {
    if (!peerStreamIdle(id)) return;
    m_lastPeerStream = id;
    recentStream_(id);
  }
  bool recentStreamClosed(uint32_t id) const {
    return m_recentStreamSet && m_recentStreamSet->find(id);
  }
  void logicalTxErrorFn(uint32_t id, ZiTxErrorFn fn) {
    if (auto stream_ = h2Stream(id)) stream_->txErrorFn = ZuMv(fn);
  }

  unsigned dataMaxSize(uint32_t id) {
    if (impl_()->app()->txInvoked()) return dataMaxSizeTx_(id);
    auto tx = impl_()->txStream();
    uint32_t length = peerFrameSize();
    if (auto entry_ = h2Stream(id)) {
      uint32_t connection =
	m_txWindowHint > 0 ? uint32_t(m_txWindowHint) : 1;
      uint32_t stream = entry_->txWindowHint > 0 ?
	uint32_t(entry_->txWindowHint) : 1;
      if (length > connection) length = connection;
      if (length > stream) length = stream;
    }
    unsigned overhead = tx.headRoom() + tx.tailRoom() + 9;
    unsigned maxSize = overhead + length;
    return maxSize < tx.maxSize() ? maxSize : tx.maxSize();
  }
  bool sendData(uint32_t id, ZmRef<ZiIOBuf> buf) {
    if (impl_()->app()->txInvoked()) {
      return sendDataTx_(id, ZuMv(buf));
    }
    if (m_stopping || !buf || buf->length < 9) return false;
    auto entry_ = h2Stream(id);
    if (!entry_ || entry_->localEndQueued) return false;
    uint32_t length = buf->length - 9;
    if (length > peerFrameSize()) {
      return streamTxError_(id, "H2 DATA frame exceeds peer maximum");
    }
    return sendFrame_(id, length, false, ZuMv(buf));
  }
  bool sendFrame(uint32_t id, ZmRef<ZiIOBuf> buf) {
    if (impl_()->app()->txInvoked()) {
      return sendFrameDirectTx_(id, 0, false, ZuMv(buf));
    }
    if (m_stopping || !buf || !buf->length) return false;
    return sendFrame_(id, 0, false, ZuMv(buf));
  }
  void sendHeaders(
    uint32_t id, HeaderFrames frames, bool endStream) {
    if (impl_()->app()->txInvoked()) {
      sendHeadersDirectTx_(id, ZuMv(frames), endStream);
      return;
    }
    if (m_stopping || !frames) return;
    auto entry_ = h2Stream(id);
    if (!entry_ || entry_->localEndQueued) return;
    unsigned admitted = 0;
    while (admitted < frames.length() && m_frameAdmission.push())
      ++admitted;
    if (admitted != frames.length()) {
      if (admitted) m_frameAdmission.pop(admitted);
      streamTxError_(id, "H2 transmit queue limit exceeded");
      return;
    }
    if (endStream) entry_->localEndQueued = true;
    auto link = impl_();
    impl_()->app()->txRun([
      link, id, endStream, frames = ZuMv(frames)
    ]() mutable {
      link->sendHeadersTx_(id, ZuMv(frames), endStream);
    });
  }
  void endHeaders(uint32_t id) {
    if (impl_()->app()->txInvoked()) {
      endTx_(id);
      return;
    }
    if (m_stopping) return;
    auto entry_ = h2Stream(id);
    if (!entry_ || entry_->localEndQueued) return;
    entry_->localEndQueued = true;
    sendFrame_(id, 0, true, {});
  }
  void endData(uint32_t id) {
    if (impl_()->app()->txInvoked()) {
      endTx_(id);
      return;
    }
    if (m_stopping) return;
    auto entry_ = h2Stream(id);
    if (!entry_ || entry_->localEndQueued) return;
    entry_->localEndQueued = true;
    auto tx = impl_()->txStream();
    auto buf = tx.allocBuf_(tx.headRoom());
    if (!buf) {
      impl_()->h2Cancel(id);
      return;
    }
    buf->length = 9;
    auto header = buf->data();
    header[0] = header[1] = header[2] = 0;
    header[3] = FrameType::Data;
    header[4] = Flag::EndStream;
    header[5] = uint8_t(id>>24);
    header[6] = uint8_t(id>>16);
    header[7] = uint8_t(id>>8);
    header[8] = uint8_t(id);
    sendFrame_(id, 0, true, ZuMv(buf));
  }

  int process(Ztls::RxStream &rx) {
    if (m_rxPreface) {
      unsigned length = PrefaceParser::value().length();
      if (rx.length() < length) return 0;
      int64_t n = rx.each(length,
	[this](ZuBSpan span) -> int64_t { return Base::process(span); });
      if (ZuUnlikely(n < 0)) return -1;
      if (ZuUnlikely(uint64_t(n) != length)) return 0;
      rx.advance(length);
      m_rxPreface = false;
      return int(length);
    }
    if (rx.length() < FrameHeaderSize) return 0;
    ZuBArray<FrameHeaderSize> bytes;
    if (ZuUnlikely(rx.copy(0, bytes.span()) != FrameHeaderSize)) return 0;
    FrameHeader header;
    if (ZuUnlikely(!decodeHeader(
	ZuCSpan{bytes.data(), FrameHeaderSize}, header))) return 0;
    if (auto error = validateFrame(header, m_config.maxFrameSize())) {
      h2Error(error);
      return -1;
    }
    if (header.type == FrameType::Data && !m_rxDataValidated) {
      unsigned prefix = (header.flags & Flag::Padded) ? 1U : 0U;
      if (prefix > header.length) {
	h2Error(Error::FrameSizeError);
	return -1;
      }
      unsigned pad = 0;
      if (prefix) {
	if (rx.length() <= FrameHeaderSize) return 0;
	ZuBArray<1> byte;
	if (ZuUnlikely(rx.copy(FrameHeaderSize, byte.span()) != 1)) return 0;
	pad = byte[0];
	if (uint64_t(prefix) + pad > header.length) {
	  h2Error(Error::ProtocolError);
	  return -1;
	}
      }
      h2DataLength(
	header.streamID, header.length - prefix - pad,
	bool(header.flags & Flag::EndStream));
      m_rxDataValidated = true;
    }
    uint64_t length = uint64_t(FrameHeaderSize) + header.length;
    if (rx.length() < length) return 0;
    m_rxData = {};
    int64_t n = rx.each(length,
      [this](ZuBSpan span) -> int64_t { return Base::process(span); });
    if (ZuUnlikely(n < 0)) return -1;
    if (ZuUnlikely(uint64_t(n) != length)) return 0;
    if (header.type == FrameType::Data) {
      m_rxDataValidated = false;
      return data_(rx);
    }
    rx.advance(length);
    m_rxDataValidated = false;
    return int(length);
  }

  void sendInitial() {
    auto tx = impl_()->txStream();
    StreamBytes<decltype(tx)> sink{tx};
    if (!m_server) putPreface(sink);
    unsigned count = (m_server ? 5 : 6) +
      unsigned(m_config.extendedConnect());
    putSettingsHeader(sink, count);
    putSetting(sink, Setting::HeaderTableSize, m_config.hpackRxCapacity());
    if (!m_server) putSetting(sink, Setting::EnablePush, 0);
    putSetting(
      sink, Setting::MaxConcurrentStreams,
      m_config.maxConcurrentStreams());
    putSetting(sink, Setting::InitialWindowSize, m_config.initialWindowSize());
    putSetting(sink, Setting::MaxFrameSize_, m_config.maxFrameSize());
    putSetting(
      sink, Setting::MaxHeaderListSize, m_config.maxHeaderListSize());
    if (m_config.extendedConnect())
      putSetting(sink, Setting::EnableConnectProtocol, 1);
    tx.flush();
    Base::settingsSent();
    if (m_config.settingsTimeout())
      impl_()->app()->mx()->add(
	&m_settingsTimer, Zm::now(m_config.settingsTimeout()),
	ZmScheduler::Update,
	[this](auto &&arm) {
	  return arm([this]() {
	    if (!m_stopping) Base::settingsTimeout();
	  });
	}, impl_()->app()->rxThread());
  }
  void h2Settings() {
    sendSettingsAck_();
    uint32_t hpackCapacity = Base::peerSettings().headerTableSize;
    uint32_t frameSize = peerFrameSize();
    bool extendedConnect = peerExtendedConnect();
    auto link = impl_();
    impl_()->app()->txRun([
      link, hpackCapacity, frameSize, extendedConnect]() {
      link->peerSettingsTx_(
	hpackCapacity, frameSize, extendedConnect);
    });
    impl_()->h2SettingsReceived();
  }
  void h2SettingsAck() {
    impl_()->app()->mx()->del(&m_settingsTimer);
  }
  void h2Setting(uint16_t key, uint32_t value) {
    impl_()->h2PeerSetting(key, value);
  }
  void h2Headers(uint32_t id, ZuBSpan span) {
    auto entry_ = begin_(id);
    if (!entry_) {
      if (m_discardStream == id &&
	  m_decoder.process(span, [](Field) { }) < 0)
	h2Error(Error::CompressionError);
      return;
    }
    if (m_decoder.process(span, [this, id](Field field) {
	  auto entry_ = h2Stream(id);
	  if (!entry_) return;
	  if (!entry_->begin) {
	    entry_->begin = true;
	    if (!dispatch_(id, Event{
	      .type = EventType::Begin,
	      .trailers = entry_->finalHeaders
	    })) return;
	    entry_ = h2Stream(id);
	    if (!entry_) return;
	  }
	  if (!m_server && field.name == ":status" && field.value &&
	      field.value[0] != '1')
	    entry_->finalHeaders = true;
	  dispatch_(id, Event{
	    .type = EventType::Field,
	    .name = field.name,
	    .value = field.value
	  });
	}) < 0)
      h2Error(Error::CompressionError);
  }
  void h2HeadersEnd(uint32_t id, bool endStream) {
    auto entry_ = begin_(id);
    if (!entry_) {
      if (m_discardStream != id) return;
      if (!m_decoder.finish())
	h2Error(Error::CompressionError);
      m_decoding = false;
      m_discardStream = 0;
      return;
    }
    if (!m_decoder.finish()) {
      h2Error(Error::CompressionError);
      return;
    }
    m_decoding = false;
    if (!entry_->begin) {
      entry_->begin = true;
      if (!dispatch_(id, Event{
	.type = EventType::Begin,
	.trailers = entry_->finalHeaders
      })) return;
      entry_ = h2Stream(id);
      if (!entry_) return;
    }
    if (!dispatch_(id, Event{
      .type = EventType::EndHeaders,
      .endStream = endStream
    })) return;
    entry_ = h2Stream(id);
    if (!entry_) return;
    entry_->begin = false;
    if (m_server) entry_->finalHeaders = true;
    if (endStream) {
      entry_->remoteEnd = true;
      impl_()->h2RemoteEnd(id);
    }
  }
  bool h2DataBegin(uint32_t id, uint32_t length) {
    auto entry_ = h2Stream(id);
    if (length > m_rxWindow) return false;
    m_rxWindow -= length;
    if (!entry_) {
      if (impl_()->h2Closed(id))
	sendReset_(id, Error::StreamClosed);
      else
	h2Error(Error::ProtocolError);
      return true;
    }
    if (entry_->flowError) return true;
    if (length > entry_->rxWindow) {
      entry_->flowError = true;
      sendReset_(id, Error::FlowControlError);
      return true;
    }
    entry_->rxWindow -= length;
    return true;
  }
  void h2Data(uint32_t, ZuBSpan) { }
  void h2DataLength(uint32_t id, uint32_t length, bool endStream) {
    if (!h2Stream(id)) return;
    (void)dispatch_(id, Event{
      .type = EventType::DataLength,
      .frameLength = length,
      .endStream = endStream
    });
  }
  void h2DataEnd(
    uint32_t id, bool endStream, uint32_t length,
    unsigned prefix, unsigned pad)
  {
    m_rxData = {
      .id = id,
      .length = length,
      .prefix = prefix,
      .pad = pad,
      .endStream = endStream,
      .ready = true
    };
  }
  void h2Reset(uint32_t id, Error::T error) {
    if (h2Stream(id)) {
      dispatch_(id, Event{
	.type = EventType::Reset,
	.error = error
      });
      if (h2Stream(id)) impl_()->h2ResetLogical(id, error);
    } else if (!impl_()->h2Closed(id))
      h2Error(Error::ProtocolError);
  }
  void h2WindowUpdate(uint32_t id, uint32_t value) {
    if (!id) {
      if (m_txWindowHint > int64_t(MaxWindow) - value) {
	h2Error(Error::FlowControlError);
	return;
      }
      m_txWindowHint += value;
    } else {
      auto entry_ = h2Stream(id);
      if (!entry_) {
	if (!impl_()->h2Closed(id))
	  h2Error(Error::ProtocolError);
	return;
      }
      if (entry_->txWindowHint > int64_t(MaxWindow) - value) {
	sendReset_(id, Error::FlowControlError);
	impl_()->h2ResetLogical(id, Error::FlowControlError);
	return;
      }
      entry_->txWindowHint += value;
    }
    auto link = impl_();
    impl_()->app()->txRun([
      link, id, value]() mutable {
      link->windowUpdateTx_(id, value);
    });
  }
  void h2Ping(ZuBSpan value) { sendPingAck_(value); }
  void h2PingAck(ZuBSpan) { }
  void h2Goaway(uint32_t last, Error::T error) {
    impl_()->h2Goaway_(last, error);
  }
  void h2Error(Error::T error) {
    if (m_errorSent) return;
    m_errorSent = true;
    sendGoaway_(error);
    impl_()->disconnect_(false);
  }
  void graceful() {
    if (m_errorSent) return;
    m_errorSent = true;
    sendGoaway_(Error::NoError);
  }

  bool peerInitialWindow(uint32_t value) {
    if (value > MaxWindow) return false;
    int64_t delta = int64_t(value) - m_peerInitialWindow;
    {
      bool valid = true;
      allStreams([delta, &valid](auto &entry_) {
	int64_t window = entry_.txWindowHint + delta;
	if (window > MaxWindow || window < -int64_t(MaxWindow))
	  valid = false;
      });
      if (!valid) return false;
    }
    allStreams([delta](auto &entry_) { entry_.txWindowHint += delta; });
    m_peerInitialWindow = value;
    auto link = impl_();
    impl_()->app()->txRun([
      link, value]() mutable {
      link->initialWindowTx_(value);
    });
    return true;
  }

  void peerSettingsTx_(
    uint32_t hpackCapacity, uint32_t frameSize, bool extendedConnect) {
    ZiAssert(impl_()->app()->txInvoked(), "Zhttp", (),
      "H2 peer settings outside Tx thread", return);
    if (!m_encoder.peerCapacity(hpackCapacity)) {
      auto link = impl_();
      impl_()->app()->rxRun([link]() {
	link->h2Error(Error::CompressionError);
      });
      return;
    }
    m_txFrameSize = frameSize;
    m_txExtendedConnect = extendedConnect;
  }

private:
  Stream<Logical> *addStream_(
    uint32_t id, ZmRef<Logical> logical, bool local) {
    if (!m_streams || m_streams->findPtr(id)) return nullptr;
    auto stream_ = m_streams->add(
      Stream<Logical>{id, ZuMv(logical), local});
    if (stream_) {
      stream_->rxWindow = m_config.initialWindowSize();
      stream_->txWindowHint = m_peerInitialWindow;
    }
    return stream_;
  }
  void recentStream_(uint32_t id) {
    if (!m_recentStreamSet || m_recentStreamSet->find(id)) return;
    if (m_recentStreams.length() < RecentStreamMax) {
      m_recentStreams.push(id);
    } else {
      m_recentStreamSet->del(m_recentStreams[m_recentStreamHead]);
      m_recentStreams[m_recentStreamHead] = id;
      if (++m_recentStreamHead == m_recentStreams.length())
	m_recentStreamHead = 0;
    }
    m_recentStreamSet->add(id, true);
  }
  Stream<Logical> *begin_(uint32_t id) {
    auto entry_ = h2Stream(id);
    if (!entry_ && m_discardStream == id) return nullptr;
    if (!entry_) entry_ = impl_()->h2OpenPeer(id);
    if (!entry_) {
      auto error = impl_()->h2OpenError(id);
      if (error == Error::StreamClosed || error == Error::RefusedStream) {
	sendReset_(id, error);
	m_discardStream = id;
	if (!m_decoding) {
	  m_decoder.reset();
	  m_decoding = true;
	}
      } else
	h2Error(error);
      return nullptr;
    }
    if (!m_decoding) {
      m_decoder.reset();
      m_decoding = true;
    }
    return entry_;
  }
  bool dispatch_(uint32_t id, Event event) {
    auto entry_ = h2Stream(id);
    if (!entry_) return false;
    auto logical = entry_->logical;
    EventRx rx{event};
    if (logical->process_(rx) < 0 && h2Stream(id))
      impl_()->h2Cancel(id);
    retireDeferred_(id, event.consumed);
    if (event.type == EventType::Data && !event.transferred) {
      event.wire->advance(event.frameLength);
      retireDeferred_(
	id, event.frameLength - event.headLen - event.tailLen);
    }
    return h2Stream(id);
  }

  int data_(Ztls::RxStream &rx) {
    if (ZuUnlikely(!m_rxData.ready)) {
      h2Error(Error::InternalError);
      return -1;
    }
    RxDataFrame data = m_rxData;
    m_rxData = {};
    uint32_t frameLength = FrameHeaderSize + data.length;
    auto entry_ = h2Stream(data.id);
    if (!entry_) {
      rx.advance(frameLength);
      retireConnection_(data.length);
      if (!impl_()->h2Closed(data.id)) h2Error(Error::ProtocolError);
      return int(frameLength);
    }
    if (entry_->flowError) {
      rx.advance(frameLength);
      retireConnection_(data.length);
      impl_()->h2ResetLogical(data.id, Error::FlowControlError);
      return int(frameLength);
    }
    unsigned protocol = data.prefix + data.pad;
    if (ZuUnlikely(protocol > data.length)) {
      h2Error(Error::ProtocolError);
      return -1;
    }
    retire_(data.id, protocol);
    uint32_t payload = data.length - protocol;
    entry_->deferred += payload;
    if (!dispatch_(data.id, Event{
      .type = EventType::Data,
      .wire = &rx,
      .frameLength = frameLength,
      .headLen = FrameHeaderSize + data.prefix,
      .tailLen = data.pad,
      .endStream = data.endStream
    })) return int(frameLength);
    entry_ = h2Stream(data.id);
    if (!entry_ || !data.endStream) return int(frameLength);
    entry_->remoteEnd = true;
    impl_()->h2RemoteEnd(data.id);
    return int(frameLength);
  }

  void retireConnection_(uint64_t length) {
    if (!length) return;
    settleConnection_(length);
    sendWindowUpdate_(0, uint32_t(length));
  }
  void settleConnection_(uint64_t length) {
    if (!length) return;
    ZmAssert(length <= uint64_t(MaxWindow));
    ZmAssert(m_rxWindow <= int64_t(MaxWindow) - int64_t(length));
    m_rxWindow += int64_t(length);
  }
  void retire_(uint32_t id, uint64_t length) {
    if (!length) return;
    retireConnection_(length);
    auto entry_ = h2Stream(id);
    if (!entry_ || entry_->flowError) return;
    ZmAssert(entry_->rxWindow <= int64_t(MaxWindow) - int64_t(length));
    entry_->rxWindow += int64_t(length);
    sendWindowUpdate_(id, uint32_t(length));
  }
  void retireDeferred_(uint32_t id, uint64_t length) {
    if (!length) return;
    auto entry_ = h2Stream(id);
    if (!entry_) {
      retireConnection_(length);
      return;
    }
    ZmAssert(length <= entry_->deferred);
    if (ZuUnlikely(length > entry_->deferred)) length = entry_->deferred;
    entry_->deferred -= length;
    retire_(id, length);
  }
  void sendSettingsAck_() {
    auto tx = impl_()->txStream();
    StreamBytes<decltype(tx)> sink{tx};
    putSettingsHeader(sink, 0, true);
    tx.flush();
  }
  void sendPingAck_(ZuBSpan value) {
    auto tx = impl_()->txStream();
    StreamBytes<decltype(tx)> sink{tx};
    putHeader(sink, {
      .length = 8,
      .type = FrameType::Ping,
      .flags = Flag::ACK
    });
    tx << value;
    tx.flush();
  }
  void sendGoaway_(Error::T error) {
    auto tx = impl_()->txStream();
    StreamBytes<decltype(tx)> sink{tx};
    putGoaway(sink, m_lastProcessedPeerStream, error);
    tx.flush();
  }
  void sendReset_(uint32_t id, Error::T error) {
    auto tx = impl_()->txStream();
    StreamBytes<decltype(tx)> sink{tx};
    putHeader(sink, {
      .length = 4,
      .streamID = id,
      .type = FrameType::RSTStream
    });
    putUInt32(sink, error);
    tx.flush();
  }
  void sendWindowUpdate_(uint32_t id, uint32_t value) {
    if (!value) return;
    auto tx = impl_()->txStream();
    StreamBytes<decltype(tx)> sink{tx};
    putHeader(sink, {
      .length = 4,
      .streamID = id,
      .type = FrameType::WindowUpdate
    });
    putUInt32(sink, value);
    tx.flush();
  }
  TxWindowEntry *txWindow_(uint32_t id) {
    auto entry_ = m_txWindows->findPtr(id);
    if (!entry_)
      entry_ = m_txWindows->add(
	TxWindowEntry{id, m_txInitialWindow});
    return entry_;
  }
  unsigned dataMaxSizeTx_(uint32_t id) {
    auto tx = impl_()->directTxStream();
    uint32_t length = m_txFrameSize;
    (void)id;
    unsigned overhead = tx.headRoom() + tx.tailRoom() + 9;
    unsigned maxSize = overhead + length;
    return maxSize < tx.maxSize() ? maxSize : tx.maxSize();
  }
  bool sendDataTx_(uint32_t id, ZmRef<ZiIOBuf> buf) {
    ZiAssert(impl_()->app()->txInvoked(), "Zhttp", (),
      "H2 DATA queue outside Tx thread", return false);
    if (!buf || buf->length < 9) return false;
    uint32_t length = buf->length - 9;
    if (length > m_txFrameSize) {
      txError_(id);
      return false;
    }
    return sendFrameDirectTx_(id, length, false, ZuMv(buf));
  }
  bool sendFrameDirectTx_(
    uint32_t id, uint32_t length, bool endStream,
    ZmRef<ZiIOBuf> buf) {
    ZiAssert(impl_()->app()->txInvoked(), "Zhttp", (),
      "H2 direct frame queue outside Tx thread", return false);
    auto entry_ = txWindow_(id);
    if (!entry_ || entry_->localEndQueued) return false;
    if (!m_frameAdmission.push()) {
      return streamTxError_(id, "H2 transmit queue limit exceeded");
    }
    if (endStream) entry_->localEndQueued = true;
    sendFrameTx_(id, length, endStream, ZuMv(buf));
    return true;
  }
  void sendHeadersDirectTx_(
    uint32_t id, HeaderFrames frames, bool endStream) {
    ZiAssert(impl_()->app()->txInvoked(), "Zhttp", (),
      "H2 direct header queue outside Tx thread", return);
    if (!frames) return;
    auto entry_ = txWindow_(id);
    if (!entry_ || entry_->localEndQueued) return;
    unsigned admitted = 0;
    while (admitted < frames.length() && m_frameAdmission.push())
      ++admitted;
    if (admitted != frames.length()) {
      if (admitted) m_frameAdmission.pop(admitted);
      streamTxError_(id, "H2 transmit queue limit exceeded");
      return;
    }
    if (endStream) entry_->localEndQueued = true;
    sendHeadersTx_(id, ZuMv(frames), endStream);
  }
  void endTx_(uint32_t id) {
    ZiAssert(impl_()->app()->txInvoked(), "Zhttp", (),
      "H2 end queue outside Tx thread", return);
    auto entry_ = txWindow_(id);
    if (!entry_ || entry_->localEndQueued) return;
    auto tx = impl_()->directTxStream();
    auto buf = tx.allocBuf_(tx.headRoom());
    if (!buf) {
      txError_(id);
      return;
    }
    buf->length = 9;
    auto header = buf->data();
    header[0] = header[1] = header[2] = 0;
    header[3] = FrameType::Data;
    header[4] = Flag::EndStream;
    header[5] = uint8_t(id>>24);
    header[6] = uint8_t(id>>16);
    header[7] = uint8_t(id>>8);
    header[8] = uint8_t(id);
    sendFrameDirectTx_(id, 0, true, ZuMv(buf));
  }
  bool sendFrame_(
    uint32_t id, uint32_t length, bool endStream,
    ZmRef<ZiIOBuf> buf) {
    if (!m_frameAdmission.push()) {
      return streamTxError_(id, "H2 transmit queue limit exceeded");
    }
    auto link = impl_();
    impl_()->app()->txRun([
      link, id, length, endStream, buf = ZuMv(buf)
    ]() mutable {
      link->sendFrameTx_(id, length, endStream, ZuMv(buf));
    });
    return true;
  }
  void sendFrameTx_(
    uint32_t id, uint32_t length, bool endStream,
    ZmRef<ZiIOBuf> buf) {
    ZiAssert(impl_()->app()->txInvoked(), "Zhttp", (),
      "H2 frame queue outside Tx thread", return);
    auto entry_ = txWindow_(id);
    if (!entry_) {
      m_frameAdmission.pop();
      return;
    }
    if (endStream) entry_->localEndQueued = true;
    m_pendingFrames.push(
      PendingFrame{
	id, length, entry_->nextSeq++, endStream, ZuMv(buf)});
    startFrameDrain_();
  }
  void sendHeadersTx_(
    uint32_t id, HeaderFrames frames, bool endStream) {
    ZiAssert(impl_()->app()->txInvoked(), "Zhttp", (),
      "H2 header queue outside Tx thread", return);
    auto entry_ = txWindow_(id);
    if (!entry_) {
      m_frameAdmission.pop(frames.length());
      return;
    }
    if (endStream) entry_->localEndQueued = true;
    unsigned n = frames.length();
    for (unsigned i = 0; i < n; ++i)
      m_pendingFrames.push(PendingFrame{
	id, 0, entry_->nextSeq++, endStream && i + 1 == n,
	ZuMv(frames[i])});
    startFrameDrain_();
  }
  void windowUpdateTx_(uint32_t id, uint32_t value) {
    ZiAssert(impl_()->app()->txInvoked(), "Zhttp", (),
      "H2 WINDOW_UPDATE outside Tx thread", return);
    if (!id) {
      if (m_txWindow > int64_t(MaxWindow) - value) {
	txError_(0);
	return;
      }
      m_txWindow += value;
    } else {
      auto entry_ = txWindow_(id);
      if (!entry_ || entry_->window > int64_t(MaxWindow) - value) {
	txError_(id);
	return;
      }
      entry_->window += value;
    }
    startFrameDrain_();
  }
  void initialWindowTx_(uint32_t value) {
    ZiAssert(impl_()->app()->txInvoked(), "Zhttp", (),
      "H2 initial-window update outside Tx thread", return);
    int64_t delta = int64_t(value) - m_txInitialWindow;
    {
      auto i = m_txWindows->iter();
      while (auto entry_ = i()) {
	int64_t window = entry_->window + delta;
	if (window > MaxWindow || window < -int64_t(MaxWindow)) {
	  txError_(0);
	  return;
	}
      }
    }
    auto i = m_txWindows->iter();
    while (auto entry_ = i()) entry_->window += delta;
    m_txInitialWindow = value;
    startFrameDrain_();
  }
  void removeFramesTx_(uint32_t id) {
    ZiAssert(impl_()->app()->txInvoked(), "Zhttp", (),
      "H2 Tx stream removal outside Tx thread", return);
    for (unsigned i = m_pendingFrames.length(); i; )
      if (m_pendingFrames[--i].id == id) {
	m_pendingFrames.splice(i, 1);
	m_frameAdmission.pop();
      }
    if (auto entry_ = m_txWindows->findPtr(id))
      m_txWindows->delNode(
	static_cast<typename TxWindowHash::Node *>(entry_));
    if (m_headerStream == id) m_headerStream = 0;
    if (m_frameCursor >= m_pendingFrames.length()) m_frameCursor = 0;
    if (m_frameScan > m_pendingFrames.length())
      m_frameScan = m_pendingFrames.length();
  }
  void clearFramesTx_() {
    ZiAssert(impl_()->app()->txInvoked(), "Zhttp", (),
      "H2 Tx clear outside Tx thread", return);
    m_frameAdmission.pop(m_pendingFrames.length());
    m_pendingFrames.length(0);
    m_txWindows->clean();
    m_frameCursor = 0;
    m_frameScan = 0;
    m_headerStream = 0;
    m_frameDrainPosted = false;
  }
  void startFrameDrain_() {
    m_frameScan = m_pendingFrames.length();
    if (m_frameDrainPosted) return;
    m_frameDrainPosted = true;
    impl_()->app()->txRun([link = impl_()]() {
      link->drainFramesTx_();
    });
  }
  void drainFramesTx_() {
    ZiAssert(impl_()->app()->txInvoked(), "Zhttp", (),
      "H2 frame drain outside Tx thread", return);
    m_frameDrainPosted = false;
    unsigned inspected = 0;
    while (m_pendingFrames && m_frameScan &&
	inspected < FrameDrainBatch) {
      if (m_frameCursor >= m_pendingFrames.length()) m_frameCursor = 0;
      auto &pending = m_pendingFrames[m_frameCursor];
      auto entry_ = m_txWindows->findPtr(pending.id);
      --m_frameScan;
      ++inspected;
      if (m_headerStream && pending.id != m_headerStream) {
	++m_frameCursor;
	continue;
      }
      if (!entry_) {
	m_pendingFrames.splice(m_frameCursor, 1);
	m_frameAdmission.pop();
	continue;
      }
      if (pending.seq != entry_->headSeq ||
	  (pending.length &&
	    (pending.length > m_txWindow ||
	     pending.length > entry_->window))) {
	++m_frameCursor;
	continue;
      }
      if (pending.length) {
	m_txWindow -= pending.length;
	entry_->window -= pending.length;
      }
      bool endStream = pending.endStream;
      uint32_t id = pending.id;
      ++entry_->headSeq;
      if (pending.buf) {
	auto frame = pending.buf->data();
	if (frame[3] == FrameType::Headers &&
	    !(frame[4] & Flag::EndHeaders))
	  m_headerStream = id;
	else if (m_headerStream == id &&
	    frame[3] == FrameType::Continuation &&
	    (frame[4] & Flag::EndHeaders))
	  m_headerStream = 0;
	auto tx = impl_()->directTxStream();
	tx.sendBuf_(ZuMv(pending.buf), false);
      }
      m_pendingFrames.splice(m_frameCursor, 1);
      m_frameAdmission.pop();
      if (m_frameScan < m_pendingFrames.length()) ++m_frameScan;
      if (endStream)
	impl_()->app()->rxRun([
	  link = impl_(), id]() {
	  link->h2LocalEnd(id);
	});
      if (m_headerStream && inspected == FrameDrainBatch)
	inspected = 0;
    }
    if (m_frameScan && m_pendingFrames) {
      m_frameDrainPosted = true;
      impl_()->app()->txRun([link = impl_()]() {
	link->drainFramesTx_();
      });
    }
  }
  void txError_(uint32_t id) {
    impl_()->app()->rxRun([
      link = impl_(), id]() {
      if (!id)
	link->h2Error(Error::FlowControlError);
      else {
	link->sendReset_(id, Error::FlowControlError);
	link->h2ResetLogical(id, Error::FlowControlError);
      }
    });
  }
  bool streamTxError_(uint32_t id, ZuCSpan message) {
    auto e = ZeEXCEPT(Error, "Zhttp", message);
    auto entry_ = h2Stream(id);
    if (entry_ && entry_->txErrorFn && !entry_->txErrorFn(e))
      impl_()->disconnectNative();
    return false;
  }

protected:
  void peerProcessed(uint32_t id) {
    if (m_server && id > m_lastProcessedPeerStream)
      m_lastProcessedPeerStream = id;
    m_decoding = false;
  }

private:
  // immutable after initWire()
  H2Config		m_config;
  bool			m_server = false;

  // shared queue admission count; queued frame storage remains Tx-owned
  alignas(Zm::CacheLineSize)
  QueueAdmission	m_frameAdmission;

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  ZmRef<Streams>	m_streams;
  ZmScheduler::Timer	m_settingsTimer;
  HPackDecoder		m_decoder;
  RxDataFrame		m_rxData;
  uint32_t		m_lastProcessedPeerStream = 0;
  uint32_t		m_peerInitialWindow = DefltWindow;
  ClosedStreams		m_recentStreams;
  ZmRef<ClosedStreamSet> m_recentStreamSet;
  uint32_t		m_nextLocalStream = 1;
  uint32_t		m_lastPeerStream = 0;
  uint32_t		m_localStreamMax = 0;
  uint32_t		m_peerStreamMax = 0;
  uint32_t		m_localStreamIDMax = MaxWindow;
  uint32_t		m_localStreamCount = 0;
  uint32_t		m_peerStreamCount = 0;
  unsigned		m_recentStreamHead = 0;
  uint32_t		m_discardStream = 0;
  int64_t		m_rxWindow = DefltWindow;
  int64_t		m_txWindowHint = DefltWindow;
  bool			m_decoding = false;
  bool			m_errorSent = false;
  bool			m_stopping = false;
  bool			m_rxPreface = false;
  bool			m_rxDataValidated = false;

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
  HPackEncoder		m_encoder;
  ZmRef<TxWindowHash>	m_txWindows;
  PendingFrameQueue	m_pendingFrames;
  uint32_t		m_txInitialWindow = DefltWindow;
  uint32_t		m_txFrameSize = DefltFrameSize;
  uint32_t		m_frameCursor = 0;
  uint32_t		m_frameScan = 0;
  uint32_t		m_headerStream = 0;
  int64_t		m_txWindow = DefltWindow;
  bool			m_frameDrainPosted = false;
  bool			m_txExtendedConnect = false;
};

template <typename App> class ClientHub;
template <typename App> class CliLink;

struct ClientSlot {
  ZmContext	owner;
  void		(*close)(void *) = nullptr;
  bool		(*available)(void *) = nullptr;
  bool		(*down)(void *) = nullptr;
  Ztls::Host	host;
  uint16_t	port = 0;
};

struct ClientPoolKey {
  Ztls::Host	host;
  uint16_t	port = 0;

  bool equals(const ClientPoolKey &key) const {
    return port == key.port && host == key.host;
  }
  friend bool operator ==(
    const ClientPoolKey &l, const ClientPoolKey &r) {
    return l.equals(r);
  }
  uint32_t hash() const {
    return ZuHash<Ztls::Host>::hash(host) ^ uint32_t(port);
  }
};

struct ClientPoolEntry {
  ClientPoolKey	key;
  ZmContext	owner;
};

inline const ClientPoolKey &ClientPoolEntry_KeyAxor(
  const ClientPoolEntry &entry)
{
  return entry.key;
}

ZuDerive(ClientPoolHash,
  (ZmHash<ClientPoolEntry,
    ZmHashNode<ClientPoolEntry,
      ZmHashKey<ClientPoolEntry_KeyAxor,
	ZmHashLock<ZmNoLock,
	  ZmHashHeapID<"Zhttp.H2">>>>>));

template <typename App>
class ClientHub : public Ztls::Client<ClientHub<App>> {
public:
  using Base = Ztls::Client<ClientHub>;
  using Link = CliLink<App>;
  using StopFn = ZmFn<void(bool), ZmFnHeapID<"Zhttp.H2">>;
  using Slots = ZtArray<ClientSlot,
    ZtArrayHeapID<"Zhttp.H2">>;
  using StopFns = ZtArray<StopFn,
    ZtArrayHeapID<"Zhttp.H2">>;

  App *user() { return static_cast<App *>(this); }

  ClientHub() : m_pool{new ClientPoolHash} { }

  bool init(const HubConfig &hub, const H2Config &config) {
    if (!TLS_::valid(config) || config.policy() != H2Policy::Force)
      return false;
    m_config = config;
    auto params = TLS_::clientParams(hub, config);
    return Base::init(ZuMv(params));
  }

  template <typename Logical>
  void connect(Logical *logical, Ztls::Host host, uint16_t port) {
    ZmRef<Logical> logical_ = ZmMkRef(logical);
    this->rxInvoke([
      this, logical = ZuMv(logical_), host = ZuMv(host), port
    ]() mutable {
      ClientPoolKey key{host, port};
      auto poolEntry = m_pool->findPtr(key);
      ZmRef<Link> link;
      if (poolEntry) {
	link = poolEntry->owner.object<Link>();
	if (!link->available()) link = nullptr;
      }
      if (!link) {
	link = new Link{this, host, port, m_config};
	m_slots.push(ClientSlot{
	  .owner = link,
	  .close = [](void *ptr) {
	    static_cast<Link *>(ptr)->beginStop();
	  },
	  .available = [](void *ptr) {
	    return static_cast<Link *>(ptr)->available();
	  },
	  .down = [](void *ptr) {
	    return static_cast<Link *>(ptr)->isDown();
	  },
	  .host = host,
	  .port = port
	});
	if (poolEntry)
	  poolEntry->owner = link;
	else
	  m_pool->add(ClientPoolEntry{ZuMv(key), link});
	link->add(ZuMv(logical));
	link->connect(ZuMv(host), port);
	return;
      }
      link->add(ZuMv(logical));
    });
  }

  bool stop() {
    return ZmBlock<bool>{}(
      [this](auto wake) { stop(StopFn{ZuMv(wake)}); });
  }
  template <typename Done>
  void stop(Done &&done) {
    StopFn fn{ZuFwd<Done>(done)};
    this->rxRun([this, fn = ZuMv(fn)]() mutable {
      m_stopFns.push(ZuMv(fn));
      if (m_stopping) return;
      m_stopping = true;
      m_stopPending = 0;
      for (auto &slot: m_slots)
	if (!slot.down(slot.owner.object<void>())) ++m_stopPending;
      if (!m_stopPending) {
	stopDrain_();
	return;
      }
      for (auto &slot: m_slots)
	if (!slot.down(slot.owner.object<void>()))
	  slot.close(slot.owner.object<void>());
    });
  }
  void linkDown(CliLink<App> *link) {
    if (auto entry = m_pool->findPtr(
	ClientPoolKey{link->host(), link->port()}))
      if (entry->owner.object<CliLink<App>>() == link)
	m_pool->delNode(
	  static_cast<ClientPoolHash::Node *>(entry));
    for (unsigned i = 0; i < m_slots.length(); ++i)
      if (m_slots[i].owner.object<CliLink<App>>() == link) {
	m_slots.splice(i, 1);
	break;
      }
    if (m_stopping && m_stopPending && !--m_stopPending)
      stopDrain_();
  }
  void final() {
    for ([[maybe_unused]] auto &slot: m_slots)
      ZmAssert(slot.down(slot.owner.object<void>()));
    m_slots.length(0);
    ZmAssert(!m_pool->count_());
    m_pool->clean();
    Base::final();
  }
  unsigned reconnFreq() const { return 0; }
  const H2Config &h2Config() const { return m_config; }

private:
  void stopDrain_() {
    this->rxRun([this]() {
      this->txRun([this]() {
	this->rxRun([this]() {
	  Base::stop([this](bool ok) {
	    this->rxRun([this, ok]() { stopped_(ok); });
	  });
	});
      });
    });
  }
  void stopped_(bool ok) {
    auto fns = ZuMv(m_stopFns);
    m_stopFns.init_();
    m_stopPending = 0;
    m_stopping = false;
    for (auto &fn: fns) {
      fn(ok);
      fn = {};
    }
  }

  Slots		m_slots;
  ZmRef<ClientPoolHash> m_pool;
  StopFns	m_stopFns;
  H2Config	m_config;
  unsigned	m_stopPending = 0;
  bool		m_stopping = false;
};

template <typename App>
class CliLink :
  public Ztls::CliLink<ClientHub<App>, CliLink<App>,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>,
  public Wire<CliLink<App>, typename App::Link> {
public:
  using Hub = ClientHub<App>;
  using Logical = typename App::Link;
  using Base = Ztls::CliLink<Hub, CliLink,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>;
  using Wire_ = Wire<CliLink, Logical>;
  using Pending = ZtArray<ZmRef<Logical>,
    ZtArrayHeapID<"Zhttp.H2">>;
  using Active = ZtArray<ZmRef<Logical>,
    ZtArrayHeapID<"Zhttp.H2">>;
  using StreamIDs = ZtArray<uint32_t,
    ZtArrayHeapID<"Zhttp.H2">>;

  CliLink(
    Hub *app, Ztls::Host host_, uint16_t port_,
    const H2Config &config) :
      Base{app}, m_host{ZuMv(host_)}, m_port{port_}
  {
    Wire_::initWire(false, config);
    m_pendingMax = config.maxPending();
  }
  ~CliLink() {
    Wire_::finalWire();
  }

  const Ztls::Host &host() const { return m_host; }
  uint16_t port() const { return m_port; }
  void add(ZmRef<Logical> logical) {
    if (m_pending.length() >= m_pendingMax &&
	(!m_ready || !Wire_::canOpenLocalStream())) {
      logical->connectFailed_(false);
      return;
    }
    logical->native(this);
    if (!m_ready || m_pending || !Wire_::canOpenLocalStream()) {
      m_pending.push(ZuMv(logical));
      return;
    }
    openNow_(ZuMv(logical));
  }
  bool available() const {
    return !m_down && !m_draining &&
      !Wire_::localStreamsExhausted() &&
      (m_ready && Wire_::canOpenLocalStream() ||
	m_pending.length() < m_pendingMax);
  }
  bool isDown() const { return m_down; }
  void connected(Ztls::Connected info) {
    if (info.alpn != "h2") {
      connectFailed(false);
      return;
    }
    Wire_::sendInitial();
  }
  void h2SettingsReceived() {
    m_ready = true;
    admit_();
  }
  void disconnected(bool peer) {
    if (m_down) return;
    m_down = true;
    Wire_::stopWire();
    Active active;
    Wire_::allStreams([&active](auto &entry) {
      if (!entry.notified) active.push(entry.logical);
    });
    Wire_::clearStreams([
      link = ZmMkRef(this), active = ZuMv(active), peer
    ]() mutable {
      for (auto &logical: active) {
	if (logical->result() == ResultCode::OK)
	  logical->result_(ResultCode::Indeterminate);
	logical->disconnected_(peer);
      }
      for (auto &logical: link->m_pending) {
	if (logical->result() == ResultCode::OK)
	  logical->result_(ResultCode::Unprocessed);
	logical->connectFailed_(false);
      }
      link->m_pending.length(0);
      link->app()->linkDown(link);
    });
  }
  void connectFailed(bool transient) {
    if (m_down) return;
    m_down = true;
    Wire_::stopWire();
    for (auto &logical: m_pending) {
      if (logical->result() == ResultCode::OK)
	logical->result_(ResultCode::Unprocessed);
      logical->connectFailed_(transient);
    }
    m_pending.length(0);
    Base::disconnect();
    this->app()->linkDown(this);
  }
  int process(Ztls::RxStream &rx) { return Wire_::process(rx); }
  auto txStream() { return Base::txStream(); }
  auto directTxStream() { return Base::txStream_(); }
  void disconnectNative() { Base::disconnect(); }

  void openNow_(ZmRef<Logical> logical) {
    auto entry = Wire_::openLocalStream(logical);
    if (!entry) {
      logical->connectFailed_(false);
      logical->native({});
      return;
    }
    logical->stream(entry->id);
    logical->connected_(ProfileTraits<H2TLS>::apply({
      .alpn = "h2",
      .version = uint32_t(13),
      .transport = Transport::TLS,
      .secure = true
    }));
  }
  auto logicalTx(uint32_t id) {
    return HeaderBlock<CliLink>{
      *this, Wire_::encoder(), id, Wire_::peerFrameSize()};
  }
  void close(Logical *logical, uint32_t id) {
    this->app()->rxInvoke([
      link = this, logical = ZmMkRef(logical), id
    ]() mutable {
      auto entry = link->h2Stream(id);
      if (!entry || entry->logical.ptr() != logical.ptr()) return;
      link->rst_(id, Error::Cancel);
      link->notify_(id, false);
    });
  }
  void h2LocalEnd(uint32_t id) {
    if (auto entry = Wire_::h2Stream(id)) {
      entry->localEnd = true;
      if (entry->remoteEnd) closeLater_(id, false);
    }
  }
  void h2RemoteEnd(uint32_t id) {
    Wire_::peerProcessed(id);
    if (auto entry = Wire_::h2Stream(id)) {
      entry->remoteEnd = true;
      if (entry->localEnd) closeLater_(id, true);
    }
  }
  void h2ResetLogical(uint32_t id, Error::T) { notify_(id, true); }
  void h2Cancel(uint32_t id) {
    rst_(id, Error::Cancel);
    notify_(id, false);
  }
  auto h2OpenPeer(uint32_t) -> Stream<Logical> * { return nullptr; }
  bool h2Closed(uint32_t id) const { return Wire_::streamClosed(id); }
  Error::T h2OpenError(uint32_t id) const {
    return h2Closed(id) ? Error::StreamClosed : Error::ProtocolError;
  }
  void h2Goaway_(uint32_t last, Error::T) {
    m_draining = true;
    StreamIDs close;
    Wire_::allStreams([last, &close](auto &entry) {
      if (entry.id > last) {
	entry.logical->result_(ResultCode::Unprocessed);
	close.push(entry.id);
      }
    });
    for (auto id: close) notify_(id, true);
    requeue_();
  }
  void h2PeerSetting(uint16_t key, uint32_t value) {
    switch (key) {
      case Setting::MaxConcurrentStreams:
	Wire_::localStreamMax(value);
	admit_();
	break;
      case Setting::InitialWindowSize:
	if (!Wire_::peerInitialWindow(value))
	  this->h2Error(Error::FlowControlError);
	break;
      default:
	break;
    }
  }
  void beginStop() {
    if (m_stopping || m_down) return;
    m_stopping = true;
    m_draining = true;
    Wire_::allStreams([](auto &entry) {
      entry.logical->result_(ResultCode::Cancelled);
    });
    for (auto &logical: m_pending)
      logical->result_(ResultCode::Cancelled);
    Wire_::stopWire();
    Wire_::graceful();
    Base::disconnect();
  }

private:
  void admit_() {
    if (!m_ready) return;
    unsigned n = 0;
    while (n < m_pending.length() && Wire_::canOpenLocalStream())
      openNow_(ZuMv(m_pending[n++]));
    if (n) m_pending.splice(0, n);
    if (Wire_::localStreamsExhausted()) {
      m_draining = true;
      requeue_();
    }
  }
  void requeue_() {
    if (!m_pending || m_requeuePosted) return;
    m_requeuePosted = true;
    this->app()->rxRun([
      link = this]() { link->requeueNow_(); });
  }
  void requeueNow_() {
    unsigned n = m_pending.length();
    if (n > H2_::RequeueBatch) n = H2_::RequeueBatch;
    for (unsigned i = 0; i < n; ++i) {
      auto logical = ZuMv(m_pending[i]);
      if (m_stopping) {
	logical->result_(ResultCode::Cancelled);
	logical->connectFailed_(false);
      } else {
	logical->connect(m_host, m_port);
      }
    }
    if (n) m_pending.splice(0, n);
    if (m_pending) {
      this->app()->rxRun([
	link = this]() { link->requeueNow_(); });
    } else {
      m_requeuePosted = false;
    }
  }
  void closeLater_(uint32_t id, bool peer) {
    auto entry = Wire_::h2Stream(id);
    if (!entry || entry->closing) return;
    entry->closing = true;
    this->app()->rxRun([
      link = this, id, peer]() {
      link->notify_(id, peer);
    });
  }
  void rst_(uint32_t id, Error::T error) {
    auto tx = Base::txStream();
    StreamBytes<decltype(tx)> sink{tx};
    putHeader(sink, {
      .length = 4, .streamID = id, .type = FrameType::RSTStream
    });
    putUInt32(sink, error);
    tx.flush();
  }
  void notify_(uint32_t id, bool peer) {
    auto entry = Wire_::h2Stream(id);
    if (!entry || entry->notified) return;
    entry->notified = true;
    auto logical = entry->logical;
    Wire_::removeStream(id, [
      link = this, logical = ZuMv(logical), peer
    ]() mutable {
      logical->disconnected_(peer);
      link->admit_();
    });
  }

  Pending	m_pending;
  Ztls::Host	m_host;
  uint16_t	m_port = 0;
  uint32_t	m_pendingMax = 0;
  bool		m_ready = false;
  bool		m_down = false;
  bool		m_draining = false;
  bool		m_stopping = false;
  bool		m_requeuePosted = false;
};

template <typename App> class ServerHub;
template <typename App> class SrvLink;

template <typename App>
class ServerHub : public Ztls::Server<ServerHub<App>> {
public:
  using Base = Ztls::Server<ServerHub>;
  using Link = SrvLink<App>;
  using StopFn = ZmFn<void(bool), ZmFnHeapID<"Zhttp.H2">>;
  using StopFns = ZtArray<StopFn,
    ZtArrayHeapID<"Zhttp.H2">>;

  App *user() { return static_cast<App *>(this); }
  const App *user() const { return static_cast<const App *>(this); }

  bool init(const HubConfig &hub, const H2Config &config) {
    if (!TLS_::valid(config) || config.policy() != H2Policy::Force)
      return false;
    m_config = config;
    auto params = TLS_::serverParams(hub, config);
    return Base::init(ZuMv(params));
  }
  ZiIP localIP() const { return user()->localIP(); }
  unsigned localPort() const { return user()->localPort(); }
  unsigned nAccepts() const { return 8; }
  void listening(const ZiListenInfo &info) { user()->listening(info); }
  void listenFailed(bool transient) { user()->listenFailed(transient); }
  ZiConnection *accepted(const ZiCxnInfo &ci) {
    if (!user()->admit(ci)) return nullptr;
    return new typename Link::Cxn(
      new Link{this, ci, m_config}, ci);
  }
  bool start() {
    if (!Base::start()) return false;
    Base::listen();
    return true;
  }
  template <typename Done>
  void start(Done &&done) {
    Base::start([this, done = ZuFwd<Done>(done)](bool ok) mutable {
      if (ok) Base::listen();
      done(ok);
    });
  }
  void stopAccepting() { Base::stopListening(); }
  bool stop() {
    return ZmBlock<bool>{}(
      [this](auto wake) { stop(StopFn{ZuMv(wake)}); });
  }
  template <typename Done>
  void stop(Done &&done) {
    StopFn fn{ZuFwd<Done>(done)};
    Base::stopListening();
    this->rxRun([this, fn = ZuMv(fn)]() mutable {
      m_stopFns.push(ZuMv(fn));
      if (m_stopping) return;
      m_stopping = true;
      this->rxRun([this]() {
	m_stopPending = 0;
	Base::allLinks_({this, [](ServerHub *hub, Ztc::Link *link) {
	  ++hub->m_stopPending;
	  static_cast<Link *>(link)->beginStop();
	}});
	if (!m_stopPending) stopDrain_();
      });
    });
  }
  void linkDown() {
    if (m_stopping && m_stopPending && !--m_stopPending)
      stopDrain_();
  }
  void final() {
    ZmAssert(!m_stopPending);
    ZmAssert(!m_stopFns);
    Base::final();
  }
  const H2Config &h2Config() const { return m_config; }

private:
  void stopDrain_() {
    this->rxRun([this]() {
      this->txRun([this]() {
	this->rxRun([this]() {
	  Base::stop([this](bool ok) {
	    this->rxRun([this, ok]() { stopped_(ok); });
	  });
	});
      });
    });
  }
  void stopped_(bool ok) {
    auto fns = ZuMv(m_stopFns);
    m_stopFns.init_();
    m_stopPending = 0;
    m_stopping = false;
    for (auto &fn: fns) {
      fn(ok);
      fn = {};
    }
  }

  H2Config	m_config;
  StopFns	m_stopFns;
  unsigned	m_stopPending = 0;
  bool		m_stopping = false;
};

template <typename App>
class SrvLink :
  public Ztls::SrvLink<ServerHub<App>, SrvLink<App>,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>,
  public Wire<SrvLink<App>, typename App::Link> {
public:
  using Hub = ServerHub<App>;
  using Logical = typename App::Link;
  using Base = Ztls::SrvLink<Hub, SrvLink,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>;
  using Wire_ = Wire<SrvLink, Logical>;
  using Cxn = typename Base::Cxn;
  using Active = ZtArray<ZmRef<Logical>,
    ZtArrayHeapID<"Zhttp.H2">>;

  SrvLink(
    Hub *app, const ZiCxnInfo &ci, const H2Config &config) :
      Base{app}
  {
    m_remote << ci.remoteIP;
    Wire_::initWire(true, config);
  }
  ~SrvLink() {
    Wire_::finalWire();
  }

  void connected(Ztls::Connected info) {
    if (info.alpn != "h2") {
      Base::disconnect();
      return;
    }
    Wire_::sendInitial();
  }
  void h2SettingsReceived() { }
  void disconnected(bool peer) {
    if (m_down) return;
    m_down = true;
    Wire_::stopWire();
    Active active;
    Wire_::allStreams([&active](auto &entry) {
      if (!entry.notified) active.push(entry.logical);
    });
    Wire_::clearStreams([
      link = ZmMkRef(this), active = ZuMv(active), peer
    ]() mutable {
      for (auto &logical: active) logical->disconnected_(peer);
      if (!link->m_released) {
	link->m_released = true;
	link->app()->user()->release();
      }
      link->app()->linkDown();
    });
  }
  int process(Ztls::RxStream &rx) { return Wire_::process(rx); }
  auto txStream() { return Base::txStream(); }
  auto directTxStream() { return Base::txStream_(); }
  void disconnectNative() { Base::disconnect(); }
  auto logicalTx(uint32_t id) {
    return HeaderBlock<SrvLink>{
      *this, Wire_::encoder(), id, Wire_::peerFrameSize()};
  }

  Stream<Logical> *h2OpenPeer(uint32_t id) {
    if (m_draining || !Wire_::canOpenPeerStream(id)) return nullptr;
    ZmRef<Logical> logical = new Logical{
      this->app()->user(), this, id, ZuCSpan{m_remote}};
    auto entry = Wire_::openPeerStream(id, logical);
    if (!entry) return nullptr;
    logical->connected_(ProfileTraits<H2TLS>::apply({
      .alpn = "h2",
      .version = uint32_t(13),
      .transport = Transport::TLS,
      .secure = true
    }));
    return entry;
  }
  bool h2Closed(uint32_t id) const { return Wire_::streamClosed(id); }
  Error::T h2OpenError(uint32_t id) {
    if (h2Closed(id)) return Error::StreamClosed;
    if (Wire_::peerStreamIdle(id)) {
      Wire_::refusePeerStream(id);
      return Error::RefusedStream;
    }
    return Error::ProtocolError;
  }
  void h2LocalEnd(uint32_t id) {
    if (auto entry = Wire_::h2Stream(id)) {
      entry->localEnd = true;
      if (entry->remoteEnd) closeLater_(id, false);
    }
  }
  void h2RemoteEnd(uint32_t id) {
    Wire_::peerProcessed(id);
    if (auto entry = Wire_::h2Stream(id)) {
      entry->remoteEnd = true;
      if (entry->localEnd) closeLater_(id, true);
    }
  }
  void h2ResetLogical(uint32_t id, Error::T) { notify_(id, true); }
  void h2Cancel(uint32_t id) {
    rst_(id, Error::Cancel);
    notify_(id, false);
  }
  void h2Goaway_(uint32_t, Error::T) { m_draining = true; }
  void h2PeerSetting(uint16_t key, uint32_t value) {
    if (key == Setting::InitialWindowSize &&
	!Wire_::peerInitialWindow(value))
      this->h2Error(Error::FlowControlError);
  }
  void beginStop() {
    if (m_stopping || m_down) return;
    m_stopping = true;
    m_draining = true;
    Wire_::stopWire();
    Wire_::graceful();
    Base::disconnect();
  }

private:
  void closeLater_(uint32_t id, bool peer) {
    auto entry = Wire_::h2Stream(id);
    if (!entry || entry->closing) return;
    entry->closing = true;
    this->app()->rxRun([
      link = this, id, peer]() {
      link->notify_(id, peer);
    });
  }
  void rst_(uint32_t id, Error::T error) {
    auto tx = Base::txStream();
    StreamBytes<decltype(tx)> sink{tx};
    putHeader(sink, {
      .length = 4, .streamID = id, .type = FrameType::RSTStream
    });
    putUInt32(sink, error);
    tx.flush();
  }
  void notify_(uint32_t id, bool peer) {
    auto entry = Wire_::h2Stream(id);
    if (!entry || entry->notified) return;
    entry->notified = true;
    auto logical = entry->logical;
    Wire_::removeStream(id, [logical = ZuMv(logical), peer]() mutable {
      logical->disconnected_(peer);
    });
  }

  EndpointString m_remote;
  bool		m_draining = false;
  bool		m_down = false;
  bool		m_released = false;
  bool		m_stopping = false;
};

} // namespace H2_

namespace H2_ {

template <typename Impl>
class LogicalStream {
public:
  bool streamPeerCap() {
    auto tx = streamImpl_()->txStream();
    return tx.extendedConnect();
  }
  bool streamLocalCap() {
    auto tx = streamImpl_()->txStream();
    return tx.localExtendedConnect();
  }
  template <typename L>
  void streamTx(L &&l) {
    auto tx = streamImpl_()->txStream();
    auto body = tx.body();
    ZuFwd<L>(l)(body);
  }
  void streamTxEnd() {
    auto tx = streamImpl_()->txStream();
    tx.end();
  }
  void streamTxReset() { streamImpl_()->disconnect(); }

private:
  Impl *streamImpl_() { return static_cast<Impl *>(this); }
};

template <typename App, typename Impl, typename NativeLink>
class ClientLogical : public ZmObject, public LogicalStream<Impl> {
public:
  enum { TLS = 1, Multiplexed = 1 };

  ClientLogical(App *app) : m_app{app} {
#ifdef ZmObject_DEBUG
    this->ZmObject::debug();
#endif
  }
  App *app() const { return m_app; }
  auto impl() { return static_cast<Impl *>(this); }

  template <typename Host>
  void connect(Host &&host, uint16_t port) {
    prepare_();
    m_app->connect(
      impl(), Ztls::Host{ZuFwd<Host>(host)}, port);
  }
  void prepare_() {
    m_cancelled = false;
    m_connected = false;
    m_failed = false;
    m_result = ResultCode::OK;
  }
  template <typename Endpoint>
  void connectEndpoint(const Endpoint &endpoint) {
    connect(endpoint.target, endpoint.port);
  }
  auto txStream() { return m_native->logicalTx(m_streamID); }
  void txErrorFn(ZiTxErrorFn fn) {
    m_txErrorFn = ZuMv(fn);
    if (m_native && m_streamID)
      m_native->logicalTxErrorFn(m_streamID, m_txErrorFn);
  }
  template <typename Parser, typename Rx>
  auto receive(Parser &parser, Rx &rx) { return parser.process(rx); }
  template <typename Builder>
  auto transmit(Builder &) { return txStream(); }
  void finish() { }
  bool active() const { return m_native && m_streamID; }
  void disconnect() {
    m_cancelled = true;
    if (m_native && m_streamID)
      m_native->close(impl(), m_streamID);
  }
  void connected_(ConnectedInfo info) {
    if (m_cancelled) {
      disconnect();
      return;
    }
    m_connected = true;
    m_app->connected(*impl(), ZuMv(info));
  }
  void disconnected_(bool peer) {
    m_native = nullptr;
    m_streamID = 0;
    if (m_connected)
      m_app->disconnected(*impl(), peer);
    else if (!m_failed)
      connectFailed_(false);
  }
  void connectFailed_(bool transient) {
    if (m_failed) return;
    m_failed = true;
    m_native = nullptr;
    m_streamID = 0;
    m_app->connectFailed(*impl(), transient);
  }
  int8_t result() const { return m_result; }
  void result_(int8_t result) { m_result = result; }
  template <typename Rx>
  int process_(Rx &rx) { return m_app->process(*impl(), rx); }
  void native(NativeLink *native) { m_native = native; }
  void stream(uint32_t id) {
    m_streamID = id;
    if (m_native && m_streamID)
      m_native->logicalTxErrorFn(m_streamID, m_txErrorFn);
  }
  template <typename State> void responseHeadersParsed(State *) { }
  template <typename State> void responseBodyBytes(State *) { }

private:
  App		*m_app = nullptr;
  NativeLink	*m_native = nullptr;
  ZiTxErrorFn	m_txErrorFn;
  uint32_t	m_streamID = 0;
  bool		m_connected = false;
  bool		m_failed = false;
  bool		m_cancelled = false;
  int8_t	m_result = ResultCode::OK;
};

} // namespace H2_

template <typename App, typename Impl>
class ClientLink<App, Impl, H2TLS> :
  public H2_::ClientLogical<App, Impl, H2_::CliLink<App>> {
  using Base =
    H2_::ClientLogical<App, Impl, H2_::CliLink<App>>;

public:
  using Base::Base;
};

template <typename App>
class ClientHub<App, H2TLS> : public H2_::ClientHub<App> {
public:
  using Base = H2_::ClientHub<App>;
  enum { TLS = 1, Multiplexed = 1 };
  using Base::connect;
  using Base::init;

  template <typename Link>
  void connected(Link &, ConnectedInfo) { }
  template <typename Link>
  void disconnected(Link &, bool) { }
  template <typename Link>
  void connectFailed(Link &, bool) { }
};

namespace H2_ {

template <typename App, typename Impl, typename Session, typename NativeLink>
class ServerLogical : public ZmObject, public LogicalStream<Impl> {
public:
  enum { TLS = 1, Multiplexed = 1 };

  ServerLogical(
    App *app, NativeLink *native, uint32_t streamID, ZuCSpan remote) :
      m_app{app}, m_native{native}, m_streamID{streamID}, m_remote{remote}
  {
#ifdef ZmObject_DEBUG
    this->ZmObject::debug();
#endif
  }
  App *app() const { return m_app; }
  auto impl() { return static_cast<Impl *>(this); }
  ZuCSpan remote() const { return m_remote; }
  Session &session() { return m_session; }
  auto txStream() { return m_native->logicalTx(m_streamID); }
  void txErrorFn(ZiTxErrorFn fn) {
    if (m_native)
      m_native->logicalTxErrorFn(m_streamID, ZuMv(fn));
  }
  template <typename Parser, typename Rx>
  auto receive(Parser &parser, Rx &rx) { return parser.process(rx); }
  template <typename Builder>
  auto transmit(Builder &) { return txStream(); }
  void finish() { }
  void disconnect() {
    if (m_native) m_native->h2Cancel(m_streamID);
  }
  void connected_(ConnectedInfo info) {
    m_session.connected(*impl());
    m_app->connected(*impl(), ZuMv(info));
  }
  void disconnected_(bool peer) {
    if (!m_native) return;
    m_session.disconnected(*impl(), peer);
    m_app->disconnected(*impl(), peer);
    m_native = nullptr;
    m_streamID = 0;
  }
  template <typename Rx>
  int process_(Rx &rx) { return m_session.process(*impl(), rx); }

private:
  App		*m_app = nullptr;
  NativeLink	*m_native = nullptr;
  uint32_t	m_streamID = 0;
  Session	m_session;
  EndpointString m_remote;
};

} // namespace H2_

template <typename App, typename Impl, typename Session>
class ServerLink<App, Impl, H2TLS, Session> :
  public H2_::ServerLogical<
    App, Impl, Session, H2_::SrvLink<App>> {
  using Base = H2_::ServerLogical<
    App, Impl, Session, H2_::SrvLink<App>>;

public:
  using Base::Base;
};

namespace TLS_ {

template <typename App, typename H1Logical, typename H2Logical>
class ClientHub;
template <typename App, typename H1Logical, typename H2Logical>
class CliLink;
template <typename App> class ServerHub;
template <typename App> class SrvLink;

template <typename App, typename Impl, typename NativeLink>
class ClientH1Logical : public ZmObject {
public:
  enum { TLS = 1, Multiplexed = 0 };

  ClientH1Logical(App *app) : m_app{app} {
#ifdef ZmObject_DEBUG
    this->ZmObject::debug();
#endif
  }

  App *app() const { return m_app; }
  auto impl() { return static_cast<Impl *>(this); }
  auto txStream() { return m_native->txStream(); }
  void txErrorFn(ZiTxErrorFn fn) {
    m_txErrorFn = ZuMv(fn);
    if (m_native) m_native->txErrorFn(m_txErrorFn);
  }
  template <typename Parser, typename Rx>
  auto receive(Parser &parser, Rx &rx) { return parser.process(rx); }
  template <typename Builder>
  auto transmit(Builder &) { return txStream(); }
  void finish() { }
  template <typename Done>
  void complete(Done &&done) {
    if (m_native)
      m_native->releaseH1(impl(), ZuFwd<Done>(done));
    else
      done();
  }
  bool active() const { return m_native; }
  void prepare_() {
    m_connected = false;
    m_failed = false;
    m_cancelled = false;
    m_result = ResultCode::OK;
  }
  void disconnect() {
    m_cancelled = true;
    if (m_native) m_native->disconnectNative();
  }
  void connected_(ConnectedInfo info) {
    if (m_cancelled) {
      disconnect();
      return;
    }
    m_connected = true;
    m_app->connected(*impl(), ZuMv(info));
  }
  void disconnected_(bool peer) {
    m_native = nullptr;
    if (m_connected)
      m_app->disconnected(*impl(), peer);
    else if (!m_failed)
      connectFailed_(false);
  }
  void connectFailed_(bool transient) {
    if (m_failed) return;
    m_failed = true;
    m_native = nullptr;
    m_app->connectFailed(*impl(), transient);
  }
  int8_t result() const { return m_result; }
  void result_(int8_t result) { m_result = result; }
  int process_(Ztls::RxStream &rx) {
    return m_app->process(*impl(), rx);
  }
  void native(NativeLink *native) {
    m_native = native;
    if (m_native) m_native->txErrorFn(m_txErrorFn);
  }
  template <typename State> void responseHeadersParsed(State *) { }
  template <typename State> void responseBodyBytes(State *) { }

private:
  App		*m_app = nullptr;
  NativeLink	*m_native = nullptr;
  ZiTxErrorFn	m_txErrorFn;
  bool		m_connected = false;
  bool		m_failed = false;
  bool		m_cancelled = false;
  int8_t	m_result = ResultCode::OK;
};

template <typename H1Logical, typename H2Logical>
struct ClientChoice {
  ZmRef<H1Logical>	h1;
  ZmRef<H2Logical>	h2;
};

template <typename App, typename H1Logical_, typename H2Logical_>
class ClientHub :
  public Ztls::Client<ClientHub<App, H1Logical_, H2Logical_>> {
public:
  using Base = Ztls::Client<ClientHub>;
  using Link = CliLink<App, H1Logical_, H2Logical_>;
  using H1Logical = H1Logical_;
  using H2Logical = H2Logical_;
  using StopFn = ZmFn<void(bool), ZmFnHeapID<"Zhttp.H2">>;
  using Slots = ZtArray<ZmRef<Link>,
    ZtArrayHeapID<"Zhttp.H2">>;
  using StopFns = ZtArray<StopFn,
    ZtArrayHeapID<"Zhttp.H2">>;

  App *user() { return static_cast<App *>(this); }

  ClientHub() : m_pool{new H2_::ClientPoolHash} { }

  bool init(const HubConfig &hub, const H2Config &config) {
    if (!TLS_::valid(config)) return false;
    m_config = config;
    return Base::init(TLS_::clientParams(hub, config));
  }
  void connect(
    H1Logical *h1, H2Logical *h2, Ztls::Host host, uint16_t port) {
    ZmRef<H1Logical> h1_ = ZmMkRef(h1);
    ZmRef<H2Logical> h2_ = ZmMkRef(h2);
    h1_->prepare_();
    h2_->prepare_();
    this->rxInvoke([
      this, h1 = ZuMv(h1_), h2 = ZuMv(h2_),
      host = ZuMv(host), port
    ]() mutable {
      H2_::ClientPoolKey key{host, port};
      auto poolEntry = m_pool->findPtr(key);
      ZmRef<Link> link;
      if (poolEntry) {
	link = poolEntry->owner.object<Link>();
	if (!link->available()) link = nullptr;
      }
      if (!link) {
	link = new Link{this, host, port, m_config};
	m_slots.push(link);
	if (poolEntry)
	  poolEntry->owner = link;
	else
	  m_pool->add(H2_::ClientPoolEntry{ZuMv(key), link});
	link->add({ZuMv(h1), ZuMv(h2)});
	link->connect(ZuMv(host), port);
	return;
      }
      link->add({ZuMv(h1), ZuMv(h2)});
    });
  }
  bool stop() {
    return ZmBlock<bool>{}(
      [this](auto wake) { stop(StopFn{ZuMv(wake)}); });
  }
  template <typename Done>
  void stop(Done &&done) {
    StopFn fn{ZuFwd<Done>(done)};
    this->rxRun([this, fn = ZuMv(fn)]() mutable {
      m_stopFns.push(ZuMv(fn));
      if (m_stopping) return;
      m_stopping = true;
      m_stopPending = 0;
      for (auto &slot: m_slots)
	if (!slot->isDown()) ++m_stopPending;
      if (!m_stopPending) {
	stopDrain_();
	return;
      }
      for (auto &slot: m_slots)
	if (!slot->isDown()) slot->beginStop();
    });
  }
  void linkDown(Link *link) {
    if (auto entry = m_pool->findPtr(
	H2_::ClientPoolKey{link->host(), link->port()}))
      if (entry->owner.object<Link>() == link)
	m_pool->delNode(
	  static_cast<H2_::ClientPoolHash::Node *>(entry));
    for (unsigned i = 0; i < m_slots.length(); ++i)
      if (m_slots[i].ptr() == link) {
	m_slots.splice(i, 1);
	break;
      }
    if (m_stopping && m_stopPending && !--m_stopPending)
      stopDrain_();
  }
  void goaway(uint32_t) { }
  void final() {
    for ([[maybe_unused]] auto &slot: m_slots) ZmAssert(slot->isDown());
    m_slots.length(0);
    ZmAssert(!m_pool->count_());
    m_pool->clean();
    Base::final();
  }
  unsigned reconnFreq() const { return 0; }

private:
  void stopDrain_() {
    this->rxRun([this]() {
      this->txRun([this]() {
	this->rxRun([this]() {
	  Base::stop([this](bool ok) {
	    this->rxRun([this, ok]() { stopped_(ok); });
	  });
	});
      });
    });
  }
  void stopped_(bool ok) {
    auto fns = ZuMv(m_stopFns);
    m_stopFns.init_();
    m_stopPending = 0;
    m_stopping = false;
    for (auto &fn: fns) {
      fn(ok);
      fn = {};
    }
  }

  Slots		m_slots;
  ZmRef<H2_::ClientPoolHash> m_pool;
  StopFns	m_stopFns;
  H2Config	m_config;
  unsigned	m_stopPending = 0;
  bool		m_stopping = false;
};

template <typename App, typename H1Logical_, typename H2Logical_>
class CliLink :
  public Ztls::CliLink<
    ClientHub<App, H1Logical_, H2Logical_>,
    CliLink<App, H1Logical_, H2Logical_>,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>,
  public H2_::Wire<
    CliLink<App, H1Logical_, H2Logical_>, H2Logical_> {
public:
  using Hub = ClientHub<App, H1Logical_, H2Logical_>;
  using H1Logical = H1Logical_;
  using H2Logical = H2Logical_;
  using Choice = ClientChoice<H1Logical, H2Logical>;
  using Base = Ztls::CliLink<Hub, CliLink,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>;
  using Wire = H2_::Wire<CliLink, H2Logical>;
  using Pending = ZtArray<Choice,
    ZtArrayHeapID<"Zhttp.H2">>;
  using Active = ZtArray<ZmRef<H2Logical>,
    ZtArrayHeapID<"Zhttp.H2">>;
  using StreamIDs = ZtArray<uint32_t,
    ZtArrayHeapID<"Zhttp.H2">>;

  CliLink(
    Hub *app, Ztls::Host host, uint16_t port,
    const H2Config &config) :
      Base{app}, m_host{ZuMv(host)}, m_port{port},
      m_pendingMax{config.maxPending()}, m_policy{config.policy()}
  {
    Wire::initWire(false, config);
  }
  ~CliLink() {
    Wire::finalWire();
  }

  const Ztls::Host &host() const { return m_host; }
  uint16_t port() const { return m_port; }
  bool isDown() const { return m_down; }
  bool available() const {
    if (m_down || m_stopping || m_draining) return false;
    if (!m_ready)
      return m_policy == H2Policy::Force &&
	m_pending.length() < m_pendingMax;
    switch (m_version) {
      case Version::H1:
	return !m_h1 || m_pending.length() < m_pendingMax;
      case Version::H2:
	return !Wire::localStreamsExhausted() &&
	  (Wire::canOpenLocalStream() ||
	    m_pending.length() < m_pendingMax);
      default:
	return false;
    }
  }
  void add(Choice choice) {
    if (m_pending.length() >= m_pendingMax &&
	(!m_ready || m_version != Version::H2 ||
	 !Wire::canOpenLocalStream())) {
      fail_(choice, false);
      return;
    }
    if (!m_ready || m_pending ||
	m_version == Version::H1 && m_h1 ||
	m_version == Version::H2 && !Wire::canOpenLocalStream()) {
      m_pending.push(ZuMv(choice));
      return;
    }
    open_(ZuMv(choice));
  }
  void connected(Ztls::Connected info) {
    m_version = TLS_::version(info.alpn, m_policy);
    m_tlsVersion = info.version;
    switch (m_version) {
      case Version::H1:
	m_ready = true;
	admit_();
	break;
      case Version::H2:
	Wire::sendInitial();
	break;
      default:
	connectFailed(false);
	break;
    }
  }
  void h2SettingsReceived() {
    if (m_version != Version::H2) return;
    m_ready = true;
    admit_();
  }
  void disconnected(bool peer) {
    if (m_down) return;
    m_down = true;
    Wire::stopWire();
    if (m_h1) {
      if (m_h1->result() == ResultCode::OK)
	m_h1->result_(ResultCode::Indeterminate);
      m_h1->disconnected_(peer);
      m_h1 = nullptr;
    }
    Active active;
    if (m_version == Version::H2)
      Wire::allStreams([&active](auto &entry) {
	if (!entry.notified) active.push(entry.logical);
      });
    Wire::clearStreams([
      link = ZmMkRef(this), active = ZuMv(active), peer
    ]() mutable {
      for (auto &logical: active) {
	if (logical->result() == ResultCode::OK)
	  logical->result_(ResultCode::Indeterminate);
	logical->disconnected_(peer);
      }
      for (auto &choice: link->m_pending)
	link->fail_(choice, false);
      link->m_pending.length(0);
      link->app()->linkDown(link);
    });
  }
  void connectFailed(bool transient) {
    if (m_down) return;
    m_down = true;
    Wire::stopWire();
    for (auto &choice: m_pending) fail_(choice, transient);
    m_pending.length(0);
    Base::disconnect();
    this->app()->linkDown(this);
  }
  int process(Ztls::RxStream &rx) {
    switch (m_version) {
      case Version::H1: return m_h1 ? m_h1->process_(rx) : -1;
      case Version::H2: return Wire::process(rx);
      default: return -1;
    }
  }
  auto txStream() { return Base::txStream(); }
  auto directTxStream() { return Base::txStream_(); }
  auto logicalTx(uint32_t id) {
    return H2_::HeaderBlock<CliLink>{
      *this, this->encoder(), id, this->peerFrameSize()};
  }
  void disconnectNative() { Base::disconnect(); }
  template <typename Done>
  void releaseH1(H1Logical *logical, Done &&done) {
    this->app()->rxRun([
      link = this, logical = ZmMkRef(logical),
      done = ZuFwd<Done>(done)
    ]() mutable {
      if (link->m_h1.ptr() == logical.ptr()) {
	link->m_h1->disconnected_(false);
	link->m_h1 = nullptr;
	link->admit_();
      }
      done();
    });
  }

  void close(H2Logical *logical, uint32_t id) {
    this->app()->rxInvoke([
      link = this, logical = ZmMkRef(logical), id
    ]() mutable {
      auto entry = link->h2Stream(id);
      if (!entry || entry->logical.ptr() != logical.ptr()) return;
      link->rst_(id, H2::Error::Cancel);
      link->notify_(id, false);
    });
  }
  void h2LocalEnd(uint32_t id) {
    if (auto entry = Wire::h2Stream(id)) {
      entry->localEnd = true;
      if (entry->remoteEnd) closeLater_(id, false);
    }
  }
  void h2RemoteEnd(uint32_t id) {
    Wire::peerProcessed(id);
    if (auto entry = Wire::h2Stream(id)) {
      entry->remoteEnd = true;
      if (entry->localEnd) closeLater_(id, true);
    }
  }
  void h2ResetLogical(uint32_t id, H2::Error::T) {
    notify_(id, true);
  }
  void h2Cancel(uint32_t id) {
    rst_(id, H2::Error::Cancel);
    notify_(id, false);
  }
  auto h2OpenPeer(uint32_t) -> H2_::Stream<H2Logical> * {
    return nullptr;
  }
  bool h2Closed(uint32_t id) const { return Wire::streamClosed(id); }
  H2::Error::T h2OpenError(uint32_t id) const {
    return h2Closed(id) ?
      H2::Error::StreamClosed : H2::Error::ProtocolError;
  }
  void h2Goaway_(uint32_t last, H2::Error::T) {
    m_draining = true;
    StreamIDs close;
    Wire::allStreams([last, &close](auto &entry) {
      if (entry.id > last) {
	entry.logical->result_(ResultCode::Unprocessed);
	close.push(entry.id);
      }
    });
    for (auto id: close) notify_(id, true);
    requeue_();
    this->app()->user()->goaway(last);
  }
  void h2PeerSetting(uint16_t key, uint32_t value) {
    switch (key) {
      case H2::Setting::MaxConcurrentStreams:
	Wire::localStreamMax(value);
	admit_();
	break;
      case H2::Setting::InitialWindowSize:
	if (!Wire::peerInitialWindow(value))
	  this->h2Error(H2::Error::FlowControlError);
	break;
      default:
	break;
    }
  }
  void beginStop() {
    if (m_stopping || m_down) return;
    m_stopping = true;
    m_draining = true;
    if (m_h1) m_h1->result_(ResultCode::Cancelled);
    Wire::allStreams([](auto &entry) {
      entry.logical->result_(ResultCode::Cancelled);
    });
    for (auto &choice: m_pending)
      switch (m_policy) {
	case H2Policy::Disable:
	  choice.h1->result_(ResultCode::Cancelled);
	  break;
	default:
	  choice.h2->result_(ResultCode::Cancelled);
	  break;
      }
    Wire::stopWire();
    if (m_version == Version::H2) Wire::graceful();
    Base::disconnect();
  }

private:
  void open_(Choice choice) {
    switch (m_version) {
      case Version::H1:
	m_h1 = ZuMv(choice.h1);
	m_h1->native(this);
	m_h1->connected_(ProfileTraits<H1TLS>::apply({
	  .alpn = "http/1.1",
	  .version = uint32_t(m_tlsVersion),
	  .transport = Transport::TLS,
	  .secure = true
	}));
	break;
      case Version::H2: {
	auto logical = ZuMv(choice.h2);
	logical->native(this);
	auto entry = Wire::openLocalStream(logical);
	if (!entry) {
	  logical->connectFailed_(false);
	  logical->native({});
	  return;
	}
	logical->stream(entry->id);
	logical->connected_(ProfileTraits<H2TLS>::apply({
	  .alpn = "h2",
	  .version = uint32_t(m_tlsVersion),
	  .transport = Transport::TLS,
	  .secure = true
	}));
	break;
      }
    }
  }
  void admit_() {
    if (!m_ready) return;
    unsigned n = 0;
    switch (m_version) {
      case Version::H1:
	if (!m_h1 && m_pending) {
	  open_(ZuMv(m_pending[0]));
	  n = 1;
	}
	break;
      case Version::H2:
	while (n < m_pending.length() && Wire::canOpenLocalStream())
	  open_(ZuMv(m_pending[n++]));
	break;
    }
    if (n) m_pending.splice(0, n);
    if (m_version == Version::H2 && Wire::localStreamsExhausted()) {
      m_draining = true;
      requeue_();
    }
  }
  void requeue_() {
    if (!m_pending || m_requeuePosted) return;
    m_requeuePosted = true;
    this->app()->rxRun([
      link = this]() { link->requeueNow_(); });
  }
  void requeueNow_() {
    unsigned n = m_pending.length();
    if (n > H2_::RequeueBatch) n = H2_::RequeueBatch;
    for (unsigned i = 0; i < n; ++i) {
      auto choice = ZuMv(m_pending[i]);
      if (m_stopping) {
	switch (m_policy) {
	  case H2Policy::Disable:
	    choice.h1->result_(ResultCode::Cancelled);
	    choice.h1->connectFailed_(false);
	    break;
	  default:
	    choice.h2->result_(ResultCode::Cancelled);
	    choice.h2->connectFailed_(false);
	    break;
	}
      } else {
	this->app()->connect(
	  choice.h1, choice.h2, m_host, m_port);
      }
    }
    if (n) m_pending.splice(0, n);
    if (m_pending) {
      this->app()->rxRun([
	link = this]() { link->requeueNow_(); });
    } else {
      m_requeuePosted = false;
    }
  }
  void fail_(Choice &choice, bool transient) {
    switch (m_policy) {
      case H2Policy::Disable:
	if (choice.h1->result() == ResultCode::OK)
	  choice.h1->result_(ResultCode::Unprocessed);
	choice.h1->connectFailed_(transient);
	break;
      default:
	if (choice.h2->result() == ResultCode::OK)
	  choice.h2->result_(ResultCode::Unprocessed);
	choice.h2->connectFailed_(transient);
	break;
    }
  }
  void closeLater_(uint32_t id, bool peer) {
    auto entry = Wire::h2Stream(id);
    if (!entry || entry->closing) return;
    entry->closing = true;
    this->app()->rxRun([
      link = this, id, peer]() {
      link->notify_(id, peer);
    });
  }
  void rst_(uint32_t id, H2::Error::T error) {
    auto tx = Base::txStream();
    H2_::StreamBytes<decltype(tx)> sink{tx};
    H2::putHeader(sink, {
      .length = 4, .streamID = id, .type = H2::FrameType::RSTStream
    });
    H2::putUInt32(sink, error);
    tx.flush();
  }
  void notify_(uint32_t id, bool peer) {
    auto entry = Wire::h2Stream(id);
    if (!entry || entry->notified) return;
    entry->notified = true;
    auto logical = entry->logical;
    Wire::removeStream(id, [
      link = this, logical = ZuMv(logical), peer
    ]() mutable {
      logical->disconnected_(peer);
      link->admit_();
    });
  }

  Pending		m_pending;
  ZmRef<H1Logical>	m_h1;
  Ztls::Host		m_host;
  uint16_t		m_port = 0;
  uint32_t		m_pendingMax = 0;
  int			m_tlsVersion = 0;
  int8_t		m_policy = H2Policy::Force;
  int8_t		m_version = -1;
  bool			m_ready = false;
  bool			m_down = false;
  bool			m_draining = false;
  bool			m_stopping = false;
  bool			m_requeuePosted = false;
};

template <
  typename App, typename Impl, typename Session, typename NativeLink>
class ServerH1Logical : public ZmObject {
public:
  enum { TLS = 1, Multiplexed = 0 };

  ServerH1Logical(App *app, NativeLink *native, ZuCSpan remote) :
    m_app{app}, m_native{native}, m_remote{remote}
  {
#ifdef ZmObject_DEBUG
    this->ZmObject::debug();
#endif
  }

  App *app() const { return m_app; }
  auto impl() { return static_cast<Impl *>(this); }
  ZuCSpan remote() const { return m_remote; }
  Session &session() { return m_session; }
  auto txStream() { return m_native->txStream(); }
  void txErrorFn(ZiTxErrorFn fn) {
    if (m_native) m_native->txErrorFn(ZuMv(fn));
  }
  template <typename Parser, typename Rx>
  auto receive(Parser &parser, Rx &rx) { return parser.process(rx); }
  template <typename Builder>
  auto transmit(Builder &) { return txStream(); }
  void finish() { }
  void disconnect() {
    if (m_native) m_native->disconnectNative();
  }
  void connected_(ConnectedInfo info) {
    m_session.connected(*impl());
    m_app->connected(*impl(), ZuMv(info));
    touch_();
  }
  void disconnected_(bool peer) {
    if (!m_native) return;
    m_app->mx()->del(&m_idleTimer);
    m_session.disconnected(*impl(), peer);
    m_app->disconnected(*impl(), peer);
    m_native = nullptr;
  }
  int process_(Ztls::RxStream &rx) {
    int rc = m_session.process(*impl(), rx);
    if (rc >= 0) touch_();
    return rc;
  }

private:
  void touch_() {
    if (!m_native) return;
    auto timeout = m_app->idleTimeout();
    if (!timeout) return;
    m_app->mx()->add(
      &m_idleTimer, Zm::now(timeout), ZmScheduler::Update,
      [this](auto &&arm) {
	return arm([link = impl()]() { link->disconnect(); });
      }, m_app->rxThread());
  }

  App		*m_app = nullptr;
  NativeLink	*m_native = nullptr;
  Session	m_session;
  ZmScheduler::Timer m_idleTimer;
  EndpointString m_remote;
};

template <typename App>
class ServerHub : public Ztls::Server<ServerHub<App>> {
public:
  using Base = Ztls::Server<ServerHub>;
  using Link = SrvLink<App>;
  using StopFn = ZmFn<void(bool), ZmFnHeapID<"Zhttp.H2">>;
  using StopFns = ZtArray<StopFn,
    ZtArrayHeapID<"Zhttp.H2">>;

  App *user() { return static_cast<App *>(this); }
  const App *user() const { return static_cast<const App *>(this); }

  bool init(const HubConfig &hub, const H2Config &config) {
    if (!TLS_::valid(config)) return false;
    m_config = config;
    return Base::init(TLS_::serverParams(hub, config));
  }
  ZiIP localIP() const { return user()->localIP(); }
  unsigned localPort() const { return user()->localPort(); }
  unsigned nAccepts() const { return 8; }
  void listening(const ZiListenInfo &info) { user()->listening(info); }
  void listenFailed(bool transient) { user()->listenFailed(transient); }
  ZiConnection *accepted(const ZiCxnInfo &ci) {
    if (!user()->admit(ci)) return nullptr;
    return new typename Link::Cxn(
      new Link{this, ci, m_config}, ci);
  }
  bool start() {
    if (!Base::start()) return false;
    Base::listen();
    return true;
  }
  template <typename Done>
  void start(Done &&done) {
    Base::start([this, done = ZuFwd<Done>(done)](bool ok) mutable {
      if (ok) Base::listen();
      done(ok);
    });
  }
  void stopAccepting() { Base::stopListening(); }
  template <typename Done>
  void drain(Done &&done) {
    this->rxRun([this, done = ZuFwd<Done>(done)]() mutable {
      Base::allLinks_({this, [](ServerHub *, Ztc::Link *link) {
	static_cast<Link *>(link)->drain();
      }});
      done();
    });
  }
  bool stop() {
    return ZmBlock<bool>{}(
      [this](auto wake) { stop(StopFn{ZuMv(wake)}); });
  }
  template <typename Done>
  void stop(Done &&done) {
    StopFn fn{ZuFwd<Done>(done)};
    Base::stopListening();
    this->rxRun([this, fn = ZuMv(fn)]() mutable {
      m_stopFns.push(ZuMv(fn));
      if (m_stopping) return;
      m_stopping = true;
      this->rxRun([this]() {
	m_stopPending = 0;
	Base::allLinks_({this, [](ServerHub *hub, Ztc::Link *link) {
	  ++hub->m_stopPending;
	  static_cast<Link *>(link)->beginStop();
	}});
	if (!m_stopPending) stopDrain_();
      });
    });
  }
  void linkDown() {
    if (m_stopping && m_stopPending && !--m_stopPending)
      stopDrain_();
  }
  void final() {
    ZmAssert(!m_stopPending);
    ZmAssert(!m_stopFns);
    Base::final();
  }

private:
  void stopDrain_() {
    this->rxRun([this]() {
      this->txRun([this]() {
	this->rxRun([this]() {
	  Base::stop([this](bool ok) {
	    this->rxRun([this, ok]() { stopped_(ok); });
	  });
	});
      });
    });
  }
  void stopped_(bool ok) {
    auto fns = ZuMv(m_stopFns);
    m_stopFns.init_();
    m_stopPending = 0;
    m_stopping = false;
    for (auto &fn: fns) {
      fn(ok);
      fn = {};
    }
  }

  H2Config	m_config;
  StopFns	m_stopFns;
  unsigned	m_stopPending = 0;
  bool		m_stopping = false;
};

template <typename App>
class SrvLink :
  public Ztls::SrvLink<ServerHub<App>, SrvLink<App>,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>,
  public H2_::Wire<SrvLink<App>, typename App::H2Link> {
public:
  using Hub = ServerHub<App>;
  using H1Logical = typename App::H1Link;
  using H2Logical = typename App::H2Link;
  using Base = Ztls::SrvLink<Hub, SrvLink,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>;
  using Wire = H2_::Wire<SrvLink, H2Logical>;
  using Cxn = typename Base::Cxn;
  using Active = ZtArray<ZmRef<H2Logical>,
    ZtArrayHeapID<"Zhttp.H2">>;

  SrvLink(
    Hub *app, const ZiCxnInfo &ci, const H2Config &config) :
      Base{app}, m_policy{config.policy()}
  {
    m_remote = ci.remoteIP;
    Wire::initWire(true, config);
  }
  ~SrvLink() {
    Wire::finalWire();
  }

  void connected(Ztls::Connected info) {
    m_version = TLS_::version(info.alpn, m_policy);
    switch (m_version) {
      case Version::H1:
	m_h1 = new H1Logical{
	  this->app()->user(), this, ZuCSpan{m_remote}};
	m_h1->connected_(ProfileTraits<H1TLS>::apply({
	  .alpn = info.alpn,
	  .version = uint32_t(info.version),
	  .transport = Transport::TLS,
	  .secure = true
	}));
	break;
      case Version::H2:
	Wire::sendInitial();
	break;
      default:
	Base::disconnect();
	break;
    }
  }
  void h2SettingsReceived() { }
  void disconnected(bool peer) {
    if (m_down) return;
    m_down = true;
    Wire::stopWire();
    if (m_h1) {
      m_h1->disconnected_(peer);
      m_h1 = nullptr;
      down_();
      return;
    }
    if (m_version != Version::H2) {
      down_();
      return;
    }
    Active active;
    Wire::allStreams([&active](auto &entry) {
      if (!entry.notified) active.push(entry.logical);
    });
    Wire::clearStreams([
      link = ZmMkRef(this), active = ZuMv(active), peer
    ]() mutable {
      for (auto &logical: active) logical->disconnected_(peer);
      link->down_();
    });
  }
  int process(Ztls::RxStream &rx) {
    switch (m_version) {
      case Version::H1: return m_h1 ? m_h1->process_(rx) : -1;
      case Version::H2: return Wire::process(rx);
      default: return -1;
    }
  }
  auto txStream() { return Base::txStream(); }
  auto directTxStream() { return Base::txStream_(); }
  auto logicalTx(uint32_t id) {
    return H2_::HeaderBlock<SrvLink>{
      *this, Wire::encoder(), id, Wire::peerFrameSize()};
  }
  void disconnectNative() { Base::disconnect(); }
  void drain() {
    if (m_version != Version::H2 || m_draining || m_down) return;
    m_draining = true;
    Wire::graceful();
  }

  H2_::Stream<H2Logical> *h2OpenPeer(uint32_t id) {
    if (m_draining || !Wire::canOpenPeerStream(id)) return nullptr;
    ZmRef<H2Logical> logical = new H2Logical{
      this->app()->user(), this, id, ZuCSpan{m_remote}};
    auto entry = Wire::openPeerStream(id, logical);
    if (!entry) return nullptr;
    logical->connected_(ProfileTraits<H2TLS>::apply({
      .alpn = "h2",
      .version = uint32_t(13),
      .transport = Transport::TLS,
      .secure = true
    }));
    return entry;
  }
  bool h2Closed(uint32_t id) const { return Wire::streamClosed(id); }
  H2::Error::T h2OpenError(uint32_t id) {
    if (h2Closed(id)) return H2::Error::StreamClosed;
    if (Wire::peerStreamIdle(id)) {
      Wire::refusePeerStream(id);
      return H2::Error::RefusedStream;
    }
    return H2::Error::ProtocolError;
  }
  void h2LocalEnd(uint32_t id) {
    if (auto entry = Wire::h2Stream(id)) {
      entry->localEnd = true;
      if (entry->remoteEnd) closeLater_(id, false);
    }
  }
  void h2RemoteEnd(uint32_t id) {
    Wire::peerProcessed(id);
    if (auto entry = Wire::h2Stream(id)) {
      entry->remoteEnd = true;
      if (entry->localEnd) closeLater_(id, true);
    }
  }
  void h2ResetLogical(uint32_t id, H2::Error::T) {
    notify_(id, true);
  }
  void h2Cancel(uint32_t id) {
    rst_(id, H2::Error::Cancel);
    notify_(id, false);
  }
  void h2Goaway_(uint32_t, H2::Error::T) { m_draining = true; }
  void h2PeerSetting(uint16_t key, uint32_t value) {
    if (key == H2::Setting::InitialWindowSize &&
	!Wire::peerInitialWindow(value))
      this->h2Error(H2::Error::FlowControlError);
  }
  void beginStop() {
    if (m_stopping || m_down) return;
    m_stopping = true;
    m_draining = true;
    Wire::stopWire();
    if (m_version == Version::H2) Wire::graceful();
    Base::disconnect();
  }

private:
  void closeLater_(uint32_t id, bool peer) {
    auto entry = Wire::h2Stream(id);
    if (!entry || entry->closing) return;
    entry->closing = true;
    this->app()->rxRun([
      link = this, id, peer]() {
      link->notify_(id, peer);
    });
  }
  void rst_(uint32_t id, H2::Error::T error) {
    auto tx = Base::txStream();
    H2_::StreamBytes<decltype(tx)> sink{tx};
    H2::putHeader(sink, {
      .length = 4, .streamID = id, .type = H2::FrameType::RSTStream
    });
    H2::putUInt32(sink, error);
    tx.flush();
  }
  void notify_(uint32_t id, bool peer) {
    auto entry = Wire::h2Stream(id);
    if (!entry || entry->notified) return;
    entry->notified = true;
    auto logical = entry->logical;
    Wire::removeStream(id, [logical = ZuMv(logical), peer]() mutable {
      logical->disconnected_(peer);
    });
  }
  void down_() {
    this->app()->user()->release();
    this->app()->linkDown();
  }

  ZmRef<H1Logical>	m_h1;
  EndpointString	m_remote;
  int8_t		m_policy = H2Policy::Force;
  int8_t		m_version = -1;
  bool			m_draining = false;
  bool			m_down = false;
  bool			m_stopping = false;
};

} // namespace TLS_

template <typename App>
class Server<App, H2TLS> : public H2_::ServerHub<App> {
public:
  using Base = H2_::ServerHub<App>;
  enum { TLS = 1, Multiplexed = 1 };
  using Base::init;
  using Base::start;

  bool admit(const ZiCxnInfo &) { return true; }
  void release() { }
  template <typename Link>
  void connected(Link &, ConnectedInfo) { }
  template <typename Link>
  void disconnected(Link &, bool) { }
};

} // namespace Zhttp

#endif /* ZhttpH2Hub_HH */
