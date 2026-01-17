//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// calculate available stack memory

#ifndef ZmStackAvail_HH
#define ZmStackAvail_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZmThread.hh>

// assumes a downwards-growing stack (i.e. the stack address is the limit)
// - this is true for all modern architectures, including x86 and ARM
inline unsigned ZmStackAvail()
{
  uint8_t *sp;
#if defined(__GNUC__) && defined(__x86_64__)
  __asm__("movq %%rsp, %0" : "=q" (sp));
#else
  sp = reinterpret_cast<uint8_t *>(&sp);
#endif
  auto addr = static_cast<uint8_t *>(ZmSelf()->stackAddr());
  auto avail = sp - addr;
  if (ZuUnlikely(avail < 0)) return 0;
  if (avail >= static_cast<ptrdiff_t>(UINT_MAX)) return UINT_MAX;
  return avail;
}

#endif /* ZmStackAvail_HH */
