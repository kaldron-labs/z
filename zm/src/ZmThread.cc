//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// thread class

#include <zlib/ZuLib.hh>

#include <stdio.h>

#ifndef _WIN32
#include <sched.h>
#ifdef linux
#include <syscall.h>
#include <sys/time.h>
#include <sys/resource.h>
#endif
#endif

#include <iostream>

#include <zlib/ZmSingleton.hh>
#include <zlib/ZmSpecific.hh>
#include <zlib/ZmTopology.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmTime.hh>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4996)
#endif

// hwloc topology

ZmTopology *ZmTopology::instance()
{
  static constexpr auto ctor = []() { return new ZmTopology(); };
  return
    ZmSingleton<ZmTopology,
      ZmSingletonCtor<ctor,
	ZmSingletonCleanup<ZmCleanup::Thread>>>::instance();
}

ZmTopology::ZmTopology() : m_errorFn(0)
{
  ZmPLock_init(m_lock);
  hwloc_topology_init(&m_hwloc);
  hwloc_topology_load(m_hwloc);
}

ZmTopology::~ZmTopology()
{
  ZmPLock_lock(m_lock);
  hwloc_topology_destroy(m_hwloc);
  ZmPLock_unlock(m_lock);
  ZmPLock_final(m_lock);
}

void ZmTopology::errorFn(ErrorFn fn)
{
  instance()->m_errorFn = fn;
}

void ZmTopology::error(int errNo)
{
  if (ErrorFn fn = instance()->m_errorFn) (*fn)(errNo);
}

#ifdef _WIN32
struct HandleCloser : public ZmObject {
  ~HandleCloser() { if (h) CloseHandle(h); }
  HANDLE h = 0;
};
#endif

struct ZmThread_Main {
  static bool &is_() { return ZmTLS<bool, is_>(); }
  ZmThread_Main() { is_() = true; }
  bool is() { return is_(); }
};
static ZmThread_Main ZmThread_main;

void ZmThreadContext_::init()
{
  m_main = ZmThread_main.is();
#ifndef _WIN32
  m_pthread = pthread_self();
#ifdef linux
  m_tid = syscall(SYS_gettid);
#endif
  pthread_getcpuclockid(m_pthread, &m_cid);
  m_rtLast = Zm::now();
#else /* !_WIN32 */
  m_tid = GetCurrentThreadId();
  DuplicateHandle(
    GetCurrentProcess(),
    GetCurrentThread(),
    GetCurrentProcess(),
    &m_handle,
    0,
    FALSE,
    DUPLICATE_SAME_ACCESS);
  ZmSpecific<HandleCloser>::instance()->h = m_handle;
  m_rtLast = __rdtsc();
#endif /* !_WIN32 */

#ifndef _WIN32
  void *addr;
  size_t size;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_getattr_np(pthread_self(), &attr);
  pthread_attr_getstack(&attr, &addr, &size);
  m_stackAddr = addr;
  m_stackSize = size;
  pthread_attr_destroy(&attr);
#else /* !_WIN32 */
// NT_TIB structure locations:
//
// x86-32	__readfsdword(0x18)
// x86-64	__readgsqword(0x30)
// arm		(ULONG_PTR)_MoveFromCoprocessor(CP15_TPIDRURW)
// arm64	__getReg(18)			// register x18
// alpha	_rdteb()			// PAL instruction
// ia64		_rdtebex()			// register r13
// MIPS		((PCR *)0x7ffff000)->Teb
// PowerPC	__gregister_get(13) 		// register r13

#ifdef __x86_64__
  auto tib = reinterpret_cast<const NT_TIB *>(__readgsqword(0x30));
#else
  auto tib = reinterpret_cast<const NT_TIB *>(__readfsdword(0x18));
#endif
  m_stackAddr = tib->StackLimit;
  m_stackSize =
    reinterpret_cast<const uint8_t *>(tib->StackBase) -
    reinterpret_cast<const uint8_t *>(tib->StackLimit);
#endif /* !_WIN32 */
}

void ZmThreadContext::init()
{
  ZmThreadContext_::init();
  if (!m_name) {
    if (main())
      m_name << "main";
    else
      m_name << ZuBoxed(tid());
  }
  m_name.terminate();
  if (m_partition < 0) m_partition = 0;
}

void ZmThreadContext::telemetry(ZmThreadTelemetry &data) const {
  data.name = m_name;
  data.tid = tid();
  data.stackSize = stackSize();
  data.cpuset = m_cpuset;
  data.cpuUsage = cpuUsage();
  data.allocStack = allocStack();
  data.allocHeap = allocHeap();
  data.sysPriority = sysPriority();
  data.sid = m_sid;
  data.priority = m_priority;
  data.partition = m_partition;
  data.main = this->main();
  data.detached = m_detached;
}

void ZmThreadContext::prioritize(int priority)
{
  m_priority = priority;
  prioritize();
}

#ifndef _WIN32
struct ZmThread_Priorities {
  ZmThread_Priorities() {
    fifo = sched_get_priority_max(SCHED_FIFO);
    rr = sched_get_priority_max(SCHED_RR);
  }
  int	fifo, rr;
};
void ZmThreadContext::prioritize()
{
  static ZmThread_Priorities p;
  if (m_priority == ZmThreadPriority::RealTime) {
    int policy = !!m_cpuset ? SCHED_FIFO : SCHED_RR;
    sched_param s;
    s.sched_priority = !!m_cpuset ? p.fifo : p.rr;
    int r = pthread_setschedparam(m_pthread, policy, &s);
    if (r) {
      std::cerr << 
	"pthread_setschedparam() failed: " << ZuBoxed(r) << ' ' <<
	strerror(r) << '\n' << std::flush;
    }
  } else if (m_priority < 0)
    m_priority = ZmThreadPriority::Normal;
}
#else /* !_WIN32 */
void ZmThreadContext::prioritize()
{
  static int p[] = {
    THREAD_PRIORITY_TIME_CRITICAL,
    THREAD_PRIORITY_ABOVE_NORMAL,
    THREAD_PRIORITY_NORMAL,
    THREAD_PRIORITY_BELOW_NORMAL
  };
  if (m_priority >= 0)
    SetThreadPriority(
	m_handle, p[m_priority > 3 ? 3 : m_priority]);
}
#endif /* !_WIN32 */

void ZmThreadContext::bind(unsigned partition, const ZmBitmap &cpuset)
{
  m_partition = partition;
  if (!cpuset) return;
  m_cpuset = cpuset;
  bind();
}

void ZmThreadContext::bind()
{
  if (!m_cpuset) return;
  if (hwloc_set_cpubind(
	ZmTopology::hwloc(), m_cpuset, HWLOC_CPUBIND_THREAD) < 0) {
    int errno_ = errno;
    std::cerr << 
      "bind " << ZuBoxed(tid()) << " to cpuset " << m_cpuset <<
      " failed: " << strerror(errno_) << '\n' << std::flush;
    ZmTopology::error(errno_);
  }
  hwloc_get_cpubind(
      ZmTopology::hwloc(), m_cpuset, HWLOC_CPUBIND_THREAD);
}

#ifndef _WIN32
void *ZmThread_start(void *c_)
#else
ZmAPI unsigned __stdcall ZmThread_start(void *c_)
#endif
{
  ZmThreadContext *c = ZmThreadContext::self((ZmThreadContext *)c_);
  c->deref();
  c->init();
#ifndef _WIN32
#ifdef linux
  pthread_setname_np(c->m_pthread, c->name_());
#endif
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0);
  c->bind();
  auto lambda = c->m_lambda;
  c->m_lambda = nullptr;
  return c->m_result = c->m_callFn(lambda);
#else
  c->bind();
  if (c->m_detached) {
    CloseHandle(c->m_handle);
    c->m_handle = 0;
  }
  auto lambda = c->m_lambda;
  c->m_lambda = nullptr;
  c->m_result = c->m_callFn(lambda);
  _endthreadex(0);
  return 0;
#endif
}

int ZmThread::run_(ZmThreadContext *c)
{
  ZmREF(c);
  m_context = c;
#ifndef _WIN32
  {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (c->detached())
      pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (c->stackSize())
      pthread_attr_setstacksize(&attr, c->stackSize());
    // pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    if (pthread_create(&c->m_pthread, &attr, &ZmThread_start, c) < 0) {
      pthread_attr_destroy(&attr);
      ZmDEREF(c);
      m_context = nullptr;
      return -1;
    }
    pthread_attr_destroy(&attr);
  }
  c->prioritize();
#else
  HANDLE h = (HANDLE)_beginthreadex(
      0, c->stackSize(), &ZmThread_start,
      c, CREATE_SUSPENDED, &c->m_tid);
  if (!h) {
    ZmDEREF(c);
    m_context = nullptr;
    return -1;
  }
  c->m_handle = h;
  c->prioritize();
  ResumeThread(h);
#endif
  return 0;
}

int ZmThread::join(void **status)
{
  if (!m_context || m_context->m_detached) return -1;
#ifndef _WIN32
  int r = pthread_join(m_context->m_pthread, status);
  m_context = nullptr;
  return r;
#else
  WaitForSingleObject(m_context->m_handle, INFINITE);
  CloseHandle(m_context->m_handle);
  if (status) *status = m_context->m_result;
  m_context = nullptr;
  return 0;
#endif
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
