//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC buffer ownership

#ifndef Zquic_HH
#error "include zlib/Zquic.hh before this header"
#endif

#include <string.h>

#include <zlib/ZmRef.hh>

#include <zlib/ZiAssert.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/ZiTxStream.hh>

namespace Zquic_ {

// Intrusive base shared by packet, stream and crypto buffers; Zi::IOBufBuiltin
// supplies the role heap. It is deliberately independent of either I/O queue.
struct BufNode : public ZiIOBuf {
  using ZiIOBuf::ZiIOBuf;
};

using RxStream = ZiRxStream<ZiRxQueue>;

template <
  unsigned Size = BufSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = "Zquic.Buf">
using BufAlloc =
  Zi::IOBufBuiltin<BufNode, Size, ZiIOBuf_Align, ZuStringT<HeapID>, MaxSize>;

} // namespace Zquic_

namespace Zquic {

using RxStream = Zquic_::RxStream;

template <
  unsigned Size = BufSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = "Zquic.Pkt.Rx">
Zi::IOBufAllocator *pktRxAllocator(ZiMultiplex *mx) {
  return mx->allocator<Zi::IOBufType::RxUDP, Zquic_::BufNode,
    Size, MaxSize, ZiIOBuf_Align, ZuStringT<HeapID>>();
}

template <
  unsigned Size = BufSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = "Zquic.Pkt.Tx">
Zi::IOBufAllocator *pktTxAllocator(ZiMultiplex *mx) {
  return mx->allocator<Zi::IOBufType::TxUDP, Zquic_::BufNode,
    Size, MaxSize, ZiIOBuf_Align, ZuStringT<HeapID>>();
}

template <
  unsigned Size = BufSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = "Zquic.Stream.TxBuf">
using StreamTxBufAlloc = Zquic_::BufAlloc<Size, MaxSize, HeapID>;

template <
  unsigned Size = BufSize,
  unsigned MaxSize = (64<<10), // 64K
  ZuString HeapID = "Zquic.Crypto.TxBuf">
using CryptoTxBufAlloc = Zquic_::BufAlloc<Size, MaxSize, HeapID>;

struct BufDiag { };

// Retained payload positions are relative to the original allocation.
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

inline void assertPktCapacity(const ZiIOBuf *buf, unsigned required)
{
  ZiAssert(buf && buf->size >= required, "Zquic", (required),
    "packet buffer capacity violation required=" << required,
    return);
}

inline void assertStreamCapacity(const ZiIOBuf *buf, unsigned required)
{
  ZiAssert(buf && buf->size >= required, "Zquic", (required),
    "stream buffer capacity violation required=" << required,
    return);
}

} // namespace Zquic
