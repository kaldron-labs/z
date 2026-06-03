//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC buffer ownership

#ifndef ZquicBuf_HH
#define ZquicBuf_HH

#ifndef ZquicTypes_HH
#include <zlib/ZquicTypes.hh>
#endif

#include <string.h>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZiAssert.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/ZiTxStream.hh>

namespace Zquic_ {

ZuDerive(IOQueue,
  (ZmList<ZiIOBuf, ZmListNode<ZiIOBuf, ZmListHeapID<"">>>));

using RxStream = ZiRxStream<IOQueue>;

template <
  unsigned Size = Zquic::BufSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = ZiIOBuf_HeapID{}()>
using BufAlloc =
  Zi::IOBufAlloc<IOQueue::Node, Size, MaxSize, ZuStringT<HeapID>>;

} // namespace Zquic_

namespace Zquic {

using RxStream = Zquic_::RxStream;

template <
  unsigned Size = BufSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = "Zquic.Packet">
using PacketBufAlloc = Zquic_::BufAlloc<Size, MaxSize, HeapID>;

template <
  unsigned Size = BufSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = "Zquic.StreamBuf">
using StreamBufAlloc = Zquic_::BufAlloc<Size, MaxSize, HeapID>;

struct BufDiag {
  ZmAtomic<uint64_t>	packetAllocs = 0;
  ZmAtomic<uint64_t>	streamAllocs = 0;
  ZmAtomic<uint64_t>	rxPacketToStreamCopies = 0;
  ZmAtomic<uint64_t>	forbiddenCopies = 0;
};

struct TxRange {
  ZmRef<ZiIOBuf>	buf;
  uint32_t		offset = 0;
  uint32_t		length = 0;
  uint64_t		streamOffset = 0;

  TxRange() = default;
  TxRange(
    ZmRef<ZiIOBuf> buf_, uint32_t offset_, uint32_t length_,
    uint64_t streamOffset_ = 0) :
    buf{ZuMv(buf_)}, offset{offset_}, length{length_},
    streamOffset{streamOffset_} { }
};

inline bool copyPacketToStream(
  ZiIOBuf *dst, const ZiIOBuf *src, unsigned offset, unsigned length,
  BufDiag *diag = nullptr)
{
  ZiAssert(src && dst, "Zquic", (src, dst),
    "null buffer in packet-to-stream copy", return false);
  ZiAssert(offset <= src->length && length <= src->length - offset,
    "Zquic", (offset, length, src->length),
    "packet-to-stream copy source range violation", return false);
  ZiAssert(dst->size >= length, "Zquic", (dst->size, length),
    "packet-to-stream copy destination too small", return false);
  dst->skip = 0;
  dst->length = length;
  if (length) memcpy(dst->data_(), src->data() + offset, length);
  if (diag) ++diag->rxPacketToStreamCopies;
  return true;
}

inline void assertPacketCapacity(const ZiIOBuf *buf, unsigned required)
{
  ZiAssert(buf && buf->size >= required, "Zquic", (buf, required),
    "packet buffer capacity violation required=" << required,
    return);
}

inline void assertStreamCapacity(const ZiIOBuf *buf, unsigned required)
{
  ZiAssert(buf && buf->size >= required, "Zquic", (buf, required),
    "stream buffer capacity violation required=" << required,
    return);
}

} // namespace Zquic

#endif /* ZquicBuf_HH */
