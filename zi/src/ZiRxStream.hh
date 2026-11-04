//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// I/O Receive Stream
// - owns a queue of receive buffers and presents a consumable stream view
// - applications define:
//   - queue type, which must support:
//     count_(), headNode(), shift(), pushNode(node), unshiftNode(node), clean()
// - stream operations:
//   - scan(frame, pos) recognizes a complete prefix without consuming it
//   - consume(frame, data) returns the total number of bytes consumed across all spans
//     - int64_t frame(mutableSpan) returns the number of bytes to be consumed
//     - data(mutableSpan) delivers contiguous frame data to the app
//   - extract(frame, alloc, range) retains original backing and positions
//     - alloc() is used only for shared fragmented contiguous-only input
//   - splice(dst, frame, head, tail, transform) transfers retained payload ranges
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

namespace Zi {

// A retained view owns the original backing, never its slot or packed flags.
// Positions are relative to data_(), independent of the buffer's active span.
struct RxRange {
  ZmRef<IOBuf>	buf;
  unsigned	offset = 0;
  unsigned	length = 0;

  RxRange() = default;
  RxRange(ZmRef<IOBuf> buf_, unsigned offset_, unsigned length_) :
    buf{ZuMv(buf_)}, offset{offset_ + (buf ? buf->skip : 0)},
    length{length_} { }

  static RxRange fromStorage(ZmRef<IOBuf> buf, unsigned offset, unsigned length) {
    RxRange range;
    range.buf = ZuMv(buf);
    range.offset = offset;
    range.length = length;
    return range;
  }
  RxRange subspan(unsigned start, unsigned length_) const {
    return fromStorage(buf, offset + start, length_);
  }

  auto span() const {
    return buf ? ZuSpan<uint8_t>{buf->data_() + offset, length} :
      ZuSpan<uint8_t>{};
  }
  auto cspan() const { return ZuBSpan{span()}; }
  bool operator !() const { return !buf; }
  ZuOpBool
};

// Original receive nodes carry their own queue links. Only a split shared by
// two queues needs a separate slice node; ordinary nodes have no range owner.
struct RxBuf : public IOBuf {
  const RxRange	*range = nullptr;

  using IOBuf::IOBuf;
};

} // Zi

ZmListDerive(ZiRxQueue, Zi::RxBuf, ZmListNode<Zi::RxBuf, ZmListHeapID<"">>);

namespace Zi {

template <typename Base>
struct RxSlice : public ZmNHeap<RxSlice<Base>>, public Base {
  RxRange value;

  friend inline ZuStringT<"Zi.RxSlice"> ZmNHeapID(RxSlice *);
  RxSlice(RxRange value_) :
    Base{nullptr, 0, value_.buf->owner, 0}, value{ZuMv(value_)}
  {
    this->range = &value;
  }
};

struct RxPass {
  template <typename Span>
  void operator ()(Span) const { }
};

struct RxFramePos {
  uint64_t	count = 0;
  uint64_t	total = 0;
  unsigned	length = 0;
};

template <typename Queue_>
class RxStream {
  RxStream(const RxStream &) = delete;
  RxStream &operator =(const RxStream &) = delete;

public:
  using Queue = Queue_;

  RxStream() = default;
  ~RxStream() = default;

  RxStream(RxStream &&stream) :
    m_queue{ZuMv(stream.m_queue)}, m_length{stream.m_length},
    m_offset{stream.m_offset}
  {
    stream.m_length = stream.m_offset = 0;
  }
  RxStream &operator =(RxStream &&stream) {
    if (this == &stream) return *this;
    m_queue = ZuMv(stream.m_queue);
    m_length = stream.m_length;
    m_offset = stream.m_offset;
    stream.m_length = stream.m_offset = 0;
    return *this;
  }

  template <typename NodeRef>
  void push(NodeRef &&node) {
    unsigned length = nodeSpan_(node).length();
    if (!length) return;
    ZmAssert(m_length <= UINT64_MAX - length);
    m_length += length;
    m_queue.pushNode(ZuFwd<NodeRef>(node));
  }
  void push(RxRange range) {
    if (!range.length) return;
    push(typename Queue::NodeRef{new RxSlice<typename Queue::Node>{ZuMv(range)}});
  }

  void clean() { m_queue.clean(); m_length = m_offset = 0; }
  auto count_() const { return m_queue.count_(); }
  uint64_t length() const { return m_length; }

private:
  enum { Locked = !ZuIsSame<ZmNoLock, typename Queue::Lock>{} };
  using NodeRef = ZuIf<Locked, typename Queue::NodeRef, typename Queue::NodePtr>;
  auto head() const {
    if constexpr (Locked) return m_queue.headNode();
    else return m_queue.headPtr();
  }
  template <typename Node>
  static auto nodeSpan_(const Node &node) {
    return node->range ? node->range->span() :
      static_cast<IOBuf *>(node)->span();
  }
  template <typename Node>
  static RxRange range_(const Node &node, unsigned offset, unsigned length) {
    if (auto range = node->range)
      return range->subspan(offset, length);
    return {static_cast<IOBuf *>(node), offset, length};
  }

public:
  auto span() {
    NodeRef node = head();
    auto span = node ? nodeSpan_(node) : ZuSpan<uint8_t>{};
    span.offset(m_offset);
    return span;
  }
  auto span() const {
    return ZuBSpan{const_cast<RxStream *>(this)->span()};
  }
  RxRange range() const {
    NodeRef node = head();
    return node ? range_(node, m_offset, nodeSpan_(node).length() - m_offset) :
      RxRange{};
  }
  unsigned capacity() const {
    NodeRef node = head();
    if (!node) return 0;
    auto range = node->range;
    auto buf = range ? range->buf.ptr() : static_cast<IOBuf *>(&*node);
    // Stream consumption does not reduce the configured receive capacity.
    return buf->size - (range ? range->offset : buf->skip);
  }

private:
  template <typename Alloc>
  typename Queue::NodeRef gather_(unsigned length, Alloc &&alloc) {
    NodeRef node = head();
    typename Queue::NodeRef buf;
    if (!node->range && node->refCount() == 1 + Locked) {
      unsigned first = node->length - m_offset;
      node->skip += m_offset;
      node->length = first;
      if (ZuUnlikely(!node->ensure(length))) return {};
      buf = m_queue.shift();
      m_length -= first;
      m_offset = 0;
    } else {
      buf = alloc();
      if (ZuUnlikely(!buf || !buf->ensure(length))) return {};
    }
    while (m_length && buf->length < length) {
      auto part = span();
      unsigned remaining = length - buf->length;
      if (part.length() > remaining) part.trunc(remaining);
#ifdef ZiMultiplex_DEBUG
      NodeRef node = head();
      const IOBuf *source = node->range ? node->range->buf.ptr() :
	static_cast<const IOBuf *>(&*node);
      buf->mergeRx_(*source, part.length());
#endif
      buf->append(part);
      advance(part.length());
    }
    return buf;
  }

public:
  // Assemble an oversized frame as it arrives, returning receive slots before
  // waiting for the suffix. Only the final frame buffer grows; trailing bytes
  // keep their original backing and an independent position.
  template <typename Alloc>
  bool gather(unsigned length, Alloc &&alloc) {
    auto buf = gather_(length, ZuFwd<Alloc>(alloc));
    if (ZuUnlikely(!buf)) return false;
    if (m_offset) {
      auto tail = range();
      m_queue.shift();
      m_queue.unshiftNode(new RxSlice<typename Queue::Node>{ZuMv(tail)});
      m_offset = 0;
    }
    m_length += buf->length;
    m_queue.unshiftNode(ZuMv(buf));
    return true;
  }

  uint64_t advance(uint64_t n) {
    if (n > m_length) n = m_length;
    uint64_t advanced = n;
    while (n) {
      NodeRef node = head();
      unsigned available = nodeSpan_(node).length() - m_offset;
      unsigned length = n < available ? unsigned(n) : available;
      if (length < available) m_offset += length;
      else { m_queue.shift(); m_offset = 0; }
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
    offset += m_offset;
    auto i = m_queue.citer();
    while (auto node = i()) {
      auto span = nodeSpan_(node);
      if (offset >= span.length()) { offset -= span.length(); continue; }
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
  int64_t each(uint64_t offset, uint64_t length, L &&l) const {
    return visit_(offset, length, [&l](auto node, unsigned offset, unsigned length) {
      auto span = nodeSpan_(node);
      span.offset(offset);
      span.trunc(length);
      return l(span);
    });
  }
  template <typename L>
  int64_t each(uint64_t length, L &&l) const {
    return each(0, length, ZuFwd<L>(l));
  }
  template <typename L>
  int64_t eachRange(uint64_t length, L &&l) const {
    return visit_(0, length, [&l](auto node, unsigned offset, unsigned length) {
      return l(range_(node, offset, length));
    });
  }

  template <typename Frame>
  int64_t scan(Frame &&frame, RxFramePos &pos, uint64_t offset = 0) {
    pos = {};
    return scan_(ZuFwd<Frame>(frame), pos, offset);
  }

  // This API explicitly requests contiguous callback data. Only fragmented
  // input needs scratch; span consumers use each()/splice() instead.
  template <unsigned Padding = 0,
    ZuString HeapID = typename Queue::HeapID{}(), typename Frame, typename Data>
  int64_t consume(Frame &&frame, Data &&data) {
    RxFramePos pos;
    int64_t framed = scan_(ZuFwd<Frame>(frame), pos);
    if (framed <= 0) return framed;
    uint64_t length = pos.total > Padding ? pos.total - Padding : 0;
    auto first = span();
    if (length <= first.length()) {
      first.trunc(unsigned(length));
      data(first);
    } else {
      if (ZuUnlikely(length > UINT_MAX)) return -1;
      using Scratch = ZtArray<uint8_t, ZtArrayHeapID<HeapID>>;
      auto scratch = ZmScratch(uint8_t, unsigned(length), typename Scratch::VHeap);
      each(length, [&scratch](auto span) -> int64_t {
	scratch << span;
	return span.length();
      });
      data(scratch.span());
    }
    advance(pos.total);
    return framed;
  }

  // Retain a contiguous frame by range. Fragmented contiguous-only delivery
  // reuses exclusive original storage or gathers once into the final owner.
  template <typename Frame, typename Alloc>
  int64_t extract(Frame &&frame, Alloc &&alloc, RxRange &out) {
    out = {};
    RxFramePos pos;
    int64_t framed = scan_(ZuFwd<Frame>(frame), pos);
    if (framed <= 0) return framed;
    if (ZuUnlikely(pos.total > UINT32_MAX)) return -1;
    unsigned length = unsigned(pos.total);
    if (!pos.count) {
      NodeRef node = head();
      if (!node->range && length == node->length - m_offset) {
	out = {m_queue.shift(), m_offset, length};
	m_length -= length;
	m_offset = 0;
	return framed;
      }
      out = range_(node, m_offset, length);
      advance(length);
      return framed;
    }
    auto buf = gather_(length, ZuFwd<Alloc>(alloc));
    if (ZuUnlikely(!buf)) return -1;
    out = {ZuMv(buf), 0, length};
    return framed;
  }

  // Framing removes positions, not payload. Whole nodes move between queues;
  // only independently retained partial nodes allocate slice metadata.
  template <typename Frame, typename Transform = RxPass>
  int64_t splice(RxStream &dst, Frame &&frame,
    uint64_t headLen = 0, uint64_t tailLen = 0, Transform &&transform = {})
  {
    RxFramePos pos;
    int64_t framed = scan_(ZuFwd<Frame>(frame), pos);
    if (framed <= 0) return framed;
    uint64_t total = pos.total;
    if (ZuUnlikely(headLen > total || tailLen > total - headLen)) return -1;
    advance(headLen);
    uint64_t remaining = total - headLen - tailLen;
    while (remaining) {
      NodeRef node = head();
      auto part = span();
      unsigned length = remaining < part.length() ? unsigned(remaining) : part.length();
      part.trunc(length);
      transform(part);
      if (!m_offset && length == nodeSpan_(node).length()) {
	auto moved = m_queue.shift();
	m_length -= length;
	dst.push(ZuMv(moved));
      } else {
	dst.push(range_(node, m_offset, length));
	advance(length);
      }
      remaining -= length;
    }
    advance(tailLen);
    return framed;
  }

  bool empty() const { return !m_length; }
  bool operator !() const { return empty(); }
  ZuOpBool

private:
  template <typename L>
  int64_t visit_(uint64_t offset, uint64_t length, L &&l) const {
    if (offset > m_length || length > m_length - offset) return 0;
    if (length > uint64_t(INT64_MAX)) return -1;
    uint64_t traversed = 0;
    offset += m_offset;
    auto i = m_queue.citer();
    while (length) {
      auto node = i();
      unsigned available = nodeSpan_(node).length();
      if (offset >= available) { offset -= available; continue; }
      unsigned start = unsigned(offset);
      available -= start;
      offset = 0;
      unsigned size = length < available ? unsigned(length) : available;
      int64_t n = l(node, start, size);
      if (ZuUnlikely(n < 0)) return n;
      if (ZuUnlikely(uint64_t(n) != size)) return -1;
      traversed += size;
      length -= size;
    }
    return int64_t(traversed);
  }

  template <typename Frame>
  int64_t scan_(Frame &&frame, RxFramePos &pos, uint64_t offset = 0) {
    if (ZuUnlikely(offset > m_length)) return -1;
    uint64_t skippedTotal = offset;
    offset += m_offset;
    auto i = m_queue.citer();
    while (auto node = i()) {
      auto span = nodeSpan_(node);
      if (offset >= span.length()) {
	offset -= span.length();
	++pos.count;
	continue;
      }
      unsigned skipped = unsigned(offset);
      span.offset(skipped);
      offset = 0;
      int64_t n = frame(span);
      if (ZuUnlikely(n < 0)) return n;
      if (n) {
	if (ZuUnlikely(uint64_t(n) > span.length())) return -1;
	if (ZuUnlikely(skippedTotal > uint64_t(INT64_MAX) - uint64_t(n))) return -1;
	pos.length = skipped + unsigned(n);
	pos.total = skippedTotal + uint64_t(n);
	return int64_t(pos.total);
      }
      if (ZuUnlikely(skippedTotal > uint64_t(INT64_MAX) - span.length())) return -1;
      ++pos.count;
      skippedTotal += span.length();
    }
    return 0;
  }

  Queue			m_queue;
  uint64_t		m_length = 0;
  unsigned		m_offset = 0;
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
