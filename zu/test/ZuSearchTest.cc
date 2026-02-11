//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <time.h>

#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuSort.hh>
#include <zlib/ZuSearch.hh>
#include <zlib/ZuJoin.hh>

using namespace ZuTestUtil;

void search(ZuSpan<int> data, int value, unsigned pos_, unsigned nc_)
{
  ZuTestScope(search);
  unsigned pos, nc = 0;

  pos = ZuInterSearch<false>(data.length(), [&data, value, &nc](unsigned i) {
    ++nc;
    return value - data[i];
  });
  pos = ZuSearchPos(pos);
  if (verbose)
    std::cerr << "value=" << value << " pos=" << pos << " nc=" << nc << '\n';
  ZuCHECK(pos == pos_);
  ZuCHECK(nc == nc_);
}

int main(int argc, char **argv)
{
  parse(argc, argv);

  ZuTestMain();

  ZuArray<int, 10> foo{1, 9, 9, 9, 9, 9, 9, 9, 9, 9 };
  ZuArray<int, 10> bar{1, 1, 1, 1, 1, 1, 1, 1, 1, 9 };

  ZuTestCall(search, foo, 0, 0, 2);
  ZuTestCall(search, bar, 0, 0, 2);
  ZuTestCall(search, foo, 1, 0, 2);
  ZuTestCall(search, bar, 1, 0, 2);
  ZuTestCall(search, foo, 2, 1, 3);
  ZuTestCall(search, bar, 2, 9, 6);
  ZuTestCall(search, foo, 3, 1, 4);
  ZuTestCall(search, bar, 3, 9, 5);
  ZuTestCall(search, foo, 4, 1, 5);
  ZuTestCall(search, bar, 4, 9, 5);
  ZuTestCall(search, foo, 5, 1, 5);
  ZuTestCall(search, bar, 5, 9, 5);
  ZuTestCall(search, foo, 6, 1, 5);
  ZuTestCall(search, bar, 6, 9, 5);
  ZuTestCall(search, foo, 7, 1, 6);
  ZuTestCall(search, bar, 7, 9, 5);
  ZuTestCall(search, foo, 8, 1, 6);
  ZuTestCall(search, bar, 8, 9, 4);
  ZuTestCall(search, foo, 9, 1, 6);
  ZuTestCall(search, bar, 9, 9, 4);
  ZuTestCall(search, foo, 10, 10, 2);
  ZuTestCall(search, bar, 10, 10, 2);
}
