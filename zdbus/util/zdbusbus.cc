//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Private bus fixture: print one ready address, then stop on stdin EOF.

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <zlib/ZdbusAddress.hh>

#include "ZdbusTestTool.hh"

static bool writeAll(int fd, ZuCSpan text)
{
  unsigned offset = 0;
  unsigned length = text.length();
  while (offset < length) {
    int n = int(::write(fd, text.data() + offset, length - offset));
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return false;
    offset += unsigned(n);
  }
  return true;
}

int main()
{
  // pipe2's two-int array is required by the OS ABI.
  int output[2];
  if (::pipe2(output, O_CLOEXEC)) return 1;
  pid_t daemon = ::fork();
  if (daemon < 0) {
    ::close(output[0]);
    ::close(output[1]);
    return 1;
  }
  if (!daemon) {
    if (::prctl(PR_SET_PDEATHSIG, SIGTERM) || ::getppid() == 1)
      _exit(1);
    ::close(output[0]);
    ::dup2(output[1], STDOUT_FILENO);
    ::close(output[1]);
    ::execlp("dbus-daemon", "dbus-daemon", "--session", "--nofork",
      "--print-address=1", static_cast<char *>(nullptr));
    _exit(errno == ENOENT ? 127 : 126);
  }
  ::close(output[1]);
  ZdbusAddress::Text line;
  bool ready = ZdbusTestTool::line(output[0], line);
  ::close(output[0]);
  ZdbusAddress addr;
  if (ready) ready = ZdbusAddress::parse(addr, line);
  if (ready) ready = writeAll(STDOUT_FILENO, line) &&
    writeAll(STDOUT_FILENO, "\n");
  if (ready) {
    char c;
    while (::read(STDIN_FILENO, &c, 1) < 0 && errno == EINTR) { }
  }
  int status = 0;
  bool reaped = ZdbusTestTool::waitChild(daemon, SIGTERM, status);
  bool clean = WIFEXITED(status) ? !WEXITSTATUS(status) :
    WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM;
  return ready && reaped && clean ? 0 : 1;
}
