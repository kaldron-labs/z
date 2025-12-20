//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// guard template for locks

#ifndef ZmGuard_HH
#define ZmGuard_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZmLockTraits.hh>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4800)
#endif

template <class Lock> class ZmGuard : private ZmLockTraits<Lock> {
  using Traits = ZmLockTraits<Lock>;

public:
  struct Try { };	// disambiguator

  ZmGuard() : m_lock{nullptr} { }

  ZmGuard(Lock &l) : m_lock(&l) { this->lock(l); }

  explicit ZmGuard(Lock &l, Try) : m_lock(&l) {
    if (this->trylock(l)) m_lock = nullptr;
  }
  explicit ZmGuard(Lock &l, Try, int &r) : m_lock(&l) {
    if (r = this->trylock(l)) m_lock = nullptr;
  }

  ZmGuard(ZmGuard &&guard) : m_lock(guard.m_lock) {
    guard.m_lock = nullptr;
  }
  ZmGuard &operator =(ZmGuard &&guard) {
    if (ZuLikely(this != &guard)) {
      if (m_lock) Traits::unlock(*m_lock);
      m_lock = guard.m_lock;
      guard.m_lock = nullptr;
    }
    return *this;
  }

  ~ZmGuard() { if (m_lock) Traits::unlock(*m_lock); }

  bool locked() const { return m_lock; }

  void unlock() {
    if (m_lock) { Traits::unlock(*m_lock); m_lock = nullptr; }
  }

private:
  Lock		*m_lock;
};

template <typename Lock> ZmGuard(Lock &) -> ZmGuard<Lock>;

#ifdef _MSC_VER
#pragma warning(pop)
#endif

template <class Lock> class ZmReadGuard : private ZmLockTraits<Lock> {
  using Traits = ZmLockTraits<Lock>;

public:
  struct Try { };

  ZmReadGuard(const Lock &l) :
    m_lock(&(const_cast<Lock &>(l))) {
    this->readlock(const_cast<Lock &>(l));
  }
  explicit ZmReadGuard(const Lock &l, Try) :
      m_lock(&(const_cast<Lock &>(l))) {
    if (this->tryreadlock(const_cast<Lock &>(l))) m_lock = nullptr;
  }
  explicit ZmReadGuard(const Lock &l, Try, int &r) :
      m_lock(&(const_cast<Lock &>(l))) {
    if (r = this->tryreadlock(const_cast<Lock &>(l))) m_lock = nullptr;
  }
  ZmReadGuard(ZmReadGuard &&guard) : m_lock(guard.m_lock) {
    guard.m_lock = nullptr;
  }
  ~ZmReadGuard() { if (m_lock) Traits::readunlock(*m_lock); }

  void unlock() {
    if (m_lock) { Traits::readunlock(*m_lock); m_lock = nullptr; }
  }

  ZmReadGuard &operator =(ZmReadGuard &&guard) {
    if (ZuLikely(this != &guard)) {
      if (m_lock) Traits::readunlock(*m_lock);
      m_lock = guard.m_lock;
      guard.m_lock = nullptr;
    }
    return *this;
  }

private:
  Lock		*m_lock;
};

#endif /* ZmGuard_HH */
