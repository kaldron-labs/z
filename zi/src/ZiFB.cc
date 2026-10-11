//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZiFB.hh>

namespace ZiFB::Values {

ZfStructImpl(Int128);
ZfStructImpl(UInt128);
ZfStructImpl(Fixed);
ZfStructImpl(Decimal);
ZfStructImpl(Time);
ZfStructImpl(DateTime);
ZfStructImpl(Bytes);
ZfStructImpl(Bitmap);

} // ZiFB::Values

namespace ZiFB {

ZfStructImpl(IPv4);
ZfStructImpl(IPv6);

} // ZiFB
