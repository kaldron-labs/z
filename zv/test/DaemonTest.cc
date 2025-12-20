//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZmPlatform.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZeLog.hh>

#include <zlib/ZvDaemon.hh>
#include <zlib/ZvCf.hh>

namespace {
  void usage() {
    puts("Usage: DaemonTest [username [password]] [-d|--daemonize]");
    Zm::exit(1);
  }

  void notify(const char *text) {
    ZeLOG(Info, ZtSprintf("PID %d: %s", (int)Zm::getPID(), text));
  }

  ZmSemaphore done;

  void sigint() {
    ZeLOG(Info, "SIGINT");
    done.post();
  }
} // namespace

struct Options {
  ZuCSpan	username;
  ZuCSpan	password;
  bool		daemonize;
  bool		help;
};

ZuStruct(Options,
  (((username),		(CLI::Arg<1>)),		(String)),
  (((password),		(CLI::Arg<2>)),		(String)),
  (((daemonize),	(CLI::Flag<'d'>)),	(Bool)),
  (((help)),					(Bool)));

int main(int argc_, char **argv)
{
  Options options;
  int argc = ZtCLI::load(options, argc_, argv);
  if (argc < 1 || argc > 3) usage();
  if (options.help) usage();

  ZeLog::init("DaemonTest");
  ZeLog::level(0);
  ZeLog::sink(ZeLog::debugSink());

  ZmTrap::sigintFn(sigint);
  ZmTrap::trap();

  int r = ZvDaemon::init(
    options.username, options.password,
    0, options.daemonize, "DaemonTest.pid");

  ZeLog::start();

  switch (r) {
    case ZvDaemon::OK:
      notify("OK");
      break;
    case ZvDaemon::Running:
      notify("already running");
      ZeLog::stop();
      Zm::exit(1);
      break;
    case ZvDaemon::Error:
      notify("error");
      ZeLog::stop();
      Zm::exit(1);
      break;
  }

  done.wait();

  ZeLog::stop();
  return 0;
}
