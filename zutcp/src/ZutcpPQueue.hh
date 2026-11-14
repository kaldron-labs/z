//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// TCP byte receive queues and SACK metadata

#ifndef Zutcp_HH
#error "include zlib/Zutcp.hh before this header"
#endif

#include <zlib/ZuPtr.hh>

#include <zlib/ZmPQueue.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZutcpOptions.hh>

namespace Zutcp {

// Borrowed slices release their original packet on heap replacement, rather
// than keeping its physical ingress frame until the slice metadata dies.
struct RxSliceAllocator : public Zi::IOBufAllocator {
  mutable ZmRef<ZiIOBuf> packet;
  Zi::IOBufAllocator *heap;

  RxSliceAllocator(ZmRef<ZiIOBuf> packet_, Zi::IOBufAllocator *heap_) :
    Zi::IOBufAllocator{nullptr, Zi::IOBufType::App, 0},
    packet{ZuMv(packet_)}, heap{heap_} { }

  uint8_t *alloc(unsigned size) const override { return heap->alloc(size); }
  void free(uint8_t *ptr, unsigned size, bool registered) const override {
    if (packet)
      packet = nullptr;
    else
      heap->free(ptr, size, registered);
  }
  ZmRef<Zi::IOBuf> allocBuf(
    void *owner = nullptr, uint32_t length = 0, uint32_t mps = ~uint32_t(0)) override
  {
    return heap->allocBuf(owner, length, mps);
  }
};

struct DataRx : public ZiRxQueue::Node {
  using Base = ZiRxQueue::Node;

  RxSliceAllocator	slice;
  uint64_t		offset = 0;

  DataRx() : Base{nullptr, 0, nullptr}, slice{nullptr, nullptr} { }
  DataRx(
    ZmRef<ZiIOBuf> packet_, const uint8_t *data_, unsigned length_,
    Zi::IOBufAllocator *allocator_, void *owner_, uint64_t offset_) :
    Base{const_cast<uint8_t *>(data_), length_, &slice, owner_, length_, false,
      packet_->registered()},
    slice{ZuMv(packet_), allocator_}, offset{offset_} { }

  ~DataRx() {
    // The allocator is embedded in this derived node. Release while it is
    // still alive, then leave the base destructor no storage to free.
    free_();
    data__ = 0;
  }

  bool attach(ZiIOBuf *stub) {
    ZmAssert_(!stub->data_() && !stub->length);
    auto packet = slice.packet.ptr();
    if (packet && packet->registered() && !packet->builtin() &&
	packet->refCount() == 1 && refCount() == 1) {
      stub->skip = unsigned(data_() + skip - packet->data_());
      stub->size = packet->size;
      stub->length = length;
      stub->data__ = packet->data__;
      packet->data__ = 0;
      slice.packet = nullptr;
      data__ = 0;
      size = skip = length = 0;
      return true;
    }
    // Shared packet slices cannot transfer their physical allocation lease.
    auto ptr = stub->ensure(length);
    if (!ptr) return false;
    memcpy(ptr, data_() + skip, length);
    stub->length = length;
    return true;
  }

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

constexpr auto ZutcpPQueueBufLenAxor() {
  return []<typename T>(const T &v) -> uint64_t { return v.length; };
}

using DataRxPQueueFn =
  ZmPQueueDefaultFn<DataRx,
    ZmPQueueDefaultKeyAxor(),
    ZutcpPQueueBufLenAxor()>;

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
  enum { Max = 128 }; // preserved former public capacity
  // Ordinary focused cases had a 95th percentile of two retained ranges;
  // eight leaves one allocation-free growth class.  The adversarial case
  // reached 256 ranges and deliberately exercises named heap fallback.
  enum { BuiltinRanges = 8 };
  using Ranges = ZtBuiltin<
    ZtArray<TxUnackdRange,
      ZtArrayHeapID<"Zutcp.Connection.TxUnackdRanges">>,
    BuiltinRanges>;
  using Indexed =
    ZmPQueue<TxUnackdRange,
      ZmPQueueNode<ZuObject,
	ZmPQueueHeapID<"Zutcp.Connection.TxUnackdIndex",
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
      return addIndexed(range);
    bool fin = range.fin;
    unsigned i = lowerBound(first);
    if (i && endOffset(m_ranges[i - 1]) >= first)
      --i;
    unsigned begin = i;
    uint64_t removed = 0;
    unsigned n = m_ranges.length();
    while (i < n) {
      uint64_t rangeFirst = m_ranges[i].offset;
      uint64_t rangeEnd = endOffset(m_ranges[i]);
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
    TxUnackdRange merged = makeRange(first, end, fin);
    if (m_ranges.length() - (i - begin) + 1 > BuiltinRanges) {
      promote();
      return addIndexed(range);
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
    if (m_index) return clearIndexed(offset, end);
    unsigned i = lowerBound(offset);
    if (i && endOffset(m_ranges[i - 1]) > offset) --i;
    unsigned begin = i;
    uint64_t removed = 0;
    ZuArray<TxUnackdRange, 2> survivors;
    unsigned n = m_ranges.length();
    while (i < n) {
      const TxUnackdRange &range = m_ranges[i];
      uint64_t first = range.offset;
      uint64_t rangeEnd = endOffset(range);
      if (first >= end) break;
      removed += range.length();
      if (first < offset)
	survivors.push(slice(range, first, offset));
      if (rangeEnd > end)
	survivors.push(slice(range, end, rangeEnd));
      ++i;
    }
    if (i == begin) return false;
    uint64_t retained = 0;
    for (unsigned j = 0, n = survivors.length(); j < n; ++j)
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
	if (endOffset(range) > offset && !l(range)) return false;
      }
      return true;
    }
    unsigned i = lowerBound(offset);
    if (i && endOffset(m_ranges[i - 1]) > offset) --i;
    for (unsigned n = m_ranges.length(); i < n; ++i) {
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
    unsigned i = lowerBound(offset);
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
    for (unsigned i = 0, n = m_ranges.length(); i < n; ++i) {
      if (!m_ranges[i]) return false;
      if (m_ranges[i].offset > uint64_t(-1) - m_ranges[i].length())
	return false;
      if (i && endOffset(m_ranges[i - 1]) >=
	  m_ranges[i].offset)
	return false;
      if (length > uint64_t(-1) - m_ranges[i].length())
	return false;
      length += m_ranges[i].length();
    }
    return length == m_length;
  }

private:
  void promote() {
    m_index = new Indexed{0};
    for (unsigned i = 0, n = m_ranges.length(); i < n; ++i)
      m_index->add(new Indexed::Node{m_ranges[i]});
    m_ranges.clear();
#ifdef ZDEBUG
    ++m_promotions;
#endif
  }
  ZmPQResult::T addIndexed(const TxUnackdRange &range) {
    uint64_t first = range.offset;
    uint64_t end = endOffset(range);
    bool fin = range.fin;
    if (first) {
      auto prev = m_index->find(first - 1);
      if (prev && endOffset(prev->data()) >= first) {
	TxUnackdRange old = prev->data();
	first = old.offset;
	if (endOffset(old) > end) {
	  end = endOffset(old);
	  fin = old.fin;
	} else if (endOffset(old) == end)
	  fin = fin || old.fin;
	(void)m_index->del(old.offset);
      }
    }
    auto iter = m_index->iter(first);
    while (auto node = iter()) {
      const TxUnackdRange &old = node->data();
      if (old.offset > end) break;
      uint64_t oldEnd = endOffset(old);
      if (oldEnd > end) {
	end = oldEnd;
	fin = old.fin;
      } else if (oldEnd == end)
	fin = fin || old.fin;
      (void)iter.del();
    }
    m_index->add(new Indexed::Node{makeRange(first, end, fin)});
    return ZmPQResult::Inserted;
  }
  bool clearIndexed(uint64_t offset, uint64_t end) {
    ZuArray<TxUnackdRange, 2> survivors;
    bool changed = false;
    auto iter = m_index->iter(offset);
    while (auto node = iter()) {
      TxUnackdRange range = node->data();
      uint64_t first = range.offset;
      uint64_t rangeEnd = endOffset(range);
      if (first >= end) break;
      if (rangeEnd <= offset) continue;
      changed = true;
      (void)iter.del();
      if (first < offset)
	survivors.push(slice(range, first, offset));
      if (rangeEnd > end)
	survivors.push(slice(range, end, rangeEnd));
    }
    for (unsigned i = 0, n = survivors.length(); i < n; ++i)
      addIndexed(survivors[i]);
    return changed;
  }
  static uint64_t endOffset(const TxUnackdRange &range) {
    return range.offset + range.length();
  }
  unsigned lowerBound(uint64_t offset) const {
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
  static TxUnackdRange makeRange(
    uint64_t first, uint64_t end, bool fin) {
    if (fin) return TxUnackdRange{first, end - first - 1, true};
    return TxUnackdRange{first, end - first, false};
  }
  static TxUnackdRange slice(
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

using DataRxPQueue =
  ZmPQueue<DataRx,
    ZmPQueueNode<DataRx,
      ZmPQueueHeapID<"Zutcp.Connection.RxQueue",
	ZmPQueueFn<DataRxPQueueFn,
	  ZmPQueueOverwrite<false,
	    ZmPQueueBits<2,
	      ZmPQueueLevels<2>>>>>>>;


// Queue coordinates change as bytes are packetized. Shared storage keeps the
// original backing coordinates for retained frames, even after queue removal.
struct TxStoredRange : public TxBacking {
  uint64_t dataOffset = 0;
  uint32_t offset = 0, bytes = 0;

  TxStoredRange() = default;
  TxStoredRange(ZmRef<ZiIOBuf> buf, uint64_t offset_, unsigned skip = 0) :
    TxBacking{ZuMv(buf)}, dataOffset{offset_},
    offset{pending ? pending->skip + skip : 0},
    bytes{pending ? pending->length - skip : 0} {
    payloadOffset += skip;
    payloadLength -= skip;
  }
  uint64_t key() const { return dataOffset; }
  uint64_t length() const { return bytes; }
  uint64_t clipHead(uint64_t n) {
    if (n > bytes) n = bytes;
    offset += unsigned(n);
    bytes -= unsigned(n);
    dataOffset += n;
    return bytes;
  }
  uint64_t clipTail(uint64_t n) {
    if (n > bytes) n = bytes;
    return bytes -= unsigned(n);
  }
  template <typename I> void write(const I &) { }
};
using TxRanges = ZmPQueue<TxStoredRange,
  ZmPQueueNode<TxStoredRange,
    ZmPQueueHeapID<"Zutcp.Connection.TxRanges",
      ZmPQueueFn<ZmPQueueDefaultFn<TxStoredRange>,
	ZmPQueueBits<2, ZmPQueueLevels<3>>>>>>;

using DataTxPQueue = TxUnackdRanges;

struct RxSpan {
  uint64_t	first = 0;
  uint64_t	last = 0;

  uint64_t length() const { return last - first; }
};

// Rx-owned report ordering only. The receive queue remains the sole owner of
// accepted byte ranges/backing; these markers are never a reassembly registry.
class RxSACK {
public:
  void reset() {
    m_recentCount = 0;
    m_duplicate = {};
    m_dupVersion = 0;
  }
  void accepted(uint64_t first) {
    unsigned n = m_recentCount;
    for (unsigned i = 0; i < n; ++i)
      if (m_recent[i] == first) { n = i; break; }
    if (n == TCPOptions::MaxSACKBlocks) --n;
    for (unsigned i = n; i; --i) m_recent[i] = m_recent[i - 1];
    m_recent[0] = first;
    if (m_recentCount < TCPOptions::MaxSACKBlocks && n == m_recentCount)
      ++m_recentCount;
  }
  // The caller has validated the actual duplicate range and advances the
  // external window serial for each replacement report, even without new DATA.
  bool duplicate(uint64_t first, uint64_t last, uint64_t serial) {
    if (last <= first || last - first >= uint64_t(0x80000000)) return false;
    m_duplicate = {first, last};
    m_dupVersion = serial;
    return true;
  }
  void sent(uint64_t serial, bool actualDSACK) {
    if (actualDSACK && serial >= m_dupVersion) m_duplicate = {};
  }

  // Preserve caller option flags/timestamp values; emit relative wire edges.
  // Return whether the selected snapshot actually includes the pending DSACK.
  template <typename Queue>
  bool fill(const Queue &queue, uint64_t next, TCPOptions &options) {
    RxSpan candidates[TCPOptions::MaxSACKBlocks] = {};
    unsigned ranks[TCPOptions::MaxSACKBlocks] = {};
    unsigned count = 0;
    RxSpan containing;
    queue.spans([this, next, &candidates, &ranks, &count, &containing](const auto &span) {
      uint64_t first = span.key(), last = first + span.length();
      if (first <= next || last <= first) return true;
      if (m_duplicate.first > next && first <= m_duplicate.first &&
	  last >= m_duplicate.last) containing = {first, last};
      unsigned rank = m_recentCount;
      for (unsigned i = 0; i < m_recentCount; ++i)
	if (first <= m_recent[i] && m_recent[i] < last) { rank = i; break; }
      unsigned at = 0;
      while (at < count && ranks[at] <= rank) ++at;
      if (at == TCPOptions::MaxSACKBlocks) return true;
      if (count < TCPOptions::MaxSACKBlocks) ++count;
      for (unsigned i = count - 1; i > at; --i) {
	candidates[i] = candidates[i - 1];
	ranks[i] = ranks[i - 1];
      }
      candidates[at] = {first, last};
      ranks[at] = rank;
      return true;
    });
    options.sackCount = 0;
    for (auto &block : options.sack) block = {};
    unsigned capacity = options.flags & TCPOptions::TS ? 3 : TCPOptions::MaxSACKBlocks;
    bool dsack = m_duplicate.last > m_duplicate.first &&
      (m_duplicate.last <= next || containing.last > containing.first);
    auto append = [&options](const RxSpan &span) {
      options.sack[options.sackCount++] = {uint32_t(span.first), uint32_t(span.last)};
    };
    if (dsack) {
      append(m_duplicate);
      if (m_duplicate.first > next) append(containing);
    }
    bool selected[TCPOptions::MaxSACKBlocks] = {};
    bool included = false;
    for (unsigned i = 0; i < count; ++i) {
      bool containsDuplicate = dsack && m_duplicate.first > next &&
	candidates[i].first == containing.first;
      if (containsDuplicate) { selected[i] = included = true; continue; }
      if (options.sackCount >= capacity) continue;
      append(candidates[i]);
      selected[i] = true;
    }
    // Retain normal report priorities, keeping the block containing the latest
    // accepted marker first even when DSACK forces another block onto the wire.
    m_recentCount = 0;
    for (unsigned i = 0; i < count; ++i)
      if (selected[i]) m_recent[m_recentCount++] = candidates[i].first;
    if (dsack && m_duplicate.first > next && !included)
      m_recent[m_recentCount++] = containing.first;
    for (unsigned i = 0; i < count && m_recentCount < TCPOptions::MaxSACKBlocks; ++i)
      if (!selected[i]) m_recent[m_recentCount++] = candidates[i].first;
    return dsack;
  }

private:
  uint64_t	m_recent[TCPOptions::MaxSACKBlocks] = {};
  unsigned	m_recentCount = 0;
  RxSpan	m_duplicate;
  uint64_t	m_dupVersion = 0;
};

ZuDerive(RxSpans, (ZtArray<RxSpan, ZtArrayHeapID<"Zutcp.RxSpans">>));

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
  for (unsigned i = 0, n = spans.length(); i < n; ++i) bytes += spans[i].length();
  return bytes;
}

template <typename Spans, typename Alloc, typename Enqueue>
inline bool queueRxSpans(
  const Spans &spans, uint64_t srcOffset, ZuBSpan payload,
  Alloc alloc, Enqueue enqueue)
{
  for (unsigned i = 0, n = spans.length(); i < n; ++i) {
    uint64_t payloadOffset = spans[i].first - srcOffset;
    uint64_t length64 = spans[i].length();
    if (payloadOffset > payload.length() ||
	length64 > payload.length() - payloadOffset)
      return false;
    unsigned length = unsigned(length64);
    auto buf = alloc(length);
    if (ZuUnlikely(!buf)) return false;
    buf->append(ZuBSpan{payload.data() + payloadOffset, length});
    enqueue(ZuMv(buf), spans[i].first, length);
  }
  return true;
}

} // namespace Zutcp
