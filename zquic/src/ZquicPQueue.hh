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
  uint64_t		bufOffset = 0;
  uint64_t		bytes = 0;

  RxData() = default;
  RxData(
    ZmRef<ZiIOBuf> buf_, uint64_t offset_, uint64_t bufOffset_,
    uint64_t bytes_) :
    buf{ZuMv(buf_)}, offset{offset_}, bufOffset{bufOffset_}, bytes{bytes_}
    { }

  uint64_t key() const { return offset; }
  uint64_t length() const { return bytes; }

  uint64_t clipHead(uint64_t length) {
    if (length > bytes) length = bytes;
    offset += length;
    bufOffset += length;
    return bytes -= length;
  }
  uint64_t clipTail(uint64_t length) {
    if (length > bytes) length = bytes;
    return bytes -= length;
  }
  template <typename I>
  void write(const I &) { }
};

struct StreamRxData : public Zquic_::IOQueue::Node {
  using Base = Zquic_::IOQueue::Node;

  ZmRef<ZiIOBuf>	packet;
  uint64_t		offset = 0;
  uint64_t		bufOffset = 0;
  uint64_t		bytes = 0;

  StreamRxData() : Base{nullptr, 0, nullptr, 0} { }
  StreamRxData(
    ZmRef<ZiIOBuf> packet_, const uint8_t *data_, unsigned length_,
    void *owner_, uint64_t offset_) :
    Base{const_cast<uint8_t *>(data_), length_, owner_, length_},
    packet{ZuMv(packet_)}, offset{offset_}, bytes{length_} { }

  uint64_t key() const { return offset; }
  uint64_t length() const { return bytes; }

  uint64_t clipHead(uint64_t length) {
    if (length > bytes) length = bytes;
    offset += length;
    bufOffset += length;
    return bytes -= length;
  }
  uint64_t clipTail(uint64_t length) {
    if (length > bytes) length = bytes;
    return bytes -= length;
  }
  template <typename I>
  void write(const I &) { }
};

struct StreamTxData : public ZiIOBuf {
  alignas(ZiIOBuf_Align) uint8_t data_[BufSize];
  uint64_t		streamOffset = 0;
  uint64_t		bufOffset = 0;
  uint64_t		bytes = 0;

  StreamTxData() : ZiIOBuf{data_, BufSize, nullptr} { }
  explicit StreamTxData(void *owner_) : ZiIOBuf{data_, BufSize, owner_} { }

  void publish(uint32_t offset_, uint32_t bytes_, uint64_t streamOffset_) {
    bufOffset = offset_;
    bytes = bytes_;
    streamOffset = streamOffset_;
  }
  uint64_t key() const { return streamOffset; }
  uint64_t length() const { return bytes; }

  uint64_t clipHead(uint64_t length) {
    if (length > bytes) length = bytes;
    bufOffset += length;
    streamOffset += length;
    return bytes -= length;
  }
  uint64_t clipTail(uint64_t length) {
    if (length > bytes) length = bytes;
    return bytes -= length;
  }
  template <typename I>
  void write(const I &) { }
};

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

using StreamRxPQueue =
  ZmPQueue<StreamRxData,
    ZmPQueueNode<StreamRxData,
      ZmPQueueHeapID<"Zquic.Stream.RxNode",
	ZmPQueueOverwrite<false,
	  ZmPQueueBits<2,
	    ZmPQueueLevels<2>>>>>>;

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
	ZmPQueueBits<2,
	  ZmPQueueLevels<3>>>>>;

using StreamTxPQueue =
  ZmPQueue<TxUnackdRange,
    ZmPQueueNode<ZuObject,
      ZmPQueueHeapID<"Zquic.Stream.TxUnackdNode",
	ZmPQueueOverwrite<false,
	  ZmPQueueBits<2,
	    ZmPQueueLevels<2>>>>>>;

using CryptoTxPQueue =
  ZmPQueue<TxUnackdRange,
    ZmPQueueNode<ZuObject,
      ZmPQueueHeapID<"Zquic.Crypto.TxUnackdNode",
	ZmPQueueOverwrite<false,
	  ZmPQueueBits<2,
	    ZmPQueueLevels<2>>>>>>;

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
  const RxSpans &spans, uint64_t srcOffset, ZuCSpan payload,
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
    if (ZuUnlikely(buf->size < length))
      if (ZuUnlikely(!buf->ensure(length))) return false;
    buf->skip = 0;
    buf->length = length;
    if (length) memcpy(buf->data_(), payload.data() + payloadOffset, length);
    enqueue(ZuMv(buf), spans[i].first, length);
  }
  return true;
}

} // namespace Zquic

#endif /* ZquicPQueue_HH */
