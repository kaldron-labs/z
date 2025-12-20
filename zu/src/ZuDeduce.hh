//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// function signature deduction

// ZuDeduce<decltype(&L::operator())>::
// ZuDeduce<decltype(&fn)>::
//   O		- object type (undefined for a plain function)
//   R		- return value
//   Args	- argument typelist

#ifndef ZuDeduce_HH
#define ZuDeduce_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuTL.hh>

template <typename> struct ZuDeduce;
template <typename O_, typename R_, typename ...Args_>
struct ZuDeduce<R_ (O_::*)(Args_...) const> {
  enum { Member = 1 };
  using O = O_;
  using R = R_;
  using Args = ZuTypeList<Args_...>;
};
template <typename O_, typename R_, typename ...Args_>
struct ZuDeduce<R_ (O_::*)(Args_...)> {
  enum { Member = 1 };
  using O = O_;
  using R = R_;
  using Args = ZuTypeList<Args_...>;
};
template <typename R_, typename ...Args_>
struct ZuDeduce<R_ (*)(Args_...)> {
  enum { Member = 0 };
  using R = R_;
  using Args = ZuTypeList<Args_...>;
};

#endif /* ZuDeduce_HH */
