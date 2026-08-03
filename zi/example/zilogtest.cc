//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdio.h>

#include <zlib/ZiLog.hh>

#ifdef _WIN32
#define TestError ERROR_FILE_NOT_FOUND
#else
#define TestError ENOENT
#endif

int main(int argc, char **argv)
{
  if (argc < 1 || argc > 2 || (argc == 2 && strcmp(argv[1], "-s") && strcmp(argv[1], "-c"))) {
    std::cerr << "Usage: zilogtest [-s|-c]\n" << std::flush;
    Zm::exit(1);
  }

  ZiLog::init("zilogtest");

  ZiLog::level(0);

  if (argc == 2 && !strcmp(argv[1], "-s"))
    ZiLog::sink(ZiLog::sysSink());
  else if (argc == 2 && !strcmp(argv[1], "-c"))
    ZiLog::sink(ZiLog::csvSink());
  else
    ZiLog::sink(ZiLog::fileSink());

  ZiLog::start();

  ZiLOGBT(Error, "zilogtest", "test backtrace");

  ZiLOG(Debug, "zilogtest", "test Debug message");
  ZiLOG(Info, "zilogtest", "test Info message");
  ZiLOG(Warning, "zilogtest", "test Warning message");
  ZiLOG(Error, "zilogtest", "test Error message");
  ZiLOG(Fatal, "zilogtest", "test Fatal message");
  ZiLOG(Error, "zilogtest", ZtSprintf<ZeString>("test %s %d", "Error message", 42));
  ZiLOG(Error, "zilogtest", ZeError{TestError});
  ZiLOG(Error, "zilogtest",
    ZtSprintf<ZeString>("fopen() failed: %s",
      ZeError{TestError}.message()));

  ZiLog::stop();

  return 0;
}
