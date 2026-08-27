//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// dynamic loading of shared objects / DLLs

#ifndef ZiModule_HH
#define ZiModule_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZmLock.hh>
#include <zlib/ZmTopology.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiPlatform.hh>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4251)
#endif

class ZiAPI ZiModule {
public:
#ifdef _WIN32
  using Handle = HMODULE;
#else
  using Handle = void *;
#endif

  using Path = Zi::Path;

  using Lock = ZmLock;
  using Guard = ZmGuard<Lock>;

  enum Flags {
    GC	= 0x001,	// unload() handle in destructor
    Pre	= 0x002		// equivalent to LD_PRELOAD / RTLD_DEEPBIND
  };

  struct Error {
#ifdef _WIN32
    static void clear() { }
    static ZeString last() {
      return Ze::strerror(GetLastError());
    }
#else
    static void clear() { dlerror(); }
    static ZeString last() { return dlerror(); }
#endif
  };

  ZiModule() : m_handle(0), m_flags(0) { ZmTopology::hwloc(); }
  ~ZiModule() { finalize(); }

  ZiModule(const ZiModule &) = delete;
  ZiModule &operator =(const ZiModule &) = delete;

  Handle handle() { return m_handle; }

  unsigned flags() { return m_flags; }
  void setFlags(unsigned f) { Guard guard(m_lock); m_flags |= f; }
  void clrFlags(unsigned f) { Guard guard(m_lock); m_flags &= ~f; }

  int init(Handle handle, unsigned flags, ZeString *e = 0) {
    Guard guard(m_lock);

    if (m_flags & GC) unload();
    m_handle = handle;
    m_flags = flags;
    return Zi::OK;
  }

  void finalize() {
    Guard guard(m_lock);

    if (m_flags & GC)
      unload();
    else
      m_handle = 0;
  }

  int load(const Path &name, unsigned flags, ZeString *e = 0);
  int unload(ZeString *e = 0);

  void *resolve(const char *symbol, ZeString *e = 0);

private:
  ZmLock	m_lock;
  Handle	m_handle;
  unsigned	m_flags;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif /* ZiModule_HH */
