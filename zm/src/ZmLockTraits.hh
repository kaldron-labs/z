//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// lock traits

#ifndef ZmLockTraits_HH
#define ZmLockTraits_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

template <class Lock_> struct ZmGenericLockTraits {
  using Lock = Lock_;
  enum { CanTry = 1, Recursive = 1, RWLock = 0 };
  ZuInline static void lock(Lock &l) { l.lock(); }
  ZuInline static int trylock(Lock &l) { return l.trylock(); }
  ZuInline static void unlock(Lock &l) { l.unlock(); }
  ZuInline static void readlock(Lock &l) { l.lock(); }
  ZuInline static int readtrylock(Lock &l) { return l.trylock(); }
  ZuInline static void readunlock(Lock &l) { l.unlock(); }
};

template <class Lock>
struct ZmLockTraits : public ZmGenericLockTraits<Lock> { };

#endif /* ZmLockTraits_HH */
