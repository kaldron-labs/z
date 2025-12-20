//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// platform primitives

#ifndef ZmPlatform_HH
#define ZmPlatform_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#ifndef _WIN32
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#else
#include <winsock2.h>
#include <ole2.h>
#include <olectl.h>
#include <process.h>
#endif

#include <zlib/ZuInt.hh>
#include <zlib/ZuArray.hh>

#ifdef linux
#include <sys/types.h>
#include <linux/unistd.h>
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4251 4800 4996)
#endif

#define ZmIDStrSize	60	// max length of a heap/hash ID

using ZmIDString = ZuCArray<ZmIDStrSize>;

#ifdef __aarch64__	/* 64bit ARM */
#define ZmCacheLineSize 128
#else			/* everything other than 64bit ARM */
#define ZmCacheLineSize 64
#endif

#ifdef _MSC_VER
#define ZmCacheAlign /**/
#else
#define ZmCacheAlign __attribute__((aligned(ZmCacheLineSize)))
#endif

namespace Zm {

enum { CacheLineSize = ZmCacheLineSize };

#ifndef _WIN32
using ProcessID = pid_t;
#ifdef linux
using ThreadID = pid_t;
#else
using ThreadID = pthread_t;
#endif
#else /* !_WIN32 */
using ProcessID = DWORD;
using ThreadID = DWORD;
#endif /* !_WIN32 */

// process ID
#ifndef _WIN32
inline ProcessID getPID() { return getpid(); }
#else
inline ProcessID getPID() { return GetCurrentProcessId(); }
#endif

// thread ID
#if defined(linux) && defined(__x86_64__)
ZuInline ThreadID getTID() {
  unsigned tid;
  __asm__("mov %%fs:0x2d0, %0" : "=r" (tid));
  return tid;
}
#endif
#ifdef _WIN32
ZuInline ThreadID getTID() { return GetCurrentThreadId(); }
#endif

// #cpus (number of cores)
#ifdef _WIN32
typedef BOOL (WINAPI *PIsWow64Process)(HANDLE, PBOOL);
typedef void (WINAPI *PGetNativeSystemInfo)(LPSYSTEM_INFO);

inline unsigned getncpu() {
  SYSTEM_INFO si;
  {
    HMODULE kernel32 = GetModuleHandle(L"kernel32.dll");
    if (!kernel32)
      GetSystemInfo(&si);
    else {
      auto isWow64Process = reinterpret_cast<PIsWow64Process>(
	  GetProcAddress(kernel32, "IsWow64Process"));
      BOOL isWow64;
      if (!isWow64Process ||
	  !isWow64Process(GetCurrentProcess(), &isWow64) ||
	  !isWow64)
	GetSystemInfo(&si);
      else {
	  auto getNativeSystemInfo = reinterpret_cast<PGetNativeSystemInfo>(
	      GetProcAddress(kernel32, "GetNativeSystemInfo"));
	  if (!getNativeSystemInfo)
	    GetSystemInfo(&si);
	  else
	    getNativeSystemInfo(&si);
      }
    }
  }
  int n = si.dwNumberOfProcessors;
  return n < 1 ? 1 : n;
}
#endif
#ifdef linux
inline unsigned getncpu() {
  int n = sysconf(_SC_NPROCESSORS_ONLN);
  return n < 1 ? 1 : n;
}
#endif

// sleep & yield
#ifndef _WIN32
ZuInline void yield() { sched_yield(); }
#else
ZuInline void yield() { ::Sleep(0); }
#endif

// (hard) exit process
#ifndef _WIN32
inline void exit(int code) { ::_exit(code); }
#else
inline void exit(int code) { ::ExitProcess(code); }
#endif

// aligned allocation/free
#ifndef _WIN32
template <unsigned Align_>
inline void *alignedAlloc(unsigned size) {
  enum { Align = Align_ < sizeof(void *) ? sizeof(void *) : Align_ };
  void *ptr;
  if (posix_memalign(&ptr, Align, size)) ptr = nullptr;
  return ptr;
}
inline void alignedFree(const void *ptr) {
  ::free(const_cast<void *>(ptr));
}
#else
template <unsigned Align_>
inline void *alignedAlloc(unsigned size) {
  enum { Align = Align_ < sizeof(void *) ? sizeof(void *) : Align_ };
  return _aligned_malloc(size, Align);
}
inline void alignedFree(const void *ptr) {
  _aligned_free(const_cast<void *>(ptr));
}
#endif

} // namespace Zm

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif /* ZmPlatform_HH */
