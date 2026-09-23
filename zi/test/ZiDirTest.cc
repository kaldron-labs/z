//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZiDir.hh>
#include <zlib/ZiFile.hh>

#include "ZiTestResidue.hh"

using namespace ZuTestUtil;

namespace ZiDirTest_ {
Zi::Path g_root;
Zi::Path g_file;

void initPaths()
{
  g_root = ZiTestResidue::dir("dir");
  g_file = ZiFile::append(g_root, "entry.txt");
}

void cleanupDir()
{
  ZiFile::remove(g_file);
  ZiFile::rmdir(g_root);
}

void setupDir()
{
  cleanupDir();
  if (ZiFile::mkdir(g_root) != Zi::OK) {
    log_("mkdir failed: ", g_root);
    Zm::exit(1);
  }

  ZiFile f;
  if (f.open(g_file, ZiFile::Write | ZiFile::GC) != Zi::OK) {
    log_("open failed: ", f.error());
    Zm::exit(1);
  }
  if (f.write("x", 1) != Zi::OK) {
    log_("write failed: ", f.error());
    Zm::exit(1);
  }
  f.close();
}

void testOpenReadAndEOF()
{
  ZuTestScope(testOpenReadAndEOF);

  setupDir();

  ZiDir dir;
  ZuCheck(dir.open(g_root) == Zi::OK);

  bool sawFile = false;
  bool sawDot = false;
  bool sawDotDot = false;

  Zi::Path name;
  for (;;) {
    int rc = dir.read(name);
    if (rc == Zi::EndOfFile) break;
    ZuCheck(rc == Zi::OK);
    if (rc != Zi::OK) break;

    if (name == "entry.txt") sawFile = true;
    if (name == ".") sawDot = true;
    if (name == "..") sawDotDot = true;
  }

  ZuCheck(sawFile);
  ZuCheck(sawDot);
  ZuCheck(sawDotDot);

  // Stable EOF after iteration completes.
  ZuCheck(dir.read(name) == Zi::EndOfFile);
  dir.close();
}

void testReadBeforeOpenFails()
{
  ZuTestScope(testReadBeforeOpenFails);

  ZiDir dir;
  Zi::Path name;
  ZuCheck(dir.read(name) == Zi::IOError);
}

void testOpenNondirectoryFails()
{
  ZuTestScope(testOpenNondirectoryFails);

  setupDir();

  ZiDir dir;
  ZuCheck(dir.open(g_file) == Zi::IOError);
  dir.close();
}

} // ZiDirTest_

using namespace ZiDirTest_;

int main(int argc, char **argv)
{
  ZiTestResidue::init("ZiDirTest");
  ZmTrap::sigintFn(&ZiTestResidue::cleanup);
  ZmTrap::trap();

  initPaths();
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testOpenReadAndEOF);
  ZuTestCall(testReadBeforeOpenFails);
  ZuTestCall(testOpenNondirectoryFails);
  return 0;
}
