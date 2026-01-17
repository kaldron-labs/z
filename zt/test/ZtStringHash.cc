//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>
#include <float.h>

#include <string_view>

#include <zlib/ZmHash.hh>
#include <zlib/ZtString.hh>

#include "../../zu/test/Analyze.hh"

void doit(bool high)
{
  FILE *f = fopen("words", "r");
  if (!f) { perror("words"); Zm::exit(1); }
  char buf[512];
  int count[1024]; memset(count, 0, sizeof(count));
  int count2[1024]; memset(count2, 0, sizeof(count2));
  memset(count, 0, sizeof(count));
  while (fgets(buf, 512, f)) {
    buf[511] = 0;
    ZtString<> s; s -= buf;
    int n = s.length();
    if (n <= 1) continue;
    s.length(n - 1);
    uint32_t hash = s.hash();
    hash = high ? ZmHashBits(hash, 10) : (hash & 1023);
    uint32_t hash2 = std::hash<std::basic_string_view<char>>{}(
      std::basic_string_view<char>(s.data(), s.length()));
    hash2 = high ? ZmHashBits(hash, 10) : (hash2 & 1023);
    count[hash]++;
    count2[hash2]++;
  }
  fclose(f);
  analyze("string ZuHash", count, 1024);
  analyze("string std::hash", count2, 1024);
}

int main()
{
  doit(true);
  doit(false);
}
