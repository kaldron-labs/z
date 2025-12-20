//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// dynamic loading of shared objects / DLLs

#include <zlib/ZiModule.hh>

#ifdef _WIN32

int ZiModule::load(const Path &name, unsigned flags, ZeString *e)
{
  Guard guard(m_lock);

  if (m_flags & GC) unload();
  m_flags = flags;
  if (m_handle = LoadLibrary(name)) return Zi::OK;
  if (e) *e = Error::last();
  return Zi::IOError;
}

int ZiModule::unload(ZeString *e)
{
  Guard guard(m_lock);

  if (!m_handle) return Zi::OK;
  if (FreeLibrary(m_handle)) {
    m_handle = 0;
    return Zi::OK;
  }
  m_handle = 0;
  if (e) *e = Error::last();
  return Zi::IOError;
}

void *ZiModule::resolve(const char *symbol, ZeString *e)
{
  Guard guard(m_lock);
  void *ptr;

  ptr = (void *)GetProcAddress(m_handle, symbol);
  if (!ptr && e) *e = Error::last();
  return ptr;
}

#else

#include <dlfcn.h>

int ZiModule::load(const Path &name, unsigned flags, ZeString *e)
{
  Guard guard(m_lock);

  if (m_flags & GC) unload();
  m_flags = flags;
  int flags_ = RTLD_LAZY | RTLD_GLOBAL;
  if (flags & Pre) flags_ |= RTLD_DEEPBIND;
  if (m_handle = dlopen(name, flags_)) return Zi::OK;
  if (e) *e = Error::last();
  return Zi::IOError;
}

int ZiModule::unload(ZeString *e)
{
  Guard guard(m_lock);

  if (!m_handle) return Zi::OK;
  if (!dlclose(m_handle)) {
    m_handle = 0;
    return Zi::OK;
  }
  m_handle = 0;
  if (e) *e = Error::last();
  return Zi::IOError;
}

void *ZiModule::resolve(const char *symbol, ZeString *e)
{
  Guard guard(m_lock);
  void *ptr;

  if ((ptr = dlsym(m_handle, symbol)) && ptr != (void *)(uintptr_t)-1)
    return ptr;
  if (e) *e = Error::last();
  return 0;
}

#endif
