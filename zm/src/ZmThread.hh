//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// thread class
// - globally configured
//   - CPU affinity, priority, stack size, etc.
// - integrates with telemetry (ZvTelemetry)
// - provides available stack to ZmAlloc for safe alloca()

#ifndef ZmThread_HH
#define ZmThread_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <cstddef>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuHash.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuLambdaTraits.hh>

#include <zlib/ZmPlatform.hh>
#include <zlib/ZmBitmap.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmCleanup.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZmPLock.hh>
#include <zlib/ZmGuard.hh>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4251)
#endif

using ZmThreadID = Zm::ThreadID;

namespace ZmThreadPriority {
  enum {		// thread priorities
    Unset = -1,
    RealTime = 0,
    High = 1,
    Normal = 2,
    Low = 3
  };
}

using ZmThreadName = ZuCArray<28>;

// display sequence:
//   name, id, tid, cpuUsage, cpuset, priority, sysPriority,
//   stackSize, partition, main, detached
struct ZmThreadTelemetry {
  ZmThreadName	name;
  uint64_t	tid = 0;	// primary key
  uint64_t	stackSize = 0;
  ZmBitmap	cpuset;
  double	cpuUsage = 0.0;	// graphable (*)
  uint64_t	allocStack = 0;
  uint64_t	allocHeap = 0;
  int32_t	sysPriority = 0;
  uint16_t	sid = 0;	// thread container's slot ID
  uint16_t	partition = 0;
  int8_t	priority = -1;
  uint8_t	main = 0;
  uint8_t	detached = 0;
};

class ZmThreadContext;

#ifndef _WIN32
extern "C" { ZmExtern void *ZmThread_start(void *); }
#else
extern "C" { ZmExtern unsigned __stdcall ZmThread_start(void *); }
#endif

class ZmThreadParams {
public:
  ZmThreadParams &&name(ZuCSpan s)
    { m_name = s; return ZuMv(*this); }
  ZmThreadParams &&stackSize(unsigned v)
    { m_stackSize = v; return ZuMv(*this); }
  ZmThreadParams &&priority(int v)
    { m_priority = v; return ZuMv(*this); }
  ZmThreadParams &&partition(unsigned v)
    { m_partition = v; return ZuMv(*this); }
  ZmThreadParams &&cpuset(const ZmBitmap &b)
    { m_cpuset = b; return ZuMv(*this); }
  ZmThreadParams &&detached(bool b)
    { m_detached = b; return ZuMv(*this); }

  const ZmThreadName &name() const { return m_name; }
  unsigned stackSize() const { return m_stackSize; }
  int priority() const { return m_priority; }
  int partition() const { return m_partition; }
  const ZmBitmap &cpuset() const { return m_cpuset; }
  bool detached() const { return m_detached; }

private:
  ZmThreadName		m_name;
  unsigned		m_stackSize = 0;
  int			m_priority = -1;
  int			m_partition = -1;
  ZmBitmap		m_cpuset;
  bool			m_detached = false;
};

template <typename> struct ZmAlloc_;

class ZmAPI ZmThreadContext_ {
#ifndef _WIN32
  friend ZmAPI void *ZmThread_start(void *);
#else
  friend ZmAPI unsigned __stdcall ZmThread_start(void *);
#endif
  template <typename> friend struct ZmAlloc_;
  template <typename> friend struct ZmLocal_;

protected:
  ZmThreadContext_() noexcept { }

public:
  bool main() const { return m_main; }

#ifndef _WIN32
  pthread_t pthread() const { return m_pthread; }
#ifdef linux
  pid_t tid() const { return m_tid; }
#else
  pthread_t tid() const { return m_pthread; }
#endif
  clockid_t cid() const { return m_cid; }
  double cpuUsage() const {
    ZuTime cpuLast = m_cpuLast;
    ZuTime rtLast = m_rtLast;
    {
      timespec cpuLast_;
      clock_gettime(m_cid, &cpuLast_);
      m_cpuLast = ZuTime{cpuLast_};
    }
    m_rtLast = Zm::now();
    if (ZuUnlikely(!cpuLast || !rtLast)) return 0.0;
    auto cpuDelta = (m_cpuLast - cpuLast).as_fp();
    auto rtDelta = (m_rtLast - rtLast).as_fp();
    return cpuDelta / rtDelta;
  }
  int32_t sysPriority() const {
    struct sched_param p;
#ifdef linux
    sched_getparam(m_tid, &p);
#else
    sched_getparam(0, &p);
#endif
    return p.sched_priority;
  }
#else /* !_WIN32 */
  unsigned tid() const { return m_tid; }
  HANDLE handle() const { return m_handle; }
  double cpuUsage() const {
    ULONG64 cpuLast = m_cpuLast;
    ULONG64 rtLast = m_rtLast;
    QueryThreadCycleTime(m_handle, &m_cpuLast);
    m_rtLast = __rdtsc();
    if (ZuUnlikely(!cpuLast || !rtLast)) return 0.0;
    return
      static_cast<double>(m_cpuLast - cpuLast) /
      static_cast<double>(m_rtLast - rtLast);
  }
  int32_t sysPriority() const {
    return GetThreadPriority(m_handle);
  }
#endif /* !_WIN32 */

  void *stackAddr() const { return m_stackAddr; }
  unsigned stackSize() const { return m_stackSize; }

  uint64_t allocStack() const { return m_allocStack; }
  uint64_t allocHeap() const { return m_allocHeap; }

protected:
  void init();

  bool			m_main = false;
#ifndef _WIN32
  pthread_t		m_pthread = 0;
#ifdef linux
  pid_t			m_tid = 0;
#endif
  clockid_t		m_cid = 0;
  mutable ZuTime	m_cpuLast = 0;
  mutable ZuTime	m_rtLast = 0;
#else /* !_WIN32 */
  unsigned		m_tid = 0;
  HANDLE		m_handle = 0;
  mutable ULONG64	m_cpuLast = 0;
  mutable ULONG64	m_rtLast = 0;
#endif /* !_WIN32 */
  void			*m_stackAddr = nullptr;
  unsigned		m_stackSize = 0;
  uint64_t		m_allocStack = 0;
  uint64_t		m_allocHeap = 0;
};

class ZmThread;

class ZmAPI ZmThreadContext : public ZmObject, public ZmThreadContext_ {
  friend ZmThreadContext *ZmThreadContext_new();
#ifndef _WIN32
  friend ZmAPI void *ZmThread_start(void *);
#else
  friend ZmAPI unsigned __stdcall ZmThread_start(void *);
#endif
  friend ZmThread;

  ZmThreadContext() noexcept { // only called via self() for unmanaged threads
    init();
  }

  template <
    typename L,
    ZuIfT<ZuIsSame<L, void (*)()>{}, int> = 0>
  ZmThreadContext(L l, const ZmThreadParams &params, int sid = -1) :
    m_callFn{[](void *fn) -> void * {
      try { (*reinterpret_cast<L>(fn))(); } catch (...) { }
      return nullptr;
    }},
    m_dtorFn{nullptr},
    m_lambda{reinterpret_cast<void *>(l)},
    m_name{params.name()}, m_sid{sid},
    m_priority{params.priority()},
    m_partition{params.partition()},
    m_cpuset{params.cpuset()},
    m_detached{params.detached()}
  {
    m_stackSize = params.stackSize();
    m_name.terminate();
  }

  template <
    typename L,
    ZuIfT<ZuIsSame<L, void *(*)()>{}, int> = 0>
  ZmThreadContext(L l, const ZmThreadParams &params, int sid = -1) :
    m_callFn{[](void *fn) -> void * {
      void *res = nullptr;
      try {
	res = reinterpret_cast<void *>((*reinterpret_cast<L>(fn))());
      } catch (...) { }
      return res;
    }},
    m_dtorFn{nullptr},
    m_lambda{reinterpret_cast<void *>(l)},
    m_name{params.name()}, m_sid{sid},
    m_priority{params.priority()},
    m_partition{params.partition()},
    m_cpuset{params.cpuset()},
    m_detached{params.detached()}
  {
    m_stackSize = params.stackSize();
    m_name.terminate();
  }

  template <
    typename L,
    decltype(
      ZuIfT<bool(ZuIsStatelessLambda<L>{}) &&
      bool(ZuIsVoidRetLambda<L>{})>(), int()) = 0>
  ZmThreadContext(L l, const ZmThreadParams &params, int sid = -1) :
    m_callFn{[](void *) -> void * {
      try { ZuInvokeLambda<L>(); } catch (...) { }
      return nullptr;
    }},
    m_dtorFn{nullptr},
    m_lambda{nullptr},
    m_name{params.name()}, m_sid{sid},
    m_priority{params.priority()},
    m_partition{params.partition()},
    m_cpuset{params.cpuset()},
    m_detached{params.detached()}
  {
    m_stackSize = params.stackSize();
    m_name.terminate();
  }

  template <
    typename L,
    decltype(ZuIfT<
      !ZuIsStatelessLambda<L>{} &&
      bool(ZuIsVoidRetLambda<L>{})>(), int()) = 0>
  ZmThreadContext(L l, const ZmThreadParams &params, int sid = -1) :
    m_callFn{[](void *lambda_) -> void * {
      if (ZuUnlikely(!lambda_)) return nullptr;
      auto lambda = reinterpret_cast<L *>(lambda_);
      try { (*lambda)(); } catch (...) { }
      delete lambda;
      return nullptr;
    }},
    m_dtorFn{[](void *lambda) {
      delete reinterpret_cast<L *>(lambda);
    }},
    m_lambda{new L{ZuMv(l)}},
    m_name{params.name()}, m_sid{sid},
    m_priority{params.priority()},
    m_partition{params.partition()},
    m_cpuset{params.cpuset()},
    m_detached{params.detached()}
  {
    m_stackSize = params.stackSize();
    m_name.terminate();
  }

  template <
    typename L,
    decltype(ZuIfT<
      bool(ZuIsStatelessLambda<L>{}) &&
      !ZuIsVoidRetLambda<L>{}>(), int()) = 0>
  ZmThreadContext(L l, const ZmThreadParams &params, int sid = -1) :
    m_callFn{[](void *) -> void * {
      void *res = nullptr;
      try { res = static_cast<void *>(ZuInvokeLambda<L>()); } catch (...) { }
      return res;
    }},
    m_dtorFn{nullptr},
    m_lambda{nullptr},
    m_name{params.name()}, m_sid{sid},
    m_priority{params.priority()},
    m_partition{params.partition()},
    m_cpuset{params.cpuset()},
    m_detached{params.detached()}
  {
    m_stackSize = params.stackSize();
    m_name.terminate();
  }

  template <
    typename L,
    decltype(ZuIfT<
      !ZuIsStatelessLambda<L>{} &&
      !ZuIsVoidRetLambda<L>{}>(), int()) = 0>
  ZmThreadContext(L l, const ZmThreadParams &params, int sid = -1) :
    m_callFn{[](void *lambda_) -> void * {
      if (ZuUnlikely(!lambda_)) return nullptr;
      auto lambda = reinterpret_cast<L *>(lambda_);
      void *res = nullptr;
      try { res = static_cast<void *>((*lambda)()); } catch (...) { }
      delete lambda;
      return res;
    }},
    m_dtorFn{[](void *lambda) {
      delete reinterpret_cast<L *>(lambda);
    }},
    m_lambda{new L{ZuMv(l)}},
    m_name{params.name()}, m_sid{sid},
    m_priority{params.priority()},
    m_partition{params.partition()},
    m_cpuset{params.cpuset()},
    m_detached{params.detached()}
  {
    m_stackSize = params.stackSize();
    m_name.terminate();
  }

public:
  ~ZmThreadContext() noexcept {
    if (m_dtorFn) (*m_dtorFn)(m_lambda);
  }

  void init();

  void prioritize(int priority);
  void bind(unsigned partition, const ZmBitmap &cpuset);

  inline static ZmThreadContext *self();

  ZmThreadID tid() const {
#ifndef _WIN32
#ifdef linux
    return m_tid;
#else
    return m_pthread;
#endif
#else /* !_WIN32 */
    return m_tid;
#endif /* !_WIN32 */
  }

  const ZmThreadName &name() const { return m_name; }
  const char *name_() const { return m_name.data(); }
  int sid() const { return m_sid; }

  int priority() const { return m_priority; }

  int partition() const { return m_partition; }
  const ZmBitmap &cpuset() const { return m_cpuset; }

  void *result() const { return m_result; }

  bool detached() const { return m_detached; }

  void telemetry(ZmThreadTelemetry &data) const;

  template <typename S> void print(S &s) const {
    s << this->name() << " (" << ZuBoxed(sid()) << ") [" << m_cpuset << "] "
      << ZuBoxed(cpuUsage() * 100.0).fmt<ZuFmt::FP<2>>() << '%';
  }
  friend ZuPrintFn ZuPrintType(ZmThreadContext *);

private:
  inline static ZmThreadContext *self(ZmThreadContext *c);

  void prioritize();
  void bind();

  typedef void *(*CallFn)(void *);
  typedef void (*DtorFn)(void *);

  CallFn		m_callFn = nullptr;
  DtorFn		m_dtorFn = nullptr;
  void			*m_lambda = nullptr;

  ZmThreadName		m_name;
  int			m_sid = -1;	// thread container's slot ID

  int			m_priority = -1;
  int			m_partition = -1;
  ZmBitmap		m_cpuset;

  void			*m_result = nullptr;

  bool			m_detached = false;
};

// Note: self() depends on ZmSpecific and should not be called during exit
#define ZmSelf() (ZmThreadContext::self())

class ZmAPI ZmThread {
public:
  using Context = ZmThreadContext;
  using ID = ZmThreadID;

  ZmThread() noexcept { }
  template <typename L>
  ZmThread(L l, ZmThreadParams params = ZmThreadParams{}, int sid = -1) {
    run_(new ZmThreadContext{ZuMv(l), params, sid});
  }

  ZmThread(const ZmThread &t) noexcept : m_context(t.m_context) { }
  ZmThread &operator =(const ZmThread &t) noexcept {
    m_context = t.m_context;
    return *this;
  }

  ZmThread(ZmThread &&t) noexcept : m_context(ZuMv(t.m_context)) { }
  ZmThread &operator =(ZmThread &&t) noexcept {
    m_context = ZuMv(t.m_context);
    return *this;
  }

  ZmThread(Context *context) noexcept : m_context(context) { }
  ZmThread &operator =(Context *context) noexcept {
    m_context = context;
    return *this;
  }

  template <typename L>
  int run(L l, ZmThreadParams params = ZmThreadParams{}, int sid = -1) {
    return run_(new ZmThreadContext{ZuMv(l), params, sid});
  }
private:
  int run_(ZmThreadContext *c);

public:
  int join(void **status = 0);

  ZmRef<Context> context() { return m_context; }

  int sid() const {
    if (!m_context) return -1;
    return m_context->sid();
  }

  ID tid() const {
    if (!m_context) return 0;
    return m_context->tid();
  }

  bool equals(const ZmThread &t) const {
    return tid() == t.tid();
  }
  int cmp(const ZmThread &t) const {
    return ZuCmp<ID>::cmp(tid(), t.tid());
  }
  friend inline bool operator ==(const ZmThread &l, const ZmThread &r) {
    return l.equals(r);
  }
  friend inline int operator <=>(const ZmThread &l, const ZmThread &r) {
    return l.cmp(r);
  }

  uint32_t hash() { return ZuHash<ID>::hash(tid()); }

  bool operator !() const { return !m_context; }
  ZuOpBool

  template <class S> struct CSV_ {
    CSV_(S &stream) : m_stream(stream) { 
      m_stream <<
	"name,sid,tid,cpuUsage,cpuSet,sysPriority,priority,"
	"stackSize,partition,main,detached,allocStack,allocHeap\n";
    }
    void print(const ZmThreadContext *tc) {
      ZmThreadTelemetry data;
      static ZmPLock lock;
      ZmGuard<ZmPLock> guard(lock);
      tc->telemetry(data);
      m_stream << data.name
	<< ',' << data.sid
	<< ',' << data.tid
	<< ',' << ZuBoxed(data.cpuUsage * 100.0).fmt<ZuFmt::FP<2>>()
	<< ",\"" << ZmBitmap(data.cpuset) << '"'
	<< ',' << ZuBoxed(data.sysPriority)
	<< ',' << ZuBoxed(data.priority)
	<< ',' << data.stackSize
	<< ',' << ZuBoxed(data.partition)
	<< ',' << ZuBoxed(data.main)
	<< ',' << ZuBoxed(data.detached)
	<< ',' << ZuBoxed(data.allocStack)
	<< ',' << ZuBoxed(data.allocHeap) << '\n';
    }
    S &stream() { return m_stream; }

  private:
    S	&m_stream;
  };
  struct CSV {
    template <typename S> void print(S &s) const;
    friend ZuPrintFn ZuPrintType(CSV *);
  };
  static CSV csv() { return {}; }

private:
  ZmRef<Context>	m_context;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <zlib/ZmSpecific.hh>

inline ZmThreadContext *ZmThreadContext_new() {
  return new ZmThreadContext();
}
ZuDerive(ZmThreadContextTLS,
  (ZmSpecific<ZmThreadContext,
    ZmSpecificCtor<ZmThreadContext_new,
      ZmSpecificCleanup<ZmCleanup::Thread>>>));

inline ZmThreadContext *ZmThreadContext::self() {
  return ZmThreadContextTLS::instance();
}
inline ZmThreadContext *ZmThreadContext::self(ZmThreadContext *c) {
  return ZmThreadContextTLS::instance(c);
}
template <typename S>
inline void ZmThread::CSV::print(S &s) const {
  CSV_<S> csv{s};
  ZmThreadContextTLS::all(
    [&csv](const ZmThreadContext *tc) { csv.print(tc); });
}

#endif /* ZmThread_HH */
