//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC priority-queue item contracts

#ifndef ZquicPQueue_HH
#define ZquicPQueue_HH

#ifndef ZquicLib_HH
#include <zlib/ZquicLib.hh>
#endif

#include <zlib/ZmPQueue.hh>
#include <zlib/ZtArray.hh>

#include <zlib/ZquicBuf.hh>

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
  static constexpr unsigned Max = 128;

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
    uint64_t length = range.length();
    if (!length) return ZmPQResult::Invalid;
    uint64_t first = range.offset;
    uint64_t end = first + length;
    bool fin = range.fin;
    unsigned i = 0;
    while (i < m_count) {
      uint64_t rangeFirst = m_ranges[i].offset;
      uint64_t rangeEnd = rangeFirst + m_ranges[i].length();
      if (rangeEnd < first) { ++i; continue; }
      if (end < rangeFirst) break;
      if (rangeFirst < first) first = rangeFirst;
      if (rangeEnd > end) {
	end = rangeEnd;
	fin = m_ranges[i].fin;
      } else if (rangeEnd == end)
	fin = fin || m_ranges[i].fin;
      remove_(i);
    }
    if (m_count >= Max) return ZmPQResult::Invalid;
    insert_(i, makeRange_(first, end, fin));
    return ZmPQResult::Inserted;
  }
  bool clear(uint64_t offset, uint64_t length) {
    if (!length) return false;
    uint64_t end = offset + length;
    bool changed = false;
    unsigned i = 0;
    while (i < m_count) {
      TxUnackdRange range = m_ranges[i];
      uint64_t first = range.offset;
      uint64_t rangeEnd = first + range.length();
      if (rangeEnd <= offset) { ++i; continue; }
      if (first >= end) break;
      changed = true;
      remove_(i);
      if (first < offset)
	insert_(i++, slice_(range, first, offset));
      if (rangeEnd > end)
	insert_(i++, slice_(range, end, rangeEnd));
    }
    return changed;
  }
  template <typename Fn>
  bool spans(uint64_t offset, uint64_t length, Fn fn) const {
    if (!length) return true;
    uint64_t end = offset + length;
    for (unsigned i = 0; i < m_count; ++i) {
      uint64_t first = m_ranges[i].offset;
      uint64_t rangeEnd = first + m_ranges[i].length();
      if (rangeEnd <= offset) continue;
      if (first >= end) break;
      if (!fn(m_ranges[i])) return false;
    }
    return true;
  }
  ZmRef<Node> find(uint64_t offset) const {
    for (unsigned i = 0; i < m_count; ++i)
      if (m_ranges[i].offset == offset)
	return new Node{m_ranges[i]};
    return nullptr;
  }
  void clear() {
    m_count = 0;
    m_length = 0;
  }
  void clean() { clear(); }
  unsigned count_() const { return m_count; }
  uint64_t length_() const { return m_length; }
  bool verify() const {
    uint64_t length = 0;
    for (unsigned i = 0; i < m_count; ++i) {
      if (!m_ranges[i]) return false;
      if (i && m_ranges[i - 1].offset + m_ranges[i - 1].length() >=
	  m_ranges[i].offset)
	return false;
      length += m_ranges[i].length();
    }
    return length == m_length;
  }

private:
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
  void insert_(unsigned i, const TxUnackdRange &range) {
    unsigned j = m_count++;
    while (j > i) {
      m_ranges[j] = m_ranges[j - 1];
      --j;
    }
    m_ranges[i] = range;
    m_length += range.length();
  }
  void remove_(unsigned i) {
    m_length -= m_ranges[i].length();
    --m_count;
    while (i < m_count) {
      m_ranges[i] = m_ranges[i + 1];
      ++i;
    }
  }

  TxUnackdRange	m_ranges[Max];
  unsigned	m_count = 0;
  uint64_t	m_length = 0;
};

using StreamRxPQueue =
  ZmPQueue<StreamRxData,
    ZmPQueueNode<StreamRxData,
      ZmPQueueHeapID<"Zquic.Stream.RxNode",
	ZmPQueueFn<StreamRxPQueueFn,
	  ZmPQueueOverwrite<false,
	    ZmPQueueBits<2,
	      ZmPQueueLevels<2>>>>>>>;

using CryptoRxPQueue =
  ZmPQueue<RxData,
    ZmPQueueNode<ZuObject,
      ZmPQueueHeapID<"Zquic.Crypto.RxNode",
	ZmPQueueOverwrite<false,
	  ZmPQueueBits<2,
	    ZmPQueueLevels<2>>>>>>;

using TxDataPQueue =
  ZmPQueue<StreamTxData,
    ZmPQueueNode<StreamTxData,
      ZmPQueueHeapID<"Zquic.Stream.TxNode",
	ZmPQueueFn<StreamTxPQueueFn,
	  ZmPQueueBits<2,
	    ZmPQueueLevels<3>>>>>>;

using StreamTxPQueue = TxUnackdRanges;
using CryptoTxPQueue = TxUnackdRanges;

using PktRxPQueue =
  ZmPQueue<RxPktMark,
    ZmPQueueNode<ZuObject,
      ZmPQueueHeapID<"Zquic.Pkt.RxNode",
	ZmPQueueOverwrite<false,
	  ZmPQueueBits<4,
	    ZmPQueueLevels<4>>>>>>;

struct RxSpan {
  uint64_t	first = 0;
  uint64_t	last = 0;

  uint64_t length() const { return last - first; }
};

ZuDerive(RxSpans, (ZtArray<RxSpan, ZtArrayHeapID<"Zquic.RxSpans">>));

template <typename Queue>
inline bool rxNovelSpans(
  const Queue &queue, uint64_t first, uint64_t end, RxSpans &spans)
{
  if (end < first) return false;
  return queue.gaps(first, end - first, [&spans](const auto &span) {
    spans << RxSpan{span.key(), span.key() + span.length()};
    return true;
  });
}

inline uint64_t rxSpanBytes(const RxSpans &spans)
{
  uint64_t bytes = 0;
  for (unsigned i = 0; i < spans.length(); ++i) bytes += spans[i].length();
  return bytes;
}

template <typename Alloc, typename Enqueue>
inline bool queueRxSpans(
  const RxSpans &spans, uint64_t srcOffset, ZuBSpan payload,
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

#endif /* ZquicPQueue_HH */
