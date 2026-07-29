//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP decoded entity-body receive layer

#ifndef ZhttpBody_HH
#define ZhttpBody_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZiRxStream.hh>

namespace Zhttp {

// Synchronous adapter over decoder-owned payload.  HTTP parsers call offer()
// only with entity bytes; framing, padding, trailers, and following messages
// remain decoder-owned.  The application must consume the complete offered
// region before its callback returns.
class BodyRx {
public:
  using Layer = ZiRxLayer<BodyRx>;

  BodyRx() : m_layer{*this} { }

  void reset() {
    m_layer.reset();
    clear_();
  }
  void cancel() {
    m_layer.clean();
    clear_();
  }

  template <typename L>
  bool start(L &&l) {
    if (m_started || m_layer.complete() || m_layer.failed()) return false;
    m_started = true;
    ZuFwd<L>(l)(m_layer);
    return true;
  }

  template <typename L>
  bool offer(ZuBSpan span, bool final, L &&l) {
    if (m_state != Zi::RxRefill::Wait || m_span ||
	m_layer.complete() || m_layer.failed())
      return false;
    m_span = span;
    m_consumed = 0;
    m_state = final ? Zi::RxRefill::Final : Zi::RxRefill::Input;
    if (span)
      ZuFwd<L>(l)(m_layer);
    else if (final)
      (void)m_layer.input();
    else
      m_state = Zi::RxRefill::Wait;
    return !m_span && (!final || m_layer.complete());
  }

  bool finish() {
    return m_layer.complete() ||
      offer({}, true, [](auto &rx) { (void)rx.input(); });
  }
  template <typename L>
  bool finish(L &&l) {
    if (m_layer.complete()) return true;
    if (m_state != Zi::RxRefill::Wait || m_span || m_layer.failed())
      return false;
    m_state = Zi::RxRefill::Final;
    ZuFwd<L>(l)(m_layer);
    return m_layer.complete();
  }
  template <typename L>
  bool fail(L &&l) {
    if (m_layer.complete() || m_layer.failed())
      return m_layer.failed();
    m_span = {};
    m_state = Zi::RxRefill::Error;
    ZuFwd<L>(l)(m_layer);
    return m_layer.failed();
  }

  uint32_t consumed() const { return m_consumed; }
  bool complete() const { return m_layer.complete(); }
  bool failed() const { return m_layer.failed(); }

  Zi::RxRefill rxRefill_() {
    Zi::RxRefill::T state = m_state;
    m_state = Zi::RxRefill::Wait;
    return {uint32_t(m_span.length()), state};
  }
  ZuBSpan rxSpan_() { return m_span; }
  unsigned rxAdvance_(unsigned n) {
    if (n > m_span.length()) return 0;
    m_span.offset(n);
    m_consumed += n;
    return n;
  }
  void rxCancel_() { clear_(); }

private:
  void clear_() {
    m_span = {};
    m_consumed = 0;
    m_state = Zi::RxRefill::Wait;
    m_started = false;
  }

  Layer		m_layer;
  ZuBSpan	m_span;
  uint32_t	m_consumed = 0;
  Zi::RxRefill::T m_state = Zi::RxRefill::Wait;
  bool		m_started = false;
};

template <typename Rx, typename L>
bool bodyEach(Rx &rx, L &&l) {
  while (rx.input()) {
    int64_t n = rx.consume(
      [](ZuBSpan span) -> int64_t { return span.length(); },
      ZuFwd<L>(l));
    if (n < 0) return false;
    if (!n) break;
  }
  return !rx.failed() && !rx.available();
}

template <typename Rx>
bool bodyDrain(Rx &rx) {
  return bodyEach(rx, [](ZuBSpan) { });
}

} // namespace Zhttp

#endif /* ZhttpBody_HH */
