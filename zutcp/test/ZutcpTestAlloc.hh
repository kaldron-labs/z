//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZutcpTestAlloc_HH
#define ZutcpTestAlloc_HH

#include <zlib/Zutcp.hh>

// Test/application-side requests exercise the public mx factory. Zutcp itself
// supplies lower-level providers to that factory and must never call it.
namespace ZutcpTest {
using namespace Zutcp;

template <typename Policy>
Zi::IOBufAllocator *plaintextAllocator(ZiMultiplex *mx) {
  return [mx]<
    Zi::IOBufType::T Type, typename Buf, unsigned Size, unsigned Align,
    typename HeapID, unsigned VHMaxSize, auto Grow
  >(Zi::IOBufBuiltinAllocator<Type, Buf, Size, Align, HeapID, VHMaxSize, Grow> *) {
    ZuAssert(Type == Zi::IOBufType::App);
    return mx->allocator<Type, Buf, Size, Align, HeapID, VHMaxSize, Grow>();
  }(static_cast<Policy *>(nullptr));
}


template <
  unsigned Size = BufSize,
  unsigned MaxSize = ZiIOBuf_DefltVHMaxSize,
  ZuString HeapID = "Zutcp.Pkt.Rx">
Zi::IOBufAllocator *pktRxAllocator(ZiMultiplex *mx) {
  return mx->allocator<Zi::IOBufType::RxTCP, Zutcp_::BufNode,
    Size, ZiIOBuf_Align, ZuStringT<HeapID>, MaxSize>();
}

template <
  unsigned Size = BufSize,
  unsigned MaxSize = ZiIOBuf_DefltVHMaxSize,
  ZuString HeapID = "Zutcp.Pkt.Tx">
Zi::IOBufAllocator *pktTxAllocator(ZiMultiplex *mx) {
  return mx->allocator<Zi::IOBufType::TxTCP, Zutcp_::BufNode,
    Size, ZiIOBuf_Align, ZuStringT<HeapID>, MaxSize>();
}

} // namespace ZutcpTest

#endif /* ZutcpTestAlloc_HH */
