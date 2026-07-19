//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// function delegate optimized for performance and avoidance of heap allocation
// - most function delegate capture packs can be reduced to a single pointer
//   (or smart pointer), usually `this`
// - for the common use case of a callback with a single context pointer,
//   ZmFn can capture the pointer and the function address by value, avoiding
//   heap allocation entirely
// - ZmFn falls back to heap allocation for larger capture packs
// - ZmFn<> is shorthand for ZmFn<void()>
// - the built-in by-value capture is a ZmContext, which can be a raw pointer
//   or a ZmRef<T> where T is ZmPolymorph-derived (i.e. is intrusively
//   reference-counted and has a virtual destructor); when used with
//   ZmRef/ZmPolymorph, the ZmFn increments the reference-count of the
//   referenced object during its lifetime, ensuring that it's lifetime
//   exceeds that of the ZmFn

// usage:
//
// ZmThread t([]{ puts("Hello World"); });	// lambda
//
// R foo() { ... }				// plain function
// ZmThread t(ZmFn<>::Ptr<&foo>::fn());		// R must cast ok to uintptr_t
//
// class F { void *operator ()() { ... } };	// callable object
// F f;
// ZmThread t(ZuMv(f));
//
// class G {
//   void *bar() const { ... }			// member function
// };
// G g;
// ZmThread t(&g, ZmFnPtr<&G::bar>);		// Note: pointer to G
//
// class G2 : public ZmPolymorph, public G { };	// call G::bar via G2
// G2 g2;
// ZmThread t(ZmMkRef(&g2), ZmFnPtr<&G::bar>); // capture ZmRef to g2
//
// class H { static int bah() { ... } };	// static member function
// ZmThread t{ZmFn<>::Ptr<&H::bah>::fn()};
//
// class I { ... };				// bound regular function
// void baz(I *i) { ... }
// I *i;
// ZmThread t{ZmFn<>{i, ZmFnPtr<&baz>{}}};	// Note: pointer to I
//
// using Fn = ZmFn<R(Params)>;
// R foo(Fn fn);
// foo(Fn([this, ...](params) { ... }));
//
// Note: The lambda closure objects are managed by a ZmHeap, but the size
// of each object depends on the captures used, resulting in multiple
// heap caches for different sizes of lambda
//
// stateless lambdas do not have any captures, i.e. [](...) { ... }
// stateless lambdas should not have data members, and C++11 guarantees they
// are convertible to a primitive function pointer; lambdas with captures
// cannot be converted to a function pointer; stateless lambdas are
// implicitly const (immutable)
//
// if the lambda has no captures, the lambda will not be instantiated or
// heap allocated; both fn1 and fn2 below behave identically, but fn2 is
// much more efficient since the ZmRef<O> o is captured by ZmFn<>
// and the lambda remains stateless, instead of o being captured by the
// lambda and causing heap allocation; fn3 is more efficient than fn2,
// moving the ZmRef into the ZmFn, avoiding unnecessary manipulation of
// the reference count
//
// struct O { ... void fn() { ... } };
// ZmRef<O> o = new O(...);
// ZmFn<> fn1{[o = ZuMv(o)]() { o->fn(); }};	// inefficient
// ZmFn<> fn2{ZuMv(o), [](O *o) { o->fn(); }};	// built-in ZmFn capture
// ZmFn<> fn3{ZmFn<>::mvFn(ZuMv(o), [](ZmRef<O> o) { o->fn(); })}; // move

// performance:
// member/lambda overhead is ~2ns per call (g++ -O3, Core i7 @3.6GHz)

#ifndef ZmFn_HH
#define ZmFn_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZmFn_.hh>
#include <zlib/ZmHeap.hh>

// heap-allocated reference-counted stateful lambda
template <typename Heap, typename L, typename ArgList> struct ZmLambda_;
template <typename Heap, typename L, typename ...Args>
struct ZmLambda_<Heap, L, ZuTypeList<Args...>> :
  public Heap, public ZmPolymorph
{
  L lambda;

  ZmLambda_(L l) : lambda{ZuMv(l)} { }

  decltype(auto) invoke(Args... args) {
    return lambda(ZuFwd<Args>(args)...);
  }
  decltype(auto) cinvoke(Args... args) const {
    return lambda(ZuFwd<Args>(args)...);
  }

  ZmLambda_() = delete;
  ZmLambda_(const ZmLambda_ &) = delete;
  ZmLambda_ &operator =(const ZmLambda_ &) = delete;
  ZmLambda_(ZmLambda_ &&) = delete;
  ZmLambda_ &operator =(ZmLambda_ &&) = delete;
};
template <typename HeapID, bool Sharded, typename L, typename ArgList>
using ZmLambda = ZmLambda_<
  ZmHeap_<HeapID, ZmLambda_<ZuVoid, L, ArgList>, Sharded>,
  L, ArgList>;

// stateful immutable lambda
template <typename R_, typename ...Args_, typename NTP>
template <typename L>
template <typename L_>
ZmFn<R_(Args_...), NTP>
ZmFn<R_(Args_...), NTP>::LambdaInvoker<L, false, false>::fn(L_ &&l) {
  ZuAssert((!IsMutable<L>{}));
  ZuAssert((!IsMutable<ZuDecay<L_>>{}));
  using O = ZmLambda<HeapID, Sharded, ZuDecay<L>, Args>;
  return ZmFn{ZmRef<const O>{new O{ZuFwd<L_>(l)}}, ZmFnPtr<&O::cinvoke>{}};
}

// stateful mutable lambda
template <typename R_, typename ...Args_, typename NTP>
template <typename L>
template <typename L_>
ZmFn<R_(Args_...), NTP>
ZmFn<R_(Args_...), NTP>::LambdaInvoker<L, false, true>::fn(L_ &&l) {
  using O = ZmLambda<HeapID, Sharded, ZuDecay<L>, Args>;
  return ZmFn{ZmRef<O>{new O{ZuFwd<L_>(l)}}, ZmFnPtr<&O::invoke>{}};
}

#endif /* ZmFn_HH */
