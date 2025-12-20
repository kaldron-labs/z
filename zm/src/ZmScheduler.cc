//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// scheduler with thread pool

#include <zlib/ZuBox.hh>

#include <zlib/ZmScheduler.hh>
#include <zlib/ZmTrap.hh>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4800)
#endif

ZmScheduler::ZmScheduler(ZmSchedParams params) : m_params{ZuMv(params)}
{
  unsigned n = m_params.nThreads();
  for (unsigned sid = 0; sid <= n; sid++) {
    auto &thread = m_params.thread(sid);
    if (!thread.name()) {
      if (!sid)
	thread.name(ZmThreadName{} << m_params.id() << ":timer");
      else
	thread.name(ZmThreadName{} << m_params.id() << ':' << ZuBoxed(sid));
    }
    if (!thread.stackSize()) thread.stackSize(m_params.stackSize());
    if (thread.priority() < 0) thread.priority(m_params.priority());
    if (thread.partition() < 0) thread.partition(m_params.partition());
  }
  m_threads = new Thread[n];
  m_workers = new Thread *[n];
  for (unsigned i = 0; i < n; i++) {
    unsigned sid = i + 1;
    Ring &ring = m_threads[i].ring;
    ring.init(ZmRingParams{m_params.queueSize()}.
	ll(m_params.ll()).
	spin(m_params.spin()).
	timeout(m_params.timeout()).
	cpuset(m_params.thread(sid).cpuset()));
    int r;
    if ((r = ring.open(Ring::Read | Ring::Write)) != Zu::OK)
      throw Zu::IOResult{r};
    // if ((r = ring.attach()) != Zu::OK) throw Zu::IOResult{r};
    // m_threads[i].queue.init(ZmQueueParams{});
    if (!m_params.thread(sid).isolated())
      m_workers[m_nWorkers++] = &m_threads[i];
  }
}

ZmScheduler::~ZmScheduler()
{
  for (unsigned i = 0, n = m_params.nThreads(); i < n; i++) {
    auto &ring = m_threads[i].ring;
    auto &thread = m_threads[i].thread;
    // protect against blocking on self-destruction, do NOT use ZmSelf()
    // - ZmSelf() depends on TLS and should not be called during exit
    auto self = Zm::getTID();
    if (!ring.closed()) {
      if (thread) {
	ring.eof(true);
	if (thread.tid() != self) thread.join();
      }
      ring.close();
    }
  }
  delete [] m_workers;
  delete [] m_threads;
}

void ZmScheduler::start_()
{
  bool ok = start__();
  started(ok);
  if (ok) timer();
}
bool ZmScheduler::start__()
{
  {
    unsigned n = m_params.nThreads();
    SpawnGuard spawnGuard(m_spawnLock);
    for (unsigned i = 0; i < n; i++)
      m_threads[i].ring.eof(false);
    while (m_runThreads < n) {
      int sid = ++m_runThreads;
      if (m_threads[sid - 1].thread.run(
	  [this]() { work(); },
	  m_params.thread(sid), sid) < 0)
	return false;
    }
  }
  return true;
}
bool ZmScheduler::stop()
{
  // protect against blocking on self-destruction, do NOT use ZmSelf()
  // - ZmSelf() depends on TLS and should not be called during exit
  auto self = Zm::getTID();
  {
    unsigned n = m_params.nThreads();
    SpawnReadGuard spawnGuard(m_spawnLock);
    for (unsigned i = 0; i < n; i++)
      if (m_threads[i].tid == self) {
	spawnGuard.unlock();
	ZmEngine::stop({});	// async
	return true;		// return success
      }
  }
  return ZmEngine::stop();
}
void ZmScheduler::wake()
{
  m_pending.post();
}
void ZmScheduler::stop_()
{
  stopped(stop__());
}
bool ZmScheduler::stop__()
{
  {
    unsigned n = m_params.nThreads();
    SpawnGuard spawnGuard(m_spawnLock);
    for (unsigned i = 0; i < n; i++)
      m_threads[i].ring.eof(true);
    for (unsigned i = 0; i < n; i++) {
      auto &thread = m_threads[i].thread;
      if (thread) {
	wake(&m_threads[i]);
	thread.join();
	thread = {};
      }
    }
  }
  m_thread = {};
  return true;
}
bool ZmScheduler::reset()
{
  if (running()) return false;

  {
    unsigned n = m_params.nThreads();
    SpawnGuard spawnGuard(m_spawnLock);
    for (unsigned i = 0; i < n; i++) {
      auto thread = &m_threads[i];
      if (!thread->thread) {
	thread->queueCount = 0;
	thread->queue.clean();
	thread->ring.reset(); // ok to ignore errors
      }
    }
  }

  return true;
}

void ZmScheduler::wakeFn(unsigned sid, ZmFn<> fn)
{
  ZmAssert(sid && sid <= m_params.nThreads());
  m_threads[sid - 1].wakeFn = ZuMv(fn);
}

void ZmScheduler::timer()
{
  for (;;) {
    if (stopped()) return;

    {
      ZuTime minimum;

      {
	SchedGuard schedGuard(m_schedLock);

	if (Timer *first = m_schedule.minimum()) minimum = first->timeout;
      }

      if (*minimum)
	m_pending.timedwait(minimum);
      else
	m_pending.wait();
    }

    if (stopped()) return;

    ZuTime now = Zm::now();
    now += m_params.quantum();

    {
      SchedGuard schedGuard(m_schedLock);
      auto i = m_schedule.iter<ZmRBTreeLessEqual>(now);
      while (Timer *timer = i()) {
	i.del(timer);
	bool ok;
	unsigned sid = timer->sid;
	if (ZuLikely(sid))
	  ok = tryRun_(&m_threads[sid - 1], timer->fn);
	else
	  ok = timerAdd(timer->fn);
	if (ZuUnlikely(!ok)) {
	  m_schedule.addNode(timer);
	  schedGuard.unlock();
	  Zm::sleep(m_params.quantum());
	  return;
	}
	timer->timeout = ZuTime{};
	if (timer->transient) delete timer;
      }
    }
  }
}

bool ZmScheduler::timerAdd(Fn &fn)
{
  if (ZuUnlikely(!m_nWorkers)) return false;
  unsigned first = m_next++;
  unsigned next = first;
  do {
    Thread *thread = m_workers[next % m_nWorkers];
    if (tryRun_(thread, fn)) return true;
  } while (((next = m_next++) - first) < m_nWorkers);
  return false;
}

void ZmScheduler::schedule_(
    unsigned sid, Fn &fn, ZuTime timeout, int mode, Timer *timer)
{
  ZmAssert(sid <= m_params.nThreads());

  bool kick = true;

  if (!timer) timer = new Timer{true};

  {
    SchedGuard schedGuard(m_schedLock);

    if (ZuLikely(*timer)) {
      switch (mode) {
	case Advance:
	  if (ZuUnlikely(timer->timeout <= timeout)) return;
	  break;
	case Defer:
	  if (ZuUnlikely(timer->timeout >= timeout)) return;
	  break;
      }
      m_schedule.delNode(timer);
      timer->timeout = ZuTime{};
    }

    if (ZuUnlikely(timeout <= Zm::now())) {
      if (ZuLikely(sid)) {
	if (ZuLikely(tryRun_(&m_threads[sid - 1], fn))) return;
      } else {
	if (ZuLikely(timerAdd(fn))) return;
      }
    }

    if (Timer *first = m_schedule.minimum())
      kick = timeout < first->timeout;

    timer->timeout = timeout;
    timer->sid = sid;
    timer->fn = ZuMv(fn);
    m_schedule.addNode(timer);
  }

  if (kick) wake();
}

bool ZmScheduler::del(Timer *timer)
{
  SchedGuard schedGuard(m_schedLock);

  if (!*timer) return false;
  bool found = !!m_schedule.delNode(timer);
  timer->timeout = ZuTime{};
  if (timer->transient) delete timer;
  return found;
}

void ZmScheduler::add_(Fn &fn)
{
  if (ZuUnlikely(!m_nWorkers)) return;
  unsigned first = m_next++;
  unsigned next = first;
  do {
    auto thread = m_workers[next % m_nWorkers];
    if (tryRun_(thread, fn)) return;
  } while (((next = m_next++) - first) < m_nWorkers);
  run_(m_workers[first % m_nWorkers], fn);
}

void ZmScheduler::run_(Thread *thread, Fn &fn)
{
  if (ZuLikely(push_(thread, fn))) wake(thread);
}

bool ZmScheduler::tryRun_(Thread *thread, Fn &fn)
{
  if (ZuLikely(tryPush_(thread, fn))) { wake(thread); return true; }
  return false;
}

bool ZmScheduler::push_(Thread *thread, Fn &fn)
{
  // Note: the MPSC requirement is to serialize each producing thread's work
  if (ZuLikely(!thread->queueCount.load_())) goto push;
overflow:
  ++thread->queueCount;
  thread->queue.push(ZuMv(fn));
  return true;
push:
  {
    unsigned size = fn.pushSize();
    void *ptr;
    if (ZuLikely(ptr = thread->ring.tryPush(size))) {
      fn.push(ptr);
      thread->ring.push2(ptr, size);
      return true;
    }
  }
  int status = thread->ring.writeStatus();
  if (status == Zu::EndOfFile || status >= 0) goto overflow;
  // should never happen - the enqueuing thread will normally
  // be forced to wait for the dequeuing thread to drain the ring,
  // i.e. a slower consumer will apply back-pressure to the producer
  ZuCArray<120> s;
  s << "FATAL - Thread Dispatch Failure - push() failed: ";
  if (status <= 0)
    s << Zu::IOResult{status};
  else
    s << ZuBoxed(status) << " bytes remaining";
  ZmTrap::log(s);
  return false;
}

bool ZmScheduler::tryPush_(Thread *thread, Fn &fn)
{
  {
    unsigned size = fn.pushSize();
    void *ptr;
    if (ZuLikely(ptr = thread->ring.tryPush(size))) {
      fn.push(ptr);
      thread->ring.push2(ptr, size);
      return true;
    }
  }
  return false;
}

void ZmScheduler::work()
{
  Thread *thread = &m_threads[ZmSelf()->sid() - 1];

  thread->tid = thread->thread.tid();

  m_threadInitFn();

  for (;;) { // Note: this is single-threaded, but queueCount is SWMR
    if (ZuLikely(!thread->queueCount.load_())) goto shift;
    if (Fn fn = thread->queue.shift()) {
      unsigned size = fn.pushSize();
      void *ptr;
      if (ZuLikely(ptr = thread->ring.tryPush(size))) {
	fn.push(ptr);
	thread->ring.push2(ptr, size);
	--thread->queueCount;
      } else {
	thread->queue.unshift(ZuMv(fn));
      }
    }
shift:
    if (void *ptr = thread->ring.shift()) {
      thread->ring.shift2(Fn::invoke(ptr));
    } else {
      if (thread->ring.readStatus() == Zu::EndOfFile) break;
    }
  }

  m_threadFinalFn();

  --m_runThreads;
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
