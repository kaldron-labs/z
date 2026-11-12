//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <net/if.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZtString.hh>

#include "zxdplog.hh"

using namespace ZuTestUtil;

static bool contains(const char *path, const char *text)
{
  // The fixture's result is a single bounded summary line.
  char bytes[4096];
  return ZxdpTest::readLog(path, bytes) && strstr(bytes, text);
}

static int fixture(const char *output, const char *mode, const char *payload,
  const char *count = "32")
{
  auto pid = fork();
  if (pid < 0) return -1;
  if (!pid) {
    ZtString<> log;
    log << output << ".log";
    int fd = open(log, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0 || dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) _exit(126);
    close(fd);
    execl("/bin/bash", "bash", ZXDP_FIXTURE, "--utility", ZXDP_PACKET,
      "--output", output, "--count", count, "--payload", payload, "--mode", mode,
      static_cast<char *>(nullptr));
    _exit(127);
  }
  int status;
  while (waitpid(pid, &status, 0) < 0) if (errno != EINTR) return -1;
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void testProcesses()
{
  ZuTestScopeRT(testProcesses);
  char directory[] = "/tmp/zxdp-loop.XXXXXX";
  auto parent = mkdtemp(directory);
  ZuCheckRT(!!parent, log_("private fixture artifact parent created"));
  if (!parent) return;
  auto beforeA = if_nametoindex("zxdpp0"), beforeB = if_nametoindex("zxdpp1");
  const char *modes[] = {"wait", "spin", "ll"};
  const char *payloads[] = {"1472", "129", "128"};
  for (unsigned i = 0; i < 3; ++i) {
    ZtString<> output;
    output << parent << '/' << modes[i];
    int result = fixture(output, modes[i], payloads[i]);
    ZuCheckRT(result == 0, log_(modes[i], " standalone pair exits successfully: ", output));
    ZtString<> client, server, state;
    client << output << "/client.log";
    server << output << "/server.log";
    state << output << "/state.txt";
    ZuCheckRT(contains(client, "PASS role=client packets=32 completions=32") &&
	contains(server, "PASS role=server packets=32 completions=32") &&
	contains(state, "fixture_exit=0"),
	log_(modes[i], " separate processes verify all payloads and kernel completions"));
    ZuCheckRT(if_nametoindex("zxdpp0") == beforeA && if_nametoindex("zxdpp1") == beforeB,
	log_(modes[i], " namespace cleanup preserves the caller's interfaces"));
  }
  ZtString<> output, server;
  output << parent << "/oversize";
  server << output << "/server.log";
  ZuCheckRT(fixture(output, "wait", "1473") != 0 &&
      contains(server, "invalid interface/payload geometry"),
    log_("oversized standalone payload fails before packet binding: ", output));
  log("Standalone packet artifacts retained: ", parent);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  if (geteuid() || !getenv("ZXDP_RUN_FIXTURE")) {
    std::cout << "1..0 # SKIP requires root and ZXDP_RUN_FIXTURE=1 for scoped namespace setup\n";
    return 0;
  }
  ZuTestMain();
  ZuTestCall(testProcesses);
}
