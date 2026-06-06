//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC priority-queue item contracts

#ifndef ZquicPQueue_HH
#define ZquicPQueue_HH

#ifndef ZquicBuf_HH
#include <zlib/ZquicBuf.hh>
#endif

#include <zlib/ZmPQueue.hh>

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

struct TxData {
  ZmRef<ZiIOBuf>	buf;
  uint32_t		offset = 0;
  uint64_t		bytes = 0;
  uint64_t		streamOffset = 0;

  TxData() = default;
  TxData(
    ZmRef<ZiIOBuf> buf_, uint32_t offset_, uint64_t bytes_,
    uint64_t streamOffset_ = 0) :
    buf{ZuMv(buf_)}, offset{offset_}, bytes{bytes_},
    streamOffset{streamOffset_} { }
  TxData(TxRange range) :
    buf{ZuMv(range.buf)}, offset{range.offset}, bytes{range.length},
    streamOffset{range.streamOffset} { }

  operator TxRange() const {
    return TxRange{buf, offset, uint32_t(bytes), streamOffset};
  }

  uint64_t key() const { return streamOffset; }
  uint64_t length() const { return bytes; }

  uint64_t clipHead(uint64_t length) {
    if (length > bytes) length = bytes;
    offset += uint32_t(length);
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

struct ByteRangeMark {
  uint64_t	offset = 0;
  uint64_t	bytes = 0;

  ByteRangeMark() = default;
  ByteRangeMark(uint64_t offset_, uint64_t bytes_) :
    offset{offset_}, bytes{bytes_} { }

  uint64_t key() const { return offset; }
  uint64_t length() const { return bytes; }
  uint64_t clipHead(uint64_t length) {
    if (length > bytes) length = bytes;
    offset += length;
    return bytes -= length;
  }
  uint64_t clipTail(uint64_t length) {
    if (length > bytes) length = bytes;
    return bytes -= length;
  }
  template <typename I>
  void write(const I &) { }
};

struct RxPacketMark {
  uint64_t	pn = 0;

  RxPacketMark() = default;
  explicit RxPacketMark(uint64_t pn_) : pn{pn_} { }

  uint64_t key() const { return pn; }
  uint64_t length() const { return 1; }
  uint64_t clipHead(uint64_t) { return 0; }
  uint64_t clipTail(uint64_t) { return 0; }
  template <typename I>
  void write(const I &) { }
};

using StreamRxPQueue =
  ZmPQueue<RxData,
    ZmPQueueOverwrite<false,
      ZmPQueueBits<2,
	ZmPQueueLevels<2>>>>;

using CryptoRxPQueue = StreamRxPQueue;

using TxDataPQueue =
  ZmPQueue<TxData,
    ZmPQueueBits<2,
      ZmPQueueLevels<3>>>;

using ByteRangePQueue =
  ZmPQueue<ByteRangeMark,
    ZmPQueueOverwrite<false,
      ZmPQueueBits<2,
	ZmPQueueLevels<2>>>>;

using PacketRxPQueue =
  ZmPQueue<RxPacketMark,
    ZmPQueueNode<ZuObject,
      ZmPQueueOverwrite<false,
	ZmPQueueBits<4,
	  ZmPQueueLevels<4>>>>>;

} // namespace Zquic

#endif /* ZquicPQueue_HH */
