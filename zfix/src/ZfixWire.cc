//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZfixWire.hh>

namespace Zfix {

struct WireBorrow : public ZmNHeap<WireBorrow>, public WireBuf {
friend inline ZuStringT<"Zfix.WireBorrow"> ZmNHeapID(WireBorrow *);

  explicit WireBorrow(ZmRef<ZiIOBuf> frame) :
    WireBuf{frame->data(), frame->length} {
    length = frame->length; backing = ZuMv(frame);
  }
};

ZuAssert(sizeof(WireBorrow) == sizeof(WireBuf));

ZmRef<WireBuf> wireBorrow(ZmRef<ZiIOBuf> frame) {
  return new WireBorrow{ZuMv(frame)};
}
} // Zfix

