//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// low-level last-ditch error logging and signal trapping

#include <zlib/ZmTrap.hh>

#include <stdio.h>
#include <signal.h>

#include <zlib/ZuArray.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuTime.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmBackTrace.hh>

static ZmTrap::Fn ZmTrap_sigintFn;
static ZmTrap::Fn ZmTrap_sighupFn;

// no need for MT safety
void ZmTrap::sigintFn(Fn fn) { ZmTrap_sigintFn = fn; }
ZmTrap::Fn ZmTrap::sigintFn() { return ZmTrap_sigintFn; }

void ZmTrap::sighupFn(Fn fn) { ZmTrap_sighupFn = fn; }
ZmTrap::Fn ZmTrap::sighupFn() { return ZmTrap_sighupFn; }

extern "C" {
#ifndef _WIN32
  extern void ZmTrap_sigabrt(int);
  extern void ZmTrap_sigint(int);
  extern void ZmTrap_sighup(int);
  extern void ZmTrap_sigsegv(int, siginfo_t *, void *);
#else
  extern void __cdecl ZmTrap_sigabrt(int);
  extern BOOL WINAPI ZmTrap_handler(DWORD event);
  extern LONG NTAPI ZmTrap_exHandler(EXCEPTION_POINTERS *exInfo);
#endif
};

// trap signals, etc.
void ZmTrap::trap()
{
#ifndef _WIN32
  {
    struct sigaction s;

    memset(&s, 0, sizeof(struct sigaction));
    s.sa_handler = ZmTrap_sigabrt;
    sigemptyset(&s.sa_mask);
    sigaction(SIGABRT, &s, 0);
    memset(&s, 0, sizeof(struct sigaction));
    s.sa_handler = ZmTrap_sigint;
    sigemptyset(&s.sa_mask);
    sigaction(SIGINT, &s, 0);
    sigaction(SIGTERM, &s, 0);
    s.sa_handler = ZmTrap_sighup;
    sigaction(SIGHUP, &s, 0);
#ifdef ZDEBUG
    memset(&s, 0, sizeof(struct sigaction));
    s.sa_flags = SA_SIGINFO;
    s.sa_sigaction = ZmTrap_sigsegv;
    sigemptyset(&s.sa_mask);
    sigaction(SIGSEGV, &s, 0);
#endif
  }
#else
  signal(SIGABRT, &ZmTrap_sigabrt);
  SetConsoleCtrlHandler(&ZmTrap_handler, TRUE);
#ifdef ZDEBUG
  AddVectoredExceptionHandler(1, &ZmTrap_exHandler);
#endif
#endif
}

// last-ditch logging of a fatal error
void ZmTrap::log(ZuCSpan s)
{
#ifndef _WIN32
  ::write(2, "\r\n", 2);
  ::write(2, s.data(), s.length());
  ::write(2, "\r\n", 2);
#else
  ZmTrap::winErrLog(EVENTLOG_ERROR_TYPE, s);
#endif
}

// Windows error logging
// * used for last-ditch error logging under Windows
// * the classic approach MessageBoxA(0, s, "Title", MB_ICONEXCLAMATION)
//   is inappropriate for server-side mission-critical software
// * GetStdHandle(STD_ERROR_HANDLE) causes unwanted console popups
//   in non-console apps
#ifdef _WIN32
static ZmPLock ZmTrap_lock;

enum { ProgramSize = 64 }; // program name length
using Program = ZuCArray<ProgramSize>;
static Program ZmTrap_program;

void ZmTrap::winProgram(ZuCSpan s)
{
  ZmGuard<ZmPLock> guard(ZmTrap_lock);
  ZmTrap_program = s;
}

void ZmTrap::winErrLog(int type, ZuCSpan s)
{
  // NTFS max path length - MAX_PATH is 260 and deprecated
  enum { BufSize = 32768 };
  using WBuf = ZuWArray<BufSize>;
  using Buf = ZuCArray<BufSize>;

  static Program program;
  static WBuf wbuf;
  static Buf buf;
  static HANDLE handle = INVALID_HANDLE_VALUE;

  ZmGuard<ZmPLock> guard(ZmTrap_lock);

  if (ZuUnlikely(handle == INVALID_HANDLE_VALUE))
    handle = RegisterEventSource(0, L"EventSystem");

  if (ZuUnlikely(!ZmTrap_program)) {
    buf.null();
    GetModuleFileNameA(0, buf.data(), BufSize);
    buf.calcLength();
    int i = buf.length();
    while (i > 0) { if (buf[--i] == '\\') break; }
    if (i) ++i;
    ZuCSpan program_ = buf;
    program_.offset(i);
    if (program_.length() > 64) program_.offset(program_.length() - 64);
    program = program_;
  }

  buf.null();
  buf << ZmTrap_program << " - " << s;

  wbuf.null();
  wbuf.length(ZuUTF<wchar_t, char>::cvt(
	ZuSpan<wchar_t>(wbuf.data(), wbuf.size() - 1), buf));
  wbuf[wbuf.length() - (wbuf.length() == wbuf.size())] = 0;

  const wchar_t *w = wbuf.data();
  ReportEvent(handle, type, 0, 512, 0, 1, 0, &w, 0);
}
#endif

// SIGABRT handling, primarily for assertion failures
void ZmTrap_sigabrt(int)
{
  static ZmAtomic<uint32_t> recursed = 0;
  if (recursed.cmpXch(1, 0)) return;
#ifndef _WIN32
  {
    struct sigaction s;

    memset(&s, 0, sizeof(struct sigaction));
    s.sa_flags = 0;
    s.sa_sigaction =
      reinterpret_cast<void (*)(int, siginfo_t *, void *)>(SIG_DFL);
    sigemptyset(&s.sa_mask);
    sigaction(SIGSEGV, &s, 0);
  }
#else
  signal(SIGABRT, SIG_DFL);
#endif
  ZmBackTrace bt;
  bt.capture(1);
  static ZuCArray<ZmBackTrace_BUFSIZ> buf;
  buf << "SIGABRT\n" << bt;
  ZmTrap::log(buf);
#ifdef _WIN32
  ::ExitProcess(3);
#endif
}

// CTRL-C and disconnect/logout handling
#ifndef _WIN32
void ZmTrap_sigint(int)
{
  if (auto fn = ZmTrap_sigintFn) fn();
}

void ZmTrap_sighup(int)
{
  if (auto fn = ZmTrap_sighupFn) fn();
}
#else
BOOL WINAPI ZmTrap_handler(DWORD event)
{
  if (auto fn = ZmTrap_sigintFn) fn();
  return TRUE;
}
#endif

// SEGV handling
#ifndef _WIN32
void ZmTrap_sigsegv(int s, siginfo_t *si, void *c)
{
  static ZmAtomic<uint32_t> recursed = 0;
  if (recursed.cmpXch(1, 0)) return;
  {
    struct sigaction s;

    memset(&s, 0, sizeof(struct sigaction));
    s.sa_flags = 0;
    s.sa_sigaction =
      reinterpret_cast<void (*)(int, siginfo_t *, void *)>(SIG_DFL);
    sigemptyset(&s.sa_mask);
    sigaction(SIGSEGV, &s, 0);
  }
  ZmBackTrace bt;
  bt.capture(1);
  static ZuCArray<ZmBackTrace_BUFSIZ> buf;
  buf << "SIGSEGV @0x" << ZuBoxPtr(si->si_addr).hex() << '\n' << bt;
  ZmTrap::log(buf);
}
#else
LONG NTAPI ZmTrap_exHandler(EXCEPTION_POINTERS *exInfo)
{
  static ZmAtomic<uint32_t> recursed = 0;
  if (recursed.cmpXch(1, 0) ||
      exInfo->ExceptionRecord->ExceptionCode != STATUS_ACCESS_VIOLATION)
    return EXCEPTION_CONTINUE_SEARCH;
  ZmBackTrace bt;
  bt.capture(exInfo, 0);
  static ZuCArray<ZmBackTrace_BUFSIZ> buf;
  buf << "SIGSEGV\n" << bt;
  ZmTrap::log(buf);
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif
