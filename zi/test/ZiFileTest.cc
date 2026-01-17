//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZiLog.hh>
#include <zlib/ZiFile.hh>

int main()
{
  ZiLog::init("ZiFileTest");
  ZiLog::sink(ZiLog::fileSink());
  ZiLog::start();

  try {
    {
      ZiFile f;
      if (f.open("foo", ZiFile::Create | ZiFile::Truncate, 0666) != Zi::OK)
	throw f.error();
      ZtString<> hw = "Hello World\n";

      if (f.write(hw.data(), hw.length()) != Zi::OK) throw f.error();

      ZiFile g;

      g.init(f.handle(), 0);

      printf("%d %d\n", f.blkSize(), g.blkSize());
    }

    {
      ZiFile f;
      char buf[1024];
      int i;

      if (f.open("foo", ZiFile::ReadOnly, 0777) != Zi::OK) throw f.error();
      i = f.read(buf, 1024);
      if (i < 0) throw f.error();
      printf("%d\n", i);
      buf[i] = 0;
      fputs(buf, stdout);
    }

    {
      ZiFile f;
      if (f.open("bar", ZiFile::Create | ZiFile::Truncate, 0666) != Zi::OK)
	throw f.error();
      uint32_t u = 1;

      if (f.pwrite(4, &u, 4) != Zi::OK) throw f.error();
      if (f.pread(0, &u, 4) < 4) throw f.error();
      printf("uninitialized data: %.8x\n", (int)u); fflush(stdout);
    }
  } catch (const ZeError &e) {
    ZiLOG(Fatal, "ZiFileTest", e);
    Zm::exit(1);
  }

  ZiLog::stop();
  return 0;
}
