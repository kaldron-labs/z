//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details).

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include "ZiTestResidue.hh"

using namespace ZuTestUtil;

static void exchange(bool v6, const char *direction, const char *profile = "baseline")
{
  ZuTestScopeRT(exchange);
  Zi::Name name;
  name << (v6 ? "v6-" : "v4-") << direction;
  if (strcmp(profile, "baseline")) name << '-' << profile;
  auto directory = ZiTestResidue::dir(name);
  auto child = fork();
  if (!child) {
    execl("/bin/bash", "bash", "./zutcpkernelfixture.sh", v6 ? "6" : "4", direction,
      directory.data(), profile, static_cast<char *>(nullptr));
    _exit(127);
  }
  int status = -1;
  if (child > 0) {
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    if (result != child) status = -1;
  }
  ZuCheckRT(!status, log_("IPv", v6 ? 6 : 4, ' ', direction, ' ', profile,
    " independent kernel TCP exact binary request/echo/FIN and native drain; logs ", directory));
}

static void nativeClientV4() { exchange(false, "native-client"); }
static void nativeClientV6() { exchange(true, "native-client"); }
static void kernelClientV4() { exchange(false, "kernel-client"); }
static void kernelClientV6() { exchange(true, "kernel-client"); }

static void nativeClientAbsentV4() { exchange(false, "native-client", "absent"); }
static void nativeClientAbsentV6() { exchange(true, "native-client", "absent"); }
static void kernelClientAbsentV4() { exchange(false, "kernel-client", "absent"); }
static void kernelClientAbsentV6() { exchange(true, "kernel-client", "absent"); }
static void nativeClientLossV4() { exchange(false, "native-client", "loss"); }
static void nativeClientLossV6() { exchange(true, "native-client", "loss"); }
static void kernelClientLossV4() { exchange(false, "kernel-client", "loss"); }
static void kernelClientLossV6() { exchange(true, "kernel-client", "loss"); }
static void nativeClientWindowV4() { exchange(false, "native-client", "window"); }
static void nativeClientWindowV6() { exchange(true, "native-client", "window"); }
static void kernelClientWindowV4() { exchange(false, "kernel-client", "window"); }
static void kernelClientWindowV6() { exchange(true, "kernel-client", "window"); }

static void nativeClientRepeatV4() { exchange(false, "native-client", "repeat"); }
static void nativeClientRepeatV6() { exchange(true, "native-client", "repeat"); }
static void kernelClientRepeatV4() { exchange(false, "kernel-client", "repeat"); }
static void kernelClientRepeatV6() { exchange(true, "kernel-client", "repeat"); }

static void nativeClientResetV4() { exchange(false, "native-client", "reset"); }
static void nativeClientResetV6() { exchange(true, "native-client", "reset"); }
static void kernelClientResetV4() { exchange(false, "kernel-client", "reset"); }
static void kernelClientResetV6() { exchange(true, "kernel-client", "reset"); }

static void nativeClientTFOV4() { exchange(false, "native-client", "tfo"); }
static void nativeClientTFOV6() { exchange(true, "native-client", "tfo"); }
static void kernelClientTFOV4() { exchange(false, "kernel-client", "tfo"); }
static void kernelClientTFOV6() { exchange(true, "kernel-client", "tfo"); }
static void nativeClientTFOFallbackV4() { exchange(false, "native-client", "tfo-fallback"); }
static void nativeClientTFOFallbackV6() { exchange(true, "native-client", "tfo-fallback"); }
static void kernelClientTFOFallbackV4() { exchange(false, "kernel-client", "tfo-fallback"); }
static void kernelClientTFOFallbackV6() { exchange(true, "kernel-client", "tfo-fallback"); }

static void nativeClientReorderV4() { exchange(false, "native-client", "reorder"); }
static void nativeClientReorderV6() { exchange(true, "native-client", "reorder"); }
static void kernelClientReorderV4() { exchange(false, "kernel-client", "reorder"); }
static void kernelClientReorderV6() { exchange(true, "kernel-client", "reorder"); }

int main(int argc, char **argv)
{
  parse(argc, argv);
  if (geteuid() || !getenv("ZXDP_RUN_FIXTURE")) {
    std::cout << "1..0 # SKIP requires privileged private namespace fixture\n";
    return 0;
  }
  ZiTestResidue::init("zutcpkerneltest");
  ZuTestMain();
  ZuTestCall(nativeClientV4);
  ZuTestCall(nativeClientV6);
  ZuTestCall(kernelClientV4);
  ZuTestCall(kernelClientV6);
  ZuTestCall(nativeClientAbsentV4);
  ZuTestCall(nativeClientAbsentV6);
  ZuTestCall(kernelClientAbsentV4);
  ZuTestCall(kernelClientAbsentV6);
  ZuTestCall(nativeClientLossV4);
  ZuTestCall(nativeClientLossV6);
  ZuTestCall(kernelClientLossV4);
  ZuTestCall(kernelClientLossV6);
  ZuTestCall(nativeClientWindowV4);
  ZuTestCall(nativeClientWindowV6);
  ZuTestCall(kernelClientWindowV4);
  ZuTestCall(kernelClientWindowV6);
  ZuTestCall(nativeClientRepeatV4);
  ZuTestCall(nativeClientRepeatV6);
  ZuTestCall(kernelClientRepeatV4);
  ZuTestCall(kernelClientRepeatV6);
  ZuTestCall(nativeClientResetV4);
  ZuTestCall(nativeClientResetV6);
  ZuTestCall(kernelClientResetV4);
  ZuTestCall(kernelClientResetV6);
  ZuTestCall(nativeClientTFOV4);
  ZuTestCall(nativeClientTFOV6);
  ZuTestCall(kernelClientTFOV4);
  ZuTestCall(kernelClientTFOV6);
  ZuTestCall(nativeClientTFOFallbackV4);
  ZuTestCall(nativeClientTFOFallbackV6);
  ZuTestCall(kernelClientTFOFallbackV4);
  ZuTestCall(kernelClientTFOFallbackV6);
  ZuTestCall(nativeClientReorderV4);
  ZuTestCall(nativeClientReorderV6);
  ZuTestCall(kernelClientReorderV4);
  ZuTestCall(kernelClientReorderV6);
}
