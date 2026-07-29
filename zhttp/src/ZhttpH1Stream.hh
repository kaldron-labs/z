//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP/1 logical-stream adaptation

#ifndef ZhttpH1Stream_HH
#define ZhttpH1Stream_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZtEnum.hh>

#include <zlib/ZiRxStream.hh>

#include <zlib/ZhttpStream.hh>

namespace Zhttp {
namespace H1 {

// Callback-scoped, no-copy layer over the native TCP/TLS receive stream.
template <typename NativeRx>
class StreamRx {
public:
  using Layer = ZiRxLayer<StreamRx>;

  StreamRx() : m_layer{*this} { }

  void reset() {
    m_native = nullptr;
    m_state = Zi::RxRefill::Wait;
    m_layer.reset();
  }

  template <typename Dispatch>
  int process(NativeRx &native, Dispatch &dispatch) {
    m_native = &native;
    int rc = dispatch.process(m_layer);
    m_native = nullptr;
    return rc;
  }

  template <typename Dispatch>
  void terminal(Zi::RxRefill::T state, Dispatch &dispatch) {
    if (m_layer.complete() || m_layer.failed()) return;
    m_state = state;
    dispatch.process(m_layer);
  }

  Zi::RxRefill rxRefill_() {
    Zi::RxRefill::T state = m_state;
    m_state = Zi::RxRefill::Wait;
    if (state != Zi::RxRefill::Wait) return {0, state};
    if (!m_native) return {};
    uint32_t length = m_native->span().length();
    return {length, Zi::RxRefill::T(
      length ? Zi::RxRefill::Input : Zi::RxRefill::Wait)};
  }
  ZuBSpan rxSpan_() {
    return m_native ? m_native->span() : ZuBSpan{};
  }
  unsigned rxAdvance_(unsigned n) {
    return m_native ? m_native->advance(n) : 0;
  }
  void rxCancel_() {
    m_native = nullptr;
    m_state = Zi::RxRefill::Wait;
  }

private:
  Layer			m_layer;
  NativeRx		*m_native = nullptr;
  Zi::RxRefill::T	m_state = Zi::RxRefill::Wait;
};

// Rx-owned Upgrade state and application dispatch.  Transport teardown has
// already drained Rx and Tx when the generic link calls disconnected().
template <typename Link, typename Consumer, typename NativeRx>
class StreamBinding {
public:
  struct State {
    ZtEnum(State, int8_t, HTTP, Stream, Terminal);
  };
  struct Term {
    enum { None, End, Reset };
  };

  bool enable(bool enabled) {
    if (m_state != State::HTTP) return false;
    m_enabled = enabled;
    return true;
  }
  bool accept(Link &link, Consumer &consumer) {
    if (!m_enabled || m_state != State::HTTP) return false;
    m_dispatch.init(link, consumer);
    m_peerCap = true;
    m_state = State::Stream;
    return true;
  }

  bool localCap() const { return m_enabled; }
  bool peerCap() const { return m_peerCap; }
  bool stream() const { return m_state == State::Stream; }
  bool terminal() const { return m_state == State::Terminal; }
  bool tx() const { return m_enabled && m_state != State::Terminal; }

  int process(NativeRx &rx) { return m_rx.process(rx, m_dispatch); }

  void reopen() {
    m_rx.reset();
    m_state = State::HTTP;
    m_term = Term::None;
    m_enabled = false;
    m_peerCap = false;
  }

  bool end() {
    if (m_state != State::Stream) return false;
    m_term = Term::End;
    m_state = State::Terminal;
    return true;
  }
  bool reset() {
    if (m_term == Term::Reset) return false;
    m_term = Term::Reset;
    m_state = State::Terminal;
    return true;
  }

  void disconnected(bool peer) {
    if (!m_peerCap) return;
    if (m_state == State::Stream)
      m_rx.terminal(
	peer ? Zi::RxRefill::Final : Zi::RxRefill::Error, m_dispatch);
    m_state = State::Terminal;
    m_dispatch.disable_();
    m_dispatch.final_();
  }

private:
  StreamDispatch<Link, Consumer>	m_dispatch;
  StreamRx<NativeRx>		m_rx;
  State::T			m_state = State::HTTP;
  uint8_t			m_term = Term::None;
  bool				m_enabled = false;
  bool				m_peerCap = false;
};

} // namespace H1
} // namespace Zhttp

#endif /* ZhttpH1Stream_HH */
