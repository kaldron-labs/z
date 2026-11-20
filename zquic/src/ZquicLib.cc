//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC library

#include <zlib/ZePlatform.hh>

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <zlib/Zquic.hh>
#include "ZquicOpenSSL.hh"

ZquicExtern const char ZquicLib[] = "@(#) Z QUIC Library v" Z_VERNAME;

namespace Zquic {

  ZquicExtern void init()
  {
    if (!Ztls::cryptoInit()) throw ZeEXCEPT(Error, "Zquic", "crypto initialization failed");
  }

  ZquicExtern void warmup() { (void)opensslCipherCtx(); }

}
