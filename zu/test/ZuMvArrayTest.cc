//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <iostream>

#include <zlib/ZuMvArray.hh>

inline void out(const char *s) { std::cout << s << '\n'; }

#define CHECK(x) ((x) ? out("OK  " #x) : out("NOK " #x))

struct E { bool x = false; };
struct G { ZuMvArray<E> e; int i; };
struct H { ZuSpan<E> e; int i; };

int main()
{
  {
    G g{ .e = { { .x = false }, { .x = true } }, .i = 42 };
    CHECK(!g.e[0].x);
    CHECK(g.e[1].x);
  }
  {
    H h{ .e = { { .x = false }, { .x = true } }, .i = 42 };
    CHECK(!h.e[0].x);
    CHECK(h.e[1].x);
  }
}
