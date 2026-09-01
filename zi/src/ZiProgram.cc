//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// current program

#include <zlib/ZiProgram.hh>

#ifndef _WIN32

#ifdef linux
extern "C" {
  extern char *program_invocation_short_name;
}
#endif

Zi::Name ZiProgram::name()
{
#ifdef linux
  return program_invocation_short_name;
#else
  return {};
#endif
}

#else /* !_WIN32 */

Zi::Path ZiProgram::cmdLine()
{
  return GetCommandLine();
}

Zi::Name ZiProgram::name()
{
  return name(cmdLine());
}

Zi::Name ZiProgram::name(const Zi::Path &cmdLine_)
{
  auto cmdLine = cmdLine_.cspan();
  if (!cmdLine) return {};

  if (cmdLine[0] == L'"') {
    cmdLine.offset(1);
    auto n = cmdLine.find(L'"');
    if (n < 0) n = cmdLine.length();
    return Zi::Name{cmdLine.left(n)};
  }

  auto n = cmdLine.find(L' ');
  if (n < 0) n = cmdLine.length();
  return Zi::Name{cmdLine.left(n)};
}

#endif /* !_WIN32 */
