//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>
#include <sstream>

#include <zlib/ZuBox.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuStream.hh>
#include <zlib/ZuVStream.hh>
#include <zlib/ZuArray.hh>

using namespace ZuTestUtil;

struct A {
  template <typename S>
  friend inline decltype(auto) operator <<(S &s, const A &) {
    return s << "A";
  }
};

template <typename S>
void test(S &s) {
  s << 42 << ' ' << A{} << L" " << 42.42 << ' ' << ZuBoxed(42.0F) << "\n";
}

template <typename S>
void vtest(S &s_) {
  ZuVStream s{s_};
  test(s);
}

template <typename Char>
void stest() {
  ZuTestScope(stest);
  Char buf[80];
  ZuSpan<Char> s{&buf[0], sizeof(buf)};
  ZuStream_<Char> s_(s);
  s_ << L"hello " << "world" << L'!' << ' ' << 42.42;
  s.trunc(s_.data() - s.data());
  if constexpr (sizeof(Char) == 1) {
    ZuCHECK(s == "hello world! 42.42");
  } else {
    ZuCHECK(s == L"hello world! 42.42");
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);

  ZuTestMain();
  {
    ZuCArray<80> s1;
    std::stringstream s2;
    vtest(s1);
    vtest(s2);
    ZuCHECK(s1 == s2.str());
    ZuArray<wchar_t, 80> w1;
    test(w1);
    s1 = w1;
    ZuCHECK(s1 == s2.str());
  }
  if (verbose) vtest(std::cerr);
  {
    ZuTestCall((stest<char>));
    ZuTestCall((stest<wchar_t>));
  }
}
