//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// bytes format
// - common enum used by ZtJSON, etc.

#ifndef ZtBytesFmt_HH
#define ZtBytesFmt_HH

#ifndef ZtLib_HH
#include <zlib/ZtLib.hh>
#endif

namespace ZtBytesFmt {

enum { Base64 = 0, Base64URL, Base32, Hex, Raw };

} // ZtBytesFmt

#endif /* ZtBytesFmt_HH */
