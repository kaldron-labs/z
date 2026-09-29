//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Private seams for the separately compiled heap test executable.

#ifndef ZmHeapTest_HH
#define ZmHeapTest_HH

#include <stdint.h>

class ZmHeapCache;
class ZmHeapLookup;
struct ZmHeapGlobalStats;

class ZmHeapTest {
public:
  enum {
    Prepared, Registered, Published, Owned, Rebuild, Find,
    Associated, Removed, Created, Freeing
  };
  using Hook = void (*)(unsigned, const ZmHeapCache *, const void *);

  static void hook(unsigned, const ZmHeapCache *, const void * = nullptr);
  static bool fail(const ZmHeapCache *);
  static void run(const char *);
  static int duplicate(bool);

private:
  static void publication();
  static void bounds();
  template <bool Sharded> static void activation();
  static void ranges();
  static void rebuild();
  static void receivers();
  static void groups();
  static void transition(bool);
  static void cleanup();
  static void zeroFirst();
  static void zero(ZmHeapCache *, uint16_t, uint8_t);
  static void duplicates(const char *);
  static ZmHeapCache *arena(void *, unsigned, ZmHeapGlobalStats &);
  static void dispose(ZmHeapCache *);
  static bool covers(ZmHeapLookup &, ZmHeapCache *);
  static unsigned entries(ZmHeapLookup &, ZmHeapCache *);

  static Hook m_hook;
  static const ZmHeapCache *m_fail;
  static unsigned m_managers;
};

#endif /* ZmHeapTest_HH */
