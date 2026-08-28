//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <iostream>

#include <zlib/ZuBox.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmPlatform.hh>
#include <zlib/ZmRing.hh>

#include <zlib/ZtString.hh>

#include <zlib/ZiFile.hh>

using namespace ZuTestUtil;

namespace ZiRingMatrix_ {

enum {
  CaseTimeout = 1,
  RingSlots = 4
};

struct MsgLayout {
  uintptr_t	p;
  uintptr_t	q;
  uint64_t	seq;
  uint32_t	writer;
};

enum {
  MsgSize = ZmRing_::AlignFn<false, true>::align(sizeof(MsgLayout)),
  RingSize = RingSlots * MsgSize,
  WrapCount = RingSlots + 2,
  BroadcastSize = (WrapCount * 2) * MsgSize
};

static ZtString<> matrixDir;
static unsigned caseID;

static bool systemOK(int status)
{
#ifndef _WIN32
  return status != -1 && WIFEXITED(status) && !WEXITSTATUS(status);
#else
  return status == 0;
#endif
}

static bool hasDirSep(ZuCSpan s)
{
  for (unsigned i = 0, n = s.length(); i < n; ++i)
    if (s[i] == '/' || s[i] == '\\') return true;
  return false;
}

static ZtString<> pathAbs(ZuCSpan path)
{
#ifndef _WIN32
  ZiFile::Path p;
  p << path;
  if (ZiFile::absolute(p)) return p;
  return ZiFile::append(ZiFile::cwd(), p);
#else
  ZtString<> p;
  p << path;
  return p;
#endif
}

static ZtString<> pathDirname(ZuCSpan path)
{
#ifndef _WIN32
  ZiFile::Path p;
  p << path;
  return ZiFile::dirname(p);
#else
  int off = -1;
  for (unsigned i = 0, n = path.length(); i < n; ++i)
    if (path[i] == '/' || path[i] == '\\') off = int(i);
  if (off < 0) return ".";
  if (!off) return "/";
  ZtString<> dir;
  dir << ZuCSpan{path.data(), unsigned(off)};
  return dir;
#endif
}

static ZtString<> executableDir(const char *argv0)
{
  if (!argv0 || !argv0[0]) return ".";
  ZuCSpan arg0{argv0};
  if (!hasDirSep(arg0)) return ".";
  ZtString<> dir = pathDirname(pathAbs(arg0).cspan());
#ifndef _WIN32
  ZiFile::Path p;
  p << dir;
  if (ZiFile::leafname(p) == ".libs") return ZiFile::dirname(p);
#endif
  return dir;
}

static void shellQuote(ZtString<> &s, ZuCSpan value)
{
  s << '\'';
  for (unsigned i = 0, n = value.length(); i < n; ++i) {
    if (value[i] == '\'')
      s << "'\\''";
    else
      s << value[i];
  }
  s << '\'';
}

static bool writeFile(ZuCSpan path, ZuCSpan data, unsigned mode = 0666)
{
  ZiFile file;
  if (file.open(path, ZiFile::Write, mode) != Zi::OK) return false;
  if (file.write(data.data(), data.length()) != Zi::OK) return false;
  file.close();
  return true;
}

static void printFile(const char *label, ZuCSpan path)
{
  ZtString<> path_;
  path_ << path;
  FILE *file = ::fopen(path_.data(), "r");
  if (!file) return;
  std::cout << "# " << label << ":\n";
  char buf[512];
  while (::fgets(buf, sizeof(buf), file)) std::cout << "# " << buf;
  ::fclose(file);
}

struct TempDir {
  char		path[PATH_MAX]{};
  ZtString<>	script;
  ZtString<>	log;

  ~TempDir() { cleanup(); }

  bool init()
  {
    ::snprintf(path, sizeof(path), "/tmp/zringmatrix.XXXXXX");
    if (!::mkdtemp(path)) return false;
    script << static_cast<const char *>(path) << "/case.sh";
    log << static_cast<const char *>(path) << "/case.log";
    return true;
  }

  void cleanup()
  {
    if (!path[0]) return;
    ZiFile::remove(script);
    ZiFile::remove(log);
    ZiFile::rmdir(path);
    path[0] = 0;
  }

  void preserve() { path[0] = 0; }
};

struct CaseScript {
  ZtString<> text;
  ZtString<> ring;
  ZtString<> exe;
  ZtString<> log;

  CaseScript(const TempDir &temp)
  {
    unsigned pathLen = unsigned(::strlen(temp.path));
    ZuCSpan nonce{temp.path + pathLen - 6, 6};
    ring << "ZiRingMatrix." << nonce << '.' << ZuBoxed(Zm::getPID()) << '.' <<
      ZuBoxed(++caseID);
    exe << matrixDir << "/../test/ZiRingTest";
    log = temp.log;
    text <<
      "#!/bin/sh\n"
      "set -u\n"
      "pids=\n"
      "cleanup() {\n"
      "  for pid in $pids; do kill \"$pid\" 2>/dev/null || :; done\n"
      "  for pid in $pids; do wait \"$pid\" 2>/dev/null || :; done\n"
      "}\n"
      "trap cleanup EXIT\n"
      "trap 'exit 125' HUP INT TERM\n";
  }

  void command(ZuCSpan args, bool verbose = false)
  {
    if (verbose) text << "env -u HARNESS_ACTIVE ";
    shellQuote(text, exe);
    if (args) text << ' ' << args;
    text << " >>";
    shellQuote(text, log);
    text << " 2>&1";
  }

  void background(const char *pid, ZuCSpan args, bool verbose = false)
  {
    command(args, verbose);
    text << " &\n" << pid << "=$!\npids=\"$pids $" << pid << "\"\n";
  }

  void foreground(ZuCSpan args, bool verbose = false)
  {
    text << "if ! ";
    command(args, verbose);
    text << "; then exit 1; fi\n";
  }

  void wait(const char *pid)
  {
    text << "if ! wait \"$" << pid << "\"; then exit 1; fi\n";
  }

  ZtString<> args(ZuCSpan options) const
  {
    ZtString<> s;
    s << options << ' ' << ring;
    return s;
  }

  void finish()
  {
    text << "exit 0\n";
  }
};

static void cleanupRing(ZuCSpan name)
{
#ifndef _WIN32
  ZtString<> path;
  path << '/' << name << ".ctrl";
  ::shm_unlink(path.data());
  path.length(0);
  path << '/' << name << ".data";
  ::shm_unlink(path.data());
#else
  static_cast<void>(name);
#endif
}

template <typename Build>
static bool runCase(const char *name, Build build)
{
  TempDir temp;
  if (!temp.init()) {
    ZuTestMgr::check(nullptr, false, name);
    return false;
  }
  CaseScript script{temp};
  build(script);
  script.finish();
  bool written = writeFile(temp.script, script.text, 0777);
  bool ok = false;
  if (written) {
    ZtString<> command;
    command << "HARNESS_ACTIVE=1 timeout -k .1 " << CaseTimeout << " sh ";
    shellQuote(command, temp.script);
    command << " >>";
    shellQuote(command, temp.log);
    command << " 2>&1";
    ok = systemOK(::system(command.data()));
  }
  if (!ok) {
    std::cout << "# failed case: " << name << '\n';
    if (!written) std::cout << "# failed to write case script\n";
    printFile("child output", temp.log);
    printFile("script", temp.script);
    std::cout << "# preserved logs: " << static_cast<const char *>(temp.path) <<
      '\n';
    temp.preserve();
  }
  cleanupRing(script.ring);
  ZuTestMgr::check(nullptr, ok, name);
  return ok;
}

static ZtString<> opts(const char *mode, unsigned count,
    unsigned size = RingSize, ZuCSpan extra = {})
{
  ZtString<> s;
  s << mode << " -b " << ZuBoxed(size) << " -n " << ZuBoxed(count);
  if (extra) s << ' ' << extra;
  return s;
}

static void runMatrix()
{
  ZuTestScopeRT(runMatrix);

  runCase("duplex reopen", [](CaseScript &s) {
    auto a = opts("-x", WrapCount, RingSize, "-l 2");
    s.foreground(s.args(a));
  });

  runCase("separate reader writer", [](CaseScript &s) {
    auto r = opts("-r", WrapCount);
    auto w = opts("-w", WrapCount);
    s.background("r", s.args(r));
    s.foreground(s.args(w));
    s.wait("r");
  });

  runCase("writer before reader", [](CaseScript &s) {
    auto w = opts("-w", WrapCount, RingSize, "-s 1 -t 1");
    auto r = opts("-r", WrapCount, RingSize, "-s 1 -t 1");
    s.background("w", s.args(w));
    s.foreground(s.args(r));
    s.wait("w");
  });

  runCase("replacement reader", [](CaseScript &s) {
    auto r = opts("-r", 2, RingSize, "--no-eof");
    auto w = opts("-w", RingSlots * 8, RingSize, "--no-eof");
    s.background("ra", s.args(r));
    s.background("w", s.args(w));
    s.wait("ra");
    s.background("rb", s.args(r));
    s.wait("rb");
    s.text << "kill -0 \"$w\" 2>/dev/null || exit 1\n"
      "kill -KILL \"$w\" 2>/dev/null || exit 1\n"
      "wait \"$w\" 2>/dev/null || :\n";
  });

  runCase("paced replacement reader", [](CaseScript &s) {
    auto ra = opts("-r", 2, RingSize, "--no-eof -S -i .001 -t 1");
    auto rb = opts("-r", 2, RingSize, "--no-eof -t 1");
    auto w = opts("-w", RingSlots * 8, RingSize, "--no-eof -i .001 -t 1");
    s.background("ra", s.args(ra));
    s.background("w", s.args(w));
    s.wait("ra");
    s.background("rb", s.args(rb));
    s.wait("rb");
    s.text << "kill -0 \"$w\" 2>/dev/null || exit 1\n"
      "kill -KILL \"$w\" 2>/dev/null || exit 1\n"
      "wait \"$w\" 2>/dev/null || :\n";
  });

  runCase("broadcast readers", [](CaseScript &s) {
    auto r = opts("-r", WrapCount, BroadcastSize, "-R");
    auto w = opts("-w", WrapCount, BroadcastSize, "-R --no-eof");
    auto eof = opts("-w", 0, BroadcastSize, "-R");
    s.background("ra", s.args(r));
    s.background("rb", s.args(r));
    s.text << "sleep .01\n";
    s.foreground(s.args(w));
    s.foreground(s.args(eof));
    s.wait("ra");
    s.wait("rb");
  });

  runCase("multiple writers", [](CaseScript &s) {
    auto r = opts("-r", 8, RingSize, "-W -R");
    auto w = opts("-w", 4, RingSize, "-W -R --no-eof");
    auto eof = opts("-w", 0, RingSize, "-W -R");
    s.background("r", s.args(r));
    s.foreground(s.args(w));
    s.foreground(s.args(w));
    s.foreground(s.args(eof));
    s.wait("r");
  });

  runCase("concurrent multiple writers", [](CaseScript &s) {
    auto r = opts("-r", 12, RingSize, "-W -R");
    auto w = opts("-w", 6, RingSize, "-W -R --no-eof");
    auto eof = opts("-w", 0, RingSize, "-W -R");
    s.background("r", s.args(r));
    s.background("wa", s.args(w));
    s.background("wb", s.args(w));
    s.wait("wa");
    s.wait("wb");
    s.foreground(s.args(eof));
    s.wait("r");
  });

  runCase("low latency", [](CaseScript &s) {
    auto r = opts("-r", 4, RingSize, "-L -s 1 -t 1");
    auto w = opts("-w", 4, RingSize, "-L -s 1 -t 1");
    s.background("r", s.args(r));
    s.foreground(s.args(w));
    s.wait("r");
  });

  runCase("slow reader backpressure", [](CaseScript &s) {
    auto r = opts("-r", WrapCount, RingSize, "-S -i .01");
    auto w = opts("-w", WrapCount);
    s.background("r", s.args(r), true);
    s.foreground(s.args(w), true);
    s.wait("r");
    s.text << "grep -Eq 'ring full [1-9][0-9]* times' ";
    shellQuote(s.text, s.log);
    s.text << " || exit 1\n";
  });

  runCase("reset and reuse", [](CaseScript &s) {
    auto r = opts("-r", 4);
    auto w = opts("-w", 4);
    auto reset = opts("-X", 0);
    s.background("ra", s.args(r));
    s.foreground(s.args(w));
    s.wait("ra");
    s.foreground(s.args(reset));
    s.background("rb", s.args(r));
    s.foreground(s.args(w));
    s.wait("rb");
  });

  runCase("reset refusal", [](CaseScript &s) {
    auto r = opts("-r", 2, RingSize, "--no-eof");
    auto w = opts("-w", 1, RingSize, "--no-eof");
    auto reset = opts("-X", 0);
    s.background("r", s.args(r));
    s.foreground(s.args(w));
    s.text << "if ";
    s.command(s.args(reset));
    s.text << "; then exit 1; fi\n";
    s.foreground(s.args(w));
    s.wait("r");
    s.foreground(s.args(reset));
  });
}

} // namespace ZiRingMatrix_

using namespace ZiRingMatrix_;

int main(int argc, char **argv)
{
  if (argc != 1) return 1;
  matrixDir = executableDir(argv[0]);
  verbose = !::getenv("HARNESS_ACTIVE");
  ZuTestMain();
  ZuTestCall(runMatrix);
  return 0;
}
