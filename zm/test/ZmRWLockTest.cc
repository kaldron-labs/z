//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmRWLock.hh>
#include <zlib/ZmLockTraits.hh>
#include <zlib/ZmThread.hh>

using namespace ZuTestUtil;

static_assert(ZmLockTraits<ZmRWLock>::RWLock == 1);

void testWriteExcludesOthers()
{
  ZuTestScope(testWriteExcludesOthers);

  ZmRWLock lock;

  lock.lock();

  int readTry = 0;
  int writeTry = 0;
  ZmThread th{[&lock, &readTry, &writeTry] {
    readTry = lock.readtrylock();
    if (readTry == 0) lock.readunlock();

    writeTry = lock.trylock();
    if (writeTry == 0) lock.unlock();
  }};

  if (th) th.join();
  ZuCheck(readTry == -1);
  ZuCheck(writeTry == -1);

  lock.unlock();
}

void testRecursiveWriteAndTryLock()
{
  ZuTestScope(testRecursiveWriteAndTryLock);

  ZmRWLock lock;

  lock.lock();
  ZuCheck(lock.trylock() == 0); // recursive write path
  lock.unlock();
  lock.unlock();

  ZuCheck(lock.trylock() == 0);
  lock.unlock();

  ZuCheck(lock.readtrylock() == 0);
  lock.readunlock();
}

void testReadExcludesWriter()
{
  ZuTestScope(testReadExcludesWriter);

  ZmRWLock lock;
  lock.readlock();

  int writeTry = 0;
  ZmThread th{[&lock, &writeTry] {
    writeTry = lock.trylock();
    if (writeTry == 0) lock.unlock();
  }};

  if (th) th.join();
  ZuCheck(writeTry == -1);

  lock.readunlock();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testWriteExcludesOthers);
  ZuTestCall(testRecursiveWriteAndTryLock);
  ZuTestCall(testReadExcludesWriter);
  return 0;
}
