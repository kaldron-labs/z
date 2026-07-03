//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC buffer ownership

#ifndef ZquicBuf_HH
#define ZquicBuf_HH

#ifndef ZquicLib_HH
#include <zlib/ZquicLib.hh>
#endif

#include <string.h>

#include <zlib/ZmList.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZiAssert.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/ZiTxStream.hh>

#include <zlib/ZquicTypes.hh>

namespace Zquic_ {

using namespace Zquic;

ZuDerive(IOQueue,
  // Intrusive base only; Zi::IOBufAlloc supplies the role heap.
  (ZmList<ZiIOBuf, ZmListNode<ZiIOBuf, ZmListHeapID<"">>>));

using RxStream = ZiRxStream<IOQueue>;

template <
  unsigned Size = BufSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = "Zquic.Buf">
using BufAlloc =
  Zi::IOBufAlloc<IOQueue::Node, Size, MaxSize, ZuStringT<HeapID>>;

} // namespace Zquic_

namespace Zquic {

using RxStream = Zquic_::RxStream;

template <
  unsigned Size = BufSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = "Zquic.Pkt.Rx">
using PktRxBufAlloc = Zquic_::BufAlloc<Size, MaxSize, HeapID>;

template <
  unsigned Size = BufSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = "Zquic.Pkt.Tx">
using PktTxBufAlloc = Zquic_::BufAlloc<Size, MaxSize, HeapID>;

template <
  unsigned Size = BufSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = "Zquic.Stream.TxBuf">
using StreamTxBufAlloc = Zquic_::BufAlloc<Size, MaxSize, HeapID>;

template <
  unsigned Size = BufSize,
  unsigned MaxSize = (64<<10), // 64K
  ZuString HeapID = "Zquic.Crypto.RxBuf">
using CryptoRxBufAlloc = Zquic_::BufAlloc<Size, MaxSize, HeapID>;

template <
  unsigned Size = BufSize,
  unsigned MaxSize = (64<<10), // 64K
  ZuString HeapID = "Zquic.Crypto.TxBuf">
using CryptoTxBufAlloc = Zquic_::BufAlloc<Size, MaxSize, HeapID>;

struct BufDiag { };

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

#endif /* ZquicBuf_HH */
