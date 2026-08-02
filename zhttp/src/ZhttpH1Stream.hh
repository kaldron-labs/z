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

#include <zlib/ZhttpStream.hh>

namespace Zhttp {
namespace H1 {

// Rx-owned Upgrade state and application dispatch.  Transport teardown has
// already drained Rx and Tx when the generic link calls disconnected().
template <typename Link, typename Consumer>
class StreamBinding {
public:
  struct State {
    ZtEnumValues(int8_t, HTTP, Stream, Terminal);
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

  template <typename Rx>
  int process(Rx &rx) { return m_dispatch.process(rx); }

  void reopen() {
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
    if (m_state == State::Stream) {
      if (peer)
	m_dispatch.peerEnd();
      else
	m_dispatch.error();
    }
    m_state = State::Terminal;
    m_dispatch.disable_();
    m_dispatch.final_();
  }

private:
  StreamDispatch<Link, Consumer>	m_dispatch;
  State::T			m_state = State::HTTP;
  uint8_t			m_term = Term::None;
  bool				m_enabled = false;
  bool				m_peerCap = false;
};

} // namespace H1
} // namespace Zhttp

#endif /* ZhttpH1Stream_HH */
