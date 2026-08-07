//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Transport-independent RFC 6455 receive/message codec

#ifndef ZwsCodec_HH
#define ZwsCodec_HH

#ifndef ZwsLib_HH
#include <zlib/ZwsLib.hh>
#endif

#include <zlib/ZuArray.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZiIOBuf.hh>

#include <zlib/ZmScheduler.hh>

#include <zlib/ZhttpFields.hh>
#include <zlib/ZhttpTransport.hh>

#include <zlib/ZwsProtocol.hh>
#include <zlib/ZwsTx.hh>

namespace Zws {

template <typename Impl, typename Link, bool Server, typename Random>
class Codec {
public:
  using Stream = Zhttp::Stream<Link>;
  using Rx = ZiRxStream<ZiRxQueue>;
  using BufAlloc = Zi::IOBufAlloc<
    ZiRxQueue::Node, ZiIOBuf_DefltSize, ZiIOBuf_DefltMaxSize,
    ZuStringT<"Zws.Message.Rx">>;
  using WireBufAlloc = Zi::IOBufAlloc<
    ZiRxQueue::Node, ZiIOBuf_DefltSize, ZiIOBuf_DefltMaxSize,
    ZuStringT<"Zws.Wire.Rx">>;

  Codec(Link &link, Random &random, Config config = {}) :
    m_link{&link}, m_random{&random}, m_config{config} { }

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  template <typename HttpRx>
  int process(Stream stream, HttpRx &httpRx) {
    if (m_terminal || !m_link) return 0;
    m_stream = &stream;
    bool progressed = false;
    bool rejected = false;
    uint64_t lower = httpRx.length();
    uint64_t wire = m_wire.length();
    uint64_t message = m_rx.length();
    if (ZuUnlikely(wire > m_config.maxQueuedInput ||
	message > m_config.maxQueuedInput - wire ||
	lower > m_config.maxQueuedInput - (wire + message))) {
      fail_(Failure::InputPressure);
    } else if (lower) {
      uint64_t remaining = lower;
      int64_t n = httpRx.splice(
	m_wire,
	[&remaining](ZuBSpan span) mutable -> int64_t {
	  if (remaining > span.length()) {
	    remaining -= span.length();
	    return 0;
	  }
	  return remaining;
	}, allocHttp_, allocWire_);
      if (ZuUnlikely(n <= 0))
	fail_(Failure::InvalidHeader);
      else
	progressed = true;
    }
    while (m_failure == Failure::None && !m_terminal) {
      int rc = frame_(m_wire);
      if (rc <= 0) {
	if (rc < 0 && m_failure == Failure::None) rejected = true;
	break;
      }
      progressed = true;
      armPing_();
    }
    if (m_failure != Failure::None) notifyError_();
    bool ended = m_errorEnded;
    m_stream = nullptr;
    if (ended) return progressed ? 1 : 0;
    return m_failure == Failure::None && !rejected ?
      (progressed ? 1 : 0) : -1;
  }

  void peerEnd(Stream stream) {
    if (m_terminal || !m_link) return;
    m_stream = &stream;
    if (!m_peerClosed) {
      fail_(Failure::AbnormalClose);
      notifyError_();
    }
    m_stream = nullptr;
  }

  void streamError(Stream stream) {
    if (m_terminal || !m_link) return;
    m_stream = &stream;
    fail_(Failure::AbnormalClose);
    notifyError_();
    m_stream = nullptr;
  }

  template <typename L>
  void txStream(L &&l, Opcode::T opcode = Opcode::Binary) {
    if (!m_txEnabled.load_()) return;
    Link *link = m_link;
    if (!link) return;
    bool valid = true;
    Stream{*link}.txStream([
      this, opcode, &valid, l = ZuFwd<L>(l)](auto &lower) mutable {
	auto tx = txLayer<!Server>(lower, opcode, m_random);
	l(tx);
	valid = tx.valid();
      });
    if (valid) return;
    auto app = link->app();
    app->rxRun([impl = ZmMkRef(this->impl())]() mutable {
      impl->transmitFailed_();
    });
  }

  void close(uint16_t code = CloseCode::Normal, ZuCSpan reason = {}) {
    if (!m_txEnabled.load_() || reason.length() > MaxControl - 2) return;
    Link *link = m_link;
    if (!link) return;
    ZuBArray<MaxControl> reason_;
    reason_.length(reason.length());
    const unsigned n = reason.length();
    const char *src = reason.data();
    uint8_t *dst = reason_.data();
    for (unsigned i = 0; i < n; ++i) dst[i] = src[i];
    link->app()->rxRun([
      impl = ZmMkRef(this->impl()), code,
      reason = ZuMv(reason_)]() mutable {
	impl->close_(code, reason);
      });
  }

  void ping(ZuBSpan payload = {}) {
    if (!m_txEnabled.load_() || payload.length() > MaxControl) return;
    Link *link = m_link;
    if (!link) return;
    ZuBArray<MaxControl> payload_;
    payload_.length(payload.length());
    const unsigned n = payload.length();
    const uint8_t *src = payload.data();
    uint8_t *dst = payload_.data();
    for (unsigned i = 0; i < n; ++i) dst[i] = src[i];
    link->app()->rxRun([
      impl = ZmMkRef(this->impl()), payload = ZuMv(payload_)]() mutable {
	impl->ping_(payload);
      });
  }

  bool close_(uint16_t code = CloseCode::Normal, ZuCSpan reason = {}) {
    if (m_terminal || !m_link || m_closeSent || !closeCode(code))
      return false;
    unsigned reasonLen = reason.length();
    if (reasonLen > MaxControl - 2) return false;
    UTF8 utf8;
    if (!utf8.update(reason) || !utf8.complete()) return false;
    uint8_t payload[MaxControl];
    payload[0] = uint8_t(code>>8);
    payload[1] = uint8_t(code);
    for (unsigned i = 0; i < reasonLen; ++i)
      payload[i + 2] = reason[i];
    m_closeSent = true;
    stopPing_();
    if (!sendControl_(Opcode::Close, {payload, reasonLen + 2})) {
      transmitFailed_();
      return false;
    }
    m_txEnabled = 0;
    armClose_();
    return true;
  }

  bool ping_(ZuBSpan payload = {}) {
    unsigned payloadLen = payload.length();
    if (m_terminal || !m_link || m_closeSent || m_pingPending ||
	payloadLen > MaxControl)
      return false;
    m_pingPayload.length(payloadLen);
    for (unsigned i = 0; i < payloadLen; ++i)
      m_pingPayload[i] = payload[i];
    m_pingPending = true;
    if (!sendControl_(Opcode::Ping, payload)) {
      transmitFailed_();
      return false;
    }
    armPong_();
    return true;
  }

  void opening_() {
    if (!m_config.handshakeTimeout) return;
    m_link->app()->mx()->add(
      &m_handshakeTimer, Zm::now(m_config.handshakeTimeout),
      ZmScheduler::Update,
      [this](auto &&arm) {
	return arm([this]() { timeout_(); });
      }, m_link->app()->rxThread());
  }

  void up_() {
    m_established = true;
    m_link->app()->mx()->del(&m_handshakeTimer);
    m_txEnabled = 1;
    armPing_();
  }

  Failure::T failure() const { return m_failure; }
  Opcode::T messageOpcode() const { return m_msgOpcode; }
  uint64_t queuedInput() const { return m_wire.length() + m_rx.length(); }
  bool peerClosed() const { return m_peerClosed; }
  bool closeSent() const { return m_closeSent; }

  void reopen_(Link &link) {
    if (m_link) stopTimers_();
    m_txEnabled = 0;
    m_link = &link;
    m_wire.clean();
    m_rx.clean();
    m_frame = {};
    m_utf8.reset();
    m_pingPayload.length(0);
    m_frameRemain = 0;
    m_frameOffset = 0;
    m_msgLength = 0;
    m_msgOpcode = Opcode::Continuation;
    m_failure = Failure::None;
    m_stream = nullptr;
    m_frameReady = false;
    m_msgActive = false;
    m_closeSent = false;
    m_peerClosed = false;
    m_pingPending = false;
    m_errorNotified = false;
    m_errorEnded = false;
    m_established = false;
    m_terminal = false;
  }

  void disable_() {
    m_txEnabled = 0;
    m_terminal = true;
    m_stream = nullptr;
    stopTimers_();
  }

  void final_() {
    disable_();
    m_wire.clean();
    m_rx.clean();
    m_link = nullptr;
  }

  // CRTP defaults
  int messageStart(Opcode::T) { return 1; }
  int message(Rx &rx) { return Zhttp::bodyDrain(rx) ? 1 : -1; }
  int messageEnd() { return 1; }
  void pong(ZuBSpan) { }
  void closed(uint16_t, ZuBSpan) { }
  void error(Failure::T) { }

private:
  static ZmRef<ZiRxQueue::Node> allocHttp_() {
    return new Zhttp::BodyRx::BufAlloc{};
  }

  static ZmRef<ZiRxQueue::Node> allocMessage_() {
    return new BufAlloc{};
  }

  static ZmRef<ZiRxQueue::Node> allocWire_() {
    return new WireBufAlloc{};
  }

  template <typename HttpRx>
  int frame_(HttpRx &rx) {
    bool progressed = false;
    if (!m_frameReady) {
      if (rx.length() < 2) return 0;

      ZuBArray<MaxHeader> header;
      unsigned n = rx.length() < MaxHeader ?
	unsigned(rx.length()) : unsigned(MaxHeader);
      header.length(n);
      if (ZuUnlikely(rx.copy(0, header.span()) != n))
	return fail_(Failure::InvalidHeader), -1;
      ZuBSpan input = header;
      Failure::T failure = Failure::None;
      HeaderParser parser;
      int result = parser.process(input, m_frame, failure);
      if (result == HeaderParser::Result::Wait) return 0;
      if (result == HeaderParser::Result::Error)
	return fail_(failure), -1;
      unsigned headerLen = n - input.length();
      if (ZuUnlikely(m_frame.masked != Server))
	return fail_(Failure::MaskDirection), -1;

      if (control(m_frame.opcode)) {
	if (ZuUnlikely(!m_frame.final))
	  return fail_(Failure::ControlFragment), -1;
	if (ZuUnlikely(m_frame.length > MaxControl))
	  return fail_(Failure::ControlTooLarge), -1;
      } else {
	bool start = m_frame.opcode != Opcode::Continuation;
	if (!start) {
	  if (ZuUnlikely(!m_msgActive))
	    return fail_(Failure::UnexpectedContinuation), -1;
	} else if (ZuUnlikely(m_msgActive))
	  return fail_(Failure::MissingContinuation), -1;
	uint64_t msgLength = start ? 0 : m_msgLength;
	if (ZuUnlikely(msgLength > m_config.maxMessage ||
	    m_frame.length > m_config.maxMessage - msgLength))
	  return fail_(Failure::MessageTooLarge), -1;
	if (start) {
	  m_rx.clean();
	  m_utf8.reset();
	  m_msgOpcode = m_frame.opcode;
	  m_msgActive = true;
	  if (ZuUnlikely(impl()->messageStart(m_msgOpcode) < 0))
	    return reject_();
	}
	m_msgLength = msgLength + m_frame.length;
      }
      rx.advance(headerLen);
      m_frameRemain = m_frame.length;
      m_frameOffset = 0;
      m_frameReady = true;
      progressed = true;
    }

    int rc = control(m_frame.opcode) ? control_(rx) : data_(rx);
    return !rc && progressed ? 1 : rc;
  }

  template <typename HttpRx>
  int data_(HttpRx &rx) {
    uint64_t length = rx.length();
    if (length > m_frameRemain) length = m_frameRemain;
    if (length > uint64_t(INT64_MAX)) length = uint64_t(INT64_MAX);
    if (m_closeSent) {
      if (length) {
	rx.advance(length);
	m_frameRemain -= length;
	m_frameOffset += length;
      }
      if (m_frameRemain) return length ? 1 : 0;
      m_frameReady = false;
      if (m_frame.final) {
	m_rx.clean();
	m_msgActive = false;
	m_msgLength = 0;
      }
      return 1;
    }
    uint64_t queued = m_rx.length();
    if (ZuUnlikely(queued > m_config.maxQueuedInput ||
	length > m_config.maxQueuedInput - queued))
      return fail_(Failure::InputPressure), -1;

    UTF8 utf8 = m_utf8;
    bool final = m_frame.final && length == m_frameRemain;
    bool text = m_msgOpcode == Opcode::Text;
    bool valid = true;
    if constexpr (Server) {
      uint64_t offset = m_frameOffset;
      uint32_t key = m_frame.key;
      int64_t n = rx.each(
	length,
	[&utf8, text, key, &offset](ZuSpan<uint8_t> span) -> int64_t {
	  mask(span, key, offset);
	  if (text && !utf8.update(span)) return -1;
	  offset += span.length();
	  return span.length();
	});
	valid = n >= 0 && uint64_t(n) == length;
    } else if (text) {
      int64_t n = rx.each(length, [&utf8](ZuBSpan span) -> int64_t {
	return utf8.update(span) ? span.length() : -1;
	});
	valid = n >= 0 && uint64_t(n) == length;
    }
    if (ZuUnlikely(!valid || (text && final && !utf8.complete())))
	return fail_(Failure::InvalidUTF8), -1;

    int appRC = 1;
    if (length) {
      uint64_t remaining = length;
      int64_t consumed = rx.splice(
	m_rx,
	[&remaining](ZuBSpan span) mutable -> int64_t {
	  if (remaining > span.length()) {
	    remaining -= span.length();
	    return 0;
	  }
	  return remaining;
	}, allocWire_, allocMessage_);
      if (ZuUnlikely(consumed <= 0))
	return fail_(Failure::InvalidHeader), -1;
      m_utf8 = utf8;
      m_frameRemain -= length;
      m_frameOffset += length;
      appRC = impl()->message(m_rx);
    }
    if (ZuUnlikely(appRC < 0)) return reject_();
    if (m_frameRemain) return length ? 1 : 0;

    m_frameReady = false;
    if (!m_frame.final) return 1;

    m_msgActive = false;
    int endRC = impl()->messageEnd();
    m_rx.clean();
    m_msgLength = 0;
    if (ZuUnlikely(endRC < 0)) return reject_();
    return 1;
  }

  template <typename HttpRx>
  int control_(HttpRx &rx) {
    uint64_t length = rx.length();
    if (length > m_frameRemain) length = m_frameRemain;
    if (!length && m_frameRemain) return 0;
    uint64_t offset = m_frameOffset;
    uint32_t key = m_frame.key;
    int64_t n = rx.each(
      length,
      [this, key, &offset](ZuBSpan span) -> int64_t {
	const unsigned length = span.length();
	if constexpr (Server) {
	  const uint8_t bytes[] = {
	    uint8_t(key>>24), uint8_t(key>>16),
	    uint8_t(key>>8), uint8_t(key)
	  };
	  for (unsigned i = 0; i < length; ++i)
	    m_control[offset + i] =
	      span[i] ^ bytes[(offset + i) & 3];
	} else {
	  (void)key;
	  for (unsigned i = 0; i < length; ++i)
	    m_control[offset + i] = span[i];
	}
	offset += length;
	return length;
      });
    if (ZuUnlikely(n < 0 || uint64_t(n) != length))
      return fail_(Failure::InvalidHeader), -1;
    rx.advance(length);
    m_frameRemain -= length;
    m_frameOffset += length;
    if (m_frameRemain) return 1;

    m_frameReady = false;
    ZuBSpan payload{m_control, unsigned(m_frameOffset)};
    switch (m_frame.opcode) {
      case Opcode::Ping:
	if (!sendControl_(Opcode::Pong, payload))
	  return fail_(Failure::Transmit), -1;
	break;
      case Opcode::Pong:
	pong_(payload);
	break;
      case Opcode::Close:
	if (!peerClose_(payload)) return -1;
	break;
    }
    return 1;
  }

  int reject_() {
    m_txEnabled = 0;
    m_terminal = true;
    m_wire.clean();
    m_rx.clean();
    return -1;
  }

  bool peerClose_(ZuBSpan payload) {
    uint16_t code = CloseCode::NoStatus;
    ZuBSpan reason;
    if (payload.length() == 1)
      return fail_(Failure::InvalidClose), false;
    if (payload.length() >= 2) {
      code = (uint16_t(payload[0])<<8) | payload[1];
      if (!closeCode(code)) return fail_(Failure::InvalidClose), false;
      reason = {payload.data() + 2, payload.length() - 2};
      UTF8 utf8;
      if (!utf8.update(reason) || !utf8.complete())
	return fail_(Failure::InvalidUTF8), false;
    }
    m_peerClosed = true;
    m_wire.clean();
    m_rx.clean();
    m_msgActive = false;
    stopTimers_();
    impl()->closed(code, reason);
    if (!m_closeSent) {
      m_closeSent = true;
      if (!sendControl_(Opcode::Close, payload))
	return fail_(Failure::Transmit), false;
    }
    m_txEnabled = 0;
    m_stream->end();
    m_terminal = true;
    return true;
  }

  bool sendControl_(Opcode::T opcode, ZuBSpan payload) {
    if (!m_link) return false;
    bool valid = true;
    auto send = [this, opcode, payload, &valid](auto &stream) {
      stream.txStream([this, opcode, payload, &valid](auto &lower) {
	auto tx = txLayer<!Server>(lower, opcode, m_random);
	tx << payload;
	tx.flush();
	valid = tx.valid();
      });
    };
    if (m_stream)
      send(*m_stream);
    else {
      Stream stream{*m_link};
      send(stream);
    }
    return valid;
  }

  void fail_(Failure::T failure) {
    if (m_failure == Failure::None) m_failure = failure;
  }

  void notifyError_() {
    if (m_errorNotified) return;
    m_errorNotified = true;
    m_txEnabled = 0;
    stopTimers_();
    m_wire.clean();
    m_rx.clean();
    m_msgActive = false;
    errorClose_();
    impl()->error(m_failure);
    m_terminal = true;
  }

  void errorClose_() {
    if (!m_established || !m_stream || m_closeSent) return;
    uint16_t code;
    switch (m_failure) {
      case Failure::InvalidUTF8:
	code = CloseCode::InvalidData;
	break;
      case Failure::MessageTooLarge:
      case Failure::InputPressure:
	code = CloseCode::TooLarge;
	break;
      case Failure::Transmit:
      case Failure::Handshake:
      case Failure::Capability:
      case Failure::Timeout:
      case Failure::AbnormalClose:
	return;
      default:
	code = CloseCode::Protocol;
	break;
    }
    uint8_t payload[] = {uint8_t(code>>8), uint8_t(code)};
    m_closeSent = true;
    if (!sendControl_(Opcode::Close, payload)) return;
    m_stream->end();
    m_errorEnded = true;
  }

  void pong_(ZuBSpan payload) {
    if (m_pingPending && payload == m_pingPayload.cspan()) {
      m_pingPending = false;
      m_pingPayload.length(0);
      m_link->app()->mx()->del(&m_pongTimer);
      armPing_();
    }
    impl()->pong(payload);
  }

  void armClose_() {
    if (!m_config.closeTimeout) return;
    m_link->app()->mx()->add(
      &m_closeTimer, Zm::now(m_config.closeTimeout), ZmScheduler::Update,
      [this](auto &&arm) {
	return arm([this]() { timeout_(); });
      }, m_link->app()->rxThread());
  }

  void armPing_() {
    if (m_terminal || m_closeSent || m_pingPending ||
	!m_config.pingInterval)
      return;
    m_link->app()->mx()->add(
      &m_pingTimer, Zm::now(m_config.pingInterval), ZmScheduler::Update,
      [this](auto &&arm) {
	return arm([this]() {
	  if (!m_terminal) (void)ping_();
	});
      }, m_link->app()->rxThread());
  }

  void armPong_() {
    if (!m_config.pongTimeout) return;
    m_link->app()->mx()->add(
      &m_pongTimer, Zm::now(m_config.pongTimeout), ZmScheduler::Update,
      [this](auto &&arm) {
	return arm([this]() { timeout_(); });
      }, m_link->app()->rxThread());
  }

  void stopPing_() {
    if (!m_link) return;
    m_link->app()->mx()->del(&m_pingTimer);
    m_link->app()->mx()->del(&m_pongTimer);
    m_pingPending = false;
    m_pingPayload.length(0);
  }

  void stopTimers_() {
    if (!m_link) return;
    stopPing_();
    m_link->app()->mx()->del(&m_handshakeTimer);
    m_link->app()->mx()->del(&m_closeTimer);
  }

  void timeout_() {
    if (m_terminal || !m_link) return;
    fail_(Failure::Timeout);
    stopTimers_();
    notifyError_();
    Stream{*m_link}.reset();
  }

  void transmitFailed_() {
    if (m_terminal || !m_link) return;
    fail_(Failure::Transmit);
    if (m_stream) return;
    stopTimers_();
    notifyError_();
    Stream{*m_link}.reset();
  }

  Link			*m_link;
  Random		*m_random;
  Config		m_config;
  ZmAtomic<unsigned>	m_txEnabled = 0;
  Rx			m_wire;
  Rx			m_rx;
  Stream		*m_stream = nullptr;
  Frame			m_frame;
  UTF8			m_utf8;
  ZuBArray<MaxControl>	m_pingPayload;
  ZmScheduler::Timer	m_closeTimer;
  ZmScheduler::Timer	m_handshakeTimer;
  ZmScheduler::Timer	m_pingTimer;
  ZmScheduler::Timer	m_pongTimer;
  uint64_t		m_frameRemain = 0;
  uint64_t		m_frameOffset = 0;
  uint64_t		m_msgLength = 0;
  uint8_t		m_control[MaxControl];
  Opcode::T		m_msgOpcode = Opcode::Continuation;
  Failure::T		m_failure = Failure::None;
  bool			m_frameReady = false;
  bool			m_msgActive = false;
  bool			m_closeSent = false;
  bool			m_peerClosed = false;
  bool			m_pingPending = false;
  bool			m_errorNotified = false;
  bool			m_errorEnded = false;
  bool			m_established = false;
  bool			m_terminal = false;
};

} // namespace Zws

#endif /* ZwsCodec_HH */
