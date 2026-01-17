//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// fast platform-native recursive mutex

#ifndef ZmLock_HH
#define ZmLock_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZmPLock.hh>

#if defined(ZDEBUG) && !defined(ZmLock_DEBUG)
#define ZmLock_DEBUG	// enable testing / debugging
#endif

#ifdef ZmLock_DEBUG
#include <zlib/ZmBackTracer.hh>
#endif

class ZmLock;

#ifdef ZmLock_DEBUG
template <unsigned> class ZmBackTracer;
class ZmAPI ZmLock_Debug {
friend ZmLock;
  static void enable();
  static void disable();
  static void capture(unsigned skip = 1);
  static ZmBackTracer<64> *tracer();
};
#endif /* ZmLock_DEBUG */

#ifdef _MSC_VER
template class ZmAPI ZmAtomic<Zm::ThreadID>;
#endif

class ZmLock {
public:
  ZmLock() { ZmPLock_init(m_lock); }
  ~ZmLock() { ZmPLock_final(m_lock); }

  ZmLock(const ZmLock &) = delete;
  ZmLock &operator =(const ZmLock &) = delete;

  void lock() {
    Zm::ThreadID tid = Zm::getTID();
    if (m_tid == tid) { m_count++; return; } // acquire
#ifdef ZmLock_DEBUG
    if (m_prevTID && m_prevTID != tid)
      ZmLock_Debug::capture();
    m_prevTID = tid;
#endif
    ZmPLock_lock(m_lock);
    m_count = 1;
    m_tid = tid; // release
  }
  int trylock() {
    auto tid = Zm::getTID();
    if (m_tid == tid) { ++m_count; return 0; } // acquire
    if (ZmPLock_trylock(m_lock)) return -1;
    m_count = 1;
    m_tid = tid; // release
    return 0;
  }
  void unlock() {
    if (!m_count) return;
    auto tid = Zm::getTID();
    if (m_tid != tid) return; // acquire
    if (!--m_count) {
      m_tid = 0; // release
      ZmPLock_unlock(m_lock);
    }
  }

#ifdef ZmLock_DEBUG
  static void traceEnable() { ZmLock_Debug::enable(); }
  static void traceDisable() { ZmLock_Debug::disable(); }
  static ZmBackTracer<64> *tracer() { return ZmLock_Debug::tracer(); }
#endif

  // ZmCondition integration
  class Wait;
friend Wait;
  class Wait {
  friend ZmLock;
  private:
    Wait(ZmLock &lock) :
	m_lock(lock), m_count(lock.m_count), m_tid(lock.m_tid) {
      m_lock.m_count = 0;
      m_lock.m_tid = 0;
    }
  public:
    ~Wait() {
      m_lock.m_count = m_count;
      m_lock.m_tid = m_tid;
    }
  private:
    ZmLock		&m_lock;
    uint32_t		m_count;
    Zm::ThreadID	m_tid;
  };
  Wait wait() { return {*this}; }
  ZuInline void lock_() { ZmPLock_lock(m_lock); }
  ZuInline void unlock_() { ZmPLock_unlock(m_lock); }

private:
  ZmPLock_			m_lock;
  uint32_t			m_count = 0;
  ZmAtomic<Zm::ThreadID>	m_tid = 0;
#ifdef ZmLock_DEBUG
  Zm::ThreadID			m_prevTID = 0;
#endif
};

#endif /* ZmLock_HH */
