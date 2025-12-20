//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// ring buffer intra-process mirrored memory region

#ifndef ZmRingMirror_HH
#define ZmRingMirror_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZmPlatform.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmBitmap.hh>
#include <zlib/ZmTopology.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZmBackTrace.hh>

class ZmAPI ZmRingMirror {
public:
  bool open(unsigned size);
  void close();

  ZuInline void *addr() const { return m_addr; }
  ZuInline unsigned size() const { return m_size; }

private:
#ifndef _WIN32
  using Handle = int;
  static constexpr Handle nullHandle() { return -1; }
#else
  using Handle = HANDLE;
  static constexpr Handle nullHandle() { return INVALID_HANDLE_VALUE; }
#endif

  Handle		m_handle = nullHandle();
  void			*m_addr = nullptr;
  unsigned		m_size = 0;
};

#endif /* ZmRingMirror_HH */
