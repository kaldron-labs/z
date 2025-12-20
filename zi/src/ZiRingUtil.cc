//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// ring buffer inter-process utility functions
//
// (mainly a wrapper around Linux futexes and Win32 equivalent)

#include <zlib/ZuArray.hh>

#include <zlib/ZiRingUtil.hh>

#ifdef linux

#include <sys/syscall.h>
#include <linux/futex.h>

int ZiRingUtil::open(ZeError *e) { return Zi::OK; }

int ZiRingUtil::close(ZeError *e) { return Zi::OK; }

int ZiRingUtil::wait(ZmAtomic<uint32_t> &addr, uint32_t val)
{
  if (addr.cmpXch(val | Waiting, val) != val) return Zi::OK;
  val |= Waiting;
  if (ZuUnlikely(m_params->timeout())) {
    ZuTime out = Zm::now(int(m_params->timeout()));
    unsigned i = 0, n = m_params->spin();
    do {
      if (ZuUnlikely(i >= n)) {
	if (syscall(SYS_futex, reinterpret_cast<volatile int *>(&addr),
	      FUTEX_WAIT_BITSET | FUTEX_CLOCK_REALTIME,
	      int(val), &out, 0, FUTEX_BITSET_MATCH_ANY) < 0) {
	  if (errno == ETIMEDOUT) return Zi::NotReady;
	  if (errno == EAGAIN) return Zi::OK;
	}
	i = 0;
      } else
	++i;
    } while (addr == val);
  } else {
    unsigned i = 0, n = m_params->spin();
    do {
      if (ZuUnlikely(i >= n)) {
	syscall(SYS_futex, reinterpret_cast<volatile int *>(&addr),
	    FUTEX_WAIT, int(val), 0, 0, 0);
	i = 0;
      } else
	++i;
    } while (addr == val);
  }
  return Zi::OK;
}

int ZiRingUtil::wake(ZmAtomic<uint32_t> &addr, unsigned n)
{
  addr &= ~Waiting;
  syscall(SYS_futex, reinterpret_cast<volatile int *>(&addr),
      FUTEX_WAKE, n, 0, 0, 0);
  return Zi::OK;
}

#endif /* linux */

#ifdef _WIN32

int ZiRingUtil::open(ZeError *e)
{
  if (m_sem[Head]) return Zi::OK;
  ZiFile::Path path(params().name().length() + 16);
  path << L"Global\\" << params().name() << L".sem";
  if (!(m_sem[Head] = CreateSemaphore(0, 0, 0x7fffffff, path))) {
    if (e) *e = ZeLastError;
    return Zi::IOError;
  }
  if (!(m_sem[Tail] = CreateSemaphore(0, 0, LONG_MAX, path))) {
    if (e) *e = ZeLastError;
    CloseHandle(m_sem[Head]);
    m_sem[Head] = 0;
    return Zi::IOError;
  }
  return Zi::OK;
}

int ZiRingUtil::close(ZeError *e)
{
  if (!m_sem[Head]) return Zi::OK;
  bool error = false;
  if (!CloseHandle(m_sem[Head])) { if (e) *e = ZeLastError; error = true; }
  if (!CloseHandle(m_sem[Tail])) { if (e) *e = ZeLastError; error = true; }
  m_sem[Head] = m_sem[Tail] = 0;
  return error ? Zi::IOError : Zi::OK;
}

int ZiRingUtil::wait(unsigned index, ZmAtomic<uint32_t> &addr, uint32_t val)
{
  if (addr.cmpXch(val | Waiting, val) != val) return Zi::OK;
  val |= Waiting;
  DWORD timeout = params().timeout() ? params().timeout() * 1000 : INFINITE;
  unsigned i = 0, n = params().spin();
  do {
    if (ZuUnlikely(i >= n)) {
      DWORD r = WaitForSingleObject(m_sem[index], timeout);
      switch ((int)r) {
	case WAIT_OBJECT_0:	return Zi::OK;
	case WAIT_TIMEOUT:	return Zi::NotReady;
	default:		return Zi::IOError;
      }
      i = 0; // not reached
    } else
      ++i;
  } while (addr == val);
  return Zi::OK;
}

int ZiRingUtil::wake(unsigned index, ZmAtomic<uint32_t> &addr, unsigned n)
{
  addr &= ~Waiting;
  ReleaseSemaphore(m_sem[index], n, 0);
  return Zi::OK;
}

#endif /* _WIN32 */

void ZiRingUtil::getpinfo(uint32_t &pid, ZuTime &start)
{
#ifdef linux
  pid = getpid();
  ZuCArray<20> path; path << "/proc/" << ZuBox<uint32_t>(pid);
  struct stat s;
  start = (::stat(path, &s) < 0) ? ZuTime() : ZuTime(s.st_ctim);
#endif
#ifdef _WIN32
  pid = GetCurrentProcessId();
  FILETIME creation, exit, kernel, user;
  start = !GetProcessTimes(
      GetCurrentProcess(), &creation, &exit, &kernel, &user) ?
    ZuTime{} : ZuTime{creation};
#endif
}

bool ZiRingUtil::alive(uint32_t pid, ZuTime start)
{
  if (!pid) return false;
#ifdef linux
  ZuCArray<20> path; path << "/proc/" << ZuBox<uint32_t>(pid);
  struct stat s;
  if (::stat(path, &s) < 0) return false;
  return !start || ZuTime{s.st_ctim} == start;
#endif
#ifdef _WIN32
  HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, pid);
  if (!h || h == INVALID_HANDLE_VALUE) return false;
  DWORD r = WaitForSingleObject(h, 0);
  if (r != WAIT_TIMEOUT) { CloseHandle(h); return false; }
  FILETIME creation, exit, kernel, user;
  if (!GetProcessTimes(h, &creation, &exit, &kernel, &user))
    { CloseHandle(h); return false; }
  return !start || ZuTime{creation} == start;
  CloseHandle(h);
#endif
}

bool ZiRingUtil::kill(uint32_t pid, bool coredump)
{
  if (!pid) return false;
#ifdef linux
  return ::kill(pid, coredump ? SIGQUIT : SIGKILL) >= 0;
#endif
#ifdef _WIN32
  HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
  if (!h || h == INVALID_HANDLE_VALUE) return false;
  bool ok = TerminateProcess(h, unsigned(-1)) == TRUE;
  CloseHandle(h);
  return ok;
#endif
}

