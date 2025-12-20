//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// intrusively reference-counted object debugging

#ifndef ZmObjectDebug_HH
#define ZmObjectDebug_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#ifdef ZmObject_DEBUG

#include <stddef.h>
#include <stdlib.h>

#include <zlib/ZmAtomic.hh>

#include <zlib/ZmBackTrace_.hh>

class ZmObjectDebug;

extern "C" {
  ZmExtern void ZmObject_ref(const ZmObjectDebug *, const void *);
  ZmExtern void ZmObject_deref(const ZmObjectDebug *, const void *);
}

class ZmAPI ZmObjectDebug {
friend ZmAPI void ZmObject_ref(const ZmObjectDebug *, const void *);
friend ZmAPI void ZmObject_deref(const ZmObjectDebug *, const void *);

public:
  ZmObjectDebug() : m_debug(0) { }
  ~ZmObjectDebug() { ::free(m_debug); }

  void debug() const;

  // context, referrer, backtrace
  typedef void (*DumpFn)(void *, const void *, const ZmBackTrace *);
  void dump(void *context, DumpFn fn) const;

protected:
  bool debugging_() const { return m_debug.load_(); }

  mutable ZmAtomic<void *>	m_debug;
};

#endif /* ZmObject_DEBUG */

#endif /* ZmObjectDebug_HH */
