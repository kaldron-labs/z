//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - HTTP/3 message implementation

#ifndef ZhttpH3_HH
#define ZhttpH3_HH

#ifndef Zhttp_HH
#include <zlib/Zhttp.hh>
#endif

#include <zlib/ZuByteSwap.hh>

namespace Zhttp {

namespace H3 {

enum {
  StatusSize = 3,		// HTTP status is exactly three decimal digits
  QPackInsnMax = 1<<16,		// bounded incomplete QPACK instruction
  RxProcessBatch = 64		// bound one reactive dequeue turn
};

enum : uint64_t {
  NoError = 0x100,
  GeneralProtocolError = 0x101,
  InternalError = 0x102,
  StreamCreationError = 0x103,
  ClosedCriticalStream = 0x104,
  FrameUnexpected = 0x105,
  FrameError = 0x106,
  ExcessiveLoad = 0x107,
  IDError = 0x108,
  SettingsError = 0x109,
  MissingSettings = 0x10a,
  RequestRejected = 0x10b,
  RequestCancelled = 0x10c,
  RequestIncomplete = 0x10d,
  MessageError = 0x10e,
  ConnectError = 0x10f,
  VersionFallback = 0x110,
  QPackEncoderError = 0x201,
  QPackDecoderError = 0x202
};

// HTTP/3 connection state
ZtEnumStruct(ZhttpAPI, CxnState, int8_t,
  Init,
  LocalControlOpen,
  LocalQPackOpen,
  PeerControlOpen,
  PeerSettingsReceived,
  Ready,
  Goaway,
  Draining,
  Error);

ZtEnumStruct(ZhttpAPI, ParserState, int8_t,
  Initial,		// expecting first HEADERS frame
  Body,		// after initial HEADERS; accepting DATA or trailers
  Trailers,		// trailing HEADERS received; no more frames allowed
  Stream,		// Extended CONNECT ordered payload
  RemoteClosed,	// stream FIN received after all payload
  Complete,		// stream FIN / closed cleanly
  Cancelled,	// RESET_STREAM / STOP_SENDING / app cancellation
  Error);		// invalid frame sequence or decode failure

ZtEnumStruct(ZhttpAPI, CxnStreamState, int8_t,
  Type,		// reading stream type
  Control,		// HTTP/3 control stream frames
  Push,		// reading a push stream ID
  QPackEncoder,	// peer QPACK encoder stream bytes
  QPackDecoder,	// peer QPACK decoder stream bytes
  Extension,	// unknown extension stream bytes
  Complete,		// stream FIN / closed cleanly
  Cancelled,	// RESET_STREAM / STOP_SENDING
  Error);		// invalid connection stream

struct Frame {
  uint64_t	type = 0;
  uint64_t	length = 0;
  uint64_t	total = 0;
  unsigned	header = 0;
};

// load a QUIC variable-length integer of a known encoded width
template <typename T, uint64_t Mask>
ZuInline uint64_t var_(const void *p) {
  return uint64_t(*static_cast<const ZuBigEndian<T> *>(p)) & Mask;
}

ZuInline uint64_t var_(const uint8_t *p, unsigned n) {
  switch (n) {
    case 1: return p[0] & 0x3f;
    case 2: return var_<uint16_t, 0x3fff>(p);
    case 4: return var_<uint32_t, 0x3fffffff>(p);
    case 8: return var_<uint64_t, 0x3fffffffffffffffULL>(p);
  }
  return 0; // unreachable - encoded width is selected by the first byte
}

// parse a QUIC variable-length integer from a reactive receive queue
template <typename Rx>
int rxVar(
  Rx &rx, uint64_t offset, uint64_t &value, unsigned &length)
{
  ZuBArray<8> bytes;
  unsigned copied = rx.copy(offset, bytes.span());
  if (!copied) return 0;
  length = 1U << (bytes[0] >> 6);
  if (copied < length) return 0;
  value = var_(bytes.data(), length);
  return 1;
}

// parse an HTTP/3 frame header from a reactive receive queue
template <typename Rx>
int rxFrame(Rx &rx, Frame &frame)
{
  unsigned typeLen = 0, lengthLen = 0;
  int state = rxVar(rx, 0, frame.type, typeLen);
  if (state <= 0) return state;
  state = rxVar(rx, typeLen, frame.length, lengthLen);
  if (state <= 0) return state;
  frame.header = typeLen + lengthLen;
  if (frame.length > uint64_t(INT64_MAX) - frame.header) return -1;
  frame.total = frame.header + frame.length;
  return 1;
}

using SettingsKeySet = ZmHashKV<
  uint64_t, bool,
  ZmHashLock<ZmNoLock,
    ZmHashHeapID<"Zhttp.H3.SettingsKeys">>>;

// parse a QUIC variable-length integer from a contiguous span
ZuInline int var(ZuCSpan in, unsigned &o, uint64_t &v) {
  unsigned n = in.length();
  if (o >= n) return -1;
  auto p = reinterpret_cast<const uint8_t *>(&in[o]);
  unsigned len = 1U << (p[0] >> 6);
  if (n - o < len) return -1;
  v = var_(p, len);
  o += len;
  return 0;
}

ZuInline bool var(ZuCSpan in, uint64_t &v) {
  unsigned o = 0;
  return var(in, o, v) >= 0 && o == in.length();
}

ZuInline bool pushPromise(ZuCSpan in, uint64_t &id) {
  unsigned o = 0;
  unsigned n = in.length();
  return var(in, o, id) >= 0 && n >= o + 2;
}

template <typename> struct IsTypeList_ : public ZuFalse { };
template <typename ...Ts>
struct IsTypeList_<ZuTypeList<Ts...>> : public ZuTrue { };

template <typename Impl, typename = void>
struct HasH3Cxn : public ZuFalse { };
template <typename Impl>
struct HasH3Cxn<Impl,
  decltype(ZuDeclVal<Impl *>()->h3Cxn(), void())> : public ZuTrue { };

// HTTP/3 unidirectional connection stream parser
//
// CRTP - implementation may implement the following callbacks:
#if 0
struct Impl : public CxnParser<Impl> {
  using Base = CxnParser<Impl>;

  // optional - if implemented, must call Base::reset()
  void reset();

  // optional - connection state storage, defaults to Base-owned state
  CxnState::T h3State() const;
  void h3State(CxnState::T);

  // optional - validate peer unidirectional stream uniqueness/policy
  // - return false to reject the stream
  bool peerControlStream();
  bool peerEncoderStream();
  bool peerDecoderStream();
  bool peerExtensionStream(uint64_t type);

  // optional - peer role and typed HTTP/3 connection-error sink
  bool h3Server() const;
  void h3Error(uint64_t error);

  // optional - SETTINGS callback
  // - Base::setting() posts SETTINGS_QPACK_MAX_TABLE_CAPACITY to Tx
  void setting(uint64_t key, uint64_t value);

  // optional - GOAWAY callback
  void goaway(uint64_t id);

  // optional - peer encoder stream state; nullptr disables dynamic QPACK Rx
  QPackRxTable *qpackRx();

  // optional - post peer decoder instructions and SETTINGS capacity to Tx
  bool qpackTxInsn(QPackInsn::T, uint64_t);
  bool qpackTxMaxCapacity(uint64_t);
  bool qpackTxBlocked(uint64_t);
};
#endif

template <typename Impl>
class CxnParser {
public:
  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  using State = CxnState;
  using StreamState = CxnStreamState;

private:
  bool applyEncoderInstruction_(const QPackDecodedInsn &i) {
    auto rx = impl()->qpackRx();
    if (!rx) return i.type == QPackInsn::SetCapacity && !i.value;
    if (i.type == QPackInsn::SetCapacity) {
	if (i.value > uint32_t(-1)) return false;
	return rx->setCapacity(uint32_t(i.value));
    }
    if (i.type == QPackInsn::InsertWithoutNameRef)
	return rx->insert(i.header);
    if (i.type == QPackInsn::Duplicate)
	return rx->duplicate(i.value);
    if (i.type != QPackInsn::InsertWithNameRef) return false;
    HeaderName name;
    if (i.nameRefDynamic) {
	Header h;
	if (!rx->lookupRelative(rx->insertCount(), i.value, h)) return false;
	name = h.name;
    } else if (!QPack::staticName(i.value, name))
	return false;
    return rx->insert(Header{name, i.header.value});
  }
  bool applyDecoderInstruction_(const QPackDecodedInsn &i) {
    return impl()->qpackTxInsn(i.type, i.value);
  }

  bool fail_(uint64_t error) {
    if (!m_error) {
	m_error = error;
	impl()->h3Error(error);
    }
    m_streamState = StreamState::Error;
    impl()->h3State(State::Error);
    return false;
  }

  bool streamType_(uint64_t type) {
    switch (type) {
	case 0x00: // control stream
	  if (!impl()->peerControlStream()) return false;
	  m_streamState = StreamState::Control;
	  impl()->h3State(State::PeerControlOpen);
	  return true;
	case 0x01: // push stream
	  if (impl()->h3Server()) return fail_(StreamCreationError);
	  m_streamState = StreamState::Push;
	  return true;
	case 0x02: // QPACK encoder stream
	  if (!impl()->peerEncoderStream()) return false;
	  m_streamState = StreamState::QPackEncoder;
	  return true;
	case 0x03: // QPACK decoder stream
	  if (!impl()->peerDecoderStream()) return false;
	  m_streamState = StreamState::QPackDecoder;
	  return true;
	default:
	  if (!impl()->peerExtensionStream(type)) return false;
	  m_streamState = StreamState::Extension;
	  return true;
    }
  }

  // parse SETTINGS pairs in one pass
  bool settings_(ZuCSpan payload) {
    unsigned o = 0;
    unsigned n = payload.length();
    while (o < n) {
	uint64_t key = 0, value = 0;
	if (var(payload, o, key) < 0 ||
	    var(payload, o, value) < 0)
	  return false;
	if (m_settingsKeys->find(key)) return false;
	m_settingsKeys->add(key, true);
	impl()->setting(key, value);
	if (m_streamState == StreamState::Error) return false;
    }
    impl()->h3State(State::PeerSettingsReceived);
    return true;
  }

  // parse a complete control-stream frame
  bool controlFrame_(ZuCSpan frameBytes) {
    unsigned o = 0;
    unsigned n = frameBytes.length();
    uint64_t type = 0, len = 0;
    if (var(frameBytes, o, type) < 0 ||
	var(frameBytes, o, len) < 0 ||
	n != o + len)
	return false;
    ZuCSpan payload{&frameBytes[o], unsigned(len)};
    switch (type) {
	case 0x04: // SETTINGS
	  if (m_settings) return fail_(SettingsError);
	  m_settings = true;
	  if (!settings_(payload)) return fail_(SettingsError);
	  return true;
	case 0x03: { // CANCEL_PUSH
	  uint64_t id = 0;
	  if (!var(payload, id)) return fail_(FrameError);
	  return fail_(IDError);
	}
	case 0x05: { // PUSH_PROMISE
	  uint64_t id = 0;
	  if (!pushPromise(payload, id)) return fail_(FrameError);
	  return fail_(FrameUnexpected);
	}
	case 0x07: { // GOAWAY
	  unsigned p = 0;
	  uint64_t id = 0;
	  if (var(payload, p, id) < 0 || p != payload.length())
	    return fail_(FrameError);
	  impl()->goaway(id);
	  impl()->h3State(State::Goaway);
	  return true;
	}
	case 0x0d: { // MAX_PUSH_ID
	  uint64_t id = 0;
	  if (!var(payload, id)) return fail_(FrameError);
	  if (!impl()->h3Server()) return fail_(FrameUnexpected);
	  if (m_maxPushID != uint64_t(-1) && id < m_maxPushID)
	    return fail_(IDError);
	  m_maxPushID = id;
	  return true;
	}
	case 0x00: // DATA
	case 0x01: // HEADERS
	  return fail_(FrameUnexpected);
	default:
	  return true;
    }
  }

  template <typename Stream>
  bool retire_(Stream &stream, uint64_t length) {
    return !length || stream.retireRx(length) || fail_(InternalError);
  }

  template <typename Stream>
  int type_(Stream &stream) {
    auto &rx = stream.rxStream();
    uint64_t type = 0;
    unsigned length = 0;
    int state = rxVar(rx, 0, type, length);
    if (state <= 0) return state;
    if (!streamType_(type)) return -1;
    rx.advance(length);
    return retire_(stream, length) ? int(length) : -1;
  }

  template <typename Stream>
  int control_(Stream &stream) {
    auto &rx = stream.rxStream();
    Frame frame;
    int state = rxFrame(rx, frame);
    if (state <= 0) return state;
    if (frame.total > UINT_MAX) return fail_(ExcessiveLoad) ? 0 : -1;
    if (rx.length() < frame.total) return 0;
    uint64_t remaining = frame.total;
    ZmRef<ZiIOBuf> buf;
    int64_t n = rx.extract(
      [&remaining](ZuBSpan span) -> int64_t {
	if (remaining > span.length()) {
	  remaining -= span.length();
	  return 0;
	}
	return remaining;
      }, wireAlloc_, buf);
    if (ZuUnlikely(n <= 0)) return fail_(InternalError) ? 0 : -1;
    if (!retire_(stream, frame.total)) return -1;
    bool ok = controlFrame_(ZuCSpan{buf->span()});
    return ok ? int(frame.total > INT_MAX ? INT_MAX : frame.total) : -1;
  }

  template <typename Stream>
  int push_(Stream &stream) {
    auto &rx = stream.rxStream();
    uint64_t id = 0;
    unsigned length = 0;
    int state = rxVar(rx, 0, id, length);
    if (state <= 0) return state;
    (void)id;
    rx.advance(length);
    if (!retire_(stream, length)) return -1;
    return fail_(IDError) ? int(length) : -1;
  }

  template <typename Stream, typename Decode, typename Apply>
  int qpack_(Stream &stream, Decode decode, Apply apply) {
    auto &rx = stream.rxStream();
    if (!rx) return 0;
    QPackDecodedInsn insn;
    auto span = rx.span();
    int n = decode(ZuCSpan{span}, insn);
    if (n == -2 && rx.count_() > 1) {
      uint64_t available = rx.length();
      unsigned length = available < QPackInsnMax ?
	unsigned(available) : QPackInsnMax;
      auto bytes = ZmScratch(uint8_t, length, HdrBytes::VHeap);
      bytes.length(length);
      if (ZuUnlikely(rx.copy(0, bytes.span()) != length))
	return fail_(InternalError) ? 0 : -1;
      n = decode(ZuCSpan{bytes}, insn);
    }
    if (n == -2) {
      if (rx.length() >= QPackInsnMax)
	return fail_(ExcessiveLoad) ? 0 : -1;
      return 0;
    }
    if (n <= 0 || !apply(insn)) return -1;
    rx.advance(unsigned(n));
    return retire_(stream, unsigned(n)) ? n : -1;
  }

  template <typename Stream>
  int drain_(Stream &stream) {
    auto &rx = stream.rxStream();
    uint64_t length = rx.length();
    rx.clean();
    return retire_(stream, length) ? int(length > INT_MAX ? INT_MAX : length) : -1;
  }

public:
  template <typename Stream>
  State::T process(Stream &stream) {
    if (stream.resetReceived()) {
	if (m_streamState == StreamState::Control ||
	    m_streamState == StreamState::QPackEncoder ||
	    m_streamState == StreamState::QPackDecoder)
	  fail_(ClosedCriticalStream);
	else
	  m_streamState = StreamState::Cancelled;
	auto &rx = stream.rxStream();
	uint64_t length = rx.length();
	rx.clean();
	(void)retire_(stream, length);
	return impl()->h3State();
    }

    int consumed = 0;
    unsigned processed = 0;
    do {
	consumed = 0;
	switch (m_streamState) {
	  default:
	    break;
	  case StreamState::Type:
	    consumed = type_(stream);
	    break;
	  case StreamState::Control:
	    consumed = control_(stream);
	    break;
	  case StreamState::Push:
	    consumed = push_(stream);
	    break;
	  case StreamState::QPackEncoder:
	    consumed = qpack_(stream,
	      [](ZuCSpan bytes, QPackDecodedInsn &i) {
		return QPack::decodeEncoderInsn(bytes, i);
	      }, [this](const QPackDecodedInsn &i) {
		return this->applyEncoderInstruction_(i);
	      });
	    if (consumed < 0) fail_(QPackEncoderError);
	    break;
	  case StreamState::QPackDecoder:
	    consumed = qpack_(stream,
	      [](ZuCSpan bytes, QPackDecodedInsn &i) {
		return QPack::decodeDecoderInsn(bytes, i);
	      }, [this](const QPackDecodedInsn &i) {
		return this->applyDecoderInstruction_(i);
	      });
	    if (consumed < 0) fail_(QPackDecoderError);
	    break;
	  case StreamState::Extension:
	    consumed = drain_(stream);
	    break;
	}
	if (consumed < 0 || m_streamState == StreamState::Error) {
	  impl()->h3State(State::Error);
	  break;
	}
      if (consumed > 0) ++processed;
    } while (consumed && processed < RxProcessBatch);

    auto &rx = stream.rxStream();
    if (processed == RxProcessBatch && rx && consumed > 0)
      stream.rescheduleDequeue();
    else if (stream.finReceived() &&
	m_streamState != StreamState::Error &&
	m_streamState != StreamState::Cancelled) {
      if (rx || m_streamState == StreamState::Type ||
	  m_streamState == StreamState::Push)
	fail_(StreamCreationError);
      else if (m_streamState == StreamState::Control ||
	  m_streamState == StreamState::QPackEncoder ||
	  m_streamState == StreamState::QPackDecoder)
	fail_(ClosedCriticalStream);
      else
	m_streamState = StreamState::Complete;
    }
    return impl()->h3State();
  }

  void reset() {
    m_streamState = StreamState::Type;
    m_settings = false;
    m_error = 0;
    m_maxPushID = -1;
    m_settingsKeys->clean();
  }

  State::T h3State() const { return m_cxnState; }
  void h3State(State::T state) { m_cxnState = state; }
  bool peerControlStream() { return true; }
  bool peerEncoderStream() { return true; }
  bool peerDecoderStream() { return true; }
  bool peerExtensionStream(uint64_t) { return true; }
  bool h3Server() const { return false; }
  void h3Error(uint64_t) { }
  void setting(uint64_t key, uint64_t value) {
    if (key == 0x01) {
      if (!impl()->qpackTxMaxCapacity(value))
	m_streamState = StreamState::Error;
    } else if (key == 0x07) {
      if (!impl()->qpackTxBlocked(value))
	m_streamState = StreamState::Error;
    }
    if (key == 0x08 && value > 1)
      m_streamState = StreamState::Error;
  }
  void goaway(uint64_t) { }
  QPackRxTable *qpackRx() { return nullptr; }
  bool qpackTxInsn(QPackInsn::T, uint64_t) { return true; }
  bool qpackTxMaxCapacity(uint64_t) { return true; }
  bool qpackTxBlocked(uint64_t) { return true; }

private:
  static ZmRef<ZiRxQueue::Node> wireAlloc_() {
    return new BodyRx::WireBufAlloc{};
  }

  // Rx thread exclusive
  State::T		m_cxnState = State::Init;
  StreamState::T	m_streamState = StreamState::Type;
  uint64_t		m_error = 0;
  uint64_t		m_maxPushID = -1;
  bool		m_settings = false;
  ZmRef<SettingsKeySet>	m_settingsKeys{new SettingsKeySet};
};

// HTTP/3 request/response stream parser
template <
  typename Impl,
  bool Request_ = false,
  typename Headers_ = ZuTypeList<>,
  uint64_t MaxBody_ = DefltMaxBody>
class Parser {
public:
  using QPackWriteFn = bool (*)(void *, ZuBSpan);
  using ErrorFn = void (*)(void *, uint64_t);

  Parser() : m_bodyRx{MaxBody} { }

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  enum { Request = Request_ };
  using Headers = typename Headers_::template Unshift<
    ZuStringT<"content-length">, void>;
  static constexpr uint64_t MaxBody = MaxBody_;
  using State = ParserState;

  void requestMethod(Method::T method) { m_requestMethod = method; }
  void extendedConnect(bool value) { m_extendedConnect = value; }
  void stream() {
    m_state = State::Stream;
    m_streamMode = true;
    m_bodyRx.reset(uint64_t(-1));
  }

  void h3(
    QPackRxTable *rx, void *cxn, QPackWriteFn decoderWrite,
    ErrorFn error, uint64_t streamID, const Params *params = nullptr) {
    m_qpackRx = rx;
    m_h3Cxn = cxn;
    m_qpackDecoderWrite = decoderWrite;
    m_errorFn = error;
    m_streamID = streamID;
    m_params = params;
  }

private:
  void error_() {
    m_state = State::Error;
  }
  bool fail_(uint64_t error) {
    if (!m_h3Error) {
	m_h3Error = error;
	if (m_errorFn) m_errorFn(m_h3Cxn, error);
    }
    error_();
    return false;
  }

  void complete_(State::T state) {
    if (m_complete) return;
    m_complete = true;
    impl()->complete(state);
  }

  using FieldState = Fields::Semantics<Request>;

  bool qpackHeader_(
    FieldState &fields, ZuCSpan name, ZuCSpan value) {
    return fields.field(name, value,
      [this](ZuBSpan key, ZuBSpan value) {
	header_(key, value);
      }) && m_state != State::Error;
  }

  const Params &h3Params() const {
    if (m_params) return *m_params;
    static const Params params;
    return params;
  }
  uint64_t streamID() const { return m_streamID; }
  bool qpackDecoderWrite(ZuBSpan span) {
    if (m_qpackDecoderWrite)
      return m_qpackDecoderWrite(m_h3Cxn, span);
    if constexpr (HasH3Cxn<Impl>{})
	return impl()->h3Cxn().qpackDecoderWrite(span);
    else
	return false;
  }

  template <typename Key> void header_(ZuBSpan value) {
    if constexpr (Key{}() == "content-length") {
	uint64_t contentLength;
	if (!atou(value, contentLength) ||
	    contentLength > MaxBody) {
	  m_state = State::Error;
	  ZiLOG(Error, "Zhttp", "invalid content-length");
	} else {
	  m_contentLen = contentLength;
	  impl()->contentLength(contentLength);
	}
    } else {
	impl()->template header<Key>(value);
    }
  }

  void runtimeHeader_(ZuBSpan key, ZuBSpan value) {
    if constexpr (Fields::HasRuntime<Impl>{})
	impl()->header(key, value);
  }

  void header_(ZuBSpan key, ZuBSpan value) {
    Fields::dispatch<Headers>(
      key, value,
      [this](auto key, ZuBSpan value) {
	this->template header_<ZuDecay<decltype(key)>>(value);
      },
      [this](auto key, auto value) {
	impl()->template header<
	  ZuDecay<decltype(key)>, ZuDecay<decltype(value)>>();
      },
      [this](ZuBSpan key, ZuBSpan value) {
	runtimeHeader_(key, value);
      });
  }

  Fields::Section parseFields_(ZuCSpan payload, bool initial) {
    auto rx = impl()->qpackRx();
    FieldSectionPrefix prefix;
    FieldState fields;
    fields.trailers(!initial);
    fields.extendedConnect(m_extendedConnect);
    if constexpr (!Request) fields.requestMethod(m_requestMethod);
    bool ok = true;
    unsigned n = payload.length();
    int used = QPack::decodeFieldSection(
	payload, rx,
	[this, &fields, &ok](Header h, QPackFieldFlags) {
	  if (!this->qpackHeader_(fields, h.name, h.value))
	    ok = false;
	},
	impl()->h3Params(), 0, 0, &prefix);
    if (used != int(n) || !ok) return Fields::Invalid;
    if (prefix.requiredInsertCount && !rx) return Fields::Invalid;
    if (prefix.requiredInsertCount) {
	HdrBytes ack;
	if (QPack::encodeSectionAck(ack, impl()->streamID()) < 0)
	  return Fields::Invalid;
	if (!impl()->qpackDecoderWrite(ZuBSpan{ack}))
	  return Fields::Invalid;
    }
    auto section = fields.finish(
      [this](Method::T method, const RequestTarget &target) {
	impl()->operation(method, target);
      },
      [this](unsigned status) { impl()->status(status); },
      [this](ZuBSpan key, ZuBSpan value) { header_(key, value); });
    if (section != Fields::Invalid)
      impl()->headers(section, false);
    return section;
  }

  bool bodyComplete_() const {
    return m_contentLen < 0 || m_bodyLen == uint64_t(m_contentLen);
  }

  bool payloadFrame_(uint64_t type, ZuCSpan payload) {
    switch (type) {
      case 0x01: // HEADERS
	if (m_state == State::Initial) {
	  auto section = parseFields_(payload, true);
	  if (section == Fields::Invalid) return false;
	  if (section == Fields::Informational) return true;
	  if (m_state != State::Stream) m_state = State::Body;
	  return true;
	}
	if (m_state != State::Body) return false;
	if (!bodyComplete_() ||
	    parseFields_(payload, false) != Fields::Trailers)
	  return false;
	m_state = State::Trailers;
	return true;
      case 0x03: // CANCEL_PUSH
      case 0x0d: { // MAX_PUSH_ID
	uint64_t id = 0;
	if (!var(payload, id)) return fail_(FrameError);
	return fail_(FrameUnexpected);
      }
      case 0x05: { // PUSH_PROMISE
	uint64_t id = 0;
	if (!pushPromise(payload, id)) return fail_(FrameError);
	return fail_(Request ? FrameUnexpected : IDError);
      }
      default:
	return true; // ignore unknown extension frames on request streams
    }
  }

  bool valid_(const Frame &frame) {
    switch (frame.type) {
      case 0x00: // DATA
	if (m_state == State::Stream) return true;
	if (m_state != State::Body ||
	    m_bodyLen > MaxBody || frame.length > MaxBody - m_bodyLen)
	  return fail_(MessageError);
	if (m_contentLen >= 0 &&
	    (m_bodyLen > uint64_t(m_contentLen) ||
	     frame.length > uint64_t(m_contentLen) - m_bodyLen))
	  return fail_(MessageError);
	return true;
      case 0x01: // HEADERS
	if (m_state != State::Initial && m_state != State::Body)
	  return fail_(FrameUnexpected);
	if (frame.length > impl()->h3Params().maxHeaderListSize())
	  return fail_(ExcessiveLoad);
	return true;
      case 0x03: // CANCEL_PUSH
      case 0x05: // PUSH_PROMISE
      case 0x0d: // MAX_PUSH_ID
	return frame.length <= UINT_MAX || fail_(ExcessiveLoad);
      default:
	return true;
    }
  }

  template <typename Stream>
  bool retire_(Stream &stream, uint64_t length) {
    if (!length) return true;
    if (ZuLikely(stream.retireRx(length))) {
      m_progressed = true;
      return true;
    }
    return fail_(InternalError);
  }

  template <typename Stream>
  bool data_(Stream &stream, const Frame &frame) {
    auto &rx = stream.rxStream();
    uint64_t before = m_bodyRx.consumed();
    if (ZuUnlikely(m_deferred > UINT64_MAX - frame.length))
      return fail_(ExcessiveLoad);
    uint64_t remaining = frame.total;
    int64_t n = m_bodyRx.splice(
      rx, frame.length,
      [&remaining](ZuBSpan span) -> int64_t {
	if (remaining > span.length()) {
	  remaining -= span.length();
	  return 0;
	}
	return remaining;
      }, wireAlloc_, alloc_, frame.header, 0,
      [this](auto &body) {
	if (m_state == State::Stream)
	  impl()->streamRx_(body);
	else
	  impl()->body(body);
      });
    if (ZuUnlikely(n <= 0)) return fail_(InternalError);
    m_deferred += frame.length;
    uint64_t consumed = m_bodyRx.consumed() - before;
    ZmAssert(consumed <= m_deferred);
    if (ZuUnlikely(consumed > m_deferred)) return fail_(InternalError);
    m_deferred -= consumed;
    if (!retire_(stream, frame.header + consumed)) return false;
    if (m_state != State::Stream) m_bodyLen += frame.length;
    return true;
  }

  template <typename Stream>
  bool meta_(Stream &stream, const Frame &frame) {
    auto &rx = stream.rxStream();
    if (frame.type != 0x01 && frame.type != 0x03 &&
	frame.type != 0x05 && frame.type != 0x0d) {
      rx.advance(frame.total);
      return retire_(stream, frame.total);
    }
    if (ZuUnlikely(frame.total > UINT_MAX)) return fail_(ExcessiveLoad);
    uint64_t remaining = frame.total;
    ZmRef<ZiIOBuf> buf;
    int64_t n = rx.extract(
      [&remaining](ZuBSpan span) -> int64_t {
	if (remaining > span.length()) {
	  remaining -= span.length();
	  return 0;
	}
	return remaining;
      }, wireAlloc_, buf);
    if (ZuUnlikely(n <= 0)) return fail_(InternalError);
    if (!retire_(stream, frame.total)) return false;
    auto span = buf->span();
    span.offset(frame.header);
    bool ok = payloadFrame_(frame.type, ZuCSpan{span});
    if (!ok) return fail_(MessageError);
    return true;
  }

  template <typename Stream>
  bool settle_(Stream &stream) {
    uint64_t length = m_bodyRx.discard();
    ZmAssert(length <= m_deferred);
    if (ZuUnlikely(length > m_deferred)) return fail_(InternalError);
    m_deferred -= length;
    return retire_(stream, length);
  }

  template <typename Stream>
  void terminal_(Stream &stream, State::T state) {
    if (m_complete) return;
    if (m_streamMode && state != State::Complete)
      impl()->streamError_();
    complete_(state);
    (void)settle_(stream);
    auto &rx = stream.rxStream();
    uint64_t length = rx.length();
    rx.clean();
    (void)retire_(stream, length);
  }

  static ZmRef<ZiRxQueue::Node> alloc_() {
    return new BodyRx::BufAlloc{};
  }
  static ZmRef<ZiRxQueue::Node> wireAlloc_() {
    return new BodyRx::WireBufAlloc{};
  }

public:
  // top-level process
  template <typename Stream>
  State::T process(Stream &stream) {
    if (m_state == State::Complete ||
	m_state == State::Cancelled ||
	m_state == State::Error)
	return m_state;
    m_progressed = false;
    if (stream.resetReceived() ||
	(stream.stopReceived() &&
	  (m_state == State::Stream || m_state == State::RemoteClosed))) {
	m_state = State::Cancelled;
	terminal_(stream, m_state);
	return m_state;
    }

    auto &rx = stream.rxStream();
    unsigned processed = 0;
    while (m_state != State::Error && processed < RxProcessBatch) {
      Frame frame;
      int state = rxFrame(rx, frame);
      if (state < 0) {
	fail_(FrameError);
	break;
      }
      if (!state || !valid_(frame)) break;
      if (rx.length() < frame.total) break;
      bool ok = frame.type == 0x00 ?
	data_(stream, frame) : meta_(stream, frame);
      if (!ok) break;
      ++processed;
    }

    if (processed == RxProcessBatch && rx)
      stream.rescheduleDequeue();
    else if (stream.rxComplete()) {
      if (rx) {
	fail_(FrameError);
      } else if (m_state == State::Stream) {
	impl()->streamPeerEnd_();
	(void)settle_(stream);
	m_state = State::RemoteClosed;
      } else if (m_state == State::Initial || !bodyComplete_()) {
	fail_(MessageError);
      } else if (m_state != State::Error) {
	m_state = State::Complete;
	complete_(m_state);
	(void)settle_(stream);
      }
    }
    if (m_state == State::Error) terminal_(stream, m_state);
    return m_state;
  }

  bool progressed() const { return m_progressed; }

  void reset() {
    m_bodyRx.reset(MaxBody);
    m_state = State::Initial;
    m_contentLen = -1;
    m_bodyLen = 0;
    m_requestMethod = -1;
    m_extendedConnect = false;
    m_deferred = 0;
    m_h3Error = 0;
    m_complete = false;
    m_progressed = false;
    m_streamMode = false;
  }

  // CRTP defaults
  void operation(Method::T, const RequestTarget &) { }
  void status(unsigned) { }
  template <typename Key> void header(ZuBSpan) { }
  template <typename Key, typename Value> void header() { }
  void header(ZuBSpan, ZuBSpan) { }
  void contentLength(uint64_t) { }
  void headers(Fields::Section, bool) { }
  template <typename Rx>
  void body(Rx &rx) { bodyDrain(rx); }
  template <typename Rx>
  void streamRx_(Rx &rx) { bodyDrain(rx); }
  void streamPeerEnd_() { }
  void streamError_() { }
  void complete(State::T) { }
  bool rxComplete() const { return impl()->finReceived(); }
  QPackRxTable *qpackRx() {
    if (m_qpackRx) return m_qpackRx;
    if constexpr (HasH3Cxn<Impl>{})
	return impl()->h3Cxn().qpackRx();
    else
	return nullptr;
  }
private:
  // Rx thread exclusive
  int64_t		m_contentLen = -1;
  uint64_t		m_bodyLen = 0;
  uint64_t		m_deferred = 0;
  BodyRx		m_bodyRx;
  Method::T		m_requestMethod = -1;
  State::T		m_state = State::Initial;
  QPackRxTable		*m_qpackRx = nullptr;
  void			*m_h3Cxn = nullptr;
  QPackWriteFn		m_qpackDecoderWrite = nullptr;
  ErrorFn		m_errorFn = nullptr;
  uint64_t		m_streamID = 0;
  const Params		*m_params = nullptr;
  uint64_t		m_h3Error = 0;
  bool			m_complete = false;
  bool			m_extendedConnect = false;
  bool			m_progressed = false;
  bool			m_streamMode = false;
};

// QUIC variable-length integer and HTTP/3 field encoding
template <typename Bytes>
inline int putVar(Bytes &out, uint64_t v) {
  if (v < (1ULL<<6)) {
    out.push(uint8_t(v));
    return 0;
  }
  if (v < (1ULL<<14)) {
    out.push(uint8_t(0x40 | (v >> 8)));
    out.push(uint8_t(v));
    return 0;
  }
  if (v < (1ULL<<30)) {
    out.push(uint8_t(0x80 | (v >> 24)));
    out.push(uint8_t(v >> 16));
    out.push(uint8_t(v >> 8));
    out.push(uint8_t(v));
    return 0;
  }
  if (v < (1ULL<<62)) {
    out.push(uint8_t(0xc0 | (v >> 56)));
    out.push(uint8_t(v >> 48));
    out.push(uint8_t(v >> 40));
    out.push(uint8_t(v >> 32));
    out.push(uint8_t(v >> 24));
    out.push(uint8_t(v >> 16));
    out.push(uint8_t(v >> 8));
    out.push(uint8_t(v));
    return 0;
  }
  return -1;
}

using Compression::putPref;
using Compression::putString;

struct CountBytes {
  uint64_t	n = 0;

  void push(uint8_t) { ++n; }
  void skip(uint64_t n_) { n += n_; }
  uint64_t length() const { return n; }
};

struct SpanBytes {
  ZuSpan<uint8_t>	data;
  unsigned		offset = 0;

  void push(uint8_t c) { data[offset++] = c; }
  unsigned length() const { return offset; }
};

template <typename Stream>
struct TxBytes {
  Stream	&stream;
  uint64_t	n = 0;

  TxBytes(Stream &stream_) : stream(stream_) { }

  void push(uint8_t c) {
    stream << char(c);
    ++n;
  }
  uint64_t length() const { return n; }
};

template <typename Stream>
inline int writeFrameHeader(Stream &stream, uint64_t type, uint64_t length) {
  TxBytes out{stream};
  if (putVar(out, type) < 0 || putVar(out, length) < 0)
    return -1;
  return out.length();
}

template <typename Bytes>
inline int qpackEncodeFieldLine(Bytes &out, Header h, const Params &params) {
  int staticIndex = QPack::staticIndex(h.name, h.value);
  if (staticIndex >= 0)
    return putPref(out, 0xc0, 6, unsigned(staticIndex)) < 0 ?
	-1 : int(out.length());

  uint64_t nameIndex = 0;
  if (QPack::staticNameIndex(h.name, nameIndex)) {
    uint8_t prefix = uint8_t(
	0x50 | (params.neverIndex(h.name) ? 0x20 : 0));
    return putPref(out, prefix, 4, nameIndex) < 0 ||
	putString(out, 0x00, 7, h.value) < 0 ? -1 : int(out.length());
  }

  return putString(
    out, uint8_t(0x20 | (params.neverIndex(h.name) ? 0x10 : 0)),
    3, h.name) < 0 ||
    putString(out, 0x00, 7, h.value) < 0 ? -1 : int(out.length());
}

template <typename Bytes>
inline int qpackEncodeDynamicIndexed(Bytes &out, uint64_t relativeIndex) {
  return putPref(out, 0x80, 6, relativeIndex) < 0 ?
    -1 : int(out.length());
}

template <typename Bytes>
inline int qpackEncodeDynamicName(
  Bytes &out, uint64_t relativeIndex, ZuCSpan value, bool never) {
  return putPref(out, uint8_t(0x40 | (never ? 0x20 : 0)), 4,
      relativeIndex) < 0 ||
    putString(out, 0, 7, value) < 0 ? -1 : int(out.length());
}

namespace DataStream_ {

template <typename Lower>
auto frameMax(Lower &lower, int) ->
  decltype(lower.frameMax(), unsigned{})
{
  uint64_t max = lower.frameMax() >> 1;
  if (!max) max = lower.frameMax();
  return max < UINT_MAX ? unsigned(max) : UINT_MAX;
}

template <typename Lower>
unsigned frameMax(Lower &lower, ...) { return lower.maxSize(); }

} // namespace DataStream_

template <typename Lower>
struct DataStream : public ZiTxLayer<DataStream<Lower>, Lower> {
  using Base = ZiTxLayer<DataStream<Lower>, Lower>;

  DataStream(Lower &lower, uint64_t remaining = uint64_t(-1)) :
    Base(lower, 9, 0, DataStream_::frameMax(lower, 0)),
    m_remaining{remaining} { }

  ~DataStream() { this->flush(); }

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
    CountBytes count;
    putVar(count, 0);
    putVar(count, length);
    unsigned frameHdrLength = unsigned(count.length());
    ZiAssert(buf->skip >= frameHdrLength,
	"Zhttp", (), "H3 DataStream headroom error", return);
    buf->rewind(frameHdrLength);
    SpanBytes frameHdr{{buf->data(), frameHdrLength}};
    putVar(frameHdr, 0);
    putVar(frameHdr, length);
  }
  uint64_t produced() const { return m_produced; }
  bool valid() const { return m_valid; }
  bool complete() const {
    return m_valid &&
      (m_remaining == uint64_t(-1) || !m_remaining);
  }

private:
  uint64_t	m_remaining;
  uint64_t	m_produced = 0;
  bool		m_valid = true;
};

template <typename Lower>
auto dataStream(Lower &lower) { return DataStream<Lower>(lower); }
template <typename Lower>
auto dataStream(Lower &lower, uint64_t length) {
  return DataStream<Lower>(lower, length);
}

// HTTP/3 message builder
template <
  typename Impl,
  typename Headers_ = ZuTypeList<>,
  typename Trailers_ = ZuTypeList<>,	// ignored if not chunked
  bool HasBody_ = false,		// has a body
  bool Streaming_ = false>
class Builder_ {
public:
  using QPackWriteFn = bool (*)(void *, ZuBSpan);

  enum {
    PrefixBuiltin = 16,
    EncoderScratchBuiltin = 256
  };

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  using Headers = Headers_;
  using Trailers = Trailers_;
  enum { HasBody = HasBody_ };
  enum { Streaming = Streaming_ };

  void h3(
    QPackTxTable *tx, void *encoder, QPackWriteFn encoderWrite,
    uint64_t streamID, bool peerExtendedConnect = false,
    const Params *params = nullptr) {
    m_qpackTx = tx;
    m_qpackEncoder = encoder;
    m_qpackEncoderWrite = encoderWrite;
    m_streamID = streamID;
    m_peerExtendedConnect = peerExtendedConnect;
    m_params = params;
  }

  // Retained messages must not publish encoder-stream or table state before
  // their message commit.  Literal encoding is the transaction-safe fallback
  // until the retained commit set has a staged QPACK transaction.
  void deferCompression() { m_deferCompression = true; }

private:
  template <typename L>
  void runtimeHeaders_(L &&l) {
    if constexpr (Fields::HasRuntimeBuilder<Impl, L>{})
	impl()->header(ZuFwd<L>(l));
  }

  static ZuCSpan statusSpan_(unsigned status, ZuCArray<StatusSize> &buf) {
    buf[0] = char('0' + ((status / 100) % 10));
    buf[1] = char('0' + ((status / 10) % 10));
    buf[2] = char('0' + (status % 10));
    return ZuCSpan{buf.data(), StatusSize};
  }

  template <typename Stream>
  static void writeSpan_(Stream &stream, ZuBSpan span) {
    stream << span;
  }

  template <typename Stream>
  static void flush_(Stream &stream) {
    stream.flush();
  }

  template <typename Stream>
  struct StreamBytes {
    StreamBytes(Stream &stream_) : stream(stream_) { }

    void push(uint8_t c) {
	stream << char(c);
	++n;
    }
    uint64_t length() const { return n; }

    Stream	&stream;
    uint64_t	n = 0;
  };

  template <typename Stream>
  static bool writeFrameHeader_(
    Stream &stream, uint64_t type, uint64_t length) {
    return writeFrameHeader(stream, type, length) >= 0;
  }

  template <typename Bytes, bool Plan>
  struct Build {
    struct InsertPlan {
	QPackTxString	name;
	QPackTxString	value;
	uint64_t	nameIndex = 0;
	bool		nameRef = false;
	bool		nameDynamic = false;
    };
    using InsertPlans =
	ZtArray<InsertPlan, ZtArrayHeapID<"Zhttp.H3.QPackInsertPlan">>;

    Bytes		&out;
    QPackTxRefs	refs;
    InsertPlans	inserts;
    QPackTxTable	*tx = nullptr;
    Params		params;
    uint64_t		base = 0;
    uint64_t		required = 0;
    uint32_t		plannedCapacity = 0;
    bool		sendCapacity = false;
    bool		dynamic = false;
    bool		ok = true;

    Build(Bytes &out_) : out{out_} { }

    bool planned_(ZuCSpan name, ZuCSpan value) const {
	for (unsigned i = 0; i < inserts.length(); ++i)
	  if (inserts[i].name == name && inserts[i].value == value)
	    return true;
	return false;
    }
    void planInsert_(ZuCSpan name, ZuCSpan value) {
	if constexpr (Plan) {
	  if (!tx || !dynamic || !plannedCapacity ||
	      params.neverIndex(name))
	    return;
	  if (tx->find(name, value) || planned_(name, value)) return;
	  auto plan = new (inserts.push()) InsertPlan();
	  plan->name = name;
	  plan->value = value;
	  if (QPack::staticNameIndex(name, plan->nameIndex)) {
	    plan->nameRef = true;
	    return;
	  }
	  unsigned i = inserts.length() - 1;
	  while (i)
	    if (inserts[--i].name == name) {
	      plan->nameIndex = inserts.length() - i - 2;
	      plan->nameRef = true;
	      plan->nameDynamic = true;
	      return;
	    }
	  if (auto entry = tx->findName(name)) {
	    plan->nameIndex =
	      base + inserts.length() - entry->abs - 2;
	    plan->nameRef = true;
	    plan->nameDynamic = true;
	  }
	}
    }
    void field(ZuCSpan name, ZuCSpan value) {
	if (!ok) return;
	Header h{name, value};
	if (QPack::staticIndex(name, value) >= 0) {
	  ok = qpackEncodeFieldLine(out, h, params) >= 0;
	  return;
	}
	if (tx && dynamic)
	  if (auto e = tx->find(name, value))
	    if (e->abs < base && e->abs + 1 <= tx->knownReceivedCount()) {
	      ok = qpackEncodeDynamicIndexed(out, base - e->abs - 1) >= 0;
	      if (required < e->abs + 1) required = e->abs + 1;
	      if constexpr (Plan) refs.push(e->abs);
	      return;
	    }
	uint64_t staticName = 0;
	if (tx && dynamic && !QPack::staticNameIndex(name, staticName))
	  if (auto e = tx->findName(name, base)) {
	    ok = qpackEncodeDynamicName(
	      out, base - e->abs - 1, value, params.neverIndex(name)) >= 0;
	    if (required < e->abs + 1) required = e->abs + 1;
	    if constexpr (Plan) refs.push(e->abs);
	    if (ok) planInsert_(name, value);
	    return;
	  }
	ok = qpackEncodeFieldLine(out, h, params) >= 0;
	if (!ok) return;
	planInsert_(name, value);
    }
    template <typename P>
    ZuIfT<!Compression::IsPrintString<P>{}>
    field(ZuCSpan name, const P &value) {
	if (!ok) return;
	uint64_t nameIndex = 0;
	if (QPack::staticNameIndex(name, nameIndex)) {
	  uint8_t prefix = uint8_t(
	    0x50 | (params.neverIndex(name) ? 0x20 : 0));
	  ok = putPref(out, prefix, 4, nameIndex) >= 0 &&
	    Compression::putPrint(out, 0, 7, value) >= 0;
	  return;
	}
	ok = putString(
	  out, uint8_t(0x20 | (params.neverIndex(name) ? 0x10 : 0)),
	  3, name) >= 0 && Compression::putPrint(out, 0, 7, value) >= 0;
    }
    void field(ZuCSpan name, ZuCSpan value1, char sep, ZuCSpan value2) {
	if (!ok) return;
	uint64_t length = uint64_t(value1.length()) + 1 + value2.length();
	QPackTxString value{length + 1};
	value << value1 << sep << value2;
	field(name, value);
    }
  };

  template <typename KVs, typename Build>
  bool headers_(Build &build) {
    using HeaderKeys = ZuTypeSlice<2, 0, KVs>;
    using HeaderValues = ZuTypeSlice<2, 1, KVs>;
    ZuUnroll::all<HeaderKeys>([this, &build]<typename Key>() {
	using Value = ZuType<ZuTypeIndex<Key, HeaderKeys>{}, HeaderValues>;
	if constexpr (!ZuIsSame<Value, void>{}) {
	  ZuAssert(!IsTypeList_<Value>{},
	    "H3 Builder header values must be void or a single value");
	  build.field(Key{}(), Value{}());
	} else {
	  impl()->template header<Key>([&build]<typename V>(V &&v) {
	    build.field(Key{}(), ZuFwd<V>(v));
	  });
	}
    });
    runtimeHeaders_([&build]<typename K, typename V>(K &&k, V &&v) {
	ZtString<ZtStringHeapID<"Zhttp.H3.HeaderName">> name;
	name << ZuFwd<K>(k);
	if (name) build.field(ZuCSpan{name}, ZuFwd<V>(v));
    });
    return build.ok;
  }

  template <typename Build>
  bool contentLength_(Build &build) {
    (void)build;
    return true;
  }

  template <typename Build>
  static void encodeMethod_(Build &build, Method::T method) {
    build.field(":method", Method::name(method));
  }

  template <typename Stream, typename Encode>
  void writeHeaders_(Stream &stream, Encode &&encode) {
    impl()->qpackFailure(QPackBuildFailure::None);
    CountBytes count;
    Build<CountBytes, true> plan{count};
    plan.tx = impl()->qpackTx();
    plan.params = impl()->h3Params();
    if (!m_deferCompression && plan.tx &&
	(plan.dynamic = plan.tx->sectionAdmissible(impl()->streamID()))) {
	uint32_t desired = plan.tx->effectiveCapacity();
	plan.plannedCapacity = plan.tx->capacity();
	if (plan.plannedCapacity != desired) {
	  plan.plannedCapacity = desired;
	  plan.sendCapacity = true;
	}
	plan.base = plan.tx->insertCount();
    }
    if (!encode(plan) || !plan.ok) {
	impl()->qpackFailure(QPackBuildFailure::Plan);
	ZiLOG(Error, "Zhttp", "failed to plan H3 headers");
	return;
    }
    if (plan.tx && plan.dynamic) {
	if (!plan.tx->canCommit(plan.plannedCapacity, plan.inserts)) {
	  count.n = 0;
	  plan.refs.init();
	  plan.inserts.init();
	  plan.required = 0;
	  plan.plannedCapacity = plan.tx->capacity();
	  plan.sendCapacity = false;
	  plan.dynamic = false;
	  plan.ok = true;
	  if (!encode(plan) || !plan.ok) {
	    impl()->qpackFailure(QPackBuildFailure::Plan);
	    ZiLOG(Error, "Zhttp", "failed to re-plan literal H3 headers");
	    return;
	  }
	}
    }
    auto prefix = ZmScratch(uint8_t, PrefixBuiltin, HdrBytes::VHeap);
    FieldSectionPrefix p;
    p.requiredInsertCount = plan.required;
    p.base = plan.required ? plan.base : 0;
    if (QPack::encodeFieldSectionPrefix(
	    prefix, p, plan.plannedCapacity) < 0) {
	impl()->qpackFailure(QPackBuildFailure::PrefixEncode);
	ZiLOG(Error, "Zhttp", "failed to encode H3 QPACK prefix");
	return;
    }
    bool encoderEmitted = false;
    if (plan.sendCapacity || plan.inserts.length()) {
	auto scratch = ZtScratch(HdrBytes, EncoderScratchBuiltin);
	if (plan.sendCapacity) {
	  if (QPack::encodeSetCapacity(scratch, plan.plannedCapacity) < 0) {
	    impl()->qpackFailure(QPackBuildFailure::CapacityPolicy);
	    ZiLOG(Error, "Zhttp", "failed to encode H3 QPACK capacity");
	    return;
	  }
	  if (!impl()->qpackEncoderWrite(ZuBSpan{scratch})) {
	    impl()->qpackFailure(QPackBuildFailure::EncoderCapacityWrite);
	    ZiLOG(Error, "Zhttp", "failed to write H3 QPACK capacity");
	    return;
	  }
	  encoderEmitted = true;
	}
	for (unsigned i = 0; i < plan.inserts.length(); ++i) {
	  const auto &insert = plan.inserts[i];
	  int n = insert.nameRef ?
	    QPack::encodeInsertWithNameRef(
	      scratch, insert.nameIndex, insert.nameDynamic, insert.value) :
	    QPack::encodeInsertLiteral(
	      scratch, Header{insert.name, insert.value});
	  if (n < 0) {
	    impl()->qpackFailure(QPackBuildFailure::Plan);
	    ZiLOG(Error, "Zhttp", "failed to encode H3 QPACK insert");
	    return;
	  }
	  if (!impl()->qpackEncoderWrite(ZuBSpan{scratch})) {
	    impl()->qpackFailure(QPackBuildFailure::EncoderInsertWrite);
	    ZiLOG(Error, "Zhttp", "failed to write H3 QPACK insert");
	    return;
	  }
	  encoderEmitted = true;
	}
    }
    if (!writeFrameHeader_(
	    stream, 0x01, prefix.length() + count.length())) {
	impl()->qpackFailure(QPackBuildFailure::HeadersFrameHeaderWrite);
	ZiLOG(Error, "Zhttp", "failed to write H3 HEADERS frame header");
	return;
    }
    StreamBytes out{stream};
    writeSpan_(stream, ZuBSpan{prefix});
    out.n += prefix.length();
    uint64_t bodyStart = out.length();
    Build<StreamBytes<Stream>, false> emit{out};
    emit.tx = plan.tx;
    emit.params = plan.params;
    emit.base = plan.base;
    emit.plannedCapacity = plan.plannedCapacity;
    emit.sendCapacity = plan.sendCapacity;
    emit.dynamic = plan.dynamic;
    if (!encode(emit) || !emit.ok ||
	  out.length() - bodyStart != count.length()) {
	impl()->qpackFailure(QPackBuildFailure::HeadersPayloadEmit);
	ZiLOG(Error, "Zhttp", "failed to emit H3 headers");
	return;
    }
    flush_(stream);
    if (plan.tx) {
	if (encoderEmitted) {
	  if (plan.sendCapacity) {
	    if (!plan.tx->setCapacity(plan.plannedCapacity)) {
	      impl()->qpackFailure(QPackBuildFailure::CapacityCommit);
	      ZiLOG(Error, "Zhttp", "failed to commit QPACK capacity");
	      return;
	    }
	    plan.tx->capacitySent = true;
	  }
	  for (unsigned i = 0; i < plan.inserts.length(); ++i)
	    if (!plan.tx->insert(
		  ZuMv(plan.inserts[i].name),
		  ZuMv(plan.inserts[i].value))) {
	      impl()->qpackFailure(QPackBuildFailure::InsertCommit);
	      ZiLOG(Error, "Zhttp", "failed to commit QPACK insert");
	      return;
	    }
	}
	if (plan.refs.length()) {
	  bool tracked =
	    plan.tx->trackSection(impl()->streamID(), ZuMv(plan.refs));
	  ZiAssert(tracked, "Zhttp", (),
	    "pre-admitted QPACK section tracking failed", return);
	}
    }
  }

  // request
protected:
  template <typename Stream>
  bool request_(Stream &stream) {
    bool valid = true;
    writeHeaders_(stream, [this, &valid](auto &build) {
	impl()->operation([this, &build, &valid]<typename Target>(
	    Method::T method, Target &&target) {
	  ZuCSpan protocol;
	  if (method == Method::CONNECT)
	    impl()->protocol([&protocol]<typename P>(P &&value) {
	      protocol = ZuCSpan{ZuFwd<P>(value)};
	    });
	  if (protocol && !m_peerExtendedConnect) {
	    valid = false;
	    return;
	  }
	  encodeMethod_(build, method);
	  if (method == Method::CONNECT && !protocol) return;
	  build.field(":scheme", "https");
	  build.field(":path", ZuFwd<Target>(target));
	  if (protocol) build.field(":protocol", protocol);
	});
	if (!valid) return false;
	impl()->host([&build]<typename Host>(Host &&host) {
	  build.field(":authority", ZuFwd<Host>(host));
	});
	contentLength_(build);
	headers_<Headers>(build);
	return build.ok;
    });
    return valid && m_qpackFailure == QPackBuildFailure::None;
  }

  // response
  template <typename Stream>
  void response_(Stream &stream) {
    writeHeaders_(stream, [this](auto &build) {
	unsigned status = impl()->status();
	ZuCArray<StatusSize> buf;
	build.field(":status", statusSpan_(status, buf));
	contentLength_(build);
	headers_<Headers>(build);
	return build.ok;
    });
  }

  // body
public:
  template <typename Stream>
  auto body(Stream &stream) {
    return dataStream(stream);
  }
  template <typename Stream>
  auto body(Stream &stream, uint64_t remaining) {
    if constexpr (Streaming)
      return dataStream(stream);
    else
      return dataStream(stream, remaining);
  }

  // finish
  template <typename Stream>
  void finish(Stream &stream) {
    if constexpr (Trailers::N) {
	writeHeaders_(stream, [self = this](auto &build) {
	  self->template headers_<Trailers>(build);
	  return build.ok;
	});
    }
    stream.flush();
  }

  void reset() { }
  QPackBuildFailure::T qpackFailure() const { return m_qpackFailure; }

  // CRTP defaults
  template <typename L> void operation(L &&l) { l(Method::GET, "/"); }
  template <typename L> void host(L &&l) { l("127.0.0.1"); }
  template <typename L> void protocol(L &&) { }
  unsigned status() { return 200; }
  template <typename L> void reason(L &&l) { l(""); }
  template <typename Key, typename L>
  void header(L &&) { }
  template <typename L> void header(L &&) { }
  QPackTxTable *qpackTx() {
    if (m_qpackTx) return m_qpackTx;
    if constexpr (HasH3Cxn<Impl>{})
	return impl()->h3Cxn().qpackTx();
    else
	return nullptr;
  }
  bool qpackEncoderWrite(ZuBSpan span) {
    if (m_qpackEncoderWrite)
      return m_qpackEncoderWrite(m_qpackEncoder, span);
    if constexpr (HasH3Cxn<Impl>{})
	return impl()->h3Cxn().qpackEncoderWrite(span);
    else
	return false;
  }
  void qpackFailure(QPackBuildFailure::T failure) { m_qpackFailure = failure; }
  const Params &h3Params() const {
    if (m_params) return *m_params;
    static const Params params;
    return params;
  }
  uint64_t streamID() const { return m_streamID; }

private:
  // Tx thread exclusive
  QPackBuildFailure::T	m_qpackFailure = QPackBuildFailure::None;
  QPackTxTable		*m_qpackTx = nullptr;
  void			*m_qpackEncoder = nullptr;
  QPackWriteFn		m_qpackEncoderWrite = nullptr;
  uint64_t		m_streamID = 0;
  const Params		*m_params = nullptr;
  bool			m_peerExtendedConnect = false;
  bool			m_deferCompression = false;
};

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false,
  bool Streaming = false>
class RequestBuilder :
  public Builder_<Impl, Headers, Trailers, HasBody, Streaming> {
  using Base = Builder_<Impl, Headers, Trailers, HasBody, Streaming>;

public:
  template <typename Stream>
  bool request(Stream &stream) { return Base::request_(stream); }
};

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false,
  bool Streaming = false>
class ResponseBuilder :
  public Builder_<Impl, Headers, Trailers, HasBody, Streaming> {
  using Base = Builder_<Impl, Headers, Trailers, HasBody, Streaming>;

public:
  template <typename Stream>
  void response(Stream &stream) { Base::response_(stream); }
};

} // namespace H3

} // namespace Zhttp

#endif /* ZhttpH3_HH */
