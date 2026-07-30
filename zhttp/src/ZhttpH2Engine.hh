//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP/2 physical TLS sessions and application logical-link adapters

#ifndef ZhttpH2Engine_HH
#define ZhttpH2Engine_HH

#ifndef Zhttp_HH
#define Zhttp_CORE_ONLY
#include <zlib/Zhttp.hh>
#undef Zhttp_CORE_ONLY
#endif

#include <zlib/ZmBlock.hh>
#include <zlib/ZmContext.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmScheduler.hh>

#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpServer.hh>

namespace Zhttp {

namespace H2_ {

using namespace H2;

enum {
  // Bounds one scheduler turn while amortizing cross-shard post overhead.
  FrameDrainBatch = 64,
  RequeueBatch = 64
};

struct EventType {
  enum T : uint8_t {
    Begin, Field, EndHeaders, Data, Reset
  };
};

struct Event {
  EventType::T	type = EventType::Begin;
  ZuCSpan	name;
  ZuCSpan	value;
  Error::T	error = Error::NoError;
  bool		trailers = false;
  bool		endStream = false;
};

struct EventRx {
  template <typename Parser>
  auto process(Parser &parser) {
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
      case EventType::Data:
	parser.data(ZuBSpan{event.value}, event.endStream);
	break;
      case EventType::Reset:
	parser.cancel();
	break;
    }
    return parser.state();
  }

  Event event;
};

struct CountBytes {
  void push(uint8_t) { ++m_length; }
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
  void sendBuf_(ZmRef<ZiIOBuf> buf, bool) {
    if (m_frames)
      m_frames->push(ZuMv(buf));
    else
      m_native->sendFrame(m_streamID, ZuMv(buf));
  }

private:
  Native	*m_native;
  HeaderFrames	*m_frames = nullptr;
  uint32_t	m_streamID = 0;
};

template <typename Native>
class HeaderBlock {
  using FrameTx = FrameStream<Native>;

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
    m_frames.length(0);
    m_updates = m_encoder.updates();
    m_first = true;
    m_open = true;
    m_endStream = endStream;
  }
  void field(ZuCSpan name, ZuCSpan value) {
    field_(Compression::FieldView{name, value});
  }
  void field(
    ZuCSpan name, ZuCSpan value1, char separator, ZuCSpan value2) {
    field_(Compression::FieldView{name, value1, separator, value2});
  }
  void endHeaders(bool) {
    if (!m_open) return;
    if (m_first && m_updates.count) {
      CountBytes count;
      if (m_encoder.emit(count, m_updates) < 0) return;
      Bytes bytes{
	m_native, m_streamID, count.length(), m_frameSize,
	true, m_endStream, m_frames};
      if (m_encoder.emit(bytes, m_updates) < 0) return;
      bytes.flush();
      m_encoder.commit(m_updates);
      m_updates = {};
      m_first = false;
    }
    FrameTx tx{m_native, m_streamID, &m_frames};
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
    m_native.sendHeaders(m_streamID, ZuMv(m_frames), m_endStream);
  }
  auto body() { return DataStream<HeaderBlock>{*this, m_streamID}; }
  auto body(uint64_t length) {
    return DataStream<HeaderBlock>{*this, m_streamID, length};
  }
  void end() {
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
  void sendBuf_(ZmRef<ZiIOBuf> buf, bool) {
    m_native.sendData(m_streamID, ZuMv(buf));
  }

private:
  void field_(Compression::FieldView field) {
    auto plan = m_encoder.plan(field);
    CountBytes count;
    if ((m_first && m_encoder.emit(count, m_updates) < 0) ||
	m_encoder.emit(count, plan) < 0)
      return;
    Bytes bytes{
      m_native, m_streamID, count.length(), m_frameSize,
      m_first, m_endStream, m_frames};
    if ((m_first && m_encoder.emit(bytes, m_updates) < 0) ||
	m_encoder.emit(bytes, plan) < 0)
      return;
    bytes.flush();
    if (m_first) {
      m_encoder.commit(m_updates);
      m_updates = {};
    }
    m_encoder.commit(plan);
    m_first = false;
  }

  Native	&m_native;
  HPackEncoder	&m_encoder;
  HPackUpdates	m_updates;
  uint32_t	m_streamID = 0;
  uint32_t	m_frameSize = DefltFrameSize;
  HeaderFrames	m_frames;
  bool		m_first = false;
  bool		m_open = false;
  bool		m_endStream = false;
};

template <typename Logical>
struct LogicalEntry {
  LogicalEntry() = default;
  LogicalEntry(uint32_t id_, ZmRef<Logical> logical_) :
    id{id_}, logical{ZuMv(logical_)} { }

  uint32_t	id = 0;
  ZmRef<Logical> logical;
  bool		begin = false;
  bool		finalHeaders = false;
  bool		localEnd = false;
  bool		localEndQueued = false;
  bool		remoteEnd = false;
  bool		notified = false;
  bool		closing = false;
  bool		flowError = false;
  int64_t	rxWindow = DefltWindow;
  int64_t	txWindowHint = DefltWindow;
};

template <typename Logical>
inline uint32_t LogicalEntry_IDAxor(const LogicalEntry<Logical> &entry)
{
  return entry.id;
}

template <typename Logical>
using LogicalHash = ZmHash<LogicalEntry<Logical>,
  ZmHashNode<LogicalEntry<Logical>,
    ZmHashKey<LogicalEntry_IDAxor<Logical>,
      ZmHashLock<ZmNoLock,
	ZmHashHeapID<"Zhttp.H2">>>>>;

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
  using Entries = LogicalHash<Logical>;

public:
  bool initWire(bool server, const H2Config &config) {
    m_server = server;
    m_config = config;
    m_frameAdmission.init(config.maxQueuedFrames());
    if (!Base::init(server, config.maxFrameSize()) ||
	!m_decoder.init(
	  config.hpackRxCapacity(), config.maxHeaderListSize()) ||
	!m_encoder.init(config.hpackTxCapacity()))
      return false;
    m_entries = new Entries;
    m_txWindows = new TxWindowHash;
    return true;
  }
  void finalWire() {
    ZmAssert(!count());
    ZmAssert(!m_pendingFrames);
    ZmAssert(!m_txWindows || !m_txWindows->count_());
    ZmAssert(!m_frameAdmission.count());
    ZmAssert(!m_frameDrainPosted);
    stopWire();
    m_decoder.final();
    m_encoder.final();
    m_entries = nullptr;
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

  LogicalEntry<Logical> *entry(uint32_t id) const {
    return m_entries ? m_entries->findPtr(id) : nullptr;
  }
  LogicalEntry<Logical> *add(uint32_t id, ZmRef<Logical> logical) {
    if (!m_entries || m_entries->findPtr(id)) return nullptr;
    auto entry_ = m_entries->add(
      LogicalEntry<Logical>{id, ZuMv(logical)});
    if (entry_) {
      entry_->rxWindow = m_config.initialWindowSize();
      entry_->txWindowHint = m_peerInitialWindow;
    }
    return entry_;
  }
  template <typename Done>
  void remove(uint32_t id, Done &&done) {
    if (!m_entries) {
      ZuFwd<Done>(done)();
      return;
    }
    auto entry_ = m_entries->findPtr(id);
    if (entry_) m_entries->delNode(
      static_cast<typename Entries::Node *>(entry_));
    auto session = impl_();
    impl_()->app()->txRun([
      session, id, done = ZuFwd<Done>(done)]() mutable {
      session->removeFramesTx_(id);
      session->app()->rxRun(
	[done = ZuMv(done)]() mutable { done(); });
    });
  }
  template <typename Done>
  void clear(Done &&done) {
    if (m_entries) m_entries->clean();
    auto session = impl_();
    impl_()->app()->txRun([
      session, done = ZuFwd<Done>(done)]() mutable {
      session->clearFramesTx_();
      session->app()->rxRun(
	[done = ZuMv(done)]() mutable { done(); });
    });
  }
  template <typename L>
  void all(L &&l) {
    if (!m_entries) return;
    auto i = m_entries->iter();
    while (auto entry_ = i()) l(*entry_);
  }
  unsigned count() const { return m_entries ? m_entries->count_() : 0; }

  unsigned dataMaxSize(uint32_t id) {
    if (impl_()->app()->txInvoked()) return dataMaxSizeTx_(id);
    auto tx = impl_()->txStream();
    uint32_t length = peerFrameSize();
    if (auto entry_ = entry(id)) {
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
  void sendData(uint32_t id, ZmRef<ZiIOBuf> buf) {
    if (impl_()->app()->txInvoked()) {
      sendDataTx_(id, ZuMv(buf));
      return;
    }
    if (m_stopping || !buf || buf->length < 9) return;
    auto entry_ = entry(id);
    if (!entry_ || entry_->localEndQueued) return;
    uint32_t length = buf->length - 9;
    if (length > peerFrameSize()) {
      impl_()->h2Cancel(id);
      return;
    }
    sendFrame_(id, length, false, ZuMv(buf));
  }
  void sendFrame(uint32_t id, ZmRef<ZiIOBuf> buf) {
    if (impl_()->app()->txInvoked()) {
      sendFrameDirectTx_(id, 0, false, ZuMv(buf));
      return;
    }
    if (m_stopping || !buf || !buf->length) return;
    sendFrame_(id, 0, false, ZuMv(buf));
  }
  void sendHeaders(
    uint32_t id, HeaderFrames frames, bool endStream) {
    if (impl_()->app()->txInvoked()) {
      sendHeadersDirectTx_(id, ZuMv(frames), endStream);
      return;
    }
    if (m_stopping || !frames) return;
    auto entry_ = entry(id);
    if (!entry_ || entry_->localEndQueued) return;
    unsigned admitted = 0;
    while (admitted < frames.length() && m_frameAdmission.push())
      ++admitted;
    if (admitted != frames.length()) {
      if (admitted) m_frameAdmission.pop(admitted);
      impl_()->h2Cancel(id);
      return;
    }
    if (endStream) entry_->localEndQueued = true;
    auto session = impl_();
    impl_()->app()->txRun([
      session, id, endStream, frames = ZuMv(frames)
    ]() mutable {
      session->sendHeadersTx_(id, ZuMv(frames), endStream);
    });
  }
  void endHeaders(uint32_t id) {
    if (impl_()->app()->txInvoked()) {
      endTx_(id);
      return;
    }
    if (m_stopping) return;
    auto entry_ = entry(id);
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
    auto entry_ = entry(id);
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
    int64_t n = rx.consume(
      [this](ZuBSpan span) -> int64_t { return Base::process(span); },
      [](ZuBSpan) { });
    return int(n);
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
    auto session = impl_();
    impl_()->app()->txRun([
      session, hpackCapacity, frameSize, extendedConnect]() {
      session->peerSettingsTx_(
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
	  auto entry_ = entry(id);
	  if (!entry_) return;
	  if (!entry_->begin) {
	    entry_->begin = true;
	    if (!dispatch_(id, Event{
	      .type = EventType::Begin,
	      .trailers = entry_->finalHeaders
	    })) return;
	    entry_ = entry(id);
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
      entry_ = entry(id);
      if (!entry_) return;
    }
    if (!dispatch_(id, Event{
      .type = EventType::EndHeaders,
      .endStream = endStream
    })) return;
    entry_ = entry(id);
    if (!entry_) return;
    entry_->begin = false;
    if (m_server) entry_->finalHeaders = true;
    if (endStream) {
      entry_->remoteEnd = true;
      impl_()->h2RemoteEnd(id);
    }
  }
  bool h2DataBegin(uint32_t id, uint32_t length) {
    auto entry_ = entry(id);
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
  void h2Data(uint32_t id, ZuBSpan span) {
    auto entry_ = entry(id);
    if (!entry_ || entry_->flowError) return;
    dispatch_(id, Event{
      .type = EventType::Data,
      .value = ZuCSpan{span}
    });
  }
  void h2DataEnd(uint32_t id, bool endStream, uint32_t length) {
    auto entry_ = entry(id);
    if (!entry_) {
      m_rxWindow += length;
      sendWindowUpdate_(0, length);
      if (!impl_()->h2Closed(id))
	h2Error(Error::ProtocolError);
      return;
    }
    m_rxWindow += length;
    sendWindowUpdate_(0, length);
    if (entry_->flowError) {
      impl_()->h2ResetLogical(id, Error::FlowControlError);
      return;
    }
    entry_->rxWindow += length;
    sendWindowUpdate_(id, length);
    if (!endStream) return;
    if (!dispatch_(id, Event{
      .type = EventType::Data,
      .endStream = true
    })) return;
    entry_ = entry(id);
    if (!entry_) return;
    entry_->remoteEnd = true;
    impl_()->h2RemoteEnd(id);
  }
  void h2Reset(uint32_t id, Error::T error) {
    if (entry(id)) {
      dispatch_(id, Event{
	.type = EventType::Reset,
	.error = error
      });
      if (entry(id)) impl_()->h2ResetLogical(id, error);
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
      auto entry_ = entry(id);
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
    auto session = impl_();
    impl_()->app()->txRun([
      session, id, value]() mutable {
      session->windowUpdateTx_(id, value);
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

  void resetBlock() {
    m_decoding = false;
    m_discardStream = 0;
  }

  bool peerInitialWindow(uint32_t value) {
    int64_t delta = int64_t(value) - m_peerInitialWindow;
    {
      bool valid = true;
      all([delta, &valid](auto &entry_) {
	int64_t window = entry_.txWindowHint + delta;
	if (window > MaxWindow || window < -int64_t(MaxWindow))
	  valid = false;
      });
      if (!valid) return false;
    }
    all([delta](auto &entry_) { entry_.txWindowHint += delta; });
    m_peerInitialWindow = value;
    auto session = impl_();
    impl_()->app()->txRun([
      session, value]() mutable {
      session->initialWindowTx_(value);
    });
    return true;
  }

  void peerSettingsTx_(
    uint32_t hpackCapacity, uint32_t frameSize, bool extendedConnect) {
    ZiAssert(impl_()->app()->txInvoked(), "Zhttp", (),
      "H2 peer settings outside Tx thread", return);
    if (!m_encoder.peerCapacity(hpackCapacity)) {
      auto session = impl_();
      impl_()->app()->rxRun([session]() {
	session->h2Error(Error::CompressionError);
      });
      return;
    }
    m_txFrameSize = frameSize;
    m_txExtendedConnect = extendedConnect;
  }

private:
  LogicalEntry<Logical> *begin_(uint32_t id) {
    auto entry_ = entry(id);
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
    auto entry_ = entry(id);
    if (!entry_) return false;
    auto logical = entry_->logical;
    EventRx rx{event};
    if (logical->process_(rx) < 0 && entry(id))
      impl_()->h2Cancel(id);
    return entry(id);
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
    putGoaway(sink, m_lastPeer, error);
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
  void sendDataTx_(uint32_t id, ZmRef<ZiIOBuf> buf) {
    ZiAssert(impl_()->app()->txInvoked(), "Zhttp", (),
      "H2 DATA queue outside Tx thread", return);
    if (!buf || buf->length < 9) return;
    uint32_t length = buf->length - 9;
    if (length > m_txFrameSize) {
      txError_(id);
      return;
    }
    sendFrameDirectTx_(id, length, false, ZuMv(buf));
  }
  void sendFrameDirectTx_(
    uint32_t id, uint32_t length, bool endStream,
    ZmRef<ZiIOBuf> buf) {
    ZiAssert(impl_()->app()->txInvoked(), "Zhttp", (),
      "H2 direct frame queue outside Tx thread", return);
    auto entry_ = txWindow_(id);
    if (!entry_ || entry_->localEndQueued) return;
    if (!m_frameAdmission.push()) {
      txError_(id);
      return;
    }
    if (endStream) entry_->localEndQueued = true;
    sendFrameTx_(id, length, endStream, ZuMv(buf));
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
      txError_(id);
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
  void sendFrame_(
    uint32_t id, uint32_t length, bool endStream,
    ZmRef<ZiIOBuf> buf) {
    if (!m_frameAdmission.push()) {
      impl_()->h2Cancel(id);
      return;
    }
    auto session = impl_();
    impl_()->app()->txRun([
      session, id, length, endStream, buf = ZuMv(buf)
    ]() mutable {
      session->sendFrameTx_(id, length, endStream, ZuMv(buf));
    });
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
    impl_()->app()->txRun([session = impl_()]() {
      session->drainFramesTx_();
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
	  session = impl_(), id]() {
	  session->h2LocalEnd(id);
	});
      if (m_headerStream && inspected == FrameDrainBatch)
	inspected = 0;
    }
    if (m_frameScan && m_pendingFrames) {
      m_frameDrainPosted = true;
      impl_()->app()->txRun([session = impl_()]() {
	session->drainFramesTx_();
      });
    }
  }
  void txError_(uint32_t id) {
    impl_()->app()->rxRun([
      session = impl_(), id]() {
      if (!id)
	session->h2Error(Error::FlowControlError);
      else {
	session->sendReset_(id, Error::FlowControlError);
	session->h2ResetLogical(id, Error::FlowControlError);
      }
    });
  }

protected:
  void peerProcessed(uint32_t id) {
    if (m_server && id > m_lastPeer) m_lastPeer = id;
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
  ZmRef<Entries>	m_entries;
  ZmScheduler::Timer	m_settingsTimer;
  HPackDecoder		m_decoder;
  uint32_t		m_lastPeer = 0;
  uint32_t		m_peerInitialWindow = DefltWindow;
  uint32_t		m_discardStream = 0;
  int64_t		m_rxWindow = DefltWindow;
  int64_t		m_txWindowHint = DefltWindow;
  bool			m_decoding = false;
  bool			m_errorSent = false;
  bool			m_stopping = false;

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

template <typename App> class ClientEngine;
template <typename App> class ClientSession;

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
class ClientEngine : public Ztls::Client<ClientEngine<App>> {
public:
  using Base = Ztls::Client<ClientEngine>;
  using Link = ClientSession<App>;
  using StopFn = ZmFn<void(bool), ZmFnHeapID<"Zhttp.H2">>;
  using Slots = ZtArray<ClientSlot,
    ZtArrayHeapID<"Zhttp.H2">>;
  using StopFns = ZtArray<StopFn,
    ZtArrayHeapID<"Zhttp.H2">>;

  App *user() { return static_cast<App *>(this); }

  ClientEngine() : m_pool{new ClientPoolHash} { }

  bool init(const EngineConfig &engine, const H2Config &config) {
    if (!TLS_::valid(config) || config.policy() != H2Policy::Force)
      return false;
    m_config = config;
    auto params = TLS_::clientParams(engine, config);
    return Base::init(ZuMv(params));
  }

  template <typename Logical>
  void connect(Logical *logical, Ztls::Host host, uint16_t port) {
    using Session = ClientSession<App>;
    ZmRef<Logical> logical_ = ZmMkRef(logical);
    this->rxInvoke([
      this, logical = ZuMv(logical_), host = ZuMv(host), port
    ]() mutable {
      ClientPoolKey key{host, port};
      auto poolEntry = m_pool->findPtr(key);
      ZmRef<Session> session;
      if (poolEntry) {
	session = poolEntry->owner.object<Session>();
	if (!session->available()) session = nullptr;
      }
      if (!session) {
	session = new Session{this, host, port, m_config};
	m_slots.push(ClientSlot{
	  .owner = session,
	  .close = [](void *ptr) {
	    static_cast<Session *>(ptr)->beginStop();
	  },
	  .available = [](void *ptr) {
	    return static_cast<Session *>(ptr)->available();
	  },
	  .down = [](void *ptr) {
	    return static_cast<Session *>(ptr)->isDown();
	  },
	  .host = host,
	  .port = port
	});
	if (poolEntry)
	  poolEntry->owner = session;
	else
	  m_pool->add(ClientPoolEntry{ZuMv(key), session});
	session->add(ZuMv(logical));
	session->connect(ZuMv(host), port);
	return;
      }
      session->add(ZuMv(logical));
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
  void sessionDown(ClientSession<App> *session) {
    if (auto entry = m_pool->findPtr(
	ClientPoolKey{session->host(), session->port()}))
      if (entry->owner.object<ClientSession<App>>() == session)
	m_pool->delNode(
	  static_cast<ClientPoolHash::Node *>(entry));
    for (unsigned i = 0; i < m_slots.length(); ++i)
      if (m_slots[i].owner.object<ClientSession<App>>() == session) {
	m_slots.splice(i, 1);
	break;
      }
    if (m_stopping && m_stopPending && !--m_stopPending)
      stopDrain_();
  }
  void final() {
    for (auto &slot: m_slots)
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
class ClientSession :
  public Ztls::CliLink<ClientEngine<App>, ClientSession<App>,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>,
  public Wire<ClientSession<App>, typename App::Link> {
public:
  using Engine = ClientEngine<App>;
  using Logical = typename App::Link;
  using Base = Ztls::CliLink<Engine, ClientSession,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>;
  using Wire_ = Wire<ClientSession, Logical>;
  using Pending = ZtArray<ZmRef<Logical>,
    ZtArrayHeapID<"Zhttp.H2">>;
  using Active = ZtArray<ZmRef<Logical>,
    ZtArrayHeapID<"Zhttp.H2">>;
  using StreamIDs = ZtArray<uint32_t,
    ZtArrayHeapID<"Zhttp.H2">>;

  ClientSession(
    Engine *app, Ztls::Host host_, uint16_t port_,
    const H2Config &config) :
      Base{app}, m_host{ZuMv(host_)}, m_port{port_}
  {
    Wire_::initWire(false, config);
    m_pendingMax = config.maxPending();
    m_session.init(
      false, config.maxConcurrentStreams(),
      config.maxConcurrentStreams(), config.maxPending());
    m_session.localIDMax(config.maxStreamID());
  }
  ~ClientSession() {
    m_session.final();
    Wire_::finalWire();
  }

  const Ztls::Host &host() const { return m_host; }
  uint16_t port() const { return m_port; }
  void add(ZmRef<Logical> logical) {
    if (m_pending.length() >= m_pendingMax &&
	(!m_ready || !m_session.canOpenLocal())) {
      logical->connectFailed_(false);
      return;
    }
    logical->session(this);
    if (!m_ready || m_pending || !m_session.canOpenLocal()) {
      m_pending.push(ZuMv(logical));
      return;
    }
    openNow_(ZuMv(logical));
  }
  bool available() const {
    return !m_down && !m_draining &&
      !m_session.localExhausted() &&
      (m_ready && m_session.canOpenLocal() ||
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
    Wire_::all([&active](auto &entry) {
      if (!entry.notified) active.push(entry.logical);
    });
    m_session.final();
    Wire_::clear([
      session = ZmMkRef(this), active = ZuMv(active), peer
    ]() mutable {
      for (auto &logical: active) {
	if (logical->result() == ResultCode::OK)
	  logical->result_(ResultCode::Indeterminate);
	logical->disconnected_(peer);
      }
      for (auto &logical: session->m_pending) {
	if (logical->result() == ResultCode::OK)
	  logical->result_(ResultCode::Unprocessed);
	logical->connectFailed_(false);
      }
      session->m_pending.length(0);
      session->app()->sessionDown(session);
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
    m_session.final();
    Base::disconnect();
    this->app()->sessionDown(this);
  }
  int process(Ztls::RxStream &rx) { return Wire_::process(rx); }
  auto txStream() { return Base::txStream(); }
  auto directTxStream() { return Base::txStream_(); }

  void openNow_(ZmRef<Logical> logical) {
    auto stream = m_session.openLocal();
    if (!stream) {
      logical->connectFailed_(false);
      logical->session({});
      return;
    }
    auto entry = Wire_::add(stream->id, logical);
    if (!entry) {
      logical->connectFailed_(false);
      logical->session({});
      return;
    }
    logical->stream(stream->id);
    logical->connected_(ProfileTraits<H2TLS>::apply({
      .alpn = "h2",
      .version = uint32_t(13),
      .transport = Transport::TLS,
      .secure = true
    }));
  }
  auto logicalTx(uint32_t id) {
    return HeaderBlock<ClientSession>{
      *this, Wire_::encoder(), id, Wire_::peerFrameSize()};
  }
  void close(Logical *logical, uint32_t id) {
    this->app()->rxInvoke([
      session = this, logical = ZmMkRef(logical), id
    ]() mutable {
      auto entry = session->entry(id);
      if (!entry || entry->logical.ptr() != logical.ptr()) return;
      session->rst_(id, Error::Cancel);
      session->notify_(id, false);
    });
  }
  void h2LocalEnd(uint32_t id) {
    if (auto entry = Wire_::entry(id)) {
      entry->localEnd = true;
      if (entry->remoteEnd) closeLater_(id, false);
    }
  }
  void h2RemoteEnd(uint32_t id) {
    Wire_::peerProcessed(id);
    if (auto entry = Wire_::entry(id)) {
      entry->remoteEnd = true;
      if (entry->localEnd) closeLater_(id, true);
    }
  }
  void h2ResetLogical(uint32_t id, Error::T) { notify_(id, true); }
  void h2Cancel(uint32_t id) {
    rst_(id, Error::Cancel);
    notify_(id, false);
  }
  auto h2OpenPeer(uint32_t) -> LogicalEntry<Logical> * { return nullptr; }
  bool h2Closed(uint32_t id) const { return m_session.closed(id); }
  Error::T h2OpenError(uint32_t id) const {
    return h2Closed(id) ? Error::StreamClosed : Error::ProtocolError;
  }
  void h2Goaway_(uint32_t last, Error::T) {
    m_draining = true;
    StreamIDs close;
    Wire_::all([last, &close](auto &entry) {
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
	m_session.localMax(value);
	admit_();
	break;
      case Setting::InitialWindowSize:
	if (!Wire_::peerInitialWindow(value) ||
	    !m_session.peerInitialWindow(value))
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
    Wire_::all([](auto &entry) {
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
    while (n < m_pending.length() && m_session.canOpenLocal())
      openNow_(ZuMv(m_pending[n++]));
    if (n) m_pending.splice(0, n);
    if (m_session.localExhausted()) {
      m_draining = true;
      requeue_();
    }
  }
  void requeue_() {
    if (!m_pending || m_requeuePosted) return;
    m_requeuePosted = true;
    this->app()->rxRun([
      session = this]() { session->requeueNow_(); });
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
	session = this]() { session->requeueNow_(); });
    } else {
      m_requeuePosted = false;
    }
  }
  void closeLater_(uint32_t id, bool peer) {
    auto entry = Wire_::entry(id);
    if (!entry || entry->closing) return;
    entry->closing = true;
    this->app()->rxRun([
      session = this, id, peer]() {
      session->notify_(id, peer);
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
    auto entry = Wire_::entry(id);
    if (!entry || entry->notified) return;
    entry->notified = true;
    auto logical = entry->logical;
    m_session.close(id);
    Wire_::remove(id, [
      session = this, logical = ZuMv(logical), peer
    ]() mutable {
      logical->disconnected_(peer);
      session->admit_();
    });
  }

  H2::Session	m_session;
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

template <typename App> class ServerEngine;
template <typename App> class ServerSession;

template <typename App>
class ServerEngine : public Ztls::Server<ServerEngine<App>> {
public:
  using Base = Ztls::Server<ServerEngine>;
  using Session = ServerSession<App>;
  using Link = Session;
  using StopFn = ZmFn<void(bool), ZmFnHeapID<"Zhttp.H2">>;
  using StopFns = ZtArray<StopFn,
    ZtArrayHeapID<"Zhttp.H2">>;

  App *user() { return static_cast<App *>(this); }
  const App *user() const { return static_cast<const App *>(this); }

  bool init(const EngineConfig &engine, const H2Config &config) {
    if (!TLS_::valid(config) || config.policy() != H2Policy::Force)
      return false;
    m_config = config;
    auto params = TLS_::serverParams(engine, config);
    return Base::init(ZuMv(params));
  }
  ZiIP localIP() const { return user()->localIP(); }
  unsigned localPort() const { return user()->localPort(); }
  unsigned nAccepts() const { return 8; }
  void listening(const ZiListenInfo &info) { user()->listening(info); }
  void listenFailed(bool transient) { user()->listenFailed(transient); }
  ZiConnection *accepted(const ZiCxnInfo &ci) {
    if (!user()->admit(ci)) return nullptr;
    return new typename Session::Cxn(
      new Session{this, ci, m_config}, ci);
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
	Base::allLinks_({this, [](ServerEngine *engine, Ztc::Link *link) {
	  ++engine->m_stopPending;
	  static_cast<Session *>(link)->beginStop();
	}});
	if (!m_stopPending) stopDrain_();
      });
    });
  }
  void sessionDown() {
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
class ServerSession :
  public Ztls::SrvLink<ServerEngine<App>, ServerSession<App>,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>,
  public Wire<ServerSession<App>, typename App::Link> {
public:
  using Engine = ServerEngine<App>;
  using Logical = typename App::Link;
  using Base = Ztls::SrvLink<Engine, ServerSession,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>;
  using Wire_ = Wire<ServerSession, Logical>;
  using Cxn = typename Base::Cxn;
  using Active = ZtArray<ZmRef<Logical>,
    ZtArrayHeapID<"Zhttp.H2">>;

  ServerSession(
    Engine *app, const ZiCxnInfo &ci, const H2Config &config) :
      Base{app}
  {
    m_remote << ci.remoteIP;
    Wire_::initWire(true, config);
    m_session.init(
      true, 0, config.maxConcurrentStreams(),
      config.maxPending());
  }
  ~ServerSession() {
    m_session.final();
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
    Wire_::all([&active](auto &entry) {
      if (!entry.notified) active.push(entry.logical);
    });
    m_session.final();
    Wire_::clear([
      session = ZmMkRef(this), active = ZuMv(active), peer
    ]() mutable {
      for (auto &logical: active) logical->disconnected_(peer);
      if (!session->m_released) {
	session->m_released = true;
	session->app()->user()->release();
      }
      session->app()->sessionDown();
    });
  }
  int process(Ztls::RxStream &rx) { return Wire_::process(rx); }
  auto txStream() { return Base::txStream(); }
  auto directTxStream() { return Base::txStream_(); }
  auto logicalTx(uint32_t id) {
    return HeaderBlock<ServerSession>{
      *this, Wire_::encoder(), id, Wire_::peerFrameSize()};
  }

  LogicalEntry<Logical> *h2OpenPeer(uint32_t id) {
    if (m_draining || !m_session.openPeer(id)) return nullptr;
    ZmRef<Logical> logical = new Logical{
      this->app()->user(), this, id, ZuCSpan{m_remote}};
    auto entry = Wire_::add(id, logical);
    if (!entry) return nullptr;
    logical->connected_(ProfileTraits<H2TLS>::apply({
      .alpn = "h2",
      .version = uint32_t(13),
      .transport = Transport::TLS,
      .secure = true
    }));
    return entry;
  }
  bool h2Closed(uint32_t id) const { return m_session.closed(id); }
  Error::T h2OpenError(uint32_t id) {
    if (h2Closed(id)) return Error::StreamClosed;
    if (m_session.peerIdle(id)) {
      m_session.refusePeer(id);
      return Error::RefusedStream;
    }
    return Error::ProtocolError;
  }
  void h2LocalEnd(uint32_t id) {
    if (auto entry = Wire_::entry(id)) {
      entry->localEnd = true;
      if (entry->remoteEnd) closeLater_(id, false);
    }
  }
  void h2RemoteEnd(uint32_t id) {
    Wire_::peerProcessed(id);
    if (auto entry = Wire_::entry(id)) {
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
	(!Wire_::peerInitialWindow(value) ||
	 !m_session.peerInitialWindow(value)))
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
    auto entry = Wire_::entry(id);
    if (!entry || entry->closing) return;
    entry->closing = true;
    this->app()->rxRun([
      session = this, id, peer]() {
      session->notify_(id, peer);
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
    auto entry = Wire_::entry(id);
    if (!entry || entry->notified) return;
    entry->notified = true;
    auto logical = entry->logical;
    m_session.close(id);
    Wire_::remove(id, [logical = ZuMv(logical), peer]() mutable {
      logical->disconnected_(peer);
    });
  }

  H2::Session	m_session;
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

template <typename App, typename Impl, typename Native>
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
  auto txStream() { return m_session->logicalTx(m_streamID); }
  template <typename Parser, typename Rx>
  auto receive(Parser &parser, Rx &rx) { return parser.process(rx); }
  template <typename Builder>
  auto transmit(Builder &) { return txStream(); }
  void finish() { }
  bool active() const { return m_session && m_streamID; }
  void disconnect() {
    m_cancelled = true;
    if (m_session && m_streamID)
      m_session->close(impl(), m_streamID);
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
    m_session = nullptr;
    m_streamID = 0;
    if (m_connected)
      m_app->disconnected(*impl(), peer);
    else if (!m_failed)
      connectFailed_(false);
  }
  void connectFailed_(bool transient) {
    if (m_failed) return;
    m_failed = true;
    m_session = nullptr;
    m_streamID = 0;
    m_app->connectFailed(*impl(), transient);
  }
  int8_t result() const { return m_result; }
  void result_(int8_t result) { m_result = result; }
  template <typename Rx>
  int process_(Rx &rx) { return m_app->process(*impl(), rx); }
  void session(Native *session) { m_session = session; }
  void stream(uint32_t id) { m_streamID = id; }
  template <typename State> void responseHeadersParsed(State *) { }
  template <typename State> void responseBodyBytes(State *) { }

private:
  App		*m_app = nullptr;
  Native	*m_session = nullptr;
  uint32_t	m_streamID = 0;
  bool		m_connected = false;
  bool		m_failed = false;
  bool		m_cancelled = false;
  int8_t	m_result = ResultCode::OK;
};

} // namespace H2_

template <typename App, typename Impl>
class ClientLink<App, Impl, H2TLS> :
  public H2_::ClientLogical<App, Impl, H2_::ClientSession<App>> {
  using Base =
    H2_::ClientLogical<App, Impl, H2_::ClientSession<App>>;

public:
  using Base::Base;
};

template <typename App>
class Client<App, H2TLS> : public H2_::ClientEngine<App> {
public:
  using Base = H2_::ClientEngine<App>;
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

template <typename App, typename Impl, typename Session, typename Native>
class ServerLogical : public ZmObject, public LogicalStream<Impl> {
public:
  enum { TLS = 1, Multiplexed = 1 };

  ServerLogical(
    App *app, Native *native, uint32_t streamID, ZuCSpan remote) :
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
  Native	*m_native = nullptr;
  uint32_t	m_streamID = 0;
  Session	m_session;
  EndpointString m_remote;
};

} // namespace H2_

template <typename App, typename Impl, typename Session>
class ServerLink<App, Impl, H2TLS, Session> :
  public H2_::ServerLogical<
    App, Impl, Session, H2_::ServerSession<App>> {
  using Base = H2_::ServerLogical<
    App, Impl, Session, H2_::ServerSession<App>>;

public:
  using Base::Base;
};

namespace TLS_ {

template <typename App, typename H1Logical, typename H2Logical>
class ClientEngine;
template <typename App, typename H1Logical, typename H2Logical>
class ClientSession;
template <typename App> class ServerEngine;
template <typename App> class ServerSession;

template <typename App, typename Impl, typename Native>
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
  void session(Native *native) { m_native = native; }
  template <typename State> void responseHeadersParsed(State *) { }
  template <typename State> void responseBodyBytes(State *) { }

private:
  App		*m_app = nullptr;
  Native	*m_native = nullptr;
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
class ClientEngine :
  public Ztls::Client<ClientEngine<App, H1Logical_, H2Logical_>> {
public:
  using Base = Ztls::Client<ClientEngine>;
  using Session = ClientSession<App, H1Logical_, H2Logical_>;
  using Link = Session;
  using H1Logical = H1Logical_;
  using H2Logical = H2Logical_;
  using StopFn = ZmFn<void(bool), ZmFnHeapID<"Zhttp.H2">>;
  using Slots = ZtArray<ZmRef<Session>,
    ZtArrayHeapID<"Zhttp.H2">>;
  using StopFns = ZtArray<StopFn,
    ZtArrayHeapID<"Zhttp.H2">>;

  App *user() { return static_cast<App *>(this); }

  ClientEngine() : m_pool{new H2_::ClientPoolHash} { }

  bool init(const EngineConfig &engine, const H2Config &config) {
    if (!TLS_::valid(config)) return false;
    m_config = config;
    return Base::init(TLS_::clientParams(engine, config));
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
      ZmRef<Session> session;
      if (poolEntry) {
	session = poolEntry->owner.object<Session>();
	if (!session->available()) session = nullptr;
      }
      if (!session) {
	session = new Session{this, host, port, m_config};
	m_slots.push(session);
	if (poolEntry)
	  poolEntry->owner = session;
	else
	  m_pool->add(H2_::ClientPoolEntry{ZuMv(key), session});
	session->add({ZuMv(h1), ZuMv(h2)});
	session->connect(ZuMv(host), port);
	return;
      }
      session->add({ZuMv(h1), ZuMv(h2)});
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
  void sessionDown(Session *session) {
    if (auto entry = m_pool->findPtr(
	H2_::ClientPoolKey{session->host(), session->port()}))
      if (entry->owner.object<Session>() == session)
	m_pool->delNode(
	  static_cast<H2_::ClientPoolHash::Node *>(entry));
    for (unsigned i = 0; i < m_slots.length(); ++i)
      if (m_slots[i].ptr() == session) {
	m_slots.splice(i, 1);
	break;
      }
    if (m_stopping && m_stopPending && !--m_stopPending)
      stopDrain_();
  }
  void goaway(uint32_t) { }
  void final() {
    for (auto &slot: m_slots) ZmAssert(slot->isDown());
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
class ClientSession :
  public Ztls::CliLink<
    ClientEngine<App, H1Logical_, H2Logical_>,
    ClientSession<App, H1Logical_, H2Logical_>,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>,
  public H2_::Wire<
    ClientSession<App, H1Logical_, H2Logical_>, H2Logical_> {
public:
  using Engine = ClientEngine<App, H1Logical_, H2Logical_>;
  using H1Logical = H1Logical_;
  using H2Logical = H2Logical_;
  using Choice = ClientChoice<H1Logical, H2Logical>;
  using Base = Ztls::CliLink<Engine, ClientSession,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>;
  using Wire = H2_::Wire<ClientSession, H2Logical>;
  using Pending = ZtArray<Choice,
    ZtArrayHeapID<"Zhttp.H2">>;
  using Active = ZtArray<ZmRef<H2Logical>,
    ZtArrayHeapID<"Zhttp.H2">>;
  using StreamIDs = ZtArray<uint32_t,
    ZtArrayHeapID<"Zhttp.H2">>;

  ClientSession(
    Engine *app, Ztls::Host host, uint16_t port,
    const H2Config &config) :
      Base{app}, m_host{ZuMv(host)}, m_port{port},
      m_pendingMax{config.maxPending()}, m_policy{config.policy()}
  {
    Wire::initWire(false, config);
    m_h2.init(
      false, config.maxConcurrentStreams(),
      config.maxConcurrentStreams(), config.maxPending());
    m_h2.localIDMax(config.maxStreamID());
  }
  ~ClientSession() {
    m_h2.final();
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
	return !m_h2.localExhausted() &&
	  (m_h2.canOpenLocal() ||
	    m_pending.length() < m_pendingMax);
      default:
	return false;
    }
  }
  void add(Choice choice) {
    if (m_pending.length() >= m_pendingMax &&
	(!m_ready || m_version != Version::H2 ||
	 !m_h2.canOpenLocal())) {
      fail_(choice, false);
      return;
    }
    if (!m_ready || m_pending ||
	m_version == Version::H1 && m_h1 ||
	m_version == Version::H2 && !m_h2.canOpenLocal()) {
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
      Wire::all([&active](auto &entry) {
	if (!entry.notified) active.push(entry.logical);
      });
    m_h2.final();
    Wire::clear([
      session = ZmMkRef(this), active = ZuMv(active), peer
    ]() mutable {
      for (auto &logical: active) {
	if (logical->result() == ResultCode::OK)
	  logical->result_(ResultCode::Indeterminate);
	logical->disconnected_(peer);
      }
      for (auto &choice: session->m_pending)
	session->fail_(choice, false);
      session->m_pending.length(0);
      session->app()->sessionDown(session);
    });
  }
  void connectFailed(bool transient) {
    if (m_down) return;
    m_down = true;
    Wire::stopWire();
    for (auto &choice: m_pending) fail_(choice, transient);
    m_pending.length(0);
    m_h2.final();
    Base::disconnect();
    this->app()->sessionDown(this);
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
    return H2_::HeaderBlock<ClientSession>{
      *this, this->encoder(), id, this->peerFrameSize()};
  }
  void disconnectNative() { Base::disconnect(); }
  template <typename Done>
  void releaseH1(H1Logical *logical, Done &&done) {
    this->app()->rxRun([
      session = this, logical = ZmMkRef(logical),
      done = ZuFwd<Done>(done)
    ]() mutable {
      if (session->m_h1.ptr() == logical.ptr()) {
	session->m_h1->disconnected_(false);
	session->m_h1 = nullptr;
	session->admit_();
      }
      done();
    });
  }

  void close(H2Logical *logical, uint32_t id) {
    this->app()->rxInvoke([
      session = this, logical = ZmMkRef(logical), id
    ]() mutable {
      auto entry = session->entry(id);
      if (!entry || entry->logical.ptr() != logical.ptr()) return;
      session->rst_(id, H2::Error::Cancel);
      session->notify_(id, false);
    });
  }
  void h2LocalEnd(uint32_t id) {
    if (auto entry = Wire::entry(id)) {
      entry->localEnd = true;
      if (entry->remoteEnd) closeLater_(id, false);
    }
  }
  void h2RemoteEnd(uint32_t id) {
    Wire::peerProcessed(id);
    if (auto entry = Wire::entry(id)) {
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
  auto h2OpenPeer(uint32_t) -> H2_::LogicalEntry<H2Logical> * {
    return nullptr;
  }
  bool h2Closed(uint32_t id) const { return m_h2.closed(id); }
  H2::Error::T h2OpenError(uint32_t id) const {
    return h2Closed(id) ?
      H2::Error::StreamClosed : H2::Error::ProtocolError;
  }
  void h2Goaway_(uint32_t last, H2::Error::T) {
    m_draining = true;
    StreamIDs close;
    Wire::all([last, &close](auto &entry) {
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
	m_h2.localMax(value);
	admit_();
	break;
      case H2::Setting::InitialWindowSize:
	if (!Wire::peerInitialWindow(value) ||
	    !m_h2.peerInitialWindow(value))
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
    Wire::all([](auto &entry) {
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
	m_h1->session(this);
	m_h1->connected_(ProfileTraits<H1TLS>::apply({
	  .alpn = "http/1.1",
	  .version = uint32_t(m_tlsVersion),
	  .transport = Transport::TLS,
	  .secure = true
	}));
	break;
      case Version::H2: {
	auto stream = m_h2.openLocal();
	if (!stream) {
	  choice.h2->connectFailed_(false);
	  return;
	}
	auto logical = ZuMv(choice.h2);
	logical->session(this);
	auto entry = Wire::add(stream->id, logical);
	if (!entry) {
	  logical->connectFailed_(false);
	  logical->session({});
	  return;
	}
	logical->stream(stream->id);
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
	while (n < m_pending.length() && m_h2.canOpenLocal())
	  open_(ZuMv(m_pending[n++]));
	break;
    }
    if (n) m_pending.splice(0, n);
    if (m_version == Version::H2 && m_h2.localExhausted()) {
      m_draining = true;
      requeue_();
    }
  }
  void requeue_() {
    if (!m_pending || m_requeuePosted) return;
    m_requeuePosted = true;
    this->app()->rxRun([
      session = this]() { session->requeueNow_(); });
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
	session = this]() { session->requeueNow_(); });
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
    auto entry = Wire::entry(id);
    if (!entry || entry->closing) return;
    entry->closing = true;
    this->app()->rxRun([
      session = this, id, peer]() {
      session->notify_(id, peer);
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
    auto entry = Wire::entry(id);
    if (!entry || entry->notified) return;
    entry->notified = true;
    auto logical = entry->logical;
    m_h2.close(id);
    Wire::remove(id, [
      session = this, logical = ZuMv(logical), peer
    ]() mutable {
      logical->disconnected_(peer);
      session->admit_();
    });
  }

  H2::Session		m_h2;
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
  typename App, typename Impl, typename Session, typename Native>
class ServerH1Logical : public ZmObject {
public:
  enum { TLS = 1, Multiplexed = 0 };

  ServerH1Logical(App *app, Native *native, ZuCSpan remote) :
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
  Native	*m_native = nullptr;
  Session	m_session;
  ZmScheduler::Timer m_idleTimer;
  EndpointString m_remote;
};

template <typename App>
class ServerEngine : public Ztls::Server<ServerEngine<App>> {
public:
  using Base = Ztls::Server<ServerEngine>;
  using Session = ServerSession<App>;
  using Link = Session;
  using StopFn = ZmFn<void(bool), ZmFnHeapID<"Zhttp.H2">>;
  using StopFns = ZtArray<StopFn,
    ZtArrayHeapID<"Zhttp.H2">>;

  App *user() { return static_cast<App *>(this); }
  const App *user() const { return static_cast<const App *>(this); }

  bool init(const EngineConfig &engine, const H2Config &config) {
    if (!TLS_::valid(config)) return false;
    m_config = config;
    return Base::init(TLS_::serverParams(engine, config));
  }
  ZiIP localIP() const { return user()->localIP(); }
  unsigned localPort() const { return user()->localPort(); }
  unsigned nAccepts() const { return 8; }
  void listening(const ZiListenInfo &info) { user()->listening(info); }
  void listenFailed(bool transient) { user()->listenFailed(transient); }
  ZiConnection *accepted(const ZiCxnInfo &ci) {
    if (!user()->admit(ci)) return nullptr;
    return new typename Session::Cxn(
      new Session{this, ci, m_config}, ci);
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
      Base::allLinks_({this, [](ServerEngine *, Ztc::Link *link) {
	static_cast<Session *>(link)->drain();
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
	Base::allLinks_({this, [](ServerEngine *engine, Ztc::Link *link) {
	  ++engine->m_stopPending;
	  static_cast<Session *>(link)->beginStop();
	}});
	if (!m_stopPending) stopDrain_();
      });
    });
  }
  void sessionDown() {
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
class ServerSession :
  public Ztls::SrvLink<ServerEngine<App>, ServerSession<App>,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>,
  public H2_::Wire<ServerSession<App>, typename App::H2Link> {
public:
  using Engine = ServerEngine<App>;
  using H1Logical = typename App::H1Link;
  using H2Logical = typename App::H2Link;
  using Base = Ztls::SrvLink<Engine, ServerSession,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>;
  using Wire = H2_::Wire<ServerSession, H2Logical>;
  using Cxn = typename Base::Cxn;
  using Active = ZtArray<ZmRef<H2Logical>,
    ZtArrayHeapID<"Zhttp.H2">>;

  ServerSession(
    Engine *app, const ZiCxnInfo &ci, const H2Config &config) :
      Base{app}, m_policy{config.policy()}
  {
    m_remote = ci.remoteIP;
    Wire::initWire(true, config);
    m_h2.init(
      true, 0, config.maxConcurrentStreams(),
      config.maxPending());
  }
  ~ServerSession() {
    m_h2.final();
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
    Wire::all([&active](auto &entry) {
      if (!entry.notified) active.push(entry.logical);
    });
    m_h2.final();
    Wire::clear([
      session = ZmMkRef(this), active = ZuMv(active), peer
    ]() mutable {
      for (auto &logical: active) logical->disconnected_(peer);
      session->down_();
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
    return H2_::HeaderBlock<ServerSession>{
      *this, Wire::encoder(), id, Wire::peerFrameSize()};
  }
  void disconnectNative() { Base::disconnect(); }
  void drain() {
    if (m_version != Version::H2 || m_draining || m_down) return;
    m_draining = true;
    Wire::graceful();
  }

  H2_::LogicalEntry<H2Logical> *h2OpenPeer(uint32_t id) {
    if (m_draining || !m_h2.openPeer(id)) return nullptr;
    ZmRef<H2Logical> logical = new H2Logical{
      this->app()->user(), this, id, ZuCSpan{m_remote}};
    auto entry = Wire::add(id, logical);
    if (!entry) return nullptr;
    logical->connected_(ProfileTraits<H2TLS>::apply({
      .alpn = "h2",
      .version = uint32_t(13),
      .transport = Transport::TLS,
      .secure = true
    }));
    return entry;
  }
  bool h2Closed(uint32_t id) const { return m_h2.closed(id); }
  H2::Error::T h2OpenError(uint32_t id) {
    if (h2Closed(id)) return H2::Error::StreamClosed;
    if (m_h2.peerIdle(id)) {
      m_h2.refusePeer(id);
      return H2::Error::RefusedStream;
    }
    return H2::Error::ProtocolError;
  }
  void h2LocalEnd(uint32_t id) {
    if (auto entry = Wire::entry(id)) {
      entry->localEnd = true;
      if (entry->remoteEnd) closeLater_(id, false);
    }
  }
  void h2RemoteEnd(uint32_t id) {
    Wire::peerProcessed(id);
    if (auto entry = Wire::entry(id)) {
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
	(!Wire::peerInitialWindow(value) ||
	 !m_h2.peerInitialWindow(value)))
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
    auto entry = Wire::entry(id);
    if (!entry || entry->closing) return;
    entry->closing = true;
    this->app()->rxRun([
      session = this, id, peer]() {
      session->notify_(id, peer);
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
    auto entry = Wire::entry(id);
    if (!entry || entry->notified) return;
    entry->notified = true;
    auto logical = entry->logical;
    m_h2.close(id);
    Wire::remove(id, [logical = ZuMv(logical), peer]() mutable {
      logical->disconnected_(peer);
    });
  }
  void down_() {
    this->app()->user()->release();
    this->app()->sessionDown();
  }

  H2::Session		m_h2;
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
class Server<App, H2TLS> : public H2_::ServerEngine<App> {
public:
  using Base = H2_::ServerEngine<App>;
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

#endif /* ZhttpH2Engine_HH */
