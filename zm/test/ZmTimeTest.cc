//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <iostream>

#include <zlib/ZmPlatform.hh>
#include <zlib/ZmTime.hh>

void fail() { Zm::exit(1); }

void out(const char *s) {
  std::cout << s << '\n' << std::flush;
}

#define CHECK(x) ((x) ? (out("OK  " #x), void()) : (out("NOK " #x), fail()))

int main()
{
  ZuTime t;
  CHECK(!*t);
  CHECK(!!t);
  CHECK(t);
  ZuTime t2 = 0;
  CHECK(*t2);
  CHECK(!t2);
  std::cout << Zm::now() << '\n';
}
