//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// directory scanning

#ifndef ZiDir_HH
#define ZiDir_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZmLock.hh>
#include <zlib/ZmGuard.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiPlatform.hh>

#ifndef _WIN32
#include <sys/types.h>
#include <dirent.h>
#endif

class ZiAPI ZiDir {
public:
  using Path = Zi::Path;

  using Guard = ZmGuard<ZmLock>;

  ZiDir() :
#ifdef _WIN32
      m_handle(INVALID_HANDLE_VALUE)
#else
      m_dir(0)
#endif
  { }

  ~ZiDir() { close(); }

  ZiDir(const ZiDir &) = delete;
  ZiDir &operator =(const ZiDir &) = delete;

  bool operator !() const {
    return
#ifdef _WIN32
      m_handle == INVALID_HANDLE_VALUE;
#else
      !m_dir;
#endif
  }
  ZuOpBool

  int open(const Path &name, ZeError *e = nullptr);
  int read(Path &name, ZeError *e = nullptr);
  void close();

private:
  ZmLock	m_lock;
#ifdef _WIN32
    Path	  m_match;
    HANDLE	  m_handle;
#else
    DIR		  *m_dir;
#endif
};

#endif /* ZiDir_HH */
