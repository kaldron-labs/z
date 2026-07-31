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
//   - extract(frame, alloc, out) detaches one complete frame into out
//     - frame(span) has the same boundary contract as consume()
//     - alloc() returns a queue-compatible pooled buffer when gathering or
//       preserving a coalesced trailing remainder is required
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

#include <stdint.h>

#include <zlib/ZuSpan.hh>

#include <zlib/ZmNoLock.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZmScratch.hh>

#include <zlib/ZiIOBuf.hh>

namespace Zi {

struct RxRefill {
  ZtEnum(RxRefill, int8_t, Wait, Input, Final, Error);

  uint32_t	length = 0;
  T		state = Wait;
};

namespace RxEvent {
  ZtFlags(RxEvent, uint8_t, Start, Input, Final, Error);
}

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
  auto span() {
    NodeRef node = head();
    return node ? node->span() : ZuSpan<uint8_t>{};
  }
  auto span() const {
    NodeRef node = head();
    return node ? node->span() : ZuSpan<const uint8_t>{};
  }
  unsigned advance(unsigned n) {
    NodeRef node = head();
    if (!node) return 0;
    if (n > node->length) n = node->length;
    if (n < node->length)
      node->advance(n);
    else
      m_queue.shift();
    return n;
  }

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
	if (ZuUnlikely(dataLen > UINT_MAX)) return -1;
	unsigned dataSize = unsigned(dataLen);
	auto scratch = ZmScratch(
	  uint8_t, dataSize, typename Scratch::VHeap);
	auto i = m_queue.citer();
	while (dataLen) {
	  auto node = i();
	  auto span = node->span();
	  if (span.length() > dataLen) span.trunc(dataLen);
	  scratch << span;
	  dataLen -= span.length();
	}
	data(scratch.cspan());
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

  // Detach one complete frame.  A frame wholly occupying the head buffer is
  // returned unchanged.  A coalesced trailing remainder is copied into one
  // successor buffer so the original frame allocation can still be detached.
  // A fragmented frame is copied directly into one final pooled buffer.
  template <typename Frame, typename Alloc>
  int64_t extract(Frame &&frame, Alloc &&alloc, ZmRef<ZiIOBuf> &out) {
    out = nullptr;
    unsigned consumed = 0;
    unsigned count = 0;
    uint64_t total = 0;
    {
      auto i = m_queue.citer();
      while (auto node = i()) {
	int64_t n = frame(node->span());
	if (ZuUnlikely(n < 0)) return n;
	if (n) {
	  if (ZuUnlikely(n > int64_t(node->length))) return -1;
	  consumed = unsigned(n);
	  total += consumed;
	  goto framed;
	}
	++count;
	total += node->length;
      }
      return 0;
    }
  framed:
    if (ZuUnlikely(total > UINT32_MAX)) return -1;
    unsigned length = unsigned(total);
    if (!count) {
      NodeRef node = head();
      if (ZuUnlikely(consumed < node->length)) {
	auto next = alloc();
	if (ZuUnlikely(!next || !next->alloc(node->length - consumed)))
	  return -1;
	next->append(
	  node->span().data() + consumed, node->length - consumed);
	node->length = consumed;
	out = m_queue.shift();
	m_queue.unshiftNode(ZuMv(next));
      } else {
	out = m_queue.shift();
      }
      return length;
    }

    auto buf = alloc();
    if (ZuUnlikely(!buf || !buf->alloc(length))) return -1;
    {
      unsigned remaining = length;
      auto i = m_queue.citer();
      while (remaining) {
	auto node = i();
	auto span = node->span();
	if (span.length() > remaining) span.trunc(remaining);
	buf->append(span);
	remaining -= span.length();
      }
    }
    while (count--) m_queue.shift();
    NodeRef node = head();
    if (consumed < node->length)
      node->advance(consumed);
    else
      m_queue.shift();
    out = ZuMv(buf);
    return length;
  }

  bool empty() const { return !head(); }

  bool operator !() const { return empty(); }
  ZuOpBool

private:

  Queue			m_queue;
};

// Synchronous bounded view over decoder-owned native input.  Impl provides:
//   RxRefill rxRefill_()          - expose the next payload-only region
//   auto rxSpan_()                - current native input span
//   unsigned rxAdvance_(unsigned) - consume payload from native input
//   void rxCancel_()              - discard decoder state on cancellation
// The decoder consumes all hidden framing/control input in rxRefill_().  A
// returned length bounds the view even if rxSpan_() also contains framing or
// bytes belonging to the next logical message.
template <typename Impl>
class RxLayer {
  RxLayer(const RxLayer &) = delete;
  RxLayer &operator =(const RxLayer &) = delete;

public:
  RxLayer(Impl &impl_) : m_impl{&impl_} { }
  ~RxLayer() { clean(); }

  RxLayer(RxLayer &&layer) :
    m_impl{layer.m_impl},
    m_avail{layer.m_avail},
    m_events{layer.m_events},
    m_final{layer.m_final},
    m_complete{layer.m_complete},
    m_failed{layer.m_failed}
  {
    layer.m_impl = nullptr;
    layer.clear_();
  }
  RxLayer &operator =(RxLayer &&layer) {
    if (this == &layer) return *this;
    clean();
    m_impl = layer.m_impl;
    m_avail = layer.m_avail;
    m_events = layer.m_events;
    m_final = layer.m_final;
    m_complete = layer.m_complete;
    m_failed = layer.m_failed;
    layer.m_impl = nullptr;
    layer.clear_();
    return *this;
  }

  void reset() {
    clean();
    clear_();
  }
  void clean() {
    if (m_impl && !m_complete) m_impl->rxCancel_();
    m_avail = 0;
    m_final = false;
    m_complete = true;
  }

  RxEvent::T events() {
    RxEvent::T events = m_events;
    m_events = 0;
    return events;
  }
  bool complete() const { return m_complete; }
  bool failed() const { return m_failed; }
  uint32_t available() const { return m_avail; }

  bool input() {
    return m_avail || refill_();
  }
  bool empty() { return !input(); }

  // Same callback shape as RxStream::consume(), bounded to the currently
  // exposed logical-message region.  Repeated calls refill transparently;
  // one call never crosses into hidden input or the following message.
  template <typename Frame, typename Data>
  int64_t consume(Frame &&frame, Data &&data) {
    if (m_failed) return -1;
    if (!input()) return 0;
    auto span = m_impl->rxSpan_();
    if (ZuUnlikely(!span.length())) return fail_();
    if (span.length() > m_avail) span.trunc(m_avail);
    int64_t n = frame(span);
    if (n <= 0) return n;
    if (ZuUnlikely(uint64_t(n) > span.length())) return fail_();
    span.trunc(unsigned(n));
    data(span);
    if (ZuUnlikely(m_impl->rxAdvance_(unsigned(n)) != unsigned(n)))
      return fail_();
    m_avail -= unsigned(n);
    if (!m_avail && m_final) {
      m_complete = true;
      m_events |= RxEvent::Final();
    }
    return n;
  }

private:
  bool refill_() {
    if (!m_impl || m_complete || m_failed) return false;
    RxRefill refill = m_impl->rxRefill_();
    switch (refill.state) {
      case RxRefill::Wait:
	return false;
      case RxRefill::Input:
      case RxRefill::Final:
	if (ZuUnlikely(!refill.length)) {
	  if (refill.state == RxRefill::Final) {
	    m_final = m_complete = true;
	    m_events |= RxEvent::Final();
	    return false;
	  }
	  return fail_(), false;
	}
	m_avail = refill.length;
	m_final = refill.state == RxRefill::Final;
	m_events |= RxEvent::Input();
	return true;
      case RxRefill::Error:
	return fail_(), false;
    }
    return fail_(), false;
  }
  int64_t fail_() {
    m_avail = 0;
    m_failed = true;
    m_events |= RxEvent::Error();
    return -1;
  }
  void clear_() {
    m_avail = 0;
    m_events = RxEvent::Start();
    m_final = false;
    m_complete = false;
    m_failed = false;
  }

  Impl		*m_impl = nullptr;
  uint32_t	m_avail = 0;
  RxEvent::T	m_events = RxEvent::Start();
  bool		m_final = false;
  bool		m_complete = false;
  bool		m_failed = false;
};

} // Zi

template <typename Queue>
using ZiRxStream = Zi::RxStream<Queue>;

template <typename Impl>
using ZiRxLayer = Zi::RxLayer<Impl>;

#endif /* ZiRxStream_HH */
