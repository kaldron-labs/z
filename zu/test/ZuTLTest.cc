//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <zlib/ZuAssert.hh>
#include <zlib/ZuUnroll.hh>
#include <zlib/ZuDemangle.hh>

#include <iostream>

inline void out(const char *s) { std::cout << s << '\n'; }

#define CHECK(x) ((x) ? out("OK  " #x) : out("NOK " #x))

#define DEFINE(ID, I_) \
struct ID { enum { I = I_ }; static const char *id() { return #ID; } }

DEFINE(A, 3);
DEFINE(B, 2);
DEFINE(C, 1);
DEFINE(D, 5);
DEFINE(E, 4);

template <typename T> struct Index : public ZuUnsigned<T::I> { };

using Sorted = ZuTypeSort<Index, A, B, C, D, E>;

struct X {
  X() = default;
  X(int j_, int k_) : j{j_}, k{k_} { }
  int i = 42, j = 43, k = 44;
};

int main(int argc, char **argv)
{
  {
    ZuUnroll::all<Sorted>([]<typename T>() {
      std::cout << T::I << ' ' << T::id() << '\n';
    });
    X x;
    // X y = x;
    [[maybe_unused]] X z{x};
    X q;
    q = x;
  }
  {
    std::cout << "--- 0 1 2 3\n";
    ZuUnroll::all<4>([](auto i) { std::cout << i << '\n'; });
    ZuAssert(ZuUnroll::all<4>(0, [](auto i, int j) {
      return j + 1;
    }) == 4);
    auto j = ZuUnroll::all<4>(0, [](auto i, int j) {
      std::cout << i << '\n';
      return j + 1;
    });
    std::cout << "j=" << j << '\n';
  }
  {
    std::cout << "--- 3 2 1 0\n";
    ZuUnroll::all<ZuTypeRev<ZuSeqTL<ZuMkSeq<4>>>>([]<typename I>() {
      std::cout << I{} << '\n';
    });
  }
  {
    std::cout << "--- 1 2 3\n";
    ZuUnroll::all<ZuTypeTail<1, ZuSeqTL<ZuMkSeq<4>>>>([]<typename I>() {
      std::cout << I{} << '\n';
    });
  }
  {
    std::cout << "--- 0 1 2\n";
    ZuUnroll::all<ZuTypeHead<3, ZuSeqTL<ZuMkSeq<4>>>>([]<typename I>() {
      std::cout << I{} << '\n';
    });
  }
  {
    std::cout << "--- 42 42 42\n";
    ZuUnroll::all<ZuTypeRepeat<3, ZuInt<42>>>([]<typename I>() {
      std::cout << I{} << '\n';
    });
  }
}
