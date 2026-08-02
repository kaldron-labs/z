//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP decoded entity-body receive queue

#ifndef ZhttpBody_HH
#define ZhttpBody_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <stdint.h>

#include <zlib/ZuString.hh>

#include <zlib/ZmAssert.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiRxStream.hh>

namespace Zhttp {

class BodyRx {
public:
  using Queue = ZiRxQueue;
  using Stream = ZiRxStream<Queue>;
  using BufAlloc = Zi::IOBufAlloc<
    Queue::Node, ZiIOBuf_DefltSize, ZiIOBuf_DefltMaxSize,
    ZuStringT<"Zhttp.Body.Rx">>;

  BodyRx(uint64_t max = uint64_t(-1)) : m_max{max} { }

  void reset() {
    m_rx.clean();
    m_received = m_consumed = 0;
  }
  void reset(uint64_t max) { m_max = max; reset(); }
  uint64_t discard() {
    uint64_t n = m_rx.length();
    m_rx.clean();
    m_consumed += n;
    return n;
  }

  Stream &rx() { return m_rx; }
  const Stream &rx() const { return m_rx; }
  uint64_t received() const { return m_received; }
  uint64_t consumed() const { return m_consumed; }
  uint64_t max() const { return m_max; }

  template <typename NodeRef, typename L>
  bool push(NodeRef &&node, L &&l) {
    unsigned n = node->length;
    if (ZuUnlikely(uint64_t(n) > m_max - m_received)) return false;
    if (!n) return true;
    m_received += n;
    m_rx.push(ZuFwd<NodeRef>(node));
    prompt(ZuFwd<L>(l));
    return true;
  }

  template <typename SrcQueue, typename Frame,
    typename SrcAlloc, typename DstAlloc, typename L,
    typename Transform = Zi::RxPass>
  int64_t splice(
    ZiRxStream<SrcQueue> &src, uint64_t length, Frame &&frame,
    SrcAlloc &&srcAlloc, DstAlloc &&dstAlloc,
    uint64_t headLen, uint64_t tailLen, L &&l,
    Transform &&transform = {})
  {
    if (ZuUnlikely(length > m_max - m_received)) return -1;
    int64_t n = src.splice(
      m_rx, ZuFwd<Frame>(frame),
      ZuFwd<SrcAlloc>(srcAlloc), ZuFwd<DstAlloc>(dstAlloc),
      headLen, tailLen, ZuFwd<Transform>(transform));
    if (n <= 0) return n;
    m_received += length;
    if (length) prompt(ZuFwd<L>(l));
    return n;
  }

  template <typename L>
  uint64_t prompt(L &&l) {
    uint64_t before = m_rx.length();
    ZuFwd<L>(l)(m_rx);
    uint64_t after = m_rx.length();
    ZmAssert(after <= before);
    uint64_t n = before - after;
    m_consumed += n;
    return n;
  }

private:
  Stream	m_rx;
  uint64_t	m_max;
  uint64_t	m_received = 0;
  uint64_t	m_consumed = 0;
};

template <typename Rx, typename L>
bool bodyEach(Rx &rx, L &&l) {
  while (rx) {
    int64_t n = rx.consume(
      [](ZuBSpan span) -> int64_t { return span.length(); },
      l);
    if (ZuUnlikely(n <= 0)) return false;
  }
  return true;
}

template <typename Rx>
bool bodyDrain(Rx &rx) {
  return bodyEach(rx, [](ZuBSpan) { });
}

} // namespace Zhttp

#endif /* ZhttpBody_HH */
