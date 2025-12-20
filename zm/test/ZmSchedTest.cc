//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// scheduler test program

#include <zlib/ZuLib.hh>

#include <stdlib.h>
#include <stdio.h>

#include <zlib/ZmFn.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmSpecific.hh>
#include <zlib/ZmBackoff.hh>
#include <zlib/ZmTimeout.hh>

struct TLS : public ZmObject {
  TLS() : m_ping(0) {
    printf("TLS(0) [%d]\n", ZmSelf()->sid());
  }
  ~TLS() {
    printf("~TLS(%u) [%d]\n", m_ping, ZmSelf()->sid());
  }
  void ping() { ++m_ping; }
  unsigned	m_ping;
};

class Job : public ZmPolymorph {
public:
  Job(const char *message, ZuTime timeout) :
    m_message{message}, m_timeout{timeout}
  {
    std::cout
      << "Job() this=" << ZuBoxPtr(this).hex()
      << " message=" << ZuBoxPtr(m_message).hex()
      << ' ' << message << '\n' << std::flush;
  }
  ~Job() {
    printf("~Job() %p ~%s [%d]\n",
	this, m_message, (int)ZmThreadContext::self()->sid());
    ::free((void *)m_message);
  }

  void *operator()() {
    ZmAssert(!(reinterpret_cast<uintptr_t>(this) & 8));
    ZmSpecific<TLS>::instance()->ping();
    printf("Job::() %p %s [%d]\n",
	this, m_message, (int)ZmThreadContext::self()->sid());
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

    printf("%d %ld\n", (int)now.sec(), (long)now.nsec());
  }
};

#include <signal.h>

void segv(int s)
{
  printf("%d/%d: SEGV\n", (int)Zm::getPID(), (int)Zm::getTID());
  fflush(stdout);
  while (-1);
}

#ifdef _MSC_VER
#pragma warning(disable:4996)
#endif

void usage()
{
  fputs(
    "Usage: ZmSchedTest [OPTION]...\n\n"
    "Options:\n"
    "  -n N\tset number of threads to N\n"
    "  -c ID=CPUSET\tset thread ID affinity to CPUSET (e.g. 1=2,4)\n"
    "  -i BITMAP\tset isolation (e.g. 1,3-4)\n"
    , stderr);
  Zm::exit(1);
}

void fail(const char *s) 
{
  printf("FAIL: %s\n", s);
}

#define test(t, x) \
  (((ZuCArray<32>() << t(x)) == x) ? void() : fail(#t " \"" x "\""))
#define test2(t, x, y) \
  (((ZuCArray<32>() << t(x)) == y) ? void() : \
   fail(#t " \"" x "\" != \"" y "\""))

void breakpoint(ZmScheduler::Timer *timer)
{
}

int main(int argc, char **argv)
{
  {
    test(ZmBitmap, "");
    test2(ZmBitmap, ",", "");
    test2(ZmBitmap, ",,", "");
    test(ZmBitmap, "0-");
    test(ZmBitmap, "0,3-");
    test(ZmBitmap, "3-");
    test(ZmBitmap, "3-5,7");
    test(ZmBitmap, "3-5,7,9-");
  }

  signal(SIGSEGV, segv);

  ZmSchedParams params = ZmSchedParams().id("sched");
  ZmBitmap isolation;

  for (int i = 1; i < argc; i++) {
    if (argv[i][0] != '-') usage();
    switch (argv[i][1]) {
      default:
	usage();
	break;
      case 'n':
	if (++i >= argc) usage();
	params.nThreads(ZuBox<unsigned>(argv[i]));
	break;
      case 'c': {
	if (++i >= argc) usage();
	unsigned o, n = strlen(argv[i]);
	for (o = 0; o < n; o++) if (argv[i][o] == '=') break;
	if (!o || o >= n - 1) usage();
	params.thread(ZuBox<unsigned>(ZuCSpan(argv[i], o)))
	  .cpuset(ZuCSpan(&argv[i][o + 1], n - o - 1));
      } break;
      case 'i':
	if (++i >= argc) usage();
	isolation = argv[i];
	break;
    }
  }

  {
    int tid = isolation.first();
    while (tid >= 0) {
      params.thread(tid).isolated(true);
      tid = isolation.next(tid);
    }
  }

  ZmScheduler s{ZuMv(params)};
  // ZmRef<Job> jobs[10];
  // ZmFn<> fns[10];
  ZmScheduler::Timer timers[10];

  s.start();
  ZuTime t = Zm::now();
  int i;

  for (i = 0; i < 10; i++) {
    int j = (i & 1) ? ((i>>1) + 6) : (5 - (i>>1));
    char *buf = static_cast<char *>(malloc(32));
    sprintf(buf, "Goodbye World %d", j);
    // jobs[j - 1] = new Job(buf, t + ZuTime(((double)j) / 10.0));
    // fns[j - 1] = ZmFn<>{jobs[j - 1].ptr(), ZmFnPtr<&Job::operator()>{}};
    // s.add(&timers[j - 1], fns[j - 1], jobs[j - 1]->timeout());
    ZuTime out = t + ZuTime(((double)j) / 10.0);
    s.add([
      job = ZmMkRef(new Job(buf, out))
    ](this const auto &self) {
      std::cout << "operator()() this=" << ZuBoxPtr(&self).hex() << '\n';
      (*job)();
    }, out, &timers[j - 1]);
    printf("Hello World %d\n", j);
  }

#if 0
  for (i = 5; i < 10; i++) {
    int j = (i & 1) ? ((i>>1) + 6) : (5 - (i>>1));
    if (timers[j - 1]) printf("Disabling %d\n", j);
    timers[j - 1].fn = ZmFn<>{};
    // fns[j - 1] = ZmFn<>();
    // jobs[j - 1] = 0;
  }
#endif

  for (i = 0; i < 5; i++) {
    int j = (i & 1) ? ((i>>1) + 6) : (5 - (i>>1));
    if (timers[j - 1]) printf("Deleting %d\n", j);
    printf("Delete World %d\n", j);
    if (s.del(&timers[j - 1]))
      printf("Found and deleted %d\n", j);
    // timers[j - 1] = 0;
    // fns[j - 1] = ZmFn<>();
    // jobs[j - 1] = 0;
    Zm::sleep(ZuTime(.1));
  }

  Zm::sleep(ZuTime(.6));

  puts("threads:");
  std::cout << ZmThread::csv() << '\n';

  s.stop();

  s.start();

  t = Zm::now();

  for (i = 0; i < 10; i++) {
    int j = (i & 1) ? ((i>>1) + 6) : (5 - (i>>1));
    char *buf = (char *)malloc(32);
    sprintf(buf, "Goodbye World %d", j);
    // jobs[j - 1] = new Job(buf, t + ZuTime(((double)j) / 10.0));
    // fns[j - 1] = ZmFn<>{jobs[j - 1].ptr(), ZmFnPtr<&Job::operator()>{}};
    ZuTime out = t + ZuTime(((double)j) / 10.0);
    s.add([job = ZmMkRef(new Job(buf, out))]() { (*job)(); },
	out, &timers[j - 1]);
    printf("Hello World %d\n", j);
    if (j == 2) breakpoint(&timers[j - 1]);
  }

  for (i = 0; i < 5; i++) {
    int j = (i & 1) ? ((i>>1) + 6) : (5 - (i>>1));

    timers[j - 1].fn = []{ };

    // fns[j - 1] = ZmFn<>();
    // jobs[j - 1] = 0;
  }

  for (i = 5; i < 10; i++) {
    int j = (i & 1) ? ((i>>1) + 6) : (5 - (i>>1));
    printf("Delete World %d\n", j);
    s.del(&timers[j - 1]);
    // timers[j - 1] = 0;
    // fns[j - 1] = ZmFn<>();
    // jobs[j - 1] = 0;
    Zm::sleep(ZuTime(.1));
  }

  Zm::sleep(ZuTime(.6));

  puts("threads:");
  std::cout << ZmThread::csv() << '\n';

  s.stop();

  ZmBackoff o(.25, 5, 1.25, .25);

  s.start();

  ZmRef<Timer> r = new Timer(&s, o);

  r->retry();
  r->start(ZmFn<>{r.ptr(), ZmFnPtr<&Timer::retry>{}});

  Zm::sleep(ZuTime(8));

  r->stop();

  puts("threads:");
  std::cout << ZmThread::csv() << '\n';

  s.stop();
}
