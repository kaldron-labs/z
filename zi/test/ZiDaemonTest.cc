//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZmPlatform.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZiDaemon.hh>

#include <string.h>

namespace {
  void usage() {
    puts("Usage: ZiDaemonTest [username [password]] [-d|--daemonize]");
    Zm::exit(1);
  }

  void notify(const char *text) {
    ZiLOG(Info, "ZiDaemonTest", ([pid = Zm::getPID(), text](auto &s) {
      s << "PID " << pid << ": " << text;
    }));
  }

  ZmSemaphore done;

  void sigint() {
    ZiLOG(Info, "ZiDaemonTest", "SIGINT");
    done.post();
  }
} // namespace

struct Options {
  const char	*username;
  const char	*password;
  bool		daemonize;
  bool		help;
};

int main(int argc_, char **argv)
{
  Options options{};
  for (int i = 1; i < argc_; i++) {
    if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
      options.help = true;
    else if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--daemonize"))
      options.daemonize = true;
    else if (!options.username)
      options.username = argv[i];
    else if (!options.password)
      options.password = argv[i];
    else
      usage();
  }
  if (options.help) usage();

  ZiLog::init("ZiDaemonTest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::debugSink());

  ZmTrap::sigintFn(sigint);
  ZmTrap::trap();

  int r = ZiDaemon::init(
    options.username, options.password,
    0, options.daemonize, "ZiDaemonTest.pid");

  ZiLog::start();

  switch (r) {
    case ZiDaemon::OK:
      notify("OK");
      break;
    case ZiDaemon::Running:
      notify("already running");
      ZiLog::stop();
      Zm::exit(1);
      break;
    case ZiDaemon::Error:
      notify("error");
      ZiLog::stop();
      Zm::exit(1);
      break;
  }

  done.wait();

  ZiLog::stop();
  return 0;
}
