//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <zlib/ZuBitmap.hh>
#include <zlib/ZuArray.hh>

int main()
{
  ZuBitmap<256> a;
  a.set(2, 6);
  a.set(10, 15);
  a.set(100, 256);
  ZuCArray<100> s;
  s << a;
  std::cout << s << '\n' << std::flush;
  ZuBitmap<256> b(s);
  std::cout << b << '\n' << std::flush;
}
