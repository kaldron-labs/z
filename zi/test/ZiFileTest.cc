//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <cstdint>
#include <stdlib.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmTrap.hh>
#include <zlib/ZiFile.hh>

#include "ZiTestResidue.hh"

using namespace ZuTestUtil;

namespace {
Zi::Path g_root;
Zi::Path g_foo;
Zi::Path g_bar;
Zi::Path g_baz;
Zi::Path g_copy;
Zi::Path g_renamed;
Zi::Path g_dir;
Zi::Path g_child;
Zi::Path g_link;

void initPaths()
{
  g_root = ZiTestResidue::tempRoot();

  g_foo = ZiFile::append(g_root, "foo");
  g_bar = ZiFile::append(g_root, "bar");
  g_baz = ZiFile::append(g_root, "baz");
  g_copy = ZiFile::append(g_root, "copy");
  g_renamed = ZiFile::append(g_root, "renamed");
  g_dir = ZiFile::append(g_root, "dir");
  g_child = ZiFile::append(g_dir, "child.txt");
  g_link = ZiFile::append(g_dir, "link.txt");

  ZiTestResidue::addFile(g_foo);
  ZiTestResidue::addFile(g_bar);
  ZiTestResidue::addFile(g_baz);
  ZiTestResidue::addFile(g_copy);
  ZiTestResidue::addFile(g_renamed);
  ZiTestResidue::addFile(g_child);
  ZiTestResidue::addDir(g_dir);
}

void cleanupFiles()
{
  ZiFile::remove(g_link);
  ZiFile::remove(g_child);
  ZiFile::remove(g_foo);
  ZiFile::remove(g_bar);
  ZiFile::remove(g_baz);
  ZiFile::remove(g_copy);
  ZiFile::remove(g_renamed);
  ZiFile::rmdir(g_dir);
}

Zi::Path path_(const char *s)
{
#ifndef _WIN32
  return s;
#else
  Zi::Path out;
  while (*s) out << wchar_t(*s++);
  return out;
#endif
}

bool makeSymlink(const Zi::Path &target, const Zi::Path &link)
{
#ifndef _WIN32
  return ::symlink(target, link) == 0;
#else
#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif
  return CreateSymbolicLinkW(
    link, target, SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0 ||
    CreateSymbolicLinkW(link, target, 0) != 0;
#endif
}

void testWriteReadAndBlockSize()
{
  ZuTestScope(testWriteReadAndBlockSize);

  cleanupFiles();

  ZtString<> hw = "Hello World\n";
  {
    ZiFile f;
    ZuCHECK(f.open(g_foo, ZiFile::Write, 0666) == Zi::OK,
      "open(foo) failed: ", f.error());

    ZuCHECK(f.write(hw.data(), hw.length()) == Zi::OK,
      "write(foo) failed: ", f.error());
    ZuCheck(f.offset() == static_cast<ZiFile::Offset>(hw.length()));

    ZiFile g;
    g.init(f.handle(), 0);
    ZuCheck(f.blkSize() > 0);
    ZuCheck(g.blkSize() == f.blkSize());

    f.close();
  }

  {
    ZiFile f;
    char buf[1024];
    ZuCHECK(f.open(g_foo, ZiFile::ReadOnly, 0777) == Zi::OK,
      "open(foo) for read failed: ", f.error());

    int i = f.read(buf, sizeof(buf) - 1);
    ZuCHECK(i >= 0, "read(foo) failed: ", f.error());
    ZuCheck(f.offset() == i);
    buf[i] = 0;

    ZtString<> got;
    got << ZuCSpan(buf, i);
    ZuCheck(got == hw);

    f.close();
  }
}

void testSparseReadDefaultsToZero()
{
  ZuTestScope(testSparseReadDefaultsToZero);

  ZiFile::remove(g_bar);

  ZiFile f;
  ZuCHECK(f.open(g_bar, ZiFile::Create | ZiFile::Truncate, 0666) == Zi::OK,
    "open(bar) failed: ", f.error());

  uint32_t u = 1;
  ZuCHECK(f.pwrite(4, &u, sizeof(u)) == Zi::OK,
    "pwrite(bar) failed: ", f.error());
  ZuCheck(f.offset() == 0);

  u = 0xffffffffU;
  int n = f.pread(0, &u, sizeof(u));
  ZuCHECK(n >= static_cast<int>(sizeof(u)),
    "pread(bar) failed: ", f.error());
  ZuCheck(f.offset() == 0);
  ZuCheck(u == 0);

  f.close();
}

void testVectoredIO()
{
  ZuTestScope(testVectoredIO);

  ZiFile::remove(g_baz);

  ZiFile f;
  ZuCHECK(f.open(g_baz, ZiFile::Create | ZiFile::Truncate, 0666) == Zi::OK,
    "open(baz) failed: ", f.error());

  const char *a = "alpha";
  const char *b = "beta";
  ZiVec wv[2];
  ZiVec_init(wv[0], const_cast<char *>(a), 5);
  ZiVec_init(wv[1], const_cast<char *>(b), 4);
  ZuCHECK(f.writev(wv, 2) == Zi::OK, "writev failed: ", f.error());
  ZuCheck(f.offset() == 9);

  const char *x = "ZZ";
  const char *y = "YY";
  ZiVec pwv[2];
  ZiVec_init(pwv[0], const_cast<char *>(x), 2);
  ZiVec_init(pwv[1], const_cast<char *>(y), 2);
  ZuCHECK(f.pwritev(2, pwv, 2) == Zi::OK, "pwritev failed: ", f.error());
  ZuCheck(f.offset() == 9);

  char out1[8] = {0};
  char out2[8] = {0};
  ZiVec rv[2];
  ZiVec_init(rv[0], out1, 4);
  ZiVec_init(rv[1], out2, 5);
  ZuCHECK(f.preadv(0, rv, 2) == Zi::OK, "preadv failed: ", f.error());
  ZuCheck(f.offset() == 9);

  ZtString<> got;
  got << ZuCSpan(out1, 4) << ZuCSpan(out2, 5);
  ZuCheck(got == "alZZYYeta");

  memset(out1, 0, sizeof(out1));
  memset(out2, 0, sizeof(out2));
  ZuCHECK(f.seek(0) == Zi::OK, "seek(baz) failed: ", f.error());
  ZuCHECK(f.readv(rv, 2) == Zi::OK, "readv failed: ", f.error());
  ZuCheck(f.offset() == 9);
  got = {};
  got << ZuCSpan(out1, 4) << ZuCSpan(out2, 5);
  ZuCheck(got == "alZZYYeta");

  f.close();
}

void testSeekSizeTruncateSync()
{
  ZuTestScope(testSeekSizeTruncateSync);

  ZiFile::remove(g_foo);

  ZiFile f;
  ZuCHECK(f.open(g_foo, ZiFile::Write, 0666) == Zi::OK,
    "open failed: ", f.error());
  ZuCHECK(f.write("0123456789", 10) == Zi::OK, "write failed: ", f.error());
  ZuCheck(f.offset() == 10);

  ZuCheck(f.size() >= 10);
  ZuCheck(f.offset() == 10);

  ZuCHECK(f.seek(4) == Zi::OK, "seek failed: ", f.error());
  ZuCheck(f.offset() == 4);

  ZuCHECK(f.truncate(6) == Zi::OK, "truncate failed: ", f.error());
  ZuCheck(f.size() == 6);

  ZuCHECK(f.sync() == Zi::OK, "sync failed: ", f.error());
  f.close();

  ZiFile r;
  ZuCHECK(r.open(g_foo, ZiFile::ReadOnly, 0777) == Zi::OK,
    "open read failed: ", r.error());
  char buf[32];
  int n = r.read(buf, sizeof(buf));
  ZuCHECK(n == 6, "read expected 6 got ", n);
  r.close();
}

void testMMapSpans()
{
  ZuTestScope(testMMapSpans);

  cleanupFiles();

  ZuCSpan data{"mapped"};
  ZiMMapFile file;
  ZuCHECK(file.mmap(
      g_foo, ZiFile::Create | ZiFile::Truncate | ZiFile::GC,
      data.length()) == Zi::OK, "mmap failed: ", file.error());

  auto span = file.span();
  ZuCheck(span.data() == file.addr());
  ZuCheck(span.length() == data.length());
  memcpy(span.data(), data.data(), data.length());

  const ZiMMapFile &cfile = file;
  ZuBSpan cspan = cfile.cspan();
  ZuCheck(cspan.data() == file.addr());
  ZuCheck(cspan.length() == data.length());
  ZuCheck(!memcmp(cspan.data(), data.data(), data.length()));

  file.close();
}

void testMetadataAndPathHelpers()
{
  ZuTestScope(testMetadataAndPathHelpers);

  cleanupFiles();

  Zi::Path cwd = ZiFile::cwd();
  ZuCheck(!!cwd);
  ZuCheck(ZiStat{cwd}.isdir());

  ZuCHECK(ZiFile::mkdir(g_dir) == Zi::OK, "mkdir failed");
  ZiStat dirStat{g_dir};
  ZuCheck(dirStat.exists());
  ZuCheck(dirStat.isdir());

  {
    ZiFile f;
    ZuCHECK(f.open(g_foo, ZiFile::Write, 0666) == Zi::OK, "open failed: ", f.error());
    ZuCHECK(f.write("content", 7) == Zi::OK, "write failed: ", f.error());
    f.close();
  }

  ZiStat stat{g_foo};
  ZuCheck(stat.exists());
  ZuCheck(!stat.isdir());
  ZuCheck(stat.size() == 7);
  ZuCheck(!!stat.mtime());

  ZuCHECK(ZiFile::rename(g_foo, g_renamed) == Zi::OK, "rename failed");
  ZuCheck(stat.exists());
  ZuCheck(stat.size() == 7);
  ZuCheck(!ZiStat{g_foo}.exists());
  ZuCheck(ZiStat{g_renamed}.exists());

  ZiStat copyStat{g_copy};
  ZuCHECK(ZiFile::copy(g_renamed, g_copy) == Zi::OK, "copy failed");
  ZuCheck(copyStat.exists());

  ZiFile f;
  ZuCHECK(f.open(g_copy, ZiFile::ReadOnly, 0777) == Zi::OK, "open copy failed: ", f.error());
  char buf[16] = {0};
  int n = f.read(buf, sizeof(buf));
  ZuCHECK(n == 7, "copy read length mismatch");
  ZtString<> got;
  got << ZuCSpan(buf, 7);
  ZuCheck(got == "content");
  f.close();

  Zi::Path joined = ZiFile::append(g_dir, "joined.txt");
  ZuCheck(ZiFile::leafname(joined) == "joined.txt");
  ZuCheck(ZiFile::dirname(joined) == g_dir);
  ZuCheck(!ZiFile::absolute(joined));

  Zi::Path absPath = ZiFile::append(cwd, "ZiFileTest.abs");
  ZuCheck(ZiFile::absolute(absPath));

  ZuCHECK(ZiFile::rmdir(g_dir) == Zi::OK, "rmdir failed");
  ZiStat missing{g_dir};
  ZuCheck(!missing.isdir());
  ZuCheck(!!missing.error());
}

void testNegativeOpen()
{
  ZuTestScope(testNegativeOpen);

  ZiFile f;
  ZuCheck(f.open(ZiFile::append(g_root, "does-not-exist"), ZiFile::ReadOnly, 0777) == Zi::IOError);
}

void testOpenAtNoFollowAndStat()
{
  ZuTestScope(testOpenAtNoFollowAndStat);

  cleanupFiles();
  ZuCHECK(ZiFile::mkdir(g_dir) == Zi::OK, "mkdir(dir) failed");

  {
    ZiFile f;
    ZuCHECK(f.open(g_child, ZiFile::Write, 0666) == Zi::OK,
      "open child failed: ", f.error());
    ZuCHECK(f.write("child", 5) == Zi::OK, "write child failed: ", f.error());
  }

  ZiFile dir;
  ZuCHECK(dir.open(g_dir,
      ZiFile::ReadOnly | ZiFile::Directory | ZiFile::GC) == Zi::OK,
    "open directory failed: ", dir.error());

  ZiFile::Stat dirStat;
  ZuCHECK(dir.fstat(dirStat) == Zi::OK, "directory fstat failed: ", dir.error());
  ZuCheck(dirStat.directory);
  ZuCheck(!dirStat.regular);

  ZiFile file;
  ZuCHECK(file.openAt(dir, path_("child.txt"),
      ZiFile::ReadOnly | ZiFile::NoFollow | ZiFile::GC) == Zi::OK,
    "openAt child failed: ", file.error());

  ZiFile::Stat fileStat;
  ZuCHECK(file.fstat(fileStat) == Zi::OK, "file fstat failed: ", file.error());
  ZuCheck(fileStat.regular);
  ZuCheck(!fileStat.directory);
  ZuCheck(fileStat.size == 5);
  ZuCheck(!!fileStat.mtime);

  char buf[8] = {0};
  int n = file.read(buf, 5);
  ZuCHECK(n == 5, "file read length mismatch: ", n);
  ZuCheck(ZuCSpan(buf, 5) == "child");

  ZiFile dup;
  ZuCHECK(dup.dup(file, ZiFile::GC) == Zi::OK, "dup failed: ", dup.error());
  file.close();
  n = dup.pread(0, buf, 5);
  ZuCHECK(n == 5, "dup read length mismatch: ", n);
  ZuCheck(ZuCSpan(buf, 5) == "child");

  ZiFile bad;
  ZuCheck(bad.openAt(dir, path_("nested/name"), ZiFile::ReadOnly) == Zi::IOError);
  ZuCheck(bad.openAt(dir, g_child, ZiFile::ReadOnly) == Zi::IOError);

  if (makeSymlink(g_child, g_link)) {
    ZiFile link;
    ZuCheck(link.openAt(dir, path_("link.txt"),
	ZiFile::ReadOnly | ZiFile::NoFollow | ZiFile::GC) == Zi::IOError);
    ZiFile::remove(g_link);
  }
}

} // namespace

int main(int argc, char **argv)
{
  ZiTestResidue::init("ZiFileTest");
  ZmTrap::sigintFn(&ZiTestResidue::cleanupNow);
  ZmTrap::trap();
  ::atexit(&ZiTestResidue::cleanupNow);

  initPaths();
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testWriteReadAndBlockSize);
  ZuTestCall(testSparseReadDefaultsToZero);
  ZuTestCall(testVectoredIO);
  ZuTestCall(testSeekSizeTruncateSync);
  ZuTestCall(testMMapSpans);
  ZuTestCall(testMetadataAndPathHelpers);
  ZuTestCall(testNegativeOpen);
  ZuTestCall(testOpenAtNoFollowAndStat);
  cleanupFiles();
  return 0;
}
