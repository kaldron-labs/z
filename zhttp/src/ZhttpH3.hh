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

namespace Zhttp {

namespace H3 {

enum {
  Decimal64Size = 20,		// maximum decimal width of uint64_t
  StatusSize = 3		// HTTP status is exactly three decimal digits
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

ZtEnumNS(ZhttpAPI, FrameState, int8_t,
  Type = 0,		// expecting frame type prefix
  TypeCont,		// reading remaining frame type bytes
  Length,		// expecting frame length prefix
  LengthCont,	// reading remaining frame length bytes
  Payload);		// reading frame payload bytes

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

using SettingsKeySet = ZmHashKV<
  uint64_t, bool,
  ZmHashLock<ZmNoLock,
    ZmHashHeapID<"Zhttp.H3.SettingsKeys">>>;

static inline int decodeVar(ZuCSpan in, unsigned &o, uint64_t &v) {
  if (o >= in.length()) return -1;
  uint8_t c = uint8_t(in[o++]);
  unsigned len = 1U << (c >> 6);
  v = c & 0x3f;
  if (in.length() < o + len - 1) return -1;
  for (unsigned i = 1; i < len; ++i) v = (v << 8) | uint8_t(in[o++]);
  return 0;
}

static inline bool decodeVarFull(ZuCSpan in, uint64_t &v) {
  unsigned o = 0;
  return decodeVar(in, o, v) >= 0 && o == in.length();
}

static inline bool decodePushPromise(ZuCSpan in, uint64_t &id) {
  unsigned o = 0;
  return decodeVar(in, o, id) >= 0 && in.length() >= o + 2;
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

  template <typename Decode, typename Apply>
  bool parseQPack_(
    QPackInsnParser &parser, ZuBSpan span, Decode decode, Apply apply) {
    return parser.parse(span, decode, apply);
  }

  int64_t consumeVar_(ZuBSpan span) {
    for (unsigned o = 0; o < span.length(); ++o) {
	uint8_t c = span[o];
	if (!m_varBytes) {
	  m_varLen = 1U << (c >> 6);
	  m_varBytes = 1;
	  if (m_varBytes == m_varLen) return o + 1;
	  continue;
	}
	if (++m_varBytes == m_varLen) return o + 1;
    }
    return 0;
  }

  int64_t consumeFrame_(ZuBSpan span) {
    for (unsigned o = 0; o < span.length(); ++o) {
	uint8_t c = span[o];
	if (m_frameState == FrameState::Type) {
	  m_frameState = FrameState::TypeCont;
	  m_varLen = 1U << (c >> 6);
	  m_varBytes = 1;
	  if (m_varBytes < m_varLen) continue;
	  m_frameState = FrameState::Length;
	  continue;
	}
	if (m_frameState == FrameState::TypeCont) {
	  if (++m_varBytes < m_varLen) continue;
	  m_frameState = FrameState::Length;
	  continue;
	}
	if (m_frameState == FrameState::Length) {
	  m_varLen = 1U << (c >> 6);
	  m_varBytes = 1;
	  m_frameLen = c & 0x3f;
	  m_frameState = FrameState::LengthCont;
	  if (m_varBytes < m_varLen) continue;
	  m_frameState = FrameState::Payload;
	  m_frameOff = 0;
	  if (!m_frameLen) return o + 1;
	  continue;
	}
	if (m_frameState == FrameState::LengthCont) {
	  m_frameLen = (m_frameLen << 8) | c;
	  if (++m_varBytes < m_varLen) continue;
	  m_frameState = FrameState::Payload;
	  m_frameOff = 0;
	  if (!m_frameLen) return o + 1;
	  continue;
	}
	if (m_frameState == FrameState::Payload) {
	  uint64_t avail = span.length() - o;
	  if (avail >= m_frameLen - m_frameOff)
	    return o + int64_t(m_frameLen - m_frameOff);
	  m_frameOff += avail;
	  return 0;
	}
    }
    return 0;
  }

  void resetFrame_() {
    m_frameState = FrameState::Type;
    m_varLen = m_varBytes = 0;
    m_frameLen = m_frameOff = 0;
  }

  bool parseType_(ZuCSpan bytes) {
    unsigned o = 0;
    uint64_t type = 0;
    if (decodeVar(bytes, o, type) < 0 || o != bytes.length())
	return false;
    m_streamType = type;
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

  bool parseSettings_(ZuCSpan payload) {
    unsigned o = 0;
    while (o < payload.length()) {
	uint64_t key = 0, value = 0;
	if (decodeVar(payload, o, key) < 0 ||
	    decodeVar(payload, o, value) < 0)
	  return false;
	if (m_settingsKeys->find(key)) return false;
	m_settingsKeys->add(key, true);
	impl()->setting(key, value);
	if (m_streamState == StreamState::Error) return false;
    }
	impl()->h3State(State::PeerSettingsReceived);
    return true;
  }

  bool parseControlFrame_(ZuCSpan frameBytes) {
    unsigned o = 0;
    uint64_t type = 0, len = 0;
    if (decodeVar(frameBytes, o, type) < 0 ||
	  decodeVar(frameBytes, o, len) < 0 ||
	  frameBytes.length() != o + len)
	return false;
    ZuCSpan payload{frameBytes.data() + o, unsigned(len)};
    switch (type) {
	case 0x04: // SETTINGS
	  if (m_settings) return fail_(SettingsError);
	  m_settings = true;
	  if (!parseSettings_(payload)) return fail_(SettingsError);
	  return true;
	case 0x03: { // CANCEL_PUSH
	  uint64_t id = 0;
	  if (!decodeVarFull(payload, id)) return fail_(FrameError);
	  return fail_(IDError);
	}
	case 0x05: { // PUSH_PROMISE
	  uint64_t id = 0;
	  if (!decodePushPromise(payload, id)) return fail_(FrameError);
	  return fail_(FrameUnexpected);
	}
	case 0x07: { // GOAWAY
	  unsigned p = 0;
	  uint64_t id = 0;
	  if (decodeVar(payload, p, id) < 0 || p != payload.length())
	    return fail_(FrameError);
	  impl()->goaway(id);
	  impl()->h3State(State::Goaway);
	  return true;
	}
	case 0x0d: { // MAX_PUSH_ID
	  uint64_t id = 0;
	  if (!decodeVarFull(payload, id)) return fail_(FrameError);
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

  int drain_(auto &rx) {
    int64_t consumed;
    do {
	consumed = rx.consume(
	  [](ZuBSpan span) -> int64_t { return span.length(); },
	  [](ZuBSpan) { });
	if (consumed < 0) return -1;
    } while (consumed);
    return 0;
  }

public:
  template <typename Stream>
  State::T process(Stream &stream) {
    if (stream.resetReceived()) {
	m_streamState = StreamState::Cancelled;
	return impl()->h3State();
    }

    auto &rx = stream.rxStream();
    int64_t consumed = 0;
    do {
	consumed = 0;
	switch (m_streamState) {
	  default:
	    break;
	  case StreamState::Type: {
	    consumed = rx.template consume<0, "Zhttp.H3.CxnType">(
	      [this](ZuBSpan span) { return this->consumeVar_(span); },
	      [this](ZuBSpan span) {
		if (!this->parseType_(span))
		  m_streamState = StreamState::Error;
		m_varLen = m_varBytes = 0;
	      });
	  } break;
	  case StreamState::Control: {
	    auto frameState = m_frameState;
	    unsigned varLen = m_varLen, varBytes = m_varBytes;
	    uint64_t frameLen = m_frameLen, frameOff = m_frameOff;
	    consumed = rx.template consume<0, "Zhttp.H3.CxnFrame">(
	      [this](ZuBSpan span) { return this->consumeFrame_(span); },
	      [this](ZuBSpan span) {
		if (!this->parseControlFrame_(span))
		  m_streamState = StreamState::Error;
		resetFrame_();
	      });
	    if (!consumed) {
	      m_frameState = frameState;
	      m_varLen = varLen;
	      m_varBytes = varBytes;
	      m_frameLen = frameLen;
	      m_frameOff = frameOff;
	    }
	  } break;
	  case StreamState::Push:
	    consumed = rx.template consume<0, "Zhttp.H3.PushID">(
	      [this](ZuBSpan span) { return this->consumeVar_(span); },
	      [this](ZuBSpan span) {
		uint64_t id = 0;
		if (!decodeVarFull(span, id))
		  fail_(FrameError);
		else
		  fail_(IDError);
		m_varLen = m_varBytes = 0;
	      });
	    break;
	  case StreamState::QPackEncoder:
	    consumed = rx.consume(
	      [](ZuBSpan span) -> int64_t { return span.length(); },
	      [this](ZuBSpan span) {
		if (!this->parseQPack_(m_qpackEncoderParser, span,
		    [](ZuCSpan bytes, QPackDecodedInsn &i) {
		      return QPack::decodeEncoderInsn(bytes, i);
		    },
		    [this](const QPackDecodedInsn &i) {
		      return this->applyEncoderInstruction_(i);
		    }))
		  m_streamState = StreamState::Error;
	      });
	    break;
	  case StreamState::QPackDecoder:
	    consumed = rx.consume(
	      [](ZuBSpan span) -> int64_t { return span.length(); },
	      [this](ZuBSpan span) {
		if (!this->parseQPack_(m_qpackDecoderParser, span,
		    [](ZuCSpan bytes, QPackDecodedInsn &i) {
		      return QPack::decodeDecoderInsn(bytes, i);
		    },
		    [this](const QPackDecodedInsn &i) {
		      return this->applyDecoderInstruction_(i);
		    }))
		  m_streamState = StreamState::Error;
	      });
	    break;
	  case StreamState::Extension:
	    if (drain_(rx) < 0) m_streamState = StreamState::Error;
	    break;
	}
	if (consumed < 0 || m_streamState == StreamState::Error) {
	  impl()->h3State(State::Error);
	  break;
	}
    } while (consumed);

    if (stream.finReceived() &&
	  m_streamState != StreamState::Error &&
	  m_streamState != StreamState::Cancelled &&
	  (m_streamState == StreamState::Type ||
	   m_streamState == StreamState::Push))
      fail_(StreamCreationError);
    else if (stream.finReceived() && rx.empty() &&
	  m_streamState != StreamState::Error &&
	  m_streamState != StreamState::Cancelled) {
      m_streamState = StreamState::Complete;
    }
    return impl()->h3State();
  }

  void reset() {
    m_streamState = StreamState::Type;
    m_streamType = -1;
    m_settings = false;
    m_error = 0;
    m_maxPushID = -1;
    m_settingsKeys->clean();
    m_qpackEncoderParser.reset();
    m_qpackDecoderParser.reset();
    resetFrame_();
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
  // Rx thread exclusive
  State::T		m_cxnState = State::Init;
  StreamState::T	m_streamState = StreamState::Type;
  uint64_t		m_streamType = -1;
  uint64_t		m_error = 0;
  uint64_t		m_maxPushID = -1;
  bool		m_settings = false;
  FrameState::T	m_frameState = FrameState::Type;
  unsigned		m_varLen = 0;
  unsigned		m_varBytes = 0;
  uint64_t		m_frameLen = 0;
  uint64_t		m_frameOff = 0;
  QPackInsnParser	m_qpackEncoderParser;
  QPackInsnParser	m_qpackDecoderParser;
  ZmRef<SettingsKeySet>	m_settingsKeys{new SettingsKeySet};
};

// HTTP/3 request/response stream parser
struct QPackStringRef {
  ZuCSpan	raw;
  bool	huffman = false;
};

template <
  typename Impl,
  bool Request_ = false,
  typename Headers_ = ZuTypeList<>,
  uint64_t MaxBody_ = DefltMaxBody>
class Parser {
public:
  using QPackWriteFn = bool (*)(void *, ZuBSpan);
  using ErrorFn = void (*)(void *, uint64_t);

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
    (void)m_bodyRx.start(
      [this](auto &rx) { impl()->streamRx_(rx); });
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
    if (m_state == State::Stream)
      (void)m_bodyRx.fail(
	[this](auto &rx) { impl()->streamRx_(rx); });
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

  int64_t consumePayload_(ZuBSpan span) {
    auto n = span.length();
    uint64_t remaining = m_frameLen - m_frameOff;
    if (n > remaining) n = remaining;
    if (!(m_frameOff += n)) return n;
    if (m_frameOff == m_frameLen) m_frameState = FrameState::Type;
    return n;
  }

  int64_t consumeFullPayload_(ZuBSpan span) {
    auto n = span.length();
    uint64_t remaining = m_frameLen - m_frameOff;
    if (n >= remaining) {
	m_frameOff = m_frameLen;
	m_frameState = FrameState::Type;
	return remaining;
    }
    m_frameOff += n;
    return 0;
  }

  static int parseString_(
    ZuCSpan in, unsigned &o, unsigned bits, uint8_t huffmanMask,
    QPackStringRef &out) {
    uint64_t len = 0;
    uint8_t first = 0;
    if (Compression::decodePref(in, o, bits, len, &first) < 0 ||
	  in.length() < o + len)
	return -1;
    out.raw = ZuCSpan{in.data() + o, unsigned(len)};
    out.huffman = first & huffmanMask;
    o += unsigned(len);
    return int(out.raw.length());
  }

  template <typename L>
  bool withString_(QPackStringRef ref, L l) {
    if (!ref.huffman)
	return ref.raw.length() <= impl()->h3Params().maxHeaderListSize() &&
	  l(ref.raw);
    uint64_t decodedMax = Compression::Huffman::declen(ref.raw.length());
    if (ZuUnlikely(decodedMax > UINT_MAX ||
	decodedMax > impl()->h3Params().maxHeaderListSize()))
	return false;
    unsigned storageSize = unsigned(decodedMax);
    auto storage = ZmScratch(uint8_t, storageSize, HdrBytes::VHeap);
    int64_t n = Compression::Huffman::decode(
	storage.span(),
	ref.raw);
    if (n < 0) return false;
    storage.length(unsigned(n));
    return l(storage.cspan());
  }

  using FieldState = Fields::Semantics<Request>;

  template <unsigned I>
  bool staticHeader_(FieldState &fields, bool initial) {
    using KV = QPackKV<I>;
    using Key = StaticKey<KV>;
    using Value = QPackValue<KV>;
    if constexpr (ZuIsSame<Value, void>{})
	return qpackHeader_(fields, initial, Key{}(), "");
    else
	return qpackHeader_(fields, initial, Key{}(), Value{}());
  }

  bool qpackHeaderRef_(
    FieldState &fields, bool initial, ZuCSpan name, QPackStringRef valueRef) {
    (void)initial;
    return withString_(valueRef, [this, &fields, name](ZuCSpan value) {
	return fields.field(name, value,
	  [this](ZuBSpan key, ZuBSpan value) {
	    header_(key, value);
	  }) && m_state != State::Error;
    });
  }

  bool qpackHeader_(
    FieldState &fields, bool initial, ZuCSpan name, ZuCSpan value) {
    return qpackHeaderRef_(fields, initial, name, { value, false });
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
	if (!parseUInt64Full_(value, contentLength) ||
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
    int used = QPack::decodeFieldSection(
	payload, rx,
	[this, &fields, initial, &ok](Header h, QPackFieldFlags) {
	  if (!this->qpackHeader_(fields, initial, h.name, h.value))
	    ok = false;
	},
	impl()->h3Params(), 0, 0, &prefix);
    if (used != int(payload.length()) || !ok) return Fields::Invalid;
    if (prefix.requiredInsertCount && !rx) return Fields::Invalid;
    if (prefix.requiredInsertCount) {
	HdrBytes ack;
	if (QPack::encodeSectionAck(ack, impl()->streamID()) < 0)
	  return Fields::Invalid;
	if (!impl()->qpackDecoderWrite(ZuBSpan{ack}))
	  return Fields::Invalid;
    }
    auto section = fields.finish(
      [this](Method::T method, ZuBSpan path) {
	impl()->operation(method, path);
      },
      [this](unsigned status) { impl()->status(status); });
    if constexpr (Request)
      if (section == Fields::Final)
	if (ZuCSpan protocol = fields.protocol())
	  impl()->protocol(protocol);
    if (section != Fields::Invalid)
      impl()->headers(section, false);
    return section;
  }

  bool bodyComplete_() const {
    return m_contentLen < 0 || m_bodyLen == uint64_t(m_contentLen);
  }

  bool processPayloadFrame_(uint64_t type, ZuCSpan payload) {
    switch (type) {
      case 0x00: // DATA
	return processDataPayload_(payload);
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
	if (!decodeVarFull(payload, id)) return fail_(FrameError);
	return fail_(FrameUnexpected);
      }
      case 0x05: { // PUSH_PROMISE
	uint64_t id = 0;
	if (!decodePushPromise(payload, id)) return fail_(FrameError);
	return fail_(Request ? FrameUnexpected : IDError);
      }
      default:
	return true; // ignore unknown extension frames on request streams
    }
  }

  void resetFrame_() {
    m_frameState = FrameState::Type;
    m_varLen = m_varBytes = 0;
    m_frameType = m_frameLen = m_frameOff = 0;
  }

  bool processDataPayload_(ZuCSpan payload) {
    if (m_state == State::Stream) {
      bool ok = m_bodyRx.offer(payload, false,
	[this](auto &rx) { impl()->streamRx_(rx); });
      if (!ok) error_();
      return ok;
    }
    if (m_state != State::Body) return false;
    if (payload.length() > MaxBody ||
	  m_bodyLen > MaxBody - payload.length())
	return false;
    m_bodyLen += payload.length();
    if (m_contentLen >= 0 && m_bodyLen > uint64_t(m_contentLen))
	return false;
    return m_bodyRx.offer(payload, false,
      [this](auto &rx) { impl()->body(rx); });
  }

  template <typename Stream, typename Rx>
  static bool streamComplete_(Stream &stream, Rx &rx) {
    return stream.rxComplete() && rx.empty();
  }

  static int decodeVar_(ZuCSpan in, unsigned &o, uint64_t &v) {
    if (o >= in.length()) return -1;
    uint8_t c = uint8_t(in[o++]);
    unsigned len = 1U << (c >> 6);
    v = c & 0x3f;
    if (in.length() < o + len - 1) return -1;
    for (unsigned i = 1; i < len; ++i) v = (v << 8) | uint8_t(in[o++]);
    return 0;
  }
 
public:
  // top-level process
  template <typename Stream>
  State::T process(Stream &stream) {
    if (m_state == State::Complete ||
	m_state == State::Cancelled ||
	m_state == State::Error)
      return m_state;
    if (stream.resetReceived() ||
	(stream.stopReceived() &&
	  (m_state == State::Stream || m_state == State::RemoteClosed))) {
	if (m_state == State::Stream)
	  (void)m_bodyRx.fail(
	    [this](auto &rx) { impl()->streamRx_(rx); });
	m_state = State::Cancelled;
	complete_(m_state);
	return m_state;
    }

    auto &rx = stream.rxStream();
    int64_t consumed = 0;
    do {
	consumed = 0;
	switch (m_frameState) {
	  default:
	    error_();
	    break;
	  case FrameState::Type: { // parse frame type
	    auto frameState = m_frameState;
	    unsigned varLen = m_varLen, varBytes = m_varBytes;
	    uint64_t frameType = m_frameType;
	    consumed = rx.template consume<0, "Zhttp.H3.Type">(
	      [this](ZuBSpan span) -> int64_t {
		for (unsigned o = 0; o < span.length(); ++o) {
		  uint8_t c = span[o];
		  if (m_frameState == FrameState::Type) {
		    m_frameState = FrameState::TypeCont;
		    m_varLen = 1U << (c >> 6);
		    m_varBytes = 1;
		    m_frameType = c & 0x3f;
		    if (m_varBytes < m_varLen) continue;
		    m_frameState = FrameState::Length;
		    return o + 1;
		  }
		  m_frameType = (m_frameType << 8) | c;
		  if (++m_varBytes < m_varLen) continue;
		  m_frameState = FrameState::Length;
		  return o + 1;
		}
		return 0;
	      },
	      [](ZuBSpan) { });
	    if (!consumed) {
	      m_frameState = frameState;
	      m_varLen = varLen;
	      m_varBytes = varBytes;
	      m_frameType = frameType;
	    }
	  } break;
	  case FrameState::TypeCont: { // parse frame type continuation
	    auto frameState = m_frameState;
	    unsigned varBytes = m_varBytes;
	    uint64_t frameType = m_frameType;
	    consumed = rx.template consume<0, "Zhttp.H3.Type">(
	      [this](ZuBSpan span) -> int64_t {
		for (unsigned o = 0; o < span.length(); ++o) {
		  m_frameType = (m_frameType << 8) | uint8_t(span[o]);
		  if (++m_varBytes < m_varLen) continue;
		  m_frameState = FrameState::Length;
		  return o + 1;
		}
		return 0;
	      },
	      [](ZuBSpan) { });
	    if (!consumed) {
	      m_frameState = frameState;
	      m_varBytes = varBytes;
	      m_frameType = frameType;
	    }
	  } break;
	  case FrameState::Length: { // parse frame length
	    auto frameState = m_frameState;
	    unsigned varLen = m_varLen, varBytes = m_varBytes;
	    uint64_t frameLen = m_frameLen;
	    consumed = rx.template consume<0, "Zhttp.H3.Length">(
	      [this](ZuBSpan span) -> int64_t {
		for (unsigned o = 0; o < span.length(); ++o) {
		  uint8_t c = span[o];
		  if (m_frameState == FrameState::Length) {
		    m_varLen = 1U << (c >> 6);
		    m_varBytes = 1;
		    m_frameLen = c & 0x3f;
		    m_frameState = FrameState::LengthCont;
		    if (m_varBytes < m_varLen) continue;
		    m_frameState = FrameState::Payload;
		    m_frameOff = 0;
		    return o + 1;
		  }
		  m_frameLen = (m_frameLen << 8) | c;
		  if (++m_varBytes < m_varLen) continue;
		  m_frameState = FrameState::Payload;
		  m_frameOff = 0;
		  return o + 1;
		}
		return 0;
	      },
	      [](ZuBSpan) { });
	    if (!consumed) {
	      m_frameState = frameState;
	      m_varLen = varLen;
	      m_varBytes = varBytes;
	      m_frameLen = frameLen;
	    }
	  } break;
	  case FrameState::LengthCont: { // parse frame length continuation
	    auto frameState = m_frameState;
	    unsigned varBytes = m_varBytes;
	    uint64_t frameLen = m_frameLen;
	    consumed = rx.template consume<0, "Zhttp.H3.Length">(
	      [this](ZuBSpan span) -> int64_t {
		for (unsigned o = 0; o < span.length(); ++o) {
		  m_frameLen = (m_frameLen << 8) | uint8_t(span[o]);
		  if (++m_varBytes < m_varLen) continue;
		  m_frameState = FrameState::Payload;
		  m_frameOff = 0;
		  return o + 1;
		}
		return 0;
	      },
	      [](ZuBSpan) { });
	    if (!consumed) {
	      m_frameState = frameState;
	      m_varBytes = varBytes;
	      m_frameLen = frameLen;
	    }
	  } break;
	  case FrameState::Payload: { // parse frame payload
	    if (!m_frameLen) {
	      if (!processPayloadFrame_(m_frameType, {}))
		error_();
	      resetFrame_();
	      consumed = 1;
	      break;
	    }
	    if (m_frameType == 0x00) { // DATA
	      consumed = rx.consume(
		[this](ZuBSpan span) {
		  return this->consumePayload_(span);
		}, [this](ZuBSpan span) {
		  ZuCSpan payload{span};
		  if (!this->processDataPayload_(payload))
		    error_();
		  if (m_frameOff == m_frameLen) resetFrame_();
		});
	      break;
	    }
	    uint64_t frameOff = m_frameOff;
	    consumed = rx.template consume<0, "Zhttp.H3.Payload">(
	      [this](ZuBSpan span) {
		return this->consumeFullPayload_(span);
	      }, [this](ZuBSpan span) {
		ZuCSpan payload{span};
		if (!this->processPayloadFrame_(m_frameType, payload))
		  error_();
		resetFrame_();
	      });
	    if (!consumed) m_frameOff = frameOff;
	  } break;
	}
	if (consumed < 0) error_();
	if (m_state == State::Error) break;
    } while (consumed);

    if (streamComplete_(stream, rx)) {
	if (m_state == State::Stream) {
	  if (!m_bodyRx.finish(
	      [this](auto &rx) { impl()->streamRx_(rx); }))
	    error_();
	  else {
	    m_state = State::RemoteClosed;
	  }
	} else if (m_state == State::Initial)
	  error_();
	else if (!bodyComplete_())
	  error_();
	else if (m_state != State::Error)
	  m_state = State::Complete;
    }
    if (m_state == State::Complete && !m_bodyRx.finish())
      error_();
    if (m_state == State::Complete ||
	  m_state == State::Cancelled ||
	  m_state == State::Error)
	complete_(m_state);
    return m_state;
  }

  void reset() {
    m_bodyRx.reset();
    m_state = State::Initial;
    m_contentLen = -1;
    m_bodyLen = 0;
    m_requestMethod = -1;
    m_extendedConnect = false;
    m_frameState = FrameState::Type;
    m_varLen = m_varBytes = 0;
    m_frameType = m_frameLen = m_frameOff = 0;
    m_h3Error = 0;
    m_complete = false;
  }

  // CRTP defaults
  void operation(Method::T, ZuBSpan) { }
  void status(unsigned) { }
  template <typename Key> void header(ZuBSpan) { }
  template <typename Key, typename Value> void header() { }
  void header(ZuBSpan, ZuBSpan) { }
  void contentLength(uint64_t) { }
  void protocol(ZuBSpan) { }
  void headers(Fields::Section, bool) { }
  template <typename Rx>
  void body(Rx &rx) { bodyDrain(rx); }
  template <typename Rx>
  void streamRx_(Rx &rx) { bodyDrain(rx); }
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
  BodyRx		m_bodyRx;
  Method::T		m_requestMethod = -1;
  State::T		m_state = State::Initial;
  FrameState::T	m_frameState = FrameState::Type;
  unsigned		m_varLen = 0;
  unsigned		m_varBytes = 0;
  uint64_t		m_frameLen = 0;
  uint64_t		m_frameType = 0;
  uint64_t		m_frameOff = 0;
  QPackRxTable		*m_qpackRx = nullptr;
  void			*m_h3Cxn = nullptr;
  QPackWriteFn		m_qpackDecoderWrite = nullptr;
  ErrorFn		m_errorFn = nullptr;
  uint64_t		m_streamID = 0;
  const Params		*m_params = nullptr;
  uint64_t		m_h3Error = 0;
  bool			m_complete = false;
  bool			m_extendedConnect = false;
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
  uint64_t length() const { return n; }
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
inline int encodeFieldPrefix(Bytes &out) {
  out.push(0); // Required Insert Count
  out.push(0); // Delta Base
  return 0;
}

template <typename Bytes, ZuString Key, ZuString Value>
inline int encodeKnownField(Bytes &out) {
  if constexpr (QPackKVIndex<Key, Value>{} >= 0) {
    return putPref(out, 0xc0, 6, QPackKVIndex<Key, Value>{});
  } else if constexpr (QPackKeyIndex<Key>{} >= 0) {
    if (putPref(out, 0x50, 4, QPackKeyIndex<Key>{}) < 0)
	return -1;
    return putString(out, 0, 7, Value);
  } else {
    if (putString(out, 0x20, 3, Key) < 0) return -1;
    return putString(out, 0, 7, Value);
  }
}

template <typename Bytes, ZuString Key>
inline int encodeVariableField(Bytes &out, ZuCSpan value) {
  if constexpr (QPackKeyIndex<Key>{} >= 0) {
    if (putPref(out, 0x50, 4, QPackKeyIndex<Key>{}) < 0)
	return -1;
    return putString(out, 0, 7, value);
  } else {
    if (putString(out, 0x20, 3, Key) < 0) return -1;
    return putString(out, 0, 7, value);
  }
}

template <typename Bytes, ZuString Key>
inline int encodeVariableField(
  Bytes &out, ZuCSpan value1, char sep, ZuCSpan value2) {
  if constexpr (QPackKeyIndex<Key>{} >= 0) {
    if (putPref(out, 0x50, 4, QPackKeyIndex<Key>{}) < 0)
	return -1;
    return putString(out, 0, 7, value1, sep, value2);
  } else {
    if (putString(out, 0x20, 3, Key) < 0) return -1;
    return putString(out, 0, 7, value1, sep, value2);
  }
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
inline int qpackEncodeFieldLine(
  Bytes &out, ZuCSpan name, ZuCSpan value1, char sep, ZuCSpan value2,
  const Params &params) {
  uint64_t nameIndex = 0;
  if (QPack::staticNameIndex(name, nameIndex)) {
    uint8_t prefix = uint8_t(
	0x50 | (params.neverIndex(name) ? 0x20 : 0));
    return putPref(out, prefix, 4, nameIndex) < 0 ||
	putString(out, 0x00, 7, value1, sep, value2) < 0 ?
	-1 : int(out.length());
  }

  return putString(
    out, uint8_t(0x20 | (params.neverIndex(name) ? 0x10 : 0)),
    3, name) < 0 ||
    putString(out, 0x00, 7, value1, sep, value2) < 0 ?
    -1 : int(out.length());
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

template <typename Bytes>
inline int qpackEncodeDynamicName(
  Bytes &out, uint64_t relativeIndex,
  ZuCSpan value1, char sep, ZuCSpan value2, bool never) {
  return putPref(out, uint8_t(0x40 | (never ? 0x20 : 0)), 4,
      relativeIndex) < 0 ||
    putString(out, 0, 7, value1, sep, value2) < 0 ?
    -1 : int(out.length());
}

template <typename Lower>
struct DataStream : public ZiTxLayer<DataStream<Lower>, Lower> {
  using Base = ZiTxLayer<DataStream<Lower>, Lower>;

  DataStream(Lower &lower, uint64_t remaining = uint64_t(-1)) :
    Base(lower, 9, 0), m_remaining{remaining} { }

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
    enum { FramePrefixSize = 16 }; // two maximum-width QUIC varints
    using FrameHdr = ZtArray<uint8_t,
	ZtArrayHeapID<"Zhttp.H3.FrameHdr">>;
    auto frameHdr = ZmScratch(
      uint8_t, FramePrefixSize, FrameHdr::VHeap);
    putVar(frameHdr, 0);
    putVar(frameHdr, buf->length);
    ZiAssert(buf->skip >= frameHdr.length(),
	"Zhttp", (), "H3 DataStream headroom error", return);
    buf->rewind(frameHdr.length());
    memcpy(buf->data(), frameHdr.data(), frameHdr.length());
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
class Builder {
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

private:
  template <typename L>
  void runtimeHeaders_(L &&l) {
    if constexpr (Fields::HasRuntimeBuilder<Impl, L>{})
	impl()->header(ZuFwd<L>(l));
  }

  static ZuCSpan uintSpan_(uint64_t v, ZuCArray<Decimal64Size> &buf) {
    unsigned o = buf.size();
    do {
	buf[--o] = char('0' + (v % 10));
	v /= 10;
    } while (v);
    return ZuCSpan{buf.data() + o, buf.size() - o};
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
    void field(ZuCSpan name, ZuCSpan value1, char sep, ZuCSpan value2) {
	if (!ok) return;
	uint64_t staticName = 0;
	if (tx && dynamic && !QPack::staticNameIndex(name, staticName))
	  if (auto e = tx->findName(name, base)) {
	    ok = qpackEncodeDynamicName(
	      out, base - e->abs - 1, value1, sep, value2,
	      params.neverIndex(name)) >= 0;
	    if (required < e->abs + 1) required = e->abs + 1;
	    if constexpr (Plan) refs.push(e->abs);
	    return;
	  }
	ok = qpackEncodeFieldLine(out, name, value1, sep, value2, params) >= 0;
    }
  };

  template <typename KVs, typename Build>
  bool headers_(Build &build) {
    using HeaderKeys = ZuTypeSlice<2, 0, KVs>;
    using HeaderValues = ZuTypeSlice<2, 1, KVs>;
    ZuUnroll::all<HeaderKeys>([this, &build]<typename Key>() {
	using Value = ZuType<ZuTypeIndex<Key, HeaderKeys>{}, HeaderValues>;
	if constexpr (!ZuIsSame<Value, void>{}) {
	  static_assert(!IsTypeList_<Value>{},
	    "H3 Builder header values must be void or a single value");
	  build.field(Key{}(), Value{}());
	} else {
	  impl()->template header<Key>([&build]<typename V>(V &&v) {
	    ZuCSpan value{ZuFwd<V>(v)};
	    if (value) build.field(Key{}(), value);
	  });
	}
    });
    runtimeHeaders_([&build](ZuCSpan name, ZuCSpan value) {
	if (name) build.field(name, value);
    });
    return build.ok;
  }

  template <typename Build>
  bool contentLength_(Build &build) {
    if constexpr (HasBody && !Streaming) {
	ZuCArray<Decimal64Size> buf;
	build.field("content-length", uintSpan_(impl()->contentLength(), buf));
	return build.ok;
    }
    return true;
  }

  template <typename Build>
  static void encodeMethod_(Build &build, Method::T method) {
    build.field(":method", Method::name(method));
  }

  template <typename Build>
  static void encodePath_(Build &build, ZuCSpan path, ZuCSpan query) {
    if (!query) {
	build.field(":path", path);
	return;
    }
    build.field(":path", path, '?', query);
  }

  template <typename Stream, typename Encode>
  void writeHeaders_(Stream &stream, Encode &&encode) {
    impl()->qpackFailure(QPackBuildFailure::None);
    CountBytes count;
    Build<CountBytes, true> plan{count};
    plan.tx = impl()->qpackTx();
    plan.params = impl()->h3Params();
    if (plan.tx &&
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
public:
  template <typename Stream>
  bool request(Stream &stream) {
    bool valid = true;
    writeHeaders_(stream, [this, &valid](auto &build) {
	impl()->operation([this, &build, &valid]<typename Path, typename Query>(
	    Method::T method, Path &&path, Query &&query) {
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
	  encodePath_(build, ZuCSpan{ZuFwd<Path>(path)},
	    ZuCSpan{ZuFwd<Query>(query)});
	  if (protocol) build.field(":protocol", protocol);
	});
	if (!valid) return false;
	impl()->host([&build]<typename Host>(Host &&host) {
	  build.field(":authority", ZuCSpan{ZuFwd<Host>(host)});
	});
	contentLength_(build);
	headers_<Headers>(build);
	return build.ok;
    });
    return valid && m_qpackFailure == QPackBuildFailure::None;
  }

  // response
  template <typename Stream>
  void response(Stream &stream) {
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
  template <typename Stream>
  auto body(Stream &stream) {
    if constexpr (Streaming)
      return dataStream(stream);
    else
      return dataStream(stream, impl()->contentLength());
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
  bool request() { return true; }
  template <typename L> void operation(L &&l) { l(Method::GET, "/", ""); }
  template <typename L> void host(L &&l) { l("127.0.0.1"); }
  template <typename L> void protocol(L &&) { }
  unsigned status() { return 200; }
  template <typename L> void reason(L &&l) { l(""); }
  template <typename Key, typename L>
  void header(L &&) { }
  template <typename L> void header(L &&) { }
  uint64_t contentLength() { return 0; }
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
};

} // namespace H3

} // namespace Zhttp

#endif /* ZhttpH3_HH */
