//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdlib.h>
#include <time.h>

#include <iostream>

#include <zlib/ZuArray.hh>
#include <zlib/ZuSort.hh>
#include <zlib/ZuSearch.hh>
#include <zlib/ZuJoin.hh>

inline void out(const char *s) { std::cout << s << '\n'; }

#define CHECK(x) ((x) ? out("OK  " #x) : out("NOK " #x))

void search(ZuSpan<int> data, int value, unsigned pos_, unsigned nc_)
{
  unsigned pos, nc = 0;

  pos = ZuInterSearch<false>(data.length(), [&data, value, &nc](unsigned i) {
    ++nc;
    return value - data[i];
  });
  pos = ZuSearchPos(pos);
  std::cout << "value=" << value << " pos=" << pos << " nc=" << nc << '\n';
  CHECK(pos == pos_);
  CHECK(nc == nc_);
}

int main(int argc, char **argv)
{
  ZuArray<int, 10> foo{1, 9, 9, 9, 9, 9, 9, 9, 9, 9 };
  ZuArray<int, 10> bar{1, 1, 1, 1, 1, 1, 1, 1, 1, 9 };

  search(foo, 0, 0, 2);
  search(bar, 0, 0, 2);
  search(foo, 1, 0, 2);
  search(bar, 1, 0, 2);
  search(foo, 2, 1, 3);
  search(bar, 2, 9, 6);
  search(foo, 3, 1, 4);
  search(bar, 3, 9, 5);
  search(foo, 4, 1, 5);
  search(bar, 4, 9, 5);
  search(foo, 5, 1, 5);
  search(bar, 5, 9, 5);
  search(foo, 6, 1, 5);
  search(bar, 6, 9, 5);
  search(foo, 7, 1, 6);
  search(bar, 7, 9, 5);
  search(foo, 8, 1, 6);
  search(bar, 8, 9, 4);
  search(foo, 9, 1, 6);
  search(bar, 9, 9, 4);
  search(foo, 10, 10, 2);
  search(bar, 10, 10, 2);
}
