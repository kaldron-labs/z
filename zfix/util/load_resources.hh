//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Opt-in process resource envelope for repository-owned Linux benchmark peers

// CPU/context counters cover every thread in this endpoint process.
#ifndef ZfixLoadResources_HH
#define ZfixLoadResources_HH
#include <stdint.h>
#include <time.h>
#include <sys/resource.h>

namespace ZfixLoad {

struct ProcessPhaseResources {
  uint64_t wallBeforeNs = 0, wallAfterNs = 0, cpuNs = 0;
  uint64_t userNs = 0, systemNs = 0, voluntary = 0, involuntary = 0;
  bool valid = false;

  static uint64_t ns(const timespec &value) {
    return uint64_t(value.tv_sec) * 1000000000 + value.tv_nsec;
  }
  static uint64_t ns(const timeval &value) {
    return uint64_t(value.tv_sec) * 1000000000 + uint64_t(value.tv_usec) * 1000;
  }
  static ProcessPhaseResources capture() {
    ProcessPhaseResources result;
    timespec before{}, after{}, cpu{};
    rusage usage{};
    if (clock_gettime(CLOCK_MONOTONIC, &before) ||
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu) ||
	getrusage(RUSAGE_SELF, &usage) ||
	clock_gettime(CLOCK_MONOTONIC, &after)) return result;
    result.wallBeforeNs = ns(before); result.wallAfterNs = ns(after);
    result.cpuNs = ns(cpu); result.userNs = ns(usage.ru_utime);
    result.systemNs = ns(usage.ru_stime);
    result.voluntary = usage.ru_nvcsw; result.involuntary = usage.ru_nivcsw;
    result.valid = true;
    return result;
  }
};
} // ZfixLoad
#endif
