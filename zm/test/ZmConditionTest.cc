//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <thread>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmPLock.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmCondition.hh>

using namespace ZuTestUtil;

void testSignalNoMissedWakeup()
{
  ZuTestScope(testSignalNoMissedWakeup);

  ZmPLock lock;
  ZmCondition<ZmPLock> cond(lock);

  bool ready = false;
  bool woke = false;
  ZmAtomic<int> waiting = 0;

  ZmThread th{[&lock, &waiting, &ready, &cond, &woke] {
    ZmGuard<ZmPLock> guard(lock);
    waiting = 1;
    while (!ready) cond.wait();
    woke = true;
  }};

  while (!waiting) std::this_thread::yield();

  {
    ZmGuard<ZmPLock> guard(lock);
    ready = true;
    cond.signal();
  }

  if (th) th.join();
  ZuCheck(woke);
}

void testTimedWaitAndBroadcast()
{
  ZuTestScope(testTimedWaitAndBroadcast);

  ZmPLock lock;
  ZmCondition<ZmPLock> cond(lock);

  int timed = 0;
  ZmThread timeoutTh{[&lock, &timed, &cond] {
    ZmGuard<ZmPLock> guard(lock);
    timed = cond.timedWait(Zm::now() + ZuTime(0.03));
  }};
  if (timeoutTh) timeoutTh.join();
  ZuCheck(timed == -1);

  bool go = false;
  int wakeCount = 0;
  auto waiter = [&lock, &go, &cond, &wakeCount] {
    ZmGuard<ZmPLock> guard(lock);
    while (!go) cond.wait();
    ++wakeCount;
  };

  ZmThread a{waiter};
  ZmThread b{waiter};

  Zm::sleep(ZuTime(0.005));
  {
    ZmGuard<ZmPLock> guard(lock);
    go = true;
    cond.broadcast();
  }

  if (a) a.join();
  if (b) b.join();
  ZuCheck(wakeCount == 2);
}

void testTimeoutSignalRace()
{
  ZuTestScope(testTimeoutSignalRace);

  ZmPLock lock;
  ZmCondition<ZmPLock> cond(lock);

  for (unsigned i = 0; i < 32; i++) {
    int rc = 99;
    bool done = false;

    ZmThread th{[&lock, &rc, &cond, &done] {
      ZmGuard<ZmPLock> guard(lock);
      rc = cond.timedWait(Zm::now() + ZuTime(0.01));
      done = true;
    }};

    Zm::sleep(ZuTime(0.005));
    {
      ZmGuard<ZmPLock> guard(lock);
      cond.signal();
    }

    if (th) th.join();
    ZuCheck(done);
    ZuCheck(rc == 0 || rc == -1);
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testSignalNoMissedWakeup);
  ZuTestCall(testTimedWaitAndBroadcast);
  ZuTestCall(testTimeoutSignalRace);
  return 0;
}
