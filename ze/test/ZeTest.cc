//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdio.h>

#include <zlib/ZeLog.hh>

#ifdef _WIN32
#define TestError ERROR_FILE_NOT_FOUND
#else
#define TestError ENOENT
#endif

int main(int argc, char **argv)
{
  if (argc < 1 || argc > 2 || (argc == 2 && strcmp(argv[1], "-s"))) {
    std::cerr << "Usage: ZeTest [-s]\n" << std::flush;
    Zm::exit(1);
  }

  ZeLog::init("LogTest");

  ZeLog::level(0);

  if (argc == 2)
    ZeLog::sink(ZeLog::sysSink());
  else
    ZeLog::sink(ZeLog::fileSink());

  ZeLog::start();

  ZeLOGBT(Error, "test backtrace");

  ZeLOG(Debug, "test Debug message");
  ZeLOG(Info, "test Info message");
  ZeLOG(Warning, "test Warning message");
  ZeLOG(Error, "test Error message");
  ZeLOG(Fatal, "test Fatal message");
  ZeLOG(Error, ZtSprintf<ZeString>("test %s %d", "Error message", 42));
  ZeLOG(Error, ZeError{TestError});
  ZeLOG(Error,
    ZtSprintf<ZeString>("fopen() failed: %s",
      ZeError{TestError}.message()));

  ZeLog::stop();

  return 0;
}
