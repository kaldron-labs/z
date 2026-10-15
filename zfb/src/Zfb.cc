//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/Zfb.hh>

namespace Zfb::Values {

ZfStructImpl(Int128);
ZfStructImpl(Int128, FB);
ZfStructImpl(UInt128);
ZfStructImpl(UInt128, FB);
ZfStructImpl(Fixed);
ZfStructImpl(Fixed, FB);
ZfStructImpl(Decimal);
ZfStructImpl(Decimal, FB);
ZfStructImpl(Time);
ZfStructImpl(Time, FB);
ZfStructImpl(DateTime);
ZfStructImpl(DateTime, FB);
ZfStructImpl(Bytes);
ZfStructImpl(Bytes, FB);
ZfStructImpl(Bitmap);
ZfStructImpl(Bitmap, FB);

} // Zfb::Values

namespace Zfb {

ZfStructImpl(IPv4);
ZfStructImpl(IPv4, FB);
ZfStructImpl(IPv6);
ZfStructImpl(IPv6, FB);

} // Zfb
