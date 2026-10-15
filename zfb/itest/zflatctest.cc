//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

#include <zlib/ZfCLI.hh>
#include "ZiTestResidue.hh"
#include "zflatcfixture.hh"

using namespace ZuTestUtil;
using FlatcFixture::Text;

static Zi::Path executable(const Zi::Path &dir, ZuCSpan name)
{
  Zi::Path path = ZiFile::append(dir, Zi::Path{name});
#ifdef _WIN32
  path << ".exe";
#endif
  return path;
}

template <typename ...Args>
static int run(const Zi::Path &exe, const Zi::Path &cwd,
    const Zi::Path &log, Args &&...args)
{
  ZiFile output{log, ZiFile::Write | ZiFile::GC};
  if (!output) return 255;
  ZfCLI::Argv argv;
  argv.push(ZfCLI::Arg{exe});
  (argv.push(ZfCLI::Arg{ZuFwd<Args>(args)}), ...);
  enum { Timeout = 60 }; // process-lifecycle bound, not a performance target
#ifndef _WIN32
  ZfCLI::Argv_C cargs;
  for (const auto &arg : argv) cargs.push(arg.data());
  cargs.push(nullptr);
  pid_t pid = fork();
  if (pid < 0) return 255;
  if (!pid) {
    alarm(Timeout);
    if (chdir(cwd.data()) || dup2(output.handle(), STDOUT_FILENO) < 0 ||
	dup2(output.handle(), STDERR_FILENO) < 0) _exit(255);
    execv(exe.data(), const_cast<char *const *>(cargs.data()));
    _exit(127);
  }
  int status;
  pid_t waited;
  do waited = waitpid(pid, &status, 0); while (waited < 0 && errno == EINTR);
  return waited == pid && WIFEXITED(status) ? WEXITSTATUS(status) : 255;
#else
  SetHandleInformation(output.handle(), HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
  Text command;
  for (const auto &arg : argv) {
    if (command) command << ' ';
    ZfCLI::CmdQuote::quote(command, arg);
  }
  STARTUPINFOA start{};
  start.cb = sizeof(start);
  start.dwFlags = STARTF_USESTDHANDLES;
  start.hStdOutput = output.handle(); start.hStdError = output.handle();
  start.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION process{};
  Text directory{cwd};
  if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE, 0,
      nullptr, directory.data(), &start, &process)) return 255;
  DWORD wait = WaitForSingleObject(process.hProcess, Timeout * 1000);
  DWORD status = 255;
  if (wait == WAIT_TIMEOUT) {
    TerminateProcess(process.hProcess, 254);
    WaitForSingleObject(process.hProcess, INFINITE);
  } else if (wait == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &status);
  CloseHandle(process.hThread); CloseHandle(process.hProcess);
  return int(status);
#endif
}

static void cases(const Zi::Path &dir)
{
  ZuTestScopeRT(cases);
  auto cwd = ZiTestResidue::dir("cases");
  auto tool = executable(ZiFile::append(dir, "../src"), "zflatc");
  auto save = executable(dir, "zfbsave");
  unsigned seq = 0;
  auto log = [&]() {
    Text name; name << "child-" << ZuBox<unsigned>{seq++} << ".log";
    return ZiFile::append(cwd, Zi::Path{name});
  };
  auto path = [&](ZuCSpan name) { return ZiFile::append(cwd, Zi::Path{name}); };
  auto write = [&](ZuCSpan name, ZuCSpan data) {
    return FlatcFixture::write(Text{path(name)}, data);
  };
  auto read = [&](ZuCSpan name) { return FlatcFixture::read(Text{path(name)}); };
  auto stem = path("original");
  ZuCheckRT(run(save, cwd, log(), stem) == 0);
  using Variants = ZuStringTL<"", "_alt", "_zuobject", "_zupolymorph", "_zmobject", "_zmpolymorph", "_custom">;
  ZuUnroll::all<Variants>([&]<typename V>() {
    Text name{"zfbcheck"}; name << V{}();
    auto check = executable(dir, name);
    Text output{"reverse"}; output << V{}() << ".bin";
    auto reverse = path(output);
    auto result = run(check, cwd, log(), stem, reverse);
    ZuCheckRT(result == 0, log_("checker ", name));
    ZuCheckRT(run(save, cwd, log(), "--verify", reverse) == 0);
    using Alts = ZuStringTL<".point.bin", ".wide.bin", ".bytes.bin", ".none.bin">;
    ZuUnroll::all<Alts>([&]<typename A>() {
      Text output{reverse}; output << A{}();
      ZuCheckRT(run(save, cwd, log(), "--verify", output) == 0);
    });
  });
  ZuCheckRT(run(tool, cwd, log(), "--help") == 0);
  ZuCheckRT(run(executable(dir, "zfbmapcheck"), cwd, log()) == 0);
  ZuCheckRT(run(executable(dir, "zfbmapimport"), cwd, log()) == 0);
  ZuCheckRT(run(executable(dir, "zfbmapref"), cwd, log()) == 0);
  auto mapStem = path("native_map");
  auto mapReverse = path("reverse_map.bin");
  ZuCheckRT(run(executable(dir, "zfbmapexport"), cwd, log(), mapStem) == 0);
  ZuCheckRT(run(executable(dir, "zfbmapgenerated"), cwd, log(), mapStem, mapReverse) == 0);
  ZuCheckRT(run(executable(dir, "zfbmapexport"), cwd, log(), "--verify", mapReverse) == 0);
  ZuCheckRT(run(executable(dir, "zfbroot"), cwd, log()) == 0);
  ZuCheckRT(run(executable(dir, "zfbroot_alt"), cwd, log()) == 0);
  ZuCheckRT(run(executable(dir, "zfbroot_zuobject"), cwd, log()) == 0);
  ZuCheckRT(run(executable(dir, "zfbroot_zupolymorph"), cwd, log()) == 0);
  ZuCheckRT(run(executable(dir, "zfbroot_zmobject"), cwd, log()) == 0);
  ZuCheckRT(run(executable(dir, "zfbroot_zmpolymorph"), cwd, log()) == 0);
  ZuCheckRT(run(executable(dir, "zfbinclude"), cwd, log()) == 0);
  ZuCheckRT(run(executable(dir, "zfbinclude_alt"), cwd, log()) == 0);
  auto facet = path("facet");
  auto reverse = path("reverse-facet.bin");
  ZuCheckRT(run(save, cwd, log(), "--facet", facet) == 0);
  ZuCheckRT(run(executable(dir, "zfbcheck_wire"), cwd, log(), facet, reverse) == 0);
  ZuCheckRT(run(save, cwd, log(), "--verify-facet", reverse) == 0);
  using FacetAlts = ZuStringTL<".point.bin", ".wide.bin", ".bytes.bin", ".none.bin">;
  ZuUnroll::all<FacetAlts>([&]<typename A>() {
    Text output{reverse}; output << A{}();
    ZuCheckRT(run(save, cwd, log(), "--verify-facet", output) == 0);
  });
  using Goldens = ZuStringTL<"flatcdep", "flatcroot">;
  ZuUnroll::all<Goldens>([&]<typename G>() {
    Text input; input << G{}() << ".fbs";
    auto schema = ZiFile::append(Zi::Path{ZFLATC_FIXTURES}, Zi::Path{input});
    ZuCheckRT(run(tool, cwd, log(), schema, "-o", G{}()) == 0);
    using Exts = ZuStringTL<"hh", "cc">;
    ZuUnroll::all<Exts>([&]<typename E>() {
      Text output; output << G{}() << '.' << E{}();
      Text golden; golden << G{}() << ".expected." << E{}();
      auto expected = ZiFile::append(Zi::Path{ZFLATC_FIXTURES}, Zi::Path{golden});
      ZuCheckRT(read(output) == FlatcFixture::read(Text{expected}));
    });
  });
  ZuCheckRT(ZiFile::mkdir(path("sub")) == Zi::OK);
  ZuCheckRT(ZiFile::mkdir(path("out")) == Zi::OK);
  ZuCheckRT(write("sub/foo.fbs", "// zflatc_source_only_6c4e\nnamespace test; table Row { display_name:string; } root_type test.Row;"));
  ZuCheckRT(run(tool, cwd, log(), "sub/foo.fbs") == 0);
  ZuCheckRT(ZiStat{path("sub/foo.hh")}.exists() && ZiStat{path("sub/foo.cc")}.exists());
  ZuCheckRT(read("sub/foo.hh").find("zflatc_source_only_6c4e") < 0);
  ZuCheckRT(read("sub/foo.cc").find("zflatc_source_only_6c4e") < 0);
  ZuCheckRT(run(tool, cwd, log(), "sub/foo.fbs", "-o", "foo") == 0);
  ZuCheckRT(ZiStat{path("foo.hh")}.exists() && ZiStat{path("foo.cc")}.exists());
  ZuCheckRT(run(tool, cwd, log(), "sub/foo.fbs", "--alt", "-o", "out/bar") == 0);
  ZuCheckRT(read("out/bar.cpp").find("#include \"bar.hpp\"") >= 0);
  ZuCheckRT(!ZiStat{path("out/bar.hh")}.exists() && !ZiStat{path("out/bar.cc")}.exists());
  ZuCheckRT(write("foo.hh", "old header") && write("foo.cc", "old source"));
  ZuCheckRT(write("bad.fbs", "namespace test; table Row { badName:int; }"));
  auto diagnostic = log();
  ZuCheckRT(run(tool, cwd, diagnostic, "bad.fbs", "-o", "foo") != 0);
  ZuCheckRT(FlatcFixture::read(Text{diagnostic}).find("bad.fbs:1:") >= 0);
  ZuCheckRT(read("foo.hh") == "old header" && read("foo.cc") == "old source");
  ZuCheckRT(run(tool, cwd, log(), "sub/foo.fbs", "-o", "foo") == 0);
  ZuCheckRT(read("foo.hh") != "old header" && read("foo.cc") != "old source");
  ZuCheckRT(write("blocked.hh", "keep header"));
  ZuCheckRT(ZiFile::mkdir(path("blocked.cc")) == Zi::OK);
  ZuCheckRT(run(tool, cwd, log(), "sub/foo.fbs", "-o", "blocked") != 0);
  ZuCheckRT(read("blocked.hh") == "keep header" && ZiStat{path("blocked.cc")}.isdir());
  ZuCheckRT(!ZiTestResidue::glob(cwd, "blocked.hh.zflatc.").length());
  ZuCheckRT(ZiFile::mkdir(path("inc1")) == Zi::OK && ZiFile::mkdir(path("inc2")) == Zi::OK);
  ZuCheckRT(write("inc1/child.fbs", "namespace picks; enum Kind:byte { One = 0 }"));
  ZuCheckRT(write("inc2/child.fbs", "namespace picks; enum Kind:ushort { One = 0 }"));
  ZuCheckRT(write("include.fbs", "include \"child.fbs\"; namespace picks; table Row { kind:Kind = 0; }"));
  ZuCheckRT(run(tool, cwd, log(), "-I", "inc1", "-I", "inc2", "include.fbs") == 0);
  ZuCheckRT(read("include.hh").find("int8_t kind") >= 0);
  ZuCheckRT(read("include.hh").find("#include \"child.hh\"") >= 0);
  ZuCheckRT(!ZiStat{path("child.hh")}.exists() && !ZiStat{path("inc1/child.hh")}.exists());
  ZuCheckRT(run(tool, cwd, log(), "-I", "inc2", "-I", "inc1", "include.fbs", "--alt") == 0);
  ZuCheckRT(read("include.hpp").find("uint16_t kind") >= 0);
  ZuCheckRT(read("include.hpp").find("#include \"child.hpp\"") >= 0);
  ZuCheckRT(run(tool, cwd, log(), "inc1/child.fbs", "-o", "child") == 0);
  ZuCheckRT(ZiStat{path("child.hh")}.exists() && ZiStat{path("child.cc")}.exists());
  ZuCheckRT(write("missing.fbs", "include \"absent.fbs\";"));
  ZuCheckRT(run(tool, cwd, log(), "missing.fbs") != 0);
  ZuCheckRT(write("cycle1.fbs", "include \"cycle2.fbs\";"));
  ZuCheckRT(write("cycle2.fbs", "include \"cycle1.fbs\";"));
  ZuCheckRT(run(tool, cwd, log(), "cycle1.fbs") != 0);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiTestResidue::init("zflatctest");
  Zi::Path self{argv[0]};
  if (!ZiFile::absolute(self)) self = ZiFile::append(ZiFile::cwd(), self);
  auto dir = ZiFile::dirname(self);
  if (ZiFile::leafname(dir) == ".libs") dir = ZiFile::dirname(dir);
  ZuTestMain();
  ZuTestCall(cases, dir);
}
