//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmPlatform.hh>

#include <zlib/ZtCLI.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiDaemon.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>

#include "ZiTestResidue.hh"

using namespace ZuTestUtil;

struct Options {
  bool		quiet;
  bool		child;
  bool		daemonize;
  ZuCSpan	pidFile;
  ZuCSpan	marker;
  bool		help;
};

ZtStruct(Options,
  (((quiet),		(Ctor<0>, CLI::Flag<'q'>)),		(Bool)),
  (((child),		(Ctor<1>, CLI::Flag<'c'>)),		(Bool)),
  (((daemonize),	(Ctor<2>, CLI::Flag<'d'>)),		(Bool)),
  (((pidFile),		(Ctor<3>, CLI::Opt<'p'>)),		(String)),
  (((marker),		(Ctor<4>, CLI::ID<"marker">,
			  CLI::Opt<'m'>)),			(String)),
  (((help),		(Ctor<5>, CLI::Flag<'h'>)),		(Bool)));

namespace {

Zi::Path path(const char *name)
{
  return ZiFile::append(ZiTestResidue::tempRoot(), name);
}

bool writeFile(const Zi::Path &path, ZuCSpan text)
{
  ZiFile f;
  if (f.open(path, ZiFile::Write | ZiFile::GC, 0666) != Zi::OK) {
    log_("open(", path, ") failed: ", f.error());
    return false;
  }
  if (f.write(text.data(), text.length()) != Zi::OK) {
    log_("write(", path, ") failed: ", f.error());
    return false;
  }
  return true;
}

bool writePID(const Zi::Path &path, int pid)
{
  ZtString<> text;
  text << ZuBox<int>(pid);
  return writeFile(path, text);
}

int readPID(const Zi::Path &path)
{
  ZiFile f;
  if (f.open(path, ZiFile::ReadOnly | ZiFile::GC, 0) != Zi::OK) {
    log_("open(", path, ") failed: ", f.error());
    return -1;
  }
  char buf[32];
  int n = f.read(buf, sizeof(buf) - 1);
  if (n < 0) {
    log_("read(", path, ") failed: ", f.error());
    return -1;
  }
  buf[n] = 0;
  return ZuBox<int>{ZuCSpan{buf, static_cast<unsigned>(n)}};
}

ZtString<> readText(const Zi::Path &path)
{
  ZiFile f;
  if (f.open(path, ZiFile::ReadOnly | ZiFile::GC, 0) != Zi::OK) {
    log_("open(", path, ") failed: ", f.error());
    return {};
  }
  char buf[64];
  int n = f.read(buf, sizeof(buf) - 1);
  if (n < 0) {
    log_("read(", path, ") failed: ", f.error());
    return {};
  }
  buf[n] = 0;
  ZtString<> out;
  out << ZuCSpan{buf, static_cast<unsigned>(n)};
  return out;
}

bool waitForFile(const Zi::Path &path)
{
  for (unsigned i = 0; i < 50; i++) {
    if (ZiFile::exists(path)) return true;
#ifndef _WIN32
    ::usleep(100000);
#endif
  }
  return ZiFile::exists(path);
}

template <typename Argv>
int loadOptions(Options &options, const Argv &argv)
{
  ZtCLI::Parser<Options, ZuFacet::Core> parser;
  if (!parser.scanArgv(argv)) return -1;
  options = ZtCLI::handler<Options, ZuFacet::Core>(parser.root).ctor();
  return parser.argc;
}

int loadOptions(Options &options, int argc, const char *const *argv)
{
  ZtCLI::InArgv<> in(argc, argv);
  return loadOptions(options, in.argv);
}

int childMain(const Options &options)
{
  ZiLog::init("ZiDaemonTest.child");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::debugSink());

  int r = ZiDaemon::init(
    nullptr, nullptr, -1, options.daemonize,
    options.pidFile ? options.pidFile.data() : nullptr);

  if (options.marker) {
    ZtString<> text;
    text << ZuBox<int>(r) << ' ' << ZuBox<int>(Zm::getPID());
    if (!writeFile(options.marker, text)) return 127;
  }

  return r == ZiDaemon::OK ? 0 : 10 - r;
}

#ifndef _WIN32
int runSelf(const char *self, ZuCSpan arg1, ZuCSpan arg2 = {},
    ZuCSpan arg3 = {}, ZuCSpan arg4 = {}, ZuCSpan arg5 = {},
    ZuCSpan arg6 = {}, ZuCSpan arg7 = {})
{
  const char *argv[10] = {
    self, arg1.data(), arg2.data(), arg3.data(), arg4.data(),
    arg5.data(), arg6.data(), arg7.data(), nullptr
  };
  unsigned argc = 1;
  while (argc < 8 && argv[argc] && argv[argc][0]) argc++;
  argv[argc] = nullptr;

  pid_t pid = ::fork();
  if (pid < 0) {
    log_("fork() failed");
    return 128;
  }
  if (!pid) {
    ::execv(self, const_cast<char *const *>(argv));
    _exit(127);
  }

  int status = 0;
  if (::waitpid(pid, &status, 0) != pid) {
    log_("waitpid() failed");
    return 128;
  }
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return 128;
}
#endif

void testNoPIDFile()
{
  ZuTestScope(testNoPIDFile);

  ZuCheck(ZiDaemon::init(nullptr, nullptr, -1, false, nullptr) == ZiDaemon::OK);
}

void testPIDFileCreateAndRewrite()
{
  ZuTestScope(testPIDFileCreateAndRewrite);

  auto pidFile = path("daemon.pid");
  ZiTestResidue::addFile(pidFile);
  ZiFile::remove(pidFile);

  ZuCheck(ZiDaemon::init(nullptr, nullptr, -1, false, pidFile) == ZiDaemon::OK);
  ZuCheck(readPID(pidFile) == static_cast<int>(Zm::getPID()));

  ZuCheck(writePID(pidFile, 99999999));
  ZuCheck(ZiDaemon::init(nullptr, nullptr, -1, false, pidFile) == ZiDaemon::OK);
  ZtString<> pidText;
  pidText << ZuBox<int>(Zm::getPID());
  ZuCheck(!readText(pidFile).cmp(pidText, pidText.length()));
}

void testRunningPID()
{
  ZuTestScope(testRunningPID);

  auto pidFile = path("running.pid");
  ZiTestResidue::addFile(pidFile);
  ZuCheck(writePID(pidFile, static_cast<int>(Zm::getPID())));

  ZuCheck(
    ZiDaemon::init(nullptr, nullptr, -1, false, pidFile) == ZiDaemon::Running);
}

void testBadUserDoesNotBypassPIDChecks()
{
  ZuTestScope(testBadUserDoesNotBypassPIDChecks);

  auto pidFile = path("bad-user.pid");
  ZiTestResidue::addFile(pidFile);
  ZiFile::remove(pidFile);

  ZuCheck(
    ZiDaemon::init("ZiDaemonTest.NoSuchUser", nullptr, -1, false, pidFile) ==
      ZiDaemon::OK);
  ZuCheck(readPID(pidFile) == static_cast<int>(Zm::getPID()));
}

void testPIDFileOpenError()
{
  ZuTestScope(testPIDFileOpenError);

  auto dir = path("pid-dir");
  ZiTestResidue::addDir(dir);
  ZiFile::rmdir(dir);
  ZuCheck(ZiFile::mkdir(dir) == Zi::OK);

  ZuCheck(
    ZiDaemon::init(nullptr, nullptr, -1, false, dir) == ZiDaemon::Error);
}

void testCLIParsing()
{
  ZuTestScope(testCLIParsing);

  char argv0[] = "ZiDaemonTest";
  char argv1[] = "-qcd";
  char argv2[] = "-p";
  char argv3[] = "x.pid";
  char argv4[] = "-m";
  char argv5[] = "x.marker";
  const char *argv[] = { argv0, argv1, argv2, argv3, argv4, argv5 };
  Options options{};
  int argc = loadOptions(options, 6, argv);
  ZuCheck(argc == 1);
  ZuCheck(options.quiet);
  ZuCheck(options.child);
  ZuCheck(options.daemonize);
  ZuCheck(options.pidFile == "x.pid");
  ZuCheck(options.marker == "x.marker");
}

void testChildAndDaemonize(const char *self)
{
  ZuTestScope(testChildAndDaemonize);

#ifndef _WIN32
  auto pidFile = path("child.pid");
  auto marker = path("child.marker");
  auto daemonPIDFile = path("daemonized.pid");
  auto daemonMarker = path("daemonized.marker");
  ZiTestResidue::addFile(pidFile);
  ZiTestResidue::addFile(marker);
  ZiTestResidue::addFile(daemonPIDFile);
  ZiTestResidue::addFile(daemonMarker);
  ZiFile::remove(pidFile);
  ZiFile::remove(marker);
  ZiFile::remove(daemonPIDFile);
  ZiFile::remove(daemonMarker);

  ZuCheck(runSelf(self, "-c", "-p", pidFile, "-m", marker) == 0);
  ZuCheck(waitForFile(marker));
  ZuCheck(readPID(pidFile) > 0);

  ZuCheck(
    runSelf(self, "-cd", "-p", daemonPIDFile, "-m", daemonMarker) == 0);
  ZuCheck(waitForFile(daemonMarker));
  int daemonPID = readPID(daemonPIDFile);
  ZuCheck(daemonPID > 0);
  ZuCheck(daemonPID != static_cast<int>(Zm::getPID()));
#else
  static_cast<void>(self);
  ZuCheck(true);
#endif
}

void usage(const char *self)
{
  std::cerr <<
    "Usage: " << self << " [-q] [-c] [-d] [-p pid-file] [-m marker]\n";
  ::exit(1);
}

} // namespace

int main(int argc, char **argv)
{
  Options options{};
  int argc_ = loadOptions(options, argc, argv);
  if (argc_ < 1 || options.help) usage(argv[0]);
  verbose = options.quiet ? false : !::getenv("HARNESS_ACTIVE");

  if (options.child) return childMain(options);

  ZiTestResidue::init("ZiDaemonTest");

  ZuTestMain();
  ZuTestCall(testNoPIDFile);
  ZuTestCall(testPIDFileCreateAndRewrite);
  ZuTestCall(testRunningPID);
  ZuTestCall(testBadUserDoesNotBypassPIDChecks);
  ZuTestCall(testPIDFileOpenError);
  ZuTestCall(testCLIParsing);
  ZuTestCall(testChildAndDaemonize, argv[0]);
  ZiTestResidue::cleanupNow();
  return 0;
}
