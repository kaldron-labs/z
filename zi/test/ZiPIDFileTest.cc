//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <zlib/ZuArray.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmPlatform.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiPIDFile.hh>

#include "ZiTestResidue.hh"

using namespace ZuTestUtil;

static Zi::Path g_root;
using PIDText = ZuCArray<sizeof(int) * 3 + 2>;

static bool writeText(const Zi::Path &path, ZuCSpan text)
{
  ZiFile file;
  return file.open(path, ZiFile::Write | ZiFile::GC, 0644) == Zi::OK &&
    file.write(text.data(), text.length()) == Zi::OK;
}

static bool writePID(const Zi::Path &path, int pid)
{
  PIDText text;
  text << ZuBox<int>{pid};
  return writeText(path, text);
}

static ZtString<> readText(const Zi::Path &path)
{
  ZiFile file;
  if (file.open(path, ZiFile::ReadOnly | ZiFile::NoFollow | ZiFile::GC) !=
	Zi::OK) return {};
  PIDText buf;
  int n = file.read(buf.data(), buf.size());
  if (n < 0) return {};
  return ZuCSpan{buf.data(), unsigned(n)};
}

static int readPID(const Zi::Path &path)
{
  auto text = readText(path);
  ZuBox<int> pid;
  return pid.scan(text) == int(text.length()) ? int(pid) : 0;
}

static void testFreshNestedAndFinal()
{
  ZuTestScope(testFreshNestedAndFinal);

  auto dir1 = ZiFile::append(g_root, "one");
  auto dir2 = ZiFile::append(dir1, "two");
  auto path = ZiFile::append(dir2, "fresh.pid");
  ZiFile::removeTree(dir1);
  {
    ZiPIDFile file;
    ZuCHECK(file.init(g_root, "one/two/fresh.pid") == ZiPIDFile::OK,
      "init failed: ", file.error());
    ZuCheck(file.path() == path);
    ZuCheck(readPID(path) == int(Zm::getPID()));
    auto text = readText(path);
    ZuCheck(text == ZuBox<int>{int(Zm::getPID())});
#ifndef _WIN32
    struct stat stat;
    ZuCheck(!::stat(dir1, &stat) && (stat.st_mode & 0777) == 0755);
    ZuCheck(!::stat(dir2, &stat) && (stat.st_mode & 0777) == 0755);
    ZuCheck(!::stat(path, &stat) && (stat.st_mode & 0777) == 0644);
#endif
  }
  ZuCheck(!ZiStat{path}.exists());
  ZuCheck(ZiStat{dir1}.isdir());
  ZuCheck(ZiStat{dir2}.isdir());
}

static void testStaleAndMalformed()
{
  ZuTestScope(testStaleAndMalformed);

  auto path = ZiFile::append(g_root, "stale.pid");
  ZuCheck(writePID(path, 99999999));
  {
    ZiPIDFile file;
    ZuCHECK(file.init(g_root, "stale.pid") == ZiPIDFile::OK,
      "stale init failed: ", file.error());
    auto text = readText(path);
    ZuCheck(text == ZuBox<int>{int(Zm::getPID())});
    ZiPIDFile contender;
    ZuCheck(contender.init(g_root, "stale.pid") == ZiPIDFile::Running);
  }
  ZuCheck(!ZiStat{path}.exists());

  ZuCheck(writeText(path, "not-a-pid"));
  {
    ZiPIDFile file;
    ZuCHECK(file.init(g_root, "stale.pid") == ZiPIDFile::OK,
      "malformed init failed: ", file.error());
    ZuCheck(readPID(path) == int(Zm::getPID()));
  }
  ZuCheck(!ZiStat{path}.exists());

  ZuCheck(writeText(path, {}));
  {
    ZiPIDFile file;
    ZuCHECK(file.init(g_root, "stale.pid") == ZiPIDFile::OK,
      "empty init failed: ", file.error());
    ZuCheck(readPID(path) == int(Zm::getPID()));
  }
  ZuCheck(!ZiStat{path}.exists());
}

static void testRunning()
{
  ZuTestScope(testRunning);

  auto path = ZiFile::append(g_root, "running.pid");
  ZuCheck(writePID(path, int(Zm::getPID())));
  {
    ZiPIDFile file;
    ZuCheck(file.init(g_root, "running.pid") == ZiPIDFile::Running);
    ZuCheck(file.pid() == int(Zm::getPID()));
  }
  ZuCheck(ZiStat{path}.exists());
  ZiFile::remove(path);
}

#ifndef _WIN32
static void testPublicationRetry()
{
  ZuTestScope(testPublicationRetry);

  auto path = ZiFile::append(g_root, "publishing.pid");
  ZiFile::remove(path);
  ZiFile publisher;
  ZuCHECK(publisher.open(path,
	ZiFile::WriteOnly | ZiFile::Create | ZiFile::Exclusive |
	ZiFile::NoFollow | ZiFile::Unpublished | ZiFile::GC, 0) == Zi::OK,
    "publisher open failed: ", publisher.error());
  PIDText text;
  text << ZuBox<int>{int(Zm::getPID())};
  ZuCHECK(publisher.write(text.data(), text.length()) == Zi::OK,
    "publisher write failed: ", publisher.error());

  ZuArray<int, 2> ready;
  ZuCHECK(!::pipe(ready.data()), "pipe failed");
  pid_t child = ::fork();
  if (child < 0) {
    ::close(ready[0]);
    ::close(ready[1]);
    return;
  }
  if (!child) {
    ::close(ready[0]);
    publisher.close();
    char byte = 0;
    if (::write(ready[1], &byte, 1) != 1) _exit(2);
    ::close(ready[1]);
    ZiPIDFile contender;
    int result = contender.init(g_root, "publishing.pid");
    _exit(result == ZiPIDFile::Running &&
	contender.pid() == int(getppid()) ? 0 : 3);
  }
  ZuCheck(child > 0);

  ::close(ready[1]);
  char byte;
  ZuCHECK(::read(ready[0], &byte, 1) == 1, "child readiness failed");
  ::close(ready[0]);

  Zm::sleep(3);
  int status = 0;
  ZuCheck(::waitpid(child, &status, WNOHANG) == 0);
  ZuCHECK(publisher.mode(0644) == Zi::OK,
    "publisher mode failed: ", publisher.error());
  ZuCHECK(::waitpid(child, &status, 0) == child, "waitpid failed");
  ZuCheck(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  ZiFile::remove(path);
}

static void testCrashRecovery()
{
  ZuTestScope(testCrashRecovery);

  auto path = ZiFile::append(g_root, "crashed.pid");
  ZiFile::remove(path);
  pid_t child = ::fork();
  if (child < 0) return;
  if (!child) {
    ZiPIDFile file;
    _exit(file.init(g_root, "crashed.pid") == ZiPIDFile::OK ? 0 : 1);
  }
  ZuCheck(child > 0);
  int status = 0;
  ZuCHECK(::waitpid(child, &status, 0) == child, "waitpid failed");
  ZuCheck(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  ZuCheck(readPID(path) == int(child));

  {
    ZiPIDFile file;
    ZuCHECK(file.init(g_root, "crashed.pid") == ZiPIDFile::OK,
      "crash recovery failed: ", file.error());
    ZuCheck(readPID(path) == int(Zm::getPID()));
  }
  ZuCheck(!ZiStat{path}.exists());
}
#endif

static void testPaths()
{
  ZuTestScope(testPaths);

  ZiPIDFile file;
  ZuCheck(file.init(g_root, "../bad.pid") == ZiPIDFile::Error);
  ZuCheck(file.init(g_root, "bad//name.pid") == ZiPIDFile::Error);
  auto baseFile = ZiFile::append(g_root, "not-a-directory");
  ZuCheck(writeText(baseFile, "x"));
  ZuCheck(file.init(baseFile, "bad.pid") == ZiPIDFile::Error);
#ifndef _WIN32
  ZuCheck(file.init(g_root, "/bad.pid") == ZiPIDFile::Error);
  auto target = ZiFile::append(g_root, "target");
  auto link = ZiFile::append(g_root, "link");
  ZiFile::remove(link);
  ZuCheck(ZiFile::mkdir(target) == Zi::OK || ZiStat{target}.isdir());
  if (!::symlink(target, link)) {
    ZuCheck(file.init(g_root, "link/bad.pid") == ZiPIDFile::Error);
    ZiFile::remove(link);
  }
#endif
}

static void testDefaultDir()
{
  ZuTestScope(testDefaultDir);

  ZtString<> name;
  name << "ZiPIDFileTest-" << ZuBox<int>{int(Zm::getPID())} << ".pid";
  auto path = ZiFile::append(ZiFile::tmpDir(), name);
  ZiFile::remove(path);
  {
    ZiPIDFile file;
    ZuCHECK(file.init(name) == ZiPIDFile::OK,
      "default init failed: ", file.error());
    ZuCheck(file.path() == path);
  }
  ZuCheck(!ZiStat{path}.exists());
}

int main(int argc, char **argv)
{
  ZiTestResidue::init("ZiPIDFileTest");
  g_root = ZiTestResidue::dir("pidfiles");
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testFreshNestedAndFinal);
  ZuTestCall(testStaleAndMalformed);
  ZuTestCall(testRunning);
#ifndef _WIN32
  ZuTestCall(testPublicationRetry);
  ZuTestCall(testCrashRecovery);
#endif
  ZuTestCall(testPaths);
  ZuTestCall(testDefaultDir);
  return 0;
}
