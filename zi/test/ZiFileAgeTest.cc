//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmTrap.hh>
#include <zlib/ZiFile.hh>

#include "ZiTestResidue.hh"

using namespace ZuTestUtil;

namespace {
enum { TestAge = 3 }; // exercise bounded rotation with a short history

Zi::Path g_base;
Zi::Path g_root;

Zi::Path rotated(unsigned i)
{
  Zi::Path path;
  path << g_base << '.' << ZuBox<unsigned>(i);
  return path;
}

void initPaths()
{
  g_root = ZiTestResidue::dir("age");
  g_base = ZiFile::append(g_root, "file");
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

bool writePath(const Zi::Path &path, const char *s)
{
  ZiFile f;
  if (f.open(path, ZiFile::Write | ZiFile::GC) != Zi::OK) return false;
  return f.write(s, static_cast<unsigned>(::strlen(s))) == Zi::OK;
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
  ZiFile::age(g_base, TestAge);

  writeBase("B");
  ZiFile::age(g_base, TestAge);

  writeBase("C");
  ZiFile::age(g_base, TestAge);

  writeBase("D");
  ZiFile::age(g_base, TestAge);

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

void testTreeAgeAndRemoval()
{
  ZuTestScope(testTreeAgeAndRemoval);

  Zi::Path tree = ZiFile::append(g_root, "tree");
  Zi::Path child = ZiFile::append(tree, "child");
  Zi::Path archived;
  archived << tree << ".1";

  ZuCheck(ZiFile::mkdir(tree) == Zi::OK);
  ZuCheck(writePath(child, "old"));
  ZiFile::ageTree(tree, TestAge);
  ZuCheck(!ZiStat{tree}.exists());
  ZuCheck(ZiStat{ZiFile::append(archived, "child")}.exists());

  ZuCheck(ZiFile::mkdir(tree) == Zi::OK);
  ZuCheck(writePath(child, "new"));
  ZiFile::ageTree(tree, TestAge);
  ZuCheck(readPath(ZiFile::append(archived, "child")) == "new");
  ZuCheck(ZiFile::removeTree(archived) == Zi::OK);
  ZuCheck(!ZiStat{archived}.exists());
  Zi::Path archived2;
  archived2 << tree << ".2";
  ZuCheck(ZiFile::removeTree(archived2) == Zi::OK);
}

void testTreeDoesNotFollowSymlink()
{
  ZuTestScope(testTreeDoesNotFollowSymlink);
#ifndef _WIN32
  Zi::Path tree = ZiFile::append(g_root, "links");
  Zi::Path target = ZiFile::append(g_root, "target");
  Zi::Path link = ZiFile::append(tree, "link");
  ZuCheck(ZiFile::mkdir(tree) == Zi::OK);
  ZuCheck(writePath(target, "target"));
  ZuCheck(!::symlink(target, link));
  ZuCheck(ZiFile::removeTree(tree) == Zi::OK);
  ZuCheck(ZiStat{target}.exists());
  ZuCheck(ZiFile::remove(target) == Zi::OK);
#else
  ZuCheck(true);
#endif
}

void testTreeRemovalError()
{
  ZuTestScope(testTreeRemovalError);
#ifndef _WIN32
  Zi::Path tree = ZiFile::append(g_root, "unreadable");
  Zi::Path child = ZiFile::append(tree, "child");
  ZuCheck(ZiFile::mkdir(tree) == Zi::OK);
  ZuCheck(writePath(child, "child"));
  ZuCheck(!::chmod(tree, 0));
  ZeError error;
  ZuCheck(ZiFile::removeTree(tree, &error) == Zi::IOError);
  ZuCheck(!!error);
  ZuCheck(!::chmod(tree, 0700));
  ZuCheck(ZiFile::removeTree(tree) == Zi::OK);
#else
  ZuCheck(true);
#endif
}

} // namespace

int main(int argc, char **argv)
{
  ZiTestResidue::init("ZiFileAgeTest");
  ZmTrap::sigintFn(&ZiTestResidue::cleanup);
  ZmTrap::trap();

  initPaths();
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testAgeRotationBounded);
  ZuTestCall(testTreeAgeAndRemoval);
  ZuTestCall(testTreeDoesNotFollowSymlink);
  ZuTestCall(testTreeRemovalError);
  return 0;
}
