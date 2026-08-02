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
//   - scan(frame, pos) recognizes a complete prefix without consuming it
//   - consume(frame, data) returns the total number of bytes consumed across all spans
//     - int64_t frame(span) returns the number of bytes to be consumed in span
//     - data(span) delivers contiguous frame data to the app
//   - extract(frame, alloc, out) detaches one complete frame into out
//     - frame(span) has the same boundary contract as consume()
//     - alloc() returns a queue-compatible pooled buffer when gathering or
//       preserving a coalesced trailing remainder is required
//   - empty()   - true when no readable bytes remain
//   - length()  - maintained readable-byte count
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
#include <string.h>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuSpan.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmNoLock.hh>
#include <zlib/ZmScratch.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZiIOBuf.hh>

ZuDerive(ZiRxQueue,
  (ZmList<ZiIOBuf, ZmListNode<ZiIOBuf, ZmListHeapID<"">>>));

namespace Zi {

struct RxPass {
  template <typename Span>
  void operator ()(Span) const { }
};

struct RxFramePos {
  uint64_t	count = 0;
  uint64_t	total = 0;
  unsigned	length = 0;
};

template <typename Queue>
class RxStream {
  RxStream(const RxStream &) = delete;
  RxStream &operator =(const RxStream &) = delete;

public:
  RxStream() = default;
  ~RxStream() = default;

  RxStream(RxStream &&stream) :
    m_queue{ZuMv(stream.m_queue)}, m_length{stream.m_length}
  {
    stream.m_length = 0;
  }
  RxStream &operator =(RxStream &&stream) {
    if (this == &stream) return *this;
    m_queue = ZuMv(stream.m_queue);
    m_length = stream.m_length;
    stream.m_length = 0;
    return *this;
  }

  template <typename NodeRef>
  void push(NodeRef &&node) {
    if (node->length) { // filter out empty buffers
      ZmAssert(m_length <= UINT64_MAX - node->length);
      m_length += node->length;
      m_queue.pushNode(ZuFwd<NodeRef>(node));
    }
  }

  void clean() { m_queue.clean(); m_length = 0; }

  auto count_() const { return m_queue.count_(); }
  uint64_t length() const { return m_length; }

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
  uint64_t advance(uint64_t n) {
    if (n > m_length) n = m_length;
    uint64_t advanced = n;
    while (n) {
      NodeRef node = head();
      unsigned length = n < node->length ? unsigned(n) : node->length;
      if (length < node->length)
	node->advance(length);
      else
	m_queue.shift();
      m_length -= length;
      n -= length;
    }
    return advanced;
  }

  unsigned copy(uint64_t offset, ZuSpan<uint8_t> out) const {
    if (offset >= m_length || !out) return 0;
    uint64_t available = m_length - offset;
    if (out.length() > available) out.trunc(unsigned(available));
    unsigned copied = 0;
    auto i = m_queue.citer();
    while (auto node = i()) {
      auto span = node->cspan();
      if (offset >= span.length()) {
	offset -= span.length();
	continue;
      }
      span.offset(unsigned(offset));
      offset = 0;
      unsigned n = out.length() - copied;
      if (n > span.length()) n = span.length();
      memcpy(&out[copied], span.data(), n);
      copied += n;
      if (copied == out.length()) break;
    }
    return copied;
  }

  template <typename L>
  int64_t each(uint64_t offset, uint64_t length, L &&l) {
    if (offset > m_length || length > m_length - offset) return 0;
    if (length > uint64_t(INT64_MAX)) return -1;
    uint64_t traversed = 0;
    auto i = m_queue.citer();
    while (length) {
      auto node = i();
      auto span = node->span();
      if (offset >= span.length()) {
	offset -= span.length();
	continue;
      }
      span.offset(unsigned(offset));
      offset = 0;
      if (span.length() > length) span.trunc(unsigned(length));
      int64_t n = l(span);
      if (ZuUnlikely(n < 0)) return n;
      if (ZuUnlikely(uint64_t(n) != span.length())) return -1;
      traversed += uint64_t(n);
      length -= uint64_t(n);
    }
    return int64_t(traversed);
  }

  template <typename L>
  int64_t each(uint64_t length, L &&l) {
    return each(0, length, ZuFwd<L>(l));
  }

  template <typename Frame>
  int64_t scan(Frame &&frame, Zi::RxFramePos &pos) {
    pos = {};
    return scan_(ZuFwd<Frame>(frame), pos);
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
    RxFramePos pos;
    int64_t framed = scan_(ZuFwd<Frame>(frame), pos);
    if (framed <= 0) return framed;
    uint64_t count = pos.count;
    uint64_t total = pos.total;
    unsigned consumed = pos.length;

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
    m_length -= total;
    return total;
  }

  // Detach one complete frame.  A frame wholly occupying the head buffer is
  // returned unchanged.  At a coalesced boundary, copy the smaller of the
  // frame or trailing remainder.  A fragmented frame is copied directly into
  // one final pooled buffer.
  template <typename Frame, typename Alloc>
  int64_t extract(Frame &&frame, Alloc &&alloc, ZmRef<ZiIOBuf> &out) {
    out = nullptr;
    RxFramePos pos;
    int64_t framed = scan_(ZuFwd<Frame>(frame), pos);
    if (framed <= 0) return framed;
    uint64_t total = pos.total;
    if (ZuUnlikely(total > UINT32_MAX)) return -1;
    unsigned consumed = pos.length;
    unsigned count = unsigned(pos.count);
    unsigned length = unsigned(total);
    if (!count) {
      NodeRef node = head();
      if (ZuUnlikely(consumed < node->length)) {
	unsigned right = node->length - consumed;
	if (consumed <= right) {
	  auto frame = alloc();
	  if (ZuUnlikely(!frame || !frame->alloc(consumed))) return -1;
	  frame->append(node->span().data(), consumed);
	  out = ZuMv(frame);
	  node->advance(consumed);
	} else {
	  auto next = alloc();
	  if (ZuUnlikely(!next || !next->alloc(right))) return -1;
	  next->append(node->span().data() + consumed, right);
	  node->length = consumed;
	  out = m_queue.shift();
	  m_queue.unshiftNode(ZuMv(next));
	}
      } else {
	out = m_queue.shift();
      }
      m_length -= length;
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
    m_length -= length;
    return length;
  }

  // Atomically consume one complete wire frame and append its decoded payload
  // to dst.  head/tail bytes are framing.  srcAlloc/dstAlloc provision a
  // boundary split for the queue which retains the copied side.
  template <typename DstQueue, typename Frame,
    typename SrcAlloc, typename DstAlloc, typename Transform = RxPass>
  int64_t splice(
    RxStream<DstQueue> &dst, Frame &&frame,
    SrcAlloc &&srcAlloc, DstAlloc &&dstAlloc,
    uint64_t headLen = 0, uint64_t tailLen = 0,
    Transform &&transform = {})
  {
    RxFramePos pos;
    int64_t framed = scan_(ZuFwd<Frame>(frame), pos);
    if (framed <= 0) return framed;
    uint64_t total = pos.total;
    if (ZuUnlikely(headLen > total || tailLen > total - headLen))
      return -1;
    uint64_t payloadLen = total - (headLen + tailLen);
    if (!payloadLen) {
      discard_(total);
      return int64_t(total);
    }

    uint64_t payloadEnd = headLen + payloadLen;
    ZiIOBuf *boundary = nullptr;
    unsigned startOff = 0, endOff = 0;
    {
      uint64_t offset = 0;
      auto i = m_queue.citer();
      while (auto node = i()) {
	uint64_t next = offset + node->length;
	if (payloadEnd < next) {
	  boundary = node;
	  endOff = unsigned(payloadEnd - offset);
	  if (headLen > offset) startOff = unsigned(headLen - offset);
	  break;
	}
	offset = next;
      }
    }

    using SrcNodeRef = typename Queue::NodeRef;
    using DstNodeRef = typename DstQueue::NodeRef;
    SrcNodeRef srcSplit;
    DstNodeRef dstSplit;
    bool copyPayload = false;
    if (boundary) {
      unsigned payloadPart = endOff - startOff;
      unsigned right = boundary->length - endOff;
      copyPayload = payloadPart <= right;
      if (copyPayload) {
	dstSplit = dstAlloc();
	if (ZuUnlikely(!dstSplit || !dstSplit->ensure(payloadPart))) return -1;
	dstSplit->append(boundary->data() + startOff, payloadPart);
      } else {
	srcSplit = srcAlloc();
	if (ZuUnlikely(!srcSplit || !srcSplit->ensure(right))) return -1;
	srcSplit->append(boundary->data() + endOff, right);
      }
    }

    discard_(headLen);
    uint64_t remaining = payloadLen;
    while (remaining) {
      NodeRef node = head();
      if (remaining >= node->length) {
	unsigned length = node->length;
	transform(node->span());
	auto moved = m_queue.shift();
	m_length -= length;
	dst.push(ZuMv(moved));
	remaining -= length;
	continue;
      }

      unsigned length = unsigned(remaining);
      if (copyPayload) {
	transform(dstSplit->span());
	dst.push(ZuMv(dstSplit));
	advance(length);
      } else {
	node->length = length;
	transform(node->span());
	auto moved = m_queue.shift();
	m_length -= length;
	m_queue.unshiftNode(ZuMv(srcSplit));
	dst.push(ZuMv(moved));
      }
      remaining = 0;
    }
    discard_(tailLen);
    return int64_t(total);
  }

  bool empty() const { return !head(); }

  bool operator !() const { return empty(); }
  ZuOpBool

private:

  template <typename Frame>
  int64_t scan_(Frame &&frame, RxFramePos &pos) {
    auto i = m_queue.citer();
    while (auto node = i()) {
      int64_t n = frame(node->span());
      if (ZuUnlikely(n < 0)) return n;
      if (n) {
	if (ZuUnlikely(uint64_t(n) > node->length)) return -1;
	if (ZuUnlikely(pos.total > uint64_t(INT64_MAX) - uint64_t(n)))
	  return -1;
	pos.length = unsigned(n);
	pos.total += uint64_t(n);
	return int64_t(pos.total);
      }
      if (ZuUnlikely(pos.total > uint64_t(INT64_MAX) - node->length))
	return -1;
      ++pos.count;
      pos.total += node->length;
    }
    return 0;
  }

  void discard_(uint64_t length) {
    advance(length);
  }

  Queue			m_queue;
  uint64_t		m_length = 0;
};

// Receive-side protocol geometry.  Each layer adds its framing head/tail room
// to the complete requirement exposed by the layer below it.  The outermost
// values are passed to the lowest pooled-buffer allocation boundary.
template <typename Below>
class RxLayer {
  RxLayer(const RxLayer &) = delete;
  RxLayer &operator =(const RxLayer &) = delete;

public:
  RxLayer(Below &below, unsigned headRoom, unsigned tailRoom) :
    m_below{below},
    m_headRoom{headRoom}, m_tailRoom{tailRoom}
  {
    ZmAssert(m_headRoom <= UINT_MAX - below.headRoom());
    ZmAssert(m_tailRoom <= UINT_MAX - below.tailRoom());
    m_headRoom += below.headRoom();
    m_tailRoom += below.tailRoom();
    ZmAssert(m_headRoom <= maxSize());
    ZmAssert(m_tailRoom <= maxSize() - m_headRoom);
  }
  unsigned maxSize() const { return m_below.maxSize(); }
  unsigned headRoom() const { return m_headRoom; }
  unsigned tailRoom() const { return m_tailRoom; }

private:
  Below			&m_below;
  unsigned		m_headRoom;
  unsigned		m_tailRoom;
};

} // Zi

template <typename Queue>
using ZiRxStream = Zi::RxStream<Queue>;

template <typename Below>
using ZiRxLayer = Zi::RxLayer<Below>;

#endif /* ZiRxStream_HH */
