//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// generic blocking call to async function with continuation
//
// consolidates thread-local semaphore usage into a single instance

#ifndef ZmBlock_HH
#define ZmBlock_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuTuple.hh>

#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmSpecific.hh>
#include <zlib/ZmAlloc.hh>

namespace ZmBlock_ {
  inline ZmSemaphore &sem() { return ZmTLS<ZmSemaphore, sem>(); }
}

template <typename ...Args> struct ZmBlock {
  using R = ZuTuple<Args...>;
  template <typename L>
  R operator ()(L &&l) const {
    R r;
    auto &sem = ZmBlock_::sem();
    ZuFwd<L>(l)([&sem, &r](Args... args) mutable {
      r = ZuMvTuple(ZuMv(args)...);
      sem.post();
    });
    sem.wait();
    return r;
  }
  template <typename L, typename Reduce>
  R operator ()(unsigned n, L l, Reduce reduce) const {
    auto r = ZmAlloc(R, n);
    if (!r) throw std::bad_alloc{};
    auto &sem = ZmBlock_::sem();
    for (unsigned i = 0; i < n; i++)
      l(i, [&sem, &r = r[i]](Args... args) mutable {
	r = ZuMvTuple(ZuMv(args)...);
	sem.post();
      });
    for (unsigned i = 0; i < n; i++) sem.wait();
    for (unsigned i = 1; i < n; i++) reduce(r[0], r[i]);
    return r[0];
  }
};
template <typename Arg> struct ZmBlock<Arg> {
  using R = Arg;
  template <typename L>
  R operator ()(L &&l) const {
    R r;
    auto &sem = ZmBlock_::sem();
    ZuFwd<L>(l)([&sem, &r](Arg arg) mutable {
      r = ZuMv(arg);
      sem.post();
    });
    sem.wait();
    return r;
  }
  template <typename L, typename Reduce>
  R operator ()(unsigned n, L l, Reduce reduce) const {
    auto r = ZmAlloc(R, n);
    if (!r) throw std::bad_alloc{};
    auto &sem = ZmBlock_::sem();
    for (unsigned i = 0; i < n; i++)
      l(i, [&sem, &r = r[i]](Arg arg) mutable {
	r = ZuMv(arg);
	sem.post();
      });
    for (unsigned i = 0; i < n; i++) sem.wait();
    for (unsigned i = 1; i < n; i++) reduce(r[0], r[i]);
    return r[0];
  }
};
template <> struct ZmBlock<> {
  template <typename L>
  void operator ()(L &&l) const {
    auto &sem = ZmBlock_::sem();
    ZuFwd<L>(l)([&sem]() mutable { sem.post(); });
    sem.wait();
    return;
  }
  template <typename L>
  void operator ()(unsigned n, L &&l) const {
    auto &sem = ZmBlock_::sem();
    for (unsigned i = 0; i < n; i++)
      ZuFwd<L>(l)(i, [&sem]() mutable { sem.post(); });
    for (unsigned i = 0; i < n; i++) sem.wait();
    return;
  }
};

#endif /* ZmBlock_HH */
