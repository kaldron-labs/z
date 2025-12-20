//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// generic invocation

// ZuInvoke<Fn>(this, args...) invokes one of:
//   (this->*Fn)(args...)	// member function
//   Fn(this, args...)		// bound function/lambda (passing this)
//   Fn(args)			// unbound function/lambda (discarding this)

#ifndef ZuInvoke_HH
#define ZuInvoke_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuTL.hh>

template <auto Fn, typename O, typename Ts, typename = void>
struct ZuInvoke_MemberFn_;
template <auto Fn, typename O, typename ...Ts>
struct ZuInvoke_MemberFn_<
    Fn, O, ZuTypeList<Ts...>,
    decltype((ZuDeclVal<O *>()->*Fn)(ZuDeclVal<Ts>()...), void())> {
  using T = decltype((ZuDeclVal<O *>()->*Fn)(ZuDeclVal<Ts>()...));
};
template <auto Fn, typename O, typename ...Ts>
using ZuInvoke_MemberFn =
  typename ZuInvoke_MemberFn_<Fn, O, ZuTypeList<Ts...>>::T;
template <auto Fn, typename O, typename ...Ts>
auto ZuInvoke(O *ptr, Ts &&... args) -> ZuInvoke_MemberFn<Fn, O, Ts...> {
  return (ptr->*Fn)(ZuFwd<Ts>(args)...);
}

template <auto Fn, typename O, typename Ts, typename = void>
struct ZuInvoke_BoundFn_;
template <auto Fn, typename O, typename ...Ts>
struct ZuInvoke_BoundFn_<
    Fn, O, ZuTypeList<Ts...>,
    decltype(Fn(ZuDeclVal<O *>(), ZuDeclVal<Ts>()...), void())> {
  using T = decltype(Fn(ZuDeclVal<O *>(), ZuDeclVal<Ts>()...));
};
template <auto Fn, typename O, typename ...Ts>
using ZuInvoke_BoundFn =
  typename ZuInvoke_BoundFn_<Fn, O, ZuTypeList<Ts...>>::T;
template <auto Fn, typename O, typename ...Ts>
auto ZuInvoke(O *ptr, Ts &&... args) -> ZuInvoke_BoundFn<Fn, O, Ts...> {
  return Fn(ptr, ZuFwd<Ts>(args)...);
}

template <auto Fn, typename O, typename Ts, typename = void>
struct ZuInvoke_UnboundFn_;
template <auto Fn, typename O, typename ...Ts>
struct ZuInvoke_UnboundFn_<
    Fn, O, ZuTypeList<Ts...>,
    decltype(Fn(ZuDeclVal<Ts>()...), void())> {
  using T = decltype(Fn(ZuDeclVal<Ts>()...));
};
template <auto Fn, typename O, typename ...Ts>
using ZuInvoke_UnboundFn =
  typename ZuInvoke_UnboundFn_<Fn, O, ZuTypeList<Ts...>>::T;
template <auto Fn, typename O, typename ...Ts>
auto ZuInvoke(O *, Ts &&... args) -> ZuInvoke_UnboundFn<Fn, O, Ts...> {
  return Fn(ZuFwd<Ts>(args)...);
}

#endif /* ZuInvoke_HH */
