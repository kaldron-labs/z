//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuAssert.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuStream.hh>
#include <zlib/ZuUnroll.hh>
#include <zlib/ZuDemangle.hh>
#include <zlib/ZuTL.hh>

using namespace ZuTestUtil;

#define DEFINE(ID, I_) \
struct ID { enum { I = I_ }; static const char *id() { return #ID; } }

DEFINE(A, 3);
DEFINE(B, 2);
DEFINE(C, 1);
DEFINE(D, 5);
DEFINE(E, 4);

template <typename T> struct Index : public ZuUnsigned<T::I> { };

using Sorted = ZuTypeSort<Index, A, B, C, D, E>;

template <typename T> struct Wrap { };
template <typename T> struct IsIntegralT : public ZuBool<ZuTraits<T>::IsIntegral> { };

using TL = ZuTypeList<int, double, char>;
using TLMap = ZuTypeMap<Wrap, TL>;
using TLGrep = ZuTypeGrep<IsIntegralT, TL>;
using TLHead = ZuTypeHead<2, TL>;
using TLTail = ZuTypeTail<1, TL>;

static_assert(TLMap::N == 3);
static_assert(TLGrep::N == 2);
static_assert(TLHead::N == 2);
static_assert(TLTail::N == 2);

using ConvertFrom = ZuTypeList<short, int>;
using ConvertTo = ZuTypeList<int, long>;
static_assert(ZuTLConverts<ConvertFrom, ConvertTo>{});
static_assert(ZuTLConstructs<ConvertFrom, ConvertTo>{});

struct X {
  X() = default;
  X(int j_, int k_) : j{j_}, k{k_} { }
  int i = 42, j = 43, k = 44;
};

int main(int argc, char **argv)
{
  parse(argc, argv);

  ZuTestMain();

  {
    ZuCArray<32> buf;
    ZuStream s(buf.span());
    ZuUnroll::all<Sorted>([&s]<typename T>() {
      s << T::I << ':' << T::id() << '|';
    });
    s.finish(buf);
    ZuCHECK(buf == "1:C|2:B|3:A|4:E|5:D|");
    X x;
    // X y = x;
    [[maybe_unused]] X z{x};
    X q;
    q = x;
  }
  {
    log("--- 0 1 2 3");
    ZuUnroll::all<4>([](auto i) { log(i); });
    ZuCHECK(ZuUnroll::all<4>(0, [](auto i, int j) {
      return j + 1;
    }) == 4);
    auto j = ZuUnroll::all<4>(0, [](auto i, int j) {
      log(i);
      return j + 1;
    });
    ZuCHECK(j == 4);
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
