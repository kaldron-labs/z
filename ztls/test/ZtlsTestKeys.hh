//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// First-use key fixtures shared by the cryptographic interoperability tests.
#ifndef ZtlsTestKeys_HH
#define ZtlsTestKeys_HH

#ifdef _WIN32
#include <process.h>
#else
#include <errno.h>
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
#endif

#include <zlib/ZiFile.hh>

namespace ZtlsTestKeys {

inline Zi::Path path(ZuCSpan name) { return ZiFile::append(ZtlsFixtureDir, name); }

inline bool init()
{
  if (ZiStat{ZtlsFixtureDir}.isdir()) return true;
  const char *args[] = {ZtlsFixtureGo, "-C", ZtlsFixtureSource, "run", "-mod=readonly", ".",
    ZtlsFixtureDir, ZtlsFixtureKeygen, nullptr};
#ifdef _WIN32
  return _spawnvp(_P_WAIT, args[0], args) == 0;
#else
  pid_t pid;
  if (posix_spawnp(&pid, args[0], nullptr, nullptr, const_cast<char **>(args), environ))
    return false;
  int	status;
  pid_t waited;
  do {
    waited = waitpid(pid, &status, 0);
  } while (waited < 0 && errno == EINTR);
  return waited == pid && WIFEXITED(status) && !WEXITSTATUS(status);
#endif
}

inline bool read(ZuCSpan name, ZuSpan<uint8_t> out)
{
  ZiFile file{path(name), ZiFile::ReadOnly | ZiFile::GC};
  return file && file.size() == int64_t(out.length()) &&
    file.read(out.data(), out.length()) == int64_t(out.length());
}

template <typename Array> bool load(ZuCSpan name, Array &out)
{
  ZiFile file{path(name), ZiFile::ReadOnly | ZiFile::GC};
  if (!file) return false;
  auto n = file.size();
  if (n <= 0 || n > (1 << 20)) return false;
  out.length(n);
  return file.read(out.data(), n) == n;
}

} // namespace ZtlsTestKeys

#endif
