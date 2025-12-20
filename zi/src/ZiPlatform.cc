//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZiPlatform.hh>

#include <zlib/ZmAlloc.hh>

#ifndef _WIN32

#include <pwd.h>

Zi::Username Zi::username(ZeError *e)
{
  struct passwd pwd;
  struct passwd *result = 0;
  ssize_t bufSize = 0;
  Zi::Username name;

  bufSize = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (bufSize < 0) bufSize = (1<<14);
  auto pwdBuf = ZmAlloc(char, bufSize);
  if (!pwdBuf) return name;

  int s = getpwuid_r(geteuid(), &pwd, pwdBuf.ptr, bufSize, &result);
  if (!result && s != 0) {
    if (e) *e = ZeError(s);
  } else if (result) {
    name = pwd.pw_name;
  }
  return name;
}

Zi::Hostname Zi::hostname(ZeError *e)
{
  Zi::Hostname name;
  name.size(HOST_NAME_MAX + 1);
  int s = gethostname(name.data(), HOST_NAME_MAX);
  if (s < 0) { name.null(); return name; }
  name.calcLength();
  name.truncate();
  return name;
}

#else

Zi::Username Zi::username(ZeError *e)
{
  Zi::Username name;
  name.size(Zi::NameMax + 1);
  DWORD len = Zi::NameMax;
  if (!GetUserName(name.data(), &len))
    name.null();
  else {
    name.calcLength();
    name.truncate();
  }
  return name;
}

Zi::Hostname Zi::hostname(ZeError *e)
{
  Zi::Hostname name;
#ifdef _WIN32
  ZuCArray<Zi::NameMax + 1> buf;
#else
  Zi::Hostname &buf = name;
#endif
  name.size(Zi::NameMax + 1);
  if (gethostname(buf.data(), Zi::NameMax))
    buf.null();
  else {
    buf.calcLength();
#ifndef _WIN32
    buf.truncate();
#endif
  }
#ifdef _WIN32
  name = buf;
#endif
  return name;
}

#endif /* _WIN32 */
