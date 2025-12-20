//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// condition variable

#ifndef ZmCondition_HH
#define ZmCondition_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZmPlatform.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmSpecific.hh>
#include <zlib/ZmSemaphore.hh>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4101 4251)
#endif

template <typename Lock> class ZmCondition_ {
  ZmCondition_() = delete;

protected:
  using Wait = typename Lock::Wait;

  ZmCondition_(Lock &lock) : m_lock{lock} { }

  ZuInline Wait wait_() { return m_lock.wait(); }
  ZuInline void lock_() { m_lock.lock_(); }
  ZuInline void unlock_() { m_lock.unlock_(); }

private:
  Lock		&m_lock;
};

class ZmNoLock;

template <> class ZmCondition_<ZmNoLock> {
protected:
  struct Wait { };

  ZmCondition_() { }
  ZmCondition_(ZmNoLock &) { }

  ZuInline Wait wait_() { return {}; }
  ZuInline void lock_() { }
  ZuInline void unlock_() { }
};

template <typename Lock> class ZmCondition : public ZmCondition_<Lock> {
  using Base = ZmCondition_<Lock>;
  using Wait = typename Base::Wait;
  using Base::wait_;
  using Base::lock_;
  using Base::unlock_;

  struct Thread : public ZmObject {
    ZmSemaphore	sem;
    Thread	*next = nullptr;
    Thread	*prev = nullptr;
    bool	waiting = false;
  };

public:
  template <
    typename Lock_ = Lock,
    decltype(ZuSame<ZmNoLock, Lock_>(), int()) = 0>
  ZmCondition() : Base{} { }
  template <
    typename Lock_ = Lock,
    decltype(ZuNotSame<ZmNoLock, Lock_>(), int()) = 0>
  ZmCondition(Lock &lock) : Base{lock} { }
  ~ZmCondition() { }

  ZmCondition() = delete;
  ZmCondition(const ZmCondition &) = delete;
  ZmCondition &operator =(const ZmCondition &) = delete; // prevent mis-use

  void wait() {
    Wait wait{this->wait_()};
    Thread *thread = ZmSpecific<Thread>::instance();
    thread->next = nullptr;
    thread->waiting = true;
    {
      ZmGuard<ZmPLock> guard(m_condLock);
      thread->prev = m_tail;
      if (m_head)
	m_tail->next = thread;
      else
	m_head = thread;
      m_tail = thread;
    }
    unlock_();
    thread->sem.wait();
    lock_();
  }
  int timedWait(ZuTime timeout) {
    Wait wait{this->wait_()};
    Thread *thread = ZmSpecific<Thread>::instance();
    thread->next = nullptr;
    thread->waiting = true;
    {
      ZmGuard<ZmPLock> guard(m_condLock);
      thread->prev = m_tail;
      if (m_head)
	m_tail->next = thread;
      else
	m_head = thread;
      m_tail = thread;
    }
    unlock_();
    if (thread->sem.timedwait(timeout)) { // timed out
      ZmGuard<ZmPLock> guard(m_condLock);
      if (thread->waiting) { // normal case, still waiting
	if (thread->prev)
	  thread->prev->next = thread->next;
	else
	  m_head = thread->next;
	if (thread->next)
	  thread->next->prev = thread->prev;
	else
	  m_tail = thread->prev;
	thread->waiting = false;
	guard.unlock();
	lock_();
	return -1;
      }
      guard.unlock(); // timed out, but was concurrently woken
      thread->sem.wait();
    }
    lock_();
    return 0;
  }
  void signal() {
    ZmGuard<ZmPLock> guard(m_condLock);
    if (Thread *thread = m_head) {
      if (!(m_head = thread->next)) {
	m_tail = nullptr;
      } else {
	m_head->prev = nullptr;
      }
      thread->waiting = false;
      guard.unlock();
      thread->sem.post();
      return;
    }
  }
  void broadcast() {
loop:
    ZmGuard<ZmPLock> guard(m_condLock);
    if (Thread *thread = m_head) {
      if (!(m_head = thread->next)) {
	m_tail = nullptr;
      } else {
	m_head->prev = nullptr;
      }
      thread->waiting = false;
      guard.unlock();
      thread->sem.post();
      goto loop;
    }
  }

private:
  ZmPLock	m_condLock;
    Thread	  *m_head = nullptr;
    Thread	  *m_tail = nullptr;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif /* ZmCondition_HH */
