//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdio.h>

#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmRWLock.hh>
#include <zlib/ZmSingleton.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmSemaphore.hh>

class Thread;

class Global {
public:
  Global() : m_started(ZuTime::Now), m_nThreads(0), m_threads(0),
	     m_nLocks(0), m_locks(0) { }
  ~Global() {
    if (m_threads) delete [] m_threads;
    if (m_locks) delete [] m_locks;
  }

  void start_(int nThreads, int nLocks);
  void stop_();

  static void start(int nThreads, int nLocks)
    { instance()->start_(nThreads, nLocks); }
  static void stop() { instance()->stop_(); }

  static ZuTime &started() { return instance()->m_started; }

  static Thread *thread(int i) { return instance()->m_threads[i]; }
  static ZmRWLock &lock(int i) { return instance()->m_locks[i]; }

  static Global *instance();

private:
  ZuTime			m_started;
  //ZmSemaphore			m_completed;
  int				m_nThreads;
  ZmRef<Thread>			*m_threads;
  int				m_nLocks;
  ZmRWLock			*m_locks;
};

class Work;

class Thread : public ZmObject {
public:
  Thread(int id) : m_id(id) { }

  void operator()();

  void start() {
    m_thread = ZmThread(0, ZmFn<>::Member<&Thread::operator()>::fn(this));
  }
  int synchronous(Work *work) {
    m_work = work;
    m_pending.post();
    m_completed.wait();
    return m_result;
  }
  void asynchronous(Work *work) {
    m_work = work;
    m_pending.post();
  }
  int result() {
    m_completed.wait();
    return m_result;
  }
  void stop() { m_work = 0; m_pending.post(); m_thread.join(); }

private:
  int			m_id;
  ZmThread		m_thread;
  ZmSemaphore		m_pending;
  ZmSemaphore		m_completed;
  ZmRef<Work>		m_work;
  int			m_result;
};

Global *Global::instance() { return ZmSingleton<Global>::instance(); }

void Global::start_(int nThreads, int nLocks)
{
  m_threads = new ZmRef<Thread>[m_nThreads = nThreads];
  m_locks = new ZmRWLock[m_nLocks = nLocks];
  for (int i = 0; i < nThreads; i++) {
    (m_threads[i] = new Thread(i))->start();
  }
}

void Global::stop_()
{
  for (int i = 0; i < m_nThreads; i++) {
    m_threads[i]->stop();
    m_threads[i] = 0;
  }
  delete [] m_threads;
  m_threads = 0;
  m_nThreads = 0;
  delete [] m_locks;
  m_locks = 0;
  m_nLocks = 0;
}

class Work : public ZmObject {
public:
  enum Insn { Lock, Unlock, ReadLock, ReadUnlock };

  Work(Insn insn, int lock, bool try_) :
	m_insn(insn), m_lock(lock), m_try(try_) { }

  int operator ()(int tid);

  void dump(int tid, const char *prePost, ZmRWLock &lock);

private:
  Insn		m_insn;
  int		m_lock;
  bool		m_try;
};

void Thread::operator()()
{
  for (;;) {
    m_pending.wait();
    {
      ZmRef<Work> work = m_work;
      if (!work) return;
      m_work = 0;
      m_result = (*work)(m_id);
    }
    m_completed.post();
  }
}

void Work::dump(int tid, const char *prePost, ZmRWLock &lock)
{
  const char *insns[] = { "Lock", "Unlock", "ReadLock", "ReadUnlock" };

  ZuTime now = Zm::now();
  ZuTime stamp = now - Global::started();

  if (m_try)
    printf("%+14.3f %3d %s %10s %3d %s Try\n",
	   stamp.dtime(), tid, prePost, insns[m_insn], m_lock,
	   lock.dump().data());
  else
    printf("%+14.3f %3d %s %10s %3d %s\n",
	   stamp.dtime(), tid, prePost, insns[m_insn], m_lock,
	   lock.dump().data());

  fflush(stdout);
}

int Work::operator ()(int tid)
{
  ZmRWLock &lock = Global::lock(m_lock);
  int result = 0;

  dump(tid, "PRE ", lock);

  switch (m_insn) {
    case Lock:
      if (m_try)
	result = lock.trylock();
      else
	lock.lock(), result = 0;
      break;
    case Unlock:
      lock.unlock();
      result = 0;
      break;
    case ReadLock:
      if (m_try)
	result = lock.readtrylock();
      else
	lock.readlock(), result = 0;
      break;
    case ReadUnlock:
      lock.readunlock();
      result = 0;
      break;
  }

  dump(tid, "POST", lock);

  return result;
}

int synchronous(int tid, Work *work)
{
  return Global::thread(tid)->synchronous(work);
}

void asynchronous(int tid, Work *work)
{
  Global::thread(tid)->asynchronous(work);
}

int result(int tid)
{
  return Global::thread(tid)->result();
}

#define Lock(l) new Work(Work::Lock, l, false)
#define TryLock(l) new Work(Work::Lock, l, true)
#define Unlock(l) new Work(Work::Unlock, l, false)
#define ReadLock(l) new Work(Work::ReadLock, l, false)
#define TryReadLock(l) new Work(Work::ReadLock, l, true)
#define ReadUnlock(l) new Work(Work::ReadUnlock, l, false)

int main(int argc, char **argv)
{
  int n = argc < 2 ? 1 : atoi(argv[1]);
  Global::start(8, 8);

  for (int i = 0; i < n; i++) {
    synchronous(0, ReadLock(0));
    synchronous(1, ReadLock(0));
    asynchronous(0, Lock(0));
    Zm::sleep(1);
    asynchronous(3, Lock(0));
    Zm::sleep(1);
    asynchronous(2, ReadLock(0));
    synchronous(1, ReadUnlock(0));
    result(0);
    synchronous(0, ReadUnlock(0));
    synchronous(0, ReadUnlock(0));
    result(3);
    synchronous(3, ReadUnlock(0));
    result(2);
    synchronous(2, ReadUnlock(0));
    synchronous(0, ReadLock(0));
    synchronous(1, ReadLock(0));
    asynchronous(0, Lock(0));
    synchronous(1, Lock(0));
    synchronous(1, ReadUnlock(0));
    synchronous(1, ReadUnlock(0));
    result(0);
    synchronous(0, ReadUnlock(0));
    synchronous(0, ReadUnlock(0));
  }

  Global::stop();
  return 0;
}
