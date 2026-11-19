//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZfixAFXDPWire_HH
#define ZfixAFXDPWire_HH

#include <zlib/ZmHeap.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZfixWire.hh>

namespace ZfixTest {

  // Zutcp admits one packet-sized buffer per submission. WireTx preserves
  // whole FIX frames and counts their final record completion as usual.
  template <typename Link> Zfix::WireGeometry packetGeometry(Link *link) {
    auto allocator = link->txAllocator();
    uint64_t backing = uint64_t(allocator->capacity()) + allocator->headRoom + allocator->tailRoom;
    using Buf = typename Link::TxBufAlloc::Buf;
    return {.size = link->txBufSize_(), .head = allocator->headRoom, .tail = allocator->tailRoom,
      .memory = backing + ZmHeapAllocSize<sizeof(Zi::IOBufStub<Buf>)>{}};
  }
}

#endif /* ZfixAFXDPWire_HH */
