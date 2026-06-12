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

#include <zlib/ZtArray.hh>
#include <zlib/ZtLocalArray.hh>

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

  auto count_() const { return m_queue.count_(); }

private:
  enum { Locked = !ZuIsSame<ZmNoLock, typename Queue::Lock>{} };
  using NodeRef = ZuIf<Locked, typename Queue::NodeRef, typename Queue::NodePtr>;
  auto head() const {
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
  //     - the current span will be included in the data if N > Padding
  // - data(span) delivers contiguous frame data to the app (length == total - Padding)
  // - iteration may complete without any consumption having occurred (will return 0)
  template <
    unsigned Padding = 0,
    ZuString HeapID = typename Queue::HeapID{}(),
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

    {
      uint64_t dataLen = total;
      uint64_t dataCount = count;
      if constexpr (Padding > 0) {
	if (dataLen > Padding)
	  dataLen -= Padding;
	else
	  dataLen = 0;
	if (ZuUnlikely(dataCount == 1 && consumed <= Padding))
	  // edge case - 2 spans - but 2nd span is entirely padding
	  dataCount = 0;
      }
      if (!dataLen) {
	// edge case - short data
	data(ZuSpan<uint8_t>{});
      } else if (ZuLikely(!dataCount)) {
	// single span - fast path - can be passed directly
	NodeRef head = this->head();
	auto span = head->span();
	span.trunc(dataLen);
	data(span);
      } else {
	// multiple spans - need gathering into contiguous scratch buffer
	using Scratch = ZtArray<uint8_t, ZtArrayHeapID<HeapID>>;
	auto scratch = ZtLocalArray(Scratch, dataLen);
	auto i = m_queue.citer();
	while (dataLen) {
	  auto node = i();
	  auto span = node->span();
	  if (span.length() > dataLen) span.trunc(dataLen);
	  scratch << span;
	  dataLen -= span.length();
	}
	data(scratch.span());
      }
    }

    while (count--) m_queue.shift();
    NodeRef head = this->head();
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

} // Zi

template <typename Queue>
using ZiRxStream = Zi::RxStream<Queue>;

#endif /* ZiRxStream_HH */
