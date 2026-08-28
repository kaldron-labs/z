//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZiGlob.hh>
#include <zlib/ZiFile.hh>

#include "ZiTestResidue.hh"

using namespace ZuTestUtil;

namespace {
Zi::Path g_root;
Zi::Path g_subdir;
Zi::Path g_alpha;
Zi::Path g_beta;

void initPaths()
{
  g_root = ZiTestResidue::dir("glob");
  g_subdir = ZiFile::append(g_root, "subdir");
  g_alpha = ZiFile::append(g_root, "alpha.txt");
  g_beta = ZiFile::append(g_root, "beta.log");

}

void cleanupTmpDir()
{
  ZiFile::remove(g_alpha);
  ZiFile::remove(g_beta);
  ZiFile::rmdir(g_subdir);
  ZiFile::rmdir(g_root);
}

bool makeFile(const Zi::Path &path, ZuCSpan contents)
{
  ZiFile f;
  if (f.open(path, ZiFile::Write | ZiFile::GC) != Zi::OK) {
    log_("open(", path, ") failed: ", f.error());
    return false;
  }
  if (f.write(contents.data(), contents.length()) != Zi::OK) {
    log_("write(", path, ") failed: ", f.error());
    return false;
  }
  f.close();
  return true;
}

void setupFixtures()
{
  cleanupTmpDir();
  if (ZiFile::mkdir(g_root) != Zi::OK) {
    log_("mkdir root failed: ", g_root);
    Zm::exit(1);
  }
  if (ZiFile::mkdir(g_subdir) != Zi::OK) {
    log_("mkdir subdir failed: ", g_subdir);
    Zm::exit(1);
  }
  if (!makeFile(g_alpha, "alpha") || !makeFile(g_beta, "beta"))
    Zm::exit(1);
}

void testGlobIterate()
{
  ZuTestScope(testGlobIterate);

  setupFixtures();

  Zi::Path prefix = ZiFile::append(g_root, "");
  ZiGlob g;
  ZuCheck(g.init(prefix));

  bool sawAlpha = false;
  bool sawBeta = false;
  bool sawSubdir = false;
  unsigned count = 0;

  while (auto entry = g.iterate(true, false)) {
    ++count;
    ZtString<> name;
    name << entry->name;
    if (name == "alpha.txt") sawAlpha = true;
    if (name == "beta.log") sawBeta = true;
    if (name == "subdir" && entry->isdir) sawSubdir = true;
  }

  ZuCheck(sawAlpha);
  ZuCheck(sawBeta);
  ZuCheck(sawSubdir);
  ZuCheck(count >= 3);
}

void testPrefixResetWrapAndReverse()
{
  ZuTestScope(testPrefixResetWrapAndReverse);

  setupFixtures();

  {
    ZiGlob g;
    Zi::Path pfx = ZiFile::append(g_root, "a");
    ZuCheck(g.init(pfx));
    auto entry = g.iterate(true, false);
    ZuCheck(entry);
    ZuCheck(entry && entry->name == "alpha.txt");
    ZuCheck(!g.iterate(true, false));
    g.reset();
    auto again = g.iterate(true, false);
    ZuCheck(again);
    ZuCheck(again && again->name == "alpha.txt");
  }

  {
    ZiGlob g;
    Zi::Path pfx = ZiFile::append(g_root, "b");
    ZuCheck(g.init(pfx));
    auto entry = g.iterate(true, false);
    ZuCheck(entry);
    ZuCheck(entry && entry->name == "beta.log");
  }

  {
    ZiGlob g;
    Zi::Path pfx = ZiFile::append(g_root, "");
    ZuCheck(g.init(pfx));
    ZuCheck(g.iterate(true, true));
    ZuCheck(g.iterate(true, true));
    ZuCheck(g.iterate(false, true));
  }
}

} // namespace

int main(int argc, char **argv)
{
  ZiTestResidue::init("ZiGlobTest");
  ZmTrap::sigintFn(&ZiTestResidue::cleanup);
  ZmTrap::trap();

  initPaths();
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testGlobIterate);
  ZuTestCall(testPrefixResetWrapAndReverse);
  return 0;
}
