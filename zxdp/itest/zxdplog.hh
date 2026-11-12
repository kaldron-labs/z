//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Read a bounded diagnostic prefix from an isolated child process.

#ifndef zxdplog_HH
#define zxdplog_HH

#include <zlib/ZiFile.hh>

namespace ZxdpTest {

  template <unsigned N> bool readLog(const char *path, char (&bytes)[N])
  {
    bytes[0] = 0;
    ZiFile file;
    if (file.open(path, ZiFile::ReadOnly) != Zi::OK) return false;
    auto n = file.read(bytes, N - 1);
    if (n < 0) return false;
    bytes[n] = 0;
    return true;
  }
}

#endif /* zxdplog_HH */
