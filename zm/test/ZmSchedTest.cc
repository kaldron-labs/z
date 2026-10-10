//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// scheduler test program

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmFn.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmSpecific.hh>
#include <zlib/ZmBackoff.hh>
#include <zlib/ZmTimeout.hh>

#include <zlib/ZtcQueue.hh>

using namespace ZuTestUtil;

struct QueueCheck {
  unsigned	size;
  unsigned	count = 0;
  bool		valid = true;
};

struct TLS : public ZmObject {
  TLS() : m_ping(0) {
    log("TLS(0) [", ZmSelf()->sid(), ']');
  }
  ~TLS() {
    log("~TLS(", m_ping, ") [", ZmSelf()->sid(), ']');
  }
  void ping() { ++m_ping; }
  unsigned	m_ping;
};

class Job : public ZmPolymorph {
public:
  Job(const char *message, ZuTime timeout) :
    m_message{message}, m_timeout{timeout}
  {
    log("Job() this=", ZuBoxPtr(this).hex(),
	" message=", ZuBoxPtr(m_message).hex(),
	' ', message);
  }
  ~Job() {
    log("~Job() ", ZuBoxPtr(this).hex(), " ~", m_message,
	" [", ZmThreadContext::self()->sid(), ']');
    ::free((void *)m_message);
  }

  void *operator()() {
    ZmAssert(!(reinterpret_cast<uintptr_t>(this) & 8));
    ZmSpecific<TLS>::instance()->ping();
    log("Job::() ", ZuBoxPtr(this).hex(), ' ', m_message,
	" [", ZmThreadContext::self()->sid(), ']');
    return 0;
  }

  ZuTime timeout() { return m_timeout; }

private:
  const char	*m_message = nullptr;
  ZuTime	m_timeout;
};

class Timer : public ZmObject, public ZmTimeout {
public:
  Timer(ZmScheduler *s, const ZmBackoff &t) : ZmTimeout(s, t, -1) { }

  void retry() {
    ZuTime now = Zm::now();

    log(now.sec(), ' ', now.nsec());
  }
};

#include <signal.h>

void segv(int s)
{
  log(Zm::getPID(), '/', Zm::getTID(), ": SEGV");
  
  while (-1);
}

void breakpoint(ZmScheduler::Timer *timer)
{
  log("breakpoint");
}

char *message_(int j)
{
  ZuCArray<32> s;
  s << "Goodbye World " << j;
  auto n = s.length() + 1;
  s[n - 1] = 0;
  auto *buf = static_cast<char *>(malloc(n));
  if (!buf) return buf;
  for (unsigned k = 0; k < n; k++) buf[k] = s.data()[k];
  return buf;
}

void usage_()
{
  std::cerr <<
    "Usage: ZmSchedTest [OPTION]...\n\n"
    "Options:\n"
    "  -q\t\tquiet output (default when test-harnessed)\n"
    "  -n N\t\tset number of threads to N\n"
    "  -c ID=CPUSET\tset thread ID affinity to CPUSET (e.g. 1=2,4)\n"
    "  -i BITMAP\tset isolation (e.g. 1,3-4)\n";
  Zm::exit(1);
}

// Leave room for a small callback after a larger one has overflowed.
// Both must retain their submission order across the ring/queue boundary.
static void overflowOrder()
{
  ZuTestScopeRT(overflowOrder);
  ZmScheduler s{ZmSchedParams{}.id("overflow").queueSize(4096)};
  ZmSemaphore entered, release, done;
  bool ran = false, ordered = false;
  struct Large {
    ZmSemaphore *done;
    bool *ran;
    // Deliberately exceed the remaining ring space while a small lambda fits.
    char padding[512];
    void operator()() { *ran = true; done->post(); }
  };
  bool started = s.start();
  ZuCheckRT(started);
  if (!started) return;
  s.run([&entered, &release]() { entered.post(); release.wait(); }, 1);
  entered.wait();
  while (s.ring(1).size() - s.ring(1).count_() >=
      sizeof(Large) + sizeof(void *))
    s.run([]() { }, 1);
  s.run(Large{&done, &ran, {}}, 1);
  ZuCheckRT(s.queue(1).count_() == 1);
  s.run([&ran, &ordered, &done]() { ordered = ran; done.post(); }, 1);
  release.post();
  done.wait();
  done.wait();
  bool stopped = s.stop();
  ZuCheckRT(stopped);
  ZuCheckRT(ordered);
}

struct ControlGate {
  ZmSemaphore entered, release, finished, destroying, destroyed;
};

struct ControlTLS : public ZmObject {
  ControlGate *gate = nullptr;

  ~ControlTLS() {
    if (!gate) return;
    gate->entered.post();
    gate->release.wait();
    gate->finished.post();
  }
};

class ControlSched : public ZmScheduler {
public:
  ControlSched(ControlGate &gate) : m_gate{gate} { }
  ~ControlSched() { m_gate.destroying.post(); }

protected:
  bool start__() override {
    if (!m_starts++) ZmSpecific<ControlTLS>::instance()->gate = &m_gate;
    return ZmScheduler::start__();
  }

private:
  ControlGate &m_gate;
  unsigned m_starts = 0;
};

// Hold the first control thread in TLS cleanup. Destruction must join it,
// including when a stop callback synchronously starts its successor.
static void controlLifetime(bool restart, bool implicitStop = false)
{
  ZuTestScopeRT(controlLifetime);
  ControlGate gate;
  ControlSched *s = nullptr;
  ZmSemaphore ready, destroy;
  ZmThread owner{[&s, &gate, &ready, &destroy]() {
    {
      ControlSched scheduler{gate};
      s = &scheduler;
      ready.post();
      destroy.wait();
    }
    gate.destroyed.post();
  }};
  ready.wait();
  bool started = s->start();
  ZuCheckRT(started);
  if (!started) {
    destroy.post();
    owner.join();
    return;
  }
  if (restart) {
    ZmSemaphore restarted;
    bool ok = false;
    static_cast<ZmEngine<ZmScheduler> &>(*s).stop(
      [s, &ok, &restarted](bool stopped) {
	ok = stopped && s->start();
	restarted.post();
      });
    restarted.wait();
    ZuCheckRT(ok);
  }
  if (!implicitStop) {
    ZuCheckRT(s->stop());
    gate.entered.wait();
  }
  destroy.post();
  gate.destroying.wait();
  if (implicitStop) gate.entered.wait();
  // No timing controls production shutdown: the timeout only detects an
  // early destructor return while the control thread is explicitly held.
  bool early = !gate.destroyed.timedwait(Zm::now() + ZuTime{0.1});
  gate.release.post();
  if (!early) gate.destroyed.wait();
  owner.join();
  gate.finished.wait();
  ZuCheckRT(!early);
}

static void selfStop()
{
  ZuTestScope(selfStop);
  ZmScheduler s;
  ZuCheck(s.start());
  ZmSemaphore done;
  bool stopped = false;
  s.run([&s, &stopped, &done]() {
    stopped = s.stop();
    done.post();
  }, 1);
  done.wait();
  ZuCheck(stopped);
  ZuCheck(s.stop());
}

int main(int argc, char **argv)
{
  ZmSchedParams params = ZmSchedParams().id("sched");
  ZmBitmap isolation;

  ZuTestUtil::parse(1, argv);
  for (int i = 1; i < argc; i++) {
    if (argv[i][0] != '-') usage_();
    switch (argv[i][1]) {
      case 'q':
	verbose = false;
	break;
      case 'n':
	if (++i >= argc) usage_();
	params.nThreads(ZuBox<unsigned>(argv[i]));
	break;
      case 'c': {
	if (++i >= argc) usage_();
	unsigned o, n = strlen(argv[i]);
	for (o = 0; o < n; o++) if (argv[i][o] == '=') break;
	if (!o || o >= n - 1) usage_();
	params.thread(ZuBox<unsigned>(ZuCSpan(argv[i], o)))
	  .cpuset(ZuCSpan(&argv[i][o + 1], n - o - 1));
      } break;
      case 'i':
	if (++i >= argc) usage_();
	isolation = argv[i];
	break;
      default:
	usage_();
	break;
    }
  }

  ZuTestMain();

  ZuTestCall(overflowOrder);
  ZuTestCall(controlLifetime, false);
  ZuTestCall(controlLifetime, true);
  ZuTestCall(controlLifetime, false, true);
  ZuTestCall(selfStop);

  signal(SIGSEGV, segv);

  {
    int tid = isolation.first();
    while (tid >= 0) {
      params.thread(tid).isolated(true);
      tid = isolation.next(tid);
    }
  }

  unsigned nQueues = params.nThreads();
  QueueCheck queues{params.queueSize()};
  ZmScheduler s{ZuMv(params)};
  unsigned allQueues = s.allQueues(Ztc::QueueMgr::AllFn{
    &queues, [](QueueCheck *queues, Ztc::Queue *queue) {
      auto key = queue->telKey();
      Ztc::QueueTelemetry data;
      queue->telemetry(data);
      ++queues->count;
      queues->valid =
	queues->valid &&
	key.p<0>() == data.ownerID &&
	key.p<1>() == data.id &&
	key.p<2>() == data.type &&
	data.type == Ztc::QueueType::Thread &&
	data.size == queues->size &&
	!data.count &&
	!data.inCount &&
	!data.inBytes &&
	!data.outCount &&
	!data.outBytes &&
	!data.full;
    }});
  // ZmRef<Job> jobs[10];
  // ZmFn<> fns[10];
  ZmScheduler::Timer timers[10];

  s.start();
  ZuTime t = Zm::now();
  int i;

  for (i = 0; i < 10; i++) {
    int j = (i & 1) ? ((i>>1) + 6) : (5 - (i>>1));
    char *buf = message_(j);
    ZuTime out = t + ZuTime(((double)j) / 10.0);
    s.add(&timers[j - 1], out, ZmScheduler::Update,
      [buf, out](auto &&arm) {
	return arm([job = ZmRef(new Job(buf, out))](this const auto &self) {
	  log("operator()() this=", ZuBoxPtr(&self).hex());
	  (*job)();
	});
      });
    log("Hello World ", j);
  }

  for (i = 0; i < 5; i++) {
    int j = (i & 1) ? ((i>>1) + 6) : (5 - (i>>1));
    if (timers[j - 1]) log("Deleting ", j);
    log("Delete World ", j);
    if (s.del(&timers[j - 1]))
      log("Found and deleted ", j);
    Zm::sleep(ZuTime(.1));
  }

  Zm::sleep(ZuTime(.6));

  log("threads:");
  log(Ztc::threadCSV());
  s.stop();

  s.start();

  t = Zm::now();

  for (i = 0; i < 10; i++) {
    int j = (i & 1) ? ((i>>1) + 6) : (5 - (i>>1));
    char *buf = message_(j);
    ZuTime out = t + ZuTime(((double)j) / 10.0);
    s.add(&timers[j - 1], out, ZmScheduler::Update,
      [buf, out](auto &&arm) {
	return arm([job = ZmRef(new Job(buf, out))]() { (*job)(); });
      });
    log("Hello World ", j);
    if (j == 2) breakpoint(&timers[j - 1]);
  }

  for (i = 0; i < 5; i++) {
    int j = (i & 1) ? ((i>>1) + 6) : (5 - (i>>1));

    s.del(&timers[j - 1]);

    // fns[j - 1] = ZmFn<>();
    // jobs[j - 1] = 0;
  }

  for (i = 5; i < 10; i++) {
    int j = (i & 1) ? ((i>>1) + 6) : (5 - (i>>1));
    log("Delete World ", j);
    s.del(&timers[j - 1]);
    // timers[j - 1] = 0;
    // fns[j - 1] = ZmFn<>();
    // jobs[j - 1] = 0;
    Zm::sleep(ZuTime(.1));
  }

  Zm::sleep(ZuTime(.6));

  log("threads:");
  log(Ztc::threadCSV());
  s.stop();

  ZmBackoff o(.25, 5, 1.25, .25);

  s.start();

  ZmRef<Timer> r = new Timer(&s, o);

  r->retry();
  r->start(ZmTimeout::Fn{r.ptr(), ZmFnPtr<&Timer::retry>{}});

  Zm::sleep(ZuTime(8));

  r->stop();

  log("threads:");
  log(Ztc::threadCSV());
  s.stop();

  ZuCheck(queues.valid &&
    allQueues == nQueues && queues.count == allQueues);
}
