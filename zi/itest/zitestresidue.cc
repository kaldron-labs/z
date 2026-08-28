//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZiTestResidue process-lifecycle integration test

#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <zlib/ZuBox.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmTrap.hh>

#include <zlib/ZtEnum.hh>

#include <zlib/ZfCLI.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>

#include "ZiTestResidue.hh"

using namespace ZuTestUtil;

ZtEnumNS(, Mode, int8_t, Parent, PassFile, FailFile, Family, FailFamily,
    FailLog, Interrupt, Shm, SecondName, InvalidPath, Duplicate, Overlap,
    RepeatFinal, Unique);
ZtEnumImplNS(Mode);

struct Options {
  Mode::T	mode = Mode::Parent;
};

ZfStruct((Options, CLI),
  (((mode), (Ctor<0>, Enum<Mode::Map>, CLI::Long<"mode">)),
    (Int8, Mode::Parent)));

static int load_(Options &options, int argc, const char *const *argv)
{
  ZfCLI::InArgv<> in(argc, argv);
  ZfCLI::Parser<Options> parser;
  parser.scanArgv(in.argv);
  options = ZfCLI::handler<Options>(parser.root).ctor();
  return parser.argc;
}

static bool write_(const Zi::Path &path, ZuCSpan data)
{
  ZiFile file;
  if (file.open(path, ZiFile::Write | ZiFile::GC, 0666) != Zi::OK)
    return false;
  return file.write(data.data(), data.length()) == Zi::OK;
}

static bool readable_(const Zi::Path &path)
{
  ZiFile file;
  if (file.open(path, ZiFile::ReadOnly | ZiFile::GC) != Zi::OK) return false;
  char c;
  return file.read(&c, 1) == 1;
}

#ifndef _WIN32
static Zi::Path shmPath_(const Zi::Name &base, const char *suffix)
{
  Zi::Path path;
  path << '/' << base << suffix;
  return path;
}

static bool makeShm_(const Zi::Name &base)
{
  Zi::Path ctrl = shmPath_(base, ".ctrl");
  Zi::Path data = shmPath_(base, ".data");
  int ctrlFD = ::shm_open(ctrl, O_CREAT | O_EXCL | O_RDWR, 0600);
  if (ctrlFD < 0) return false;
  ::close(ctrlFD);
  int dataFD = ::shm_open(data, O_CREAT | O_EXCL | O_RDWR, 0600);
  if (dataFD >= 0) ::close(dataFD);
  if (dataFD < 0) ::shm_unlink(ctrl);
  return dataFD >= 0;
}

static bool noShm_(const Zi::Name &base)
{
  for (const char *suffix : { ".ctrl", ".data" }) {
    Zi::Path path = shmPath_(base, suffix);
    int fd = ::shm_open(path, O_RDWR, 0600);
    if (fd >= 0) {
      ::close(fd);
      return false;
    }
    if (errno != ENOENT) return false;
  }
  return true;
}
#endif

static Zi::Path root_()
{
  const char *env = ::getenv("ZI_LOGDIR");
  Zi::Path root = env && *env ? Zi::Path{env} : ZiFile::cwd();
  if (!ZiFile::absolute(root)) root = ZiFile::append(ZiFile::cwd(), root);
  return ZiFile::canonical(root);
}

static Zi::Path artifact_(const char *test, const char *name)
{
  Zi::Path leaf;
  leaf << test << '.' << name;
  return ZiFile::append(root_(), leaf);
}

static Zi::Path g_capture;

static Zi::Path capture_(const char *mode)
{
  Zi::Name name;
  name << mode << ".tap";
  return ZiFile::append(g_capture, name);
}

#ifndef _WIN32
static int run_(const char *self, const char *mode)
{
  ZiFile output;
  if (output.open(capture_(mode), ZiFile::Write | ZiFile::GC, 0666) != Zi::OK)
    return 255;
  pid_t pid = ::fork();
  if (pid < 0) return 255;
  if (!pid) {
    ::dup2(output.handle(), STDOUT_FILENO);
    ::dup2(output.handle(), STDERR_FILENO);
    const char *argv[] = { self, "--mode", mode, nullptr };
    ::execv(self, const_cast<char *const *>(argv));
    _exit(127);
  }

  for (unsigned i = 0; i < 100; ++i) {
    int status = 0;
    pid_t result = ::waitpid(pid, &status, WNOHANG);
    if (result == pid) {
      if (WIFEXITED(status)) return WEXITSTATUS(status);
      return 128;
    }
    if (result < 0) return 255;
    ::usleep(10000);
  }
  ::kill(pid, SIGTERM);
  ::waitpid(pid, nullptr, 0);
  return 254;
}
#else
static int run_(const char *self, const char *mode)
{
  ZiFile output;
  if (output.open(capture_(mode), ZiFile::Write | ZiFile::GC, 0666) != Zi::OK)
    return 255;
  SetHandleInformation(output.handle(), HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
  ZtString<> command;
  ZfCLI::CmdQuote::quote(command, self);
  command << " --mode ";
  ZfCLI::CmdQuote::quote(command, mode);
  STARTUPINFOA start{};
  start.cb = sizeof(start);
  start.dwFlags = STARTF_USESTDHANDLES;
  start.hStdOutput = output.handle();
  start.hStdError = output.handle();
  start.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION process{};
  if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE, 0,
      nullptr, nullptr, &start, &process)) return 255;
  DWORD wait = WaitForSingleObject(process.hProcess, 1000);
  DWORD status = 255;
  if (wait == WAIT_TIMEOUT) {
    TerminateProcess(process.hProcess, 254);
    WaitForSingleObject(process.hProcess, INFINITE);
    status = 254;
  } else if (wait == WAIT_OBJECT_0) {
    GetExitCodeProcess(process.hProcess, &status);
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return int(status);
}
#endif

static int child_(Mode::T mode)
{
  static ZuTest_Scope ZuTest_scope = { nullptr, 0, true };
  ZuTest_Context ZuTest_context(&ZuTest_scope);
  ZuTestMgr::start();
  switch (mode) {
    case Mode::PassFile: {
      ZiTestResidue::init("zitestresidue-pass");
      ZuCheckRT(write_(ZiTestResidue::file("output"), "pass"));
      break;
    }
    case Mode::FailFile: {
      ZiTestResidue::init("zitestresidue-fail");
      ZuCheckRT(write_(ZiTestResidue::file("output"), "failure"));
      ZuCheckRT(false);
      break;
    }
    case Mode::Family:
    case Mode::FailFamily: {
      const char *name = mode == Mode::Family ?
        "zitestresidue-family" : "zitestresidue-fail-family";
      ZiTestResidue::init(name);
      Zi::Path dir = ZiTestResidue::dir("tree");
      Zi::Path first = ZiFile::append(dir, "one.tmp");
      Zi::Path second = ZiFile::append(dir, "two.keep");
      Zi::Path nested = ZiFile::append(dir, "nested");
      ZuCheckRT(ZiFile::mkdir(nested) == Zi::OK);
      ZuCheckRT(write_(first, "one"));
      ZuCheckRT(write_(second, "two"));
      ZuCheckRT(write_(ZiFile::append(nested, "three.tmp"), "three"));
      ZuCheckRT(ZiTestResidue::glob(dir, "one").length() == 1);
      ZiTestResidue::del(dir, "one");
      ZuCheckRT(!ZiStat{first}.exists());
      if (mode == Mode::FailFamily) ZuCheckRT(false);
      break;
    }
    case Mode::FailLog: {
      ZiTestResidue::init("zitestresidue-fail-log");
      Zi::Path log = ZiTestResidue::path("output.log");
      ZiTestResidue::add(log);
      ZiLog::init("zitestresidue");
      ZiLog::level(0);
      ZiLog::sink(ZiLog::fileSink(
          ZiSinkOptions{}.path(log).age(ZiTestResidue::Age)));
      ZiLog::start();
      ZiLOG(Info, "ZiTestResidue", "retained failure log");
      ZiLog::stop();
      ZuCheckRT(false);
      break;
    }
    case Mode::Interrupt: {
      ZiTestResidue::init("zitestresidue-interrupt");
      ZuCheckRT(write_(ZiTestResidue::file("output"), "interrupted"));
      ZmTrap::sigintFn(&ZiTestResidue::cleanup);
#ifndef _WIN32
      ZmTrap::trap();
      ::raise(SIGINT);
#else
      ZmTrap::sigintFn()();
#endif
      return 1;
    }
    case Mode::Shm: {
      ZiTestResidue::init("zitestresidue-shm");
      Zi::Name name = ZiTestResidue::uniqueName("shm");
#ifndef _WIN32
      ZuCheckRT(makeShm_(name));
#endif
      ZiTestResidue::addShm(name);
      ZiTestResidue::final(true);
#ifndef _WIN32
      return noShm_(name) ? 0 : 1;
#else
      return 0;
#endif
    }
    case Mode::SecondName:
      ZiTestResidue::init("zitestresidue-name");
      ZiTestResidue::init("zitestresidue-other");
      return 0;
    case Mode::InvalidPath:
      ZiTestResidue::init("zitestresidue-invalid");
      ZiTestResidue::path("../escape");
      return 0;
    case Mode::Duplicate: {
      ZiTestResidue::init("zitestresidue-duplicate");
      Zi::Path path = ZiTestResidue::path("output");
      ZiTestResidue::add(path);
      ZiTestResidue::add(path);
      return 0;
    }
    case Mode::Overlap: {
      ZiTestResidue::init("zitestresidue-overlap");
      Zi::Path path = ZiTestResidue::dir("same");
      ZiTestResidue::add(path);
      return 0;
    }
    case Mode::RepeatFinal:
      ZiTestResidue::init("zitestresidue-repeat");
      if (!write_(ZiTestResidue::file("output"), "repeat")) return 1;
      ZiTestResidue::final(true);
      ZiTestResidue::final(false);
      return 0;
    case Mode::Unique: {
      ZiTestResidue::init("zitestresidue-unique");
      Zi::Name first = ZiTestResidue::uniqueName();
      Zi::Name second = ZiTestResidue::uniqueName("tag");
      bool different = first != second && second.find("tag") >= 0;
      ZiTestResidue::final(different);
      return different ? 0 : 1;
    }
    default:
      return 2;
  }
  return 0;
}

static void removeFamily_(const Zi::Path &path)
{
  ZiFile::removeTree(path);
  for (unsigned i = 1; i <= ZiTestResidue::Age; ++i) {
    Zi::Path archived;
    archived << path << '.' << ZuBox<unsigned>{i};
    ZiFile::removeTree(archived);
  }
}

static void removeFile_(const Zi::Path &path)
{
  ZiFile::remove(path);
  for (unsigned i = 1; i <= ZiTestResidue::Age; ++i) {
    Zi::Path archived;
    archived << path << '.' << ZuBox<unsigned>{i};
    ZiFile::remove(archived);
  }
}

static int parent_(const char *self)
{
  ZiTestResidue::init("zitestresidue");
  g_capture = ZiTestResidue::dir("captures");
  ZuTestMain();

  Zi::Path pass = artifact_("zitestresidue-pass", "output");
  Zi::Path fail = artifact_("zitestresidue-fail", "output");
  Zi::Path failLog = artifact_("zitestresidue-fail-log", "output.log");
  Zi::Path interrupt = artifact_("zitestresidue-interrupt", "output");
  Zi::Path family = artifact_("zitestresidue-family", "tree");
  Zi::Path failFamily = artifact_("zitestresidue-fail-family", "tree");
  removeFile_(pass);
  removeFile_(fail);
  removeFile_(failLog);
  removeFile_(interrupt);
  removeFamily_(family);
  removeFamily_(failFamily);

  ZuCheck(run_(self, "PassFile") == 0);
  ZuCheck(ZiStat{capture_("PassFile")}.exists());
  ZuCheck(!ZiStat{pass}.exists());
  ZuCheck(write_(fail, "old-current"));
  bool fileSeeded = true;
  for (unsigned i = 1; i <= ZiTestResidue::Age; ++i) {
    Zi::Path archived;
    archived << fail << '.' << ZuBox<unsigned>{i};
    fileSeeded &= write_(archived, "old-archive");
  }
  ZuCheck(fileSeeded);
  ZuCheck(run_(self, "FailFile") != 0);
  ZuCheck(ZiStat{fail}.exists());
  bool filesAged = true;
  for (unsigned i = 1; i <= ZiTestResidue::Age; ++i) {
    Zi::Path archived;
    archived << fail << '.' << ZuBox<unsigned>{i};
    filesAged &= ZiStat{archived}.exists();
  }
  ZuCheck(filesAged);
  Zi::Path excess;
  excess << fail << '.' << ZuBox<unsigned>{ZiTestResidue::Age + 1};
  ZuCheck(!ZiStat{excess}.exists());
  ZuCheck(write_(failLog, "old-current"));
  bool logsSeeded = true;
  for (unsigned i = 1; i <= ZiTestResidue::Age; ++i) {
    Zi::Path archived;
    archived << failLog << '.' << ZuBox<unsigned>{i};
    logsSeeded &= write_(archived, "old-archive");
  }
  ZuCheck(logsSeeded);
  ZuCheck(run_(self, "FailLog") != 0);
  ZuCheck(readable_(failLog));
  bool logsAged = true;
  for (unsigned i = 1; i <= ZiTestResidue::Age; ++i) {
    Zi::Path archived;
    archived << failLog << '.' << ZuBox<unsigned>{i};
    logsAged &= readable_(archived);
  }
  ZuCheck(logsAged);
  ZuCheck(run_(self, "Interrupt") != 0);
  ZuCheck(readable_(interrupt));
  ZuCheck(run_(self, "Family") == 0);
  ZuCheck(!ZiStat{family}.exists());
  ZuCheck(ZiFile::mkdir(failFamily) == Zi::OK);
  ZuCheck(write_(ZiFile::append(failFamily, "old"), "old-current"));
  bool dirsSeeded = true;
  for (unsigned i = 1; i <= ZiTestResidue::Age; ++i) {
    Zi::Path archived;
    archived << failFamily << '.' << ZuBox<unsigned>{i};
    dirsSeeded &= ZiFile::mkdir(archived) == Zi::OK;
    dirsSeeded &= write_(ZiFile::append(archived, "old"), "old-archive");
  }
  ZuCheck(dirsSeeded);
  ZuCheck(run_(self, "FailFamily") != 0);
  ZuCheck(ZiStat{ZiFile::append(failFamily, "two.keep")}.exists());
  bool dirsAged = true;
  for (unsigned i = 1; i <= ZiTestResidue::Age; ++i) {
    Zi::Path archived;
    archived << failFamily << '.' << ZuBox<unsigned>{i};
    dirsAged &= ZiStat{ZiFile::append(archived, "old")}.exists();
  }
  ZuCheck(dirsAged);

  ZuCheck(run_(self, "Shm") == 0);
  ZuCheck(run_(self, "SecondName") != 0);
  ZuCheck(run_(self, "InvalidPath") != 0);
  ZuCheck(run_(self, "Duplicate") != 0);
  ZuCheck(run_(self, "Overlap") != 0);
  ZuCheck(run_(self, "RepeatFinal") == 0);
  ZuCheck(run_(self, "Unique") == 0);

  removeFile_(fail);
  removeFile_(failLog);
  removeFile_(interrupt);
  removeFamily_(failFamily);
  removeFamily_(artifact_("zitestresidue-overlap", "same"));
  return 0;
}

int main(int argc, char **argv)
{
  Options options{};
  if (load_(options, argc, argv) != 1) return 2;
  if (options.mode != Mode::Parent) return child_(options.mode);
  return parent_(argv[0]);
}
