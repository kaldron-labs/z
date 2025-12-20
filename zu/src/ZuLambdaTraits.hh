//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// compile-time callable traits for lambdas
// - works with templated call operators (generic lambdas)
// - C++23 static operator() makes all this redundant, but the ABI
//   changes wreak havoc

#ifndef ZuLambdaTraits_HH
#define ZuLambdaTraits_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuAssert.hh>
#include <zlib/ZuTL.hh>

#ifdef _MSC_VER
#pragma once
#endif

namespace ZuLambdaTraits {

// deduce args of a non-generic lambda - ArgList<L>
template <typename L, auto Fn> struct ArgList__; // undefined
template <
  typename L, typename R,
  typename ...FnArgs, R (*Fn)(FnArgs...)>
struct ArgList__<L, Fn> { using T = ZuTypeList<FnArgs...>; }; // static
template <
  typename L, typename R,
  typename ...FnArgs, R (L::*Fn)(FnArgs...)>
struct ArgList__<L, Fn> { using T = ZuTypeList<FnArgs...>; }; // mutable
template <
  typename L, typename R,
  typename ...FnArgs, R (L::*Fn)(FnArgs...) const>
struct ArgList__<L, Fn> { using T = ZuTypeList<FnArgs...>; }; // const
template <typename L, typename = void> struct ArgList_; // undefined
template <typename L> struct ArgList_<L, decltype(&L::operator(), void())> :
    public ArgList__<L, &L::operator()> { };
template <typename L> using ArgList = typename ArgList_<ZuDecay<L>>::T;

// deduce return type of lambda - Return<L, ArgList>
template <typename L, typename ArgList_> struct Return_;
template <typename L>
struct Return_<L, ZuTypeList<>> {
  using T = decltype(ZuDeclVal<L &>()());
};
template <typename L, typename ...Args>
struct Return_<L, ZuTypeList<Args...>> {
  using T = decltype(ZuDeclVal<L &>()(ZuDeclVal<Args>()...));
};
template <typename L, typename ArgList_ = ArgList<L>>
using Return = typename Return_<ZuDecay<L>, ArgList_>::T;

template <
  typename L, typename ArgList_ = ArgList<L>, typename = Return<L, ArgList_>>
struct IsVoidRet : public ZuFalse { };
template <typename L, typename ArgList_>
struct IsVoidRet<L, ArgList_, void> : public ZuTrue { };

// deduce mutability of lambda - IsMutable<L, ArgList>
template <typename L, typename = void>
struct IsMutable_NoArgs : public ZuTrue { };
template <typename L>
struct IsMutable_NoArgs<L, decltype(ZuDeclVal<const L &>()(), void())> :
    public ZuFalse { };
template <typename L, typename ArgList_, typename = void>
struct IsMutable_Args : public ZuTrue { };
template <typename L, typename ...Args>
struct IsMutable_Args<L, ZuTypeList<Args...>,
  decltype(ZuDeclVal<const L &>()(ZuDeclVal<Args>()...), void())> :
    public ZuFalse { };
template <typename L, typename ArgList_ = ArgList<L>>
struct IsMutable : public IsMutable_Args<ZuDecay<L>, ArgList_> { };
template <typename L>
struct IsMutable<L, ZuTypeList<>> : public IsMutable_NoArgs<ZuDecay<L>> { };

// deduce statefulness of lambda - IsStateless<L, ArgList>
template <typename ArgList_, typename FnArgList>
struct IsStateless_Fn__ : public ZuBool<ArgList_::N == FnArgList::N> { };
template <
  typename L, typename ArgList_,
  typename R, typename FnArgList, typename = void>
struct IsStateless_Fn_ : public ZuFalse { };
template <
  typename L, typename ArgList_,
  typename R, typename ...FnArgs>
struct IsStateless_Fn_<L, ArgList_, R, ZuTypeList<FnArgs...>,
  decltype(static_cast<R (*)(FnArgs...)>(ZuDeclVal<const L &>()), void())> :
    public IsStateless_Fn__<ArgList_, ZuTypeList<FnArgs...>> { };
template <typename L, typename ArgList_, auto Fn>
struct IsStateless_Fn : public ZuFalse { };
template <
  typename L, typename ArgList_,
  typename R, typename ...FnArgs, R (*Fn)(FnArgs...)> // static
struct IsStateless_Fn<L, ArgList_, Fn> :
    public IsStateless_Fn_<L, ArgList_, R, ZuTypeList<FnArgs...>> { };
template <
  typename L, typename ArgList_,
  typename R, typename ...FnArgs, R (L::*Fn)(FnArgs...)> // mutable
struct IsStateless_Fn<L, ArgList_, Fn> :
    public IsStateless_Fn_<L, ArgList_, R, ZuTypeList<FnArgs...>> { };
template <
  typename L, typename ArgList_,
  typename R, typename ...FnArgs, R (L::*Fn)(FnArgs...) const> // const
struct IsStateless_Fn<L, ArgList_, Fn> :
    public IsStateless_Fn_<L, ArgList_, R, ZuTypeList<FnArgs...>> { };
template <typename L, typename R, typename ArgList_, typename = void>
struct IsStateless_Generic_ : public ZuFalse { };
template <typename L, typename R, typename ...Args>
struct IsStateless_Generic_<L, R, ZuTypeList<Args...>,
  decltype(static_cast<R (*)(Args...)>(ZuDeclVal<const L &>()), void())> :
    public ZuTrue { };
template <typename L, typename = void>
struct IsStateless_Generic_NoArgs : public ZuFalse { };
template <typename L>
struct IsStateless_Generic_NoArgs<L, decltype(ZuDeclVal<L &>()(), void())> :
    public IsStateless_Generic_<L, Return<L, ZuTypeList<>>, ZuTypeList<>> { };
template <typename L, typename ArgList, typename = void>
struct IsStateless_Generic_Args : public ZuFalse { };
template <typename L, typename ...Args>
struct IsStateless_Generic_Args<L, ZuTypeList<Args...>,
  decltype(ZuDeclVal<L &>()(ZuDeclVal<Args>()...), void())> :
    public IsStateless_Generic_<
      L, Return<L, ZuTypeList<Args...>>, ZuTypeList<Args...>> { };
template <typename L, typename ArgList> struct IsStateless_Generic;
template <typename L>
struct IsStateless_Generic<L, ZuTypeList<>> :
    public IsStateless_Generic_NoArgs<L> { };
template <typename L, typename ...Args>
struct IsStateless_Generic<L, ZuTypeList<Args...>> :
    public IsStateless_Generic_Args<L, ZuTypeList<Args...>> { };
template <typename L, typename ArgList_, typename = void>
struct IsStateless_ : public IsStateless_Generic<L, ArgList_> { };
template <typename L, typename ArgList_>
struct IsStateless_<L, ArgList_, decltype(&L::operator(), void())> :
    public IsStateless_Fn<L, ArgList_, &L::operator()> { };
template <typename L, typename ArgList_ = ArgList<L>>
using IsStateless = IsStateless_<ZuDecay<L>, ArgList_>;

// convert stateless lambda to plain function

template <typename L, typename R, typename ArgList_> struct InvokeFnT_;
template <typename L, typename R, typename ...Args>
struct InvokeFnT_<L, R, ZuTypeList<Args...>> {
  typedef R (*T)(Args...);
};
template <
  typename L,
  typename ArgList_ = ArgList<L>,
  typename R = Return<L, ArgList_>>
using InvokeFnT = typename InvokeFnT_<ZuDecay<L>, R, ArgList_>::T;

} // ZuLambdaTraits

template <typename L> using ZuArgList = ZuLambdaTraits::ArgList<L>;

template <typename L, typename ArgList = ZuArgList<L>>
using ZuIsMutableLambda = ZuLambdaTraits::IsMutable<L, ArgList>;
template <typename L, typename ArgList = ZuArgList<L>>
using ZuIsVoidRetLambda = ZuLambdaTraits::IsVoidRet<L, ArgList>;
template <typename L, typename ArgList = ZuArgList<L>>
using ZuIsStatelessLambda = ZuLambdaTraits::IsStateless<L, ArgList>;
template <typename L, typename ArgList = ZuArgList<L>>
using ZuLambdaReturn = ZuLambdaTraits::Return<L, ArgList>;
template <typename L, typename ArgList = ZuArgList<L>>
using ZuInvokeFnT = ZuLambdaTraits::InvokeFnT<L, ArgList>;

template <typename L, typename ArgList = ZuArgList<L>>
constexpr auto ZuInvokeFn(const L &l) {
  return static_cast<ZuInvokeFnT<L>>(l);
}

// stateless lambdas invoked using a placeholder this...
// may be technically undefined behavior, but it elides capturing this
// - this is intentionally not constexpr, it is intended for run-time only
//
// Note: C++ standards have gone backwards and forwards here; this
// implementation is consistent with the behavior of gcc, clang, MSVC, etc.:
// - this->x does not imply evaluating (*this).x (the reverse is true)
//   if this points at an empty class (there is nothing to dereference)
// - while this is passed to non-static member functions as a parameter,
//   if the class is empty (has no data members), has no vtbl and is final,
//   a non-static member function cannot make any use of the this pointer
//   other than to call other member functions
// - stateless lambdas without captures are required to be convertible to
//   plain function pointers - they are implicitly empty, final, and
//   do not have a vtbl - they have just one member function - operator ()
// - define ZuLambda_Dogma to 1 for strict/dogmatic UB conformance

#ifndef ZuLambda_Dogma
#define ZuLambda_Dogma 0	// dogmatic undefined behavior conformance
#endif

template <typename L_, typename ArgList = ZuArgList<L_>, typename ...Args>
auto ZuInvokeLambda(Args &&...args) {
  using L = ZuDecay<L_>;
  ZuAssert((ZuIsStatelessLambda<L, ArgList>{}));
#if ZuLambda_Dogma
  struct _ {
    constexpr _() noexcept { }
    constexpr ~_() noexcept { }
    union { L _; };
  };
  ZuAssert(sizeof(L) == sizeof(_));
#endif
  return
#if ZuLambda_Dogma
    (&_._)
#else
    reinterpret_cast<L *>(0)
#endif
    ->operator ()(ZuFwd<Args>(args)...);
}

template <typename L, typename ArgList = ZuArgList<L>, typename R = void>
using ZuMutableLambda = ZuIfT<ZuIsMutableLambda<L, ArgList>{}, R>;
template <typename L, typename ArgList = ZuArgList<L>, typename R = void>
using ZuNotMutableLambda = ZuIfT<!ZuIsMutableLambda<L, ArgList>{}, R>;
template <typename L, typename ArgList = ZuArgList<L>, typename R = void>
using ZuVoidRetLambda = ZuIfT<ZuIsVoidRetLambda<L, ArgList>{}, R>;
template <typename L, typename ArgList = ZuArgList<L>, typename R = void>
using ZuNotVoidRetLambda = ZuIfT<!ZuIsVoidRetLambda<L, ArgList>{}, R>;
template <typename L, typename ArgList = ZuArgList<L>, typename R = void>
using ZuStatelessLambda = ZuIfT<ZuIsStatelessLambda<L, ArgList>{}, R>;
template <typename L, typename ArgList = ZuArgList<L>, typename R = void>
using ZuNotStatelessLambda = ZuIfT<!ZuIsStatelessLambda<L, ArgList>{}, R>;

#endif /* ZuLambdaTraits_HH */
