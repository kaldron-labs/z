//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuTest.hh>
#include <zlib/ZuAssert.hh>
#include <zlib/ZuUnroll.hh>
#include <zlib/ZuDemangle.hh>

bool verbose = false;

template <typename ...Args>
void log_(Args &&...args) {
  if constexpr (sizeof...(args))
    (std::cerr << ...<< ZuFwd<Args>(args)) << '\n';
}
template <typename ...Args>
void log(Args &&...args) {
  if (verbose) log_(ZuFwd<Args>(args)...);
}
#define CHECK(x, ...) ZuCheck(x, log_(__VA_ARGS__))

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

static void usage()
{
  std::cerr << "usage: ZuBoxTest [-v]\n";
  ::exit(1);
}

int main(int argc, char **argv)
{
  if (argc < 1 || argc > 2) usage();
  if (argc == 2) {
    if (strcmp(argv[1], "-v")) usage();
    verbose = true;
  }

  ZuTestMain();

  {
    ZuUnroll::all<Sorted>([]<typename T>() {
      log(T::I, ' ', T::id());
    });
    X x;
    // X y = x;
    [[maybe_unused]] X z{x};
    X q;
    q = x;
  }
  {
    log("--- 0 1 2 3");
    ZuUnroll::all<4>([](auto i) { log(i); });
    ZuCheck(ZuUnroll::all<4>(0, [](auto i, int j) {
      return j + 1;
    }) == 4);
    auto j = ZuUnroll::all<4>(0, [](auto i, int j) {
      log(i);
      return j + 1;
    });
    ZuCheck(j == 4);
    log("j=", j);
  }
  {
    log("--- 3 2 1 0");
    ZuUnroll::all<ZuTypeRev<ZuSeqTL<ZuMkSeq<4>>>>([]<typename I>() {
      log(I{});
    });
  }
  {
    log("--- 1 2 3");
    ZuUnroll::all<ZuTypeTail<1, ZuSeqTL<ZuMkSeq<4>>>>([]<typename I>() {
      log(I{});
    });
  }
  {
    log("--- 0 1 2");
    ZuUnroll::all<ZuTypeHead<3, ZuSeqTL<ZuMkSeq<4>>>>([]<typename I>() {
      log(I{});
    });
  }
  {
    log("--- 42 42 42");
    ZuUnroll::all<ZuTypeRepeat<3, ZuInt<42>>>([]<typename I>() {
      log(I{});
    });
  }
}
