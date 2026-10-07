//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP/2 native TLS Links and application logical-link adapters

#ifndef ZhttpH2Hub_HH
#define ZhttpH2Hub_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <string.h>

#include <zlib/ZmBlock.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmContext.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmScheduler.hh>

#include <zlib/ZtScratch.hh>

#include <zlib/ZiTxStream.hh>

#include <zlib/ZhttpCore.hh>
#include <zlib/ZhttpTransport.hh>
#include <zlib/ZhttpH2.hh>
#include <zlib/ZhttpHPack.hh>

namespace Zhttp {

namespace H2 {

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

} // namespace H2

namespace H2_ {

template <typename Link,
  typename = decltype(ZuDeclVal<Link * &>()->hpackSeedPlans())>
decltype(auto) hpackSeedPlans_(Link *link, int) {
  return link->hpackSeedPlans();
}
struct EmptyHPackPlan {
  H2::HPackWarmEntries entries;
};
using EmptyHPackPlans =
  ZtArray<EmptyHPackPlan, ZtArrayHeapID<"Zhttp.H2.EmptyHPackPlans">>;
template <typename Link>
const EmptyHPackPlans &hpackSeedPlans_(Link *, ...) {
  static const EmptyHPackPlans plans;
  return plans;
}

template <typename Logical>
struct Stream {
  Stream() = default;
  Stream(uint32_t id_, ZmRef<Logical> logical_, bool local_) :
    logical{ZuMv(logical_)}, id{id_}, local{local_} { }

  ZmRef<Logical> logical;
  ZiTxErrorFn	txErrorFn;
  uint64_t	deferred = 0;
  int64_t	rxWindow = H2::DefltWindow;
  int64_t	txWindowHint = H2::DefltWindow;
  uint32_t	id = 0;
  bool		begin = false;
  bool		finalHeaders = false;
  bool		localEnd = false;
  bool		localEndQueued = false;
  bool		remoteEnd = false;
  bool		notified = false;
  bool		closing = false;
  bool		flowError = false;
  bool		local = false;
};

template <typename Logical>
inline uint32_t Stream_IDAxor(const Stream<Logical> &stream)
{
  return stream.id;
}

template <typename Logical>
using StreamHash = ZmHash<Stream<Logical>,
  ZmHashNode<Stream<Logical>,
    ZmHashKey<Stream_IDAxor<Logical>,
      ZmHashLock<ZmNoLock,
	ZmHashHeapID<"Zhttp.H2">>>>>;

} // namespace H2_

namespace TLS_ {

inline bool validPolicy(H2Policy::T policy)
{
  return policy >= H2Policy::Force && policy <= H2Policy::Disable;
}

inline bool valid(const H2Config &config)
{
  return config.hpackRxCapacity() <= H2Config::MaxHPackCapacity &&
    config.hpackTxCapacity() <= H2Config::MaxHPackCapacity &&
    config.maxFrameSize() >= H2::DefltFrameSize &&
    config.maxFrameSize() <= H2::MaxFrameSize &&
    config.initialWindowSize() <= H2::MaxWindow &&
    config.maxConcurrentStreams() && config.maxPending() &&
    config.maxQueuedFrames() && config.maxStreamID() &&
    config.maxStreamID() <= H2::MaxWindow &&
    (config.maxStreamID() & 1U) &&validPolicy(config.policy());
}

template <typename Params>
inline void alpn(Params &params, H2Policy::T policy)
{
  switch (policy) {
    case H2Policy::Force:
      params.alpn(ZuSpan<ZuCSpan>{"h2"});
      break;
    case H2Policy::Prefer:
      params.alpn(ZuSpan<ZuCSpan>{"h2", "http/1.1"});
      break;
    case H2Policy::Disable:
      params.alpn(ZuSpan<ZuCSpan>{"http/1.1"});
      break;
  }
}

inline Ztls::ClientParams clientParams(
  const HubConfig &hub, const H2Config &config)
{
  Ztls::ClientParams params{
    hub.mx(), hub.rxThread(), hub.txThread()};
  params.asyncThread(hub.asyncThread())
    .caPath(config.caPath()).certPath(config.certPath())
    .keyPath(config.keyPath());
  alpn(params, config.policy());
  return params;
}

inline Ztls::ServerParams serverParams(
  const HubConfig &hub, const H2Config &config)
{
  Ztls::ServerParams params{
    hub.mx(), hub.rxThread(), hub.txThread()};
  params.asyncThread(hub.asyncThread())
    .caPath(config.caPath()).certPath(config.certPath())
    .keyPath(config.keyPath())
    .mTLS(config.mTLS()).cacheTimeout(config.cacheTimeout());
  alpn(params, config.policy());
  return params;
}

ZhttpAPI Version::T version(ZuCSpan alpn, H2Policy::T policy);

} // namespace TLS_

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
  ZuBSpan	name;
  ZuSpan<uint8_t> value;
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

template <typename Tx>
struct StreamBytes {
  void push(uint8_t value) { tx << char(value); if (!tx.failed()) ++m_length; }
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

  ~DataStream() { this->flush(); }

  bool prepareBuf_(ZiIOBuf *buf, bool) {
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
      "H2 DATA headroom error", return false);
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
    return true;
  }
  uint64_t produced() const { return m_produced; }
  bool valid() const { return m_valid && !Base::operator !(); }
  bool complete() const {
    return valid() &&
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
  FrameStream(Native &native, HeaderFrames &frames) :
    Base(
      native.txMaxSize(), native.template txHeadRoom<false>(),
      native.template txTailRoom<false>()),
    m_native{&native}, m_frames{&frames}
  { }

  ~FrameStream() { this->flush(); }

  ZmRef<ZiIOBuf> allocBuf_(unsigned headRoom) {
    return m_native->template txAllocBuf<false>(headRoom);
  }
  bool sendBuf_(ZmRef<ZiIOBuf> buf, bool) {
    m_frames->push(ZuMv(buf));
    return true;
  }

private:
  Native	*m_native;
  HeaderFrames	*m_frames;
};

template <typename Native, bool AppThread = true>
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
	m_streamID{streamID}, m_tx{native, frames},
	m_left{length}, m_frameSize{frameSize},
	m_first{first}, m_endStream{endStream}
    {
      next_();
    }

    void push(uint8_t value) {
      if (m_tx.failed()) return;
      m_tx << char(value);
      if (m_tx.failed()) return;
      ++m_length;
      if (!--m_frameLeft && m_left) next_();
    }
    void write(ZuBSpan data) {
      while (data && !m_tx.failed()) {
	unsigned n = data.length() < m_frameLeft ?
	  data.length() : m_frameLeft;
	m_tx << ZuBSpan{data.data(), n};
	if (m_tx.failed()) return;
	m_length += n;
	m_frameLeft -= n;
	data.offset(n);
	if (!m_frameLeft && m_left) next_();
      }
    }
    unsigned length() const { return m_length; }
    bool flush() { return m_tx.flush(); }

  private:
    void next_() {
      if (m_length && !m_tx.flush()) return;
      uint32_t length = m_left > m_frameSize ? m_frameSize : m_left;
      m_left -= length;
      m_frameLeft = length;
      StreamBytes<FrameTx> sink{m_tx};
      putHeader(sink, {
	.length = length,
	.streamID = m_streamID,
	.type = uint8_t(m_first ? FrameType::Headers :
	  FrameType::Continuation),
	.flags = uint8_t(
	  (m_first && m_endStream ? Flag::EndStream : 0) |
	  (!m_left ? Flag::EndHeaders : 0))
      });
      m_first = false;
    }

    uint32_t	m_streamID = 0;
    FrameTx	m_tx;
    uint32_t	m_left = 0;
    uint32_t	m_frameLeft = 0;
    uint32_t	m_frameSize = DefltFrameSize;
    unsigned	m_length = 0;
    bool	m_first = false;
    bool	m_endStream = false;
  };

public:
  using HeaderBytes = HPackBytes;
  using HeaderSection = Compression::FieldSectionBuffer<HeaderBytes>;

  struct ValueSpan {
    uint64_t	offset = 0;
    unsigned	length = 0;
  };

  HeaderBlock(
    Native &native, HPackEncoder &encoder, uint32_t streamID,
    uint32_t frameSize, unsigned plan = unsigned(-1)) :
      m_native{native}, m_encoder{encoder}, m_streamID{streamID},
      m_frameSize{frameSize}, m_plan{plan} { }
  ~HeaderBlock() {
    if (m_reserved) m_encoder.rollbackBlock();
  }

  void beginHeaders(HeaderSection &section, bool endStream = false) {
    ZuAssert(!AppThread,
      "H2 header encoding requires transmit_()/txStream_() on Tx");
    ZiAssert(m_native.app()->txInvoked(), "Zhttp", (),
      "H2 header encoding outside Tx thread", return);
    if (failed()) return;
    m_frames.length(0);
    m_section = &section;
    m_block = &section.bytes();
    m_block->length(0);
    if (!m_encoder.beginBlock(m_plan, m_reserved)) {
      m_valid = false;
      return;
    }
    Compression::putBytes(*m_block, m_encoder.updateBytes());
    m_open = true;
    m_endStream = endStream;
  }
  void plan(unsigned plan) { m_plan = plan; }
  void field(ZuBSpan name, ZuBSpan value) {
    field_({name, value});
  }
  void fieldName(ZuBSpan name, ZuBSpan value) {
    fieldName_(name, value);
  }
  void fieldLiteral(ZuBSpan name, ZuBSpan value) {
    fieldLiteral_(name, value);
  }
  void fieldFixed(ZuBSpan name, ZuBSpan value) {
    if (!m_valid ||
	m_encoder.emitFixed(*m_block, {name, value}, m_reserved) < 0)
      m_valid = false;
  }
  template <typename P, typename = ZuIfT<!Compression::IsPrintString<P>{}>>
  void
  fieldFixed(ZuBSpan name, const P &value) {
    if (!m_valid) return;
    auto rendered = ZtScratch(HPackScratch, 256);
    Compression::PrintBytes out{rendered};
    out << value;
    if (!out.ok() ||
	m_encoder.emitFixed(
	  *m_block, {name, rendered}, m_reserved) < 0)
      m_valid = false;
  }
  template <typename P, typename = ZuIfT<!Compression::IsPrintString<P>{}>>
  void
  field(ZuBSpan name, const P &value) {
    fieldPrint_(name, value);
  }
  template <typename P, typename = ZuIfT<!Compression::IsPrintString<P>{}>>
  void
  fieldName(ZuBSpan name, const P &value) {
    fieldNamePrint_(name, value);
  }
  template <typename P, typename = ZuIfT<!Compression::IsPrintString<P>{}>>
  void
  fieldLiteral(ZuBSpan name, const P &value) {
    fieldLiteralPrint_(name, value);
  }
  template <typename P>
  ValueSpan fieldMutable(ZuBSpan name, const P &value) {
    if (!m_valid) return {};
    uint64_t nameIndex = m_encoder.nameIndex(name);
    uint8_t prefix = m_encoder.neverIndexed(name) ? 0x10 : 0;
    if (Compression::putPref(*m_block, prefix, 4, nameIndex) < 0 ||
	(!nameIndex && Compression::putString(*m_block, 0, 7, name) < 0)) {
      m_valid = false;
      return {};
    }
    ValueSpan span;
    if (m_section->putPrint(
	  0, 7, value, span.offset, span.length) < 0) {
      m_valid = false;
      return {};
    }
    return span;
  }
  uint8_t *headerBase() { return m_block->data(); }
  bool endHeaders(bool) {
    if (!m_open || !m_valid) return false;
    Bytes bytes{
      m_native, m_streamID, uint32_t(m_section->length()), m_frameSize,
      true, m_endStream, m_frames};
    m_section->each(0, [&bytes](ZuBSpan span) { bytes.write(span); });
    if (!bytes.flush()) { fail_(); return false; }
    m_open = false;
    m_block = nullptr;
    m_section = nullptr;
    if (!m_deferred) {
      bool ok = m_native.template sendHeaders<false>(
	m_streamID, ZuMv(m_frames), m_endStream);
      m_frames = {}; // moved ZtArray retains a non-owning view
      if (!ok) return fail_();
      commitHPack_();
    } else {
      m_initialHeaders = true;
      m_initialEndStream = m_endStream;
    }
    return true;
  }
  auto body() { return DataStream<HeaderBlock>{*this, m_streamID}; }
  auto body(uint64_t length) {
    return DataStream<HeaderBlock>{*this, m_streamID, length};
  }
  void end() {
    if (failed()) return;
    if (m_deferred)
      m_endData = true;
    else if (!m_native.template endData<AppThread>(m_streamID))
      fail_();
  }
  bool extendedConnect() const {
    return m_native.template peerExtendedConnect<AppThread>();
  }
  bool localExtendedConnect() const {
    return m_native.localExtendedConnect();
  }
  bool failed() const { return !m_valid; }
  bool flush() { return valid() || fail_(); }
  unsigned maxSize() {
    return m_native.template dataMaxSize<AppThread>(m_streamID);
  }
  unsigned headRoom() const {
    return m_native.template txHeadRoom<AppThread>();
  }
  unsigned tailRoom() const {
    return m_native.template txTailRoom<AppThread>();
  }
  ZmRef<ZiIOBuf> allocBuf_(unsigned headRoom) {
    if (failed()) return nullptr;
    auto buf = m_native.template txAllocBuf<AppThread>(headRoom);
    if (!buf || buf->failed()) { fail_(); return nullptr; }
    return buf;
  }
  bool sendBuf(ZmRef<ZiIOBuf> buf, bool) {
    if (failed()) return false;
    if (!buf || buf->failed()) return fail_();
    if (m_deferred) {
      m_data.push(ZuMv(buf));
      return true;
    }
    if (!m_native.template sendData<AppThread>(m_streamID, ZuMv(buf)))
      return fail_();
    return true;
  }
  void defer(uint64_t max = uint64_t(-1)) {
    if (failed()) return;
    m_frames.length(0);
    m_data.length(0);
    m_initialHeaders = false;
    m_endData = false;
    m_initialEndStream = false;
    m_retainedMax = max;
    m_deferred = true;
  }
  bool valid() const {
    if (!m_valid) return false;
    uint64_t n = 0;
    for (unsigned i = 0, l = m_frames.length(); i < l; ++i) {
      if (m_frames[i]->failed() ||
	m_frames[i]->length > m_retainedMax - n) return false;
      n += m_frames[i]->length;
    }
    for (unsigned i = 0, l = m_data.length(); i < l; ++i) {
      if (m_data[i]->failed() ||
	m_data[i]->length > m_retainedMax - n) return false;
      n += m_data[i]->length;
    }
    return m_valid;
  }
  bool commit() {
    if (!valid()) return fail_();
    if (!m_deferred) return true;
    if (m_initialHeaders) {
      bool ok = m_native.template sendHeaders<false>(
	  m_streamID, ZuMv(m_frames), m_initialEndStream);
      m_frames = {}; // discard the moved array's non-owning view
      if (!ok) return fail_();
      // The complete header block has been admitted. Its compression updates
      // belong to accepted work even if subsequent DATA admission fails.
      commitHPack_();
    }
    for (unsigned i = 0, n = m_data.length(); i < n; ++i)
      if (!m_native.template sendData<AppThread>(m_streamID, ZuMv(m_data[i])))
	return fail_();
    m_data.length(0);
    if (m_endData && !m_native.template endData<AppThread>(m_streamID))
      return fail_();
    m_deferred = false;
    return true;
  }

private:
  bool fail_() {
    m_valid = false;
    m_frames.length(0);
    m_data.length(0);
    return false;
  }

  void commitHPack_() {
    m_encoder.commitUpdates();
    if (m_reserved) m_encoder.commitBlock();
    m_reserved = false;
  }

  template <typename P>
  void fieldPrint_(ZuBSpan name, const P &value) {
    if (!m_valid) return;
    auto rendered = ZtScratch(HPackScratch, 256);
    Compression::PrintBytes out{rendered};
    out << value;
    if (!out.ok() || m_encoder.emitRuntime(
	*m_block, {name, rendered}) < 0)
      m_valid = false;
  }

  template <typename P>
  void fieldNamePrint_(ZuBSpan name, const P &value) {
    if (!m_valid) return;
    uint64_t nameIndex = m_encoder.nameIndex(name);
    uint8_t prefix = m_encoder.neverIndexed(name) ? 0x10 : 0;
    uint64_t offset;
    unsigned length;
    if (Compression::putPref(*m_block, prefix, 4, nameIndex) < 0 ||
	(!nameIndex && Compression::putString(*m_block, 0, 7, name) < 0) ||
	m_section->putPrint(0, 7, value, offset, length) < 0) {
      m_valid = false;
    }
  }

  template <typename P>
  void fieldLiteralPrint_(ZuBSpan name, const P &value) {
    if (!m_valid) return;
    uint64_t offset;
    unsigned length;
    uint8_t prefix = m_encoder.neverIndexed(name) ? 0x10 : 0;
    if (Compression::putPref(*m_block, prefix, 4, 0) < 0 ||
	Compression::putString(*m_block, 0, 7, name) < 0 ||
	m_section->putPrint(0, 7, value, offset, length) < 0)
      m_valid = false;
  }

  void fieldName_(ZuBSpan name, ZuBSpan value) {
    if (!m_valid) return;
    uint64_t nameIndex = m_encoder.nameIndex(name);
    uint8_t prefix = m_encoder.neverIndexed(name) ? 0x10 : 0;
    if (Compression::putPref(*m_block, prefix, 4, nameIndex) < 0 ||
	(!nameIndex && Compression::putString(*m_block, 0, 7, name) < 0) ||
	Compression::putString(*m_block, 0, 7, value) < 0)
      m_valid = false;
  }

  void fieldLiteral_(ZuBSpan name, ZuBSpan value) {
    if (!m_valid) return;
    uint8_t prefix = m_encoder.neverIndexed(name) ? 0x10 : 0;
    if (Compression::putPref(*m_block, prefix, 4, 0) < 0 ||
	Compression::putString(*m_block, 0, 7, name) < 0 ||
	Compression::putString(*m_block, 0, 7, value) < 0)
      m_valid = false;
  }

  void field_(Field field) {
    if (!m_valid) return;
    if (m_encoder.emitRuntime(*m_block, field) < 0)
      m_valid = false;
  }

  Native	&m_native;
  HPackEncoder	&m_encoder;
  uint32_t	m_streamID = 0;
  uint32_t	m_frameSize = DefltFrameSize;
  unsigned	m_plan = unsigned(-1);
  uint64_t	m_retainedMax = uint64_t(-1);
  HeaderFrames	m_frames;
  HPackBytes	*m_block = nullptr;
  HeaderSection	*m_section = nullptr;
  DataFrames	m_data;
  bool		m_open = false;
  bool		m_endStream = false;
  bool		m_endData = false;
  bool		m_deferred = false;
  bool		m_valid = true;
  bool		m_reserved = false;
  bool		m_initialHeaders = false;
  bool		m_initialEndStream = false;
};

using ClosedStreams =
  ZtArray<uint32_t, ZtArrayHeapID<"Zhttp.H2.ClosedStreams">>;
using ClosedStreamSet = ZmHashKV<
  uint32_t, bool,
  ZmHashLock<ZmNoLock,
    ZmHashHeapID<"Zhttp.H2.ClosedStreams">>>;

struct TxSchedule {
  enum : int8_t {
    None, Ready, Connection, Stream
  };
};

struct TxWindowEntry {
  TxWindowEntry() = default;
  TxWindowEntry(uint32_t id_, int64_t window_) :
    id{id_}, window{window_} { }

  ZiTxQueue	frames;
  ZiTxErrorFn	txErrorFn;
  uint32_t	id = 0;
  int64_t	window = DefltWindow;
  int8_t	scheduled = TxSchedule::None;
  bool		localEndQueued = false;
  bool		endMarker = false;
};

inline uint32_t TxWindowEntry_IDAxor(const TxWindowEntry &entry)
{
  return entry.id;
}

ZmListDerive(TxWindowList, TxWindowEntry,
  ZmListNode<TxWindowEntry, ZmListShadow<>>);

ZmHashDerive(TxWindowHash, typename TxWindowList::Node,
  (ZmHashNode<typename TxWindowList::Node,
    ZmHashKey<TxWindowEntry_IDAxor,
      ZmHashLock<ZmNoLock,
	ZmHashHeapID<"Zhttp.H2">>>>));

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
    ZmAssert(!m_txWindows || !m_txWindows->count_());
    ZmAssert(m_readyFrames.empty_());
    ZmAssert(m_connectionFrames.empty_());
    ZmAssert(m_streamFrames.empty_());
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

  const Impl *impl_() const { return static_cast<const Impl *>(this); }
  Impl *impl_() { return static_cast<Impl *>(this); }
  void h2CapacityTx_(bool) { }
  HPackEncoder &encoder() { return m_encoder; }
  template <bool AppThread = true>
  uint32_t peerFrameSize() {
    if constexpr (AppThread) return Base::peerSettings().maxFrameSize;
    else return m_txFrameSize;
  }
  template <bool AppThread = true>
  bool peerExtendedConnect() {
    if constexpr (AppThread) return Base::peerSettings().enableConnectProtocol;
    else return m_txExtendedConnect;
  }
  bool localExtendedConnect() const { return m_config.extendedConnect(); }

  Stream<Logical> *h2Stream(uint32_t id) const {
    ZiAssert(impl_()->app()->rxInvoked(), "Zhttp", (),
      "H2 stream lookup outside Rx thread", return nullptr);
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
  uint32_t nextLocalStreamID() const { return m_nextLocalStream; }
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
    clearStreams_([](auto &) { }, ZuFwd<Done>(done), 0);
  }
  template <typename L, typename Done>
  void clearStreams(L &&l, Done &&done) {
    clearStreams_(ZuFwd<L>(l), ZuFwd<Done>(done), 0);
  }
private:
  template <typename L, typename Done>
  void clearStreams_(L l, Done done, uint64_t deferred) {
    if (m_streams) {
      auto i = m_streams->iter();
	unsigned n = 0;
      while (n++ < FrameDrainBatch)
	if (auto stream_ = i()) {
	  l(*stream_);
	deferred += stream_->deferred;
	recentStream_(stream_->id);
	  i.del();
	} else
	  break;
      if (m_streams->count_()) {
	auto link = impl_();
	impl_()->app()->rxRun([
	  link, l = ZuMv(l), done = ZuMv(done), deferred
	]() mutable {
	  link->clearStreams_(ZuMv(l), ZuMv(done), deferred);
	});
	return;
      }
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
public:
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
    ZiAssert(impl_()->app()->rxInvoked(), "Zhttp", (),
      "H2 Tx error callback outside Rx thread", return);
    if (auto stream_ = h2Stream(id)) {
      stream_->txErrorFn = fn;
      auto link = impl_();
      impl_()->app()->txRun([
	link, id, fn = ZuMv(fn)]() mutable {
	link->logicalTxErrorFnTx_(id, ZuMv(fn));
      });
    }
  }
  template <bool AppThread = true>
  unsigned dataMaxSize(uint32_t id) {
    uint32_t length = peerFrameSize<AppThread>();
    int64_t connection, stream;
    if constexpr (AppThread) {
      connection = m_txWindowHint;
      auto entry_ = h2Stream(id);
      stream = entry_ ? entry_->txWindowHint : 1;
    } else {
      connection = m_txWindow;
      auto entry_ = m_txWindows->findPtr(id);
      stream = entry_ ? entry_->window : m_txInitialWindow;
    }
    if (connection < 1) connection = 1;
    if (stream < 1) stream = 1;
    if (length > connection) length = uint32_t(connection);
    if (length > stream) length = uint32_t(stream);
    unsigned overhead = impl_()->template txHeadRoom<AppThread>() +
      impl_()->template txTailRoom<AppThread>() + FrameHeaderSize;
    unsigned maxSize = overhead + length;
    unsigned capacity = impl_()->txMaxSize();
    return maxSize < capacity ? maxSize : capacity;
  }
  template <bool AppThread = true>
  bool sendData(uint32_t id, ZmRef<ZiIOBuf> buf) {
    if constexpr (!AppThread) {
      return sendDataTx_(id, ZuMv(buf));
    }
    if (m_stopping || !buf || buf->failed() || buf->length < 9) return false;
    auto entry_ = h2Stream(id);
    if (!entry_ || entry_->localEndQueued) return false;
    uint32_t length = buf->length - 9;
    if (length > peerFrameSize()) {
      return streamTxErrorRx_(id, "H2 DATA frame exceeds peer maximum");
    }
    return sendFrame_(id, length, false, ZuMv(buf));
  }
  template <bool AppThread = true>
  bool sendFrame(uint32_t id, ZmRef<ZiIOBuf> buf) {
    if constexpr (!AppThread) {
      return sendFrameDirectTx_(id, 0, false, ZuMv(buf));
    }
    if (m_stopping || !buf || buf->failed() || !buf->length) return false;
    return sendFrame_(id, 0, false, ZuMv(buf));
  }
  template <bool AppThread = true>
  bool sendHeaders(
    uint32_t id, HeaderFrames frames, bool endStream) {
    if constexpr (!AppThread) {
      return sendHeadersDirectTx_(id, ZuMv(frames), endStream);
    }
    if (m_stopping || !frames) return false;
    auto entry_ = h2Stream(id);
    if (!entry_ || entry_->localEndQueued) return false;
    unsigned admitted = 0, n = frames.length();
    while (admitted < n && m_frameAdmission.push())
      ++admitted;
    if (admitted != n) {
      if (admitted) m_frameAdmission.pop(admitted);
      streamTxErrorRx_(id, "H2 transmit queue limit exceeded");
      return false;
    }
    if (endStream) entry_->localEndQueued = true;
    auto link = impl_();
    impl_()->app()->txRun([
      link, id, endStream, frames = ZuMv(frames)
    ]() mutable {
      link->sendHeadersTx_(id, ZuMv(frames), endStream);
    });
    return true;
  }
  template <bool AppThread = true>
  bool endHeaders(uint32_t id) {
    if constexpr (!AppThread) return endTx_(id);
    if (m_stopping) return false;
    auto entry_ = h2Stream(id);
    if (!entry_ || entry_->localEndQueued) return false;
    entry_->localEndQueued = true;
    return sendFrame_(id, 0, true, {});
  }
  template <bool AppThread = true>
  bool endData(uint32_t id) {
    if constexpr (!AppThread) {
      return endTx_(id);
    }
    if (m_stopping) return false;
    auto entry_ = h2Stream(id);
    if (!entry_ || entry_->localEndQueued) return false;
    entry_->localEndQueued = true;
    auto buf = impl_()->template txAllocBuf<true>(
      impl_()->template txHeadRoom<true>());
    if (!buf) {
      impl_()->h2Cancel(id);
      return false;
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
    return sendFrame_(id, 0, true, ZuMv(buf));
  }

  int process(Ztls::RxStream &rx) {
    if (m_rxPreface) {
      unsigned length = PrefaceParser::value().length();
      if (rx.length() < length) return 0;
      int64_t n = rx.each(length,
	[this](ZuSpan<uint8_t> span) -> int64_t { return Base::process(span); });
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
    if (ZuUnlikely(!decodeHeader(bytes.span(), header))) return 0;
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
      [this](ZuSpan<uint8_t> span) -> int64_t { return Base::process(span); });
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
  void h2Headers(uint32_t id, ZuSpan<uint8_t> span) {
    auto entry_ = begin_(id);
    if (!entry_) {
      if (m_discardStream == id &&
	  m_decoder.process(span, [](DecodedField) { }) < 0)
	h2Error(Error::CompressionError);
      return;
    }
    if (m_decoder.process(span, [this, id](DecodedField field) {
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
    auto seedState = m_encoder.seedState();
    if ((seedState == H2::HPackSeedState::Cold ||
	 seedState == H2::HPackSeedState::Warm) &&
	!m_encoder.bind(hpackSeedPlans_(impl_(), 0))) {
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
  TxWindowHash::Node *txWindow_(uint32_t id) {
    ZiAssert(impl_()->app()->txInvoked(), "Zhttp", (),
      "H2 Tx window lookup outside Tx thread", return nullptr);
    auto entry_ = m_txWindows->findPtr(id);
    if (!entry_) {
      entry_ = new TxWindowHash::Node{
	TxWindowEntry{id, m_txInitialWindow}};
      m_txWindows->addNode(entry_);
    }
    return entry_;
  }
  void logicalTxErrorFnTx_(uint32_t id, ZiTxErrorFn fn) {
    ZiAssert(impl_()->app()->txInvoked(), "Zhttp", (),
      "H2 Tx error callback update outside Tx thread", return);
    if (auto entry_ = txWindow_(id)) entry_->txErrorFn = ZuMv(fn);
  }
  bool sendDataTx_(uint32_t id, ZmRef<ZiIOBuf> buf) {
    ZiAssert(impl_()->app()->txInvoked(), "Zhttp", (),
      "H2 DATA queue outside Tx thread", return false);
    if (!buf || buf->failed() || buf->length < 9) return false;
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
      return streamTxErrorTx_(id, "H2 transmit queue limit exceeded");
    }
    if (endStream) entry_->localEndQueued = true;
    sendFrameTx_(id, length, endStream, ZuMv(buf));
    return true;
  }
  bool sendHeadersDirectTx_(
    uint32_t id, HeaderFrames frames, bool endStream) {
    ZiAssert(impl_()->app()->txInvoked(), "Zhttp", (),
      "H2 direct header queue outside Tx thread", return false);
    if (!frames) return false;
    auto entry_ = txWindow_(id);
    if (!entry_ || entry_->localEndQueued) return false;
    unsigned admitted = 0, n = frames.length();
    while (admitted < n && m_frameAdmission.push())
      ++admitted;
    if (admitted != n) {
      if (admitted) m_frameAdmission.pop(admitted);
      streamTxErrorTx_(id, "H2 transmit queue limit exceeded");
      return false;
    }
    if (endStream) entry_->localEndQueued = true;
    sendHeadersTx_(id, ZuMv(frames), endStream);
    return true;
  }
  bool endTx_(uint32_t id) {
    ZiAssert(impl_()->app()->txInvoked(), "Zhttp", (),
      "H2 end queue outside Tx thread", return false);
    auto entry_ = txWindow_(id);
    if (!entry_ || entry_->localEndQueued) return false;
    auto buf = impl_()->template txAllocBuf<false>(
      impl_()->template txHeadRoom<false>());
    if (!buf) {
      txError_(id);
      return false;
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
    return sendFrameDirectTx_(id, 0, true, ZuMv(buf));
  }
  bool sendFrame_(
    uint32_t id, uint32_t length, bool endStream,
    ZmRef<ZiIOBuf> buf) {
    if (!m_frameAdmission.push()) {
      return streamTxErrorRx_(id, "H2 transmit queue limit exceeded");
    }
    auto link = impl_();
    impl_()->app()->txRun([
      link, id, length, endStream, buf = ZuMv(buf)
    ]() mutable {
      link->sendFrameTx_(id, length, endStream, ZuMv(buf));
    });
    return true;
  }
  static uint32_t frameLengthTx_(const ZiIOBuf *buf) {
    auto frame = buf->data();
    if (frame[3] != FrameType::Data) return 0;
    return (uint32_t(frame[0])<<16) |
      (uint32_t(frame[1])<<8) | uint32_t(frame[2]);
  }
  void detachFrameTx_(TxWindowHash::Node *entry_) {
    switch (entry_->scheduled) {
      case TxSchedule::Ready:
	if (m_readyFrames.headPtr() == entry_)
	  m_readyFrames.shift();
	else
	  m_readyFrames.delNode(entry_);
	break;
      case TxSchedule::Connection:
	if (m_connectionFrames.headPtr() == entry_)
	  m_connectionFrames.shift();
	else
	  m_connectionFrames.delNode(entry_);
	break;
      case TxSchedule::Stream:
	if (m_streamFrames.headPtr() == entry_)
	  m_streamFrames.shift();
	else
	  m_streamFrames.delNode(entry_);
	break;
      default:
	return;
    }
    entry_->scheduled = TxSchedule::None;
  }
  void scheduleFrameTx_(TxWindowHash::Node *entry_) {
    ZmAssert(entry_->scheduled == TxSchedule::None);
    auto buf = entry_->frames.headPtr();
    if (!buf) {
      if (!entry_->endMarker) return;
    } else {
      uint32_t length = frameLengthTx_(buf);
      if (length > m_txWindow) {
	entry_->scheduled = TxSchedule::Connection;
	m_connectionFrames.pushNode(entry_);
	impl_()->h2CapacityTx_(true);
	return;
      }
      if (length > entry_->window) {
	entry_->scheduled = TxSchedule::Stream;
	m_streamFrames.pushNode(entry_);
	return;
      }
    }
    entry_->scheduled = TxSchedule::Ready;
    m_readyFrames.pushNode(entry_);
  }
  void queueFrameTx_(
      TxWindowHash::Node *entry_, ZmRef<ZiIOBuf> buf, bool endStream) {
    if (buf)
      entry_->frames.pushNode(Transport_::txBufNode(buf.ptr()));
    else {
      ZmAssert(endStream && !entry_->endMarker);
      entry_->endMarker = true;
    }
    if (entry_->scheduled == TxSchedule::None)
      scheduleFrameTx_(entry_);
  }
  static unsigned clearFrameTx_(TxWindowHash::Node *entry_, bool) {
    unsigned n = 0;
    while (entry_->frames.shift()) {
      ++n;
    }
    if (entry_->endMarker) {
      entry_->endMarker = false;
      ++n;
    }
    return n;
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
    (void)length;
    if (endStream) entry_->localEndQueued = true;
    queueFrameTx_(entry_, ZuMv(buf), endStream);
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
      queueFrameTx_(entry_, ZuMv(frames[i]), endStream && i + 1 == n);
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
      auto blocked = ZuMv(m_connectionFrames);
      while (auto entry_ = blocked.shift()) {
	entry_->scheduled = TxSchedule::None;
	scheduleFrameTx_(static_cast<TxWindowHash::Node *>(entry_));
      }
    } else {
      auto entry_ = txWindow_(id);
      if (!entry_ || entry_->window > int64_t(MaxWindow) - value) {
	txError_(id);
	return;
      }
      entry_->window += value;
      if (entry_->scheduled == TxSchedule::Stream) {
	detachFrameTx_(entry_);
	scheduleFrameTx_(entry_);
      }
    }
    startFrameDrain_();
    impl_()->h2CapacityTx_(!m_connectionFrames.empty_());
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
    while (auto entry_ = i()) {
      entry_->window += delta;
      detachFrameTx_(entry_);
      scheduleFrameTx_(entry_);
    }
    m_txInitialWindow = value;
    startFrameDrain_();
    impl_()->h2CapacityTx_(!m_connectionFrames.empty_());
  }
  void removeFramesTx_(uint32_t id) {
    ZiAssert(impl_()->app()->txInvoked(), "Zhttp", (),
      "H2 Tx stream removal outside Tx thread", return);
    if (auto entry_ = m_txWindows->findPtr(id)) {
      detachFrameTx_(entry_);
      unsigned n = clearFrameTx_(entry_, false);
      if (n) m_frameAdmission.pop(n);
      m_txWindows->delNode(
	static_cast<TxWindowHash::Node *>(entry_));
    }
    if (m_headerStream == id) m_headerStream = 0;
  }
  void clearFramesTx_() {
    ZiAssert(impl_()->app()->txInvoked(), "Zhttp", (),
      "H2 Tx clear outside Tx thread", return);
    unsigned n = 0;
    auto i = m_txWindows->iter();
    while (auto entry_ = i()) {
      detachFrameTx_(entry_);
      n += clearFrameTx_(entry_, false);
      i.del();
    }
    if (n) m_frameAdmission.pop(n);
    m_readyFrames.clean();
    m_connectionFrames.clean();
    m_streamFrames.clean();
    m_headerStream = 0;
    m_frameDrainPosted = false;
  }
  void startFrameDrain_() {
    if (m_frameDrainPosted ||
	(!m_headerStream && m_readyFrames.empty_())) return;
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
    while (m_headerStream || inspected < FrameDrainBatch) {
      TxWindowHash::Node *entry_;
      if (m_headerStream) {
	entry_ = m_txWindows->findPtr(m_headerStream);
	if (!entry_ || entry_->scheduled != TxSchedule::Ready) break;
	detachFrameTx_(entry_);
      } else {
	auto scheduled = m_readyFrames.shift();
	if (!scheduled) break;
	entry_ = static_cast<TxWindowHash::Node *>(scheduled);
	entry_->scheduled = TxSchedule::None;
      }
      ++inspected;
      uint32_t id = entry_->id;
      bool endStream;
      if (auto pending = entry_->frames.shift()) {
	auto frame = static_cast<ZiIOBuf *>(pending.ptr())->data();
	uint32_t length = frameLengthTx_(pending);
	if (length) {
	  m_txWindow -= length;
	  entry_->window -= length;
	}
	endStream = frame[4] & Flag::EndStream;
	if (frame[3] == FrameType::Headers &&
	    !(frame[4] & Flag::EndHeaders))
	  m_headerStream = id;
	else if (m_headerStream == id &&
	    frame[3] == FrameType::Continuation &&
	    (frame[4] & Flag::EndHeaders))
	  m_headerStream = 0;
	auto tx = impl_()->txStream_();
	if (!tx.sendBuf(ZuMv(pending), false)) {
	  m_frameAdmission.pop();
	  // A failed header/continuation send leaves peer compression state
	  // uncertain. Stop the connection rather than continue draining it.
	  impl_()->app()->rxRun([link = impl_()]() {
	    link->disconnectNative();
	  });
	  return;
	}
      } else {
	ZmAssert(entry_->endMarker);
	entry_->endMarker = false;
	endStream = true;
      }
      m_frameAdmission.pop();
      scheduleFrameTx_(entry_);
      if (endStream)
	impl_()->app()->rxRun([
	  link = impl_(), id]() {
	  link->h2LocalEnd(id);
	});
    }
    if (m_headerStream || !m_readyFrames.empty_()) {
      m_frameDrainPosted = true;
      impl_()->app()->txRun([link = impl_()]() {
	link->drainFramesTx_();
      });
    }
    impl_()->h2CapacityTx_(!m_connectionFrames.empty_());
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
  bool streamTxError_(ZiTxErrorFn *fn, ZuCSpan message) {
    auto e = ZeEXCEPT(Error, "Zhttp", message);
    if (fn && *fn && !(*fn)(e)) impl_()->disconnectNative();
    return false;
  }
  bool streamTxErrorRx_(uint32_t id, ZuCSpan message) {
    ZiAssert(impl_()->app()->rxInvoked(), "Zhttp", (),
      "H2 Tx error handling outside Rx thread", return false);
    auto entry_ = h2Stream(id);
    return streamTxError_(entry_ ? &entry_->txErrorFn : nullptr, message);
  }
  bool streamTxErrorTx_(uint32_t id, ZuCSpan message) {
    ZiAssert(impl_()->app()->txInvoked(), "Zhttp", (),
      "H2 Tx error handling outside Tx thread", return false);
    auto entry_ = txWindow_(id);
    return streamTxError_(entry_ ? &entry_->txErrorFn : nullptr, message);
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

  // Exceptional shared atomic admission count; frame storage remains Tx-owned.
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
  TxWindowList		m_readyFrames;
  TxWindowList		m_connectionFrames;
  TxWindowList		m_streamFrames;
  uint32_t		m_txInitialWindow = DefltWindow;
  uint32_t		m_txFrameSize = DefltFrameSize;
  uint32_t		m_headerStream = 0;
  int64_t		m_txWindow = DefltWindow;
  bool			m_frameDrainPosted = false;
  bool			m_txExtendedConnect = false;
};

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
  void streamTx(L &&l) { write_<false>(ZuFwd<L>(l)); }
  template <typename L>
  void streamTx_(L &&l) { write_<true>(ZuFwd<L>(l)); }
  void streamTxEnd() {
    auto tx = streamImpl_()->txStream();
    tx.end();
  }
  void streamTxReset() { streamImpl_()->disconnect(); }

private:
  template <bool OnOwner, typename L>
  void write_(L &&l) {
    auto impl = streamImpl_();
    auto tx = [impl]() {
      if constexpr (OnOwner) return impl->txStream_();
      else return impl->txStream();
    }();
    auto body = tx.body();
    ZuFwd<L>(l)(body);
  }
  Impl *streamImpl_() { return static_cast<Impl *>(this); }
};

} // namespace H2_
} // namespace Zhttp

#endif /* ZhttpH2Hub_HH */
