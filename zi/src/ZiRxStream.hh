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
//   - consume(frame, data) returns the total number of bytes consumed across all spans
//     - int64_t frame(span) returns the number of bytes to be consumed in span
//     - data(span) delivers contiguous frame data to the app
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

private:
  enum { Locked = !ZuIsSame<ZmNoLock, typename Queue::Lock>{} };
  using NodeRef = ZuIf<typename Queue::NodeRef, typename Queue::NodePtr, Locked>;
  auto head() {
    if constexpr (Locked)
      return m_queue.headNode();
    else
      return m_queue.headPtr();
  }

public:
  // consume(frame, data) returns the total number of bytes consumed across all spans
  // - int64_t frame(span)
  //   - returns the number of bytes N to be consumed in span
  //   - N < 0 indicates an error:
  //     - iteration ends
  //     - no consumption occurs
  //     - data(span) is not called
  //   - if !N, iteration continues to the next span
  //   - if N > 0:
  //     - iteration ends
  //     - prior spans will be consumed entirely
  //     - the current span is consumed by N
  //     - the current span will be included in the data if N > Trailer
  // - data(span) delivers contiguous frame data to the app (length == total - Trailer)
  // - iteration may complete without any consumption having occurred (will return 0)
  template <
    unsigned Trailer = 0,
    typename HeapID = typename Queue::HeapID,
    typename Frame, typename Data>
  int64_t consume(Frame &&frame, Data &&data) {
    int64_t consumed = 0;
    uint64_t count = 0, total = 0;
    {
      auto i = m_queue.citer();
      while (auto node = i()) {
	if (consumed = frame(node->span())) goto framed;
	++count;
	total += node->length;
      }
      return 0;
    }
  framed:
    if (consumed < 0) return consumed; // error
    total += consumed;
    if (Trailer > 0 && ZuUnlikely(total < Trailer)) {
      // edge case - short data
      data(ZuBSpan{});
      goto ret;
    }
    NodeRef head = this->head();
    if (Trailer > 0 &&
	ZuUnlikely(count == 1 && consumed <= Trailer)) {
      // edge case - 2 spans - but 2nd span is trailer-only
      auto span = head->span();
      span.trunc(span.length() - (Trailer - consumed));
      data(span);
      goto ret;
    }
    if (ZuUnlikely(count > 0)) {
      // multiple spans - need assembly into contiguous scratch buffer
      using Scratch = ZtArray<uint8_t, ZtArrayHeapID_<HeapID>>;
      auto scratch = ZtLocalArray(Scratch, total - Trailer);
      do {
	auto span = head->span();
	if (Trailer > 0 &&
	    ZuUnlikely(count == 1 && consumed <= Trailer))
	  span.trunc(span.length() - (Trailer - consumed));
	scratch << span;
	m_queue.shift();
	head = this->head();
      } while (--count);
      if (consumed > Trailer) {
	auto span = head->span();
	span.trunc(consumed - Trailer);
	scratch << span;
      }
      data(scratch.span());
    } else {
      // single span - fast path - can be passed directly
      auto span = head->span();
      span.trunc(consumed - Trailer);
      data(span);
    }
ret:
    if (consumed < head->length)
      head->advance(consumed);
    else
      m_queue.shift();
    return total;
  }

  bool empty() const { return !head(); }

  bool operator !() const { return empty(); }
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
