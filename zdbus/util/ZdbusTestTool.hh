//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZdbusTestTool_HH
#define ZdbusTestTool_HH

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZiEventLoop.hh>
#include <zlib/ZiFile.hh>

namespace ZdbusTestTool {

// Cold test-control lines have a bounded footprint and startup deadline.
enum { LineLimit = 4096, LineTimeout = 5, ChildTimeout = 5 };

// pidfd readiness makes a child wait bounded without polling waitpid.
inline bool waitChild(pid_t pid, int signal, int &status,
  unsigned timeout = ChildTimeout)
{
  if (!timeout) timeout = 1;
  int fd = int(::syscall(SYS_pidfd_open, pid, 0));
  if (fd < 0) {
    ::kill(pid, SIGKILL);
    pid_t reaped;
    do reaped = ::waitpid(pid, &status, 0);
    while (reaped < 0 && errno == EINTR);
    return false;
  }
  if (signal) ::kill(pid, signal);
  ZmScheduler sched{ZmSchedParams().id("ZdbusTestTool.Child")};
  ZiEventLoop loop;
  ZmSemaphore done, stopped;
  ZmAtomic<int> result = 0;
  auto finish = [&result, &done](int value) {
    if (!result.cmpXch(value, 0)) done.post();
  };
  sched.start();
  loop.init(&sched, 1, [&finish](ZeException) { finish(-1); });
  loop.start([&loop, fd, pid, &status, &finish](
      ZiEvent::StartResult started) {
    if (started.is<ZiEvent::Exception>()) {
      finish(-1);
      return;
    }
    if (!loop.addHandle(fd, {}, [pid, &status, &finish](Zi::Handle) {
          pid_t child;
          do child = ::waitpid(pid, &status, WNOHANG);
          while (child < 0 && errno == EINTR);
          if (child == pid) finish(1);
          else if (child < 0) finish(-1);
        })) finish(-1);
  });
  bool exited = done.timedwait(Zm::now(timeout)) == 0 &&
    result.load_() == 1;
  if (!exited) {
    ::kill(pid, SIGKILL);
    done.timedwait(Zm::now(timeout));
  }
  loop.stop([&stopped](ZiEvent::StopResult) { stopped.post(); });
  bool drained = stopped.timedwait(Zm::now(timeout)) == 0;
  sched.stop();
  loop.final();
  ::close(fd);
  if (result.load_() != 1) {
    // The pidfd callback could not reap; avoid leaving this owned child alive.
    pid_t reaped;
    do reaped = ::waitpid(pid, &status, 0);
    while (reaped < 0 && errno == EINTR);
  }
  return drained && result.load_() == 1;
}

template <typename Text>
bool line(int fd, Text &text)
{
  if (!ZiEventLoop::unblock(fd)) return false;
  ZmScheduler sched{ZmSchedParams().id("ZdbusTestTool.Line")};
  ZiEventLoop loop;
  ZmSemaphore done, stopped;
  ZmAtomic<int> result = 0;
  unsigned length = 0;
  auto finish = [&result, &done](int value) {
    if (!result.cmpXch(value, 0)) done.post();
  };
  sched.start();
  loop.init(&sched, 1, [&finish](ZeException) { finish(-1); });
  loop.start([&loop, fd, &text, &length, &finish](
      ZiEvent::StartResult started) {
    if (started.is<ZiEvent::Exception>()) {
      finish(-1);
      return;
    }
    if (!loop.addHandle(fd, {},
        [fd, &text, &length, &finish](Zi::Handle) {
          if (length == LineLimit) {
            finish(-1);
            return;
          }
          while (length < LineLimit) {
            char c;
            int n = int(::read(fd, &c, 1));
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
            if (n != 1) {
              finish(-1);
              return;
            }
            if (c == '\n') {
              finish(text.length() ? 1 : -1);
              return;
            }
            text << c;
            ++length;
          }
          finish(-1);
        })) finish(-1);
  });
  bool signalled = done.timedwait(Zm::now(LineTimeout)) == 0;
  loop.stop([&stopped](ZiEvent::StopResult) { stopped.post(); });
  bool drained = stopped.timedwait(Zm::now(LineTimeout)) == 0;
  sched.stop();
  loop.final();
  return signalled && drained && result.load_() == 1;
}

// Search PATH, including empty components for the current directory. An
// existing but non-executable tool is a startup failure, not a TAP skip.
inline bool available(const char *tool)
{
  const char *path = ::getenv("PATH");
  if (!path) path = "/bin:/usr/bin";
  unsigned toolLen = unsigned(strlen(tool));
  for (;;) {
    const char *end = path;
    while (*end && *end != ':') ++end;
    using Text = ZtString<ZtStringHeapID<"Zdbus.TestTool">>;
    auto candidate = ZtScratch(Text,
      unsigned(end - path) + toolLen + 2);
    if (end == path) candidate << '.';
    else candidate << ZuCSpan{path, unsigned(end - path)};
    candidate << '/' << tool;
    ZiStat stat{candidate};
    if (stat.exists() && !stat.isdir()) return true;
    if (!*end) return false;
    path = end + 1;
  }
}

} // ZdbusTestTool

#endif /* ZdbusTestTool_HH */
