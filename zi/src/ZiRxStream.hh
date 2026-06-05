//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// I/O Receive Stream
// - owns a queue of receive buffers and presents a consumable stream view
// - applications define:
//   - queue type, which must support:
//     count_(), headNode(), shift(), pushNode(node), clean()
// - stream operations:
//   - span()    - current contiguous bytes
//   - spans()   - iterate over pending buffer spans in the stream
//   - advance() - consume bytes from the stream
//   - empty()   - true when no readable bytes remain
// - queue operations:
//   - push(node)
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
  void push(NodeRef &&node) {
    if (node->length) // filter out empty buffers
      m_queue.pushNode(ZuFwd<NodeRef>(node));
  }

  void clean() { m_queue.clean(); }

  auto count_() { return m_queue.count_(); }

  ZuSpan<uint8_t> span() {
    auto head = m_queue.headNode();
    return head ? head->span() : ZuSpan<uint8_t>{};
  }

  template <typename L>
  bool spans(L &&l) {
    auto i = m_queue.citer();
    while (auto node = i())
      if (!l(node->span())) return false;
    return true;
  }

  bool empty() const {
    return !m_queue.headNode();
  }

  bool advance(unsigned n) {
    bool consumed = false;
    while (n) {
      auto head = m_queue.headNode();
      if (!head) break;
      unsigned m = n;
      if (m > head->length) m = head->length;
      head->advance(m);
      n -= m;
      consumed |= m;
      if (!head->length) m_queue.shift();
    }
    return consumed;
  }

  bool operator !() const {
    return !m_queue.headNode();
  }
  ZuOpBool

private:

  Queue			m_queue;
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
