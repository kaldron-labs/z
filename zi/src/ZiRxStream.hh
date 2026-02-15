//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// IO Receive Stream
// - owns a queue of receive buffers and presents a consumable stream view
// - applications define:
//   - queue type, which must support count_(), headNode(), shift(),
//     pushNode(node), clean()
// - stream operations:
//   - span()    - current contiguous bytes
//   - advance() - consume bytes from current buffer
//   - next()    - skip to next buffer
//   - empty()   - true when no readable bytes remain
// - queue operations:
//   - pushNode(node)
//   - clean()
//   - count_()

#ifndef ZiRxStream_HH
#define ZiRxStream_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZuSpan.hh>

#include <zlib/ZiIOBuf.hh>

namespace Zi {

template <typename Queue>
class RxStream {
  RxStream(const RxStream &) = delete;
  RxStream &operator =(const RxStream &) = delete;

public:
  RxStream() = default;
  ~RxStream() = default;

  RxStream(RxStream &&) = default;
  RxStream &operator =(RxStream &&) = default;

  template <typename NodeRef>
  void pushNode(NodeRef &&node) {
    m_queue.pushNode(ZuFwd<NodeRef>(node));
    if (!m_cur && m_queue.count_()) m_cur = m_queue.headNode();
  }

  void clean() {
    m_queue.clean();
    m_cur = nullptr;
  }

  auto count_() { return m_queue.count_(); }

  ZuSpan<uint8_t> span() {
    refresh_();
    return m_cur ? m_cur->span() : ZuSpan<uint8_t>{};
  }

  bool advance(size_t n) {
    refresh_();
    if (!m_cur) return false;
    if (n > m_cur->length) n = m_cur->length;
    m_cur->advance(n);
    if (!m_cur->length) pop_();
    return n != 0;
  }

  bool next() {
    refresh_();
    if (!m_cur) return false;
    pop_();
    return m_cur;
  }

  bool empty() {
    refresh_();
    return !m_cur;
  }

private:
  void pop_() {
    if (!m_cur) return;
    m_queue.shift();
    m_cur = m_queue.count_() ? m_queue.headNode() : nullptr;
  }

  void refresh_() {
    if (!m_cur && m_queue.count_()) m_cur = m_queue.headNode();
    while (m_cur && !m_cur->length) pop_();
  }

  Queue		m_queue;
  ZmRef<ZiIOBuf>	m_cur;
};

template <typename Queue>
auto rxStream()
{
  return RxStream<Queue>();
}

} // Zi

template <typename Queue>
using ZiRxStream = Zi::RxStream<Queue>;

#endif /* ZiRxStream_HH */
