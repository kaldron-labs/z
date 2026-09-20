//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z MCP library

#include <zlib/ZmcpLib.hh>

#include <stdint.h>

#ifndef _WIN32
#include <signal.h>
#include <pthread.h>
#else
#include <windows.h>
#endif

ZmcpExtern const char ZmcpLib[] = "@(#) Z MCP Library v" Z_VERNAME;

namespace Zmcp {
namespace Private {

#ifndef _WIN32

static void stdioInterrupt(int) { }

static bool stdioSignal()
{
  static bool initialized = []() {
    struct sigaction action{};
    action.sa_handler = stdioInterrupt;
    sigemptyset(&action.sa_mask);
    return !sigaction(SIGURG, &action, nullptr);
  }();
  return initialized;
}

ZmcpExtern uintptr_t stdioThread()
{
  if (!stdioSignal()) return 0;
  sigset_t signals;
  sigemptyset(&signals);
  sigaddset(&signals, SIGURG);
  if (pthread_sigmask(SIG_UNBLOCK, &signals, nullptr)) return 0;
  return uintptr_t(pthread_self());
}

ZmcpExtern void interruptStdio(uintptr_t thread)
{
  if (thread) pthread_kill(pthread_t(thread), SIGURG);
}

ZmcpExtern void closeStdioThread(uintptr_t) { }

#else

ZmcpExtern uintptr_t stdioThread()
{
  HANDLE thread = nullptr;
  if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
	GetCurrentProcess(), &thread, 0, FALSE, DUPLICATE_SAME_ACCESS))
    return 0;
  return uintptr_t(thread);
}

ZmcpExtern void interruptStdio(uintptr_t thread)
{
  if (thread) CancelSynchronousIo(HANDLE(thread));
}

ZmcpExtern void closeStdioThread(uintptr_t thread)
{
  if (thread) CloseHandle(HANDLE(thread));
}

#endif

} // Private
} // Zmcp
