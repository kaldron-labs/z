//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// PID file creation and ownership

#ifndef ZiPIDFile_HH
#define ZiPIDFile_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZePlatform.hh>

#include <zlib/ZiPlatform.hh>

class ZiAPI ZiPIDFile {
  ZiPIDFile(const ZiPIDFile &) = delete;
  ZiPIDFile &operator =(const ZiPIDFile &) = delete;

public:
  enum { OK = 0, Error = -1, Running = -2 };

  ZiPIDFile() = default;
  ~ZiPIDFile();

  int init(const Zi::Path &name);
  int init(const Zi::Path &dir, const Zi::Path &name);

  const Zi::Path &path() const { return m_path; }
  ZeError error() const { return m_error; }
  int pid() const { return m_pid; }

private:
  void final();

  Zi::Path	m_path;
  ZeError	m_error = ZeOK;
  int		m_pid = 0;
  bool		m_owned = false;
};

#endif /* ZiPIDFile_HH */
