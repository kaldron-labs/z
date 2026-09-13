//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z REST library

#include <zlib/Zrest.hh>

ZrestExtern const char ZrestLib[] = "@(#) Z REST Library v" Z_VERNAME;

namespace Zrest {

ZrestExtern bool skip(ZuSpan<uint8_t> &path, unsigned n)
{
  if (ZuLikely(!n)) return true;
  if (ZuUnlikely(!path || path[0] != '/')) return false;
  for (unsigned i = 0; i < n; ++i) {
    path.offset(1);
    auto offset = path.find([](auto c) { return c == '/'; });
    if (ZuUnlikely(offset < 0)) return false;
    path.offset(offset);
  }
  return true;
}

} // Zrest
