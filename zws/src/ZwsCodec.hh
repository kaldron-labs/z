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
#include <zlib/ZiRxStream.hh>

#include <zlib/ZmScheduler.hh>

#include <zlib/ZhttpStream.hh>

#include <zlib/ZwsProtocol.hh>
#include <zlib/ZwsTx.hh>

namespace Zws {

template <typename Impl, typename Link, bool Server, typename Random>
class Codec {
public:
  using Stream = Zhttp::Stream<Link>;
  using RxLayer = ZiRxLayer<Codec>;

  Codec(Link &link, Random &random, Config config = {}) :
    m_link{&link}, m_random{&random}, m_config{config}, m_rx{*this}
  {
    m_rx.clean();
  }

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  template <typename HttpRx>
  int process(Stream stream, HttpRx &httpRx) {
    if (m_terminal || !m_link) return 0;
    m_stream = &stream;
    bool progressed = false;
    bool final = false;
    bool failed = false;
    for (;;) {
      bool input = httpRx.input();
      auto events = httpRx.events();
      if (events & Zi::RxEvent::Error()) {
	fail_(Failure::AbnormalClose);
	break;
      }
      final |= events & Zi::RxEvent::Final();
      if (input) {
	if (httpRx.available() > m_config.maxQueuedInput) {
	  fail_(Failure::InputPressure);
	  break;
	}
	int rc = 1;
	int64_t n = httpRx.consume(
	  [](ZuBSpan span) -> int64_t { return span.length(); },
	  [this, &rc](ZuBSpan span) { rc = decode_(span); });
	if (n < 0 || rc < 0) {
	  failed = true;
	  break;
	}
	if (!n) break;
	progressed = true;
	armPing_();
	continue;
      }
      events |= httpRx.events();
      if (events & Zi::RxEvent::Error())
	fail_(Failure::AbnormalClose);
      final |= events & Zi::RxEvent::Final();
      if (final && !m_peerClosed)
	fail_(Failure::AbnormalClose);
      break;
    }
    if (m_failure != Failure::None) notifyError_();
    bool ended = m_errorEnded;
    m_stream = nullptr;
    if (ended) return progressed ? 1 : 0;
    return m_failure == Failure::None && !failed ?
      (progressed ? 1 : 0) : -1;
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
  bool peerClosed() const { return m_peerClosed; }
  bool closeSent() const { return m_closeSent; }

  void reopen_(Link &link) {
    if (m_link) stopTimers_();
    m_txEnabled = 0;
    m_link = &link;
    m_rx.clean();
    m_input = {};
    m_header.reset();
    m_frame = {};
    m_utf8.reset();
    m_pingPayload.length(0);
    m_frameRemain = 0;
    m_frameOffset = 0;
    m_msgLength = 0;
    m_controlLen = 0;
    m_msgOpcode = Opcode::Continuation;
    m_failure = Failure::None;
    m_stream = nullptr;
    m_frameReady = false;
    m_msgActive = false;
    m_layerActive = false;
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
    m_rx.clean();
    m_input = {};
    m_link = nullptr;
  }

  // CRTP defaults
  int message(RxLayer &rx) {
    (void)rx.events();
    while (rx.input()) {
      if (rx.consume(
	  [](ZuBSpan span) -> int64_t { return span.length(); },
	  [](ZuBSpan) { }) <= 0)
	break;
    }
    (void)rx.events();
    return rx.failed() ? -1 : 1;
  }
  void pong(ZuBSpan) { }
  void closed(uint16_t, ZuBSpan) { }
  void error(Failure::T) { }

  Zi::RxRefill rxRefill_() {
    for (;;) {
      if (m_failure != Failure::None) return {0, Zi::RxRefill::Error};
      int result;
      if (!m_frameReady) {
	result = nextFrame_();
	if (result == Next::Wait) return {};
	if (result == Next::Error) return {0, Zi::RxRefill::Error};
      } else {
	result = control(m_frame.opcode) ? Next::Control : Next::Data;
      }
      if (result == Next::Control) {
	if (!control_()) return {};
	continue;
      }
      if (!m_frameRemain) {
	bool final = m_frame.final;
	m_frameReady = false;
	if (!final) continue;
	if (m_msgOpcode == Opcode::Text && !m_utf8.complete()) {
	  fail_(Failure::InvalidUTF8);
	  return {0, Zi::RxRefill::Error};
	}
	m_msgActive = false;
	return {0, Zi::RxRefill::Final};
      }
      if (!m_input) return {};
      uint32_t length = m_input.length() < m_frameRemain ?
	m_input.length() : uint32_t(m_frameRemain);
      auto span = mutable_({m_input.data(), length});
      if constexpr (Server) mask(span, m_frame.key, m_frameOffset);
      bool final = m_frame.final && uint64_t(length) == m_frameRemain;
      if (m_msgOpcode == Opcode::Text) {
	if (!m_utf8.update(span) || (final && !m_utf8.complete())) {
	  fail_(Failure::InvalidUTF8);
	  return {0, Zi::RxRefill::Error};
	}
      }
      return {
	length,
	Zi::RxRefill::T(final ?
	  Zi::RxRefill::Final : Zi::RxRefill::Input)
      };
    }
  }

  ZuBSpan rxSpan_() {
    unsigned length = m_input.length() < m_frameRemain ?
      m_input.length() : unsigned(m_frameRemain);
    return {m_input.data(), length};
  }

  unsigned rxAdvance_(unsigned n) {
    if (n > m_input.length() || n > m_frameRemain) return 0;
    m_input.offset(n);
    m_frameRemain -= n;
    m_frameOffset += n;
    if (!m_frameRemain) {
      m_frameReady = false;
      if (m_frame.final) m_msgActive = false;
    }
    return n;
  }

  void rxCancel_() {
    if (m_layerActive) fail_(Failure::InvalidHeader);
    m_input = {};
    m_layerActive = false;
  }

private:
  struct Next {
    enum { Wait, Data, Control, Error };
  };

  static ZuSpan<uint8_t> mutable_(ZuBSpan span) {
    // The logical Rx contract is a const view over mutable pooled I/O storage.
    // Server-side RFC 6455 unmasking intentionally transforms it in place.
    return {const_cast<uint8_t *>(span.data()), span.length()};
  }

  int decode_(ZuBSpan input) {
    m_input = input;
    bool appFailed = false;
    while (m_input && m_failure == Failure::None) {
      if (!m_msgActive) {
	int result = nextFrame_();
	if (result == Next::Wait) break;
	if (result == Next::Error) break;
	if (result == Next::Control) {
	  if (!control_()) break;
	  continue;
	}
	m_rx.reset();
	m_layerActive = true;
      }
      if (m_closeSent) {
	discardData_();
	continue;
      }
      auto before = m_input.length();
      int rc = impl()->message(m_rx);
      if (rc < 0) {
	appFailed = true;
	m_txEnabled = 0;
	m_terminal = true;
	break;
      }
      if (m_rx.available()) {
	fail_(Failure::InvalidHeader);
	break;
      }
      if (m_rx.complete()) {
	m_layerActive = false;
	rc = impl()->message(m_rx); // deliver consumed message Final event
	if (rc < 0) {
	  appFailed = true;
	  m_txEnabled = 0;
	  m_terminal = true;
	  break;
	}
      }
      if (m_input.length() == before) break;
    }
    bool consumed = !m_input;
    m_input = {};
    if (!consumed && !appFailed && m_failure == Failure::None)
      fail_(Failure::InvalidHeader);
    return m_failure == Failure::None && !appFailed ? 1 : -1;
  }

  int nextFrame_() {
    if (m_frameReady) return control(m_frame.opcode) ?
      Next::Control : Next::Data;
    int result = m_header.process(m_input, m_frame, m_failure);
    if (result == HeaderParser::Result::Wait) return Next::Wait;
    if (result == HeaderParser::Result::Error) return Next::Error;
    if (m_frame.masked != Server)
      return fail_(Failure::MaskDirection), Next::Error;
    if (m_frame.length > m_config.maxFrame)
      return fail_(Failure::FrameTooLarge), Next::Error;
    m_frameRemain = m_frame.length;
    m_frameOffset = 0;
    m_frameReady = true;
    if (control(m_frame.opcode)) {
      if (!m_frame.final)
	return fail_(Failure::ControlFragment), Next::Error;
      if (m_frame.length > MaxControl)
	return fail_(Failure::ControlTooLarge), Next::Error;
      m_controlLen = 0;
      return Next::Control;
    }
    if (m_frame.opcode == Opcode::Continuation) {
      if (!m_msgActive)
	return fail_(Failure::UnexpectedContinuation), Next::Error;
    } else {
      if (m_msgActive)
	return fail_(Failure::MissingContinuation), Next::Error;
      m_msgOpcode = m_frame.opcode;
      m_msgLength = 0;
      m_utf8.reset();
      m_msgActive = true;
    }
    if (m_frame.length > m_config.maxMessage - m_msgLength)
      return fail_(Failure::MessageTooLarge), Next::Error;
    m_msgLength += m_frame.length;
    return Next::Data;
  }

  bool control_() {
    while (m_frameRemain && m_input) {
      unsigned n = m_input.length() < m_frameRemain ?
	m_input.length() : unsigned(m_frameRemain);
      auto span = mutable_({m_input.data(), n});
      if constexpr (Server) mask(span, m_frame.key, m_frameOffset);
      for (unsigned i = 0; i < n; ++i)
	m_control[m_controlLen + i] = span[i];
      m_controlLen += n;
      m_input.offset(n);
      m_frameRemain -= n;
      m_frameOffset += n;
    }
    if (m_frameRemain) return false;
    m_frameReady = false;
    ZuBSpan payload{m_control, m_controlLen};
    switch (m_frame.opcode) {
      case Opcode::Ping:
	if (!sendControl_(Opcode::Pong, payload))
	  return fail_(Failure::Transmit), false;
	break;
      case Opcode::Pong:
	pong_(payload);
	break;
      case Opcode::Close:
	if (!peerClose_(payload)) return false;
	break;
    }
    return true;
  }

  void discardData_() {
    unsigned n = m_input.length() < m_frameRemain ?
      m_input.length() : unsigned(m_frameRemain);
    m_input.offset(n);
    m_frameRemain -= n;
    m_frameOffset += n;
    if (m_frameRemain) return;
    m_frameReady = false;
    if (m_frame.final) m_msgActive = false;
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
    if (m_established) {
      m_rx.reset();
      m_layerActive = true;
      (void)m_rx.input();
      (void)impl()->message(m_rx);
      m_layerActive = false;
    }
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
      case Failure::FrameTooLarge:
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
  RxLayer		m_rx;
  Stream		*m_stream = nullptr;
  ZuBSpan		m_input;
  HeaderParser		m_header;
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
  unsigned		m_controlLen = 0;
  Opcode::T		m_msgOpcode = Opcode::Continuation;
  Failure::T		m_failure = Failure::None;
  bool			m_frameReady = false;
  bool			m_msgActive = false;
  bool			m_layerActive = false;
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
