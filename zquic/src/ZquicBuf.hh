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
#include <zlib/ZiIP.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/ZiTx.hh>
#include <zlib/ZiTxStream.hh>

namespace Zquic_ {

// Receive packets and retained plaintext remain independent of the Tx queue.
struct BufNode : public ZiIOBuf {
  using ZiIOBuf::ZiIOBuf;
};

// Final UDP output owns its queue links and immutable destination metadata.
struct TxPkt : public ZiTxBuf {
  ZiSockAddr	addr;
  EcnMark::T	ecn = EcnMark::NotECT;

  using ZiTxBuf::ZiTxBuf;
};

using RxStream = ZiRxStream<ZiRxQueue>;

template <
  unsigned Size = BufSize,
  unsigned MaxSize = ZiIOBuf_DefltVHMaxSize,
  ZuString HeapID = "Zquic.Buf">
using BufAlloc =
  Zi::IOBufBuiltinAllocator<
    Zi::IOBufType::TxTLS, BufNode, Size, ZiIOBuf_Align, ZuStringT<HeapID>, MaxSize>;

} // namespace Zquic_

namespace Zquic {

using RxStream = Zquic_::RxStream;

template <typename Policy>
Zi::IOBufAllocator *plaintextAllocator(ZiMultiplex *mx) {
  return [mx]<
    Zi::IOBufType::T Type, typename Buf, unsigned Size, unsigned Align,
    typename HeapID, unsigned VHMaxSize, auto Grow
  >(Zi::IOBufBuiltinAllocator<Type, Buf, Size, Align, HeapID, VHMaxSize, Grow> *) {
    ZuAssert(Type == Zi::IOBufType::RxTLS || Type == Zi::IOBufType::TxTLS);
    return mx->allocator<Type, Buf, Size, Align, HeapID, VHMaxSize, Grow>();
  }(static_cast<Policy *>(nullptr));
}


template <
  unsigned Size = BufSize,
  unsigned MaxSize = ZiIOBuf_DefltVHMaxSize,
  ZuString HeapID = "Zquic.Pkt.Rx">
Zi::IOBufAllocator *pktRxAllocator(ZiMultiplex *mx) {
  return mx->allocator<Zi::IOBufType::RxUDP, Zquic_::BufNode,
    Size, ZiIOBuf_Align, ZuStringT<HeapID>, MaxSize>();
}

template <
  unsigned Size = BufSize,
  unsigned MaxSize = ZiIOBuf_DefltVHMaxSize,
  ZuString HeapID = "Zquic.Pkt.Tx">
Zi::IOBufAllocator *pktTxAllocator(ZiMultiplex *mx) {
  return mx->allocator<Zi::IOBufType::TxUDP, Zquic_::TxPkt,
    Size, ZiIOBuf_Align, ZuStringT<HeapID>, MaxSize>();
}

template <
  unsigned Size = BufSize,
  unsigned MaxSize = ZiIOBuf_DefltVHMaxSize,
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

inline unsigned txPktCapacity(const ZiIOBuf *buf)
{
  return buf->avail() - buf->allocator()->tailRoom;
}

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
