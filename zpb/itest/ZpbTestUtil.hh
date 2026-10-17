//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZpbTestUtil_HH
#define ZpbTestUtil_HH

#include <errno.h>
#include <limits.h>
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZfCLI.hh>
#include <zlib/ZiFile.hh>

namespace ZpbTest {

ZuDerive(Text, (ZtString<ZtStringHeapID<"ZpbTest.Text">>));
ZuDerive(Bytes, (ZtArray<uint8_t, ZtArrayHeapID<"ZpbTest.Bytes">>));

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
  enum { Timeout = 60 };
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
  SetHandleInformation(output.handle(), HANDLE_FLAG_INHERIT,
    HANDLE_FLAG_INHERIT);
  Text command;
  for (const auto &arg : argv) {
    if (command) command << ' ';
    ZfCLI::CmdQuote::quote(command, arg);
  }
  STARTUPINFOA start{};
  start.cb = sizeof(start);
  start.dwFlags = STARTF_USESTDHANDLES;
  start.hStdOutput = output.handle();
  start.hStdError = output.handle();
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
  } else if (wait == WAIT_OBJECT_0)
    GetExitCodeProcess(process.hProcess, &status);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return int(status);
#endif
}

static bool write(const Zi::Path &path, ZuCSpan data)
{
  ZiFile file{path, ZiFile::Write | ZiFile::GC};
  return file && file.write(data.data(), data.length()) == Zi::OK &&
    file.close() == Zi::OK;
}

static Text read(const Zi::Path &path)
{
  ZiFile file{path, ZiFile::ReadOnly | ZiFile::GC};
  if (!file) return {};
  auto stat = file.fstat();
  if (!stat || stat.size() < 0 || uint64_t(stat.size()) > INT_MAX) return {};
  Text out;
  out.length(stat.size());
  if (stat.size() && file.read(out.data(), stat.size()) != stat.size())
    return {};
  return out;
}

} // ZpbTest

#endif // ZpbTestUtil_HH
