//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// high resolution timer, which is challenging on Windows

#include <zlib/ZmTime.hh>
#include <zlib/ZmPLock.hh>

#ifdef _WIN32

#include <intrin.h>

class Zm_WinTimer;

typedef ZuTime (*NowFn)(const Zm_WinTimer *);
ZuTime Zm_now_fast(const Zm_WinTimer *);
ZuTime Zm_now_slow(const Zm_WinTimer *);
static NowFn Zm_nowFn = Zm_now_slow;

class Zm_WinTimer {
friend ZuTime Zm_now_fast(const Zm_WinTimer *);
friend ZuTime Zm_now_slow(const Zm_WinTimer *);

public:
  Zm_WinTimer() { calibrate(); }

  double cpuFreq() const { return m_cpuFreq; }

private:
  void calibrate() {
    ZmGuard<ZmPLock> guard(m_lock);

    unsigned cpuid[4];

    uint64_t ftNow, ftCheck;
    uint64_t qpcNow, qpcCheck, qpcStamp;
    uint64_t cpuStamp;

    // attempt CPU core isolation
 
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    SetThreadAffinityMask(GetCurrentThread(), 2);

    // "burn in" - tight loop until FT ticks up

    GetSystemTimeAsFileTime((FILETIME *)&ftCheck);
    do {
      QueryPerformanceCounter((LARGE_INTEGER *)&qpcCheck);
      do {
	__cpuid((int *)cpuid, 0);
	QueryPerformanceCounter((LARGE_INTEGER *)&qpcNow);
	cpuStamp = __rdtsc();
      } while (qpcNow == qpcCheck);
      qpcStamp = qpcNow;
      GetSystemTimeAsFileTime((FILETIME *)&ftNow);
    } while (ftNow == ftCheck);

    // establish initial start times

    uint64_t ftStart = ftNow;
    uint64_t qpcStart = qpcNow;

    // calculate and record for 100 FT ticks

    ftCheck = ftStart;
    unsigned i = 0, j = 0;
    uint64_t ftTotal = 0;
    uint64_t qpcTotal = 0, qpcDelta;
    for (;;) {
      QueryPerformanceCounter((LARGE_INTEGER *)&qpcCheck);
      do {
	QueryPerformanceCounter((LARGE_INTEGER *)&qpcNow);
      } while (qpcNow == qpcCheck);
      GetSystemTimeAsFileTime((FILETIME *)&ftNow);
      QueryPerformanceCounter((LARGE_INTEGER *)&qpcDelta);
      qpcNow += ((qpcDelta - qpcNow)>>1);
      qpcDelta = qpcNow - qpcStart;
      qpcTotal += qpcDelta;
      ftTotal += ftNow - ftStart;
      j++;
      if (ftNow != ftCheck) {
	if (++i >= 100) break;
	ftCheck = ftNow;
      }
    }

    // adjust FT and QPC start times to average out jitter

    double jd = j;
    ftStart += (uint64_t)((double)ftTotal / jd);
    qpcStart += (uint64_t)((double)qpcTotal / jd);

    // adjust FT start to Unix epoch (FT epoch is Jan 1 1601)
 
    ftStart -= Zm_FT_Epoch;

    // calculate QPC/FT and nsec/QPC ratios
 
    uint64_t qpcFreq;
    QueryPerformanceFrequency((LARGE_INTEGER *)&qpcFreq);
    m_qpc_ft = (uint64_t)((double)1000.0L *
	((double)qpcFreq / (double)10000000.0L));
    m_ns_qpc = (uint64_t)((double)1000.0L *
	((double)1000000000.0L / (double)qpcFreq));

    // calculate FT and QPC offsets

    qpcStart *= 1000;
    m_ftOffset = ftStart - (qpcStart / m_qpc_ft);
    m_qpcOffset = qpcStart % m_qpc_ft;

    // divert to faster implementation if QPC frequency == FT frequency

    if (m_qpc_ft == 1000) Zm_nowFn = Zm_now_fast;

    // obtain CPU TSC frequency, preferring CPUID to elapsed TSC
 
    __cpuid((int *)cpuid, 0);
    if (cpuid[0] < 0x15) {
fallback:
      uint64_t cpuDelta;
      QueryPerformanceCounter((LARGE_INTEGER *)&qpcCheck);
      do {
	__cpuid((int *)cpuid, 0);
	QueryPerformanceCounter((LARGE_INTEGER *)&qpcNow);
	cpuDelta = __rdtsc();
      } while (qpcNow == qpcCheck);
      cpuDelta -= cpuStamp;
      qpcDelta = qpcNow - qpcStamp;
      qpcFreq = (qpcFreq + 500) / 1000;
      m_cpuFreq = (uint64_t)
	((double)((cpuDelta * qpcFreq) / qpcDelta) * 1000.0L);
    } else {
      // below code ported from Linux kernel
      __cpuid((int *)cpuid, 0x1);
      unsigned family = (cpuid[0] >> 8) & 0xf;
      unsigned model = (cpuid[0] >> 4) & 0xf;
      if (family == 0xf) family += (cpuid[0] >> 20) & 0xff;
      if (family >= 0x6) model += ((cpuid[0] >> 16) & 0xf) << 4;
      __cpuid((int *)cpuid, 0x15);
      unsigned crystal_khz = (cpuid[2] + 500) / 1000;
      if (!crystal_khz) {
	// ApolloLake, GeminiLake, CannonLake (and subsequent chipsets)
	// return crystal_khz directly via CPUID.0x15; SkyLake and KabyLake
	// and variants erroneously return 0, despite supporting CPUID.0x15,
	// so these are handled explicitly:
	switch (model) {
	  case 0x4e: // INTEL_FAM6_SKYLAKE_MOBILE
	  case 0x5e: // INTEL_FAM6_SKYLAKE_DESKTOP
	  case 0x8e: // INTEL_FAM6_KABYLAKE_MOBILE
	  case 0x9e: // INTEL_FAM6_KABYLAKE_DESKTOP
	    crystal_khz = 24000;
	    break;
	  case 0x5f: // INTEL_FAM6_ATOM_GOLDMONT_X
	    crystal_khz = 25000;
	    break;
	  case 0x5c: // INTEL_FAM6_ATOM_GOLDMONT
	    crystal_khz = 19200;
	    break;
	  default:
	    goto fallback;
	}
      }
      m_cpuFreq = ((crystal_khz * cpuid[1]) / cpuid[0]) * 1000;
    }

    // revert thread to normal
 
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    SetThreadAffinityMask(GetCurrentThread(), 0xFF);
  }

private:
  ZmPLock	m_lock;		// serializes calibration

  uint64_t	m_qpcOffset;	// QPC offset
  uint64_t	m_ftOffset;	// FILETIME offset
  uint64_t	m_qpc_ft;	// QPC/FILETIME ratio (x1000)
  uint64_t	m_ns_qpc;	// nsec/QPC ratio (x1000)
  uint64_t	m_cpuFreq;	// CPU frequency (TSC cycles per second)
};

ZuTime Zm_now_slow(const Zm_WinTimer *this_)
{
  ZuTime t;
  uint64_t qpc;
  QueryPerformanceCounter((LARGE_INTEGER *)&qpc);
  qpc *= 1000U;
  qpc -= this_->m_qpcOffset;
  uint64_t ft = qpc / this_->m_qpc_ft;
  qpc %= this_->m_qpc_ft;
  ft += this_->m_ftOffset;
  t.tv_sec = ft / 10000000U;
  ft %= 10000000U;
  t.tv_nsec = (qpc * this_->m_ns_qpc) / 1000000U + ft * 100U;
  return t;
}

ZuTime Zm_now_fast(const Zm_WinTimer *this_)
{
  ZuTime t;
  uint64_t qpc;
  QueryPerformanceCounter((LARGE_INTEGER *)&qpc);
  qpc += this_->m_ftOffset;
  t.tv_sec = qpc / 10000000U;
  qpc %= 10000000U;
  t.tv_nsec = qpc * 100U;
  return t;
}

static Zm_WinTimer Zm_winTimer;

ZuTime Zm::now() {
  return (*Zm_nowFn)(&Zm_winTimer);
}

uint64_t Zm::cpuFreq() {
  return Zm_winTimer.cpuFreq();
}

#endif /* !_WIN32 */

// sleep()

#ifndef _WIN32
void Zm::sleep(ZuTime timeout) {
  timespec timeout_{timeout.sec(), timeout.nsec()};
  timespec remaining;

  while (nanosleep(&timeout_, &remaining)) {
    if (errno != EINTR) break;
    timeout_ = remaining;
  }
}
#else
void Zm::sleep(ZuTime timeout) {
  Sleep(timeout.millisecs());
}
#endif
