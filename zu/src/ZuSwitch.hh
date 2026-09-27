//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// compile-time switch

// template <unsigned I, typename = ZuIfT<(I < 3)>>
// void foo() {
//   if constexpr (I == 0) puts("0");
//   else if constexpr (I == 1) puts("1");
//   else puts("2");
// }

// unsigned i = ...;
// ZuSwitch::dispatch<3>(i, [](auto I) { foo<I>(); });
// ZuSwitch::dispatch<3>(i, [](auto I) { foo<I>(); }, []{ puts("default"); });

// gcc/clang at -O2 or better compiles to a classic switch jump table

// the underlying trick uses std::initializer_list<> to unpack
// a parameter pack where each expression in the list is evaluated with
// a side effect that conditionally invokes a lambda which is
// passed a constexpr index parameter; in this way the initializer_list
// composes the switch statement, each item in it becomes a case, and the
// lambda can invoke code that is specialized by the constexpr index;
// each specialization is then the code body of the corresponding case

#ifndef ZuSwitch_HH
#define ZuSwitch_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuSeq.hh>

#include <initializer_list>

namespace ZuSwitch {

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-value"
template <typename R, typename Seq> struct Dispatch;
template <unsigned ...I> struct Dispatch<void, ZuSeq<I...>> {
  template <typename L>
  ZuInline static constexpr void fn(unsigned i, L &&l) {
    std::initializer_list<int>{
      (i == I ? ZuFwd<L>(l)(ZuUnsigned<I>{}), 0 : 0)...
    };
  }
};
template <typename R, unsigned ...I> struct Dispatch<R, ZuSeq<I...>> {
  template <typename L>
  ZuInline static constexpr R fn(unsigned i, L &&l) {
    R r{};
    std::initializer_list<int>{
      (i == I ? (r = ZuFwd<L>(l)(ZuUnsigned<I>{})), 0 : 0)...
    };
    return r;
  }
  template <typename L, typename R_>
  ZuInline static constexpr R fn(unsigned i, L &&l, R_ &&deflt) {
    R r = ZuFwd<R_>(deflt);
    std::initializer_list<int>{
      (i == I ? (r = ZuFwd<L>(l)(ZuUnsigned<I>{})), 0 : 0)...
    };
    return r;
  }
};
#pragma GCC diagnostic pop

template <unsigned N, typename L>
ZuInline constexpr decltype(auto) dispatch(unsigned i, L &&l) {
  using R = decltype(ZuFwd<L>(l)(ZuUnsigned<0>{}));
  return Dispatch<ZuDecay<R>, ZuMkSeq<N>>::fn(i, ZuFwd<L>(l));
}

template <unsigned N, typename L, typename R>
ZuInline constexpr decltype(auto) dispatch(unsigned i, L &&l, R &&deflt) {
  return Dispatch<ZuDecay<R>, ZuMkSeq<N>>::fn(i, ZuFwd<L>(l), ZuFwd<R>(deflt));
}

template <typename Seq, typename L>
ZuInline constexpr decltype(auto) dispatch(unsigned i, L &&l) {
  using R = decltype(ZuFwd<L>(l)(ZuUnsigned<0>{}));
  return Dispatch<ZuDecay<R>, Seq>::fn(i, ZuFwd<L>(l));
}

template <typename Seq, typename L, typename R>
ZuInline constexpr decltype(auto) dispatch(unsigned i, L &&l, R &&deflt) {
  return Dispatch<ZuDecay<R>, Seq>::fn(i, ZuFwd<L>(l), ZuFwd<R>(deflt));
}

} // ZuSwitch

#endif /* ZuSwitch_HH */
