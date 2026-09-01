//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// current program

#ifndef ZiProgram_HH
#define ZiProgram_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZiPlatform.hh>

class ZiAPI ZiProgram {
  ZiProgram() = delete;
  ZiProgram(const ZiProgram &) = delete;
  ZiProgram &operator =(const ZiProgram &) = delete;

public:
  static Zi::Name name();

#ifdef _WIN32
  static Zi::Path cmdLine();
  static Zi::Name name(const Zi::Path &cmdLine);
#endif
};

#endif /* ZiProgram_HH */
