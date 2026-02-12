//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <memory>
#include <string>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuAlt.hh>
#include <zlib/ZuDeduce.hh>
#include <zlib/ZuEquiv.hh>
#include <zlib/ZuLargest.hh>
#include <zlib/ZuMostAligned.hh>
#include <zlib/ZuNorm.hh>
#include <zlib/ZuObjectTraits.hh>
#include <zlib/ZuSeq.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuPolymorph.hh>
#include <zlib/ZuStdString.hh>
#include <zlib/ZuFnName.hh>

using namespace ZuTestUtil;

template <typename T>
struct Wrap {
  using Type = T;
};

template <typename T>
struct IsIntegralT : public ZuBool<ZuTraits<T>::IsIntegral> { };

struct A { enum { I = 3 }; };
struct B { enum { I = 1 }; };
struct C { enum { I = 2 }; };

template <typename T>
struct Index : public ZuUnsigned<T::I> { };

int freeFn(double)
{
  return 0;
}

void testStaticTraits()
{
  ZuTestScope(testStaticTraits);

  static_assert(ZuIsSame<ZuAlt<char>, wchar_t>{});
  static_assert(ZuIsSame<ZuAlt<wchar_t>, char>{});
  static_assert(ZuEquiv<char, unsigned char>{});
  static_assert(ZuIsSame<ZuNorm<const volatile char &>, char>{});

  static_assert(ZuIsSame<ZuLargest<uint8_t, uint16_t, uint32_t>, uint32_t>{});
  static_assert(alignof(ZuMostAligned<char, double, int>) == alignof(double));

  using TL = ZuTypeList<int, double, char>;
  using Head = ZuTypeHead<2, TL>;
  using Tail = ZuTypeTail<1, TL>;
  using Mapped = ZuTypeMap<Wrap, TL>;
  using Grep = ZuTypeGrep<IsIntegralT, TL>;
  using Sorted = ZuTypeSort<Index, A, B, C>;

  static_assert(Head::N == 2);
  static_assert(Tail::N == 2);
  static_assert(Mapped::N == 3);
  static_assert(Grep::N == 2);
  static_assert(ZuType<0, Sorted>::I == 1);
  static_assert(ZuType<1, Sorted>::I == 2);
  static_assert(ZuType<2, Sorted>::I == 3);

  using From = ZuTypeList<short, int>;
  using To = ZuTypeList<int, long>;
  static_assert(ZuTLConverts<From, To>{});
  static_assert(ZuTLConstructs<From, To>{});

  using Seq = ZuSeq<1, 3, 5>;
  static_assert(ZuSeqBitmap<Seq>() == ((1ULL << 1) | (1ULL << 3) | (1ULL << 5)));

  using D = ZuDeduce<decltype(&freeFn)>;
  static_assert(!D::Member);
  static_assert(ZuIsSame<typename D::R, int>{});
}

void testRuntimeSmokes()
{
  ZuTestScope(testRuntimeSmokes);

  struct P : public ZuPolymorph { };

  P p;
  ZuCheck(p.refCount() == 0);
  p.ref();
  ZuCheck(p.refCount() == 1);
  ZuCheck(p.deref());
  ZuCheck(ZuObjectTraits<P>::IsObject);

  // Forward declaration smoke from ZuStdString.hh.
  std::basic_string<char, std::char_traits<char>, std::allocator<char>> s{"ok"};
  ZuCheck(s == "ok");

#if defined(__GNUC__) || defined(_MSC_VER)
  const char *name = ZuFnName;
  ZuCheck(name && name[0]);
#else
  ZuCheck(true);
#endif
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testStaticTraits);
  ZuTestCall(testRuntimeSmokes);
  return 0;
}
