//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZtBitmap.hh>

void out(bool ok, ZuCSpan check, ZuCSpan diag) {
  std::cout
    << (ok ? "OK  " : "NOK ") << check << ' ' << diag
    << '\n' << std::flush;
}

#define CHECK_(x) out((x), #x, "")
#define CHECK(x, y) out((x), #x, y)

int main()
{
  ZtBitmap a{256U};
  a.set(2, 6);
  a.set(10, 15);
  a.set(100, 256);
  ZuCArray<100> s;
  s << a;
  std::cout << s << '\n';
  ZuBitmap<256> b(s);
  s = {}; s << b;
  CHECK_(s == "2-5,10-14,100-");
}
