//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Diagnostic fixture: repeat selector inspection without sample allocations.

#include <stdlib.h>
#include <stdio.h>
#include <zlib/ZfixFmt.hh>
int main(int argc, char **argv) {
  unsigned n = argc > 1 ? atoi(argv[1]) : 1;
  constexpr auto text = "49=CLIENT\1" "56=SERVER\1" "34=7\1" "11=42\1" "150=F\1"_z;
  unsigned sum = 0;
  for (unsigned i = 0; i < n; ++i) {
    Zfix::Fmt::FieldPos pos;
    if (Zfix::Fmt::inspect(text, 150, pos)) return 1;
    sum += pos.length;
  }
  printf("%u\n", sum); return 0;
}
