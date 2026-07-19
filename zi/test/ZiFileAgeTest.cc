//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmTrap.hh>
#include <zlib/ZiFile.hh>

#include "ZiTestResidue.hh"

using namespace ZuTestUtil;

namespace {
Zi::Name g_base;

Zi::Path rotated(unsigned i)
{
  Zi::Path path;
  path << g_base << '.' << ZuBox<unsigned>(i);
  return path;
}

void initPaths()
{
  g_base = {};
  g_base << ZiTestResidue::tempRoot() << "/age";
  ZiTestResidue::addFile(g_base);
  for (unsigned i = 1; i <= 8; i++)
    ZiTestResidue::addFile(rotated(i));
}

void cleanupAge()
{
  ZiFile::remove(g_base);
  for (unsigned i = 0; i <= 8; i++)
    ZiFile::remove(rotated(i));
}

void writeBase(const char *s)
{
  ZiFile f;
  if (f.open(g_base, ZiFile::Write | ZiFile::GC) != Zi::OK) {
    log_("open(age) failed: ", f.error());
    Zm::exit(1);
  }
  if (f.write(s, static_cast<unsigned>(::strlen(s))) != Zi::OK) {
    log_("write(age) failed: ", f.error());
    Zm::exit(1);
  }
  f.close();
}

ZtString<> readPath(const Zi::Path &path)
{
  ZiFile f;
  if (f.open(path, ZiFile::ReadOnly, 0777) != Zi::OK) {
    log_("open(", path, ") failed: ", f.error());
    Zm::exit(1);
  }
  char buf[128];
  int n = f.read(buf, sizeof(buf));
  if (n < 0) {
    log_("read(", path, ") failed: ", f.error());
    Zm::exit(1);
  }
  ZtString<> out;
  out << ZuCSpan(buf, static_cast<unsigned>(n));
  f.close();
  return out;
}

void testAgeRotationBounded()
{
  ZuTestScope(testAgeRotationBounded);

  cleanupAge();

  writeBase("A");
  ZiFile::age(g_base, 3);

  writeBase("B");
  ZiFile::age(g_base, 3);

  writeBase("C");
  ZiFile::age(g_base, 3);

  writeBase("D");
  ZiFile::age(g_base, 3);

  ZuCheck(!ZiStat{g_base}.exists());
  ZuCheck(ZiStat{rotated(1)}.exists());
  ZuCheck(ZiStat{rotated(2)}.exists());
  ZuCheck(ZiStat{rotated(3)}.exists());
  ZuCheck(!ZiStat{rotated(4)}.exists());

  ZuCheck(readPath(rotated(1)) == "D");
  ZuCheck(readPath(rotated(2)) == "C");
  ZuCheck(readPath(rotated(3)) == "B");

  cleanupAge();
}

} // namespace

int main(int argc, char **argv)
{
  ZiTestResidue::init("ZiFileAgeTest");
  ZmTrap::sigintFn(&ZiTestResidue::cleanupNow);
  ZmTrap::trap();
  ::atexit(&ZiTestResidue::cleanupNow);

  initPaths();
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testAgeRotationBounded);
  return 0;
}
