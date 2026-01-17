//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// command line interface

#ifndef ZrlTypes_HH
#define ZrlTypes_HH

#ifndef ZrlLib_HH
#include <zlib/ZrlLib.hh>
#endif

#include <zlib/ZtString.hh>
#include <zlib/ZtArray.hh>

namespace Zrl {

ZuDerive(Passwd, (ZtString<ZtStringHeapID<"Zrl.Passwd">>));
ZuDerive(Out, (ZtArray<uint8_t, ZtArrayHeapID<"Zrl.Out">>));

} // Zrl

#endif /* ZrlTypes_HH */
