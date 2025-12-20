//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// ck FAS spinlock wrapper

#ifndef ZmSpinLock_HH
#define ZmSpinLock_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZmPlatform.hh>
#include <zlib/ZmLockTraits.hh>

#ifdef _WIN32
using ZmSpinLock = ZmPLock;
#endif

#ifndef _WIN32
#include <ck_spinlock.h>

class ZmSpinLock {
public:
  ZuInline ZmSpinLock() { ck_spinlock_fas_init(&m_lock); }

  ZmSpinLock(const ZmSpinLock &) = delete;
  ZmSpinLock &operator =(const ZmSpinLock &) = delete;

  ZuInline void lock() { ck_spinlock_fas_lock(&m_lock); }
  ZuInline int trylock() { return -!ck_spinlock_fas_trylock(&m_lock); }
  ZuInline void unlock() { ck_spinlock_fas_unlock(&m_lock); }

private:
  ck_spinlock_fas_t	m_lock;
};

template <>
struct ZmLockTraits<ZmSpinLock> : public ZmGenericLockTraits<ZmSpinLock> {
  enum { CanTry = 1, Recursive = 0, RWLock = 0 };
};
#endif /* !_WIN32 */

#endif /* ZmSpinLock_HH */
