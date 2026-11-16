//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <iostream>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <zlib/ZuTestUtil.hh>
#include "ZiTestResidue.hh"

using namespace ZuTestUtil;

static void exchange(bool v6, const char *mode)
{
  ZuTestScopeRT(exchange);
  Zi::Name name;
  name << (v6 ? "v6-" : "v4-") << mode;
  auto directory = ZiTestResidue::dir(name);
  auto child = fork();
  if (!child) {
    execl("/bin/bash", "bash", "./zutcppairfixture.sh", v6 ? "6" : "4", mode,
      directory.data(), static_cast<char *>(nullptr));
    _exit(127);
  }
  int status = -1;
  if (child > 0) {
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    if (result != child) status = -1;
  }
  ZuCheckRT(!status, log_("native IPv", v6 ? 6 : 4, ' ', mode,
    " exact multi-packet exchange/drain; logs ", directory));
}

static void testV4() { exchange(false, "plain"); }
static void testV6() { exchange(true, "plain"); }
static void testHalfCloseV4() { exchange(false, "wait-eof"); }
static void testHalfCloseV6() { exchange(true, "wait-eof"); }
static void testLossV4() { exchange(false, "loss"); }
static void testLossV6() { exchange(true, "loss"); }
static void testDataLossV4() { exchange(false, "data-loss"); }
static void testDataLossV6() { exchange(true, "data-loss"); }
static void testDupV4() { exchange(false, "duplicate"); }
static void testDupV6() { exchange(true, "duplicate"); }
static void testReorderV4() { exchange(false, "reorder"); }
static void testReorderV6() { exchange(true, "reorder"); }
static void testAckLossV4() { exchange(false, "ack-loss"); }
static void testAckLossV6() { exchange(true, "ack-loss"); }
static void testPartialAckV4() { exchange(false, "partial-ack"); }
static void testPartialAckV6() { exchange(true, "partial-ack"); }
static void testSACKHolesV4() { exchange(false, "sack-holes"); }
static void testSACKHolesV6() { exchange(true, "sack-holes"); }
static void testRenegingV4() { exchange(false, "sack-reneging"); }
static void testRenegingV6() { exchange(true, "sack-reneging"); }
static void testTLPV4() { exchange(false, "tail-matrix"); }
static void testTLPV6() { exchange(true, "tail-matrix"); }
static void testECNV4() { exchange(false, "ecn-matrix"); }
static void testECNV6() { exchange(true, "ecn-matrix"); }
static void testTFOV4() { exchange(false, "tfo-matrix"); }
static void testTFOV6() { exchange(true, "tfo-matrix"); }
static void testOptionsV4() { exchange(false, "option-matrix"); }
static void testOptionsV6() { exchange(true, "option-matrix"); }

int main(int argc, char **argv)
{
  parse(argc, argv);
  if (geteuid() || !getenv("ZXDP_RUN_FIXTURE")) {
    std::cout << "1..0 # SKIP requires privileged private namespace fixture\n";
    return 0;
  }
  ZiTestResidue::init("zutcppairtest");
  ZuTestMain();
  ZuTestCall(testV4);
  ZuTestCall(testV6);
  ZuTestCall(testHalfCloseV4);
  ZuTestCall(testHalfCloseV6);
  ZuTestCall(testLossV4);
  ZuTestCall(testLossV6);
  ZuTestCall(testDataLossV4);
  ZuTestCall(testDataLossV6);
  ZuTestCall(testDupV4);
  ZuTestCall(testDupV6);
  ZuTestCall(testReorderV4);
  ZuTestCall(testReorderV6);
  ZuTestCall(testAckLossV4);
  ZuTestCall(testAckLossV6);
  ZuTestCall(testPartialAckV4);
  ZuTestCall(testPartialAckV6);
  ZuTestCall(testSACKHolesV4);
  ZuTestCall(testSACKHolesV6);
  ZuTestCall(testRenegingV4);
  ZuTestCall(testRenegingV6);
  ZuTestCall(testOptionsV4);
  ZuTestCall(testOptionsV6);
  ZuTestCall(testECNV4);
  ZuTestCall(testECNV6);
  ZuTestCall(testTLPV4);
  ZuTestCall(testTLPV6);
  ZuTestCall(testTFOV4);
  ZuTestCall(testTFOV6);
}
