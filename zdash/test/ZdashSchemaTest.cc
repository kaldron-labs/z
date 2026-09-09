//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZiFile.hh>

using namespace ZuTestUtil;

static void schemas()
{
  ZuTestScope(schemas);
  static const char *names[] = {"request", "reqack", "link", "version"};
  for (auto name : names) {
    ZtString<> path{ZDASH_SRCDIR};
    path << "/fbs/" << name << ".fbs";
    ZiFile file{path, ZiFile::ReadOnly | ZiFile::GC};
    ZuCHECK(file, name);
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(schemas);
  return 0;
}
