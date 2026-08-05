//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC priority-queue item contracts

#ifndef Zquic_HH
#error "include zlib/Zquic.hh before this header"
#endif

#include <zlib/ZuPtr.hh>

#include <zlib/ZmPQueue.hh>

#include <zlib/ZtArray.hh>

namespace Zquic {

struct RxData {
  ZmRef<ZiIOBuf>	buf;
  uint64_t		offset = 0;

  RxData() = default;
  RxData(ZmRef<ZiIOBuf> buf_, uint64_t offset_) :
    buf{ZuMv(buf_)}, offset{offset_} { }

  uint64_t key() const { return offset; }
  uint64_t length() const { return buf ? buf->length : 0; }

  uint64_t clipHead(uint64_t n) {
    uint64_t length = this->length();
    if (!length) return 0;
    if (n > length) n = length;
    offset += n;
    buf->skip += n;
    return buf->length -= n;
  }
  uint64_t clipTail(uint64_t n) {
    uint64_t length = this->length();
    if (!length) return 0;
    if (n > length) n = length;
    return buf->length -= n;
  }
  template <typename I>
  void write(const I &) { }
};

struct StreamRxData : public Zquic_::IOQueue::Node {
  using Base = Zquic_::IOQueue::Node;

  ZmRef<ZiIOBuf>	packet;
  uint64_t		offset = 0;

  StreamRxData() : Base{nullptr, 0, nullptr, 0} { }
  StreamRxData(
    ZmRef<ZiIOBuf> packet_, const uint8_t *data_, unsigned length_,
    void *owner_, uint64_t offset_) :
    Base{const_cast<uint8_t *>(data_), length_, owner_, length_},
    packet{ZuMv(packet_)}, offset{offset_} { }

  uint64_t key() const { return offset; }

  uint64_t clipHead(uint64_t n) {
    if (n > length) n = length;
    offset += n;
    skip += n;
    return length -= n;
  }
  uint64_t clipTail(uint64_t n) {
    if (n > length) n = length;
    return length -= n;
  }
  template <typename I>
  void write(const I &) { }
};

constexpr auto ZquicPQueueBufLenAxor() {
  return []<typename T>(const T &v) -> uint64_t { return v.length; };
}

using StreamRxPQueueFn =
  ZmPQueueDefaultFn<StreamRxData,
    ZmPQueueDefaultKeyAxor(),
    ZquicPQueueBufLenAxor()>;

struct StreamTxData : public ZiIOBuf {
  // ZiIOBuf requires payload storage inline at a stable offset for pool reuse.
  alignas(ZiIOBuf_Align) uint8_t data_[BufSize];
  uint64_t		streamOffset = 0;

  StreamTxData() : ZiIOBuf{data_, BufSize, nullptr} { }
  explicit StreamTxData(void *owner_) : ZiIOBuf{data_, BufSize, owner_} { }

  void publish(uint32_t offset_, uint32_t length_, uint64_t streamOffset_) {
    skip = offset_;
    length = length_;
    streamOffset = streamOffset_;
  }
  uint64_t key() const { return streamOffset; }

  uint64_t clipHead(uint64_t n) {
    if (n > length) n = length;
    skip += n;
    streamOffset += n;
    return length -= n;
  }
  uint64_t clipTail(uint64_t n) {
    if (n > length) n = length;
    return length -= n;
  }
  template <typename I>
  void write(const I &) { }
};

using StreamTxPQueueFn =
  ZmPQueueDefaultFn<StreamTxData,
    ZmPQueueDefaultKeyAxor(),
    ZquicPQueueBufLenAxor()>;

struct RxPktMark {
  uint64_t	pn = 0;

  RxPktMark() = default;
  explicit RxPktMark(uint64_t pn_) : pn{pn_} { }

  uint64_t key() const { return pn; }
  uint64_t length() const { return 1; }
  uint64_t clipHead(uint64_t) { return 0; }
  uint64_t clipTail(uint64_t) { return 0; }
  template <typename I>
  void write(const I &) { }
};

struct TxUnackdRange {
  uint64_t	offset = 0;
  uint64_t	bytes = 0;
  bool		fin = false;

  TxUnackdRange() = default;
  TxUnackdRange(uint64_t offset_, uint64_t bytes_, bool fin_ = false) :
    offset{offset_}, bytes{bytes_}, fin{fin_} { }

  uint64_t key() const { return offset; }
  uint64_t length() const { return bytes + (fin ? 1 : 0); }
  bool operator !() const { return !length(); }
  ZuOpBool

  uint64_t clipHead(uint64_t length) {
    uint64_t n = this->length();
    if (length >= n) {
      offset += n;
      bytes = 0;
      fin = false;
      return 0;
    }
    offset += length;
    if (length <= bytes)
      bytes -= length;
    else {
      bytes = 0;
      fin = false;
    }
    return this->length();
  }
  uint64_t clipTail(uint64_t length) {
    uint64_t n = this->length();
    if (length >= n) {
      bytes = 0;
      fin = false;
      return 0;
    }
    if (fin && length) {
      fin = false;
      --length;
    }
    if (length) bytes -= length;
    return this->length();
  }
  template <typename I>
  void write(const I &) { }
};

class TxUnackdRanges {
public:
  static constexpr unsigned Max = 128; // preserved former public capacity
  // Ordinary focused cases had a 95th percentile of two retained ranges;
  // eight leaves one allocation-free growth class.  The adversarial case
  // reached 256 ranges and deliberately exercises named heap fallback.
  static constexpr unsigned BuiltinRanges = 8;
  using Ranges = ZtBuiltin<
    ZtArray<TxUnackdRange,
      ZtArrayHeapID<"Zquic.Stream.TxUnackdRanges">>,
    BuiltinRanges>;
  using Indexed =
    ZmPQueue<TxUnackdRange,
      ZmPQueueNode<ZuObject,
	ZmPQueueHeapID<"Zquic.Stream.TxUnackdIndex",
	  ZmPQueueBits<2,
	    ZmPQueueLevels<3>>>>>;

  class Node : public ZuObject {
  public:
    Node() = default;
    explicit Node(const TxUnackdRange &range) : m_range{range} { }

    TxUnackdRange &data() { return m_range; }
    const TxUnackdRange &data() const { return m_range; }

  private:
    TxUnackdRange	m_range;
  };

  explicit TxUnackdRanges(uint64_t = 0) { }

  ZmPQResult::T add(Node *node) {
    ZmRef<Node> node_{node};
    if (!node_) return ZmPQResult::Invalid;
    return add(node_->data());
  }
  ZmPQResult::T add(const TxUnackdRange &range) {
    if (range.fin && range.bytes == uint64_t(-1))
      return ZmPQResult::Invalid;
    uint64_t length = range.length();
    if (!length || range.offset > uint64_t(-1) - length)
      return ZmPQResult::Invalid;
    uint64_t first = range.offset;
    uint64_t end = first + length;
    if (m_index)
      return addIndexed_(range);
    bool fin = range.fin;
    unsigned i = lowerBound_(first);
    if (i && end_(m_ranges[i - 1]) >= first)
      --i;
    unsigned begin = i;
    uint64_t removed = 0;
    while (i < m_ranges.length()) {
      uint64_t rangeFirst = m_ranges[i].offset;
      uint64_t rangeEnd = end_(m_ranges[i]);
      if (end < rangeFirst) break;
      if (rangeFirst < first) first = rangeFirst;
      if (rangeEnd > end) {
	end = rangeEnd;
	fin = m_ranges[i].fin;
      } else if (rangeEnd == end)
	fin = fin || m_ranges[i].fin;
      removed += m_ranges[i].length();
      ++i;
    }
    TxUnackdRange merged = makeRange_(first, end, fin);
    if (m_ranges.length() - (i - begin) + 1 > BuiltinRanges) {
      promote_();
      return addIndexed_(range);
    }
    m_ranges.splice(
      begin, i - begin, ZuSpan<const TxUnackdRange>{&merged, 1});
    m_length = m_length - removed + merged.length();
    ZmAssert(verify());
    return ZmPQResult::Inserted;
  }
  bool clear(uint64_t offset, uint64_t length) {
    if (!length || offset > uint64_t(-1) - length) return false;
    uint64_t end = offset + length;
    if (m_index) return clearIndexed_(offset, end);
    unsigned i = lowerBound_(offset);
    if (i && end_(m_ranges[i - 1]) > offset) --i;
    unsigned begin = i;
    uint64_t removed = 0;
    ZuArray<TxUnackdRange, 2> survivors;
    while (i < m_ranges.length()) {
      const TxUnackdRange &range = m_ranges[i];
      uint64_t first = range.offset;
      uint64_t rangeEnd = end_(range);
      if (first >= end) break;
      removed += range.length();
      if (first < offset)
	survivors.push(slice_(range, first, offset));
      if (rangeEnd > end)
	survivors.push(slice_(range, end, rangeEnd));
      ++i;
    }
    if (i == begin) return false;
    uint64_t retained = 0;
    for (unsigned j = 0; j < survivors.length(); ++j)
      retained += survivors[j].length();
    m_ranges.splice(begin, i - begin, survivors.cspan());
    m_length = m_length - removed + retained;
    ZmAssert(verify());
    return true;
  }
  template <typename L>
  bool spans(uint64_t offset, uint64_t length, L &&l) const {
    if (!length) return true;
    if (offset > uint64_t(-1) - length) return false;
    uint64_t end = offset + length;
    if (m_index) {
      auto iter = m_index->citer(offset);
      while (auto node = iter()) {
	const TxUnackdRange &range = node->data();
	if (range.offset >= end) break;
	if (end_(range) > offset && !l(range)) return false;
      }
      return true;
    }
    unsigned i = lowerBound_(offset);
    if (i && end_(m_ranges[i - 1]) > offset) --i;
    for (; i < m_ranges.length(); ++i) {
      uint64_t first = m_ranges[i].offset;
      if (first >= end) break;
      if (!l(m_ranges[i])) return false;
    }
    return true;
  }
  ZmRef<Node> find(uint64_t offset) const {
    if (m_index) {
      auto node = m_index->find(offset);
      if (node && node->data().offset == offset)
	return new Node{node->data()};
      return nullptr;
    }
    unsigned i = lowerBound_(offset);
    if (i < m_ranges.length() && m_ranges[i].offset == offset)
      return new Node{m_ranges[i]};
    return nullptr;
  }
  void clear() {
    m_ranges.clear();
    m_index = nullptr;
    m_length = 0;
  }
  void clean() { clear(); }
  unsigned count_() const {
    return m_index ? m_index->count_() : m_ranges.length();
  }
  uint64_t length_() const {
    return m_index ? m_index->length_() : m_length;
  }
#ifdef ZDEBUG
  unsigned promotions() const { return m_promotions; }
  bool indexed() const { return m_index; }
#endif
  bool verify() const {
    if (m_index) return m_index->verify();
    uint64_t length = 0;
    for (unsigned i = 0; i < m_ranges.length(); ++i) {
      if (!m_ranges[i]) return false;
      if (m_ranges[i].offset > uint64_t(-1) - m_ranges[i].length())
	return false;
      if (i && end_(m_ranges[i - 1]) >=
	  m_ranges[i].offset)
	return false;
      if (length > uint64_t(-1) - m_ranges[i].length())
	return false;
      length += m_ranges[i].length();
    }
    return length == m_length;
  }

private:
  void promote_() {
    m_index = new Indexed{0};
    for (unsigned i = 0; i < m_ranges.length(); ++i)
      m_index->add(new Indexed::Node{m_ranges[i]});
    m_ranges.clear();
#ifdef ZDEBUG
    ++m_promotions;
#endif
  }
  ZmPQResult::T addIndexed_(const TxUnackdRange &range) {
    uint64_t first = range.offset;
    uint64_t end = end_(range);
    bool fin = range.fin;
    if (first) {
      auto prev = m_index->find(first - 1);
      if (prev && end_(prev->data()) >= first) {
	TxUnackdRange old = prev->data();
	first = old.offset;
	if (end_(old) > end) {
	  end = end_(old);
	  fin = old.fin;
	} else if (end_(old) == end)
	  fin = fin || old.fin;
	(void)m_index->del(old.offset);
      }
    }
    auto iter = m_index->iter(first);
    while (auto node = iter()) {
      const TxUnackdRange &old = node->data();
      if (old.offset > end) break;
      uint64_t oldEnd = end_(old);
      if (oldEnd > end) {
	end = oldEnd;
	fin = old.fin;
      } else if (oldEnd == end)
	fin = fin || old.fin;
      (void)iter.del();
    }
    m_index->add(new Indexed::Node{makeRange_(first, end, fin)});
    return ZmPQResult::Inserted;
  }
  bool clearIndexed_(uint64_t offset, uint64_t end) {
    ZuArray<TxUnackdRange, 2> survivors;
    bool changed = false;
    auto iter = m_index->iter(offset);
    while (auto node = iter()) {
      TxUnackdRange range = node->data();
      uint64_t first = range.offset;
      uint64_t rangeEnd = end_(range);
      if (first >= end) break;
      if (rangeEnd <= offset) continue;
      changed = true;
      (void)iter.del();
      if (first < offset)
	survivors.push(slice_(range, first, offset));
      if (rangeEnd > end)
	survivors.push(slice_(range, end, rangeEnd));
    }
    for (unsigned i = 0; i < survivors.length(); ++i)
      addIndexed_(survivors[i]);
    return changed;
  }
  static uint64_t end_(const TxUnackdRange &range) {
    return range.offset + range.length();
  }
  unsigned lowerBound_(uint64_t offset) const {
    unsigned l = 0, r = m_ranges.length();
    while (l < r) {
      unsigned m = (l + r) >> 1;
      if (m_ranges[m].offset < offset)
	l = m + 1;
      else
	r = m;
    }
    return l;
  }
  static TxUnackdRange makeRange_(
    uint64_t first, uint64_t end, bool fin) {
    if (fin) return TxUnackdRange{first, end - first - 1, true};
    return TxUnackdRange{first, end - first, false};
  }
  static TxUnackdRange slice_(
    const TxUnackdRange &range, uint64_t first, uint64_t end) {
    uint64_t dataEnd = range.offset + range.bytes;
    bool fin = range.fin && end == dataEnd + 1;
    uint64_t bytes = 0;
    if (first < dataEnd) {
      uint64_t bytesEnd = end < dataEnd ? end : dataEnd;
      bytes = bytesEnd - first;
    }
    return TxUnackdRange{first, bytes, fin};
  }
  Ranges	m_ranges;
  ZuPtr<Indexed>	m_index;
  uint64_t	m_length = 0;
#ifdef ZDEBUG
  unsigned	m_promotions = 0;
#endif
};

using StreamRxPQueue =
  ZmPQueue<StreamRxData,
    ZmPQueueNode<StreamRxData,
      ZmPQueueHeapID<"Zquic.Stream.RxQueue",
	ZmPQueueFn<StreamRxPQueueFn,
	  ZmPQueueOverwrite<false,
	    ZmPQueueBits<2,
	      ZmPQueueLevels<2>>>>>>>;

using CryptoRxPQueue =
  ZmPQueue<RxData,
    ZmPQueueNode<ZuObject,
      ZmPQueueHeapID<"Zquic.Crypto.RxQueue",
	ZmPQueueOverwrite<false,
	  ZmPQueueBits<2,
	    ZmPQueueLevels<2>>>>>>;

using TxDataPQueue =
  ZmPQueue<StreamTxData,
    ZmPQueueNode<StreamTxData,
      ZmPQueueHeapID<"Zquic.Stream.TxQueue",
	ZmPQueueFn<StreamTxPQueueFn,
	  ZmPQueueBits<2,
	    ZmPQueueLevels<3>>>>>>;

using StreamTxPQueue = TxUnackdRanges;
using CryptoTxPQueue = TxUnackdRanges;

using PktRxPQueue =
  ZmPQueue<RxPktMark,
    ZmPQueueNode<ZuObject,
      ZmPQueueHeapID<"Zquic.Pkt.RxQueue",
	ZmPQueueOverwrite<false,
	  ZmPQueueBits<4,
	    ZmPQueueLevels<4>>>>>>;

struct RxSpan {
  uint64_t	first = 0;
  uint64_t	last = 0;

  uint64_t length() const { return last - first; }
};

ZuDerive(RxSpans, (ZtArray<RxSpan, ZtArrayHeapID<"Zquic.RxSpans">>));

template <typename Queue, typename Spans>
inline bool rxNovelSpans(
  const Queue &queue, uint64_t first, uint64_t end, Spans &spans)
{
  if (end < first) return false;
  return queue.gaps(first, end - first, [&spans](const auto &span) {
    spans << RxSpan{span.key(), span.key() + span.length()};
    return true;
  });
}

template <typename Spans>
inline uint64_t rxSpanBytes(const Spans &spans)
{
  uint64_t bytes = 0;
  for (unsigned i = 0; i < spans.length(); ++i) bytes += spans[i].length();
  return bytes;
}

template <typename Spans, typename Alloc, typename Enqueue>
inline bool queueRxSpans(
  const Spans &spans, uint64_t srcOffset, ZuBSpan payload,
  Alloc alloc, Enqueue enqueue)
{
  for (unsigned i = 0; i < spans.length(); ++i) {
    uint64_t payloadOffset = spans[i].first - srcOffset;
    uint64_t length64 = spans[i].length();
    if (payloadOffset > payload.length() ||
	length64 > payload.length() - payloadOffset)
      return false;
    unsigned length = unsigned(length64);
    ZmRef<ZiIOBuf> buf = alloc(length);
    if (ZuUnlikely(!buf)) return false;
    buf->append(ZuBSpan{payload.data() + payloadOffset, length});
    enqueue(ZuMv(buf), spans[i].first, length);
  }
  return true;
}

} // namespace Zquic
