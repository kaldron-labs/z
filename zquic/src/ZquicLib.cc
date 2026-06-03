//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC library

#include <zlib/ZquicLib.hh>

#include <zlib/ZtlsLib.hh>

ZquicExtern const char ZquicLib[] = "@(#) Z QUIC Library v" Z_VERNAME;

namespace Zquic {

ZquicExtern void lib_init()
{
  Ztls::lib_init();
}

} // namespace Zquic
